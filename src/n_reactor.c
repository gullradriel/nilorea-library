/**
 * @file n_reactor.c
 * @brief Single-threaded epoll reactor.
 *
 * See `nilorea/n_reactor.h` for the design rationale, the public
 * API contract, and the cross-platform availability gate.
 */

#include "nilorea/n_reactor.h"

#include "nilorea/n_log.h"
#include "nilorea/n_common.h"
#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include "nilorea/n_network.h"
#include "nilorea/n_zlib.h"
#include "nilorea/n_lz4.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#if N_REACTOR_AVAILABLE

#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <arpa/inet.h> /* ntohl, htonl */
#include <stdatomic.h>

/* Default registered-fd capacity. The internal table grows past
 * this on demand at register time, the hint just sizes the
 * initial alloc. */
#define N_REACTOR_DEFAULT_MAX_FDS 256

/* Maximum events drained per `epoll_wait` call. Larger = fewer
 * syscalls under load; smaller = lower per-event scheduling
 * latency. 64 is a common pragmatic compromise. */
#define N_REACTOR_BATCH_SIZE 64

struct n_reactor {
    int epoll_fd;
    int stop_efd;       /* eventfd written by n_reactor_stop */
    int wake_efd;       /* eventfd written by producers */
    int stop_requested; /* set by stop_efd handler */

    /* Registered NETWORKs. The wake-event handler scans this list to
     * find connections that have new data in send_buf; the EPOLLOUT
     * handler reaches NETWORK directly via data.ptr and doesn't need
     * the list. Mutex protects insert / remove from any thread (the
     * register / unregister API is called from the producer thread,
     * the same thread that calls n_reactor_run, or from accept-pool
     * threads). */
    LIST* registered;
    pthread_mutex_t registered_lock;

    /* Stats, accessed via atomic loads/stores so n_reactor_get_stats
     * is safe from any thread without a mutex. */
    atomic_llong events_processed;
    atomic_llong writes_partial;
    atomic_llong reads_partial;
    atomic_llong fds_registered;
    atomic_llong fds_unregistered;
    atomic_llong wake_signals;
    /* Registered-list walk instrumentation. Counts how often the
     * registered list is walked at wake time and how productive each
     * walk is. */
    atomic_llong wake_walks;       /* # of times the registered list was walked at wake */
    atomic_llong wake_walk_visits; /* sum of NETWORKs visited across all wake walks */
    atomic_llong wake_walk_drains; /* # of NETWORKs that had pending data during walks */

    /* n_reactor_notify_send adds the NETWORK to `dirty_pending`
     * (CAS-guarded via NETWORK.in_dirty_list) under dirty_lock; the
     * wake handler walks only this list instead of the full
     * registered list. */
    LIST* dirty_pending; /* NETWORK* entries with pending sends */
    pthread_mutex_t dirty_lock;
};

/* Internal helpers. */

static int register_internal_efd(int epoll_fd, int efd, uint64_t tag) {
    /* The two internal eventfds (stop, wake) are registered with
     * data.u64 = a tag value the run loop uses to dispatch back
     * to internal handling without a per-fd lookup. NETWORK fds
     * use data.ptr. */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.u64 = tag;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, efd, &ev) != 0) {
        n_log(LOG_ERR, "n_reactor: epoll_ctl ADD eventfd failed: %s",
              strerror(errno));
        return 0;
    }
    return 1;
}

/* Tag values for data.u64 on internally-registered eventfds. They
 * are small enough to never collide with a real pointer (no
 * legitimate `NETWORK *` lives in the bottom MB of address space);
 * the run loop branches on `data.u64 < N_REACTOR_TAG_INTERNAL_MAX`
 * to detect internal vs NETWORK fds. */
#define N_REACTOR_TAG_STOP 1ULL
#define N_REACTOR_TAG_WAKE 2ULL
#define N_REACTOR_TAG_INTERNAL_MAX 16ULL

/* Drain an eventfd. Reads in 8-byte units (eventfd's contract);
 * keeps reading until EAGAIN. We don't care about the count, just
 * the fact that something happened. */
static long long drain_eventfd(int efd) {
    long long total = 0;
    uint64_t buf;
    for (;;) {
        ssize_t n = read(efd, &buf, sizeof(buf));
        if (n == sizeof(buf)) {
            total += (long long)buf;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        /* Either a partial read (impossible per eventfd's contract)
         * or some other error. Either way, break, the wake event
         * is "something happened", we don't lose state. */
        break;
    }
    return total;
}

/* Set a file descriptor to non-blocking mode. Returns 1 on success. */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return 0;
    if ((flags & O_NONBLOCK) == O_NONBLOCK) return 1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return 0;
    return 1;
}

/* Free any in-flight recv accumulator state on the NETWORK. */
static void reactor_recv_state_reset(NETWORK* netw) {
    FreeNoLog(netw->reactor_read_payload);
    netw->reactor_read_phase = 0;
    netw->reactor_read_hdr_have = 0;
    netw->reactor_read_pkt_state = 0;
    netw->reactor_read_pkt_length = 0;
    netw->reactor_read_payload_have = 0;
}

/* Free any in-flight send buffer on the NETWORK. */
static void reactor_send_state_reset(NETWORK* netw) {
    FreeNoLog(netw->reactor_send_buf);
    netw->reactor_send_len = 0;
    netw->reactor_send_off = 0;
    netw->reactor_write_armed = 0;
    netw->reactor_send_wants_read = 0;
    netw->reactor_recv_wants_write = 0;
}

/* Update the EPOLL_CTL_MOD events for a registered NETWORK. ev_mask
 * is the desired event set (already including EPOLLIN | EPOLLRDHUP |
 * EPOLLET, caller adds or removes EPOLLOUT). */
static int reactor_epoll_mod(n_reactor* r, NETWORK* netw, uint32_t ev_mask) {
    struct epoll_event ev;
    ev.events = ev_mask;
    ev.data.ptr = netw;
    return epoll_ctl(r->epoll_fd, EPOLL_CTL_MOD, netw->link.sock, &ev) == 0;
}

/* Pop the next N_STR from netw->send_buf (under sendbolt) and pre-frame
 * it into a contiguous buffer ready to send: 4-byte state word + 4-byte
 * payload length + payload bytes, with optional zlib/lz4 compression
 * applied to the payload (same gating as netw_send_func: compress_mode
 * != NONE && payload >= NETW_COMPRESS_THRESHOLD && ratio >= 10 %).
 *
 * On success populates netw->reactor_send_buf / _len / _off and returns
 * 1. On nothing-to-send returns 0. On allocation failure returns -1
 * (caller should treat as fatal).
 */
