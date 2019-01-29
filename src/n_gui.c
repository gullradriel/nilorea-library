/**\file n_gui.c
*
*  GUI source for Nilorea/Allegro5
*
*\author Castagnier Mickaël aka Gull Ra Driel
*
*\version 1.0
*
*\date 03/01/2019
*
*/

#include "nilorea/n_gui.h"
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_config_file.h"


int ngui_cmp_item( void *a, void *b )
{
    NGUI_ITEM *item_a = (NGUI_ITEM *)a;
    NGUI_ITEM *item_b = (NGUI_ITEM *)b;
    if( item_a -> z > item_b -> z )
        return -1 ;
    if( item_a -> z < item_b -> z )
        return 1 ;
    return 0 ;
}

/*!\fn int ngui_sort_dialog( NGUI_DIALOG *dialog )
 *\brief sort items in dialogs
 *\param dialog dialog object to sort (this is recursive through dialog list)
  *\return NULL or the given item
 */
int ngui_sort_dialog( NGUI_DIALOG *dialog )
{
    /*list_foreach( )*/
} /* ngui_sort_dialog */



/*!\fn NGUI_ITEM *ngui_new_item( DIALOG *dialog , char *id )
 *\brief new ngui item
 *\param dialog The dialog in which to add the item
 *\param id The item id inside the dialog
 *\return NULL or a pointer to the new allocated NGUI_ITEM
 */
NGUI_ITEM *ngui_new_item( NGUI_DIALOG *dialog, char *id )
{
    NGUI_ITEM *item = NULL ;
    __n_assert( dialog, return NULL );
    __n_assert( id, return NULL );
    void *ptr = NULL ;
    list_foreach( node, dialog -> item_list )
    {
        NGUI_ITEM *item = node -> ptr ;
        if( item ->id && strstr( item ->id, id ) )
        {
            n_log( LOG_DEBUG, "dialog: %s item %s already exist !", dialog -> name, id );
            return NULL ;
        }
    }

    Malloc( item, NGUI_ITEM, 1 );
    __n_assert( item, return NULL );

    item -> id = strdup( id );
    item -> dialog = dialog ;

    if( list_push_sorted( dialog -> item_list, item, &ngui_cmp_item, &ngui_delete_item ) == FALSE )
    {
        n_log( LOG_ERR, "dialog: %s impossible to add item %s", dialog -> name, item -> id );
        ngui_delete_item( item );
        item = NULL ;
    }
    return item ;
} /* ngui_new_item */



/*!\fn void ngui_delete_item( void *item )
 *\brief delete ngui item
 *\param item pointer to item
 */
void ngui_delete_item( void *item )
{
    __n_assert( item, return );
    NGUI_ITEM *ngui_item = (NGUI_ITEM *)item;
    if( ngui_item -> text )
        FreeNoLog( ngui_item -> text );
    if( ngui_item -> id )
        FreeNoLog( ngui_item -> id );
    Free( ngui_item );
} /* ngui_delete_item */



/*!\fn NGUI_ITEM *ngui_get_item( NGUI_DIALOG *dialog , char *id )
 *\brief get item pointer from dialog
 *\param dialog Dialog from which to get the item
 *\param id Id of the item to retrieve
 *\return NULL or the given item
 */
NGUI_ITEM *ngui_get_item( NGUI_DIALOG *dialog, char *id )
{
    __n_assert( dialog, return NULL );
    __n_assert( id, return NULL );

    NGUI_ITEM *item = NULL ;
    if( ht_get_ptr( dialog -> item_list, id, (void **)&item ) == FALSE )
    {
        n_log( LOG_DEBUG, "dialog: %s item %s does not exist !", dialog -> name, id );
        return NULL ;
    }
    return item ;
} /* ngui_get_item */



/*!\fn int ngui_set_item_callback( NGUI_ITEM *item , int state , void (*callback)(void *data) )
 *\brief set function callback on item state
 *\param item aimed item
 *\param state NGUI state on which to add a callback
 *\param callback callback to add
 *\return TRUE or FALSE
 */
int ngui_set_item_callback( NGUI_ITEM *item, int state, void (*callback)(void *data) )
{
    __n_assert( item, return FALSE );

    if( state < 0 || state >= NGUI_NBSTATES )
    {
        n_log( LOG_ERR, "dialog: %s item: %s, cannot set callback, state %d unknown", item -> dialog -> name, item -> id, state );
        return FALSE ;
    }
    item -> callback[ state ] = callback ;

    return TRUE ;
} /* ngui_set_item_callback */



/*!\fn int ngui_set_item_sound( NGUI_ITEM *item , int state , ALLEGRO_SAMPLE *sample )
 *\brief set sound on item state
 *\param item aimed item
 *\param state NGUI state on which to add a sound
 *\param sample The sound to add
 *\return TRUE or FALSE
 */
