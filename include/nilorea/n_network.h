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
/*! code for a socekt error */
#define NETW_SOCKET_ERROR -1
/*! Code for a disconnected recv */
#define NETW_SOCKET_DISCONNECTED -2
/*! Send or recv max number of retries */
#define NETW_MAX_RETRIES 8
/*! Send or recv delay between retries in usec */
#define NETW_RETRY_DELAY 1000

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
/*! socket type for windows */
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

#include "n_time.h"

#elif defined __windows__

#define SHUT_WR SD_SEND
#define SHUT_RD SD_RECEIVE
#define SHUT_RDWR SD_BOTH

#ifndef ECONNRESET
#define ECONNRESET 104
#endif

#if (_WIN32_WINNT < 0x0501)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
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
    _(NETW_DESTROY_SENDBUF, 32768)

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
        wait_close_timeout;

    /*! state of the connection , NETW_RUN, NETW_QUIT, NETW_STOP , NETW_ERR */
    uint32_t state;

    /*! send func ptr */
    netw_func send_data,
        /*! receive func ptr */
        recv_data;

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

} NETWORK;

/*! structure of a network pool */
typedef struct NETWORK_POOL {
    /*! table of clients */
    HASH_TABLE* pool;

    /*! thread safety */
    pthread_rwlock_t rwlock;

} NETWORK_POOL;

/*! structure for splitting HTTP requests */
typedef struct NETWORK_HTTP_INFO {
    // Store content type
    char content_type[256];
    // Store content length
    size_t content_length;
    // Pointer to the body data
    char* body;
    // Type of request
    char* type;
} NETWORK_HTTP_INFO;

/* host to network size_t */
size_t htonst(size_t value);
/* network to host size_t */
size_t ntohst(size_t value);
#ifdef HAVE_OPENSSL
/* set SSL */
int netw_set_crypto(NETWORK* netw, char* key, char* certificate);
/* init ssl helper */
int netw_init_openssl(void);
/* unload ssl helper */
int netw_unload_openssl(void);
/* connect ssl helper */
int netw_ssl_connect(NETWORK** netw, char* host, char* port, int ip_version, char* ssl_key_file, char* ssl_cert_file);
/* SSL Writting to a socket */
ssize_t send_ssl_data(void* netw, char* buf, uint32_t n);
/* SSL Reading from a socket */
ssize_t recv_ssl_data(void* netw, char* buf, uint32_t n);
#endif
/* Used by Init & Close network */
int netw_init_wsa(int mode, int v1, int v2);
/* Set flags on network */
int netw_set(NETWORK* netw, int flag);
/* Get flags from network */
int netw_get_state(NETWORK* netw, uint32_t* state, int* thr_engine_status);
/* Set common socket options (disable naggle, send/recv buf, reuse addr) */
int netw_setsockopt(NETWORK* netw, int optname, int value);
/* set blocking mode */
int netw_set_blocking(NETWORK* netw, unsigned long int is_blocking);
/* Connecting, extended */
int netw_connect_ex(NETWORK** netw, char* host, char* port, size_t send_list_limit, size_t recv_list_limit, int ip_version, char* ssl_key_file, char* ssl_cert_file);
/* Connecting */
int netw_connect(NETWORK** netw, char* host, char* port, int ip_version);
/* wait for send buffer to be empty */
int deplete_send_buffer(int fd, int timeout);
/* Closing */
int netw_close(NETWORK** netw);
/* Closing for peer */
int netw_wait_close(NETWORK** netw);
/* Closing for peer timeouted*/
int netw_wait_close_timed(NETWORK** netw, size_t timeout);
/* Stop a NETWORK connection sending and receing thread */
int netw_stop_thr_engine(NETWORK* netw);
/* Listening network */
int netw_make_listening(NETWORK** netw, char* addr, char* port, int nbpending, int ip_version);
/* Accepting routine extended */
NETWORK* netw_accept_from_ex(NETWORK* from, size_t send_list_limit, size_t recv_list_limit, int blocking, int* retval);
/* Accepting routine */
NETWORK* netw_accept_from(NETWORK* from);
/* Accepting routine */
NETWORK* netw_accept_nonblock_from(NETWORK* from, int blocking);
/* Add a message to send in aimed NETWORK */
int netw_add_msg(NETWORK* netw, N_STR* msg);
/* Add a char message to send in the aimed NETWORK */
int netw_add_msg_ex(NETWORK* netw, char* str, unsigned int length);
/* Get a message from aimed NETWORK. Instant return to NULL if no MSG */
N_STR* netw_get_msg(NETWORK* netw);
/* Wait a message from aimed NETWORK. Recheck each 'refresh' usec until 'timeout' usec */
N_STR* netw_wait_msg(NETWORK* netw, unsigned int refresh, size_t timeout);
/* Create the sending and receiving thread of a NETWORK */
int netw_start_thr_engine(NETWORK* netw);
/* Thread Sending management function */
void* netw_send_func(void* NET);
/* Thread Receiving management function */
void* netw_recv_func(void* NET);
/* Writting to a socket */
ssize_t send_data(void* netw, char* buf, uint32_t n);
/* Reading from a socket */
ssize_t recv_data(void* netw, char* buf, uint32_t n);
/* sending to php */
ssize_t send_php(SOCKET s, int _code, char* buf, int n);
/* receive from php */
ssize_t recv_php(SOCKET s, int* _code, char** buf);
/* get queue status */
int netw_get_queue_status(NETWORK* netw, size_t* nb_to_send, size_t* nb_to_read);

