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
 *@file n_trajectory.c
 *@brief Trajectory interpolation and dead reckoning for 2D/3D
 *
 * Implements the "Defeating Lag With Cubic Splines" approach from
 * GameDev.net Article 914 by Nicholas Van Caldwell, using cubic
 * Hermite spline interpolation with quadratic extrapolation.
 *
 * Cubic Hermite spline formula:
 *   P(s) = h00(s)*P0 + h10(s)*M0 + h01(s)*P1 + h11(s)*M1
 *
 * Where s is normalized time in [0,1], P0/P1 are endpoint positions,
 * M0/M1 are tangent vectors (velocity * duration), and:
 *   h00(s) =  2s^3 - 3s^2 + 1   (start position weight)
 *   h10(s) =   s^3 - 2s^2 + s   (start tangent weight)
 *   h01(s) = -2s^3 + 3s^2       (end position weight)
 *   h11(s) =   s^3 -  s^2       (end tangent weight)
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 28/02/2026
 */

#include "nilorea/n_trajectory.h"
#include <math.h>

/**
 *@brief Hermite basis function h00: weights the start position
 *@param s normalized time in [0,1]
 *@return h00(s) = 2s^3 - 3s^2 + 1
 */
static double hermite_h00(double s) {
    return 2.0 * s * s * s - 3.0 * s * s + 1.0;
}

/**
 *@brief Hermite basis function h10: weights the start tangent
 *@param s normalized time in [0,1]
 *@return h10(s) = s^3 - 2s^2 + s
 */
static double hermite_h10(double s) {
    return s * s * s - 2.0 * s * s + s;
}

/**
 *@brief Hermite basis function h01: weights the end position
 *@param s normalized time in [0,1]
 *@return h01(s) = -2s^3 + 3s^2
 */
static double hermite_h01(double s) {
    return -2.0 * s * s * s + 3.0 * s * s;
}

/**
 *@brief Hermite basis function h11: weights the end tangent
 *@param s normalized time in [0,1]
 *@return h11(s) = s^3 - s^2
 */
static double hermite_h11(double s) {
    return s * s * s - s * s;
}

/**
 *@brief First derivative of h00: dh00/ds = 6s^2 - 6s
 *@param s normalized time in [0,1]
 *@return dh00(s)
 */
static double hermite_dh00(double s) {
    return 6.0 * s * s - 6.0 * s;
}

/**
 *@brief First derivative of h10: dh10/ds = 3s^2 - 4s + 1
 *@param s normalized time in [0,1]
 *@return dh10(s)
 */
static double hermite_dh10(double s) {
    return 3.0 * s * s - 4.0 * s + 1.0;
}

/**
 *@brief First derivative of h01: dh01/ds = -6s^2 + 6s
 *@param s normalized time in [0,1]
 *@return dh01(s)
 */
static double hermite_dh01(double s) {
    return -6.0 * s * s + 6.0 * s;
}

/**
 *@brief First derivative of h11: dh11/ds = 3s^2 - 2s
 *@param s normalized time in [0,1]
 *@return dh11(s)
 */
static double hermite_dh11(double s) {
    return 3.0 * s * s - 2.0 * s;
}

/**
 *@brief Second derivative of h00: d2h00/ds2 = 12s - 6
 *@param s normalized time in [0,1]
 *@return d2h00(s)
 */
static double hermite_d2h00(double s) {
    return 12.0 * s - 6.0;
}

/**
 *@brief Second derivative of h10: d2h10/ds2 = 6s - 4
 *@param s normalized time in [0,1]
 *@return d2h10(s)
 */
static double hermite_d2h10(double s) {
    return 6.0 * s - 4.0;
}

/**
 *@brief Second derivative of h01: d2h01/ds2 = -12s + 6
 *@param s normalized time in [0,1]
 *@return d2h01(s)
 */
static double hermite_d2h01(double s) {
    return -12.0 * s + 6.0;
}

