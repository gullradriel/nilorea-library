/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
