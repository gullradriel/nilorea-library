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

/*!@file n_lz4.h
 * LZ4 block-compression handler.
 *
 * Companion to n_zlib.h that wraps the lz4 reference implementation
 * shipped under external/lz4/.  Same self-framing layout as
 * zip_nstr (u32 original size in network byte order + compressed
 * payload), just a different codec inside.  Picked when callers
 * want speed over ratio, LZ4 compresses ~25x faster and
 * decompresses ~10x faster than zlib at a ~25 % ratio cost on
 * text / JSON payloads.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 23/04/2026
 */

#ifndef __N__LZ4_HEADER
#define __N__LZ4_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include "n_str.h"

/**@defgroup LZ4 LZ4: shortcuts to easy compress/decompress data
  @addtogroup LZ4
  @{
  */

/*! @brief Return a compressed version of src using LZ4 block format.
 *  Self-framing, the output starts with a u32 (network byte order)
 *  carrying the original uncompressed length, followed by the
 *  lz4-compressed payload. Returns NULL on error. */
N_STR* zip4_nstr(N_STR* src);

/*! @brief Return an uncompressed version of src produced by
 *  zip4_nstr. Rejects payloads whose decoded-size header is
 *  absurdly large to stop a malicious sender from over-allocating.
 *  Returns NULL on error. */
N_STR* unzip4_nstr(N_STR* src);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif  // header guard
