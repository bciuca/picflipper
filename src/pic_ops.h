// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#pragma once
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <stdint.h>
#include <stdbool.h>

// PIC operations subsystem: the dump/progress view plus every chip op (dump
// flash/RAM, write/restore, verify) and the shared chip-detection gate. All
// operation state is private to PicOps.
typedef struct PicOps PicOps;

// `view_id` is the id the caller registers get_view() under with the
// dispatcher; the module switches to it when an op starts.
PicOps *pic_ops_alloc(Storage *storage, DialogsApp *dialogs,
                      ViewDispatcher *view_dispatcher, uint32_t view_id);
View   *pic_ops_get_view(PicOps *ops);
void    pic_ops_free(PicOps *ops);

// Menu entry points. Each gates on chip detection first; write/verify prompt
// for a .bin and no-op if the user backs out or an op is already running.
void pic_ops_start_dump_flash(PicOps *ops);
void pic_ops_start_dump_ram(PicOps *ops);
void pic_ops_start_write(PicOps *ops, bool keep_boot);
void pic_ops_start_verify(PicOps *ops);
