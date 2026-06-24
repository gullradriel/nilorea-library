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
 *@file n_pcre.c
 *@brief PCRE helpers for regex matching
 *@author Castagnier Mickael
 *@version 2.0
 *@date 04/12/2019
 */

#include "nilorea/n_pcre.h"
#include "nilorea/n_log.h"

#include <string.h>

/*
 * This module was originally written for legacy PCRE (pcre.h).
 * It has been migrated to PCRE2 (pcre2.h) using the 8-bit API.
 */

/**
  From pcre doc, the flag bits are:
  PCRE_ANCHORED           Force pattern anchoring
  PCRE_AUTO_CALLOUT       Compile automatic callouts
  PCRE_BSR_ANYCRLF        \\R matches only CR, LF, or CRLF
  PCRE_BSR_UNICODE        \\R matches all Unicode line endings
  PCRE_CASELESS           Do caseless matching
  PCRE_DOLLAR_ENDONLY     $ not to match newline at end
  PCRE_DOTALL             . matches anything including NL
  PCRE_DUPNAMES           Allow duplicate names for subpatterns
  PCRE_EXTENDED           Ignore white space and # comments
  PCRE_EXTRA              PCRE extra features
  (not much use currently)
  PCRE_FIRSTLINE          Force matching to be before newline
  PCRE_JAVASCRIPT_COMPAT  JavaScript compatibility
  PCRE_MULTILINE          ^ and $ match newlines within data
  PCRE_NEWLINE_ANY        Recognize any Unicode newline sequence
  PCRE_NEWLINE_ANYCRLF    Recognize CR, LF, and CRLF as newline
  sequences
  PCRE_NEWLINE_CR         Set CR as the newline sequence
  PCRE_NEWLINE_CRLF       Set CRLF as the newline sequence
  PCRE_NEWLINE_LF         Set LF as the newline sequence
  PCRE_NO_AUTO_CAPTURE    Disable numbered capturing paren-
  theses (named ones available)
  PCRE_NO_UTF16_CHECK     Do not check the pattern for UTF-16
  validity (only relevant if
  PCRE_UTF16 is set)
  PCRE_NO_UTF32_CHECK     Do not check the pattern for UTF-32
  validity (only relevant if
  PCRE_UTF32 is set)
  PCRE_NO_UTF8_CHECK      Do not check the pattern for UTF-8
  validity (only relevant if
  PCRE_UTF8 is set)
  PCRE_UCP                Use Unicode properties for backslash-d, backslash-w, etc.
  PCRE_UNGREEDY           Invert greediness of quantifiers
  PCRE_UTF16              Run in pcre16_compile() UTF-16 mode
  PCRE_UTF32              Run in pcre32_compile() UTF-32 mode
  PCRE_UTF8               Run in pcre_compile() UTF-8 mode
 *@brief new N_PCRE object with given parameters
 *@param str The string containing the regexp
 *@param flags pcre_compile flags
 *@return a filled N_PCRE struct or NULL
 */
N_PCRE* npcre_new(char* str, int flags) {
    N_PCRE* pcre = NULL;
    __n_assert(str, return NULL);

    Malloc(pcre, N_PCRE, 1);
    __n_assert(pcre, return NULL);

    pcre->captured = 0;
    pcre->match_list = NULL;
    init_lock(pcre->rwlock);

    pcre->regexp_str = strdup(str);
    __n_assert(pcre->regexp_str, npcre_delete(&pcre); return NULL);

    int errorcode = 0;
    PCRE2_SIZE erroroffset = 0;

    pcre->regexp = pcre2_compile(
        (PCRE2_SPTR)str,
        PCRE2_ZERO_TERMINATED,
        (uint32_t)flags,
        &errorcode,
        &erroroffset,
        NULL);

    if (!pcre->regexp) {
        PCRE2_UCHAR errmsg[256];
        (void)pcre2_get_error_message(errorcode, errmsg, sizeof(errmsg));
        n_log(LOG_ERR, "pcre2 compilation of %s failed at offset %zu : %s", str, (size_t)erroroffset, (char*)errmsg);
        npcre_delete(&pcre);
        return NULL;
    }

    /* Allocate match_data sized for this pattern (captures, etc.) */
    pcre->match_data = pcre2_match_data_create_from_pattern(pcre->regexp, NULL);
    if (!pcre->match_data) {
        n_log(LOG_ERR, "pcre2_match_data_create_from_pattern failed (OOM?)");
        npcre_delete(&pcre);
        return NULL;
    }

    /* Best-effort JIT compilation (optional). Ignore failures. */
    (void)pcre2_jit_compile(pcre->regexp, PCRE2_JIT_COMPLETE);

    return pcre;
} /* npcre_new(...) */

