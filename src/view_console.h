// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

#pragma once
#include <gui/view.h>

// "Debug console" view. While active it (a) forwards the target PIC's debug
// UART (wire PIC RC6/pin31 -> Flipper pin14 USART RX) into the FURI log stream,
// and (b) shows status. The app's own diagnostics are logged globally
// regardless, so the console works (via `ufbt cli` -> `log`) even with no PIC
// wire attached.
typedef struct PicConsoleView PicConsoleView;

PicConsoleView *pic_console_view_alloc(void);
View           *pic_console_view_get_view(PicConsoleView *c);
void            pic_console_view_free(PicConsoleView *c);
