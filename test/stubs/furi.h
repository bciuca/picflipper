// SPDX-License-Identifier: MIT
// Host test stub for <furi.h>. Provides only what the pure-logic units pull in
// transitively: standard integer/string headers and the two furi_delay_*
// helpers (no-ops on the host; timing is out of test scope).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static inline void
furi_delay_us (uint32_t us)
{
    (void)us;
}
static inline void
furi_delay_ms (uint32_t ms)
{
    (void)ms;
}
