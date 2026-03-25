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
 *@example ex_iso_astar.c
 *@brief Nilorea Library: Isometric engine + A* pathfinding + Dead Reckoning demo
 *
 * Console-only demonstration showing how the three new modules work together:
 *
 *  1. ISO_MAP: Creates a height-aware isometric map with terrain, height,
 *     and walkability layers. Demonstrates coordinate conversion, height
 *     interpolation, terrain transitions, and map save/load.
 *
 *  2. A* Pathfinding: Builds an ASTAR_GRID from the ISO_MAP and finds
 *     optimal paths between points. Shows how terrain height affects
 *     both walkability and path cost.
 *
 *  3. Dead Reckoning: Simulates network latency hiding by moving an
 *     entity along the A* path with periodic "network updates" and
 *     convergence blending.
 *
 * Usage: ./ex_iso_astar
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 01/03/2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_astar.h"
#include "nilorea/n_dead_reckoning.h"
#include "nilorea/n_iso_engine.h"
#include "nilorea/n_trajectory.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Map configuration */
#define MAP_W 20
#define MAP_H 20
#define NUM_TERRAINS 4
#define MAX_HEIGHT 5
#define TILE_WIDTH 64.0f

static const char* terrain_names[] = {"Grass", "Sand", "Water", "Rock"};

/* Print a simple ASCII view of the map */
static void print_map_ascii(const ISO_MAP* map, const ASTAR_PATH* path) {
    /* Build a grid of characters */
    char grid[MAP_H][MAP_W];
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            int t = iso_map_get_terrain(map, x, y);
            int h = iso_map_get_height(map, x, y);
            int ab = iso_map_get_ability(map, x, y);
            if (ab == BLCK) {
                grid[y][x] = '#';
            } else if (t == 2) { /* water */
                grid[y][x] = '~';
            } else {
                grid[y][x] = (char)('0' + h);
            }
        }
    }

    /* Overlay the A* path */
    if (path) {
        for (int i = 0; i < path->length; i++) {
            int px = path->nodes[i].x;
            int py = path->nodes[i].y;
            if (px >= 0 && px < MAP_W && py >= 0 && py < MAP_H) {
                if (i == 0)
                    grid[py][px] = 'S'; /* start */
                else if (i == path->length - 1)
                    grid[py][px] = 'G'; /* goal */
                else
                    grid[py][px] = '*'; /* path */
            }
        }
    }

    printf("\n  ");
    for (int x = 0; x < map->width; x++) printf("%d", x % 10);
    printf("\n");
    for (int y = 0; y < map->height; y++) {
        printf("%2d", y);
        for (int x = 0; x < map->width; x++) {
            printf("%c", grid[y][x]);
        }
        printf("\n");
    }
    printf("\nLegend: #=blocked ~=water 0-5=height S=start G=goal *=path\n");
}

/* Build a test map with varied terrain and obstacles */
static void build_test_map(ISO_MAP* map) {
    /* Fill with grass at height 0 */
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            iso_map_set_terrain(map, x, y, 0); /* grass */
            iso_map_set_height(map, x, y, 0);
            iso_map_set_ability(map, x, y, WALK);
        }
    }

    /* Sand strip along the bottom */
    for (int x = 0; x < map->width; x++) {
        for (int y = 15; y < 18; y++) {
            iso_map_set_terrain(map, x, y, 1); /* sand */
        }
    }

    /* Water river through the middle */
    for (int y = 8; y < 11; y++) {
        for (int x = 0; x < map->width; x++) {
            iso_map_set_terrain(map, x, y, 2); /* water */
            iso_map_set_ability(map, x, y, BLCK);
        }
    }
    /* Bridge over the river */
    for (int y = 8; y < 11; y++) {
        iso_map_set_terrain(map, 10, y, 3); /* rock bridge */
        iso_map_set_height(map, 10, y, 1);
        iso_map_set_ability(map, 10, y, WALK);
    }

    /* Rocky hills in the top-right */
    for (int y = 1; y < 6; y++) {
        for (int x = 14; x < 19; x++) {
            iso_map_set_terrain(map, x, y, 3); /* rock */
            iso_map_set_height(map, x, y, 2 + (x + y) % 3);
        }
    }

    /* Wall obstacle */
    for (int y = 3; y < 7; y++) {
        iso_map_set_ability(map, 7, y, BLCK);
        iso_map_set_height(map, 7, y, MAX_HEIGHT);
    }

    /* Another small wall */
    for (int x = 3; x < 7; x++) {
        iso_map_set_ability(map, x, 13, BLCK);
        iso_map_set_height(map, x, 13, MAX_HEIGHT);
    }
}

