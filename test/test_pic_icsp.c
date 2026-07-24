// SPDX-License-Identifier: MIT
// Host unit tests for the ICSP bit/command encoding in pic_icsp.c. Real GPIO is
// replaced by a tracer: every PGC low->high edge samples PGD and records one
// transmitted bit (only while PGD is a driven output); reads are served from a
// preloaded queue. This exercises the DS39688D command/payload bit order and
// opcodes without any hardware or timing.
//
// Bit-order references (DS39688D):
//   - 4-bit commands and 16-bit payloads shift LSb first.
//   - The 32-bit Program/Verify entry key 0x4D434850 ("MCHP") shifts MSb first.
//   - TBLRD*+ = 1001, Shift-out-TABLAT = 0010, TBLWT = 1100 / 1101 / 1111.
#include "tap.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <furi_hal_gpio.h> // GpioMode/GpioPin/etc. the tracer below defines

// --- GPIO tracer (satisfies stubs/furi_hal_gpio.h) --------------------------
struct GpioPin
{
    int id;
};
static struct GpioPin PGC  = {1};
static struct GpioPin PGD  = {2};
static struct GpioPin MCLR = {3};

static bool    g_pgc;               // last PGC level
static bool    g_pgd;               // last PGD level
static bool    g_pgd_output = true; // PGD direction (true = driven output)
static uint8_t g_tx[512];           // sampled transmitted bits (driven only)
static int     g_tx_n;
static uint8_t g_rx[64]; // queued bits for reads
static int     g_rx_n, g_rx_i;

static void
trace_reset (void)
{
    g_tx_n       = 0;
    g_rx_n       = 0;
    g_rx_i       = 0;
    g_pgc        = false;
    g_pgd        = false;
    g_pgd_output = true;
}
static void
rx_load (const uint8_t *bits, int n)
{
    memcpy(g_rx, bits, (size_t)n);
    g_rx_n = n;
    g_rx_i = 0;
}

void
furi_hal_gpio_init (const GpioPin *g, GpioMode m, GpioPull p, GpioSpeed s)
{
    (void)p;
    (void)s;
    if (g == &PGD)
    {
        g_pgd_output = (m == GpioModeOutputPushPull);
    }
}
void
furi_hal_gpio_write (const GpioPin *g, bool level)
{
    if (g == &PGD)
    {
        g_pgd = level;
    }
    else if (g == &PGC)
    {
        // Rising edge latches the bit currently on PGD (if PGD is driving).
        if (level && !g_pgc && g_pgd_output && g_tx_n < (int)sizeof(g_tx))
        {
            g_tx[g_tx_n++] = g_pgd ? 1 : 0;
        }
        g_pgc = level;
    }
    else if (g == &MCLR)
    {
        // not traced
    }
}
bool
furi_hal_gpio_read (const GpioPin *g)
{
    if (g == &PGD && g_rx_i < g_rx_n)
    {
        return g_rx[g_rx_i++] != 0;
    }
    return false;
}

#include "pic_icsp.c"

// Reassemble a run of traced bits, LSb first, into an integer.
static uint32_t
bits_lsb (const uint8_t *b, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
    {
        v |= (uint32_t)(b[i] & 1) << i;
    }
    return v;
}

static PicIcsp DEV = {.pgc = &PGC, .pgd = &PGD, .mclr = &MCLR};

