// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

// Where PIC dumps live on the SD card, and the helpers to name, pick, and hash
// them. Operates on Storage/DialogsApp directly — no app state.
#include "dump_store.h"
#include <furi.h>
#include <furi_hal_rtc.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sha256.h"

#define TAG               "picflipper"
#define DUMP_DIR_PARENT   EXT_PATH("apps_data/picflipper")
#define DUMP_DIR          DUMP_DIR_PARENT "/dumps"
#define HASH_CHUNK        4096

static void
ensure_dump_dir (Storage *s)
{
    storage_common_mkdir(s, DUMP_DIR_PARENT);
    storage_common_mkdir(s, DUMP_DIR);
}

void
dump_store_make_paths (Storage    *storage,
                       const char *prefix,
                       uint16_t    devid,
                       char       *fname_out,
                       size_t      fname_sz,
                       char       *binpath,
                       size_t      bin_sz,
                       char       *hexpath,
                       size_t      hex_sz)
{
    ensure_dump_dir(storage);
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    char base[48];
    // <prefix>_<devid>_YYYY-MM-DD_HH-MM-SS
    snprintf(base,
             sizeof(base),
             "%s_%04X_%04u-%02u-%02u_%02u-%02u-%02u",
             prefix,
             (unsigned)devid,
             (unsigned)dt.year,
             (unsigned)dt.month,
             (unsigned)dt.day,
             (unsigned)dt.hour,
             (unsigned)dt.minute,
             (unsigned)dt.second);
    FuriString *uniq = furi_string_alloc();
    storage_get_next_filename(
        storage, DUMP_DIR, base, ".bin", uniq, sizeof(base) - 1);
    snprintf(fname_out, fname_sz, "%s", furi_string_get_cstr(uniq));
    furi_string_free(uniq);
    snprintf(binpath, bin_sz, "%s/%s.bin", DUMP_DIR, fname_out);
    snprintf(hexpath, hex_sz, "%s/%s.hex", DUMP_DIR, fname_out);
}

bool
dump_store_hash_sha256 (Storage *storage, const char *path, char out_hex[65])
{
    File    *f   = storage_file_alloc(storage);
    bool     ok  = storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING);
    uint8_t *buf = ok ? malloc(HASH_CHUNK) : NULL;
    if (buf)
    {
        Sha256Ctx ctx;
        sha256_init(&ctx);
        size_t n;
        while ((n = storage_file_read(f, buf, HASH_CHUNK)) > 0)
        {
            sha256_update(&ctx, buf, n);
        }
        ok = storage_file_get_error(f) == FSE_OK;
        if (ok)
        {
            uint8_t digest[32];
            sha256_final(&ctx, digest);
            static const char hexd[] = "0123456789abcdef";
            for (int i = 0; i < 32; i++)
            {
                out_hex[i * 2 + 0] = hexd[digest[i] >> 4];
                out_hex[i * 2 + 1] = hexd[digest[i] & 0x0F];
            }
            out_hex[64] = '\0';
        }
        free(buf);
    }
    else
    {
        ok = false;
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}

bool
dump_store_pick_bin (DialogsApp *dialogs, char *out_path, size_t out_sz)
{
    DialogsFileBrowserOptions opts;
    dialog_file_browser_set_basic_options(&opts, ".bin", NULL);
    opts.base_path     = DUMP_DIR;
    FuriString *result = furi_string_alloc_set(DUMP_DIR);
    bool        ok = dialog_file_browser_show(dialogs, result, result, &opts);
    if (ok)
    {
        strncpy(out_path, furi_string_get_cstr(result), out_sz - 1);
        out_path[out_sz - 1] = '\0';
    }
    furi_string_free(result);
    return ok;
}