/**
 *
 *@brief Free a N_PCRE pointer
 *@param pcre The N_PCRE regexp holder
 *
 *@return TRUE or FALSE
 */
int npcre_delete(N_PCRE** pcre) {
    __n_assert(pcre && (*pcre), return FALSE);

    write_lock((*pcre)->rwlock);
    if ((*pcre)->captured > 0 && (*pcre)->match_list) {
        pcre2_substring_list_free(PCRE2_LIST_FREE_CAST((*pcre)->match_list));
        (*pcre)->match_list = NULL;
        (*pcre)->captured = 0;
    }

    if ((*pcre)->match_data) {
        pcre2_match_data_free((*pcre)->match_data);
        (*pcre)->match_data = NULL;
    }
    if ((*pcre)->regexp) {
        pcre2_code_free((*pcre)->regexp);
        (*pcre)->regexp = NULL;
    }
    FreeNoLog((*pcre)->regexp_str);
    unlock((*pcre)->rwlock);
    rw_lock_destroy((*pcre)->rwlock);
    FreeNoLog((*pcre));
    return TRUE;
} /* npcre_delete (...) */

/**
 *@brief clean the match list of the last capture, if any
 *@param pcre The N_PCRE regexp holder
 *@return TRUE or FALSE
 */
int npcre_clean_match(N_PCRE* pcre) {
    __n_assert(pcre, return FALSE);

    write_lock(pcre->rwlock);
    if (pcre->match_list) {
        pcre->captured = 0;
        pcre2_substring_list_free(PCRE2_LIST_FREE_CAST(pcre->match_list));
        pcre->match_list = NULL;
    }
    unlock(pcre->rwlock);

    return TRUE;
} /* npcre_clean_match */

/**
 *@brief print error associated to pcre error code
 *@param LOG_LEVEL The log level to use when printing errors. Note: non error will always be printed in LOG_DEBUG
 *@param file name of the file which called the function
 *@param func name of the calling function
 *@param line line in the file
 *@param rc the code to test
 *@return TRUE if everything was OK and no errors, FALSE if an error was decoded, or if it couldn't get the exact error and printed default
 */
int _npcre_print_error(int LOG_LEVEL, const char* file, const char* func, int line, int rc) {
    /*
     * PCRE2 error code names differ from legacy PCRE, and not all historical
     * constants exist in all packaged versions.
     *
     * The portable way is to ask PCRE2 for the human-readable message.
     */

    if (rc > 0) {
        _n_log(LOG_DEBUG, file, func, line, "no pcre error, match: %d elements in ovector", rc);
        return TRUE;
    }

    if (rc == 0) {
        _n_log(LOG_LEVEL, file, func, line, "match OK, but not enough space in ovector to store all elements");
        return FALSE;
    }

    PCRE2_UCHAR errmsg[256];
    PCRE2_SIZE cap = sizeof(errmsg) / sizeof(errmsg[0]);

    int mrc = pcre2_get_error_message(rc, errmsg, cap);
    if (mrc >= 0) {
        _n_log(LOG_LEVEL, file, func, line, "PCRE2 error %d: %s", rc, (const char*)errmsg);
    } else {
        _n_log(LOG_LEVEL, file, func, line, "Unknown PCRE2 error code: %d", rc);
    }

    return FALSE;
}

