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
 *@file n_cookies.c
 *@brief Domain-aware HTTP cookie jar: Set-Cookie parsing and Cookie header building
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_cookies.h"

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* Free an N_COOKIE stored in a LIST. */
static void n_cookie_free(void* ptr) {
    N_COOKIE* c = (N_COOKIE*)ptr;
    if (!c)
        return;
    FreeNoLog(c->name);
    FreeNoLog(c->value);
    FreeNoLog(c->domain);
    FreeNoLog(c->path);
    FreeNoLog(c);
}

N_COOKIE_JAR* n_cookiejar_new(void) {
    N_COOKIE_JAR* jar = NULL;
    Malloc(jar, N_COOKIE_JAR, 1);
    if (!jar)
        return NULL;
    jar->cookies = new_generic_list(0);
    if (!jar->cookies) {
        Free(jar);
        return NULL;
    }
    init_lock(jar->lock);
    return jar;
}

void n_cookiejar_free(N_COOKIE_JAR** jar) {
    if (!jar || !*jar)
        return;
    if ((*jar)->cookies)
        list_destroy(&(*jar)->cookies);
    rw_lock_destroy((*jar)->lock);
    Free(*jar);
}

/* Does the cookie's domain match the request domain? */
static int n_cookie_domain_matches(const char* cookie_domain, const char* request_domain) {
    if (!cookie_domain || !request_domain)
        return 0;
    if (strcasecmp(cookie_domain, request_domain) == 0)
        return 1;
    if (cookie_domain[0] == '.') {
        size_t cd = strlen(cookie_domain);
        size_t rd = strlen(request_domain);
        if (rd >= cd - 1) {
            const char* suffix = request_domain + rd - (cd - 1);
            if (strcasecmp(suffix, cookie_domain + 1) == 0)
                return 1;
        }
    }
    return 0;
}

/* Does the request path fall under the cookie's path prefix? */
static int n_cookie_path_matches(const char* cookie_path, const char* request_path) {
    if (!cookie_path || !request_path)
        return 1;
    return strncmp(request_path, cookie_path, strlen(cookie_path)) == 0;
}

void n_cookiejar_add_from_header(N_COOKIE_JAR* jar, const char* set_cookie_value, const char* request_domain) {
    char* dup;
    char* saveptr = NULL;
    char* token;
    char* eq;
    const char* n;
    N_COOKIE* cookie = NULL;
    if (!jar || !set_cookie_value || !request_domain)
        return;

    dup = strdup(set_cookie_value);
    if (!dup)
        return;
    token = strtok_r(dup, ";", &saveptr);
    if (!token) {
        free(dup);
        return;
    }
    eq = strchr(token, '=');
    if (!eq) {
        free(dup);
        return;
    }
    Malloc(cookie, N_COOKIE, 1);
    if (!cookie) {
        free(dup);
        return;
    }
    *eq = '\0';
    n = token;
    while (*n == ' ')
        n++;
    cookie->name = strdup(n);
    cookie->value = strdup(eq + 1);
    cookie->domain = strdup(request_domain);
    cookie->path = strdup("/");
    cookie->secure = 0;
    cookie->http_only = 0;
    cookie->expires = 0;

    while ((token = strtok_r(NULL, ";", &saveptr)) != NULL) {
        while (*token == ' ')
            token++;
        if (strncasecmp(token, "domain=", 7) == 0) {
            free(cookie->domain);
            cookie->domain = strdup(token + 7);
        } else if (strncasecmp(token, "path=", 5) == 0) {
            free(cookie->path);
            cookie->path = strdup(token + 5);
        } else if (strncasecmp(token, "secure", 6) == 0) {
            cookie->secure = 1;
        } else if (strncasecmp(token, "httponly", 8) == 0) {
            cookie->http_only = 1;
        } else if (strncasecmp(token, "max-age=", 8) == 0) {
            long ma = strtol(token + 8, NULL, 10);
            cookie->expires = (long)time(NULL) + ma;
        }
    }
    free(dup);

    write_lock(jar->lock);
    /* replace an existing cookie with the same name+domain in place */
    if (jar->cookies) {
        list_foreach(cnode, jar->cookies) {
            N_COOKIE* ex = (N_COOKIE*)cnode->ptr;
            if (ex && ex->name && cookie->name && strcmp(ex->name, cookie->name) == 0 &&
                ex->domain && cookie->domain && strcasecmp(ex->domain, cookie->domain) == 0) {
                free(ex->value);
                ex->value = cookie->value ? strdup(cookie->value) : NULL;
                free(ex->path);
                ex->path = cookie->path ? strdup(cookie->path) : NULL;
                ex->secure = cookie->secure;
                ex->http_only = cookie->http_only;
                ex->expires = cookie->expires;
                unlock(jar->lock);
                n_cookie_free(cookie);
                return;
            }
        }
    }
    list_push(jar->cookies, cookie, n_cookie_free);
    unlock(jar->lock);
}

N_STR* n_cookiejar_build_header(N_COOKIE_JAR* jar, const char* domain, const char* path, int is_secure) {
    N_STR* header;
    time_t now;
    int first = 1;
    if (!jar || !jar->cookies || !domain)
        return NULL;
    header = new_nstr(256);
    if (!header)
        return NULL;
    now = time(NULL);

    read_lock(jar->lock);
    list_foreach(node, jar->cookies) {
        N_COOKIE* c = (N_COOKIE*)node->ptr;
        if (!c || !c->name || !c->value)
            continue;
        if (c->expires > 0 && c->expires < (long)now)
            continue;
        if (!n_cookie_domain_matches(c->domain, domain))
            continue;
        if (!n_cookie_path_matches(c->path, path))
            continue;
        if (c->secure && !is_secure)
            continue;
        if (!first)
            nstrprintf_cat(header, "; ");
        nstrprintf_cat(header, "%s=%s", c->name, c->value);
        first = 0;
    }
    unlock(jar->lock);

    if (header->written == 0) {
        free_nstr(&header);
        return NULL;
    }
    return header;
}

size_t n_cookiejar_count(N_COOKIE_JAR* jar) {
    size_t n;
    if (!jar || !jar->cookies)
        return 0;
    read_lock(jar->lock);
    n = jar->cookies->nb_items;
    unlock(jar->lock);
    return n;
}

void n_cookiejar_clear(N_COOKIE_JAR* jar) {
    if (!jar || !jar->cookies)
        return;
    write_lock(jar->lock);
    while (jar->cookies->start) {
        N_COOKIE* c = (N_COOKIE*)remove_list_node_f(jar->cookies, jar->cookies->start);
        n_cookie_free(c);
    }
    unlock(jar->lock);
}
