/**\example ex_hash.c Nilorea Library hash table api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */


#include "nilorea/n_str.h"
#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"

#define NB_ELEMENTS 16

/*! string and int holder */
typedef struct DATA
{
    /*! string holder */
    N_STR *rnd_str ;
    /*! int value */
    int value ;
} DATA ;

/*!\fn void destroy_data( void *ptr )
 *\brief destroy a DATA struct
 *\param ptr A pointer to a DATA struct
 */
 void destroy_data( void *ptr )
{
    DATA *data = (DATA *)ptr ;
    free_nstr( &data -> rnd_str );
    Free( data );
}

int main( void )
{

    set_log_level( LOG_DEBUG );

    HASH_TABLE *htable = new_ht( NB_ELEMENTS );
    LIST *keys_list = new_generic_list( NB_ELEMENTS + 1 );
    DATA *data = NULL ;
    char *str = NULL ;

    n_log( LOG_INFO, "Filling HashTable with %d random elements", NB_ELEMENTS );
    for( int it = 0 ; it < NB_ELEMENTS ; it ++ )
    {
        N_STR *nkey = NULL ;
        int randomizator = rand()%5;
        nstrprintf( nkey, "key%d_%d", it, randomizator );
        switch( randomizator )
        {

        default:
        case 0:
            ht_put_int( htable, _nstr( nkey ), 666 );
            n_log( LOG_INFO, "Put int 666 with key %s", _nstr( nkey ) );
            break;
        case 1:
            ht_put_double( htable, _nstr( nkey ), 3.14 );
            n_log( LOG_INFO, "Put double 3.14 with key %s", _nstr( nkey ) );
            break;
        case 2:
            Malloc( data, DATA, 1 );
            data -> rnd_str = NULL ;
            nstrprintf( data -> rnd_str, "%s%d", _nstr( nkey ), rand()%10 );
            data -> value = 7 ;
            ht_put_ptr( htable, _nstr( nkey ), data, &destroy_data );
            n_log( LOG_INFO, "Put ptr rnd_str %s value %d with key %s", data -> rnd_str, data -> value, _nstr( nkey ) );
            break;
        case 3:
            Malloc( str, char, 64 );
            sprintf( str, "%s%d", _nstr( nkey ), rand()%10 );
            ht_put_string( htable, _nstr( nkey ), str );
            n_log( LOG_INFO, "Put string %s key %s", str, _nstr( nkey ) );
            Free( str );
            break;
        }
        /* asving key for ulterior use */
        list_push( keys_list, nkey, free_nstr_ptr );
    }


    n_log( LOG_INFO, "Reading hash table with saved keys" );
    list_foreach( node, keys_list )
    {
        N_STR *nkey = (N_STR *)node -> ptr ;
        HASH_NODE *htnode = ht_get_node( htable, _nstr( nkey ) );
        switch( htnode -> type )
        {
        case HASH_INT:
            n_log( LOG_INFO, "Get int %d with key %s", htnode -> data . ival,  _nstr( nkey ) );
            break ;
        case HASH_DOUBLE:
            n_log( LOG_INFO, "Get double %g with key %s", htnode -> data . fval,  _nstr( nkey ) );
            break ;
        case HASH_PTR:
            n_log( LOG_INFO, "Get rnd_str %s value %d with key %s", ((DATA *)htnode -> data . ptr)->rnd_str,  ((DATA *)htnode -> data . ptr)->value, _nstr( nkey ) );
            break ;
        case HASH_STRING:
            n_log( LOG_INFO, "Get string %s with key %s", htnode -> data . string, _nstr( nkey ) );
            break ;
        default:
            n_log( LOG_INFO, "Get unknwow node key %s", _nstr( nkey ) );
            break ;
        }
    }

    n_log( LOG_INFO, "Reading hash table with ht_foreach" );
    ht_foreach( node, htable )
    {
        HASH_NODE *htnode = (HASH_NODE *)node -> ptr ;
        switch( htnode -> type )
        {
        case HASH_INT:
            n_log( LOG_INFO, "Get int %d with key %s", htnode -> data . ival, htnode -> key );
            break ;
        case HASH_DOUBLE:
            n_log( LOG_INFO, "Get double %g with key %s", htnode -> data . fval, htnode -> key );
            break ;
        case HASH_PTR:
            n_log( LOG_INFO, "Get rnd_str %s value %d with key %s", ((DATA *)htnode -> data . ptr)->rnd_str,  ((DATA *)htnode -> data . ptr)->value,  htnode -> key );
            break ;
        case HASH_STRING:
            n_log( LOG_INFO, "Get string %s with key %s", htnode -> data . string, htnode -> key );
            break ;
        default:
            n_log( LOG_INFO, "Get unknwow node key %s",  htnode -> key );
            break ;
        }
    }

    list_destroy( &keys_list );
    destroy_ht( &htable );
    exit( 0 );

} /* END_OF_MAIN */

