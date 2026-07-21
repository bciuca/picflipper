// SPDX-License-Identifier: MIT
// Host test stub for <furi_hal_gpio.h>. GpioPin is opaque (product code only
// ever handles pointers). The init/write/read functions are declared here and
// defined by the GPIO-tracing test (test_pic_icsp.c).
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct GpioPin GpioPin;

typedef enum
{
    GpioModeInput,
    GpioModeOutputPushPull,
    GpioModeOutputOpenDrain,
} GpioMode;

typedef enum
{
    GpioPullNo,
    GpioPullUp,
    GpioPullDown,
} GpioPull;

typedef enum
{
    GpioSpeedLow,
    GpioSpeedMedium,
    GpioSpeedHigh,
} GpioSpeed;

void furi_hal_gpio_init(const GpioPin *g, GpioMode m, GpioPull p, GpioSpeed s);
void furi_hal_gpio_write(const GpioPin *g, bool level);
bool furi_hal_gpio_read(const GpioPin *g);