int ngui_set_item_sound( NGUI_ITEM *item, int state, ALLEGRO_SAMPLE *sample )
{
    __n_assert( item, return FALSE );

    if( state < 0 || state >= NGUI_NBSTATES )
    {
        n_log( LOG_ERR, "dialog: %s item: %s, cannot set sample, state %d unknown", item -> dialog -> name, item -> id, state );
        return FALSE ;
    }
    item ->sound[ state ] = sample ;

    return TRUE ;
} /* ngui_set_item_sound */



/*!\fn int ngui_set_item_bitmap( NGUI_ITEM *item , int state , ALLEGRO_BITMAP *bitmap )
 *\brief set bitmap on item state
 *\param item aimed item
 *\param state NGUI state on which to add a bitmap
 *\param bitmap The bitmap to add
 *\return TRUE or FALSE
 */
int ngui_set_item_bitmap( NGUI_ITEM *item, int state,ALLEGRO_BITMAP *bitmap )
{
    __n_assert( item, return FALSE );

    if( state < 0 || state >= NGUI_NBSTATES )
    {
        n_log( LOG_ERR, "dialog: %s item: %s, cannot set bitmap, state %d unknown", item -> dialog -> name, item -> id, state );
        return FALSE ;
    }
    item ->bmp[ state ] = bitmap ;

    return TRUE ;
} /* ngui_set_item_bitmap */



/*!\fn int ngui_set_item_positition( NGUI_ITEM *item , int x , int y )
 *\brief set item position
 *\param item aimed item
 *\param x position inside the dialog
 *\param y position inside the dialog
 *\return TRUE or FALSE
 */
int ngui_set_item_positition( NGUI_ITEM *item, int x, int y )
{
    __n_assert( item, return FALSE );

    item -> x = x ;
    item -> y = y ;

    return TRUE ;
} /* ngui_set_item_positition */



/*!\fn int ngui_set_item_size( NGUI_ITEM *item , int w , int h )
 *\brief set item position
 *\param item aimed item
 *\param w item's width
 *\param h item's height
 *\return TRUE or FALSE
 */
int ngui_set_item_size( NGUI_ITEM *item, int w, int h )
{
    __n_assert( item, return FALSE );

    if( w < 1 || h < 1 )
    {
        n_log( LOG_ERR, "dialog: %s item: %s, cannot set size, invalid w: %d and h: %d", item -> dialog -> name, item -> id, w, h );
        return FALSE ;
    }

    item -> w = w ;
    item -> h = h ;

    return TRUE ;
} /* ngui_set_item_size */


/*!\fn int ngui_set_item_alignement( NGUI_ITEM *item , int alignement )
 *\brief set item position
 *\param item aimed item
 *\param alignement alignement inside the dialog
 *\return TRUE or FALSE
 */
/* set item alignment flag */
int ngui_set_item_alignement( NGUI_ITEM *item, int alignment )
{
    __n_assert( item, return FALSE );

    if( alignment < 0 ||alignment > 4 )
    {
        n_log( LOG_ERR, "dialog: %s item: %s, cannot set alignment, alignment %d unknown", item -> dialog -> name, item -> id, alignment );
        return FALSE ;
    }
    item -> alignment = alignment ;

    return TRUE ;
} /* ngui_set_item_alignement */



/*!\fn int ngui_set_item_text_alignement( NGUI_ITEM *item , int text_alignment )
 *\brief set item position
 *\param item aimed item
 *\param text_alignment text alignment inside the dialog
 *\return TRUE or FALSE
 */
int ngui_set_item_text_alignement( NGUI_ITEM *item, int text_alignment )
{
    __n_assert( item, return FALSE );

    if( text_alignment < 0 ||text_alignment > 4 )
    {
        n_log( LOG_ERR, "dialog: %s item: %s, cannot set alignment, alignment %d unknown", item -> dialog -> name, item -> id, text_alignment );
        return FALSE ;
    }
    item -> text_alignment = text_alignment ;

    return TRUE ;
} /* ngui_set_item_text_alignement */



/*!\fn NGUI_DIALOG *ngui_new_dialog( char *name , int x , int y , int w , int h )
 *\brief create new dialog
 *\param name Name of dialog
 *\param x position x on screen
 *\param y position y on screen
 *\param w width of dialog
 *\param h size of dialog
 *\return A new allocated dialog
 */
NGUI_DIALOG *ngui_new_dialog( char *name, int x, int y, int z,int w, int h )
{
    __n_assert( name, return NULL );
    if( w < 1 || h < 1 )
    {
        n_log( LOG_ERR, "dialog: %s invalid size w: %d h: %d", name, w, h );
        return FALSE ;
    }
    NGUI_DIALOG *dialog = NULL ;
    Malloc( dialog, NGUI_DIALOG, 1 );
    __n_assert( dialog, return NULL );

    dialog -> item_list = new_ht( NGUI_DIALOG_ITEM_HASH_SIZE );
    dialog -> dialog_list = new_ht( NGUI_DIALOG_LIST_HASH_SIZE );

    dialog -> x = x ;
    dialog -> y = y ;
    dialog -> w = w ;
    dialog -> h = h ;
    dialog -> decorations = 0 ;
    dialog -> soft_angles = 0 ;
    dialog -> hidden = 0 ;

    for( int it = 0 ; it < NGUI_NBSTATES ; it ++ )
    {
        dialog -> bg[ it ] = al_map_rgba( 0, 0, 0, 0 );
    }
    dialog -> resources = NULL ;
    dialog -> name = strdup( name );

    return dialog ;
} /* ngui_new_dialog */



