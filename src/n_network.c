/**\file n_network.c
 *  Network Engine
 *\author Castagnier Mickael
 *\version 1.0
 *\date 10/05/2005
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include "nilorea/n_network.h"


/*!\fn NETWORK *netw_new( int send_list_limit , int recv_list_limit )
 *\brief Return an empty allocated network ready to be netw_closed
 *\param send_list_limit Thread engine number of tosend message limit
 *\param recv_list_limit Thread engine number of received message limit
 *\return NULL or a new empty network
 */
NETWORK *netw_new( int send_list_limit , int recv_list_limit )
{
	NETWORK *netw = NULL ;

	Malloc( netw, NETWORK , 1 );
	__n_assert( netw, return NULL );

	/* netw itself */
	netw -> nb_pending = -1 ;
	netw -> mode = -1 ;
	netw -> state = NETW_EXITED ;
	netw -> threaded_engine_status = NETW_THR_ENGINE_STOPPED ;

	/* netw -> link */
	netw -> link . sock = -1 ;
	netw -> link . port =
		netw -> link . ip = NULL ;
	memset( &netw -> link . hints , 0 , sizeof( struct addrinfo ) );
	memset( &netw -> link . raddr , 0 , sizeof( struct sockaddr_storage ) );

	/*initiliaze mutexs*/
	if ( pthread_mutex_init( &netw -> sendbolt , NULL ) != 0 )
	{
		n_log( LOG_ERR , "Error initializing netw -> sendbolt" ); 
		Free( netw );
		return NULL ;
	}
	/*initiliaze mutexs*/
	if ( pthread_mutex_init( &netw -> recvbolt , NULL ) != 0 )
	{
		n_log( LOG_ERR , "Error initializing netw -> recvbolt" ); 
		pthread_mutex_destroy( &netw -> sendbolt );
		Free( netw );
		return NULL ;
	}
	/*initiliaze mutexs*/
	if ( pthread_mutex_init( &netw -> eventbolt , NULL ) != 0 )
	{
		n_log( LOG_ERR , "Error initializing netw -> eventbolt" ); 
		pthread_mutex_destroy( &netw -> sendbolt );
		pthread_mutex_destroy( &netw -> recvbolt );
		Free( netw );
		return NULL ;
	}
	/*initialize queues */
	netw -> recv_buf = new_generic_list( recv_list_limit );
	if( ! netw -> recv_buf )
	{
		n_log( LOG_ERR , "Error when creating receive list with %d item limit" , recv_list_limit );
		netw_close( &netw );
		return NULL ;
	}
	netw -> send_buf = new_generic_list( send_list_limit );
	if( !netw -> send_buf )
	{
		n_log( LOG_ERR , "Error when creating send list with %d item limit" , send_list_limit );
		netw_close( &netw );
		return NULL ;
	}
	netw -> addr_infos_loaded = 0 ;
	netw -> send_queue_wait = 25000 ;
	netw -> send_queue_consecutive_wait = 1000 ;
	netw -> pause_wait = 200000 ;
	netw -> so_sndbuf = 0 ;
	netw -> so_rcvbuf = 0 ;
	netw -> tcpnodelay = 0 ;

	return netw ;

} /* netw_new() */


/*!\fn int netw_set_timers( NETWORK *netw , int send_queue_wait , int send_queue_consecutive_wait , int pause_wait )
 * \brief set the timers used when activating threaded network
 * \param netw Network to tune
 * \param send_queue_wait Time between each send_queue check if last call wasn't sending anything
 * \param send_queue_consecutive_wait Time between each send_queue check if last call was a sending one
 * \param pause_wait Time between each state check when network is paused
 * \return TRUE or FALSE
 */
int netw_set_timers( NETWORK *netw , int send_queue_wait , int send_queue_consecutive_wait , int pause_wait )
{
	__n_assert( netw , return FALSE );
	pthread_mutex_lock( &netw -> eventbolt );
	netw -> send_queue_wait = send_queue_wait ;
	netw -> send_queue_consecutive_wait = send_queue_consecutive_wait ;
	netw -> pause_wait = pause_wait ;
	pthread_mutex_unlock( &netw -> eventbolt );
	return TRUE ;
}



/*!\fn void *get_in_addr(struct sockaddr *sa)
 *\brief get sockaddr, IPv4 or IPv6 
 *\param sa socket_address
 */
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}



/*!\fn int handle_wsa( int mode , int v1 , int v2 )
 *\brief Do not directly use, internal api.
 *\param mode 1 for opening 0 for close 2 for status
 *\param v1 First digit of version requested
 *\param v2 Second digit of version requested
 *\return TRUE on success FALSE on error
 */
int handle_wsa( int mode , int v1 , int v2 )
{
	int compiler_warning_suppressor = 0 ;
#if !defined( LINUX ) && !defined( SOLARIS ) && !defined( AIX )
	static WSADATA WSAdata; /*WSA world*/
	static int WSA_IS_INITIALIZED = 0; /*status checking*/

	switch ( mode )
	{
		/*returning WSA status*/
		case 2 :
			return WSA_IS_INITIALIZED;
			break;
			/*loading WSA dll*/
		case 1 :
			if ( WSA_IS_INITIALIZED == 1 )
				return TRUE; /*already loaded*/
			if ( ( WSAStartup( MAKEWORD( v1 , v2 ), &WSAdata ) ) != 0 )
			{
				WSA_IS_INITIALIZED = 0;
				return FALSE;
			}
			else
			{
				WSA_IS_INITIALIZED = 1 ;
				return TRUE;
			}
			break;
			/*unloading (closing) WSA */
		case 0 :
			if ( WSA_IS_INITIALIZED == 0 )
				return TRUE; /*already CLEANED or not loaded */
			if ( WSACleanup() == 0 )
			{
				WSA_IS_INITIALIZED = 0;
				return TRUE;
			}
			break;
	} /*switch(...)*/
#endif /*#ifndef LINUX SOLARIS AIX */
	compiler_warning_suppressor = mode + v1 + v2 ;
	compiler_warning_suppressor = TRUE ;
	return compiler_warning_suppressor ;
} /*handle_wsa(...)*/



/*!\fn int netw_setsockopt( SOCKET sock , int disable_naggle , int sock_send_buf , int sock_recv_buf )
 *\brief Modify common socket options. Enable SO_REUSEADDR.  
 *\param sock The socket to configure
 *\param disable_naggle Set to positive to enable. Set to negative to force disable, 0 untouched
 *\param sock_send_buf If >0, then try to set the socket send buffer size in octet
 *\param sock_recv_buf If >0, then try to set the socket recv buffer size in octet
 *\return TRUE or FALSE
 */
