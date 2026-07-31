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
 *@file n_diff.h
 *@brief Line oriented text comparison (Myers shortest edit script)
 *@author Castagnier Mickael
 *@version 1.0
 *@date 31/07/2026
 */

#ifndef __N_DIFF_H
#define __N_DIFF_H

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup N_DIFF DIFF: compare two texts line by line
 *
 * Splits two texts into lines and produces the shortest edit script turning
 * the first into the second, using Myers' greedy algorithm with a stored
 * trace. Unlike an index aligned comparison, inserting one line does not
 * report every following line as changed.
 *
 * Line splitting: texts are cut on '\n'. A trailing newline closes the last
 * line instead of opening an empty one, so "a\nb\n" and "a\nb" both yield
 * two lines and compare equal. Carriage returns are kept, so a text that
 * switched from LF to CRLF shows up as a difference rather than being
 * silently normalised.
 *
 * Cost control: the forward pass keeps one trace snapshot per edit, so
 * memory grows with the square of the edit script length. n_diff_lines()
 * therefore refuses to search past max_edits differing lines; when the two
 * texts are further apart than that it falls back to reporting the whole
 * differing region as deleted-then-inserted and sets N_DIFF_RESULT.truncated.
 * The result is always a valid edit script, only a coarser one. Identical
 * leading and trailing lines are matched before the search starts, so the
 * limit applies to the part that actually differs.
 *
 *  @addtogroup N_DIFF
 *  @{
 */

#include "nilorea/n_list.h"
#include "nilorea/n_str.h"

/*! the line is present in both texts */
#define N_DIFF_COMMON 0
/*! the line is only in the first text */
#define N_DIFF_DELETE 1
/*! the line is only in the second text */
#define N_DIFF_INSERT 2

/*! N_DIFF_LINE line_a / line_b value when the line has no counterpart there */
#define N_DIFF_NO_LINE ((size_t)-1)

/*! n_diff_to_unified() context asking for every common line, without hunk headers */
#define N_DIFF_CONTEXT_FULL ((size_t)-1)

/*! n_diff_lines() default ceiling on the number of differing lines searched */
#define N_DIFF_MAX_EDITS_DEFAULT 1000

/*! hard ceiling n_diff_lines() clamps max_edits to, keeping the worst case
 *  trace under about 64 MB */
#define N_DIFF_MAX_EDITS_CEILING 4000

/*! one line of an edit script */
typedef struct N_DIFF_LINE {
    /*! line content, without its newline, owned by the result */
    char* text;
    /*! zero based position in the first text, or N_DIFF_NO_LINE */
    size_t line_a;
    /*! zero based position in the second text, or N_DIFF_NO_LINE */
    size_t line_b;
    /*! N_DIFF_COMMON, N_DIFF_DELETE or N_DIFF_INSERT */
    int op;
} N_DIFF_LINE;

/*! edit script turning one text into another */
typedef struct N_DIFF_RESULT {
    /*! LIST of N_DIFF_LINE*, in output order */
    LIST* lines;
    /*! number of N_DIFF_COMMON lines */
    size_t nb_common;
    /*! number of N_DIFF_DELETE lines */
    size_t nb_deleted;
    /*! number of N_DIFF_INSERT lines */
    size_t nb_inserted;
    /*! 1 when the edit limit was hit and the differing region was reported
     *  as one delete run followed by one insert run */
    int truncated;
} N_DIFF_RESULT;

/*!@brief Compare two texts line by line.
 *@param text_a The first text. NULL is treated as empty.
 *@param text_b The second text. NULL is treated as empty.
 *@param max_edits Ceiling on the number of differing lines to search for,
 *       or 0 for N_DIFF_MAX_EDITS_DEFAULT, clamped to
 *       N_DIFF_MAX_EDITS_CEILING. Past it the differing region is reported
 *       coarsely and truncated is set.
 *@return A new N_DIFF_RESULT the caller must release with
 *        n_diff_result_free(), or NULL on allocation failure. Two empty
 *        texts give an empty, valid result. */
N_DIFF_RESULT* n_diff_lines(const char* text_a, const char* text_b, size_t max_edits);

/*!@brief Free an edit script and every line it holds.
 *@param result Pointer to pointer, set to NULL on return. */
void n_diff_result_free(N_DIFF_RESULT** result);

/*!@brief Render an edit script as unified diff text.
 *
 * Each line is prefixed with ' ', '-' or '+'. With a finite context the
 * output is cut into hunks introduced by a "@@ -a,b +c,d @@" header and
 * common lines further than context away from a change are dropped; with
 * N_DIFF_CONTEXT_FULL every line is emitted and no header is written.
 *@param result The edit script. Must not be NULL.
 *@param context Common lines kept around each change, or N_DIFF_CONTEXT_FULL.
 *@return A new N_STR (free with free_nstr), or NULL on failure. */
N_STR* n_diff_to_unified(const N_DIFF_RESULT* result, size_t context);

/**
 *@}
 */

#ifdef __cplusplus
}
#endif

/* #ifndef __N_DIFF_H */
#endif
