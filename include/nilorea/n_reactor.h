/**
 * @file n_reactor.h
 * @brief Single-threaded epoll reactor for n_network connections.
 *
 * Alternative to the per-connection thread engine for `n_network`
 * sockets. One reactor multiplexes thousands of connections on a
 * single epoll fd; the per-NETWORK send/recv queues stay exactly as
 * with the thread engine (game thread keeps using `netw_add_msg` /
 * `netw_get_msg` unchanged), only the underlying I/O is consolidated.
 *
 * **Strictly additive.** The existing thread engine
 * (`netw_start_thr_engine`, `netw_send_func`, `netw_recv_func`) is
 * unchanged and remains the default. Reactor mode is opt-in per
 * `NETWORK *` instance via `n_reactor_register`.
 *
 * **Linux + Android only.** Guarded by `__linux__ || __ANDROID__`;
 * on every other platform the public functions are still declared
 * (so callers don't need their own `#ifdef`s) but
 * `n_reactor_new` returns NULL with a `LOG_INFO` and the rest are
 * no-ops returning error codes. Server config validators must
 * clamp `network_mode` back to "threads" when the reactor isn't
 * available.
 */

#ifndef NILOREA_N_REACTOR_H
#define NILOREA_N_REACTOR_H

/* Compile-time availability gate. Linux + Android both ship epoll
 * + eventfd. Solaris has /dev/poll and event ports (different
 * primitive); Windows has IOCP (different architecture). For now
 * the reactor is Linux/Android-only and falls back to the existing
 * thread engine on every other target.
 *
 * Wrapped in `#ifndef` so the build system can force the gate off
 * via `-DN_REACTOR_AVAILABLE=0` (e.g. on Linux when the user
 * deliberately opts out of reactor mode to drop the n_reactor.o
 * link dependency). The Makefile sets this when HAVE_REACTOR=0. */
#ifndef N_REACTOR_AVAILABLE
#if defined(__linux__) || defined(__ANDROID__)
#define N_REACTOR_AVAILABLE 1
#else
#define N_REACTOR_AVAILABLE 0
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_common.h"
#include "nilorea/n_network.h"

/*! Opaque reactor handle. Allocated by `n_reactor_new`, released by
 *  `n_reactor_destroy`. */
typedef struct n_reactor n_reactor;

/*! Counters for the dashboard / profile_server.sh. Cheap atomic
 *  loads, safe to call from any thread. */
typedef struct {
    long long events_processed; /*!< total epoll events dispatched */
    long long writes_partial;   /*!< EAGAIN on send -> re-armed EPOLLOUT */
    long long reads_partial;    /*!< EAGAIN on recv -> kept accumulator */
    long long fds_registered;   /*!< lifetime register call count */
    long long fds_unregistered; /*!< lifetime unregister call count */
    long long wake_signals;     /*!< eventfd wake events processed */
    /* Registered-list walk diagnostics. wake_walks counts how many
     * times the wake-time list walk fired; wake_walk_visits sums
     * every NETWORK visited; wake_walk_drains sums every visit that
     * found data to send. Ratio (drains/visits) measures how
     * productive the walk is. */
    long long wake_walks;
    long long wake_walk_visits;
    long long wake_walk_drains;
} n_reactor_stats;

/*!\brief Create a new reactor.
 *
 * Allocates the epoll fd, the wake-up eventfd, and zero-inits the
 * per-NETWORK state slots. Does NOT start a thread, caller decides
 * whether to put `n_reactor_run` on its own thread or run it
 * inline.
 *
 * @param max_fds_hint expected max number of registered NETWORKs.
 *        Used to size internal tables; can be exceeded at the cost
 *        of a realloc. Pass 0 to use the default (256).
 * @return reactor pointer on success, NULL on allocation failure
 *         OR on platforms where N_REACTOR_AVAILABLE == 0.
 */
n_reactor* n_reactor_new(int max_fds_hint);

/*!\brief Tear down a reactor.
 *
 * Stops the run loop if it's running, drains pending events, closes
 * the epoll + eventfd, frees per-NETWORK state. The caller is
 * responsible for cleaning up the `NETWORK *` instances themselves
 * (via `netw_close`); this only frees the reactor's internal
 * bookkeeping.
 *
 * Sets `*reactor = NULL` on return. Safe to call with `*reactor`
 * already NULL.
 */
void n_reactor_destroy(n_reactor** reactor);

/*!\brief Run the epoll loop on the calling thread.
 *
 * Blocks until `n_reactor_stop` is called. Drains pending events
 * once after stop is signalled, then returns. Idempotent, calling
 * twice in a row from the same thread is safe but pointless;
 * calling from two threads simultaneously is undefined.
 */
void n_reactor_run(n_reactor* reactor);

/*!\brief pthread_create-compatible entry point that calls
 *        `n_reactor_run` on the reactor passed via `arg`.
 *
 * Convenience for callers that want to put the reactor on a
 * dedicated thread without writing their own one-line adapter.
 * Returns NULL.
 */
void* n_reactor_run_thread_entry(void* arg);

