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
 *@example ex_entropy.c
 *@brief N_ENTROPY randomness metrics regression (headless).
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_entropy.h"
#include "nilorea/n_log.h"

#include <math.h>
#include <string.h>

static int failures = 0;

static void approx(const char* label, double got, double want, double tol) {
    if (fabs(got - want) > tol) {
        n_log(LOG_ERR, "%s: got %.4f, expected %.4f", label, got, want);
        failures++;
    }
}

int main(void) {
    unsigned char uni[256];
    unsigned char same[64];
    unsigned char z[8];
    unsigned char f[8];
    unsigned char half[8];
    unsigned char two[4] = {'A', 'B', 'A', 'B'};
    int i;
    set_log_level(LOG_ERR);

    /* a uniform byte histogram: max entropy, zero chi-square, balanced bits */
    for (i = 0; i < 256; i++)
        uni[i] = (unsigned char)i;
    approx("uniform shannon", n_entropy_shannon(uni, 256), 8.0, 0.0001);
    approx("uniform chi", n_entropy_chi_square(uni, 256), 0.0, 0.0001);
    approx("uniform monobit", n_entropy_monobit(uni, 256), 0.5, 0.01);

    /* a single repeated value: zero entropy, large chi-square */
    memset(same, 'A', sizeof(same));
    approx("same shannon", n_entropy_shannon(same, sizeof(same)), 0.0, 0.0001);
    if (n_entropy_chi_square(same, sizeof(same)) <= 255.0) {
        n_log(LOG_ERR, "same chi-square should be large");
        failures++;
    }

    /* monobit on known bit patterns */
    memset(z, 0x00, sizeof(z));
    memset(f, 0xFF, sizeof(f));
    memset(half, 0x0F, sizeof(half));
    approx("zeros monobit", n_entropy_monobit(z, sizeof(z)), 0.0, 0.0001);
    approx("ones monobit", n_entropy_monobit(f, sizeof(f)), 1.0, 0.0001);
    approx("half monobit", n_entropy_monobit(half, sizeof(half)), 0.5, 0.0001);

    /* two equally likely symbols: exactly one bit of entropy */
    approx("two-symbol shannon", n_entropy_shannon(two, 4), 1.0, 0.0001);

    /* empty / NULL samples are zero, not a crash */
    approx("empty shannon", n_entropy_shannon(NULL, 0), 0.0, 0.0);
    approx("empty chi", n_entropy_chi_square(z, 0), 0.0, 0.0);
    approx("empty monobit", n_entropy_monobit(NULL, 0), 0.0, 0.0);

    if (failures) {
        n_log(LOG_ERR, "ex_entropy: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_entropy: all checks passed");
    return 0;
}
