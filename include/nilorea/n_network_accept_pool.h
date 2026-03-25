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
 *@file n_network_accept_pool.h
 *@brief Accept pool for parallel connection acceptance (nginx-style)
 *@author Castagnier Mickael
 *@version 1.0
 *@date 11/03/2026
 */

#ifndef __NILOREA_NETWORK_ACCEPT_POOL__
#define __NILOREA_NETWORK_ACCEPT_POOL__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup ACCEPT_POOL ACCEPT_POOL: parallel accept pool for high-performance connection handling
   @addtogroup ACCEPT_POOL
   @{

 \section accept_pool_overview Overview

 The accept pool provides nginx-style parallel connection acceptance: multiple
 threads call accept() on the same listening socket, each invoking a user
 callback for every accepted connection.

 \section accept_pool_modes Server Architecture Modes

 There are three common ways to structure a server's accept + handling logic.
 Each has different performance characteristics depending on the workload:

 \subsection mode_single_inline Single-inline: accept + handle in one thread

 One thread does accept() and handles the client inline before accepting the
 next connection. Both accept and handling are fully serialized.

 - **Pros:** Simplest architecture, no synchronization overhead, no thread contention
 - **Cons:** Throughput limited to one connection at a time
 - **Best for:** Low-traffic servers, simple request/response protocols, debugging

 \subsection mode_single_pool Single-pool: one accept thread + worker thread pool

 One thread calls accept() and dispatches each connection to a worker thread
 pool for handling. Accept is serialized but handling is parallelized.

 - **Pros:** Parallelizes the expensive part (client handling), simple accept logic
 - **Cons:** Accept is still serialized — under extreme connection rates the single
   accept thread becomes the bottleneck
 - **Best for:** Most servers where per-connection work (I/O, computation) dominates
   the accept overhead. This is the sweet spot for typical workloads.

 \subsection mode_pooled Pooled: N accept threads + worker thread pool

 Multiple threads call accept() on the same listening socket (nginx-style),
 each dispatching to a worker thread pool. Both accept and handling are parallelized.

 - **Pros:** Eliminates the accept bottleneck under extreme connection rates
 - **Cons:** Extra threads and kernel-level accept queue contention add overhead.
   On localhost or low-latency networks, this overhead can outweigh the benefit,
   making pooled mode slower than single-pool.
 - **Best for:** High connection rates over real networks (WAN clients), SSL/TLS
   servers where the handshake is part of the accept path, multi-socket NUMA
   systems, or any scenario where accept() itself is slow enough to serialize.

 \section accept_pool_perf Performance Considerations

 On localhost (loopback), accept() is near-instant (nanoseconds). A single accept
 thread can drain the kernel queue faster than clients can fill it. Adding more
 accept threads introduces kernel socket lock contention and context switches
 without benefit — single-pool will often outperform pooled mode in this case.

 The pooled accept advantage appears when:
 - Clients connect over a real network with non-trivial latency
 - SSL/TLS handshakes run as part of the accept path
 - Connection rates exceed what one thread can accept (10k+ conn/sec sustained)
 - The system has many CPU cores and the kernel accept lock is the bottleneck

 The example programs (ex_accept_pool_server, ex_accept_pool_client, test_accept_pool.sh)
 demonstrate all three modes with configurable parameters for benchmarking.
 */

#include "n_common.h"
#include "n_network.h"
#include "n_thread_pool.h"

#include <time.h>

/*! accept pool state: idle, not yet started */
#define NETW_ACCEPT_POOL_IDLE 0
/*! accept pool state: running and accepting connections */
#define NETW_ACCEPT_POOL_RUNNING 1
/*! accept pool state: stop requested */
#define NETW_ACCEPT_POOL_STOPPING 2
/*! accept pool state: fully stopped */
#define NETW_ACCEPT_POOL_STOPPED 3

/*! callback type for accepted connections.
 *  @param accepted the newly accepted NETWORK connection (caller takes ownership)
 *  @param user_data opaque pointer passed at pool creation
 */
typedef void (*netw_accept_callback_t)(NETWORK* accepted, void* user_data);

/*! Statistics for the accept pool */
typedef struct NETW_ACCEPT_POOL_STATS {
    /*! total connections successfully accepted */
    size_t total_accepted;
    /*! total accept errors */
    size_t total_errors;
    /*! total accept timeouts (no connection available) */
    size_t total_timeouts;
    /*! number of accept threads currently running */
    size_t active_threads;
    /*! time when pool was started (CLOCK_MONOTONIC) */
    struct timespec start_time;
} NETW_ACCEPT_POOL_STATS;

/*! Structure of a parallel accept pool */
typedef struct NETW_ACCEPT_POOL {
    /*! listening socket (not owned, caller must keep alive) */
    NETWORK* server;
    /*! number of accept threads */
    size_t nb_accept_threads;
    /*! array of pthread ids */
    pthread_t* accept_threads;
    /*! user callback invoked on each accepted connection */
    netw_accept_callback_t callback;
    /*! opaque user data passed to callback */
    void* user_data;
    /*! accept timeout in milliseconds (passed to netw_accept_from_ex blocking param).
     *  0 = blocking, -1 = non-blocking, >0 = timeout in msec */
    int accept_timeout;
    /*! atomic pool state */
    uint32_t state;
    /*! pool statistics */
    NETW_ACCEPT_POOL_STATS stats;
    /*! mutex protecting stats */
    pthread_mutex_t stats_lock;
} NETW_ACCEPT_POOL;

/*! Lock-free atomic read of the accept pool state */
#define netw_accept_pool_atomic_read_state(pool) __atomic_load_n(&(pool)->state, __ATOMIC_ACQUIRE)

/*! Lock-free atomic write of the accept pool state */
#define netw_accept_pool_atomic_write_state(pool, val) __atomic_store_n(&(pool)->state, (val), __ATOMIC_RELEASE)

/*! Create a new accept pool */
NETW_ACCEPT_POOL* netw_accept_pool_create(NETWORK* server, size_t nb_threads, int accept_timeout, netw_accept_callback_t callback, void* user_data);

/*! Start the accept pool (launches accept threads) */
int netw_accept_pool_start(NETW_ACCEPT_POOL* pool);

/*! Request the accept pool to stop. Non-blocking, threads will finish current accept and exit. */
int netw_accept_pool_stop(NETW_ACCEPT_POOL* pool);

/*! Wait for all accept threads to finish after a stop request. */
int netw_accept_pool_wait(NETW_ACCEPT_POOL* pool, int timeout_sec);

/*! Get a snapshot of pool statistics (thread-safe copy) */
int netw_accept_pool_get_stats(NETW_ACCEPT_POOL* pool, NETW_ACCEPT_POOL_STATS* stats);

/*! Destroy an accept pool. Pool must be stopped first. */
int netw_accept_pool_destroy(NETW_ACCEPT_POOL** pool);

/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_NETWORK_ACCEPT_POOL__ */
