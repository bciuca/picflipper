// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

// ICSP worker-thread bodies for the PIC operations subsystem: dump flash, dump
// live RAM, and write/restore/verify. Each runs on a FuriThread, drives the
// chip over ICSP, and reports progress into the shared PicOps dump view.
// Controller glue (view, input, detection gate, timer) lives in pic_ops.c.
#include "pic_ops_i.h"
#include <furi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_types.h"
#include "pic_config.h"
#include "pic_icsp.h"
#include "pic_dump.h"
#include "pic_program.h"
#include "dump_writer.h"
#include "dump_store.h"
#include "prog_progress.h"

// --- dump view model helpers (model type is AppState) ---
static void
dump_set (PicOps *ops, AppPhase phase, const char *msg)
{
    AppState *st = view_get_model(ops->dump_view);
    st->phase    = phase;
    if (msg)
    {
        strncpy(st->msg, msg, sizeof(st->msg) - 1);
        st->msg[sizeof(st->msg) - 1] = '\0';
    }
    view_commit_model(ops->dump_view, true);
}

static int32_t
pic_dump_worker (void *ctx)
{
    PicOps *ops = ctx;
    PicIcsp d
        = { .pgc = PIC_PIN_PGC, .pgd = PIC_PIN_PGD, .mclr = PIC_PIN_MCLR };

    dump_set(ops, StConnecting, "Entering ICSP...");
    uint8_t *chunk = malloc(DUMP_CHUNK);
    if (!chunk)
    {
        dump_set(ops, StError, "Out of memory");
        ops->worker_active = false;
        return 0;
    }

    pic_icsp_init(&d);
    pic_icsp_enter(&d);
    uint16_t devid    = pic_read_device_id(&d);
    bool     id_match = (devid & 0xFFE0) == 0x1F20;
    FURI_LOG_I(TAG,
               "Device ID = 0x%04X (%s)",
               (unsigned)devid,
               id_match            ? "PIC18F67J60 OK"
               : (devid == 0xFFFF) ? "no device / bus floating"
               : (devid == 0x0000) ? "no device / bus low"
                                   : "unexpected part");

    if (devid == 0xFFFF || devid == 0x0000)
    {
        pic_icsp_exit(&d);
        free(chunk);
        FURI_LOG_W(TAG,
                   "Abort: check MCLR/PGC/PGD/GND + common ground; try "
                   "swapping PGC/PGD");
        dump_set(ops, StError, "No device - check wiring/GND");
        ops->worker_active = false;
        return 0;
    }

    const uint32_t total = PIC_CODE_END - PIC_CODE_START + 1;
    {
        AppState *st    = view_get_model(ops->dump_view);
        st->device_id   = devid;
        st->bytes_total = total;
        st->bytes_done  = 0;
        st->phase       = StDumping;
        view_commit_model(ops->dump_view, true);
    }
    FURI_LOG_I(TAG, "Dumping %lu bytes", (unsigned long)total);

    char fname[48];
    char binpath[128];
    char hexpath[128];
    dump_store_make_paths(ops->storage,
                          "pic",
                          devid,
                          fname,
                          sizeof(fname),
                          binpath,
                          sizeof(binpath),
                          hexpath,
                          sizeof(hexpath));

    DumpWriter w;
    bool       ok
        = dump_writer_open(&w, ops->storage, binpath, hexpath, PIC_CODE_START);
    uint8_t or_all = 0;
    if (ok)
    {
        pic_icsp_set_tblptr(&d, PIC_CODE_START);
        uint32_t filled = 0;
        for (uint32_t i = 0; i < total; i++)
        {
            uint8_t b = pic_icsp_table_read(&d);
            or_all |= b;
            chunk[filled++] = b;
            if (filled == DUMP_CHUNK)
            {
                if (!dump_writer_append(&w, chunk, filled))
                {
                    ok = false;
                    break;
                }
                filled         = 0;
                AppState *st   = view_get_model(ops->dump_view);
                st->bytes_done = i + 1;
                view_commit_model(ops->dump_view, true);
                if (ops->cancel)
                {
                    break;
                }
            }
        }
        if (ok && !ops->cancel && filled > 0)
        {
            if (!dump_writer_append(&w, chunk, filled))
            {
                ok = false;
            }
        }
    }

    pic_icsp_exit(&d);
    bool close_ok = dump_writer_close(&w);
    ok            = ok && close_ok;
    free(chunk);

    if (ops->cancel)
    {
        FURI_LOG_I(TAG, "Dump cancelled");
        dump_set(ops, StIdle, "Cancelled");
    }
    else if (!ok)
    {
        FURI_LOG_E(TAG, "SD write failed: %s", binpath);
        dump_set(ops, StError, "SD write failed");
    }
    else
    {
        FURI_LOG_I(TAG,
                   "Dump complete: %s%s",
                   binpath,
                   or_all == 0 ? " [all 0x00]" : "");
        AppState *st   = view_get_model(ops->dump_view);
        st->bytes_done = total;
        st->phase      = StDone;
        if (or_all == 0)
        {
            snprintf(
                st->msg, sizeof(st->msg), "Saved (all 0x00: code-protect?)");
        }
        else
        {
            snprintf(st->msg, sizeof(st->msg), "%s.bin", fname);
        }
        view_commit_model(ops->dump_view, true);
    }

    ops->worker_active = false;
    return 0;
}

