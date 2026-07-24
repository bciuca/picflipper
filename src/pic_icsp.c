// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

// Bit-bang low-voltage ICSP for the PIC18F97J60 family (read path only).
// Every protocol detail here is confirmed against DS39688D. Key points:
// commands & data are LSb-first; the 32-bit Program/Verify entry key
// 0x4D434850 ("MCHP") is MSb-first. No 12V (J-series is low-voltage;
// VIH=0.8*VDD).
#include "pic_icsp.h"
#include "pic_config.h"
#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>

#define PIC_ENTRY_KEY \
    0x4D434850UL // DS39688D Fig 2-7, "MCHP", shifted MSb first

// One PGC clock pulse while PGD already holds the bit to be latched.
// DS39688D 2.5: data transmitted on PGC rising edge, latched on falling edge.
static inline void
pic_clock_bit (PicIcsp *d, bool bit)
{
    furi_hal_gpio_write(d->pgd, bit);
    furi_hal_gpio_write(d->pgc, true);
    furi_delay_us(PIC_T_CLK_US);
    furi_hal_gpio_write(d->pgc, false);
    furi_delay_us(PIC_T_HOLD_US);
}

void
pic_icsp_init (PicIcsp *d)
{
    furi_hal_gpio_init(
        d->pgc, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(
        d->pgd, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(
        d->mclr, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_write(d->pgc, false);
    furi_hal_gpio_write(d->pgd, false);
    furi_hal_gpio_write(d->mclr, false);
}

void
pic_icsp_enter (PicIcsp *d)
{
    // VDD is already present on the Flipper 3V3 rail. Start from idle.
    furi_hal_gpio_write(d->pgc, false);
    furi_hal_gpio_write(d->pgd, false);

    // Step 1: briefly raise MCLR to VIH, then drop it (DS39688D 2.4, Fig 2-7).
    furi_hal_gpio_write(d->mclr, true);
    furi_delay_us(10);
    furi_hal_gpio_write(d->mclr, false);

    // Step 2: wait >= P19 (1 ms), then clock the 32-bit key, MSb first.
    furi_delay_ms(2);
    for (int i = 31; i >= 0; i--)
    {
        pic_clock_bit(d, (PIC_ENTRY_KEY >> i) & 1u);
    }
    furi_hal_gpio_write(d->pgd, false);

    // Step 3: wait >= P20 (40 ns), reapply VIH to MCLR and hold, then wait
    // >= P12 (400 us) before any command/data is presented on PGD.
    furi_delay_us(50);
    furi_hal_gpio_write(d->mclr, true);
    furi_delay_us(500);
}

void
pic_icsp_exit (PicIcsp *d)
{
    // P16 (>= 20 ns) between last PGC low and dropping MCLR.
    furi_delay_us(PIC_T_HOLD_US);
    furi_hal_gpio_write(d->mclr, false);
    // Release everything to high-impedance.
    furi_hal_gpio_init(d->pgc, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(d->pgd, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_init(d->mclr, GpioModeInput, GpioPullNo, GpioSpeedLow);
}

void
pic_icsp_cmd4 (PicIcsp *d, uint8_t cmd4)
{
    for (int i = 0; i < 4; i++)
    {
        pic_clock_bit(d, (cmd4 >> i) & 1u); // LSb first
    }
}

void
pic_icsp_payload16 (PicIcsp *d, uint16_t data)
{
    for (int i = 0; i < 16; i++)
    {
        pic_clock_bit(d, (data >> i) & 1u); // LSb first
    }
}

void
pic_icsp_core_inst (PicIcsp *d, uint16_t inst)
{
    pic_icsp_cmd4(d, 0b0000); // core instruction
    pic_icsp_payload16(d, inst);
}

void
pic_icsp_set_tblptr (PicIcsp *d, uint32_t addr)
{
    // DS39688D Table 4-1 Step 1: load TBLPTRU:H:L via MOVLW/MOVWF core insts.
    pic_icsp_core_inst(d, 0x0E00 | ((addr >> 16) & 0xFF)); // MOVLW Addr[21:16]
    pic_icsp_core_inst(d, 0x6EF8);                         // MOVWF TBLPTRU
    pic_icsp_core_inst(d, 0x0E00 | ((addr >> 8) & 0xFF));  // MOVLW Addr[15:8]
    pic_icsp_core_inst(d, 0x6EF7);                         // MOVWF TBLPTRH
    pic_icsp_core_inst(d, 0x0E00 | (addr & 0xFF));         // MOVLW Addr[7:0]
    pic_icsp_core_inst(d, 0x6EF6);                         // MOVWF TBLPTRL
}

uint8_t
pic_icsp_table_read (PicIcsp *d)
{
    // DS39688D 4.1 / Fig 4-1: cmd 1001 (TBLRD *+), then a 16-clock operand
    // window.
    pic_icsp_cmd4(d, 0b1001);

    // First 8 clocks: programmer drives a don't-care byte (PGD = output, 0).
    furi_hal_gpio_write(d->pgd, false);
    for (int i = 0; i < 8; i++)
    {
        furi_hal_gpio_write(d->pgc, true);
        furi_delay_us(PIC_T_CLK_US);
        furi_hal_gpio_write(d->pgc, false);
        furi_delay_us(PIC_T_HOLD_US);
    }

    // P6 turnaround: PGC low, release PGD so the target can drive it.
    furi_hal_gpio_init(d->pgd, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_delay_us(PIC_T_HOLD_US);

    // Last 8 clocks: target drives PGD; data appears LSb -> MSb.
    // Data valid P14 (>=10 ns) after PGC rising edge; sample while high.
    uint8_t value = 0;
    for (int i = 0; i < 8; i++)
    {
        furi_hal_gpio_write(d->pgc, true);
        furi_delay_us(PIC_T_CLK_US);
        if (furi_hal_gpio_read(d->pgd))
        {
            value |= (uint8_t)(1u << i); // LSb first
        }
        furi_hal_gpio_write(d->pgc, false);
        furi_delay_us(PIC_T_HOLD_US);
    }

    // Restore PGD to a driven idle-low output for the next command.
    furi_hal_gpio_init(
        d->pgd, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_write(d->pgd, false);
    return value;
}

// Shift Out TABLAT Register (DS39688D Table 4-1, cmd 0010). Same 16-clock
// operand window as a table read: 8 programmer-driven don't-care clocks, P6
// turnaround, then 8 data clocks LSb->MSb. Reads whatever core instructions
// last loaded into TABLAT.
static uint8_t
pic_icsp_shift_out_tablat (PicIcsp *d)
{
    pic_icsp_cmd4(d, 0b0010);

    furi_hal_gpio_write(d->pgd, false);
    for (int i = 0; i < 8; i++)
    {
        furi_hal_gpio_write(d->pgc, true);
        furi_delay_us(PIC_T_CLK_US);
        furi_hal_gpio_write(d->pgc, false);
        furi_delay_us(PIC_T_HOLD_US);
    }

    furi_hal_gpio_init(d->pgd, GpioModeInput, GpioPullNo, GpioSpeedLow);
    furi_delay_us(PIC_T_HOLD_US);

    uint8_t value = 0;
    for (int i = 0; i < 8; i++)
    {
        furi_hal_gpio_write(d->pgc, true);
        furi_delay_us(PIC_T_CLK_US);
        if (furi_hal_gpio_read(d->pgd))
        {
            value |= (uint8_t)(1u << i); // LSb first
        }
        furi_hal_gpio_write(d->pgc, false);
        furi_delay_us(PIC_T_HOLD_US);
    }

    furi_hal_gpio_init(
        d->pgd, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
    furi_hal_gpio_write(d->pgd, false);
    return value;
}

uint8_t
pic_icsp_read_data (PicIcsp *d, uint16_t addr)
{
    // Read one data-memory byte (GPR/SFR file register) from the *current*
    // SRAM, via core instructions: select bank, copy the byte into WREG then
    // TABLAT, shift it out. PIC18 GPR SRAM is preserved across the MCLR-based
    // Program/Verify entry (MCLR reset leaves file registers unchanged; only
    // POR clears them), so for addr in GPR space this returns the running
    // firmware's pre-entry value — a live snapshot.
    //   0x010k = MOVLB  k            (k = addr[11:8], bank select)
    //   0x51ff = MOVF   f, W, BANKED (f = addr[7:0]; d=0 -> W, a=1 -> use BSR)
    //   0x6EF5 = MOVWF  TABLAT       (access bank, TABLAT @ 0xFF5)
    pic_icsp_core_inst(d, 0x0100 | ((addr >> 8) & 0x0F));
    pic_icsp_core_inst(d, 0x5100 | (addr & 0xFF));
    pic_icsp_core_inst(d, 0x6EF5);
    return pic_icsp_shift_out_tablat(d);
}

// --- Write side -------------------------------------------------------------

// Block while holding the current pin levels. furi_delay_ms yields to the
// scheduler for the long (ms-scale) program/erase waits without dropping the
// latched PGC/PGD levels; the µs remainder is busy-waited. Over-waiting is
// safe.
static void
pic_hold (uint32_t us)
{
    if (us >= 1000)
    {
        furi_delay_ms(us / 1000);
        us %= 1000;
    }
    if (us)
    {
        furi_delay_us(us);
    }
}

void
pic_icsp_tblwt (PicIcsp *d, uint16_t word)
{
    pic_icsp_cmd4(d, 0b1100); // Table Write (no increment)
    pic_icsp_payload16(d, word);
}

void
pic_icsp_tblwt_postinc (PicIcsp *d, uint16_t word)
{
    pic_icsp_cmd4(d, 0b1101); // Table Write, post-increment TBLPTR by 2
    pic_icsp_payload16(d, word);
}

void
pic_icsp_tblwt_start (PicIcsp *d, uint16_t word)
{
    pic_icsp_cmd4(d, 0b1111); // Table Write, Start Programming (no increment)
    pic_icsp_payload16(d, word);
}

void
pic_icsp_prog_nop (PicIcsp *d, uint32_t hold_us)
{
    // Core-instruction NOP (cmd 0000) whose 4th command clock is stretched: the
    // J-series self-times program/row-erase while the 4th PGC is held HIGH for
    // P9/P10 (DS39688D Figs 3-4/3-6). Bits 0..2 clock normally; the 4th is
    // held.
    for (int i = 0; i < 3; i++)
    {
        pic_clock_bit(d, false);
    }
    furi_hal_gpio_write(d->pgd, false);
    furi_hal_gpio_write(d->pgc, true); // 4th command clock HIGH ...
    pic_hold(hold_us);                 // ... held for the program/erase time
    furi_hal_gpio_write(d->pgc, false);
    furi_delay_us(PIC_T_HOLD_US);
    pic_icsp_payload16(d, 0x0000); // remainder of the NOP (16-bit operand)
}

void
pic_icsp_erase_nop (PicIcsp *d, uint32_t hold_us)
{
    // Bulk erase self-times (DS39688D Fig 3-2 / §3.1.1): the erase begins on
    // the 4th PGC FALLING edge of this NOP, after which PGD must be held LOW
    // until it completes (P11).
    for (int i = 0; i < 4; i++)
    {
        pic_clock_bit(d, false);
    }
    furi_hal_gpio_write(d->pgd, false); // hold low through the wait
    pic_hold(hold_us); // furi_delay_ms yields (no UI/watchdog starvation)
    pic_icsp_payload16(d, 0x0000); // remainder of the NOP (16-bit operand)
}
