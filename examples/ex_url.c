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
 *@example ex_url.c
 *@brief n_url_canonicalize regression test (scheme/host case, default port, dot segments).
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_str.h"

#include <stdio.h>
#include <string.h>
#include <zlib.h>

/* Compress s with the given zlib window bits (31 = gzip, -15 = raw deflate). */
static N_STR* gz_compress(const char* s, int window_bits) {
    z_stream strm;
    size_t inlen = strlen(s);
    uLong cap;
    char* buf;
    N_STR* r = NULL;
    size_t outlen;
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return NULL;
    cap = deflateBound(&strm, (uLong)inlen);
    buf = malloc(cap);
    if (!buf) {
        deflateEnd(&strm);
        return NULL;
    }
    strm.next_in = (Bytef*)s;
    strm.avail_in = (uInt)inlen;
    strm.next_out = (Bytef*)buf;
    strm.avail_out = (uInt)cap;
    deflate(&strm, Z_FINISH);
    outlen = cap - strm.avail_out;
    deflateEnd(&strm);
    char_to_nstr_ex(buf, (NSTRBYTE)outlen, &r);
    free(buf);
    return r;
}

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
                fprintf(stderr, "ex_url\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

static void expect_canon(const char* in, const char* want) {
    N_STR* got = n_url_canonicalize_string(in);
    if (!got || !got->data || strcmp(got->data, want) != 0) {
        n_log(LOG_ERR, "canon('%s'): got '%s', expected '%s'", in, got && got->data ? got->data : "(null)", want);
        failures++;
    } else {
        n_log(LOG_INFO, "canon('%s') = '%s' OK", in, got->data);
    }
    if (got) free_nstr(&got);
}

static void expect_resolve(const char* base, const char* ref, const char* want) {
    N_STR* got = n_url_resolve(base, ref);
    if (!got || !got->data || strcmp(got->data, want) != 0) {
        n_log(LOG_ERR, "resolve('%s','%s'): got '%s', expected '%s'", base, ref, got && got->data ? got->data : "(null)", want);
        failures++;
    } else {
        n_log(LOG_INFO, "resolve('%s','%s') = '%s' OK", base, ref, got->data);
    }
    if (got) free_nstr(&got);
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    /* scheme + host lowercased, default port dropped, dot segments removed */
    expect_canon("HTTP://Example.COM:80/a/./b/../c?x=1", "http://example.com/a/c?x=1");
    expect_canon("https://Host:443/", "https://host/");
    expect_canon("http://host", "http://host/");
    expect_canon("https://host/a/b/../../c/", "https://host/c/");
    /* non-default port kept */
    expect_canon("http://host:8080/p", "http://host:8080/p");

    /* two spellings of the same resource canonicalize identically (dedup) */
    {
        N_STR* a = n_url_canonicalize_string("http://EXAMPLE.com/x/./y");
        N_STR* b = n_url_canonicalize_string("http://example.com:80/x/y");
        if (!a || !b || strcmp(a->data, b->data) != 0) {
            n_log(LOG_ERR, "dedup mismatch: '%s' vs '%s'", a && a->data ? a->data : "(null)", b && b->data ? b->data : "(null)");
            failures++;
        }
        if (a) free_nstr(&a);
        if (b) free_nstr(&b);
    }

    /* RFC 3986 reference resolution against an absolute base */
    {
        const char* base = "http://example.com/dir/page.html?old=1";
        expect_resolve(base, "other.html", "http://example.com/dir/other.html");
        expect_resolve(base, "sub/leaf.html", "http://example.com/dir/sub/leaf.html");
        expect_resolve(base, "/root.html", "http://example.com/root.html");
        expect_resolve(base, "../up.html", "http://example.com/up.html");
        expect_resolve(base, "./same.html", "http://example.com/dir/same.html");
        expect_resolve(base, "?new=2", "http://example.com/dir/page.html?new=2");
        expect_resolve(base, "#frag", "http://example.com/dir/page.html?old=1");
        expect_resolve(base, "next.html#frag", "http://example.com/dir/next.html");
        expect_resolve(base, "//cdn.example.org/x.js", "http://cdn.example.org/x.js");
        expect_resolve(base, "https://other.test/p", "https://other.test/p");
        expect_resolve(base, "mailto:a@b.com", "mailto:a@b.com");
        expect_resolve(base, "", "http://example.com/dir/page.html?old=1");
    }
    /* a port in the base authority is preserved through relative resolution */
    expect_resolve("http://host:8080/a/b", "c", "http://host:8080/a/c");

    /* HTTP status classifiers */
    {
        if (n_http_status_class(200) != 2 || n_http_status_class(404) != 4 ||
            n_http_status_class(599) != 5 || n_http_status_class(99) != 0 ||
            n_http_status_class(600) != 0) {
            n_log(LOG_ERR, "n_http_status_class wrong");
            failures++;
        }
        if (!n_http_status_is_informational(100) || n_http_status_is_informational(200)) failures++;
        if (!n_http_status_is_success(204) || n_http_status_is_success(301)) failures++;
        if (!n_http_status_is_redirect(302) || n_http_status_is_redirect(200)) failures++;
        if (!n_http_status_is_client_error(404) || n_http_status_is_client_error(500)) failures++;
        if (!n_http_status_is_server_error(503) || n_http_status_is_server_error(404)) failures++;
    }

    /* HTTP body decompression: gzip and raw-deflate round-trips + identity */
    {
        const char* msg = "The quick brown fox jumps over the lazy dog, repeatedly. AAAAAAAAAAAA.";
        N_STR* gz = gz_compress(msg, 31);  /* gzip framing */
        N_STR* df = gz_compress(msg, -15); /* raw deflate */
        N_STR* plain = char_to_nstr(msg);
        N_STR* out = NULL;
        if (gz && n_http_decompress_body(gz, "gzip", &out) == 0 && out && out->data && strcmp(out->data, msg) == 0) {
            free_nstr(&out);
        } else {
            n_log(LOG_ERR, "gzip decompress round-trip failed");
            failures++;
            if (out) free_nstr(&out);
        }
        if (df && n_http_decompress_body(df, "deflate", &out) == 0 && out && out->data && strcmp(out->data, msg) == 0) {
            free_nstr(&out);
        } else {
            n_log(LOG_ERR, "deflate decompress round-trip failed");
            failures++;
            if (out) free_nstr(&out);
        }
        /* identity / NULL encoding passes the body through unchanged */
        if (plain && n_http_decompress_body(plain, NULL, &out) == 0 && out && out->data && strcmp(out->data, msg) == 0) {
            free_nstr(&out);
        } else {
            n_log(LOG_ERR, "identity passthrough failed");
            failures++;
            if (out) free_nstr(&out);
        }
        if (gz) free_nstr(&gz);
        if (df) free_nstr(&df);
        if (plain) free_nstr(&plain);
    }

    /* multipart/form-data parse, build round-trip, and boundary extraction */
    {
        char bnd[128];
        const char* wire =
            "--BkXyZ\r\n"
            "Content-Disposition: form-data; name=\"field1\"\r\n"
            "\r\n"
            "value1\r\n"
            "--BkXyZ\r\n"
            "Content-Disposition: form-data; name=\"file1\"; filename=\"a.txt\"\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "file-content\r\n"
            "--BkXyZ--\r\n";
        N_STR* body = char_to_nstr(wire);
        LIST* parts = n_http_parse_multipart(body, "BkXyZ");

        if (n_http_multipart_boundary("multipart/form-data; boundary=----WebKitX", bnd, sizeof(bnd)) != 0 || strcmp(bnd, "----WebKitX") != 0) {
            n_log(LOG_ERR, "boundary extraction failed: '%s'", bnd);
            failures++;
        }
        if (!parts || parts->nb_items != 2) {
            n_log(LOG_ERR, "multipart parse: expected 2 parts, got %zu", parts ? parts->nb_items : (size_t)0);
            failures++;
        } else {
            N_HTTP_MULTIPART_PART* p0 = (N_HTTP_MULTIPART_PART*)parts->start->ptr;
            N_HTTP_MULTIPART_PART* p1 = (N_HTTP_MULTIPART_PART*)parts->start->next->ptr;
            if (strcmp(p0->name, "field1") != 0 || strcmp(p0->filename, "") != 0 || !p0->body || strcmp(p0->body->data, "value1") != 0) {
                n_log(LOG_ERR, "multipart part0 wrong: name=%s file=%s body=%s", p0->name, p0->filename, p0->body ? p0->body->data : "(null)");
                failures++;
            }
            if (strcmp(p1->name, "file1") != 0 || strcmp(p1->filename, "a.txt") != 0 || strcmp(p1->content_type, "text/plain") != 0 || !p1->body || strcmp(p1->body->data, "file-content") != 0) {
                n_log(LOG_ERR, "multipart part1 wrong: name=%s file=%s ct=%s body=%s", p1->name, p1->filename, p1->content_type, p1->body ? p1->body->data : "(null)");
                failures++;
            }
            /* build from the parsed parts, re-parse, and confirm the round-trip */
            {
                N_STR* rebuilt = n_http_build_multipart(parts, "BkXyZ");
                LIST* parts2 = rebuilt ? n_http_parse_multipart(rebuilt, "BkXyZ") : NULL;
                if (!parts2 || parts2->nb_items != 2) {
                    n_log(LOG_ERR, "multipart round-trip: expected 2 parts");
                    failures++;
                } else {
                    N_HTTP_MULTIPART_PART* q1 = (N_HTTP_MULTIPART_PART*)parts2->start->next->ptr;
                    if (strcmp(q1->name, "file1") != 0 || strcmp(q1->filename, "a.txt") != 0 || strcmp(q1->body->data, "file-content") != 0) {
                        n_log(LOG_ERR, "multipart round-trip mismatch");
                        failures++;
                    }
                }
                if (parts2) n_http_multipart_free(&parts2);
                if (rebuilt) free_nstr(&rebuilt);
            }
        }
        if (parts) n_http_multipart_free(&parts);
        free_nstr(&body);
    }

    if (failures) {
        n_log(LOG_ERR, "ex_url: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_url: all checks passed");
    return 0;
}
