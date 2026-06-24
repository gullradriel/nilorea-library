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
 *@file n_dead_reckoning.c
 *@brief Dead Reckoning implementation for latency hiding in networked games
 *
 * Implements the algorithms described in:
 * - "Dead Reckoning: Latency Hiding for Networked Games" (Game Developer)
 * - "Believable Dead Reckoning for Networked Games" by Curtiss Murphy
 *   (Game Engine Gems 2, 2011)
 *
 * The three convergence modes:
 *
 * 1. SNAP: Immediately jump to the new position. Cheap but visually jarring.
 *
 * 2. PVB (Projective Velocity Blending):
 *    Extrapolate both old (before update) and new (after update) states
 *    forward, and linearly blend between the two extrapolated positions
 *    over blend_time seconds. Works well for straight-line motion.
 *
 * 3. CUBIC (Cubic Bezier):
 *    Fit a cubic Bezier curve from the old predicted position to the
 *    new extrapolated position. Control points ensure C1 continuity
 *    (velocity matching). Works well for curved paths.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 01/03/2026
 */

#include "nilorea/n_dead_reckoning.h"
#include "nilorea/n_common.h"
#include <stdlib.h>

/**
 *@brief Evaluate a cubic Bezier curve at parameter t in [0, 1].
 * B(t) = (1-t)^3 * P0 + 3(1-t)^2*t * P1 + 3(1-t)*t^2 * P2 + t^3 * P3
 *@param p0 start point
 *@param p1 control point 1
 *@param p2 control point 2
 *@param p3 end point
 *@param t parameter in [0, 1]
 *@return interpolated point
 */
static DR_VEC3 cubic_bezier(DR_VEC3 p0, DR_VEC3 p1, DR_VEC3 p2, DR_VEC3 p3, double t) {
    double u = 1.0 - t;
    double u2 = u * u;
    double u3 = u2 * u;
    double t2 = t * t;
    double t3 = t2 * t;

    DR_VEC3 r;
    r.x = u3 * p0.x + 3.0 * u2 * t * p1.x + 3.0 * u * t2 * p2.x + t3 * p3.x;
    r.y = u3 * p0.y + 3.0 * u2 * t * p1.y + 3.0 * u * t2 * p2.y + t3 * p3.y;
    r.z = u3 * p0.z + 3.0 * u2 * t * p1.z + 3.0 * u * t2 * p2.z + t3 * p3.z;
    return r;
}

/**
 *@brief Create a new dead reckoning entity
 *@param algo extrapolation algorithm (DR_ALGO_STATIC, DR_ALGO_VEL, DR_ALGO_VEL_ACC)
 *@param blend_mode convergence mode (DR_BLEND_SNAP, DR_BLEND_PVB, DR_BLEND_CUBIC)
 *@param pos_threshold distance threshold for triggering network updates
 *@param blend_time convergence blend duration in seconds
 *@return newly allocated entity, or NULL on failure
 */
DR_ENTITY* dr_entity_create(DR_ALGO algo, DR_BLEND blend_mode, double pos_threshold, double blend_time) {
    DR_ENTITY* ent = NULL;
    Malloc(ent, DR_ENTITY, 1);
    __n_assert(ent, return NULL);

    ent->algo = algo;
    ent->blend_mode = blend_mode;
    ent->pos_threshold = pos_threshold;
    ent->blend_time = blend_time;

    ent->last_known.pos = dr_vec3_zero();
    ent->last_known.vel = dr_vec3_zero();
    ent->last_known.acc = dr_vec3_zero();
    ent->last_known.time = 0.0;

    ent->prev_predicted.pos = dr_vec3_zero();
    ent->prev_predicted.vel = dr_vec3_zero();
    ent->prev_predicted.acc = dr_vec3_zero();
    ent->prev_predicted.time = 0.0;

    ent->blend_start = 0.0;
    ent->blending = false;

    ent->bezier_p0 = dr_vec3_zero();
    ent->bezier_p1 = dr_vec3_zero();
    ent->bezier_p2 = dr_vec3_zero();
    ent->bezier_p3 = dr_vec3_zero();

    ent->display_pos = dr_vec3_zero();
    ent->display_vel = dr_vec3_zero();
    ent->update_count = 0;

    return ent;
}

/**
 *@brief Destroy a dead reckoning entity and set the pointer to NULL
 *@param entity_ptr pointer to the entity pointer
 */
void dr_entity_destroy(DR_ENTITY** entity_ptr) {
    __n_assert(entity_ptr, return);
    Free((*entity_ptr));
}

/**
 *@brief Extrapolate a kinematic state forward by dt seconds.
 * Uses the entity's configured algorithm (static, velocity, or velocity+acceleration).
 *@param entity the entity (used for algorithm selection)
 *@param state the state to extrapolate from
 *@param dt time delta in seconds (can be negative for backward extrapolation)
 *@return the extrapolated position
 */
