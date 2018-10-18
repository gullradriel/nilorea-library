/**\file n_config_file.c
 *  common function
 *\author Castagnier Mickael
 *\version 1.0
 *\date 24/03/05
 */

#include "nilorea/n_common.h"
#include "nilorea/n_config_file.h"


/*!\fn void destroy_config_file_section( void *ptr )
 *\brief Destroy a config file section 
 *\param ptr A pointer to a config file section 
 *\return void
 */
void destroy_config_file_section( void *ptr )
{
	CONFIG_FILE_SECTION *section = (CONFIG_FILE_SECTION *)ptr;
	if( !section )
		return ;
	Free( section -> section_name );
	destroy_ht( &section -> entries );
	Free( section );
	return ;
} /* destroy_config_file_section(...) */



/*!\fn CONFIG_FILE *load_config_file( char *filename , int *errors )
 *\brief load a config file
 *\param filename Filename of the config file
 *\param errors pointer to an int which will represent the number of errors encountered when reading config file
 *\return NULL or a loaded config file
 */
CONFIG_FILE *load_config_file( char *filename , int *errors )
{  
	CONFIG_FILE *cfg_file = NULL ;
	CONFIG_FILE_SECTION *section = NULL ;
	char buffer[ MAX_CONFIG_LINE_LEN ] = "" ;
	char **split_result = NULL ;
	int error = 0 ;
	FILE *in = NULL ;

	__n_assert( filename , return NULL );
	in = fopen( filename , "r" );
	error = errno ;
	if( !in )
	{
		n_log( LOG_ERR , "Unable to open %s: %s" , _str( filename ) , strerror( error ) );
		return NULL ;
	}
	Malloc( cfg_file , CONFIG_FILE , 1 );
	if( !cfg_file )
	{
		fclose( in );
		return NULL ;
	}
	cfg_file -> sections = new_generic_list( 0 );
	if( !cfg_file -> sections )
	{
		Free( cfg_file );
		fclose( in );
		return NULL ;
	}
	cfg_file -> filename = strdup( filename );
	if( !cfg_file -> filename )
	{
		n_log( LOG_ERR , "couldn't strdup( %s )" , _str( filename ) );
		Free( cfg_file );
		fclose( in );
		return NULL ;
	}

	int line_number = 0 ;
	while( fgets( buffer , MAX_CONFIG_LINE_LEN , in ) )
	{
		NSTRBYTE start = 0 , end = 0 ;

		char *bufptr = (char *)&buffer ;
		line_number ++ ;

		bufptr = trim_nocopy( buffer );
		if( strlen( bufptr ) == 0 )
		{
			n_log( LOG_DEBUG , "empty config line" );
			continue ;
		}

		n_log( LOG_DEBUG , "RAWCFG:'%s'" , bufptr ); 

		switch( bufptr[ 0 ] )
		{
			/* comment */
			case '#' :
				n_log( LOG_DEBUG , "comment" );
				continue ;
			case ';' :
				n_log( LOG_DEBUG , "comment" );
				continue ;
				/* new section */
			case '[' :
				end = 0 ;
				/* go to begin of section name */
				if( skipu( bufptr , ']' , &end , 1 ) != TRUE )
				{
					n_log( LOG_ERR , "coulnd't find end of section at line %d of %s (string:%s)" , line_number , filename , buffer ); 
					continue ;
				}
				/* keep only the name */
				bufptr ++ ;
				bufptr[ end - 1 ] = '\0' ;
				bufptr = trim_nocopy( bufptr );
				n_log( LOG_DEBUG , "%s" , bufptr );
				if( strlen( bufptr ) == 0 )
				{
					n_log( LOG_ERR , "section without name at line %d" , line_number );
					continue ;
				}

				Malloc( section  , CONFIG_FILE_SECTION , 1 );
				__n_assert( section , break );

				section -> section_name = strdup( bufptr );
				if( !section -> section_name )
				{
					n_log( LOG_ERR , "couldn't duplicate %s" , bufptr );
					continue ;
				}

				section -> entries = new_ht( CONFIG_SECTION_HASH_TABLE_LEN );
				if( !section -> entries )
				{
					n_log( LOG_ERR , "error creating [%s] hash table of size %d" , bufptr , CONFIG_SECTION_HASH_TABLE_LEN );
					continue ;
				}
				n_log( LOG_DEBUG , "[%s] created" , section -> section_name );
				list_push( cfg_file -> sections , section , &destroy_config_file_section );
				continue ;

				/* if it's not a comment, not a section, not empty, then it's an entry ! */
			default:
				if( !section )
				{
					n_log( LOG_ERR , "entry without section at line %d of %s (string:%s)" , line_number , filename , bufptr ); 
					continue ;
				}
				split_result = split( bufptr , "=" , 0 );
				if( !split_result )
				{
					n_log( LOG_ERR , "couldn't find entry separator '=' at line %d of %s (string:%s)" , line_number , filename , bufptr ); 
					continue ;
				}
				if( strlen( trim_nocopy( split_result[ 0 ] ) ) == 0 )
				{
					free_split_result( &split_result );
					n_log( LOG_ERR , "couldn't find entry name at line %d of %s (string:%s)" , line_number , filename , (char *)buffer[ start ] ); 
					continue ;
				}

				if( strlen( trim_nocopy( split_result[ 1 ] ) ) == 0 )
				{
					free_split_result( &split_result );
					n_log( LOG_ERR , "couldn't find value for entry at line %d of %s (string:%s)" , line_number , filename ,  (char *)buffer[ start ] ); 
					continue ;
				}
				ht_put_ptr( section -> entries , trim_nocopy( split_result[ 0 ] ) , strdup( trim_nocopy( split_result[ 1 ] ) ) , free ); 
				n_log( LOG_DEBUG , "[%s]%s=%s" , section -> section_name , trim_nocopy( split_result[ 0 ] ) , trim_nocopy( split_result[ 1 ] ) );
				free_split_result( &split_result );
				continue ;
		}
	}
	if( !feof( in ) )
	{
		n_log( LOG_ERR , "couldn't read EOF for %s" , filename );
	}
	fclose( in );

	n_log( LOG_DEBUG , "config_file %s returned %p" , filename , cfg_file );

	return cfg_file ;

} /*load_config_file */



