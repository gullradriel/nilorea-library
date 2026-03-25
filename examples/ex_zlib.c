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
 *@example ex_zlib.c
 *@brief Nilorea Library zlib compression api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 27/06/2017
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_zlib.h"

int main(void) {
    set_log_level(LOG_DEBUG);

    /* test raw compress / uncompress with GetMaxCompressedLen, CompressData, UncompressData */
    const char* src_text =
        "Hello World! This is a test of the zlib compression wrappers in nilorea. "
        "Repeated data compresses well: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    size_t src_len = strlen(src_text) + 1;

    size_t max_dst = GetMaxCompressedLen(src_len);
    n_log(LOG_INFO, "Source length: %zu, max compressed length: %zu", src_len, max_dst);

    unsigned char* compressed = NULL;
    Malloc(compressed, unsigned char, max_dst);
    __n_assert(compressed, return 1);

    size_t compressed_len = CompressData((unsigned char*)src_text, src_len, compressed, max_dst);
    if (compressed_len == 0) {
        n_log(LOG_ERR, "CompressData failed");
        Free(compressed);
        return 1;
    }
    n_log(LOG_INFO, "Compressed %zu bytes to %zu bytes", src_len, compressed_len);

    unsigned char* decompressed = NULL;
    Malloc(decompressed, unsigned char, src_len);
    __n_assert(decompressed, Free(compressed); return 1);

    size_t decompressed_len = UncompressData(compressed, compressed_len, decompressed, src_len);
    if (decompressed_len == 0) {
        n_log(LOG_ERR, "UncompressData failed");
    } else {
        n_log(LOG_INFO, "Decompressed %zu bytes back to %zu bytes", compressed_len, decompressed_len);
        if (strcmp((char*)decompressed, src_text) == 0) {
            n_log(LOG_NOTICE, "Raw compress/decompress: data matches");
        } else {
            n_log(LOG_ERR, "Raw compress/decompress: data mismatch");
        }
    }
    Free(compressed);
    Free(decompressed);

    /* test N_STR wrappers: zip_nstr, unzip_nstr */
    N_STR* original = char_to_nstr(
        "Nilorea Library zlib N_STR test. "
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB "
        "The quick brown fox jumps over the lazy dog.");
    n_log(LOG_INFO, "Original N_STR: %zu written, %zu length", original->written, original->length);

    N_STR* zipped = zip_nstr(original);
    if (!zipped) {
        n_log(LOG_ERR, "zip_nstr failed");
        free_nstr(&original);
        return 1;
    }
    n_log(LOG_INFO, "Zipped N_STR: %zu written, %zu length", zipped->written, zipped->length);

    N_STR* unzipped = unzip_nstr(zipped);
    if (!unzipped) {
        n_log(LOG_ERR, "unzip_nstr failed");
    } else {
        n_log(LOG_INFO, "Unzipped N_STR: %zu written, %zu length", unzipped->written, unzipped->length);
        if (strcmp(original->data, unzipped->data) == 0) {
            n_log(LOG_NOTICE, "N_STR zip/unzip: data matches");
        } else {
            n_log(LOG_ERR, "N_STR zip/unzip: data mismatch");
        }
        free_nstr(&unzipped);
    }
    free_nstr(&zipped);
    free_nstr(&original);

    n_log(LOG_INFO, "zlib example done");
    exit(0);
}