/* Demo 1: Isometric engine features */
static void demo_iso_engine(ISO_MAP* map) {
    printf("=== DEMO 1: Isometric Engine ===\n\n");

    /* Show projection info */
    printf("Projection: classic 2:1 (%.1f deg)\n", map->proj.angle_deg);
    printf("  half_w=%.1f  half_h=%.1f  tile_lift=%.1f\n\n",
           map->proj.half_w, map->proj.half_h, map->proj.tile_lift);

    /* Map-to-screen conversion */
    printf("Coordinate conversion examples (map -> screen):\n");
    int test_coords[][3] = {{0, 0, 0}, {5, 5, 0}, {10, 0, 0}, {0, 10, 0}, {10, 10, 2}};
    for (int i = 0; i < 5; i++) {
        float sx, sy;
        iso_map_to_screen(map, test_coords[i][0], test_coords[i][1],
                          test_coords[i][2], &sx, &sy);
        printf("  Map(%d,%d) h=%d -> Screen(%.1f, %.1f)\n",
               test_coords[i][0], test_coords[i][1], test_coords[i][2], sx, sy);
    }

    /* Screen-to-map conversion */
    printf("\nReverse conversion examples (screen -> map):\n");
    float test_screen[][2] = {{32.0f, 16.0f}, {200.0f, 100.0f}, {0.0f, 320.0f}};
    for (int i = 0; i < 3; i++) {
        int mx, my;
        iso_screen_to_map(map, test_screen[i][0], test_screen[i][1], &mx, &my);
        printf("  Screen(%.0f, %.0f) -> Map(%d, %d)\n",
               test_screen[i][0], test_screen[i][1], mx, my);
    }

    /* Height interpolation */
    printf("\nHeight interpolation at fractional coordinates:\n");
    float test_frac[][2] = {{10.0f, 9.0f}, {10.5f, 9.5f}, {15.0f, 3.0f}, {15.5f, 3.5f}};
    for (int i = 0; i < 4; i++) {
        float h = iso_map_interpolate_height(map, test_frac[i][0], test_frac[i][1]);
        printf("  Height at (%.1f, %.1f) = %.2f\n", test_frac[i][0], test_frac[i][1], h);
    }

    /* Terrain transitions */
    printf("\nTerrain transitions (edge/corner bitmasks):\n");
    int trans_coords[][2] = {{9, 8}, {10, 7}, {14, 6}, {6, 15}};
    for (int i = 0; i < 4; i++) {
        int edge, corner;
        int tx = trans_coords[i][0], ty = trans_coords[i][1];
        iso_map_calc_transitions(map, tx, ty, &edge, &corner);
        printf("  (%2d,%2d) terrain=%s  edge=0x%X  corner=0x%X\n",
               tx, ty, terrain_names[iso_map_get_terrain(map, tx, ty)], edge, corner);
    }

    /* Corner heights for smooth rendering */
    printf("\nCorner heights for smooth rendering:\n");
    int corner_coords[][2] = {{10, 9}, {15, 3}, {7, 5}};
    for (int i = 0; i < 3; i++) {
        float hn, he, hs, hw;
        int cx = corner_coords[i][0], cy = corner_coords[i][1];
        iso_map_corner_heights(map, cx, cy, &hn, &he, &hs, &hw);
        printf("  (%2d,%2d) h=%d  corners: N=%.1f E=%.1f S=%.1f W=%.1f\n",
               cx, cy, iso_map_get_height(map, cx, cy), hn, he, hs, hw);
    }

    /* Save and reload */
    printf("\nMap save/load test:\n");
    int saved = iso_map_save(map, "test_iso_map.bin");
    printf("  Save: %s\n", saved ? "OK" : "FAILED");
    ISO_MAP* loaded = iso_map_load("test_iso_map.bin");
    if (loaded) {
        int match = 1;
        for (int y = 0; y < map->height && match; y++)
            for (int x = 0; x < map->width && match; x++)
                if (iso_map_get_terrain(map, x, y) != iso_map_get_terrain(loaded, x, y) ||
                    iso_map_get_height(map, x, y) != iso_map_get_height(loaded, x, y) ||
                    iso_map_get_ability(map, x, y) != iso_map_get_ability(loaded, x, y))
                    match = 0;
        printf("  Load: OK (data %s)\n", match ? "matches" : "MISMATCH");
        iso_map_free(&loaded);
    } else {
        printf("  Load: FAILED\n");
    }
    remove("test_iso_map.bin");
}

