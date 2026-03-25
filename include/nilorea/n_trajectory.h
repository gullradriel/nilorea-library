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
 *@file n_trajectory.h
 *@brief Trajectory interpolation and dead reckoning for 2D/3D networked simulations
 *
 * Implementation of the "Defeating Lag With Cubic Splines" approach
 * (GameDev.net Article 914 by Nicholas Van Caldwell) combined with
 * cubic Hermite spline interpolation and quadratic extrapolation.
 *
 * The API provides smooth movement prediction for networked games and
 * local simulations by:
 * - Using cubic Hermite spline interpolation between two known states
 *   (matching position and velocity at both endpoints)
 * - Quadratic extrapolation beyond the known interval using velocity
 *   and acceleration
 * - Smooth orientation interpolation using the same Hermite approach
 *
 * Works for both 2D (xy) and 3D (xyz) coordinate systems.
 *
 * Time units are user-defined: as long as position, speed, acceleration,
 * and time values use consistent units, the math works. For example:
 * - position in pixels, speed in pixels/sec, accel in pixels/sec^2, time in sec
 * - position in meters, speed in m/usec, accel in m/usec^2, time in usec
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 28/02/2026
 */

#ifndef _N_TRAJECTORY_HEADER
#define _N_TRAJECTORY_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup TRAJECTORY TRAJECTORY: cubic Hermite spline dead reckoning and movement simulation for 2D/3D
  @addtogroup TRAJECTORY
  @{
  */

#include "n_3d.h"

/*! trajectory is interpolating along the cubic Hermite spline between start and end */
#define TRAJECTORY_INTERP 1
/*! trajectory is extrapolating beyond the end state using quadratic motion */
#define TRAJECTORY_EXTRAP 2
/*! trajectory is extrapolating before the start state using quadratic motion */
#define TRAJECTORY_BEFORE 3

/*! use 2 components (x,y) for trajectory computation */
#define TRAJECTORY_2D 2
/*! use 3 components (x,y,z) for trajectory computation */
#define TRAJECTORY_3D 3

/*! a single waypoint in a multi-point trajectory path */
typedef struct TRAJECTORY_POINT {
    /*! physics state (position, speed, etc.) at this waypoint */
    PHYSICS state;
    /*! timestamp of this waypoint */
    double time_val;
} TRAJECTORY_POINT;

/*! structure holding all data for trajectory interpolation / extrapolation */
typedef struct TRAJECTORY {
    /*! state at trajectory start (initial known state) */
    PHYSICS start;
    /*! state at trajectory end (terminal known state) */
    PHYSICS end;
    /*! current computed state at current_time */
    PHYSICS current;

    /*! timestamp of start state */
    double start_time;
    /*! timestamp of end state */
    double end_time;
    /*! duration = end_time - start_time */
    double duration;
    /*! last computed time */
    double current_time;

    /*! Hermite tangent at start for position (start.speed * duration) */
    VECTOR3D m0;
    /*! Hermite tangent at end for position (end.speed * duration) */
    VECTOR3D m1;
    /*! Hermite tangent at start for orientation (start.angular_speed * duration) */
    VECTOR3D m0_orient;
    /*! Hermite tangent at end for orientation (end.angular_speed * duration) */
    VECTOR3D m1_orient;

    /*! array of waypoints for multi-point paths (NULL if single segment) */
    TRAJECTORY_POINT* points;
    /*! number of waypoints in the points array */
    int nb_points;
    /*! allocated capacity of the points array */
    int nb_points_allocated;
    /*! index of the currently loaded segment (points[i] -> points[i+1]), -1 if none */
    int current_segment;

    /*! number of components to process: TRAJECTORY_2D (2) or TRAJECTORY_3D (3) */
    int nb_components;
    /*! current computation mode: TRAJECTORY_INTERP, TRAJECTORY_EXTRAP, or TRAJECTORY_BEFORE */
    int mode;
} TRAJECTORY;

/*! allocate a new trajectory for 2D or 3D use */
TRAJECTORY* trajectory_new(int nb_components);
/*! free a trajectory */
void trajectory_delete(TRAJECTORY** traj);

/*! set the initial state and time of the trajectory */
int trajectory_set_start(TRAJECTORY* traj, PHYSICS* state, double time_val);
/*! set the terminal state and time of the trajectory, recomputes Hermite tangents */
int trajectory_set_end(TRAJECTORY* traj, PHYSICS* state, double time_val);
/*! shift: old end becomes new start, set a new end state (for continuous dead reckoning updates) */
int trajectory_update(TRAJECTORY* traj, PHYSICS* new_end, double time_val);

/*! compute full state (position, speed, acceleration, orientation) at given time */
int trajectory_compute(TRAJECTORY* traj, double time_val);
/*! compute only position at given time, stores result in out */
int trajectory_get_position(TRAJECTORY* traj, double time_val, VECTOR3D out);
/*! compute only speed at given time, stores result in out */
int trajectory_get_speed(TRAJECTORY* traj, double time_val, VECTOR3D out);
/*! compute only acceleration at given time, stores result in out */
int trajectory_get_acceleration(TRAJECTORY* traj, double time_val, VECTOR3D out);
/*! compute only orientation at given time, stores result in out */
int trajectory_get_orientation(TRAJECTORY* traj, double time_val, VECTOR3D out);

/*! add a waypoint to the multi-point path (times must be strictly increasing) */
int trajectory_add_point(TRAJECTORY* traj, PHYSICS* state, double time_val);
/*! clear all waypoints from the multi-point path */
int trajectory_clear_points(TRAJECTORY* traj);

/*! distance along the trajectory between two times */
double trajectory_distance(TRAJECTORY* traj, double time_a, double time_b, int steps);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif  // header guard
