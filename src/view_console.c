// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#include "view_console.h"
#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>

#define TAG "picflipper"
#define TAG_PIC \
    "PIC-UART" // forwarded target-PIC debug bytes show under this tag
#define RX_STREAM_SIZE 512

static const uint32_t baud_list[] = { 9600, 19200, 38400, 57600, 115200 };
#define BAUD_N (sizeof(baud_list) / sizeof(baud_list[0]))

typedef struct
{
    bool     active;
    uint32_t baud;
    uint32_t rx_bytes;
} ConsoleModel;

struct PicConsoleView
{
    View                *view;
    FuriHalSerialHandle *serial;
    FuriStreamBuffer    *rx_stream;
    FuriThread          *drain;
    volatile bool        drain_run;
    uint8_t              baud_idx;
    uint32_t             rx_bytes;
};

// --- UART RX (ISR ctx: only push bytes to the stream buffer) ---
static void
console_rx_irq (FuriHalSerialHandle *handle, FuriHalSerialRxEvent ev, void *ctx)
{
    PicConsoleView *c = ctx;
    if (ev & FuriHalSerialRxEventData)
    {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(c->rx_stream, &b, 1, 0);
    }
}

// --- drain thread: assemble lines, forward to FURI log, update on-screen
// counter ---
static int32_t
console_drain (void *ctx)
{
    PicConsoleView *c = ctx;
    char            line[160];
    size_t          li = 0;
    uint8_t         buf[64];
    while (c->drain_run)
    {
        size_t n
            = furi_stream_buffer_receive(c->rx_stream, buf, sizeof(buf), 100);
        if (n == 0)
        {
            // idle: flush any partial line so short bursts without a newline
            // still show
            if (li > 0)
            {
                line[li] = '\0';
                FURI_LOG_I(TAG_PIC, "%s", line);
                li = 0;
            }
            continue;
        }
        c->rx_bytes += n;
        for (size_t i = 0; i < n; i++)
        {
            char ch = (char)buf[i];
            if (ch == '\n' || ch == '\r' || li >= sizeof(line) - 1)
            {
                if (li > 0)
                {
                    line[li] = '\0';
                    FURI_LOG_I(TAG_PIC, "%s", line);
                    li = 0;
                }
            }
            else
            {
                line[li++] = (ch >= 32 && ch < 127) ? ch : '.';
            }
        }
        with_view_model(
            c->view, ConsoleModel * m, { m->rx_bytes = c->rx_bytes; }, true);
    }
    return 0;
}

static void
console_start (PicConsoleView *c)
{
    uint32_t baud = baud_list[c->baud_idx];
    c->rx_stream  = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
    c->drain_run  = true;
    c->drain = furi_thread_alloc_ex("PicConsoleRx", 1024, console_drain, c);
    furi_thread_start(c->drain);
    c->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if (c->serial)
    {
        furi_hal_serial_init(c->serial, baud);
        furi_hal_serial_async_rx_start(c->serial, console_rx_irq, c, false);
        FURI_LOG_I(
            TAG,
            "Console ON: PIC UART RX on pin14 @ %lu baud (view via CLI 'log')",
            baud);
    }
    else
    {
        FURI_LOG_E(TAG, "Console: USART busy; app diagnostics still log");
    }
}

static void
console_stop (PicConsoleView *c)
{
    if (c->serial)
    {
        furi_hal_serial_deinit(c->serial);
        furi_hal_serial_control_release(c->serial);
        c->serial = NULL;
    }
    c->drain_run = false;
    if (c->drain)
    {
        furi_thread_join(c->drain);
        furi_thread_free(c->drain);
        c->drain = NULL;
    }
    if (c->rx_stream)
    {
        furi_stream_buffer_free(c->rx_stream);
        c->rx_stream = NULL;
    }
    FURI_LOG_I(TAG, "Console OFF");
}

// Draw an inverted command token (white text on a filled black box) at baseline
// y; returns the x just past the box. Makes CLI commands stand out inline.
static int
console_cmd (Canvas *canvas, int x, int y, const char *s)
{
    int w = canvas_string_width(canvas, s);
    canvas_draw_box(canvas, x, y - 7, w + 4, 10);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str(canvas, x + 2, y, s);
    canvas_set_color(canvas, ColorBlack);
    return x + w + 4;
}