/**
 *@brief Second derivative of h11: d2h11/ds2 = 6s - 2
 *@param s normalized time in [0,1]
 *@return d2h11(s)
 */
static double hermite_d2h11(double s) {
    return 6.0 * s - 2.0;
}

/**
 *@brief Recompute the Hermite tangent vectors from current start/end states.
 * Tangents are: m = velocity * duration (for position) and angular_speed * duration (for orientation)
 *@param traj The trajectory to update tangents for
 */
static void trajectory_compute_tangents(TRAJECTORY* traj) {
    for (int i = 0; i < traj->nb_components; i++) {
        traj->m0[i] = traj->start.speed[i] * traj->duration;
        traj->m1[i] = traj->end.speed[i] * traj->duration;
        traj->m0_orient[i] = traj->start.angular_speed[i] * traj->duration;
        traj->m1_orient[i] = traj->end.angular_speed[i] * traj->duration;
    }
}

/**
 *@brief Load the correct spline segment for a given time when using multi-point paths.
 * Searches forward/backward from current_segment for efficiency with sequential access.
 * Updates traj->start, end, start_time, end_time, duration, and tangents for the found segment.
 *@param traj The trajectory (must have nb_points >= 2)
 *@param time_val The time to find the segment for
 */
static void trajectory_load_segment(TRAJECTORY* traj, double time_val) {
    if (traj->nb_points < 2) return;

    int seg = traj->current_segment;
    if (seg < 0) seg = 0;

    /* search forward from current segment */
    while (seg < traj->nb_points - 2 &&
           time_val >= traj->points[seg + 1].time_val) {
        seg++;
    }
    /* search backward if needed */
    while (seg > 0 && time_val < traj->points[seg].time_val) {
        seg--;
    }

    if (seg != traj->current_segment) {
        traj->current_segment = seg;
        memcpy(&traj->start, &traj->points[seg].state, sizeof(PHYSICS));
        memcpy(&traj->end, &traj->points[seg + 1].state, sizeof(PHYSICS));
        traj->start_time = traj->points[seg].time_val;
        traj->end_time = traj->points[seg + 1].time_val;
        traj->duration = traj->end_time - traj->start_time;
        trajectory_compute_tangents(traj);
    }
}

/**
 *@brief Allocate and initialize a new TRAJECTORY
 *@param nb_components TRAJECTORY_2D (2) or TRAJECTORY_3D (3)
 *@return A new TRAJECTORY or NULL on error
 */
TRAJECTORY* trajectory_new(int nb_components) {
    if (nb_components != TRAJECTORY_2D && nb_components != TRAJECTORY_3D) {
        n_log(LOG_ERR, "nb_components must be TRAJECTORY_2D (2) or TRAJECTORY_3D (3), got %d", nb_components);
        return NULL;
    }

    TRAJECTORY* traj = NULL;
    Malloc(traj, TRAJECTORY, 1);
    __n_assert(traj, return NULL);

    traj->nb_components = nb_components;
    traj->mode = TRAJECTORY_BEFORE;
    traj->start_time = 0.0;
    traj->end_time = 0.0;
    traj->duration = 0.0;
    traj->current_time = 0.0;
    traj->points = NULL;
    traj->nb_points = 0;
    traj->nb_points_allocated = 0;
    traj->current_segment = -1;

    return traj;
} /* trajectory_new() */

/**
 *@brief Free a TRAJECTORY and set the pointer to NULL
 *@param traj Pointer to the TRAJECTORY pointer to free
 */
void trajectory_delete(TRAJECTORY** traj) {
    __n_assert(traj, return);
    Free((*traj)->points);
    Free((*traj));
} /* trajectory_delete() */

/**
 *@brief Set the initial (start) state and time of the trajectory
 *@param traj The trajectory to configure
 *@param state The physics state at the start point
 *@param time_val The timestamp of this state
 *@return TRUE or FALSE
 */
