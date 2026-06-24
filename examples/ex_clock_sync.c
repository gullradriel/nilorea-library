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
 *@example ex_clock_sync.c
 *@brief Clock synchronization demo using UDP networking
 *
 * Demonstrates the N_CLOCK_SYNC module by running a server (time authority)
 * and a client that estimates the clock offset between them.
 *
 * The client periodically sends sync requests containing its local timestamp.
 * The server echoes back the client timestamp along with its own timestamp.
 * The client feeds these into N_CLOCK_SYNC to compute the median offset and RTT.
 *
 * Usage:
 *   Server:  ./ex_clock_sync -p 12345
 *   Client:  ./ex_clock_sync -s 127.0.0.1 -p 12345
 *
 * Use -o OFFSET on the server to simulate a clock offset (seconds).
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 14/03/2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_clock_sync.h"

#define MODE_SERVER 0
#define MODE_CLIENT 1

/*! sync request message: "SYNC client_send_time\n" */
#define MSG_SYNC "SYNC"
/*! sync response message: "RESP client_send_time server_time\n" */
#define MSG_RESP "RESP"

static void usage(const char* prog) {
    fprintf(stderr,
            "Usage:\n"
            "  Server: %s -p PORT [-o OFFSET] [-V LOGLEVEL]\n"
            "  Client: %s -s ADDRESS -p PORT [-n COUNT] [-V LOGLEVEL]\n"
            "\n"
            "Options:\n"
            "  -p PORT      port to use\n"
            "  -s ADDRESS   server address (client mode)\n"
            "  -o OFFSET    simulated clock offset in seconds (server only, default: 0.42)\n"
            "  -n COUNT     number of sync rounds (client only, default: 20)\n"
            "  -V LEVEL     log level: LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_ERR\n"
            "  -h           show this help\n",
            prog, prog);
}

static double get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void run_server(char* port, double fake_offset, int nb_rounds) {
    NETWORK* netw = NULL;

    n_log(LOG_NOTICE, "Server: binding UDP on port %s (simulated offset: %.4f s)", port, fake_offset);

    if (netw_bind_udp(&netw, NULL, port, NETWORK_IPALL) == FALSE) {
        n_log(LOG_ERR, "Server: failed to bind UDP on port %s", port);
        return;
    }

    n_log(LOG_NOTICE, "Server: listening for sync requests...");

    for (int i = 0; i < nb_rounds; i++) {
        char buf[256] = "";
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);

        ssize_t received = netw_udp_recvfrom(netw, buf, sizeof(buf) - 1,
                                             (struct sockaddr*)&client_addr, &client_len);
        if (received <= 0) {
            n_log(LOG_ERR, "Server: recv error");
            continue;
        }
        buf[received] = '\0';

        /* parse: "SYNC client_send_time" */
        double client_send_time = 0.0;
        if (sscanf(buf, MSG_SYNC " %lf", &client_send_time) != 1) {
            n_log(LOG_ERR, "Server: malformed request: %s", buf);
            continue;
        }

        /* server time = local time + fake offset to simulate a different clock */
        double server_time = get_time() + fake_offset;

        /* respond: "RESP client_send_time server_time" */
        char resp[256];
        snprintf(resp, sizeof(resp), MSG_RESP " %.9f %.9f", client_send_time, server_time);

        ssize_t sent = netw_udp_sendto(netw, resp, (uint32_t)strlen(resp),
                                       (struct sockaddr*)&client_addr, client_len);
        if (sent <= 0) {
            n_log(LOG_ERR, "Server: send error");
        } else {
            n_log(LOG_DEBUG, "Server: responded to sync #%d (server_time=%.4f)", i + 1, server_time);
        }
    }

    n_log(LOG_NOTICE, "Server: done after %d rounds", nb_rounds);
    netw_close(&netw);
}

