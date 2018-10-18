/**\file exserver.c
*
*  Nilorea Server main file
*
*\author Castagnier Mickaël
*
*\version 1.0
*
*\date 16/09/05
*
*/

#define ALLEGRO_USE_CONSOLE
#define _SERVER_ "1.0 b"



#include <nilorea.h>     /* NILOREA ENGINE HEADER */


NETWORK *server = NULL ,
	    *newclient = NULL ;

N_USER USERLIST;

static pthread_t th1;


void *client( void *CClient );



int MAX_CLIENTS = 0 ,       /* nb max client for the server                         */
    RESERVED = 0 ,         /* nb reserved admin connection                         */
    PORT = 0 ,            /* client connection port                               */
    ADMPORT = 0 ,        /* admin connection port                                */
    LOGGING = 0 ,       /* State of LOGGING, 1-ON 2-OFF                         */
    sendlimit = 0 ,    /* Limit of Octsended/sec for each client, 0 -> disable */
    STATE = RUNNING , /* State of the server                                  */
    IT = 0;          /* Iterator                                             */

	char *worldmap = NULL ;
	
int main( void )
{
	allegro_init();
	install_timer();	

	set_log_level( LOG_DEBUG );

  /*ShowWindow(win_get_window(),SW_HIDE);*/

  n_log( LOG_INFO ,"int size: %d , double size: %d\n" , sizeof( int ) , sizeof( double ) );

  n_log( LOG_INFO ,"Loading settings from file serveur.cfg . . .\n\n" );

  load_server_config( "serveur.cfg" ,
                      &MAX_CLIENTS , &RESERVED , &PORT , &ADMPORT ,
                      &LOGGING , &sendlimit , &worldmap );

  n_log( LOG_INFO ,"\nNetwork Config Loaded !\n" );

  if (
    save_server_config( "serveur.cfg" ,
                        MAX_CLIENTS , RESERVED , PORT , ADMPORT ,
                        LOGGING , sendlimit , worldmap ) == TRUE )
    n_log( LOG_INFO ,"\nNetwork Config Saved !\n" );
  else
    n_log( LOG_INFO ,"\nNetwork Config Creating-saving FAILED ! FATAL ERROR! EXITING NOW !\n" );


   n_log( LOG_INFO ,"Creating server:\nPort %d\nAdmin port %d\nMax clients %d\nReserved: %d\nLogging %d\nSend limit %d\n\n",
          PORT , ADMPORT , MAX_CLIENTS , RESERVED , LOGGING , sendlimit );



  /*
   * Initialisation
   */


  if( create_list_of_user( &USERLIST , MAX_CLIENTS ) == FALSE )
        {
        n_log( LOG_INFO , "Error initializing USERLIST\n" );
        exit( FALSE );
        }


  if ( Init_All_Network( 2, 2 ) == FALSE )
    {
      n_log( LOG_INFO , "Error initializing network\n" );
      exit( FALSE );
    }

  else
    n_log( LOG_INFO ,"Network Initialised\n" );

  if ( Make_Listening_Network( &server , PORT , 10 ) == FALSE )
    {
      n_log( LOG_INFO ,"crashed on making listening socket\n" );
      exit( FALSE );
    }

  else
    n_log( LOG_INFO ,"Listening Network created\n" );

  do
    {

      n_log( LOG_INFO ,"Waiting for new connection ...\n" );

      if ( !( newclient = Accept_From_Network( server ) ) )
        {
          n_log( LOG_INFO , "Error on accept\n" );
        }

      else
        {
           n_log( LOG_INFO ,"Connection from %s accepted\n" , inet_ntoa( newclient -> link . raddr . sin_addr ) );
          if( pthread_create ( &th1, NULL, client, ( void * ) newclient ) == 0 )
			n_log( LOG_INFO ,"Thread Created\n" );
		  else
			n_log( LOG_INFO ,"Error when creating main client thread\n");
        }

    }

  while ( STATE == RUNNING );

  /*
   * Exiting
   */

    allegro_exit();

  return TRUE;


}END_OF_MAIN()








void *client( void *CClient )
{

  NETWORK * newnet = NULL ;

	int state = TRUE ,
		id = -1;
	  
	int   d_id = 0 ;

  N_STR *tmpstr = NULL , *in_msg = NULL ;


  n_log( LOG_INFO ,"New thread\n" );
  
  if( Create_Send_Thread( newnet ) == FALSE )
	TRACE("Error creating sending thread\n");
  if( Create_Recv_Thread( newnet ) == FALSE )
    TRACE("Error creating receiving thread\n");

  id = add_user( &USERLIST );

  n_log( LOG_INFO ,"User added with id %d\n" , id );

  pthread_mutex_lock( &USERLIST . user_bolt );

  newnet = ( NETWORK * ) CClient;
  USERLIST . list [id] . netw = newnet;

  pthread_mutex_unlock( &USERLIST . user_bolt );
  
	/* wait for ident msg */
	do
	{
		in_msg = Get_Msg( newnet );
    	rest( 10 );
    }while( !in_msg ); 
	
	n_log( LOG_NOTICE , "received %d octets" , in_msg -> length );

    if( in_msg && ( netw_msg_get_type( in_msg ) == NETMSG_IDENT_REQUEST ) )
    {
		N_STR *name = NULL , *passwd = NULL ;
		int  d_it = 0 , ident = 0 ;
	    netw_get_ident( in_msg , &d_it , &ident , &name , &passwd );
		netw_send_ident( newnet , NETMSG_IDENT_REPLY_OK , d_id , name , passwd );
		n_log( LOG_NOTICE , "Ident %d sended" , id);
    }
    else
	{
		n_log( LOG_ERR , "ident error. in_msg: %p , %d" , in_msg , in_msg?netw_msg_get_type( in_msg ):-1 );
        goto exit_client;
	}
	free_str( &in_msg );	
	
	
  do{

	tmpstr = Get_Msg( newnet );
	
	if( tmpstr )
	{
		add_msg_to_all( &USERLIST , tmpstr );
		free_str( &tmpstr );
		rest(0);
	}else
		rest( 10 );

	pthread_mutex_lock( &newnet -> eventbolt );
	if( newnet -> state == NETW_QUIT )
		state = FALSE;
	pthread_mutex_unlock( &newnet -> eventbolt );

  }while( state == TRUE );

  
  exit_client : 
  Stop_Network( &USERLIST . list [id] . netw );
  Close_Network( &USERLIST . list [id] . netw );

  n_log( LOG_INFO ,"EXIT Status: %d\n", state );

  rem_user( &USERLIST , id );

  n_log( LOG_INFO ,"Connection with id %d closed\n" , id );

  return NULL;

}
