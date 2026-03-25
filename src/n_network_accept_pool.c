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
 *@file n_network_accept_pool.c
 *@brief Accept pool for parallel connection acceptance (nginx-style)
 *@author Castagnier Mickael
 *@version 1.0
 *@date 11/03/2026
 */

#include "nilorea/n_network_accept_pool.h"

#include <errno.h>
#include <string.h>

/**
 *@brief Thread function for accept pool workers.
 * Each thread loops calling accept on the shared listening socket
 * and invokes the user callback for each accepted connection.
 *@param arg pointer to the NETW_ACCEPT_POOL
 *@return NULL
 */
static void* netw_accept_pool_thread_func(void* arg) {
    NETW_ACCEPT_POOL* pool = (NETW_ACCEPT_POOL*)arg;

    __n_assert(pool, return NULL);
    __n_assert(pool->server, return NULL);
    __n_assert(pool->callback, return NULL);

    pthread_mutex_lock(&pool->stats_lock);
    pool->stats.active_threads++;
    pthread_mutex_unlock(&pool->stats_lock);

    while (netw_accept_pool_atomic_read_state(pool) == NETW_ACCEPT_POOL_RUNNING) {
        int retval = 0;
        NETWORK* accepted = netw_accept_from_ex(pool->server, 0, 0, pool->accept_timeout, &retval);

        /* check if we should exit */
        if (netw_accept_pool_atomic_read_state(pool) != NETW_ACCEPT_POOL_RUNNING) {
            if (accepted) {
                netw_close(&accepted);
            }
            break;
        }

        if (accepted) {
            pthread_mutex_lock(&pool->stats_lock);
            pool->stats.total_accepted++;
            pthread_mutex_unlock(&pool->stats_lock);

            pool->callback(accepted, pool->user_data);
        } else {
            if (retval == EINTR) {
                /* interrupted, just retry */
                continue;
            }
            if (retval == EAGAIN || retval == EWOULDBLOCK || retval == 0) {
                /* timeout or no connection available */
                pthread_mutex_lock(&pool->stats_lock);
                pool->stats.total_timeouts++;
                pthread_mutex_unlock(&pool->stats_lock);
                /* small sleep to avoid busy-looping on non-blocking accept */
                if (pool->accept_timeout == -1) {
                    u_sleep(1000);
                }
            } else {
                /* actual error */
                pthread_mutex_lock(&pool->stats_lock);
                pool->stats.total_errors++;
                pthread_mutex_unlock(&pool->stats_lock);
                n_log(LOG_ERR, "accept pool thread: accept error, retval=%d", retval);
            }
        }
    }

    pthread_mutex_lock(&pool->stats_lock);
    if (pool->stats.active_threads > 0) {
        pool->stats.active_threads--;
    }
    pthread_mutex_unlock(&pool->stats_lock);

    return NULL;
}

/**
 *@brief Create a new accept pool
 *@param server listening NETWORK (must already be listening)
 *@param nb_threads number of accept threads
 *@param accept_timeout timeout in msec for accept (0=blocking, -1=nonblocking, >0=timeout ms)
 *@param callback function called for each accepted connection
 *@param user_data opaque pointer passed to callback
 *@return new NETW_ACCEPT_POOL or NULL on error
 */
NETW_ACCEPT_POOL* netw_accept_pool_create(NETWORK* server, size_t nb_threads, int accept_timeout, netw_accept_callback_t callback, void* user_data) {
    __n_assert(server, return NULL);
    __n_assert(callback, return NULL);

    if (nb_threads == 0) {
        n_log(LOG_ERR, "netw_accept_pool_create: nb_threads must be > 0");
        return NULL;
    }

    NETW_ACCEPT_POOL* pool = NULL;
    Malloc(pool, NETW_ACCEPT_POOL, 1);
    __n_assert(pool, return NULL);

    pool->server = server;
    pool->nb_accept_threads = nb_threads;
    pool->accept_timeout = accept_timeout;
    pool->callback = callback;
    pool->user_data = user_data;

    netw_accept_pool_atomic_write_state(pool, NETW_ACCEPT_POOL_IDLE);

    memset(&pool->stats, 0, sizeof(NETW_ACCEPT_POOL_STATS));

    if (pthread_mutex_init(&pool->stats_lock, NULL) != 0) {
        n_log(LOG_ERR, "netw_accept_pool_create: failed to init stats mutex");
        Free(pool);
        return NULL;
    }

    Malloc(pool->accept_threads, pthread_t, nb_threads);
    if (!pool->accept_threads) {
        n_log(LOG_ERR, "netw_accept_pool_create: failed to allocate thread array");
        pthread_mutex_destroy(&pool->stats_lock);
        Free(pool);
        return NULL;
    }

    return pool;
}

/**
 *@brief Start the accept pool (launches accept threads)
 *@param pool the accept pool to start
 *@return TRUE on success, FALSE on error
 */
