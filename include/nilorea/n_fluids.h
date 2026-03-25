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
 *@file n_fluids.h
 *@brief Fluid management port from "How to write an Eulerian fluid simulator with 200 lines of code", by Ten Minute Physics ( https://www.youtube.com/watch?v=iKAVRgIrUOU )
 *@author Castagnier Mickael aka Gull Ra Driel
 *@version 1.0
 *@date 31/12/2022
 */

#ifndef __N_FLUID_HEADER
#define __N_FLUID_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup N_FLUIDS FLUIDS: fluid simulation example
  @addtogroup N_FLUIDS
  @{
  */

#include "allegro5/allegro.h"
#include <allegro5/allegro_primitives.h>
#include "nilorea/n_list.h"
#include "nilorea/n_thread_pool.h"

#ifndef _z
/*! test if component is near zero, according to fluid's precision */
#define _z(__fluid, __component) ((__fluid->__component) > (__fluid->negative_float_tolerance) && (__fluid->__component) < (__fluid->positive_float_tolerance))
#endif

#ifndef _zd
/*! test if value is near zero, according to fluid's precision */
#define _zd(__fluid, __value) ((__value) > (__fluid->negative_float_tolerance) && (__value) < (__fluid->positive_float_tolerance))
#endif

/*! structure passed to a threaded fluid process */
typedef struct N_FLUID_THREAD_PARAMS {
    /*! pointer to data which will be used in the proc */
    void* ptr;
    /*! x start point */
    size_t x_start;
    /*! x end point */
    size_t x_end;
    /*! y start point */
    size_t y_start;
    /*! y end point */
    size_t y_end;
} N_FLUID_THREAD_PARAMS;

/*! structure of a fluid */
typedef struct N_FLUID {
    /*! number of cells in X */
    size_t numX;
    /*! number of cells in Y */
    size_t numY;
    /*! number of cells in Z */
    size_t numZ;
    /*! total number of cells */
    size_t numCells;
    /*! number of fluid processing iterations for each frame */
    size_t numIters;
    /*! density of the fluid (not working ?)*/
    double density;
    /*! time between frames */
    double dt;
    /*! */
    double h;
    /*! gravity on Y */
    double gravity;
    /*! over relaxation */
    double overRelaxation;
    /*! display fluid as a colored cloud instead of black and white, can be combined with showPressure*/
    bool showSmoke;
    /*! activate a fonky palette, override smoke and pressure display */
    bool showPaint;
    /*! display fluids pressures as color variations, can be combined with showSmoke */
    bool showPressure;

    /*! fluid double negative precision setting */
    double negative_float_tolerance;
    /*! fluid double positive precision setting */
    double positive_float_tolerance;

    /*! size of the produced fluid */
    double fluid_production_percentage;
    /*! scale used to deduce cellX and cellY from screen/window width and height */
    double cScale;

    /*! holder for u arrays */
    double* u;
    /*! holder for newU arrays */
    double* newU;

    /*! holder for v arrays */
    double* v;
    /*! holder for newV arrays */
    double* newV;

    /*! holder for p arrays */
    double* p;
    /*! holder for s arrays */
    double* s;

    /*! holder for m arrays */
    double* m;
    /*! holder for newM arrays */
    double* newM;

    /*! preprocessed list of threaded procs parameters, for n_fluid_integrate  */
    LIST* integrate_chunk_list;
    /*! preprocessed list of threaded procs parameters, for n_fluid_solveIncompressibility  */
    LIST* solveIncompressibility_chunk_list;
    /*! preprocessed list of threaded procs parameters, for n_fluid_advectVel */
    LIST* advectVel_chunk_list;
    /*! preprocessed list of threaded procs parameters, for n_fluid_advectSmoke  */
    LIST* advectSmoke_chunk_list;

} N_FLUID;

/*! destroy a fluid simulation */
int destroy_n_fluid(N_FLUID** fluid);
/*! create a new fluid simulation */
N_FLUID* new_n_fluid(double density, double gravity, size_t numIters, double dt, double overRelaxation, size_t sx, size_t sy);

/*! integrate fluid velocities */
int n_fluid_integrate(N_FLUID* fluid);
/*! solve incompressibility constraint */
int n_fluid_solveIncompressibility(N_FLUID* fluid);
/*! extrapolate fluid boundaries */
int n_fluid_extrapolate(N_FLUID* fluid);

/*! sample a field value at given coordinates */
double n_fluid_sampleField(N_FLUID* fluid, double x, double y, uint32_t field);
/*! compute average U velocity at cell */
double n_fluid_avgU(N_FLUID* fluid, size_t i, size_t j);
/*! compute average V velocity at cell */
double n_fluid_avgV(N_FLUID* fluid, size_t i, size_t j);

/*! advect velocity field */
int n_fluid_advectVel(N_FLUID* fluid);
/*! advect smoke density field */
int n_fluid_advectSmoke(N_FLUID* fluid);

/*! run one simulation step */
int n_fluid_simulate(N_FLUID* fluid);
/*! run one simulation step using thread pool */
int n_fluid_simulate_threaded(N_FLUID* fluid, THREAD_POOL* thread_pool);

/*! set an obstacle in the fluid */
int n_fluid_setObstacle(N_FLUID* fluid, double x, double y, double vx, double vy, double r);
/*! reset all obstacles */
int n_fluid_resetObstacles(N_FLUID* fluid);

/*! get a scientific color for a value */
ALLEGRO_COLOR n_fluid_getSciColor(N_FLUID* fluid, double val, double minVal, double maxVal);
/*! draw the fluid simulation */
int n_fluid_draw(N_FLUID* fluid);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif
