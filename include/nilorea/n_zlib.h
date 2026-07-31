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

/*!@file n_zlib.h
 * ZLIB compression handler
 *@author Castagnier Mickael
 *@version 1.0
 *@date 27/06/2017
 */

#ifndef __N__Z_LIB_HEADER
#define __N__Z_LIB_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <zlib.h>
#include "n_str.h"

/**@defgroup ZLIB ZLIB: shortcuts to easy compress/decompress data
  @addtogroup ZLIB
  @{
  */

/*! @brief Return the maximum compressed size */
size_t GetMaxCompressedLen(size_t nLenSrc);
/*! @brief Compress a string to another */
size_t CompressData(unsigned char* abSrc, size_t nLenSrc, unsigned char* abDst, size_t nLenDst);
/*! @brief Uncompress a string to another */
size_t UncompressData(unsigned char* abSrc, size_t nLenSrc, unsigned char* abDst, size_t nLenDst);

/*! @brief Return a compressed version of src */
N_STR* zip_nstr(N_STR* src);
/*! @brief Return an uncompressed version of src */
N_STR* unzip_nstr(N_STR* src);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif  // header guard
