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
 *@example ex_trajectory.c
 *@brief Nilorea Library trajectory API graphical demo (Allegro5)
 *
 * Split-screen demonstration of the cubic Hermite spline trajectory API
 * with multi-waypoint paths:
 *  - Left panel (2D): click to build a multi-waypoint path, object follows it
 *  - Right panel (3D): pre-built looping 3D path with cabinet projection
 *
 * Controls:
 *  - Left click on left panel: add waypoint to the path
 *  - R: reset 2D demo
 *  - ESC: quit
 *
 * Based on "Defeating Lag With Cubic Splines" (GameDev.net Article 914)
 *
 *@author Castagnier Mickael
 *@version 3.0
 *@date 28/02/2026
 */

#define ALLEGRO_UNSTABLE 1

#define WIDTH 800
#define HEIGHT 600
#define PANEL_W 400
#define FPS 60.0
#define DT (1.0 / FPS)
#define MAX_TRAIL 300

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_trajectory.h"

#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 2D Demo State */
static TRAJECTORY* traj_2d = NULL;
static double time_2d = 0.0;
static int running_2d = 0;

/* raw waypoint storage for Catmull-Rom velocity computation */
#define MAX_WP2D 50
static double wp2d_x[MAX_WP2D], wp2d_y[MAX_WP2D], wp2d_t[MAX_WP2D];
static int wp2d_count = 0;
static const double WP2D_SPEED = 80.0; /* pixels/sec reference speed */

static float trail_2d[MAX_TRAIL][2];
static int trail_2d_count = 0;
static int trail_2d_head = 0;

/* 3D Demo State */
static TRAJECTORY* traj_3d = NULL;
static double time_3d = 0.0;
static double time_3d_end = 0.0;

static float trail_3d[MAX_TRAIL][2];
static int trail_3d_count = 0;
static int trail_3d_head = 0;

/* 3D waypoints forming a closed loop (last = first for seamless looping) */
typedef struct {
    double x, y, z, vx, vy, vz;
} WAYPOINT3D;

#define NUM_WP3D 7
static const WAYPOINT3D waypoints_3d[NUM_WP3D] = {
    {0.0, 0.0, 0.0, 2.0, 1.0, 0.5},
    {3.0, 2.0, 1.0, 1.0, 1.5, -0.5},
    {4.0, 4.0, -1.0, -1.0, 1.0, -1.0},
    {1.0, 5.0, -2.0, -2.0, 0.0, 0.5},
    {-2.0, 3.0, 0.0, -1.0, -1.5, 1.0},
    {-1.0, 1.0, 1.5, 1.0, -1.0, 0.0},
    {0.0, 0.0, 0.0, 2.0, 1.0, 0.5}, /* loop back to start */
};
static const double WP3D_DUR = 4.0; /* seconds per 3D segment */

/* Helpers */

static void init_physics_2d(PHYSICS* p, double px, double py, double vx, double vy) {
    memset(p, 0, sizeof(PHYSICS));
    VECTOR3D_SET(p->position, px, py, 0.0);
    VECTOR3D_SET(p->speed, vx, vy, 0.0);
}

static void init_physics_3d(PHYSICS* p, double px, double py, double pz, double vx, double vy, double vz) {
    memset(p, 0, sizeof(PHYSICS));
    VECTOR3D_SET(p->position, px, py, pz);
    VECTOR3D_SET(p->speed, vx, vy, vz);
}

static void trail_push_2d(float x, float y) {
    trail_2d[trail_2d_head][0] = x;
    trail_2d[trail_2d_head][1] = y;
    trail_2d_head = (trail_2d_head + 1) % MAX_TRAIL;
    if (trail_2d_count < MAX_TRAIL) trail_2d_count++;
}

static void trail_push_3d(float x, float y) {
    trail_3d[trail_3d_head][0] = x;
    trail_3d[trail_3d_head][1] = y;
    trail_3d_head = (trail_3d_head + 1) % MAX_TRAIL;
    if (trail_3d_count < MAX_TRAIL) trail_3d_count++;
}

/* 3D Projection (Cabinet) */

static void project_3d(double x, double y, double z, float* sx, float* sy) {
    double scale = 30.0;
    double cx = (double)PANEL_W + (double)PANEL_W / 2.0;
    double cy = (double)HEIGHT / 2.0;
    double angle = M_PI / 4.0;
    *sx = (float)(cx + x * scale + z * 0.5 * cos(angle) * scale);
    *sy = (float)(cy - y * scale - z * 0.5 * sin(angle) * scale);
}

