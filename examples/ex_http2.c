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
 *@example ex_http2.c
 *@brief n_http2 regression: frame codec, SETTINGS, and HPACK (RFC 7541 vectors).
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_http2.h"
#include "nilorea/n_log.h"

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

/* Round-trip a frame header (build then parse) and check every field. */
static void test_roundtrip(void) {
    unsigned char buf[64];
    N_H2_FRAME f;
    size_t consumed = 0, w;

    /* a HEADERS frame on stream 1 with END_HEADERS|END_STREAM and a payload */
    w = n_http2_frame_build_header(buf, sizeof(buf), 5, N_H2_HEADERS,
                                   N_H2_FLAG_END_HEADERS | N_H2_FLAG_END_STREAM, 1);
    if (w != N_H2_FRAME_HEADER_LEN) failures++;
    memcpy(buf + w, "hello", 5); /* the header block (opaque here) */

    if (n_http2_frame_parse(buf, w + 5, &f, &consumed) != 1) failures++;
    if (f.length != 5 || f.type != N_H2_HEADERS) failures++;
    if (f.flags != (N_H2_FLAG_END_HEADERS | N_H2_FLAG_END_STREAM)) failures++;
    if (f.stream_id != 1) failures++;
    if (consumed != w + 5 || !f.payload || memcmp(f.payload, "hello", 5) != 0) failures++;

    /* a 24-bit stream id and a zero-length frame (no payload pointer) */
    w = n_http2_frame_build_header(buf, sizeof(buf), 0, N_H2_WINDOW_UPDATE, 0, 0x7FFFFFFFu);
    if (n_http2_frame_parse(buf, w, &f, &consumed) != 1) failures++;
    if (f.stream_id != 0x7FFFFFFFu || f.length != 0 || f.payload != NULL) failures++;
    if (consumed != N_H2_FRAME_HEADER_LEN) failures++;

    /* the reserved top bit of the stream id must be cleared on parse */
    buf[5] = (unsigned char)(buf[5] | 0x80);
    if (n_http2_frame_parse(buf, N_H2_FRAME_HEADER_LEN, &f, &consumed) != 1) failures++;
    if (f.stream_id != 0x7FFFFFFFu) failures++;
}

/* A truncated buffer needs more; an over-large length is rejected. */
static void test_boundaries(void) {
    unsigned char buf[16];
    N_H2_FRAME f;
    size_t consumed = 0;

    n_http2_frame_build_header(buf, sizeof(buf), 4, N_H2_DATA, 0, 3);
    if (n_http2_frame_parse(buf, N_H2_FRAME_HEADER_LEN, &f, &consumed) != 0) failures++; /* payload missing */
    if (n_http2_frame_parse(buf, 3, &f, &consumed) != 0) failures++;                     /* header incomplete */

    /* a declared length beyond the max frame size is a protocol error */
    buf[0] = 0xFF;
    buf[1] = 0xFF;
    buf[2] = 0xFF;
    if (n_http2_frame_parse(buf, sizeof(buf), &f, &consumed) != -1) failures++;

    /* guards */
    if (n_http2_frame_parse(NULL, 9, &f, &consumed) != -1) failures++;
    if (n_http2_frame_build_header(buf, 4, 0, N_H2_PING, 0, 0) != 0) failures++;                     /* out too small */
    if (n_http2_frame_build_header(buf, sizeof(buf), 0x01000000u, N_H2_DATA, 0, 1) != 0) failures++; /* 25-bit length */
}

/* SETTINGS payload parses into id/value pairs. */
static void test_settings(void) {
    unsigned char pay[12];
    N_H2_SETTING s[4];
    size_t count = 0;

    /* MAX_CONCURRENT_STREAMS = 100, INITIAL_WINDOW_SIZE = 65535 */
    pay[0] = 0x00;
    pay[1] = N_H2_SETTINGS_MAX_CONCURRENT_STREAMS;
    pay[2] = 0x00;
    pay[3] = 0x00;
    pay[4] = 0x00;
    pay[5] = 100;
    pay[6] = 0x00;
    pay[7] = N_H2_SETTINGS_INITIAL_WINDOW_SIZE;
    pay[8] = 0x00;
    pay[9] = 0x00;
    pay[10] = 0xFF;
    pay[11] = 0xFF;

    if (n_http2_settings_parse(pay, sizeof(pay), s, 4, &count) != 0) failures++;
    if (count != 2) failures++;
    if (s[0].id != N_H2_SETTINGS_MAX_CONCURRENT_STREAMS || s[0].value != 100) failures++;
    if (s[1].id != N_H2_SETTINGS_INITIAL_WINDOW_SIZE || s[1].value != 65535) failures++;

    /* an empty SETTINGS (an ACK carries none) is valid */
    if (n_http2_settings_parse(NULL, 0, s, 4, &count) != 0 || count != 0) failures++;

    /* a length not a multiple of 6 is a frame-size error */
    if (n_http2_settings_parse(pay, 5, s, 4, &count) != -1) failures++;
}

