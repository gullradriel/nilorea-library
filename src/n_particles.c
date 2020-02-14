/**\file n_particles.c
*
*  particle function file for SantaHack 2012
*
*\author Castagnier Mickaël aka Gull Ra Driel
*
*\version 1.0
*
*\date 20/12/2012
*
*/



#include "nilorea/n_particles.h"
#include "math.h"


int init_particle_system( PARTICLE_SYSTEM **psys, int max, double x, double y, double z, int max_sprites )
{
    Malloc( (*psys), PARTICLE_SYSTEM, 1 );

    start_HiTimer( &(*psys) -> timer );

    (*psys) -> list = new_generic_list( max );

    (*psys) -> source[ 0 ] = x ;
    (*psys) -> source[ 1 ] = y ;
    (*psys) -> source[ 2 ] = z ;
    (*psys) -> source[ 3 ] = 0 ;

    (*psys) -> sprites = (ALLEGRO_BITMAP **)calloc( max_sprites, sizeof( ALLEGRO_BITMAP *) );

    return TRUE ;
}



int add_particle( PARTICLE_SYSTEM *psys, int spr, int mode, int lifetime, int size, ALLEGRO_COLOR color, PHYSICS object )
{
    int it = 0 ;

    PARTICLE *new_p = NULL ;


    if( psys -> list -> nb_items == psys -> list -> nb_max_items )
        return FALSE ;

    for( it = 0 ; it < 3 ; it ++ )
    {
        object . position[ it ] += psys -> source[ it ] ;
    }

    Malloc( new_p, PARTICLE, 1 );
    __n_assert( new_p, return FALSE );

    new_p -> spr_id   = spr ;
    new_p -> mode     = mode ;
    new_p -> lifetime = lifetime ;
    new_p -> color    = color ;
    new_p -> size    = size ;

    memcpy( new_p -> object . position, object . position, sizeof( VECTOR3D ) );
    memcpy( new_p -> object . speed, object . speed, sizeof( VECTOR3D ) );
    memcpy( new_p -> object . acceleration, object . acceleration, sizeof( VECTOR3D ) );
    memcpy( new_p -> object . angular_speed, object . angular_speed, sizeof( VECTOR3D ) );
    memcpy( new_p -> object . angular_acceleration, object . angular_acceleration, sizeof( VECTOR3D ) );
    memcpy( new_p -> object . orientation, object . orientation, sizeof( VECTOR3D ) );

    return list_push( psys -> list, new_p, &free );
}



int add_particle_ex( PARTICLE_SYSTEM *psys, int spr, int mode, int off_x, int off_y, int lifetime, int size, ALLEGRO_COLOR color,
                     double vx, double vy, double vz,
                     double ax, double ay, double az )
{
    PHYSICS object ;
    VECTOR3D_SET( object . position, off_x, off_y, 0.0 );
    VECTOR3D_SET( object . speed,  vx, vy, vz );
    VECTOR3D_SET( object . acceleration, ax, ay, az );
    VECTOR3D_SET( object . orientation, 0.0, 0.0, 0.0 );
    VECTOR3D_SET( object . angular_speed, 0.0, 0.0, 0.0 );
    VECTOR3D_SET( object . angular_acceleration, 0.0, 0.0, 0.0 );

    return add_particle( psys, spr, mode, lifetime, size,color, object );
}



int manage_particle( PARTICLE_SYSTEM *psys)
{
    LIST_NODE *node = NULL ;
    PARTICLE *ptr = NULL ;

    node = psys -> list -> start ;

    double delta = 0 ;

    delta = get_usec( &psys -> timer ) ;

    while( node )
    {
        ptr = (PARTICLE *)node -> ptr ;
        if( ptr -> lifetime != -1 )
        {
            ptr -> lifetime -= delta/1000.0 ;
            if( ptr -> lifetime == -1 )
                ptr -> lifetime = 0 ;
        }

        if( ptr -> lifetime > 0 || ptr -> lifetime == -1 )
        {
            update_physics_position( &ptr -> object, delta );
            node = node -> next ;
        }
        else
        {
            LIST_NODE *node_to_kill = node ;
            node = node -> next ;
            ptr = remove_list_node( psys -> list, node_to_kill, PARTICLE );
            Free( ptr );
        }
    }

    return TRUE;
}



