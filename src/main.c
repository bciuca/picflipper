// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <string.h>
#include <stdlib.h>

#include "view_pins.h"
#include "view_console.h"
#include "view_about.h"
#include "dump_store.h"
#include "pic_ops.h"

#define TAG "picflipper"

typedef enum
{
    PicViewMenu,
    PicViewDump,
    PicViewPins,
    PicViewConsole,
    PicViewAbout,
} PicViewId;

typedef enum
{
    MenuDumpCode,
    MenuDumpRam,
    MenuWriteFull, // bulk-erase + program whole chip from .bin (also = restore)
    MenuWriteApp,  // row-erase + program app range only, keep bootloader region
    MenuVerify,    // read-back compare a .bin against the chip (no erase/write)
    MenuPins,
    MenuConsole,
    MenuAbout,
} MenuId;

typedef struct
{
    Gui            *gui;
    ViewDispatcher *view_dispatcher;
    Submenu        *submenu;
    PicPinsView    *pins;
    PicConsoleView *console;
    PicAboutView   *about;
    PicOps         *ops;
    Storage        *storage;
    DialogsApp     *dialogs;
} App;

// --- navigation ---
static uint32_t
to_menu (void *ctx)
{
    UNUSED(ctx);
    return PicViewMenu;
}

static bool
nav_event_cb (void *ctx)
{
    UNUSED(ctx);
    return false; // Back at the menu -> exit the dispatcher
}

static void
menu_cb (void *ctx, uint32_t index)
{
    App *app = ctx;
    switch (index)
    {
        case MenuDumpCode:
            pic_ops_start_dump_flash(app->ops);
            break;
        case MenuDumpRam:
            pic_ops_start_dump_ram(app->ops);
            break;
        case MenuWriteFull:
            pic_ops_start_write(app->ops, false);
            break;
        case MenuWriteApp:
            pic_ops_start_write(app->ops, true);
            break;
        case MenuVerify:
            pic_ops_start_verify(app->ops);
            break;
        case MenuPins:
            view_dispatcher_switch_to_view(app->view_dispatcher, PicViewPins);
            break;
        case MenuConsole:
            view_dispatcher_switch_to_view(app->view_dispatcher,
                                           PicViewConsole);
            break;
        case MenuAbout:
            view_dispatcher_switch_to_view(app->view_dispatcher, PicViewAbout);
            break;
        default:
            break;
    }
}

int32_t
picflipper_app (void *p)
{
    UNUSED(p);
    FURI_LOG_I(TAG, "PICFlipper starting");

    App *app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->gui     = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher,
                                                  nav_event_cb);

    // main menu
    app->submenu = submenu_alloc();
    submenu_add_item(
        app->submenu, "Dump flash image", MenuDumpCode, menu_cb, app);
    submenu_add_item(
        app->submenu, "Dump RAM snapshot", MenuDumpRam, menu_cb, app);
    submenu_add_item(
        app->submenu, "Write full image", MenuWriteFull, menu_cb, app);
    submenu_add_item(
        app->submenu, "Write app data (keep boot)", MenuWriteApp, menu_cb, app);
    submenu_add_item(
        app->submenu, "Verify image on PIC", MenuVerify, menu_cb, app);
    submenu_add_item(app->submenu, "Wiring", MenuPins, menu_cb, app);
    submenu_add_item(app->submenu, "Debug console", MenuConsole, menu_cb, app);
    submenu_add_item(app->submenu, "About", MenuAbout, menu_cb, app);
    view_dispatcher_add_view(
        app->view_dispatcher, PicViewMenu, submenu_get_view(app->submenu));

    // dump/write/verify operations + their view
    app->ops = pic_ops_alloc(
        app->storage, app->dialogs, app->view_dispatcher, PicViewDump);
    view_set_previous_callback(pic_ops_get_view(app->ops), to_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, PicViewDump, pic_ops_get_view(app->ops));

    // pin-status + console views
    app->pins = pic_pins_view_alloc();
    view_set_previous_callback(pic_pins_view_get_view(app->pins), to_menu);
    view_dispatcher_add_view(
        app->view_dispatcher, PicViewPins, pic_pins_view_get_view(app->pins));

    app->console = pic_console_view_alloc();
    view_set_previous_callback(pic_console_view_get_view(app->console),
                               to_menu);
    view_dispatcher_add_view(app->view_dispatcher,
                             PicViewConsole,
                             pic_console_view_get_view(app->console));

    // About page
    app->about = pic_about_view_alloc();
    view_set_previous_callback(pic_about_view_get_view(app->about), to_menu);
    view_dispatcher_add_view(app->view_dispatcher,
                             PicViewAbout,
                             pic_about_view_get_view(app->about));

    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, PicViewMenu);
    view_dispatcher_run(app->view_dispatcher);

    // cleanup
    view_dispatcher_remove_view(app->view_dispatcher, PicViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, PicViewDump);
    view_dispatcher_remove_view(app->view_dispatcher, PicViewPins);
    view_dispatcher_remove_view(app->view_dispatcher, PicViewConsole);
    view_dispatcher_remove_view(app->view_dispatcher, PicViewAbout);
    submenu_free(app->submenu);
    pic_ops_free(app->ops);
    pic_pins_view_free(app->pins);
    pic_console_view_free(app->console);
    pic_about_view_free(app->about);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);

    FURI_LOG_I(TAG, "PICFlipper exit");
    return 0;
}
