/**
 *@file n_fluids.c
 *@brief Fluid management functions , ported from https://www.youtube.com/watch?v=iKAVRgIrUOU&t=522s
 *@author Castagnier Mickael aka Gull Ra Driel
 *@version 1.0
 *@date 31/12/2022
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <strings.h>
#include "nilorea/n_fluids.h"
#include "nilorea/n_common.h"
#include "nilorea/n_thread_pool.h"

/*! array number for u */
#define N_FLUID_U_FIELD 0
/*! array number for v */
#define N_FLUID_V_FIELD 1
/*! array number for s */
#define N_FLUID_S_FIELD 2

/**
 *@brief memset bytes to a custom value
 *@param dst destination pointer
 *@param val value to write
 *@param size size of value
 *@param count how many val are we writting
 */
void n_memset(void* dst, void* val, size_t size, size_t count) {
    void* ptr = dst;
    while (count-- > 0) {
        memcpy(ptr, val, size);
        ptr += size;
    }
}

/**
 *@brief ddestroy a fluid structure
 *@param fluid pointer to the structure to destroy
 *@return TRUE or FALSE
 */
int destroy_n_fluid(N_FLUID** fluid) {
    __n_assert((*fluid), return FALSE);

    FreeNoLog((*fluid)->u);
    FreeNoLog((*fluid)->newU);
    FreeNoLog((*fluid)->v);
    FreeNoLog((*fluid)->newV);
    FreeNoLog((*fluid)->p);
    FreeNoLog((*fluid)->s);
    FreeNoLog((*fluid)->m);
    FreeNoLog((*fluid)->newM);
    FreeNoLog((*fluid));

    return TRUE;
}

/**
 * @brief return a newly allocated fluid
 * @param density density of the fluid
 * @param gravity gravity applied (Y axe)
 * @param numIters predefined number of iterations for each frames
 * @param dt preconfigured delta time
 * @param overRelaxation over predefined over relaxation value
 * @param sx number of cells on x axis
 * @param sy number of cells on y axis
 * @return a newly allocated N_FLUID structure
 */