/* Demo 2: A* pathfinding on the isometric map */
static ASTAR_PATH* demo_astar(const ISO_MAP* map) {
    printf("\n=== DEMO 2: A* Pathfinding ===\n\n");

    /* Create ASTAR_GRID from the ISO_MAP */
    ASTAR_GRID* grid = n_astar_grid_new(map->width, map->height, 1);
    if (!grid) {
        printf("Failed to create A* grid\n");
        return NULL;
    }

    /* Populate walkability from map abilities */
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            int ab = iso_map_get_ability(map, x, y);
            uint8_t walkable = (ab == WALK || ab == SWIM) ? 1 : 0;
            n_astar_grid_set_walkable(grid, x, y, 0, walkable);

            /* Higher terrain costs more */
            int h = iso_map_get_height(map, x, y);
            n_astar_grid_set_cost(grid, x, y, 0, ASTAR_COST_CARDINAL + h * 200);
        }
    }

    /* Find path from top-left area to bottom-right area */
    int sx = 2, sy = 2, gx = 17, gy = 17;
    printf("Finding path from (%d,%d) to (%d,%d)...\n", sx, sy, gx, gy);

    ASTAR_PATH* path = n_astar_find_path(grid, sx, sy, 0, gx, gy, 0,
                                         ASTAR_ALLOW_DIAGONAL,
                                         ASTAR_HEURISTIC_CHEBYSHEV);
    if (path) {
        printf("Path found! Length: %d nodes, Cost: %d (x1000)\n",
               path->length, path->cost);
        printf("Path: ");
        for (int i = 0; i < path->length; i++) {
            if (i > 0) printf(" -> ");
            printf("(%d,%d)", path->nodes[i].x, path->nodes[i].y);
        }
        printf("\n");
    } else {
        printf("No path found!\n");
    }

    /* Try a second path that must cross the river via the bridge */
    printf("\nFinding path from (2,5) to (2,15) (must use bridge)...\n");
    ASTAR_PATH* path2 = n_astar_find_path(grid, 2, 5, 0, 2, 15, 0,
                                          ASTAR_ALLOW_DIAGONAL,
                                          ASTAR_HEURISTIC_CHEBYSHEV);
    if (path2) {
        printf("Path found! Length: %d nodes, Cost: %d\n", path2->length, path2->cost);
        printf("Path: ");
        for (int i = 0; i < path2->length; i++) {
            if (i > 0) printf(" -> ");
            printf("(%d,%d)", path2->nodes[i].x, path2->nodes[i].y);
        }
        printf("\n");
        n_astar_path_free(path2);
    } else {
        printf("No path found!\n");
    }

    /* Try an impossible path */
    printf("\nFinding path from (0,0) to blocked cell (7,4)...\n");
    ASTAR_PATH* path3 = n_astar_find_path(grid, 0, 0, 0, 7, 4, 0,
                                          ASTAR_ALLOW_DIAGONAL,
                                          ASTAR_HEURISTIC_CHEBYSHEV);
    printf("Result: %s\n", path3 ? "Found (unexpected!)" : "No path (correct - cell is blocked)");
    if (path3) n_astar_path_free(path3);

    n_astar_grid_free(grid);
    return path;
}

