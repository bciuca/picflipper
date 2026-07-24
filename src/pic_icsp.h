// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#pragma once
#include <furi_hal_gpio.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct
{
    const GpioPin *pgc;
    const GpioPin *pgd;
    const GpioPin *mclr;
} PicIcsp;
void pic_icsp_init(PicIcsp *d);  // configure GPIO directions, idle levels
void pic_icsp_enter(PicIcsp *d); // enter Program/Verify mode (see VERIFY #1)
void pic_icsp_exit(PicIcsp *d);  // release MCLR, tristate
void pic_icsp_cmd4(PicIcsp *d, uint8_t cmd4);        // 4-bit, LSB first
void pic_icsp_payload16(PicIcsp *d, uint16_t data);  // 16-bit, LSB first
void pic_icsp_core_inst(PicIcsp *d, uint16_t inst);  // cmd 0000 + 16-bit inst
void pic_icsp_set_tblptr(PicIcsp *d, uint32_t addr); // MOVLW/MOVWF TBLPTRU/H/L
uint8_t pic_icsp_table_read(PicIcsp *d); // TBLRD*+ , return data byte
uint8_t pic_icsp_read_data(PicIcsp *d,
                           uint16_t addr); // live data-RAM (GPR/SFR) byte read

// --- Write side (DS39688D §3, Table 2-3) ------------------------------------
void pic_icsp_tblwt(PicIcsp *d,
                    uint16_t word); // cmd 1100: Table Write (no inc)
void
pic_icsp_tblwt_postinc(PicIcsp *d,
                       uint16_t word); // cmd 1101: Table Write, TBLPTR += 2
void pic_icsp_tblwt_start(
    PicIcsp *d, uint16_t word); // cmd 1111: Table Write + Start Programming
// NOP whose 4th command-clock high phase is stretched to `hold_us` (4th PGC
// held HIGH). Self-times program (P9) and row erase (P10). DS39688D Figs
// 3-4/3-6.
void pic_icsp_prog_nop(PicIcsp *d, uint32_t hold_us);
// NOP for bulk erase: erase starts on the 4th PGC falling edge, then PGD is
// held LOW for `hold_us` while PGC idles (P11). DS39688D Fig 3-2 / §3.1.1.
void pic_icsp_erase_nop(PicIcsp *d, uint32_t hold_us);