N_FLUID* new_n_fluid(double density, double gravity, size_t numIters, double dt, double overRelaxation, size_t sx, size_t sy) {
    N_FLUID* fluid = NULL;

    Malloc(fluid, N_FLUID, 1);
    __n_assert(fluid, return NULL);

    fluid->density = density;
    fluid->gravity = gravity;
    fluid->numIters = numIters;
    fluid->dt = dt;
    fluid->h = 1.0 / 100.0;
    fluid->overRelaxation = overRelaxation;
    fluid->numX = sx + 2;
    fluid->numY = sy + 2;
    fluid->numZ = 1;  // TODO: expand Z
    fluid->numCells = fluid->numX * fluid->numY * fluid->numZ;

    Malloc(fluid->u, double, fluid->numCells);
    Malloc(fluid->newU, double, fluid->numCells);
    Malloc(fluid->v, double, fluid->numCells);
    Malloc(fluid->newV, double, fluid->numCells);
    Malloc(fluid->p, double, fluid->numCells);
    Malloc(fluid->s, double, fluid->numCells);
    Malloc(fluid->m, double, fluid->numCells);
    Malloc(fluid->newM, double, fluid->numCells);

    fluid->showSmoke = 1;
    fluid->showPressure = 0;
    fluid->showPaint = 0;
    fluid->fluid_production_percentage = 0.1;
    fluid->cScale = 16.0;
    // double precision. Taking 'value', if( fabs( value ) < float_tolerance )  value is considered as zero
    fluid->negative_float_tolerance = -0.00001;
    fluid->positive_float_tolerance = 0.00001;

    double d_val = 1.0;
    n_memset(fluid->m, &d_val, sizeof(d_val), fluid->numCells);

    // precalculate and allocated N_PROC_PARAMS lists for threaded computing
    fluid->integrate_chunk_list = new_generic_list(MAX_LIST_ITEMS);
    fluid->solveIncompressibility_chunk_list = new_generic_list(MAX_LIST_ITEMS);
    fluid->advectVel_chunk_list = new_generic_list(MAX_LIST_ITEMS);
    fluid->advectSmoke_chunk_list = new_generic_list(MAX_LIST_ITEMS);

    int nb_cores = get_nb_cpu_cores();
    if (nb_cores == -1) {
        nb_cores = 1;
    }
    size_t steps = fluid->numX / nb_cores;

    N_FLUID_THREAD_PARAMS* params = NULL;
    // integrate
    for (size_t i = 1; i < fluid->numX; i += steps) {
        Malloc(params, N_FLUID_THREAD_PARAMS, 1);
        params->ptr = fluid;
        params->x_start = i;
        params->x_end = i + steps;
        params->y_start = 1;
        params->y_end = fluid->numY - 1;
        list_push(fluid->integrate_chunk_list, params, &free);
    }
    // set the last batch at the end of the range
    params = (N_FLUID_THREAD_PARAMS*)fluid->integrate_chunk_list->end->ptr;
    params->x_end = fluid->numX;

    // solveIncompressibility
    for (size_t i = 1; i < fluid->numX - 1; i += steps) {
        Malloc(params, N_FLUID_THREAD_PARAMS, 1);
        params->ptr = fluid;
        params->x_start = i;
        params->x_end = i + steps;
        params->y_start = 1;
        params->y_end = fluid->numY - 1;
        list_push(fluid->solveIncompressibility_chunk_list, params, &free);
    }
    // set the last batch at the end of the range
    params = (N_FLUID_THREAD_PARAMS*)fluid->solveIncompressibility_chunk_list->end->ptr;
    params->x_end = fluid->numX - 1;

    // advectVel
    for (size_t i = 1; i < fluid->numX; i += steps) {
        Malloc(params, N_FLUID_THREAD_PARAMS, 1);
        params->ptr = fluid;
        params->x_start = i;
        params->x_end = i + steps;
        params->y_start = 1;
        params->y_end = fluid->numY;
        list_push(fluid->advectVel_chunk_list, params, &free);
    }
    // set the last batch at the end of the range
    params = (N_FLUID_THREAD_PARAMS*)fluid->advectVel_chunk_list->end->ptr;
    params->x_end = fluid->numX;

    // adVectSmoke
    for (size_t i = 1; i < fluid->numX - 1; i += steps) {
        Malloc(params, N_FLUID_THREAD_PARAMS, 1);
        params->ptr = fluid;
        params->x_start = i;
        params->x_end = i + steps;
        params->y_start = 1;
        params->y_end = fluid->numY - 1;
        list_push(fluid->advectSmoke_chunk_list, params, &free);
    }
    // set the last batch at the end of the range
    params = (N_FLUID_THREAD_PARAMS*)fluid->advectSmoke_chunk_list->end->ptr;
    params->x_end = fluid->numX - 1;

    return fluid;
} /* new_n_fluid */

/**
 *@brief ready to be threaded integration function
 *@param ptr a N_FLUID_THREAD_PARAMS ptr
 *@return NULL
 */
void* n_fluid_integrate_proc(void* ptr) {
    N_FLUID_THREAD_PARAMS* params = (N_FLUID_THREAD_PARAMS*)ptr;
    N_FLUID* fluid = (N_FLUID*)params->ptr;

    size_t n = fluid->numY;
    for (size_t i = params->x_start; i < params->x_end; i++) {
        for (size_t j = params->y_start; j < params->y_end; j++) {
            if (!_z(fluid, s[i * n + j]) && !_z(fluid, s[i * n + j - 1]))
                fluid->v[i * n + j] += fluid->gravity * fluid->dt;
        }
    }
    return NULL;
}

/**
 * @brief non threaded version of integration function
 * @param fluid a N_FLUID to integrate_chunk_list
 * @return TRUE or FALSE
 */
