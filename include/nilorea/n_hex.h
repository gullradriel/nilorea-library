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
 *@file n_hex.h
 *@brief Hexadecimal encode/decode helpers
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_HEX_HEADER
#define __NILOREA_HEX_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_str.h"

#include <stddef.h>

/**@defgroup N_HEX HEX: hexadecimal encode and decode
  @addtogroup N_HEX
  @{
  */

/*! @brief encode len bytes of data as a lowercase hex N_STR (2*len characters); free with free_nstr. Returns NULL on a NULL/oversized input or on allocation failure. */
N_STR* n_hex_encode(const unsigned char* data, size_t len);

/*! @brief decode a hex string (ASCII whitespace between digits is ignored) into a freshly allocated, NUL-terminated byte buffer; *out_len receives the decoded byte count. Free the buffer with Free()/free(). Returns NULL on an odd number of hex digits or an invalid character. */
unsigned char* n_hex_decode(const char* hex, size_t* out_len);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_HEX_HEADER */
