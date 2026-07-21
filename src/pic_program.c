// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

// Write/flash engine over ICSP. Built to DS39688D: Table 3-1 (bulk erase),
// Table 3-2 (row erase), Table 3-3 (program), §4.2 (verify). The source image
// is streamed from SD in CHUNK-sized reads since the 128 KB image does not fit
// in the ~89 KB heap. Image byte offset maps 1:1 to flash address; bytes past
// EOF (or a short image) are treated as 0xFF.
#include "pic_program.h"
#include "pic_dump.h" // pic_read_device_id
#include "pic_config.h"
#include <furi.h>
#include <string.h>
#include <stdlib.h>

#define TAG   "PicProg"
#define CHUNK 4096U // SD read granularity; multiple of PIC_WRITE_BLOCK

// EECON1 control via core instructions (DS39688D Tables 3-1..3-3, verified).
#define INST_BSF_WREN 0x84A6 // BSF EECON1, WREN
#define INST_BSF_FREE 0x88A6 // BSF EECON1, FREE
#define INST_BSF_WR   0x82A6 // BSF EECON1, WR
#define INST_BCF_WREN 0x94A6 // BCF EECON1, WREN

static bool
is_67j60 (uint16_t id)
{
    return (id & 0xFFE0u)
           == 0x1F20u; // DEVID2:DEVID1 high byte 0x1F, 0b001x_xxxx
}

// Program-mode entry can be marginal on long/imperfect ICSP wiring: a single
// enter intermittently fails to latch and DEVID reads back 0x0000 (the dump
// path was more reliable only because it incidentally enters twice). Retry the
// enter + DEVID read a few times. On success the part is LEFT in program/verify
// mode (ready for erase/program); on failure the bus is exited. r->device_id
// holds the last value read either way.
static bool
enter_and_identify (PicProgReq *r)
{
    for (int attempt = 0; attempt < 6; attempt++)
    {
        pic_icsp_enter(r->d);
        r->device_id = pic_read_device_id(r->d);
        if (is_67j60(r->device_id))
        {
            return true;
        }
        pic_icsp_exit(r->d);
        furi_delay_ms(2);
    }
    FURI_LOG_E(
        TAG, "DEVID 0x%04X not a 67J60 after retries; abort", r->device_id);
    return false;
}

bool
pic_prog_bulk_erase (PicIcsp *d)
{
    // DS39688D Table 3-1 / §3.1.1: write 0101h->3C0005h then 8080h->3C0004h,
    // then a NOP whose 4th PGC falling edge starts the self-timed erase (hold
    // P11). J-series bulk erase is armed ONLY by this 0180h holding-latch
    // pattern; it does NOT go through the EECON1 WREN/WR engine, so do NOT set
    // WREN here — doing so makes the special-write decode silently reject the
    // unlock.
    pic_icsp_set_tblptr(d, 0x3C0005UL);
    pic_icsp_tblwt(d, 0x0101);
    pic_icsp_set_tblptr(d, 0x3C0004UL);
    pic_icsp_tblwt(d, 0x8080);
    pic_icsp_erase_nop(d, PIC_P11_US);
    return true;
}

bool
pic_prog_row_erase_range (PicIcsp       *d,
                          uint32_t       start,
                          uint32_t       end,
                          ProgProgressCb cb,
                          void          *ctx,
                          volatile bool *cancel)
{
    // DS39688D Table 3-2: WREN once, then per 1024-byte row: FREE, WR, NOP held
    // high P10. TBLPTR may point at any byte in the row to be erased.
    uint32_t first = start & ~(uint32_t)(PIC_ERASE_ROW - 1);
    uint32_t last  = end & ~(uint32_t)(PIC_ERASE_ROW - 1);
    uint32_t nrows = (last - first) / PIC_ERASE_ROW + 1;

    pic_icsp_core_inst(d, INST_BSF_WREN);
    uint32_t done = 0;
    for (uint32_t row = first; row <= last; row += PIC_ERASE_ROW)
    {
        pic_icsp_set_tblptr(d, row);
        pic_icsp_core_inst(d, INST_BSF_FREE);
        pic_icsp_core_inst(d, INST_BSF_WR);
        pic_icsp_prog_nop(d, PIC_P10_US);
        done++;
        if (cb)
        {
            cb(ProgStageErase, done, nrows, ctx);
        }
        if (cancel && *cancel)
        {
            pic_icsp_core_inst(d, INST_BCF_WREN);
            return false;
        }
    }
    pic_icsp_core_inst(d, INST_BCF_WREN);
    return true;
}

