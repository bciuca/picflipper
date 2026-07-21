// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

// PIC operations subsystem — view controller: renders the dump/progress view,
// handles input, runs the chip-detection gate and the spinner/hold timer, and
// owns the lifecycle + public start API. The ICSP worker-thread bodies (the
// actual dump/RAM/write loops) live in pic_jobs.c; shared state in pic_ops_i.h.
#include "pic_ops_i.h"
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_types.h"
#include "app_ui.h"
#include "pic_config.h"
#include "pic_icsp.h"
#include "pic_dump.h"
#include "dump_store.h"

// Screen header for the current op; stamped into the model at view entry so it
// persists across every phase of that op.
static const char *
op_title (uint8_t op)
{
    switch (op)
    {
        case OpDumpFlash:
            return "Dump Flash";
        case OpDumpRam:
            return "Dump RAM";
        case OpWriteFull:
            return "Write Flash (full)";
        case OpWriteApp:
            return "Write App";
        case OpVerify:
            return "Verify";
        default:
            return "PICFlipper";
    }
}

// --- chip-detection gate (shared by every dump-view op) --------------------

// Flash-dump detection gate: enter ICSP, read the device ID, exit, and retry
// every ~400 ms until the expected family answers or the user backs out. Phase
// stays StSearching (spinner) between tries; on detect -> StFound (OK to dump).
// RAM and write/verify deliberately skip this — they must not reset the target
// just by opening the view (see enter_ram_ready / enter_confirm).
static int32_t
pic_search_worker (void *ctx)
{
    PicOps *ops = ctx;
    PicIcsp d
        = { .pgc = PIC_PIN_PGC, .pgd = PIC_PIN_PGD, .mclr = PIC_PIN_MCLR };
    pic_icsp_init(&d);
    while (!ops->cancel)
    {
        pic_icsp_enter(&d);
        uint16_t devid = pic_read_device_id(&d);
        pic_icsp_exit(&d);
        if ((devid & 0xFFE0) == 0x1F20)
        {
            FURI_LOG_I(TAG,
                       "Search: found PIC18F67J60 (DEVID 0x%04X)",
                       (unsigned)devid);
            // Clear worker_active before publishing StFound so a fast OK isn't
            // rejected by the dump_start busy-guard.
            ops->worker_active = false;
            AppState *st       = view_get_model(ops->dump_view);
            st->device_id      = devid;
            st->phase          = StFound;
            view_commit_model(ops->dump_view, true);
            return 0;
        }
        for (int i = 0; i < 8 && !ops->cancel; i++)
        {
            furi_delay_ms(50);
        }
    }
    ops->worker_active = false;
    return 0;
}

// Switch to the dump view and start the detection gate for ops->op. Callers set
// op/job/keep_boot/verify_only (and confirm_sha for writes) beforehand.
static void
enter_search (PicOps *ops)
{
    // Force a clean handoff from any prior worker (stale search or finished
    // op).
    if (ops->worker)
    {
        ops->cancel = true;
        furi_thread_join(ops->worker);
        furi_thread_free(ops->worker);
        ops->worker = NULL;
    }
    furi_timer_stop(ops->anim_timer);
    ops->holding = false;
    ops->cancel  = false;
    {
        AppState *st = view_get_model(ops->dump_view);
        memset(st, 0, sizeof(*st));
        snprintf(st->title, sizeof(st->title), "%s", op_title(ops->op));
        st->phase = StSearching;
        view_commit_model(ops->dump_view, true);
    }
    view_dispatcher_switch_to_view(ops->view_dispatcher, ops->view_id);
    ops->worker_active = true;
    ops->worker
        = furi_thread_alloc_ex("PicSearchWorker", 4096, pic_search_worker, ops);
    furi_thread_start(ops->worker);
    furi_timer_start(ops->anim_timer, furi_ms_to_ticks(120));
}

// RAM is a one-shot live snapshot: probing to detect the chip would enter ICSP
// and reset the target, destroying the state we want to capture. So the RAM
// view opens on a ready prompt with NO probe; OK triggers the single ICSP entry
// + capture at the instant the user chooses. (Same clean handoff as
// enter_search, minus the detection worker/timer.)
static void
enter_ram_ready (PicOps *ops)
{
    if (ops->worker)
    {
        ops->cancel = true;
        furi_thread_join(ops->worker);
        furi_thread_free(ops->worker);
        ops->worker = NULL;
    }
    furi_timer_stop(ops->anim_timer);
    ops->holding = false;
    ops->cancel  = false;
    {
        AppState *st = view_get_model(ops->dump_view);
        memset(st, 0, sizeof(*st));
        snprintf(st->title, sizeof(st->title), "%s", op_title(ops->op));
        st->phase = StIdle;
        view_commit_model(ops->dump_view, true);
    }
    view_dispatcher_switch_to_view(ops->view_dispatcher, ops->view_id);
}

