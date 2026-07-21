// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

#pragma once
#include <storage/storage.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// --- Single-shot writers: buffer the whole image and write it in one call.
// For dumps small enough to hold in heap (the ~4 KB RAM snapshot). Thin
// wrappers over the streaming writer below, so there is one Intel-HEX impl.
bool dump_write_bin(Storage       *s,
                    const char    *path,
                    const uint8_t *buf,
                    size_t         len);
bool dump_write_ihex(Storage       *s,
                     const char    *path,
                     uint32_t       base,
                     const uint8_t *buf,
                     size_t         len); // Intel HEX, type-04 ext addr

// --- Streaming writer -------------------------------------------------------
// The PIC18F67J60 flash image is 128 KB, but this Flipper's largest free heap
// block is ~89 KB, so the dump cannot be buffered whole. Appends
// arbitrary-sized chunks to a .bin and/or .hex as bytes are read. Pass NULL for
// either path to skip that file.
typedef struct
{
    File    *bin;       // nullable
    File    *hex;       // nullable
    uint32_t base;      // absolute base address for hex records
    uint32_t pos;       // bytes fed to the hex stream so far
    uint16_t hex_upper; // last emitted type-04 upper address word
    bool     hex_started;
    uint8_t  rec[16]; // pending (<16) hex record bytes across chunk boundaries
    uint8_t  rec_len;
    bool     ok;
} DumpWriter;

bool dump_writer_open(DumpWriter *w,
                      Storage    *s,
                      const char *bin_path,
                      const char *hex_path,
                      uint32_t    base);
bool dump_writer_append(DumpWriter *w, const uint8_t *data, size_t len);
bool dump_writer_close(
    DumpWriter *w); // finalizes hex EOF, closes files, returns ok
