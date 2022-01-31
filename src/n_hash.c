/**\file n_hash.c
 * Hash functions and table
 *\author Castagnier Mickael
 *\version 2.0
 *\date 16/03/2015
 */



#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_hash.h"

#include <pthread.h>
#include <string.h>
#include <fnmatch.h>

#ifdef __windows__
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif



/*!\fn HASH_NODE *_ht_new_node_trie( HASH_TABLE *table , char key )
 *\internal
 *\brief node creation, HASH_CLASSIC mode
 *\param table Target table
 *\param key key of new node
 *\return NULL or a new HASH_NODE *
 */
HASH_NODE *_ht_new_node_trie( HASH_TABLE *table , char key )
{
	__n_assert( table , return NULL );

	HASH_NODE *new_hash_node = NULL ;
	Malloc( new_hash_node, HASH_NODE , 1 );
	__n_assert( new_hash_node, n_log( LOG_ERR, "Could not allocate new_hash_node" ); return NULL );
	new_hash_node -> key = NULL ;
	new_hash_node -> hash_value = 0 ;
	new_hash_node -> data. ptr = NULL ;
	new_hash_node -> destroy_func = NULL ;
	new_hash_node -> children = NULL ;
	new_hash_node -> is_leaf = 0 ;
	new_hash_node -> alphabet_length = table -> alphabet_length ;

	Malloc( new_hash_node -> children , HASH_NODE * , table -> alphabet_length );
	__n_assert( new_hash_node -> children , n_log( LOG_ERR, "Could not allocate new_hash_node" ); Free( new_hash_node ); return NULL );

	/* n_log( LOG_DEBUG , "node: %d %c table: alpha: %d / offset %d" , key , key , table -> alphabet_length , table -> alphabet_offset ); */

	for( uint32_t it = 0 ;  it < table -> alphabet_length ; it++ )
	{
		new_hash_node -> children[ it ] = NULL;
	}
	new_hash_node -> is_leaf = 0 ;
	new_hash_node -> key_id = key ;
	return new_hash_node;
}



int _ht_put_int_trie( HASH_TABLE *table , char *key , int value )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );

	HASH_NODE *node = table -> root ;

	for( uint32_t it = 0 ; key[ it ] != '\0' ; it++ )
	{
		int index = (int)key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		/* n_log( LOG_DEBUG , "index:%d" , index ); */
		if( node -> children[ index ] == NULL )
		{
			/* create a node */
			node -> children[ index ] = _ht_new_node_trie( table , key[ it ] );
			__n_assert( node -> children[ index ] , return FALSE );
		}
		else {
			/* nothing to do since node is existing */
		}
		/* go down a level, to the child referenced by index */
		node = node -> children[ index ];
	}
	/* At the end of the key, mark this node as the leaf node */
	node -> is_leaf = 1;
	/* Put the key */
	node -> key = strdup( key );
	/* Put the value */
	node -> data . ival = value ;
	node -> type = HASH_INT ;

	return TRUE;
}


int _ht_put_double_trie( HASH_TABLE *table , char *key , double value )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );

	HASH_NODE *node = table -> root ;

	for( uint32_t it = 0 ; key[ it ] != '\0' ; it++ )
	{
		int index = (int)key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] == NULL )
		{
			/* create a node */
			node -> children[ index ] = _ht_new_node_trie( table , key[ it ] );
			__n_assert( node -> children[ index ] , return FALSE );
		}
		else {
			/* nothing to do since node is existing */
		}
		/* go down a level, to the child referenced by index */
		node = node -> children[ index ];
	}
	/* At the end of the key, mark this node as the leaf node */
	node -> is_leaf = 1;
	/* Put the key */
	node -> key = strdup( key );
	/* Put the value */
	node -> data . fval = value ;
	node -> type = HASH_DOUBLE ;

	return TRUE;
}



int _ht_put_string_trie( HASH_TABLE *table , char *key , char *string )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );

	HASH_NODE *node = table -> root ;

	for( uint32_t it = 0 ; key[ it ] != '\0' ; it++ )
	{
		int index = (int)key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] == NULL )
		{
			/* create a node */
			node -> children[ index ] = _ht_new_node_trie( table , key[ it ] );
			__n_assert( node -> children[ index ] , return FALSE );
		}
		else {
			/* nothing to do since node is existing */
		}
		/* go down a level, to the child referenced by index */
		node = node -> children[ index ];
	}
	/* At the end of the key, mark this node as the leaf node */
	node -> is_leaf = 1;
	/* Put the key */
	node -> key = strdup( key );
	/* Put the value */
	if( string )
			node -> data . string = strdup( string );
	else
			node -> data . string = NULL ;

	node -> type = HASH_STRING ;

	return TRUE;
}

int _ht_put_string_ptr_trie( HASH_TABLE *table , char *key , char *string )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );

	HASH_NODE *node = table -> root ;

	for( uint32_t it = 0 ; key[ it ] != '\0' ; it++ )
	{
		int index = (int)key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] == NULL )
		{
			/* create a node */
			node -> children[ index ] = _ht_new_node_trie( table , key[ it ] );
			__n_assert( node -> children[ index ] , return FALSE );
		}
		else {
			/* nothing to do since node is existing */
		}
		/* go down a level, to the child referenced by index */
		node = node -> children[ index ];
	}
	/* At the end of the key, mark this node as the leaf node */
	node -> is_leaf = 1;
	/* Put the key */
	node -> key = strdup( key );
	/* Put the string */
	node -> data . string = string ;
	node -> type = HASH_STRING ;

	return TRUE;
}


int _ht_put_ptr_trie( HASH_TABLE *table , char *key , void *ptr , void (*destructor)(void *ptr ) )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );

	HASH_NODE *node = table -> root ;

	for( uint32_t it = 0 ; key[ it ] != '\0' ; it++ )
	{
		int index = (int)key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] == NULL )
		{
			/* create a node */
			node -> children[ index ] = _ht_new_node_trie( table , key[ it ] );
			__n_assert( node -> children[ index ] , return FALSE );
		}
		else {
			/* nothing to do since node is existing */
		}
		/* go down a level, to the child referenced by index */
		node = node -> children[ index ];
	}
	/* At the end of the key, mark this node as the leaf node */
	node -> is_leaf = 1;
	/* Put the key */
	node -> key = strdup( key );
	/* Put the value */
	node -> data . ptr = ptr ;
	if( destructor )
		node -> destroy_func = destructor ;
	node -> type = HASH_PTR ;

	return TRUE;
}



HASH_NODE *_ht_get_node_trie( HASH_TABLE *table , char *key )
{
	__n_assert( table , return NULL );
	__n_assert( key , return NULL );

	HASH_NODE *node = table -> root;
	for( uint32_t it = 0 ; key[ it ] != '\0' ; it++ )
	{
		int index = key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at index %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] == NULL )
		{
			/* not found */
			return NULL;
		}
		node = node -> children[ index ];
	}
	return node ;
}