int n_fluid_integrate(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);

    size_t n = fluid->numY;
    for (size_t i = 1; i < fluid->numX; i++) {
        for (size_t j = 1; j < fluid->numY - 1; j++) {
            if (!_z(fluid, s[i * n + j]) && !_z(fluid, s[i * n + j - 1]))
                fluid->v[i * n + j] += fluid->gravity * fluid->dt;
        }
    }
    return TRUE;
}

/**
 *@brief ready to be threaded incompressibility solving function
 *@param ptr a N_FLUID_THREAD_PARAMS ptr
 *@return NULL
 */
void* n_fluid_solveIncompressibility_proc(void* ptr) {
    N_FLUID_THREAD_PARAMS* params = (N_FLUID_THREAD_PARAMS*)ptr;
    N_FLUID* fluid = (N_FLUID*)params->ptr;

    double cp = (fluid->density * fluid->h) / fluid->dt;

    size_t n = fluid->numY;
    for (size_t i = params->x_start; i < params->x_end; i++) {
        for (size_t j = params->y_start; j < params->y_end; j++) {
            if (_z(fluid, s[i * n + j]))
                continue;

            double sx0 = fluid->s[(i - 1) * n + j];
            double sx1 = fluid->s[(i + 1) * n + j];
            double sy0 = fluid->s[i * n + j - 1];
            double sy1 = fluid->s[i * n + j + 1];
            double s = sx0 + sx1 + sy0 + sy1;
            if (_zd(fluid, s))
                continue;

            double div = fluid->u[(i + 1) * n + j] - fluid->u[i * n + j] + fluid->v[i * n + j + 1] - fluid->v[i * n + j];
            double p = (-div * fluid->overRelaxation) / s;
            fluid->p[i * n + j] += cp * p;
            fluid->u[i * n + j] -= sx0 * p;
            fluid->u[(i + 1) * n + j] += sx1 * p;
            fluid->v[i * n + j] -= sy0 * p;
            fluid->v[i * n + j + 1] += sy1 * p;
        }
    }
    return NULL;
}

/**
 *@brief non threaded version of incompressibility solving function
 *@param fluid N_FLUID to solve
 *@return TRUE or FALSE
 */
int n_fluid_solveIncompressibility(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);
    size_t n = fluid->numY;

    double cp = (fluid->density * fluid->h) / fluid->dt;

    for (size_t iter = 0; iter < fluid->numIters; iter++) {
        for (size_t i = 1; i < fluid->numX - 1; i++) {
            for (size_t j = 1; j < fluid->numY - 1; j++) {
                if (_z(fluid, s[i * n + j]))
                    continue;

                double sx0 = fluid->s[(i - 1) * n + j];
                double sx1 = fluid->s[(i + 1) * n + j];
                double sy0 = fluid->s[i * n + j - 1];
                double sy1 = fluid->s[i * n + j + 1];
                double s = sx0 + sx1 + sy0 + sy1;
                if (_zd(fluid, s))
                    continue;

                double div = fluid->u[(i + 1) * n + j] - fluid->u[i * n + j] + fluid->v[i * n + j + 1] - fluid->v[i * n + j];
                double p = (-div * fluid->overRelaxation) / s;
                fluid->p[i * n + j] += cp * p;
                fluid->u[i * n + j] -= sx0 * p;
                fluid->u[(i + 1) * n + j] += sx1 * p;
                fluid->v[i * n + j] -= sy0 * p;
                fluid->v[i * n + j + 1] += sy1 * p;
            }
        }
    }
    return TRUE;
}

/**
 *@brief non threaded extrapolation function
 *@param fluid a N_FLUID to extrapolate
 *@return TRUE or FALSE
 */
