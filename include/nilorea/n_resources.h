/**\file n_resources.h
 *  Resources header for dialogs, games, applications
 *\author Castagnier Mickael
 *\version 1.0
 *\date 01/1/2019
 */

#ifndef __N_RESOURCES_HEADER
#define __N_RESOURCES_HEADER

#ifdef __cplusplus
extern "C"
{
#endif

#include "nilorea/n_hash.h"

/**\defgroup RESOURCES RESOURCES: generic data handling type
   \addtogroup RESOURCES
  @{
*/


#define RESOURCE_KEY_TYPES_HASH_SIZE 128
#define RESOURCE_ENTRY_HASH_SIZE 512


#define RESOURCE_BITMAP 0
#define RESOURCE_SAMPLE 1
#define RESOURCE_FONT 2
#define RESOURCE_DATA 3

#define RESOURCE_NB_TYPES 4

typedef struct RESOURCE_ENTRY
{
    int type ;
    size_t size ;
    void *ptr ;
} RESOURCE_ENTRY;

typedef struct RESOURCES
{
    HASH_TABLE *table[RESOURCE_NB_TYPES] ;
} RESOURCES ;

RESOURCE_ENTRY *resource_entry_new( int type, void *ptr, size_t size );
void resource_entry_delete( RESOURCE_ENTRY *resource_entry );
RESOURCES *resource_new( void );
void resource_delete( RESOURCES *resource );

int add_item_to_resource( RESOURCES *resource, int type, char *id, void *ptr, size_t size );
RESOURCE_ENTRY *get_item_from_resource( RESOURCES *resource, int type, char *id );

int resource_load_data( RESOURCES *resource, char *filename, char *id );

/* ALLEGRO5 */
int resource_load_bitmap( RESOURCES *resource, char *bitmap, char *id );
int resource_load_sample( RESOURCES *resource, char *sample, char *id );
int resource_load_font( RESOURCES *resource, char *font, char *id );

/*@}*/

#ifdef __cplusplus
}
#endif

#endif
