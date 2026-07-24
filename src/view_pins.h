// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#pragma once
#include <gui/view.h>

// "Wiring" view: shows the wiring diagram with a live badge — an animated
// "Searching..." until the expected PIC answers, then "WIRING: OK". Re-probes
// on a timer while the view is open; OK forces an immediate re-check.
typedef struct PicPinsView PicPinsView;

PicPinsView *pic_pins_view_alloc(void);
View        *pic_pins_view_get_view(PicPinsView *p);
void         pic_pins_view_free(PicPinsView *p);