/*!\fn int _ht_get_int_trie( HASH_TABLE *table , char *key , int *val )
 *\brief Retrieve an integral value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to a destination integer
 *\return TRUE or FALSE.
 */
int _ht_get_int_trie( HASH_TABLE *table, char *key, int *val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node_trie( table, key );

	if( !node || !node -> is_leaf )
		return FALSE ;

	if( node -> type != HASH_INT )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . ival ;

	return TRUE ;
} /* _ht_get_int_trie() */



/*!\fn int _ht_get_double_trie( HASH_TABLE *table , char *key , double *val )
 *\brief Retrieve an integral value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to a destination integer
 *\return TRUE or FALSE.
 */
int _ht_get_double_trie( HASH_TABLE *table, char *key, double *val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node_trie( table, key );

	if( !node || !node -> is_leaf )
		return FALSE ;

	if( node -> type != HASH_DOUBLE )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . fval ;

	return TRUE ;
} /* _ht_get_double_trie() */



/*!\fn int _ht_get_string_trie( HASH_TABLE *table , char *key , char **val )
 *\brief Retrieve an integral value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to a destination integer
 *\return TRUE or FALSE.
 */
int _ht_get_string_trie( HASH_TABLE *table, char *key, char **val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node_trie( table, key );

	if( !node || !node -> is_leaf )
		return FALSE ;

	if( node -> type != HASH_STRING )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . string ;

	return TRUE ;
} /* _ht_get_string_trie() */


/*!\fn int _ht_get_ptr_trie( HASH_TABLE *table , char *key , void **val )
 *\brief Retrieve an integral value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to a destination pointer
 *\return TRUE or FALSE.
 */
int _ht_get_ptr_trie( HASH_TABLE *table, char *key , void **val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node_trie( table, key );

	if( !node || !node -> is_leaf )
		return FALSE ;

	if( node -> type != HASH_STRING )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . ptr ;

	return TRUE ;
} /* _ht_get_ptr_trie() */



int _ht_check_trie_divergence( HASH_TABLE *table , char *key )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );

	/* check and return branching if any */
	HASH_NODE* node = table -> root;

	int last_index = 0;
	for( uint32_t it = 0 ; key[ it ] != '\0' ; it ++ )
	{
		int index = key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] )
		{
			/* child check */
			for( uint32_t it2 = 0 ; it2 < table -> alphabet_length ; it2++ )
			{
				if( it2 != (unsigned)index && node -> children[ it2 ] )
				{
					/* child found, update branch index */
					last_index = it + 1;
					break;
				}
			}
			/* Go to the next child in the sequence */
			node = node->children[ index ];
		}
	}
	return last_index;
}



char* _ht_find_longest_prefix_trie( HASH_TABLE *table , char *key )
{
	__n_assert( table , return NULL );
	__n_assert( key , return NULL );

	int len = strlen( key );

	/* start with full key and backtrack to search for divergences */
	char *longest_prefix = NULL ;
	Malloc( longest_prefix , char , len + 1 );
	memcpy( longest_prefix , key , len );

	/* check branching from the root */
	int branch_index  = _ht_check_trie_divergence( table , longest_prefix ) - 1 ;
	if(branch_index >= 0 )
	{
		/* there is branching, update the position to the longest match and update the longest prefix by the branch index length */
		longest_prefix[ branch_index ] = '\0';
		Realloc( longest_prefix , char , (branch_index + 1) );
		__n_assert( longest_prefix , return NULL );
	}
	return longest_prefix;
}



int _ht_is_leaf_node_trie( HASH_TABLE *table , char *key )
{
	__n_assert( table , return -1 );

	/* checks if the prefix match of key and root is a leaf node */
	HASH_NODE* node = table -> root;
	for( uint32_t it = 0 ; key[ it ] ; it++ )
	{
		int index = (int)key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] ) 
		{
			node = node -> children[ index ];
		}
	}
	return node -> is_leaf ;
}

/*!\fn void _ht_node_destroy( void *node )
 *\brief destroy a HASH_NODE by first calling the HASH_NODE destructor
 *\param node The node to kill
 */
void _ht_node_destroy( void *node )
{
	HASH_NODE *node_ptr = (HASH_NODE *)node ;
	__n_assert( node_ptr, return );
	if( node_ptr -> type == HASH_STRING )
	{
		Free( node_ptr  -> data . string );
	}
	if( node_ptr -> type == HASH_PTR )
	{
		if( node_ptr -> destroy_func && node_ptr -> data . ptr )
		{
			node_ptr -> destroy_func( node_ptr  -> data . ptr );
		}
		/* No free by default. must be passed as a destroy_func
		   else
		   {
		   Free( node_ptr  -> data . ptr );
		   }
		   */
	}
	FreeNoLog( node_ptr -> key );
	if( node_ptr -> alphabet_length > 0 )
	{
		for( uint32_t it = 0 ; it < node_ptr -> alphabet_length ; it ++ )
		{
			if( node_ptr -> children[ it ] )
			{
				_ht_node_destroy( node_ptr -> children[ it ] );
			}
		}
		Free( node_ptr -> children );
	}
	Free( node_ptr )
} /* _ht_node_destroy */



int _ht_remove_trie( HASH_TABLE *table , char *key )
{
	__n_assert( table , return FALSE );
	__n_assert( table -> root , return FALSE );
	__n_assert( key , return FALSE );

	/* stop if matching node not a leaf node */
	if( !_ht_is_leaf_node_trie( table , key ) )
	{
		return FALSE ;
	}

	HASH_NODE *node = table -> root ;
	/* find the longest prefix string that is not the current key */
	char *longest_prefix = _ht_find_longest_prefix_trie( table , key );
	if( longest_prefix && longest_prefix[ 0 ] == '\0' )
	{
		Free( longest_prefix );
		return FALSE ;
	}
	/* keep track of position in the tree */
	uint32_t it = 0 ;
	for( it = 0 ; longest_prefix[ it ] != '\0'; it++ )
	{
		int index = (int)longest_prefix[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index ] != NULL )
		{
			/* common prefix, keep moving */
			node = node -> children[ index ];
		}
		else 
		{
			/* not found */
			Free( longest_prefix );
			return FALSE ;
		}
	}
	/* deepest common node between the two strings */
	/* deleting the sequence corresponding to key */
	for( ; key[ it ] != '\0' ; it++ )
	{
		int index = (int)key[ it ] - table -> alphabet_offset ;
		if( index < 0 || (unsigned)index >= table -> alphabet_length ){ n_log( LOG_ERR , "Invalid value %d for charater at position %d of %s, set to 0" , index , it , key ); index = 0 ; }
		if( node -> children[ index] )
		{
			/* delete the remaining sequence */
			HASH_NODE* node_to_kill = node -> children[ index ];
			node -> children[ index ] = NULL;
			_ht_node_destroy( node_to_kill );
		}
	}
	Free( longest_prefix );
	return TRUE ;
}