/*!\fn void ngui_delete_dialog( void *dialog )
 *\brief delete a dialog
 *\param dialog pointer to the dialog to delete
 */
void ngui_delete_dialog( void *dialog )
{
    __n_assert( dialog, return );
    NGUI_DIALOG *ngui_dialog = (NGUI_DIALOG *)dialog ;
    if( ngui_dialog -> item_list )
        destroy_ht( &ngui_dialog -> item_list );
    if( ngui_dialog -> dialog_list )
        destroy_ht( &ngui_dialog -> dialog_list );
    FreeNoLog( ngui_dialog -> name );
    /* free resources here */
    FreeNoLog( ngui_dialog );
} /* ngui_delete_dialog */



/*!\fn NGUI_DIALOG *ngui_load_from_file( char *file )
 *\brief load a dialog from a file
 *\param file The filename containing the dialog scheme
 *\return NULL or a loaded dialog scheme
 */
NGUI_DIALOG *ngui_load_from_file( char *file )
{
    __n_assert( file, return NULL );
    int errors = 0 ;

    CONFIG_FILE *config = load_config_file( file, &errors );
    if( !config )
    {
        n_log( LOG_ERR, "Unable to load config file from %s", file );
    }
    if( errors != 0 )
    {
        n_log( LOG_ERR, "There were %d errors in %s. Check the logs !", errors, file );

    }
    /* get a loaded config value */
    int nb = get_nb_config_file_sections( config, "DIALOG" );
    n_log( LOG_DEBUG, "%d DIALOG sections", nb );
    for( int it = 0 ; it < nb ; it++ )
    {
        n_log( LOG_DEBUG, "[DIALOG]" );
        char *value = NULL ;
        value = get_config_section_value( config, "DIALOG", it, "name", 0 );
        n_log( LOG_INFO, "check_interval:%s", (value!=NULL)?value:"NULL" );
        value = get_config_section_value( config, "DIALOG", it, "x", 0 );
        n_log( LOG_INFO, "refresh_interval:%s",  (value!=NULL)?value:"NULL" );
        value = get_config_section_value( config, "DIALOG", it, "y", 0 );
        n_log( LOG_INFO, "cache_file:%s", (value!=NULL)?value:"NULL" );
        value = get_config_section_value( config, "DIALOG", it, "w", 0 );
        n_log( LOG_INFO, "cache_file_swp:%s", (value!=NULL)?value:"NULL" );
        value = get_config_section_value( config, "DIALOG", it, "h", 0 );
        n_log( LOG_INFO, "cache_refresh_interval:%s", (value!=NULL)?value:"NULL" );
        value = get_config_section_value( config, "DIALOG", it, "hide", 0 );
        n_log( LOG_INFO, "cache_refresh_interval:%s", (value!=NULL)?value:"NULL" );
        value = get_config_section_value( config, "DIALOG", it, "hide", 0 );
        n_log( LOG_INFO, "cache_refresh_interval:%s", (value!=NULL)?value:"NULL" );
        int nb_cmd = get_nb_config_file_sections_entries( config, "SECTION_FILE_MULTICMD", it, "command" );
        for( int it1 = 0 ; it1 < nb_cmd ; it1 ++ )
        {
            value = get_config_section_value( config, "SECTION_FILE_MULTICMD", it, "command", it1 );
            n_log( LOG_INFO, "command:%s", (value!=NULL)?value:"NULL" );
        }

    }
}

/*!\fn NGUI_DIALOG *ngui_load_from_file( char *file )
 *\brief load a dialog from a file
 *\param file The filename containing the dialog scheme
 *\return NULL or a loaded dialog scheme
 */
NGUI_DIALOG *ngui_load_from_file( char *file );


/*!\fn int ngui_manage( NGUI_DIALOG *dialog );
 *\brief process dialog logic
 *\param dialog the dialog to process
 *\return TRUE or FALSE
 */
int ngui_manage( NGUI_DIALOG *dialog );


/*!\fn int ngui_draw( NGUI_DIALOG *dialog );
 *\brief draw a dialog scene on the current bitmap buffer
 *\param dialog the dialog to draw
 *\return TRUE or FALSE
 */
int ngui_draw( NGUI_DIALOG *dialog );


/*!\fn int ngui_destroy( NGUI_DIALOG **dialog );
 *\brief destroy a loaded dialog
 *\param dialog pointer to the dialog to destroy
 *\return TRUE or FALSE
 */
int ngui_destroy( NGUI_DIALOG **dialog );