// Live SRAM snapshot. ICSP Program/Verify entry resets the CPU via MCLR but
// leaves the GPR file registers unchanged (only POR clears them), so reading
// data memory here returns the firmware's pre-entry running state. One-shot:
// trigger an action on the target, then run this to capture the RAM (e.g. the
// freshly-built radio TX buffer) before it's reused.
static int32_t
pic_ram_worker (void *ctx)
{
    PicOps *ops = ctx;
    PicIcsp d
        = { .pgc = PIC_PIN_PGC, .pgd = PIC_PIN_PGD, .mclr = PIC_PIN_MCLR };
    const uint16_t start = PIC_RAM_START, end = PIC_RAM_END;
    const uint32_t total = (uint32_t)(end - start + 1);

    dump_set(ops, StConnecting, "Snapshot: entering ICSP...");
    uint8_t *buf = malloc(total);
    if (!buf)
    {
        dump_set(ops, StError, "Out of memory");
        ops->worker_active = false;
        return 0;
    }

    pic_icsp_init(&d);
    pic_icsp_enter(&d);
    uint16_t devid    = pic_read_device_id(&d);
    bool     id_match = (devid & 0xFFE0) == 0x1F20;
    FURI_LOG_I(TAG,
               "RAM snapshot DEVID=0x%04X (%s)",
               (unsigned)devid,
               id_match ? "PIC18F67J60 OK" : "?");
    if (devid == 0xFFFF || devid == 0x0000)
    {
        pic_icsp_exit(&d);
        free(buf);
        dump_set(ops, StError, "No device - check wiring/GND");
        ops->worker_active = false;
        return 0;
    }
    {
        AppState *st    = view_get_model(ops->dump_view);
        st->device_id   = devid;
        st->bytes_total = total;
        st->bytes_done  = 0;
        st->phase       = StDumping;
        view_commit_model(ops->dump_view, true);
    }

    for (uint32_t i = 0; i < total; i++)
    {
        buf[i] = pic_icsp_read_data(&d, (uint16_t)(start + i));
        if ((i & 0xFF) == 0xFF)
        {
            AppState *st   = view_get_model(ops->dump_view);
            st->bytes_done = i + 1;
            view_commit_model(ops->dump_view, true);
            if (ops->cancel)
            {
                break;
            }
        }
    }
    pic_icsp_exit(&d);

    bool ok        = !ops->cancel;
    char fname[48] = { 0 };
    if (ok)
    {
        char binpath[128], hexpath[128];
        dump_store_make_paths(ops->storage,
                              "picram",
                              devid,
                              fname,
                              sizeof(fname),
                              binpath,
                              sizeof(binpath),
                              hexpath,
                              sizeof(hexpath));
        ok = dump_write_bin(ops->storage, binpath, buf, total);
        ok = dump_write_ihex(ops->storage, hexpath, start, buf, total) && ok;
        if (ok)
        {
            FURI_LOG_I(TAG, "RAM snapshot saved: %s", binpath);
        }
    }
    free(buf);

    if (ops->cancel)
    {
        dump_set(ops, StIdle, "Cancelled");
    }
    else if (!ok)
    {
        dump_set(ops, StError, "SD write failed");
    }
    else
    {
        AppState *st   = view_get_model(ops->dump_view);
        st->bytes_done = total;
        st->phase      = StDone;
        snprintf(st->msg, sizeof(st->msg), "%s.bin", fname);
        view_commit_model(ops->dump_view, true);
    }
    ops->worker_active = false;
    return 0;
}

void
dump_start (PicOps *ops)
{
    if (ops->worker_active)
    {
        return;
    }
    if (ops->worker)
    {
        furi_thread_join(ops->worker);
        furi_thread_free(ops->worker);
        ops->worker = NULL;
    }
    ops->cancel        = false;
    ops->worker_active = true;
    FuriThreadCallback fn
        = (ops->job == JobRam) ? pic_ram_worker : pic_dump_worker;
    ops->worker = furi_thread_alloc_ex("PicDumpWorker", 4096, fn, ops);
    furi_thread_start(ops->worker);
}
// --- write / flash ---------------------------------------------------------

// Bundles the worker's ops with the live request so the progress callback can
// surface the device ID: it is read during ICSP entry (enter_and_identify),
// before the first progress tick, so the StWriting screen shows the real DEVID
// instead of 0000 for the whole operation.
typedef struct
{
    PicOps           *ops;
    const PicProgReq *req;
} ProgCbCtx;

