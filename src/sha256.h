// SPDX-License-Identifier: MIT
// Copyright (c) 2026, BC (https://github.com/bciuca). All rights reserved.

// Self-contained SHA-256 (streaming). The firmware ships mbedtls' sha256 but
// does not export it in the FAP API symbol table, so the app carries its own.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[64];
    uint8_t  buflen;
} Sha256Ctx;

void sha256_init(Sha256Ctx *c);
void sha256_update(Sha256Ctx *c, const uint8_t *data, size_t len);
void sha256_final(Sha256Ctx *c, uint8_t out[32]); // 32-byte digest