int netw_setsockopt( SOCKET sock , int disable_naggle , int sock_send_buf , int sock_recv_buf )
{
	/* disable naggle algorithm */
	if( disable_naggle > 0 )
	{
		if ( setsockopt( sock , IPPROTO_TCP, TCP_NODELAY, ( const char * ) &disable_naggle, sizeof( disable_naggle ) ) == -1 )
		{
			n_log( LOG_ERR , "Error from setsockopt(TCP_NODELAY). errno: %s" , strerror( errno ) );
			return FALSE ;
		}
	}
	if( disable_naggle < 0 )
	{
		if ( setsockopt( sock , IPPROTO_TCP, TCP_NODELAY, ( const char * ) &disable_naggle, sizeof( disable_naggle ) ) == -1 )
		{
			n_log( LOG_ERR , "Error from setsockopt(TCP_NODELAY). errno: %s" , strerror( errno ) );
			return FALSE ;
		}
	}

	/* socket sending buffer size */
	if( sock_send_buf > 0 )
	{
		if ( setsockopt ( sock , SOL_SOCKET, SO_SNDBUF, ( const char * ) &sock_send_buf, sizeof( sock_send_buf ) ) == -1 )
		{
			n_log( LOG_ERR , "Error from setsockopt(SO_SNDBUF). errno: %s" , strerror( errno ) );
			return FALSE ;
		}
	}

	/* socket receiving buffer */
	if( sock_recv_buf > 0 )
	{
		if ( setsockopt ( sock , SOL_SOCKET, SO_RCVBUF, ( const char * ) &sock_recv_buf, sizeof( sock_recv_buf ) ) == -1 )
		{
			n_log( LOG_ERR , "Error from setsockopt(SO_RCVBUF). errno: %s" , strerror( errno ) );
			return FALSE ;
		}
	}

	int tmp = 1 ;
	/* lose the pesky "Address already in use" error message*/
	if ( setsockopt( sock , SOL_SOCKET, (SO_REUSEPORT | SO_REUSEADDR) , (char *)&tmp , sizeof( tmp ) ) == -1 )
	{
		int error=errno ;
		n_log( LOG_ERR , "Error from setsockopt(SO_REUSEADDR). errno: %s" , strerror( error ) );
		return FALSE ;
	}

	/* Do not close until all data is sent/reiceived, for 3 seconds */
	struct linger linger = { 0 , 0 };
	linger.l_onoff = 1 ;
	linger.l_linger = 3 ;
	if ( setsockopt( sock , SOL_SOCKET, SO_LINGER , (const char *)&linger , sizeof( linger ) ) == -1 )
	{
		n_log( LOG_ERR , "Error from setsockopt(SO_LINGER). errno: %s" , strerror( errno ) );
		return FALSE ;
	}

	return TRUE ;
} /* netw_setsockopt( ... ) */



/*!\fn int netw_connect_ex(NETWORK **netw , char *host , char *port, int disable_naggle , int sock_send_buf , int sock_recv_buf , int send_list_limit , int recv_list_limit )
 *\brief Use this to connect a NETWORK to any listening one
 *\param netw a NETWORK *object
 *\param host Host or IP to connect to
 *\param port Port to use to connect
 *\param disable_naggle Disable Naggle Algorithm. Set to 1 do activate, 0 or negative to leave defaults
 *\param sock_send_buf NETW_SOCKET_SEND_BUF socket parameter , 0 or negative to leave defaults
 *\param sock_recv_buf NETW_SOCKET_RECV_BUF socket parameter , 0 or negative to leave defaults
 *\param send_list_limit Internal sending list maximum number of item. 0 or negative for unrestricted
 *\param recv_list_limit Internal receiving list maximum number of item. 0 or negative for unrestricted
 *\return TRUE or FALSE
 */
int netw_connect_ex( NETWORK **netw , char *host , char *port, int disable_naggle , int sock_send_buf , int sock_recv_buf , int send_list_limit , int recv_list_limit  )
{
	int error = 0 ,net_status = 0;

	/*do not work over an already used netw*/
	if( (*netw) )
	{
		n_log( LOG_ERR , "Unable to allocate (*netw), already existing. You must use empty NETWORK *structs." );
		return FALSE ;
	}

	/*creating array*/
	(*netw) = netw_new( send_list_limit , recv_list_limit );
	__n_assert( (*netw) , return FALSE );

	/*checking WSA when under windows*/
	if ( handle_wsa( 1 , 2 , 2 ) == FALSE )
	{
		n_log( LOG_ERR , "Unable to load WSA dll's" );
		Free( (*netw) );
		return FALSE ;
	}

	/* Obtain address(es) matching host/port */
	(*netw) -> link . hints . ai_family   = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
	(*netw) -> link . hints . ai_socktype = SOCK_STREAM;   /* Datagram socket */
	(*netw) -> link . hints . ai_flags    = AI_PASSIVE;    /* For wildcard IP address */

	/* Note: on some system, i.e Solaris, it WILL show leak in getaddrinfo.
	 * Testing it inside a 1,100 loop showed not effect on the amount of leaked
	 * memory */
	error = getaddrinfo( host , port , &(*netw) -> link . hints , &(*netw) -> link . rhost );
	if( error != 0 )
	{
		n_log( LOG_ERR , "Error when resolving %s:%s getaddrinfo: %s", host , port , gai_strerror( error ) );
		netw_close( &(*netw) );
		return FALSE ;
	}
	(*netw) -> addr_infos_loaded = 1 ;

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect(2).
	   If socket(2) (or connect(2)) fails, we (close the socket
	   and) try the next address. */
	struct addrinfo *rp = NULL ;
	for( rp = (*netw) -> link . rhost ; rp != NULL ; rp = rp -> ai_next )
	{
		int sock = socket( rp -> ai_family , rp -> ai_socktype , rp->ai_protocol );
		error = errno ;
		if( sock == -1 )
		{  
			n_log( LOG_ERR , "Error while trying to make a socket: %s"  ,strerror( error ) );
			continue ;
		}
		if( netw_setsockopt( sock , disable_naggle , sock_send_buf , sock_recv_buf ) != TRUE )
		{
			n_log( LOG_ERR , "Some socket options could not be set on %d" , (*netw) -> link . sock );   
		}
		net_status = connect( sock , rp -> ai_addr , rp -> ai_addrlen );
		error = errno ;
		if( net_status == -1 )
		{
			n_log( LOG_ERR , "Error from connect(). host: %s:%s errno: %s" , host , port , strerror( error ) );
			closesocket( sock );
		}
		else
		{
			(*netw) -> link . sock = sock ;
			break;                  /* Success */
		}
	}
	if( rp == NULL )
	{
		/* No address succeeded */
		n_log( LOG_ERR , "Couldn't connect to %s:%s", host , port );
		netw_close( &(*netw) );
		return FALSE ;
	}

	/*storing connected port and ip adress*/
	Malloc( (*netw) -> link . ip , char , INET6_ADDRSTRLEN + 2 );
	__n_assert( (*netw) -> link . ip , netw_close( &(*netw ) ); return FALSE );
	if( !inet_ntop( rp->ai_family , get_in_addr( (struct sockaddr *)rp -> ai_addr ) , (*netw) -> link . ip , INET6_ADDRSTRLEN ) )
	{
		n_log( LOG_ERR , "inet_ntop: %p , %s" , rp , strerror( errno ) );  
	}


	(*netw)->link . port = strdup( port );
	__n_assert( (*netw) -> link . port , netw_close( &(*netw ) ); return FALSE );

	n_log( LOG_DEBUG , "Connected to %s:%s" , (*netw) -> link . ip , (*netw) -> link . port );

	netw_set( (*netw) , NETW_CLIENT|NETW_RUN|NETW_THR_ENGINE_STOPPED );

	return TRUE ;
} /* netw_connect_ex(...)*/



