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
 *@file n_network.h
 *@brief Network Engine
 *@author Castagnier Mickael
 *@version 2.0
 *@date 11/03/2015
 */

#ifndef N_NETWORK
#define N_NETWORK

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup NETWORKING NETWORK ENGINE: connect, accept, send and recv wrappers. Network Queue, thread-safe add/get message, ssl/tls secured communications
  @addtogroup NETWORKING
  @{
  */

#include "n_common.h"
#include "n_str.h"
#include "n_list.h"
#include "n_hash.h"

/*! Enable/disable recv of length 0. Warning: only enable if you know what you are doing ! (TRUE or FALSE)*/
#define NETWORK_DISABLE_ZERO_LENGTH_RECV FALSE
/*! Flag for auto detection by OS of ip version to use */
#define NETWORK_IPALL 0
/*! Flag to force IPV4  */
#define NETWORK_IPV4 1
/*! Flag to force IPV6  */
#define NETWORK_IPV6 2
/*! Flag for TCP transport (default) */
#define NETWORK_TCP 0
/*! Flag for UDP transport */
#define NETWORK_UDP 1
/*! Flag to set send buffer depletion timeout  */
#define NETWORK_DEPLETE_SOCKET_TIMEOUT 512
/*! Flag to set network queues depletion timeout  */
#define NETWORK_DEPLETE_QUEUES_TIMEOUT 1024
/*! Flag to set consecutive send waiting timeout  */
#define NETWORK_CONSECUTIVE_SEND_WAIT 2048
/*! Flag to set network closing wait timeout */
#define NETWORK_WAIT_CLOSE_TIMEOUT 4096
/*! Size of a HEAD message */
#define HEAD_SIZE 10
/*! Code of a HEAD message */
#define HEAD_CODE 3
/*! code for a socket error */
#define NETW_SOCKET_ERROR -1
/*! Code for a disconnected recv */
#define NETW_SOCKET_DISCONNECTED -2
/*! single-attempt I/O (send_data_once / recv_data_once):
 *  the operation cannot progress until the socket is READABLE.
 *  Raw recv maps EAGAIN here; TLS maps SSL_ERROR_WANT_READ, note a
 *  TLS *send* can also return this mid-renegotiation. */
#define NETW_IO_WANT_READ -10
/*! single-attempt I/O: the operation cannot progress until
 *  the socket is WRITABLE. Raw send maps EAGAIN here; TLS maps
 *  SSL_ERROR_WANT_WRITE, a TLS *recv* can also return this. */
#define NETW_IO_WANT_WRITE -11
/*! Send or recv max number of retries */
#define NETW_MAX_RETRIES 8
/*! Send or recv delay between retries in usec */
#define NETW_RETRY_DELAY 1000
/*! Internal DONE value indicating a send/recv thread reached end-of-life cleanly (peer QUIT or local NETW_EXIT_ASKED ack'd). */
#define NETW_THR_EXIT_OK 100
/*! Internal DONE value indicating a send/recv thread observed NETW_ERROR without an in-flight local shutdown. */
#define NETW_THR_EXIT_ERROR 666

#ifndef SOCKET
/*! default socket declaration */
#ifdef __windows__
#ifndef ARCH32BITS
/*! socket type for windows */
typedef long long unsigned int SOCKET;
/*! socket associated printf style */
#define SOCKET_SIZE_FORMAT "%zu"
#endif
#else
/*! socket type for POSIX systems */
typedef int SOCKET;
/*! socket associated printf style */
#define SOCKET_SIZE_FORMAT "%d"
#endif
#endif

#if defined(__linux__) || defined(__sun) || defined(_AIX)
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <semaphore.h>

/*! socket associated printf style */
#define SOCKET_SIZE_FORMAT "%d"

#ifdef __linux__
#include <linux/sockios.h>
#include <signal.h>
#endif

#define netw_unload()

/*! socket wrapper */
#define closesocket close
/*! Netflag for sigpipe */
#define NETFLAGS MSG_NOSIGNAL /* for program to not quit under linux when a connection is crashing 2 times */
/*! missing flag */
#define INVALID_SOCKET -1
/*! cast for send/recv buffer length: int on Windows, size_t on POSIX */
#define NETW_BUFLEN_CAST(x) ((size_t)(x))

#include "n_time.h"

#elif defined __windows__

#define SHUT_WR SD_SEND
#define SHUT_RD SD_RECEIVE
#define SHUT_RDWR SD_BOTH

#ifndef ECONNRESET
#define ECONNRESET 104
#endif

#include "nilorea/n_windows.h"

#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include "n_time.h"

#ifndef MSG_EOR
#define MSG_EOR 0
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef MSG_WAITALL
#define MSG_WAITALL 0
#endif

#ifndef EAI_SYSTEM
#define EAI_SYSTEM 0
#endif

#ifndef AI_PASSIVE
#define AI_PASSIVE 0x00000001
#endif
#ifndef AI_CANONNAME
#define AI_CANONNAME 0x00000002
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0x00000004
#endif
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0x00000008
#endif
#ifndef AI_ALL
#define AI_ALL 0x00000100
#endif
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0x00000400
#endif
#ifndef AI_V4MAPPED
#define AI_V4MAPPED 0x00000800
#endif
#ifndef AI_NON_AUTHORITATIVE
#define AI_NON_AUTHORITATIVE 0x00004000
#endif
#ifndef AI_SECURE
#define AI_SECURE 0x00008000
#endif
#ifndef AI_RETURN_PREFERRED_NAMES
#define AI_RETURN_PREFERRED_NAMES 0x00010000
#endif

#include <pthread.h>
#include <semaphore.h>

/*! Netflag for sigpipe */
#define NETFLAGS 0 /* no flags needed for microsoft */

/*! cast for send/recv buffer length: int on Windows, size_t on POSIX */
#define NETW_BUFLEN_CAST(x) ((int)(x))

/*! WINDOWS ONLY call at program exit. Unload WSA DLL and call cleanups, no effect on other OS */
#define netw_unload() netw_init_wsa(0, 2, 2)

#endif

#ifdef HAVE_OPENSSL
#define _OPEN_SYS_SOCK_IPV6 1
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#endif

/*! Network codes definition */
#define N_ENUM_netw_code_type(_)     \
    _(NETW_CLIENT, 2)                \
    _(NETW_SERVER, 4)                \
    _(NETW_RESTART_TIMER, 8)         \
    _(NETW_EMPTY_SENDBUF, 16)        \
    _(NETW_EMPTY_RECVBUF, 32)        \
    _(NETW_RUN, 64)                  \
    _(NETW_EXIT_ASKED, 128)          \
    _(NETW_EXITED, 256)              \
    _(NETW_ERROR, 512)               \
    _(NETW_ENCRYPT_NONE, 1024)       \
    _(NETW_ENCRYPT_OPENSSL, 2048)    \
    _(NETW_THR_ENGINE_STARTED, 4096) \
    _(NETW_THR_ENGINE_STOPPED, 8192) \
    _(NETW_DESTROY_RECVBUF, 16384)   \
    _(NETW_DESTROY_SENDBUF, 32768)   \
    _(NETW_COMPRESSED_ZLIB, 65536)   \
    _(NETW_COMPRESSED_LZ4, 131072)

/*! Legacy alias, the first compression commit shipped a single
 *  NETW_COMPRESSED flag before LZ4 was added. Keep the name for any
 *  downstream consumer that referenced it before this split. */
#define NETW_COMPRESSED NETW_COMPRESSED_ZLIB

/*! Compression mode selector for NETWORK.compress_mode.  The
 *  per-packet state word on the wire carries NETW_COMPRESSED_ZLIB
 *  or NETW_COMPRESSED_LZ4 so a receiver in NETW_COMPRESS_AUTO (the
 *  default) can decode whichever algorithm the sender chose, while
 *  a receiver pinned to a specific algorithm will decode only that
 *  flag and pass anything else through uncompressed. */
typedef enum NETW_COMPRESS_MODE {
    /*! no automatic compression on send, still decompresses inbound */
    NETW_COMPRESS_NONE = 0,
    /*! compress on send with zlib, decompress either on recv */
    NETW_COMPRESS_ZLIB = 1,
    /*! compress on send with LZ4, decompress either on recv */
    NETW_COMPRESS_LZ4 = 2
} NETW_COMPRESS_MODE;

/*! Opportunistic compression policy. Outbound payloads shorter than
 *  NETW_COMPRESS_THRESHOLD bytes skip the codec entirely; longer
 *  payloads are compressed only if the result is at least
 *  NETW_COMPRESS_MIN_RATIO percent smaller than the original
 *  (otherwise the original is shipped uncompressed). The reactor's
 *  send path (n_reactor_drain_writes) shares these knobs with the
 *  thread engine's `netw_send_func`. */
#define NETW_COMPRESS_THRESHOLD 256u
#define NETW_COMPRESS_MIN_RATIO 10u

/*! Network codes declaration */
N_ENUM_DECLARE(N_ENUM_netw_code_type, __netw_code_type);

/*! send/recv func ptr type */
typedef ssize_t (*netw_func)(void*, char*, uint32_t);

/*! Structure of a N_SOCKET */
typedef struct N_SOCKET {
    /*!port of socket*/
    char* port;
    /*!a normal socket*/
    SOCKET sock;
    /*!ip of the connected socket*/
    char* ip;

    /*! flag to quickly check socket mode */
    unsigned long int is_blocking;

    /*!address of local machine*/
    struct addrinfo hints;
    /*! getaddrinfo results */
    struct addrinfo* rhost;
    /*! connected remote addr */
    struct sockaddr_storage raddr;
} N_SOCKET;

/*! Structure of a NETWORK */
typedef struct NETWORK {
    /*! Nb pending connection,if listening */
    int nb_pending,
        /*! NETWORK mode , 1 listening, 0 connecting */
        mode,
        /*! Threaded network engine state for this network. NETW_THR_ENGINE_STARTED or NETW_THR_ENGINE_STOPPED */
        threaded_engine_status,
        /*! Internal flag to know if we have to free addr infos */
        addr_infos_loaded,
        /*! send queue consecutive pool interval, used when there are still items to send, in usec */
        send_queue_consecutive_wait,
        /*! so reuseaddr state */
        so_reuseaddr,
        /* disabled so reuseport state */
        // so_reuseport,
        /*! so keepalive state */
        so_keepalive,
        /*! state of naggle algorythm, 0 untouched, 1 forcibly disabled */
        tcpnodelay,
        /*! size of the socket send buffer, 0 untouched, else size in bytes */
        so_sndbuf,
        /*! size of the socket recv buffer, 0 untouched, else size in bytes */
        so_rcvbuf,
        /*! send timeout value */
        so_sndtimeo,
        /*! send timeout value */
        so_rcvtimeo,
        /*! close lingering value (-1 disabled, 0 force close, >0 linger ) */
        so_linger,
        /*! tell if the socket have to be encrypted (flags NETW_CRYPTO_*) */
        crypto_mode,
        /*! if encryption is on, which one (flags NETW_ENCRYPT_*) */
        crypto_algo,
        /*! if part of a user property, id of the user */
        user_id,
        /*! nb running threads, if > 0 thread engine is still running */
        nb_running_threads,
        /*! deplete network queues timeout ( 0 disabled, > 0 wait for timeout and check unset/unack datas) */
        deplete_queues_timeout,
        /*! deplete socket send buffer timeout ( 0 disabled, > 0 wait for timeout and check unset/unack datas) */
        deplete_socket_timeout,
        /*! network wait close timeout value ( < 1 disabled, >= 1 timeout sec ) */
        wait_close_timeout,
        /*! transport type: NETWORK_TCP (0) or NETWORK_UDP (1) */
        transport_type;

    /*! state of the connection , NETW_RUN, NETW_QUIT, NETW_STOP , NETW_ERR */
    uint32_t state;

    /*! send func ptr */
    netw_func send_data,
        /*! receive func ptr */
        recv_data,
        /*! single-attempt send (non-blocking / reactor use).
         *  Contract: one transport attempt; > 0 bytes written,
         *  NETW_IO_WANT_READ / NETW_IO_WANT_WRITE when the transport
         *  needs that readiness to progress, NETW_SOCKET_DISCONNECTED,
         *  or NETW_SOCKET_ERROR. Dispatches to the TLS variant when
         *  crypto is set, this is what lets the epoll reactor carry
         *  TLS connections. */
        send_data_once,
        /*! single-attempt recv, same contract as
         *  send_data_once. */
        recv_data_once;

#ifdef HAVE_OPENSSL
    /*! SSL method container */
    const SSL_METHOD* method;
    /*! SSL context holder */
    SSL_CTX* ctx;
    /*! SSL handle */
    SSL* ssl;
    /*! openssl certificate file */
    char *certificate,
        /*! openssl key file */
        *key;
#endif

    /*!networking socket*/
    N_SOCKET link;

    /*!sending buffer (for outgoing queuing )*/
    LIST* send_buf;
    /*!reveicing buffer (for incomming usage)*/
    LIST* recv_buf;

    /*!sending thread*/
    pthread_t send_thr;
    /*!receiving thread*/
    pthread_t recv_thr;

    /*!mutex for threaded access of send_buf */
    pthread_mutex_t sendbolt;
    /*!mutex for threaded access of recv buf */
    pthread_mutex_t recvbolt;
    /*!mutex for threaded access of state event */
    pthread_mutex_t eventbolt;
    /*! block sending func */
    sem_t send_blocker;

    /*! pointers to network pools if members of any */
    LIST* pools;

    /*! per-connection error capture ring buffer (max 8 entries, 512 chars each) */
    char netw_errors[8][512];
    /*! number of captured errors */
    int netw_err_count;
    /*! next write slot in ring buffer */
    int netw_err_next;

    /*! Per-packet compression mode, see NETW_COMPRESS_MODE. Set via
     *  netw_set_compression_mode(); default is NETW_COMPRESS_ZLIB to
     *  preserve the behaviour from when a single unconditional
     *  compression path was the only option. Any NETW_COMPRESSED_*
     *  flag on the wire is honoured on recv regardless of this
     *  field, so two peers with different send-side picks still
     *  interop. */
    int compress_mode;

    /*! Opt-in epoll-reactor mode flag. 0 (default) = classic
     *  per-connection thread engine; 1 = registered with an
     *  `n_reactor`. Atomically set by `n_reactor_register`, cleared
     *  by `n_reactor_unregister`. Read concurrently from the game
     *  thread (e.g. `netw_close`, `netw_add_msg`), always access
     *  via `netw_atomic_read_reactor_mode` /
     *  `netw_atomic_write_reactor_mode` so the read/clear race
     *  during peer-driven unregister is well-defined under TSan.
     *  Always 0 on platforms where N_REACTOR_AVAILABLE == 0. */
    int reactor_mode;

    /*! Back-pointer to the registered reactor. Opaque void * so
     *  n_network.h doesn't need to include n_reactor.h (avoids a
     *  header-ordering dependency since n_reactor.h includes
     *  n_network.h itself). NULL when reactor_mode == 0.
     *  Same atomicity contract as `reactor_mode`: use
     *  `netw_atomic_read_reactor_handle` /
     *  `netw_atomic_write_reactor_handle`. */
    void* reactor_handle;

    /* Incremental recv state for reactor mode. Unused by thread
     * mode (`netw_recv_func` reads blocking). The state machine
     * has three phases: STATE word (4 B), LENGTH word (4 B),
     * PAYLOAD (length B), then push to recv_buf and reset. Per-frame
     * state lives across multiple readable events because TCP can
     * deliver any prefix of a frame before EAGAIN. */
    int reactor_read_phase;                /*!< 0=STATE, 1=LENGTH, 2=PAYLOAD */
    unsigned char reactor_read_hdr_buf[4]; /*!< accumulator for header words */
    int reactor_read_hdr_have;             /*!< bytes accumulated in reactor_read_hdr_buf */
    uint32_t reactor_read_pkt_state;       /*!< state word for the in-flight frame */
    uint32_t reactor_read_pkt_length;      /*!< payload length for the in-flight frame */
    char* reactor_read_payload;            /*!< malloc'd accumulator for payload bytes */
    size_t reactor_read_payload_have;      /*!< bytes accumulated into reactor_read_payload */

    /* Incremental send state for reactor mode. Mirrors
     * `netw_send_func`'s output: each queued N_STR is pre-framed
     * (4-byte state + 4-byte length + payload, with optional
     * compression) into a contiguous buffer; the reactor sends as
     * the kernel buffer accepts bytes, tracking offset across
     * multiple writable events. `reactor_write_armed` mirrors
     * whether EPOLLOUT is in the registered events, used to
     * arm/disarm exactly once per back-pressure cycle. */
    char* reactor_send_buf;  /*!< malloc'd framed bytes, NULL when idle */
    size_t reactor_send_len; /*!< total bytes in reactor_send_buf */
    size_t reactor_send_off; /*!< bytes already sent to socket */
    int reactor_write_armed; /*!< 1 = EPOLLOUT currently registered */
    /*! TLS-over-reactor: the in-flight send returned
     *  NETW_IO_WANT_READ (renegotiation), the reactor retries the
     *  write drain after the next readable event instead of waiting
     *  on EPOLLOUT. Always 0 on cleartext sockets. */
    int reactor_send_wants_read;
    /*! TLS-over-reactor: recv returned NETW_IO_WANT_WRITE, the
     *  reactor re-runs the readable drain after the next writable
     *  event. Always 0 on cleartext sockets. */
    int reactor_recv_wants_write;

    /*! Dirty-list membership flag. CAS-guards a NETWORK's membership
     *  in the reactor's `dirty_pending` list. Producer side
     *  (`n_reactor_notify_send`) attempts a 0-to-1 CAS; on success it
     *  pushes the NETWORK onto the dirty list and writes the
     *  wake-eventfd. Consumer side (`n_reactor_run` wake handler)
     *  clears the flag BEFORE draining so a concurrent producer can
     *  immediately re-add the NETWORK for the next pass: no message
     *  can be lost. */
    int in_dirty_list;

    /*! Close handshake. Game thread sets NETW_EXIT_ASKED, notifies
     *  the reactor, and waits on this flag. `n_reactor_unregister`
     *  publishes it with a release store as its final action, so
     *  EVERY unregister path (the EXIT_ASKED sweep, a peer EOF /
     *  EPOLLRDHUP, or a hard I/O error) releases the waiter, not just
     *  the sweep. The acquire-load on the game side observes, and is
     *  ordered after, every side effect of unregister (epoll DEL,
     *  cleared reactor_mode, freed accumulators, list removal) before
     *  it closes the socket / frees the NETWORK. Reset to 0 in
     *  `n_reactor_register`. Always accessed via __atomic builtins
     *  (release on the reactor side, acquire on the game side); plain
     *  `volatile` would give no happens-before and is a data race. */
    int reactor_close_acked;

    /*! Reactor close-handshake latch. Set to 1 by `n_reactor_register`
     *  and never cleared by the reactor (unlike `reactor_mode`, which
     *  the reactor clears mid-unregister). `netw_close` keys its decision
     *  to run `n_reactor_close_netw_sync` on THIS flag rather than on the
     *  live `reactor_mode`: otherwise, if the reactor clears reactor_mode
     *  during a peer-EOF unregister in the instant before netw_close
     *  reads it, the game thread would skip the ack-wait and free the
     *  NETWORK while the reactor is still inside `n_reactor_unregister`
     *  (a use-after-free / data race). With the latch the game thread
     *  always waits for `reactor_close_acked`, which the reactor publishes
     *  as the last thing it ever does with the NETWORK. Acquire/release
     *  __atomic access, same contract as the other reactor flags. */
    int reactor_registered;

    /*! Measured DNS resolution time of the most recent connect, in
     *  microseconds (0 if not measured). Populated by netw_connect_ex_to
     *  so callers can report a connect-phase timing breakdown. */
    long long connect_dns_usec;
    /*! Measured TCP connect time of the most recent connect, in
     *  microseconds (0 if not measured). Populated by netw_connect_ex_to. */
    long long connect_tcp_usec;

} NETWORK;

/*! Lock-free atomic read of the network state field.
 *  Uses GCC __atomic builtins (supported on Linux gcc, Solaris gcc, Windows mingw gcc).
 *  Provides acquire semantics so subsequent reads see up-to-date values.
 *  For use in send/recv thread hot loops to avoid mutex overhead. */
#define netw_atomic_read_state(netw) __atomic_load_n(&(netw)->state, __ATOMIC_ACQUIRE)

/*! Lock-free atomic write of the network state field.
 *  Uses GCC __atomic builtins with release semantics so prior writes are
 *  visible to threads performing an acquire load. */
#define netw_atomic_write_state(netw, val) __atomic_store_n(&(netw)->state, (val), __ATOMIC_RELEASE)

/*! Lock-free atomic read of the reactor_mode flag. The reactor thread
 *  clears this flag inside `n_reactor_unregister` (peer EOF, EXIT_ASKED
 *  sweep, or game-thread close handshake), while the game thread reads
 *  it from `netw_close` and `netw_add_msg` to decide which dispatch
 *  path to take. Both outcomes are correct (the close handshake is
 *  designed to be idempotent), but TSan still requires synchronised
 *  access to the read/write pair. */
#define netw_atomic_read_reactor_mode(netw) __atomic_load_n(&(netw)->reactor_mode, __ATOMIC_ACQUIRE)
#define netw_atomic_write_reactor_mode(netw, val) __atomic_store_n(&(netw)->reactor_mode, (val), __ATOMIC_RELEASE)
/*! Same contract for the back-pointer to the reactor. NULL after
 *  unregister. Reads in `netw_add_msg` / `n_reactor_close_netw_sync`
 *  pair with the write inside `n_reactor_unregister`. */
#define netw_atomic_read_reactor_handle(netw) __atomic_load_n(&(netw)->reactor_handle, __ATOMIC_ACQUIRE)
#define netw_atomic_write_reactor_handle(netw, val) __atomic_store_n(&(netw)->reactor_handle, (val), __ATOMIC_RELEASE)

/*! structure of a network pool */
typedef struct NETWORK_POOL {
    /*! table of clients */
    HASH_TABLE* pool;

    /*! thread safety */
    pthread_rwlock_t rwlock;

} NETWORK_POOL;

/*! structure for splitting HTTP requests */
typedef struct NETWORK_HTTP_INFO {
    /*! Store content type */
    char content_type[256];
    /*! Store content length */
    size_t content_length;
    /*! Pointer to the body data */
    char* body;
    /*! Type of request */
    char* type;
} NETWORK_HTTP_INFO;

/*! host to network size_t */
size_t htonst(size_t value);
/*! network to host size_t */
size_t ntohst(size_t value);
#ifdef HAVE_OPENSSL
/*! set SSL */
int netw_set_crypto(NETWORK* netw, char* key, char* certificate);
/*! set SSL from PEM strings in memory */
int netw_set_crypto_pem(NETWORK* netw, const char* key_pem, const char* cert_pem);
/*! set SSL certificate chain from PEM file */
int netw_set_crypto_chain(NETWORK* netw, char* key, char* certificate, char* ca_file);
/*! set SSL certificate chain from PEM strings in memory */
int netw_set_crypto_chain_pem(NETWORK* netw, const char* key_pem, const char* cert_pem, const char* ca_pem);
/*! set custom CA verify path or file */
int netw_ssl_set_ca(NETWORK* netw, const char* ca_file, const char* ca_path);
/*! enable or disable peer certificate verification */
int netw_ssl_set_verify(NETWORK* netw, int enable);
/*! load client certificate and private key for mTLS */
int netw_ssl_set_client_cert(NETWORK* netw, const char* cert_file, const char* key_file);
/*! init ssl helper */
int netw_init_openssl(void);
/*! unload ssl helper */
int netw_unload_openssl(void);
/*! connect ssl helper */
int netw_ssl_connect(NETWORK** netw, char* host, char* port, int ip_version, char* ssl_key_file, char* ssl_cert_file);
/*! @brief connect as SSL client without client certificate (TCP + SSL_CTX, no handshake yet) */
int netw_ssl_connect_client(NETWORK** netw, char* host, char* port, int ip_version);
/*! @brief like netw_ssl_connect_client but bounds the TCP connect by connect_timeout_ms (0 = OS default) */
int netw_ssl_connect_client_to(NETWORK** netw, char* host, char* port, int ip_version, int connect_timeout_ms);
/*! @brief complete the SSL handshake on a NETWORK whose ctx was already created */
int netw_ssl_do_handshake(NETWORK* netw, const char* sni_hostname);
/*! SSL Writing to a socket */
ssize_t send_ssl_data(void* netw, char* buf, uint32_t n);
/*! SSL Reading from a socket */
ssize_t recv_ssl_data(void* netw, char* buf, uint32_t n);
/*! SSL single-attempt write (see send_data_once contract) */
ssize_t send_ssl_data_once(void* netw, char* buf, uint32_t n);
/*! SSL single-attempt read (see recv_data_once contract) */
ssize_t recv_ssl_data_once(void* netw, char* buf, uint32_t n);
#endif
/*! Used by Init & Close network */
int netw_init_wsa(int mode, int v1, int v2);
/*! Set flags on network */
int netw_set(NETWORK* netw, int flag);
/*! Get flags from network */
int netw_get_state(NETWORK* netw, uint32_t* state, int* thr_engine_status);
/*! Set common socket options (disable naggle, send/recv buf, reuse addr) */
int netw_setsockopt(NETWORK* netw, int optname, int value);
/*! set blocking mode */
int netw_set_blocking(NETWORK* netw, unsigned long int is_blocking);
/*! Pick send-side compression algorithm for this NETWORK:
 *  NETW_COMPRESS_NONE, _ZLIB, _LZ4. Independent of the inbound
 *  decode path, which always honours whichever flag the sender
 *  set. */
int netw_set_compression_mode(NETWORK* netw, int mode);
/*! Connecting, extended */
int netw_connect_ex(NETWORK** netw, char* host, char* port, size_t send_list_limit, size_t recv_list_limit, int ip_version, char* ssl_key_file, char* ssl_cert_file);
/*! Connecting, extended, with a bounded connection-establishment time (connect_timeout_ms, 0 = OS default) */
int netw_connect_ex_to(NETWORK** netw, char* host, char* port, size_t send_list_limit, size_t recv_list_limit, int ip_version, char* ssl_key_file, char* ssl_cert_file, int connect_timeout_ms);
/*! Connecting */
int netw_connect(NETWORK** netw, char* host, char* port, int ip_version);
/*! Connecting, with a bounded connection-establishment time (connect_timeout_ms, 0 = OS default) */
int netw_connect_to(NETWORK** netw, char* host, char* port, int ip_version, int connect_timeout_ms);
/*! wait for send buffer to be empty */
int deplete_send_buffer(int fd, int timeout);
/*! Create a UDP bound socket */
int netw_bind_udp(NETWORK** netw, char* addr, char* port, int ip_version);
/*! Connect a UDP socket to a remote host */
int netw_connect_udp(NETWORK** netw, char* host, char* port, int ip_version);
/*! UDP send data to connected peer or specified addr */
ssize_t send_udp_data(void* netw, char* buf, uint32_t n);
/*! UDP recv data from socket */
ssize_t recv_udp_data(void* netw, char* buf, uint32_t n);
/*! UDP sendto with explicit destination */
ssize_t netw_udp_sendto(NETWORK* netw, char* buf, uint32_t n, struct sockaddr* dest_addr, socklen_t dest_len);
/*! UDP recvfrom with source address capture */
ssize_t netw_udp_recvfrom(NETWORK* netw, char* buf, uint32_t n, struct sockaddr* src_addr, socklen_t* src_len);
/*! Closing */
int netw_close(NETWORK** netw);
/*! Closing for peer */
int netw_wait_close(NETWORK** netw);
/*! Closing for peer with timeout */
int netw_wait_close_timed(NETWORK** netw, size_t timeout);
/*! Stop a NETWORK connection sending and receiving thread */
int netw_stop_thr_engine(NETWORK* netw);
/*! Listening network */
int netw_make_listening(NETWORK** netw, char* addr, char* port, int nbpending, int ip_version);
/*! Accepting routine extended */
NETWORK* netw_accept_from_ex(NETWORK* from, size_t send_list_limit, size_t recv_list_limit, int blocking, int* retval);
/*! Accepting routine */
NETWORK* netw_accept_from(NETWORK* from);
/*! Accepting routine */
NETWORK* netw_accept_nonblock_from(NETWORK* from, int blocking);
/*! Add a message to send in aimed NETWORK */
int netw_add_msg(NETWORK* netw, N_STR* msg);
/*! Add a char message to send in the aimed NETWORK */
int netw_add_msg_ex(NETWORK* netw, char* str, unsigned int length);
/*! Get a message from aimed NETWORK. Instant return to NULL if no MSG */
N_STR* netw_get_msg(NETWORK* netw);
/*! Wait a message from aimed NETWORK. Recheck each 'refresh' usec until 'timeout' usec */
N_STR* netw_wait_msg(NETWORK* netw, unsigned int refresh, size_t timeout);
/*! Create the sending and receiving thread of a NETWORK */
int netw_start_thr_engine(NETWORK* netw);
/*! Thread Sending management function */
void* netw_send_func(void* NET);
/*! Thread Receiving management function */
void* netw_recv_func(void* NET);
/*! Writing to a socket */
ssize_t send_data(void* netw, char* buf, uint32_t n);
/*! Reading from a socket */
ssize_t recv_data(void* netw, char* buf, uint32_t n);
/*! Single-attempt write (see NETWORK.send_data_once contract) */
ssize_t send_data_once(void* netw, char* buf, uint32_t n);
/*! Single-attempt read (see NETWORK.recv_data_once contract) */
ssize_t recv_data_once(void* netw, char* buf, uint32_t n);
/*! sending to php */
ssize_t send_php(SOCKET s, int _code, char* buf, int n);
/*! receive from php */
ssize_t recv_php(SOCKET s, int* _code, char** buf);
/*! get queue status */
int netw_get_queue_status(NETWORK* netw, size_t* nb_to_send, size_t* nb_to_read);

/*! init pools */
NETWORK_POOL* netw_new_pool(size_t nb_min_element);
/*! destroy pool */
int netw_destroy_pool(NETWORK_POOL** netw_pool);
/*! close pool */
void netw_pool_netw_close(void* netw_ptr);
/*! add network to pool */
int netw_pool_add(NETWORK_POOL* netw_pool, NETWORK* netw);
/*! remove network from pool */
int netw_pool_remove(NETWORK_POOL* netw_pool, NETWORK* netw);
/*! broadcast message to pool */
int netw_pool_broadcast(NETWORK_POOL* netw_pool, const NETWORK* from, N_STR* net_msg);
/*! get nb clients */
size_t netw_pool_nbclients(NETWORK_POOL* netw_pool);

/*! set user id on a netw */
int netw_set_user_id(NETWORK* netw, int id);

/*! send ping message */
int netw_send_ping(NETWORK* netw, int type, int id_from, int id_to, int time);
/*! send ident message */
int netw_send_ident(NETWORK* netw, int type, int id, N_STR* name, N_STR* passwd);
/*! send position message */
int netw_send_position(NETWORK* netw, int id, double X, double Y, double vx, double vy, double acc_x, double acc_y, int time_stamp);
/*! send string to a specific target */
int netw_send_string_to(NETWORK* netw, int id_to, N_STR* name, N_STR* chan, N_STR* txt, int color);
/*! send string to all targets */
int netw_send_string_to_all(NETWORK* netw, N_STR* name, N_STR* chan, N_STR* txt, int color);
/*! send quit message */
int netw_send_quit(NETWORK* netw);

/*! calculate URL-encoded size */
size_t netw_calculate_urlencoded_size(const char* str, size_t len);
/*! extract HTTP request type */
char* netw_extract_http_request_type(const char* request);
/*! extract HTTP info from request */
NETWORK_HTTP_INFO netw_extract_http_info(char* request);
/*! destroy HTTP info structure */
int netw_info_destroy(NETWORK_HTTP_INFO http_request);
/*! URL-encode a string */
char* netw_urlencode(const char* str, size_t len);
/*! get URL from HTTP request */
int netw_get_url_from_http_request(const char* request, char* url, size_t size);
/*! URL-decode a string */
char* netw_urldecode(const char* str);
/*! parse POST data into hash table */
HASH_TABLE* netw_parse_post_data(const char* post_data);
/*! guess HTTP content type from URL */
const char* netw_guess_http_content_type(const char* url);
/*! get HTTP status message string */
const char* netw_get_http_status_message(int status_code);
/*! get current HTTP date string */
int netw_get_http_date(char* buffer, size_t buffer_size);
/*! build HTTP response */
int netw_build_http_response(N_STR** http_response, int status_code, const char* server_name, const char* content_type, char* additional_headers, N_STR* body);

/**
 * @brief Parsed proxy URL components.
 */
typedef struct N_PROXY_CFG {
    char* scheme;   /*!< "http", "https", or "socks5" */
    char* host;     /*!< proxy hostname */
    int port;       /*!< proxy port */
    char* username; /*!< NULL if no auth */
    char* password; /*!< NULL if no auth */
} N_PROXY_CFG;

/**
 * @brief Parse a proxy URL string into an N_PROXY_CFG struct.
 *
 * Accepted formats:
 *   http://host:port
 *   http://user:pass\@host:port
 *   https://host:port
 *   https://user:pass\@host:port
 *   socks5://host:port
 *   socks5://user:pass\@host:port
 *
 * @param url   Proxy URL string. Must not be NULL.
 * @return      Newly allocated N_PROXY_CFG, or NULL on parse failure.
 *              Caller must free with n_proxy_cfg_free().
 */
N_PROXY_CFG* n_proxy_cfg_parse(const char* url);

/**
 * @brief Free an N_PROXY_CFG created by n_proxy_cfg_parse().
 * @param cfg  Pointer to pointer; set to NULL on return.
 */
void n_proxy_cfg_free(N_PROXY_CFG** cfg);

/**
 * @brief Open a TCP connection through an HTTP proxy using CONNECT tunneling.
 *
 * Connects to the proxy, issues:
 *   CONNECT target_host:target_port HTTP/1.1
 *   Host: target_host:target_port
 *   Proxy-Authorization: Basic base64(user:pass)  (if credentials present)
 *
 * Verifies the proxy responds with 200 Connection established.
 * Returns the raw socket file descriptor ready for a TLS handshake
 * or plain HTTP use over the tunnel.
 *
 * @param proxy         Parsed proxy configuration. Must not be NULL.
 * @param target_host   Destination hostname.
 * @param target_port   Destination port.
 * @return              Connected socket fd on success, -1 on failure.
 */
int n_proxy_connect_tunnel(const N_PROXY_CFG* proxy,
                           const char* target_host,
                           int target_port);

#ifdef HAVE_OPENSSL
/**
 * @brief Open a TCP connection through an HTTPS proxy using CONNECT tunneling.
 *
 * Like n_proxy_connect_tunnel(), but connects to the proxy over TLS first.
 * The CONNECT request and response are exchanged over the encrypted channel.
 * After the tunnel is established, the proxy TLS session is torn down and
 * the raw socket fd is returned, ready for a second TLS handshake to the
 * target or plain HTTP use.
 *
 * @param proxy         Parsed proxy configuration (scheme must be "https").
 * @param target_host   Destination hostname.
 * @param target_port   Destination port.
 * @return              Connected socket fd on success, -1 on failure.
 */
int n_proxy_connect_tunnel_ssl(const N_PROXY_CFG* proxy,
                               const char* target_host,
                               int target_port);
#endif /* HAVE_OPENSSL */

/**
 * @brief Open a TCP connection through a SOCKS5 proxy.
 *
 * Performs the SOCKS5 handshake (no-auth or user/pass auth),
 * requests connection to target_host:target_port.
 * Returns the raw socket fd ready for use.
 *
 * @param proxy         Parsed proxy configuration. Must not be NULL.
 * @param target_host   Destination hostname.
 * @param target_port   Destination port.
 * @return              Connected socket fd on success, -1 on failure.
 */
int n_proxy_connect_socks5(const N_PROXY_CFG* proxy,
                           const char* target_host,
                           int target_port);

/*! maximum number of parsed query parameters */
#define N_URL_MAX_PARAMS 64

/*! a single query parameter key=value pair */
typedef struct N_URL_PARAM {
    char* key;   /*!< parameter name */
    char* value; /*!< parameter value */
} N_URL_PARAM;

/*! parsed URL components */
typedef struct N_URL {
    char* scheme;                         /*!< "http" or "https" */
    char* host;                           /*!< hostname */
    int port;                             /*!< port number (0 if not specified) */
    char* path;                           /*!< path starting with "/", or "/" if empty */
    char* query;                          /*!< raw query string without leading '?', or NULL */
    N_URL_PARAM params[N_URL_MAX_PARAMS]; /*!< parsed key=value pairs */
    int nb_params;                        /*!< number of parsed parameters */
} N_URL;

/*! parse a URL string into components */
N_URL* n_url_parse(const char* url);
/*! build a URL string from parsed components */
N_STR* n_url_build(const N_URL* u);
/*! percent-encode a string for use in URLs (delegates to n_str_url_encode()) */
N_STR* n_url_encode(const char* str);
/*! decode a percent-encoded string (returns N_STR) */
N_STR* n_url_decode(const char* str);
/*! free a N_URL and all its members */
void n_url_free(N_URL** u);

/*! WebSocket opcodes per RFC 6455 */
#define N_WS_OP_TEXT 0x01
#define N_WS_OP_BINARY 0x02
#define N_WS_OP_CLOSE 0x08
#define N_WS_OP_PING 0x09
#define N_WS_OP_PONG 0x0A

/*! WebSocket message */
typedef struct N_WS_MESSAGE {
    int opcode;     /*!< frame opcode */
    N_STR* payload; /*!< message payload */
    int masked;     /*!< 1 if masked */
} N_WS_MESSAGE;

/*! WebSocket connection */
typedef struct N_WS_CONN {
    NETWORK* netw; /*!< underlying network connection */
    int connected; /*!< 1 if handshake completed */
    char* host;    /*!< remote hostname */
    char* path;    /*!< resource path */
} N_WS_CONN;

#ifdef HAVE_OPENSSL
/*! @brief connect to a WebSocket server (ws:// or wss://) and perform the HTTP/1.1 upgrade handshake */
N_WS_CONN* n_ws_connect(const char* host, const char* port, const char* path, int use_ssl);

/*! @brief send a WebSocket frame (client always masks) */
int n_ws_send(N_WS_CONN* conn, const char* payload, size_t len, int opcode);

/*! @brief receive one WebSocket frame */
int n_ws_recv(N_WS_CONN* conn, N_WS_MESSAGE* msg_out);

/*! @brief send close frame and close the connection */
void n_ws_close(N_WS_CONN* conn);

/*! @brief free a WebSocket connection structure */
void n_ws_conn_free(N_WS_CONN** conn);
#endif

/*! SSE event received from server */
typedef struct N_SSE_EVENT {
    N_STR* event; /*!< event type (or NULL for default) */
    N_STR* data;  /*!< event data */
    N_STR* id;    /*!< last event ID (or NULL) */
    int retry;    /*!< retry interval in ms (0 if not set) */
} N_SSE_EVENT;

struct N_SSE_CONN;

/*! SSE connection handle */
typedef struct N_SSE_CONN {
    NETWORK* netw;                                                                  /*!< underlying network connection */
    volatile int stop_flag;                                                         /*!< atomic stop flag */
    void (*on_event)(N_SSE_EVENT* event, struct N_SSE_CONN* conn, void* user_data); /*!< callback */
    void* user_data;                                                                /*!< user data for callback */
} N_SSE_CONN;

#ifdef HAVE_OPENSSL
/*! @brief connect to an SSE endpoint and start reading events (blocks until stopped).
 *  @param user_agent optional User-Agent header value. NULL emits no
 *                    User-Agent header; some servers (e.g. Wikimedia)
 *                    reject anonymous clients with 403. */
N_SSE_CONN* n_sse_connect(const char* host, const char* port, const char* path, int use_ssl, const char* user_agent, void (*on_event)(N_SSE_EVENT*, N_SSE_CONN*, void*), void* user_data);

/*! signal the SSE connection to stop reading */
void n_sse_stop(N_SSE_CONN* conn);

/*! free an SSE connection */
void n_sse_conn_free(N_SSE_CONN** conn);

/*! free an SSE event's contents (does not free the event struct itself) */
void n_sse_event_clean(N_SSE_EVENT* event);
#endif

/*! parsed HTTP request for mock server callback */
typedef struct N_HTTP_REQUEST {
    char method[16];  /*!< HTTP method */
    char path[2048];  /*!< request path */
    char query[2048]; /*!< query string (or empty) */
    LIST* headers;    /*!< list of char* "Name: Value" strings */
    N_STR* body;      /*!< request body (or NULL) */
} N_HTTP_REQUEST;

/*! HTTP response to send from mock server callback */
typedef struct N_HTTP_RESPONSE {
    int status_code;        /*!< HTTP status code */
    char content_type[128]; /*!< Content-Type header value */
    N_STR* body;            /*!< response body */
} N_HTTP_RESPONSE;

/*! mock HTTP server handle */
typedef struct N_MOCK_SERVER {
    NETWORK* listener;                                                               /*!< listening network */
    volatile int stop_flag;                                                          /*!< atomic stop flag */
    void (*on_request)(N_HTTP_REQUEST* req, N_HTTP_RESPONSE* resp, void* user_data); /*!< request handler */
    void* user_data;                                                                 /*!< user data for handler */
    int port;                                                                        /*!< listening port */
} N_MOCK_SERVER;

/*! @brief start a mock HTTP server on localhost:port (returns immediately) */
N_MOCK_SERVER* n_mock_server_start(int port,
                                   void (*on_request)(N_HTTP_REQUEST*, N_HTTP_RESPONSE*, void*),
                                   void* user_data);

/*! @brief run the mock server accept loop (blocks until stop_flag is set) */
void n_mock_server_run(N_MOCK_SERVER* server);

/*! @brief signal the mock server to stop accepting connections */
void n_mock_server_stop(N_MOCK_SERVER* server);

/*! @brief free a mock server and close the listening socket */
void n_mock_server_free(N_MOCK_SERVER** server);

/* per-NETWORK error capture */

/*! Get number of captured errors on a NETWORK handle */
int n_netw_get_error_count(NETWORK* netw);
/*! Get captured error message by index (0 = oldest). Returns NULL if out of range */
const char* n_netw_get_error(NETWORK* netw, int index);
/*! Clear captured errors on a NETWORK handle */
void n_netw_clear_errors(NETWORK* netw);

/* pre-connection error capture (thread-local, no NETWORK yet) */

/*! Get number of pre-connection errors captured on this thread */
int n_netw_get_connect_error_count(void);
/*! Get pre-connection error message by index. Returns NULL if out of range */
const char* n_netw_get_connect_error(int index);
/*! Clear pre-connection errors on this thread */
void n_netw_clear_connect_errors(void);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /*#ifndef N_NETWORK*/
