// SPDX-License-Identifier: MIT
// Host unit tests for the word-wrap in view_about.c (about_wrap/about_emit).
// canvas_string_width is stubbed with a fixed-advance model (6 px per char) so
// wrap points are deterministic; the rest of the GUI surface is inert stubs
// (see test/stubs/gui/*). The View allocation/draw/input paths are never
// invoked. maxw is passed explicitly per case for hand-checkable wrap points.
#include "tap.h"
#include <gui/gui.h> // Canvas type for the width stub below
#include <string.h>

// Fixed 6-px advance per glyph. 60 px => 10 chars fit; 66 px (11 chars) do not.
uint16_t
canvas_string_width (Canvas *canvas, const char *str)
{
    (void)canvas;
    return (uint16_t)(strlen(str) * 6);
}

#include "view_about.c"

// Convenience wrapper around the static about_wrap().
static int
wrap (const char *text, int maxw, char lines[][ABOUT_LINE_CHARS])
{
    return about_wrap(NULL, text, maxw, lines, ABOUT_MAX_LINES);
}

int
main (void)
{
    char lines[ABOUT_MAX_LINES][ABOUT_LINE_CHARS];
    int  n;

    // --- Wrap at word boundaries --------------------------------------------
    n = wrap("hello world foo", 60, lines);
    CHECK(n == 2 && strcmp(lines[0], "hello") == 0
              && strcmp(lines[1], "world foo") == 0,
          "wraps at word boundary (10-char lines)");

    // --- Honor '\n', including a preserved blank line ------------------------
    n = wrap("a\n\nb", 600, lines);
    CHECK(n == 3 && strcmp(lines[0], "a") == 0 && strcmp(lines[1], "") == 0
              && strcmp(lines[2], "b") == 0,
          "explicit newlines split lines; blank line preserved");

    // --- Preserved leading indent on the first line -------------------------
    n = wrap(" - Dump full flash binary image", 60, lines);
    CHECK(n == 4 && strcmp(lines[0], " - Dump") == 0
              && strcmp(lines[1], "full flash") == 0
              && strcmp(lines[2], "binary") == 0
              && strcmp(lines[3], "image") == 0,
          "leading indent kept on first wrapped line");

    // --- Hard-break of an over-long token (URL, no spaces) ------------------
    n = wrap("bciuca.com/picflipper", 60, lines);
    CHECK(n == 3 && strcmp(lines[0], "bciuca.com") == 0
              && strcmp(lines[1], "/picflippe") == 0
              && strcmp(lines[2], "r") == 0,
          "over-long token hard-breaks at the width limit");

    // --- about_emit truncates an over-length source at ABOUT_LINE_CHARS-1 ---
    {
        int  m = 0;
        char src[ABOUT_LINE_CHARS + 20];
        memset(src, 'x', sizeof(src) - 1);
        src[sizeof(src) - 1] = '\0';
        about_emit(lines, &m, src, (int)strlen(src));
        CHECK(m == 1 && (int)strlen(lines[0]) == ABOUT_LINE_CHARS - 1
                  && lines[0][ABOUT_LINE_CHARS - 1] == '\0',
              "about_emit clamps a too-long line to ABOUT_LINE_CHARS-1");
    }

    return tap_done("view_about");
}
