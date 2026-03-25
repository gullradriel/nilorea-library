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
 *@example ex_accept_pool_client.c
 *@brief Client for accept pool testing: connects repeatedly to stress the server
 *@author Castagnier Mickael
 *@version 1.0
 *@date 11/03/2026
 *
 * Usage:
 *   ./ex_accept_pool_client -s HOST -p PORT -n 500 -t 8
 *   ./ex_accept_pool_client -s HOST -p PORT -n 500 -t 8 -e   (echo mode)
 *
 * Default mode: connect + close (measures pure accept throughput).
 * With -e: connect, send message, wait for echo, close.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_network_msg.h"
#include "nilorea/n_thread_pool.h"

/*! type of data message */
#define NETMSG_DATA 1

static int client_echo_mode = 0;

/*! shared state for client threads */
typedef struct CLIENT_STATE {
    /*! server host */
    char* host;
    /*! server port */
    char* port;
    /*! atomic counter of successful connections */
    size_t success_count;
    /*! atomic counter of failed connections */
    size_t fail_count;
    /*! mutex for counters */
    pthread_mutex_t lock;
} CLIENT_STATE;

/**
 *@brief Fast mode: connect and immediately close (measures pure accept throughput)
 *@param ptr pointer to CLIENT_STATE
 *@return NULL
 */
static void* client_worker_fast(void* ptr) {
    CLIENT_STATE* state = (CLIENT_STATE*)ptr;
    __n_assert(state, return NULL);

    NETWORK* netw = NULL;
    if (netw_connect(&netw, state->host, state->port, NETWORK_IPALL) != TRUE) {
        pthread_mutex_lock(&state->lock);
        state->fail_count++;
        pthread_mutex_unlock(&state->lock);
        return NULL;
    }

    pthread_mutex_lock(&state->lock);
    state->success_count++;
    pthread_mutex_unlock(&state->lock);

    netw_close(&netw);
    return NULL;
}

/**
 *@brief Echo mode: connect, send a message, receive echo, close
 *@param ptr pointer to CLIENT_STATE
 *@return NULL
 */
static void* client_worker_echo(void* ptr) {
    CLIENT_STATE* state = (CLIENT_STATE*)ptr;
    __n_assert(state, return NULL);

    NETWORK* netw = NULL;
    if (netw_connect(&netw, state->host, state->port, NETWORK_IPALL) != TRUE) {
        pthread_mutex_lock(&state->lock);
        state->fail_count++;
        pthread_mutex_unlock(&state->lock);
        return NULL;
    }

    netw_start_thr_engine(netw);

    /* build and send a message */
    NETW_MSG* msg = NULL;
    create_msg(&msg);
    if (msg) {
        add_int_to_msg(msg, NETMSG_DATA);
        N_STR* payload = char_to_nstr("HELLO_FROM_CLIENT");
        add_nstrptr_to_msg(msg, payload);
        N_STR* packed = make_str_from_msg(msg);
        delete_msg(&msg);
        if (packed) {
            netw_add_msg(netw, packed);
        }
    }

    /* wait for echo response */
    N_STR* response = netw_wait_msg(netw, 10000, 20000000);
    if (response) {
        free_nstr(&response);
        pthread_mutex_lock(&state->lock);
        state->success_count++;
        pthread_mutex_unlock(&state->lock);
    } else {
        pthread_mutex_lock(&state->lock);
        state->fail_count++;
        pthread_mutex_unlock(&state->lock);
        n_log(LOG_DEBUG, "no response from server");
    }

    netw_close(&netw);
    return NULL;
}

static void usage(void) {
    fprintf(stderr,
            "Usage: ex_accept_pool_client [options]\n"
            "  -s HOST     server address (required)\n"
            "  -p PORT     server port (required)\n"
            "  -n COUNT    number of connections (default: 500)\n"
            "  -t THREADS  concurrent client threads (default: 8)\n"
            "  -e          echo mode: send+recv messages (default: connect+close)\n"
            "  -V LEVEL    log level (default: LOG_NOTICE)\n"
            "  -h          show this help\n");
}

int main(int argc, char** argv) {
    char* host = NULL;
    char* port = NULL;
    int total = 500;
    int nb_threads = 8;
    int log_level = LOG_NOTICE;
    int opt;

    while ((opt = getopt(argc, argv, "hes:p:n:t:V:")) != -1) {
        switch (opt) {
            case 's':
                host = strdup(optarg);
                break;
            case 'p':
                port = strdup(optarg);
                break;
            case 'n':
                total = atoi(optarg);
                break;
            case 't':
                nb_threads = atoi(optarg);
                break;
            case 'e':
                client_echo_mode = 1;
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
                exit(1);
        }
    }

    if (!host || !port) {
        fprintf(stderr, "Error: -s HOST and -p PORT are required\n");
        usage();
        exit(1);
    }

    set_log_level(log_level);

#ifdef __linux__
    signal(SIGPIPE, SIG_IGN);
#endif

    CLIENT_STATE state;
    state.host = host;
    state.port = port;
    state.success_count = 0;
    state.fail_count = 0;
    pthread_mutex_init(&state.lock, NULL);

    THREAD_POOL* pool = new_thread_pool((size_t)nb_threads, (size_t)(total + 16));
    if (!pool) {
        n_log(LOG_ERR, "Failed to create thread pool");
        exit(1);
    }

    void* (*worker_func)(void*) = client_echo_mode ? &client_worker_echo : &client_worker_fast;

    n_log(LOG_NOTICE, "=== Starting %d connections to %s:%s (%d threads, %s mode) ===",
          total, host, port, nb_threads,
          client_echo_mode ? "echo" : "connect+close");

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (int i = 0; i < total; i++) {
        add_threaded_process(pool, worker_func, (void*)&state, NORMAL_PROC);
        if ((i + 1) % 100 == 0) {
            n_log(LOG_NOTICE, "submitted %d/%d connection tasks", i + 1, total);
        }
    }

    n_log(LOG_NOTICE, "All tasks submitted, waiting for completion...");
    wait_for_threaded_pool(pool);

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (double)(t_end.tv_sec - t_start.tv_sec) +
                     (double)(t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    n_log(LOG_NOTICE, "=== RESULTS ===");
    n_log(LOG_NOTICE, "Total: %d, Success: %zu, Failed: %zu",
          total, state.success_count, state.fail_count);
    n_log(LOG_NOTICE, "Time: %.3f seconds (%.1f conn/sec)",
          elapsed, (double)total / elapsed);

    destroy_threaded_pool(&pool, 500000);
    pthread_mutex_destroy(&state.lock);

    FreeNoLog(host);
    FreeNoLog(port);

    netw_unload();
    return 0;
}
