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
 *@file n_astar.h
 *@brief A* Pathfinding API for 2D and 3D grids
 *
 * Implements the A* search algorithm for grid-based pathfinding,
 * supporting both 2D grids (with 4 or 8 directional movement) and
 * 3D grids (with 6 or 26 directional movement).
 *
 * Designed to integrate with the isometric engine (n_iso_engine):
 * populate an ASTAR_GRID from the MAP cell walkability data, find
 * a path, and optionally feed the resulting waypoints into a
 * TRAJECTORY for smooth movement.
 *
 * Based on:
 * - GameDev.net Article 2003: "A* Pathfinding for Beginners"
 * - Red Blob Games: "Implementation of A*"
 *
 * Usage (2D):
 *@code
 *   ASTAR_GRID *grid = n_astar_grid_new(width, height, 1);
 *   n_astar_grid_set_walkable(grid, 5, 3, 0, 0);  // block cell (5,3)
 *   ASTAR_PATH *path = n_astar_find_path(grid, 0,0,0, 9,9,0,
 *                                         ASTAR_ALLOW_DIAGONAL,
 *                                         ASTAR_HEURISTIC_CHEBYSHEV);
 *   if (path) {
 *       for (int i = 0; i < path->length; i++)
 *           printf("(%d,%d)\n", path->nodes[i].x, path->nodes[i].y);
 *       n_astar_path_free(path);
 *   }
 *   n_astar_grid_free(grid);
 *@endcode
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 01/03/2026
 */
#ifndef N_ASTAR_H
#define N_ASTAR_H

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup ASTAR ASTAR: A* pathfinding for 2D/3D grids
  @addtogroup ASTAR
  @{
  */

#include <stdlib.h>
#include <stdint.h>

/*---------------------------------------------------------------------------
 * Constants
 *-------------------------------------------------------------------------*/

/*! Movement mode: 4-dir (2D) or 6-dir (3D) */
#define ASTAR_CARDINAL_ONLY 0
/*! Movement mode: 8-dir (2D) or 26-dir (3D) */
#define ASTAR_ALLOW_DIAGONAL 1

/*! Default cost for straight movement (fixed-point x1000) */
#define ASTAR_COST_CARDINAL 1000
/*! Default cost for 2D diagonal movement (sqrt(2)*1000) */
#define ASTAR_COST_DIAGONAL 1414
/*! Default cost for 3D diagonal movement (sqrt(3)*1000) */
#define ASTAR_COST_DIAGONAL3D 1732

/*! Node has not been visited */
#define ASTAR_NODE_NONE 0
/*! Node is in the open list */
#define ASTAR_NODE_OPEN 1
/*! Node has been fully evaluated */
#define ASTAR_NODE_CLOSED 2

/*---------------------------------------------------------------------------
 * Heuristic Types
 *-------------------------------------------------------------------------*/

/*! Heuristic function selection for h(n) estimation */
typedef enum {
    ASTAR_HEURISTIC_MANHATTAN = 0, /*!< sum of axis deltas (optimal for 4-dir) */
    ASTAR_HEURISTIC_EUCLIDEAN,     /*!< straight-line distance */
    ASTAR_HEURISTIC_CHEBYSHEV      /*!< max of axis deltas (optimal for 8-dir) */
} ASTAR_HEURISTIC;

/*---------------------------------------------------------------------------
 * Data Structures
 *-------------------------------------------------------------------------*/

/*! A single node in the resulting path */
typedef struct ASTAR_NODE {
    int x; /*!< grid X coordinate */
    int y; /*!< grid Y coordinate */
    int z; /*!< grid Z coordinate (0 for 2D) */
} ASTAR_NODE;

/*! The computed path result */
typedef struct ASTAR_PATH {
    ASTAR_NODE* nodes; /*!< array of path nodes from start to goal */
    int length;        /*!< number of nodes in the path */
    int cost;          /*!< total path cost (x1000 fixed-point) */
} ASTAR_PATH;

/*! Internal node data used during pathfinding */
typedef struct ASTAR_CELL {
    int g;          /*!< cost from start to this cell */
    int h;          /*!< heuristic estimate to goal */
    int f;          /*!< g + h */
    int parent_x;   /*!< parent cell X (-1 if none) */
    int parent_y;   /*!< parent cell Y */
    int parent_z;   /*!< parent cell Z */
    uint8_t status; /*!< ASTAR_NODE_NONE / OPEN / CLOSED */
} ASTAR_CELL;

/*! Min-heap entry for the open list priority queue */
typedef struct ASTAR_HEAP_NODE {
    int x, y, z; /*!< grid coordinates */
    int f;       /*!< priority (f = g + h) */
} ASTAR_HEAP_NODE;

/*! Binary min-heap (priority queue) for the open list */
typedef struct ASTAR_HEAP {
    ASTAR_HEAP_NODE* data; /*!< heap array */
    int size;              /*!< current number of elements */
    int capacity;          /*!< allocated capacity */
} ASTAR_HEAP;

/*! Grid structure holding walkability, costs, and dimensions */
typedef struct ASTAR_GRID {
    int width;         /*!< grid width (X axis) */
    int height;        /*!< grid height (Y axis) */
    int depth;         /*!< grid depth (Z axis, 1 for 2D) */
    uint8_t* walkable; /*!< walkability map: 1=passable, 0=blocked */
    int* cost;         /*!< per-cell movement cost multiplier (x1000) */
} ASTAR_GRID;

/*---------------------------------------------------------------------------
 * API Functions
 *-------------------------------------------------------------------------*/

/*! create a new grid for pathfinding, all cells default to walkable */
ASTAR_GRID* n_astar_grid_new(int width, int height, int depth);
/*! free a grid and all its internal data */
void n_astar_grid_free(ASTAR_GRID* grid);
/*! set a cell's walkability (1=passable, 0=blocked) */
void n_astar_grid_set_walkable(ASTAR_GRID* grid, int x, int y, int z, uint8_t walkable);
/*! get a cell's walkability (1=passable, 0=blocked, 0 if out of bounds) */
uint8_t n_astar_grid_get_walkable(const ASTAR_GRID* grid, int x, int y, int z);
/*! set a cell's movement cost multiplier (x1000) */
void n_astar_grid_set_cost(ASTAR_GRID* grid, int x, int y, int z, int cost);
/*! get a cell's movement cost multiplier (x1000) */
int n_astar_grid_get_cost(const ASTAR_GRID* grid, int x, int y, int z);
/*! set a rectangular region as blocked */
void n_astar_grid_set_rect_blocked(ASTAR_GRID* grid,
                                   int x1,
                                   int y1,
                                   int z1,
                                   int x2,
                                   int y2,
                                   int z2);
/*! find a path using A* search, returns NULL if no path exists */
ASTAR_PATH* n_astar_find_path(const ASTAR_GRID* grid,
                              int sx,
                              int sy,
                              int sz,
                              int gx,
                              int gy,
                              int gz,
                              int diagonal,
                              ASTAR_HEURISTIC heuristic);
/*! free a path returned by n_astar_find_path */
void n_astar_path_free(ASTAR_PATH* path);
/*! compute heuristic distance between two points (x1000) */
int n_astar_heuristic(int x1, int y1, int z1, int x2, int y2, int z2, ASTAR_HEURISTIC heuristic);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* N_ASTAR_H */
