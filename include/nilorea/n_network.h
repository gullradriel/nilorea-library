/**\file n_network.h
 *  Network Engine
 *\author Castagnier Mickael
 *\version 2.0
 *\date 11/03/2015
 */
#ifndef N_NETWORK
#define N_NETWORK

#ifdef __cplusplus
extern "C"
{
#endif

/**\defgroup NETWORKING NETWORK: connect, accept, send and recv wrappers. Network Queue, thread-safe add/get message
   \addtogroup NETWORKING
  @{
*/

#include "n_common.h"
#include "n_str.h"
#include "n_list.h"
#include "n_log.h"

#define NETWORK_IPALL 0
#define NETWORK_IPV4  1
#define NETWORK_IPV6  2

#if defined( __linux__ ) || defined( __sun ) || defined( _AIX )
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#define _OPEN_SYS_SOCK_IPV6 1
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/sockios.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef SOCKET
/*! default socket declaration */
typedef int SOCKET ;
#endif

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
#define AI_PASSIVE                  0x00000001
#endif
#ifndef AI_CANONNAME
#define AI_CANONNAME                0x00000002
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST              0x00000004
#endif
#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV              0x00000008
#endif
#ifndef AI_ALL
#define AI_ALL                      0x00000100
#endif
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG               0x00000400
#endif
#ifndef AI_V4MAPPED
#define AI_V4MAPPED                 0x00000800
#endif
#ifndef AI_NON_AUTHORITATIVE
#define AI_NON_AUTHORITATIVE        0x00004000
#endif
#ifndef AI_SECURE
#define AI_SECURE                   0x00008000
#endif
#ifndef AI_RETURN_PREFERRED_NAMES
#define AI_RETURN_PREFERRED_NAMES   0x00010000
#endif

#include <pthread.h>

/*! Netflag for sigpipe */
#define NETFLAGS 0           /* no flags needed for microsoft */

#endif

/*! NETWORK -> mode value for a CLIENT (meaning connecting) */
#define NETW_CLIENT 2
/*! NETWORK -> mode value for a SERVER (meaning listening) */
#define NETW_SERVER 4
/*! Flag: start or rester intertal network timing */
#define NETW_RESTART_TIMER    8
/*! Flag: empty send buffer */
#define NETW_EMPTY_SENDBUF    16
/*! Flag: empty recv buffer */
#define NETW_EMPTY_RECVBUF    32
/*! State for a running NETWORK */
#define NETW_RUN     64
/*! State for a NETWORK who want to pause processing of network queues */
#define NETW_PAUSE   128
/*! State for a NETWORK who want to end/exit connection !!THESE HAVE TO BE NEGATIVE AS THEY ARE USED To DETECT END OF CONNECTION IN NBBYTE SEND/RECV */
#define NETW_EXIT_ASKED    256
/*! State for a NETWORK that was first asked to exit, then is exited !!THESE HAVE TO BE NEGATIVE AS THEY ARE USED To DETECT END OF CONNECTION IN NBBYTE SEND/RECV */
#define NETW_EXITED    512
/*! State to signal errors in the network !!THESE HAVE TO BE NEGATIVE AS THEY ARE USED To DETECT END OF CONNECTION IN NBBYTE SEND/RECV */
#define NETW_ERROR   1024
/*! State for a started threaded network engine */
#define NETW_THR_ENGINE_STARTED 2048
/*! State for a stopped threaded network engine */
#define NETW_THR_ENGINE_STOPPED 4096
/*! Flag: empty send buffer */
#define NETW_DESTROY_SENDBUF 8192
/*! Flag: empty recv buffer */
#define NETW_DESTROY_RECVBUF  16384
/*! PHP send and receive header size */
#define HEAD_SIZE 10
/*! PHP send and receive header size */
#define HEAD_CODE 3


/*! Structure of a N_SOCKET */
typedef struct N_SOCKET
{
    /*!port of socket*/
    char *port;
    /*!a normal socket*/
    SOCKET sock;
    /*!ip of the connected socket*/
    char *ip;

    /*!address of local machine*/
    struct addrinfo hints ;
    /*! getaddrinfo results */
    struct addrinfo *rhost ;
    /*! connected remote addr */
    struct sockaddr_storage raddr ;
} N_SOCKET;

/*! Structure of a NETWORK */
typedef struct NETWORK
{
    /*! Nb pending connection,if listening */
    int nb_pending,
        /*! NETWORK mode , 1 listening, 0 connecting */
        mode,
        /*! state of the connection , NETW_RUN, NETW_PAUSE, NETW_QUIT, NETW_STOP , NETW_ERR */
        state,
        /*! Threaded network engine state for this network. NETW_THR_ENGINE_STARTED or NETW_THR_ENGINE_STOPPED */
        threaded_engine_status,
        /*! Internal flag to know if we have to free addr infos */
        addr_infos_loaded,
        /*! send queue pool interval, used when there is no item in queue, in usec */
        send_queue_wait,
        /*! send queue consecutive pool interval, used when there are still items to send, in usec */
        send_queue_consecutive_wait,
        /*! interval between state checks in pause mode */
        pause_wait,
        /*! state of naggle algorythm, 0 untouched, 1 forcibly disabled */
        tcpnodelay,
        /*! size of the socket send buffer, 0 untouched, else size in bytes */
        so_sndbuf,
        /*! size of the socket recv buffer, 0 untouched, else size in bytes */
        so_rcvbuf ;


    /*!networking socket*/
    N_SOCKET link;

    /*!sending buffer (for outgoing queuing )*/
    LIST *send_buf;
    /*!reveicing buffer (for incomming usage)*/
    LIST *recv_buf;

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

} NETWORK;

/* Used by Init & Close network */
int handle_wsa( int mode, int v1, int v2 );
/* Set flags on network */
int netw_set( NETWORK *netw, int flag );
/* Set threaded network timers */
int netw_set_timers( NETWORK *netw, int send_queue_wait, int send_queue_consecutive_wait, int pause_wait );
/* Get flags from network */
int netw_get_state( NETWORK *netw, int *state, int *thr_engine_status );
/* Set common socket options (disable naggle, send/recv buf, reuse addr) */
int netw_setsockopt( SOCKET sock, int disable_naggle, int sock_send_buf, int sock_recv_buf );
/* new tcp socket with error checking */
int netw_tcp_socket( SOCKET *sock );
/* Connecting, extended */
int netw_connect_ex( NETWORK **netw, char *host, char *port, int disable_naggle, int sock_send_buf, int sock_recv_buf, int send_list_limit, int recv_list_limit, int ip_version );
/* Connecting */
int netw_connect( NETWORK **netw, char *host, char *port, int ip_version );
/* Closing */
int netw_close( NETWORK **netw );
int netw_wait_close( NETWORK **netw );
/* Stop a NETWORK connection sending and receing thread */
int netw_stop_thr_engine( NETWORK *netw );
/* Listening network */
int netw_make_listening( NETWORK **netw, char *addr, char *port, int nbpending, int ip_version );
/* Accepting routine extended */
NETWORK *netw_accept_from_ex( NETWORK *from,int disable_naggle, int sock_send_buf, int sock_recv_buf, int send_list_limit, int recv_list_limit, int non_blocking, int *retval  );
/* Accepting routine */
NETWORK *netw_accept_from( NETWORK *from );
/* Accepting routine */
NETWORK *netw_accept_nonblock_from( NETWORK *from, int blocking );
/* Add a message to send in aimed NETWORK */
int netw_add_msg( NETWORK *netw, N_STR *msg );
/* Get a message from aimed NETWORK. Instant return to NULL if no MSG */
N_STR *netw_get_msg( NETWORK *netw );
/* Wait a message from aimed NETWORK. Recheck each 'refresh' usec until 'timeout' usec */
N_STR *netw_wait_msg( NETWORK *netw, long refresh, long timeout );
/* Create the sending and receiving thread of a NETWORK */
int netw_start_thr_engine( NETWORK *netw );
/* Thread Sending management function */
void *netw_send_func( void *NET );
/* Thread Receiving management function */
void *netw_recv_func( void *NET );
/* Writting to a socket */
int send_data( SOCKET s, char *buf, NSTRBYTE n );
/* Reading from a socket */
int recv_data( SOCKET s, char *buf, NSTRBYTE n );
/* sending to php */
int send_php( SOCKET s, int _code, char *buf, int n );
/* receive from php */
int recv_php( SOCKET s, int *_code, char **buf );
/* get queue status */
int netw_get_queue_status( NETWORK *netw, int *nb_to_send, int *nb_to_read );

/*@}*/

#ifdef __cplusplus
}
#endif

#endif /*#ifndef N_NETWORK*/

