/**\file n_particles.c
*
*  Particle functions
*
 *\author Castagnier Mickael
*
*\version 1.0
*
*\date 15/12/2007
*
*/


#include "nilorea/n_particles.h"


/*!\fn int init_particle_system( PARTICLE_SYSTEM **psys , int max , double x , double y , double z )
*\brief Initialize a particle system
*\param psys a pointer to a NULL initialized particle system
*\param max maximum number of particles in the system
*\param x position of particles emitter
*\param y position of particles emitter
*\param z position of particles emitter
*\return TRUE or FALSE
*/
int init_particle_system( PARTICLE_SYSTEM **psys, int max, double x, double y, double z )
{
    Malloc( (*psys), PARTICLE_SYSTEM, 1 );
    if( !(*psys) )
        return FALSE ;

    (*psys) -> list_start = (*psys) -> list_end = NULL ;

    start_HiTimer( &(*psys) -> timer );

    (*psys) -> max_particles = max ;
    (*psys) -> nb_particles  = 0 ;
    (*psys) -> x = x ;
    (*psys) -> y = y ;
    (*psys) -> z = z ;

    return TRUE ;
} /* init_particle_system */



/*!\fn int add_particle( PARTICLE_SYSTEM *psys , BITMAP *spr , int mode , int off_x , int off_y , int off_z , int lifetime , int color , double vx , double vy , double vz , double ax , double ay , double az )
*\brief Add a particle to a particle system
*\param psys Aimed particle system
*\param spr Sprite of the particle, if any
*\param mode Type of particle, NORMAL_PART or SINUS_PART, coupled eventually with TRANS_PART|RECYLCING_PART
*\param off_x ofsset X from particle system X source
*\param off_y ofsset Y from particle system Y source
*\param off_z ofsset Z from particle system Z source
*\param lifetime Lifetime of the particle
*\param color color of the particle
*\param vx X speed of the particle
*\param vy Y speed of the particle
*\param vz Z speed of the particle
*\param ax X acceleration of the particle
*\param ay Y acceleration of the particle
*\param az Z acceleration of the particle
*\return TRUE or FALSE
*/
int add_particle( PARTICLE_SYSTEM *psys, BITMAP *spr, int mode, int off_x, int off_y, int off_z,
                  , int lifetime, int color,
                  double vx, double vy, double vz,
                  double ax, double ay, double az )
{
    if( psys -> nb_particles >= psys -> max_particles )
        return FALSE ;

    if( !psys -> list_start ) /* no particles */
    {
        Malloc( psys -> list_start, PARTICLE, 1 );

        psys -> list_start -> prev = psys -> list_start -> next = NULL ;
        psys -> list_end = psys -> list_start ;

        psys -> list_start -> sprite = spr ;
        psys -> list_start -> mode = mode ;
        psys -> list_start -> off_x = off_x ;
        psys -> list_start -> off_y = off_y ;
        psys -> list_start -> off_z = off_z ;
        psys -> list_start -> lifetime = lifetime ;
        psys -> list_start -> x = psys -> x + off_x ;
        psys -> list_start -> y = psys -> y + off_y ;
        psys -> list_start -> z = psys -> z + off_z ;
        psys -> list_start -> ax = ax ;
        psys -> list_start -> ay = ay ;
        psys -> list_start -> az = az ;
        psys -> list_start -> vx = vx ;
        psys -> list_start -> vy = vy ;
        psys -> list_start -> vz = vz ;
        psys -> list_start -> color = color ;

        psys -> nb_particles ++ ;

        return TRUE;
    }

    Malloc( psys -> list_end -> next, PARTICLE, 1 );
    psys -> list_end -> next -> prev = psys -> list_end ;
    psys -> list_end -> next -> next = NULL ;
    psys -> list_end = psys -> list_end -> next ;
    psys -> list_end -> sprite = spr ;
    psys -> list_end -> mode = mode ;
    psys -> list_end -> off_x = off_x ;
    psys -> list_end -> off_y = off_y ;
    psys -> list_end -> lifetime = lifetime ;
    psys -> list_end -> x = psys -> x + off_x ;
    psys -> list_end -> y = psys -> y + off_y ;
    psys -> list_end -> z = psys -> z + off_z ;
    psys -> list_end -> ax = ax ;
    psys -> list_end -> ay = ay ;
    psys -> list_end -> az = az ;
    psys -> list_end -> vx = vx ;
    psys -> list_end -> vy = vy ;
    psys -> list_end -> vz = vz ;
    psys -> list_end -> color = color ;

    psys -> nb_particles ++ ;

    return TRUE ;

} /* add_particle */