/*!\fn int _empty_ht_trie( HASH_TABLE *table )
 *\brief Empty a TRIE hash table
 *\param table Targeted hash table
 *\return TRUE or FALSE.
 */
int _empty_ht_trie( HASH_TABLE *table )
{
	__n_assert( table, return FALSE );

	_ht_node_destroy( table -> root );

	table -> root = _ht_new_node_trie( table , '\0' );

	table -> nb_keys = 0 ;
	return TRUE ;
} /* _empty_ht_trie */



/*!\fn int destroy_ht( HASH_TABLE **table )
 *\brief Free and set the table to NULL
 *\param table Targeted hash table
 *\return TRUE or FALSE.
 */
int _destroy_ht_trie( HASH_TABLE **table )
{
	__n_assert( table&&(*table), n_log( LOG_ERR, "Can't destroy table: already NULL" ); return FALSE );

	_ht_node_destroy( (*table) -> root );

	Free( (*table ) );

	return TRUE ;
} /* _destroy_ht */






void _ht_print_trie_helper( HASH_TABLE *table , HASH_NODE *node )
{
	if( !node )
		return ;

	if( node -> is_leaf )
	{
		printf( "key: %s, val: " , node -> key );
		switch( node -> type )
		{
			case HASH_INT:
				printf( "int: %d" , node -> data . ival );
				break;
			case HASH_DOUBLE:
				printf( "double: %f" , node -> data . fval );
				break;
			case HASH_PTR:
				printf( "ptr: %p" , node -> data . ptr );
				break;
			case HASH_STRING:
				printf( "%s" , node -> data . string );
				break;
			default:
				printf( "unknwow type %d" , node -> type );
				break;
		}
		printf( "\n" );
	}
	for( uint32_t it = 0 ; it < table -> alphabet_length ; it++ )
	{
		_ht_print_trie_helper( table , node -> children[ it ] ); 
	}
}



void _ht_print_trie( HASH_TABLE *table )
{
	__n_assert( table , return );
	__n_assert( table -> root , return );

	HASH_NODE *node = table -> root ;

	_ht_print_trie_helper( table , node );

	return ;
}

void _ht_search_trie_helper( LIST *results , HASH_NODE *node , char *exp )
{
	if( !node )
		return ;

	if( node -> is_leaf )
	{
		if( !fnmatch( exp , node -> key , FNM_NOESCAPE ) )
		{
			list_push( results , strdup( node -> key ) , &free );
		}
	}
	for( uint32_t it = 0 ; it < node -> alphabet_length ; it++ )
	{
		_ht_search_trie_helper( results , node -> children[ it ] , exp );
	}
}

LIST *_ht_search_trie( HASH_TABLE *table , char *exp )
{
	__n_assert( table , return NULL );
	__n_assert( exp , return NULL );
	LIST *results = new_generic_list( 0 );

	_ht_search_trie_helper( results , table -> root , exp );
	if( results -> nb_items < 1 )
		list_destroy( &results );

	return results ;
}



/* depth search of key */
int _ht_depth_first_search( HASH_NODE *node , LIST *results ) 
{
	__n_assert( results , return FALSE );

	if( !node )
		return FALSE ;

	for( uint32_t it = 0; it < node -> alphabet_length ; it++ )
	{
		_ht_depth_first_search( node -> children[ it ], results );
	}
	if( node -> is_leaf ) 
	{
		if( results -> nb_items < results -> nb_max_items )
		{
			return list_push( results , strdup( node -> key ) , &free );
		}
		return FALSE ;
	}	
	return TRUE ;
}



/* completion search keys */
LIST *_ht_get_completion_list( HASH_TABLE *table , char *keybud , uint32_t max_results )
{
	__n_assert( table , return NULL );
	__n_assert( table -> root , return NULL );
	__n_assert( keybud , return NULL );

	LIST *results = new_generic_list( max_results );
	int found = FALSE ;

	HASH_NODE* node = _ht_get_node_trie( table , keybud );
	if( node )
	{
		if( list_push( results , strdup( keybud ) , &free ) == TRUE )
		{
			found = TRUE ;
			_ht_depth_first_search( node , results );
		}
	}
	if( found == FALSE )
		list_destroy( &results );

	return results ;
}



#ifndef _MSC_VER
/*!\fn static FORCE_INLINE uint32_t rotl32 ( uint32_t x, int8_t r )
 *\brief rotate byte 32bit version (from murmur's author)
 *\param x value
 *\param r rotation value
 *\return rotated value
 */
static FORCE_INLINE uint32_t rotl32( uint32_t x, int8_t r )
{
	return (x << r) | (x >> (32 - r));
} /* rotl32(...) */
#endif

/*!\fn static FORCE_INLINE uint32_t getblock( const uint32_t *p, int i )
 *\brief Block read - (from murmur's author)
 if your platform needs to do endian-swapping or can only
 handle aligned reads, do the conversion here
 *\param p value
 *\param i position
 *\return value at position inside block
 */
static FORCE_INLINE uint32_t getblock( const uint32_t *p, int i )
{
	uint32_t var ;
	memcpy( &var, p+i, sizeof( uint32_t ) );
#if( BYTEORDER_ENDIAN == BYTEORDER_LITTLE_ENDIAN )
	var = htonl( var );
#endif
	return var ;
} /* get_block(...) */



/*!\fn static FORCE_INLINE uint32_t fmix32 ( uint32_t h )
 *\brief Finalization mix - force all bits of a hash block to avalanche (from murmur's author)
 *\param h value
 *\return mixed value
 */
static FORCE_INLINE uint32_t fmix32 ( uint32_t h )
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
} /* fmix32(...) */

/*!\fn static FORCE_INLINE uint64_t fmix64 ( uint64_t k )
 *\brief Finalization mix - force all bits of a hash block to avalanche, 64bit version (from murmur's author)
 *\param k value
 *\return mixed value
 */
/*static FORCE_INLINE uint64_t fmix64 ( uint64_t k )
  {
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xff51afd7ed558ccd);
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
  k ^= k >> 33;

  return k;
  } */
/* fmix64(...) */



/*!\fn void MurmurHash3_x86_32 ( const void * key, int len, uint32_t seed, void * out )
 * brief MurmurHash3 was written by Austin Appleby, and is placed in the public
 domain. The author hereby disclaims copyright to this source code.
 Note - The x86 and x64 versions do _not_ produce the same results, as the
 algorithms are optimized for their respective platforms. You can still
 compile and run any of them on any platform, but your performance with the
 non-native version will be less than optimal.
 *\param key char *string as the key
 *\param len size of the key
 *\param seed seed value for murmur hash
 *\param out generated hash
 */
