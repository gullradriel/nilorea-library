/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@file n_particles.h
 *@brief Particles management
 *@author Castagnier Mickael aka Gull Ra Driel
 *@version 1.0
 *@date 20/12/2012
 */

#ifndef __N_PARTICLE_HEADER
#define __N_PARTICLE_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup PARTICLES GRAPHICS: 3D particles utilities
   @addtogroup PARTICLES
  @{
*/

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

/*! classic moving particle */
#define NORMAL_PART 0
/*! sinus based moving particle */
#define SINUS_PART 1
/*! transparent particle */
#define TRANS_PART 2
/*! snow particle */
#define SNOW_PART 3
/*! fire particle */
#define FIRE_PART 4
/*! star particle */
#define STAR_PART 5
/*! circle particle */
#define CIRCLE_PART 6
/*! pixel particle */
#define PIXEL_PART 7
/*! bitmap particle */
#define BITMAP_PART 8
/*! bitmap particle */
#define TEXT_PART 9

/*! Structure of a single particle */
typedef struct PARTICLE {
    /*! particle mode: NORMAL_PART,SINUS_PART,PIXEL_PART */
    int mode,
        /*! lifetime (counts down, msecs) */
        lifetime,
        /*! sprite id in library */
        spr_id,
        /*! current size of particle */
        size;

    /*! color of the particle */
    ALLEGRO_COLOR color;

    /*! particle physical properties */
    PHYSICS object;

    /*! start color for lerp (set by emitter, ignored if color_start == color_end) */
    ALLEGRO_COLOR color_start;
    /*! end color for lerp */
    ALLEGRO_COLOR color_end;
    /*! current age in msecs (incremented by manage_particle_ex) */
    int age;
    /*! original lifetime at birth (needed for lerp ratio = age/lifetime_max) */
    int lifetime_max;
    /*! particle size at birth */
    int size_start;
    /*! particle size at death */
    int size_end;
    /*! 1=additive blend, 0=normal alpha */
    int additive;
    /*! 1=depth-sorted with terrain, 0=flat overlay after iso pass */
    int depth_sort;
    /*! back-pointer to owning emitter (NULL if spawned manually) */
    struct PARTICLE_EMITTER* emitter;

} PARTICLE;

/*! Structure of a particle emitter */
typedef struct PARTICLE_EMITTER {
    /*! emitter world position */
    VECTOR3D position;
    /*! offset when attached to entity */
    VECTOR3D offset;
    /*! random velocity range min */
    VECTOR3D velocity_min;
    /*! random velocity range max */
    VECTOR3D velocity_max;
    /*! constant acceleration (gravity etc.) */
    VECTOR3D acceleration;
    /*! particle start color */
    ALLEGRO_COLOR color_start;
    /*! particle end color (lerp over lifetime) */
    ALLEGRO_COLOR color_end;
    /*! particles per second (continuous mode) */
    double emit_rate;
    /*! internal: fractional particle accumulator */
    double emit_accumulator;
    /*! particle lifetime range min (msecs) */
    int lifetime_min;
    /*! particle lifetime range max (msecs) */
    int lifetime_max;
    /*! particle size at birth */
    int size_start;
    /*! particle size at death */
    int size_end;
    /*! particle draw mode (NORMAL_PART, CIRCLE_PART, PIXEL_PART, FIRE_PART, etc.) */
    int particle_mode;
    /*! if >0, emit this many instantly then deactivate */
    int burst_count;
    /*! 1=emitting, 0=paused */
    int active;
    /*! -1=not attached, >=0=entity id */
    int attached_entity_id;
    /*! sprite id or -1 */
    int spr_id;
    /*! emission shape: 0=point, 1=line, 2=rect, 3=circle */
    int shape;
    /*! shape dimension 1: line length, rect width, or circle radius (world units) */
    double shape_w;
    /*! shape dimension 2: rect height (world units) */
    double shape_h;
    /*! shape rotation in radians */
    double shape_rotation;
    /*! per-emitter particle cap (0=unlimited) */
    int max_particles;
    /*! 1=additive blend, 0=normal alpha blend */
    int additive_blend;
    /*! 1=depth-sorted with terrain, 0=flat overlay */
    int depth_sort;
    /*! current live particle count for this emitter (managed by system) */
    int live_count;
} PARTICLE_EMITTER;

/*! Structure of a particle system */
typedef struct PARTICLE_SYSTEM {
    /*! list of PARTICLE pointers */
    LIST* list;

    /*! Coordinate of emitting point (used by add_particle/add_particle_ex) */
    VECTOR3D source;

    /*! Internal: particle system timer */
    N_TIME timer;

    /*! Library of picture for the particles */
    ALLEGRO_BITMAP** sprites;
    /*! size of the picture library */
    int max_sprites;

    /*! list of PARTICLE_EMITTER pointers */
    LIST* emitters;
} PARTICLE_SYSTEM;

/*! initialize a particle system */
int init_particle_system(PARTICLE_SYSTEM** psys, int max, double x, double y, double z, int max_sprites);

/*! add a particle to the system */
int add_particle(PARTICLE_SYSTEM* psys, int spr, int mode, int lifetime, int size, ALLEGRO_COLOR color, PHYSICS object);
/*! add a particle to the system (extended version) */
int add_particle_ex(PARTICLE_SYSTEM* psys, int spr, int mode, int off_x, int off_y, int lifetime, int size, ALLEGRO_COLOR color, double vx, double vy, double vz, double ax, double ay, double az);

/*! manage particles with custom delta time */
int manage_particle_ex(PARTICLE_SYSTEM* psys, double delta_t);

/*! manage particles using internal timer */
int manage_particle(PARTICLE_SYSTEM* psys);

/*! draw particles on screen */
int draw_particle(PARTICLE_SYSTEM* psys, double xpos, double ypos, int w, int h, double range);

/*! free a particle system */
int free_particle_system(PARTICLE_SYSTEM** psys);

/*! move all particles by a velocity offset */
int move_particles(PARTICLE_SYSTEM* psys, double vx, double vy, double vz);

/*! add a new emitter to the particle system. Returns pointer for caller to configure, or NULL on error. */
PARTICLE_EMITTER* add_emitter(PARTICLE_SYSTEM* psys);
/*! remove an emitter from the particle system and free it */
int remove_emitter(PARTICLE_SYSTEM* psys, PARTICLE_EMITTER* em);
/*! free all emitters in the particle system */
int free_emitters(PARTICLE_SYSTEM* psys);

/*! add a particle at an absolute world position (not relative to psys->source) */
int add_particle_at(PARTICLE_SYSTEM* psys, int spr, int mode, double px, double py, double pz, int lifetime, int size_start, int size_end, ALLEGRO_COLOR color_start, ALLEGRO_COLOR color_end, double vx, double vy, double vz, double ax, double ay, double az);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* __N_PARTICLE_HEADER */
