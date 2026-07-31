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
 *@example ex_random.c
 *@brief n_random regression: secure random bytes and hex token generation.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_random.h"
#include "nilorea/n_str.h"

#include <ctype.h>
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
                fprintf(stderr, "ex_random\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

static void test_bytes(void) {
    unsigned char a[32];
    unsigned char b[32];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));

    if (n_random_bytes(a, sizeof(a)) != 0 || n_random_bytes(b, sizeof(b)) != 0) {
        n_log(LOG_ERR, "n_random_bytes failed");
        failures++;
        return;
    }
    /* two independent 32-byte draws colliding is cryptographically impossible */
    if (memcmp(a, b, sizeof(a)) == 0) {
        n_log(LOG_ERR, "two random draws were identical");
        failures++;
    }
    /* an all-zero 32-byte buffer after a fill is effectively impossible */
    int all_zero = 1;
    for (size_t i = 0; i < sizeof(a); i++) {
        if (a[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        n_log(LOG_ERR, "random buffer is all zero");
        failures++;
    }

    /* zero length is a no-op success; NULL with n>0 is rejected */
    if (n_random_bytes(NULL, 0) != 0) {
        n_log(LOG_ERR, "n_random_bytes(NULL,0) should succeed");
        failures++;
    }
    if (n_random_bytes(NULL, 8) == 0) {
        n_log(LOG_ERR, "n_random_bytes(NULL,8) should fail");
        failures++;
    }
}

static void test_token(void) {
    N_STR* t1 = n_random_hex_token(16);
    N_STR* t2 = n_random_hex_token(16);
    if (!t1 || !t2) {
        n_log(LOG_ERR, "n_random_hex_token returned NULL");
        failures++;
    } else {
        if (t1->written != 32 || t2->written != 32) {
            n_log(LOG_ERR, "token length: got %zu/%zu expected 32", t1->written, t2->written);
            failures++;
        }
        for (size_t i = 0; i < t1->written; i++) {
            if (!isxdigit((unsigned char)t1->data[i]) || (t1->data[i] >= 'A' && t1->data[i] <= 'F')) {
                n_log(LOG_ERR, "token has a non-lowercase-hex character at %zu", i);
                failures++;
                break;
            }
        }
        if (strcmp(t1->data, t2->data) == 0) {
            n_log(LOG_ERR, "two tokens were identical");
            failures++;
        }
    }
    free_nstr(&t1);
    free_nstr(&t2);

    /* a zero-byte token is the empty string */
    N_STR* t0 = n_random_hex_token(0);
    if (!t0 || t0->written != 0) {
        n_log(LOG_ERR, "zero-byte token should be empty");
        failures++;
    }
    free_nstr(&t0);
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    test_bytes();
    test_token();

    if (failures) {
        n_log(LOG_ERR, "ex_random: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_random: all checks passed");
    return 0;
}