void MurmurHash3_x86_32 ( const void * key, int len, uint32_t seed, void * out )
{
	const uint8_t * data = (const uint8_t*)key;
	const int nblocks = len / 4;

	uint32_t h1 = seed;

	uint32_t c1 = 0xcc9e2d51;
	uint32_t c2 = 0x1b873593;

	/* body */
	const uint32_t * blocks = (const void *)(data + nblocks*4);

	int i = 0 ;
	for(i = -nblocks; i; i++)
	{
		uint32_t k1 = getblock(blocks,i);

		k1 *= c1;
		k1 = ROTL32(k1,15);
		k1 *= c2;

		h1 ^= k1;
		h1 = ROTL32(h1,13);
		h1 = h1*5+0xe6546b64;
	}

	/* tail */
	const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

	uint32_t k1 = 0;

	switch(len & 3)
	{
		case 3:
			k1 ^= tail[2] << 16;
			FALL_THROUGH ;
		case 2:
			k1 ^= tail[1] << 8;
			FALL_THROUGH ;
		case 1:
			k1 ^= tail[0];
			k1 *= c1;
			k1 = ROTL32(k1,15);
			k1 *= c2;
			h1 ^= k1;
		default:
			break ;
	};

	/* finalization */
	h1 ^= len;
	h1 = fmix32(h1);
	*(uint32_t*)out = h1;
} /* MurmurHash3_x86_32(...) */



/*!\fn void MurmurHash3_x86_128( const void * key, int len, uint32_t seed, void * out )
 * brief MurmurHash3 was written by Austin Appleby, and is placed in the public
 domain. The author hereby disclaims copyright to this source code.
 Note - The x86 and x64 versions do _not_ produce the same results, as the
 algorithms are optimized for their respective platforms. You can still
 compile and run any of them on any platform, but your performance with the
 non-native version will be less than optimal.
 *\param key char *string as the key
 *\param len size of the key
 *\param seed seed value for murmur hash
 *\param out generated hash
 */
void MurmurHash3_x86_128 ( const void * key, const int len, uint32_t seed, void * out )
{
	const uint8_t * data = (const uint8_t*)key;
	const int nblocks = len / 16;

	uint32_t h1 = seed;
	uint32_t h2 = seed;
	uint32_t h3 = seed;
	uint32_t h4 = seed;

	uint32_t c1 = 0x239b961b;
	uint32_t c2 = 0xab0e9789;
	uint32_t c3 = 0x38b34ae5;
	uint32_t c4 = 0xa1e38b93;

	/* body */
	const uint32_t * blocks = (const void *)(data + nblocks*16);

	int i = 0 ;
	for( i = -nblocks ; i; i++)
	{
		uint32_t k1 = getblock(blocks,i*4+0);
		uint32_t k2 = getblock(blocks,i*4+1);
		uint32_t k3 = getblock(blocks,i*4+2);
		uint32_t k4 = getblock(blocks,i*4+3);

		k1 *= c1;
		k1  = ROTL32(k1,15);
		k1 *= c2;
		h1 ^= k1;
		h1 = ROTL32(h1,19);
		h1 += h2;
		h1 = h1*5+0x561ccd1b;

		k2 *= c2;
		k2  = ROTL32(k2,16);
		k2 *= c3;
		h2 ^= k2;
		h2 = ROTL32(h2,17);
		h2 += h3;
		h2 = h2*5+0x0bcaa747;

		k3 *= c3;
		k3  = ROTL32(k3,17);
		k3 *= c4;
		h3 ^= k3;
		h3 = ROTL32(h3,15);
		h3 += h4;
		h3 = h3*5+0x96cd1c35;

		k4 *= c4;
		k4  = ROTL32(k4,18);
		k4 *= c1;
		h4 ^= k4;
		h4 = ROTL32(h4,13);
		h4 += h1;
		h4 = h4*5+0x32ac3b17;
	}

	/* tail */
	const uint8_t * tail = (const uint8_t*)(data + nblocks*16);

	uint32_t k1 = 0;
	uint32_t k2 = 0;
	uint32_t k3 = 0;
	uint32_t k4 = 0;

	switch(len & 15)
	{
		case 15:
			k4 ^= tail[14] << 16;
			FALL_THROUGH ;
		case 14:
			k4 ^= tail[13] << 8;
			FALL_THROUGH ;
		case 13:
			k4 ^= tail[12] << 0;
			k4 *= c4;
			k4  = ROTL32(k4,18);
			k4 *= c1;
			h4 ^= k4;
			FALL_THROUGH ;
		case 12:
			k3 ^= tail[11] << 24;
			FALL_THROUGH ;
		case 11:
			k3 ^= tail[10] << 16;
			FALL_THROUGH ;
		case 10:
			k3 ^= tail[ 9] << 8;
			FALL_THROUGH ;
		case  9:
			k3 ^= tail[ 8] << 0;
			k3 *= c3;
			k3  = ROTL32(k3,17);
			k3 *= c4;
			h3 ^= k3;
			FALL_THROUGH ;
		case  8:
			k2 ^= tail[ 7] << 24;
			FALL_THROUGH ;
		case  7:
			k2 ^= tail[ 6] << 16;
			FALL_THROUGH ;
		case  6:
			k2 ^= tail[ 5] << 8;
			FALL_THROUGH ;
		case  5:
			k2 ^= tail[ 4] << 0;
			k2 *= c2;
			k2  = ROTL32(k2,16);
			k2 *= c3;
			h2 ^= k2;
			FALL_THROUGH ;
		case  4:
			k1 ^= tail[ 3] << 24;
			FALL_THROUGH ;
		case  3:
			k1 ^= tail[ 2] << 16;
			FALL_THROUGH ;
		case  2:
			k1 ^= tail[ 1] << 8;
			FALL_THROUGH ;
		case  1:
			k1 ^= tail[ 0] << 0;
			k1 *= c1;
			k1  = ROTL32(k1,15);
			k1 *= c2;
			h1 ^= k1;
		default:
			break ;
	};
	/* finalization */
	h1 ^= len;
	h2 ^= len;
	h3 ^= len;
	h4 ^= len;

	h1 += h2;
	h1 += h3;
	h1 += h4;
	h2 += h1;
	h3 += h1;
	h4 += h1;

	h1 = fmix32(h1);
	h2 = fmix32(h2);
	h3 = fmix32(h3);
	h4 = fmix32(h4);

	h1 += h2;
	h1 += h3;
	h1 += h4;
	h2 += h1;
	h3 += h1;
	h4 += h1;

	((uint32_t*)out)[0] = h1;
	((uint32_t*)out)[1] = h2;
	((uint32_t*)out)[2] = h3;
	((uint32_t*)out)[3] = h4;
} /* MurmurHash3_x86_32(...) */