static int reactor_send_state_load_next(NETWORK* netw) {
    if (netw->reactor_send_buf) return 1; /* already loaded */

    pthread_mutex_lock(&netw->sendbolt);
    N_STR* msg = list_shift(netw->send_buf, N_STR);
    pthread_mutex_unlock(&netw->sendbolt);
    if (!msg) return 0;

    /* Optional compression, same gating as netw_send_func, including
     * the per-NETWORK compress_mode choice (NONE / ZLIB / LZ4) and
     * the threshold + ratio macros (NETW_COMPRESS_THRESHOLD,
     * NETW_COMPRESS_MIN_RATIO) shared with the thread engine via the
     * public n_network.h. */
    uint32_t pkt_state = NETW_RUN;
    if (netw->compress_mode != NETW_COMPRESS_NONE &&
        msg->written >= (size_t)NETW_COMPRESS_THRESHOLD) {
        N_STR* zipped = NULL;
        uint32_t flag_bit = 0;
        if (netw->compress_mode == NETW_COMPRESS_LZ4) {
            zipped = zip4_nstr(msg);
            flag_bit = NETW_COMPRESSED_LZ4;
        } else {
            zipped = zip_nstr(msg);
            flag_bit = NETW_COMPRESSED_ZLIB;
        }
        if (zipped && zipped->written > 0 &&
            zipped->written * 100u <=
                msg->written * (100u - NETW_COMPRESS_MIN_RATIO)) {
            /* Saved at least NETW_COMPRESS_MIN_RATIO %, swap. */
            free_nstr(&msg);
            msg = zipped;
            pkt_state |= flag_bit;
        } else if (zipped) {
            free_nstr(&zipped);
        }
    }

    if (msg->written > UINT32_MAX) {
        n_log(LOG_ERR, "n_reactor: payload too large (%zu > UINT32_MAX); dropping",
              msg->written);
        free_nstr(&msg);
        return 0;
    }
    size_t total = 8 + msg->written;
    char* buf = NULL;
    Malloc(buf, char, total);
    if (!buf) {
        free_nstr(&msg);
        return -1;
    }
    uint32_t state_be = htonl(pkt_state);
    uint32_t length_be = htonl((uint32_t)msg->written);
    memcpy(buf, &state_be, 4);
    memcpy(buf + 4, &length_be, 4);
    memcpy(buf + 8, msg->data, msg->written);
    free_nstr(&msg);

    netw->reactor_send_buf = buf;
    netw->reactor_send_len = total;
    netw->reactor_send_off = 0;
    return 1;
}

/* Drain as much of netw->reactor_send_buf as the kernel will accept,
 * loop to load the next queued frame on completion. Returns:
 *   1, drained fully (send_buf empty, no in-flight). Caller disarms
 *       EPOLLOUT.
 *   0, back-pressure (EAGAIN). Caller arms EPOLLOUT.
 *  -1, hard error / connection closed by peer. Caller unregisters
 *       and flags NETW_ERROR.
 */
static int reactor_drain_writes(NETWORK* netw, n_reactor* reactor) {
    for (;;) {
        if (!netw->reactor_send_buf) {
            int r = reactor_send_state_load_next(netw);
            if (r < 0) return -1;
            if (r == 0) return 1; /* nothing to send */
        }
        size_t remaining = netw->reactor_send_len - netw->reactor_send_off;
        if (remaining == 0) {
            /* Buffer fully drained, free and try the next queued msg. */
            Free(netw->reactor_send_buf);
            netw->reactor_send_buf = NULL;
            netw->reactor_send_len = 0;
            netw->reactor_send_off = 0;
            continue;
        }
        /* TLS-over-reactor: route through the NETWORK's
         * single-attempt send pointer (raw send() on cleartext,
         * SSL_write on crypto sockets). EINTR is retried inside the
         * call; partial-write resume rides the same buffer+offset
         * thanks to SSL_MODE_ENABLE_PARTIAL_WRITE. */
        netw->reactor_send_wants_read = 0;
        uint32_t attempt = (remaining > UINT32_MAX) ? UINT32_MAX : (uint32_t)remaining;
        ssize_t sent = netw->send_data_once((void*)netw,
                                            netw->reactor_send_buf + netw->reactor_send_off,
                                            attempt);
        if (sent > 0) {
            netw->reactor_send_off += (size_t)sent;
            continue;
        }
        if (sent == NETW_IO_WANT_WRITE) {
            atomic_fetch_add(&reactor->writes_partial, 1);
            return 0;
        }
        if (sent == NETW_IO_WANT_READ) {
            /* TLS renegotiation: the write needs inbound bytes first.
             * Flag it so the readable path retries the drain; the
             * caller's EPOLLOUT arming is harmless (edge-triggered,
             * fires at most once while we wait). */
            netw->reactor_send_wants_read = 1;
            atomic_fetch_add(&reactor->writes_partial, 1);
            return 0;
        }
        /* NETW_SOCKET_DISCONNECTED / NETW_SOCKET_ERROR (already logged
         * inside the once-helper). */
        return -1;
    }
}

/* Push a fully-received frame onto the NETWORK's recv_buf. Mirrors
 * the thread-mode netw_recv_func tail: optionally decompresses
 * based on the frame's state-word flags, then list_push under the
 * recvbolt mutex. Frees pkt_payload on failure. */
