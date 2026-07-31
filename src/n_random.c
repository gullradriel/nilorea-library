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
 *@file n_random.c
 *@brief Cryptographically secure random bytes and hex tokens (POSIX)
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_random.h"

#include "nilorea/n_common.h"
#include "nilorea/n_hex.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* getrandom(2) exists on glibc >= 2.25; guard its header behind that so the
 * /dev/urandom path remains the portable fallback (musl, older glibc). */
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
#define N_RANDOM_HAVE_GETRANDOM 1
#include <sys/random.h>
#endif

/* Read n bytes from /dev/urandom into buf. Returns 0 on success, -1 on error. */
static int n_random_from_urandom(unsigned char* buf, size_t n) {
    FILE* f = fopen("/dev/urandom", "rb");
    if (!f) {
        n_log(LOG_ERR, "n_random_bytes: cannot open /dev/urandom: %s", strerror(errno));
        return -1;
    }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (got != n) {
        n_log(LOG_ERR, "n_random_bytes: short read from /dev/urandom (%zu of %zu)", got, n);
        return -1;
    }
    return 0;
}

int n_random_bytes(unsigned char* buf, size_t n) {
    if (!buf && n > 0) {
        n_log(LOG_ERR, "n_random_bytes: NULL buffer with n %zu", n);
        return -1;
    }
    if (n == 0) {
        return 0;
    }

#ifdef N_RANDOM_HAVE_GETRANDOM
    size_t got = 0;
    while (got < n) {
        ssize_t r = getrandom(buf + got, n - got, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            break; /* fall through to the /dev/urandom fallback */
        }
        got += (size_t)r;
    }
    if (got == n) {
        return 0;
    }
    /* getrandom did not deliver everything; finish from /dev/urandom */
    return n_random_from_urandom(buf + got, n - got);
#else
    return n_random_from_urandom(buf, n);
#endif
}

N_STR* n_random_hex_token(size_t nbytes) {
    if (nbytes == 0) {
        return n_hex_encode((const unsigned char*)"", 0);
    }
    unsigned char* buf = NULL;
    Malloc(buf, unsigned char, nbytes);
    __n_assert(buf, return NULL);
    if (n_random_bytes(buf, nbytes) != 0) {
        Free(buf);
        return NULL;
    }
    N_STR* hex = n_hex_encode(buf, nbytes);
    Free(buf);
    return hex;
}
