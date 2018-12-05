/**\file ex_network.h
 *  Nilorea Library n_network api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */

#define NETMSG_DATA 1
#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>

#include "nilorea/n_common.h"
#ifndef __windows__
	#include <sys/wait.h>
#endif

#include "nilorea/n_network.h"
#include "nilorea/n_network_msg.h"
#include "nilorea/n_thread_pool.h"

#ifdef __linux__
void sigchld_handler( int sig )
{
	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
	n_log( LOG_DEBUG , "Signal %d" , sig );
}
#endif

int send_net_datas( NETWORK *netw , N_STR *data )
{
	NETW_MSG *msg = NULL ;
	N_STR *tmpstr = NULL ;
	N_STR *hostname = NULL ;

	__n_assert( netw , return FALSE );

	create_msg( &msg );
	__n_assert( msg , return FALSE );

	hostname = new_nstr( 1024 );
	if( gethostname( hostname -> data , hostname -> length ) != 0 )
	{
		n_log( LOG_ERR , "Coudldn't resolve hostname. Error:%s" , strerror( errno ) );
		free_nstr( &hostname );
		return FALSE;
	}
	hostname -> written = strlen( hostname -> data );

	add_int_to_msg( msg , NETMSG_DATA );

	add_nstrptr_to_msg( msg , hostname );

	n_log( LOG_DEBUG , "Adding string %s" , data -> data );

	add_nstrdup_to_msg( msg , data );

	for( int i = 0 ; i < 10 ; i ++ )
	{
		int val = ( rand()%20 ) - 10 ; 
		add_int_to_msg( msg , val );
		n_log( LOG_DEBUG , "adding %d to message" , val );
	}

	tmpstr = make_str_from_msg( msg );

	delete_msg( &msg );

	__n_assert( tmpstr , return FALSE );

	return netw_add_msg( netw , tmpstr );
} /* send_net_datas */



int get_net_datas( N_STR *str , N_STR **hostname , N_STR **data )
{
	NETW_MSG *netmsg = NULL ;
	int type  = 0;

	__n_assert( str , return FALSE );

	netmsg = make_msg_from_str( str );
	__n_assert( netmsg , return FALSE );

	get_int_from_msg( netmsg , &type );
	if( type != NETMSG_DATA )
	{
		n_log( LOG_ERR , "Error: message is not NETMSG_DATA(%d) but %d !" , NETMSG_DATA , type );
		delete_msg( &netmsg );
		return FALSE;
	}
	get_nstr_from_msg( netmsg , &(*hostname) );
	get_nstr_from_msg( netmsg , &(*data) );
	n_log( LOG_DEBUG , "getting string %s" , (*data) -> data );

	for( int i = 0 ; i < 10 ; i ++ )
	{
		int val = 0 ;
		get_int_from_msg( netmsg , &val );
		n_log( LOG_DEBUG , "getting %d from message" , val );
	}


	delete_msg( &netmsg );

	return TRUE;
} /* get_net_datas( ... ) */



/*!\fn netw_msg_get_type( N_STR *msg )
 *\param msg A char *msg_object you want to have type
 *\brief Get the type of message without killing the first number. Use with netw_get_XXX
 *\return The value corresponding to the type or FALSE
 */
int netw_msg_get_type( N_STR *msg )
{
	NSTRBYTE tmpnb = 0 ;
	char *charptr = NULL ;

	__n_assert( msg , return FALSE );

	charptr = msg -> data ;

	/* here we bypass the number of int, numebr of flt, number of str, (4+4+4) to get the
	 * first number which should be type */
	charptr += 3 * sizeof( NSTRBYTE );
	memcpy( &tmpnb , charptr , sizeof( NSTRBYTE ) ) ;
	tmpnb = ntohl( tmpnb );

	return tmpnb;
} /* netw_msg_get_type(...) */ 