int n_fluid_extrapolate(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);
    size_t n = fluid->numY;
    for (size_t i = 0; i < fluid->numX; i++) {
        fluid->u[i * n + 0] = fluid->u[i * n + 1];
        fluid->u[i * n + fluid->numY - 1] = fluid->u[i * n + fluid->numY - 2];
    }
    for (size_t j = 0; j < fluid->numY; j++) {
        fluid->v[0 * n + j] = fluid->v[1 * n + j];
        fluid->v[(fluid->numX - 1) * n + j] = fluid->v[(fluid->numX - 2) * n + j];
    }
    return TRUE;
}

/**
 * @brief compute a sample value at a field position
 * @param fluid target N_FLUID
 * @param x X position in the fluid grid
 * @param y Y position in the fluid grid
 * @param field type of field wanted, one of N_FLUID_U_FIELD, N_FLUID_V_FIELD, N_FLUID_S_FIELD
 * @return the computed value
 */
double n_fluid_sampleField(N_FLUID* fluid, double x, double y, uint32_t field) {
    __n_assert(fluid, return FALSE);
    size_t n = fluid->numY;
    double h1 = 1.0 / fluid->h;
    double h2 = 0.5 * fluid->h;

    x = MAX(MIN(x, fluid->numX * fluid->h), fluid->h);
    y = MAX(MIN(y, fluid->numY * fluid->h), fluid->h);

    double dx = 0.0;
    double dy = 0.0;

    double* f = NULL;
    switch (field) {
        case N_FLUID_U_FIELD:
            f = fluid->u;
            dy = h2;
            break;
        case N_FLUID_V_FIELD:
            f = fluid->v;
            dx = h2;
            break;
        case N_FLUID_S_FIELD:
            f = fluid->m;
            dx = h2;
            dy = h2;
            break;
    }

    double x0 = MIN(floor((x - dx) * h1), fluid->numX - 1);
    double tx = ((x - dx) - x0 * fluid->h) * h1;
    double x1 = MIN(x0 + 1, fluid->numX - 1);

    double y0 = MIN(floor((y - dy) * h1), fluid->numY - 1);
    double ty = ((y - dy) - y0 * fluid->h) * h1;
    double y1 = MIN(y0 + 1, fluid->numY - 1);

    double sx = 1.0 - tx;
    double sy = 1.0 - ty;

    double val = sx * sy * f[(size_t)(x0 * n + y0)] +
                 tx * sy * f[(size_t)(x1 * n + y0)] +
                 tx * ty * f[(size_t)(x1 * n + y1)] +
                 sx * ty * f[(size_t)(x0 * n + y1)];

    return val;
}

/**
 * @brief compute the average U value at a fluid position using it's surrounding
 * @param fluid targeted N_FLUID
 * @param i X position in the fluid grid
 * @param j Y position in the fluid grid
 * @return the average computed value
 */
double n_fluid_avgU(N_FLUID* fluid, size_t i, size_t j) {
    __n_assert(fluid, return FALSE);
    size_t n = fluid->numY;
    double u = (fluid->u[i * n + j - 1] + fluid->u[i * n + j] +
                fluid->u[(i + 1) * n + j - 1] + fluid->u[(i + 1) * n + j]) *
               0.25;

    return u;
}

/**
 * @brief compute the average V value at a fluid position using it's surrounding
 * @param fluid targeted N_FLUID
 * @param i X position in the fluid grid
 * @param j Y position in the fluid grid
 * @return the average computed value
 */
double n_fluid_avgV(N_FLUID* fluid, size_t i, size_t j) {
    __n_assert(fluid, return FALSE);
    size_t n = fluid->numY;
    double v = (fluid->v[(i - 1) * n + j] + fluid->v[i * n + j] +
                fluid->v[(i - 1) * n + j + 1] + fluid->v[i * n + j + 1]) *
               0.25;
    return v;
}

/**
 * @brief ready to be threaded add velocities function
 * @param ptr a N_FLUID_THREAD_PARAMS ptr
 * @return NULL
 */
