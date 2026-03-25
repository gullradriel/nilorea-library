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
 *@file n_astar.c
 *@brief A* Pathfinding implementation for 2D and 3D grids
 *
 * Uses a binary min-heap as the open list priority queue for O(log n)
 * insert/extract operations, and a flat array for O(1) closed-set
 * membership checks.
 *
 * Based on:
 * - GameDev.net Article 2003: "A* Pathfinding for Beginners"
 * - Red Blob Games: "Implementation of A*"
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 01/03/2026
 */

#include "nilorea/n_astar.h"
#include <string.h>
#include <math.h>

/*---------------------------------------------------------------------------
 * Internal helpers
 *-------------------------------------------------------------------------*/

/*! Convert 3D coordinates to flat array index */
static inline int grid_index(const ASTAR_GRID* grid, int x, int y, int z) {
    return (z * grid->height + y) * grid->width + x;
}

/*! Check if coordinates are within grid bounds */
static inline int in_bounds(const ASTAR_GRID* grid, int x, int y, int z) {
    return x >= 0 && x < grid->width &&
           y >= 0 && y < grid->height &&
           z >= 0 && z < grid->depth;
}

/*! Absolute value for integers */
static inline int iabs(int v) {
    return v < 0 ? -v : v;
}

/*---------------------------------------------------------------------------
 * Binary Min-Heap (priority queue)
 *-------------------------------------------------------------------------*/

static ASTAR_HEAP* heap_new(int capacity) {
    ASTAR_HEAP* h = (ASTAR_HEAP*)calloc(1, sizeof(ASTAR_HEAP));
    if (!h) return NULL;

    h->data = (ASTAR_HEAP_NODE*)malloc((size_t)capacity * sizeof(ASTAR_HEAP_NODE));
    if (!h->data) {
        free(h);
        return NULL;
    }
    h->size = 0;
    h->capacity = capacity;
    return h;
}

static void heap_free(ASTAR_HEAP* h) {
    if (!h) return;
    free(h->data);
    free(h);
}

static int heap_grow(ASTAR_HEAP* h) {
    int new_cap = h->capacity * 2;
    ASTAR_HEAP_NODE* new_data = (ASTAR_HEAP_NODE*)realloc(
        h->data, (size_t)new_cap * sizeof(ASTAR_HEAP_NODE));
    if (!new_data) return 0;
    h->data = new_data;
    h->capacity = new_cap;
    return 1;
}

static void heap_swap(ASTAR_HEAP_NODE* a, ASTAR_HEAP_NODE* b) {
    ASTAR_HEAP_NODE tmp = *a;
    *a = *b;
    *b = tmp;
}

static void heap_push(ASTAR_HEAP* h, int x, int y, int z, int f) {
    if (h->size >= h->capacity) {
        if (!heap_grow(h)) return;
    }

    int i = h->size++;
    h->data[i].x = x;
    h->data[i].y = y;
    h->data[i].z = z;
    h->data[i].f = f;

    /* Sift up */
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->data[i].f < h->data[parent].f) {
            heap_swap(&h->data[i], &h->data[parent]);
            i = parent;
        } else {
            break;
        }
    }
}

static ASTAR_HEAP_NODE heap_pop(ASTAR_HEAP* h) {
    ASTAR_HEAP_NODE top = h->data[0];
    h->data[0] = h->data[--h->size];

    /* Sift down */
    int i = 0;
    for (;;) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;

        if (left < h->size && h->data[left].f < h->data[smallest].f)
            smallest = left;
        if (right < h->size && h->data[right].f < h->data[smallest].f)
            smallest = right;

        if (smallest != i) {
            heap_swap(&h->data[i], &h->data[smallest]);
            i = smallest;
        } else {
            break;
        }
    }

    return top;
}

/*---------------------------------------------------------------------------
 * Heuristic Functions
 *-------------------------------------------------------------------------*/