int
main (void)
{
    // --- 4-bit command, LSb first -------------------------------------------
    trace_reset();
    pic_icsp_cmd4(&DEV, 0b1001); // TBLRD*+
    CHECK(g_tx_n == 4 && g_tx[0] == 1 && g_tx[1] == 0 && g_tx[2] == 0 &&
              g_tx[3] == 1,
          "cmd4(1001) shifts LSb first -> 1,0,0,1");

    trace_reset();
    pic_icsp_cmd4(&DEV, 0b1100); // TBLWT
    CHECK(g_tx_n == 4 && g_tx[0] == 0 && g_tx[1] == 0 && g_tx[2] == 1 &&
              g_tx[3] == 1,
          "cmd4(1100) shifts LSb first -> 0,0,1,1");

    // --- 16-bit payload, LSb first ------------------------------------------
    trace_reset();
    pic_icsp_payload16(&DEV, 0xABCD);
    CHECK(g_tx_n == 16 && bits_lsb(g_tx, 16) == 0xABCD,
          "payload16(0xABCD) reassembles LSb first");

    // --- core instruction = cmd 0000 + 16-bit payload -----------------------
    trace_reset();
    pic_icsp_core_inst(&DEV, 0x6EF8); // MOVWF TBLPTRU
    CHECK(g_tx_n == 20 && bits_lsb(g_tx, 4) == 0x0 &&
              bits_lsb(g_tx + 4, 16) == 0x6EF8,
          "core_inst(0x6EF8): nibble 0000 then payload LSb first");

    // --- TBLPTR load sequence (DS39688D Table 4-1 Step 1) -------------------
    trace_reset();
    pic_icsp_set_tblptr(&DEV, 0x1F2340);
    {
        const uint16_t exp[6] = {
            0x0E1F, // MOVLW Addr[21:16]
            0x6EF8, // MOVWF TBLPTRU
            0x0E23, // MOVLW Addr[15:8]
            0x6EF7, // MOVWF TBLPTRH
            0x0E40, // MOVLW Addr[7:0]
            0x6EF6, // MOVWF TBLPTRL
        };
        int ok = (g_tx_n == 6 * 20);
        for (int k = 0; k < 6 && ok; k++)
        {
            const uint8_t *ins = g_tx + k * 20;
            ok                 = ok && bits_lsb(ins, 4) == 0x0 &&
                                 bits_lsb(ins + 4, 16) == exp[k];
        }
        CHECK(ok, "set_tblptr(0x1F2340): six MOVLW/MOVWF core insts, in order");
    }

    // --- Table-write opcode variants ----------------------------------------
    trace_reset();
    pic_icsp_tblwt(&DEV, 0x1234);
    CHECK(bits_lsb(g_tx, 4) == 0b1100 // TBLWT
              && bits_lsb(g_tx + 4, 16) == 0x1234,
          "tblwt: cmd 1100, payload LSb first");

    trace_reset();
    pic_icsp_tblwt_postinc(&DEV, 0x5678);
    CHECK(bits_lsb(g_tx, 4) == 0b1101 // TBLWT, post-increment
              && bits_lsb(g_tx + 4, 16) == 0x5678,
          "tblwt_postinc: cmd 1101");

    trace_reset();
    pic_icsp_tblwt_start(&DEV, 0x9ABC);
    CHECK(bits_lsb(g_tx, 4) == 0b1111 // 1111
              && bits_lsb(g_tx + 4, 16) == 0x9ABC,
          "tblwt_start: cmd 1111");

    // --- Table read: cmd 1001, then 8 driven dummy clocks; byte read LSb->MSb
    trace_reset();
    {
        uint8_t val = 0x5A;
        uint8_t bits[8];
        for (int i = 0; i < 8; i++)
        {
            bits[i] = (val >> i) & 1; // LSb first, as the target presents it
        }
        rx_load(bits, 8);
        uint8_t got = pic_icsp_table_read(&DEV);
        CHECK(got == 0x5A, "table_read reassembles LSb-first data byte 0x5A");
        CHECK(bits_lsb(g_tx, 4) == 0b1001,
              "table_read issues cmd 1001 (TBLRD*+)");
        CHECK(g_tx_n == 12, "table_read drives 4 cmd + 8 dummy clocks");
    }

    // --- Live data read: MOVLB / MOVF / MOVWF TABLAT then shift-out (0010) ---
    trace_reset();
    {
        uint8_t val = 0x3C;
        uint8_t bits[8];
        for (int i = 0; i < 8; i++)
        {
            bits[i] = (val >> i) & 1;
        }
        rx_load(bits, 8);
        uint8_t got = pic_icsp_read_data(&DEV, 0x0A5);
        // Three setup core instructions.
        CHECK(bits_lsb(g_tx + 0 * 20 + 4, 16) == 0x0100, // MOVLB 0
              "read_data: MOVLB k for bank 0");
        CHECK(bits_lsb(g_tx + 1 * 20 + 4, 16) == 0x51A5, // MOVF f,W,BANKED
              "read_data: MOVF f,W,BANKED for f=0xA5");
        CHECK(bits_lsb(g_tx + 2 * 20 + 4, 16) == 0x6EF5, // MOVWF TABLAT
              "read_data: MOVWF TABLAT");
        // Shift-out command nibble 0010.
        CHECK(bits_lsb(g_tx + 3 * 20, 4) == 0b0010, // Shift-out TABLAT
              "read_data: shift-out-TABLAT cmd 0010");
        CHECK(got == 0x3C, "read_data reassembles LSb-first byte 0x3C");
    }

    // --- Entry key: 32 bits, MSb first, value 0x4D434850 ("MCHP") -----------
    trace_reset();
    pic_icsp_enter(&DEV);
    {
        uint32_t key = 0;
        int      ok  = (g_tx_n == 32); // enter clocks exactly the 32 key bits
        for (int i = 0; i < 32 && ok; i++)
        {
            key |= (uint32_t)(g_tx[i] & 1) << (31 - i); // MSb first
        }
        CHECK(ok && key == 0x4D434850UL,
              "pic_icsp_enter clocks the 32-bit key MSb first (0x4D434850)");
    }

    return tap_done("pic_icsp");
}
