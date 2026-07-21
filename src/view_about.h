// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

#pragma once
#include <gui/view.h>

// "About" view: sticky inverted title bar + word-wrapped, scrolling body text
// (up/down scroll one line at a time). Self-contained — the copy and the
// word-wrap logic live in view_about.c.
typedef struct PicAboutView PicAboutView;

PicAboutView *pic_about_view_alloc(void);
View         *pic_about_view_get_view(PicAboutView *a);
void          pic_about_view_free(PicAboutView *a);