int trajectory_set_start(TRAJECTORY* traj, PHYSICS* state, double time_val) {
    __n_assert(traj, return FALSE);
    __n_assert(state, return FALSE);

    memcpy(&traj->start, state, sizeof(PHYSICS));
    traj->start_time = time_val;
    traj->duration = traj->end_time - traj->start_time;

    if (traj->duration > 0.0) {
        trajectory_compute_tangents(traj);
    }

    return TRUE;
} /* trajectory_set_start() */

/**
 *@brief Set the terminal (end) state and time of the trajectory.
 * Recomputes the Hermite tangent vectors.
 *@param traj The trajectory to configure
 *@param state The physics state at the end point
 *@param time_val The timestamp of this state (must be > start_time)
 *@return TRUE or FALSE
 */
int trajectory_set_end(TRAJECTORY* traj, PHYSICS* state, double time_val) {
    __n_assert(traj, return FALSE);
    __n_assert(state, return FALSE);

    memcpy(&traj->end, state, sizeof(PHYSICS));
    traj->end_time = time_val;
    traj->duration = traj->end_time - traj->start_time;

    if (traj->duration <= 0.0) {
        n_log(LOG_ERR, "end_time (%g) must be greater than start_time (%g)", time_val, traj->start_time);
        return FALSE;
    }

    trajectory_compute_tangents(traj);

    return TRUE;
} /* trajectory_set_end() */

/**
 *@brief Update the trajectory with a new end state (dead reckoning shift).
 * The previous end state becomes the new start state, and the new state becomes the end.
 * This is the core function for continuous dead reckoning: call it each time a new
 * network update arrives to create a smooth spline from the current endpoint to the new one.
 *@param traj The trajectory to update
 *@param new_end The new physics state received (e.g. from a network packet)
 *@param time_val The timestamp of the new state
 *@return TRUE or FALSE
 */
int trajectory_update(TRAJECTORY* traj, PHYSICS* new_end, double time_val) {
    __n_assert(traj, return FALSE);
    __n_assert(new_end, return FALSE);

    if (time_val <= traj->end_time) {
        n_log(LOG_ERR, "new time (%g) must be greater than current end_time (%g)", time_val, traj->end_time);
        return FALSE;
    }

    /* shift: old end becomes new start */
    memcpy(&traj->start, &traj->end, sizeof(PHYSICS));
    traj->start_time = traj->end_time;

    /* set new end */
    memcpy(&traj->end, new_end, sizeof(PHYSICS));
    traj->end_time = time_val;
    traj->duration = traj->end_time - traj->start_time;

    trajectory_compute_tangents(traj);

    return TRUE;
} /* trajectory_update() */

/**
 *@brief Compute the full state (position, speed, acceleration, orientation, angular_speed)
 * at a given time using cubic Hermite interpolation (within the interval) or
 * quadratic extrapolation (outside the interval).
 *@param traj The trajectory
 *@param time_val The time at which to compute the state
 *@return TRUE or FALSE
 */
