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
