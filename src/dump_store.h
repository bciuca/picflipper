// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

#pragma once
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <stdint.h>
#include <stdbool.h>

// Storage layout + file helpers for PIC dumps, kept under
// /ext/apps_data/picflipper/dumps.

// Build a collision-free <prefix>_<devid>_<ts>[.N].{bin,hex} path pair in the
// dump dir (created if needed). fname_out receives the chosen base name.
void dump_store_make_paths(Storage    *storage,
                           const char *prefix,
                           uint16_t    devid,
                           char       *fname_out,
                           size_t      fname_sz,
                           char       *binpath,
                           size_t      bin_sz,
                           char       *hexpath,
                           size_t      hex_sz);

// Stream a file through SHA-256; write the 64-char lowercase hex digest (+NUL)
// into out_hex[65]. Returns false on open/read failure.
bool dump_store_hash_sha256(Storage    *storage,
                            const char *path,
                            char        out_hex[65]);

// Modal .bin picker rooted at the dump dir. Fills out_path and returns true on
// selection; false if the user backed out.
bool dump_store_pick_bin(DialogsApp *dialogs, char *out_path, size_t out_sz);