// Write and verify open on this — NO probe (navigating to the op must not reset
// the target). Shows the picked file's name + SHA-256. Write is then the
// hold-to-erase gate; verify is a single OK. The worker does the one ICSP entry
// and the device-ID check when the user commits.
static void
enter_confirm (PicOps *ops)
{
    if (ops->worker)
    {
        ops->cancel = true;
        furi_thread_join(ops->worker);
        furi_thread_free(ops->worker);
        ops->worker = NULL;
    }
    furi_timer_stop(ops->anim_timer);
    ops->holding = false;
    ops->cancel  = false;
    {
        AppState *st = view_get_model(ops->dump_view);
        memset(st, 0, sizeof(*st));
        snprintf(st->title, sizeof(st->title), "%s", op_title(ops->op));
        const char *base = strrchr(ops->write_path, '/');
        snprintf(st->msg,
                 sizeof(st->msg),
                 "%.*s",
                 (int)sizeof(st->msg) - 1,
                 base ? base + 1 : ops->write_path);
        snprintf(st->stage,
                 sizeof(st->stage),
                 "%s",
                 ops->verify_only ? "Verify"
                 : ops->keep_boot ? "App+boot"
                                  : "Full");
        strncpy(st->sha, ops->confirm_sha, sizeof(st->sha) - 1);
        st->sha[sizeof(st->sha) - 1] = '\0';
        st->phase                    = StConfirm;
        view_commit_model(ops->dump_view, true);
    }
    view_dispatcher_switch_to_view(ops->view_dispatcher, ops->view_id);
}

// Periodic (started on demand) timer that both advances the search spinner and
// fills the confirm-gate hold bar. Self-stops whenever neither is active. When
// the hold reaches 5 s, it kicks off the write. Runs on the timer task.
static void
anim_timer_cb (void *ctx)
{
    PicOps *ops  = ctx;
    bool    keep = false, fire_write = false;
    with_view_model(
        ops->dump_view,
        AppState * st,
        {
            if (st->phase == StSearching)
            {
                st->spin++;
                keep = true;
            }
            else if (st->phase == StConfirm && ops->holding)
            {
                uint32_t need    = furi_ms_to_ticks(5000);
                uint32_t elapsed = furi_get_tick() - ops->hold_start;
                float    p       = need ? (float)elapsed / (float)need : 1.0f;
                if (p >= 1.0f)
                {
                    p          = 1.0f;
                    fire_write = true;
                }
                st->hold = p;
                keep     = true;
            }
        },
        true);
    if (fire_write)
    {
        ops->holding = false;
        furi_timer_stop(ops->anim_timer);
        prog_start(ops);
    }
    else if (!keep)
    {
        furi_timer_stop(ops->anim_timer);
    }
}

// --- dump view callbacks ---
static void
dump_draw_cb (Canvas *canvas, void *model)
{
    app_ui_draw(canvas, (AppState *)model);
}

static bool
dump_input_cb (InputEvent *event, void *ctx)
{
    PicOps  *ops = ctx;
    AppPhase phase;
    with_view_model(
        ops->dump_view, AppState * st, { phase = st->phase; }, false);

    if (event->type == InputTypeShort && event->key == InputKeyBack)
    {
        ops->holding
            = false; // dropping the confirm gate cancels any in-progress hold
        if (phase == StSearching)
        {
            // Cancel the probe loop and drop the phase so the spinner timer
            // self-stops, then fall through to the menu.
            ops->cancel = true;
            with_view_model(
                ops->dump_view, AppState * st, { st->phase = StIdle; }, false);
            return false; // -> menu
        }
        if (ops->worker_active)
        {
            ops->cancel = true; // abort in-flight dump/write, stay on screen
            return true;
        }
        return false; // -> menu
    }

    // Write confirm gate: OK must be held continuously for 5 s (the anim timer
    // fills the bar and fires the write); releasing early resets the fill.
    if (phase == StConfirm)
    {
        // Verify is read-only: a single OK runs it (no hold gate).
        if (ops->verify_only)
        {
            if (event->type == InputTypeShort && event->key == InputKeyOk)
            {
                prog_start(ops);
            }
            return true;
        }
        // Write: OK must be held continuously for 5 s (the anim timer fills the
        // bar and fires the write); releasing early resets the fill.
        if (event->key == InputKeyOk)
        {
            if (event->type == InputTypePress)
            {
                ops->hold_start = furi_get_tick();
                ops->holding    = true;
                furi_timer_start(ops->anim_timer, furi_ms_to_ticks(50));
            }
            else if (event->type == InputTypeRelease)
            {
                ops->holding = false;
                with_view_model(
                    ops->dump_view, AppState * st, { st->hold = 0.0f; }, true);
            }
        }
        return true; // swallow other keys here (short Back handled above ->
                     // menu)
    }

    // RAM ready prompt (no detection probe — see enter_ram_ready): OK captures.
    if (phase == StIdle && event->type == InputTypeShort
        && event->key == InputKeyOk)
    {
        dump_start(ops);
        return true;
    }
    // Chip detected (flash dump only): OK starts the dump.
    if (phase == StFound && event->type == InputTypeShort
        && event->key == InputKeyOk)
    {
        dump_start(ops);
        return true;
    }
    // After a result, OK re-runs: RAM back to its ready prompt (no probe),
    // everything else from the detection gate.
    if ((phase == StDone || phase == StError) && event->type == InputTypeShort
        && event->key == InputKeyOk)
    {
        if (ops->op == OpDumpRam)
        {
            enter_ram_ready(ops); // ready prompt, no probe
        }
        else if (ops->op == OpVerify || ops->op == OpWriteFull
                 || ops->op == OpWriteApp)
        {
            enter_confirm(ops); // back to the file name + SHA confirm, no probe
        }
        else
        {
            enter_search(ops); // flash dump: detection gate
        }
        return true;
    }
    return false;
}

