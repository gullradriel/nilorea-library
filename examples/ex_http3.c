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
 *@file ex_http3.c
 *@brief n_http3 example + self-test: parse a URL (always) and, when built with
 *       HAVE_HTTP3, optionally fetch a live https URL over HTTP/3.
 *@author Castagnier Mickael
 *@version 1.0
 *
 * Usage: ex_http3 [https://host/path]
 *
 * The URL-parser checks run in every build and make this double as a unit test
 * (a non-zero exit signals a failure). A live fetch is attempted only when a URL
 * argument is given and the library was built with HAVE_HTTP3; against an
 * authorized, reachable HTTP/3 host it prints the status, headers, and body size.
 */

#include "nilorea/n_http3.h"
#include "nilorea/n_log.h"

#include <stdio.h>
#include <string.h>

/*! test counter */
static int g_fails = 0;
/*! tiny assert that keeps going and tallies failures */
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            n_log(LOG_ERR, "CHECK failed: %s (%s:%d)", #cond, __FILE__, __LINE__); \
            g_fails++;                                                             \
        }                                                                          \
    } while (0)

/*! exercise the pure URL parser (runs in every build) */
static void test_parse_url(void) {
    char host[128];
    char port[16];
    char path[256];

    CHECK(n_http3_parse_url("https://example.com/a/b?c=d", host, sizeof(host), port, sizeof(port), path, sizeof(path)) == 0);
    CHECK(strcmp(host, "example.com") == 0);
    CHECK(strcmp(port, "443") == 0);
    CHECK(strcmp(path, "/a/b?c=d") == 0);

    CHECK(n_http3_parse_url("https://host.test:8443/", host, sizeof(host), port, sizeof(port), path, sizeof(path)) == 0);
    CHECK(strcmp(host, "host.test") == 0);
    CHECK(strcmp(port, "8443") == 0);
    CHECK(strcmp(path, "/") == 0);

    CHECK(n_http3_parse_url("https://[::1]:443/x", host, sizeof(host), port, sizeof(port), path, sizeof(path)) == 0);
    CHECK(strcmp(host, "::1") == 0);
    CHECK(strcmp(port, "443") == 0);

    /* a bare host with no path defaults the path to "/" */
    CHECK(n_http3_parse_url("https://only.host", host, sizeof(host), NULL, 0, path, sizeof(path)) == 0);
    CHECK(strcmp(path, "/") == 0);

    /* non-https and NULL are rejected */
    CHECK(n_http3_parse_url("http://insecure/", host, sizeof(host), port, sizeof(port), path, sizeof(path)) == -1);
    CHECK(n_http3_parse_url(NULL, host, sizeof(host), port, sizeof(port), path, sizeof(path)) == -1);
}

/*! exercise the request entry points' guards + delegation (deterministic, no network) */
static void test_request_guards(void) {
    N_HTTP3_RESPONSE r;
    /* a NULL out is rejected by both entry points */
    CHECK(n_http3_request("GET", "https://x/", NULL, NULL, 0, 100, NULL) == -1);
    CHECK(n_http3_request_ex("GET", "https://x/", NULL, NULL, 0, 100, N_HTTP3_REQ_ALLOW_ILLEGAL_HEADERS, NULL) == -1);
    /* an empty URL is rejected identically through the _ex entry (n_http3_request delegates
       to n_http3_request_ex, so the two agree); the response error is set */
    memset(&r, 0, sizeof(r));
    CHECK(n_http3_request_ex("GET", "", NULL, NULL, 0, 100, 0u, &r) == -1);
    CHECK(r.error != NULL);
    n_http3_response_free(&r);
}

int main(int argc, char** argv) {
    set_log_level(LOG_NOTICE);

    test_parse_url();
    test_request_guards();

    n_log(LOG_NOTICE, "n_http3_available() = %d", n_http3_available());

    if (argc > 1) {
        N_HTTP3_RESPONSE resp;
        int rv = n_http3_get(argv[1], 15000, &resp);
        if (rv == 0) {
            n_log(LOG_NOTICE, "HTTP/3 %d, %zu body bytes", resp.status, resp.body_len);
            if (resp.headers) {
                printf("%s", resp.headers);
            }
            printf("\n[%zu body bytes]\n", resp.body_len);
        } else {
            n_log(LOG_ERR, "HTTP/3 request failed: %s", resp.error ? resp.error : "(unknown)");
            if (!n_http3_available()) {
                n_log(LOG_NOTICE, "rebuild the library with HAVE_HTTP3 (ngtcp2 + nghttp3 + OpenSSL QUIC) to enable HTTP/3");
            }
        }
        n_http3_response_free(&resp);

        /* a second "illegal" argument demonstrates the security-testing mode: send a
           normally-forbidden transfer-encoding header on the HTTP/3 request (a compliant
           endpoint rejects it) via n_http3_request_ex with N_HTTP3_REQ_ALLOW_ILLEGAL_HEADERS */
        if (argc > 2 && strcmp(argv[2], "illegal") == 0) {
            N_HTTP3_RESPONSE r2;
            int rv2 = n_http3_request_ex("GET", argv[1], "transfer-encoding: chunked\r\n", NULL, 0, 15000, N_HTTP3_REQ_ALLOW_ILLEGAL_HEADERS, &r2);
            n_log(LOG_NOTICE, "HTTP/3 + illegal transfer-encoding header -> rv=%d status=%d%s%s",
                  rv2, r2.status, r2.error ? " error=" : "", r2.error ? r2.error : "");
            n_http3_response_free(&r2);
        }
    } else {
        n_log(LOG_NOTICE, "pass an https URL to fetch it over HTTP/3, e.g. ex_http3 https://cloudflare-quic.com/ (add 'illegal' to also send a forbidden framing header)");
    }

    if (g_fails == 0) {
        n_log(LOG_NOTICE, "ex_http3: ALL PARSE TESTS PASSED");
    } else {
        n_log(LOG_ERR, "ex_http3: %d CHECK(s) FAILED", g_fails);
    }
    return g_fails ? 1 : 0;
}