/*!\fn int netw_connect( NETWORK **netw , char *host , char *port )
 *\brief Use this to connect a NETWORK to any listening one, with following defaults: disable_naggle = 0 , untouched sockbufsendsize and sockbufrecvsize and unrestricted sned/recv lists
 *\param netw a NETWORK *object
 *\param host Host or IP to connect to
 *\param port Port to use to connect
 *\return TRUE or FALSE
 */
int netw_connect( NETWORK **netw , char *host , char *port )
{
	n_log( LOG_INFO , "Trying to connect to %s : %s" , _str( host ) , _str( port ) );
	return netw_connect_ex( &(*netw) , host , port , 0 , 0 , 0 , 0 , 0 );
} /* netw_connect() */



/*!\fn int netw_get_state( NETWORK *netw , int *state , int *thr_engine_status )
 *\brief Get the state of a network
 *\param netw The NETWORK *connection to query
 *\param state pointer to network status storage , NETW_PAUSE , NETW_RUN , NETW_EXIT_ASKED , NETW_EXITED
 *\param thr_engine_status pointer to network thread engine status storage ,NETW_THR_ENGINE_STARTED , NETW_THR_ENGINE_STOPPED
 *\return TRUE or FALSE
 */
int netw_get_state( NETWORK *netw , int *state , int *thr_engine_status )
{
	if( netw )
	{
		pthread_mutex_lock( &netw -> sendbolt );
		if( state )
			(*state) = netw -> state ;
		if( thr_engine_status )
			(*thr_engine_status) = netw -> threaded_engine_status ;
		pthread_mutex_unlock( &netw -> sendbolt );
		return TRUE ;
	}
	else
	{
		n_log( LOG_ERR , "Can't get status of a NULL network" );
	}
	return FALSE ;
} /*netw_get_state() */



/*!\fn int netw_set( NETWORK *netw , int flag )
 *\brief Restart or reset the specified network ability
 *\param netw The NETWORK *connection to modify
 *\param flag NETW_EMPTY_SENDBUF, NETW_EMPTY_RECVBUF, NETW_PAUSE , NETW_RUN , NETW_EXIT_ASKED , NETW_EXITED
 *\return TRUE or FALSE
 */
int netw_set( NETWORK *netw , int flag )
{
	if( flag & NETW_EMPTY_SENDBUF )
	{
		pthread_mutex_lock( &netw -> sendbolt );
		if( netw -> send_buf )
			list_empty( netw -> send_buf );
		pthread_mutex_unlock( &netw -> sendbolt );
	};
	if( flag & NETW_EMPTY_RECVBUF )
	{
		pthread_mutex_lock( &netw -> recvbolt );
		if( netw -> recv_buf )
			list_empty( netw -> recv_buf );
		pthread_mutex_unlock( &netw -> recvbolt );
	}
	if( flag & NETW_DESTROY_SENDBUF )
	{
		pthread_mutex_lock( &netw -> sendbolt );
		if( netw -> send_buf )
			list_destroy( &netw -> send_buf );
		pthread_mutex_unlock( &netw -> sendbolt );
	};
	if( flag & NETW_DESTROY_RECVBUF )
	{
		pthread_mutex_lock( &netw -> recvbolt );
		if( netw -> recv_buf )
			list_destroy( &netw -> recv_buf );
		pthread_mutex_unlock( &netw -> recvbolt );
	}
	pthread_mutex_lock( &netw -> eventbolt );
	if( flag & NETW_CLIENT )
	{
		netw -> mode = NETW_CLIENT ;
	}  
	if( flag & NETW_SERVER )
	{
		netw -> mode = NETW_SERVER ;
	}
	if( flag & NETW_RUN )
	{
		netw -> state = NETW_RUN ;
	}  
	if( flag & NETW_PAUSE )
	{
		netw -> state = NETW_PAUSE ;
	}
	if( flag & NETW_EXITED )
	{
		netw -> state = NETW_EXITED ;
	}
	if( flag & NETW_ERROR )
	{
		netw -> state = NETW_ERROR ;
	}
	if( flag & NETW_EXIT_ASKED )
	{
		netw -> state = NETW_EXIT_ASKED ;
	} 
	if( flag & NETW_THR_ENGINE_STARTED )
	{
		netw -> threaded_engine_status = NETW_THR_ENGINE_STARTED ;
	}
	if( flag & NETW_THR_ENGINE_STOPPED )
	{
		netw -> threaded_engine_status = NETW_THR_ENGINE_STOPPED ;
	}
	pthread_mutex_unlock( &netw -> eventbolt );
	return TRUE;
} /* netw_set(...) */



/*!\fn netw_close( NETWORK **netw )
 *\brief Closing a specified Network, destroy queues, free the structure
 *\param netw A NETWORK *network to close
 *\return TRUE on success , FALSE on failure
 */
int netw_close( NETWORK **netw )
{
	int state = 0 , thr_engine_status = 0 ;
	__n_assert( (*netw) , return FALSE );

	netw_get_state( (*netw) , &state , &thr_engine_status );
	if( thr_engine_status == NETW_THR_ENGINE_STARTED )
	{
		netw_stop_thr_engine( (*netw ) ); 
	}
	/*closing connection*/
	/* Now with end of send fix */
	if( (*netw) -> link . sock != -1 )
	{
		shutdown( (*netw) -> link . sock , SHUT_WR );
		int res = 0 ;
		char buffer[ 4096 ] = "" ;
		for( ;; )
		{
			res = read( (*netw) -> link . sock , buffer , 4096 );
			if( !res )
				break;
			if( res < 0 )
			{
				n_log( LOG_ERR , "read returned an error when closing socket %d: %s" , (*netw) -> link . sock , strerror( errno ) );
				break ;
			}
		}
		closesocket( (*netw) -> link . sock );
	}

	FreeNoLog( (*netw) -> link . ip );
	FreeNoLog( (*netw) -> link . port );

	if( (*netw) -> addr_infos_loaded == 1 )
		freeaddrinfo( (*netw) -> link . rhost );

	/*list freeing*/
	netw_set( (*netw) , NETW_DESTROY_SENDBUF|NETW_DESTROY_RECVBUF );

	pthread_mutex_destroy( &(*netw) -> recvbolt );
	pthread_mutex_destroy( &(*netw) -> sendbolt );
	pthread_mutex_destroy( &(*netw) -> eventbolt );

	Free( (*netw) );

	return TRUE;
} /* netw_close(...)*/



