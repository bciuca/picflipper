// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca).

// Dump engine: device-ID probe + code-memory read loop over ICSP.
// PIC18F67J60: code memory 0x000000-0x01FFFF, DEVID at 0x3FFFFE.
#include "pic_dump.h"
#include <furi.h>

#define PIC_DEVID_ADDR 0x3FFFFEUL // DEVID1:DEVID2 (DS39688D 5.1)

// Reads the 2-byte device ID. Assumes Program/Verify mode is already entered.
// Returns (DEVID2 << 8) | DEVID1. For PIC18F67J60: high byte 0x1F, low byte
// 0b001x xxxx (low 5 bits = revision). 0xFFFF/0x0000 => no usable device.
uint16_t
pic_read_device_id (PicIcsp *d)
{
    pic_icsp_set_tblptr(d, PIC_DEVID_ADDR);
    uint8_t lo = pic_icsp_table_read(d); // DEVID1 @ 0x3FFFFE
    uint8_t hi = pic_icsp_table_read(d); // DEVID2 @ 0x3FFFFF
    return ((uint16_t)hi << 8) | lo;
}