/**
 *@brief Compute heuristic distance between two 3D points
 *@param x1 first point X
 *@param y1 first point Y
 *@param z1 first point Z
 *@param x2 second point X
 *@param y2 second point Y
 *@param z2 second point Z
 *@param heuristic the heuristic to use
 *@return estimated distance in fixed-point (x1000)
 */
int n_astar_heuristic(int x1, int y1, int z1, int x2, int y2, int z2, ASTAR_HEURISTIC heuristic) {
    int dx = iabs(x2 - x1);
    int dy = iabs(y2 - y1);
    int dz = iabs(z2 - z1);

    switch (heuristic) {
        case ASTAR_HEURISTIC_MANHATTAN:
            return (dx + dy + dz) * ASTAR_COST_CARDINAL;

        case ASTAR_HEURISTIC_EUCLIDEAN: {
            double dist = sqrt((double)(dx * dx + dy * dy + dz * dz));
            return (int)(dist * ASTAR_COST_CARDINAL);
        }

        case ASTAR_HEURISTIC_CHEBYSHEV:
        default: {
            int vals[3] = {dx, dy, dz};
            /* Sort ascending */
            if (vals[0] > vals[1]) {
                int t = vals[0];
                vals[0] = vals[1];
                vals[1] = t;
            }
            if (vals[1] > vals[2]) {
                int t = vals[1];
                vals[1] = vals[2];
                vals[2] = t;
            }
            if (vals[0] > vals[1]) {
                int t = vals[0];
                vals[0] = vals[1];
                vals[1] = t;
            }

            int cost = vals[2] * ASTAR_COST_CARDINAL;
            cost += vals[0] * (ASTAR_COST_DIAGONAL3D - ASTAR_COST_CARDINAL);
            cost += (vals[1] - vals[0]) * (ASTAR_COST_DIAGONAL - ASTAR_COST_CARDINAL);
            return cost;
        }
    }
}

/*---------------------------------------------------------------------------
 * Grid Management
 *-------------------------------------------------------------------------*/

/**
 *@brief Create a new grid for A* pathfinding
 *@param width grid width (X)
 *@param height grid height (Y)
 *@param depth grid depth (Z, use 1 for 2D)
 *@return new grid or NULL. All cells default to walkable with cost 1000.
 */
ASTAR_GRID* n_astar_grid_new(int width, int height, int depth) {
    if (width <= 0 || height <= 0 || depth <= 0) return NULL;

    ASTAR_GRID* grid = (ASTAR_GRID*)calloc(1, sizeof(ASTAR_GRID));
    if (!grid) return NULL;

    grid->width = width;
    grid->height = height;
    grid->depth = depth;

    size_t total = (size_t)width * (size_t)height * (size_t)depth;

    grid->walkable = (uint8_t*)malloc(total);
    if (!grid->walkable) {
        free(grid);
        return NULL;
    }
    memset(grid->walkable, 1, total);

    grid->cost = (int*)malloc(total * sizeof(int));
    if (!grid->cost) {
        free(grid->walkable);
        free(grid);
        return NULL;
    }
    for (size_t i = 0; i < total; i++) {
        grid->cost[i] = ASTAR_COST_CARDINAL;
    }

    return grid;
}

/**
 *@brief Free a grid and all its internal data
 *@param grid grid to free (safe to pass NULL)
 */
void n_astar_grid_free(ASTAR_GRID* grid) {
    if (!grid) return;
    free(grid->walkable);
    free(grid->cost);
    free(grid);
}

/**
 *@brief Set a cell's walkability
 *@param grid the grid
 *@param x cell X coordinate
 *@param y cell Y coordinate
 *@param z cell Z coordinate
 *@param walkable 1=passable, 0=blocked
 */
void n_astar_grid_set_walkable(ASTAR_GRID* grid, int x, int y, int z, uint8_t walkable) {
    if (!grid || !in_bounds(grid, x, y, z)) return;
    grid->walkable[grid_index(grid, x, y, z)] = walkable;
}

/**
 *@brief Get a cell's walkability
 *@param grid the grid
 *@param x cell X coordinate
 *@param y cell Y coordinate
 *@param z cell Z coordinate
 *@return 1 if passable, 0 if blocked or out of bounds
 */
