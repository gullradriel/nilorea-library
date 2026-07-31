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
 *@file n_entropy.c
 *@brief Implementation of the byte-sample randomness metrics.
 */

#include "nilorea/n_entropy.h"

#include <math.h>

double n_entropy_shannon(const unsigned char* data, size_t len) {
    size_t counts[256];
    size_t i;
    double h = 0.0;
    if (!data || len == 0)
        return 0.0;
    for (i = 0; i < 256; i++)
        counts[i] = 0;
    for (i = 0; i < len; i++)
        counts[data[i]]++;
    for (i = 0; i < 256; i++) {
        if (counts[i]) {
            double p = (double)counts[i] / (double)len;
            h -= p * log2(p);
        }
    }
    return h;
}

double n_entropy_monobit(const unsigned char* data, size_t len) {
    size_t i;
    size_t ones = 0;
    if (!data || len == 0)
        return 0.0;
    for (i = 0; i < len; i++) {
        unsigned int b = data[i];
        /* popcount of one byte */
        b = b - ((b >> 1) & 0x55u);
        b = (b & 0x33u) + ((b >> 2) & 0x33u);
        b = (b + (b >> 4)) & 0x0Fu;
        ones += b;
    }
    return (double)ones / ((double)len * 8.0);
}

double n_entropy_chi_square(const unsigned char* data, size_t len) {
    size_t counts[256];
    size_t i;
    double expected, chi = 0.0;
    if (!data || len == 0)
        return 0.0;
    for (i = 0; i < 256; i++)
        counts[i] = 0;
    for (i = 0; i < len; i++)
        counts[data[i]]++;
    expected = (double)len / 256.0;
    for (i = 0; i < 256; i++) {
        double diff = (double)counts[i] - expected;
        chi += (diff * diff) / expected;
    }
    return chi;
}