DR_VEC3 dr_entity_extrapolate(const DR_ENTITY* entity, const DR_STATE* state, double dt) {
    switch (entity->algo) {
        case DR_ALGO_STATIC:
            return state->pos;

        case DR_ALGO_VEL:
            return dr_vec3_add(state->pos, dr_vec3_scale(state->vel, dt));

        case DR_ALGO_VEL_ACC:
            /* P(t) = P0 + V0*dt + 0.5*A0*dt^2 */
            return dr_vec3_add(
                dr_vec3_add(state->pos, dr_vec3_scale(state->vel, dt)),
                dr_vec3_scale(state->acc, 0.5 * dt * dt));

        default:
            return state->pos;
    }
}

/**
 *@brief Receive a new authoritative state update from the network.
 *
 * This is called when a new state packet arrives from the network (or when
 * the owner determines a new update is needed).  It records the current
 * predicted position as prev_predicted so the convergence algorithm can
 * smoothly blend from there to the new state's extrapolation.
 *
 *@param entity the entity
 *@param pos new authoritative position (required)
 *@param vel new authoritative velocity (NULL treated as zero)
 *@param acc new authoritative acceleration (NULL treated as zero)
 *@param time timestamp of the update
 */
void dr_entity_receive_state(DR_ENTITY* entity,
                             const DR_VEC3* pos,
                             const DR_VEC3* vel,
                             const DR_VEC3* acc,
                             double time) {
    __n_assert(entity, return);
    __n_assert(pos, return);

    /* Save where we THINK the entity is right now (i.e. extrapolate
     * the old state to the current time), this becomes the starting
     * point for convergence blending. */
    double dt_old = time - entity->last_known.time;
    entity->prev_predicted.pos = dr_entity_extrapolate(entity, &entity->last_known, dt_old);
    entity->prev_predicted.vel = entity->last_known.vel;
    entity->prev_predicted.acc = entity->last_known.acc;
    entity->prev_predicted.time = time;

    /* Store the new authoritative state */
    entity->last_known.pos = *pos;
    entity->last_known.vel = vel ? *vel : dr_vec3_zero();
    entity->last_known.acc = acc ? *acc : dr_vec3_zero();
    entity->last_known.time = time;

    /* Activate blending (unless SNAP mode) */
    if (entity->blend_mode != DR_BLEND_SNAP) {
        entity->blending = true;
        entity->blend_start = time;

        /* For cubic Bezier: compute control points */
        if (entity->blend_mode == DR_BLEND_CUBIC) {
            /* P0 = where we were (old prediction) */
            entity->bezier_p0 = entity->prev_predicted.pos;
            /* P3 = where the new state will be after blend_time */
            entity->bezier_p3 = dr_entity_extrapolate(
                entity, &entity->last_known, entity->blend_time);
            /* P1 = P0 + (old_velocity * blend_time / 3)
             *   Ensures C1 continuity at the start */
            entity->bezier_p1 = dr_vec3_add(
                entity->bezier_p0,
                dr_vec3_scale(entity->prev_predicted.vel, entity->blend_time / 3.0));
            /* P2 = P3 - (new_velocity * blend_time / 3)
             *   Ensures C1 continuity at the end */
            DR_VEC3 end_vel = entity->last_known.vel;
            if (entity->algo == DR_ALGO_VEL_ACC) {
                /* Adjust velocity for acceleration over blend_time */
                end_vel = dr_vec3_add(end_vel,
                                      dr_vec3_scale(entity->last_known.acc, entity->blend_time));
            }
            entity->bezier_p2 = dr_vec3_sub(
                entity->bezier_p3,
                dr_vec3_scale(end_vel, entity->blend_time / 3.0));
        }
    }

    entity->update_count++;
}

/**
 *@brief Compute the dead reckoned display position at a given time.
 *
 * If the entity is currently blending (after receiving a new state update),
 * this returns a convergence-blended position. Otherwise, it returns the
 * pure extrapolated position from the last known state.
 *
 *@param entity the entity
 *@param time current time
 *@param out_pos output: the blended display position
 */
