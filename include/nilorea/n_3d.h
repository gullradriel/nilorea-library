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
 *@file n_3d.h
 *@brief Simple 3D movement simulation
 *@author Castagnier Mickael
 *@version 1.0
 *@date 30/04/2014
 */

#ifndef _N_3D_HEADER
#define _N_3D_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup PHYSICS PHYSICS: simple 3D movement simulation
  @addtogroup PHYSICS
  @{
  */

#include "n_log.h"
#include "n_common.h"
#include <sys/time.h>

/*! PHYSICS object state for STOPPED */
#define MOVE_STOPPED 0
/*! PHYSICS object state for move interpolated between two position */
#define MOVE_INTER 1
/*! PHYSICS object state for move simulated from latest update */
#define MOVE_SIMU 2

/*! value when the two VECTOR3D are not connected */
#define VECTOR3D_DONT_INTERSECT -2
/*! value when the two VECTOR3D are collinear */
#define VECTOR3D_COLLINEAR -1
/*! value when the two VECTOR3D are intersecting */
#define VECTOR3D_DO_INTERSECT 0

/*! struct of a point */
typedef double VECTOR3D[3]; /* x , y , z  */

/*! helper to set a VECTOR3D position */
#define VECTOR3D_SET(VECTOR, X, Y, Z) \
    do {                              \
        VECTOR[0] = (X);              \
        VECTOR[1] = (Y);              \
        VECTOR[2] = (Z);              \
    } while (0)

/*! structure of the physics of an object */
typedef struct PHYSICS {
    /*! last delta_t used */
    time_t delta_t;
    /*! x,y,z actual position */
    VECTOR3D position;
    /*! vx,vy,vz actual speed */
    VECTOR3D speed;
    /*! ax,ay,az actual acceleration */
    VECTOR3D acceleration;
    /*! ax,ay,az actual rotation position */
    VECTOR3D orientation;
    /*! rvx,rvy,rvz actual angular speed */
    VECTOR3D angular_speed;
    /*! rax,ray,raz actual angular acceleration */
    VECTOR3D angular_acceleration;
    /*! gx , gy , gz gravity */
    VECTOR3D gravity;
    /*! ability */
    int can_jump;
    /*! size */
    int sz;
    /*! optionnal type id */
    int type;
} PHYSICS;

/*! distance between two points */
double distance(VECTOR3D* p1, VECTOR3D* p2);
/*! update component[ it ] of a VECTOR3D */
int update_physics_position_nb(PHYSICS* object, int it, double delta_t);
/*! update component[ it ] of a VECTOR3D, reversed time */
int update_physics_position_reverse_nb(PHYSICS* object, int it, double delta_t);
/*! update the position of an object, using the delta time T to update positions */
int update_physics_position(PHYSICS* object, double delta_t);
/*! update the position of an object, using the delta time T to reverse update positions */
int update_physics_position_reverse(PHYSICS* object, double delta_t);
/*! compute if two vector are colliding, storing the resulting point in px */
int vector_intersect(VECTOR3D* p0, VECTOR3D* p1, VECTOR3D* p2, VECTOR3D* p3, VECTOR3D* px);
/*! dot product */
double vector_dot_product(VECTOR3D* vec1, VECTOR3D* vec2);
/*! vector normalize */
double vector_normalize(VECTOR3D* vec);
/*! vector angle with other vector */
double vector_angle_between(VECTOR3D* vec1, VECTOR3D* vec2);

/*! VECTOR3D copy wrapper */
#define copy_point(__src_, __dst_) \
    memcpy(__dst_, __src_, sizeof(VECTOR3D));

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif  // header guard