/* init pools */
NETWORK_POOL* netw_new_pool(size_t nb_min_element);
/* destroy pool */
int netw_destroy_pool(NETWORK_POOL** netw_pool);
/* close pool */
void netw_pool_netw_close(void* netw_ptr);
/* add network to pool */
int netw_pool_add(NETWORK_POOL* netw_pool, NETWORK* netw);
/* add network to pool */
int netw_pool_remove(NETWORK_POOL* netw_pool, NETWORK* netw);
/* add message to pool */
int netw_pool_broadcast(NETWORK_POOL* netw_pool, NETWORK* from, N_STR* net_msg);
/* get nb clients */
size_t netw_pool_nbclients(NETWORK_POOL* netw_pool);

/* set user id on a netw */
int netw_set_user_id(NETWORK* netw, int id);

/* homemade tcp ip protocol helpers */
int netw_send_ping(NETWORK* netw, int type, int id_from, int id_to, int time);
int netw_send_ident(NETWORK* netw, int type, int id, N_STR* name, N_STR* passwd);
int netw_send_position(NETWORK* netw, int id, double X, double Y, double vx, double vy, double acc_x, double acc_y, int time_stamp);
int netw_send_string_to(NETWORK* netw, int id_to, N_STR* name, N_STR* chan, N_STR* txt, int color);
int netw_send_string_to_all(NETWORK* netw, N_STR* name, N_STR* chan, N_STR* txt, int color);
int netw_send_quit(NETWORK* netw);

/* http helpers */
size_t netw_calculate_urlencoded_size(const char* str, size_t len);
char* netw_extract_http_request_type(const char* request);
NETWORK_HTTP_INFO netw_extract_http_info(char* request);
int netw_info_destroy(NETWORK_HTTP_INFO http_request);
char* netw_urlencode(const char* str, size_t len);
int netw_get_url_from_http_request(const char* request, char* url, size_t size);
char* netw_urldecode(const char* str);
HASH_TABLE* netw_parse_post_data(const char* post_data);
const char* netw_guess_http_content_type(const char* url);
const char* netw_get_http_status_message(int status_code);
int netw_get_http_date(char* buffer, size_t buffer_size);
int netw_build_http_response(N_STR** http_response, int status_code, const char* server_name, const char* content_type, char* additional_headers, N_STR* body);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /*#ifndef N_NETWORK*/
