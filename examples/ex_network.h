/**\file ex_network.h
 *  Nilorea Library n_network api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */

#define NETMSG_DATA 1

#include <sys/time.h>


#ifdef LINUX
void sigchld_handler()
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
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
   netw_close( &netw );

   /* pthread_exit( NULL ); */

      return NULL;

} /* manage_client(...) */