/*!\fn int get_nb_config_file_sections( CONFIG_FILE *cfg_file , char *section_name )
 *\brief Get the number of config file with section_name
 *\param cfg_file Config file to process 
 *\param section_name name of sections to search 
 *\return The number of named sections
 */
int get_nb_config_file_sections( CONFIG_FILE *cfg_file , char *section_name )
{
	__n_assert( cfg_file , return -1 );
	__n_assert( cfg_file -> sections , return -2 );
	__n_assert( section_name , return -3 );

	int nb_sections = 0 ;
	list_foreach( listnode , cfg_file -> sections )
	{
		CONFIG_FILE_SECTION *section = (CONFIG_FILE_SECTION *)listnode -> ptr ;
		if( !strcmp( section -> section_name , section_name ) )
		{
			nb_sections ++ ;
		}
	}
	return nb_sections ;
} /* get_nb_config_file_sections(...) */


/*!\fn char *get_config_section_value( CONFIG_FILE *cfg_file , char *section_name , int position , char *entry )
 *\brief Function to parse sections and get entries values
 *\param cfg_file name of config file
 *\param section_name name of section
 *\param position section number
 *\param entry entry name
 *\return the value of the named entry in section[ position ]
 */
char *get_config_section_value( CONFIG_FILE *cfg_file , char *section_name , int position , char *entry )
{
	__n_assert( cfg_file , return NULL );
	__n_assert( cfg_file -> sections , return NULL );
	__n_assert( section_name , return NULL );
	__n_assert( entry , return NULL );

	if( position >= cfg_file -> sections -> nb_items )
	{
		n_log( LOG_DEBUG , "position (%d) is higher than the number of item in list (%d)" , position ,  cfg_file -> sections -> nb_items );
		return NULL ;
	}

	int section_position = 0 ;
	HASH_TABLE *entries = NULL ;
	list_foreach( listnode , cfg_file -> sections )
	{
		CONFIG_FILE_SECTION *section = (CONFIG_FILE_SECTION *)listnode -> ptr ;
		if( !strcmp( section -> section_name , section_name ) )
		{
			if( section_position == position )
			{
				entries = section -> entries ;
				break ;
			}
			section_position ++ ;
		}
	}

	char *result = NULL ;
	if( ht_get_ptr( entries , entry , (void **)&result )  == FALSE )
	{
		n_log( LOG_DEBUG , "ht_get_ptr( %p , trim_nocopy( %s ) , (void **)&result ) returned FALSE !" , entries , entry );
		return NULL ;
	}

	return result ;
} /* get_config_section_value */



/*!\fn int destroy_config_file( CONFIG_FILE **cfg_file )
 *\brief Destroy a loaded config file
 *\param cfg_file pointer to a loaded config file to destroy
 *\return TRUE or FALSE
 */
int destroy_config_file( CONFIG_FILE **cfg_file )
{
	__n_assert( (*cfg_file) , return FALSE );

	Free( (*cfg_file) -> filename );

	if( (*cfg_file) -> sections )
	{
		list_destroy( &(*cfg_file) -> sections );
	}

	Free( (*cfg_file) );

	return TRUE ;
} /* destroy_config_file */



/* put/update a config value */ 
/*int put_cfg_value( CONFIG_FILE *cfg_file , char *section , char *entry , char *val );*/



/* write file from config */
/* int write_config_file( CONFIG_FILE *cfg_file , char *filename );*/


