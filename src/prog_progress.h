// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

// Pure mapping of a write/verify engine progress event onto the shared view
// model (AppState). Split out of pic_jobs.c's progress callback so it can be
// unit-tested on the host without the worker/ICSP/storage stack. The device ID
// is passed in (read during ICSP entry, before the first progress tick) so the
// StWriting screen shows the real DEVID from the first update instead of 0000.
#pragma once
#include "app_types.h"
#include "pic_program.h" // ProgStage
#include <stdint.h>
#include <string.h>

static inline void
prog_apply_progress (AppState *st, ProgStage stage, uint32_t done,
                     uint32_t total, uint16_t device_id)
{
    const char *name = (stage == ProgStageErase)   ? "Erasing"
                       : (stage == ProgStageWrite) ? "Writing"
                                                   : "Verifying";
    strncpy(st->stage, name, sizeof(st->stage) - 1);
    st->stage[sizeof(st->stage) - 1] = '\0';
    st->device_id                    = device_id;
    st->bytes_done                   = done;
    st->bytes_total                  = total;
    st->phase                        = StWriting;
}