/* Demo 3: Dead reckoning simulation along the A* path */
static void demo_dead_reckoning(const ISO_MAP* map, const ASTAR_PATH* path) {
    printf("\n=== DEMO 3: Dead Reckoning Along A* Path ===\n\n");

    if (!path || path->length < 2) {
        printf("No valid path for dead reckoning demo\n");
        return;
    }

    /* Create trajectory from A* path waypoints */
    TRAJECTORY* traj = trajectory_new(TRAJECTORY_2D);
    if (!traj) {
        printf("Failed to create trajectory\n");
        return;
    }

    double move_speed = 3.0; /* tiles per second */
    double t = 0.0;
    for (int i = 0; i < path->length; i++) {
        PHYSICS state;
        memset(&state, 0, sizeof(state));
        state.position[0] = (double)path->nodes[i].x;
        state.position[1] = (double)path->nodes[i].y;

        /* Compute velocity as direction to next waypoint */
        if (i < path->length - 1) {
            double dx = (double)(path->nodes[i + 1].x - path->nodes[i].x);
            double dy = (double)(path->nodes[i + 1].y - path->nodes[i].y);
            double dist = sqrt(dx * dx + dy * dy);
            if (dist > 0.001) {
                state.speed[0] = dx / dist * move_speed;
                state.speed[1] = dy / dist * move_speed;
            }
        }

        trajectory_add_point(traj, &state, t);

        /* Time to next waypoint */
        if (i < path->length - 1) {
            double dx = (double)(path->nodes[i + 1].x - path->nodes[i].x);
            double dy = (double)(path->nodes[i + 1].y - path->nodes[i].y);
            double dist = sqrt(dx * dx + dy * dy);
            t += dist / move_speed;
        }
    }

    double total_time = t;
    printf("Trajectory total time: %.2f seconds (%d waypoints)\n\n", total_time, path->length);

    /* Create dead reckoning entity with PVB blending */
    DR_ENTITY* dr = dr_entity_create(DR_ALGO_VEL_ACC, DR_BLEND_PVB, 0.5, 0.3);
    if (!dr) {
        printf("Failed to create DR entity\n");
        trajectory_delete(&traj);
        return;
    }

    /* Simulation: "true" entity follows trajectory exactly.
     * Network sends updates every 0.5 seconds.
     * DR entity receives updates and extrapolates/blends between them. */
    double sim_dt = 0.05;              /* simulation timestep (50ms) */
    double send_interval = 0.5;        /* network update interval */
    double last_send = -send_interval; /* force initial send */

    printf("Time  | True Pos      | DR Pos        | Error | Height | Updates\n");
    printf("------+---------------+---------------+-------+--------+--------\n");

    for (double sim_t = 0.0; sim_t <= total_time + 0.1; sim_t += sim_dt) {
        /* Compute "true" position from trajectory */
        VECTOR3D true_pos;
        trajectory_get_position(traj, sim_t, true_pos);

        /* Compute true velocity */
        VECTOR3D true_vel;
        trajectory_get_speed(traj, sim_t, true_vel);

        /* Simulate periodic network updates */
        if (sim_t - last_send >= send_interval) {
            DR_VEC3 pos = dr_vec3(true_pos[0], true_pos[1], 0.0);
            DR_VEC3 vel = dr_vec3(true_vel[0], true_vel[1], 0.0);
            DR_VEC3 acc = dr_vec3_zero();
            dr_entity_receive_state(dr, &pos, &vel, &acc, sim_t);
            last_send = sim_t;
        }

        /* Compute DR display position */
        DR_VEC3 dr_pos;
        dr_entity_compute(dr, sim_t, &dr_pos);

        /* Compute error */
        double err = sqrt((dr_pos.x - true_pos[0]) * (dr_pos.x - true_pos[0]) +
                          (dr_pos.y - true_pos[1]) * (dr_pos.y - true_pos[1]));

        /* Get terrain height at DR position */
        float h = iso_map_interpolate_height(map, (float)dr_pos.x, (float)dr_pos.y);

        /* Print at regular intervals */
        double print_interval = 0.5;
        double mod = fmod(sim_t, print_interval);
        if (mod < sim_dt || sim_t < sim_dt) {
            printf("%5.2f | (%5.2f,%5.2f) | (%5.2f,%5.2f) | %5.3f | %6.2f | %d\n",
                   sim_t,
                   true_pos[0], true_pos[1],
                   dr_pos.x, dr_pos.y,
                   err, h, dr->update_count);
        }
    }

    /* Show blending mode comparison */
    printf("\n--- Convergence Mode Comparison ---\n");
    const char* mode_names[] = {"Snap", "PVB", "Cubic"};
    DR_BLEND modes[] = {DR_BLEND_SNAP, DR_BLEND_PVB, DR_BLEND_CUBIC};

    for (int m = 0; m < 3; m++) {
        DR_ENTITY* test_dr = dr_entity_create(DR_ALGO_VEL_ACC, modes[m], 0.5, 0.3);
        double total_err = 0.0;
        int samples = 0;
        last_send = -send_interval;

        for (double sim_t = 0.0; sim_t <= total_time; sim_t += sim_dt) {
            VECTOR3D tp, tv;
            trajectory_get_position(traj, sim_t, tp);
            trajectory_get_speed(traj, sim_t, tv);

            if (sim_t - last_send >= send_interval) {
                DR_VEC3 p = dr_vec3(tp[0], tp[1], 0.0);
                DR_VEC3 v = dr_vec3(tv[0], tv[1], 0.0);
                DR_VEC3 a = dr_vec3_zero();
                dr_entity_receive_state(test_dr, &p, &v, &a, sim_t);
                last_send = sim_t;
            }

            DR_VEC3 dp;
            dr_entity_compute(test_dr, sim_t, &dp);

            double err = sqrt((dp.x - tp[0]) * (dp.x - tp[0]) +
                              (dp.y - tp[1]) * (dp.y - tp[1]));
            total_err += err;
            samples++;
        }

        printf("  %-6s: avg error = %.4f over %d samples\n",
               mode_names[m], total_err / (double)samples, samples);
        dr_entity_destroy(&test_dr);
    }

    /* Threshold check demo */
    printf("\n--- Threshold Check Demo ---\n");
    DR_ENTITY* owner_dr = dr_entity_create(DR_ALGO_VEL_ACC, DR_BLEND_PVB, 1.0, 0.3);
    DR_VEC3 init_pos = dr_vec3(path->nodes[0].x, path->nodes[0].y, 0.0);
    dr_entity_set_position(owner_dr, &init_pos, NULL, NULL, 0.0);

    int updates_sent = 0;
    for (double sim_t = 0.0; sim_t <= total_time; sim_t += sim_dt) {
        VECTOR3D tp, tv;
        trajectory_get_position(traj, sim_t, tp);
        trajectory_get_speed(traj, sim_t, tv);
        DR_VEC3 true_p = dr_vec3(tp[0], tp[1], 0.0);
        DR_VEC3 true_v = dr_vec3(tv[0], tv[1], 0.0);
        DR_VEC3 true_a = dr_vec3_zero();

        if (dr_entity_check_threshold(owner_dr, &true_p, &true_v, &true_a, sim_t)) {
            dr_entity_receive_state(owner_dr, &true_p, &true_v, &true_a, sim_t);
            updates_sent++;
        }
    }
    printf("  With threshold=1.0 tile: sent %d updates over %.1f seconds\n",
           updates_sent, total_time);
    printf("  (vs %d with fixed 0.5s interval)\n", (int)(total_time / send_interval) + 1);
    dr_entity_destroy(&owner_dr);

    dr_entity_destroy(&dr);
    trajectory_delete(&traj);
}