uint8_t n_astar_grid_get_walkable(const ASTAR_GRID* grid, int x, int y, int z) {
    if (!grid || !in_bounds(grid, x, y, z)) return 0;
    return grid->walkable[grid_index(grid, x, y, z)];
}

/**
 *@brief Set a cell's movement cost multiplier
 *@param grid the grid
 *@param x cell X coordinate
 *@param y cell Y coordinate
 *@param z cell Z coordinate
 *@param cost cost multiplier (x1000)
 */
void n_astar_grid_set_cost(ASTAR_GRID* grid, int x, int y, int z, int cost) {
    if (!grid || !in_bounds(grid, x, y, z)) return;
    grid->cost[grid_index(grid, x, y, z)] = cost;
}

/**
 *@brief Get a cell's movement cost multiplier
 *@param grid the grid
 *@param x cell X coordinate
 *@param y cell Y coordinate
 *@param z cell Z coordinate
 *@return cost multiplier (x1000), or 0 if out of bounds
 */
int n_astar_grid_get_cost(const ASTAR_GRID* grid, int x, int y, int z) {
    if (!grid || !in_bounds(grid, x, y, z)) return 0;
    return grid->cost[grid_index(grid, x, y, z)];
}

/**
 *@brief Set a rectangular region as blocked (wall)
 *@param grid the grid
 *@param x1 start corner X (inclusive)
 *@param y1 start corner Y (inclusive)
 *@param z1 start corner Z (inclusive)
 *@param x2 end corner X (inclusive)
 *@param y2 end corner Y (inclusive)
 *@param z2 end corner Z (inclusive)
 */
void n_astar_grid_set_rect_blocked(ASTAR_GRID* grid,
                                   int x1,
                                   int y1,
                                   int z1,
                                   int x2,
                                   int y2,
                                   int z2) {
    if (!grid) return;
    if (x1 > x2) {
        int t = x1;
        x1 = x2;
        x2 = t;
    }
    if (y1 > y2) {
        int t = y1;
        y1 = y2;
        y2 = t;
    }
    if (z1 > z2) {
        int t = z1;
        z1 = z2;
        z2 = t;
    }

    for (int z = z1; z <= z2; z++)
        for (int y = y1; y <= y2; y++)
            for (int x = x1; x <= x2; x++)
                n_astar_grid_set_walkable(grid, x, y, z, 0);
}

/*---------------------------------------------------------------------------
 * Path Reconstruction
 *-------------------------------------------------------------------------*/

/*! Build path from goal back to start following parent pointers */
static ASTAR_PATH* reconstruct_path(ASTAR_CELL* cells, const ASTAR_GRID* grid, int sx, int sy, int sz, int gx, int gy, int gz) {
    /* Count path length */
    int length = 0;
    int cx = gx, cy = gy, cz = gz;
    while (cx != sx || cy != sy || cz != sz) {
        length++;
        int idx = grid_index(grid, cx, cy, cz);
        int px = cells[idx].parent_x;
        int py = cells[idx].parent_y;
        int pz = cells[idx].parent_z;
        cx = px;
        cy = py;
        cz = pz;
        if (length > grid->width * grid->height * grid->depth) {
            return NULL;
        }
    }
    length++;

    ASTAR_PATH* path = (ASTAR_PATH*)calloc(1, sizeof(ASTAR_PATH));
    if (!path) return NULL;

    path->nodes = (ASTAR_NODE*)malloc((size_t)length * sizeof(ASTAR_NODE));
    if (!path->nodes) {
        free(path);
        return NULL;
    }
    path->length = length;
    path->cost = cells[grid_index(grid, gx, gy, gz)].g;

    int i = length - 1;
    cx = gx;
    cy = gy;
    cz = gz;
    while (i >= 0) {
        path->nodes[i].x = cx;
        path->nodes[i].y = cy;
        path->nodes[i].z = cz;
        if (cx == sx && cy == sy && cz == sz) break;
        int idx = grid_index(grid, cx, cy, cz);
        int px = cells[idx].parent_x;
        int py = cells[idx].parent_y;
        int pz = cells[idx].parent_z;
        cx = px;
        cy = py;
        cz = pz;
        i--;
    }

    return path;
}

