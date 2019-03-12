/**\file n_network_msg.c
 *  network messages function
 *  Network Engine
 *\author Castagnier Mickael
 *\version 1.0
 *\date 10/05/2005
 */
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_network_msg.h"

/*!\fn static double double_swap( double value )
 *\brief Swap bytes endiannes for a double
 *\param value The double to swap
 *\return A swapped double
 */
double double_swap( double value )
{
    double swaped = 0 ;
    unsigned char *src = (unsigned char *)&value ;
    unsigned char *dst = (unsigned char *)&swaped ;

    /* swap bytes */
    dst[0] = src[7];
    dst[1] = src[6];
    dst[2] = src[5];
    dst[3] = src[4];
    dst[4] = src[3];
    dst[5] = src[2];
    dst[6] = src[1];
    dst[7] = src[0];

    return swaped ;
} /* double_swap() */



/*!\fn static double htond( double value )
 *\brief If needed swap bytes for a double
 *\param value The value to eventually swap
 *\return The modified value
 */
double htond( double value )
{
    double ret = value ;
    /* if byte order is little like x86, PC, ... we swap to network notation */
#if __BYTE_ORDER == __LITTLE_ENDIAN
    ret = double_swap( value );
#endif
    return ret ;
}



/*!\fn static double ntohd( double value )
 *\brief If needed swap bytes for a double
 *\param value The value to eventually swap
 *\return The modified value
 */
double ntohd( double value )
{
    double ret = value ;
    /* if byte order is little like x86, PC, ... we swap to network notation */
#if __BYTE_ORDER == __LITTLE_ENDIAN
    ret = double_swap( value );
#endif
    return ret ;
}



/*!\fn create_msg( NETW_MSG **msg )
 *\brief Create a NETW_MSG *object
 *\param msg A NETW_MSG *object
 *\return TRUE or FALSE
 */
int create_msg( NETW_MSG **msg )
{
    if( (*msg) )
    {
        n_log( LOG_ERR, "create_msg destination is valid and should be NULL !" );
        return FALSE;
    }

    Malloc( (*msg), NETW_MSG, 1 );
    __n_assert( (*msg), return FALSE );

    (*msg) -> tabstr = new_generic_list( -1 );
    __n_assert( (*msg) -> tabstr, return FALSE );
    (*msg) -> tabint = new_generic_list( -1 );
    __n_assert( (*msg) -> tabint, return FALSE );
    (*msg) -> tabflt = new_generic_list( -1 );
    __n_assert( (*msg) -> tabflt, return FALSE );

    return TRUE;
} /* create_msg( ... ) */



/*!\fn delete_msg( NETW_MSG **msg )
 *\brief Delete a NETW_MSG *object
 *\param msg A NETW_MSG *object to delete
 *\return TRUE or FALSE
 */
int delete_msg( NETW_MSG **msg )
{
    if( (*msg) )
    {
        if( (*msg) -> tabint )
        {
            list_destroy( &(*msg) -> tabint );
        }
        if( (*msg) -> tabflt )
        {
            list_destroy( &(*msg) -> tabflt );
        }
        if( (*msg) -> tabstr )
        {
            list_destroy( &(*msg) -> tabstr );
        }
        Free( (*msg) );
    }
    else
    {
        n_log( LOG_ERR, "(*msg) already NULL" );
        return FALSE ;
    }
    return TRUE;
} /* delete_msg */



/*!\fn empty_msg( NETW_MSG **msg )
 *\brief Empty a NETW_MSG *object
 *\param msg A NETW_MSG **object
 *\return TRUE or FALSE
 */
int empty_msg( NETW_MSG **msg )
{
    __n_assert( (*msg), return FALSE );

    list_empty( (*msg) -> tabint );
    list_empty( (*msg) -> tabflt );
    list_empty( (*msg) -> tabstr );

    return TRUE;
} /* empty_msg(...) */



/*!\fn add_nb_to_msg( NETW_MSG *msg , double value )
 *\brief Add an float to the message
 *\param msg A NETW_MSG *object to complete with the value
 *\param value The value to add to the message
 *\warning The floating value will be rounded by the network serialization !
 *\return TRUE or FALSE
 */
