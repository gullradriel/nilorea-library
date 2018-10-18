/**\file ex_n_network.c
 *  Nilorea Library n_network api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */

#include "n_network.h"
#include "n_network_msg.h"
#include "ex_n_network.h"


NETWORK *server = NULL , /*! Network for server mode, accepting incomming */
        *netw = NULL   ; /*! Network for managing conenctions */

static pthread_t netw_thr ;


void process_args( int argc , char **argv , char **server , char **port )
{
   int getoptret = 0 , 
       log_level = LOG_ERR;  /* default log level */

   set_log_level( log_level );

   /* Arguments optionnels */
   /* -v version
    * -V log level
    * -h help 
    * -s serveur name/ip
    * -p port
    */

   if( argc == 1 )
   {
      fprintf( stderr ,  "No arguments given, help:\n" );
      fprintf( stderr , "     -v version\n"
            "     -V log level: NOLOG, LOG_INFO, LOG_NOTICE, LOG_ERR, LOG_DEBUG\n"
            "     -h help\n"
            "     -s serveur name/ip (client mode)\n"
            "     -p port\n" );
      exit( 1 );
   }


   while( ( getoptret = getopt( argc, argv, "hvs:V:p:" ) ) != EOF) 
   {
      switch( getoptret )
      {
         case 'v' :
            fprintf( stderr , "Date de compilation : %s a %s.\n" , __DATE__ , __TIME__ );
            exit( 1 );
         case 'V' :
            if( !strncmp( "NOLOG" , optarg, 5 ) ) 
            {
               log_level = NOLOG ;
            }
            else 
            {
               if( !strncmp( "LOG_NOTICE" , optarg , 6 ) ) 
               {
                  log_level = LOG_NOTICE;
               }
               else 
               {
                  if( !strncmp( "LOG_INFO" , optarg, 7 ) ) 
                  {
                     log_level = LOG_INFO;
                  }
                  else 
                  {
                     if( !strncmp( "LOG_ERR" , optarg , 5 ) ) 
                     {
                        log_level = LOG_ERR;
                     }
                     else 
                     {
                        if( !strncmp( "LOG_DEBUG" , optarg , 5 ) ) 
                        {
                           log_level = LOG_DEBUG;
                        }
                        else 
                        {
                           fprintf( stderr , "%s n'est pas un niveau de log valide.\n" , optarg );
                           exit( -1 );
                        }
                     }
                  }
               }
            }
            break;
         case 's' :
            (*server) = strdup( optarg ); 
            break ;
         case 'p' :
            (*port) = strdup( optarg ); 
            break ;
         default :   
         case '?' :
            if( optopt == 'V' )
            {
               fprintf( stderr , "\n      Missing log level\n" );
            }else if( optopt == 'p' )
            {
               fprintf( stderr , "\n      Missing port\n" );
            }else if( optopt != 's' )
            {
               fprintf( stderr , "\n      Unknow missing option %c\n" , optopt );
            }
         case 'h' :
            fprintf( stderr , "     -v version\n"
                  "     -V log level: NOLOG, LOG_INFO, LOG_NOTICE, LOG_ERR, LOG_DEBUG\n"
                  "     -h help\n"
                  "     -s serveur name/ip (client mode)\n"
                  "     -p port\n" );
            exit( 1 );
      }
   }
   set_log_level( log_level );
} /* void process_args( ... ) */


#define SERVER 0
#define CLIENT 1

int mode = -1 ;

int main(int argc, char **argv) {

   char *srv = NULL ;
   char *port = NULL ;

   /* processing args and set log_level */
   process_args( argc, argv , &srv , &port );

   if( !port )
   {
      n_log( LOG_ERR , "No port given. Exiting." );
      exit( -1 );
   }

   if( srv )
   {
      n_log( LOG_INFO , "Client mode, connecting to %s:%s" , srv , port );
      mode = CLIENT ;
   }
   else
   {
      n_log( LOG_INFO , "Server mode , waiting client on port %s" , port );
      mode = SERVER ;
   }

   if( mode == SERVER )
   {
      struct sigaction sa;
      sa.sa_handler = sigchld_handler; // reap all dead processes
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = SA_RESTART;
      if (sigaction(SIGCHLD, &sa, NULL) == -1) {
         perror("sigaction");
         exit(1);
      }


      /* create listening network */
      if( netw_make_listening( &server , port , 10 ) == FALSE )
      {
         n_log( LOG_ERR , "Fatal error with network initialization" );
         exit( -1 );
      }

      /* get any accepted client on a network */
      if ( !( netw = netw_accept_from( server ) ) )
      {
         n_log( LOG_ERR , "Error on accept" );
      }
      else
      {
         /* someone is connected. starting some dialog */
         if( pthread_create ( &netw_thr , NULL, manage_client , (void *)netw ) != 0 )
         {
            n_log( LOG_ERR , "Error creating client management thread" );
            exit( -1 );
         }
      }

      pthread_join( netw_thr , NULL );

      netw_close( &server );

   }
   else if( mode == CLIENT )
   {
      if( netw_connect( &netw , srv , port ) != TRUE )
      {
         /* there were some error when trying to connect */
         n_log( LOG_ERR , "Unable to connect to %s:%s" , server , port );
         exit( 1 );
      }

      /* backgrounding network send / recv */
      netw_start_thr_engine( netw );

      N_STR *sended_data = NULL , *recved_data = NULL , *hostname = NULL , *tmpstr = NULL ;

      sended_data = char_to_nstr( "TEST ENVOI DE DONNEES" );
      send_net_datas( netw , sended_data );

      free_nstr( &sended_data );

      /* let's check for an answer: test each 250000 usec, with
       * a limit of 1000000 usec */
      tmpstr = netw_wait_msg( netw , 25000 , 10000000 );

      if( tmpstr )
      {
         get_net_datas( tmpstr , &hostname , &recved_data );
         free_nstr( &tmpstr );
         free_nstr( &recved_data );
         free_nstr( &hostname );
      }
      else
      {
         n_log( LOG_ERR , "Error getting back answer from server" );
      }

      netw_stop_thr_engine( netw );
      netw_close( &netw );
   }

   FreeNoLog( srv );
   FreeNoLog( port )

      exit( 0 );
} /* END_OF_MAIN() */