/* Drawing Utilities */

static void draw_arrow(float x1, float y1, float x2, float y2, ALLEGRO_COLOR color, float thickness) {
    al_draw_line(x1, y1, x2, y2, color, thickness);
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 2.0f) return;
    float ux = dx / len, uy = dy / len;
    float px = -uy * 4.0f, py = ux * 4.0f;
    float bx = x2 - ux * 8.0f, by = y2 - uy * 8.0f;
    al_draw_filled_triangle(x2, y2, bx + px, by + py,
                            bx - px, by - py, color);
}

static void draw_trail(float trail[][2], int count, int head, ALLEGRO_COLOR base_color) {
    if (count < 2) return;
    unsigned char r, g, b;
    al_unmap_rgb(base_color, &r, &g, &b);
    for (int i = 1; i < count; i++) {
        int i0 = (head - count + i - 1 + MAX_TRAIL) % MAX_TRAIL;
        int i1 = (head - count + i + MAX_TRAIL) % MAX_TRAIL;
        float alpha = (float)i / (float)count;
        ALLEGRO_COLOR c = al_map_rgba(
            (unsigned char)(r * alpha),
            (unsigned char)(g * alpha),
            (unsigned char)(b * alpha),
            (unsigned char)(alpha * 200));
        al_draw_line(trail[i0][0], trail[i0][1],
                     trail[i1][0], trail[i1][1], c, 2);
    }
}

/* 2D Demo: Init / Reset / Click / Rebuild / Draw */

static void rebuild_2d_trajectory(void) {
    trajectory_clear_points(traj_2d);
    if (wp2d_count < 2) return;

    for (int i = 0; i < wp2d_count; i++) {
        double vx, vy;

        if (i == 0) {
            /* forward difference */
            double dt = wp2d_t[1] - wp2d_t[0];
            vx = (wp2d_x[1] - wp2d_x[0]) / dt;
            vy = (wp2d_y[1] - wp2d_y[0]) / dt;
        } else if (i == wp2d_count - 1) {
            /* backward difference */
            double dt = wp2d_t[i] - wp2d_t[i - 1];
            vx = (wp2d_x[i] - wp2d_x[i - 1]) / dt;
            vy = (wp2d_y[i] - wp2d_y[i - 1]) / dt;
        } else {
            /* Catmull-Rom: central difference */
            double dt = wp2d_t[i + 1] - wp2d_t[i - 1];
            vx = (wp2d_x[i + 1] - wp2d_x[i - 1]) / dt;
            vy = (wp2d_y[i + 1] - wp2d_y[i - 1]) / dt;
        }

        PHYSICS state;
        init_physics_2d(&state, wp2d_x[i], wp2d_y[i], vx, vy);
        trajectory_add_point(traj_2d, &state, wp2d_t[i]);
    }

    running_2d = 1;
}

static void reset_2d(void) {
    if (traj_2d) trajectory_delete(&traj_2d);
    traj_2d = trajectory_new(TRAJECTORY_2D);
    time_2d = 0.0;
    running_2d = 0;
    wp2d_count = 0;
    trail_2d_count = 0;
    trail_2d_head = 0;
}

static void handle_click_2d(float mx, float my) {
    if (mx >= PANEL_W || wp2d_count >= MAX_WP2D) return;

    if (wp2d_count == 0) {
        wp2d_x[0] = (double)mx;
        wp2d_y[0] = (double)my;
        wp2d_t[0] = 0.0;
        wp2d_count = 1;
        return;
    }

    /* compute time for new waypoint based on distance */
    double dx = (double)mx - wp2d_x[wp2d_count - 1];
    double dy = (double)my - wp2d_y[wp2d_count - 1];
    double dist = sqrt(dx * dx + dy * dy);
    if (dist < 10.0) return;

    double dur = dist / WP2D_SPEED;
    if (dur < 0.3) dur = 0.3;

    wp2d_x[wp2d_count] = (double)mx;
    wp2d_y[wp2d_count] = (double)my;
    wp2d_t[wp2d_count] = wp2d_t[wp2d_count - 1] + dur;
    wp2d_count++;

    rebuild_2d_trajectory();
}