void* manage_client( void *ptr )
{
	NETWORK *netw  = (NETWORK *)ptr ;
	N_STR *netw_exchange = NULL ;
	int state = 0 , thr_engine_state = 0 ;

	n_log( LOG_NOTICE , "manage_client started for netw %d" , netw -> link . sock );
	netw_start_thr_engine( netw );

	int DONE = 0 ;
	while( !DONE )
	{
		if( ( netw_exchange = netw_get_msg( netw ) ) )
		{
			N_STR *hostname = NULL , *data = NULL ;

			int type =  netw_msg_get_type( netw_exchange ) ;
			switch( type )
			{ 
				case NETMSG_DATA:
					get_net_datas( netw_exchange , &hostname , &data );
					if( hostname && hostname -> data && data && data -> data )
					{
						n_log( LOG_NOTICE , "RECV: %s: %s , %s" , netw -> link . ip , hostname -> data , data -> data );
						n_log( LOG_NOTICE , "Sending data back to emitter after sleeping 1 sec" );
						u_sleep( 1000000 );
						send_net_datas( netw , data );
					}
					else
					{
						n_log( LOG_ERR , "Error decoding request" );
					}
					break ;
				default:
					n_log( LOG_ERR , "Unknow message type %d" , type );
					DONE = 1 ;
					break ;
			}
			if( data )
				free_nstr( &data );
			if( hostname )
				free_nstr( &hostname );
			if( netw_exchange )
				free_nstr( &netw_exchange );
		}
		else
		{
			u_sleep( 500 );
		}
		netw_get_state( netw, &state , &thr_engine_state );
		if( (state&NETW_EXITED) || (state&NETW_ERROR ) || (state&NETW_EXIT_ASKED) )
			DONE = 1 ;
	}/* while( !DONE ) */

	netw_stop_thr_engine( netw );
	n_log( LOG_NOTICE , "manage_client stopped for netw %d" , netw -> link . sock );
	netw_wait_close( &netw );
	n_log( LOG_NOTICE , "network close" );

	return NULL;

} /* manage_client(...) */



/*!\fn int netw_send_ident( NETWORK *netw , int type , int id , N_STR *name , N_STR *passwd  )
 *\brief Add a formatted NETWMSG_IDENT message to the specified network
 *\param netw The aimed NETWORK where we want to add something to send
 *\param type type of identification ( NETW_IDENT_REQUEST , NETW_IDENT_NEW )
 *\param id The ID of the sending client
 *\param name Username
 *\param passwd Password
 *\return TRUE or FALSE
 */
int netw_send_ident( NETWORK *netw , int type , int id , N_STR *name , N_STR *passwd  )
{
	NETW_MSG *msg = NULL ;
	N_STR *tmpstr = NULL ;

	__n_assert( netw , return FALSE );

	create_msg( &msg );

	__n_assert( msg , return FALSE );

	add_int_to_msg( msg , type );
	add_int_to_msg( msg , id );

	add_nstrdup_to_msg( msg , name );
	add_nstrdup_to_msg( msg , passwd );

	tmpstr = make_str_from_msg( msg );
	delete_msg( &msg );
	__n_assert( tmpstr , return FALSE );

	return netw_add_msg( netw , tmpstr );
} /* netw_send_ident( ... ) */



/*!\fn netw_send_position( NETWORK *netw , int id , double X , double Y , double vx , double vy , double acc_x , double acc_y , int time_stamp )
 *\brief Add a formatted NETWMSG_IDENT message to the specified network
 *\param netw The aimed NETWORK where we want to add something to send
 *\param id The ID of the sending client
 *\param X X position inside a big grid
 *\param Y Y position inside a big grid
 *\param vx X speed
 *\param vy Y speed
 *\param acc_x Y acceleration
 *\param acc_y X acceleration
 *\param time_stamp Current Time when sending (for some delta we would want to compute )
 *\return TRUE or FALSE
 */
