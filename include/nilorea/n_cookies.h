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
 *@file n_cookies.h
 *@brief Domain-aware HTTP cookie jar: Set-Cookie parsing and Cookie header building
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_COOKIES_HEADER
#define __NILOREA_COOKIES_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_list.h"
#include "nilorea/n_str.h"

#include <pthread.h>

/**@defgroup N_COOKIES COOKIES: domain-aware HTTP cookie jar
  @addtogroup N_COOKIES
  @{
  */

/*! a single stored cookie */
typedef struct N_COOKIE {
    char* name;    /*!< cookie name */
    char* value;   /*!< cookie value */
    char* domain;  /*!< domain (e.g. "example.com" or ".example.com" for subdomains) */
    char* path;    /*!< path prefix (e.g. "/") */
    int secure;    /*!< 1 = only sent over HTTPS */
    int http_only; /*!< 1 = HttpOnly flag was set */
    long expires;  /*!< unix expiry timestamp, 0 = session cookie */
} N_COOKIE;

/*! a thread-safe cookie jar */
typedef struct N_COOKIE_JAR {
    LIST* cookies;         /*!< list of N_COOKIE* */
    pthread_rwlock_t lock; /*!< guards cookies for concurrent use */
} N_COOKIE_JAR;

/*! @brief create an empty cookie jar; free with n_cookiejar_free. Returns NULL on error. */
N_COOKIE_JAR* n_cookiejar_new(void);
/*! @brief free a cookie jar and all its cookies, then NULL the pointer */
void n_cookiejar_free(N_COOKIE_JAR** jar);
/*! @brief parse one Set-Cookie value (without the "Set-Cookie:" name) and store it; request_domain/path supply the defaults */
void n_cookiejar_add_from_header(N_COOKIE_JAR* jar, const char* set_cookie_value, const char* request_domain);
/*! @brief build a "name=value; name2=value2" Cookie header value for a request (domain/path/secure/expiry matched); free with free_nstr. Returns NULL when no cookie matches. */
N_STR* n_cookiejar_build_header(N_COOKIE_JAR* jar, const char* domain, const char* path, int is_secure);
/*! @brief number of cookies currently stored */
size_t n_cookiejar_count(N_COOKIE_JAR* jar);
/*! @brief drop every stored cookie */
void n_cookiejar_clear(N_COOKIE_JAR* jar);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_COOKIES_HEADER */
