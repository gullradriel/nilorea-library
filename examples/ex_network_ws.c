/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
