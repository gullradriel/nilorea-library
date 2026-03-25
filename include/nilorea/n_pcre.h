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
 *@file n_pcre.h
 *@brief PCRE helpers for regex matching
 *@author Castagnier Mickael
 *@version 2.0
 *@date 04/12/2019
 */

#ifndef __NILOREA_PCRE_HELPERS__
#define __NILOREA_PCRE_HELPERS__

#ifdef __cplusplus
extern "C" {
#endif

/* PCRE2 uses a different API than legacy PCRE (pcre.h).
 * We use the 8-bit library (char / UTF-8 style strings). */
#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif
#include <pcre2.h>

#include <nilorea/n_common.h>

/* -------------------------------------------------------------------------
 * Compatibility aliases
 * -------------------------------------------------------------------------
 * The legacy PCRE (pcre.h) used PCRE_* option names. PCRE2 uses PCRE2_*.
 * If your existing code still passes PCRE_* flags to npcre_new(), these
 * aliases let it keep compiling.
 */
#ifndef PCRE_CASELESS
#define PCRE_CASELESS PCRE2_CASELESS
#endif
#ifndef PCRE_MULTILINE
#define PCRE_MULTILINE PCRE2_MULTILINE
#endif
#ifndef PCRE_DOTALL
#define PCRE_DOTALL PCRE2_DOTALL
#endif
#ifndef PCRE_EXTENDED
#define PCRE_EXTENDED PCRE2_EXTENDED
#endif
#ifndef PCRE_ANCHORED
#define PCRE_ANCHORED PCRE2_ANCHORED
#endif
#ifndef PCRE_UTF8
#define PCRE_UTF8 PCRE2_UTF
#endif
#ifndef PCRE_UCP
#define PCRE_UCP PCRE2_UCP
#endif

/* pcre2_substring_list_free() had a const PCRE2_UCHAR** argument up to 10.41,
 * then it was changed to PCRE2_UCHAR** in 10.42 to fix the mismatch with
 * pcre2_substring_list(). Use void* as intermediate to satisfy both. */
#ifdef PCRE2_LIST_FREE_CONST
#define PCRE2_LIST_FREE_CAST(x) ((const PCRE2_UCHAR8**)(void*)(x))
#else
#define PCRE2_LIST_FREE_CAST(x) ((PCRE2_UCHAR8**)(void*)(x))
#endif

/*! PCRE error logging function wrapper to get line and func */
#define npcre_print_error(__LEVEL__, __rc__)                                 \
    do {                                                                     \
        _npcre_print_error(__LEVEL__, __FILE__, __func__, __LINE__, __rc__); \
    } while (0)

/**@defgroup PCRE PCREs: regex matching helpers
  @addtogroup PCRE
  @{
  */

/*! N_PCRE structure */
typedef struct N_PCRE {
    /*! original regexp string */
    char* regexp_str;
    /*! regexp */
    pcre2_code* regexp;
    /*! match data storage (ovector, etc.) */
    pcre2_match_data* match_data;
    /*! populated match list (NULL-terminated) after npcre_match_capture()
     *  Allocated by PCRE2 via pcre2_substring_list_get() and must be freed
     *  with pcre2_substring_list_free().
     */
    PCRE2_UCHAR8** match_list;
    /*! flag for match_list cleaning */
    int captured;
    /*! rwlock protecting match_data, match_list, and captured */
    pthread_rwlock_t rwlock;
} N_PCRE;

/*! pcre helper, compile and optimize regexp */
N_PCRE* npcre_new(char* str, int flags);
/*! pcre helper, delete/free a compiled regexp */
int npcre_delete(N_PCRE** pcre);
/*! @brief clean match results from a previous npcre_match_capture */
int npcre_clean_match(N_PCRE* pcre);
/*! @brief thread-safe pcre match, only returns TRUE/FALSE, no captures stored */
int npcre_match(char* str, N_PCRE* pcre);
/*! @brief thread-safe pcre match with captures stored in pcre->match_list */
int npcre_match_capture(char* str, N_PCRE* pcre);
/*! print error helper */
int _npcre_print_error(int LOG_LEVEL, const char* file, const char* func, int line, int rc);

/**
@}
*/

#ifdef __cplusplus
}
#endif
/* #ifndef __NILOREA_PCRE_HELPERS__ */
#endif