static void reactor_recv_dispatch_frame(NETWORK* netw,
                                        uint32_t pkt_state,
                                        uint32_t pkt_length,
                                        char* pkt_payload) {
    /* Wrap the raw bytes in an N_STR, same shape the thread-mode
     * recv path produces. */
    N_STR* msg = NULL;
    Malloc(msg, N_STR, 1);
    if (!msg) {
        Free(pkt_payload);
        return;
    }
    msg->data = pkt_payload;
    msg->length = (size_t)pkt_length + 1;
    msg->written = (size_t)pkt_length;
    /* Ensure NUL-terminator for downstream consumers that treat
     * the buffer as a C string (decompression's unzip*_nstr
     * inspects strptr->data[written] in some paths). The buffer
     * was sized to pkt_length+1 in the caller. */
    msg->data[pkt_length] = '\0';

    /* Decompression: same logic as netw_recv_func. */
    int want_zlib = (pkt_state & NETW_COMPRESSED_ZLIB) != 0;
    int want_lz4 = (pkt_state & NETW_COMPRESSED_LZ4) != 0;
    if (want_zlib || want_lz4) {
        N_STR* plain = want_lz4 ? unzip4_nstr(msg) : unzip_nstr(msg);
        if (plain) {
            free_nstr(&msg);
            msg = plain;
        } else {
            n_log(LOG_ERR,
                  "n_reactor: failed to decompress payload "
                  "(%" PRIu32 " bytes, codec=%s); dropping",
                  pkt_length, want_lz4 ? "lz4" : "zlib");
            free_nstr(&msg);
            return;
        }
    }

    pthread_mutex_lock(&netw->recvbolt);
    if (list_push(netw->recv_buf, msg, free_nstr_ptr) == FALSE) {
        pthread_mutex_unlock(&netw->recvbolt);
        n_log(LOG_ERR, "n_reactor: recv_buf list_push failed; dropping frame");
        free_nstr(&msg);
        return;
    }
    pthread_mutex_unlock(&netw->recvbolt);
}

/* Sweep the registered list for NETWORKs whose game thread has set
 * NETW_EXIT_ASKED, drain pending sends best-effort,
 * shutdown(SHUT_WR) so the peer observes EOF, unregister from the
 * epoll set, and signal the close ack so the game thread can close
 * the fd.
 *
 * Snapshots pointers under the lock, then processes them with the
 * lock released, `n_reactor_unregister` reacquires the same lock,
 * so we mustn't hold it across the call. The snapshot batch caps at
 * N_REACTOR_BATCH_SIZE; any additional EXIT_ASKED entries are caught
 * on the next sweep (the sweep runs at the end of every wake batch
 * + every heartbeat tick, so the lag is bounded). */
static void reactor_sweep_exit_asked(n_reactor* reactor) {
    NETWORK* batch[N_REACTOR_BATCH_SIZE];
    int count = 0;

    pthread_mutex_lock(&reactor->registered_lock);
    LIST_NODE* node = reactor->registered->start;
    while (node && count < N_REACTOR_BATCH_SIZE) {
        NETWORK* n = (NETWORK*)node->ptr;
        node = node->next;
        if (!n || !netw_atomic_read_reactor_mode(n)) continue;
        uint32_t st = 0;
        int thr_st = 0;
        netw_get_state(n, &st, &thr_st);
        if (st & NETW_EXIT_ASKED) {
            batch[count++] = n;
        }
    }
    pthread_mutex_unlock(&reactor->registered_lock);

    for (int i = 0; i < count; i++) {
        NETWORK* n = batch[i];
        /* Best-effort drain, peer is going away so partial is fine.
         * Swallow the return value: error / EAGAIN both lead to the
         * same teardown path next. */
        if (n->reactor_send_buf || (n->send_buf && n->send_buf->nb_items > 0)) {
            (void)reactor_drain_writes(n, reactor);
        }
        /* Half-close the write side so the peer's read side observes
         * EOF immediately. The fd close itself happens later in the
         * caller's netw_close after `reactor_close_acked` flips.
         * n_reactor_unregister publishes that ack with a release store
         * as its final action, so nothing may touch `n` afterwards: the
         * game thread's acquire-load can observe the ack and free the
         * NETWORK the instant unregister returns. */
        shutdown(n->link.sock, SHUT_WR);
        n_reactor_unregister(reactor, n);
    }
}

/* Drain the socket non-blockingly into the NETWORK's recv accumulator.
 * Parses as many complete frames as the bytes allow; pushes each onto
 * recv_buf. Edge-triggered: keeps reading until EAGAIN. Returns 1 on
 * normal progress, 0 if the connection should be torn down (peer
 * closed cleanly, or a hard error). */
static int reactor_handle_readable(NETWORK* netw, n_reactor* reactor) {
    char chunk[4096];
    int eof = 0;

    /* TLS-over-reactor: route through the NETWORK's
     * single-attempt recv pointer (raw recv() on cleartext, SSL_read
     * on crypto sockets). Looping until NETW_IO_WANT_READ both drains
     * the edge-triggered readiness AND empties OpenSSL's internal
     * plaintext buffer (SSL_read reports WANT_READ only once that
     * buffer is dry), so no SSL_pending() poll is needed. */
    netw->reactor_recv_wants_write = 0;

    for (;;) {
        ssize_t got = netw->recv_data_once((void*)netw, chunk, (uint32_t)sizeof(chunk));
        if (got == NETW_SOCKET_DISCONNECTED) {
            eof = 1;
            break;
        }
        if (got == NETW_IO_WANT_READ) break; /* drained */
        if (got == NETW_IO_WANT_WRITE) {
            /* TLS renegotiation: the read needs outbound room first.
             * Flag it so the run loop arms EPOLLOUT and re-runs this
             * drain after the socket turns writable. */
            netw->reactor_recv_wants_write = 1;
            break;
        }
        if (got <= 0) {
            /* NETW_SOCKET_ERROR: already logged inside the helper. */
            return 0;
        }

        /* Feed `got` bytes through the state machine. */
        const char* p = chunk;
        size_t rem = (size_t)got;
        while (rem > 0) {
            switch (netw->reactor_read_phase) {
                case 0: /* STATE word */
                case 1: /* LENGTH word */
                {
                    size_t need = 4 - (size_t)netw->reactor_read_hdr_have;
                    size_t take = (rem < need) ? rem : need;
                    memcpy(netw->reactor_read_hdr_buf + netw->reactor_read_hdr_have,
                           p, take);
                    netw->reactor_read_hdr_have += (int)take;
                    p += take;
                    rem -= take;
                    if (netw->reactor_read_hdr_have < 4) {
                        /* Need more bytes for this header word. */
                        atomic_fetch_add(&reactor->reads_partial, 1);
                        break;
                    }
                    /* Header word complete. */
                    uint32_t word;
                    memcpy(&word, netw->reactor_read_hdr_buf, sizeof(word));
                    word = ntohl(word);
                    netw->reactor_read_hdr_have = 0;
                    if (netw->reactor_read_phase == 0) {
                        netw->reactor_read_pkt_state = word;
                        if (word == NETW_EXIT_ASKED) {
                            /* Peer requested clean shutdown. Treat as EOF. */
                            eof = 1;
                            rem = 0;
                            break;
                        }
                        netw->reactor_read_phase = 1;
                    } else {
                        netw->reactor_read_pkt_length = word;
                        /* Allocate the payload buffer. +1 for the NUL
                         * the dispatch helper writes after fill. */
                        if (word == 0) {
                            /* Zero-byte payload: dispatch immediately
                             * with an empty N_STR-shaped buffer. */
                            char* empty = NULL;
                            Malloc(empty, char, 1);
                            if (!empty) {
                                n_log(LOG_ERR, "n_reactor: alloc(empty payload) failed");
                                return 0;
                            }
                            reactor_recv_dispatch_frame(netw,
                                                        netw->reactor_read_pkt_state,
                                                        0, empty);
                            netw->reactor_read_phase = 0;
                        } else {
                            Malloc(netw->reactor_read_payload, char, word + 1);
                            if (!netw->reactor_read_payload) {
                                n_log(LOG_ERR, "n_reactor: alloc(%u-byte payload) failed",
                                      word);
                                return 0;
                            }
                            netw->reactor_read_payload_have = 0;
                            netw->reactor_read_phase = 2;
                        }
                    }
                    break;
                }
                case 2: /* PAYLOAD */
                {
                    size_t need = (size_t)netw->reactor_read_pkt_length - netw->reactor_read_payload_have;
                    size_t take = (rem < need) ? rem : need;
                    memcpy(netw->reactor_read_payload + netw->reactor_read_payload_have,
                           p, take);
                    netw->reactor_read_payload_have += take;
                    p += take;
                    rem -= take;
                    if (netw->reactor_read_payload_have < netw->reactor_read_pkt_length) {
                        /* Partial payload, stay in payload-read state, wait
                         * for more bytes (next read or next epoll event). */
                        atomic_fetch_add(&reactor->reads_partial, 1);
                        break;
                    }
                    /* Complete payload, dispatch. ownership of the
                     * buffer transfers into the dispatcher. */
                    char* payload = netw->reactor_read_payload;
                    netw->reactor_read_payload = NULL;
                    reactor_recv_dispatch_frame(netw,
                                                netw->reactor_read_pkt_state,
                                                netw->reactor_read_pkt_length,
                                                payload);
                    netw->reactor_read_phase = 0;
                    netw->reactor_read_payload_have = 0;
                    break;
                }
            }
        }
    }
    if (eof) return 0;
    return 1;
}