void dr_entity_compute(DR_ENTITY* entity, double time, DR_VEC3* out_pos) {
    __n_assert(entity, return);
    __n_assert(out_pos, return);

    if (!entity->blending) {
        /* No blending: pure extrapolation from last known state */
        double dt = time - entity->last_known.time;
        *out_pos = dr_entity_extrapolate(entity, &entity->last_known, dt);
        entity->display_pos = *out_pos;
        return;
    }

    /* Compute blend parameter t_hat in [0, 1] */
    double elapsed = time - entity->blend_start;
    double t_hat = (entity->blend_time > 0.0) ? elapsed / entity->blend_time : 1.0;

    if (t_hat >= 1.0) {
        /* Blending is complete, switch to pure extrapolation */
        entity->blending = false;
        double dt = time - entity->last_known.time;
        *out_pos = dr_entity_extrapolate(entity, &entity->last_known, dt);
        entity->display_pos = *out_pos;
        return;
    }

    switch (entity->blend_mode) {
        case DR_BLEND_PVB: {
            /* Projective Velocity Blending:
             * Extrapolate BOTH old and new states forward from blend_start,
             * then lerp between the two extrapolated positions. */

            /* Where would the OLD prediction be at (time)? */
            double dt_old = time - entity->prev_predicted.time;
            DR_VEC3 old_extrap = dr_entity_extrapolate(
                entity, &entity->prev_predicted, dt_old);

            /* Where does the NEW state predict the entity is at (time)? */
            double dt_new = time - entity->last_known.time;
            DR_VEC3 new_extrap = dr_entity_extrapolate(
                entity, &entity->last_known, dt_new);

            /* Blend */
            *out_pos = dr_vec3_lerp(old_extrap, new_extrap, t_hat);
            break;
        }

        case DR_BLEND_CUBIC:
            /* Cubic Bezier convergence */
            *out_pos = cubic_bezier(entity->bezier_p0, entity->bezier_p1,
                                    entity->bezier_p2, entity->bezier_p3, t_hat);
            break;

        case DR_BLEND_SNAP:
        default: {
            /* Should not happen (blending is not activated for SNAP), but handle it */
            double dt = time - entity->last_known.time;
            *out_pos = dr_entity_extrapolate(entity, &entity->last_known, dt);
            entity->blending = false;
            break;
        }
    }

    entity->display_pos = *out_pos;
}

/**
 *@brief Check whether the owner's true state has diverged from the
 * dead reckoned prediction beyond the configured threshold.
 *
 * The owner entity runs the same extrapolation algorithm as remotes.
 * If the error exceeds the threshold, a new state update should be sent.
 *
 *@param entity the entity
 *@param true_pos the owner's true position
 *@param true_vel the owner's true velocity (unused, reserved for velocity threshold)
 *@param true_acc the owner's true acceleration (unused, reserved)
 *@param time current time
 *@return true if threshold exceeded (send update), false if prediction is acceptable
 */
bool dr_entity_check_threshold(const DR_ENTITY* entity,
                               const DR_VEC3* true_pos,
                               const DR_VEC3* true_vel,
                               const DR_VEC3* true_acc,
                               double time) {
    (void)true_vel;
    (void)true_acc;

    __n_assert(entity, return false);
    __n_assert(true_pos, return false);

    double dt = time - entity->last_known.time;
    DR_VEC3 predicted = dr_entity_extrapolate(entity, &entity->last_known, dt);
    double error = dr_vec3_distance(predicted, *true_pos);

    return error > entity->pos_threshold;
}

/**
 *@brief Set the extrapolation algorithm
 *@param entity the entity
 *@param algo new algorithm
 */
void dr_entity_set_algo(DR_ENTITY* entity, DR_ALGO algo) {
    __n_assert(entity, return);
    entity->algo = algo;
}

/**
 *@brief Set the convergence blending mode
 *@param entity the entity
 *@param blend_mode new blending mode
 */
void dr_entity_set_blend_mode(DR_ENTITY* entity, DR_BLEND blend_mode) {
    __n_assert(entity, return);
    entity->blend_mode = blend_mode;
}

/**
 *@brief Set the position error threshold for triggering network updates
 *@param entity the entity
 *@param threshold new threshold value (distance units)
 */
void dr_entity_set_threshold(DR_ENTITY* entity, double threshold) {
    __n_assert(entity, return);
    entity->pos_threshold = threshold;
}

/**
 *@brief Set the convergence blend duration
 *@param entity the entity
 *@param blend_time new blend time in seconds
 */
void dr_entity_set_blend_time(DR_ENTITY* entity, double blend_time) {
    __n_assert(entity, return);
    entity->blend_time = blend_time;
}

/**
 *@brief Force-set entity position without triggering convergence blending.
 * Used for teleportation or initial placement.
 *@param entity the entity
 *@param pos position (required)
 *@param vel velocity (NULL treated as zero)
 *@param acc acceleration (NULL treated as zero)
 *@param time timestamp
 */
void dr_entity_set_position(DR_ENTITY* entity,
                            const DR_VEC3* pos,
                            const DR_VEC3* vel,
                            const DR_VEC3* acc,
                            double time) {
    __n_assert(entity, return);
    __n_assert(pos, return);

    entity->last_known.pos = *pos;
    entity->last_known.vel = vel ? *vel : dr_vec3_zero();
    entity->last_known.acc = acc ? *acc : dr_vec3_zero();
    entity->last_known.time = time;

    entity->prev_predicted = entity->last_known;
    entity->display_pos = *pos;
    entity->blending = false;
    entity->update_count = 0;
}
