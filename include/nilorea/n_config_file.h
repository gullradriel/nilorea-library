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

#include "n_hash.h"
#include "n_str.h"
#include "n_log.h"

#define MAX_CONFIG_LINE_LEN 1024
#define CONFIG_SECTION_HASH_TABLE_LEN 16

   /**\defgroup CONFIGFILE CONFIGFILE: classic config file loading/saving 
     \addtogroup CONFIGFILE
     @{
     */
   
   /*! Structure of a config section */
   typedef struct CONFIG_FILE_SECTION
   {
      char *section_name ;
      HASH_TABLE *entries ;
   } CONFIG_FILE_SECTION ;


   /*! Structure of a config file */
   typedef struct CONFIG_FILE
   {
      char *filename ;
      LIST *sections ;
   } CONFIG_FILE ;

   /* load a config from a file */
   CONFIG_FILE *load_config_file( char *filename );
   /* write file from config */
   int write_config_file( CONFIG_FILE *cfg_file , char *filename );
   /* get a loaded config value */
   /*int get_cfg_value( CONFIG_FILE *cfg_file , char *section , char *entry );*/
   /* put/update a config value */ 
   /* int put_cfg_value( CONFIG_FILE *cfg_file , char *section , char *entry , char *val ); */
   /* destroy a laoded config */
   int destroy_config_file( CONFIG_FILE *cfg_file );

   /*@}*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __N_CONFIG_FILE__ */