/*!\fn int netw_make_listening( NETWORK **netw , char *port , int nbpending )
 *\brief Make a NETWORK be a Listening network
 *\param netw A NETWORK **network to make listening
 *\param port For choosing a PORT to listen to
 *\param nbpending Number of pending connection when listening
 *\return TRUE on success, FALSE on error
 */
int netw_make_listening( NETWORK **netw , char *port , int nbpending )
{
	/*checking WSA when under windows*/
	if ( handle_wsa( 1 , 2 , 2 ) == FALSE )
	{
		n_log( LOG_ERR , "Unable to load WSA dll's" );
		return FALSE ;
	}

	if( *netw )
	{
		n_log( LOG_ERR , "Cannot use an allocated network. Please pass a NULL network to modify" );
		return FALSE; 
	}
	(*netw) = netw_new( -1 , -1 ); 
	(*netw) -> link . port = strdup( port );
	/*creating array*/
	(*netw) -> link . hints . ai_family   = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
	(*netw) -> link . hints . ai_socktype = SOCK_STREAM;   /* Datagram socket */
	(*netw) -> link . hints . ai_flags    = AI_PASSIVE;    /* For wildcard IP address */
	int error = getaddrinfo( NULL , port , &(*netw) -> link . hints , &(*netw) -> link . rhost );
	if( error != 0)
	{
		n_log( LOG_ERR , "Error when creating listening socket on port %s", port , gai_strerror( error ) );
		netw_close( &(*netw) );
		return FALSE ;
	}
	(*netw) -> addr_infos_loaded = 1 ;

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect(2).
	   If socket(2) (or connect(2)) fails, we (close the socket
	   and) try the next address. */
	struct addrinfo *rp = NULL ;
	for( rp = (*netw) -> link . rhost ; rp != NULL ; rp = rp -> ai_next )
	{
		(*netw) -> link . sock = socket( rp -> ai_family , rp -> ai_socktype , rp->ai_protocol );
		if( (*netw) -> link . sock == -1 )
		{  
			n_log( LOG_ERR , "Error while trying to make a socket: %s"  ,strerror( errno ) );
			continue ;
		}
		if( netw_setsockopt( (*netw) -> link . sock , 0 , 0 , 0 ) != TRUE )
		{
			n_log( LOG_ERR , "Some socket options could not be set on %d" , (*netw) -> link . sock );   
		}
		if( bind( (*netw) -> link . sock , rp -> ai_addr , rp -> ai_addrlen ) == 0 )
		{
			break;                  /* Success */
		}
		error = errno ;
		n_log( LOG_ERR , "Error from bind() on port %s errno: %s" , port , strerror( errno ) , error );
		closesocket( (*netw) -> link .sock );
	}
	if( rp == NULL )
	{
		/* No address succeeded */
		n_log( LOG_ERR , "Couldn't get a socket for listening on port %s", port );
		netw_close( &(*netw) );
		return FALSE ;
	}

	/* nb_pending connections*/
	( *netw ) -> nb_pending = nbpending ;
	listen( ( *netw ) -> link . sock, ( *netw ) -> nb_pending );

	netw_set( (*netw) , NETW_SERVER|NETW_RUN|NETW_THR_ENGINE_STOPPED );

	return TRUE;
} /* netw_make_listening(...)*/



/*!\fn NETWORK *netw_accept_from_ex( NETWORK *from , int disable_naggle , int sock_send_buf , int sock_recv_buf , int send_list_limit , int recv_list_limit , int non_blocking , int *retval )
 *\brief make a normal 'accept' . Network 'from' must be allocated with netw_make_listening.
 *\param from the network from where we accept
 *\param disable_naggle Disable Naggle Algorithm. Set to 1 do activate, 0 or negative to leave defaults
 *\param sock_send_buf NETW_SOCKET_SEND_BUF socket parameter , 0 or negative to leave defaults
 *\param sock_recv_buf NETW_SOCKET_RECV_BUF socket parameter , 0 or negative to leave defaults
 *\param send_list_limit Internal sending list maximum number of item. 0 or negative for unrestricted
 *\param recv_list_limit Internal receiving list maximum number of item. 0 or negative for unrestricted
 *\param non_blocking set to -1 to make it non blocking, to 0 for blocking, else it's the select timeout value in mseconds.
 *\param retval EAGAIN ou EWOULDBLOCK or errno
 *\return NULL on failure, if not a pointer to the connected network
 */
NETWORK *netw_accept_from_ex( NETWORK *from , int disable_naggle , int sock_send_buf , int sock_recv_buf , int send_list_limit , int recv_list_limit , int non_blocking , int *retval )
{
	int tmp = 0 ;
	fd_set accept_set ;

	FD_ZERO( &accept_set );

#if defined( LINUX ) || defined( SOLARIS ) || defined( AIX )
	socklen_t sin_size = 0 ;
#else
	int sin_size = 0 ;
#endif

	NETWORK *netw = NULL ;

	/*checking WSA when under windows*/
	if( handle_wsa( 1 , 2 , 2 ) == FALSE )
	{
		n_log( LOG_ERR , "Unable to load WSA dll's" );
		return NULL ;
	}

	__n_assert( from , return NULL );

	netw = netw_new( send_list_limit , recv_list_limit );

	sin_size = sizeof( netw -> link . raddr );

	if( non_blocking > 0 )
	{
		struct timeval select_timeout ;
		select_timeout . tv_sec = non_blocking ;
		select_timeout . tv_usec = 0;

		FD_SET( from -> link . sock , &accept_set );

		int ret = select( from -> link . sock + 1 , &accept_set , NULL , NULL , &select_timeout );
		if( ret == -1 )
		{
			if( retval != NULL )
				(*retval) = errno ;
			n_log( LOG_ERR , "Error on select with %d timeout" , non_blocking );
			netw_close( &netw );
			return NULL;
		}
		else if( ret == 0 )
		{
			/* that one produce waaaay too much logs under a lot of cases */
			/* n_log( LOG_DEBUG , "No connection waiting on %d" , from -> link . sock ); */    
			netw_close( &netw );
			return NULL;
		}
		if( FD_ISSET(  from -> link . sock , &accept_set ) ) 
		{          
			tmp = accept( from -> link . sock, (struct sockaddr * )&netw -> link . raddr , &sin_size );
			int error = errno ;
			if ( tmp < 0 )
			{
				if( retval != NULL )
					(*retval) = errno ;
				n_log( LOG_DEBUG , "error accepting on %d, %s" , netw -> link . sock , strerror( error ) );
				netw_close( &netw );
				return NULL;
			}
		}
		else
		{
			netw_close( &netw );
			return NULL;
		}
	}
	else if( non_blocking == -1 )
	{
		int flags = fcntl( from -> link . sock , F_GETFL , 0 );
		if( !(flags&O_NONBLOCK) )
			fcntl( from -> link . sock , F_SETFL , flags|O_NONBLOCK );
		tmp = accept( from -> link . sock, (struct sockaddr *)&netw -> link . raddr  , &sin_size );

		if( retval != NULL )
			(*retval) = errno ;

		if ( tmp < 0 )
		{
			netw_close( &netw );
			return NULL;
		}
		/* make the obtained socket blocking if ever it was not */
		flags = fcntl( tmp , F_GETFL , 0 );
		flags = flags&(~O_NONBLOCK) ;
		fcntl( tmp , F_SETFL , flags );
	}
	else
	{
		tmp = accept( from -> link . sock, (struct sockaddr *)&netw -> link . raddr , &sin_size );
		int error = errno ;
		if ( tmp < 0 )
		{
			n_log( LOG_ERR , "error accepting on %d, %s" , netw -> link . sock , error );
			netw_close( &netw );
			return NULL;
		}
	}

	netw -> link . sock = tmp ;
	netw -> link . port = strdup( from -> link . port );
	Malloc( netw -> link . ip , char , INET6_ADDRSTRLEN + 1 );
	if( !inet_ntop( netw -> link . raddr .ss_family , get_in_addr( (struct sockaddr*)&netw -> link . raddr) , netw -> link . ip , INET6_ADDRSTRLEN ) )
	{
		n_log( LOG_ERR , "inet_ntop: %p , %s" , netw -> link . raddr , strerror( errno ) );  
	}

	if( netw_setsockopt( netw -> link . sock , disable_naggle , sock_send_buf , sock_recv_buf ) != TRUE )
	{
		n_log( LOG_ERR , "Some socket options could not be set on %d" , netw-> link . sock );   
	}

	netw_set( netw , NETW_SERVER|NETW_RUN|NETW_THR_ENGINE_STOPPED );

	n_log( LOG_DEBUG , "Connection accepted from %s:%s" , netw-> link . ip , netw -> link . port );

	return netw;
} /* netw_accept_from_ex(...) */