void* n_fluid_advectVel_proc(void* ptr) {
    N_FLUID_THREAD_PARAMS* params = (N_FLUID_THREAD_PARAMS*)ptr;
    N_FLUID* fluid = (N_FLUID*)params->ptr;

    size_t n = fluid->numY;
    double h2 = 0.5 * fluid->h;
    for (size_t i = params->x_start; i < params->x_end; i++) {
        for (size_t j = params->y_start; j < params->y_end; j++) {
            size_t index = i * n + j;
            // u component
            if (!_z(fluid, s[index]) && !_z(fluid, s[(i - 1) * n + j]) && j < fluid->numY - 1) {
                double x = i * fluid->h;
                double y = j * fluid->h + h2;
                double u = fluid->u[index];
                double v = n_fluid_avgV(fluid, i, j);
                // double v = n_fluid_sampleField( fluid , x , y , N_FLUID_V_FIELD );
                x = x - fluid->dt * u;
                y = y - fluid->dt * v;
                u = n_fluid_sampleField(fluid, x, y, N_FLUID_U_FIELD);
                fluid->newU[index] = u;
            }
            // v component
            if (!_z(fluid, s[index]) && !_z(fluid, s[index - 1]) && i < fluid->numX - 1) {
                double x = i * fluid->h + h2;
                double y = j * fluid->h;
                double u = n_fluid_avgU(fluid, i, j);
                // double u = n_fluid_sampleField( fluid , x , y , N_FLUID_U_FIELD );
                double v = fluid->v[index];
                x = x - fluid->dt * u;
                y = y - fluid->dt * v;
                v = n_fluid_sampleField(fluid, x, y, N_FLUID_V_FIELD);
                fluid->newV[index] = v;
            }
        }
    }
    return NULL;
}

/**
 * @brief non threaded version of add velocities function
 * @param fluid a N_FLUID ptr
 * @return TRUE or FALSE
 */
int n_fluid_advectVel(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);

    memcpy(fluid->newU, fluid->u, fluid->numCells * sizeof(double));
    memcpy(fluid->newV, fluid->v, fluid->numCells * sizeof(double));

    size_t n = fluid->numY;
    double h2 = 0.5 * fluid->h;
    for (size_t i = 1; i < fluid->numX; i++) {
        for (size_t j = 1; j < fluid->numY; j++) {
            size_t index = i * n + j;
            // u component
            if (!_z(fluid, s[index]) && !_z(fluid, s[(i - 1) * n + j]) && j < fluid->numY - 1) {
                double x = i * fluid->h;
                double y = j * fluid->h + h2;
                double u = fluid->u[index];
                double v = n_fluid_avgV(fluid, i, j);
                // double v = n_fluid_sampleField( fluid , x , y , N_FLUID_V_FIELD );
                x = x - fluid->dt * u;
                y = y - fluid->dt * v;
                u = n_fluid_sampleField(fluid, x, y, N_FLUID_U_FIELD);
                fluid->newU[index] = u;
            }
            // v component
            if (!_z(fluid, s[index]) && !_z(fluid, s[index - 1]) && i < fluid->numX - 1) {
                double x = i * fluid->h + h2;
                double y = j * fluid->h;
                double u = n_fluid_avgU(fluid, i, j);
                // double u = n_fluid_sampleField( fluid , x , y , N_FLUID_U_FIELD );
                double v = fluid->v[index];
                x = x - fluid->dt * u;
                y = y - fluid->dt * v;
                v = n_fluid_sampleField(fluid, x, y, N_FLUID_V_FIELD);
                fluid->newV[index] = v;
            }
        }
    }
    double* ptr = fluid->u;
    fluid->u = fluid->newU;
    fluid->newU = ptr;

    ptr = fluid->v;
    fluid->v = fluid->newV;
    fluid->newV = ptr;

    return TRUE;
}

/**
 * @brief ready to be threaded add smoke function
 * @param ptr a N_FLUID_THREAD_PARAMS ptr
 * @return NULL
 */
