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

/**@file n_json.h
 * @brief Small public JSON helper, a thin wrapper over the vendored cJSON.
 *
 * Hides cJSON behind an opaque N_JSON node and the handful of accessors that
 * consumers repeat: parse a string or a file, walk objects/arrays, and pull
 * typed values by key with a default. Build with -DHAVE_CJSON; without it the
 * functions are present but return NULL/defaults and log an error.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 2024
 */

#ifndef __N_JSON_H
#define __N_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/*! @defgroup N_JSON JSON: a thin public wrapper over cJSON
   @addtogroup N_JSON
  @{
*/

/*! Opaque JSON node (a cJSON node under the hood). */
typedef struct N_JSON N_JSON;

/*!@brief Parse a JSON document from a NUL-terminated string.
 *@param text The JSON text. NULL returns NULL.
 *@return A new root node (free with n_json_free), or NULL on a parse error. */
N_JSON* n_json_parse(const char* text);

/*!@brief Parse a JSON document from a file.
 *@param path Path to the JSON file. NULL returns NULL.
 *@return A new root node (free with n_json_free), or NULL on read/parse error. */
N_JSON* n_json_parse_file(const char* path);

/*!@brief Free a JSON tree and NULL the caller's pointer.
 *@param root Address of the root pointer. May be NULL or point to NULL. */
void n_json_free(N_JSON** root);

/*!@brief Get a child member of an object by key (case-sensitive).
 *@param obj The object node, or NULL.
 *@param key The member name. NULL returns NULL.
 *@return The child node (borrowed), or NULL when absent. */
N_JSON* n_json_get(const N_JSON* obj, const char* key);

/*!@brief Get a string member, or a default when absent/not a string.
 *@param obj The object node, or NULL.
 *@param key The member name.
 *@param dflt Value returned when the member is missing or not a string.
 *@return A borrowed string pointer, or @p dflt. */
const char* n_json_string(const N_JSON* obj, const char* key, const char* dflt);

/*!@brief Duplicate a string member (caller frees), or NULL when absent.
 *@param obj The object node, or NULL.
 *@param key The member name.
 *@return A malloc'd copy of the string value, or NULL. */
char* n_json_string_dup(const N_JSON* obj, const char* key);

/*!@brief Get a numeric member as a long, or a default when absent/not a number.
 *@param obj The object node, or NULL.
 *@param key The member name.
 *@param dflt Value returned when the member is missing or not a number.
 *@return The integer value, or @p dflt. */
long n_json_int(const N_JSON* obj, const char* key, long dflt);

/*!@brief Get a boolean member, or a default when absent/not a boolean.
 *@param obj The object node, or NULL.
 *@param key The member name.
 *@param dflt Value returned when the member is missing or not a boolean.
 *@return 1 or 0, or @p dflt. */
int n_json_bool(const N_JSON* obj, const char* key, int dflt);

/*!@brief Report whether a node is a JSON array.
 *@param v The node, or NULL.
 *@return 1 when @p v is an array, 0 otherwise. */
int n_json_is_array(const N_JSON* v);

/*!@brief Report whether a node is a JSON object.
 *@param v The node, or NULL.
 *@return 1 when @p v is an object, 0 otherwise. */
int n_json_is_object(const N_JSON* v);

/*!@brief Number of elements in an array node.
 *@param arr The array node, or NULL.
 *@return The element count, or 0. */
size_t n_json_array_size(const N_JSON* arr);

/*!@brief Get an array element by index.
 *@param arr The array node, or NULL.
 *@param i Zero-based index.
 *@return The element (borrowed), or NULL when out of range. */
N_JSON* n_json_array_at(const N_JSON* arr, size_t i);

/*!@brief Get the string value of a node itself (e.g. an array element).
 *@param v The node, or NULL.
 *@param dflt Value returned when @p v is not a string.
 *@return A borrowed string pointer, or @p dflt. */
const char* n_json_as_string(const N_JSON* v, const char* dflt);

/*! @} */

#ifdef __cplusplus
}
#endif

#endif /* __N_JSON_H */
