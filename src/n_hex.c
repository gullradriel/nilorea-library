/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 *@file n_hex.c
 *@brief Hexadecimal encode/decode helpers
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_hex.h"

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

/*! lowercase hexadecimal digit table */
static const char n_hex_digits[] = "0123456789abcdef";

/* Map one ASCII hex digit to its 0..15 value, or -1 when it is not a hex digit. */
static int n_hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

N_STR* n_hex_encode(const unsigned char* data, size_t len) {
    if (!data && len > 0) {
        n_log(LOG_ERR, "n_hex_encode: NULL data with len %zu", len);
        return NULL;
    }
    if (len > (SIZE_MAX - 1) / 2) {
        n_log(LOG_ERR, "n_hex_encode: input length %zu too large", len);
        return NULL;
    }

    N_STR* out = new_nstr(len * 2 + 1);
    __n_assert(out, return NULL);

    char* p = out->data;
    for (size_t i = 0; i < len; i++) {
        *p++ = n_hex_digits[(data[i] >> 4) & 0x0F];
        *p++ = n_hex_digits[data[i] & 0x0F];
    }
    *p = '\0';
    out->written = len * 2;
    return out;
}

unsigned char* n_hex_decode(const char* hex, size_t* out_len) {
    if (out_len) *out_len = 0;
    __n_assert(hex, return NULL);

    size_t hlen = strlen(hex);

    /* first pass: count hex digits and validate every non-space character */
    size_t ndigits = 0;
    for (size_t i = 0; i < hlen; i++) {
        unsigned char c = (unsigned char)hex[i];
        if (isspace(c)) continue;
        if (n_hex_value(c) < 0) {
            n_log(LOG_ERR, "n_hex_decode: invalid hex character 0x%02x at offset %zu", c, i);
            return NULL;
        }
        ndigits++;
    }
    if (ndigits % 2 != 0) {
        n_log(LOG_ERR, "n_hex_decode: odd hex digit count %zu", ndigits);
        return NULL;
    }

    size_t blen = ndigits / 2;
    unsigned char* buf = NULL;
    Malloc(buf, unsigned char, blen + 1);
    __n_assert(buf, return NULL);

    /* second pass: pack each hex pair into one byte */
    size_t bi = 0;
    int high = -1;
    for (size_t i = 0; i < hlen; i++) {
        unsigned char c = (unsigned char)hex[i];
        if (isspace(c)) continue;
        int v = n_hex_value(c);
        if (high < 0) {
            high = v;
        } else {
            buf[bi++] = (unsigned char)((high << 4) | v);
            high = -1;
        }
    }
    buf[blen] = '\0';
    if (out_len) *out_len = blen;
    return buf;
}
