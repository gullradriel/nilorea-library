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
 *@example ex_cookies.c
 *@brief n_cookies regression: Set-Cookie parsing, domain/path/secure/expiry matching.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_cookies.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

/* Parse the -V LOG_LEVEL verbosity argument. */
void process_args(int argc, char** argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hvV:")) != EOF) {
        switch (opt) {
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 8))
                    set_log_level(LOG_NULL);
                else if (!strncmp("LOG_NOTICE", optarg, 10))
                    set_log_level(LOG_NOTICE);
                else if (!strncmp("LOG_INFO", optarg, 8))
                    set_log_level(LOG_INFO);
                else if (!strncmp("LOG_ERR", optarg, 7))
                    set_log_level(LOG_ERR);
                else if (!strncmp("LOG_DEBUG", optarg, 9))
                    set_log_level(LOG_DEBUG);
                else {
                    fprintf(stderr, "Unknown log level %s\n", optarg);
                    exit(1);
                }
                break;
            case 'v':
                fprintf(stderr, "ex_cookies\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

/* Does the built header contain the substring (NULL header counts as no)? */
static int hdr_has(N_STR* h, const char* needle) {
    return h && h->data && strstr(h->data, needle) != NULL;
}

int main(int argc, char** argv) {
    N_COOKIE_JAR* jar;
    N_STR* h;
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    jar = n_cookiejar_new();
    if (!jar) {
        n_log(LOG_ERR, "jar alloc failed");
        return 1;
    }

    n_cookiejar_add_from_header(jar, "sid=abc; Path=/; Domain=example.com", "example.com");
    n_cookiejar_add_from_header(jar, "token=xyz; Secure", "example.com");
    n_cookiejar_add_from_header(jar, "sub=1; Domain=.example.com", "example.com");
    n_cookiejar_add_from_header(jar, "adm=1; Path=/admin", "example.com");
    n_cookiejar_add_from_header(jar, "old=1; Max-Age=-100", "example.com"); /* already expired */
    if (n_cookiejar_count(jar) != 5) {
        n_log(LOG_ERR, "expected 5 cookies, got %zu", n_cookiejar_count(jar));
        failures++;
    }

    /* http request to / : sid + sub, not the Secure token, not the /admin cookie,
       not the expired one */
    h = n_cookiejar_build_header(jar, "example.com", "/", 0);
    if (!hdr_has(h, "sid=abc") || hdr_has(h, "token=xyz") || hdr_has(h, "adm=1") || hdr_has(h, "old=1")) {
        n_log(LOG_ERR, "http / header wrong: '%s'", h && h->data ? h->data : "(null)");
        failures++;
    }
    free_nstr(&h);

    /* https request: the Secure cookie is now included */
    h = n_cookiejar_build_header(jar, "example.com", "/", 1);
    if (!hdr_has(h, "token=xyz") || !hdr_has(h, "sid=abc")) {
        n_log(LOG_ERR, "https header missing secure cookie: '%s'", h && h->data ? h->data : "(null)");
        failures++;
    }
    free_nstr(&h);

    /* the .example.com cookie matches a subdomain */
    h = n_cookiejar_build_header(jar, "www.example.com", "/", 0);
    if (!hdr_has(h, "sub=1")) {
        n_log(LOG_ERR, "subdomain match failed: '%s'", h && h->data ? h->data : "(null)");
        failures++;
    }
    free_nstr(&h);

    /* the /admin cookie is sent only under /admin */
    h = n_cookiejar_build_header(jar, "example.com", "/admin/panel", 0);
    if (!hdr_has(h, "adm=1")) {
        n_log(LOG_ERR, "path match failed: '%s'", h && h->data ? h->data : "(null)");
        failures++;
    }
    free_nstr(&h);

    /* an unrelated domain matches nothing */
    h = n_cookiejar_build_header(jar, "other.test", "/", 0);
    if (h != NULL) {
        n_log(LOG_ERR, "unrelated domain should yield no header");
        failures++;
        free_nstr(&h);
    }

    /* re-Set sid: value replaced, count unchanged */
    n_cookiejar_add_from_header(jar, "sid=NEW; Domain=example.com", "example.com");
    if (n_cookiejar_count(jar) != 5) {
        n_log(LOG_ERR, "replace changed count: %zu", n_cookiejar_count(jar));
        failures++;
    }
    h = n_cookiejar_build_header(jar, "example.com", "/", 0);
    if (!hdr_has(h, "sid=NEW")) {
        n_log(LOG_ERR, "sid not replaced: '%s'", h && h->data ? h->data : "(null)");
        failures++;
    }
    free_nstr(&h);

    n_cookiejar_clear(jar);
    if (n_cookiejar_count(jar) != 0) {
        n_log(LOG_ERR, "clear left %zu cookies", n_cookiejar_count(jar));
        failures++;
    }

    n_cookiejar_free(&jar);
    if (jar) {
        n_log(LOG_ERR, "free did not NULL the jar");
        failures++;
    }

    if (failures) {
        n_log(LOG_ERR, "ex_cookies: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_cookies: all checks passed");
    return 0;
}
