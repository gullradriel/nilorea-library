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
 *@example ex_network_reactor.c
 *@brief Single-threaded epoll reactor demo (n_reactor + netw_accept_into_reactor)
 *@author Castagnier Mickael
 *@version 1.0
 *@date 27/04/2026
 *
 * Usage:
 *   server: ./ex_network_reactor -a [ADDR] -p PORT -n N
 *   client: ./ex_network_reactor -s HOST   -p PORT -n N
 *
 * Server mode wires up the n_reactor:
 *   - n_reactor_new() allocates the epoll/eventfd state.
 *   - n_reactor_run_thread_entry runs the event loop on its own thread.
 *   - netw_accept_into_reactor() accepts + registers each client in one
 *     call (internally calls n_reactor_register).
 *   - netw_get_msg() drains complete frames the reactor pushed onto
 *     netw->recv_buf; netw_add_msg() echoes them back. The send-side
 *     wakeup (n_reactor_notify_send) is triggered automatically by
 *     netw_add_msg when the connection is in reactor_mode.
 *   - netw_close() takes the synchronous close path
 *     (n_reactor_close_netw_sync) automatically when reactor_mode is
 *     set, so no special teardown code is needed here.
 *   - n_reactor_get_stats() prints lifetime counters at the end.
 *
 * Reactor is Linux/Android only. On other platforms n_reactor_new
 * returns NULL with a LOG_INFO and the example exits 0 (treated as a
 * skip rather than a failure).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_reactor.h"

#define MODE_SERVER 0
#define MODE_CLIENT 1

static volatile sig_atomic_t g_running = 1;

static void sighandler(int sig) {
    (void)sig;
    g_running = 0;
}

static void usage(void) {
    fprintf(stderr,
            "Usage: ex_network_reactor [options]\n"
            "  -a ADDR     server mode, bind ADDR (use \"\" for all interfaces)\n"
            "  -s HOST     client mode, connect to HOST\n"
            "  -p PORT     port (required)\n"
            "  -n COUNT    server: connections to handle, client: connect attempts (default 5)\n"
            "  -V LEVEL    log level: LOG_DEBUG/LOG_INFO/LOG_NOTICE/LOG_ERR (default LOG_NOTICE)\n"
            "  -h          show this help\n");
}

/**
 *@brief Server-side: accept up to `target` connections into the reactor,
 *       echo one message back per client, close. The reactor itself runs
 *       on a dedicated thread; this function is the "game thread" that
 *       drains recv queues and queues replies.
 *@param addr bind address (may be NULL/empty for all interfaces)
 *@param port port to listen on
 *@param target number of connections to handle before stopping
 *@return 0 on success, non-zero on error
 */
static int run_server(const char* addr, const char* port, int target) {
    NETWORK* listener = NULL;
    if (netw_make_listening(&listener, (char*)((addr && addr[0]) ? addr : NULL),
                            (char*)port, 64, NETWORK_IPALL) == FALSE) {
        n_log(LOG_ERR, "netw_make_listening failed on %s:%s", addr ? addr : "*", port);
        return 1;
    }
    n_log(LOG_NOTICE, "reactor server listening on %s:%s (target %d connections)",
          addr && addr[0] ? addr : "*", port, target);

    /* Reactor + dedicated I/O thread. n_reactor_new returns NULL on
     * non-Linux platforms (the public function is still safe to call,
     * it just yields a polite no-op so callers don't need #ifdefs). */
    n_reactor* reactor = n_reactor_new(0);
    if (!reactor) {
        n_log(LOG_NOTICE, "n_reactor unavailable on this platform, skipping (exit 0)");
        netw_close(&listener);
        netw_unload();
        return 0;
    }

    pthread_t reactor_thr;
    if (pthread_create(&reactor_thr, NULL, &n_reactor_run_thread_entry, reactor) != 0) {
        n_log(LOG_ERR, "pthread_create(reactor): %s", strerror(errno));
        n_reactor_destroy(&reactor);
        netw_close(&listener);
        netw_unload();
        return 2;
    }

    /* Tracking list of active reactor-registered clients. The reactor
     * owns I/O; we own the per-connection lifecycle (accept -> echo ->
     * close). */
    LIST* active = new_generic_list(MAX_LIST_ITEMS);
    if (!active) {
        n_log(LOG_ERR, "new_generic_list failed");
        n_reactor_stop(reactor);
        pthread_join(reactor_thr, NULL);
        n_reactor_destroy(&reactor);
        netw_close(&listener);
        netw_unload();
        return 3;
    }

    int handled = 0;
    while (g_running && handled < target) {
        /* Step 1: try to accept (500 ms select timeout, short enough
         * that we keep draining recv queues responsively). */
        int retval = 0;
        NETWORK* client = netw_accept_into_reactor(listener, 0, 0, 500, reactor, &retval);
        if (client) {
            n_log(LOG_INFO, "accepted client fd=%d (now %d active)",
                  client->link.sock, (int)(active->nb_items + 1));
            list_push(active, client, NULL);
        }

        /* Step 2: drain any messages the reactor posted onto active
         * clients' recv_buf. Echo them back via netw_add_msg, that
         * calls n_reactor_notify_send under the hood, waking the
         * reactor so the send queue gets flushed. */
        LIST_NODE* node = active->start;
        while (node) {
            LIST_NODE* next = node->next;
            NETWORK* c = (NETWORK*)node->ptr;
            N_STR* msg = netw_get_msg(c);
            if (msg) {
                n_log(LOG_INFO, "echoing %zu bytes back to fd=%d",
                      msg->length, c->link.sock);
                if (netw_add_msg(c, msg) != TRUE) {
                    free_nstr(&msg);
                }
                /* Give the reactor a brief window to flush the echo
                 * before the close handshake severs SHUT_WR. */
                u_sleep(20000);
                /* netw_close calls n_reactor_close_netw_sync internally
                 * because c->reactor_mode is set, no manual unregister
                 * needed. */
                netw_close(&c);
                /* remove_list_node_f unlinks `node`, frees the
                 * LIST_NODE struct, and returns the void* it held
                 * (which we already netw_close'd). */
                (void)remove_list_node_f(active, node);
                handled++;
                n_log(LOG_NOTICE, "handled %d/%d connections", handled, target);
            }
            node = next;
        }
    }

    /* Anything still registered didn't get its echo before the target
     * was reached or SIGINT fired. Close them cleanly. */
    LIST_NODE* node = active->start;
    while (node) {
        LIST_NODE* next = node->next;
        NETWORK* c = (NETWORK*)node->ptr;
        netw_close(&c);
        (void)remove_list_node_f(active, node);
        node = next;
    }
    list_destroy(&active);

    n_reactor_stats stats;
    n_reactor_get_stats(reactor, &stats);
    n_log(LOG_NOTICE,
          "reactor stats: events=%lld registered=%lld unregistered=%lld "
          "wake=%lld writes_partial=%lld reads_partial=%lld",
          stats.events_processed, stats.fds_registered, stats.fds_unregistered,
          stats.wake_signals, stats.writes_partial, stats.reads_partial);

    n_reactor_stop(reactor);
    pthread_join(reactor_thr, NULL);
    n_reactor_destroy(&reactor);

    netw_close(&listener);
    netw_unload();
    n_log(LOG_NOTICE, "reactor server done (%d connections handled)", handled);
    return 0;
}

