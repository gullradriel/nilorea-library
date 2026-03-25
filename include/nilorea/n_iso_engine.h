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
 *@file n_iso_engine.h
 *@brief Isometric/axonometric tile engine with height maps, terrain
 * transitions, and A* pathfinding integration.
 *
 * Provides data structures and utility functions for diamond-layout
 * isometric maps with:
 * - Per-cell terrain type, height, ability, object, and music layers
 * - Configurable axonometric projections (classic, steep, wide, military)
 * - Map-to-screen and screen-to-map coordinate conversion
 * - Bilinear height interpolation for smooth terrain
 * - Terrain transition bitmask computation (edge + corner)
 * - Integration with n_astar for pathfinding on the tile grid
 * - Chunk-based binary map file format (save/load)
 *
 * Based on:
 * - GameDev.net Articles 747/748: "Isometric Tile Engine" series
 * - GameDev.net Article 934: "Terrain Transitions"
 * - GameDev.net Article 1269: "Axonometric Projections"
 * - GameDev.net Article 2026: "Height-Mapped Isometric"
 *
 *@author Castagnier Mickael
 *@version 2.0
 *@date 01/03/2026
 */

#ifndef NILOREA_ISOMETRIC_ENGINE
#define NILOREA_ISOMETRIC_ENGINE

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup ISOMETRIC_ENGINE GRAPHICS: isometric engine, maps, tiles, ...
   @addtogroup ISOMETRIC_ENGINE
  @{
*/

#include "n_common.h"
#include "n_log.h"

/*---------------------------------------------------------------------------
 * Ability flags (shared by both legacy MAP and new ISO_MAP)
 *-------------------------------------------------------------------------*/

/*! FLAG of a walkable tile */
#define WALK 1
/*! FLAG of a swimmable tile */
#define SWIM 2
/*! FLAG of a stopping tile */
#define BLCK 3

/*---------------------------------------------------------------------------
 * Terrain transition bitmask definitions (Article 934)
 *
 * Edge bits represent which cardinal sides have a higher-priority terrain.
 * Corner bits represent which diagonal neighbors have a higher-priority
 * terrain (only when the adjacent edges do NOT have that terrain).
 *-------------------------------------------------------------------------*/

/*! Edge bitmask: west neighbor has higher terrain */
#define ISO_EDGE_W (1 << 0)
/*! Edge bitmask: north neighbor has higher terrain */
#define ISO_EDGE_N (1 << 1)
/*! Edge bitmask: east neighbor has higher terrain */
#define ISO_EDGE_E (1 << 2)
/*! Edge bitmask: south neighbor has higher terrain */
#define ISO_EDGE_S (1 << 3)
/*! Number of possible edge mask combinations (2^4 = 16) */
#define ISO_NUM_EDGE_MASKS 16

/*! Corner bitmask: northwest diagonal has higher terrain */
#define ISO_CORNER_NW (1 << 0)
/*! Corner bitmask: northeast diagonal has higher terrain */
#define ISO_CORNER_NE (1 << 1)
/*! Corner bitmask: southeast diagonal has higher terrain */
#define ISO_CORNER_SE (1 << 2)
/*! Corner bitmask: southwest diagonal has higher terrain */
#define ISO_CORNER_SW (1 << 3)
/*! Number of possible corner mask combinations (2^4 = 16) */
#define ISO_NUM_CORNER_MASKS 16
/*! Total number of transition masks (edge + corner) */
#define ISO_NUM_MASKS (ISO_NUM_EDGE_MASKS + ISO_NUM_CORNER_MASKS)

/*---------------------------------------------------------------------------
 * Projection presets
 *-------------------------------------------------------------------------*/

/*! Projection ID: classic 2:1 isometric (~26.565 degree angle) */
#define ISO_PROJ_CLASSIC 0
/*! Projection ID: true isometric (30 degree angle) */
#define ISO_PROJ_ISOMETRIC 1
/*! Projection ID: military/planometric (45 degree angle) */
#define ISO_PROJ_MILITARY 2
/*! Projection ID: flatter dimetric (~18.43 degree angle) */
#define ISO_PROJ_STAGGERED 3
/*! Number of built-in projection presets */
#define ISO_NUM_PROJECTIONS 4

/* Legacy aliases */
/*! Legacy alias: ISO_PROJ_STEEP maps to ISO_PROJ_ISOMETRIC */
#define ISO_PROJ_STEEP ISO_PROJ_ISOMETRIC
/*! Legacy alias: ISO_PROJ_WIDE maps to ISO_PROJ_STAGGERED */
#define ISO_PROJ_WIDE ISO_PROJ_STAGGERED

/*---------------------------------------------------------------------------
 * New height-aware isometric data structures (no Allegro dependency)
 *-------------------------------------------------------------------------*/

/*! Max height segments per tile (ground + one elevated structure). */
#define ISO_MAX_SEGMENTS_PER_TILE 2

/*! Single vertical segment of a tile: solid matter from bottom to top. */
typedef struct ISO_TILE_SEGMENT {
    int bottom;     /*!< lower height bound (inclusive), must be < top */
    int top;        /*!< upper height bound, must be > bottom */
    int upper_tile; /*!< terrain index for top face (-1 = use tile's terrain layer) */
    int lower_tile; /*!< terrain index for wall/side faces (-1 = use tile's terrain layer) */
} ISO_TILE_SEGMENT;

/*! Per-tile height segments.  A normal solid tile is [{0, h}].
 *  A bridge tile might be [{0,1},{3,4}].  A flat tile has count=0. */
typedef struct ISO_TILE_SEGMENTS {
    int count; /*!< 0..ISO_MAX_SEGMENTS_PER_TILE */
    ISO_TILE_SEGMENT segs[ISO_MAX_SEGMENTS_PER_TILE];
} ISO_TILE_SEGMENTS;

/*! Draw order entry for segment-sorted rendering.
 *  Produced by iso_map_build_draw_order(). */
typedef struct ISO_DRAW_ENTRY {
    int mx, my;    /*!< tile coordinates */
    int seg_idx;   /*!< segment index within tile (0..count-1) */
    int bottom;    /*!< segment bottom height (primary sort key) */
    int top;       /*!< segment top height */
    int underside; /*!< 1 if underside face should be drawn */
} ISO_DRAW_ENTRY;

/*! Axonometric projection parameters.
 * Controls how map coordinates are projected to screen coordinates.
 * The half-widths define the diamond shape of each tile. */
typedef struct ISO_PROJECTION {
    float half_w;       /*!< half-width of a tile in pixels (horizontal extent) */
    float half_h;       /*!< half-height of a tile in pixels (vertical extent) */
    float angle_deg;    /*!< current projection angle in degrees */
    float tile_lift;    /*!< vertical pixel offset per height unit */
    float target_angle; /*!< target angle for smooth interpolation (degrees) */
    float lerp_speed;   /*!< interpolation speed factor (default 3.0) */
} ISO_PROJECTION;

/*---------------------------------------------------------------------------
 * 2D Isometric Camera
 *
 * Manages camera position, zoom, and coordinate conversion between
 * screen pixels and world coordinates.  Works with iso_map_draw()
 * and iso_screen_to_map_height() using the library coordinate system.
 *-------------------------------------------------------------------------*/

/*! 2D isometric camera for viewport management. */
typedef struct N_ISO_CAMERA {
    float x;        /*!< camera X offset (world units, pre-zoom) */
    float y;        /*!< camera Y offset (world units, pre-zoom) */
    float zoom;     /*!< zoom factor (1.0 = no zoom) */
    float zoom_min; /*!< minimum allowed zoom */
    float zoom_max; /*!< maximum allowed zoom */
} N_ISO_CAMERA;

/*! Height-aware isometric map with terrain and height layers.
 * Extends the legacy MAP concept with per-cell height values,
 * configurable projection, and terrain transition support. */
typedef struct ISO_MAP {
    int width;        /*!< map width in tiles (X axis) */
    int height;       /*!< map height in tiles (Y axis) */
    int num_terrains; /*!< number of terrain types used */
    int max_height;   /*!< maximum allowed height value */

    int* terrain;   /*!< terrain type per cell [height * width], indices 0..num_terrains-1 */
    int* heightmap; /*!< height value per cell [height * width], 0..max_height */
    int* ability;   /*!< walkability per cell [height * width] (WALK/SWIM/BLCK) */
    int* overlay;   /*!< overlay tile index per cell [height * width], 0 = none. NULL = no overlays */

    /*! Per-cell height segments.  NULL = use heightmap as single segment.
     *  When non-NULL, heightmap[] is derived (top of highest segment). */
    ISO_TILE_SEGMENTS* segments;

    ISO_PROJECTION proj; /*!< current projection parameters */

    /* --- rendering flags (used by iso_map_draw) --- */
    int smooth_height;    /*!< 0 = CUT mode (cliff walls), 1 = SMOOTH mode (per-corner slopes) */
    int smooth_slope_max; /*!< max height diff rendered as slope (default 1) */
    int show_grid;        /*!< 1 = draw grid overlay */
    int hover_mx;         /*!< hovered tile X (-1 = none) */
    int hover_my;         /*!< hovered tile Y (-1 = none) */

    /* --- height-based ambient tinting --- */
    float height_tint_intensity; /*!< 0.0 = no tint, 0.3 = subtle, 1.0 = full range.
                                  * Higher segments are brighter, lower are darker.
                                  * Applied as a multiply tint on tile top faces and walls. */

    /* --- neighbor edge heights for chunked rendering --- */
    /*! Heights from west neighbor chunk's east edge [height], NULL = boundary */
    int* neighbor_heights_west;
    /*! Heights from north neighbor chunk's south edge [width], NULL = boundary */
    int* neighbor_heights_north;
    /*! Heights from east neighbor chunk's west edge [height], NULL = boundary */
    int* neighbor_heights_east;
    /*! Heights from south neighbor chunk's north edge [width], NULL = boundary */
    int* neighbor_heights_south;

    /*! Global ambient color (day/night + dark place tinting).
     *  Applied as additional multiply on top of height tinting.
     *  Default: (1.0, 1.0, 1.0) = no change.
     *  Set by client lighting system each frame. */
    float ambient_r, ambient_g, ambient_b;

    /*! Per-tile dynamic light accumulation buffer.
     *  Caller fills this before iso_map_draw() with additive light contributions.
     *  Array of [width * height * 3] floats (r, g, b per tile).
     *  NULL = no dynamic lighting. Caller-owned. */
    float* dynamic_light_map;
} ISO_MAP;

/*---------------------------------------------------------------------------
 * New height-aware isometric API (Articles 747/748/934/1269/2026)
 * These functions do NOT require Allegro.
 *-------------------------------------------------------------------------*/

/*! create a new height-aware ISO_MAP with given dimensions */
ISO_MAP* iso_map_new(int width, int height, int num_terrains, int max_height);
/*! free an ISO_MAP and set the pointer to NULL */
void iso_map_free(ISO_MAP** map);

/*! set projection preset and snap to it immediately */
void iso_map_set_projection(ISO_MAP* map, int preset, float tile_width);
/*! set custom projection parameters directly */
void iso_map_set_projection_custom(ISO_MAP* map, float half_w, float half_h, float tile_lift);
/*! set target projection preset for smooth interpolation via iso_map_lerp_projection */
void iso_map_set_projection_target(ISO_MAP* map, int preset);
/*! smoothly interpolate current projection angle toward target (call every frame) */
void iso_map_lerp_projection(ISO_MAP* map, float dt);
/*! get the display name for a projection preset */
const char* iso_projection_name(int preset);

/*! get terrain type for a cell (bounds-checked) */
int iso_map_get_terrain(const ISO_MAP* map, int mx, int my);
/*! set terrain type for a cell (bounds-checked) */
void iso_map_set_terrain(ISO_MAP* map, int mx, int my, int terrain);
/*! get height for a cell (bounds-checked, clamped) */
int iso_map_get_height(const ISO_MAP* map, int mx, int my);
/*! set height for a cell (bounds-checked, clamped) */
void iso_map_set_height(ISO_MAP* map, int mx, int my, int h);
/*! get ability for a cell (bounds-checked) */
int iso_map_get_ability(const ISO_MAP* map, int mx, int my);
/*! set ability for a cell (bounds-checked) */
void iso_map_set_ability(ISO_MAP* map, int mx, int my, int ab);

/*! convert map tile coordinates to screen pixel coordinates */
void iso_map_to_screen(const ISO_MAP* map, int mx, int my, int h, float* screen_x, float* screen_y);
/*! convert screen pixel coordinates to map tile coordinates (flat, no height) */
void iso_screen_to_map(const ISO_MAP* map, float screen_x, float screen_y, int* mx, int* my);
/*! convert map tile coordinates to screen pixel coordinates (float version) */
void iso_map_to_screen_f(const ISO_MAP* map, float fmx, float fmy, float h, float* screen_x, float* screen_y);
/*! Project a tile corner to screen coordinates (canonical formula).
 * Produces bit-identical results for the same (cx,cy,fh) regardless of
 * which tile's draw call computes the vertex.  Used by the renderer
 * to eliminate shared-edge seams between adjacent wall faces. */
void iso_corner_to_screen(const ISO_MAP* map, int cx, int cy, float fh, float cam_x, float cam_y, float zoom, float* sx, float* sy);
/*! height-aware screen-to-map conversion with diamond hit testing.
 * Tests all heights from max_height down to 0, returns the topmost tile
 * at the given screen position. */
void iso_screen_to_map_height(const ISO_MAP* map, float screen_x, float screen_y, int tile_w, int tile_h, int* mx, int* my);
/*! height-aware screen-to-map conversion returning fractional tile coordinates.
 * Like iso_screen_to_map_height but outputs the exact position within the tile
 * via out_fx/out_fy (range: tile.x .. tile.x+1). */
void iso_screen_to_map_height_f(const ISO_MAP* map, float screen_x, float screen_y, int tile_w, int tile_h, int* mx, int* my, float* out_fx, float* out_fy);

/*! test if pixel (px,py) is inside the isometric diamond of size tile_w x tile_h */
int iso_is_in_diamond(int px, int py, int tile_w, int tile_h);
/*! distance from pixel to diamond edge (0 at edge, 1 at center) */
float iso_diamond_dist(int px, int py, int tile_w, int tile_h);

/*! bilinear height interpolation at fractional map coordinates */
float iso_map_interpolate_height(const ISO_MAP* map, float fx, float fy);

/*! compute terrain transition bitmasks for a cell (Article 934) */
void iso_map_calc_transitions(const ISO_MAP* map, int mx, int my, int* edge_bits, int* corner_bits);
/*! check if terrain blending should occur between two adjacent cells */
int iso_map_should_transition(const ISO_MAP* map, int mx1, int my1, int mx2, int my2);

/*! compute average corner heights for smooth rendering (Article 2026) */
void iso_map_corner_heights(const ISO_MAP* map, int mx, int my, float* h_n, float* h_e, float* h_s, float* h_w);

/*! save ISO_MAP to binary file (chunk-based format) */
int iso_map_save(const ISO_MAP* map, const char* filename);
/*! load ISO_MAP from binary file */
ISO_MAP* iso_map_load(const char* filename);

/*! randomize terrain and height for testing/demo purposes */
void iso_map_randomize(ISO_MAP* map);

#ifdef N_ASTAR_H
/*! populate an ASTAR_GRID from ISO_MAP walkability data.
 * Uses flood-fill from (start_x, start_y) so that walkability respects
 * height constraints relative to the player's current elevation rather
 * than marking cliff-edge tiles as globally blocked. */
ASTAR_GRID* iso_map_to_astar_grid(const ISO_MAP* map, int max_height_diff, int start_x, int start_y);
#endif

/*! compute smooth corner heights with slope clamping (for rendering) */
void iso_map_smooth_corner_heights(const ISO_MAP* map, int mx, int my, float* h_n, float* h_e, float* h_s, float* h_w);

/*! set per-tile segments on an ISO_MAP.  Copies data into map->segments
 *  (allocated on first call).  Also updates heightmap for the tile.
 *@param map the ISO_MAP
 *@param mx tile X coordinate
 *@param my tile Y coordinate
 *@param segs array of segments (may be NULL if count==0)
 *@param count number of segments (0..ISO_MAX_SEGMENTS_PER_TILE)
 *@return 1 on success, 0 if invariants violated or out of range */
int iso_map_set_segments(ISO_MAP* map, int mx, int my, const ISO_TILE_SEGMENT* segs, int count);

/*! get per-tile segments.  Returns NULL if no segment data is set. */
const ISO_TILE_SEGMENTS* iso_map_get_segments(const ISO_MAP* map, int mx, int my);

/*! Build the draw order for segment-sorted rendering.
 *  Populates out[] with one entry per segment (or one per tile if no
 *  segment data is available), sorted by (bottom, my, mx).
 *@param map the ISO_MAP
 *@param out output array, must have room for width*height*ISO_MAX_SEGMENTS_PER_TILE entries
 *@param max_entries capacity of out[]
 *@return number of entries written */
int iso_map_build_draw_order(const ISO_MAP* map, ISO_DRAW_ENTRY* out, int max_entries);

/*! check if terrain transition should render between two cells (height-aware) */
int iso_map_should_transition_smooth(const ISO_MAP* map, int mx1, int my1, int mx2, int my2);

/*! compute per-terrain transition bitmasks with height filtering (Article 934).
 * edge_bits and corner_bits must be arrays of size num_terrains. */
void iso_map_calc_transitions_full(const ISO_MAP* map, int mx, int my, int* edge_bits, int* corner_bits);

/*---------------------------------------------------------------------------
 * Height border edge visibility
 *
 * Determines which diamond edges of an elevated tile should be outlined.
 * An edge is drawn when the adjacent tile has a height difference > 1
 * or does not exist (map boundary).
 *-------------------------------------------------------------------------*/

/*! Per-tile edge visibility flags for height border rendering. */
typedef struct ISO_VISIBLE_EDGES {
    int draw_nw;         /*!< 1 if NW edge (N→W, top-left) should be drawn */
    int draw_ne;         /*!< 1 if NE edge (N→E, top-right) should be drawn */
    int draw_se;         /*!< 1 if SE edge (E→S, bottom-right) should be drawn */
    int draw_sw;         /*!< 1 if SW edge (S→W, bottom-left) should be drawn */
    int draw_underside;  /*!< 1 if floating segment underside border should be drawn */
    int neighbor_top_nw; /*!< top height of NW neighbor (0 if absent/boundary) */
    int neighbor_top_ne; /*!< top height of NE neighbor (0 if absent/boundary) */
    int neighbor_top_se; /*!< top height of SE neighbor (0 if absent/boundary) */
    int neighbor_top_sw; /*!< top height of SW neighbor (0 if absent/boundary) */
} ISO_VISIBLE_EDGES;

/*! Compute which diamond edges of tile (mx, my) need a height border.
 * An edge is marked for drawing when the neighbor in that direction has
 * a height difference > 1 or is out of bounds.
 * Edge-to-neighbor mapping (diamond iso layout):
 *   NW → (mx-1, my),  NE → (mx, my-1),
 *   SE → (mx+1, my),  SW → (mx, my+1)
 *@param map the ISO_MAP
 *@param mx tile X coordinate
 *@param my tile Y coordinate
 *@return ISO_VISIBLE_EDGES with flags set */
ISO_VISIBLE_EDGES iso_map_get_visible_edges(const ISO_MAP* map, int mx, int my);

/*! Compute per-segment edge visibility for height border rendering.
 * Uses height-range overlap to determine adjacency: two segments are
 * "touching" only if max(b1,b2) < min(t1,t2).  A segment with no
 * overlapping neighbour on a given side draws that edge.
 * Floating segments (bottom > 0) also compute underside border visibility.
 *@param map the ISO_MAP
 *@param mx tile X coordinate
 *@param my tile Y coordinate
 *@param seg_idx segment index within tile (0..count-1)
 *@return ISO_VISIBLE_EDGES with flags set (including draw_underside) */
ISO_VISIBLE_EDGES iso_map_get_visible_edges_segment(const ISO_MAP* map, int mx, int my, int seg_idx);

/*---------------------------------------------------------------------------
 * N_ISO_CAMERA API
 *
 * 2D camera management for isometric viewports.  Coordinates match the
 * library convention used by iso_map_to_screen() and iso_map_draw().
 *-------------------------------------------------------------------------*/

/*! Create a new camera with the given zoom limits.
 *@param zoom_min minimum zoom factor (e.g. 0.5)
 *@param zoom_max maximum zoom factor (e.g. 6.0)
 *@return new camera, or NULL on failure */
N_ISO_CAMERA* n_iso_camera_new(float zoom_min, float zoom_max);

/*! Free a camera and set the pointer to NULL. */
void n_iso_camera_free(N_ISO_CAMERA** cam);

/*! Scroll the camera by (dx, dy) world units. */
void n_iso_camera_scroll(N_ISO_CAMERA* cam, float dx, float dy);

/*! Zoom the camera toward a screen-space point (e.g. mouse cursor).
 *@param cam the camera
 *@param dz zoom delta (positive = zoom in)
 *@param mouse_x screen X of zoom pivot
 *@param mouse_y screen Y of zoom pivot */
void n_iso_camera_zoom(N_ISO_CAMERA* cam, float dz, float mouse_x, float mouse_y);

/*! Center the camera so that a world point is at screen center.
 *@param cam the camera
 *@param world_x world X to center on
 *@param world_y world Y to center on
 *@param screen_w viewport width in pixels
 *@param screen_h viewport height in pixels */
void n_iso_camera_center_on(N_ISO_CAMERA* cam, float world_x, float world_y, int screen_w, int screen_h);

/*! Smoothly follow a world-space target (camera lerp).
 *@param cam the camera
 *@param target_x target world X
 *@param target_y target world Y
 *@param screen_w viewport width
 *@param screen_h viewport height
 *@param smoothing lerp speed factor (higher = snappier, e.g. 8.0)
 *@param dt time step in seconds */
void n_iso_camera_follow(N_ISO_CAMERA* cam, float target_x, float target_y, int screen_w, int screen_h, float smoothing, float dt);

/*! Convert screen pixel coordinates to world coordinates.
 *@param cam the camera
 *@param sx screen X
 *@param sy screen Y
 *@param wx output world X
 *@param wy output world Y */
void n_iso_camera_screen_to_world(const N_ISO_CAMERA* cam, float sx, float sy, float* wx, float* wy);

/*! Convert world coordinates to screen pixel coordinates.
 *@param cam the camera
 *@param wx world X
 *@param wy world Y
 *@param sx output screen X
 *@param sy output screen Y */
void n_iso_camera_world_to_screen(const N_ISO_CAMERA* cam, float wx, float wy, float* sx, float* sy);

/*---------------------------------------------------------------------------
 * Isometric drawable object for depth-sorted rendering
 *
 * Objects are interleaved with tiles during iso_map_draw() based on
 * their map position, achieving correct painter's algorithm ordering.
 * When an object is behind a taller tile, it can optionally be redrawn
 * as a transparent overlay so the player can still see it.
 *-------------------------------------------------------------------------*/

/*! Object draw callback.
 *@param screen_x screen pixel X position (camera-transformed)
 *@param screen_y screen pixel Y position (camera-transformed)
 *@param zoom current camera zoom factor
 *@param alpha opacity multiplier (1.0 = normal draw, <1.0 = occluded overlay)
 *@param user_data user pointer from N_ISO_OBJECT */
typedef void (*N_ISO_OBJECT_DRAW_FN)(float screen_x, float screen_y, float zoom, float alpha, void* user_data);

/*! Drawable object for depth-sorted isometric rendering. */
typedef struct N_ISO_OBJECT {
    float fx;                  /*!< fractional map X position */
    float fy;                  /*!< fractional map Y position */
    float fz;                  /*!< height in map units (elevation) */
    float occluded_alpha;      /*!< overlay alpha when behind tiles [0..1], 0=hidden, 0.35=ghost, 1=always visible */
    float sprite_h;            /*!< entity height in world units for head occlusion (e.g. 2.0 for players) */
    int is_occluded;           /*!< OUTPUT: set to 1 if behind tiles during last iso_map_draw() call */
    N_ISO_OBJECT_DRAW_FN draw; /*!< draw callback */
    void* user_data;           /*!< user data passed to draw callback */
} N_ISO_OBJECT;

/*---------------------------------------------------------------------------
 * ISO_MAP rendering API (requires Allegro 5)
 *
 * Provides full isometric rendering with height, terrain transitions,
 * cliff walls, hover highlight, and grid overlay. Requires pre-generated
 * tile bitmaps and transition mask overlays from the application.
 *-------------------------------------------------------------------------*/
#ifdef HAVE_ALLEGRO

#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>

/*! Draw the full ISO_MAP with height, transitions, overlays, cliff walls, hover, and grid.
 * Objects (if provided) are depth-sorted and interleaved with tiles using the
 * painter's algorithm. Occluded objects are optionally redrawn as a transparent
 * overlay based on their occluded_alpha setting.
 *@param map the ISO_MAP
 *@param tile_bitmaps array of ALLEGRO_BITMAP* per terrain type
 *@param transition_tiles 2D array [num_terrains][num_masks] of pre-composited transitions
 *@param num_masks total number of masks (edge + corner, typically 32)
 *@param overlay_bitmaps array of ALLEGRO_BITMAP* per overlay tile (may be NULL)
 *@param num_overlay_tiles number of overlay bitmaps (0 if none)
 *@param cam_px camera X offset in pixels (pre-snapped)
 *@param cam_py camera Y offset in pixels (pre-snapped)
 *@param zoom display zoom factor
 *@param screen_w current screen width in pixels
 *@param screen_h current screen height in pixels
 *@param player_mode 1 if in player mode (hover is green), 0 for editor (hover is yellow)
 *@param objects array of N_ISO_OBJECT to depth-sort with tiles (may be NULL)
 *@param num_objects number of objects in the array (0 if none)
 */
void iso_map_draw(const ISO_MAP* map,
                  ALLEGRO_BITMAP** tile_bitmaps,
                  ALLEGRO_BITMAP*** transition_tiles,
                  int num_masks,
                  ALLEGRO_BITMAP** overlay_bitmaps,
                  int num_overlay_tiles,
                  float cam_px,
                  float cam_py,
                  float zoom,
                  int screen_w,
                  int screen_h,
                  int player_mode,
                  N_ISO_OBJECT* objects,
                  int num_objects);

/*! Mask a tile bitmap to the isometric diamond shape.
 * Pixels outside the diamond are set to fully transparent (0,0,0,0).
 *@param bmp the tile bitmap to mask
 *@param tile_w tile width in pixels
 *@param tile_h tile height in pixels
 */
void iso_mask_tile_to_diamond(ALLEGRO_BITMAP* bmp, int tile_w, int tile_h);

/*! Generate the 32 procedural transition alpha masks (16 edge + 16 corner).
 * The masks array must have room for ISO_NUM_MASKS entries.
 *@param masks output array of ALLEGRO_BITMAP* (caller must free)
 *@param tile_w tile width in pixels
 *@param tile_h tile height in pixels
 */
void iso_generate_transition_masks(ALLEGRO_BITMAP** masks, int tile_w, int tile_h);

/*! Pre-composite transition tiles (terrain texture * alpha mask).
 * tiles[t][m] = terrain t composited with mask m.
 * tiles must be a [num_terrains][ISO_NUM_MASKS] array of ALLEGRO_BITMAP*.
 *@param tiles output 2D array of ALLEGRO_BITMAP* (caller must free)
 *@param masks array of ISO_NUM_MASKS transition alpha masks
 *@param tile_bitmaps array of num_terrains terrain tile bitmaps
 *@param num_terrains number of terrain types
 *@param tile_w tile width in pixels
 *@param tile_h tile height in pixels
 */
void iso_generate_transition_tiles(ALLEGRO_BITMAP*** tiles,
                                   ALLEGRO_BITMAP** masks,
                                   ALLEGRO_BITMAP** tile_bitmaps,
                                   int num_terrains,
                                   int tile_w,
                                   int tile_h);

/*---------------------------------------------------------------------------
 * Legacy API (requires Allegro 5)
 *-------------------------------------------------------------------------*/

/*! FLAG of tile type*/
#define N_TILE 2
/*! FLAG of ability type*/
#define N_ABILITY 3
/*! FLAG of music type*/
#define N_MUSIC 4
/*! FLAG of object type*/
#define N_OBJECT 5

/*! Cell of a MAP */
typedef struct CELL {
    int /*! ident of the tile in the MAP->tile library */
        tilenumber,
        /*! ability of the tile (walking, swimming, blocking, killing ?) */
        ability,
        /*! ident of the object in the MAP->object library */
        objectnumber,
        /*! ident of the music on the tile */
        music;

} CELL;

/*! MAP with objects, tiles, skins (legacy structure) */
typedef struct MAP {
    /*! Grid of each cell of the map */
    CELL* grid;

    /*! Name of the map ( used for linking between two map ) */
    char* name;

    /*! Map for mouse collision between mouse pointer and map */
    ALLEGRO_BITMAP *mousemap,
        /*! default full colored background tile */
        *colortile;

    int /*! size X of the grid (nbXcell) */
        XSIZE,
        /*! size Y of the grid (nbYcell) */
        YSIZE,
        /*! size X of tiles of the map (in pixel) */
        TILEW,
        /*! size Y of tiles of the map (in pixel) */
        TILEH,
        /*! X starting cell for drawing */
        ptanchorX,
        /*! Y starting cell for drawing */
        ptanchorY,
        /*! X move in pixel for drawing */
        X,
        /*! Y move in pixel for drawing */
        Y;
    /*! color of the bg */
    ALLEGRO_COLOR bgcolor;
    /*! color of wire */
    ALLEGRO_COLOR wirecolor;
    /*! name of the main group of the (kind of) library of objects, filled by zero else, size iz limited to 512 characaters */
    char* object_group_names;
    /*! as for previous:char *object_group_names */
    char* anim_group_names;
    /*! as for previous:char *object_group_names */
    char* sounds_group_names;
    /*! as for previous:char *object_group_names */
    char* quests_group_names;

} MAP;

/*! Create an empty map */
int create_empty_map(MAP** map, char* name, int XSIZE, int YSIZE, int TILEW, int TILEH, int nbmaxobjects, int nbmaxgroup, int nbmaxanims, int nbtiles, int nbanims);
/*! Set the tilenumber of a cell */
int set_value(MAP* map, int type, int x, int y, int value);
/*! Get the tilenumber of a cell */
int get_value(MAP* map, int type, int x, int y);
/*! Convert screen coordinate to map coordinate */
int ScreenToMap(int mx, int my, int* Tilex, int* Tiley, ALLEGRO_BITMAP* mousemap);
/*! Center Map on given screen coordinate */
int camera_to_scr(MAP** map, int x, int y);
/*! Center Map on given map coordinate, with x & y offset */
int camera_to_map(MAP** map, int tx, int ty, int x, int y);
/*! Draw the map using its coordinate on the specified bitmap */
int draw_map(MAP* map, ALLEGRO_BITMAP* bmp, int destx, int desty, int mode);
/*! Load the map */
int load_map(MAP** map, char* filename);
/*! Save the map */
int save_map(MAP* map, char* filename);
/*! Free the map */
int free_map(MAP** map);

#endif /* HAVE_ALLEGRO */

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef NILOREA_ISOMETRIC_ENGINE */
