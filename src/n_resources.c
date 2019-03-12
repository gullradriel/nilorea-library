/**\file n_resources.c
 *  Resources sources for dialogs, games, applications
 *\author Castagnier Mickael
 *\version 1.0
 *\date 01/1/2019
 */

#include "nilorea/n_resources.h"
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"

#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>

RESOURCE_ENTRY *resource_entry_new( int type, void *ptr, size_t size )
{
    RESOURCE_ENTRY *item = NULL ;
    Malloc( item, RESOURCE_ENTRY, 1 );
    __n_assert( item, return NULL );
    item -> type = type ;
    item -> ptr = ptr ;
    item -> size = size ;
    return item ;
}

void resource_entry_delete( RESOURCE_ENTRY *resource_entry )
{
    __n_assert( resource_entry, return );
    switch( resource_entry -> type )
    {
    case RESOURCE_BITMAP:
        if( resource_entry -> ptr )
            al_destroy_bitmap( resource_entry -> ptr );
        break;
    case RESOURCE_SAMPLE:
        if( resource_entry -> ptr )
            al_destroy_sample( resource_entry -> ptr );
        break;
    case RESOURCE_FONT:
        if( resource_entry -> ptr )
            al_destroy_font( resource_entry -> ptr );
        break;
    }
    Free( resource_entry );
    return ;
}
RESOURCES *resource_new( void );
void resource_delete( RESOURCES *resource );

int add_item_to_resource( RESOURCES *resource, int type, char *id, void *ptr, size_t size );
RESOURCE_ENTRY *get_item_from_resource( RESOURCES *resource, int type, char *id );

int resource_load_data( RESOURCES *resource, char *filename, char *id );

/* ALLEGRO5 */
int resource_load_bitmap( RESOURCES *resource, char *bitmap, char *id );
int resource_load_sample( RESOURCES *resource, char *sample, char *id );
int resource_load_font( RESOURCES *resource, char *font, char *id );
