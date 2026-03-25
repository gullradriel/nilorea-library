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
 *@file n_dead_reckoning.h
 *@brief Dead Reckoning API for latency hiding in networked games
 *
 * Implements dead reckoning (DR) as described in:
 * - "Dead Reckoning: Latency Hiding for Networked Games" (Game Developer)
 * - "Believable Dead Reckoning for Networked Games" by Curtiss Murphy
 *   (Game Engine Gems 2, 2011)
 *
 * Dead reckoning is a technique where every participant in a networked game
 * simulates all entities using agreed-upon extrapolation algorithms.  When
 * the owner's true state diverges from the extrapolated state beyond a
 * configurable threshold, a new state update is sent.  On receipt, the
 * remote side uses a convergence algorithm to smoothly blend from the
 * predicted position to the newly received authoritative state.
 *
 * Can be used together with n_trajectory for cubic Hermite spline
 * interpolation between known states, while dead reckoning provides the
 * network-level threshold checking and convergence blending.
 *
 * Supports three extrapolation models:
 *   - DR_ALGO_STATIC:   entity stays at last known position
 *   - DR_ALGO_VEL:      P(t) = P0 + V0 * t
 *   - DR_ALGO_VEL_ACC:  P(t) = P0 + V0 * t + 0.5 * A0 * t^2
 *
 * Supports three convergence/blending modes:
 *   - DR_BLEND_SNAP:    immediately snap to new state (no smoothing)
 *   - DR_BLEND_PVB:     Projective Velocity Blending (recommended)
 *   - DR_BLEND_CUBIC:   Cubic Bezier spline convergence
 *
 * Usage:
 * @code
 *   // Create entity with default settings (VEL_ACC extrapolation, PVB blending)
 *   DR_ENTITY *ent = dr_entity_create(DR_ALGO_VEL_ACC, DR_BLEND_PVB, 0.2, 5.0);
 *
 *   // When an authoritative state update arrives from the network:
 *   DR_VEC3 pos = {10.0, 5.0, 0.0};
 *   DR_VEC3 vel = {2.0, 1.0, 0.0};
 *   DR_VEC3 acc = {0.0, 0.0, 0.0};
 *   dr_entity_receive_state(ent, &pos, &vel, &acc, current_time);
 *
 *   // Each frame, compute the blended display position:
 *   DR_VEC3 display_pos;
 *   dr_entity_compute(ent, current_time, &display_pos);
 *
 *   // For the owning player: check if a network update should be sent:
 *   DR_VEC3 true_pos = { ... };
 *   DR_VEC3 true_vel = { ... };
 *   DR_VEC3 true_acc = { ... };
 *   if (dr_entity_check_threshold(ent, &true_pos, &true_vel, &true_acc, current_time)) {
 *       // send state update to other players
 *       dr_entity_receive_state(ent, &true_pos, &true_vel, &true_acc, current_time);
 *   }
 *
 *   dr_entity_destroy(&ent);
 * @endcode
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 01/03/2026
 */