int add_nb_to_msg( NETW_MSG *msg, double value )
{
    double *new_val = NULL ;

    __n_assert( msg, return FALSE );

    Malloc( new_val, double, 1 );
    __n_assert( new_val, return FALSE );

    new_val[ 0 ] = value ;

    return list_push( msg -> tabflt, new_val, free );
} /* add_nb_to_msg( ... ) */



/*!\fn add_int_to_msg( NETW_MSG *msg , int value )
 *\brief Add an int to the int list int the message
 *\param msg A NETW_MSG *object to complete with the value
 *\param value The value to add to the message
 *\return TRUE or FALSE
 */
int add_int_to_msg( NETW_MSG *msg, int value )
{
    int *new_val = NULL ;

    __n_assert( msg, return FALSE );

    Malloc( new_val, int, 1 );
    __n_assert( new_val, return FALSE );

    new_val[ 0 ] = value ;

    return list_push( msg -> tabint, new_val, free );
} /* add_int_to_msg( ... ) */



/*!\fn add_nstrptr_to_msg( NETW_MSG *msg , N_STR *str)
 *\brief Add a string to the string list in the message
 *\param msg A NETW_MSG *object to complete with the string
 *\param str The string to add to the message
 *\return TRUE or FALSE
 */
int add_nstrptr_to_msg( NETW_MSG *msg, N_STR *str )
{
    __n_assert( msg, return FALSE );
    __n_assert( str, return FALSE );
    __n_assert( str -> data, return FALSE );
    return list_push( msg -> tabstr, str, free_nstr_ptr );
} /* add_nstrptr_to_msg(...) */



/*!\fn add_nstrdup_to_msg( NETW_MSG *msg , N_STR *str)
 *\brief Add a copy of str to the string list in the message
 *\param msg A NETW_MSG *object to complete with the string
 *\param str The string to add to the message
 *\return TRUE or FALSE
 */
int add_nstrdup_to_msg( NETW_MSG *msg, N_STR *str )
{
    __n_assert( msg, return FALSE );
    __n_assert( str, return FALSE );
    __n_assert( str -> data, return FALSE );

    return list_push( msg -> tabstr, nstrdup( str ), free_nstr_ptr );
} /* add_nstrdup_to_msg(...) */




/*!\fn add_strdup_to_msg( NETW_MSG *msg , char *str)
 *\brief Add a copy of char *str to the string list in the message
 *\param msg A NETW_MSG *object to complete with the string
 *\param str The string to add to the message
 *\return TRUE or FALSE
 */
int add_strdup_to_msg( NETW_MSG *msg, char *str )
{
    __n_assert( msg, return FALSE );
    __n_assert( str, return FALSE );

    N_STR *dup = char_to_nstr( str );
    __n_assert( dup, return FALSE );

    return list_push( msg -> tabstr, dup, free_nstr_ptr );
} /* add_strdup_to_msg(...) */




/*!\fn int get_nb_from_msg( NETW_MSG *msg , double *value )
 *\brief Get a number from a message number list
 *\param msg A NETW_MSG *object for extracting number
 *\param value A int value where you want to store the result
 *\return A pointer to the N_STR, Do not forget to free it !
 */
int get_nb_from_msg( NETW_MSG *msg, double *value )
{
    double *val = NULL ;

    __n_assert( msg, return FALSE );

    val = list_shift( msg -> tabflt, double );
    if( val )
    {
        (*value) = val[ 0 ] ;
        Free( val );
        return TRUE;
    }
    return FALSE;
} /* get_nb_from_msg(...) */



/*!\fn get_int_from_msg( NETW_MSG *msg , int *value )
 *\brief Get a number from a message number list
 *\param msg A NETW_MSG *object for extracting number
 *\param value A int value where you want to store the result
 *\return A pointer to the N_STR, Do not forget to free it !
 */
int get_int_from_msg( NETW_MSG *msg, int *value )
{
    int *val = NULL ;

    __n_assert( msg, return FALSE );

    val = list_shift( msg -> tabint, int );
    if( val )
    {
        (*value) = val[ 0 ];
        Free( val );
        return TRUE;
    }
    return FALSE;

} /* get_int_from_msg(...) */



/*!\fn get_nstr_from_msg( NETW_MSG *msg , N_STR **value )
 *\brief Get a string from a message string list
 *\param msg The NETW_MSG where you have a str to get
 *\param value A pointer to a NULL N_STR for storage
 *\return TRUE or FALSE
 */
