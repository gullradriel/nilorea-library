/**\file n_config_file.h
 *  config file reading
 *\author Castagnier Mickael
 *\version 1.0
 *\date 24/03/05
 */

#ifndef __N_CONFIG_FILE__
#define __N_CONFIG_FILE__

#ifdef __cplusplus
extern "C"
{
#endif

#include "nilorea/n_hash.h"
#include "nilorea/n_str.h"
#include "nilorea/n_log.h"

	/*! Maximum config line length */
#define MAX_CONFIG_LINE_LEN 1024
	/*! hash table size for config section (note that it's just a hash table size, not the maximum number of element in the section ) */
#define CONFIG_SECTION_HASH_TABLE_LEN 16

	/**\defgroup CONFIGFILE CONFIGFILE: classic config file loading/saving 
	  \addtogroup CONFIGFILE
	  @{
	  */

	/*! Structure of a config section */
	typedef struct CONFIG_FILE_SECTION
	{
		/*! section name */
		char *section_name ;
		/*! table of section entries */
		HASH_TABLE *entries ;
	} CONFIG_FILE_SECTION ;


	/*! Structure of a config file */
	typedef struct CONFIG_FILE
	{
		/*! config file filename */
		char *filename ;
		/*! list of config file section */
		LIST *sections ;
	} CONFIG_FILE ;

	/* load a config from a file */
	CONFIG_FILE *load_config_file( char *filename , int *errors );
	/* get number of named config file sections */
	int get_nb_config_file_sections( CONFIG_FILE *cfg_file , char *section );
	/* get a loaded config value */
	char *get_config_section_value( CONFIG_FILE *cfg_file , char *section_name , int position , char *entry );
	/* destroy a loaded section */
	void destroy_config_file_section( void *ptr );
	/* destroy a loaded config */
	int destroy_config_file( CONFIG_FILE **cfg_file );

	/* write file from config */
	/* int write_config_file( CONFIG_FILE *cfg_file , char *filename ); */
	/* put/update a config value */ 
	/* int put_cfg_value( CONFIG_FILE *cfg_file , char *section , char *entry , char *val ); */



	/*@}*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __N_CONFIG_FILE__ */