static void test_preface(void) {
    /* the client preface is a fixed 24-byte string */
    if (strlen(N_H2_PREFACE) != 24) failures++;
    if (memcmp(N_H2_PREFACE, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) != 0) failures++;
}

/* Return the value of the header named `name` in a decoded list, or NULL. */
static const char* find_hdr(const N_H2_HEADER* h, size_t n, const char* name) {
    size_t i;
    for (i = 0; i < n; i++)
        if (h[i].name && strcmp(h[i].name, name) == 0)
            return h[i].value;
    return NULL;
}

/* HPACK: encode a header list, decode it back, and check every field survives,
   including a value long enough to force a multi-byte HPACK integer length. */
static void test_hpack_roundtrip(void) {
    char big[301];
    N_H2_HEADER in[4];
    N_H2_HEADER out[8];
    unsigned char buf[1024];
    size_t w, count = 0;
    N_H2_HPACK* dec = n_http2_hpack_new(0);

    memset(big, 'x', 300);
    big[300] = '\0';
    in[0].name = (char*)":method";
    in[0].value = (char*)"GET";
    in[1].name = (char*)":path";
    in[1].value = (char*)"/a/b?c=d";
    in[2].name = (char*)"x-custom";
    in[2].value = (char*)"hello world";
    in[3].name = (char*)"x-big";
    in[3].value = big;

    w = n_http2_hpack_encode(in, 4, buf, sizeof(buf));
    if (w == 0) failures++;
    if (n_http2_hpack_decode(dec, buf, w, out, 8, &count) != 0) failures++;
    if (count != 4) failures++;
    if (!find_hdr(out, count, ":method") || strcmp(find_hdr(out, count, ":method"), "GET") != 0) failures++;
    if (!find_hdr(out, count, ":path") || strcmp(find_hdr(out, count, ":path"), "/a/b?c=d") != 0) failures++;
    if (!find_hdr(out, count, "x-custom") || strcmp(find_hdr(out, count, "x-custom"), "hello world") != 0) failures++;
    if (!find_hdr(out, count, "x-big") || strlen(find_hdr(out, count, "x-big")) != 300) failures++;

    n_http2_hpack_headers_free(out, count);
    n_http2_hpack_free(&dec);
}

/* HPACK: the RFC 7541 C.3 request sequence (no Huffman) on one context, which
   exercises indexed static fields and dynamic-table references across requests. */
static void test_hpack_rfc_requests(void) {
    /* C.3.1 */
    static const unsigned char r1[] = {0x82, 0x86, 0x84, 0x41, 0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};
    /* C.3.2: 0xbe re-references :authority from the dynamic table */
    static const unsigned char r2[] = {0x82, 0x86, 0x84, 0xbe, 0x58, 0x08, 0x6e, 0x6f, 0x2d, 0x63, 0x61, 0x63, 0x68, 0x65};
    /* C.3.3 */
    static const unsigned char r3[] = {0x82, 0x87, 0x85, 0xbf, 0x40, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79, 0x0c, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x76, 0x61, 0x6c, 0x75, 0x65};
    N_H2_HEADER out[16];
    size_t count = 0;
    N_H2_HPACK* dec = n_http2_hpack_new(0);

    if (n_http2_hpack_decode(dec, r1, sizeof(r1), out, 16, &count) != 0) failures++;
    if (count != 4) failures++;
    if (!find_hdr(out, count, ":method") || strcmp(find_hdr(out, count, ":method"), "GET") != 0) failures++;
    if (!find_hdr(out, count, ":authority") || strcmp(find_hdr(out, count, ":authority"), "www.example.com") != 0) failures++;
    n_http2_hpack_headers_free(out, count);

    if (n_http2_hpack_decode(dec, r2, sizeof(r2), out, 16, &count) != 0) failures++;
    if (count != 5) failures++;
    if (!find_hdr(out, count, ":authority") || strcmp(find_hdr(out, count, ":authority"), "www.example.com") != 0) failures++;
    if (!find_hdr(out, count, "cache-control") || strcmp(find_hdr(out, count, "cache-control"), "no-cache") != 0) failures++;
    n_http2_hpack_headers_free(out, count);

    if (n_http2_hpack_decode(dec, r3, sizeof(r3), out, 16, &count) != 0) failures++;
    if (count != 5) failures++;
    if (!find_hdr(out, count, ":path") || strcmp(find_hdr(out, count, ":path"), "/index.html") != 0) failures++;
    if (!find_hdr(out, count, "custom-key") || strcmp(find_hdr(out, count, "custom-key"), "custom-value") != 0) failures++;
    n_http2_hpack_headers_free(out, count);

    n_http2_hpack_free(&dec);
}

