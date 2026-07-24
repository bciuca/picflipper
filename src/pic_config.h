// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

#pragma once
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#define PIC_PIN_PGC  (&gpio_ext_pa7)
#define PIC_PIN_PGD  (&gpio_ext_pa6)
#define PIC_PIN_MCLR (&gpio_ext_pb3)
// Bit-bang half-period delays (µs): T_CLK = PGC-high, T_HOLD =
// PGC-low/data-hold, applied to every shifted bit (pic_clock_bit). Datasheet
// minimums are tens of ns (P1A/P1B >=40 ns, P3/P4 setup/hold >=15 ns), so 2/1
// is ~50x the floor — conservatively safe for short (~4 in) wiring.
//
// The erase/write paths are verify-guarded, so too-tight timing surfaces as a
// loud "erase/verify fail" you re-run, not a silent brick: bulk erase is
// checked in-session with a 4x retry and ABORTS before any write if it doesn't
// take (pic_program.c), and the program path verifies per-block plus a final
// full verify. If a flash fails verify at 2/1, the wires are marginal -> raise
// to 8/4.
#define PIC_T_CLK_US  2
#define PIC_T_HOLD_US 1

// --- Write/erase timing (DS39688D §6 AC Programming Characteristics) ---------
// Datasheet minimums: P9=3.4 ms, P10=49 ms, P11=475 ms. Over-waiting is
// harmless; under-waiting silently corrupts flash, so these are deliberately
// generous. Tighten only after a verified successful write on real hardware.
#define PIC_P9_US \
    5000UL // block program time, per 64-byte block (min 3.4 ms; 1.47x margin,
           // per-block verified)
#define PIC_P10_US 60000UL  // row-erase time, per 1024-byte row    (min 49 ms)
#define PIC_P11_US 600000UL // bulk-erase time, whole device        (min 475 ms)

// --- Flash geometry (DS39688D §2.2, §3.2) -----------------------------------
#define PIC_WRITE_BLOCK 64U   // program write-buffer size (bytes / 32 words)
#define PIC_ERASE_ROW   1024U // row-erase granularity (bytes)

// --- Code-protect guard (DS39688D Table 5-2 / §8.3) -------------------------
// The Flash Config Words sit at 0x1FFF8-0x1FFFF (128 KB part). CONFIG1H is at
// 0x1FFF9; its bit 2 is CP0 (1 = NOT code-protected, 0 = protected). Writing an
// image with CP0=0 silently enables code-protect (in-session verify still
// passes; CP only bites after a device reset) and locks out future reads. We
// refuse such a write rather than brick the part.
#define PIC_CONFIG1H_ADDR 0x01FFF9UL
#define PIC_CP0_MASK      0x04U // CONFIG1H bit 2; set => code-protect OFF

// --- Application / bootloader split (for "keep bootloader" region writes) ----
// App range for region writes is [0 .. PIC_BOOT_REGION_START-1]; the boot
// region [PIC_BOOT_REGION_START .. PIC_BOOT_REGION_END] is left untouched.
// Full-chip writes ignore this entirely.
//
// Verified for PIC18F67J60, DEVID 0x1F23 from a full dump:
// the reset vector at 0x0000 is GOTO 0x01E800, the app ends ~0x01DBDC, and the
// bootloader code starts at exactly 0x01E800 (row-aligned) after an all-0xFF
// gap; the boot region runs to ~0x01FA79, then the Config Words at 0x1FFF8
// (also kept). NOTE: keep-boot reprograms the low region including the reset
// vector at 0x0000 — the replacement app image MUST keep 0x0000 = GOTO 0x01E800
// (and honor the bootloader's vector-remap contract) or the bootloader is
// silently bypassed. Re-derive START for a different firmware/layout before
// relying on Keep-boot.
#define PIC_BOOT_REGION_START 0x01E800UL
#define PIC_BOOT_REGION_END   0x01FFFFUL

// Keep-boot row-erases [0 .. PIC_BOOT_REGION_START-1]; row erase is 1024-byte
// granular, so START must sit on a row boundary or the last erased row would
// clip into the bootloader's first row and wipe part of it.
_Static_assert(
    (PIC_BOOT_REGION_START % PIC_ERASE_ROW) == 0,
    "PIC_BOOT_REGION_START must be a multiple of PIC_ERASE_ROW (1024)");
