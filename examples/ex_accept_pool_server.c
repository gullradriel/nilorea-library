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
 *@example ex_accept_pool_server.c
 *@brief Accept pool example: three modes of accept + client handling
 *@author Castagnier Mickael
 *@version 1.0
 *@date 11/03/2026
 *
 * Usage:
 *   ./ex_accept_pool_server -p PORT -m single-inline -n 500
 *   ./ex_accept_pool_server -p PORT -m single-pool -n 500 -t 4
 *   ./ex_accept_pool_server -p PORT -m pooled -n 500 -t 4
 *
 * Modes:
 *   single-inline - one thread doing accept + handle client in the same thread.
 *                   Both accept and handling are fully serialized.
 *                   Best for: low-traffic servers, simple protocols, debugging.
 *
 *   single-pool   - one thread doing accept, dispatching to a thread pool.
 *                   Accept is serialized, handling is parallelized.
 *                   Best for: most servers where per-connection work dominates.
 *
 *   pooled        - N threads calling accept in parallel (nginx-style),
 *                   dispatching to a thread pool for client handling.
 *                   Both accept and handling are parallelized.
 *                   Best for: high connection rates over real networks, SSL/TLS
 *                   servers, multi-socket NUMA systems.
 *
 * Performance notes:
 *   On localhost, accept() is near-instant. A single accept thread can drain
 *   the kernel queue faster than clients fill it, so single-pool often
 *   outperforms pooled mode due to less thread contention. The pooled accept
 *   advantage appears over real networks, with SSL handshakes, or at very
 *   high connection rates (10k+ conn/sec) where the kernel accept lock
 *   becomes the bottleneck.
 *
 * See the ACCEPT_POOL doxygen module documentation for detailed guidance
 * on choosing the right architecture for your use case.
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
#include "nilorea/n_network_accept_pool.h"
#include "nilorea/n_thread_pool.h"

static volatile int server_running = 1;
static int total_target = 500;

/*! mode constants */
#define MODE_SINGLE_INLINE 0
#define MODE_SINGLE_POOL 1
#define MODE_POOLED 2

/*! data passed to accept callback and worker threads */
typedef struct CALLBACK_DATA {
    /*! atomic counter of handled connections */
    size_t handled;
    /*! mutex for counter */
    pthread_mutex_t lock;
    /*! thread pool for dispatching (modes single-pool and pooled) */
    THREAD_POOL* worker_pool;
} CALLBACK_DATA;

/**
 *@brief Handle a single client: read one message, echo it back, close.
 *@param client the accepted NETWORK connection
 */
static void handle_client(NETWORK* client) {
    __n_assert(client, return);

    netw_start_thr_engine(client);

    N_STR* msg = netw_wait_msg(client, 10000, 20000000);
    if (msg) {
        netw_add_msg(client, msg);
        u_sleep(50000);
    }

    netw_close(&client);
}

/*! data passed to worker thread function */
typedef struct WORKER_ARG {
    /*! the accepted connection */
    NETWORK* client;
    /*! back pointer to callback data for counting */
    CALLBACK_DATA* cb_data;
} WORKER_ARG;

/**
 *@brief Worker thread function: handles a client and updates counter.
 *@param ptr pointer to WORKER_ARG (freed by this function)
 *@return NULL
 */
static void* worker_handle_client(void* ptr) {
    WORKER_ARG* arg = (WORKER_ARG*)ptr;
    __n_assert(arg, return NULL);

    handle_client(arg->client);

    pthread_mutex_lock(&arg->cb_data->lock);
    arg->cb_data->handled++;
    size_t h = arg->cb_data->handled;
    pthread_mutex_unlock(&arg->cb_data->lock);

    if (h % 100 == 0) {
        n_log(LOG_NOTICE, "handled %zu connections", h);
    }

    Free(arg);
    return NULL;
}

/**
 *@brief Dispatch a client to the thread pool for handling.
 *@param client the accepted NETWORK connection
 *@param cb_data callback data with thread pool and counters
 */
static void dispatch_to_pool(NETWORK* client, CALLBACK_DATA* cb_data) {
    __n_assert(client, return);
    __n_assert(cb_data, netw_close(&client); return);
    __n_assert(cb_data->worker_pool, netw_close(&client); return);

    WORKER_ARG* arg = NULL;
    Malloc(arg, WORKER_ARG, 1);
    if (!arg) {
        n_log(LOG_ERR, "failed to allocate worker arg");
        netw_close(&client);
        return;
    }
    arg->client = client;
    arg->cb_data = cb_data;

    add_threaded_process(cb_data->worker_pool, &worker_handle_client, (void*)arg, NORMAL_PROC);
}

