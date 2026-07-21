// SPDX-License-Identifier: MIT
// Minimal host-side test harness. Header-only; each test executable gets its
// own private counters. CHECK records a pass/fail line; tap_done() prints a
// summary and returns a process exit code (0 = all passed, 1 = any failure).
#pragma once
#include <stdio.h>
#include <string.h>

static int tap_run    = 0;
static int tap_failed = 0;

#define CHECK(cond, ...)                              \
    do                                                \
    {                                                 \
        tap_run++;                                    \
        if (cond)                                     \
        {                                             \
            printf("ok %d - ", tap_run);              \
            printf(__VA_ARGS__);                      \
            printf("\n");                             \
        }                                             \
        else                                          \
        {                                             \
            tap_failed++;                             \
            printf("not ok %d - ", tap_run);          \
            printf(__VA_ARGS__);                      \
            printf("\n    at %s:%d  expr: %s\n",       \
                   __FILE__,                          \
                   __LINE__,                          \
                   #cond);                            \
        }                                             \
    } while (0)

// Compare two byte buffers; on mismatch report the first differing offset.
static inline int
tap_mem_eq (const void *a, const void *b, size_t n)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++)
    {
        if (pa[i] != pb[i])
        {
            printf("    first diff at byte %zu: %02X != %02X\n",
                   i,
                   pa[i],
                   pb[i]);
            return 0;
        }
    }
    return 1;
}

static inline int
tap_done (const char *suite)
{
    printf("# %s: %d checks, %d failed\n",
           suite,
           tap_run,
           tap_failed);
    return tap_failed ? 1 : 0;
}
