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


/*! Particles who follow a normal trajectory */
#define NORMAL_PART 0
/*! Particle who follow a sinusoidal trajectory */
#define SINUS_PART 1
/*! Transparent Particles */
#define TRANS_PART 2


	/*! struct of a particle */
	typedef struct PARTICLE
	{
		double
			/*! X position */
			x , 
			/*! Y position */
			y ,
			/*! Z position */
			z ,
			/*! X acceleration */
			ax , 
			/*! Y acceleration */	
			ay , 
			/*! Z acceleration */
			az ,
			/*! X speed */
			vx , 
			/*! Y speed */
			vy , 
			/*! Z speed */
			vz ;
		/*! Particle picture if any */
		BITMAP *sprite ;
		
		int /*! type of particle: NORMAL PART , SINUS_PART , TRANS_PART */
			mode ,
			/*! X offset */
			off_x ,
			/*! Y offset */
			off_y ,
			/*! lifetime in msec */
			lifetime ,
			/*! color of the particle */
			color;
		struct PARTICLE /*! pointer to next */
						*next ,
						/*! pointer to prev */
						*prev ;
	} PARTICLE ;

	/*! Structure of a particle system */
	typedef struct PARTICLE_SYSTEM
	{
		PARTICLE 	/*! start of the list of particle */
					*list_start ,
					/*! end of the list of particle */
					*list_end ;

		double	/*! position of the origins of particles */
				x ,
				/*! position of the origins of particles */
				y ,
				/*! position of the origins of particles */
				z ;
		/*! internal particle system timer */
		N_TIME timer ;
		int /*! nb max of particle in the particle system */
			max_particles,
			/*! current nb of particles in the system */
			nb_particles ;
	}PARTICLE_SYSTEM ;

	/*! ForEach particles macro helper */
#define particle_foreach( __ITEM_ , __PARTICLE_SYSTEM_  ) \
	if( !__PARTICLE_SYSTEM_ ) \
	{ \
		n_log( LOG_ERR , "Error in particle_foreach, %s is NULL" , #__PARTICLE_SYSTEM_ ); \
	} \
	else \
	for( PARTICLE *__ITEM_ = __PARTICLE_SYSTEM_ -> list_start ; __ITEM_ != NULL; __ITEM_ = __ITEM_ -> next ) \

	
	/* init a particle system */
	int init_particle_system( PARTICLE_SYSTEM **psys , int max , double x , double y , double z );
	/* add a particle to a particle system */
	int add_particle( PARTICLE_SYSTEM *psys , BITMAP *spr , int mode , int off_x , int off_y , int off_y , int lifetime , int color ,
			double vx , double vy , double vz ,
			double ax , double ay , double az );
	/* animate particle system */
	int manage_particle( PARTICLE_SYSTEM *psys);
	/* draw the particle system */
	int draw_particle( BITMAP *bmp , PARTICLE_SYSTEM *psys );
	/* destroy a particle */
	int free_particle( PARTICLE_SYSTEM *psys , PARTICLE **ptr );
	/* destroy a particle system */
	int free_particle_system( PARTICLE_SYSTEM **psys);

#ifdef __cplusplus
}
#endif
#endif /* NILOREA_PARTICLE_HEADER_ */

