// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

#pragma once
#include "app_types.h"
#include <gui/gui.h>
// Renders AppState. main.c owns the AppState, input routing, and the
// GUI/viewport lifecycle; app_ui.c provides the draw callback only.
void app_ui_draw(Canvas *canvas, const AppState *st);