/* public API */

n_reactor* n_reactor_new(int max_fds_hint) {
    (void)max_fds_hint; /* per-fd table grows on demand at register time */

    n_reactor* r = NULL;
    Malloc(r, n_reactor, 1); /* calloc-backed: r is zero-initialised */
    __n_assert(r, return NULL);
    r->epoll_fd = -1;
    r->stop_efd = -1;
    r->wake_efd = -1;

    /* CLOEXEC because forking the asset auditor (server SIGHUP path
     * does this) shouldn't leak the epoll fd to the child. */
    r->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (r->epoll_fd < 0) {
        n_log(LOG_ERR, "n_reactor_new: epoll_create1 failed: %s",
              strerror(errno));
        goto fail;
    }

    /* Both eventfds: nonblocking (so drain doesn't block the run
     * loop) and CLOEXEC (same fork hygiene). EFD_SEMAPHORE is NOT
     * set, we want each `write` to add to the count, not sit
     * one-event-at-a-time. */
    r->stop_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (r->stop_efd < 0) {
        n_log(LOG_ERR, "n_reactor_new: eventfd(stop) failed: %s",
              strerror(errno));
        goto fail;
    }
    r->wake_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (r->wake_efd < 0) {
        n_log(LOG_ERR, "n_reactor_new: eventfd(wake) failed: %s",
              strerror(errno));
        goto fail;
    }

    if (!register_internal_efd(r->epoll_fd, r->stop_efd, N_REACTOR_TAG_STOP) ||
        !register_internal_efd(r->epoll_fd, r->wake_efd, N_REACTOR_TAG_WAKE)) {
        goto fail;
    }

    r->registered = new_generic_list(MAX_LIST_ITEMS);
    if (!r->registered) {
        n_log(LOG_ERR, "n_reactor_new: cannot allocate registered list");
        goto fail;
    }
    pthread_mutex_init(&r->registered_lock, NULL);

    atomic_store(&r->events_processed, 0);
    atomic_store(&r->writes_partial, 0);
    atomic_store(&r->reads_partial, 0);
    atomic_store(&r->fds_registered, 0);
    atomic_store(&r->fds_unregistered, 0);
    atomic_store(&r->wake_signals, 0);
    atomic_store(&r->wake_walks, 0);
    atomic_store(&r->wake_walk_visits, 0);
    atomic_store(&r->wake_walk_drains, 0);
    r->stop_requested = 0;

    /* Dirty-list: see n_reactor.h dirty_pending field comment for the
     * design rationale. */
    r->dirty_pending = new_generic_list(MAX_LIST_ITEMS);
    if (!r->dirty_pending) {
        n_log(LOG_ERR, "n_reactor_new: cannot allocate dirty_pending list");
        goto fail;
    }
    pthread_mutex_init(&r->dirty_lock, NULL);

    n_log(LOG_INFO, "n_reactor_new: created (epoll_fd=%d stop_efd=%d wake_efd=%d)",
          r->epoll_fd, r->stop_efd, r->wake_efd);
    return r;

fail:
    if (r) {
        if (r->wake_efd >= 0) close(r->wake_efd);
        if (r->stop_efd >= 0) close(r->stop_efd);
        if (r->epoll_fd >= 0) close(r->epoll_fd);
        Free(r);
    }
    return NULL;
}

void n_reactor_destroy(n_reactor** reactor) {
    if (!reactor || !*reactor) return;
    n_reactor* r = *reactor;

    /* If the run loop is still on another thread, the caller
     * should have called `n_reactor_stop` and joined that thread
     * already. We don't enforce, best-effort cleanup either way. */
    if (r->wake_efd >= 0) close(r->wake_efd);
    if (r->stop_efd >= 0) close(r->stop_efd);
    if (r->epoll_fd >= 0) close(r->epoll_fd);
    if (r->registered) {
        list_destroy(&r->registered); /* nodes hold raw NETWORK *, no destructor */
        pthread_mutex_destroy(&r->registered_lock);
    }
    if (r->dirty_pending) {
        list_destroy(&r->dirty_pending); /* same: nodes are NETWORK* aliases, owner == registered list */
        pthread_mutex_destroy(&r->dirty_lock);
    }

    Free(r);
    *reactor = NULL;
}

