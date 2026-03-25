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
 *@example ex_gui_isometric.c
 *@brief Nilorea Library: Isometric tile engine with GUI controls
 *
 * Allegro 5 graphical demo combining the n_iso_engine module with the n_gui
 * module.  Features:
 *   - Height-aware isometric map rendering (iso_map_draw)
 *   - Terrain transitions with procedural masks (Article 934)
 *   - Smooth / Cut height modes with adjustable slope max
 *   - GUI toggle buttons for modes, sliders for height/slope, listbox for
 *     terrain selection, projection radio group
 *   - Click-to-paint terrain and height editing
 *   - Mouse-wheel zoom, scroll with arrow keys
 *   - Map save/load (binary ISOM format)
 *   - Player mode: WASD movement with jump (Space), height collision,
 *     wall-sliding, A* click-to-move pathfinding, camera follow
 *   - Ghost mode: dead reckoning demo with simulated circular-path remote
 *     entity (n_dead_reckoning), toggleable DR algorithm/blend/interval
 *
 * Based on the Nilorea isometric tile engine (gullradriel/Nilorea).
 * Tile BMP assets are in DATAS/tiles/.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 01/03/2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

/* Allegro5 */
#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>

/* nilorea-library modules */
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_str.h"
#include "nilorea/n_astar.h"
#include "nilorea/n_iso_engine.h"
#include "nilorea/n_dead_reckoning.h"
#include "nilorea/n_gui.h"

/* Constants */
#define SCREEN_W 1280
#define SCREEN_H 720
#define FPS 60.0

/* Tile dimensions (isometric diamond) */
#define TILE_W 66
#define TILE_H 34

/* Map size */
#define MAP_W 20
#define MAP_H 20

/* Number of terrain types */
#define NUM_TERRAINS 8

/* Height / elevation settings */
#define MAX_HEIGHT 5
#define TILE_LIFT 16

/* Player movement settings */
#define PLAYER_MOVE_SPEED 4.0f
#define JUMP_VELOCITY 9.0f      /* initial upward velocity (height units/sec) */
#define JUMP_GRAVITY 20.0f      /* gravity for jump (height units/sec^2) */
#define CAM_SMOOTHING 8.0f      /* camera lerp speed (higher = snappier) */
#define PLAYER_MAX_HEIGH_DIFF 1 /* max height diff player can walk over */

/* Terrain type IDs, ordered by visual precedence (higher = drawn on top) */
#define TERRAIN_EAU 0      /* water - lowest precedence, base terrain */
#define TERRAIN_LAVE 1     /* lava */
#define TERRAIN_SABLE 2    /* sand */
#define TERRAIN_CHEMIN 3   /* road */
#define TERRAIN_TEMPLE3 4  /* temple floor light */
#define TERRAIN_TEMPLE4 5  /* temple floor dark */
#define TERRAIN_PAILLE 6   /* straw */
#define TERRAIN_ROCAILLE 7 /* rock - highest precedence */

/* Terrain names for HUD */
static const char* terrain_names[NUM_TERRAINS] = {
    "Water", "Lava", "Sand", "Road", "Temple Light", "Temple Dark", "Straw", "Rock"};

/* Terrain file names */
static const char* terrain_files[NUM_TERRAINS] = {
    "DATAS/tiles/eau_centre.bmp",
    "DATAS/tiles/lave.bmp",
    "DATAS/tiles/sable_centre.bmp",
    "DATAS/tiles/chemin_centre.bmp",
    "DATAS/tiles/temple3.bmp",
    "DATAS/tiles/temple4.bmp",
    "DATAS/tiles/paille.bmp",
    "DATAS/tiles/rocaille.bmp"};

/* Projection presets, use library defines (ISO_PROJ_CLASSIC, etc.) */
#define NUM_PROJECTIONS ISO_NUM_PROJECTIONS

/* Transition mask count alias */
#define NUM_MASKS ISO_NUM_MASKS

/* State */
static int current_proj = ISO_PROJ_CLASSIC;

static ISO_MAP* isomap = NULL;
static N_ISO_CAMERA* camera = NULL;

static ALLEGRO_BITMAP* tile_bitmaps[NUM_TERRAINS] = {NULL};
static ALLEGRO_BITMAP* transition_masks[NUM_MASKS] = {NULL};
static ALLEGRO_BITMAP* transition_tiles[NUM_TERRAINS][NUM_MASKS] = {{NULL}};

/* Dynamic screen dimensions */
static int screen_w = SCREEN_W;
static int screen_h = SCREEN_H;

/* UI state */
static int paint_terrain = TERRAIN_TEMPLE3;
static int paint_height = 0;
static bool height_mode = false;
static bool show_grid = false;
static bool running = true;
static bool redraw_needed = true;
static bool smooth_height = false;
static int smooth_slope_max = 1;

/* Player state: free movement with continuous float position */
static bool player_mode = false;     /* true = player control, false = editor */
static float player_fx = 10.5f;      /* continuous map X (tile center = int + 0.5) */
static float player_fy = 10.5f;      /* continuous map Y */
static int player_mx = 10;           /* current tile X (derived) */
static int player_my = 10;           /* current tile Y (derived) */
static float player_screen_x = 0.0f; /* derived screen position */
static float player_screen_y = 0.0f;
static float player_z = 0.0f;        /* absolute height position (height units) */
static float player_vz = 0.0f;       /* vertical velocity (height units/sec) */
static bool player_on_ground = true; /* true when touching the ground */

/* Dead Reckoning ghost: simulated remote player on circular path */
static bool ghost_enabled = false;
static DR_ENTITY* ghost_dr = NULL;
static float ghost_screen_x = 0.0f;
static float ghost_screen_y = 0.0f;
static float ghost_true_x = 0.0f;
static float ghost_true_y = 0.0f;
static float ghost_path_cx = 10.0f;
static float ghost_path_cy = 10.0f;
static float ghost_path_radius = 6.0f;
static float ghost_path_speed = 0.4f;
static float ghost_path_angle = 0.0f;
static double ghost_last_send = 0.0;
static double ghost_send_interval = 0.25;

/* Mouse hover tile */
static int hover_mx = -1;
static int hover_my = -1;
static int last_mouse_x = 0;
static int last_mouse_y = 0;

/* GUI context and widget IDs */
static N_GUI_CTX* gui_ctx = NULL;
static int gui_win_modes = -1;
static int gui_win_tiles = -1;
static int gui_win_proj = -1;
static int gui_win_info = -1;
static int gui_btn_player = -1;
static int gui_btn_height = -1;
static int gui_btn_grid = -1;
static int gui_btn_smooth = -1;
static int gui_btn_ghost = -1;
static int gui_btn_reset = -1;
static int gui_lbl_height = -1;
static int gui_sld_height = -1;
static int gui_lst_tiles = -1;
static int gui_lbl_slope = -1;
static int gui_sld_slope = -1;
static int gui_btn_proj[NUM_PROJECTIONS] = {-1, -1, -1, -1};
static int gui_lbl_status = -1;
static int gui_lbl_proj = -1;
static int gui_lbl_hover = -1;

/* Ghost dead reckoning GUI */
static int gui_win_ghost_dr = -1;
static int gui_lbl_dr_algo = -1;
static int gui_rad_dr_algo = -1;
static int gui_lbl_dr_blend = -1;
static int gui_rad_dr_blend = -1;
static int gui_lbl_dr_interval = -1;
static int gui_sld_dr_interval = -1;
static int gui_lbl_dr_blend_time = -1;
static int gui_sld_dr_blend_time = -1;
static int gui_lbl_dr_threshold = -1;
static int gui_sld_dr_threshold = -1;

/* A* pathfinding for click-to-move */
static ASTAR_PATH* player_path = NULL;
static int player_path_idx = 0;
static float player_path_progress = 0.0f;
/* Exact fractional click destination within the final tile */
static float player_click_fx = 0.0f;
static float player_click_fy = 0.0f;

#define MAP_FILE "iso_gui_map.isom"

/* Diamond helpers, coordinate conversion, projection interpolation,
 * transition mask/tile generation, and diamond masking are all provided
 * by the nilorea-library n_iso_engine API. */

