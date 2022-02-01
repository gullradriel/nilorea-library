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
		int randomizator = rand()%4;
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
				n_log( LOG_INFO, "Put ptr rnd_str %s value %d with key %s", _nstr( data -> rnd_str ) , data -> value, _nstr( nkey ) );
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

	n_log( LOG_INFO, "Reading hash table with ht_foreach" );
	HT_FOREACH( node, htable , {
			n_log( LOG_INFO , "HT_FOREACH key:%s" , _str( node -> key ) );	
		} );

	/*ht_print( htable );
	  char input_key[ 1024 ] = "" ;
	  printf( "Enter key starting piece, q! to quit:\n");
	  while( strcmp( input_key , "q!" ) != 0 )
	  {
	  printf( "key:" );
	  scanf( "%s" , input_key );
	  LIST *results = _ht_get_completion_list( htable , input_key , 10 );
	  if( results )
	  {
	  list_foreach( node , results )
	  {
	  printf( "result: %s\n" , (char *)node -> ptr );
	  }
	  list_destroy( &results );
	  }
	  }*/
	LIST *results = _ht_get_completion_list( htable , "key1" , 10 );
	if( results )
	{
		list_foreach( node , results )
		{
			n_log( LOG_INFO , "completion result: %s\n" , (char *)node -> ptr );
		}
		list_destroy( &results );
	}


	results = ht_search( htable , "key****" );
	list_foreach( node , results )
	{
		n_log( LOG_INFO , "htsearch: key: %s" , (char *)node -> ptr );
	}
	list_destroy( &results );

	list_destroy( &keys_list );
	destroy_ht( &htable );

	/* testing empty destroy */
	htable = new_ht( 1024 );
	empty_ht( htable );
	destroy_ht( &htable );

	htable = new_ht_trie( 256 , 32 );

	ht_put_int( htable , "TestInt" , 1 );
	ht_put_double( htable , "TestDouble" , 2.0 );
	ht_put_string( htable , "TestString" , "MyString" );
	int ival = -1 ;
	double fval = -1.0 ;
	ht_get_int( htable , "TestInt" , &ival );
	ht_get_double( htable , "TestDouble" , &fval );
	char *string = NULL ;
	ht_get_string( htable , "TestString" , &string );
	n_log( LOG_INFO , "Trie:%d %f %s" , ival , fval , string );

	results = _ht_get_completion_list( htable , "Test" , 10 );
	if( results )
	{
		list_foreach( node , results )
		{
			n_log( LOG_INFO , "completion result: %s" , (char *)node -> ptr );
		}
		list_destroy( &results );
	}

	destroy_ht( &htable );

	exit( 0 );

} /* END_OF_MAIN */

