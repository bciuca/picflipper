// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#include "app_ui.h"
#include "ui_wrap.h"
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

// Adapter so ui_wrap_hard() can measure via canvas_string_width (ctx = Canvas).
static uint16_t
ui_measure_canvas (void *ctx, const char *s)
{
    return canvas_string_width((Canvas *)ctx, s);
}

// Draw `text` centered at x=64, hard-wrapped so no line exceeds the screen.
// Returns the y past the last line drawn.
static int
ui_draw_wrapped_centered (Canvas *canvas, int y, const char *text)
{
    char lines[UI_WRAP_MAX_LINES][UI_WRAP_LINE_CAP];
    int  n = ui_wrap_hard(text, 124, ui_measure_canvas, canvas, lines,
                          UI_WRAP_MAX_LINES);
    for (int i = 0; i < n; i++)
    {
        canvas_draw_str_aligned(canvas, 64, y, AlignCenter, AlignCenter,
                                lines[i]);
        y += 10;
    }
    return y;
}

static void
ui_progress_bar (
    Canvas *canvas, uint8_t x, uint8_t y, uint8_t w, uint8_t h, float p)
{
    if (p < 0.0f)
    {
        p = 0.0f;
    }
    if (p > 1.0f)
    {
        p = 1.0f;
    }
    canvas_draw_frame(canvas, x, y, w, h);
    uint8_t fill = (uint8_t)((float)(w - 2) * p);
    if (fill > 0)
    {
        canvas_draw_box(canvas, x + 1, y + 1, fill, h - 2);
    }
}