/**
 *@brief Thread-safe match: returns TRUE/FALSE without modifying the N_PCRE struct.
 * Uses a local pcre2_match_data so multiple threads can safely share the same
 * compiled N_PCRE pattern. No captures are stored.
 *@param str String to test against the regexp
 *@param pcre The N_PCRE regexp holder (read-only access to regexp field)
 *@return TRUE or FALSE
 */
int npcre_match(char* str, N_PCRE* pcre) {
    __n_assert(str, return FALSE);
    __n_assert(pcre, return FALSE);
    __n_assert(pcre->regexp, return FALSE);

    pcre2_match_data* local_match_data = pcre2_match_data_create_from_pattern(pcre->regexp, NULL);
    if (!local_match_data) {
        n_log(LOG_ERR, "npcre_match: pcre2_match_data_create_from_pattern failed");
        return FALSE;
    }

    int rc = (int)pcre2_match(
        pcre->regexp,
        (PCRE2_SPTR)str,
        (PCRE2_SIZE)strlen(str),
        0,
        0,
        local_match_data,
        NULL);

    pcre2_match_data_free(local_match_data);

    return (rc > 0) ? TRUE : FALSE;
} /* npcre_match(...) */

/**
 *@brief Return TRUE if str matches regexp, and make captures.
 * Thread-safe: uses a rwlock to serialize access to match_data, match_list, captured.
 * The caller must read pcre->match_list/captured before the next call to
 * npcre_match_capture() or npcre_clean_match() on the same N_PCRE, as those
 * will overwrite/free the previous results.
 *@param str String to test against the regexp
 *@param pcre The N_PCRE regexp holder
 *@return TRUE or FALSE
 */
int npcre_match_capture(char* str, N_PCRE* pcre) {
    __n_assert(str, return FALSE);
    __n_assert(pcre, return FALSE);
    __n_assert(pcre->regexp_str, return FALSE);
    __n_assert(pcre->regexp, return FALSE);

    write_lock(pcre->rwlock);

    int rc = 0;
    size_t len = strlen(str);

    rc = (int)pcre2_match(
        pcre->regexp,
        (PCRE2_SPTR)str,
        (PCRE2_SIZE)len,
        0,
        0,
        pcre->match_data,
        NULL);

    if (rc < 0) {
        unlock(pcre->rwlock);
        switch (rc) {
            case PCRE2_ERROR_NOMATCH: /* n_log( LOG_DEBUG , "String did not match the pattern");*/
                break;
            case PCRE2_ERROR_NULL:
                n_log(LOG_DEBUG, "Something was null");
                break;
            case PCRE2_ERROR_BADOPTION:
                n_log(LOG_DEBUG, "A bad option was passed");
                break;
            case PCRE2_ERROR_BADMAGIC:
                n_log(LOG_DEBUG, "Magic number bad (compiled re corrupt?)");
                break;
            case PCRE2_ERROR_NOMEMORY:
                n_log(LOG_DEBUG, "Ran out of memory");
                break;
            default:
                n_log(LOG_DEBUG, "Unknown error");
                break;
        }
        return FALSE;
    }

    if (rc > 0) {
        /* clean previous match results (already under lock, call internal logic directly) */
        if (pcre->match_list) {
            pcre->captured = 0;
            pcre2_substring_list_free(PCRE2_LIST_FREE_CAST(pcre->match_list));
            pcre->match_list = NULL;
        }
        /* lengthsptr    Where to put a pointer to the lengths, or NULL */
        PCRE2_SIZE* lens = NULL;
        /* list is NULL-terminated and must be freed with pcre2_substring_list_free() */
        int lrc = pcre2_substring_list_get(pcre->match_data, &pcre->match_list, &lens);
        if (lrc != 0) {
            _npcre_print_error(LOG_ERR, __FILE__, __func__, __LINE__, lrc);
            pcre->match_list = NULL;
            pcre->captured = 0;
            unlock(pcre->rwlock);
            return FALSE;
        }
        pcre->captured = rc;
    }
    unlock(pcre->rwlock);
    return TRUE;
} /* npcre_match_capture(...) */
