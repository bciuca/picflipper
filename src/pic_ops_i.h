// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

// Internal shared state for the PIC operations subsystem, split between
// pic_ops.c (view controller: rendering, input, detection gate, timer,
// lifecycle) and pic_jobs.c (the ICSP dump/RAM/write worker-thread bodies).
// Not part of the public API — hosts include pic_ops.h only.
#pragma once
#include "pic_ops.h"
#include <furi.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <stdint.h>
#include <stdbool.h>

#define TAG "picflipper"
#define PIC_CODE_START 0x000000UL
#define PIC_CODE_END 0x01FFFFUL // inclusive; 128 KB
#define PIC_RAM_START 0x0000UL
#define PIC_RAM_END 0x0F5FUL // GPR file-register space (below SFRs @0xF60)
#define DUMP_CHUNK 4096

typedef enum
{
    JobCode,
    JobRam
} DumpJob;

// The operation a session is running. Every op gates on chip detection first
// (StSearching); on detect the search worker routes to the op's next state.
typedef enum
{
    OpDumpFlash,
    OpDumpRam,
    OpWriteFull,
    OpWriteApp,
    OpVerify,
} AppOp;

struct PicOps
{
    View            *dump_view; // model = AppState
    Storage         *storage;
    DialogsApp      *dialogs;
    ViewDispatcher  *view_dispatcher; // switched to dump_view on start
    uint32_t         view_id;
    FuriThread      *worker;
    volatile bool    worker_active;
    volatile bool    cancel;
    volatile uint8_t job; // DumpJob: JobCode (flash) or JobRam (live SRAM)
    volatile uint8_t op;  // AppOp: which operation this session runs
    char             write_path[128];
    char             confirm_sha[65];
    bool             keep_boot;
    bool             verify_only;
    FuriTimer       *anim_timer;
    uint32_t         hold_start;
    volatile bool    holding;
};

// pic_jobs.c: launch the dump/RAM or the write/verify worker thread. The
// controller calls these once the detection gate has confirmed a chip.
void dump_start(PicOps *ops);
void prog_start(PicOps *ops);