void n_reactor_run(n_reactor* reactor) {
    if (!reactor) return;

    struct epoll_event events[N_REACTOR_BATCH_SIZE];

    while (!reactor->stop_requested) {
        /* Heartbeat: bound the wait so the EXIT_ASKED sweep runs at
         * least once per second even if nobody calls
         * n_reactor_notify_send / n_reactor_close_netw_sync. The
         * notify path remains the immediate one (sub-millisecond);
         * the heartbeat is the safety net. */
        int n = epoll_wait(reactor->epoll_fd, events,
                           N_REACTOR_BATCH_SIZE, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            n_log(LOG_ERR, "n_reactor_run: epoll_wait failed: %s",
                  strerror(errno));
            break;
        }
        if (n == 0) {
            /* Heartbeat tick: nothing on the wire, but a connection
             * may have been flagged for teardown without an explicit
             * notify. */
            reactor_sweep_exit_asked(reactor);
            continue;
        }

        for (int i = 0; i < n; i++) {
            uint64_t tag = events[i].data.u64;
            atomic_fetch_add(&reactor->events_processed, 1);

            if (tag == N_REACTOR_TAG_STOP) {
                /* Drain the eventfd so it doesn't refire, then
                 * mark the loop for exit. We don't break
                 * immediately, finish processing this batch so
                 * any concurrent NETWORK events still get drained. */
                drain_eventfd(reactor->stop_efd);
                reactor->stop_requested = 1;
                continue;
            }
            if (tag == N_REACTOR_TAG_WAKE) {
                /* Producer-side wakeup. Drain the eventfd, then scan
                 * registered NETWORKs for any with pending sends and
                 * try to drain them, for the common no-back-pressure
                 * case that finishes the work before the next
                 * epoll_wait, avoiding the EPOLLOUT round trip. */
                long long drained = drain_eventfd(reactor->wake_efd);
                atomic_fetch_add(&reactor->wake_signals, drained);

                /* Wake-handler walks ONLY the dirty list. Splice
                 * dirty_pending into a local list under a single lock
                 * pair so producers can continue pushing onto the fresh
                 * list (picked up on the next wake) without contending
                 * with the drain loop. */
                atomic_fetch_add(&reactor->wake_walks, 1);
                long long visits_this_walk = 0;
                long long drains_this_walk = 0;

                LIST* walk_list = NULL;
                int walk_owned = 0; /* 1 = we allocated walk_list, must list_destroy */
                pthread_mutex_t* walk_lock = NULL;
                pthread_mutex_lock(&reactor->dirty_lock);
                {
                    LIST* old = reactor->dirty_pending;
                    LIST* fresh = new_generic_list(MAX_LIST_ITEMS);
                    if (fresh) {
                        reactor->dirty_pending = fresh;
                        walk_list = old;
                        walk_owned = 1;
                    } else {
                        /* Allocation failure: fall back to walking in-place
                         * under the lock. Suboptimal but correct. */
                        walk_list = old;
                        walk_lock = &reactor->dirty_lock;
                    }
                }
                if (walk_owned) {
                    pthread_mutex_unlock(&reactor->dirty_lock);
                }

                LIST_NODE* node = walk_list ? walk_list->start : NULL;
                while (node) {
                    NETWORK* netw = (NETWORK*)node->ptr;
                    LIST_NODE* next = node->next;
                    /* Every entry is by construction a NETWORK with
                     * pending data when it was pushed; the reactor_mode
                     * gate stays for safety against concurrent unregister. */
                    if (netw && netw_atomic_read_reactor_mode(netw)) {
                        visits_this_walk++;
                        /* Clear the in-list flag BEFORE draining.
                         * A producer that pushes during the drain succeeds
                         * the CAS and re-adds for the next wake; no
                         * message can be lost. */
                        __atomic_store_n(&netw->in_dirty_list, 0, __ATOMIC_RELEASE);
                        /* Quick check: nothing to do if both buffers
                         * (in-flight + queue) are empty. The recvbolt
                         * isn't taken, we read nb_items as a hint;
                         * the producer may be in the middle of a
                         * push, but the next wake will catch it. */
                        int has_inflight = (netw->reactor_send_buf != NULL);
                        int has_queued = (netw->send_buf->nb_items > 0);
                        if (has_inflight || has_queued) {
                            drains_this_walk++;
                            int rc = reactor_drain_writes(netw, reactor);
                            if (rc == 0) {
                                /* EAGAIN, arm EPOLLOUT for back-pressure relief. */
                                if (!netw->reactor_write_armed) {
                                    if (reactor_epoll_mod(reactor, netw,
                                                          EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET)) {
                                        netw->reactor_write_armed = 1;
                                    }
                                }
                            } else if (rc < 0) {
                                /* n_reactor_unregister takes its own
                                 * locks (registered_lock + dirty_lock).
                                 * Release whatever walk-side lock we
                                 * currently hold first to preserve the
                                 * documented ordering and avoid
                                 * self-deadlock; reacquire after. Set the
                                 * state flag BEFORE unregister: unregister
                                 * publishes the close ack as its last act,
                                 * after which the game thread may free the
                                 * NETWORK, so netw must not be touched. */
                                if (walk_lock) pthread_mutex_unlock(walk_lock);
                                netw_set(netw, NETW_ERROR);
                                n_reactor_unregister(reactor, netw);
                                if (walk_lock) pthread_mutex_lock(walk_lock);
                            } else {
                                /* Fully drained, disarm EPOLLOUT if armed. */
                                if (netw->reactor_write_armed) {
                                    if (reactor_epoll_mod(reactor, netw,
                                                          EPOLLIN | EPOLLRDHUP | EPOLLET)) {
                                        netw->reactor_write_armed = 0;
                                    }
                                }
                            }
                        }
                    }
                    node = next;
                }
                /* Release whatever walk-side lock we still hold
                 * (dirty_lock in the fallback path, none for the
                 * spliced-list happy path), then free the spliced list
                 * if we owned it. */
                if (walk_lock) pthread_mutex_unlock(walk_lock);
                if (walk_owned && walk_list) {
                    list_destroy(&walk_list); /* nodes are NETWORK* aliases, no destructor */
                }
                /* Flush per-walk visit/drain tally into the lifetime
                 * accumulators after dropping the lock. */
                atomic_fetch_add(&reactor->wake_walk_visits, visits_this_walk);
                atomic_fetch_add(&reactor->wake_walk_drains, drains_this_walk);
                continue;
            }

            /* NETWORK event. data.ptr points at the NETWORK *.
             * We rely on data.u64 >= N_REACTOR_TAG_INTERNAL_MAX
             * because every legitimate pointer is far above 16 in
             * any modern address space. */
            NETWORK* netw = (NETWORK*)events[i].data.ptr;
            if (!netw) continue;

            uint32_t evmask = events[i].events;
            if (evmask & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                /* Hard error or peer closed: flag the NETWORK so the
                 * game thread observes it on next netw_get_msg via the
                 * existing state-flag check, THEN unregister. Order
                 * matters: unregister publishes the close ack as its
                 * last act, releasing a game thread that may be spinning
                 * in n_reactor_close_netw_sync and free the NETWORK, so
                 * netw must not be touched after unregister returns. */
                netw_set(netw, NETW_ERROR);
                n_reactor_unregister(reactor, netw);
                continue;
            }
            if (evmask & EPOLLIN) {
                if (!reactor_handle_readable(netw, reactor)) {
                    /* EOF or unrecoverable read error. Flag before
                     * unregister (unregister publishes the close ack
                     * last; netw may be freed the moment it returns). */
                    netw_set(netw, NETW_EXIT_ASKED);
                    n_reactor_unregister(reactor, netw);
                    continue;
                }
                /* TLS plumbing: a send that returned
                 * WANT_READ (renegotiation) can progress now that
                 * inbound bytes arrived. */
                if (netw->reactor_send_wants_read) {
                    int rc = reactor_drain_writes(netw, reactor);
                    if (rc < 0) {
                        /* Flag before unregister: unregister publishes
                         * the close ack last and netw may be freed once
                         * it returns. */
                        netw_set(netw, NETW_ERROR);
                        n_reactor_unregister(reactor, netw);
                        continue;
                    }
                    if (rc > 0 && netw->reactor_write_armed &&
                        !netw->reactor_recv_wants_write) {
                        if (reactor_epoll_mod(reactor, netw,
                                              EPOLLIN | EPOLLRDHUP | EPOLLET)) {
                            netw->reactor_write_armed = 0;
                        }
                    } else if (rc == 0 && !netw->reactor_write_armed) {
                        if (reactor_epoll_mod(reactor, netw,
                                              EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET)) {
                            netw->reactor_write_armed = 1;
                        }
                    }
                }
                /* TLS plumbing: a recv that returned WANT_WRITE needs
                 * the socket writable: arm EPOLLOUT so the drain
                 * re-runs from that branch. */
                if (netw->reactor_recv_wants_write && !netw->reactor_write_armed) {
                    if (reactor_epoll_mod(reactor, netw,
                                          EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET)) {
                        netw->reactor_write_armed = 1;
                    }
                }
            }
            if (evmask & EPOLLOUT) {
                /* TLS plumbing: a recv blocked on WANT_WRITE
                 * retries first, the socket just turned writable. */
                if (netw->reactor_recv_wants_write) {
                    if (!reactor_handle_readable(netw, reactor)) {
                        /* Flag before unregister: unregister publishes
                         * the close ack last and netw may be freed once
                         * it returns. */
                        netw_set(netw, NETW_EXIT_ASKED);
                        n_reactor_unregister(reactor, netw);
                        continue;
                    }
                }
                int rc = reactor_drain_writes(netw, reactor);
                if (rc < 0) {
                    /* Flag before unregister: unregister publishes the
                     * close ack last and netw may be freed once it
                     * returns. */
                    netw_set(netw, NETW_ERROR);
                    n_reactor_unregister(reactor, netw);
                    continue;
                }
                if (rc > 0 && netw->reactor_write_armed &&
                    !netw->reactor_recv_wants_write) {
                    /* Drained; disarm EPOLLOUT to avoid spurious wakeups
                     * (kept armed while a renegotiating recv still
                     * needs the writable signal). */
                    if (reactor_epoll_mod(reactor, netw,
                                          EPOLLIN | EPOLLRDHUP | EPOLLET)) {
                        netw->reactor_write_armed = 0;
                    }
                }
                /* rc == 0 (back-pressure / WANT) means EPOLLOUT remains
                 * armed, kernel will fire again when buffer has room. */
            }
        }
        /* End-of-batch teardown sweep, pick up any NETWORK whose
         * game-side has flagged EXIT_ASKED during this batch (or
         * earlier batches that didn't reach the snapshot cap). */
        reactor_sweep_exit_asked(reactor);
    }

    n_log(LOG_INFO, "n_reactor_run: exiting (events=%lld)",
          (long long)atomic_load(&reactor->events_processed));
}

