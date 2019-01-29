/**\file n_particles.h
 *
 *  Particle management
 *
 *\author Castagnier Mickael
 *
 *\version 1.0
 *
 *\date 15/12/2007
 */

#ifndef NILOREA_PARTICLE_HEADER_
#define NILOREA_PARTICLE_HEADER_

#ifdef __cplusplus
extern "C" {
#endif

#include "n_common.h"
#include "n_log.h"
#include "n_common.h"


#define NORMAL_PART 0
#define SINUS_PART 1
#define TRANS_PART 2
#define CIRCLE_PART 4
#define PIXEL_PART 8


typedef struct PARTICLE
{
    double x, y, z,
           ax, ay, az,
           vx, vy, vz ;

    BITMAP *sprite ;

    int mode,
        off_x, off_y,
        lifetime,
        color;

    struct PARTICLE *next,
               *prev ;

} PARTICLE ;

typedef struct PARTICLE_SYSTEM
{
    PARTICLE *list_start, *list_end ;

    double x, y, z ;

    N_TIME timer ;

    int max_particles,
        nb_particles ;

} PARTICLE_SYSTEM ;

/*! ForEach macro helper */
#define particle_foreach( __ITEM_ , __PARTICLE_SYSTEM_  ) \
	if( !__PARTICLE_SYSTEM_ ) \
	{ \
		n_log( LOG_ERR , "Error in particle_foreach, %s is NULL" , #__PARTICLE_SYSTEM_ ); \
	} \
	else \
	for( PARTICLE *__ITEM_ = __PARTICLE_SYSTEM_ -> list_start ; __ITEM_ != NULL; __ITEM_ = __ITEM_ -> next ) \


int init_particle_system( PARTICLE_SYSTEM **psys, int max, double x, double y, double z );

int add_particle( PARTICLE_SYSTEM *psys, BITMAP *spr, int mode, int off_x, int off_y, int lifetime, int color,
                  double vx, double vy, double vz,
                  double ax, double ay, double az );

int manage_particle( PARTICLE_SYSTEM *psys);

int draw_particle( BITMAP *bmp, PARTICLE_SYSTEM *psys );

int free_particle( PARTICLE_SYSTEM *psys, PARTICLE **ptr );

int free_particle_system( PARTICLE_SYSTEM **psys);

#ifdef __cplusplus
}
#endif
#endif /* NILOREA_PARTICLE_HEADER_ */

