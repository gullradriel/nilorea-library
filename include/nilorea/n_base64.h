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
 *@file n_base64.h
 *@brief Base64 encoding and decoding functions using N_STR
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/11/2022
 */

#ifndef __N_BASE64_H
#define __N_BASE64_H

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup CYPHER_BASE64 CYPHERS: BASE64 (encode / decode) (from / to) a N_STR *string
  @addtogroup CYPHER_BASE64
  @{
  */

#include <stdlib.h>
#include <memory.h>
#include "nilorea/n_str.h"

/*! @brief check if a character is uppercase */
bool n_isupper(char c);
/*! @brief check if a character is lowercase */
bool n_islower(char c);
/*! @brief check if a character is alphabetic */
bool n_isalpha(char c);
/*! @brief convert a character to uppercase */
char n_toupper(char c);
/*! @brief convert a character to lowercase */
char n_tolower(char c);

/*! @brief encode a N_STR to base64 */
N_STR* n_base64_encode(N_STR* string);
/*! @brief decode a base64 encoded N_STR */
N_STR* n_base64_decode(N_STR* bufcoded);

/**
@}
*/

#ifdef __cplusplus
}
#endif
/* #ifndef __N_BASE64_H */
#endif