void* n_fluid_advectSmoke_proc(void* ptr) {
    N_FLUID_THREAD_PARAMS* params = (N_FLUID_THREAD_PARAMS*)ptr;
    N_FLUID* fluid = (N_FLUID*)params->ptr;

    size_t n = fluid->numY;
    double h2 = 0.5 * fluid->h;
    for (size_t i = params->x_start; i < params->x_end; i++) {
        for (size_t j = params->y_start; j < params->y_end; j++) {
            size_t index = i * n + j;
            if (!_z(fluid, s[index])) {
                double u = (fluid->u[index] + fluid->u[(i + 1) * n + j]) * 0.5;
                double v = (fluid->v[index] + fluid->v[index + 1]) * 0.5;
                double x = i * fluid->h + h2 - fluid->dt * u;
                double y = j * fluid->h + h2 - fluid->dt * v;

                fluid->newM[index] = n_fluid_sampleField(fluid, x, y, N_FLUID_S_FIELD);
            }
        }
    }
    return NULL;
}

/**
 * @brief non threaded version of add smoke function
 * @param fluid a N_FLUID ptr
 * @return TRUE or FALSE
 */
int n_fluid_advectSmoke(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);

    size_t n = fluid->numY;
    double h2 = 0.5 * fluid->h;

    memcpy(fluid->newM, fluid->m, fluid->numCells * sizeof(double));

    for (size_t i = 1; i < fluid->numX - 1; i++) {
        for (size_t j = 1; j < fluid->numY - 1; j++) {
            size_t index = i * n + j;
            if (!_z(fluid, s[index])) {
                double u = (fluid->u[index] + fluid->u[(i + 1) * n + j]) * 0.5;
                double v = (fluid->v[index] + fluid->v[index + 1]) * 0.5;
                double x = i * fluid->h + h2 - fluid->dt * u;
                double y = j * fluid->h + h2 - fluid->dt * v;

                fluid->newM[index] = n_fluid_sampleField(fluid, x, y, N_FLUID_S_FIELD);
            }
        }
    }
    double* ptr = fluid->m;
    fluid->m = fluid->newM;
    fluid->newM = ptr;

    return TRUE;
}

/**
 * @brief non threaded version of N_FLUID global processing function
 * @param fluid a N_FLUID ptr
 * @return TRUE or FALSE
 */
int n_fluid_simulate(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);
    n_fluid_integrate(fluid);
    memset(fluid->p, 0, fluid->numCells * sizeof(double));
    n_fluid_solveIncompressibility(fluid);
    n_fluid_extrapolate(fluid);
    n_fluid_advectVel(fluid);
    n_fluid_advectSmoke(fluid);
    return TRUE;
}

/**
 * @brief a threaded version of N_FLUID global processing function
 * @param fluid a N_FLUID ptr
 * @param thread_pool a THREAD_POOL pool to use
 * @return TRUE or FALSE
 */