void
app_ui_draw (Canvas *canvas, const AppState *st)
{
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas,
                            64,
                            8,
                            AlignCenter,
                            AlignCenter,
                            st->title[0] ? st->title : "PICFlipper");
    canvas_set_font(canvas, FontSecondary);

    char line[40];
    switch (st->phase)
    {
        case StIdle:
            // RAM ready prompt: the live snapshot is one-shot, so this view
            // does NOT probe on entry (that would reset the target). Trigger
            // whatever you want to capture on the target, then OK enters ICSP +
            // captures.
            canvas_draw_str_aligned(
                canvas, 64, 26, AlignCenter, AlignCenter, "Live RAM snapshot");
            canvas_draw_str_aligned(canvas,
                                    64,
                                    38,
                                    AlignCenter,
                                    AlignCenter,
                                    "Trigger target, then:");
            elements_button_center(canvas, "Snapshot");
            break;
        case StSearching: {
            // Spinner: 8 dots on a ring, a filled head + short trailing tail
            // that rotates with st->spin. The probe is near-instant, so this
            // mostly reads as "scanning" and keeps animating while we
            // auto-retry a missing chip.
            static const int8_t ox[8] = { 12, 8, 0, -8, -12, -8, 0, 8 };
            static const int8_t oy[8] = { 0, 8, 12, 8, 0, -8, -12, -8 };
            const int           cx = 64, cy = 32;
            int                 head = st->spin % 8;
            for (int k = 0; k < 8; k++)
            {
                int x = cx + ox[k], y = cy + oy[k];
                int back
                    = (head - k + 8) % 8; // 0 = head, 1 = one step behind, ...
                if (back == 0)
                {
                    canvas_draw_disc(canvas, x, y, 2);
                }
                else if (back == 1)
                {
                    canvas_draw_disc(canvas, x, y, 1);
                }
                else
                {
                    canvas_draw_dot(canvas, x, y);
                }
            }
            canvas_draw_str_aligned(canvas,
                                    64,
                                    54,
                                    AlignCenter,
                                    AlignCenter,
                                    "Searching for PIC...");
            break;
        }
        case StFound:
            snprintf(line,
                     sizeof(line),
                     "PIC18F67J60  ID:%04X",
                     (unsigned int)st->device_id);
            canvas_draw_str_aligned(
                canvas, 64, 26, AlignCenter, AlignCenter, line);
            canvas_draw_str_aligned(
                canvas, 64, 38, AlignCenter, AlignCenter, "Chip detected");
            elements_button_center(canvas, "Dump");
            break;
        case StConnecting:
            canvas_draw_str_aligned(
                canvas, 64, 36, AlignCenter, AlignCenter, "Connecting...");
            break;
        case StDumping: {
            float p = st->bytes_total
                          ? (float)st->bytes_done / (float)st->bytes_total
                          : 0.0f;
            ui_progress_bar(canvas, 8, 24, 112, 9, p);
            snprintf(line,
                     sizeof(line),
                     "%lu / %lu B",
                     (unsigned long)st->bytes_done,
                     (unsigned long)st->bytes_total);
            canvas_draw_str_aligned(
                canvas, 64, 44, AlignCenter, AlignCenter, line);
            snprintf(line,
                     sizeof(line),
                     "Device ID: %04X",
                     (unsigned int)st->device_id);
            canvas_draw_str_aligned(
                canvas, 64, 56, AlignCenter, AlignCenter, line);
            break;
        }
        case StConfirm: {
            // Confirm screen for both write and verify: file name + SHA of the
            // image. Write is destructive (hold-to-erase); verify is read-only
            // (single OK). The subtitle names each variant's scope; stage is
            // "Verify" / "App+boot" / "Full".
            bool        verify   = (strcmp(st->stage, "Verify") == 0);
            const char *subtitle = verify ? "Verify file matches chip"
                                   : strcmp(st->stage, "App+boot") == 0
                                       ? "App data only, keep boot"
                                       : "App data + Boot";
            if (subtitle)
            {
                canvas_draw_str_aligned(
                    canvas, 64, 18, AlignCenter, AlignCenter, subtitle);
            }
            canvas_draw_str_aligned(
                canvas, 64, 28, AlignCenter, AlignCenter, st->msg);
            // First 16 hex (64 bits) of sha256sum to eyeball-verify
            if (strlen(st->sha) == 64)
            {
                snprintf(line, sizeof(line), "SHA %.16s...", st->sha);
            }
            else
            {
                snprintf(
                    line, sizeof(line), "%.*s", (int)sizeof(line) - 1, st->sha);
            }
            canvas_draw_str_aligned(
                canvas, 64, 38, AlignCenter, AlignCenter, line);
            if (verify)
            {
                // Read-only compare against the file: one OK, no erase warning.
                elements_button_center(canvas, "Verify");
            }
            else
            {
                // Seconds remaining, counting 5 -> 1 as the hold progresses
                // (st->hold runs 0..1 over the 5 s). Fires the write at 1.0.
                int secs = 5 - (int)(st->hold * 5.0f);
                if (secs < 1)
                {
                    secs = 1;
                }
                if (secs > 5)
                {
                    secs = 5;
                }
                snprintf(line, sizeof(line), "Erases chip! Hold OK %ds", secs);
                canvas_draw_str_aligned(
                    canvas, 64, 47, AlignCenter, AlignCenter, line);
                // Fills over the 5s hold, releasing early resets to empty.
                elements_progress_bar(canvas, 14, 53, 100, st->hold);
            }
            break;
        }
        case StWriting: {
            float p = st->bytes_total
                          ? (float)st->bytes_done / (float)st->bytes_total
                          : 0.0f;
            ui_progress_bar(canvas, 8, 24, 112, 9, p);
            snprintf(line,
                     sizeof(line),
                     "%s %lu/%lu",
                     st->stage,
                     (unsigned long)st->bytes_done,
                     (unsigned long)st->bytes_total);
            canvas_draw_str_aligned(
                canvas, 64, 44, AlignCenter, AlignCenter, line);
            snprintf(line,
                     sizeof(line),
                     "Device ID: %04X",
                     (unsigned int)st->device_id);
            canvas_draw_str_aligned(
                canvas, 64, 56, AlignCenter, AlignCenter, line);
            break;
        }
        case StDone:
            canvas_draw_str_aligned(canvas,
                                    64,
                                    24,
                                    AlignCenter,
                                    AlignCenter,
                                    st->stage[0] ? "Done" : "Saved");
            ui_draw_wrapped_centered(canvas, 40, st->msg);
            break;
        case StError:
            canvas_draw_str_aligned(
                canvas, 64, 24, AlignCenter, AlignCenter, "Error");
            ui_draw_wrapped_centered(canvas, 40, st->msg);
            break;
    }
}
