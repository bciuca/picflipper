// SPDX-License-Identifier: MIT
// Host test stub for <gui/elements.h>. Only the scrollbar helper used by the
// about view's draw callback; inert on the host.
#pragma once
#include <gui/gui.h>
#include <stddef.h>

static inline void
elements_scrollbar (Canvas *canvas, size_t pos, size_t total)
{
    (void)canvas;
    (void)pos;
    (void)total;
}
