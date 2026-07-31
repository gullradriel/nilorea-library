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
 *@file n_pretty.h
 *@brief Lexical pretty-printers for JSON, XML/HTML and JavaScript text
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/07/2026
 */

#ifndef __N_PRETTY_H
#define __N_PRETTY_H

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup N_PRETTY PRETTY: reformat JSON, XML/HTML and JavaScript text for display
 *
 * Dependency-free lexical formatters aimed at viewers: they re-indent a
 * document so a human can read it, without validating it. Malformed input
 * is reformatted on a best-effort basis; the formatters never crash on it.
 *
 * Limits by design (these are viewers, not compilers):
 * - n_pretty_xml() treats HTML like XML with void-element and raw-text
 *   (script/style) awareness; whitespace inside text nodes is trimmed at
 *   both ends, so whitespace-significant content (pre) is not preserved
 *   exactly.
 * - n_pretty_js() is a conservative re-indenter (newlines at braces and
 *   semicolons), not a beautifier. Strings, template literals, comments
 *   and regex literals are copied verbatim. A '/' is lexed as a regex
 *   when it follows an operator, an opener, a separator, or an
 *   expression keyword (return, typeof, case, ...); the remaining
 *   ambiguity (a regex right after a closing parenthesis, as in
 *   `if (x) /re/.test(y)`) is lexed as division, which can only
 *   mis-break lines, never corrupt the text.
 * - n_pretty_json() needs the vendored cJSON (HAVE_CJSON); without it the
 *   function logs an error and returns NULL. It parses, so it reformats
 *   valid documents only: use n_pretty_json_lenient() for the rest.
 * - n_pretty_json_lenient() re-indents by punctuation without parsing, so
 *   it also breaks up a document cJSON rejects (a truncated body, a
 *   trailing comma, NDJSON). It cannot report that the input was invalid
 *   and it does not normalise anything, so prefer n_pretty_json() first
 *   and label what the fallback produces as approximate.
 *
 * All functions return a freshly allocated N_STR the caller must release
 * with free_nstr(), or NULL when the input is NULL, empty, or does not
 * look like the expected format.
 *
 *  @addtogroup N_PRETTY
 *  @{
 */

#include "nilorea/n_str.h"

/*!@brief Reformat a JSON document with cJSON (parse + print).
 *@param text The JSON text. NULL, empty, or non-JSON input returns NULL.
 *@return A new indented N_STR (free with free_nstr), or NULL. */
N_STR* n_pretty_json(const char* text);

/*!@brief Re-indent a JSON document by punctuation alone, without parsing it.
 *       String literals are copied verbatim, containers open and close a level,
 *       and each comma ends a line, so a document cJSON rejects (truncated,
 *       trailing comma, concatenated values) still becomes readable.
 *@param text The JSON text. NULL or empty input returns NULL.
 *@return A new indented N_STR (free with free_nstr), or NULL. */
N_STR* n_pretty_json_lenient(const char* text);

/*!@brief Re-indent an XML or HTML document (one tag or text run per line).
 *@param text The markup text. NULL, empty, or input whose first
 *       non-whitespace character is not '<' returns NULL.
 *@return A new indented N_STR (free with free_nstr), or NULL. */
N_STR* n_pretty_xml(const char* text);

/*!@brief Conservatively re-indent JavaScript (newlines at top-level braces
 *       and semicolons, two-space indent by brace depth).
 *@param text The script text. NULL or empty input returns NULL.
 *@return A new indented N_STR (free with free_nstr), or NULL. */
N_STR* n_pretty_js(const char* text);

/**
 *@}
 */

#ifdef __cplusplus
}
#endif

/* #ifndef __N_PRETTY_H */
#endif
