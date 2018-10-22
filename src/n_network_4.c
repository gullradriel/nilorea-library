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


/*!\fn int netw_tcp_socket( SOCKET *sock )
 *\brief Get a new tcp socket
 *\param sock Pointer to a valid and empty SOCKET
 *\return TRUE or FALSE (in that case sock is untouched)
 */
int netw_tcp_socket( SOCKET *sock )
{
   int tmpsock = 0 ;

   /* new socket */
   tmpsock = socket( AF_INET , SOCK_STREAM , 0 );
   if( tmpsock < 0 )
   {
      n_log( LOG_ERR , "Error from socket(). errno: %s" , strerror( errno ) );
      return FALSE ;
   }
   (*sock) = tmpsock ;

   return TRUE ;
} /* new_tcp_socket( ... ) */



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
	struct hostent *hostinfo = NULL ;

	/*checking WSA when under windows*/
	if ( handle_wsa( 1 , 2 , 2 ) == FALSE )
	{
		n_log( LOG_ERR , "Unable to load WSA dll's" );
		return FALSE ;
	}

	/*do not work over an already used netw*/
	if( (*netw) )
	{
		n_log( LOG_ERR , "Unable to allocate (*netw), already existing. You must use empty NETWORK *structs." );
		return FALSE ;
	}

	hostinfo = gethostbyname( host );
	if( hostinfo == NULL )
	{
		n_log( LOG_ERR , "Erreur -> host : %s inconnu.", host );
		return FALSE ;
	}

	/*creating array*/
	Malloc( ( *netw ) , NETWORK , 1 );
	__n_assert( (*netw) , return FALSE );

	/* new socket */
	if( netw_tcp_socket( &(*netw) -> link . sock ) == FALSE )
	{
		n_log( LOG_ERR , "Error from socket, %s" , strerror( errno ) );
		Free( (*netw ) );
		return FALSE ;
	}

	if( netw_setsockopt( (*netw) -> link . sock , disable_naggle , sock_send_buf , sock_recv_buf ) != TRUE )
	{
		n_log( LOG_ERR , "Some socket options could not be set on %d" , (*netw) -> link . sock );   
	}

	/*filling host information*/
	( *netw ) -> link . port = strdup( port );
	memcpy(&( *netw ) -> link . raddr . sin_addr , hostinfo -> h_addr_list[ 0 ] , hostinfo -> h_length );
	( *netw ) -> link . raddr . sin_family = AF_INET;
	( *netw ) -> link . raddr . sin_port = htons( atoi( port ) );

	/*filling local information*/
	( *netw ) -> link . haddr . sin_addr . s_addr = htonl( INADDR_ANY );
	( *netw ) -> link . haddr . sin_family = AF_INET;
	( *netw ) -> link . haddr . sin_port = htons( atoi( port ) );

	/*zero the rest of the struct */
	memset( &( ( *netw ) -> link . haddr . sin_zero ), 0 , 8 );    
	memset( &( ( *netw ) -> link . raddr . sin_zero ), 0 , 8 ); 

	/*connecting*/
	if( connect( ( *netw ) -> link .sock , ( struct sockaddr * ) &( ( *netw ) -> link . raddr ) , sizeof( struct sockaddr ) ) != 0 )
	{
		n_log( LOG_ERR , "Error from connect(). errno: %s" , strerror( errno ) );
		closesocket( (*netw) -> link . sock );
		Free( (*netw) );
		return FALSE ;
	}

	/*storing connected to ip adress*/
	( *netw ) -> link . ip = strdup( host );

	/*initialize queues */
	( *netw ) -> recv_buf = new_generic_list( recv_list_limit );
	if( ! (*netw ) -> recv_buf )
	{
		n_log( LOG_ERR , "Error when creating receive list with %d item limit" , recv_list_limit );
		closesocket( (*netw) -> link . sock );
		Free( (*netw) );
		return FALSE ;
	}
	(*netw) -> send_buf = new_generic_list( send_list_limit );
	if( ! (*netw ) -> send_buf )
	{
		n_log( LOG_ERR , "Error when creating send list with %d item limit" , send_list_limit );
		list_destroy( &( *netw ) -> recv_buf );
		closesocket( (*netw) -> link . sock );
		Free( (*netw) );
		return FALSE ;
	}

	/*initiliaze mutexs*/
	if ( pthread_mutex_init( &( *netw ) -> sendbolt , NULL ) != 0 )
	{
		n_log( LOG_ERR , "Error initializing (*netw) -> sendbolt" ); 
		list_destroy( &( *netw ) -> recv_buf );
		list_destroy( &( *netw ) -> send_buf );
		closesocket( (*netw) -> link . sock );
		Free( (*netw) );
		return FALSE ;
	}
	if ( pthread_mutex_init( &( *netw ) -> recvbolt , NULL ) != 0 )
	{ 
		n_log( LOG_ERR , "Error initializing (*netw) -> recvbolt" ); 
		pthread_mutex_destroy( &( *netw ) -> sendbolt );
		list_destroy( &( *netw ) -> recv_buf );
		list_destroy( &( *netw ) -> send_buf );
		closesocket( (*netw) -> link . sock );
		Free( (*netw) );
		return FALSE ;
	}
	if ( pthread_mutex_init( &( *netw ) -> eventbolt , NULL ) != 0 )
	{
		n_log( LOG_ERR , "Error initializing (*netw) -> eventbolt" ); 
		pthread_mutex_destroy( &( *netw ) -> sendbolt );
		pthread_mutex_destroy( &( *netw ) -> recvbolt );
		list_destroy( &( *netw ) -> recv_buf );
		list_destroy( &( *netw ) -> send_buf );
		closesocket( (*netw) -> link . sock );
		Free( (*netw) );
		return FALSE ;
	}


	netw_set( (*netw) , NETW_CLIENT|NETW_RUN|NETW_THR_ENGINE_STOPPED );

	return TRUE ;
} /* netw_connect_ex(...)*/



