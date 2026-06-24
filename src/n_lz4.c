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

/*!@file n_lz4.c
 *@brief LZ4 block-compression wrappers, zip4_nstr / unzip4_nstr.
 *@author Castagnier Mickael
 *@version 1.0
 *@date 23/04/2026
 */

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "nilorea/n_log.h"
#include "nilorea/n_common.h"
#include "nilorea/n_str.h"
#include "nilorea/n_lz4.h"

/* htonl / ntohl, winsock2.h on Windows, arpa/inet.h everywhere else.
 * n_common.h defines __windows__ on _WIN32/_WIN64. */
#ifdef __windows__
#include "nilorea/n_windows.h"
#else
#include <arpa/inet.h>
#endif

#include "lz4.h"

/**
 *@brief Compress src with LZ4 block format.
 *
 * Self-framing, the produced N_STR begins with a u32 (network byte
 * order) holding the original uncompressed length, followed by the
 * LZ4-compressed bytes. Matches zip_nstr's framing so
 * unzip{4,}_nstr can be selected per inbound packet without a
 * separate framing header.
 */
N_STR* zip4_nstr(N_STR* src) {
    __n_assert(src, return NULL);
    __n_assert(src->data, return NULL);

    if (src->length == 0) {
        n_log(LOG_ERR, "length of src == 0");
        return NULL;
    }
    if (src->written == 0) {
        n_log(LOG_ERR, "written of src == 0");
        return NULL;
    }
    /* LZ4's API is int-sized. Reject anything that would overflow. */
    if (src->written > (size_t)INT_MAX) {
        n_log(LOG_ERR, "written of src > INT_MAX");
        return NULL;
    }

    int src_len = (int)src->written;
    int max_compressed = LZ4_compressBound(src_len);
    if (max_compressed <= 0) {
        n_log(LOG_ERR, "LZ4_compressBound(%d) failed", src_len);
        return NULL;
    }

    N_STR* zipped = new_nstr((size_t)(4 + max_compressed));
    __n_assert(zipped, return NULL);
    __n_assert(zipped->data, return NULL);

    /* Original length header (network byte order). */
    uint32_t src_length_be = htonl((uint32_t)src->written);
    memcpy(zipped->data, &src_length_be, sizeof(uint32_t));
    char* dataptr = zipped->data + 4;

    int compressed = LZ4_compress_default(src->data, dataptr, src_len, max_compressed);
    if (compressed <= 0) {
        n_log(LOG_ERR, "LZ4_compress_default failed (src=%d bytes)", src_len);
        free_nstr(&zipped);
        return NULL;
    }
    zipped->written = (size_t)(4 + compressed);
    n_log(LOG_DEBUG, "lz4 zip: %d -> %zu (header incl.)", src_len, zipped->written);
    return zipped;
} /* zip4_nstr */

/**
 *@brief Decompress an N_STR produced by zip4_nstr.
 *
 * Rejects inputs whose declared original size exceeds 256 MiB as a
 * defence against malicious / corrupt frames causing a large
 * allocation.  Matches the same cap unzip_nstr uses.
 */
N_STR* unzip4_nstr(N_STR* src) {
    __n_assert(src, return NULL);
    __n_assert(src->data, return NULL);

    if (src->written < 4) {
        n_log(LOG_ERR, "compressed data too short (%zu bytes)", src->written);
        return NULL;
    }

    uint32_t original_size = 0;
    memcpy(&original_size, src->data, sizeof(uint32_t));
    original_size = ntohl(original_size);
    if (original_size == 0) {
        n_log(LOG_ERR, "lz4 frame declares original_size == 0");
        return NULL;
    }
    if (original_size > 256u * 1024u * 1024u) {
        n_log(LOG_ERR, "lz4 frame declares absurd original_size %u", original_size);
        return NULL;
    }

    N_STR* unzipped = new_nstr(original_size);
    __n_assert(unzipped, return NULL);
    __n_assert(unzipped->data, return NULL);

    size_t compressed_bytes = src->written - 4;
    if (compressed_bytes > (size_t)INT_MAX) {
        n_log(LOG_ERR, "compressed payload > INT_MAX");
        free_nstr(&unzipped);
        return NULL;
    }

    int decoded = LZ4_decompress_safe(src->data + 4, unzipped->data,
                                      (int)compressed_bytes, (int)original_size);
    if (decoded < 0) {
        n_log(LOG_ERR, "LZ4_decompress_safe failed (compressed=%zu, original=%u)",
              compressed_bytes, original_size);
        free_nstr(&unzipped);
        return NULL;
    }
    unzipped->written = (size_t)decoded;
    n_log(LOG_DEBUG, "lz4 unzip: %zu -> %zu (original=%u)",
          src->written, unzipped->written, original_size);
    return unzipped;
} /* unzip4_nstr */
