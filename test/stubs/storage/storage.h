// SPDX-License-Identifier: MIT
// Host test stub for <storage/storage.h>. Storage/File are opaque; the file
// I/O functions are declared here and defined by test_dump_writer.c, which
// captures written bytes into an in-memory buffer.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct Storage Storage;
typedef struct File    File;

typedef enum
{
    FSAM_READ  = 1,
    FSAM_WRITE = 2,
} FS_AccessMode;

typedef enum
{
    FSOM_OPEN_EXISTING = 1,
    FSOM_OPEN_APPEND   = 2,
    FSOM_CREATE_NEW    = 3,
    FSOM_CREATE_ALWAYS = 4,
} FS_OpenMode;

File  *storage_file_alloc(Storage *s);
bool   storage_file_open(File          *f,
                         const char    *path,
                         FS_AccessMode  am,
                         FS_OpenMode    om);
size_t storage_file_write(File *f, const void *buf, size_t n);
bool   storage_file_close(File *f);
void   storage_file_free(File *f);
bool   storage_common_mkdir(Storage *s, const char *path);