/**
 *@brief Handle client inline in the accept thread and update counter.
 *@param client the accepted NETWORK connection
 *@param cb_data callback data with counters
 */
static void handle_inline_and_count(NETWORK* client, CALLBACK_DATA* cb_data) {
    __n_assert(cb_data, netw_close(&client); return);

    handle_client(client);

    pthread_mutex_lock(&cb_data->lock);
    cb_data->handled++;
    size_t h = cb_data->handled;
    pthread_mutex_unlock(&cb_data->lock);

    if (h % 100 == 0) {
        n_log(LOG_NOTICE, "handled %zu connections", h);
    }
}

/**
 *@brief Accept pool callback: dispatches to thread pool.
 *@param conn the accepted NETWORK connection
 *@param user_data pointer to CALLBACK_DATA
 */
static void on_accept_pooled(NETWORK* conn, void* user_data) {
    CALLBACK_DATA* data = (CALLBACK_DATA*)user_data;
    __n_assert(data, netw_close(&conn); return);
    dispatch_to_pool(conn, data);
}

static void sighandler(int sig) {
    (void)sig;
    server_running = 0;
}

static void usage(void) {
    fprintf(stderr,
            "Usage: ex_accept_pool_server [options]\n"
            "  -p PORT     port to listen on (required)\n"
            "  -a ADDR     address to bind to (optional, default: all)\n"
            "  -m MODE     'single-inline', 'single-pool', or 'pooled' (default: single-inline)\n"
            "  -n COUNT    number of connections to handle (default: 500)\n"
            "  -t THREADS  number of threads for pool/accept (default: 4)\n"
            "  -V LEVEL    log level: LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_ERR (default: LOG_NOTICE)\n"
            "  -h          show this help\n");
}

