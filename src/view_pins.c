// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#include "view_pins.h"
#include "pic_config.h"
#include "pic_icsp.h"
#include "pic_dump.h"
#include <furi.h>
#include <furi_hal_gpio.h>
#include <gui/gui.h>
#include <input/input.h>
#include <picflipper_icons.h>
#include <stdio.h>

#define TAG "picflipper"

typedef struct
{
    bool     probed; // a probe has completed at least once this session
    uint16_t devid;  // last device ID read
    uint8_t  frame;  // animation frame for the "Searching..." ellipsis
} PinsModel;

struct PicPinsView
{
    View      *view;
    FuriTimer *timer; // paces the ellipsis animation + periodic re-probe
    uint8_t    tick;  // timer tick counter (re-probe every Nth tick)
};

// PIC18F67J60 family: device ID upper bits 0x1F2x. A successful read of the
// expected family means the ICSP wiring (MCLR/PGC/PGD/GND) is good.
static bool
pic_id_ok (uint16_t devid)
{
    return (devid & 0xFFE0) == 0x1F20;
}

// Tristate the ICSP lines (high-Z) so we don't hold the target's bus between
// probes.
static void
pins_tristate (void)
{
    furi_hal_gpio_init(PIC_PIN_MCLR, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(PIC_PIN_PGC, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(PIC_PIN_PGD, GpioModeInput, GpioPullNo, GpioSpeedLow);
}

// One-shot device-ID probe: drive the ICSP bus, read the ID, tristate back, and
// publish the result so the view redraws WIRING: OK / Searching.
static void
pins_probe (PicPinsView *p)
{
    PicIcsp d
        = { .pgc = PIC_PIN_PGC, .pgd = PIC_PIN_PGD, .mclr = PIC_PIN_MCLR };
    pic_icsp_init(&d);
    pic_icsp_enter(&d);
    uint16_t devid = pic_read_device_id(&d);
    pic_icsp_exit(&d);
    pins_tristate();
    with_view_model(
        p->view,
        PinsModel * m,
        {
            m->probed = true;
            m->devid  = devid;
        },
        true);
}

// Periodic (~500 ms): advance the ellipsis each tick; re-probe every 2nd tick
// (~1 s) so the badge tracks the live wiring (flips back to Searching if
// dropped).
static void
pins_timer_cb (void *ctx)
{
    PicPinsView *p = ctx;
    p->tick++;
    if (p->tick % 2 == 0)
    {
        pins_probe(p);
    }
    with_view_model(p->view, PinsModel * m, { m->frame++; }, true);
}

// Inverted pill (black box + white text) so the status stays legible over the
// black-on-white wiring artwork. Sits in the diagram's empty mid-band.
static void
draw_status_badge (Canvas *canvas, const char *text)
{
    canvas_set_font(canvas, FontSecondary);
    // Fixed pill width (sized for the widest state) + left-anchored text, so
    // the pill and the "Searching" label stay put while the ellipsis grows
    // rightward.
    int w1 = canvas_string_width(canvas, "Searching...");
    int w2 = canvas_string_width(canvas, "WIRING: OK");
    int bw = (w1 > w2 ? w1 : w2) + 8;
    int bh = 13;
    int bx = (128 - bw) / 2;
    int by = 33;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, bx - 1, by - 1, bw + 2, bh + 2); // white halo
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, bx, by, bw, bh); // black pill
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str_aligned(
        canvas, bx + 4, by + bh / 2 + 1, AlignLeft, AlignCenter, text);
    canvas_set_color(canvas, ColorBlack);
}

static void
pins_draw_cb (Canvas *canvas, void *model)
{
    PinsModel *m = model;
    canvas_clear(canvas);
    canvas_draw_icon(canvas, 0, 0, &I_wiring);
    char status[20];
    if (m->probed && pic_id_ok(m->devid))
    {
        snprintf(status, sizeof(status), "WIRING: OK");
    }
    else
    {
        // Left-anchored (see draw_status_badge), so just append 0..3 dots.
        int nd = m->frame % 4;
        snprintf(status, sizeof(status), "Searching%.*s", nd, "...");
    }
    draw_status_badge(canvas, status);
}

static bool
pins_input_cb (InputEvent *event, void *ctx)
{
    PicPinsView *p = ctx;
    if (event->type == InputTypeShort && event->key == InputKeyOk)
    {
        pins_probe(p); // OK forces an immediate re-check
        return true;
    }
    return false; // Back falls through to the previous-view callback (menu)
}

static void
pins_enter_cb (void *ctx)
{
    PicPinsView *p = ctx;
    FURI_LOG_I(TAG, "Wiring view");
    p->tick = 0;
    pins_tristate();
    with_view_model(
        p->view,
        PinsModel * m,
        {
            m->probed = false;
            m->frame  = 0;
        },
        false);
    pins_probe(p); // probe once immediately so the status shows without waiting
    furi_timer_start(p->timer,
                     furi_ms_to_ticks(500)); // then animate + re-probe
}

static void
pins_exit_cb (void *ctx)
{
    PicPinsView *p = ctx;
    furi_timer_stop(p->timer);
    pins_tristate(); // don't hold the target's ICSP bus once we leave
}

PicPinsView *
pic_pins_view_alloc (void)
{
    PicPinsView *p = malloc(sizeof(PicPinsView));
    p->tick        = 0;
    p->view        = view_alloc();
    p->timer       = furi_timer_alloc(pins_timer_cb, FuriTimerTypePeriodic, p);
    view_allocate_model(p->view, ViewModelTypeLocking, sizeof(PinsModel));
    view_set_context(p->view, p);
    view_set_draw_callback(p->view, pins_draw_cb);
    view_set_input_callback(p->view, pins_input_cb);
    view_set_enter_callback(p->view, pins_enter_cb);
    view_set_exit_callback(p->view, pins_exit_cb);
    return p;
}

View *
pic_pins_view_get_view (PicPinsView *p)
{
    return p->view;
}

void
pic_pins_view_free (PicPinsView *p)
{
    furi_timer_stop(p->timer);
    furi_timer_free(p->timer);
    view_free(p->view);
    free(p);
}