/* HPACK: RFC 7541 C.4.1 (Huffman-coded :authority) and C.6.1 (a Huffman-coded
   response with a date and URL, table size 256), which validate Huffman decode. */
static void test_hpack_rfc_huffman(void) {
    static const unsigned char req[] = {0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff};
    static const unsigned char resp[] = {
        0x48, 0x82, 0x64, 0x02, 0x58, 0x85, 0xae, 0xc3, 0x77, 0x1a, 0x4b, 0x61, 0x96, 0xd0, 0x7a, 0xbe,
        0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05, 0x95, 0x04, 0x0b, 0x81, 0x66, 0xe0, 0x82, 0xa6,
        0x2d, 0x1b, 0xff, 0x6e, 0x91, 0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8,
        0xe9, 0xae, 0x82, 0xae, 0x43, 0xd3};
    N_H2_HEADER out[16];
    size_t count = 0;
    N_H2_HPACK* dr = n_http2_hpack_new(0);
    N_H2_HPACK* ds = n_http2_hpack_new(256);

    if (n_http2_hpack_decode(dr, req, sizeof(req), out, 16, &count) != 0) failures++;
    if (!find_hdr(out, count, ":authority") || strcmp(find_hdr(out, count, ":authority"), "www.example.com") != 0) failures++;
    n_http2_hpack_headers_free(out, count);

    if (n_http2_hpack_decode(ds, resp, sizeof(resp), out, 16, &count) != 0) failures++;
    if (count != 4) failures++;
    if (!find_hdr(out, count, ":status") || strcmp(find_hdr(out, count, ":status"), "302") != 0) failures++;
    if (!find_hdr(out, count, "cache-control") || strcmp(find_hdr(out, count, "cache-control"), "private") != 0) failures++;
    if (!find_hdr(out, count, "date") || strcmp(find_hdr(out, count, "date"), "Mon, 21 Oct 2013 20:13:21 GMT") != 0) failures++;
    if (!find_hdr(out, count, "location") || strcmp(find_hdr(out, count, "location"), "https://www.example.com") != 0) failures++;
    n_http2_hpack_headers_free(out, count);

    n_http2_hpack_free(&dr);
    n_http2_hpack_free(&ds);
}

/* HPACK: a valid block with more fields than the caller's out[] capacity is
   rejected cleanly (the decoder frees the in-flight field and any already
   stored ones exactly once). ASan/LSan guards against a double free here. */
static void test_hpack_overflow(void) {
    /* RFC 7541 C.3.1 decodes to 4 header fields */
    static const unsigned char r1[] = {0x82, 0x86, 0x84, 0x41, 0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d};
    N_H2_HEADER out[2];
    size_t count = 123;
    N_H2_HPACK* dec = n_http2_hpack_new(0);

    /* out[] only holds 2, so the 3rd field overflows and the decode fails */
    if (n_http2_hpack_decode(dec, r1, sizeof(r1), out, 2, &count) != -1) failures++;

    n_http2_hpack_free(&dec);
}

/* HPACK: guards and a malformed block are rejected without a crash. */
static void test_hpack_guards(void) {
    static const unsigned char bad_index[] = {0x80};             /* indexed field, index 0 */
    static const unsigned char truncated[] = {0x40, 0x05, 0x61}; /* literal name len 5 but 1 byte */
    N_H2_HEADER out[4];
    size_t count = 0;
    N_H2_HPACK* dec = n_http2_hpack_new(0);

    if (n_http2_hpack_decode(dec, bad_index, sizeof(bad_index), out, 4, &count) != -1) failures++;
    if (n_http2_hpack_decode(dec, truncated, sizeof(truncated), out, 4, &count) != -1) failures++;
    if (n_http2_hpack_decode(NULL, bad_index, 1, out, 4, &count) != -1) failures++;

    n_http2_hpack_free(&dec);
}

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
                break;
            case 'v':
                fprintf(stderr, "ex_http2\n");
                exit(1);
            default:
                break;
        }
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    test_roundtrip();
    test_boundaries();
    test_settings();
    test_preface();
    test_hpack_roundtrip();
    test_hpack_rfc_requests();
    test_hpack_rfc_huffman();
    test_hpack_overflow();
    test_hpack_guards();

    if (failures) {
        n_log(LOG_ERR, "ex_http2: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_http2: all checks passed");
    return 0;
}