static void draw_full_path_2d(void) {
    if (!traj_2d || traj_2d->nb_points < 2) return;

    double t0 = traj_2d->points[0].time_val;
    double t1 = traj_2d->points[traj_2d->nb_points - 1].time_val;
    int total_steps = (traj_2d->nb_points - 1) * 30;

    VECTOR3D pos;
    trajectory_get_position(traj_2d, t0, pos);
    float px = (float)pos[0], py = (float)pos[1];

    for (int i = 1; i <= total_steps; i++) {
        double t = t0 + (t1 - t0) * (double)i / (double)total_steps;
        trajectory_get_position(traj_2d, t, pos);
        float nx = (float)pos[0], ny = (float)pos[1];
        al_draw_line(px, py, nx, ny,
                     al_map_rgba(0, 200, 255, 180), 2);
        px = nx;
        py = ny;
    }

    /* extrapolation tail: base length on last segment, not total path */
    double last_seg_dur = traj_2d->points[traj_2d->nb_points - 1].time_val - traj_2d->points[traj_2d->nb_points - 2].time_val;
    double ext = last_seg_dur * 0.5;
    if (ext < 0.3) ext = 0.3;
    for (int i = 1; i <= 20; i++) {
        double t = t1 + ext * (double)i / 20.0;
        trajectory_get_position(traj_2d, t, pos);
        float nx = (float)pos[0], ny = (float)pos[1];
        if (i % 2 == 0)
            al_draw_line(px, py, nx, ny,
                         al_map_rgba(255, 255, 0, 120), 1);
        px = nx;
        py = ny;
    }
}

static void draw_2d_panel(ALLEGRO_FONT* font) {
    al_draw_filled_rectangle(0, 0, PANEL_W, HEIGHT,
                             al_map_rgb(15, 15, 30));

    ALLEGRO_COLOR grid = al_map_rgba(35, 35, 55, 255);
    for (int x = 0; x < PANEL_W; x += 40)
        al_draw_line((float)x, 0, (float)x, HEIGHT, grid, 1);
    for (int y = 0; y < HEIGHT; y += 40)
        al_draw_line(0, (float)y, PANEL_W, (float)y, grid, 1);

    al_draw_text(font, al_map_rgb(220, 220, 220), 5, 5, 0,
                 "2D Multi-Waypoint Path");

    /* draw full spline path */
    draw_full_path_2d();

    /* draw all waypoint markers */
    for (int i = 0; i < wp2d_count; i++) {
        float wx = (float)wp2d_x[i], wy = (float)wp2d_y[i];
        ALLEGRO_COLOR wc;
        if (i == 0)
            wc = al_map_rgb(0, 200, 0);
        else if (i == wp2d_count - 1)
            wc = al_map_rgb(200, 0, 0);
        else
            wc = al_map_rgb(100, 150, 255);
        al_draw_filled_circle(wx, wy, 5, wc);
        al_draw_circle(wx, wy, 5, al_map_rgb(255, 255, 255), 1);

        char num[16];
        snprintf(num, sizeof(num), "%d", i);
        al_draw_text(font, al_map_rgb(200, 200, 200),
                     wx + 7, wy - 6, 0, num);
    }

    if (running_2d && traj_2d && traj_2d->nb_points >= 2) {
        /* trail */
        draw_trail(trail_2d, trail_2d_count, trail_2d_head,
                   al_map_rgb(100, 180, 255));

        /* current object */
        trajectory_compute(traj_2d, time_2d);
        float ox = (float)traj_2d->current.position[0];
        float oy = (float)traj_2d->current.position[1];

        al_draw_filled_circle(ox, oy, 8,
                              al_map_rgb(255, 180, 0));
        al_draw_circle(ox, oy, 8,
                       al_map_rgb(255, 255, 255), 2);

        /* velocity arrow */
        float vs = 0.15f;
        draw_arrow(ox, oy,
                   ox + (float)traj_2d->current.speed[0] * vs,
                   oy + (float)traj_2d->current.speed[1] * vs,
                   al_map_rgb(255, 255, 100), 1);

        /* info text */
        char buf[128];
        const char* mode = "???";
        if (traj_2d->mode == TRAJECTORY_INTERP)
            mode = "INTERP";
        else if (traj_2d->mode == TRAJECTORY_EXTRAP)
            mode = "EXTRAP";
        else if (traj_2d->mode == TRAJECTORY_BEFORE)
            mode = "BEFORE";

        snprintf(buf, sizeof(buf), "t=%.2f  seg=%d/%d  [%s]",
                 time_2d, traj_2d->current_segment,
                 traj_2d->nb_points - 1, mode);
        al_draw_text(font, al_map_rgb(200, 200, 200), 5, 18, 0, buf);
        snprintf(buf, sizeof(buf), "pos=(%.0f,%.0f) vel=(%.0f,%.0f) wp=%d",
                 traj_2d->current.position[0],
                 traj_2d->current.position[1],
                 traj_2d->current.speed[0],
                 traj_2d->current.speed[1],
                 wp2d_count);
        al_draw_text(font, al_map_rgb(200, 200, 200), 5, 34, 0, buf);
    } else {
        const char* msg = (wp2d_count == 0)
                              ? "Click to place waypoints"
                              : "Click to add more waypoints";
        al_draw_text(font, al_map_rgb(150, 150, 150),
                     PANEL_W / 2, 18,
                     ALLEGRO_ALIGN_CENTER, msg);
    }

    /* legend */
    al_draw_line(5, HEIGHT - 60, 25, HEIGHT - 60,
                 al_map_rgba(0, 200, 255, 180), 2);
    al_draw_text(font, al_map_rgb(160, 160, 160),
                 30, HEIGHT - 66, 0, "Interpolation");
    al_draw_line(5, HEIGHT - 46, 15, HEIGHT - 46,
                 al_map_rgba(255, 255, 0, 120), 1);
    al_draw_line(18, HEIGHT - 46, 25, HEIGHT - 46,
                 al_map_rgba(255, 255, 0, 120), 1);
    al_draw_text(font, al_map_rgb(160, 160, 160),
                 30, HEIGHT - 52, 0, "Extrapolation");

    al_draw_text(font, al_map_rgb(90, 90, 90), 5, HEIGHT - 15, 0,
                 "[R] Reset   [ESC] Quit");
}