void n_reactor_stop(n_reactor* reactor) {
    if (!reactor || reactor->stop_efd < 0) return;
    /* eventfd write semantics: any 8-byte value > 0 increments the
     * counter and triggers EPOLLIN on the read side. Use 1 for
     * clarity. */
    uint64_t one = 1;
    ssize_t w = write(reactor->stop_efd, &one, sizeof(one));
    (void)w; /* best-effort; if it fails the loop will exit on next
              * idle epoll_wait via stop_requested check */
}

void* n_reactor_run_thread_entry(void* arg) {
    n_reactor_run((n_reactor*)arg);
    return NULL;
}

void n_reactor_get_stats(const n_reactor* reactor, n_reactor_stats* out) {
    if (!out) return;
    if (!reactor) {
        memset(out, 0, sizeof(*out));
        return;
    }
    out->events_processed = atomic_load(&reactor->events_processed);
    out->writes_partial = atomic_load(&reactor->writes_partial);
    out->reads_partial = atomic_load(&reactor->reads_partial);
    out->fds_registered = atomic_load(&reactor->fds_registered);
    out->fds_unregistered = atomic_load(&reactor->fds_unregistered);
    out->wake_signals = atomic_load(&reactor->wake_signals);
    out->wake_walks = atomic_load(&reactor->wake_walks);
    out->wake_walk_visits = atomic_load(&reactor->wake_walk_visits);
    out->wake_walk_drains = atomic_load(&reactor->wake_walk_drains);
}

