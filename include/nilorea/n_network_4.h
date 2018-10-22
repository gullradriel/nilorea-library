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

	/**\defgroup NETWORKING_V4 NETWORK_IPV4: connect, listen, accept
	  \addtogroup NETWORKING_V4 
	  @{
	  */

#include "n_common.h"
#include "n_str.h"

#if defined( LINUX ) || defined( SOLARIS ) || defined( AIX )

#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef SOCKET
	/*! default socket declaration */
	typedef int SOCKET ;
#endif

	/*! socket wrapper */
#define closesocket close
	/*! Netflag for sigpipe */
#ifndef SOLARIS
#define NETFLAGS MSG_NOSIGNAL /* for program to not quit under linux when a connection is crashing 2 times */
#else
#define NETFLAGS 0
#endif
	/*! missing flag */
#define INVALID_SOCKET -1

#elif defined WIN32   /* Else it should be WIN32 */

#include <winsock.h>

	/*! Netflag for sigpipe */
#define NETFLAGS 0           /* no flags needed for microsoft */

#else
#error NO VALID OS DEFINED. PLEASE DEFINE ONE OF THE FOLLOWING: LINUX,WIN,AIX,SOLARIS
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
#define NETW_QUIT NETW_EXIT_ASKED
	/*! State for a NETWORK that was first asked to exit, then is exited !!THESE HAVE TO BE NEGATIVE AS THEY ARE USED To DETECT END OF CONNECTION IN NBBYTE SEND/RECV */
#define NETW_EXITED    512
#define NETW_STOP NETW_EXITED 
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
		char * port;
		/*!a normal socket*/
		SOCKET sock;
		/*!ip of the connected socket*/
		char *ip;

		/*!address of local machine*/
		struct sockaddr_in haddr ;
		/*! getaddrinfo results */
		struct addrinfo *rhost ;
		/*!address of remote machine*/
		struct sockaddr_in raddr ;

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
			threaded_engine_status ,
			/*! Internal flag to know if we have to free addr infos */
			addr_infos_loaded ,
			/*! send queue pool interval, used when there is no item in queue, in usec */
			send_queue_wait , 
			/*! send queue consecutive pool interval, used when there are still items to send, in usec */
			send_queue_consecutive_wait,
			/*! interval between state checks in pause mode */
			pause_wait ,
			/*! state of naggle algorythm, 0 untouched, 1 forcibly disabled */
			tcpnodelay ,
			/*! size of the socket send buffer, 0 untouched, else size in bytes */
			so_sndbuf ,
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

	/* new tcp socket */
	int netw_tcp_socket( SOCKET *sock );
	/* Connecting, extended */
	int netw_connect_ex( NETWORK **netw , char *host , char *port, int disable_naggle , int sock_send_buf , int sock_recv_buf , int send_list_limit , int recv_list_limit );
	/* Listening network */
	int netw_make_listening( NETWORK **netw , char *PORT , int nbpending );
	/* Accepting routine extended */
	NETWORK *netw_accept_from_ex( NETWORK *from , int disable_naggle , int sock_send_buf , int sock_recv_buf , int send_list_limit , int recv_list_limit , int non_blocking , int *retval );

	/*@}*/   

#ifdef __cplusplus
}
#endif

#endif /*#ifndef N_NETWORK*/