int main(int argc, char** argv) {
    char* addr = NULL;
    char* port = NULL;
    char* mode_str = "single-inline";
    int nb_threads = 4;
    int log_level = LOG_NOTICE;
    int opt;

    while ((opt = getopt(argc, argv, "hp:a:m:n:t:V:")) != -1) {
        switch (opt) {
            case 'p':
                port = strdup(optarg);
                break;
            case 'a':
                addr = strdup(optarg);
                break;
            case 'm':
                mode_str = optarg;
                break;
            case 'n':
                total_target = atoi(optarg);
                break;
            case 't':
                nb_threads = atoi(optarg);
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

    if (!port) {
        fprintf(stderr, "Error: -p PORT is required\n");
        usage();
        exit(1);
    }

    int mode = MODE_SINGLE_INLINE;
    if (strcmp(mode_str, "single-pool") == 0) {
        mode = MODE_SINGLE_POOL;
    } else if (strcmp(mode_str, "pooled") == 0) {
        mode = MODE_POOLED;
    } else if (strcmp(mode_str, "single-inline") != 0) {
        fprintf(stderr, "Error: unknown mode '%s'\n", mode_str);
        usage();
        exit(1);
    }

    set_log_level(log_level);

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
#ifdef __linux__
    signal(SIGPIPE, SIG_IGN);
#endif

    /* create listening socket */
    NETWORK* server = NULL;
    if (netw_make_listening(&server, addr, port, 1024, NETWORK_IPALL) == FALSE) {
        n_log(LOG_ERR, "Failed to create listening socket on %s:%s", addr ? addr : "*", port);
        exit(1);
    }
    n_log(LOG_NOTICE, "Listening on %s:%s (backlog=1024)", addr ? addr : "*", port);

    CALLBACK_DATA cb_data;
    cb_data.handled = 0;
    cb_data.worker_pool = NULL;
    pthread_mutex_init(&cb_data.lock, NULL);

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    const char* mode_names[] = {"SINGLE-INLINE", "SINGLE-POOL", "POOLED"};
    n_log(LOG_NOTICE, "=== %s MODE: target %d connections ===", mode_names[mode], total_target);

    if (mode == MODE_SINGLE_INLINE) {
        /* ---- SINGLE-INLINE MODE ----
         * One thread doing accept + handle client in the same thread.
         * Both accept and handling are fully serialized. */
        n_log(LOG_NOTICE, "1 thread: accept + handle inline (fully serialized)");

        while (server_running) {
            pthread_mutex_lock(&cb_data.lock);
            size_t h = cb_data.handled;
            pthread_mutex_unlock(&cb_data.lock);
            if ((int)h >= total_target) {
                break;
            }

            int retval = 0;
            NETWORK* client = netw_accept_from_ex(server, 0, 0, 500, &retval);
            if (client) {
                handle_inline_and_count(client, &cb_data);
            }
        }
    } else if (mode == MODE_SINGLE_POOL) {
        /* ---- SINGLE-POOL MODE ----
         * One thread doing accept, dispatching to a worker thread pool.
         * Accept is serialized, but client handling is parallelized. */
        n_log(LOG_NOTICE, "1 accept thread, %d worker threads", nb_threads);

        cb_data.worker_pool = new_thread_pool((size_t)nb_threads, (size_t)(total_target + 16));
        if (!cb_data.worker_pool) {
            n_log(LOG_ERR, "Failed to create worker thread pool");
            goto cleanup;
        }

        int accepted_count = 0;
        while (server_running && accepted_count < total_target) {
            int retval = 0;
            NETWORK* client = netw_accept_from_ex(server, 0, 0, 500, &retval);
            if (client) {
                dispatch_to_pool(client, &cb_data);
                accepted_count++;
            }
        }

        /* wait for all workers to finish */
        n_log(LOG_NOTICE, "all connections accepted, waiting for workers...");
        wait_for_threaded_pool(cb_data.worker_pool);
        destroy_threaded_pool(&cb_data.worker_pool, 500000);
    } else {
        /* ---- POOLED MODE ----
         * N accept threads (accept pool) + worker thread pool.
         * Both accept and handling are parallelized. */
        n_log(LOG_NOTICE, "%d accept threads, %d worker threads", nb_threads, nb_threads);

        cb_data.worker_pool = new_thread_pool((size_t)nb_threads, (size_t)(total_target + 16));
        if (!cb_data.worker_pool) {
            n_log(LOG_ERR, "Failed to create worker thread pool");
            goto cleanup;
        }

        NETW_ACCEPT_POOL* accept_pool = netw_accept_pool_create(
            server, (size_t)nb_threads, 500, on_accept_pooled, &cb_data);
        if (!accept_pool) {
            n_log(LOG_ERR, "Failed to create accept pool");
            destroy_threaded_pool(&cb_data.worker_pool, 500000);
            goto cleanup;
        }

        if (netw_accept_pool_start(accept_pool) == FALSE) {
            n_log(LOG_ERR, "Failed to start accept pool");
            netw_accept_pool_destroy(&accept_pool);
            destroy_threaded_pool(&cb_data.worker_pool, 500000);
            goto cleanup;
        }

        /* wait until we've handled enough connections or user interrupts */
        while (server_running) {
            pthread_mutex_lock(&cb_data.lock);
            size_t h = cb_data.handled;
            pthread_mutex_unlock(&cb_data.lock);

            if ((int)h >= total_target) {
                break;
            }

            NETW_ACCEPT_POOL_STATS stats;
            netw_accept_pool_get_stats(accept_pool, &stats);
            n_log(LOG_INFO, "stats: accepted=%zu errors=%zu timeouts=%zu active_threads=%zu",
                  stats.total_accepted, stats.total_errors, stats.total_timeouts, stats.active_threads);

            u_sleep(100000);
        }

        netw_accept_pool_stop(accept_pool);
        netw_accept_pool_wait(accept_pool, 10);

        NETW_ACCEPT_POOL_STATS final_stats;
        netw_accept_pool_get_stats(accept_pool, &final_stats);
        n_log(LOG_NOTICE, "Pool stats: accepted=%zu errors=%zu timeouts=%zu",
              final_stats.total_accepted, final_stats.total_errors, final_stats.total_timeouts);

        netw_accept_pool_destroy(&accept_pool);

        /* wait for all workers to finish */
        n_log(LOG_NOTICE, "accept pool stopped, waiting for workers...");
        wait_for_threaded_pool(cb_data.worker_pool);
        destroy_threaded_pool(&cb_data.worker_pool, 500000);
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (double)(t_end.tv_sec - t_start.tv_sec) +
                     (double)(t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    n_log(LOG_NOTICE, "=== DONE: %zu connections in %.3f seconds (%.1f conn/sec) ===",
          cb_data.handled, elapsed, (double)cb_data.handled / elapsed);

cleanup:
    pthread_mutex_destroy(&cb_data.lock);
    netw_close(&server);

    FreeNoLog(addr);
    FreeNoLog(port);

    netw_unload();
    n_log(LOG_NOTICE, "Server exited cleanly");

    return 0;
}