/* Bilinear height interpolation with cliff-clamping */
static float interpolate_height_at(float fx, float fy) {
    if (fx < 0.0f) fx = 0.0f;
    if (fy < 0.0f) fy = 0.0f;
    if (fx > (float)MAP_W - 1e-4f) fx = (float)MAP_W - 1e-4f;
    if (fy > (float)MAP_H - 1e-4f) fy = (float)MAP_H - 1e-4f;
    return iso_map_interpolate_height(isomap, fx, fy);
}

/* Collision-aware height interpolation: clamp neighbor tile heights so the
 * bilinear blend does not pull the player upward into wall tiles.
 * The standard interpolation samples (ix,iy), (ix+1,iy), (ix,iy+1),
 * (ix+1,iy+1).  When one of those neighbors is a tall wall, the smooth
 * blend makes the player "climb" the wall before the tile-boundary
 * collision check fires.  Clamping each neighbor to at most
 * base_height + max_climb prevents that. */
static float interpolate_height_at_clamped(float fx, float fy, int max_climb) {
    if (fx < 0.0f) fx = 0.0f;
    if (fy < 0.0f) fy = 0.0f;
    if (fx > (float)MAP_W - 1e-4f) fx = (float)MAP_W - 1e-4f;
    if (fy > (float)MAP_H - 1e-4f) fy = (float)MAP_H - 1e-4f;

    int ix = (int)floorf(fx);
    int iy = (int)floorf(fy);

    float h00 = (float)iso_map_get_height(isomap, ix, iy);

    /* In CUT mode, return flat tile height, no interpolation */
    if (!smooth_height) {
        return h00;
    }

    float frac_x = fx - (float)ix;
    float frac_y = fy - (float)iy;

    /* Clamp neighbor indices to valid range for edge tiles */
    int ix1 = (ix + 1 < MAP_W) ? ix + 1 : ix;
    int iy1 = (iy + 1 < MAP_H) ? iy + 1 : iy;

    float h10 = (float)iso_map_get_height(isomap, ix1, iy);
    float h01 = (float)iso_map_get_height(isomap, ix, iy1);
    float h11 = (float)iso_map_get_height(isomap, ix1, iy1);

    float max_h = h00 + (float)max_climb;
    if (h10 > max_h) h10 = max_h;
    if (h01 > max_h) h01 = max_h;
    if (h11 > max_h) h11 = max_h;

    float top = h00 + (h10 - h00) * frac_x;
    float bot = h01 + (h11 - h01) * frac_x;
    return top + (bot - top) * frac_y;
}

/* Object draw callbacks for N_ISO_OBJECT depth-sorted rendering */

/* Player draw callback: green circle with shadow */
static void draw_player_object(float sx, float sy, float zoom, float alpha, void* user_data) {
    (void)user_data;
    al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
    float cy = sy - 8.0f * zoom; /* offset up from feet */

    /* Shadow at ground level (only for normal draw, not overlay) */
    if (alpha >= 1.0f) {
        float ground_h = interpolate_height_at_clamped(player_fx, player_fy, PLAYER_MAX_HEIGH_DIFF);
        float gs_sx, gs_sy;
        iso_map_to_screen_f(isomap, player_fx, player_fy, ground_h, &gs_sx, &gs_sy);
        float shadow_cx, shadow_cy;
        n_iso_camera_world_to_screen(camera, gs_sx, gs_sy, &shadow_cx, &shadow_cy);
        al_draw_filled_ellipse(sx, shadow_cy, 4.0f * zoom, 2.0f * zoom,
                               al_map_rgba(0, 0, 0, 60));
    }

    al_draw_filled_circle(sx, cy, 5.0f * zoom,
                          al_map_rgba(50, 200, 50, (unsigned char)(220.0f * alpha)));
    al_draw_circle(sx, cy, 5.0f * zoom,
                   al_map_rgba(255, 255, 255, (unsigned char)(200.0f * alpha)), 1.5f);
}

/* Ghost draw callback: blue circle (DR position) + red outline (true position) */
static void draw_ghost_object(float sx, float sy, float zoom, float alpha, void* user_data) {
    (void)user_data;
    al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
    float cy = sy - 8.0f * zoom;

    /* DR-computed position (blue) */
    al_draw_filled_circle(sx, cy, 5.0f * zoom,
                          al_map_rgba(80, 120, 255, (unsigned char)(220.0f * alpha)));
    al_draw_circle(sx, cy, 5.0f * zoom,
                   al_map_rgba(200, 200, 255, (unsigned char)(200.0f * alpha)), 1.5f);
    if (alpha >= 1.0f) {
        al_draw_filled_ellipse(sx, cy + 8.0f * zoom, 4.0f * zoom, 2.0f * zoom,
                               al_map_rgba(0, 0, 0, 60));

        /* True position: red outline (only in normal draw) */
        float tcx, tcy;
        n_iso_camera_world_to_screen(camera, ghost_true_x, ghost_true_y, &tcx, &tcy);
        tcy -= 8.0f * zoom;
        al_draw_circle(tcx, tcy, 5.0f * zoom, al_map_rgba(255, 80, 80, 180), 1.5f);
        al_draw_filled_ellipse(tcx, tcy + 8.0f * zoom, 4.0f * zoom, 2.0f * zoom,
                               al_map_rgba(255, 0, 0, 30));
    }
}

/* Randomize the map */
static void randomize_map(void) {
    srand((unsigned)time(NULL));

    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            iso_map_set_terrain(isomap, x, y, TERRAIN_LAVE);
            iso_map_set_height(isomap, x, y, 0);
        }

    for (int patch = 0; patch < 15; patch++) {
        int t = rand() % NUM_TERRAINS;
        int cx = rand() % MAP_W;
        int cy = rand() % MAP_H;
        int radius = 2 + rand() % 4;
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                float dist = sqrtf((float)(dx * dx + dy * dy));
                if (dist <= radius && (rand() % 100) < (int)(100.0f * (1.0f - dist / (float)(radius + 1))))
                    iso_map_set_terrain(isomap, x, y, t);
            }
    }

    for (int hill = 0; hill < 8; hill++) {
        int peak_h = 1 + rand() % MAX_HEIGHT;
        int cx = rand() % MAP_W;
        int cy = rand() % MAP_H;
        int radius = 2 + rand() % 3;
        for (int dy = -radius; dy <= radius; dy++)
            for (int dx = -radius; dx <= radius; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                float dist = sqrtf((float)(dx * dx + dy * dy));
                if (dist > radius) continue;
                int h = (int)((float)peak_h * (1.0f - dist / (float)(radius + 1)) + 0.5f);
                if (h > iso_map_get_height(isomap, x, y))
                    iso_map_set_height(isomap, x, y, h);
            }
    }
}

/* Button callbacks for n_gui_button_set_keycode bindings */

/* Toggle player mode (P key) */
static void on_player_toggle(int id, void* data) {
    (void)id;
    (void)data;
    player_mode = n_gui_button_is_toggled(gui_ctx, gui_btn_player);
    if (player_mode) {
        if (height_mode) {
            height_mode = false;
            n_gui_button_set_toggled(gui_ctx, gui_btn_height, 0);
        }
        int drop_max_h = 0;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) {
                int h = iso_map_get_height(isomap, x, y);
                if (h > drop_max_h) drop_max_h = h;
            }
        player_z = (float)(drop_max_h + 1);
        player_vz = 0.0f;
        player_on_ground = false;
        iso_map_to_screen_f(isomap, player_fx, player_fy, player_z,
                            &player_screen_x, &player_screen_y);
        n_iso_camera_center_on(camera, player_screen_x, player_screen_y,
                               screen_w, screen_h);
    }
}

/* Toggle height editing mode (H key) */
static void on_height_toggle(int id, void* data) {
    (void)id;
    (void)data;
    height_mode = n_gui_button_is_toggled(gui_ctx, gui_btn_height);
    if (height_mode && player_mode) {
        player_mode = false;
        n_gui_button_set_toggled(gui_ctx, gui_btn_player, 0);
    }
}

/* Toggle grid overlay (G key) */
static void on_grid_toggle(int id, void* data) {
    (void)id;
    (void)data;
    show_grid = n_gui_button_is_toggled(gui_ctx, gui_btn_grid);
}

/* Toggle smooth height (V key) */
static void on_smooth_toggle(int id, void* data) {
    (void)id;
    (void)data;
    smooth_height = n_gui_button_is_toggled(gui_ctx, gui_btn_smooth);
}