/*!\fn NETWORK *netw_accept_from( NETWORK *from )
 *\brief make a normal blocking 'accept' . Network 'from' must be allocated with netw_make_listening.
 *\param from The network from which to obtaion the connection
 *\return NULL 
 */
NETWORK *netw_accept_from( NETWORK *from )
{
	return netw_accept_from_ex( from , 0 , 0 , 0 , 0 , 0 , 0 , 0 );
} /* network_accept_from( ... ) */



/*!\fn NETWORK *netw_accept_nonblock_from( NETWORK *from , int blocking )
 *\brief make a normal blocking 'accept' . Network 'from' must be allocated with netw_make_listening.
 *\param from The network from which to obtaion the connection
 *\param blocking non_blocking set to -1 to make it non blocking, to 0 for blocking, else it's the select timeout value in mseconds.
 *\return NULL 
 */
NETWORK *netw_accept_nonblock_from( NETWORK *from , int blocking )
{
	return netw_accept_from_ex( from , 0 , 0 , 0 , 0 , 0 , blocking , 0 );
} /* network_accept_from( ... ) */


/*!\fn int netw_add_msg( NETWORK *netw , N_STR *msg )
 *\brief Add a message to send in aimed NETWORK
 *\param netw NETWORK where add the message
 *\param msg  the message to add
 *\return TRUE if success FALSE on error
 */
int netw_add_msg( NETWORK *netw , N_STR *msg )
{
	__n_assert( netw , return FALSE );
	__n_assert( msg , return FALSE );
	__n_assert( msg -> data , return FALSE );

	if( msg -> length <= 0 )
	{
		n_log( LOG_ERR , "Empty messages are not supported. msg(%p)->lenght=%d" , msg , msg -> length );
		return FALSE;
	}

	pthread_mutex_lock( &netw -> sendbolt );

	if( list_push( netw -> send_buf , msg , free_nstr_ptr ) == FALSE )
	{
		pthread_mutex_unlock( &netw -> sendbolt );
		return FALSE;
	}

	pthread_mutex_unlock( &netw -> sendbolt );

	return TRUE;
} /* netw_add_msg(...) */



/*!\fn N_STR *netw_get_msg( NETWORK *netw )
 *\brief Get a message from aimed NETWORK
 *\param netw NETWORK where get the msg
 *\return NULL or a filled N_STR
 */
N_STR *netw_get_msg( NETWORK *netw )
{
	N_STR * ptr = NULL ;

	__n_assert( netw , return NULL );

	pthread_mutex_lock( &netw -> recvbolt );

	ptr = list_shift( netw -> recv_buf , N_STR );

	pthread_mutex_unlock( &netw -> recvbolt );

	return ptr ;
} /* netw_get_msg(...)*/



/*!\fn N_STR *netw_wait_msg( NETWORK *netw , long refresh , long timeout )
 *\brief Wait a message from aimed NETWORK. Recheck each usec until a valid
 *\param netw The link on which we wait a message
 *\param refresh The time in usec between each check until there is a message
 *\param timeout in usecs , maximum amount of time to wait before return. Pass a value <= 0 to disable.
 *\return NULL or a filled N_STR 
 */
N_STR *netw_wait_msg( NETWORK *netw , long refresh , long timeout )
{
	N_STR *nstrptr = NULL ;
	int timed = 0 ;

	__n_assert( netw , return NULL );

	if( refresh > 999999 )
		refresh = 999999 ;

	if( timeout > 0 )
		timed = 1 ;

	int state = NETW_RUN , thr_state = 0 ;

	/* first try without sleep in case there alreay is a message in queue */
	nstrptr = netw_get_msg( netw );
	if( nstrptr )
		return nstrptr ;
	do
	{
		usleep( refresh );

		nstrptr = netw_get_msg( netw );
		if( nstrptr )
			return nstrptr ;

		if( timed == 1 )
		{
			timeout -= refresh ;
			if( timeout < 0 )
			{
				n_log( LOG_ERR , "timed out for netw %d" , netw -> link . sock );
				break ;
			}
		}
		netw_get_state( netw, &state , &thr_state );

	}while( state == NETW_RUN );

	return NULL ;
} /* netw_wait_msg(...) */



/*!\fn netw_start_thr_engine( NETWORK *netw )
 *\brief Start the NETWORK netw Threaded Engine. Create a sending & receiving thread.
 *\param netw The aimed NETWORK connection to start receiving data
 *\return TRUE on success FALSE on failure
 */