int n_reactor_register(n_reactor* reactor, NETWORK* netw) {
    if (!reactor || !netw) return 0;
    if (netw_atomic_read_reactor_mode(netw)) {
        n_log(LOG_WARNING, "n_reactor_register: socket %d already in reactor mode",
              netw->link.sock);
        return 0;
    }
    if (netw->threaded_engine_status == NETW_THR_ENGINE_STARTED) {
        n_log(LOG_ERR,
              "n_reactor_register: socket %d has thread engine started; "
              "reactor and thread mode are mutually exclusive",
              netw->link.sock);
        return 0;
    }
    if (!set_nonblocking(netw->link.sock)) {
        n_log(LOG_ERR, "n_reactor_register: O_NONBLOCK on socket %d failed: %s",
              netw->link.sock, strerror(errno));
        return 0;
    }
    /* The reactor's I/O goes through the single-attempt
     * pointers (TLS-aware). netw_new seeds them (and the crypto setup
     * swaps in the SSL variants), but a hand-constructed NETWORK
     * shell (tests, embedders) may leave them NULL, default to the
     * raw cleartext variants rather than dereferencing NULL. */
    if (!netw->send_data_once) netw->send_data_once = &send_data_once;
    if (!netw->recv_data_once) netw->recv_data_once = &recv_data_once;

    /* Reset accumulator state from any prior use of the slot. */
    reactor_recv_state_reset(netw);
    reactor_send_state_reset(netw);
    /* Clear stale ack from a prior cycle. This runs before the netw is
     * published to the reactor thread (reactor_mode release, below), so
     * it cannot race the sweep; use the same atomic accessor for
     * consistency with the release/acquire pair on this flag. */
    __atomic_store_n(&netw->reactor_close_acked, 0, __ATOMIC_RELEASE);

    struct epoll_event ev;
    /* Edge-triggered: drain the socket fully on each event. EPOLLRDHUP
     * gives us peer-half-close as a separate signal. EPOLLOUT is added
     * lazily by the send path when there's pending data. */
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    ev.data.ptr = netw;
    if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_ADD, netw->link.sock, &ev) != 0) {
        n_log(LOG_ERR, "n_reactor_register: epoll_ctl ADD socket %d failed: %s",
              netw->link.sock, strerror(errno));
        return 0;
    }
    netw_atomic_write_reactor_handle(netw, (void*)reactor);
    /* Latch that this NETWORK now owes a reactor close-handshake ack.
     * Set only here, on full registration success, and never cleared by
     * the reactor; netw_close keys its ack-wait on this rather than on
     * the live reactor_mode (see the field doc in n_network.h). The
     * register failure paths above all return 0 before this point, so a
     * caller that netw_close's a never-registered NETWORK leaves the
     * latch 0 and avoids an unanswerable wait. */
    __atomic_store_n(&netw->reactor_registered, 1, __ATOMIC_RELEASE);
    /* Publish reactor_mode last (release on the flag pairs with the
     * acquire reads in netw_close / netw_add_msg) so any thread that
     * sees reactor_mode == 1 also sees a non-NULL reactor_handle. */
    netw_atomic_write_reactor_mode(netw, 1);

    /* Track in the reactor's registered list so the wake-event
     * handler can find this NETWORK without a tree lookup. The
     * list_push happens with NULL destructor, nodes hold raw
     * NETWORK *, ownership stays with the caller. */
    pthread_mutex_lock(&reactor->registered_lock);
    list_push(reactor->registered, netw, NULL);
    pthread_mutex_unlock(&reactor->registered_lock);

    atomic_fetch_add(&reactor->fds_registered, 1);
    n_log(LOG_DEBUG, "n_reactor: registered socket %d", netw->link.sock);
    return 1;
}

void n_reactor_unregister(n_reactor* reactor, NETWORK* netw) {
    if (!reactor || !netw) return;
    if (!netw_atomic_read_reactor_mode(netw)) return;
    /* EPOLL_CTL_DEL is best-effort, if the socket is already closed
     * the kernel returns EBADF, which we silently ignore. */
    if (epoll_ctl(reactor->epoll_fd, EPOLL_CTL_DEL, netw->link.sock, NULL) != 0) {
        if (errno != EBADF && errno != ENOENT) {
            n_log(LOG_WARNING,
                  "n_reactor_unregister: epoll_ctl DEL socket %d "
                  "failed: %s",
                  netw->link.sock, strerror(errno));
        }
    }
    reactor_recv_state_reset(netw);
    reactor_send_state_reset(netw);

    /* Remove from registered list. Walk to find, small-N typical. */
    pthread_mutex_lock(&reactor->registered_lock);
    LIST_NODE* node = reactor->registered->start;
    while (node) {
        if (node->ptr == (void*)netw) {
            remove_list_node(reactor->registered, node, NETWORK);
            break;
        }
        node = node->next;
    }
    pthread_mutex_unlock(&reactor->registered_lock);

    /* Defensive removal from dirty_pending. A racing producer
     * could have CAS'd in_dirty_list to 1 between the wake handler's
     * splice and our unregister. Walk + remove if present, then clear
     * in_dirty_list BEFORE clearing reactor_mode (below) so a producer
     * that observes the cleared mode never leaves a stale flag. */
    pthread_mutex_lock(&reactor->dirty_lock);
    {
        LIST_NODE* dn = reactor->dirty_pending->start;
        while (dn) {
            if (dn->ptr == (void*)netw) {
                remove_list_node(reactor->dirty_pending, dn, NETWORK);
                break;
            }
            dn = dn->next;
        }
    }
    pthread_mutex_unlock(&reactor->dirty_lock);
    __atomic_store_n(&netw->in_dirty_list, 0, __ATOMIC_RELEASE);

    /* Clear reactor_mode first (release) so any thread that observes
     * the cleared flag will not subsequently dereference an invalid
     * reactor_handle pointer. The handle is then nulled, a brief
     * window exists where mode == 0 but handle != NULL, which is
     * harmless because every consumer guards on mode first. */
    netw_atomic_write_reactor_mode(netw, 0);
    netw_atomic_write_reactor_handle(netw, NULL);
    atomic_fetch_add(&reactor->fds_unregistered, 1);
    n_log(LOG_DEBUG, "n_reactor: unregistered socket %d", netw->link.sock);

    /* Publish the close ack as the very last action. Every unregister
     * path, the EXIT_ASKED sweep, a peer EOF / EPOLLRDHUP, or a hard
     * I/O error, must release a game thread that may already be
     * spinning in n_reactor_close_netw_sync; only the sweep used to do
     * this, so an event-driven unregister stranded the close (TSan
     * timeout). The release store orders every side effect above before
     * the game thread's acquire-load, after which it may close(fd) and
     * free the NETWORK, so nothing below this line may touch `netw`. */
    __atomic_store_n(&netw->reactor_close_acked, 1, __ATOMIC_RELEASE);
}