int trajectory_compute(TRAJECTORY* traj, double time_val) {
    __n_assert(traj, return FALSE);

    trajectory_load_segment(traj, time_val);
    traj->current_time = time_val;
    int nc = traj->nb_components;

    if (traj->duration <= 0.0) {
        /* no valid interval: just copy start state */
        memcpy(&traj->current, &traj->start, sizeof(PHYSICS));
        traj->mode = TRAJECTORY_BEFORE;
        return TRUE;
    }

    if (time_val < traj->start_time) {
        /* before start: quadratic extrapolation backward from start state */
        traj->mode = TRAJECTORY_BEFORE;
        double dt = time_val - traj->start_time; /* negative */
        for (int i = 0; i < nc; i++) {
            traj->current.position[i] = traj->start.position[i] + traj->start.speed[i] * dt + 0.5 * traj->start.acceleration[i] * dt * dt;
            traj->current.speed[i] = traj->start.speed[i] + traj->start.acceleration[i] * dt;
            traj->current.acceleration[i] = traj->start.acceleration[i];
            traj->current.orientation[i] = traj->start.orientation[i] + traj->start.angular_speed[i] * dt + 0.5 * traj->start.angular_acceleration[i] * dt * dt;
            traj->current.angular_speed[i] = traj->start.angular_speed[i] + traj->start.angular_acceleration[i] * dt;
            traj->current.angular_acceleration[i] = traj->start.angular_acceleration[i];
        }
    } else if (time_val > traj->end_time) {
        /* after end: quadratic extrapolation forward from end state */
        traj->mode = TRAJECTORY_EXTRAP;
        double dt = time_val - traj->end_time;
        for (int i = 0; i < nc; i++) {
            traj->current.position[i] = traj->end.position[i] + traj->end.speed[i] * dt + 0.5 * traj->end.acceleration[i] * dt * dt;
            traj->current.speed[i] = traj->end.speed[i] + traj->end.acceleration[i] * dt;
            traj->current.acceleration[i] = traj->end.acceleration[i];
            traj->current.orientation[i] = traj->end.orientation[i] + traj->end.angular_speed[i] * dt + 0.5 * traj->end.angular_acceleration[i] * dt * dt;
            traj->current.angular_speed[i] = traj->end.angular_speed[i] + traj->end.angular_acceleration[i] * dt;
            traj->current.angular_acceleration[i] = traj->end.angular_acceleration[i];
        }
    } else {
        /* within interval: cubic Hermite spline interpolation */
        traj->mode = TRAJECTORY_INTERP;
        double s = (time_val - traj->start_time) / traj->duration;

        /* Hermite basis function values */
        double h00 = hermite_h00(s);
        double h10 = hermite_h10(s);
        double h01 = hermite_h01(s);
        double h11 = hermite_h11(s);

        /* first derivatives for velocity */
        double dh00 = hermite_dh00(s);
        double dh10 = hermite_dh10(s);
        double dh01 = hermite_dh01(s);
        double dh11 = hermite_dh11(s);

        /* second derivatives for acceleration */
        double d2h00 = hermite_d2h00(s);
        double d2h10 = hermite_d2h10(s);
        double d2h01 = hermite_d2h01(s);
        double d2h11 = hermite_d2h11(s);

        double inv_dur = 1.0 / traj->duration;
        double inv_dur2 = inv_dur * inv_dur;

        for (int i = 0; i < nc; i++) {
            /* position: P(s) = h00*P0 + h10*M0 + h01*P1 + h11*M1 */
            traj->current.position[i] = h00 * traj->start.position[i] + h10 * traj->m0[i] + h01 * traj->end.position[i] + h11 * traj->m1[i];

            /* velocity: V(t) = P'(s) / duration */
            traj->current.speed[i] = (dh00 * traj->start.position[i] + dh10 * traj->m0[i] + dh01 * traj->end.position[i] + dh11 * traj->m1[i]) * inv_dur;

            /* acceleration: A(t) = P''(s) / duration^2 */
            traj->current.acceleration[i] = (d2h00 * traj->start.position[i] + d2h10 * traj->m0[i] + d2h01 * traj->end.position[i] + d2h11 * traj->m1[i]) * inv_dur2;

            /* orientation: same Hermite approach */
            traj->current.orientation[i] = h00 * traj->start.orientation[i] + h10 * traj->m0_orient[i] + h01 * traj->end.orientation[i] + h11 * traj->m1_orient[i];

            /* angular speed: derivative of orientation spline / duration */
            traj->current.angular_speed[i] = (dh00 * traj->start.orientation[i] + dh10 * traj->m0_orient[i] + dh01 * traj->end.orientation[i] + dh11 * traj->m1_orient[i]) * inv_dur;

            /* angular acceleration: second derivative / duration^2 */
            traj->current.angular_acceleration[i] = (d2h00 * traj->start.orientation[i] + d2h10 * traj->m0_orient[i] + d2h01 * traj->end.orientation[i] + d2h11 * traj->m1_orient[i]) * inv_dur2;
        }
    }

    /* zero out unused components for safety */
    for (int i = nc; i < 3; i++) {
        traj->current.position[i] = 0.0;
        traj->current.speed[i] = 0.0;
        traj->current.acceleration[i] = 0.0;
        traj->current.orientation[i] = 0.0;
        traj->current.angular_speed[i] = 0.0;
        traj->current.angular_acceleration[i] = 0.0;
    }

    return TRUE;
} /* trajectory_compute() */