#ifndef N_DEAD_RECKONING_H
#define N_DEAD_RECKONING_H

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup DEAD_RECKONING DEAD_RECKONING: latency hiding with extrapolation and convergence blending
  @addtogroup DEAD_RECKONING
  @{
  */

#include <stdbool.h>
#include <math.h>

/*---------------------------------------------------------------------------
 * 3D Vector type for dead reckoning
 *-------------------------------------------------------------------------*/

/*! 3D vector used for position, velocity, and acceleration */
typedef struct DR_VEC3 {
    double x; /*!< X component */
    double y; /*!< Y component */
    double z; /*!< Z component */
} DR_VEC3;

/*---------------------------------------------------------------------------
 * Enumerations
 *-------------------------------------------------------------------------*/

/*! Dead reckoning extrapolation algorithm.
 * Determines how the entity position is predicted forward in time
 * from the last known kinematic state. */
typedef enum DR_ALGO {
    DR_ALGO_STATIC = 0, /*!< No extrapolation; entity stays at last position */
    DR_ALGO_VEL = 1,    /*!< Velocity only:  P(t) = P0 + V0*t */
    DR_ALGO_VEL_ACC = 2 /*!< Velocity + acceleration: P(t) = P0 + V0*t + 0.5*A0*t^2 */
} DR_ALGO;

/*! Dead reckoning convergence/blending mode.
 * Determines how the entity transitions from its predicted position
 * to the newly received authoritative position. */
typedef enum DR_BLEND {
    DR_BLEND_SNAP = 0, /*!< Snap instantly to new state (no smoothing) */
    DR_BLEND_PVB = 1,  /*!< Projective Velocity Blending (recommended) */
    DR_BLEND_CUBIC = 2 /*!< Cubic Bezier spline convergence */
} DR_BLEND;

/*---------------------------------------------------------------------------
 * Kinematic state snapshot
 *-------------------------------------------------------------------------*/

/*! A snapshot of entity kinematic state at a point in time */
typedef struct DR_STATE {
    DR_VEC3 pos; /*!< Position */
    DR_VEC3 vel; /*!< Velocity */
    DR_VEC3 acc; /*!< Acceleration */
    double time; /*!< Timestamp (seconds) when this state was captured */
} DR_STATE;

/*---------------------------------------------------------------------------
 * Dead reckoning entity
 *-------------------------------------------------------------------------*/

/*! Dead reckoned entity with extrapolation and convergence state.
 * Tracks the last known authoritative state, the previous predicted state
 * at the moment a new update arrived, and all parameters needed for
 * extrapolation and convergence blending. */
typedef struct DR_ENTITY {
    /* --- Configuration --- */
    DR_ALGO algo;         /*!< Extrapolation algorithm */
    DR_BLEND blend_mode;  /*!< Convergence blending mode */
    double pos_threshold; /*!< Position error threshold for sending updates (distance) */
    double blend_time;    /*!< Duration of convergence blend in seconds */

    /* --- Last known authoritative state (received from network) --- */
    DR_STATE last_known; /*!< Most recent authoritative state (P0', V0', A0') */

    /* --- State at the moment we received the last update --- */
    DR_STATE prev_predicted; /*!< Where we THOUGHT entity was when update arrived (P0, V0) */

    /* --- Convergence tracking --- */
    double blend_start; /*!< Timestamp when blending started */
    bool blending;      /*!< True if currently blending toward last_known */

    /* --- Cubic Bezier control points (computed on update) --- */
    DR_VEC3 bezier_p0; /*!< Start point (previous predicted position) */
    DR_VEC3 bezier_p1; /*!< Control point 1 */
    DR_VEC3 bezier_p2; /*!< Control point 2 */
    DR_VEC3 bezier_p3; /*!< End point (extrapolated last_known at blend_time) */

    /* --- Output --- */
    DR_VEC3 display_pos; /*!< Current blended/rendered position */
    DR_VEC3 display_vel; /*!< Current blended/rendered velocity */

    /* --- Statistics --- */
    int update_count; /*!< Number of state updates received */
} DR_ENTITY;

/*---------------------------------------------------------------------------
 * Vector operations (inline helpers)
 *-------------------------------------------------------------------------*/

/*! Create a DR_VEC3 from components */
static inline DR_VEC3 dr_vec3(double x, double y, double z) {
    DR_VEC3 v = {x, y, z};
    return v;
}

/*! Zero vector */
static inline DR_VEC3 dr_vec3_zero(void) {
    DR_VEC3 v = {0.0, 0.0, 0.0};
    return v;
}

/*! Vector addition: a + b */
static inline DR_VEC3 dr_vec3_add(DR_VEC3 a, DR_VEC3 b) {
    DR_VEC3 v = {a.x + b.x, a.y + b.y, a.z + b.z};
    return v;
}

/*! Vector subtraction: a - b */
static inline DR_VEC3 dr_vec3_sub(DR_VEC3 a, DR_VEC3 b) {
    DR_VEC3 v = {a.x - b.x, a.y - b.y, a.z - b.z};
    return v;
}

/*! Scalar multiplication: v * s */
static inline DR_VEC3 dr_vec3_scale(DR_VEC3 v, double s) {
    DR_VEC3 r = {v.x * s, v.y * s, v.z * s};
    return r;
}

/*! Linear interpolation: a + (b - a) * t */
static inline DR_VEC3 dr_vec3_lerp(DR_VEC3 a, DR_VEC3 b, double t) {
    DR_VEC3 r = {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t};
    return r;
}

/*! Squared length of vector */
static inline double dr_vec3_length_sq(DR_VEC3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

/*! Length of vector */
static inline double dr_vec3_length(DR_VEC3 v) {
    return sqrt(dr_vec3_length_sq(v));
}

/*! Distance between two points */
static inline double dr_vec3_distance(DR_VEC3 a, DR_VEC3 b) {
    return dr_vec3_length(dr_vec3_sub(a, b));
}

/*---------------------------------------------------------------------------
 * API Functions
 *-------------------------------------------------------------------------*/

/*! create a new dead reckoning entity */
DR_ENTITY* dr_entity_create(DR_ALGO algo, DR_BLEND blend_mode, double pos_threshold, double blend_time);
/*! destroy a dead reckoning entity and free its memory */
void dr_entity_destroy(DR_ENTITY** entity_ptr);
/*! receive a new authoritative state update from the network */
void dr_entity_receive_state(DR_ENTITY* entity,
                             const DR_VEC3* pos,
                             const DR_VEC3* vel,
                             const DR_VEC3* acc,
                             double time);
/*! compute the dead reckoned display position at a given time */
void dr_entity_compute(DR_ENTITY* entity, double time, DR_VEC3* out_pos);
/*! extrapolate a kinematic state forward by dt seconds */
DR_VEC3 dr_entity_extrapolate(const DR_ENTITY* entity, const DR_STATE* state, double dt);
/*! check if the owner's true state has diverged beyond the threshold */
bool dr_entity_check_threshold(const DR_ENTITY* entity,
                               const DR_VEC3* true_pos,
                               const DR_VEC3* true_vel,
                               const DR_VEC3* true_acc,
                               double time);
/*! set the extrapolation algorithm */
void dr_entity_set_algo(DR_ENTITY* entity, DR_ALGO algo);
/*! set the convergence blending mode */
void dr_entity_set_blend_mode(DR_ENTITY* entity, DR_BLEND blend_mode);
/*! set the position error threshold */
void dr_entity_set_threshold(DR_ENTITY* entity, double threshold);
/*! set the convergence blend duration */
void dr_entity_set_blend_time(DR_ENTITY* entity, double blend_time);
/*! force-set entity position without blending */
void dr_entity_set_position(DR_ENTITY* entity,
                            const DR_VEC3* pos,
                            const DR_VEC3* vel,
                            const DR_VEC3* acc,
                            double time);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* N_DEAD_RECKONING_H */