/*!\fn int netw_make_listening( NETWORK **netw , char * port , int nbpending )
 *\brief Make a NETWORK be a Listening network
 *\param netw A NETWORK **network to make listening
 *\param port For choosing a port to listen to
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

	/*creating array*/
	Malloc( *netw , NETWORK , 1 );

	if( netw_tcp_socket( &(*netw) -> link . sock ) == FALSE )
	{
		n_log( LOG_ERR , "Error from socket, %s" , strerror( errno ) );
		Free( (*netw ) );
		return FALSE ;
	}

	if( netw_setsockopt( (*netw) -> link . sock , 0 , 0 , 0 ) != TRUE )
	{
		n_log( LOG_ERR , "Some socket options could not be set on %d" , (*netw) -> link . sock );   
	}

	/*filling local information*/
	( *netw ) -> link . port = strdup( port );
	( *netw ) -> link . haddr . sin_addr.s_addr = INADDR_ANY;
	( *netw ) -> link . haddr . sin_family = AF_INET;
	( *netw ) -> link . haddr . sin_port = htons( atoi( port ) );
	( *netw ) -> nb_pending = nbpending;

	memset( &( ( *netw ) -> link . haddr . sin_zero ), 0 , 8 ); /* zero the rest of the struct*/
	memset( &( ( *netw ) -> link . raddr . sin_zero ), 0 , 8 ); /* zero the rest of the struct*/

	/*binding information to socket*/
	if ( bind( ( *netw ) -> link . sock , ( struct sockaddr * ) &( ( *netw ) -> link . haddr ) , sizeof( struct sockaddr ) ) == -1 )
	{
		n_log( LOG_ERR , "Error binding socket %d" , ( *netw ) -> link . sock );
		return FALSE;
	}

	/* nb_pending connections*/
	listen( ( *netw ) -> link . sock, ( *netw ) -> nb_pending );

	(*netw) -> mode = NETW_SERVER ;
	(*netw) -> state = NETW_RUN ;
	(*netw) -> threaded_engine_status = NETW_THR_ENGINE_STOPPED ;

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
		return FALSE ;
	}

	__n_assert( from , return FALSE );

	Malloc( netw , NETWORK , 1 );

	sin_size = sizeof( struct sockaddr_in );

	/* tmp = accept( from -> link . sock, ( struct sockaddr * ) & ( netw -> link . raddr ) , &sin_size );
	   if ( tmp < 0 )
	   {
	   n_log( LOG_ERR , "error accepting on %d" , netw -> link . sock );
	   Free( netw );
	   return NULL;
	   }*/

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

	if( netw_setsockopt( netw -> link . sock , disable_naggle , sock_send_buf , sock_recv_buf ) != TRUE )
	{
		n_log( LOG_ERR , "Some socket options could not be set on %d" , netw-> link . sock );   
	}
	/*initialize queues */
	netw -> recv_buf = new_generic_list( recv_list_limit );
	if( !netw -> recv_buf )
	{
		n_log( LOG_ERR , "Error when creating receive list with %d item limit" , recv_list_limit );
		closesocket( netw -> link . sock );
		Free( netw );
		return FALSE ;
	}
	netw -> send_buf = new_generic_list( send_list_limit );
	if( ! netw -> send_buf )
	{
		n_log( LOG_ERR , "Error when creating send list with %d item limit" , send_list_limit );
		list_destroy( &netw -> recv_buf );
		closesocket( netw -> link . sock );
		Free( netw );
		return FALSE ;
	}

	/*initiliaze mutexs*/
	if( pthread_mutex_init( &netw -> sendbolt , NULL ) != 0 )
	{
		n_log( LOG_ERR , "Error initializing netw -> sendbolt" ); 
		list_destroy( &netw -> recv_buf );
		list_destroy( &netw -> send_buf );
		closesocket( netw -> link . sock );
		Free( netw );
		return FALSE ;
	}
	if( pthread_mutex_init( &netw -> recvbolt , NULL ) != 0 )
	{ 
		n_log( LOG_ERR , "Error initializing netw -> recvbolt" ); 
		pthread_mutex_destroy( &netw -> sendbolt );
		list_destroy( &netw -> recv_buf );
		list_destroy( &netw -> send_buf );
		closesocket( netw -> link . sock );
		Free( netw );
		return FALSE ;
	}
	if ( pthread_mutex_init( &netw -> eventbolt , NULL ) != 0 )
	{
		n_log( LOG_ERR , "Error initializing (*netw) -> eventbolt" ); 
		pthread_mutex_destroy( &netw -> sendbolt );
		pthread_mutex_destroy( &netw -> recvbolt );
		list_destroy( &netw -> recv_buf );
		list_destroy( &netw -> send_buf );
		closesocket( netw -> link . sock );
		Free( netw );
		return FALSE ;
	}

	netw -> mode = NETW_SERVER ;
	netw -> state = NETW_RUN ;
	netw -> threaded_engine_status = NETW_THR_ENGINE_STOPPED ;

	Malloc( netw -> link . ip , char , INET6_ADDRSTRLEN + 1 );
	if( !inet_ntop( AF_INET , get_in_addr( (struct sockaddr*)&netw -> link . raddr) , netw -> link . ip , INET6_ADDRSTRLEN ) )
	{
		n_log( LOG_ERR , "inet_ntop: %p , %s" , netw -> link . raddr , strerror( errno ) );  
	}

	return netw;
} /* netw_accept_from_ex(...) */