/**
 *@brief Compute position at a given time.
 * This is a lightweight version of trajectory_compute that only computes position.
 *@param traj The trajectory
 *@param time_val The time at which to compute
 *@param out VECTOR3D to store the result
 *@return TRUE or FALSE
 */
int trajectory_get_position(TRAJECTORY* traj, double time_val, VECTOR3D out) {
    __n_assert(traj, return FALSE);
    __n_assert(out, return FALSE);

    trajectory_load_segment(traj, time_val);
    int nc = traj->nb_components;

    if (traj->duration <= 0.0) {
        for (int i = 0; i < nc; i++) out[i] = traj->start.position[i];
        for (int i = nc; i < 3; i++) out[i] = 0.0;
        return TRUE;
    }

    if (time_val < traj->start_time) {
        double dt = time_val - traj->start_time;
        for (int i = 0; i < nc; i++) {
            out[i] = traj->start.position[i] + traj->start.speed[i] * dt + 0.5 * traj->start.acceleration[i] * dt * dt;
        }
    } else if (time_val > traj->end_time) {
        double dt = time_val - traj->end_time;
        for (int i = 0; i < nc; i++) {
            out[i] = traj->end.position[i] + traj->end.speed[i] * dt + 0.5 * traj->end.acceleration[i] * dt * dt;
        }
    } else {
        double s = (time_val - traj->start_time) / traj->duration;
        double h00 = hermite_h00(s);
        double h10 = hermite_h10(s);
        double h01 = hermite_h01(s);
        double h11 = hermite_h11(s);
        for (int i = 0; i < nc; i++) {
            out[i] = h00 * traj->start.position[i] + h10 * traj->m0[i] + h01 * traj->end.position[i] + h11 * traj->m1[i];
        }
    }

    for (int i = nc; i < 3; i++) out[i] = 0.0;
    return TRUE;
} /* trajectory_get_position() */

/**
 *@brief Compute velocity at a given time.
 *@param traj The trajectory
 *@param time_val The time at which to compute
 *@param out VECTOR3D to store the result
 *@return TRUE or FALSE
 */
int trajectory_get_speed(TRAJECTORY* traj, double time_val, VECTOR3D out) {
    __n_assert(traj, return FALSE);
    __n_assert(out, return FALSE);

    trajectory_load_segment(traj, time_val);
    int nc = traj->nb_components;

    if (traj->duration <= 0.0) {
        for (int i = 0; i < nc; i++) out[i] = traj->start.speed[i];
        for (int i = nc; i < 3; i++) out[i] = 0.0;
        return TRUE;
    }

    if (time_val < traj->start_time) {
        double dt = time_val - traj->start_time;
        for (int i = 0; i < nc; i++) {
            out[i] = traj->start.speed[i] + traj->start.acceleration[i] * dt;
        }
    } else if (time_val > traj->end_time) {
        double dt = time_val - traj->end_time;
        for (int i = 0; i < nc; i++) {
            out[i] = traj->end.speed[i] + traj->end.acceleration[i] * dt;
        }
    } else {
        double s = (time_val - traj->start_time) / traj->duration;
        double inv_dur = 1.0 / traj->duration;
        double dh00 = hermite_dh00(s);
        double dh10 = hermite_dh10(s);
        double dh01 = hermite_dh01(s);
        double dh11 = hermite_dh11(s);
        for (int i = 0; i < nc; i++) {
            out[i] = (dh00 * traj->start.position[i] + dh10 * traj->m0[i] + dh01 * traj->end.position[i] + dh11 * traj->m1[i]) * inv_dur;
        }
    }

    for (int i = nc; i < 3; i++) out[i] = 0.0;
    return TRUE;
} /* trajectory_get_speed() */