/**
 *@brief Client-side: connect, send one message, wait for the echo,
 *       close. Repeats `attempts` times. Uses the standard thread
 *       engine (the reactor is server-side opt-in only).
 *@param host server address
 *@param port server port
 *@param attempts how many sequential connect/echo/close cycles to run
 *@return 0 on success, non-zero on error
 */
static int run_client(const char* host, const char* port, int attempts) {
    int rc = 0;
    for (int i = 0; g_running && i < attempts; i++) {
        NETWORK* netw = NULL;
        if (netw_connect(&netw, (char*)host, (char*)port, NETWORK_IPALL) != TRUE) {
            n_log(LOG_ERR, "client connect %d/%d to %s:%s failed", i + 1, attempts, host, port);
            rc = 4;
            continue;
        }
        netw_start_thr_engine(netw);

        char payload[64];
        snprintf(payload, sizeof(payload), "hello-from-client-%d", i + 1);
        N_STR* out = char_to_nstr(payload);
        if (netw_add_msg(netw, out) != TRUE) {
            free_nstr(&out);
        }

        N_STR* in = netw_wait_msg(netw, 25000, 5000000);
        if (in) {
            n_log(LOG_NOTICE, "client %d: echo received (%zu bytes)", i + 1, in->length);
            free_nstr(&in);
        } else {
            n_log(LOG_ERR, "client %d: no echo within timeout", i + 1);
            rc = 5;
        }
        netw_close(&netw);
    }
    netw_unload();
    return rc;
}

int main(int argc, char** argv) {
    char* addr = NULL;
    char* host = NULL;
    char* port = NULL;
    int mode = MODE_SERVER;
    int count = 5;
    int log_level = LOG_NOTICE;
    int explicit_server = 0;
    int opt;

    while ((opt = getopt(argc, argv, "ha:s:p:n:V:")) != -1) {
        switch (opt) {
            case 'a':
                mode = MODE_SERVER;
                explicit_server = 1;
                addr = strdup(optarg);
                break;
            case 's':
                mode = MODE_CLIENT;
                host = strdup(optarg);
                break;
            case 'p':
                port = strdup(optarg);
                break;
            case 'n':
                count = atoi(optarg);
                if (count <= 0) count = 1;
                break;
            case 'V':
                if (!strcmp(optarg, "LOG_DEBUG"))
                    log_level = LOG_DEBUG;
                else if (!strcmp(optarg, "LOG_INFO"))
                    log_level = LOG_INFO;
                else if (!strcmp(optarg, "LOG_NOTICE"))
                    log_level = LOG_NOTICE;
                else if (!strcmp(optarg, "LOG_ERR"))
                    log_level = LOG_ERR;
                break;
            case 'h':
            default:
                usage();
                FreeNoLog(addr);
                FreeNoLog(host);
                FreeNoLog(port);
                return 1;
        }
    }
    (void)explicit_server;

    if (!port) {
        fprintf(stderr, "ex_network_reactor: -p PORT is required\n");
        usage();
        FreeNoLog(addr);
        FreeNoLog(host);
        return 1;
    }

    set_log_level(log_level);
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
#ifdef __linux__
    signal(SIGPIPE, SIG_IGN);
#endif

    int rc;
    if (mode == MODE_CLIENT) {
        rc = run_client(host, port, count);
    } else {
        rc = run_server(addr, port, count);
    }

    FreeNoLog(addr);
    FreeNoLog(host);
    FreeNoLog(port);
    return rc;
}