int netw_send_position( NETWORK *netw , int id , double X , double Y , double vx , double vy , double acc_x , double acc_y , int time_stamp )
{
	NETW_MSG *msg = NULL ;
	N_STR *tmpstr = NULL ;

	__n_assert( netw , return FALSE );

	create_msg( &msg );

	__n_assert( msg , return FALSE );

	add_int_to_msg( msg , NETMSG_POSITION );
	add_int_to_msg( msg , id );
	add_nb_to_msg( msg , X );
	add_nb_to_msg( msg , Y );
	add_nb_to_msg( msg , vx );
	add_nb_to_msg( msg , vy );
	add_nb_to_msg( msg , acc_x );
	add_nb_to_msg( msg , acc_y );
	add_int_to_msg( msg , time_stamp );

	tmpstr = make_str_from_msg( msg );
	delete_msg( &msg );
	__n_assert( tmpstr , return FALSE );

	return netw_add_msg( netw , tmpstr );
} /* netw_send_position( ... ) */



/*!\fn netw_send_string_to_all( NETWORK *netw , int id , char *name , char *chan , char *txt , int color )
 *\brief Add a string to the network, aiming all server-side users
 *\param netw The aimed NETWORK where we want to add something to send
 *\param id The ID of the sending client
 *\param name Name of user
 *\param chan Target Channel, if any. Pass "ALL" to target the default channel
 *\param txt The text to send
 *\param color The color of the text
 *\return TRUE or FALSE;
 */

int netw_send_string_to_all( NETWORK *netw , int id , char *name , char *chan , char *txt , int color )
{
	NETW_MSG *msg = NULL ;

	N_STR *namestr  = NULL ,
		  *chanstr  = NULL ,
		  *txtstr   = NULL ,
		  *tmpstr   = NULL ;

	__n_assert( netw , return FALSE );

	create_msg( &msg );
	__n_assert( msg , return FALSE );

	namestr = char_to_nstr( name );
	__n_assert( namestr , delete_msg( &msg ); return FALSE );

	chanstr = char_to_nstr( chan );
	__n_assert( chanstr , delete_msg( &msg ); free_nstr( &namestr ); return FALSE );

	txtstr = char_to_nstr( txt );
	__n_assert( txtstr , delete_msg( &msg ); free_nstr( &namestr ); free_nstr( &chanstr ); return FALSE );

	n_log( LOG_DEBUG , "send_string_to_all( %d:%d:%d , %s , %s , %s )" ,NETMSG_STRING,id,color, namestr -> data , chanstr -> data , txtstr -> data );

	add_int_to_msg( msg , NETMSG_STRING );
	add_int_to_msg( msg , id );
	add_int_to_msg( msg , color );

	add_nstrptr_to_msg( msg , namestr );
	add_nstrptr_to_msg( msg , chanstr );
	add_nstrptr_to_msg( msg , txtstr );

	tmpstr = make_str_from_msg( msg );

	delete_msg( &msg );

	__n_assert( tmpstr , return FALSE );

	return netw_add_msg( netw , tmpstr );
} /* netw_send_string_to_all( ... ) */



/*!\fn netw_send_string_to( NETWORK *netw , int id_from , int id_to , char *name , char *txt , int color )
 *\brief Add a string to the network, aiming a specific user
 *\param netw The aimed NETWORK where we want to add something to send
 *\param id_from The ID of the sending client
 *\param id_to The ID of the targetted client
 *\param name Sender Name
 *\param txt Sender text
 *\param color Sender text color
 *\return TRUE or FALSE
 */
