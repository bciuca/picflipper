// SPDX-License-Identifier: MIT
// Host test stub for <gui/gui.h>. Provides the Canvas type, drawing enums, and
// canvas_* drawing calls used by view_about.c. Only canvas_string_width has
// behavior (defined by test_view_about.c); the rest are inert no-ops since the
// draw callback is never invoked by the tests.
#pragma once
#include <stdint.h>

typedef struct Canvas Canvas;

typedef enum
{
    FontPrimary,
    FontSecondary,
    FontKeyboard,
    FontBigNumbers,
} Font;

typedef enum
{
    ColorWhite,
    ColorBlack,
} Color;

typedef enum
{
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;

// Behavioral: measures string width. Defined by the test (strlen-based model).
uint16_t canvas_string_width(Canvas *canvas, const char *str);

static inline void
canvas_clear (Canvas *canvas)
{
    (void)canvas;
}
static inline void
canvas_set_font (Canvas *canvas, Font font)
{
    (void)canvas;
    (void)font;
}
static inline void
canvas_set_color (Canvas *canvas, Color color)
{
    (void)canvas;
    (void)color;
}
static inline void
canvas_draw_str (Canvas *canvas, int x, int y, const char *str)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)str;
}
static inline void
canvas_draw_str_aligned (Canvas *canvas, int x, int y, Align h, Align v,
                         const char *str)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)h;
    (void)v;
    (void)str;
}
static inline void
canvas_draw_box (Canvas *canvas, int x, int y, int w, int h)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
static inline void
canvas_draw_frame (Canvas *canvas, int x, int y, int w, int h)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