int netw_start_thr_engine( NETWORK *netw )
{
	__n_assert( netw , return FALSE );

	pthread_mutex_lock( &netw -> eventbolt );
	if( netw -> threaded_engine_status == NETW_THR_ENGINE_STARTED )
	{
		n_log( LOG_ERR , "THR Engine already started for network %p" , netw );
		pthread_mutex_unlock( &netw -> eventbolt );
		return FALSE ;
	}

	if( pthread_create(  &netw -> recv_thr ,   NULL ,  netw_recv_func ,  (void *) netw ) != 0 )
	{
		n_log( LOG_ERR , "Unable to create recv_thread for network %p" , netw );
		pthread_mutex_unlock( &netw -> eventbolt );
		return FALSE ;
	}
	if( pthread_create(  &netw -> send_thr ,   NULL ,  netw_send_func ,  (void *) netw ) != 0 )
	{
		n_log( LOG_ERR , "Unable to create send_thread for network %p" , netw );
		pthread_mutex_unlock( &netw -> eventbolt );
		return FALSE ;
	}

	netw -> threaded_engine_status = NETW_THR_ENGINE_STARTED ;

	pthread_mutex_unlock( &netw -> eventbolt );

	return TRUE ;
}  /* netw_create_recv_thread(....) */



/*!\fn void *netw_send_func( void *NET )
 *\brief Thread send function
 *\param NET the NETWORK connection to use, casted into (void*)
 */