int draw_particle( PARTICLE_SYSTEM *psys, double xpos, double ypos, int w, int h, double range )
{
    LIST_NODE *node = NULL ;
    PARTICLE *ptr = NULL ;


    node  = psys -> list -> start ;

    while( node )
    {
          double x = 0, y = 0 ;

        ptr = (PARTICLE *)node -> ptr ;
        x = ptr -> object . position[ 0 ] - xpos ;
        y = ptr -> object . position[ 1 ] - ypos ;

        if( ( x < -range ) || ( x > ( w + range ) ) || ( y< -range ) || ( y > ( h + range ) ) )
        {
            node = node -> next ;
            continue ;
        }

        while( ptr -> object . orientation[ 2 ] > 255 )
            ptr -> object . orientation[ 2 ] -= 256 ;

        while( ptr -> object . orientation[ 2 ] < 0 )
            ptr -> object . orientation[ 2 ] += 256 ;

        if( ptr -> mode == SINUS_PART )
        {
            if( ptr -> object . speed[ 0 ] != 0 )
                x = x + ptr -> object . speed[ 0 ] * sin( (ptr -> object . position[ 0 ]/ptr -> object . speed[ 0 ]) ) ;
            else
                x = x + ptr -> object . speed[ 0 ] * sin( ptr -> object . position[ 0 ] );

            if( ptr -> object . speed[ 1 ] != 0 )
                y = y + ptr -> object . speed[ 1 ] * cos( (ptr -> object . speed[ 1 ]/ptr -> object . speed[ 1 ]) ) ;
            else
                y = y + ptr -> object . speed[ 1 ] * sin( ptr -> object . position[ 1 ] ) ;

            if( ptr -> spr_id >= 0 && ptr -> spr_id < psys -> max_sprites && psys -> sprites[ ptr -> spr_id ] )
            {
                int spr_w = al_get_bitmap_width( psys -> sprites[ ptr -> spr_id ] );
                int spr_h = al_get_bitmap_height( psys -> sprites[ ptr -> spr_id ] );

                al_draw_rotated_bitmap( psys -> sprites[ ptr -> spr_id ], spr_w/2, spr_h/2, x - spr_w / 2, y - spr_h /2, al_ftofix( ptr -> object . orientation[ 2 ]), 0 );
            }
            else
                al_draw_circle( x, y, 2, ptr -> color, 1 );
        }

        if( ptr -> mode & NORMAL_PART )
        {
            if( ptr -> spr_id >= 0 && ptr -> spr_id < psys -> max_sprites && psys -> sprites[ ptr -> spr_id ] )
            {
                int w = al_get_bitmap_width( psys -> sprites[ ptr -> spr_id ] );
                int h = al_get_bitmap_height( psys -> sprites[ ptr -> spr_id ] );

                al_draw_rotated_bitmap( psys -> sprites[ ptr -> spr_id ], w/2, h/2, x - w / 2, y - h /2, al_ftofix( ptr -> object . orientation[ 2 ]), 0 );
            }
        }
        else if( ptr -> mode & PIXEL_PART )
        {
            al_draw_filled_rectangle( x - ptr -> size, y - ptr -> size, x + ptr -> size, y + ptr -> size, ptr -> color );
        }
        else
            al_draw_circle( x, y, ptr -> size, ptr -> color, 1 );
        node  = node -> next ;
    }

    return TRUE;
}



int free_particle_system( PARTICLE_SYSTEM **psys)
{
    PARTICLE *particle = NULL ;
    while( (*psys) -> list -> start )
    {
        particle = remove_list_node( (*psys) -> list, (*psys) -> list -> start, PARTICLE );
        Free( particle );
    }
    Free( (*psys) );
    return TRUE;
}



int move_particles( PARTICLE_SYSTEM *psys, double vx, double vy, double vz )
{
    LIST_NODE *node = NULL ;
    PARTICLE *ptr = NULL ;

    node  = psys -> list -> start ;

    while( node )
    {
        ptr = (PARTICLE *)node -> ptr ;
        ptr -> object . position[ 0 ] = ptr -> object . position[ 0 ] + vx ;
        ptr -> object . position[ 1 ] = ptr -> object . position[ 1 ] + vy ;
        ptr -> object . position[ 2 ] = ptr -> object . position[ 2 ] + vz ;
        node = node -> next ;
    }
    return TRUE ;
}