/* Toggle ghost dead reckoning display (N key) */
static void on_ghost_toggle(int id, void* data) {
    (void)id;
    (void)data;
    ghost_enabled = n_gui_button_is_toggled(gui_ctx, gui_btn_ghost);
    if (ghost_enabled) {
        n_gui_open_window(gui_ctx, gui_win_ghost_dr);
        if (ghost_dr) {
            ghost_last_send = al_get_time();
            ghost_path_angle = 0.0f;
            int ghost_max_h = 0;
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++) {
                    int h = iso_map_get_height(isomap, x, y);
                    if (h > ghost_max_h) ghost_max_h = h;
                }
            DR_VEC3 init_pos = dr_vec3(ghost_path_cx + ghost_path_radius,
                                       ghost_path_cy, (double)(ghost_max_h + 1));
            DR_VEC3 init_vel = dr_vec3(0.0, ghost_path_radius * ghost_path_speed, 0.0);
            dr_entity_set_position(ghost_dr, &init_pos, &init_vel, NULL,
                                   al_get_time());
        }
    } else {
        n_gui_close_window(gui_ctx, gui_win_ghost_dr);
    }
}

/* reset terrain (R key) */
static void on_reset_click(int id, void* data) {
    (void)id;
    (void)data;
    randomize_map();
    iso_map_save(isomap, MAP_FILE);
}

/* Select projection (F1-F4 keys) */
static void on_proj_select(int id, void* data) {
    (void)data;
    for (int p = 0; p < NUM_PROJECTIONS; p++) {
        if (gui_btn_proj[p] == id) {
            current_proj = p;
            iso_map_set_projection_target(isomap, current_proj);
        }
        n_gui_button_set_toggled(gui_ctx, gui_btn_proj[p], (gui_btn_proj[p] == id) ? 1 : 0);
    }
}