/**
 *@brief Compute acceleration at a given time.
 *@param traj The trajectory
 *@param time_val The time at which to compute
 *@param out VECTOR3D to store the result
 *@return TRUE or FALSE
 */
int trajectory_get_acceleration(TRAJECTORY* traj, double time_val, VECTOR3D out) {
    __n_assert(traj, return FALSE);
    __n_assert(out, return FALSE);

    trajectory_load_segment(traj, time_val);
    int nc = traj->nb_components;

    if (traj->duration <= 0.0) {
        for (int i = 0; i < nc; i++) out[i] = traj->start.acceleration[i];
        for (int i = nc; i < 3; i++) out[i] = 0.0;
        return TRUE;
    }

    if (time_val < traj->start_time || time_val > traj->end_time) {
        /* extrapolation: constant acceleration from nearest endpoint */
        PHYSICS* ref = (time_val < traj->start_time) ? &traj->start : &traj->end;
        for (int i = 0; i < nc; i++) {
            out[i] = ref->acceleration[i];
        }
    } else {
        double s = (time_val - traj->start_time) / traj->duration;
        double inv_dur2 = 1.0 / (traj->duration * traj->duration);
        double d2h00 = hermite_d2h00(s);
        double d2h10 = hermite_d2h10(s);
        double d2h01 = hermite_d2h01(s);
        double d2h11 = hermite_d2h11(s);
        for (int i = 0; i < nc; i++) {
            out[i] = (d2h00 * traj->start.position[i] + d2h10 * traj->m0[i] + d2h01 * traj->end.position[i] + d2h11 * traj->m1[i]) * inv_dur2;
        }
    }

    for (int i = nc; i < 3; i++) out[i] = 0.0;
    return TRUE;
} /* trajectory_get_acceleration() */

/**
 *@brief Compute orientation at a given time.
 *@param traj The trajectory
 *@param time_val The time at which to compute
 *@param out VECTOR3D to store the result
 *@return TRUE or FALSE
 */
int trajectory_get_orientation(TRAJECTORY* traj, double time_val, VECTOR3D out) {
    __n_assert(traj, return FALSE);
    __n_assert(out, return FALSE);

    trajectory_load_segment(traj, time_val);
    int nc = traj->nb_components;

    if (traj->duration <= 0.0) {
        for (int i = 0; i < nc; i++) out[i] = traj->start.orientation[i];
        for (int i = nc; i < 3; i++) out[i] = 0.0;
        return TRUE;
    }

    if (time_val < traj->start_time) {
        double dt = time_val - traj->start_time;
        for (int i = 0; i < nc; i++) {
            out[i] = traj->start.orientation[i] + traj->start.angular_speed[i] * dt + 0.5 * traj->start.angular_acceleration[i] * dt * dt;
        }
    } else if (time_val > traj->end_time) {
        double dt = time_val - traj->end_time;
        for (int i = 0; i < nc; i++) {
            out[i] = traj->end.orientation[i] + traj->end.angular_speed[i] * dt + 0.5 * traj->end.angular_acceleration[i] * dt * dt;
        }
    } else {
        double s = (time_val - traj->start_time) / traj->duration;
        double h00 = hermite_h00(s);
        double h10 = hermite_h10(s);
        double h01 = hermite_h01(s);
        double h11 = hermite_h11(s);
        for (int i = 0; i < nc; i++) {
            out[i] = h00 * traj->start.orientation[i] + h10 * traj->m0_orient[i] + h01 * traj->end.orientation[i] + h11 * traj->m1_orient[i];
        }
    }

    for (int i = nc; i < 3; i++) out[i] = 0.0;
    return TRUE;
} /* trajectory_get_orientation() */

