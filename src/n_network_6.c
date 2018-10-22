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
	if( !inet_ntop( AF_INET6 , get_in_addr( (struct sockaddr *)rp -> ai_addr ) , (*netw) -> link . ip , INET6_ADDRSTRLEN ) )
	{
		n_log( LOG_ERR , "inet_ntop: %p , %s" , rp , strerror( errno ) );  
	}


	(*netw)->link . port = strdup( port );
	__n_assert( (*netw) -> link . port , netw_close( &(*netw ) ); return FALSE );
	n_log( LOG_DEBUG , "Connected to %s:%s" , (*netw) -> link . ip , (*netw) -> link . port );

	netw_set( (*netw) , NETW_CLIENT|NETW_RUN|NETW_THR_ENGINE_STOPPED );

	return TRUE ;
} /* netw_connect_ex(...)*/




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