// --- public entry points ---------------------------------------------------

void
pic_ops_start_dump_flash (PicOps *ops)
{
    ops->op          = OpDumpFlash;
    ops->job         = JobCode;
    ops->verify_only = false;
    enter_search(ops);
}

void
pic_ops_start_dump_ram (PicOps *ops)
{
    ops->op          = OpDumpRam;
    ops->job         = JobRam;
    ops->verify_only = false;
    enter_ram_ready(ops); // no probe on entry — capture on OK (see helper)
}

void
pic_ops_start_write (PicOps *ops, bool keep_boot)
{
    if (ops->worker_active)
    {
        return;
    }
    if (!dump_store_pick_bin(
            ops->dialogs, ops->write_path, sizeof(ops->write_path)))
    {
        return;
    }
    ops->op          = keep_boot ? OpWriteApp : OpWriteFull;
    ops->keep_boot   = keep_boot;
    ops->verify_only = false;
    // Hash the image up front so the confirm gate can show it before erasing.
    if (!dump_store_hash_sha256(
            ops->storage, ops->write_path, ops->confirm_sha))
    {
        strncpy(
            ops->confirm_sha, "hash unavailable", sizeof(ops->confirm_sha) - 1);
        ops->confirm_sha[sizeof(ops->confirm_sha) - 1] = '\0';
    }
    enter_confirm(ops); // hold-to-write gate (no probe; the write worker checks
                        // the device ID)
}

void
pic_ops_start_verify (PicOps *ops)
{
    if (ops->worker_active)
    {
        return;
    }
    if (!dump_store_pick_bin(
            ops->dialogs, ops->write_path, sizeof(ops->write_path)))
    {
        return;
    }
    ops->op          = OpVerify;
    ops->keep_boot   = false;
    ops->verify_only = true;
    // Hash the image so the confirm screen shows the file name + SHA to check.
    if (!dump_store_hash_sha256(
            ops->storage, ops->write_path, ops->confirm_sha))
    {
        strncpy(
            ops->confirm_sha, "hash unavailable", sizeof(ops->confirm_sha) - 1);
        ops->confirm_sha[sizeof(ops->confirm_sha) - 1] = '\0';
    }
    enter_confirm(ops); // shows name + SHA; OK verifies (no probe)
}

PicOps *
pic_ops_alloc (Storage        *storage,
               DialogsApp     *dialogs,
               ViewDispatcher *view_dispatcher,
               uint32_t        view_id)
{
    PicOps *ops = malloc(sizeof(PicOps));
    memset(ops, 0, sizeof(PicOps));
    ops->storage         = storage;
    ops->dialogs         = dialogs;
    ops->view_dispatcher = view_dispatcher;
    ops->view_id         = view_id;
    ops->dump_view       = view_alloc();
    view_allocate_model(ops->dump_view, ViewModelTypeLocking, sizeof(AppState));
    {
        AppState *st = view_get_model(ops->dump_view);
        memset(st, 0, sizeof(AppState));
        st->phase = StIdle;
        view_commit_model(ops->dump_view, false);
    }
    view_set_context(ops->dump_view, ops);
    view_set_draw_callback(ops->dump_view, dump_draw_cb);
    view_set_input_callback(ops->dump_view, dump_input_cb);
    ops->anim_timer
        = furi_timer_alloc(anim_timer_cb, FuriTimerTypePeriodic, ops);
    return ops;
}

View *
pic_ops_get_view (PicOps *ops)
{
    return ops->dump_view;
}

void
pic_ops_free (PicOps *ops)
{
    furi_timer_stop(ops->anim_timer);
    furi_timer_free(ops->anim_timer);
    if (ops->worker)
    {
        ops->cancel = true;
        furi_thread_join(ops->worker);
        furi_thread_free(ops->worker);
    }
    view_free(ops->dump_view);
    free(ops);
}
