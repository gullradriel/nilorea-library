/**\file anim.c
*
*  anim functions
*
*\author Castagnier Mickaël aka Gull Ra Driel
*
*\version 1.0
*
*\date 30/12/2016
*
*/

#include "nilorea/n_anim.h"

#include "math.h"

ANIM_LIB *create_anim_library( char *name, unsigned int size )
{
    if( size < 1 )
    {
        n_log( LOG_ERR, "Invalid size (<1) for anim lib creation" );
        return NULL ;
    }

    ANIM_LIB *lib = NULL ;
    Malloc( lib, ANIM_LIB, 1 );
    __n_assert( lib, return NULL );

    lib -> gfxs = NULL ;
    Malloc( lib -> gfxs, ANIM_GFX *, size );
    if( !lib -> gfxs )
    {
        n_log( LOG_ERR, "Unable to allocate a gfx_lib of size %d", size );
        return NULL ;
    }

    if( name )
        lib -> name = strdup( name );
    else
        lib -> name = strdup( "generic-name" );

    lib -> nb_max_gfxs = size ;

    return lib ;
} /* create_anim_lib */




int delete_bmp_from_lib( ANIM_LIB *lib, unsigned int id )
{
    __n_assert( lib, return FALSE );
    __n_assert( lib -> gfxs, return FALSE );

    __n_assert( id, return FALSE );
    return TRUE ;
} /* delete anim from lib */




int add_bmp_to_lib( ANIM_LIB *lib, unsigned int pos, char *file, char *resfile )
{
    __n_assert( lib, return FALSE );
    __n_assert( file, return FALSE );
    __n_assert( resfile, return FALSE );

    if( pos >= lib -> nb_max_gfxs )
    {
        n_log( LOG_ERR, "invalid position %d, can only go from 0 to %d in anim lib %s", pos, lib -> nb_max_gfxs, lib -> name );
        return FALSE ;
    }
    if( lib -> gfxs[ pos ] != NULL )
    {
        n_log( LOG_ERR, "there already is a gfx at pos %d in anim lib %s", pos, lib -> name );
        return FALSE ;
    }

    FILE *data = fopen( resfile,"r");
    if( !data )
    {
        n_log( LOG_ERR, "Unable to open %s !", resfile );
    }
    int check_id ;
    fscanf( data, "%d", &check_id );
    if( check_id !=211282 )
    {
        n_log( LOG_ERR, "file %s: invalid check_id of %d, should be 211282", resfile, check_id );
        fclose( data );
        return FALSE ;
    }

    ANIM_GFX *gfx = NULL ;
    Malloc( gfx, ANIM_GFX, 1 );

    fscanf( data, "%d %d %d", &gfx -> w, &gfx -> h, &gfx -> nb_frames );

    Malloc( gfx -> frames, ANIM_FRAME, gfx -> nb_frames );

    gfx -> bmp = al_load_bitmap( file );

    for( unsigned int it = 0 ; it < gfx -> nb_frames ; it ++ )
    {
        fscanf( data, "%d %d %d", &gfx -> frames[ it ].x, &gfx -> frames[ it ].y, &gfx -> frames[ it ].duration );
    }
    fclose( data );

    lib -> gfxs[ pos ] = gfx ;

    return TRUE ;
}



int update_anim( ANIM_DATA *data, unsigned int delta_t )
{
    __n_assert( data, return FALSE );

    data -> elapsed += delta_t ;
    if( data -> elapsed > data -> lib -> gfxs[ data -> id ] -> frames[ data -> frame ].duration )
    {
        data -> elapsed = 0 ;
        data -> frame ++ ;
        if( data -> frame >= data -> lib -> gfxs[ data -> id ] -> nb_frames )
            data -> frame = 0 ;
    }
    return TRUE ;
}

int draw_anim( ANIM_DATA *data, int x,  int y )
{
    ALLEGRO_BITMAP *bmp = data -> lib -> gfxs[ data -> id ] -> bmp ;


    unsigned int tilew = data -> lib -> gfxs[ data -> id ] -> w ;
    unsigned int tileh = data -> lib -> gfxs[ data -> id ] -> h ;
    int px = data -> lib -> gfxs[ data -> id ] -> frames[ data -> frame ] . x ;
    int py = data -> lib -> gfxs[ data -> id ] -> frames[ data -> frame ] . y ;

    unsigned int framex = data -> frame * tilew ;

    al_draw_bitmap_region( bmp, framex, 0, tilew, tileh, x - px, y - py, 0 );

    return TRUE ;
}
/*int destroy_anim_lib( ANIM_LIB **lib );*/