/*!\fn int manage_particle( PARTICLE_SYSTEM *psys)
*\brief Compute particles movements and cycle
*\param psys The particle system
*\return TRUE of FALSE
*/
int manage_particle( PARTICLE_SYSTEM *psys)
{
    PARTICLE *ptr = NULL, *next = NULL, *prev = NULL ;

    double tmp_v = 0 ;

    ptr = psys -> list_start ;

    double delta = 0 ;

    delta = get_msec( &psys -> timer ) ;

    while( ptr )
    {
        ptr -> lifetime -= delta ;

        if( ptr -> lifetime > 0 )
        {
            tmp_v = ptr -> vx + ( ptr -> ax * ((delta * delta)/1000) ) / 2 ;
            ptr -> x = ptr -> x + delta * ( ptr -> vx + tmp_v ) / 2000 ;
            ptr -> vx = tmp_v ;

            tmp_v = ptr -> vy + ( ptr -> ay * ((delta * delta)/1000) ) / 2 ;
            ptr -> y = ptr -> y + delta * ( ptr -> vy + tmp_v ) / 2000 ;
            ptr -> vy = tmp_v ;

            tmp_v = ptr -> vz + ( ptr -> az * ((delta * delta)/1000) ) / 2 ;
            ptr -> z = ptr -> z + delta * ( ptr -> vz + tmp_v ) / 2000 ;
            ptr -> vz = tmp_v ;

            ptr = ptr -> next ;
        }
        else
        {
            next = ptr -> next ;
            prev = ptr -> prev ;

            if( next )
            {
                if( prev )
                    next -> prev = prev ;
                else
                {
                    next -> prev = NULL ;
                    psys -> list_start = next ;
                }
            }

            if( prev )
            {
                if( next )
                    prev -> next = next ;
                else
                {
                    prev -> next = NULL ;
                    psys -> list_end = prev ;
                }
            }

            Free( ptr );
            psys -> nb_particles -- ;
            if( psys -> nb_particles <= 0 )
                psys -> list_start = psys -> list_end = NULL ;

            ptr = next ;
        }
    }

    return TRUE;
} /* manage_particle */



/*!\fn int draw_particle( BITMAP *bmp , PARTICLE_SYSTEM *psys )
 *\brief Draw a particle system onto a bitmap
 *\param bmp the aimed particle system
 *\param psys the partciel system to draw
 *\return TRUE or FALSE
 */
int draw_particle( BITMAP *bmp, PARTICLE_SYSTEM *psys )
{
    PARTICLE *ptr = NULL ;

    double x = 0, y = 0 ;

    ptr = psys -> list_start ;

    while( ptr )
    {
        x = ptr -> x ;
        y = ptr -> y ;

        if( ptr -> mode & SINUS_PART )
        {
            if( ptr -> vx != 0 )
                x = x + ptr -> vx * sin( (ptr -> x/ptr -> vx) ) ;
            else
                x = x + ptr -> vx * sin( ptr -> x );

            if( ptr -> vy != 0 )
                y = y + ptr -> vy * cos( (ptr -> y/ptr -> vy) ) ;
            else
                y = y + ptr -> vy * sin( ptr -> y ) ;

            if( ptr -> vz != 0 )
                z = z + ptr -> vz * cos( (ptr -> z/ptr -> vz) ) ;
            else
                z = z + ptr -> vz * sin( ptr -> z ) ;
        }

        if( ptr -> mode & TRANS_PART )
        {
            drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
            set_alpha_blender( );
            draw_trans_sprite( bmp, ptr -> sprite, x - ptr -> sprite -> w / 2, y - ptr -> sprite -> h /2 );
            drawing_mode(DRAW_MODE_SOLID, NULL, 0, 0);

        }
        else
        {
            if( ptr -> sprite )
            {
#ifdef DEBUGMODE
                circle( bmp, x - ptr -> sprite -> w / 2, y - ptr -> sprite -> h / 2, 3,  makecol( 255, 255, 0 ) );
#endif
                masked_blit( ptr -> sprite, bmp, 0, 0, x - ptr -> sprite -> w / 2, y - ptr -> sprite -> h /2, ptr -> sprite -> w, ptr -> sprite -> h );
            }
            else
                circle( bmp, x, y, 2, ptr -> color );
        }

        ptr = ptr -> next ;
    }

    return TRUE;
} /* drax_particle */



/*!\fn int free_particle( PARTICLE_SYSTEM *psys , PARTICLE **ptr )
*\brief Free a particle from a particle system
*\param psys The aimed particle system
*\param ptr A pointer to a particle to delete
*\return TRUE or FALSE
*/
int free_particle( PARTICLE_SYSTEM *psys, PARTICLE **ptr )
{
    PARTICLE *next = NULL, *prev = NULL ;

    next = (*ptr) -> next ;
    prev = (*ptr) -> prev ;

    if( next )
    {
        if( prev )
            next -> prev = prev ;
        else
        {
            next -> prev = NULL ;
            psys -> list_start = next ;
        }
    }

    if( prev )
    {
        if( next )
            prev -> next = next ;
        else
        {
            prev -> next = NULL ;
            psys -> list_end = prev ;
        }
    }

    Free( (*ptr) );

    psys -> nb_particles -- ;

    if( psys -> nb_particles <= 0 )
        psys -> list_start = psys -> list_end = NULL ;

    (*ptr) = next ;

    return TRUE ;
} /* free_particle(...) */



/*!\fn int free_particle_system( PARTICLE_SYSTEM **psys)
*\brief free a particle system
*\param psys a pointer to a particle system to delete
*\return TRUE or FALSE
*/
int free_particle_system( PARTICLE_SYSTEM **psys)
{
    PARTICLE *ptr = NULL, *ptr_next = NULL ;

    ptr = (*psys) -> list_start ;

    while( ptr )
    {
        ptr_next = ptr -> next ;
        Free( ptr ) ;
        ptr = ptr_next ;
    }

    Free( (*psys) );

    return TRUE;
}