// Runs on the worker thread; maps an engine progress event onto the dump view.
static void
prog_progress_cb (ProgStage stage, uint32_t done, uint32_t total, void *ctx)
{
    ProgCbCtx *c  = ctx;
    AppState  *st = view_get_model(c->ops->dump_view);
    prog_apply_progress(st, stage, done, total, c->req->device_id);
    view_commit_model(c->ops->dump_view, true);
}

static int32_t
pic_prog_worker (void *ctx)
{
    PicOps *ops = ctx;
    PicIcsp d
        = { .pgc = PIC_PIN_PGC, .pgd = PIC_PIN_PGD, .mclr = PIC_PIN_MCLR };
    pic_icsp_init(&d);

    dump_set(
        ops, StConnecting, ops->verify_only ? "Verifying..." : "Erasing...");

    // Verify-only compares over the file's own span; write covers the chip
    // range (full chip, or the ops range when keeping the bootloader).
    uint32_t end;
    if (ops->verify_only)
    {
        FileInfo fi;
        if (storage_common_stat(ops->storage, ops->write_path, &fi) == FSE_OK
            && fi.size > 0)
        {
            end = (fi.size > (uint64_t)(PIC_CODE_END + 1))
                      ? PIC_CODE_END
                      : (uint32_t)(fi.size - 1);
        }
        else
        {
            end = PIC_CODE_END;
        }
    }
    else
    {
        end = ops->keep_boot ? (uint32_t)(PIC_BOOT_REGION_START - 1)
                             : PIC_CODE_END;
    }

    PicProgReq req = {
        .d         = &d,
        .storage   = ops->storage,
        .bin_path  = ops->write_path,
        .start     = PIC_CODE_START,
        .end       = end,
        .keep_boot = ops->keep_boot,
        .cb        = prog_progress_cb,
        .cb_ctx    = NULL, // set below: the callback ctx needs &req
        .cancel    = &ops->cancel,
    };
    ProgCbCtx cbctx = { .ops = ops, .req = &req };
    req.cb_ctx      = &cbctx;

    bool ok    = ops->verify_only ? pic_prog_verify(&req) : pic_prog_run(&req);
    bool id_ok = (req.device_id & 0xFFE0) == 0x1F20;

    AppState *st  = view_get_model(ops->dump_view);
    st->device_id = req.device_id;
    // Mark this as a write/verify result so StDone shows "Done" not "Saved".
    strncpy(st->stage,
            ops->verify_only ? "Verify" : "Write",
            sizeof(st->stage) - 1);
    st->stage[sizeof(st->stage) - 1] = '\0';
    if (ops->cancel)
    {
        st->phase = StError;
        snprintf(st->msg, sizeof(st->msg), "Cancelled (chip may be partial)");
    }
    else if (!id_ok)
    {
        st->phase = StError;
        if (req.device_id == 0xFFFF || req.device_id == 0x0000)
        {
            snprintf(st->msg, sizeof(st->msg), "No device - check wiring");
        }
        else
        {
            snprintf(st->msg,
                     sizeof(st->msg),
                     "Wrong chip %04X",
                     (unsigned)req.device_id);
        }
    }
    else if (req.cp_refused)
    {
        // Refused before erasing: the image would enable code-protect (chip
        // untouched).
        st->phase = StError;
        snprintf(st->msg, sizeof(st->msg), "Img enables code-protect!");
    }
    else if (!ok)
    {
        st->phase = StError;
        if (req.fail_addr != PIC_PROG_NO_FAIL)
        {
            snprintf(st->msg,
                     sizeof(st->msg),
                     "Verify FAIL @ %06lX",
                     (unsigned long)req.fail_addr);
        }
        else
        {
            snprintf(st->msg,
                     sizeof(st->msg),
                     "%s",
                     ops->verify_only ? "Read/SD error" : "Write/SD error");
        }
    }
    else
    {
        st->phase = StDone;
        snprintf(st->msg,
                 sizeof(st->msg),
                 "%s",
                 ops->verify_only ? "Verified OK" : "Flashed + verified OK");
    }
    view_commit_model(ops->dump_view, true);

    FURI_LOG_I(TAG,
               "%s result: %s (DEVID %04X)",
               ops->verify_only ? "Verify" : "Write",
               ok ? "OK" : "FAIL",
               (unsigned)req.device_id);

    ops->worker_active = false;
    return 0;
}

void
prog_start (PicOps *ops)
{
    if (ops->worker_active)
    {
        return;
    }
    if (ops->worker)
    {
        furi_thread_join(ops->worker);
        furi_thread_free(ops->worker);
        ops->worker = NULL;
    }
    ops->cancel        = false;
    ops->worker_active = true;
    ops->worker
        = furi_thread_alloc_ex("PicProgWorker", 4096, pic_prog_worker, ops);
    furi_thread_start(ops->worker);
}