/*---------------------------------------------------------------------------
 * 2D Neighbor Directions
 *-------------------------------------------------------------------------*/

static const int dir2d_cardinal[][2] = {
    {0, -1},
    {0, 1},
    {1, 0},
    {-1, 0}};
static const int dir2d_diagonal[][2] = {
    {1, -1},
    {-1, -1},
    {1, 1},
    {-1, 1}};

/*---------------------------------------------------------------------------
 * 3D Neighbor Directions
 *-------------------------------------------------------------------------*/

static const int dir3d_cardinal[][3] = {
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1}};
static const int dir3d_diagonal[][3] = {
    {1, 1, 0},
    {1, -1, 0},
    {-1, 1, 0},
    {-1, -1, 0},
    {1, 0, 1},
    {1, 0, -1},
    {-1, 0, 1},
    {-1, 0, -1},
    {0, 1, 1},
    {0, 1, -1},
    {0, -1, 1},
    {0, -1, -1},
    {1, 1, 1},
    {1, 1, -1},
    {1, -1, 1},
    {1, -1, -1},
    {-1, 1, 1},
    {-1, 1, -1},
    {-1, -1, 1},
    {-1, -1, -1}};

/*---------------------------------------------------------------------------
 * A* Core Algorithm
 *-------------------------------------------------------------------------*/

/**
 *@brief Find a path using A* search
 *@param grid the grid to search
 *@param sx start X
 *@param sy start Y
 *@param sz start Z
 *@param gx goal X
 *@param gy goal Y
 *@param gz goal Z
 *@param diagonal ASTAR_CARDINAL_ONLY or ASTAR_ALLOW_DIAGONAL
 *@param heuristic heuristic function to use
 *@return path from start to goal, or NULL if no path exists.
 *        Caller must free with n_astar_path_free().
 */