int get_nstr_from_msg( NETW_MSG *msg, N_STR **value )
{
    N_STR *val = NULL ;

    __n_assert( msg, return FALSE );

    val = list_shift( msg -> tabstr, N_STR );
    if( val )
    {
        if( (*value) )
        {
            n_log( LOG_ERR, "Previous pointer value %p overriden by pointer %p", (*value), val );
        }
        (*value) = val ;
        return TRUE;
    }
    return FALSE;
} /* get_nstr_from_msg(...) */



/*!\fn get_str_from_msg( NETW_MSG *msg , char **value )
 *\brief Get a string from a message string list
 *\param msg The NETW_MSG where you have a str to get
 *\param value A pointer to a NULL N_STR for storage
 *\return TRUE or FALSE
 */
int get_str_from_msg( NETW_MSG *msg, char **value )
{
    N_STR *val = NULL ;

    __n_assert( msg, return FALSE );

    val = list_shift( msg -> tabstr, N_STR );
    if( val )
    {
        if( (*value) )
        {
            n_log( LOG_ERR, "Previous pointer value %p overriden by pointer %p", (*value), val );
        }

        (*value) = val -> data ;
        Free( val );
        return TRUE;
    }
    return FALSE;
} /* get_str_from_msg(...) */



/*!\fn make_str_from_msg( NETW_MSG *msg )
 *\brief Make a single string of the message
 *\param msg A NETW_MSG structure to convert into a N_STR one
 *\return A pointer to the generated N_STR, do not forget to free it after use !
 */
N_STR *make_str_from_msg( NETW_MSG *msg )
{
    int str_length = 0  ;
    LIST_NODE *node = NULL ;

    N_STR *strptr = NULL,
           *generated_str = NULL ;

    char *charptr = NULL ;

    __n_assert( msg, return NULL );

    /* the first thing to do is to computer the resulting string size
       the first 12 octets are for the number of ints , floats, strings in the message
       */
    str_length = 3 * sizeof( NSTRBYTE ) + ( msg -> tabint -> nb_items * sizeof( NSTRBYTE ) ) + ( msg -> tabflt -> nb_items * sizeof( double ) ) ;

    /* let's count the eventual string blocks */
    node = msg -> tabstr -> start ;
    while( node )
    {
        strptr = (N_STR *)node -> ptr ;
        str_length += sizeof( NSTRBYTE ) + sizeof( NSTRBYTE ) + strptr -> written ;
        node = node -> next ;
    }

    /* preparing the new string */
    Malloc( generated_str, N_STR, 1 );
    __n_assert( generated_str, return NULL );
    Malloc( generated_str -> data, char, str_length + 1 );
    __n_assert( generated_str -> data, return NULL );

    generated_str -> length = str_length + 1 ;
    generated_str -> written = str_length ;


    /* copying header */
    charptr = generated_str -> data ;

    /* byte order */
    msg -> tabint -> nb_items = htonl( msg -> tabint -> nb_items );
    msg -> tabflt -> nb_items = htonl( msg -> tabflt -> nb_items );
    msg -> tabstr -> nb_items = htonl( msg -> tabstr -> nb_items );

    /* number of ints inside the message */
    memcpy ( charptr, &msg -> tabint -> nb_items, sizeof( NSTRBYTE ) );
    charptr += sizeof( NSTRBYTE ) ;
    /* number of doubles inside the message */
    memcpy ( charptr, &msg -> tabflt -> nb_items, sizeof( NSTRBYTE ) );
    charptr += sizeof( NSTRBYTE ) ;
    /* number of string inside the message */
    memcpy ( charptr, &msg -> tabstr -> nb_items, sizeof( NSTRBYTE ) );
    charptr += sizeof( NSTRBYTE ) ;

    /*reversing byte order to default host order */
    msg -> tabint -> nb_items = ntohl( msg -> tabint -> nb_items );
    msg -> tabflt -> nb_items = ntohl( msg -> tabflt -> nb_items );
    msg -> tabstr -> nb_items = ntohl( msg -> tabstr -> nb_items );

    /* if there are numbers, copy all them ! */
    node = msg -> tabint -> start ;
    while( node )
    {
        NSTRBYTE *nbptr = (NSTRBYTE *)node -> ptr ;
        int val = htonl( nbptr[ 0 ] );
        memcpy( charptr, &val, sizeof( NSTRBYTE ) );
        charptr += sizeof( NSTRBYTE ) ;
        node = node -> next ;
    }

    /* if there are numbers, copy all them ! */
    node = msg -> tabflt -> start ;
    while( node )
    {
        double *nbptr = (double *)node -> ptr ;
        double val = htond( nbptr[ 0 ] );
        memcpy( charptr, &val, sizeof( double ) );
        charptr += sizeof( double ) ;
        node = node -> next ;
    }

    /* if there are strings, copy all them ! */
    if( msg -> tabstr -> nb_items > 0 )
    {
        node = msg -> tabstr -> start ;
        while( node )
        {
            strptr = (N_STR *)node -> ptr ;

            strptr -> length = htonl( strptr -> length );
            memcpy( charptr, &strptr -> length, sizeof( NSTRBYTE ) );
            charptr += sizeof( NSTRBYTE );
            strptr -> length = ntohl( strptr -> length );

            strptr -> written = htonl( strptr -> written );
            memcpy( charptr, &strptr -> written, sizeof( NSTRBYTE ) );
            charptr += sizeof( NSTRBYTE );
            strptr -> written = ntohl( strptr -> written );

            memcpy( charptr, strptr -> data, strptr -> written );
            charptr += strptr -> written ;
            node = node -> next ;
        }

    }

    return generated_str;
} /* make_str_from_msg(...) */



