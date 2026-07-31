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
 *@file n_random.h
 *@brief Cryptographically secure random bytes and hex tokens (POSIX)
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_RANDOM_HEADER
#define __NILOREA_RANDOM_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_str.h"

#include <stddef.h>

/**@defgroup N_RANDOM RANDOM: cryptographically secure random helpers
  @addtogroup N_RANDOM
  @{
  */

/*! @brief fill buf with n cryptographically secure random bytes (getrandom(2) when available, otherwise /dev/urandom). A NULL buffer is rejected unless n is 0. Returns 0 on success, -1 on error. */
int n_random_bytes(unsigned char* buf, size_t n);

/*! @brief generate nbytes secure random bytes and return them as a lowercase hex N_STR (2*nbytes characters); free with free_nstr. Returns NULL on error. */
N_STR* n_random_hex_token(size_t nbytes);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_RANDOM_HEADER */
