// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#pragma once
#include <stdint.h>
typedef enum
{
    StIdle, // resting/default phase: zero-init at alloc, cancelled ops land
            // here
    StSearching, // flash view: probing for the chip, spinner animating
    StFound,     // flash view: chip detected, show device ID + OK-to-dump
    StConnecting,
    StDumping,
    StConfirm, // write flow: warning + hold-OK gate before erasing
    StWriting, // write flow: staged erase/program/verify progress
    StDone,
    StError,
} AppPhase;
typedef struct
{
    AppPhase phase;
    char     title[24]; // screen header: op name ("Write Flash (full)", ...)
    uint32_t bytes_done;
    uint32_t bytes_total;
    uint16_t device_id;
    char     stage[16]; // write flow: "Erasing"/"Writing"/"Verifying"
    char     msg[64];   // result text, filename, or confirm detail
    char     sha[65]; // confirm gate: lowercase hex SHA-256 of the .bin to burn
    uint8_t  spin;    // StSearching: spinner frame counter
    float    hold; // StConfirm: hold-to-write progress (0..1), fills over 5 s
} AppState;