int n_fluid_simulate_threaded(N_FLUID* fluid, THREAD_POOL* thread_pool) {
    __n_assert(fluid, return FALSE);

    // n_fluid_integrate( fluid );
    list_foreach(node, fluid->integrate_chunk_list) {
        add_threaded_process(thread_pool, &n_fluid_integrate_proc, (void*)node->ptr, SYNCED_PROC);
    }
    start_threaded_pool(thread_pool);
    wait_for_synced_threaded_pool(thread_pool);
    refresh_thread_pool(thread_pool);

    // set pressure to 0
    memset(fluid->p, 0, fluid->numCells * sizeof(double));

    // n_fluid_solveIncompressibility( fluid );
    for (size_t iter = 0; iter < fluid->numIters; iter++) {
        list_foreach(node, fluid->solveIncompressibility_chunk_list) {
            add_threaded_process(thread_pool, &n_fluid_solveIncompressibility_proc, (void*)node->ptr, SYNCED_PROC);
        }
        start_threaded_pool(thread_pool);
        wait_for_synced_threaded_pool(thread_pool);
        refresh_thread_pool(thread_pool);
    }

    // extrapolate
    n_fluid_extrapolate(fluid);

    // n_fluid_advectVel( fluid );
    memcpy(fluid->newU, fluid->u, fluid->numCells * sizeof(double));
    memcpy(fluid->newV, fluid->v, fluid->numCells * sizeof(double));

    list_foreach(node, fluid->advectVel_chunk_list) {
        add_threaded_process(thread_pool, &n_fluid_advectVel_proc, (void*)node->ptr, SYNCED_PROC);
    }
    start_threaded_pool(thread_pool);
    wait_for_synced_threaded_pool(thread_pool);
    refresh_thread_pool(thread_pool);

    double* ptr = fluid->u;
    fluid->u = fluid->newU;
    fluid->newU = ptr;

    ptr = fluid->v;
    fluid->v = fluid->newV;
    fluid->newV = ptr;

    // n_fluid_advectSmoke( fluid );
    memcpy(fluid->newM, fluid->m, fluid->numCells * sizeof(double));
    list_foreach(node, fluid->advectSmoke_chunk_list) {
        add_threaded_process(thread_pool, &n_fluid_advectSmoke_proc, (void*)node->ptr, SYNCED_PROC);
    }
    start_threaded_pool(thread_pool);
    wait_for_synced_threaded_pool(thread_pool);
    refresh_thread_pool(thread_pool);

    ptr = fluid->m;
    fluid->m = fluid->newM;
    fluid->newM = ptr;

    return TRUE;
}

/**
 *@brief set an obstacle in the fluid grid
 *@param fluid targeted N_FLUID
 *@param x X position in fluid grid
 *@param y y position in fluid grid
 *@param vx X velocity for the point
 *@param vy Y velocity for the point
 *@param r radius
 *@return TRUE or FALSE
 */
int n_fluid_setObstacle(N_FLUID* fluid, double x, double y, double vx, double vy, double r) {
    __n_assert(fluid, return FALSE);

    size_t n = fluid->numY;
    for (size_t i = 1; i < fluid->numX - 2; i++) {
        for (size_t j = 1; j < fluid->numY - 2; j++) {
            double dx = (i + 0.5) - x;
            double dy = (j + 0.5) - y;

            if (i > 7 && (dx * dx + dy * dy < r * r)) {
                fluid->s[i * n + j] = 0.0;
                fluid->m[i * n + j] = 1.0;
                fluid->u[i * n + j] = vx;
                fluid->u[(i + 1) * n + j] = vx;
                fluid->v[i * n + j] = vy;
                fluid->v[i * n + j + 1] = vy;
            }
        }
    }
    return TRUE;
}

/**
 * @brief reset the obstacles set in a N_FLUID
 * @param fluid the targeted N_FLUID
 * @return TRUE or FALSE
 */
int n_fluid_resetObstacles(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);

    size_t n = fluid->numY;
    for (size_t i = 1; i < fluid->numX - 2; i++) {
        for (size_t j = 1; j < fluid->numY - 2; j++) {
            fluid->s[i * n + j] = 1.0;
        }
    }

    return TRUE;
}

/**
 *@brief set an obstacle in the fluid grid from a bitmap mask
 *@param fluid targeted N_FLUID
 *@param bitmap the bitmap source mask
 *@param x X position in fluid grid
 *@param y y position in fluid grid
 *@param vx X velocity for the point
 *@param vy Y velocity for the point
 *@param r radius
 *@return TRUE or FALSE
 */
