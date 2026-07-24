// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

// Write/flash engine: bulk/row erase + 64-byte block program + verify, all over
// ICSP, built to DS39688D §3 (erase/program) and §4.2 (verify). The image is
// streamed from an SD .bin (the 128 KB image does not fit in the ~89 KB heap;
// see dump_writer.h), so the source is a file path, not an in-RAM buffer.
#pragma once
#include "pic_icsp.h"
#include <storage/storage.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    ProgStageErase,
    ProgStageWrite,
    ProgStageVerify
} ProgStage;
typedef void (*ProgProgressCb)(ProgStage stage,
                               uint32_t  done,
                               uint32_t  total,
                               void     *ctx);

// fail_addr value when the failure was NOT a verify mismatch (no-device,
// SD/erase error, user abort) — distinguishes those from a real "Verify FAIL @
// addr".
#define PIC_PROG_NO_FAIL 0xFFFFFFFFUL

typedef struct
{
    PicIcsp    *d;
    Storage    *storage;
    const char *bin_path; // source image on SD; byte offset == flash address
    uint32_t    start;    // inclusive byte address; 64-byte aligned
    uint32_t    end;      // inclusive byte address; (end-start+1) % 64 == 0
    bool keep_boot; // true => row-erase [start,end]; false => bulk-erase whole
                    // chip
    ProgProgressCb cb; // nullable
    void          *cb_ctx;
    volatile bool *cancel;    // nullable; polled between blocks/rows/chunks
    uint16_t       device_id; // out: DEVID read during the probe
    uint32_t
         fail_addr; // out: first verify mismatch (valid only when verify fails)
    bool cp_refused; // out: aborted (before erase) because the image would
                     // enable code-protect
} PicProgReq;

// Probe -> erase -> program -> verify, inside one enter()/exit() bracket.
// Returns true only if the read-back verify matched. On false, inspect *cancel
// and fail_addr to tell apart user-abort vs verify failure vs no-device.
bool pic_prog_run(PicProgReq *r);

// Probe -> read-back compare of bin_path over [start,end]. No erase, no write.
bool pic_prog_verify(PicProgReq *r);

// Building blocks (Program/Verify mode must already be entered). Useful for
// bring-up/blank-check. Both manage WREN as the datasheet requires.
bool pic_prog_bulk_erase(PicIcsp *d); // Table 3-1 (also clears CP)
bool pic_prog_row_erase_range(        // Table 3-2
    PicIcsp       *d,
    uint32_t       start,
    uint32_t       end,
    ProgProgressCb cb,
    void          *ctx,
    volatile bool *cancel);
