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
 *@file n_query.h
 *@brief Small boolean query language over caller-named fields
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_QUERY_HEADER
#define __NILOREA_QUERY_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**@defgroup N_QUERY QUERY: a small boolean field-query language
  @addtogroup N_QUERY
  @{

  A compact, field-agnostic query language for filtering records. The grammar is:

    query   := or
    or      := and ( "OR" and )*
    and     := not ( "AND" not )*
    not     := "NOT" not | primary
    primary := "(" or ")" | comparison
    comparison := FIELD OP VALUE

  FIELD is an identifier (letters, digits, '_' and '.', e.g. "host" or "req.path").
  VALUE is a bareword (run of non-space, non-operator, non-paren characters) or a
  double-quoted string. The operators are:

    =    case-insensitive string equality
    !=   case-insensitive string inequality
    ~    case-insensitive substring contains
    !~   does not contain
    =~   regex match (PCRE via n_pcre)
    !~~  regex does not match
    >  <  >=  <=   numeric comparison (both sides parsed as numbers)

  AND/OR/NOT are case-insensitive keywords. Evaluation pulls each field's value
  through a caller-supplied getter, so the language is independent of any record
  type: the caller maps field names to its own data.
  */

/*! Opaque compiled query (see n_query_compile / n_query_eval / n_query_free). */
typedef struct N_QUERY N_QUERY;

/*! @brief Field-value getter: return the value of @p field for the record being
 *  evaluated (NUL-terminated, borrowed), or NULL when the field is unknown/empty. */
typedef const char* (*N_QUERY_GET)(const char* field, void* user_data);

/*! @brief Compile an expression into a query. On error returns NULL and, when
 *  @p errbuf is non-NULL, writes a short message into it. Free with n_query_free.
 *  An empty/whitespace expression compiles to a match-all query. */
N_QUERY* n_query_compile(const char* expr, char* errbuf, size_t errlen);

/*! @brief Evaluate a compiled query against one record via @p get. Returns 1 on
 *  match, 0 otherwise. A NULL query matches everything (returns 1). */
int n_query_eval(const N_QUERY* query, N_QUERY_GET get, void* user_data);

/*! @brief Free a compiled query and set the pointer to NULL. */
void n_query_free(N_QUERY** query);

/**
@}
*/

#ifdef __cplusplus
}
#endif

/* #ifndef __NILOREA_QUERY_HEADER */
#endif
