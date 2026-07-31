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
 *@example ex_hex.c
 *@brief n_hex regression: hexadecimal encode/decode round-trips and error paths.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_hex.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

/* Parse the -V LOG_LEVEL verbosity argument. */
void process_args(int argc, char** argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hvV:")) != EOF) {
        switch (opt) {
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 8))
                    set_log_level(LOG_NULL);
                else if (!strncmp("LOG_NOTICE", optarg, 10))
                    set_log_level(LOG_NOTICE);
                else if (!strncmp("LOG_INFO", optarg, 8))
                    set_log_level(LOG_INFO);
                else if (!strncmp("LOG_ERR", optarg, 7))
                    set_log_level(LOG_ERR);
                else if (!strncmp("LOG_DEBUG", optarg, 9))
                    set_log_level(LOG_DEBUG);
                else {
                    fprintf(stderr, "Unknown log level %s\n", optarg);
                    exit(1);
                }
                break;
            case 'v':
                fprintf(stderr, "ex_hex\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

static void expect_encode(const unsigned char* data, size_t len, const char* want) {
    N_STR* out = n_hex_encode(data, len);
    if (!out) {
        n_log(LOG_ERR, "n_hex_encode returned NULL for expected '%s'", want);
        failures++;
        return;
    }
    if (out->written != strlen(want) || strcmp(out->data, want) != 0) {
        n_log(LOG_ERR, "n_hex_encode: got '%s' (written %zu) expected '%s'", out->data, out->written, want);
        failures++;
    }
    free_nstr(&out);
}

static void test_encode(void) {
    const unsigned char a[] = {0x00, 0x01, 0xff, 0xab};
    expect_encode(a, sizeof(a), "0001ffab");

    const unsigned char b[] = {0xDE, 0xAD, 0xBE, 0xEF};
    expect_encode(b, sizeof(b), "deadbeef");

    /* zero length yields an empty string */
    expect_encode((const unsigned char*)"", 0, "");
    /* NULL data with len 0 is allowed and yields an empty string */
    expect_encode(NULL, 0, "");
}

static void test_decode_roundtrip(void) {
    size_t n = 123;
    unsigned char* buf = n_hex_decode("0001ffab", &n);
    if (!buf || n != 4) {
        n_log(LOG_ERR, "decode '0001ffab': got len %zu expected 4", n);
        failures++;
    } else if (buf[0] != 0x00 || buf[1] != 0x01 || buf[2] != 0xff || buf[3] != 0xab) {
        n_log(LOG_ERR, "decode '0001ffab': wrong bytes");
        failures++;
    }
    Free(buf);

    /* uppercase and whitespace are tolerated and produce the same bytes */
    n = 0;
    buf = n_hex_decode("  DE AD\tBE\nEF ", &n);
    if (!buf || n != 4 || buf[0] != 0xde || buf[1] != 0xad || buf[2] != 0xbe || buf[3] != 0xef) {
        n_log(LOG_ERR, "decode with whitespace/upper failed (len %zu)", n);
        failures++;
    }
    Free(buf);

    /* round-trip a buffer containing an embedded NUL */
    const unsigned char payload[] = {0x00, 0x41, 0x00, 0x42};
    N_STR* enc = n_hex_encode(payload, sizeof(payload));
    if (!enc || strcmp(enc->data, "00410042") != 0) {
        n_log(LOG_ERR, "encode with embedded NUL failed");
        failures++;
    } else {
        n = 0;
        unsigned char* dec = n_hex_decode(enc->data, &n);
        if (!dec || n != sizeof(payload) || memcmp(dec, payload, sizeof(payload)) != 0) {
            n_log(LOG_ERR, "round-trip with embedded NUL failed (len %zu)", n);
            failures++;
        }
        Free(dec);
    }
    free_nstr(&enc);
}

static void test_decode_errors(void) {
    size_t n = 7;
    unsigned char* buf = n_hex_decode("abc", &n); /* odd digit count */
    if (buf || n != 0) {
        n_log(LOG_ERR, "decode odd-length should fail");
        failures++;
        Free(buf);
    }
    n = 7;
    buf = n_hex_decode("0g", &n); /* invalid character */
    if (buf || n != 0) {
        n_log(LOG_ERR, "decode invalid char should fail");
        failures++;
        Free(buf);
    }
    /* empty input decodes to a zero-length, non-NULL buffer */
    n = 7;
    buf = n_hex_decode("", &n);
    if (!buf || n != 0) {
        n_log(LOG_ERR, "decode empty should yield len 0 buffer (len %zu)", n);
        failures++;
    }
    Free(buf);
    /* NULL input is rejected */
    if (n_hex_decode(NULL, &n) != NULL) {
        n_log(LOG_ERR, "decode NULL should fail");
        failures++;
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    test_encode();
    test_decode_roundtrip();
    test_decode_errors();

    if (failures) {
        n_log(LOG_ERR, "ex_hex: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_hex: all checks passed");
    return 0;
}