static void
console_draw_cb (Canvas *canvas, void *model)
{
    ConsoleModel *m = model;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, 64, 8, AlignCenter, AlignCenter, "PIC Serial Monitor");
    canvas_set_font(canvas, FontSecondary);
    // What it does: passively read the target PIC's own debug UART (its printf
    // output) on the Flipper RX pin and forward each line to the Flipper log,
    // which you read on a computer. Not ICSP — no chip probe here.
    canvas_draw_str(canvas, 2, 20, "PIC TX pin -> Flipper RX pin 14");
    // "CLI: [ufbt cli]" / "then [log info]" — commands inverted (white on
    // black). Two lines: it's too wide for one, and bare `log` streams nothing
    // (level none).
    int x = 4;
    canvas_draw_str(canvas, x, 34, "CLI:");
    x += canvas_string_width(canvas, "CLI:") + 4;
    console_cmd(canvas, x, 34, "ufbt cli");
    x = 4;
    canvas_draw_str(canvas, x, 46, "then");
    x += canvas_string_width(canvas, "then") + 4;
    console_cmd(canvas, x, 46, "log info");
    // Bottom bar: D-pad left/right cycle baud — arrow glyphs pinned to the
    // corners (no label); current baud + RX-byte count centered between them.
    elements_button_left(canvas, "");
    elements_button_right(canvas, "");
    char line[32];
    snprintf(line,
             sizeof(line),
             "%lu  RX %luB",
             (unsigned long)m->baud,
             (unsigned long)m->rx_bytes);
    canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignCenter, line);
}

static bool
console_input_cb (InputEvent *event, void *ctx)
{
    PicConsoleView *c = ctx;
    if (event->type != InputTypeShort)
    {
        return false;
    }
    if (event->key == InputKeyLeft || event->key == InputKeyRight)
    {
        c->baud_idx
            = (uint8_t)((c->baud_idx
                         + (event->key == InputKeyRight ? 1 : BAUD_N - 1))
                        % BAUD_N);
        // restart serial at the new baud (keep the drain thread + stream)
        if (c->serial)
        {
            furi_hal_serial_deinit(c->serial);
            furi_hal_serial_init(c->serial, baud_list[c->baud_idx]);
            furi_hal_serial_async_rx_start(c->serial, console_rx_irq, c, false);
        }
        FURI_LOG_I(
            TAG, "Console baud -> %lu", (unsigned long)baud_list[c->baud_idx]);
        with_view_model(
            c->view,
            ConsoleModel * m,
            { m->baud = baud_list[c->baud_idx]; },
            true);
        return true;
    }
    return false; // Back -> menu
}

static void
console_enter_cb (void *ctx)
{
    PicConsoleView *c = ctx;
    c->rx_bytes       = 0;
    console_start(c);
    with_view_model(
        c->view,
        ConsoleModel * m,
        {
            m->active   = true;
            m->baud     = baud_list[c->baud_idx];
            m->rx_bytes = 0;
        },
        true);
}

static void
console_exit_cb (void *ctx)
{
    PicConsoleView *c = ctx;
    console_stop(c);
    with_view_model(c->view, ConsoleModel * m, { m->active = false; }, false);
}

PicConsoleView *
pic_console_view_alloc (void)
{
    PicConsoleView *c = malloc(sizeof(PicConsoleView));
    memset(c, 0, sizeof(PicConsoleView));
    c->baud_idx = BAUD_N - 1; // default 115200
    c->view     = view_alloc();
    view_allocate_model(c->view, ViewModelTypeLocking, sizeof(ConsoleModel));
    view_set_context(c->view, c);
    view_set_draw_callback(c->view, console_draw_cb);
    view_set_input_callback(c->view, console_input_cb);
    view_set_enter_callback(c->view, console_enter_cb);
    view_set_exit_callback(c->view, console_exit_cb);
    return c;
}

View *
pic_console_view_get_view (PicConsoleView *c)
{
    return c->view;
}

void
pic_console_view_free (PicConsoleView *c)
{
    view_free(c->view);
    free(c);
}