// Program [start,end] from the .bin (Program/Verify mode already entered, erase
// already done). Streams the image, skips all-0xFF blocks. Caller guarantees
// the range is 64-byte aligned.
static bool
prog_write_file (PicProgReq *r)
{
    File *f = storage_file_alloc(r->storage);
    if (!storage_file_open(f, r->bin_path, FSAM_READ, FSOM_OPEN_EXISTING))
    {
        FURI_LOG_E(TAG, "open failed: %s", r->bin_path);
        storage_file_free(f);
        return false;
    }
    bool     ok  = storage_file_seek(f, r->start, true);
    uint8_t *buf = ok ? malloc(CHUNK) : NULL;
    if (!buf)
    {
        storage_file_close(f);
        storage_file_free(f);
        return false;
    }

    const uint32_t total = r->end - r->start + 1;
    pic_icsp_core_inst(r->d, INST_BSF_WREN);
    uint32_t processed = 0;
    while (processed < total)
    {
        uint32_t want = total - processed;
        if (want > CHUNK)
        {
            want = CHUNK;
        }
        memset(buf, 0xFF, want); // EOF / sparse image => 0xFF (unused = FFFFh)
        // Read the full chunk: storage_file_read may return short at FS
        // boundaries; the return value was previously ignored, which could
        // desync the file vs the address and leave real data unwritten. Loop
        // until `want` or EOF (rest stays 0xFF).
        for (uint32_t rd = 0; rd < want;)
        {
            uint16_t g = storage_file_read(f, buf + rd, (uint16_t)(want - rd));
            if (!g)
            {
                break; // EOF
            }
            rd += g;
        }

        for (uint32_t off = 0; off < want; off += PIC_WRITE_BLOCK)
        {
            const uint8_t *blk   = buf + off;
            bool           blank = true;
            for (uint32_t i = 0; i < PIC_WRITE_BLOCK; i++)
            {
                if (blk[i] != 0xFF)
                {
                    blank = false;
                    break;
                }
            }
            if (blank)
            {
                continue; // erased cells already read 0xFF
            }

            const uint32_t blk_addr = r->start + processed + off;
            const uint32_t words    = PIC_WRITE_BLOCK / 2; // 32
            // Program the block, READ IT BACK, and retry until it matches.
            // Sporadic single-block program failures (the first write after a
            // bulk erase, or a bit-bang glitch on long ICSP wiring) otherwise
            // leave a block blank/partial and only surface at the final verify.
            // Retrying in place makes the write self-healing — every block is
            // confirmed before we advance.
            bool     blk_ok = false;
            uint32_t bad_i  = 0;
            uint8_t  got_b = 0, rb0 = 0;
            for (int attempt = 0; attempt < 5 && !blk_ok; attempt++)
            {
                pic_icsp_set_tblptr(r->d, blk_addr);
                for (uint32_t w = 0; w < words - 1; w++)
                { // 31 loads, post-increment
                    uint16_t word = (uint16_t)blk[2 * w]
                                    | ((uint16_t)blk[2 * w + 1] << 8);
                    pic_icsp_tblwt_postinc(r->d, word);
                }
                uint16_t lastw = (uint16_t)blk[2 * (words - 1)]
                                 | ((uint16_t)blk[2 * (words - 1) + 1] << 8);
                pic_icsp_tblwt_start(
                    r->d, lastw); // load last word + start programming
                pic_icsp_prog_nop(r->d, PIC_P9_US);

                pic_icsp_set_tblptr(
                    r->d, blk_addr); // read back the 64 bytes just written
                blk_ok = true;
                for (uint32_t i = 0; i < PIC_WRITE_BLOCK; i++)
                {
                    uint8_t rb = pic_icsp_table_read(r->d);
                    if (i == 0)
                    {
                        rb0 = rb;
                    }
                    if (rb != blk[i])
                    {
                        blk_ok = false;
                        bad_i  = i;
                        got_b  = rb;
                        break;
                    }
                }
            }
            if (!blk_ok)
            {
                FURI_LOG_E(TAG,
                           "block @%06lX FAIL byte[%lu] want %02X got %02X | "
                           "rb0=%02X want0=%02X",
                           (unsigned long)blk_addr,
                           (unsigned long)bad_i,
                           blk[bad_i],
                           got_b,
                           rb0,
                           blk[0]);
                r->fail_addr = blk_addr;
                ok           = false;
                break;
            }
        }
        if (!ok)
        {
            break;
        }
        processed += want;
        if (r->cb)
        {
            r->cb(ProgStageWrite, processed, total, r->cb_ctx);
        }
        if (r->cancel && *r->cancel)
        {
            ok = false;
            break;
        }
    }

    pic_icsp_core_inst(r->d, INST_BCF_WREN);
    free(buf);
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

// Read [start,end] back and compare to the .bin (DS39688D §4.2). Sets fail_addr
// on the first mismatch. Program/Verify mode already entered.
static bool
prog_verify_file (PicProgReq *r)
{
    File *f = storage_file_alloc(r->storage);
    if (!storage_file_open(f, r->bin_path, FSAM_READ, FSOM_OPEN_EXISTING))
    {
        FURI_LOG_E(TAG, "open failed: %s", r->bin_path);
        storage_file_free(f);
        return false;
    }
    bool     ok  = storage_file_seek(f, r->start, true);
    uint8_t *buf = ok ? malloc(CHUNK) : NULL;
    if (!buf)
    {
        storage_file_close(f);
        storage_file_free(f);
        return false;
    }

    const uint32_t total = r->end - r->start + 1;
    pic_icsp_set_tblptr(r->d, r->start);
    uint32_t processed = 0;
    bool     match     = true;
    while (processed < total)
    {
        uint32_t want = total - processed;
        if (want > CHUNK)
        {
            want = CHUNK;
        }
        memset(buf, 0xFF, want);
        storage_file_read(f, buf, want);

        for (uint32_t i = 0; i < want; i++)
        {
            uint8_t chip = pic_icsp_table_read(r->d); // TBLRD*+ auto-increment
            if (chip != buf[i])
            {
                r->fail_addr = r->start + processed + i;
                match        = false;
                break;
            }
        }
        processed += want;
        if (r->cb)
        {
            r->cb(ProgStageVerify, processed, total, r->cb_ctx);
        }
        if (!match)
        {
            break;
        }
        if (r->cancel && *r->cancel)
        {
            ok = false;
            break;
        }
    }

    free(buf);
    storage_file_close(f);
    storage_file_free(f);
    return ok && match;
}

static bool
cancelled (const PicProgReq *r)
{
    return r->cancel && *r->cancel;
}

// Pre-flight the source image BEFORE erasing anything: confirm it opens, and
// (for ranges that cover CONFIG1H) detect an image that would ENABLE
// code-protect. A CP-enabling write silently bricks read-back, so we must catch
// it before the erase. Returns false if the file can't be opened; sets
// *cp_enable accordingly.
static bool
source_preflight (PicProgReq *r, bool *cp_enable)
{
    *cp_enable = false;
    File *f    = storage_file_alloc(r->storage);
    bool  opened
        = storage_file_open(f, r->bin_path, FSAM_READ, FSOM_OPEN_EXISTING);
    if (opened)
    {
        if (r->start <= PIC_CONFIG1H_ADDR && r->end >= PIC_CONFIG1H_ADDR)
        {
            uint8_t b
                = 0xFF; // EOF / short image => unprogrammed => CP off (safe)
            if (storage_file_seek(f, PIC_CONFIG1H_ADDR, true))
            {
                storage_file_read(f, &b, 1);
            }
            *cp_enable
                = (b & PIC_CP0_MASK) == 0; // CP0 cleared => code-protected
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    return opened;
}

bool
pic_prog_run (PicProgReq *r)
{
    if (!r || !r->d || !r->storage || !r->bin_path || r->end < r->start)
    {
        return false;
    }
    if ((r->start % PIC_WRITE_BLOCK)
        || ((r->end - r->start + 1) % PIC_WRITE_BLOCK))
    {
        FURI_LOG_E(
            TAG, "range %06lX-%06lX not 64-byte aligned", r->start, r->end);
        return false;
    }
    r->fail_addr  = PIC_PROG_NO_FAIL;
    r->cp_refused = false;

    if (!enter_and_identify(r))
    {
        pic_icsp_exit(r->d);
        return false;
    }

    // Validate the source before touching the chip: never erase if we can't
    // read the image, and never enable code-protect (DS39688D Table 5-2 /
    // §8.3).
    bool cp_enable = false;
    if (!source_preflight(r, &cp_enable))
    {
        FURI_LOG_E(
            TAG, "source unreadable: %s; abort before erase", r->bin_path);
        pic_icsp_exit(r->d);
        return false;
    }
    if (cp_enable)
    {
        FURI_LOG_E(TAG,
                   "image enables code-protect (CONFIG1H CP0=0); refusing");
        r->cp_refused = true;
        pic_icsp_exit(r->d);
        return false;
    }

    bool ok;
    if (r->keep_boot)
    {
        ok = pic_prog_row_erase_range(
            r->d, r->start, r->end, r->cb, r->cb_ctx, r->cancel);
    }
    else
    {
        // Bulk erase, then verify IN-SESSION (no reset — see the loop), retry
        // x4 on marginal ICSP.
        ok = false;
        for (int e = 0; e < 4 && !ok; e++)
        {
            if (r->cb)
            {
                r->cb(ProgStageErase, e, 4, r->cb_ctx);
            }
            pic_prog_bulk_erase(r->d);
            // Read back IN-SESSION, NO reset: DS39688D §4.2 — a device reset
            // after CP is cleared leaves the part unreadable; the J-series
            // evaluates protection live from the (volatile) config, so a good
            // erase reads back immediately. Success = block 0 erased to 0xFF
            // AND CONFIG1H CP0=1 (bit2 set => NOT protected). An erased
            // CONFIG1H reads 0x04, not 0xFF.
            pic_icsp_set_tblptr(r->d, 0x01FFF9UL);
            uint8_t cfg1h = pic_icsp_table_read(r->d);
            uint8_t b0    = 0xFF;
            ok = (cfg1h & 0x04u) != 0; // CP0 bit set => code-protect OFF
            if (ok)
            {
                pic_icsp_set_tblptr(r->d, r->start);
                for (uint32_t i = 0; i < PIC_WRITE_BLOCK; i++)
                {
                    uint8_t rb = pic_icsp_table_read(r->d);
                    if (i == 0)
                    {
                        b0 = rb;
                    }
                    if (rb != 0xFF)
                    {
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok)
            {
                FURI_LOG_W(
                    TAG,
                    "erase attempt %d not clear: CONFIG1H=%02X mem[0]=%02X",
                    e + 1,
                    cfg1h,
                    b0);
            }
        }
        if (r->cb)
        {
            r->cb(ProgStageErase, 4, 4, r->cb_ctx);
        }
        if (!ok)
        {
            FURI_LOG_E(TAG, "bulk erase failed/blocked after retries; abort");
            pic_icsp_exit(r->d);
            return false;
        }
    }
    if (ok && !cancelled(r))
    {
        ok = prog_write_file(r);
    }

    bool verified = false;
    if (ok && !cancelled(r))
    {
        verified = prog_verify_file(r);
    }

    pic_icsp_exit(r->d);
    return verified;
}

bool
pic_prog_verify (PicProgReq *r)
{
    if (!r || !r->d || !r->storage || !r->bin_path || r->end < r->start)
    {
        return false;
    }
    r->fail_addr = PIC_PROG_NO_FAIL;

    if (!enter_and_identify(r))
    {
        pic_icsp_exit(r->d);
        return false;
    }
    bool ok = prog_verify_file(r);
    pic_icsp_exit(r->d);
    return ok;
}