static void run_client(char* server, char* port, int nb_rounds) {
    NETWORK* netw = NULL;

    n_log(LOG_NOTICE, "Client: connecting to %s:%s for %d sync rounds", server, port, nb_rounds);

    if (netw_connect_udp(&netw, server, port, NETWORK_IPALL) != TRUE) {
        n_log(LOG_ERR, "Client: failed to connect UDP to %s:%s", server, port);
        return;
    }

    N_CLOCK_SYNC* cs = n_clock_sync_new();
    if (!cs) {
        n_log(LOG_ERR, "Client: failed to create clock sync estimator");
        netw_close(&netw);
        return;
    }

    n_log(LOG_NOTICE, "Client: starting clock synchronization...");
    n_log(LOG_NOTICE, "");
    n_log(LOG_NOTICE, " Round |  RTT (ms) | Offset (ms) | Est.Offset (ms) | Est.RTT (ms)");
    n_log(LOG_NOTICE, "-------+-----------+-------------+-----------------+-------------");

    int rounds_done = 0;
    while (rounds_done < nb_rounds) {
        double now = get_time();

        if (!n_clock_sync_should_send(cs, now)) {
            u_sleep(50000); /* 50ms poll */
            continue;
        }

        /* send sync request */
        double send_time = get_time();
        n_clock_sync_mark_sent(cs, send_time);

        char msg[256];
        snprintf(msg, sizeof(msg), MSG_SYNC " %.9f", send_time);
        ssize_t sent = send_udp_data(netw, msg, (uint32_t)strlen(msg));
        if (sent <= 0) {
            n_log(LOG_ERR, "Client: send error");
            break;
        }

        /* wait for response */
        char buf[256] = "";
        ssize_t received = recv_udp_data(netw, buf, sizeof(buf) - 1);
        if (received <= 0) {
            n_log(LOG_ERR, "Client: recv error or timeout");
            rounds_done++;
            continue;
        }
        buf[received] = '\0';

        double recv_time = get_time();

        /* parse: "RESP client_send_time server_time" */
        double resp_client_time = 0.0, resp_server_time = 0.0;
        if (sscanf(buf, MSG_RESP " %lf %lf", &resp_client_time, &resp_server_time) != 2) {
            n_log(LOG_ERR, "Client: malformed response: %s", buf);
            rounds_done++;
            continue;
        }

        /* feed the sample into the estimator */
        n_clock_sync_process_response(cs, resp_client_time, resp_server_time, recv_time);

        rounds_done++;

        double raw_rtt = (recv_time - resp_client_time) * 1000.0;
        double raw_offset = (resp_server_time + (recv_time - resp_client_time) / 2.0 - recv_time) * 1000.0;

        n_log(LOG_NOTICE, " %5d | %9.3f | %11.3f | %15.3f | %11.3f",
              rounds_done,
              raw_rtt,
              raw_offset,
              cs->estimated_offset * 1000.0,
              cs->estimated_rtt * 1000.0);
    }

    n_log(LOG_NOTICE, "-------+-----------+-------------+-----------------+-------------");
    n_log(LOG_NOTICE, "");
    n_log(LOG_NOTICE, "Final estimated offset: %.4f ms (%.6f s)", cs->estimated_offset * 1000.0, cs->estimated_offset);
    n_log(LOG_NOTICE, "Final estimated RTT:    %.4f ms (%.6f s)", cs->estimated_rtt * 1000.0, cs->estimated_rtt);
    n_log(LOG_NOTICE, "");

    /* demonstrate n_clock_sync_server_time */
    double local_now = get_time();
    double est_server_now = n_clock_sync_server_time(cs, local_now);
    n_log(LOG_NOTICE, "Local time:             %.6f", local_now);
    n_log(LOG_NOTICE, "Estimated server time:  %.6f", est_server_now);
    n_log(LOG_NOTICE, "Difference:             %.4f ms", (est_server_now - local_now) * 1000.0);

    n_clock_sync_delete(&cs);
    netw_close(&netw);
}

int main(int argc, char* argv[]) {
    int mode = -1;
    char* server = NULL;
    char* port = NULL;
    int nb_rounds = 20;
    double fake_offset = 0.42;
    int log_level = LOG_NOTICE;
    int getoptret = 0;

    set_log_level(LOG_NOTICE);

    if (argc == 1) {
        usage(argv[0]);
        return 1;
    }

    while ((getoptret = getopt(argc, argv, "hs:p:n:o:V:")) != EOF) {
        switch (getoptret) {
            case 'h':
                usage(argv[0]);
                return 0;
            case 's':
                server = strdup(optarg);
                mode = MODE_CLIENT;
                break;
            case 'p':
                port = strdup(optarg);
                break;
            case 'n':
                nb_rounds = atoi(optarg);
                if (nb_rounds < 1) nb_rounds = 1;
                break;
            case 'o':
                fake_offset = atof(optarg);
                break;
            case 'V':
                if (!strncmp("LOG_DEBUG", optarg, 9)) {
                    log_level = LOG_DEBUG;
                } else if (!strncmp("LOG_INFO", optarg, 8)) {
                    log_level = LOG_INFO;
                } else if (!strncmp("LOG_NOTICE", optarg, 10)) {
                    log_level = LOG_NOTICE;
                } else if (!strncmp("LOG_ERR", optarg, 7)) {
                    log_level = LOG_ERR;
                } else {
                    fprintf(stderr, "Unknown log level: %s\n", optarg);
                    return 1;
                }
                set_log_level(log_level);
                break;
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (!port) {
        fprintf(stderr, "Error: -p PORT is required\n");
        usage(argv[0]);
        return 1;
    }

    if (mode != MODE_CLIENT) {
        mode = MODE_SERVER;
    }

    if (mode == MODE_SERVER) {
        run_server(port, fake_offset, nb_rounds);
    } else {
        run_client(server, port, nb_rounds);
    }

    FreeNoLog(server);
    FreeNoLog(port);

    netw_unload();

    return 0;
}