/* Demo 4: A* pathfinding standalone test */
static void demo_astar_standalone(void) {
    printf("\n=== DEMO 4: A* Pathfinding Standalone Tests ===\n\n");

    /* Create a 10x10 grid with some walls */
    ASTAR_GRID* grid = n_astar_grid_new(10, 10, 1);
    if (!grid) {
        printf("Failed to create grid\n");
        return;
    }

    /* Build a maze-like layout */
    n_astar_grid_set_rect_blocked(grid, 2, 0, 0, 2, 5, 0);
    n_astar_grid_set_rect_blocked(grid, 4, 3, 0, 4, 9, 0);
    n_astar_grid_set_rect_blocked(grid, 6, 0, 0, 6, 6, 0);
    n_astar_grid_set_rect_blocked(grid, 8, 3, 0, 8, 9, 0);

    /* Print the grid */
    printf("10x10 maze grid (. = open, # = wall):\n  ");
    for (int x = 0; x < 10; x++) printf("%d", x);
    printf("\n");
    for (int y = 0; y < 10; y++) {
        printf("%d ", y);
        for (int x = 0; x < 10; x++) {
            printf("%c", n_astar_grid_get_walkable(grid, x, y, 0) ? '.' : '#');
        }
        printf("\n");
    }

    /* Test different heuristics */
    const char* heur_names[] = {"Manhattan", "Euclidean", "Chebyshev"};
    ASTAR_HEURISTIC heurs[] = {
        ASTAR_HEURISTIC_MANHATTAN,
        ASTAR_HEURISTIC_EUCLIDEAN,
        ASTAR_HEURISTIC_CHEBYSHEV};

    printf("\nPaths from (0,0) to (9,9) with different heuristics:\n");
    for (int h = 0; h < 3; h++) {
        ASTAR_PATH* p = n_astar_find_path(grid, 0, 0, 0, 9, 9, 0,
                                          ASTAR_ALLOW_DIAGONAL, heurs[h]);
        if (p) {
            printf("  %-10s: length=%2d cost=%5d  ",
                   heur_names[h], p->length, p->cost);
            for (int i = 0; i < p->length && i < 15; i++) {
                if (i > 0) printf("->");
                printf("(%d,%d)", p->nodes[i].x, p->nodes[i].y);
            }
            if (p->length > 15) printf("...");
            printf("\n");
            n_astar_path_free(p);
        } else {
            printf("  %-10s: no path\n", heur_names[h]);
        }
    }

    /* Cardinal-only vs diagonal */
    printf("\nCardinal-only vs diagonal movement:\n");
    ASTAR_PATH* p_card = n_astar_find_path(grid, 0, 0, 0, 9, 9, 0,
                                           ASTAR_CARDINAL_ONLY,
                                           ASTAR_HEURISTIC_MANHATTAN);
    ASTAR_PATH* p_diag = n_astar_find_path(grid, 0, 0, 0, 9, 9, 0,
                                           ASTAR_ALLOW_DIAGONAL,
                                           ASTAR_HEURISTIC_CHEBYSHEV);
    if (p_card) printf("  Cardinal: length=%d cost=%d\n", p_card->length, p_card->cost);
    if (p_diag) printf("  Diagonal: length=%d cost=%d\n", p_diag->length, p_diag->cost);
    if (p_card) n_astar_path_free(p_card);
    if (p_diag) n_astar_path_free(p_diag);

    n_astar_grid_free(grid);
}