void n_reactor_notify_send(NETWORK* netw) {
    if (!netw) return;
    n_reactor* r = (n_reactor*)netw_atomic_read_reactor_handle(netw);
    if (!r) return;
    if (r->wake_efd < 0) return;

    /* Dirty-list: add this NETWORK to the reactor's pending list via
     * a CAS guard so only the first push between drains takes the
     * dirty_lock. Multiple producer threads pushing to the SAME
     * NETWORK between drains see CAS fail and skip the push (the first
     * one already enqueued it). The reactor clears in_dirty_list BEFORE
     * draining so any producer that pushes during the drain succeeds
     * the CAS and re-adds for the next pass. */
    {
        int expected = 0;
        if (__atomic_compare_exchange_n(&netw->in_dirty_list, &expected, 1,
                                        0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            pthread_mutex_lock(&r->dirty_lock);
            list_push(r->dirty_pending, netw, NULL); /* NULL dtor: alias only */
            pthread_mutex_unlock(&r->dirty_lock);
        }
    }

    uint64_t one = 1;
    ssize_t w = write(r->wake_efd, &one, sizeof(one));
    (void)w; /* best-effort; eventfd write of 8 bytes either succeeds
              * fully or returns EAGAIN if the counter is at UINT64_MAX
              * minus 1, which is impossible in practice */
}

void n_reactor_close_netw_sync(NETWORK* netw) {
    if (!netw) return;

    /* Step 1: if the connection is still registered, prod the reactor to
     * tear it down. We must NOT gate the whole function on reactor_mode:
     * the reactor can clear reactor_mode mid-unregister (peer EOF / hard
     * error) and then keep touching the NETWORK for a few more lines
     * (the EPOLL_CTL_DEL, list removal, the unregistered-socket log read
     * of link.sock) before publishing the ack. Returning early on
     * reactor_mode == 0 would let the caller free the NETWORK under those
     * accesses (use-after-free). Instead we always wait for the ack
     * below; this prod is only needed when the reactor has not yet begun
     * the teardown. The reactor_mode read is racy but only steers the
     * (idempotent) prod, not the free, so the raciness is benign.
     *
     * netw_set serialises under eventbolt; n_reactor_notify_send is a
     * no-op once reactor_handle has been nulled, which is fine because in
     * that case the ack is already set or imminently will be. */
    if (netw_atomic_read_reactor_mode(netw)) {
        netw_set(netw, NETW_EXIT_ASKED);
        n_reactor_notify_send(netw);
    }

    /* Step 2: wait for the reactor to ack. Busy-poll with a 1 ms sleep,
     * same shape as the existing nb_running_threads spin in netw_close.
     * The ack is published by n_reactor_unregister as the very last thing
     * the reactor ever does with this NETWORK, regardless of which path
     * unregistered it (the EXIT_ASKED sweep in the common case, but also
     * a concurrent peer EOF / hard error). reactor_mode is always cleared
     * before the ack store, so once we observe reactor_mode == 0 the ack
     * is guaranteed to land. The acquire-load pairs with unregister's
     * release store so all of its side effects (epoll DEL, cleared
     * reactor_mode, freed accumulators, list removal, the final log) are
     * visible, and ordered before, the caller's subsequent close(fd) /
     * free. */
    while (!__atomic_load_n(&netw->reactor_close_acked, __ATOMIC_ACQUIRE)) {
        struct timespec ts = {0, 1000000L}; /* 1 ms */
        nanosleep(&ts, NULL);
    }
}

NETWORK* netw_accept_into_reactor(NETWORK* listener,
                                  size_t send_list_limit,
                                  size_t recv_list_limit,
                                  int blocking,
                                  n_reactor* reactor,
                                  int* retval) {
    if (!reactor) {
        n_log(LOG_ERR, "netw_accept_into_reactor: NULL reactor");
        if (retval) *retval = EINVAL;
        return NULL;
    }
    NETWORK* netw = netw_accept_from_ex(listener, send_list_limit,
                                        recv_list_limit, blocking, retval);
    if (!netw) return NULL;
    /* Register before returning so the caller can't accidentally
     * call netw_start_thr_engine on a reactor-mode connection.
     * Failure path closes the freshly-accepted socket. */
    if (!n_reactor_register(reactor, netw)) {
        n_log(LOG_ERR, "netw_accept_into_reactor: register failed for socket %d",
              netw->link.sock);
        netw_close(&netw);
        if (retval) *retval = EIO;
        return NULL;
    }
    return netw;
}

#else /* N_REACTOR_AVAILABLE */

/* non-Linux stubs */

n_reactor* n_reactor_new(int max_fds_hint) {
    (void)max_fds_hint;
    n_log(LOG_INFO,
          "n_reactor_new: epoll/eventfd not available on this "
          "platform; reactor mode unsupported (use thread mode)");
    return NULL;
}

void n_reactor_destroy(n_reactor** reactor) {
    if (reactor) *reactor = NULL;
}

void n_reactor_run(n_reactor* reactor) {
    (void)reactor;
}

void n_reactor_stop(n_reactor* reactor) {
    (void)reactor;
}

void* n_reactor_run_thread_entry(void* arg) {
    (void)arg;
    return NULL;
}

void n_reactor_get_stats(const n_reactor* reactor, n_reactor_stats* out) {
    (void)reactor;
    if (out) memset(out, 0, sizeof(*out));
}

int n_reactor_register(n_reactor* reactor, NETWORK* netw) {
    (void)reactor;
    (void)netw;
    return 0;
}

void n_reactor_unregister(n_reactor* reactor, NETWORK* netw) {
    (void)reactor;
    (void)netw;
}

void n_reactor_notify_send(NETWORK* netw) {
    (void)netw;
}

void n_reactor_close_netw_sync(NETWORK* netw) {
    (void)netw;
}

NETWORK* netw_accept_into_reactor(NETWORK* listener,
                                  size_t send_list_limit,
                                  size_t recv_list_limit,
                                  int blocking,
                                  n_reactor* reactor,
                                  int* retval) {
    (void)listener;
    (void)send_list_limit;
    (void)recv_list_limit;
    (void)blocking;
    (void)reactor;
    if (retval) *retval = ENOSYS;
    return NULL;
}

#endif /* N_REACTOR_AVAILABLE */
