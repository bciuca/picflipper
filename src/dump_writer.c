// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

// Output writers: raw .bin and Intel HEX, single-shot and streaming. No GPIO.
#include "dump_writer.h"
#include <furi.h>
#include <storage/storage.h>
#include <string.h>
#include <stdio.h>

// Create the parent directory of `path` (best effort; ignores "already
// exists").
static void
ensure_parent_dir (Storage *s, const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path)
    {
        return;
    }
    size_t n = (size_t)(slash - path);
    char   dir[128];
    if (n >= sizeof(dir))
    {
        return;
    }
    memcpy(dir, path, n);
    dir[n] = '\0';
    storage_common_mkdir(s, dir); // FSE_EXIST is fine
}

// Emit one Intel HEX record: `count` data bytes at 16-bit `addr`, record
// `type`.
static bool
ihex_record (
    File *f, uint8_t count, uint16_t addr, uint8_t type, const uint8_t *data)
{
    char    line[64];
    int     pos = snprintf(line,
                       sizeof(line),
                       ":%02X%04X%02X",
                       (unsigned)count,
                       (unsigned)addr,
                       (unsigned)type);
    uint8_t sum = (uint8_t)(count + (addr >> 8) + (addr & 0xFF) + type);
    for (uint8_t i = 0; i < count; i++)
    {
        pos += snprintf(
            line + pos, sizeof(line) - (size_t)pos, "%02X", (unsigned)data[i]);
        sum = (uint8_t)(sum + data[i]);
    }
    uint8_t crc = (uint8_t)(~sum + 1); // two's-complement checksum
    pos += snprintf(
        line + pos, sizeof(line) - (size_t)pos, "%02X\r\n", (unsigned)crc);
    return storage_file_write(f, line, (size_t)pos) == (size_t)pos;
}

// Write a full/partial data record at an absolute start address, emitting a
// type-04 extended-linear-address record first if the upper 16 bits changed.
static void
hex_flush_record (DumpWriter    *w,
                  uint32_t       start_addr,
                  const uint8_t *data,
                  uint8_t        count)
{
    if (!w->ok || !w->hex || count == 0)
    {
        return;
    }
    uint16_t upper = (uint16_t)(start_addr >> 16);
    if (!w->hex_started || upper != w->hex_upper)
    {
        uint8_t ula[2] = { (uint8_t)(upper >> 8), (uint8_t)(upper & 0xFF) };
        w->ok          = ihex_record(w->hex, 2, 0x0000, 0x04, ula);
        w->hex_upper   = upper;
        w->hex_started = true;
        if (!w->ok)
        {
            return;
        }
    }
    w->ok = ihex_record(
        w->hex, count, (uint16_t)(start_addr & 0xFFFF), 0x00, data);
}

bool
dump_writer_open (DumpWriter *w,
                  Storage    *s,
                  const char *bin_path,
                  const char *hex_path,
                  uint32_t    base)
{
    memset(w, 0, sizeof(*w));
    w->base = base;
    w->ok   = true;
    if (bin_path)
    {
        ensure_parent_dir(s, bin_path);
        w->bin = storage_file_alloc(s);
        if (!storage_file_open(
                w->bin, bin_path, FSAM_WRITE, FSOM_CREATE_ALWAYS))
        {
            w->ok = false;
        }
    }
    if (hex_path)
    {
        ensure_parent_dir(s, hex_path);
        w->hex = storage_file_alloc(s);
        if (!storage_file_open(
                w->hex, hex_path, FSAM_WRITE, FSOM_CREATE_ALWAYS))
        {
            w->ok = false;
        }
    }
    return w->ok;
}

bool
dump_writer_append (DumpWriter *w, const uint8_t *data, size_t len)
{
    if (!w->ok)
    {
        return false;
    }
    if (w->bin && storage_file_write(w->bin, data, len) != len)
    {
        w->ok = false;
        return false;
    }
    if (w->hex)
    {
        for (size_t i = 0; i < len && w->ok; i++)
        {
            w->rec[w->rec_len++] = data[i];
            w->pos++;
            if (w->rec_len == 16)
            {
                hex_flush_record(w, w->base + (w->pos - 16), w->rec, 16);
                w->rec_len = 0;
            }
        }
    }
    return w->ok;
}

bool
dump_writer_close (DumpWriter *w)
{
    if (w->hex)
    {
        if (w->ok && w->rec_len > 0)
        {
            hex_flush_record(
                w, w->base + (w->pos - w->rec_len), w->rec, w->rec_len);
            w->rec_len = 0;
        }
        if (w->ok)
        {
            w->ok = ihex_record(w->hex, 0, 0x0000, 0x01, NULL); // EOF
        }
        storage_file_close(w->hex);
        storage_file_free(w->hex);
        w->hex = NULL;
    }
    if (w->bin)
    {
        storage_file_close(w->bin);
        storage_file_free(w->bin);
        w->bin = NULL;
    }
    return w->ok;
}

bool
dump_write_bin (Storage *s, const char *path, const uint8_t *buf, size_t len)
{
    DumpWriter w;
    bool       ok = dump_writer_open(&w, s, path, NULL, 0);
    if (ok)
    {
        ok = dump_writer_append(&w, buf, len);
    }
    return dump_writer_close(&w) && ok;
}

bool
dump_write_ihex (
    Storage *s, const char *path, uint32_t base, const uint8_t *buf, size_t len)
{
    DumpWriter w;
    bool       ok = dump_writer_open(&w, s, NULL, path, base);
    if (ok)
    {
        ok = dump_writer_append(&w, buf, len);
    }
    return dump_writer_close(&w) && ok;
}