/* Main */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    set_log_level(LOG_NOTICE);

    printf("Nilorea Library: Isometric + A* + Dead Reckoning Demo\n");
    printf("======================================================\n");

    /* Create the isometric map */
    ISO_MAP* map = iso_map_new(MAP_W, MAP_H, NUM_TERRAINS, MAX_HEIGHT);
    if (!map) {
        fprintf(stderr, "Failed to create map\n");
        return EXIT_FAILURE;
    }

    /* Set projection */
    iso_map_set_projection(map, ISO_PROJ_CLASSIC, TILE_WIDTH);

    /* Build the test map */
    build_test_map(map);

    /* Show ASCII map overview */
    printf("\nMap overview (%dx%d, %d terrains, max height %d):\n",
           MAP_W, MAP_H, NUM_TERRAINS, MAX_HEIGHT);
    print_map_ascii(map, NULL);

    /* Demo 1: Isometric engine */
    demo_iso_engine(map);

    /* Demo 2: A* pathfinding on the map */
    ASTAR_PATH* path = demo_astar(map);

    /* Show map with path overlaid */
    if (path) {
        printf("\nMap with A* path overlaid:\n");
        print_map_ascii(map, path);
    }

    /* Demo 3: Dead reckoning along the path */
    demo_dead_reckoning(map, path);

    /* Demo 4: A* standalone tests */
    demo_astar_standalone();

    /* Cleanup */
    if (path) n_astar_path_free(path);
    iso_map_free(&map);

    printf("\n=== All demos complete ===\n");
    return EXIT_SUCCESS;
}