ASTAR_PATH* n_astar_find_path(const ASTAR_GRID* grid,
                              int sx,
                              int sy,
                              int sz,
                              int gx,
                              int gy,
                              int gz,
                              int diagonal,
                              ASTAR_HEURISTIC heuristic) {
    if (!grid) return NULL;
    if (!in_bounds(grid, sx, sy, sz) || !in_bounds(grid, gx, gy, gz)) return NULL;
    if (!grid->walkable[grid_index(grid, sx, sy, sz)]) return NULL;
    if (!grid->walkable[grid_index(grid, gx, gy, gz)]) return NULL;

    /* Trivial case */
    if (sx == gx && sy == gy && sz == gz) {
        ASTAR_PATH* path = (ASTAR_PATH*)calloc(1, sizeof(ASTAR_PATH));
        if (!path) return NULL;
        path->nodes = (ASTAR_NODE*)malloc(sizeof(ASTAR_NODE));
        if (!path->nodes) {
            free(path);
            return NULL;
        }
        path->nodes[0].x = sx;
        path->nodes[0].y = sy;
        path->nodes[0].z = sz;
        path->length = 1;
        path->cost = 0;
        return path;
    }

    int total = grid->width * grid->height * grid->depth;
    int is_3d = (grid->depth > 1);

    ASTAR_CELL* cells = (ASTAR_CELL*)calloc((size_t)total, sizeof(ASTAR_CELL));
    if (!cells) return NULL;

    for (int i = 0; i < total; i++) {
        cells[i].parent_x = -1;
        cells[i].parent_y = -1;
        cells[i].parent_z = -1;
        cells[i].status = ASTAR_NODE_NONE;
    }

    ASTAR_HEAP* open = heap_new(256);
    if (!open) {
        free(cells);
        return NULL;
    }

    int si = grid_index(grid, sx, sy, sz);
    cells[si].g = 0;
    cells[si].h = n_astar_heuristic(sx, sy, sz, gx, gy, gz, heuristic);
    cells[si].f = cells[si].h;
    cells[si].status = ASTAR_NODE_OPEN;
    heap_push(open, sx, sy, sz, cells[si].f);

    ASTAR_PATH* result = NULL;

    while (open->size > 0) {
        ASTAR_HEAP_NODE current = heap_pop(open);
        int cx = current.x, cy = current.y, cz = current.z;
        int ci = grid_index(grid, cx, cy, cz);

        if (cells[ci].status == ASTAR_NODE_CLOSED)
            continue;

        if (cx == gx && cy == gy && cz == gz) {
            result = reconstruct_path(cells, grid, sx, sy, sz, gx, gy, gz);
            break;
        }

        cells[ci].status = ASTAR_NODE_CLOSED;

        if (is_3d) {
            /* 3D: 6 cardinal directions */
            for (int d = 0; d < 6; d++) {
                int nx = cx + dir3d_cardinal[d][0];
                int ny = cy + dir3d_cardinal[d][1];
                int nz = cz + dir3d_cardinal[d][2];
                if (!in_bounds(grid, nx, ny, nz)) continue;

                int ni = grid_index(grid, nx, ny, nz);
                if (!grid->walkable[ni] || cells[ni].status == ASTAR_NODE_CLOSED)
                    continue;

                int move_cost = (grid->cost[ci] + grid->cost[ni]) / 2;
                int tentative_g = cells[ci].g + move_cost;

                if (cells[ni].status == ASTAR_NODE_NONE || tentative_g < cells[ni].g) {
                    cells[ni].g = tentative_g;
                    cells[ni].h = n_astar_heuristic(nx, ny, nz, gx, gy, gz, heuristic);
                    cells[ni].f = tentative_g + cells[ni].h;
                    cells[ni].parent_x = cx;
                    cells[ni].parent_y = cy;
                    cells[ni].parent_z = cz;
                    cells[ni].status = ASTAR_NODE_OPEN;
                    heap_push(open, nx, ny, nz, cells[ni].f);
                }
            }

            /* 3D: 20 diagonal directions */
            if (diagonal) {
                for (int d = 0; d < 20; d++) {
                    int ddx = dir3d_diagonal[d][0];
                    int ddy = dir3d_diagonal[d][1];
                    int ddz = dir3d_diagonal[d][2];
                    int nx = cx + ddx;
                    int ny = cy + ddy;
                    int nz = cz + ddz;
                    if (!in_bounds(grid, nx, ny, nz)) continue;

                    int ni = grid_index(grid, nx, ny, nz);
                    if (!grid->walkable[ni] || cells[ni].status == ASTAR_NODE_CLOSED)
                        continue;

                    /* Corner-cutting check */
                    int blocked = 0;
                    if (ddx != 0 && ddy != 0 && !grid->walkable[grid_index(grid, cx + ddx, cy, cz)])
                        blocked = 1;
                    if (ddx != 0 && ddy != 0 && !grid->walkable[grid_index(grid, cx, cy + ddy, cz)])
                        blocked = 1;
                    if (ddx != 0 && ddz != 0 && !grid->walkable[grid_index(grid, cx + ddx, cy, cz)])
                        blocked = 1;
                    if (ddx != 0 && ddz != 0 && !grid->walkable[grid_index(grid, cx, cy, cz + ddz)])
                        blocked = 1;
                    if (ddy != 0 && ddz != 0 && !grid->walkable[grid_index(grid, cx, cy + ddy, cz)])
                        blocked = 1;
                    if (ddy != 0 && ddz != 0 && !grid->walkable[grid_index(grid, cx, cy, cz + ddz)])
                        blocked = 1;
                    if (blocked) continue;

                    int axes = (ddx != 0) + (ddy != 0) + (ddz != 0);
                    int base_cost;
                    if (axes == 3)
                        base_cost = ASTAR_COST_DIAGONAL3D;
                    else
                        base_cost = ASTAR_COST_DIAGONAL;

                    int cell_cost = (grid->cost[ci] + grid->cost[ni]) / 2;
                    int move_cost = (base_cost * cell_cost) / ASTAR_COST_CARDINAL;
                    int tentative_g = cells[ci].g + move_cost;

                    if (cells[ni].status == ASTAR_NODE_NONE || tentative_g < cells[ni].g) {
                        cells[ni].g = tentative_g;
                        cells[ni].h = n_astar_heuristic(nx, ny, nz, gx, gy, gz, heuristic);
                        cells[ni].f = tentative_g + cells[ni].h;
                        cells[ni].parent_x = cx;
                        cells[ni].parent_y = cy;
                        cells[ni].parent_z = cz;
                        cells[ni].status = ASTAR_NODE_OPEN;
                        heap_push(open, nx, ny, nz, cells[ni].f);
                    }
                }
            }
        } else {
            /* 2D: 4 cardinal directions */
            for (int d = 0; d < 4; d++) {
                int nx = cx + dir2d_cardinal[d][0];
                int ny = cy + dir2d_cardinal[d][1];
                if (!in_bounds(grid, nx, ny, 0)) continue;

                int ni = grid_index(grid, nx, ny, 0);
                if (!grid->walkable[ni] || cells[ni].status == ASTAR_NODE_CLOSED)
                    continue;

                int move_cost = (grid->cost[ci] + grid->cost[ni]) / 2;
                int tentative_g = cells[ci].g + move_cost;

                if (cells[ni].status == ASTAR_NODE_NONE || tentative_g < cells[ni].g) {
                    cells[ni].g = tentative_g;
                    cells[ni].h = n_astar_heuristic(nx, ny, 0, gx, gy, 0, heuristic);
                    cells[ni].f = tentative_g + cells[ni].h;
                    cells[ni].parent_x = cx;
                    cells[ni].parent_y = cy;
                    cells[ni].parent_z = 0;
                    cells[ni].status = ASTAR_NODE_OPEN;
                    heap_push(open, nx, ny, 0, cells[ni].f);
                }
            }

            /* 2D: 4 diagonal directions */
            if (diagonal) {
                for (int d = 0; d < 4; d++) {
                    int ddx = dir2d_diagonal[d][0];
                    int ddy = dir2d_diagonal[d][1];
                    int nx = cx + ddx;
                    int ny = cy + ddy;
                    if (!in_bounds(grid, nx, ny, 0)) continue;

                    int ni = grid_index(grid, nx, ny, 0);
                    if (!grid->walkable[ni] || cells[ni].status == ASTAR_NODE_CLOSED)
                        continue;

                    if (!grid->walkable[grid_index(grid, cx + ddx, cy, 0)] ||
                        !grid->walkable[grid_index(grid, cx, cy + ddy, 0)])
                        continue;

                    int cell_cost = (grid->cost[ci] + grid->cost[ni]) / 2;
                    int move_cost = (ASTAR_COST_DIAGONAL * cell_cost) / ASTAR_COST_CARDINAL;
                    int tentative_g = cells[ci].g + move_cost;

                    if (cells[ni].status == ASTAR_NODE_NONE || tentative_g < cells[ni].g) {
                        cells[ni].g = tentative_g;
                        cells[ni].h = n_astar_heuristic(nx, ny, 0, gx, gy, 0, heuristic);
                        cells[ni].f = tentative_g + cells[ni].h;
                        cells[ni].parent_x = cx;
                        cells[ni].parent_y = cy;
                        cells[ni].parent_z = 0;
                        cells[ni].status = ASTAR_NODE_OPEN;
                        heap_push(open, nx, ny, 0, cells[ni].f);
                    }
                }
            }
        }
    }

    heap_free(open);
    free(cells);
    return result;
}

/*---------------------------------------------------------------------------
 * Path Cleanup
 *-------------------------------------------------------------------------*/

/**
 *@brief Free a path returned by n_astar_find_path
 *@param path path to free (safe to pass NULL)
 */
void n_astar_path_free(ASTAR_PATH* path) {
    if (!path) return;
    free(path->nodes);
    free(path);
}
