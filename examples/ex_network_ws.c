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
 *@example ex_network_ws.c
 *@brief WebSocket client example using Nilorea network module
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/03/2026
 */

#include "nilorea/n_str.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"

#include <getopt.h>
#include <string.h>

void usage(void) {
    fprintf(stderr,
            "     -V 'log level' : set the log level (LOG_NULL, LOG_NOTICE, LOG_INFO, LOG_ERR, LOG_DEBUG)\n"
            "     -h             : help\n");
}

/* Stateless frame-codec regression (no network): build then parse, both masked
   and not, with extended lengths, partial buffers, and a back-to-back pair. */
static int test_frame_codec(void) {
    int fails = 0;
    unsigned char out[512];
    unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
    N_WS_FRAME f;
    size_t consumed = 0;

    /* unmasked text frame round-trips */
    {
        size_t w = n_ws_frame_build(1, N_WS_OP_TEXT, 0, (const unsigned char*)"hello", 5, NULL, out, sizeof(out));
        if (w != 7) fails++; /* 2 header + 5 payload */
        if (n_ws_frame_parse(out, w, &f, &consumed) != 1) fails++;
        if (f.fin != 1 || f.opcode != N_WS_OP_TEXT || f.masked != 0 || f.payload_len != 5) fails++;
        if (consumed != 7 || memcmp(f.payload, "hello", 5) != 0) fails++;
    }
    /* masked frame: parse then unmask recovers the plaintext */
    {
        unsigned char plain[8];
        size_t w = n_ws_frame_build(1, N_WS_OP_BINARY, 1, (const unsigned char*)"abcd", 4, mask, out, sizeof(out));
        if (w != 10) fails++; /* 2 header + 4 mask + 4 payload */
        if (n_ws_frame_parse(out, w, &f, &consumed) != 1) fails++;
        if (!f.masked || f.opcode != N_WS_OP_BINARY || f.payload_len != 4) fails++;
        n_ws_unmask(plain, f.payload, (size_t)f.payload_len, f.mask);
        if (memcmp(plain, "abcd", 4) != 0) fails++;
    }
    /* extended 16-bit length path (>125 bytes) */
    {
        unsigned char big[200];
        size_t i, w;
        for (i = 0; i < sizeof(big); i++) big[i] = (unsigned char)(i & 0xFF);
        w = n_ws_frame_build(1, N_WS_OP_BINARY, 0, big, sizeof(big), NULL, out, sizeof(out));
        if (w != 4 + sizeof(big)) fails++; /* 2 header + 2 ext len */
        if (n_ws_frame_parse(out, w, &f, &consumed) != 1) fails++;
        if (f.payload_len != sizeof(big) || memcmp(f.payload, big, sizeof(big)) != 0) fails++;
    }
    /* a truncated buffer needs more data (returns 0) */
    {
        size_t w = n_ws_frame_build(1, N_WS_OP_TEXT, 0, (const unsigned char*)"hello", 5, NULL, out, sizeof(out));
        if (n_ws_frame_parse(out, w - 2, &f, &consumed) != 0) fails++;
        if (n_ws_frame_parse(out, 1, &f, &consumed) != 0) fails++;
    }
    /* two frames back-to-back: parse the first, advance by consumed, parse the second */
    {
        size_t w1 = n_ws_frame_build(1, N_WS_OP_TEXT, 0, (const unsigned char*)"one", 3, NULL, out, sizeof(out));
        size_t w2 = n_ws_frame_build(1, N_WS_OP_PING, 0, (const unsigned char*)"pp", 2, NULL, out + w1, sizeof(out) - w1);
        size_t total = w1 + w2;
        if (n_ws_frame_parse(out, total, &f, &consumed) != 1 || consumed != w1) fails++;
        if (memcmp(f.payload, "one", 3) != 0) fails++;
        if (n_ws_frame_parse(out + consumed, total - consumed, &f, &consumed) != 1) fails++;
        if (f.opcode != N_WS_OP_PING || memcmp(f.payload, "pp", 2) != 0) fails++;
    }

    if (fails)
        n_log(LOG_ERR, "ws frame codec: %d failure(s)", fails);
    else
        n_log(LOG_NOTICE, "ws frame codec: all checks passed");
    return fails;
}

int main(int argc, char* argv[]) {
    int log_level = LOG_ERR;
    int getoptret = 0;

    while ((getoptret = getopt(argc, argv, "hV:")) != EOF) {
        switch (getoptret) {
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 8)) {
                    log_level = LOG_NULL;
                } else if (!strncmp("LOG_NOTICE", optarg, 10)) {
                    log_level = LOG_NOTICE;
                } else if (!strncmp("LOG_INFO", optarg, 8)) {
                    log_level = LOG_INFO;
                } else if (!strncmp("LOG_ERR", optarg, 7)) {
                    log_level = LOG_ERR;
                } else if (!strncmp("LOG_DEBUG", optarg, 9)) {
                    log_level = LOG_DEBUG;
                } else {
                    fprintf(stderr, "%s is not a valid log level.\n", optarg);
                    exit(1);
                }
                break;
            case 'h':
            default:
                usage();
                exit(1);
        }
    }
    set_log_level(log_level);

    /* the stateless frame codec runs without a network and must always pass */
    if (test_frame_codec() != 0)
        exit(1);

#ifdef HAVE_OPENSSL
    n_log(LOG_INFO, "Connecting to wss://echo.websocket.org:443/ ...");

    N_WS_CONN* ws = n_ws_connect("echo.websocket.org", "443", "/", 1);
    if (!ws) {
        fprintf(stdout, "WebSocket echo server unreachable, skipping\n");
        exit(0);
    }

    /* read the welcome/greeting message the server sends on connect */
    N_WS_MESSAGE greeting;
    memset(&greeting, 0, sizeof(greeting));
    if (n_ws_recv(ws, &greeting) == 0) {
        n_log(LOG_INFO, "Server greeting (opcode %d): %s", greeting.opcode, greeting.payload ? greeting.payload->data : "(empty)");
        free_nstr(&greeting.payload);
    }

    const char* test_msg = "nilorea ws test";
    n_log(LOG_INFO, "Sending: %s", test_msg);
    if (n_ws_send(ws, test_msg, strlen(test_msg), N_WS_OP_TEXT) != 0) {
        n_log(LOG_ERR, "Failed to send WebSocket message");
        n_ws_conn_free(&ws);
        exit(1);
    }

    N_WS_MESSAGE msg;
    memset(&msg, 0, sizeof(msg));
    if (n_ws_recv(ws, &msg) == 0) {
        n_log(LOG_INFO, "Received (opcode %d): %s", msg.opcode, msg.payload ? msg.payload->data : "(empty)");
        if (msg.payload && strcmp(msg.payload->data, test_msg) == 0) {
            fprintf(stdout, "WebSocket echo test PASSED\n");
        } else {
            fprintf(stdout, "WebSocket echo test: got response (opcode %d)\n", msg.opcode);
        }
        free_nstr(&msg.payload);
    } else {
        n_log(LOG_ERR, "Failed to receive WebSocket message");
        n_ws_conn_free(&ws);
        exit(1);
    }

    n_ws_conn_free(&ws);
    n_log(LOG_INFO, "WebSocket example completed successfully");
#else
    (void)log_level;
    fprintf(stdout, "OpenSSL not available, skipping WebSocket test\n");
#endif

    exit(0);
}
