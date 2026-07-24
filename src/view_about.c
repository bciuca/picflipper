// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

// "About" view: sticky inverted title bar + word-wrapped, scrolling body text.
// Fully self-contained — the copy and the word-wrap live here; the host only
// allocs the view and wires its previous-callback.
#include "view_about.h"
#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

#define ABOUT_HEADER_H 13 // black title bar height
#define ABOUT_TEXT_X   3
#define ABOUT_TEXT_W   118 // wrap width (leaves room for the scrollbar)
#define ABOUT_LINE_H   10  // baseline step
#define ABOUT_TOP      20  // first body baseline (below the header)
#define ABOUT_VISIBLE \
    5 // body lines that fit (20,30,40,50,60 — last clears descenders)
#define ABOUT_MAX_LINES  64
#define ABOUT_LINE_CHARS 44

typedef struct
{
    int scroll; // index of the top visible wrapped line
    int nlines; // total wrapped lines (recomputed each draw)
} AboutModel;

struct PicAboutView
{
    View *view;
};

static const char ABOUT_BODY[]
    = "\n"
      "This is an ICSP programmer for the PIC18F97J60 family of MCUs. I built "
      "this app "
      "because I was impatient and too cheap to order a PICkit5 programmer. "
      "And I wanted "
      "to see if it was even possible on a Flipper Zero.\n"
      "\n"
      "Features:\n"
      " - Dump full flash binary image\n"
      " - Dump live RAM\n"
      " - Write binary image with option to protect boot\n"
      " - Verify image\n"
      " - Pin connection check\n"
      " - ufbt log serial console to monitor in the cli\n"
      "\n"
      "Images are stored on the SD card:\n"
      "/apps_data/picflipper/\n"
      "  dumps\n"
      "\n"
      "Use at your own discretion!\n"
      "\n"
      "This app works great for me, but it may not for you.\n"
      "\n"
      "Worst case, you brick your device. That would suck.\n"
      "\n"
      "bciuca.com/picflipper";

static void
about_emit (char lines[][ABOUT_LINE_CHARS], int *n, const char *s, int len)
{
    if (len > ABOUT_LINE_CHARS - 1)
    {
        len = ABOUT_LINE_CHARS - 1;
    }
    memcpy(lines[*n], s, len);
    lines[*n][len] = '\0';
    (*n)++;
}

static int
about_wrap (Canvas     *canvas,
            const char *text,
            int         maxw,
            char        lines[][ABOUT_LINE_CHARS],
            int         max_lines)
{
    const int   cap = ABOUT_LINE_CHARS - 1;
    int         n   = 0;
    char        cur[ABOUT_LINE_CHARS];
    int         cl = 0; // current line length
    const char *p  = text;
    while (*p && n < max_lines)
    {
        if (*p == '\n')
        {
            about_emit(lines, &n, cur, cl);
            cl = 0;
            p++;
            continue;
        }
        if (cl == 0 && *p == ' ')
        {
            while (*p == ' ' && cl < cap)
            {
                cur[cl++] = ' ';
                p++;
            }
            continue;
        }
        const char *ws = p;
        while (*p && *p != ' ' && *p != '\n')
        {
            p++;
        }
        int wl = (int)(p - ws);
        if (*p == ' ')
        {
            p++;
        }

        char cand[ABOUT_LINE_CHARS];
        int  cn = 0;
        if (cl)
        {
            memcpy(cand, cur, cl);
            cn = cl;
            // Separate words with a space, but not when cur is bare indent.
            if (cn < cap && cur[cl - 1] != ' ')
            {
                cand[cn++] = ' ';
            }
        }
        int wc = (wl < cap - cn) ? wl : cap - cn;
        memcpy(cand + cn, ws, wc);
        cn += wc;
        cand[cn]  = '\0';
        bool fits = canvas_string_width(canvas, cand) <= maxw;
        if (fits)
        {
            memcpy(cur, cand, cn);
            cl = cn;
        }
        else if (cl)
        {
            // Doesn't fit: flush the line, start a new one with this word.
            about_emit(lines, &n, cur, cl);
            cl = (wl < cap) ? wl : cap;
            memcpy(cur, ws, cl);
        }
        else
        {
            // Single word wider than a whole line hard-break it.
            int i = 0;
            while (i < wl && n < max_lines)
            {
                char piece[ABOUT_LINE_CHARS];
                int  j = 0;
                while (i < wl && j < cap)
                {
                    piece[j]     = ws[i];
                    piece[j + 1] = '\0';
                    if (j > 0 && canvas_string_width(canvas, piece) > maxw)
                    {
                        piece[j] = '\0';
                        break;
                    }
                    i++;
                    j++;
                }
                about_emit(lines, &n, piece, j);
            }
            cl = 0;
        }
    }
    if (cl && n < max_lines)
    {
        about_emit(lines, &n, cur, cl);
    }
    return n;
}