/**
 *@brief Add a waypoint to the multi-point trajectory path.
 * Waypoints must be added in strictly increasing chronological order.
 * When 2+ waypoints exist, trajectory_compute and trajectory_get_* functions
 * automatically find and interpolate the correct segment.
 *@param traj The trajectory
 *@param state The physics state at this waypoint
 *@param time_val The timestamp of this waypoint (must be > previous point's time)
 *@return TRUE or FALSE
 */
int trajectory_add_point(TRAJECTORY* traj, PHYSICS* state, double time_val) {
    __n_assert(traj, return FALSE);
    __n_assert(state, return FALSE);

    /* enforce chronological order */
    if (traj->nb_points > 0 && time_val <= traj->points[traj->nb_points - 1].time_val) {
        n_log(LOG_ERR, "time_val (%g) must be greater than last point time (%g)",
              time_val, traj->points[traj->nb_points - 1].time_val);
        return FALSE;
    }

    /* grow array if needed */
    if (traj->nb_points >= traj->nb_points_allocated) {
        int new_size = (traj->nb_points_allocated == 0) ? 8 : traj->nb_points_allocated * 2;
        if (!Realloc(traj->points, TRAJECTORY_POINT, (size_t)new_size)) {
            return FALSE;
        }
        traj->nb_points_allocated = new_size;
    }

    memcpy(&traj->points[traj->nb_points].state, state, sizeof(PHYSICS));
    traj->points[traj->nb_points].time_val = time_val;
    traj->nb_points++;

    /* when we reach 2 points, load the first segment */
    if (traj->nb_points == 2) {
        traj->current_segment = -1; /* force reload */
        trajectory_load_segment(traj, traj->points[0].time_val);
    }

    return TRUE;
} /* trajectory_add_point() */

/**
 *@brief Clear all waypoints from the multi-point path.
 * Keeps the allocated memory for reuse. Resets start/end/duration.
 *@param traj The trajectory
 *@return TRUE or FALSE
 */
int trajectory_clear_points(TRAJECTORY* traj) {
    __n_assert(traj, return FALSE);

    traj->nb_points = 0;
    traj->current_segment = -1;
    memset(&traj->start, 0, sizeof(PHYSICS));
    memset(&traj->end, 0, sizeof(PHYSICS));
    traj->start_time = 0.0;
    traj->end_time = 0.0;
    traj->duration = 0.0;

    return TRUE;
} /* trajectory_clear_points() */

/**
 *@brief Compute the approximate arc length (distance traveled) along the trajectory
 * between two times, using numerical integration with the given number of steps.
 *@param traj The trajectory
 *@param time_a Start time
 *@param time_b End time
 *@param steps Number of integration steps (higher = more precise, 100 is usually enough)
 *@return The approximate distance, or -1.0 on error
 */
double trajectory_distance(TRAJECTORY* traj, double time_a, double time_b, int steps) {
    __n_assert(traj, return -1.0);

    if (steps < 1) steps = 1;

    double dist = 0.0;
    double dt = (time_b - time_a) / (double)steps;
    VECTOR3D prev, curr;
    int nc = traj->nb_components;

    trajectory_get_position(traj, time_a, prev);

    for (int step = 1; step <= steps; step++) {
        double t = time_a + dt * (double)step;
        trajectory_get_position(traj, t, curr);

        double seg = 0.0;
        for (int i = 0; i < nc; i++) {
            double d = curr[i] - prev[i];
            seg += d * d;
        }
        dist += sqrt(seg);

        for (int i = 0; i < 3; i++) prev[i] = curr[i];
    }

    return dist;
} /* trajectory_distance() */
