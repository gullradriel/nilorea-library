/**\file n_user.c
*
*  USERS fonctions
*
*  USERS handling for everything you want
*
*\author Castagnier Mickael
*
*\version 1.0
*
*\date 20/02/2006
*
*/



#include "nilorea/n_user.h"


int create_list_of_user( N_USER *list, int max )
{

    int IT=0;

    Malloc( list -> list, N_USER_BOX, max );

    if( !list -> list )
        return FALSE;

    list -> max = max ;
    list -> highter = 0;

    for( IT = 0; IT < max ; IT ++)
    {
        list-> list[IT].state=0 ;
        list-> list[IT].netw = NULL;
    }

    /*
             *  CONFIGURATION
             */
    pthread_mutex_init( &list -> user_bolt, NULL );

    return TRUE;

}



int add_user( N_USER *list )
{

    int it = -1;

    pthread_mutex_lock( &list -> user_bolt );

    do
    {

        it++;

    }
    while( list -> list[ it ] . state != 0);

    list -> list[ it ] . state = 1;

    if( it > list -> highter ) list -> highter = it;

    pthread_mutex_unlock( &list -> user_bolt );

    return it;

}



int rem_user( N_USER *list,int id )
{

    pthread_mutex_lock( &list -> user_bolt );

    if( list -> list[ id ] . state == 0 )
        return FALSE;

    if( list -> list[ id ] . netw )
        netw_close( &list -> list[ id ] . netw );

    list -> list[ id ] . state = 0;

    if( id == list -> highter ) list -> highter --;

    pthread_mutex_unlock( &list -> user_bolt );

    return TRUE;

}



int add_msg_to_all( N_USER *list, N_STR *msg )
{

    int it;

    pthread_mutex_lock( &list -> user_bolt );

    for( it = 0 ; it < list -> max ; it ++ )
    {

        if( list -> list[ it ] . state != 0 )
            netw_add_msg( list -> list[ it ] . netw, msg );
    }

    pthread_mutex_unlock( &list -> user_bolt );

    return TRUE;

}



int add_msg_to_user( N_USER *list, int id, N_STR *msg )
{

    pthread_mutex_lock( &list -> user_bolt );

    if( list -> list[ id ] . state != 0 )
        netw_add_msg( list -> list[ id ] . netw, msg );

    pthread_mutex_unlock( &list -> user_bolt );

    return TRUE;

}