static void
about_draw_cb (Canvas *canvas, void *model)
{
    AboutModel *m = model;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    static char lines[ABOUT_MAX_LINES][ABOUT_LINE_CHARS];
    int         n
        = about_wrap(canvas, ABOUT_BODY, ABOUT_TEXT_W, lines, ABOUT_MAX_LINES);
    m->nlines     = n;
    int maxscroll = (n > ABOUT_VISIBLE) ? (n - ABOUT_VISIBLE) : 0;
    if (m->scroll > maxscroll)
    {
        m->scroll = maxscroll;
    }
    if (m->scroll < 0)
    {
        m->scroll = 0;
    }

    int y = ABOUT_TOP;
    for (int i = 0; i < ABOUT_VISIBLE && (m->scroll + i) < n; i++)
    {
        canvas_draw_str(canvas, ABOUT_TEXT_X, y, lines[m->scroll + i]);
        y += ABOUT_LINE_H;
    }

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, ABOUT_HEADER_H);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas,
                            64,
                            ABOUT_HEADER_H / 2,
                            AlignCenter,
                            AlignCenter,
                            "About PICFlipper");
    canvas_set_color(canvas, ColorBlack);

    if (n > ABOUT_VISIBLE)
    {
        elements_scrollbar(canvas, m->scroll, maxscroll + 1);
    }
}

static bool
about_input_cb (InputEvent *event, void *ctx)
{
    PicAboutView *a = ctx;
    if (event->type != InputTypeShort && event->type != InputTypeRepeat)
    {
        return false;
    }
    if (event->key != InputKeyUp && event->key != InputKeyDown)
    {
        return false;
    }
    with_view_model(
        a->view,
        AboutModel * m,
        {
            int maxscroll
                = (m->nlines > ABOUT_VISIBLE) ? (m->nlines - ABOUT_VISIBLE) : 0;
            if (event->key == InputKeyDown && m->scroll < maxscroll)
            {
                m->scroll++;
            }
            if (event->key == InputKeyUp && m->scroll > 0)
            {
                m->scroll--;
            }
        },
        true);
    return true;
}

PicAboutView *
pic_about_view_alloc (void)
{
    PicAboutView *a = malloc(sizeof(PicAboutView));
    a->view         = view_alloc();
    view_allocate_model(a->view, ViewModelTypeLocking, sizeof(AboutModel));
    {
        AboutModel *m = view_get_model(a->view);
        m->scroll     = 0;
        m->nlines     = 0;
        view_commit_model(a->view, false);
    }
    view_set_context(a->view, a);
    view_set_draw_callback(a->view, about_draw_cb);
    view_set_input_callback(a->view, about_input_cb);
    return a;
}

View *
pic_about_view_get_view (PicAboutView *a)
{
    return a->view;
}

void
pic_about_view_free (PicAboutView *a)
{
    view_free(a->view);
    free(a);
}
