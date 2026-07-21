// SPDX-License-Identifier: MIT
// Host unit tests for the Intel-HEX / raw writer in dump_writer.c. The storage
// backend is stubbed to capture every written byte into an in-memory buffer, so
// full record output (data records, checksums, type-04 extended-linear-address
// records, EOF) can be compared against hand-computed expected text. Expected
// checksums are the two's complement of the byte sum, per the Intel HEX spec.
#include "tap.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <storage/storage.h> // File/Storage/FS_* types the stubs below define

// --- Capturing storage backend (satisfies stubs/storage/storage.h) ----------
static unsigned char g_cap[1 << 16];
static size_t        g_cap_len;

static void
cap_reset (void)
{
    g_cap_len = 0;
}
static const char *
cap_str (void)
{
    g_cap[g_cap_len] = '\0';
    return (const char *)g_cap;
}

struct File
{
    int _tag;
};
static struct File g_file;

File *
storage_file_alloc (Storage *s)
{
    (void)s;
    return &g_file;
}
bool
storage_file_open (File *f, const char *path, FS_AccessMode am, FS_OpenMode om)
{
    (void)f;
    (void)path;
    (void)am;
    (void)om;
    return true;
}
size_t
storage_file_write (File *f, const void *buf, size_t n)
{
    (void)f;
    memcpy(g_cap + g_cap_len, buf, n);
    g_cap_len += n;
    return n;
}
bool
storage_file_close (File *f)
{
    (void)f;
    return true;
}
void
storage_file_free (File *f)
{
    (void)f;
}
bool
storage_common_mkdir (Storage *s, const char *path)
{
    (void)s;
    (void)path;
    return true;
}

#include "dump_writer.c"

int
main (void)
{
    // --- Single records via the internal ihex_record() -----------------------
    cap_reset();
    ihex_record(&g_file, 0, 0x0000, 0x01, NULL); // EOF
    CHECK(strcmp(cap_str(), ":00000001FF\r\n") == 0, "EOF record");

    cap_reset();
    uint8_t ula[2] = { 0x00, 0x01 };
    ihex_record(&g_file, 2, 0x0000, 0x04, ula); // upper addr = 0x0001
    CHECK(strcmp(cap_str(), ":020000040001F9\r\n") == 0,
          "type-04 extended-linear-address record (upper 0x0001)");

    cap_reset();
    uint8_t data4[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    ihex_record(&g_file, 4, 0x0010, 0x00, data4);
    CHECK(strcmp(cap_str(), ":04001000DEADBEEFB4\r\n") == 0,
          "4-byte data record with checksum @ 0x0010");

    // --- Streaming writer: 20 bytes (0..19) at base 0 ------------------------
    // Expect: ULA(0) + one full 16-byte record @0x0000 + a 4-byte tail @0x0010
    // + EOF.
    {
        uint8_t buf[20];
        for (int i = 0; i < 20; i++)
        {
            buf[i] = (uint8_t)i;
        }
        cap_reset();
        DumpWriter w;
        bool       ok = dump_writer_open(&w, NULL, NULL, "d.hex", 0);
        ok            = dump_writer_append(&w, buf, sizeof(buf)) && ok;
        ok            = dump_writer_close(&w) && ok;
        const char *exp
            = ":020000040000FA\r\n"
              ":10000000000102030405060708090A0B0C0D0E0F78\r\n"
              ":0400100010111213A6\r\n"
              ":00000001FF\r\n";
        CHECK(ok, "streaming hex writer reports success");
        CHECK(strcmp(cap_str(), exp) == 0,
              "20-byte stream: ULA + 16B record + 4B tail + EOF");
    }

    // --- Streaming writer: upper-address change (base 0x10000) ---------------
    {
        uint8_t buf[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
        cap_reset();
        DumpWriter w;
        bool       ok = dump_writer_open(&w, NULL, NULL, "d.hex", 0x00010000);
        ok            = dump_writer_append(&w, buf, sizeof(buf)) && ok;
        ok            = dump_writer_close(&w) && ok;
        const char *exp = ":020000040001F9\r\n"
                          ":04000000AABBCCDDEE\r\n"
                          ":00000001FF\r\n";
        CHECK(ok && strcmp(cap_str(), exp) == 0,
              "base 0x10000: emits type-04 upper 0x0001 then data @0x0000");
    }

    // --- Raw .bin writer passes bytes through verbatim -----------------------
    {
        uint8_t buf[3] = { 0x01, 0x02, 0x03 };
        cap_reset();
        bool ok = dump_write_bin(NULL, "d.bin", buf, sizeof(buf));
        CHECK(ok && g_cap_len == 3 && tap_mem_eq(g_cap, buf, 3),
              "raw .bin writer emits bytes unchanged");
    }

    return tap_done("dump_writer");
}