/*!\fn char *ht_node_type( HASH_NODE *node )
 *\brief get the type of a node , text version
 *\param node node to check
 *\return NULL or the associated node type
 */
char *ht_node_type( HASH_NODE *node )
{
	static char *HASH_TYPE_STR[ 5 ] = { "HASH_INT", "HASH_DOUBLE", "HASH_STRING", "HASH_PTR", "HASH_UNKNOWN" };

	__n_assert( node, return NULL );

	if( node -> type >= 0 && node -> type < HASH_UNKNOWN )
		return HASH_TYPE_STR[ node -> type ];

	return NULL ;
} /* ht_node_type(...) */


/*!\fn HASH_NODE *_ht_get_node( HASH_TABLE *table , char *key )
 *\brief return the associated key's node inside the hash_table
 *\param table Targeted hash table
 *\param key Associated value's key
 *\return The found node, or NULL
 */
HASH_NODE *_ht_get_node( HASH_TABLE *table, char *key )
{
	uint32_t hash_value = 0 ;
	unsigned long int index = 0;

	HASH_NODE *node_ptr = NULL ;

	__n_assert( table, return NULL );
	__n_assert( key, return NULL );

	if( key[ 0 ] == '\0' )
		return NULL ;

	MurmurHash3_x86_32( key, strlen( key ), table -> seed, &hash_value );
	index= (hash_value)%(table->size);

	if( !table -> hash_table[ index ] -> start )
	{
		return NULL ;
	}

	list_foreach( list_node, table -> hash_table[ index ] )
	{
		node_ptr = (HASH_NODE *)list_node -> ptr ;
		if( !strcmp( key, node_ptr -> key ) )
		{
			return node_ptr ;
		}
	}
	return NULL ;
} /* _ht_get_node() */



/*!\fn HASH_NODE *_ht_new_node( HASH_TABLE *table , char *key )
 *\internal
 *\brief node creation, HASH_CLASSIC mode
 *\param table Target table
 *\param key key of new node
 *\return NULL or a new HASH_NODE *
 */
HASH_NODE *_ht_new_node( HASH_TABLE *table , char *key )
{
	__n_assert( table, return NULL );
	__n_assert( key, return NULL );

	HASH_NODE *new_hash_node = NULL ;
	uint32_t hash_value = 0 ;

	if( strlen( key ) == 0 )
		return NULL ;

	MurmurHash3_x86_32( key, strlen( key ), table -> seed, &hash_value );

	Malloc( new_hash_node, HASH_NODE, 1 );
	__n_assert( new_hash_node, n_log( LOG_ERR, "Could not allocate new_hash_node" ); return NULL );
	new_hash_node -> key = strdup( key );
	new_hash_node -> key_id = '\0' ;
	__n_assert( new_hash_node -> key , n_log( LOG_ERR, "Could not allocate new_hash_node->key" ); Free( new_hash_node ); return NULL );
	new_hash_node -> hash_value = hash_value ;
	new_hash_node -> data. ptr = NULL ;
	new_hash_node -> destroy_func = NULL ;
	new_hash_node -> children = NULL ;
	new_hash_node -> is_leaf = 0 ;
	new_hash_node -> alphabet_length = 0 ;

	return new_hash_node ;
} /* _ht_new_node */



/*!\fn HASH_NODE *_ht_new_int_node( HASH_TABLE *table , char *key , int value )
 *\internal
 *\brief node creation, HASH_CLASSIC mode
 *\param table Target table
 *\param key key of new node
 *\param value int value of key
 *\return NULL or a new HASH_NODE *
 */
HASH_NODE *_ht_new_int_node( HASH_TABLE *table , char *key , int value )
{
	__n_assert( table , return NULL );
	__n_assert( key , return NULL );

	HASH_NODE *new_hash_node = NULL ;
	new_hash_node = _ht_new_node( table , key );
	new_hash_node -> data . ival = value ;
	new_hash_node -> type = HASH_INT ;
	return new_hash_node ;
} /* _ht_new_int_node */



/*!\fn HASH_NODE *_ht_new_double_node( HASH_TABLE *table , char *key , double value )
 *\internal
 *\brief node creation, HASH_CLASSIC mode
 *\param table Target table
 *\param key key of new node
 *\param value double value of key
 *\return NULL or a new HASH_NODE *
 */
HASH_NODE *_ht_new_double_node( HASH_TABLE *table , char *key , double value )
{
	__n_assert( table , return NULL );
	__n_assert( key , return NULL );

	HASH_NODE *new_hash_node = NULL ;
	new_hash_node = _ht_new_node( table , key );
	new_hash_node -> data . fval = value ;
	new_hash_node -> type = HASH_DOUBLE ;
	return new_hash_node ;
} /* _ht_new_double_node */



/*!\fn HASH_NODE *_ht_new_string_node( HASH_TABLE *table , char *key , char *value )
 *\internal
 *\brief node creation, HASH_CLASSIC mode, strdup of value
 *\param table Target table
 *\param key key of new node
 *\param value char *value of key
 *\return NULL or a new HASH_NODE *
 */
HASH_NODE *_ht_new_string_node( HASH_TABLE *table , char *key , char *value )
{
	__n_assert( table , return NULL );
	__n_assert( key , return NULL );

	HASH_NODE *new_hash_node = NULL ;
	new_hash_node = _ht_new_node( table , key );
	if( value )
		new_hash_node -> data . string = strdup( value );
	else
		new_hash_node -> data . string = NULL ;
	new_hash_node -> type = HASH_STRING ;
	return new_hash_node ;
}



/*!\fn HASH_NODE *_ht_new_string_ptr_node( HASH_TABLE *table , char *key , char *value )
 *\internal
 *\brief node creation, HASH_CLASSIC mode, pointer to string value
 *\param table Target table
 *\param key key of new node
 *\param value char *value of key
 *\return NULL or a new HASH_NODE *
 */
HASH_NODE *_ht_new_string_ptr_node( HASH_TABLE *table , char *key , char *value )
{
	__n_assert( table , return NULL );
	__n_assert( key , return NULL );

	HASH_NODE *new_hash_node = NULL ;
	new_hash_node = _ht_new_node( table , key );
	new_hash_node -> data . string = value ;
	new_hash_node -> type = HASH_STRING ;
	return new_hash_node ;
}



/*!\fn HASH_NODE *_ht_new_string_ptr_node( HASH_TABLE *table , void *value , void (*destructor)(void *ptr ) )
 *\internal
 *\brief node creation, HASH_CLASSIC mode, pointer to string value
 *\param table Target table
 *\param key key of new node
 *\param value pointer data of key
 *\param destructor function pointer to a destructor of value type
 *\return NULL or a new HASH_NODE *
 */