int netw_send_string_to( NETWORK *netw , int id_from , int id_to , char *name , char *txt , int color )
{
	NETW_MSG *msg = NULL ;

	N_STR *namestr = NULL ,
		  *txtstr  = NULL ,
		  *tmpstr  = NULL ;

	__n_assert( netw , return FALSE );

	create_msg( &msg );

	__n_assert( msg , return FALSE );

	namestr = char_to_nstr( name );
	__n_assert( namestr , delete_msg( &msg ); return FALSE );

	txtstr = char_to_nstr( txt );
	__n_assert( txtstr , delete_msg( &msg ); free_nstr( &namestr ); return FALSE );

	add_int_to_msg( msg , NETMSG_STRING );
	add_int_to_msg( msg , id_from );
	add_int_to_msg( msg , id_to );
	add_int_to_msg( msg , color );


	add_nstrptr_to_msg( msg , namestr );
	add_nstrptr_to_msg( msg , txtstr );

	tmpstr = make_str_from_msg( msg );

	delete_msg( &msg );

	__n_assert( tmpstr , return FALSE );

	return netw_add_msg( netw , tmpstr );
} /* netw_send_string_to( ... ) */



/*!\fn netw_send_ping( NETWORK *netw , int id_from , int id_to , int time , int type )
 *\brief Add a ping reply to the network
 *\param netw The aimed NETWORK where we want to add something to send
 *\param id_from Identifiant of the sender
 *\param id_to Identifiant of the destination, -1 if the serveur itslef is targetted
 *\param time The time it was when the ping was sended
 *\param type NETW_PING_REQUEST or NETW_PING_REPLY
 *\return TRUE or FALSE
 */
int netw_send_ping( NETWORK *netw , int type , int id_from , int id_to , int time )
{
	NETW_MSG *msg = NULL ;

	N_STR *tmpstr = NULL ;

	__n_assert( netw , return FALSE );

	create_msg( &msg );

	__n_assert( msg , return FALSE );

	add_int_to_msg( msg , type );
	add_int_to_msg( msg , id_from );
	add_int_to_msg( msg , id_to );
	add_int_to_msg( msg , time );

	tmpstr = make_str_from_msg( msg );

	delete_msg( &msg );

	__n_assert( tmpstr , return FALSE );

	return netw_add_msg( netw , tmpstr );
} /* netw_send_ping( ... ) */


/*!\fn  int netw_get_ident( N_STR *msg , int *type ,int *ident , N_STR **name , N_STR **passwd  )
 *\brief Retrieves identification from netwmsg
 *\param msg The source string from which we are going to extract the data
 *\param type NETMSG_IDENT_NEW , NETMSG_IDENT_REPLY_OK , NETMSG_IDENT_REPLY_NOK , NETMSG_IDENT_REQUEST
 *\param ident ID for the user
 *\param name Name of the user
 *\param passwd Password of the user
 *\return TRUE or FALSE
 */
int netw_get_ident( N_STR *msg , int *type ,int *ident , N_STR **name , N_STR **passwd  )
{
	NETW_MSG *netmsg = NULL ;

	__n_assert( msg , return FALSE);

	netmsg = make_msg_from_str( msg );

	__n_assert( netmsg , return FALSE);

	get_int_from_msg( netmsg , &(*type) );
	get_int_from_msg( netmsg , &(*ident) );

	if( (*type) != NETMSG_IDENT_REQUEST && (*type) != NETMSG_IDENT_REPLY_OK && (*type) != NETMSG_IDENT_REPLY_NOK )
	{
		n_log( LOG_ERR, "unknow (*type) %d" , (*type) );
		delete_msg( &netmsg );
		return FALSE;
	}

	get_nstr_from_msg( netmsg , &(*name) );
	get_nstr_from_msg( netmsg , &(*passwd) );

	delete_msg( &netmsg );

	return TRUE;
} /* netw_get_ident( ... ) */



/*!\fn int netw_get_position( N_STR *msg , int *id , double *X , double *Y , double *vx , double *vy , double *acc_x , double *acc_y , int *time_stamp )
 *\brief Retrieves position from netwmsg
 *\param msg The source string from which we are going to extract the data
 *\param id
 *\param X X position inside a big grid
 *\param Y Y position inside a big grid
 *\param vx X speed
 *\param vy Y speed
 *\param acc_x X acceleration
 *\param acc_y Y acceleration
 *\param time_stamp Current Time when it was sended (for some delta we would want to compute )
 *\return TRUE or FALSE
 */
