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

/**@file n_json.c
 * @brief Implementation of the small public JSON wrapper over cJSON.
 *@author Castagnier Mickael
 *@version 1.0
 *@date 2024
 */

#include "nilorea/n_json.h"
#include "nilorea/n_log.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CJSON

#include "cJSON.h"
#include "nilorea/n_str.h"

/* N_JSON is an opaque alias for cJSON; cast at the boundary. */
#define AS_CJSON(p) ((cJSON*)(void*)(p))
#define AS_CONST_CJSON(p) ((const cJSON*)(const void*)(p))
#define AS_NJSON(p) ((N_JSON*)(void*)(p))

N_JSON* n_json_parse(const char* text) {
    if (!text)
        return NULL;
    return AS_NJSON(cJSON_Parse(text));
}

N_JSON* n_json_parse_file(const char* path) {
    N_STR* body = NULL;
    cJSON* root = NULL;
    if (!path)
        return NULL;
    body = file_to_nstr((char*)path);
    if (!body || !body->data) {
        if (body)
            free_nstr(&body);
        return NULL;
    }
    root = cJSON_Parse(body->data);
    free_nstr(&body);
    return AS_NJSON(root);
}

void n_json_free(N_JSON** root) {
    if (!root || !*root)
        return;
    cJSON_Delete(AS_CJSON(*root));
    *root = NULL;
}

N_JSON* n_json_get(const N_JSON* obj, const char* key) {
    if (!obj || !key)
        return NULL;
    return AS_NJSON(cJSON_GetObjectItemCaseSensitive(AS_CONST_CJSON(obj), key));
}

const char* n_json_string(const N_JSON* obj, const char* key, const char* dflt) {
    const cJSON* it = obj && key ? cJSON_GetObjectItemCaseSensitive(AS_CONST_CJSON(obj), key) : NULL;
    if (it && cJSON_IsString(it) && it->valuestring)
        return it->valuestring;
    return dflt;
}

char* n_json_string_dup(const N_JSON* obj, const char* key) {
    const char* s = n_json_string(obj, key, NULL);
    return s ? strdup(s) : NULL;
}

long n_json_int(const N_JSON* obj, const char* key, long dflt) {
    const cJSON* it = obj && key ? cJSON_GetObjectItemCaseSensitive(AS_CONST_CJSON(obj), key) : NULL;
    if (it && cJSON_IsNumber(it))
        return (long)it->valuedouble;
    return dflt;
}

int n_json_bool(const N_JSON* obj, const char* key, int dflt) {
    const cJSON* it = obj && key ? cJSON_GetObjectItemCaseSensitive(AS_CONST_CJSON(obj), key) : NULL;
    if (it && cJSON_IsBool(it))
        return cJSON_IsTrue(it) ? 1 : 0;
    return dflt;
}

int n_json_is_array(const N_JSON* v) {
    return (v && cJSON_IsArray(AS_CONST_CJSON(v))) ? 1 : 0;
}

int n_json_is_object(const N_JSON* v) {
    return (v && cJSON_IsObject(AS_CONST_CJSON(v))) ? 1 : 0;
}

size_t n_json_array_size(const N_JSON* arr) {
    if (!arr || !cJSON_IsArray(AS_CONST_CJSON(arr)))
        return 0;
    return (size_t)cJSON_GetArraySize(AS_CONST_CJSON(arr));
}

N_JSON* n_json_array_at(const N_JSON* arr, size_t i) {
    if (!arr || !cJSON_IsArray(AS_CONST_CJSON(arr)))
        return NULL;
    return AS_NJSON(cJSON_GetArrayItem(AS_CONST_CJSON(arr), (int)i));
}

const char* n_json_as_string(const N_JSON* v, const char* dflt) {
    const cJSON* c = AS_CONST_CJSON(v);
    if (c && cJSON_IsString(c) && c->valuestring)
        return c->valuestring;
    return dflt;
}

#else /* !HAVE_CJSON */

N_JSON* n_json_parse(const char* text) {
    (void)text;
    n_log(LOG_ERR, "n_json: cJSON not available (compile with -DHAVE_CJSON)");
    return NULL;
}
N_JSON* n_json_parse_file(const char* path) {
    (void)path;
    n_log(LOG_ERR, "n_json: cJSON not available (compile with -DHAVE_CJSON)");
    return NULL;
}
void n_json_free(N_JSON** root) {
    (void)root;
}
N_JSON* n_json_get(const N_JSON* obj, const char* key) {
    (void)obj;
    (void)key;
    return NULL;
}
const char* n_json_string(const N_JSON* obj, const char* key, const char* dflt) {
    (void)obj;
    (void)key;
    return dflt;
}
char* n_json_string_dup(const N_JSON* obj, const char* key) {
    (void)obj;
    (void)key;
    return NULL;
}
long n_json_int(const N_JSON* obj, const char* key, long dflt) {
    (void)obj;
    (void)key;
    return dflt;
}
int n_json_bool(const N_JSON* obj, const char* key, int dflt) {
    (void)obj;
    (void)key;
    return dflt;
}
int n_json_is_array(const N_JSON* v) {
    (void)v;
    return 0;
}
int n_json_is_object(const N_JSON* v) {
    (void)v;
    return 0;
}
size_t n_json_array_size(const N_JSON* arr) {
    (void)arr;
    return 0;
}
N_JSON* n_json_array_at(const N_JSON* arr, size_t i) {
    (void)arr;
    (void)i;
    return NULL;
}
const char* n_json_as_string(const N_JSON* v, const char* dflt) {
    (void)v;
    return dflt;
}

#endif /* HAVE_CJSON */
