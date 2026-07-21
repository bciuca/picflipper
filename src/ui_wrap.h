// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

// Hard character-wrap for space-less strings (filenames, addresses) that would
// otherwise run off the 128 px screen. Unlike the About view's word-wrap, this
// breaks mid-token: filenames like pic_1F23_2026-07-12_16-38-02.bin have no
// spaces to break on. Pure (measurement is injected) so it is host-testable
// without a Canvas.
#pragma once
#include <stdint.h>
#include <string.h>

#define UI_WRAP_LINE_CAP  40 // max chars per wrapped line, incl. NUL
#define UI_WRAP_MAX_LINES 6

// Measures the rendered width of a NUL-terminated string. In the app this wraps
// canvas_string_width; in tests it is a deterministic stub.
typedef uint16_t (*UiWidthFn)(void *ctx, const char *s);

// Greedily pack `s` into `lines`, breaking to a new line whenever adding the
// next character would exceed `maxw` (measured by `measure`). Always advances at
// least one character per line, so a glyph wider than `maxw` still terminates.
// Every character of `s` up to the max_lines/line_cap budget appears exactly
// once, in order. Returns the number of lines written.
static inline int
ui_wrap_hard (const char *s,
              int         maxw,
              UiWidthFn   measure,
              void       *ctx,
              char        lines[][UI_WRAP_LINE_CAP],
              int         max_lines)
{
    const int len = (int)strlen(s);
    const int cap = UI_WRAP_LINE_CAP - 1;
    int       i   = 0;
    int       n   = 0;
    while (i < len && n < max_lines)
    {
        char line[UI_WRAP_LINE_CAP];
        int  j = 0;
        while (i < len && j < cap)
        {
            line[j]     = s[i];
            line[j + 1] = '\0';
            if (j > 0 && (int)measure(ctx, line) > maxw)
            {
                line[j] = '\0'; // next char overflows; defer it to a new line
                break;
            }
            i++;
            j++;
        }
        memcpy(lines[n], line, (size_t)j);
        lines[n][j] = '\0';
        n++;
    }
    return n;
}