/*!\fn make_msg_from_str( N_STR *str )
 *\brief Make a single message of the string
 *\param str A N_STR structure to convert into a NETW_MSG one
 *\return A pointer to the generated NETW_MSG, do not forget to free it after use !
 */
NETW_MSG *make_msg_from_str( N_STR *str )
{
    NETW_MSG *generated_msg = NULL ;
    N_STR    *tmpstr = NULL;

    char     *charptr = NULL;

    NSTRBYTE it,
             nb_int = 0,
             nb_flt = 0,
             nb_str = 0,
             tmpnb = 0 ;

    double tmpflt = 0 ;

    __n_assert( str, return NULL );

    charptr = str -> data;

    create_msg( &generated_msg );

    __n_assert( generated_msg, return NULL );

    memcpy( &nb_int, charptr, sizeof( NSTRBYTE ) );
    charptr += sizeof( NSTRBYTE ) ;
    nb_int = ntohl( nb_int );

    memcpy( &nb_flt, charptr, sizeof( NSTRBYTE ) );
    charptr += sizeof( NSTRBYTE ) ;
    nb_flt = ntohl( nb_flt );

    memcpy( &nb_str, charptr, sizeof( NSTRBYTE ) );
    charptr += sizeof( NSTRBYTE ) ;
    nb_str = ntohl( nb_str );

    if( nb_int > 0 )
    {
        for( it = 0 ; it < nb_int ; it ++ )
        {
            memcpy( &tmpnb, charptr, sizeof( NSTRBYTE ) ) ;
            tmpnb = ntohl( tmpnb );
            charptr += sizeof( NSTRBYTE ) ;
            add_int_to_msg( generated_msg, tmpnb );
        }
    }

    if( nb_flt > 0 )
    {
        for( it = 0 ; it < nb_flt ; it ++ )
        {
            memcpy( &tmpflt, charptr, sizeof( double ) ) ;
            tmpflt = ntohd( tmpflt );
            charptr += sizeof( double ) ;
            add_nb_to_msg( generated_msg, tmpflt );
        }
    }


    if( nb_str > 0 )
    {
        for( it = 0 ; it < nb_str ; it ++ )
        {

            Malloc( tmpstr, N_STR, 1 );
            __n_assert( tmpstr, return NULL );

            memcpy( &tmpstr -> length, charptr, sizeof( NSTRBYTE ) );
            charptr += sizeof( NSTRBYTE ) ;
            memcpy( &tmpstr -> written, charptr, sizeof( NSTRBYTE ) );
            charptr += sizeof( NSTRBYTE ) ;

            tmpstr -> length  = ntohl( tmpstr -> length );
            tmpstr -> written = ntohl( tmpstr -> written );

            Malloc( tmpstr -> data, char, tmpstr -> length );
            __n_assert( tmpstr -> data, return NULL );

            memcpy( tmpstr -> data, charptr, tmpstr -> written );
            charptr += tmpstr -> written ;

            add_nstrptr_to_msg( generated_msg, tmpstr );
        }
    }
    return generated_msg ;
} /* make_msg_from_str(...) */