/* 3D Demo: Init / Update / Draw */

static void init_3d(void) {
    traj_3d = trajectory_new(TRAJECTORY_3D);
    time_3d = 0.0;
    trail_3d_count = 0;
    trail_3d_head = 0;

    /* add all waypoints to the trajectory */
    double t = 0.0;
    for (int i = 0; i < NUM_WP3D; i++) {
        const WAYPOINT3D* w = &waypoints_3d[i];
        PHYSICS state;
        init_physics_3d(&state, w->x, w->y, w->z,
                        w->vx, w->vy, w->vz);
        trajectory_add_point(traj_3d, &state, t);
        t += WP3D_DUR;
    }
    time_3d_end = t - WP3D_DUR; /* time of last waypoint */
}

static void update_3d_logic(void) {
    if (!traj_3d || traj_3d->nb_points < 2) return;

    time_3d += DT;

    /* loop when past the last waypoint */
    if (time_3d >= time_3d_end) {
        time_3d -= time_3d_end;
        trail_3d_count = 0;
        trail_3d_head = 0;
    }

    trajectory_compute(traj_3d, time_3d);
    float sx, sy;
    project_3d(traj_3d->current.position[0],
               traj_3d->current.position[1],
               traj_3d->current.position[2], &sx, &sy);
    trail_push_3d(sx, sy);
}

static void draw_3d_axes(ALLEGRO_FONT* font) {
    float ox, oy, ax, ay;
    project_3d(0, 0, 0, &ox, &oy);

    project_3d(2.5, 0, 0, &ax, &ay);
    draw_arrow(ox, oy, ax, ay, al_map_rgb(200, 60, 60), 2);
    al_draw_text(font, al_map_rgb(200, 60, 60),
                 ax + 4, ay - 6, 0, "X");

    project_3d(0, 2.5, 0, &ax, &ay);
    draw_arrow(ox, oy, ax, ay, al_map_rgb(60, 200, 60), 2);
    al_draw_text(font, al_map_rgb(60, 200, 60),
                 ax + 4, ay - 6, 0, "Y");

    project_3d(0, 0, 2.5, &ax, &ay);
    draw_arrow(ox, oy, ax, ay, al_map_rgb(60, 60, 200), 2);
    al_draw_text(font, al_map_rgb(60, 60, 200),
                 ax + 4, ay - 6, 0, "Z");
}

static void draw_ground_grid(void) {
    ALLEGRO_COLOR gc = al_map_rgba(45, 45, 45, 128);
    for (int i = -4; i <= 4; i++) {
        float x1, y1, x2, y2;
        project_3d((double)i, 0, -4, &x1, &y1);
        project_3d((double)i, 0, 4, &x2, &y2);
        al_draw_line(x1, y1, x2, y2, gc, 1);
        project_3d(-4, 0, (double)i, &x1, &y1);
        project_3d(4, 0, (double)i, &x2, &y2);
        al_draw_line(x1, y1, x2, y2, gc, 1);
    }
}