/* Main */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    set_log_level(LOG_INFO);

    if (!al_init()) {
        n_abort("Could not init Allegro.\n");
    }
    if (!al_init_image_addon()) {
        n_abort("image addon\n");
    }
    if (!al_init_primitives_addon()) {
        n_abort("primitives addon\n");
    }
    if (!al_init_font_addon()) {
        n_abort("font addon\n");
    }
    al_init_ttf_addon();
    al_install_keyboard();
    al_install_mouse();

    al_set_new_display_flags(ALLEGRO_OPENGL | ALLEGRO_WINDOWED | ALLEGRO_RESIZABLE);
    ALLEGRO_DISPLAY* display = al_create_display(SCREEN_W, SCREEN_H);
    if (!display) {
        n_abort("Unable to create display\n");
    }
    al_set_window_title(display, "Nilorea Isometric Engine + GUI Demo");

    ALLEGRO_FONT* font = al_create_builtin_font();
    if (!font) {
        n_abort("Unable to create builtin font\n");
    }

    /* Create ISO_MAP with projection */
    isomap = iso_map_new(MAP_W, MAP_H, NUM_TERRAINS, MAX_HEIGHT);
    if (!isomap) {
        n_abort("Failed to create ISO_MAP!\n");
        return -1;
    }
    iso_map_set_projection(isomap, ISO_PROJ_CLASSIC, (float)TILE_W);
    isomap->proj.tile_lift = (float)TILE_LIFT;

    /* Create camera */
    camera = n_iso_camera_new(0.5f, 6.0f);
    if (!camera) {
        n_abort("Failed to create camera!\n");
    }
    camera->zoom = 2.0f;

    /* Initialize GUI */
    gui_ctx = n_gui_new_ctx(font);
    n_gui_set_display_size(gui_ctx, (float)SCREEN_W, (float)SCREEN_H);
    // n_gui_set_virtual_size(gui_ctx, (float)SCREEN_W, (float)SCREEN_H);

    /* Modes window */
    gui_win_modes = n_gui_add_window(gui_ctx, "Modes", 5, 5, 200, 310);
    gui_btn_player = n_gui_add_toggle_button(gui_ctx, gui_win_modes, "Player [P]",
                                             10, 10, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_player_toggle, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_player, ALLEGRO_KEY_P, 0);
    gui_btn_height = n_gui_add_toggle_button(gui_ctx, gui_win_modes, "Height [H]",
                                             10, 40, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_height_toggle, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_height, ALLEGRO_KEY_H, 0);
    gui_btn_grid = n_gui_add_toggle_button(gui_ctx, gui_win_modes, "Grid [G]",
                                           10, 70, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_grid_toggle, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_grid, ALLEGRO_KEY_G, 0);
    gui_btn_smooth = n_gui_add_toggle_button(gui_ctx, gui_win_modes, "Smooth [V]",
                                             10, 100, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_smooth_toggle, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_smooth, ALLEGRO_KEY_V, 0);
    gui_btn_ghost = n_gui_add_toggle_button(gui_ctx, gui_win_modes, "Ghost [N]",
                                            10, 130, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_ghost_toggle, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_ghost, ALLEGRO_KEY_N, 0);
    gui_lbl_height = n_gui_add_label(gui_ctx, gui_win_modes, "Height: 0",
                                     10, 165, 180, 16, N_GUI_ALIGN_LEFT);
    gui_sld_height = n_gui_add_slider(gui_ctx, gui_win_modes,
                                      10, 185, 150, 20, 0.0, (double)MAX_HEIGHT, 0.0,
                                      N_GUI_SLIDER_VALUE, NULL, NULL);
    gui_btn_reset = n_gui_add_button(gui_ctx, gui_win_modes, "Reset terrain [R]",
                                     10, 250, 180, 24, N_GUI_SHAPE_ROUNDED, on_reset_click, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_reset, ALLEGRO_KEY_R, 0);
    n_gui_set_widget_visible(gui_ctx, gui_lbl_height, 0);
    n_gui_set_widget_visible(gui_ctx, gui_sld_height, 0);

    /* Tiles window */
    gui_win_tiles = n_gui_add_window(gui_ctx, "Tiles", 5, 325, 200, 200);
    gui_lst_tiles = n_gui_add_listbox(gui_ctx, gui_win_tiles, 10, 10, 180, 100,
                                      N_GUI_SELECT_SINGLE, NULL, NULL);
    for (int t = 0; t < NUM_TERRAINS; t++)
        n_gui_listbox_add_item(gui_ctx, gui_lst_tiles, terrain_names[t]);
    n_gui_listbox_set_selected(gui_ctx, gui_lst_tiles, paint_terrain, 1);
    gui_lbl_slope = n_gui_add_label(gui_ctx, gui_win_tiles, "Slope max: 1",
                                    10, 115, 180, 16, N_GUI_ALIGN_LEFT);
    gui_sld_slope = n_gui_add_slider(gui_ctx, gui_win_tiles,
                                     10, 135, 150, 20, 1.0, (double)MAX_HEIGHT, (double)smooth_slope_max,
                                     N_GUI_SLIDER_VALUE, NULL, NULL);

    /* Projection window */
    gui_win_proj = n_gui_add_window(gui_ctx, "Projection", 5, 535, 200, 160);
    gui_btn_proj[0] = n_gui_add_toggle_button(gui_ctx, gui_win_proj, "Classic [F1]",
                                              10, 10, 180, 24, N_GUI_SHAPE_ROUNDED, 1, on_proj_select, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_proj[0], ALLEGRO_KEY_F1, 0);
    gui_btn_proj[1] = n_gui_add_toggle_button(gui_ctx, gui_win_proj, "Isometric [F2]",
                                              10, 40, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_proj_select, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_proj[1], ALLEGRO_KEY_F2, 0);
    gui_btn_proj[2] = n_gui_add_toggle_button(gui_ctx, gui_win_proj, "Military [F3]",
                                              10, 70, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_proj_select, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_proj[2], ALLEGRO_KEY_F3, 0);
    gui_btn_proj[3] = n_gui_add_toggle_button(gui_ctx, gui_win_proj, "Staggered [F4]",
                                              10, 100, 180, 24, N_GUI_SHAPE_ROUNDED, 0, on_proj_select, NULL);
    n_gui_button_set_keycode(gui_ctx, gui_btn_proj[3], ALLEGRO_KEY_F4, 0);

    /* Info window */
    gui_win_info = n_gui_add_window(gui_ctx, "Info", 210, 5, 420, 90);
    gui_lbl_status = n_gui_add_label(gui_ctx, gui_win_info, "Ready", 10, 5, 400, 16, N_GUI_ALIGN_LEFT);
    gui_lbl_proj = n_gui_add_label(gui_ctx, gui_win_info, "Projection: Classic 2:1", 10, 22, 400, 16, N_GUI_ALIGN_LEFT);
    gui_lbl_hover = n_gui_add_label(gui_ctx, gui_win_info, "", 10, 39, 400, 16, N_GUI_ALIGN_LEFT);

    /* Ghost dead reckoning window */
    gui_win_ghost_dr = n_gui_add_window(gui_ctx, "Ghost DR", 1070, 5, 200, 340);
    gui_lbl_dr_algo = n_gui_add_label(gui_ctx, gui_win_ghost_dr, "Algo [M]",
                                      10, 5, 180, 16, N_GUI_ALIGN_LEFT);
    gui_rad_dr_algo = n_gui_add_radiolist(gui_ctx, gui_win_ghost_dr,
                                          10, 23, 180, 66, NULL, NULL);
    n_gui_radiolist_add_item(gui_ctx, gui_rad_dr_algo, "Static");
    n_gui_radiolist_add_item(gui_ctx, gui_rad_dr_algo, "Velocity");
    n_gui_radiolist_add_item(gui_ctx, gui_rad_dr_algo, "Vel+Acc");
    n_gui_radiolist_set_selected(gui_ctx, gui_rad_dr_algo, DR_ALGO_VEL_ACC);

    gui_lbl_dr_blend = n_gui_add_label(gui_ctx, gui_win_ghost_dr, "Blend [B]",
                                       10, 93, 180, 16, N_GUI_ALIGN_LEFT);
    gui_rad_dr_blend = n_gui_add_radiolist(gui_ctx, gui_win_ghost_dr,
                                           10, 111, 180, 66, NULL, NULL);
    n_gui_radiolist_add_item(gui_ctx, gui_rad_dr_blend, "Snap");
    n_gui_radiolist_add_item(gui_ctx, gui_rad_dr_blend, "PVB");
    n_gui_radiolist_add_item(gui_ctx, gui_rad_dr_blend, "Cubic");
    n_gui_radiolist_set_selected(gui_ctx, gui_rad_dr_blend, DR_BLEND_PVB);

    gui_lbl_dr_interval = n_gui_add_label(gui_ctx, gui_win_ghost_dr, "Interval [,/.]: 0.25s",
                                          10, 181, 180, 16, N_GUI_ALIGN_LEFT);
    gui_sld_dr_interval = n_gui_add_slider(gui_ctx, gui_win_ghost_dr,
                                           10, 199, 180, 20, 0.05, 2.0, ghost_send_interval,
                                           N_GUI_SLIDER_VALUE, NULL, NULL);

    gui_lbl_dr_blend_time = n_gui_add_label(gui_ctx, gui_win_ghost_dr, "Blend time: 0.20s",
                                            10, 225, 180, 16, N_GUI_ALIGN_LEFT);
    gui_sld_dr_blend_time = n_gui_add_slider(gui_ctx, gui_win_ghost_dr,
                                             10, 243, 180, 20, 0.01, 2.0, 0.2,
                                             N_GUI_SLIDER_VALUE, NULL, NULL);

    gui_lbl_dr_threshold = n_gui_add_label(gui_ctx, gui_win_ghost_dr, "Threshold: 0.50",
                                           10, 269, 180, 16, N_GUI_ALIGN_LEFT);
    gui_sld_dr_threshold = n_gui_add_slider(gui_ctx, gui_win_ghost_dr,
                                            10, 287, 180, 20, 0.01, 5.0, 0.5,
                                            N_GUI_SLIDER_VALUE, NULL, NULL);

    /* Start hidden; shown when ghost is enabled */
    n_gui_close_window(gui_ctx, gui_win_ghost_dr);

    /* Load tile images */
    int saved_bmp_flags = al_get_new_bitmap_flags();
    al_set_new_bitmap_flags(saved_bmp_flags & ~(ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR));
    for (int i = 0; i < NUM_TERRAINS; i++) {
        tile_bitmaps[i] = al_load_bitmap(terrain_files[i]);
        if (!tile_bitmaps[i]) {
            n_log(LOG_ERR, "Failed to load tile: %s", terrain_files[i]);
            return EXIT_FAILURE;
        }
        iso_mask_tile_to_diamond(tile_bitmaps[i], TILE_W, TILE_H);
    }

    /* Generate transition data (nilorea-library) */
    iso_generate_transition_masks(transition_masks, TILE_W, TILE_H);
    {
        ALLEGRO_BITMAP** trans_ptrs[NUM_TERRAINS];
        for (int t = 0; t < NUM_TERRAINS; t++)
            trans_ptrs[t] = transition_tiles[t];
        iso_generate_transition_tiles(trans_ptrs, transition_masks, tile_bitmaps,
                                      NUM_TERRAINS, TILE_W, TILE_H);
    }
    al_set_new_bitmap_flags(saved_bmp_flags);

    /* Initialize map: try to load from file, else randomize */
    {
        ISO_MAP* loaded = iso_map_load(MAP_FILE);
        if (loaded) {
            for (int y = 0; y < MAP_H && y < loaded->height; y++)
                for (int x = 0; x < MAP_W && x < loaded->width; x++) {
                    iso_map_set_terrain(isomap, x, y, iso_map_get_terrain(loaded, x, y));
                    iso_map_set_height(isomap, x, y, iso_map_get_height(loaded, x, y));
                }
            iso_map_free(&loaded);
        } else {
            randomize_map();
            iso_map_save(isomap, MAP_FILE);
        }
    }

    /* Center camera */
    {
        float sx_center, sy_center;
        iso_map_to_screen(isomap, MAP_W / 2, MAP_H / 2, 0, &sx_center, &sy_center);
        n_iso_camera_center_on(camera, sx_center, sy_center, screen_w, screen_h);
    }

    /* Initialize dead reckoning ghost entity */
    ghost_dr = dr_entity_create(DR_ALGO_VEL_ACC, DR_BLEND_PVB, 0.5, 0.2);
    if (ghost_dr) {
        DR_VEC3 init_pos = dr_vec3(ghost_path_cx + ghost_path_radius, ghost_path_cy, 0.0);
        dr_entity_set_position(ghost_dr, &init_pos, NULL, NULL, 0.0);
    }

    /* Event queue and timer */
    ALLEGRO_TIMER* timer = al_create_timer(1.0 / FPS);
    ALLEGRO_EVENT_QUEUE* queue = al_create_event_queue();
    al_register_event_source(queue, al_get_display_event_source(display));
    al_register_event_source(queue, al_get_timer_event_source(timer));
    al_register_event_source(queue, al_get_keyboard_event_source());
    al_register_event_source(queue, al_get_mouse_event_source());

    al_start_timer(timer);

    bool key_up = false, key_down = false, key_left = false, key_right = false;
    bool key_space = false;
    float scroll_speed = 4.0f;
    float dt = 1.0f / (float)FPS;

    /* Initialize player position */
    {
        float interp_h = interpolate_height_at_clamped(player_fx, player_fy, PLAYER_MAX_HEIGH_DIFF);
        player_z = interp_h;
        iso_map_to_screen_f(isomap, player_fx, player_fy, player_z, &player_screen_x, &player_screen_y);
    }

    /* Main loop */
    while (running) {
        ALLEGRO_EVENT ev;
        al_wait_for_event(queue, &ev);

        /* pass events to the GUI.
         * n_gui_process_event returns 1 when the event was consumed by GUI.
         * n_gui_wants_mouse() is also checked below before game mouse actions. */
        int gui_consumed = n_gui_process_event(gui_ctx, ev);
        (void)gui_consumed;

        switch (ev.type) {
            case ALLEGRO_EVENT_TIMER: {
                /* Smooth projection transition */
                iso_map_lerp_projection(isomap, dt);

                /* A* path following */
                if (player_mode && player_path && player_path_idx < player_path->length) {
                    /* For the last node, target the exact click position;
                     * for intermediate nodes, target tile centers. */
                    float target_fx, target_fy;
                    int is_last_node = (player_path_idx == player_path->length - 1);
                    if (is_last_node) {
                        target_fx = player_click_fx;
                        target_fy = player_click_fy;
                    } else {
                        target_fx = (float)player_path->nodes[player_path_idx].x + 0.5f;
                        target_fy = (float)player_path->nodes[player_path_idx].y + 0.5f;
                    }
                    float path_dx = target_fx - player_fx;
                    float path_dy = target_fy - player_fy;
                    float path_dist = sqrtf(path_dx * path_dx + path_dy * path_dy);
                    float step = PLAYER_MOVE_SPEED * dt;
                    if (path_dist <= step) {
                        player_fx = target_fx;
                        player_fy = target_fy;
                        player_mx = player_path->nodes[player_path_idx].x;
                        player_my = player_path->nodes[player_path_idx].y;
                        player_path_idx++;
                        if (player_path_idx >= player_path->length) {
                            n_astar_path_free(player_path);
                            player_path = NULL;
                        }
                    } else {
                        player_fx += (path_dx / path_dist) * step;
                        player_fy += (path_dy / path_dist) * step;
                        player_mx = (int)floorf(player_fx);
                        player_my = (int)floorf(player_fy);
                    }
                }

                /* Player free movement update */
                if (player_mode) {
                    float move_dx = 0.0f, move_dy = 0.0f;
                    if (key_up) move_dy -= 1.0f;
                    if (key_down) move_dy += 1.0f;
                    if (key_left) move_dx -= 1.0f;
                    if (key_right) move_dx += 1.0f;

                    if (move_dx != 0.0f && move_dy != 0.0f) {
                        move_dx *= 0.70710678f;
                        move_dy *= 0.70710678f;
                    }

                    /* Jump */
                    if (key_space && player_on_ground) {
                        player_vz = JUMP_VELOCITY;
                        player_on_ground = false;
                    }

                    float ground_h = interpolate_height_at_clamped(player_fx, player_fy, PLAYER_MAX_HEIGH_DIFF);

                    /* Vertical physics */
                    if (!player_on_ground) {
                        player_vz -= JUMP_GRAVITY * dt;
                        player_z += player_vz * dt;
                        if (player_z <= ground_h) {
                            player_z = ground_h;
                            player_vz = 0.0f;
                            player_on_ground = true;
                        }
                    } else {
                        if (player_z - ground_h > 1.0f) {
                            player_on_ground = false;
                            player_vz = 0.0f;
                        } else {
                            player_z = ground_h;
                        }
                    }

                    /* Horizontal movement with collision */
                    if (move_dx != 0.0f || move_dy != 0.0f) {
                        float speed = PLAYER_MOVE_SPEED * dt;
                        float new_fx = player_fx + move_dx * speed;
                        float new_fy = player_fy + move_dy * speed;

                        if (new_fx < 0.1f) new_fx = 0.1f;
                        if (new_fx > (float)MAP_W - 0.1f) new_fx = (float)MAP_W - 0.1f;
                        if (new_fy < 0.1f) new_fy = 0.1f;
                        if (new_fy > (float)MAP_H - 0.1f) new_fy = (float)MAP_H - 0.1f;

                        int new_tile_x = (int)floorf(new_fx);
                        int new_tile_y = (int)floorf(new_fy);

                        bool can_move = true;
                        if (new_tile_x != player_mx || new_tile_y != player_my) {
                            if (new_tile_x >= 0 && new_tile_x < MAP_W &&
                                new_tile_y >= 0 && new_tile_y < MAP_H) {
                                int target_h = iso_map_get_height(isomap, new_tile_x, new_tile_y);
                                int current_h = iso_map_get_height(isomap, player_mx, player_my);
                                if (player_on_ground) {
                                    if (target_h - current_h > PLAYER_MAX_HEIGH_DIFF)
                                        can_move = false;
                                } else {
                                    if ((float)target_h > player_z + 0.5f)
                                        can_move = false;
                                }
                            } else {
                                can_move = false;
                            }
                        }

                        if (can_move) {
                            player_fx = new_fx;
                            player_fy = new_fy;
                            player_mx = new_tile_x;
                            player_my = new_tile_y;
                        } else {
                            /* Wall sliding: try each axis independently */
                            float slide_fx = player_fx + move_dx * speed;
                            if (slide_fx < 0.1f) slide_fx = 0.1f;
                            if (slide_fx > (float)MAP_W - 0.1f) slide_fx = (float)MAP_W - 0.1f;
                            int slide_tx = (int)floorf(slide_fx);
                            bool can_x = true;
                            if (slide_tx != player_mx) {
                                if (slide_tx >= 0 && slide_tx < MAP_W) {
                                    int th = iso_map_get_height(isomap, slide_tx, player_my);
                                    int ch = iso_map_get_height(isomap, player_mx, player_my);
                                    if (player_on_ground) {
                                        if (th - ch > PLAYER_MAX_HEIGH_DIFF) can_x = false;
                                    } else {
                                        if ((float)th > player_z + 0.5f) can_x = false;
                                    }
                                } else {
                                    can_x = false;
                                }
                            }
                            if (can_x) {
                                player_fx = slide_fx;
                                player_mx = slide_tx;
                            }

                            float slide_fy = player_fy + move_dy * speed;
                            if (slide_fy < 0.1f) slide_fy = 0.1f;
                            if (slide_fy > (float)MAP_H - 0.1f) slide_fy = (float)MAP_H - 0.1f;
                            int slide_ty = (int)floorf(slide_fy);
                            bool can_y = true;
                            if (slide_ty != player_my) {
                                if (slide_ty >= 0 && slide_ty < MAP_H) {
                                    int th = iso_map_get_height(isomap, player_mx, slide_ty);
                                    int ch = iso_map_get_height(isomap, player_mx, player_my);
                                    if (player_on_ground) {
                                        if (th - ch > PLAYER_MAX_HEIGH_DIFF) can_y = false;
                                    } else {
                                        if ((float)th > player_z + 0.5f) can_y = false;
                                    }
                                } else {
                                    can_y = false;
                                }
                            }
                            if (can_y) {
                                player_fy = slide_fy;
                                player_my = slide_ty;
                            }
                        }
                    }

                    /* Recompute ground height after movement */
                    ground_h = interpolate_height_at_clamped(player_fx, player_fy, PLAYER_MAX_HEIGH_DIFF);
                    if (player_on_ground) {
                        if (player_z - ground_h > 1.0f) {
                            player_on_ground = false;
                            player_vz = 0.0f;
                        } else {
                            player_z = ground_h;
                        }
                    } else if (player_z <= ground_h) {
                        player_z = ground_h;
                        player_vz = 0.0f;
                        player_on_ground = true;
                    }

                    /* Update screen position */
                    iso_map_to_screen_f(isomap, player_fx, player_fy, player_z, &player_screen_x, &player_screen_y);
                }

                /* Camera: smoothly follow player in player mode */
                if (player_mode) {
                    n_iso_camera_follow(camera, player_screen_x, player_screen_y,
                                        screen_w, screen_h, CAM_SMOOTHING, dt);
                }

                /* Editor mode: scroll camera */
                if (!player_mode) {
                    float sdx = 0.0f, sdy = 0.0f;
                    if (key_left) sdx += scroll_speed / camera->zoom;
                    if (key_right) sdx -= scroll_speed / camera->zoom;
                    if (key_up) sdy += scroll_speed / camera->zoom;
                    if (key_down) sdy -= scroll_speed / camera->zoom;
                    if (sdx != 0.0f || sdy != 0.0f)
                        n_iso_camera_scroll(camera, sdx, sdy);
                }

                /* Dead reckoning ghost update */
                if (ghost_enabled && ghost_dr) {
                    double now = al_get_time();

                    ghost_path_angle += ghost_path_speed * dt;
                    if (ghost_path_angle > 2.0f * (float)M_PI)
                        ghost_path_angle -= 2.0f * (float)M_PI;

                    float true_fx = ghost_path_cx + ghost_path_radius * cosf(ghost_path_angle);
                    float true_fy = ghost_path_cy + ghost_path_radius * sinf(ghost_path_angle);

                    float true_vx = -ghost_path_radius * ghost_path_speed * sinf(ghost_path_angle);
                    float true_vy = ghost_path_radius * ghost_path_speed * cosf(ghost_path_angle);

                    float w2r = ghost_path_speed * ghost_path_speed * ghost_path_radius;
                    float true_ax = -w2r * cosf(ghost_path_angle);
                    float true_ay = -w2r * sinf(ghost_path_angle);

                    {
                        DR_VEC3 pos = dr_vec3(true_fx, true_fy, 0.0);
                        DR_VEC3 vel = dr_vec3(true_vx, true_vy, 0.0);
                        DR_VEC3 acc = dr_vec3(true_ax, true_ay, 0.0);
                        if (now - ghost_last_send >= ghost_send_interval ||
                            dr_entity_check_threshold(ghost_dr, &pos, &vel, &acc, now)) {
                            dr_entity_receive_state(ghost_dr, &pos, &vel, &acc, now);
                            ghost_last_send = now;
                        }
                    }

                    DR_VEC3 dr_pos;
                    dr_entity_compute(ghost_dr, now, &dr_pos);

                    float dr_h = interpolate_height_at((float)dr_pos.x, (float)dr_pos.y);
                    iso_map_to_screen_f(isomap, (float)dr_pos.x, (float)dr_pos.y, dr_h,
                                        &ghost_screen_x, &ghost_screen_y);

                    float true_h = interpolate_height_at(true_fx, true_fy);
                    iso_map_to_screen_f(isomap, true_fx, true_fy, true_h,
                                        &ghost_true_x, &ghost_true_y);
                }

                /* Update hover tile (skip when mouse is over a GUI window) */
                if (!n_gui_wants_mouse(gui_ctx)) {
                    float wx, wy;
                    n_iso_camera_screen_to_world(camera, (float)last_mouse_x,
                                                 (float)last_mouse_y, &wx, &wy);
                    iso_screen_to_map_height(isomap, wx, wy, TILE_W, TILE_H,
                                             &hover_mx, &hover_my);
                } else {
                    hover_mx = -1;
                    hover_my = -1;
                }

                /* GUI <-> variable sync */
                {
                    bool gui_player = n_gui_button_is_toggled(gui_ctx, gui_btn_player) != 0;
                    bool gui_height = n_gui_button_is_toggled(gui_ctx, gui_btn_height) != 0;
                    bool gui_grid = n_gui_button_is_toggled(gui_ctx, gui_btn_grid) != 0;
                    bool gui_smooth = n_gui_button_is_toggled(gui_ctx, gui_btn_smooth) != 0;
                    bool gui_ghost = n_gui_button_is_toggled(gui_ctx, gui_btn_ghost) != 0;

                    if (gui_player != player_mode) {
                        player_mode = gui_player;
                        if (player_mode) {
                            int drop_max_h = 0;
                            for (int y = 0; y < MAP_H; y++)
                                for (int x = 0; x < MAP_W; x++) {
                                    int h = iso_map_get_height(isomap, x, y);
                                    if (h > drop_max_h) drop_max_h = h;
                                }
                            player_z = (float)(drop_max_h + 1);
                            player_vz = 0.0f;
                            player_on_ground = false;
                            iso_map_to_screen_f(isomap, player_fx, player_fy, player_z,
                                                &player_screen_x, &player_screen_y);
                            n_iso_camera_center_on(camera, player_screen_x, player_screen_y,
                                                   screen_w, screen_h);
                        }
                    }
                    if (gui_height != height_mode) {
                        height_mode = gui_height;
                        if (height_mode && player_mode)
                            player_mode = false;
                    }
                    if (gui_grid != show_grid) show_grid = gui_grid;
                    if (gui_smooth != smooth_height) smooth_height = gui_smooth;
                    if (gui_ghost != ghost_enabled) {
                        ghost_enabled = gui_ghost;
                        if (ghost_enabled) {
                            n_gui_open_window(gui_ctx, gui_win_ghost_dr);
                            if (ghost_dr) {
                                ghost_last_send = al_get_time();
                                ghost_path_angle = 0.0f;
                                int ghost_max_h = 0;
                                for (int y = 0; y < MAP_H; y++)
                                    for (int x = 0; x < MAP_W; x++) {
                                        int h = iso_map_get_height(isomap, x, y);
                                        if (h > ghost_max_h) ghost_max_h = h;
                                    }
                                DR_VEC3 init_pos = dr_vec3(ghost_path_cx + ghost_path_radius,
                                                           ghost_path_cy, (double)(ghost_max_h + 1));
                                DR_VEC3 init_vel = dr_vec3(0.0, ghost_path_radius * ghost_path_speed, 0.0);
                                dr_entity_set_position(ghost_dr, &init_pos, &init_vel, NULL,
                                                       al_get_time());
                            }
                        } else {
                            n_gui_close_window(gui_ctx, gui_win_ghost_dr);
                        }
                    }

                    /* Ghost DR window sync */
                    if (ghost_enabled && ghost_dr) {
                        /* Algorithm radiolist */
                        int gui_algo = n_gui_radiolist_get_selected(gui_ctx, gui_rad_dr_algo);
                        if (gui_algo >= 0 && gui_algo != (int)ghost_dr->algo)
                            dr_entity_set_algo(ghost_dr, (DR_ALGO)gui_algo);
                        n_gui_radiolist_set_selected(gui_ctx, gui_rad_dr_algo, (int)ghost_dr->algo);

                        /* Blend radiolist */
                        int gui_blend = n_gui_radiolist_get_selected(gui_ctx, gui_rad_dr_blend);
                        if (gui_blend >= 0 && gui_blend != (int)ghost_dr->blend_mode)
                            dr_entity_set_blend_mode(ghost_dr, (DR_BLEND)gui_blend);
                        n_gui_radiolist_set_selected(gui_ctx, gui_rad_dr_blend, (int)ghost_dr->blend_mode);

                        /* Send interval slider */
                        {
                            double gui_interval = n_gui_slider_get_value(gui_ctx, gui_sld_dr_interval);
                            if (fabs(gui_interval - ghost_send_interval) > 0.001)
                                ghost_send_interval = gui_interval;
                            n_gui_slider_set_value(gui_ctx, gui_sld_dr_interval, ghost_send_interval);
                            char ibuf[48];
                            snprintf(ibuf, sizeof(ibuf), "Interval [,/.]: %.2fs", ghost_send_interval);
                            n_gui_label_set_text(gui_ctx, gui_lbl_dr_interval, ibuf);
                        }

                        /* Blend time slider */
                        {
                            double gui_bt = n_gui_slider_get_value(gui_ctx, gui_sld_dr_blend_time);
                            if (fabs(gui_bt - ghost_dr->blend_time) > 0.001)
                                dr_entity_set_blend_time(ghost_dr, gui_bt);
                            n_gui_slider_set_value(gui_ctx, gui_sld_dr_blend_time, ghost_dr->blend_time);
                            char btbuf[48];
                            snprintf(btbuf, sizeof(btbuf), "Blend time: %.2fs", ghost_dr->blend_time);
                            n_gui_label_set_text(gui_ctx, gui_lbl_dr_blend_time, btbuf);
                        }

                        /* Threshold slider */
                        {
                            double gui_th = n_gui_slider_get_value(gui_ctx, gui_sld_dr_threshold);
                            if (fabs(gui_th - ghost_dr->pos_threshold) > 0.001)
                                dr_entity_set_threshold(ghost_dr, gui_th);
                            n_gui_slider_set_value(gui_ctx, gui_sld_dr_threshold, ghost_dr->pos_threshold);
                            char thbuf[48];
                            snprintf(thbuf, sizeof(thbuf), "Threshold: %.2f", ghost_dr->pos_threshold);
                            n_gui_label_set_text(gui_ctx, gui_lbl_dr_threshold, thbuf);
                        }
                    }

                    /* Height slider */
                    {
                        int gui_h = (int)(n_gui_slider_get_value(gui_ctx, gui_sld_height) + 0.5);
                        if (gui_h != paint_height) paint_height = gui_h;
                        n_gui_slider_set_value(gui_ctx, gui_sld_height, (double)paint_height);
                        char hbuf[32];
                        snprintf(hbuf, sizeof(hbuf), "Height: %d", paint_height);
                        n_gui_label_set_text(gui_ctx, gui_lbl_height, hbuf);
                        n_gui_set_widget_visible(gui_ctx, gui_lbl_height, height_mode ? 1 : 0);
                        n_gui_set_widget_visible(gui_ctx, gui_sld_height, height_mode ? 1 : 0);
                    }

                    /* Tile listbox */
                    {
                        int gui_sel = n_gui_listbox_get_selected(gui_ctx, gui_lst_tiles);
                        if (gui_sel >= 0 && gui_sel < NUM_TERRAINS && gui_sel != paint_terrain)
                            paint_terrain = gui_sel;
                        n_gui_listbox_set_selected(gui_ctx, gui_lst_tiles, paint_terrain, 1);
                    }

                    /* Slope slider */
                    {
                        int gui_slope = (int)(n_gui_slider_get_value(gui_ctx, gui_sld_slope) + 0.5);
                        if (gui_slope != smooth_slope_max) smooth_slope_max = gui_slope;
                        n_gui_slider_set_value(gui_ctx, gui_sld_slope, (double)smooth_slope_max);
                        char sbuf2[32];
                        snprintf(sbuf2, sizeof(sbuf2), "Slope max: %d", smooth_slope_max);
                        n_gui_label_set_text(gui_ctx, gui_lbl_slope, sbuf2);
                    }

                    /* Sync variables -> GUI */
                    n_gui_button_set_toggled(gui_ctx, gui_btn_player, player_mode ? 1 : 0);
                    n_gui_button_set_toggled(gui_ctx, gui_btn_height, height_mode ? 1 : 0);
                    n_gui_button_set_toggled(gui_ctx, gui_btn_grid, show_grid ? 1 : 0);
                    n_gui_button_set_toggled(gui_ctx, gui_btn_smooth, smooth_height ? 1 : 0);
                    n_gui_button_set_toggled(gui_ctx, gui_btn_ghost, ghost_enabled ? 1 : 0);

                    /* Projection radio group */
                    for (int p = 0; p < NUM_PROJECTIONS; p++) {
                        if (n_gui_button_is_toggled(gui_ctx, gui_btn_proj[p]) && p != current_proj) {
                            current_proj = p;
                            iso_map_set_projection_target(isomap, current_proj);
                            break;
                        }
                    }
                    for (int p = 0; p < NUM_PROJECTIONS; p++)
                        n_gui_button_set_toggled(gui_ctx, gui_btn_proj[p], (p == current_proj) ? 1 : 0);
                }

                redraw_needed = true;
                break;
            }

            case ALLEGRO_EVENT_KEY_DOWN:
                switch (ev.keyboard.keycode) {
                    /* Movement keys (not GUI buttons) */
                    case ALLEGRO_KEY_LEFT:
                    case ALLEGRO_KEY_A:
                        key_left = true;
                        break;
                    case ALLEGRO_KEY_RIGHT:
                    case ALLEGRO_KEY_D:
                        key_right = true;
                        break;
                    case ALLEGRO_KEY_UP:
                    case ALLEGRO_KEY_W:
                        key_up = true;
                        break;
                    case ALLEGRO_KEY_DOWN:
                    case ALLEGRO_KEY_S:
                        key_down = true;
                        break;
                    case ALLEGRO_KEY_SPACE:
                        key_space = true;
                        break;
                    case ALLEGRO_KEY_ESCAPE:
                        running = false;
                        break;
                    case ALLEGRO_KEY_PGDN:
                        if (smooth_slope_max > 1) smooth_slope_max--;
                        n_gui_slider_set_value(gui_ctx, gui_sld_slope, (double)smooth_slope_max);
                        break;
                    case ALLEGRO_KEY_PGUP:
                        if (smooth_slope_max < MAX_HEIGHT) smooth_slope_max++;
                        n_gui_slider_set_value(gui_ctx, gui_sld_slope, (double)smooth_slope_max);
                        break;
                    case ALLEGRO_KEY_EQUALS:
                    case ALLEGRO_KEY_PAD_PLUS:
                        if (paint_height < MAX_HEIGHT) paint_height++;
                        break;
                    case ALLEGRO_KEY_MINUS:
                    case ALLEGRO_KEY_PAD_MINUS:
                        if (paint_height > 0) paint_height--;
                        break;
                    case ALLEGRO_KEY_M:
                        if (ghost_dr) {
                            int a = (int)((ghost_dr->algo + 1) % 3);
                            dr_entity_set_algo(ghost_dr, (DR_ALGO)a);
                            n_gui_radiolist_set_selected(gui_ctx, gui_rad_dr_algo, a);
                        }
                        break;
                    case ALLEGRO_KEY_B:
                        if (ghost_dr) {
                            int b = (int)((ghost_dr->blend_mode + 1) % 3);
                            dr_entity_set_blend_mode(ghost_dr, (DR_BLEND)b);
                            n_gui_radiolist_set_selected(gui_ctx, gui_rad_dr_blend, b);
                        }
                        break;
                    case ALLEGRO_KEY_COMMA:
                        ghost_send_interval += 0.05;
                        if (ghost_send_interval > 2.0) ghost_send_interval = 2.0;
                        n_gui_slider_set_value(gui_ctx, gui_sld_dr_interval, ghost_send_interval);
                        break;
                    case ALLEGRO_KEY_FULLSTOP:
                        ghost_send_interval -= 0.05;
                        if (ghost_send_interval < 0.05) ghost_send_interval = 0.05;
                        n_gui_slider_set_value(gui_ctx, gui_sld_dr_interval, ghost_send_interval);
                        break;
                        /* Keys P, H, G, V, N, F1-F4 are handled via
                           n_gui_button_set_keycode bindings */
                }
                break;

            case ALLEGRO_EVENT_KEY_UP:
                switch (ev.keyboard.keycode) {
                    case ALLEGRO_KEY_LEFT:
                    case ALLEGRO_KEY_A:
                        key_left = false;
                        break;
                    case ALLEGRO_KEY_RIGHT:
                    case ALLEGRO_KEY_D:
                        key_right = false;
                        break;
                    case ALLEGRO_KEY_UP:
                    case ALLEGRO_KEY_W:
                        key_up = false;
                        break;
                    case ALLEGRO_KEY_DOWN:
                    case ALLEGRO_KEY_S:
                        key_down = false;
                        break;
                    case ALLEGRO_KEY_SPACE:
                        key_space = false;
                        break;
                }
                break;

            case ALLEGRO_EVENT_MOUSE_AXES:
                last_mouse_x = ev.mouse.x;
                last_mouse_y = ev.mouse.y;
                if (ev.mouse.dz != 0 && !n_gui_wants_mouse(gui_ctx)) {
                    n_iso_camera_zoom(camera, (float)ev.mouse.dz * 0.25f,
                                      (float)ev.mouse.x, (float)ev.mouse.y);
                }
                break;

            case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.mouse.button == 1 && !n_gui_wants_mouse(gui_ctx)) {
                    float wx, wy;
                    n_iso_camera_screen_to_world(camera, (float)ev.mouse.x,
                                                 (float)ev.mouse.y, &wx, &wy);
                    int map_x, map_y;
                    float click_fx = 0.0f, click_fy = 0.0f;
                    iso_screen_to_map_height_f(isomap, wx, wy, TILE_W, TILE_H,
                                               &map_x, &map_y, &click_fx, &click_fy);
                    if (map_x >= 0 && map_x < MAP_W && map_y >= 0 && map_y < MAP_H) {
                        if (player_mode) {
                            /* Click-to-move via A* pathfinding */
                            if (player_path) {
                                n_astar_path_free(player_path);
                                player_path = NULL;
                            }
                            ASTAR_GRID* grid = iso_map_to_astar_grid(isomap, PLAYER_MAX_HEIGH_DIFF, player_mx, player_my);
                            if (grid) {
                                int dest_x = map_x, dest_y = map_y;
                                /* If destination is blocked, find nearest walkable tile */
                                if (!n_astar_grid_get_walkable(grid, dest_x, dest_y, 0)) {
                                    int found = 0;
                                    for (int ring = 1; ring <= MAX(MAP_W, MAP_H) && !found; ring++) {
                                        for (int ry = -ring; ry <= ring && !found; ry++)
                                            for (int rx = -ring; rx <= ring && !found; rx++) {
                                                if (abs(rx) != ring && abs(ry) != ring) continue;
                                                int nx = map_x + rx, ny = map_y + ry;
                                                if (nx >= 0 && nx < MAP_W && ny >= 0 && ny < MAP_H &&
                                                    n_astar_grid_get_walkable(grid, nx, ny, 0)) {
                                                    dest_x = nx;
                                                    dest_y = ny;
                                                    found = 1;
                                                }
                                            }
                                    }
                                }
                                player_path = n_astar_find_path(grid,
                                                                player_mx, player_my, 0,
                                                                dest_x, dest_y, 0,
                                                                ASTAR_ALLOW_DIAGONAL, ASTAR_HEURISTIC_CHEBYSHEV);
                                n_astar_grid_free(grid);
                                if (player_path && player_path->length > 1) {
                                    player_path_idx = 1;
                                    player_path_progress = 0.0f;
                                    /* If dest tile matches the clicked tile, use exact
                                     * click position; otherwise center of dest tile. */
                                    int final_tx = player_path->nodes[player_path->length - 1].x;
                                    int final_ty = player_path->nodes[player_path->length - 1].y;
                                    if (final_tx == map_x && final_ty == map_y) {
                                        player_click_fx = click_fx;
                                        player_click_fy = click_fy;
                                    } else {
                                        player_click_fx = (float)final_tx + 0.5f;
                                        player_click_fy = (float)final_ty + 0.5f;
                                    }
                                    /* Clamp to valid map range for border tiles.
                                     * Allow the full extent of the last tile so the
                                     * player can walk anywhere within border tiles. */
                                    if (player_click_fx < 0.0f) player_click_fx = 0.0f;
                                    if (player_click_fy < 0.0f) player_click_fy = 0.0f;
                                    if (player_click_fx > (float)MAP_W - 1e-4f) player_click_fx = (float)MAP_W - 1e-4f;
                                    if (player_click_fy > (float)MAP_H - 1e-4f) player_click_fy = (float)MAP_H - 1e-4f;
                                } else {
                                    if (player_path) {
                                        n_astar_path_free(player_path);
                                        player_path = NULL;
                                    }
                                }
                            }
                        } else if (height_mode) {
                            iso_map_set_height(isomap, map_x, map_y, paint_height);
                            iso_map_save(isomap, MAP_FILE);
                        } else {
                            iso_map_set_terrain(isomap, map_x, map_y, paint_terrain);
                            iso_map_save(isomap, MAP_FILE);
                        }
                    }
                }
                break;

            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                running = false;
                break;

            case ALLEGRO_EVENT_DISPLAY_RESIZE:
                al_acknowledge_resize(display);
                screen_w = al_get_display_width(display);
                screen_h = al_get_display_height(display);
                n_gui_set_display_size(gui_ctx, (float)screen_w, (float)screen_h);
                break;
        }

        /* Render */
        if (redraw_needed && al_is_event_queue_empty(queue)) {
            redraw_needed = false;
            al_set_target_backbuffer(display);
            al_clear_to_color(al_map_rgb(20, 25, 30));

            /* Sync ISO_MAP rendering flags */
            isomap->smooth_height = smooth_height ? 1 : 0;
            isomap->smooth_slope_max = smooth_slope_max;
            isomap->show_grid = show_grid ? 1 : 0;
            isomap->hover_mx = hover_mx;
            isomap->hover_my = hover_my;

            /* Build object list for depth-sorted rendering */
            N_ISO_OBJECT iso_objects[2];
            int iso_num_objects = 0;

            if (player_mode) {
                iso_objects[iso_num_objects].fx = player_fx;
                iso_objects[iso_num_objects].fy = player_fy;
                iso_objects[iso_num_objects].fz = player_z;
                iso_objects[iso_num_objects].occluded_alpha = 0.35f;
                iso_objects[iso_num_objects].is_occluded = 0;
                iso_objects[iso_num_objects].draw = draw_player_object;
                iso_objects[iso_num_objects].user_data = NULL;
                iso_num_objects++;
            }

            if (ghost_enabled && ghost_dr) {
                /* ghost uses the DR-computed screen pos; derive map pos */
                DR_VEC3 dr_pos;
                dr_entity_compute(ghost_dr, al_get_time(), &dr_pos);
                float dr_h = interpolate_height_at((float)dr_pos.x, (float)dr_pos.y);
                iso_objects[iso_num_objects].fx = (float)dr_pos.x;
                iso_objects[iso_num_objects].fy = (float)dr_pos.y;
                iso_objects[iso_num_objects].fz = dr_h;
                iso_objects[iso_num_objects].occluded_alpha = 0.35f;
                iso_objects[iso_num_objects].is_occluded = 0;
                iso_objects[iso_num_objects].draw = draw_ghost_object;
                iso_objects[iso_num_objects].user_data = NULL;
                iso_num_objects++;
            }

            /* Draw map with depth-sorted objects */
            {
                ALLEGRO_BITMAP** trans_ptrs[NUM_TERRAINS];
                for (int t = 0; t < NUM_TERRAINS; t++)
                    trans_ptrs[t] = transition_tiles[t];
                iso_map_draw(isomap, tile_bitmaps, trans_ptrs, NUM_MASKS,
                             NULL, 0,
                             floorf(camera->x * camera->zoom),
                             floorf(camera->y * camera->zoom),
                             camera->zoom,
                             screen_w, screen_h, player_mode ? 1 : 0,
                             iso_num_objects > 0 ? iso_objects : NULL,
                             iso_num_objects);
            }

            /* Update GUI info labels */
            {
                char sbuf[256];
                if (player_mode) {
                    float ph = interpolate_height_at_clamped(player_fx, player_fy, PLAYER_MAX_HEIGH_DIFF);
                    snprintf(sbuf, sizeof(sbuf), "Pos:(%.1f,%.1f) Z:%.1f GndH:%.1f %s | Zoom:%.1fx",
                             player_fx, player_fy, player_z, ph,
                             player_on_ground ? "GND" : "AIR", camera->zoom);
                } else if (height_mode) {
                    snprintf(sbuf, sizeof(sbuf), "Paint height:%d | Zoom:%.1fx | Slope max:%d",
                             paint_height, camera->zoom, smooth_slope_max);
                } else {
                    snprintf(sbuf, sizeof(sbuf), "Brush:[%s] | Zoom:%.1fx | Slope max:%d",
                             terrain_names[paint_terrain], camera->zoom, smooth_slope_max);
                }
                n_gui_label_set_text(gui_ctx, gui_lbl_status, sbuf);

                snprintf(sbuf, sizeof(sbuf), "Proj: %s (%.1f deg)",
                         iso_projection_name(current_proj), isomap->proj.angle_deg);
                n_gui_label_set_text(gui_ctx, gui_lbl_proj, sbuf);

                if (hover_mx >= 0 && hover_mx < MAP_W && hover_my >= 0 && hover_my < MAP_H) {
                    snprintf(sbuf, sizeof(sbuf), "Hover: (%d,%d) H:%d T:%s",
                             hover_mx, hover_my,
                             iso_map_get_height(isomap, hover_mx, hover_my),
                             terrain_names[iso_map_get_terrain(isomap, hover_mx, hover_my)]);
                    n_gui_label_set_text(gui_ctx, gui_lbl_hover, sbuf);
                }
            }

            /* Draw GUI overlay */
            n_gui_draw(gui_ctx);

            al_flip_display();
        }
    }

    /* Cleanup */
    dr_entity_destroy(&ghost_dr);
    if (player_path) {
        n_astar_path_free(player_path);
        player_path = NULL;
    }
    if (gui_ctx) n_gui_destroy_ctx(&gui_ctx);
    if (camera) n_iso_camera_free(&camera);
    if (isomap) iso_map_free(&isomap);

    for (int t = 0; t < NUM_TERRAINS; t++) {
        for (int m = 0; m < NUM_MASKS; m++)
            if (transition_tiles[t][m]) al_destroy_bitmap(transition_tiles[t][m]);
        if (tile_bitmaps[t]) al_destroy_bitmap(tile_bitmaps[t]);
    }
    for (int m = 0; m < NUM_MASKS; m++)
        if (transition_masks[m]) al_destroy_bitmap(transition_masks[m]);

    al_destroy_font(font);
    al_destroy_timer(timer);
    al_destroy_event_queue(queue);
    al_destroy_display(display);

    return EXIT_SUCCESS;
}
