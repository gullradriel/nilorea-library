/**\file n_network.h
 *  Network Engine 
 *\author Castagnier Mickael
 *\version 2.0
 *\date 11/03/2015
 */
#ifndef N_NETWORK_WRAPPER
#define N_NETWORK_WRAPPER

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef NETWORK_IPV4
#include "n_network_4.h"
#elif defined NETWORK_IPV6
#include "n_network_6.h"
#else
#warning You must have NETWORK_IPV4 or NETWORK_IPV4 defined as a compilation flag ! NETWORK_IPV4 is set as default !
#define NETWORK_IPV4
#include "n_network_4.h"
#endif

	/**\defgroup NETWORKING NETWORK: wrapper around ipv4/ipv6, queues, network threaded engine
	  \addtogroup NETWORKING 
	  @{
	  */

	/* get sockaddr, IPv4 or IPv6 */
	void *get_in_addr(struct sockaddr *sa);
	/* Used by Init & Close network */
	int handle_wsa( int mode , int v1 , int v2 );
	/* Connecting */
	int netw_connect( NETWORK **netw , char *host , char *port );
	/* Closing */
	int netw_close( NETWORK **netw );
	/* Used by Init & Close network */
	int handle_wsa( int mode , int v1 , int v2 );
	/* Set flags on network */
	int netw_set( NETWORK *netw , int flag );
	/* Set threaded network timers */
	int netw_set_timers( NETWORK *netw , int send_queue_wait , int send_queue_consecutive_wait , int pause_wait );
	/* Get flags from network */
	int netw_get_state( NETWORK *netw , int *state , int *thr_engine_status );
	/* Restart or reset the specified network ability */
	int netw_set( NETWORK *netw , int flag );
	/* Get state of network */
	int netw_get_state( NETWORK *netw , int *state , int *thr_engine_status );
	/* Set common socket options (disable naggle, send/recv buf, reuse addr) */
	int netw_setsockopt( SOCKET sock , int disable_naggle , int sock_send_buf , int sock_recv_buf );
	/* Accepting routine */
	NETWORK *netw_accept_from( NETWORK *from );
	/* Accepting routine */
	NETWORK *netw_accept_nonblock_from( NETWORK *from , int blocking );
	/* Add a message to send in aimed NETWORK */
	int netw_add_msg( NETWORK *netw , N_STR *msg );
	/* Get a message from aimed NETWORK. Instant return to NULL if no MSG */
	N_STR *netw_get_msg( NETWORK *netw );
	/* Wait a message from aimed NETWORK. Recheck each 'refresh' usec until 'timeout' usec */
	N_STR *netw_wait_msg( NETWORK *netw , long refresh , long timeout );
	/* Create the sending and receiving thread of a NETWORK */
	int netw_start_thr_engine( NETWORK *netw );
	/* Stop a NETWORK connection sending and receing thread */
	int netw_stop_thr_engine( NETWORK *netw );
	/* Thread Sending management function */
	void *netw_send_func( void *NET );
	/* Thread Receiving management function */
	void *netw_recv_func( void *NET );
	/* Writting to a socket */
	int send_data( SOCKET s, char *buf, NSTRBYTE n );
	/* Reading from a socket */
	int recv_data( SOCKET s, char *buf, NSTRBYTE n );
	/* sending to php */
	int send_php( SOCKET s, int _code , char *buf, int n );
	/* receive from php */
	int recv_php( SOCKET s, int *_code , char **buf );
	/* get queue status */
	int netw_get_queue_status( NETWORK *netw , int *nb_to_send , int *nb_to_read );
	
	/*@}*/
#ifdef __cplusplus
}
#endif

#endif /* n_network.h */