HASH_NODE *_ht_new_ptr_node( HASH_TABLE *table , char *key , void *value , void (*destructor)(void *ptr ) )
{
	__n_assert( table , return NULL );
	__n_assert( key , return NULL );

	HASH_NODE *new_hash_node = NULL ;
	new_hash_node = _ht_new_node( table , key );
	new_hash_node -> data . ptr = value ;
	new_hash_node -> destroy_func = destructor ;
	new_hash_node -> type = HASH_PTR ;
	return new_hash_node ;
}








/*!\fn int _ht_put_int( HASH_TABLE *table , char * key , int value )
 *\brief put an integral value with given key in the targeted hash table
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param value integral value to put
 *\return TRUE or FALSE
 */
int _ht_put_int( HASH_TABLE *table, char * key, int value )
{
	HASH_NODE *node_ptr = NULL ;

	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );

	if( ( node_ptr = _ht_get_node( table , key ) ) )
	{
		if( node_ptr -> type == HASH_INT ) 
		{
			node_ptr -> data . ival = value ;
			return TRUE ;
		}
		n_log( LOG_ERR, "Can't add key[\"%s\"] with type HASH_INT, key already exist with type %s", node_ptr -> key, ht_node_type( node_ptr ) );
		return FALSE ; /* key registered with another data type */
	}

	node_ptr = _ht_new_int_node( table , key , value );

	unsigned long int index = ( node_ptr -> hash_value )%( table -> size );
	int retcode = list_push( table -> hash_table[ index ], node_ptr , &_ht_node_destroy );
	if( retcode == TRUE )
	{		
		table -> nb_keys ++ ;
	}
	return retcode ;
} /*_ht_put_int() */



/*!\fn int _ht_put_double( HASH_TABLE *table , char * key , double value )
 *\brief put a double value with given key in the targeted hash table
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param value double value to put
 *\return TRUE or FALSE
 */
int _ht_put_double( HASH_TABLE *table, char *key, double value )
{
	HASH_NODE *node_ptr = NULL ;

	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );

	if( ( node_ptr = _ht_get_node( table , key ) ) )
	{
		if( node_ptr -> type == HASH_DOUBLE ) 
		{
			node_ptr -> data . fval = value ;
			return TRUE ;
		}
		n_log( LOG_ERR, "Can't add key[\"%s\"] with type HASH_DOUBLE, key already exist with type %s", node_ptr -> key, ht_node_type( node_ptr ) );
		return FALSE ; /* key registered with another data type */
	}

	node_ptr = _ht_new_double_node( table , key , value );

	unsigned long int index = ( node_ptr -> hash_value )%( table -> size );
	int retcode = list_push( table -> hash_table[ index ], node_ptr , &_ht_node_destroy );
	if( retcode == TRUE )
	{		
		table -> nb_keys ++ ;
	}
	return retcode ;
} /*_ht_put_double()*/



/*!\fn int _ht_put_ptr( HASH_TABLE *table , char *key , void  *ptr , void (*destructor)(void *ptr ) )
 *\brief put a pointer value with given key in the targeted hash table
 *\param table Targeted hash table
 *\param key Associated value's key
 *\param ptr pointer value to put
 *\param destructor Pointer to the ptr type destructor function. Leave to NULL if there isn't
 *\return TRUE or FALSE
 */
int _ht_put_ptr( HASH_TABLE *table, char *key, void  *ptr, void (*destructor)(void *ptr ) )
{
	HASH_NODE *node_ptr = NULL ;

	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );

	if( ( node_ptr = _ht_get_node( table , key ) ) )
	{
		/* let's check the key isn't already assigned with another data type */
		if( node_ptr -> type == HASH_PTR )
		{
			if( node_ptr -> destroy_func )
			{
				node_ptr -> destroy_func( node_ptr -> data . ptr );
				node_ptr -> data . ptr = ptr ;
				node_ptr -> destroy_func = destructor ;
			}
			return TRUE ;
		}
		n_log( LOG_ERR, "Can't add key[\"%s\"] with type HASH_PTR , key already exist with type %s", node_ptr -> key, ht_node_type( node_ptr ) );
		return FALSE ; /* key registered with another data type */
	}

	node_ptr = _ht_new_ptr_node( table , key , ptr , destructor );

	unsigned long int index = ( node_ptr -> hash_value )%( table -> size );
	int retcode = list_push( table -> hash_table[ index ], node_ptr , &_ht_node_destroy );
	if( retcode == TRUE )
	{		
		table -> nb_keys ++ ;
	}
	return retcode ;
}/* _ht_put_ptr() */



/*!\fn int _ht_put_string( HASH_TABLE *table , char *key , char  *string )
 *\brief put a null terminated char *string with given key in the targeted hash table (copy of string)
 *\param table Targeted hash table
 *\param key Associated value's key
 *\param string string value to put (will be strdup'ed)
 *\return TRUE or FALSE
 */
int _ht_put_string( HASH_TABLE *table, char *key, char *string )
{
	HASH_NODE *node_ptr = NULL ;

	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );

	if( ( node_ptr = _ht_get_node( table , key ) ) )
	{
		/* let's check the key isn't already assigned with another data type */
		if( node_ptr -> type == HASH_STRING )
		{
			Free( node_ptr -> data . string );
			node_ptr -> data . string = strdup( string );
			return TRUE ;
		}
		n_log( LOG_ERR, "Can't add key[\"%s\"] with type HASH_PTR , key already exist with type %s", node_ptr -> key, ht_node_type( node_ptr ) );
		return FALSE ; /* key registered with another data type */
	}

	node_ptr = _ht_new_string_node( table , key , string );

	unsigned long int index = ( node_ptr -> hash_value )%( table -> size );
	int retcode = list_push( table -> hash_table[ index ], node_ptr , &_ht_node_destroy );
	if( retcode == TRUE )
	{		
		table -> nb_keys ++ ;
	}
	return retcode ;
} /*_ht_put_string */



/*!\fn int _ht_put_string_ptr( HASH_TABLE *table , char *key , char  *string )
 *\brief put a null terminated char *string with given key in the targeted hash table (pointer)
 *\param table Targeted hash table
 *\param key Associated value's key
 *\param string The string to put
 *\return TRUE or FALSE
 */
