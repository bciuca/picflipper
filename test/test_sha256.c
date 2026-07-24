// SPDX-License-Identifier: MIT
// Host unit tests for sha256.c against FIPS 180-4 published vectors. sha256.c
// has no SDK dependencies, so it is included directly.
#include "tap.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "sha256.c"

// Hex string -> digest, for readable expected values.
static void
digest (const void *data, size_t len, uint8_t out[32])
{
    Sha256Ctx c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)data, len);
    sha256_final(&c, out);
}

static int
eq32 (const uint8_t got[32], const char *hex)
{
    uint8_t exp[32];
    for (int i = 0; i < 32; i++)
    {
        unsigned v;
        sscanf(hex + i * 2, "%2x", &v);
        exp[i] = (uint8_t)v;
    }
    return tap_mem_eq(got, exp, 32);
}

int
main (void)
{
    uint8_t d[32];

    // FIPS 180-4 / NIST CAVP known-answer vectors.
    digest("", 0, d);
    CHECK(eq32(d, "e3b0c44298fc1c149afbf4c8996fb924"
                  "27ae41e4649b934ca495991b7852b855"),
          "empty string");

    digest("abc", 3, d);
    CHECK(eq32(d, "ba7816bf8f01cfea414140de5dae2223"
                  "b00361a396177a9cb410ff61f20015ad"),
          "\"abc\"");

    // 56-byte message -> exercises the two-block final padding path.
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    digest(m2, strlen(m2), d);
    CHECK(eq32(d, "248d6a61d20638b8e5c026930c3e6039"
                  "a33ce45964ff2167f6ecedd419db06c1"),
          "56-byte two-block message");

    // Streaming equivalence: byte-at-a-time update must equal one-shot.
    {
        Sha256Ctx c;
        sha256_init(&c);
        const char *s = "abc";
        for (size_t i = 0; i < 3; i++)
        {
            sha256_update(&c, (const uint8_t *)&s[i], 1);
        }
        uint8_t ds[32];
        sha256_final(&c, ds);
        CHECK(eq32(ds, "ba7816bf8f01cfea414140de5dae2223"
                       "b00361a396177a9cb410ff61f20015ad"),
              "\"abc\" fed one byte at a time == one-shot");
    }

    // One million 'a' -> multi-block streaming across chunk boundaries.
    {
        Sha256Ctx c;
        sha256_init(&c);
        uint8_t block[1000];
        memset(block, 'a', sizeof(block));
        for (int i = 0; i < 1000; i++)
        {
            sha256_update(&c, block, sizeof(block));
        }
        sha256_final(&c, d);
        CHECK(eq32(d, "cdc76e5c9914fb9281a1c7e284d73e67"
                      "f1809a48a497200e046d39ccc7112cd0"),
              "one million 'a'");
    }

    return tap_done("sha256");
}
