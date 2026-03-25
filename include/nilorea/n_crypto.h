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
 *@file n_crypto.h
 *@brief Vigenere encoding and decoding functions using N_STR/files
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/11/2022
 */

#ifndef __N_CRYPTO_H
#define __N_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup CYPHER_VIGENERE CYPHERS: VIGENERE (encode / decode) a (N_STR / file) with a root key or a combination of a question and an answer
 * @addtogroup CYPHER_VIGENERE
  @{
  */

#include <stdlib.h>
#include <memory.h>
#include "nilorea/n_str.h"

/*! The % operator returns a result that adopts the sign of the dividend, a true mathematical modulus adopts the sign of the divisor. This method implements a mathematical modulus */
#define n_mathmod(__a, __b) ((__a % __b + __b) % __b)

/*! @brief generate a random root key of given size */
N_STR* n_vigenere_get_rootkey(size_t rootkey_size);
/*! @brief generate a random question of given size */
N_STR* n_vigenere_get_question(size_t question_size);
/*! @brief compute the answer from a root key and a question */
N_STR* n_vigenere_get_answer(N_STR* root_key, N_STR* question);
/*! @brief encode a N_STR using vigenere cipher with the given key */
N_STR* n_vigenere_encode(N_STR* string, N_STR* key);
/*! @brief decode a N_STR using vigenere cipher with the given key */
N_STR* n_vigenere_decode(N_STR* string, N_STR* key);
/*! @brief quick encode a N_STR using an auto-generated key */
N_STR* n_vigenere_quick_encode(N_STR* decoded_question);
/*! @brief quick decode a N_STR using an auto-generated key */
N_STR* n_vigenere_quick_decode(N_STR* encoded_question);
/*! @brief encode a N_STR using vigenere cipher with a question and answer pair */
N_STR* n_vigenere_encode_qa(N_STR* input_data, N_STR* question, N_STR* answer);
/*! @brief decode a N_STR using vigenere cipher with a question and answer pair */
N_STR* n_vigenere_decode_qa(N_STR* input_data, N_STR* question, N_STR* answer);
/*! @brief encode a file using vigenere cipher with a root key */
int n_vigenere_encode_file(N_STR* in, N_STR* out, N_STR* rootkey);
/*! @brief decode a file using vigenere cipher with a root key */
int n_vigenere_decode_file(N_STR* in, N_STR* out, N_STR* rootkey);
/*! @brief decode a file using vigenere cipher with a question and answer pair */
int n_vigenere_decode_file_qa(N_STR* in, N_STR* out, N_STR* question, N_STR* answer);
/*! @brief encode a file using vigenere cipher with a question and answer pair */
int n_vigenere_encode_file_qa(N_STR* in, N_STR* out, N_STR* question, N_STR* answer);

/**
@}
*/

#ifdef __cplusplus
}
#endif
/* #ifndef __N_CRYPTO_H */
#endif