void *netw_send_func( void *NET )
{
	int DONE = 0 ,
		state = 0 ,
		net_status = 0 ;

	NSTRBYTE nboctet = 0 ;

	char nboct[ 4 ];

	N_STR *ptr = NULL ;

	NETWORK *netw = (NETWORK *) NET ;

	__n_assert( netw , return NULL );

	do
	{
		netw_get_state( netw , &state , NULL );
		if( state&NETW_EXIT_ASKED )
		{
			DONE = 100 ;
			n_log( LOG_DEBUG , "%d Sending Quit !" , netw -> link . sock );
			/* sending state */
			nboctet = htonl( NETW_EXIT_ASKED );
			memcpy( nboct , &nboctet , sizeof( NSTRBYTE ) );
			net_status = send_data( netw -> link . sock , nboct , sizeof( NSTRBYTE ) );
			if( net_status < 0 )
				DONE = 4 ;
		}
		else
		{
			if( !(state&NETW_PAUSE) )
			{
				pthread_mutex_lock( &netw -> sendbolt );
				ptr = list_shift( netw -> send_buf , N_STR ) ;
				pthread_mutex_unlock( &netw -> sendbolt );
				if( ptr && ptr -> length > 0 && ptr -> data )
				{
					n_log( LOG_DEBUG , "Sending ptr size %ld written %ld" , ptr -> length , ptr -> written );

					/* sending state */
					nboctet = htonl( state );
					memcpy( nboct , &nboctet , sizeof( NSTRBYTE ) );
					/* sending state */
					if ( !DONE )
					{
						net_status = send_data( netw -> link . sock , nboct , sizeof( NSTRBYTE ) );
						if( net_status < 0 )
							DONE = 1 ;
					}
					if( !DONE )
					{
						/* sending number of octet */
						nboctet = htonl( ptr -> written );
						memcpy( nboct , &nboctet , sizeof( NSTRBYTE ) );
						/* sending the number of octet to receive on next message */
						net_status = send_data( netw -> link . sock , nboct , sizeof( NSTRBYTE ) );
						if( net_status < 0 )
							DONE = 2 ;
					}
					/* sending the data itself */
					if ( !DONE )
					{
						net_status = send_data( netw -> link . sock , ptr -> data , ptr -> written );
						if( net_status < 0 )
							DONE = 3 ;
					}
					free_nstr( &ptr );
#if !defined( LINUX ) && !defined( SOLARIS ) && !defined( AIX )
					rest( netw -> send_queue_consecutive_wait / 1000 );
#else
					usleep(  netw -> send_queue_consecutive_wait ); /* we will have to make a sleep thing for that */
#endif
				}
				else
				{
#if !defined( LINUX ) && !defined( SOLARIS ) && !defined( AIX ) 
					rest( netw -> send_queue_wait / 1000 );
#else
					usleep( netw -> send_queue_wait );
#endif
				}
			}
			else /* Network PAUSED */
			{
#if !defined( LINUX ) && !defined( SOLARIS ) && !defined( AIX ) 
				rest( netw -> pause_wait / 1000 );
#else
				usleep( netw -> pause_wait );
#endif
			}
		}
	}while( !DONE );

	if( DONE == 1 )
		n_log( LOG_ERR ,  "Error when sending state %d on socket %d, network: %s" , netw -> state , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );
	else if( DONE == 2 )
		n_log( LOG_ERR ,  "Error when sending number of octet to socket %d, network: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );
	else if( DONE == 3 )
		n_log( LOG_DEBUG ,  "Error when sending data on socket %d, network: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );
	else if( DONE == 4 )
		n_log( LOG_ERR ,  "Error when sending state QUIT on socket %d, network: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );
	else if( DONE == 5 )
		n_log( LOG_ERR ,  "Error when sending state QUIT number of octet (0) on socket %d, network: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );

	if( DONE == 100 )
	{
		n_log( LOG_DEBUG , "Socket %d: Sending thread exited !" , netw -> link . sock );
		netw_set( netw , NETW_EXIT_ASKED );
	}
	else
	{
		n_log( LOG_ERR , "Closing send thread for socket %d\nReason unknown\nDONE = %d" , netw -> link . sock , DONE );
		netw_set( netw , NETW_ERROR );
	}   

	pthread_exit( 0 );

	/* suppress compiler warning */
#if !defined( LINUX ) && !defined( SOLARIS )
	return NULL ;
#endif
} /* netw_send_func(...) */



/*!\fn void *netw_recv_func( void *NET )
 *\brief To Thread Receiving function
 *\param NET the NETWORK connection to use
 */
void *netw_recv_func( void *NET )
{
	int DONE = 0 ,
		state = 0 ,
		tmpstate = 0 ,
		net_status = 0 ;

	NSTRBYTE nboctet = 0 ;

	char nboct[ 4 ];

	N_STR *recvdmsg = NULL ;

	NETWORK *netw = (NETWORK *) NET ;

	__n_assert( netw , return NULL );

	do
	{
		netw_get_state( netw , &state , NULL );

		if( state&NETW_EXIT_ASKED )
		{
			DONE = 100;
		}
		else
		{
			if( !DONE && !(state&NETW_EXIT_ASKED) && !(state&NETW_EXITED) ) 
			{   
				if( !(state&NETW_PAUSE) )
				{
					/* receiving state */
					net_status = recv_data( netw -> link . sock , nboct , sizeof( NSTRBYTE ) );
					if( net_status < 0 )
					{
						DONE = 1 ;
					}
					else
					{
						memcpy( &nboctet , nboct , sizeof( NSTRBYTE ) );
						tmpstate = ntohl( nboctet );
						nboctet = tmpstate ;
						if( tmpstate==NETW_EXIT_ASKED )
						{
							n_log( LOG_DEBUG ,  "%d receiving order to QUIT, nboctet %d NETW_EXIT_ASKED %d !" , netw -> link . sock , nboctet , NETW_EXIT_ASKED );

							DONE = 100 ;
							netw_set( netw , NETW_EXIT_ASKED );
						}
						else
						{
							/* receiving nboctet */
							net_status = recv_data( netw -> link . sock , nboct , sizeof( NSTRBYTE ) );
							if( net_status < 0 )
							{
								DONE = 2 ;
							}
							else
							{
								memcpy( &nboctet , nboct , sizeof( NSTRBYTE ) );
								tmpstate = ntohl( nboctet );
								nboctet = tmpstate ;

								Malloc( recvdmsg , N_STR , 1 );
								n_log( LOG_DEBUG ,  "%d octets to receive..." , nboctet );
								Malloc( recvdmsg -> data , char , nboctet + 1 );
								recvdmsg -> length  = nboctet + 1 ;
								recvdmsg -> written = nboctet ;

								/* receiving the data itself */
								net_status = recv_data( netw -> link . sock , recvdmsg -> data , nboctet );
								if( net_status < 0 )
								{
									DONE = 3 ;
								}
								else
								{
									pthread_mutex_lock( &netw -> recvbolt );
									if( !DONE && list_push( netw -> recv_buf , recvdmsg , free_nstr_ptr ) == FALSE )
										DONE = 4 ;
									pthread_mutex_unlock( &netw -> recvbolt );
								} /* recv data */
							} /* recv nb octet*/
						} /* exit asked */
					} /* recv state */
				}
				else
				{
					/*NETW_PAUSE */
#if !defined( LINUX ) && !defined( SOLARIS ) && !defined( AIX ) 
					rest( netw -> pause_wait / 1000 );
#else
					usleep( netw -> pause_wait );
#endif
				}
			} /* if( !done) */
		} /* if !exit */

	}while( !DONE );

	if( DONE == 1 )
		n_log( LOG_ERR ,  "Error when receiving state from socket %d, net_status: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );
	else if( DONE == 2 )
		n_log( LOG_ERR ,  "Error when receiving nboctet from socket %d, net_status: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );
	else if( DONE == 3 )
		n_log( LOG_ERR ,  "Error when receiving data from socket %d, net_status: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );
	else if( DONE == 4 )
		n_log( LOG_ERR ,  "Error adding receved message from socket %d, net_status: %s" , netw -> link . sock , (net_status==-2)?"disconnected":"socket error" );


	if( DONE == 100 )
	{
		n_log( LOG_DEBUG , "Socket %d: Receive thread exiting !" , netw -> link . sock );
		netw_set( netw , NETW_EXIT_ASKED );
	}
	else
	{
		n_log( LOG_ERR , "Closing recv thread for socket %d\nReason unknown\nDONE = %d" , netw -> link . sock , DONE );
		netw_set( netw , NETW_ERROR );
	}

	pthread_exit( 0 );

	/* suppress compiler warning */
#if !defined( LINUX ) && !defined( SOLARIS )
	return NULL ;
#endif
} /* netw_recv_func(...) */



/*!\fn netw_stop_thr_engine( NETWORK *netw )
 *\brief Stop a NETWORK connection sending and receing thread
 *\param netw The aimed NETWORK conection to stop
 *\return TRUE on success FALSE on failure
 */
int netw_stop_thr_engine( NETWORK *netw )
{
	__n_assert( netw ,  return FALSE );
	int state = 0 , thr_engine_status = 0 ;

	n_log( LOG_DEBUG ,  "Network %d stop threads event" , netw -> link . sock );

	netw_get_state( netw , &state , &thr_engine_status );

	if( thr_engine_status != NETW_THR_ENGINE_STARTED )
	{
		n_log( LOG_ERR , "Thread engine status already stopped for network %p" , netw );
		return FALSE ;
	}

	netw_set( netw , NETW_EXIT_ASKED );

	n_log( LOG_DEBUG ,  "Network %d waits for send threads to stop" , netw -> link . sock );
	pthread_join( netw -> send_thr , NULL );
	n_log( LOG_DEBUG ,  "Network %d waits for recv threads to stop" , netw -> link . sock );
	pthread_join( netw -> recv_thr , NULL );

	netw_set( netw , NETW_EXITED|NETW_THR_ENGINE_STOPPED );

	n_log( LOG_DEBUG ,  "Network %d threads Stopped !" , netw -> link . sock );

	return TRUE;
} /* Stop_Network( ... ) */



/*!\fn send_data( SOCKET s , char *buf, NSTRBYTE n )
 *\brief send data onto the socket
 *\param s connected socket
 *\param buf pointer to buffer
 *\param n number of characters we want to send
 *\return -1 on error, -2 on disconnection, n on success
 */
int send_data( SOCKET s, char *buf, NSTRBYTE n )
{
	int bcount = 0 ; /* counts bytes read */
	int br = 0 ;     /* bytes read this pass */

	__n_assert( buf , return -1 );

	if( n == 0 )
	{
		n_log( LOG_ERR , "Send of 0 is unsupported." );
		return -1 ;
	}

	while( (NSTRBYTE)bcount < n )                 /* loop until full buffer */
	{
		if ( ( br = send( s, buf, n - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                /* increment byte counter */
			buf += br;                   /* move buffer ptr for next read */
		}
		else
		{
			if( errno == ECONNRESET )
			{
				n_log( LOG_DEBUG , "socket %d disconnected !" , s );
				return -2 ;
			}
			/* signal an error to the caller */
			n_log( LOG_ERR ,  "Socket %d receive Error: %d , %s" , s , br , strerror( errno ) );
			return -1 ;
		}
	}

	return bcount;
} /*send_data(...)*/



/*!\fn recv_data( SOCKET s , char *buf, NSTRBYTE n )
 *\brief recv data from the socket
 *\param s connected socket
 *\param buf pointer to buffer
 *\param n number of characters we want
 *\return -1 on error, -2 on disconnection n on success
 */
int recv_data( SOCKET s, char *buf , NSTRBYTE n )
{
	int bcount = 0 ; /* counts bytes read */
	int br = 0 ;     /* bytes read this pass */

	__n_assert( buf , return -1 );

	if( n == 0 )
	{
		n_log( LOG_ERR , "Recv of 0 is unsupported." );
		return -1 ;
	}

	while( (NSTRBYTE)bcount < n )
	{                                        /* loop until full buffer */

		if ( ( br = recv( s, buf, n - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                    /* increment byte counter */
			buf += br;                       /* move buffer ptr for next read */
		}
		else
		{
			if( br == 0 )
			{
				n_log( LOG_DEBUG ,  "socket %d disconnected !" , s );
				return -2 ;
			}
			/* signal an error to the caller */
			n_log( LOG_ERR ,  "socket %d receive Error: %s" , s , strerror( errno ) );
			return -1 ;
		}
	}

	return bcount;
} /*recv_data(...)*/



/*!\fn int send_php( SOCKET s, int _code , char *buf, int n )
 *\brief send data onto the socket
 *\param s connected socket
 *\param _code Code for php decoding rule
 *\param buf pointer to buffer
 *\param n number of characters we want to send
 *\return -1 on error, n on success
 */
int send_php( SOCKET s, int _code , char *buf, int n )
{
	int bcount = 0 ;   /* counts bytes read */
	int br = 0 ;       /* bytes read this pass */
	char *ptr = NULL ; /* temp char ptr ;-) */
	char tmpstr[ HEAD_SIZE +1 ] = "";
	char head[ HEAD_SIZE +1 ] = "" ;
	char code[ HEAD_CODE +1 ] = "" ;

	snprintf( tmpstr , HEAD_SIZE + 1 , "%%0%dd" , HEAD_SIZE );
	snprintf( head , HEAD_SIZE + 1 , tmpstr , n );
	snprintf( tmpstr , HEAD_SIZE + 1 , "%%0%dd" , HEAD_CODE );
	snprintf( code , HEAD_CODE + 1 , tmpstr , _code );

	/* sending head */
	bcount = br = 0 ;
	ptr = head ;
	while ( bcount < HEAD_SIZE )                 /* loop until full buffer */
	{
		if ( ( br = send( s, head , HEAD_SIZE - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                /* increment byte counter */
			ptr += br;                   /* move buffer ptr for next read */
		}
		else
		{                            /* signal an error to the caller */
			n_log( LOG_ERR ,  "Socket %d sending Error %d when sending head size" , s , br );
			return FALSE;
		}
	}

	/* sending code */
	bcount = br = 0 ;
	ptr = code ;
	while ( bcount < HEAD_CODE )                 /* loop until full buffer */
	{
		if ( ( br = send( s, code , HEAD_CODE - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                /* increment byte counter */
			ptr += br;                   /* move buffer ptr for next read */
		}
		else
		{
			n_log( LOG_ERR ,  "Socket %d sending Error %d when sending head code" , s , br );
			return FALSE;
		}
	}

	/* sending buf */ 
	bcount = 0;
	br = 0;
	while ( bcount < n )                 /* loop until full buffer */
	{
		if ( ( br = send( s, buf, n - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                /* increment byte counter */
			buf += br;                   /* move buffer ptr for next read */
		}
		else
		{                            /* signal an error to the caller */
			n_log( LOG_ERR , "Socket %d sending Error %d when sending message of size %d" , s , br , n );
			return FALSE;
		}
	}

	return bcount;

} /*send_php(...)*/



/*!\fn int recv_php( SOCKET s, int *_code , char **buf )
 *\brief recv data from the socket
 *\param s connected socket
 *\param _code pointer to int which will receive the code of the request
 *\param buf pointer to buffer
 *\return -1 on error, size on success
 */
int recv_php( SOCKET s, int *_code , char **buf )
{
	int bcount = 0 ; /* counts bytes read */
	int br = 0 ;     /* bytes read this pass */
	long int tmpnb = 0 , size = 0 ;   /* size of message to receive */
	char *ptr = NULL ;

	char head[ HEAD_SIZE + 1 ] = "" ;
	char code[ HEAD_CODE + 1]  = "" ;

	/* Receiving total message size */
	bcount = br = 0 ;
	ptr = head;
	while ( bcount < HEAD_SIZE )
	{                                        /* loop until full buffer */
		if ( ( br = recv( s, ptr , HEAD_SIZE - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                    /* increment byte counter */
			ptr += br;                       /* move buffer ptr for next read */
		}
		else
		{
			if( br == 0 && bcount == ( HEAD_SIZE - bcount ) )
				break ;
			/* signal an error to the caller */
			n_log( LOG_ERR , "Socket %d receive %d Error %s" , s  , br , strerror( errno ) );
			return FALSE;
		}
	}

	tmpnb = strtol( head , NULL , 10 );
	if( tmpnb == LONG_MIN || tmpnb == LONG_MAX )
	{
		n_log( LOG_ERR , "Size received ( %ld ) can not be determined on socket %d" , tmpnb , s );
		return FALSE ;
	}

	size = tmpnb ;

	/* receiving request code */
	bcount = br = 0 ;
	ptr = code;
	while ( bcount < HEAD_CODE )
	{                                        /* loop until full buffer */
		if ( ( br = recv( s, ptr , HEAD_CODE - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                    /* increment byte counter */
			ptr += br;                       /* move buffer ptr for next read */
		}
		else
		{  
			if( br == 0 && bcount == ( HEAD_CODE - bcount ) )
				break ;
			/* signal an error to the caller */
			n_log( LOG_ERR , "Socket %d receive %d Error" , s  , br );
			return FALSE;
		}
	}

	tmpnb = strtol( code , NULL , 10 );
	if( tmpnb == LONG_MIN || tmpnb == LONG_MAX )
	{
		n_log( LOG_ERR , "Code received ( %ld ) can not be determined on socket %d\n" , tmpnb , s );
		return FALSE ;
	}

	(*_code) = tmpnb ;

	/* receving message */
	if( (*buf ) )
	{
		Free( (*buf ) );
	}
	Malloc( (*buf) , char , (size+1) );
	bcount = 0;
	br = 0;
	ptr = (*buf);
	while ( bcount < size )
	{                                        /* loop until full buffer */
		if ( ( br = recv( s, ptr , size - bcount, NETFLAGS ) ) > 0 )
		{
			bcount += br;                    /* increment byte counter */
			ptr += br;                       /* move buffer ptr for next read */
		}
		else
		{
			if( br == 0 && bcount == ( size - bcount ) )
				break ;
			/* signal an error to the caller */
			n_log( LOG_ERR , "Socket %d receive %d Error" , s  , br );
			return FALSE;
		}
	}
	return size ;
} /*recv_php(...)*/


/*!\fn int netw_get_queue_status( NETWORK *netw , int *nb_to_send , int *nb_to_read )
 *\brief return the number of elements queued in the sending and receiving queue
 *\param netw The network to request
 *\param nb_to_send a pointer to an int which will get the number of elements to send 
 *\param nb_to_read a pointer to an int which will get the number of elements to read
 *\return TRUE or FALSE
 */
int netw_get_queue_status( NETWORK *netw , int *nb_to_send , int *nb_to_read )
{
	__n_assert( netw , return FALSE );

	pthread_mutex_lock( &netw -> sendbolt );
	(*nb_to_send) = netw -> send_buf -> nb_items ;
	pthread_mutex_unlock( &netw -> sendbolt );

	pthread_mutex_lock( &netw -> recvbolt );
	(*nb_to_read) = netw -> recv_buf -> nb_items ;
	pthread_mutex_unlock( &netw -> recvbolt );

	return TRUE ;
} /* netw_get_queue_status */
