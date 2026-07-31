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
 *@file n_entropy.h
 *@brief Randomness/entropy metrics for byte samples (token-randomness analysis)
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_ENTROPY_HEADER
#define __NILOREA_ENTROPY_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**@defgroup N_ENTROPY ENTROPY: randomness metrics for byte samples
  @addtogroup N_ENTROPY
  @{
  */

/*! @brief Shannon entropy of the byte sample, in bits per byte (0.0 .. 8.0).
 *  0 means a single repeated value; 8 means a perfectly uniform byte histogram.
 *  Returns 0.0 for an empty/NULL sample. */
double n_entropy_shannon(const unsigned char* data, size_t len);

/*! @brief Fraction of set bits in the sample (0.0 .. 1.0); 0.5 is the ideal for
 *  random data (the FIPS monobit idea). Returns 0.0 for an empty/NULL sample. */
double n_entropy_monobit(const unsigned char* data, size_t len);

/*! @brief Chi-square statistic of the byte histogram against a uniform
 *  distribution over 256 values. Near 255 (the degrees of freedom) for random
 *  data; large for skewed data. Returns 0.0 for an empty/NULL sample. */
double n_entropy_chi_square(const unsigned char* data, size_t len);

/**
@}
*/

#ifdef __cplusplus
}
#endif

/* #ifndef __NILOREA_ENTROPY_HEADER */
#endif