int _ht_put_string_ptr( HASH_TABLE *table, char *key, char *string )
{
	HASH_NODE *node_ptr = NULL ;

	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );

	if( ( node_ptr = _ht_get_node( table , key ) ) )
	{
		/* let's check the key isn't already assigned with another data type */
		if( node_ptr -> type == HASH_STRING )
		{
			Free( node_ptr -> data . string );
			node_ptr -> data . string = string ;
			return TRUE ;
		}
		n_log( LOG_ERR, "Can't add key[\"%s\"] with type HASH_PTR , key already exist with type %s", node_ptr -> key, ht_node_type( node_ptr ) );
		return FALSE ; /* key registered with another data type */
	}

	node_ptr = _ht_new_string_node( table , key , string );

	unsigned long int index = ( node_ptr -> hash_value )%( table -> size );
	int retcode = list_push( table -> hash_table[ index ], node_ptr , &_ht_node_destroy );
	if( retcode == TRUE )
	{		
		table -> nb_keys ++ ;
	}
	return retcode ;
} /*_ht_put_string_ptr */





/*!\fn int _ht_get_int( HASH_TABLE *table , char *key , int *val )
 *\brief Retrieve an integral value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to a destination integer
 *\return TRUE or FALSE.
 */
int _ht_get_int( HASH_TABLE *table, char *key, int *val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node( table, key );

	if( !node )
		return FALSE ;

	if( node -> type != HASH_INT )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . ival ;

	return TRUE ;
} /* _ht_get_int() */



/*!\fn int _ht_get_double( HASH_TABLE *table , char *key , double *val )
 *\brief Retrieve a double value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to a destination double
 *\return TRUE or FALSE.
 */
int _ht_get_double( HASH_TABLE *table, char * key, double *val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );

	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node( table, key );

	if( !node )
		return FALSE ;

	if( node -> type != HASH_DOUBLE )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_DOUBLE, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . fval ;

	return TRUE ;
} /* _ht_get_double()*/



/*!\fn int _ht_get_ptr( HASH_TABLE *table , char *key , void **val )
 *\brief Retrieve a pointer value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to an empty pointer store
 *\return TRUE or FALSE.
 */
int _ht_get_ptr( HASH_TABLE *table, char *key, void  **val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node( table, key );
	if( !node )
		return FALSE ;

	if( node -> type != HASH_PTR )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_PTR, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . ptr ;

	return TRUE ;
} /* _ht_get_ptr() */



/*!\fn int _ht_get_string( HASH_TABLE *table , char *key , char **val )
 *\brief Retrieve a char *string value in the hash table, at the given key. Leave val untouched if key is not found.
 *\param table Targeted hash table
 *\param key Size Associated value's key
 *\param val A pointer to an empty destination char *string
 *\return TRUE or FALSE.
 */
int _ht_get_string( HASH_TABLE *table, char * key, char  **val )
{
	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	HASH_NODE *node = _ht_get_node( table, key );
	if( !node )
		return FALSE ;

	if( node -> type != HASH_STRING )
	{
		n_log( LOG_ERR, "Can't get key[\"%s\"] of type HASH_STRING, key is type %s", key, ht_node_type( node ) );
		return FALSE ;
	}

	(*val) = node -> data . string ;

	return TRUE ;
} /* _ht_get_string() */



/*!\fn int _ht_remove( HASH_TABLE *table , char *key )
 *\brief Remove a key from a hash table
 *\param table Targeted hash table
 *\param key Key to remove
 *\return TRUE or FALSE.
 */
int _ht_remove( HASH_TABLE *table, char *key )
{
	uint32_t hash_value = 0 ;
	unsigned long int index = 0;

	HASH_NODE *node_ptr = NULL ;
	LIST_NODE *node_to_kill = NULL ;

	__n_assert( table, return FALSE );
	__n_assert( key, return FALSE );
	if( strlen( key ) == 0 )
		return FALSE ;

	MurmurHash3_x86_32( key, strlen( key ), 42, &hash_value );
	index= (hash_value)%(table->size) ;

	if( !table -> hash_table[ index ] -> start )
	{
		n_log( LOG_ERR, "Can't remove key[\"%s\"], table is empty", key );
		return FALSE ;
	}

	list_foreach( list_node, table -> hash_table[ index ] )
	{
		node_ptr = (HASH_NODE *)list_node -> ptr ;
		/* if we found the same */
		if( !strcmp( key, node_ptr -> key ) )
		{
			node_to_kill = list_node ;
			break ;
		}
	}
	if( node_to_kill )
	{
		node_ptr = remove_list_node( table -> hash_table[ index ], node_to_kill, HASH_NODE );
		_ht_node_destroy( node_ptr );

		table -> nb_keys -- ;

		return TRUE ;
	}
	n_log( LOG_ERR, "Can't delete key[\"%s\"]: inexisting key", key );
	return FALSE ;
}/* ht_remove() */



/*!\fn int empty_ht( HASH_TABLE *table )
 *\brief Empty a hash table
 *\param table Targeted hash table
 *\return TRUE or FALSE.
 */
int _empty_ht( HASH_TABLE *table )
{
	HASH_NODE *hash_node = NULL ;

	__n_assert( table, return FALSE );

	unsigned long int index = 0 ;
	for( index = 0 ; index < table -> size ; index ++ )
	{
		while( table -> hash_table[ index ] && table -> hash_table[ index ] -> start )
		{
			hash_node = remove_list_node( table -> hash_table[ index ], table -> hash_table[ index ] -> start, HASH_NODE );
			_ht_node_destroy( hash_node );
		}
	}
	table -> nb_keys = 0 ;
	return TRUE ;
} /* empty_ht */



/*!\fn int destroy_ht( HASH_TABLE **table )
 *\brief Free and set the table to NULL
 *\param table Targeted hash table
 *\return TRUE or FALSE.
 */
int _destroy_ht( HASH_TABLE **table )
{
	__n_assert( table&&(*table), n_log( LOG_ERR, "Can't destroy table: already NULL" ); return FALSE );

	if( (*table )-> hash_table )
	{
		//empty_ht( (*table) );

		unsigned long int it = 0 ;
		for( it = 0 ; it < (*table) -> size ; it ++ )
		{
			if( (*table) -> hash_table[ it ] )
				list_destroy( &(*table) -> hash_table[ it ] );
		}
		Free( (*table )->hash_table );
	}
	Free( (*table ) );
	return TRUE ;
} /* _destroy_ht */


void _ht_print( HASH_TABLE *table )
{
	__n_assert( table , return );
	__n_assert( table -> hash_table , return );
	ht_foreach( node , table )
	{
		printf( "key:%s node:%s\n" , ((HASH_NODE *)node ->ptr)->key , ((HASH_NODE *)node ->ptr)->key );
	}

	return ;
}



