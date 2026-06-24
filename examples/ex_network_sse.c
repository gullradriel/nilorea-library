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
 *@example ex_network_sse.c
 *@brief SSE (Server-Sent Events) client example using Nilorea network module
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/03/2026
 */

#include "nilorea/n_str.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"

#include <getopt.h>
#include <string.h>

/*! maximum number of events to receive before stopping */
#define MAX_EVENTS 3

/*! counter for received events */
static int event_count = 0;
/*! pointer to connection for callback to stop */
static N_SSE_CONN* g_conn = NULL;

void usage(void) {
    fprintf(stderr,
            "     -V 'log level' : set the log level (LOG_NULL, LOG_NOTICE, LOG_INFO, LOG_ERR, LOG_DEBUG)\n"
            "     -h             : help\n");
}

#ifdef HAVE_OPENSSL
/*! @brief SSE event callback
 *  @param event the received SSE event
 *  @param conn the SSE connection
 *  @param user_data unused */
static void on_sse_event(N_SSE_EVENT* event, N_SSE_CONN* conn, void* user_data) {
    (void)user_data;
    g_conn = conn;
    event_count++;
    fprintf(stdout, "SSE event %d: type=%s data=%s id=%s\n",
            event_count,
            (event->event && event->event->data) ? event->event->data : "(default)",
            (event->data && event->data->data) ? event->data->data : "(empty)",
            (event->id && event->id->data) ? event->id->data : "(none)");

    if (event_count >= MAX_EVENTS) {
        n_sse_stop(conn);
    }
}
#endif

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
    n_log(LOG_INFO, "Connecting to SSE endpoint https://sse.dev/test ...");

    g_conn = n_sse_connect("sse.dev", "443", "/test", 1, NULL, on_sse_event, NULL);
    if (!g_conn) {
        fprintf(stdout, "SSE server unreachable, skipping\n");
        exit(0);
    }

    fprintf(stdout, "SSE example completed, received %d events\n", event_count);
    n_sse_conn_free(&g_conn);
    n_log(LOG_INFO, "SSE example completed successfully");
#else
    (void)log_level;
    fprintf(stdout, "OpenSSL not available, skipping SSE test\n");
#endif

    exit(0);
}