int n_fluid_setObstacleFromBitmap(N_FLUID* fluid, ALLEGRO_BITMAP* bitmap, double x, double y, double vx, double vy, double r) {
    __n_assert(fluid, return FALSE);

    al_lock_bitmap(bitmap, al_get_bitmap_format(bitmap), ALLEGRO_LOCK_READONLY);

    size_t n = fluid->numY;
    for (size_t i = 1; i < fluid->numX - 2; i++) {
        for (size_t j = 1; j < fluid->numY - 2; j++) {
            double dx = (i + 0.5) - x;
            double dy = (j + 0.5) - y;

            if (i > 7 && (dx * dx + dy * dy < r * r)) {
                fluid->s[i * n + j] = 0.0;
                fluid->m[i * n + j] = 1.0;
                fluid->u[i * n + j] = vx;
                fluid->v[i * n + j] = vy;

                fluid->u[(i + 1) * n + j] = vx;
                fluid->v[i * n + j + 1] = vy;
            }
        }
    }

    al_unlock_bitmap(bitmap);

    return TRUE;
}

/**
 * @brief get fonky colors for the fluid
 * @param fluid targeted N_FLUID
 * @param val value mixer
 * @param minVal minimum output value
 * @param maxVal maximum output value
 * @return the generated ALLEGRO_COLOR
 */
ALLEGRO_COLOR n_fluid_getSciColor(N_FLUID* fluid, double val, double minVal, double maxVal) {
    val = MIN(MAX(val, minVal), maxVal - 0.0001);
    double d = maxVal - minVal;
    if (_zd(fluid, d)) {
        val = 0.5;
    } else {
        val = (val - minVal) / d;
    }
    double m = 0.25;
    size_t num = floor(val / m);
    double s = (val - num * m) / m;
    double r = 0.0, g = 0.0, b = 0.0;
    switch (num) {
        case 0:
            r = 0.0;
            g = s;
            b = 1.0;
            break;
        case 1:
            r = 0.0;
            g = 1.0;
            b = 1.0 - s;
            break;
        case 2:
            r = s;
            g = 1.0;
            b = 0.0;
            break;
        case 3:
            r = 1.0;
            g = 1.0 - s;
            b = 0.0;
            break;
    }
    // return[255*r,255*g,255*b, 255]
    return al_map_rgb_f(r, g, b);
}

/**
 * @brief draw a N_FLUID on screen / targert bitmap
 * @param fluid the N_FLUID to draw
 * @return TRUE or FALSE
 */
int n_fluid_draw(N_FLUID* fluid) {
    __n_assert(fluid, return FALSE);

    size_t n = fluid->numY;

    double minP = fluid->p[0];
    double maxP = fluid->p[0];

    if (fluid->showPressure) {
        for (size_t i = 0; i < fluid->numCells; i++) {
            minP = MIN(minP, fluid->p[i]);
            maxP = MAX(maxP, fluid->p[i]);
        }
    }

    ALLEGRO_COLOR color;
    double cScale = fluid->cScale;

    for (size_t i = 0; i < fluid->numX; i++) {
        for (size_t j = 0; j < fluid->numY; j++) {
            int64_t x = i * cScale;
            int64_t y = j * cScale;
            int64_t cx = x + cScale;
            int64_t cy = y + cScale;

            double s = fluid->m[i * n + j];

            if (fluid->showPaint) {
                color = n_fluid_getSciColor(fluid, s, 0.0, 1.0);
            } else if (fluid->showPressure) {
                float color_vec_f[3] = {0.0, 0.0, 0.0};
                double p = fluid->p[i * n + j];
                color = n_fluid_getSciColor(fluid, p, minP, maxP);
                if (fluid->showSmoke) {
                    al_unmap_rgb_f(color, color_vec_f, color_vec_f + 1, color_vec_f + 2);
                    color = al_map_rgb_f(MAX(0.0, color_vec_f[0] - s), MAX(0.0, color_vec_f[1] - s), MAX(0.0, color_vec_f[2] - s));
                }
            } else if (fluid->showSmoke) {
                color = al_map_rgb_f(1.0 - s, 0.0, 0.0);
            } else {
                color = al_map_rgb_f(s, s, s);
            }
            al_draw_filled_rectangle(x, y, cx, cy, color);
        }
    }
    return TRUE;
}