static void draw_full_path_3d(void) {
    if (!traj_3d || traj_3d->nb_points < 2) return;

    double t0 = traj_3d->points[0].time_val;
    double t1 = traj_3d->points[traj_3d->nb_points - 1].time_val;
    int total_steps = (traj_3d->nb_points - 1) * 30;

    VECTOR3D pos;
    trajectory_get_position(traj_3d, t0, pos);
    float px, py;
    project_3d(pos[0], pos[1], pos[2], &px, &py);

    for (int i = 1; i <= total_steps; i++) {
        double t = t0 + (t1 - t0) * (double)i / (double)total_steps;
        trajectory_get_position(traj_3d, t, pos);
        float nx, ny;
        project_3d(pos[0], pos[1], pos[2], &nx, &ny);
        al_draw_line(px, py, nx, ny,
                     al_map_rgba(255, 100, 255, 140), 2);
        px = nx;
        py = ny;
    }
}

static void draw_3d_panel(ALLEGRO_FONT* font) {
    al_draw_filled_rectangle(PANEL_W, 0, WIDTH, HEIGHT,
                             al_map_rgb(18, 18, 22));

    al_draw_text(font, al_map_rgb(220, 220, 220),
                 PANEL_W + 5, 5, 0, "3D Multi-Waypoint Loop");

    draw_ground_grid();
    draw_3d_axes(font);

    if (!traj_3d || traj_3d->nb_points < 2) return;

    /* draw full looping path */
    draw_full_path_3d();

    /* draw waypoint markers */
    for (int i = 0; i < NUM_WP3D; i++) {
        float sx, sy;
        project_3d(waypoints_3d[i].x, waypoints_3d[i].y,
                   waypoints_3d[i].z, &sx, &sy);
        ALLEGRO_COLOR c = (i == traj_3d->current_segment ||
                           i == traj_3d->current_segment + 1)
                              ? al_map_rgb(255, 80, 80)
                              : al_map_rgb(100, 100, 200);
        al_draw_filled_circle(sx, sy, 4, c);
        al_draw_circle(sx, sy, 4, al_map_rgb(200, 200, 200), 1);

        char num[16];
        snprintf(num, sizeof(num), "%d", i);
        al_draw_text(font, al_map_rgb(180, 180, 180),
                     sx + 6, sy - 6, 0, num);
    }

    /* trail */
    draw_trail(trail_3d, trail_3d_count, trail_3d_head,
               al_map_rgb(200, 120, 255));

    /* current object */
    trajectory_compute(traj_3d, time_3d);
    float obj_sx, obj_sy;
    project_3d(traj_3d->current.position[0],
               traj_3d->current.position[1],
               traj_3d->current.position[2],
               &obj_sx, &obj_sy);

    /* vertical drop line (depth cue) */
    float gnd_sx, gnd_sy;
    project_3d(traj_3d->current.position[0], 0,
               traj_3d->current.position[2],
               &gnd_sx, &gnd_sy);
    al_draw_line(gnd_sx, gnd_sy, obj_sx, obj_sy,
                 al_map_rgba(100, 100, 100, 80), 1);
    al_draw_filled_circle(gnd_sx, gnd_sy, 2,
                          al_map_rgba(100, 100, 100, 60));

    /* object */
    al_draw_filled_circle(obj_sx, obj_sy, 7,
                          al_map_rgb(255, 200, 50));
    al_draw_circle(obj_sx, obj_sy, 7,
                   al_map_rgb(255, 255, 255), 2);

    /* velocity arrow */
    double vs3 = 0.4;
    float vel_sx, vel_sy;
    project_3d(traj_3d->current.position[0] +
                   traj_3d->current.speed[0] * vs3,
               traj_3d->current.position[1] +
                   traj_3d->current.speed[1] * vs3,
               traj_3d->current.position[2] +
                   traj_3d->current.speed[2] * vs3,
               &vel_sx, &vel_sy);
    draw_arrow(obj_sx, obj_sy, vel_sx, vel_sy,
               al_map_rgb(255, 255, 100), 1);

    /* info text */
    char buf[128];
    snprintf(buf, sizeof(buf), "t=%.2f  seg=%d/%d  wp=%d",
             time_3d, traj_3d->current_segment,
             traj_3d->nb_points - 1, traj_3d->nb_points);
    al_draw_text(font, al_map_rgb(200, 200, 200),
                 PANEL_W + 5, 18, 0, buf);
    snprintf(buf, sizeof(buf), "pos=(%.1f, %.1f, %.1f)",
             traj_3d->current.position[0],
             traj_3d->current.position[1],
             traj_3d->current.position[2]);
    al_draw_text(font, al_map_rgb(200, 200, 200),
                 PANEL_W + 5, 34, 0, buf);
    snprintf(buf, sizeof(buf), "vel=(%.1f, %.1f, %.1f)",
             traj_3d->current.speed[0],
             traj_3d->current.speed[1],
             traj_3d->current.speed[2]);
    al_draw_text(font, al_map_rgb(200, 200, 200),
                 PANEL_W + 5, 50, 0, buf);
}

