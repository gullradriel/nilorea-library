/**\file particle.h
*
*  particle header file for SPEEDHACK 2014
*
*\author Castagnier Mickaël aka Gull Ra Driel
*
*\version 1.0
*
*\date 20/12/2012
*
*/



#ifndef PARTICLE_HEADER_FOR_SPEEDHACK
#define PARTICLE_HEADER_FOR_SPEEDHACK

#ifdef __cplusplus
extern "C" {
#endif


#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>


#include "n_common.h"
#include "n_log.h"
#include "n_list.h"
#include "n_3d.h"
#include "n_time.h"

#define NORMAL_PART 0
#define SINUS_PART 1
#define TRANS_PART 2
#define SNOW_PART 3
#define FIRE_PART 4
#define STAR_PART 5

typedef struct PARTICLE
{
    int mode,
        lifetime,
        spr_id;
    ALLEGRO_COLOR color ;

    PHYSICS object ;

} PARTICLE ;

typedef struct PARTICLE_SYSTEM
{
    LIST *list ;

    VECTOR3D source ;

    N_TIME timer ;

    ALLEGRO_BITMAP **sprites ;
    int max_sprites ;

} PARTICLE_SYSTEM ;

int init_particle_system( PARTICLE_SYSTEM **psys, int max, double x, double y, double z, int max_sprites );

int add_particle( PARTICLE_SYSTEM *psys, int spr, int mode, int lifetime, ALLEGRO_COLOR color, PHYSICS object );
int add_particle_ex( PARTICLE_SYSTEM *psys, int spr, int mode, int off_x, int off_y, int lifetime, ALLEGRO_COLOR color,
                     double vx, double vy, double vz,
                     double ax, double ay, double az );

int manage_particle( PARTICLE_SYSTEM *psys);

int draw_particle( PARTICLE_SYSTEM *psys );

int free_particle_system( PARTICLE_SYSTEM **psys);

int move_particles( PARTICLE_SYSTEM *psys, double vx, double vy, double vz );

#ifdef __cplusplus
}
#endif
#endif /* PARTICLE_HEADER_FOR_CHRISTMASHACK */

