// SPDX-License-Identifier: MIT
// Host test stub for <furi_hal_resources.h>. Only declares the board GpioPin
// externs referenced by pic_config.h's PIC_PIN_* macros. Those macros are not
// expanded by any unit under test, so no definitions are needed.
#pragma once
#include <furi_hal_gpio.h>

extern const GpioPin gpio_ext_pa7;
extern const GpioPin gpio_ext_pa6;
extern const GpioPin gpio_ext_pb3;