/*!\brief Signal the run loop to exit at the next iteration.
 *
 * Thread-safe, may be called from any thread. The call writes to
 * a dedicated stop-eventfd registered with the epoll set; the run
 * loop observes the wakeup and breaks out. `n_reactor_run` returns
 * shortly after.
 */
void n_reactor_stop(n_reactor* reactor);

/*!\brief Read current stats counters into `*out`.
 *
 * Thread-safe. Atomic reads, values are consistent for individual
 * fields but the snapshot across fields is not strictly
 * synchronised (no mutex). Useful for periodic dashboard logging
 * where field-level skew is irrelevant.
 */
void n_reactor_get_stats(const n_reactor* reactor, n_reactor_stats* out);

/*!\brief Register a NETWORK with the reactor.
 *
 * The NETWORK must NOT have had `netw_start_thr_engine` called on
 * it. The socket is set O_NONBLOCK, added to the reactor's epoll
 * set with EPOLLIN | EPOLLET, and `netw->reactor_mode` is set to 1.
 *
 * From this point on, raw socket reads happen on the reactor's
 * thread; complete N_STR frames are pushed into `netw->recv_buf`
 * exactly as the thread-mode `netw_recv_func` would. The game
 * thread keeps using `netw_get_msg` unchanged.
 *
 * Returns 1 on success, 0 on any failure (registration aborted,
 * NETWORK left untouched, caller can fall back to thread engine).
 */
int n_reactor_register(n_reactor* reactor, NETWORK* netw);

/*!\brief Unregister a NETWORK from the reactor.
 *
 * Removes the socket from the epoll set and frees any per-connection
 * recv accumulator state. Does NOT close the socket, the caller
 * (`netw_close` / `netw_close_ex`) handles the actual close. Safe
 * to call on a NETWORK that was never registered (no-op).
 */
void n_reactor_unregister(n_reactor* reactor, NETWORK* netw);

/*!\brief Producer-side wake-up after a netw_add_msg.
 *
 * Called by `netw_add_msg` after pushing onto `netw->send_buf`
 * when `netw->reactor_mode == 1`. Writes a byte to the reactor's
 * wake eventfd so the run loop pulls out of `epoll_wait` and
 * scans for newly-ready sends. Cheap (one 8-byte syscall, no
 * locking).
 *
 * Safe to call from any thread. No-op if `netw->reactor_handle`
 * is NULL (NETWORK not registered, or already unregistered).
 */
void n_reactor_notify_send(NETWORK* netw);

/*!\brief Synchronously close a reactor-registered NETWORK from the
 *        game thread.
 *
 * Sets `NETW_EXIT_ASKED` on the NETWORK, wakes the reactor, then
 * blocks until the reactor has drained any pending sends (best
 * effort), called `shutdown(SHUT_WR)` on the socket so the peer
 * sees EOF, and removed the fd from the epoll set. After this
 * returns the caller may safely `close()` the socket, the reactor
 * will not touch it again.
 *
 * Idempotent / safe variants:
 *   - NULL or non-reactor-mode `netw`: returns immediately.
 *   - `netw->reactor_handle` already cleared (race with
 *     reactor-initiated unregister): returns immediately.
 *
 * Must NOT be called from the reactor thread itself, that would
 * self-deadlock waiting for the very ack it should be producing.
 * The Nilorea server calls it from the game thread (`netw_close`
 * indirectly) and from the accept-pool worker thread (during
 * orderly shutdown), both of which are separate from the reactor
 * thread.
 *
 * Bounded latency: typically one epoll iteration (<=1 ms with the
 * heartbeat sweep timeout). Worst case is bounded by
 * `n_reactor_run`'s 1-second heartbeat timeout when the reactor
 * thread happens to be wedged in something it can't preempt.
 */
void n_reactor_close_netw_sync(NETWORK* netw);

/*!\brief Accept a connection on `listener` and register it with
 *        `reactor` instead of starting per-connection threads.
 *
 * Thin wrapper around `netw_accept_from_ex`: passes the
 * `blocking` parameter through (semantics identical, see that
 * function's docs, 0 = blocking accept, > 0 = millisec select
 * timeout, -1 = non-blocking accept), then calls
 * `n_reactor_register` on the accepted NETWORK. On any failure
 * (accept returned NULL, or register rejected the NETWORK) the
 * accepted socket is closed and NULL is returned, so the caller
 * doesn't have to track partial state.
 *
 * Use this from the listener loop when the server is configured
 * for reactor mode. The plain `netw_accept_from_ex` +
 * `netw_start_thr_engine` path stays available unchanged for
 * thread-mode listeners, both modes can coexist in the same
 * process (one listener per port, each with its own choice).
 *
 * Returns the accepted NETWORK on success, NULL otherwise.
 * `retval` is forwarded from the underlying accept (errno on
 * failure, 0 on success) to mirror `netw_accept_from_ex`'s
 * contract.
 */
NETWORK* netw_accept_into_reactor(NETWORK* listener,
                                  size_t send_list_limit,
                                  size_t recv_list_limit,
                                  int blocking,
                                  n_reactor* reactor,
                                  int* retval);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NILOREA_N_REACTOR_H */