/* Main */

int main(int argc, char* argv[]) {
    (void)argc;
    set_log_level(LOG_ERR);

    n_log(LOG_NOTICE, "%s is starting ...", argv[0]);

    if (!al_init()) {
        n_abort("Could not init Allegro.\n");
    }
    if (!al_init_primitives_addon()) {
        n_abort("Unable to initialize primitives addon\n");
    }
    if (!al_init_font_addon()) {
        n_abort("Unable to initialize font addon\n");
    }
    if (!al_install_keyboard()) {
        n_abort("Unable to initialize keyboard handler\n");
    }
    if (!al_install_mouse()) {
        n_abort("Unable to initialize mouse handler\n");
    }

    al_set_new_display_flags(ALLEGRO_OPENGL | ALLEGRO_WINDOWED);
    ALLEGRO_DISPLAY* display = al_create_display(WIDTH, HEIGHT);
    if (!display) {
        n_abort("Unable to create display\n");
    }
    al_set_window_title(display,
                        "Nilorea Trajectory API - Multi-Waypoint Demo");

    ALLEGRO_FONT* font = al_create_builtin_font();
    ALLEGRO_TIMER* timer = al_create_timer(1.0 / FPS);

    ALLEGRO_EVENT_QUEUE* event_queue = al_create_event_queue();
    al_register_event_source(event_queue,
                             al_get_display_event_source(display));
    al_register_event_source(event_queue,
                             al_get_timer_event_source(timer));
    al_register_event_source(event_queue,
                             al_get_keyboard_event_source());
    al_register_event_source(event_queue,
                             al_get_mouse_event_source());

    ALLEGRO_BITMAP* scrbuf = al_create_bitmap(WIDTH, HEIGHT);

    reset_2d();
    init_3d();

    al_start_timer(timer);

    int done = 0;
    int do_draw = 0;

    while (!done) {
        ALLEGRO_EVENT ev;
        al_wait_for_event(event_queue, &ev);

        if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
            done = 1;
        } else if (ev.type == ALLEGRO_EVENT_KEY_DOWN) {
            if (ev.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
                done = 1;
            } else if (ev.keyboard.keycode == ALLEGRO_KEY_R) {
                reset_2d();
            }
        } else if (ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
            if (ev.mouse.button == 1) {
                handle_click_2d((float)ev.mouse.x,
                                (float)ev.mouse.y);
            }
        } else if (ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_IN ||
                   ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT) {
            al_clear_keyboard_state(display);
            al_flush_event_queue(event_queue);
        } else if (ev.type == ALLEGRO_EVENT_TIMER) {
            /* 2D logic */
            if (running_2d && traj_2d && traj_2d->nb_points >= 2) {
                time_2d += DT;
                trajectory_compute(traj_2d, time_2d);
                trail_push_2d(
                    (float)traj_2d->current.position[0],
                    (float)traj_2d->current.position[1]);
            }

            /* 3D logic */
            update_3d_logic();

            do_draw = 1;
        }

        if (do_draw && al_is_event_queue_empty(event_queue)) {
            al_set_target_bitmap(scrbuf);
            al_clear_to_color(al_map_rgb(0, 0, 0));

            draw_2d_panel(font);
            draw_3d_panel(font);

            al_draw_line(PANEL_W, 0, PANEL_W, HEIGHT,
                         al_map_rgb(80, 80, 80), 2);

            al_set_target_bitmap(al_get_backbuffer(display));
            al_draw_bitmap(scrbuf, 0, 0, 0);
            al_flip_display();
            do_draw = 0;
        }
    }

    trajectory_delete(&traj_2d);
    trajectory_delete(&traj_3d);
    al_destroy_bitmap(scrbuf);
    al_destroy_font(font);
    al_destroy_timer(timer);
    al_destroy_event_queue(event_queue);
    al_destroy_display(display);

    return 0;
}
