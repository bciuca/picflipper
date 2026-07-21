// SPDX-License-Identifier: MIT
// Host test stub for <input/input.h>. Enough of the input event model to let
// view_about.c compile; the input callback is never exercised by the tests.
#pragma once

typedef enum
{
    InputTypePress,
    InputTypeRelease,
    InputTypeShort,
    InputTypeLong,
    InputTypeRepeat,
} InputType;

typedef enum
{
    InputKeyUp,
    InputKeyDown,
    InputKeyRight,
    InputKeyLeft,
    InputKeyOk,
    InputKeyBack,
} InputKey;

typedef struct
{
    InputType type;
    InputKey  key;
} InputEvent;