int netw_accept_pool_start(NETW_ACCEPT_POOL* pool) {
    __n_assert(pool, return FALSE);

    uint32_t current = netw_accept_pool_atomic_read_state(pool);
    if (current != NETW_ACCEPT_POOL_IDLE && current != NETW_ACCEPT_POOL_STOPPED) {
        n_log(LOG_ERR, "netw_accept_pool_start: pool is not idle or stopped (state=%u)", current);
        return FALSE;
    }

    /* reset stats */
    pthread_mutex_lock(&pool->stats_lock);
    memset(&pool->stats, 0, sizeof(NETW_ACCEPT_POOL_STATS));
    clock_gettime(CLOCK_MONOTONIC, &pool->stats.start_time);
    pthread_mutex_unlock(&pool->stats_lock);

    netw_accept_pool_atomic_write_state(pool, NETW_ACCEPT_POOL_RUNNING);

    for (size_t i = 0; i < pool->nb_accept_threads; i++) {
        int err = pthread_create(&pool->accept_threads[i], NULL, netw_accept_pool_thread_func, pool);
        if (err != 0) {
            n_log(LOG_ERR, "netw_accept_pool_start: failed to create thread %zu: %s", i, strerror(err));
            /* stop already-created threads */
            netw_accept_pool_atomic_write_state(pool, NETW_ACCEPT_POOL_STOPPING);
            for (size_t j = 0; j < i; j++) {
                pthread_join(pool->accept_threads[j], NULL);
            }
            netw_accept_pool_atomic_write_state(pool, NETW_ACCEPT_POOL_STOPPED);
            return FALSE;
        }
    }

    n_log(LOG_NOTICE, "accept pool started with %zu threads", pool->nb_accept_threads);
    return TRUE;
}

/**
 *@brief Request the accept pool to stop
 *@param pool the accept pool to stop
 *@return TRUE on success, FALSE on error
 */
int netw_accept_pool_stop(NETW_ACCEPT_POOL* pool) {
    __n_assert(pool, return FALSE);

    uint32_t current = netw_accept_pool_atomic_read_state(pool);
    if (current != NETW_ACCEPT_POOL_RUNNING) {
        n_log(LOG_WARNING, "netw_accept_pool_stop: pool is not running (state=%u)", current);
        return FALSE;
    }

    n_log(LOG_NOTICE, "accept pool: stop requested");
    netw_accept_pool_atomic_write_state(pool, NETW_ACCEPT_POOL_STOPPING);

    /* shutdown the listening socket to unblock any threads stuck in select()/accept() */
    if (pool->server && pool->server->link.sock != INVALID_SOCKET) {
        shutdown(pool->server->link.sock, SHUT_RDWR);
    }

    return TRUE;
}

/**
 *@brief Wait for all accept threads to finish after a stop request
 *@param pool the accept pool to wait on
 *@param timeout_sec maximum seconds to wait (0 = wait forever)
 *@return TRUE if all threads joined, FALSE on timeout or error
 */
int netw_accept_pool_wait(NETW_ACCEPT_POOL* pool, int timeout_sec) {
    __n_assert(pool, return FALSE);

    uint32_t current = netw_accept_pool_atomic_read_state(pool);
    if (current == NETW_ACCEPT_POOL_IDLE || current == NETW_ACCEPT_POOL_STOPPED) {
        return TRUE;
    }

    if (current == NETW_ACCEPT_POOL_RUNNING) {
        n_log(LOG_WARNING, "netw_accept_pool_wait: pool still running, calling stop first");
        netw_accept_pool_stop(pool);
    }

    (void)timeout_sec;
    for (size_t i = 0; i < pool->nb_accept_threads; i++) {
        pthread_join(pool->accept_threads[i], NULL);
    }

    netw_accept_pool_atomic_write_state(pool, NETW_ACCEPT_POOL_STOPPED);
    n_log(LOG_NOTICE, "accept pool: all threads joined");

    return TRUE;
}

/**
 *@brief Get a snapshot of pool statistics
 *@param pool the accept pool
 *@param stats output structure to fill
 *@return TRUE on success, FALSE on error
 */
int netw_accept_pool_get_stats(NETW_ACCEPT_POOL* pool, NETW_ACCEPT_POOL_STATS* stats) {
    __n_assert(pool, return FALSE);
    __n_assert(stats, return FALSE);

    pthread_mutex_lock(&pool->stats_lock);
    memcpy(stats, &pool->stats, sizeof(NETW_ACCEPT_POOL_STATS));
    pthread_mutex_unlock(&pool->stats_lock);

    return TRUE;
}

/**
 *@brief Destroy an accept pool. Pool must be stopped first.
 *@param pool pointer to the accept pool pointer (set to NULL)
 *@return TRUE on success, FALSE on error
 */
int netw_accept_pool_destroy(NETW_ACCEPT_POOL** pool) {
    __n_assert(pool, return FALSE);
    __n_assert((*pool), return FALSE);

    uint32_t current = netw_accept_pool_atomic_read_state(*pool);
    if (current == NETW_ACCEPT_POOL_RUNNING || current == NETW_ACCEPT_POOL_STOPPING) {
        n_log(LOG_WARNING, "netw_accept_pool_destroy: pool still active, stopping and waiting");
        netw_accept_pool_stop(*pool);
        netw_accept_pool_wait(*pool, 0);
    }

    pthread_mutex_destroy(&(*pool)->stats_lock);
    Free((*pool)->accept_threads);
    Free(*pool);

    return TRUE;
}