int netw_get_position( N_STR *msg , int *id , double *X , double *Y , double *vx , double *vy , double *acc_x , double *acc_y , int *time_stamp )
{
	NETW_MSG *netmsg = NULL ;

	int type = 0 ;

	__n_assert( msg , return FALSE );

	netmsg = make_msg_from_str( msg );

	__n_assert( netmsg , return FALSE );

	get_int_from_msg( netmsg , &type );

	if( type != NETMSG_POSITION )
	{
		delete_msg( &netmsg );
		return FALSE;
	}

	get_int_from_msg( netmsg , &(*id) );
	get_nb_from_msg( netmsg ,  &(*X) );
	get_nb_from_msg( netmsg ,  &(*Y) );
	get_nb_from_msg( netmsg ,  &(*vx) );
	get_nb_from_msg( netmsg ,  &(*vy) );
	get_nb_from_msg( netmsg ,  &(*acc_x) );
	get_nb_from_msg( netmsg ,  &(*acc_y) );
	get_int_from_msg( netmsg , &(*time_stamp) );

	delete_msg( &netmsg );

	return TRUE;
} /* netw_get_position( ... ) */



/*!\fn netw_get_string( N_STR *msg , int *id , N_STR **name , N_STR **chan , N_STR **txt , int *color )
 *\brief Retrieves string from netwmsg
 *\param msg The source string from which we are going to extract the data
 *\param id The ID of the sending client
 *\param name Name of user
 *\param chan Target Channel, if any. Pass "ALL" to target the default channel
 *\param txt The text to send
 *\param color The color of the text
 *
 *\return TRUE or FALSE
 */
int netw_get_string( N_STR *msg , int *id , N_STR **name , N_STR **chan , N_STR **txt , int *color )
{
	NETW_MSG *netmsg = NULL;

	int type = 0 ;

	__n_assert( msg , return FALSE );

	netmsg = make_msg_from_str( msg );

	__n_assert( netmsg , return FALSE );

	get_int_from_msg( netmsg , &type );

	if( type != NETMSG_STRING )
	{
		delete_msg( &netmsg );
		return FALSE;
	}

	get_int_from_msg( netmsg , id );
	get_int_from_msg( netmsg , color );

	get_nstr_from_msg( netmsg , &(*name) );
	get_nstr_from_msg( netmsg , &(*chan) );
	get_nstr_from_msg( netmsg , &(*txt) );

	delete_msg( &netmsg );

	return TRUE;

} /* netw_get_string( ... ) */



/*!\fn netw_get_ping( N_STR *msg , int *type , int *from , int *to , int *time )
 *\brief Retrieves a ping travel elapsed time
 *\param msg The source string from which we are going to extract the data
 *\param type NETW_PING_REQUEST or NETW_PING_REPLY
 *\param from Identifiant of the sender
 *\param to Targetted Identifiant, -1 for server ping
 *\param time The time it was when the ping was sended
 *
 *\return TRUE or FALSE
 */

int netw_get_ping( N_STR *msg , int *type , int *from , int *to , int *time )
{
	NETW_MSG *netmsg;

	__n_assert( msg , return FALSE );

	netmsg = make_msg_from_str( msg );

	__n_assert( netmsg , return FALSE );

	get_int_from_msg( netmsg , &(*type) );

	if( (*type) != NETMSG_PING_REQUEST && (*type) != NETMSG_PING_REPLY )
	{
		delete_msg( &netmsg );
		return FALSE;
	}

	get_int_from_msg( netmsg , &(*from) );
	get_int_from_msg( netmsg , &(*to) );
	get_int_from_msg( netmsg , &(*time) );


	delete_msg( &netmsg );

	return TRUE;
} /* netw_get_ping( ... ) */