LIST *_ht_search( HASH_TABLE *table , char *exp )
{
	__n_assert( table , return NULL );
	__n_assert( exp , return NULL );
	LIST *results = new_generic_list( 0 );
	ht_foreach( node , table )
	{
		HASH_NODE *hnode = (HASH_NODE *)node -> ptr;
		if( !fnmatch( exp , hnode -> key , FNM_NOESCAPE ) )
		{
			list_push( results , strdup( hnode -> key ) , &free );
		}
	}
	if( results -> nb_items < 1 )
		list_destroy( &results );
	return results ;
}



/*!\fn HASH_TABLE *new_ht_trie( unsigned int alphabet_size , unsigned int alphabet_offset )
 *\brief Create a hash table with the given size
 *\param size Size of the root hash node table
 *\return NULL or the new allocated hash table
 */
HASH_TABLE *new_ht_trie( unsigned int alphabet_length , unsigned int alphabet_offset )
{
	HASH_TABLE *table = NULL ;

	Malloc( table, HASH_TABLE, 1 );
	__n_assert( table, n_log( LOG_ERR, "Error allocating HASH_TABLE *table" ); return NULL );

	table -> size = 0 ;
	table -> seed = 0 ;
	table -> nb_keys = 0 ;
	errno = 0 ;
	table -> hash_table = NULL ;

	table -> ht_put_int        = _ht_put_int_trie ;
	table -> ht_put_double     = _ht_put_double_trie ;
	table -> ht_put_ptr        = _ht_put_ptr_trie ;
	table -> ht_put_string     = _ht_put_string_trie ;
	table -> ht_put_string_ptr = _ht_put_string_ptr_trie ;
	table -> ht_get_int        = _ht_get_int_trie ;
	table -> ht_get_double     = _ht_get_double_trie ;
	table -> ht_get_string     = _ht_get_string_trie ;
	table -> ht_get_ptr        = _ht_get_ptr_trie ;
	table -> ht_remove         = _ht_remove_trie ;
	table -> ht_search         = _ht_search_trie ;
	table -> empty_ht          = _empty_ht_trie ;
	table -> destroy_ht        = _destroy_ht_trie ;
	table -> ht_print          = _ht_print_trie ;

	table -> alphabet_length = alphabet_length ;
	table -> alphabet_offset = alphabet_offset ;

	table -> root =	_ht_new_node_trie( table , '\0' );
	table -> mode = HASH_TRIE ;

	return table ;
} /* new_ht_trie */

/*!\fn HASH_TABLE *new_ht( int size )
 *\brief Create a hash table with the given size
 *\param size Size of the root hash node table
 *\return NULL or the new allocated hash table
 */
HASH_TABLE *new_ht( unsigned long int size )
{
	HASH_TABLE *table = NULL ;

	if( size < 1 )
	{
		n_log( LOG_ERR, "Invalide size %d for new_ht()", size );
		return NULL ;
	}
	Malloc( table, HASH_TABLE, 1 );
	__n_assert( table, n_log( LOG_ERR, "Error allocating HASH_TABLE *table" ); return NULL );

	table -> size = size ;
	table -> seed = rand()%1000 ;
	table -> nb_keys = 0 ;
	errno = 0 ;
	Malloc( table -> hash_table, LIST *, size );
	// table -> hash_table = (LIST **)calloc( size, sizeof( LIST *) );
	__n_assert( table -> hash_table, n_log( LOG_ERR, "Can't allocate table -> hash_table with size %d !", size ); Free( table ); return NULL );

	unsigned long int it = 0 ;
	for(  it = 0 ; it < size ; it ++ )
	{
		table -> hash_table[ it ] = new_generic_list( 0 );
		// if no valid table then unroll previsouly created slots
		if( !table -> hash_table[ it ] )
		{
			n_log( LOG_ERR, "Can't allocate table -> hash_table[ %d ] !", it );
			unsigned long int it_delete = 0 ;
			for( it_delete = 0 ; it_delete < it ; it_delete ++ )
			{
				list_destroy( &table -> hash_table[ it_delete ] );
			}
			Free( table -> hash_table );
			Free( table );
			return NULL ;
		}
	}
	table -> mode = HASH_CLASSIC ;

	table -> ht_put_int        = _ht_put_int ;
	table -> ht_put_double     = _ht_put_double ;
	table -> ht_put_ptr        = _ht_put_ptr ;
	table -> ht_put_string     = _ht_put_string ;
	table -> ht_put_string_ptr = _ht_put_string_ptr ;
	table -> ht_get_int        = _ht_get_int ;
	table -> ht_get_double     = _ht_get_double ;
	table -> ht_get_string     = _ht_get_string ;
	table -> ht_get_ptr        = _ht_get_ptr ;
	table -> ht_get_node        = _ht_get_node;
	table -> ht_remove         = _ht_remove ;
	table -> ht_search         = _ht_search ;
	table -> empty_ht          = _empty_ht ;
	table -> destroy_ht        = _destroy_ht ;
	table -> ht_print          = _ht_print ;

	return table ;
} /* new_ht */

HASH_NODE *ht_get_node( HASH_TABLE *table, char *key )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_get_node( table , key );
}
 
int ht_get_double( HASH_TABLE *table, char * key, double *val )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_get_double( table , key , val );
}
int ht_get_int( HASH_TABLE *table, char *key, int *val )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_get_int( table , key , val );
}
int ht_get_ptr( HASH_TABLE *table, char * key, void  **val )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table -> ht_get_ptr( table , key , &(*val) );
}
int ht_get_string( HASH_TABLE *table, char * key, char  **val )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_get_string( table , key , val );
}
int ht_put_double( HASH_TABLE *table, char *key, double value )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_put_double( table , key , value );
}
int ht_put_int( HASH_TABLE *table, char * key, int value )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_put_int( table , key , value );
}
int ht_put_ptr( HASH_TABLE *table, char *key, void  *ptr, void (*destructor)(void *ptr ) )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_put_ptr( table , key , ptr , destructor );
}
int ht_put_string( HASH_TABLE *table, char *key, char *string )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_put_string( table , key , string );
}
int ht_put_string_ptr( HASH_TABLE *table, char *key, char *string )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_put_string_ptr( table , key , string );
}
int ht_remove( HASH_TABLE *table, char *key )
{
	__n_assert( table , return FALSE );
	__n_assert( key , return FALSE );
	return table->ht_remove( table , key );
}
void ht_print( HASH_TABLE *table )
{
	__n_assert( table , return );
	table->ht_print( table );
	return ;
}

LIST *ht_search( HASH_TABLE *table , char *exp )
{
	__n_assert( table , return FALSE );
	__n_assert( exp , return FALSE );
	return table -> ht_search( table , exp );
}

int empty_ht( HASH_TABLE *table )
{
	__n_assert( table , return FALSE );
	table -> empty_ht( table );
	return FALSE;
}

int destroy_ht( HASH_TABLE **table )
{
	__n_assert( (*table) , return FALSE );
	(*table)->destroy_ht( table );
	return FALSE;
}




