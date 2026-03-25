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
 *@file n_iso_engine.c
 *@brief Isometric/axonometric tile engine with height maps, terrain
 * transitions, and A* pathfinding integration.
 *@author Castagnier Mickael
 *@version 2.0
 *@date 01/03/2026
 */

#include <math.h>
#include <stdio.h>

/* Include n_astar.h BEFORE n_iso_engine.h so the iso_map_to_astar_grid
 * function is enabled via the N_ASTAR_H guard */
#include "nilorea/n_astar.h"
#include "nilorea/n_iso_engine.h"

#ifndef NOISOENGINE

/*===========================================================================
 * Legacy API (requires Allegro 5)
 * Only compiled when HAVE_ALLEGRO is defined.
 *=========================================================================*/
#ifdef HAVE_ALLEGRO

/**
 *@brief Create an empty map
 *@param map the map pointer to point on the allocated one
 *@param name Name of the map (used for linking between two map )
 *@param XSIZE the X size of the map (in tile)
 *@param YSIZE the Y size of the map (in tile)
 *@param TILEW the Width of a tile
 *@param TILEH the Heigh of a tile
 *@param nbmaxobjects the maximum number of objects in the map
 *@param nbmaxgroup the maximum number of active objects in the map
 *@param nbmaxanims the maximum number of animations in the map
 *@param nbtiles the maximum number of tiles in the map
 *@param nbanims the maximum number of animations in the map
 *@return TRUE on success FALSE on failure
 */
int create_empty_map(MAP** map, char* name, int XSIZE, int YSIZE, int TILEW, int TILEH, int nbmaxobjects, int nbmaxgroup, int nbmaxanims, int nbtiles, int nbanims) {
    int x, y;

    (void)nbmaxobjects;
    (void)nbmaxgroup;
    (void)nbmaxanims;
    (void)nbtiles;
    (void)nbanims;

    /* allocating map */
    Malloc((*map), MAP, 1);

    if (!(*map))
        return FALSE;

    /* allocating grid */
    Malloc((*map)->grid, CELL, (size_t)XSIZE * (size_t)YSIZE);
    if (!(*map)->grid) {
        Free((*map));
        return FALSE;
    }

    /* allocating name */
    Malloc((*map)->name, char, strlen(name) + 1);
    if (!(*map)->name) {
        Free((*map)->grid);
        Free((*map));
        return FALSE;
    }

    strcpy((*map)->name, name);

    (*map)->XSIZE = XSIZE;
    (*map)->YSIZE = YSIZE;

    for (x = 0; x < XSIZE; x++) {
        for (y = 0; y < YSIZE; y++) {
            set_value((*map), N_TILE, x, y, FALSE);
            set_value((*map), N_ABILITY, x, y, FALSE);
            set_value((*map), N_MUSIC, x, y, FALSE);
            set_value((*map), N_OBJECT, x, y, FALSE);
        }
    }

    (*map)->TILEW = TILEW;
    (*map)->TILEH = TILEH;
    (*map)->bgcolor = al_map_rgba(0, 0, 0, 255);
    (*map)->wirecolor = al_map_rgba(255, 255, 255, 255);

    /* Drawing the mousemap and colortile */
    ALLEGRO_BITMAP* prev_target = al_get_target_bitmap();

    (*map)->mousemap = al_create_bitmap((*map)->TILEW, (*map)->TILEH);
    (*map)->colortile = al_create_bitmap((*map)->TILEW, (*map)->TILEH);

    float tw = (float)TILEW;
    float th = (float)TILEH;
    float tw2 = tw / 2.0f;
    float th2 = th / 2.0f;

    /* drawing mousemap: white diamond center, colored corner triangles */
    al_set_target_bitmap((*map)->mousemap);
    al_clear_to_color(al_map_rgb(255, 255, 255));

    /* top-left corner: red */
    al_draw_filled_triangle(0, 0, tw2, 0, 0, th2, al_map_rgb(255, 0, 0));
    /* top-right corner: yellow */
    al_draw_filled_triangle(tw2, 0, tw, 0, tw, th2, al_map_rgb(255, 255, 0));
    /* bottom-left corner: green */
    al_draw_filled_triangle(0, th2, 0, th, tw2, th, al_map_rgb(0, 255, 0));
    /* bottom-right corner: blue */
    al_draw_filled_triangle(tw, th2, tw2, th, tw, th, al_map_rgb(0, 0, 255));

    /* drawing colortile: transparent background, bgcolor diamond */
    al_set_target_bitmap((*map)->colortile);
    al_clear_to_color(al_map_rgba(0, 0, 0, 0));

    /* draw diamond as two triangles filled with bgcolor */
    al_draw_filled_triangle(tw2, 0, 0, th2, tw, th2, (*map)->bgcolor);
    al_draw_filled_triangle(0, th2, tw2, th, tw, th2, (*map)->bgcolor);

    /* restore previous target */
    if (prev_target)
        al_set_target_bitmap(prev_target);

    (*map)->ptanchorX = (*map)->X = 0;
    (*map)->ptanchorY = (*map)->Y = 0;

    return TRUE;
} /* create_empty_map( ... ) */

/**
 *@brief Set the value of a cell in a map
 *@param map A MAP *map to 'touch'
 *@param type It is the type of value to change , it can be N_TILE,N_ABILITY,N_MUSIC,N_OBJECT
 *@param x X coordinate of the cell
 *@param y Y coordinate of the cell
 *@param value Value to give to the cell
 *@return TRUE or FALSE
 */
int set_value(MAP* map, int type, int x, int y, int value) {
    if (!map || x < 0 || y < 0 || x >= map->XSIZE || y >= map->YSIZE)
        return FALSE;

    if (type == N_TILE) {
        map->grid[x + y * map->XSIZE].tilenumber = value;

        return TRUE;
    }

    if (type == N_ABILITY) {
        map->grid[x + y * map->XSIZE].ability = value;

        return TRUE;
    }

    if (type == N_MUSIC) {
        map->grid[x + y * map->XSIZE].music = value;

        return TRUE;
    }

    if (type == N_OBJECT) {
        map->grid[x + y * map->XSIZE].objectnumber = value;

        return TRUE;
    }

    return FALSE;
} /* set_value( ... ) */

/**
 *@brief Get a the tilenumber of a cell item
 *@param map A MAP *map to 'touch'
 *@param type It is the type of value to change , it can be N_TILE,N_ABILITY,N_MUSIC,N_OBJECT
 *@param x X coordinate of the cell
 *@param y Y coordinate of the cell
 *@return The specified type value of the cell, or FALSE if unavailable
 */
int get_value(MAP* map, int type, int x, int y) {
    if (!map || x < 0 || y < 0 || x >= map->XSIZE || y >= map->YSIZE)
        return FALSE;

    if (type == N_TILE)
        return map->grid[x + y * map->XSIZE].tilenumber;

    if (type == N_OBJECT)
        return map->grid[x + y * map->XSIZE].objectnumber;

    if (type == N_MUSIC)
        return map->grid[x + y * map->XSIZE].music;

    if (type == N_ABILITY)
        return map->grid[x + y * map->XSIZE].ability;

    return FALSE;

} /* get_value( ... ) */

/**
 *@brief Convert screen coordinate to map coordinate
 *@param mx The 'x' coordinate in pixel
 *@param my The 'y' coordinate in pixel
 *@param Tilex Output for the generated Tilex
 *@param Tiley Output for the generated Tiley
 *@param mousemap A mousemap with colors for one tile
 *@return TRUE
 */
int ScreenToMap(int mx, int my, int* Tilex, int* Tiley, ALLEGRO_BITMAP* mousemap) {
    /* SCREEN TO MAP VARIABLES */
    int RegionX, RegionY,
        RegionDX = 0, RegionDY = 0,
        MouseMapX, MouseMapY,
        MouseMapWidth, MouseMapHeight;

    MouseMapWidth = al_get_bitmap_width(mousemap);
    MouseMapHeight = al_get_bitmap_height(mousemap);

    /* First step find out where we are on the mousemap */
    RegionX = (mx / MouseMapWidth);
    RegionY = (my / MouseMapHeight) << 1; /* The multiplying by two is very important */

    /* Second Step: Find out WHERE in the mousemap our mouse is, by finding MouseMapX and MouseMapY. */

    MouseMapX = mx % MouseMapWidth;
    MouseMapY = my % MouseMapHeight;

    /* third step: Find the color in the mouse map */
    ALLEGRO_COLOR c = al_get_pixel(mousemap, MouseMapX, MouseMapY);
    unsigned char r, g, b;
    al_unmap_rgb(c, &r, &g, &b);

    if (r == 255 && g == 0 && b == 0) {
        /* red: top-left */
        RegionDX = -1;
        RegionDY = -1;
    }

    if (r == 255 && g == 255 && b == 0) {
        /* yellow: top-right */
        RegionDX = 0;
        RegionDY = -1;
    }

    if (r == 255 && g == 255 && b == 255) {
        /* white: center diamond */
        RegionDX = 0;
        RegionDY = 0;
    }

    if (r == 0 && g == 255 && b == 0) {
        /* green: bottom-left */
        RegionDX = -1;
        RegionDY = 1;
    }

    if (r == 0 && g == 0 && b == 255) {
        /* blue: bottom-right */
        RegionDX = 0;
        RegionDY = 1;
    }

    *Tilex = (RegionDX + RegionX);
    *Tiley = (RegionDY + RegionY);

    return TRUE;
} /* ScreenToMap( ... ) */

/**
 *@brief Center Map on given screen coordinate
 *@param map A MAP *map to center on x,y
 *@param x the X coordinate (in pixel) to center on;
 *@param y the Y coordintae (in pixel) to center on;
 *@return TRUE
 */
int camera_to_scr(MAP** map, int x, int y) {
    if (x < 0)
        x = 0;

    if (y < 0)
        y = 0;

    (*map)->X = x % (*map)->TILEW;
    (*map)->Y = y % (*map)->TILEH;
    (*map)->ptanchorY = y / ((*map)->TILEH >> 1);
    (*map)->ptanchorY = (*map)->ptanchorY >> 1;
    (*map)->ptanchorY = (*map)->ptanchorY << 1;
    (*map)->ptanchorX = (x + ((*map)->ptanchorY & 1) * ((*map)->TILEW >> 1)) / (*map)->TILEW;

    return TRUE;
} /* camera_to_scr( ... )*/

/**
 *@brief Center Map on given map coordinate, with x & y offset
 *@param map A MAP *map to center on tx+x , ty+y
 *@param tx TILEX offset in tile
 *@param ty TILEY offset in tile
 *@param x X offset in pixel
 *@param y Y Offset in pixel
 *@return TRUE
 */
int camera_to_map(MAP** map, int tx, int ty, int x, int y) {
    (*map)->ptanchorX = tx;
    (*map)->ptanchorY = ty;
    (*map)->X = x;
    (*map)->Y = y;

    return TRUE;
} /* camera_to_map(...) */

/**
 *@brief Draw a MAP *map on the ALLEGRO_BITMAP *bmp.
 *@param map The Map to draw
 *@param bmp The screen destination
 *@param destx X offset on bmp
 *@param desty Y offste on bmp
 *@param mode Drawing mode
 *@return TRUE
 */
int draw_map(MAP* map, ALLEGRO_BITMAP* bmp, int destx, int desty, int mode) {
    int a, b,     /* iterators */
        x, y,     /* temp store of coordinate      */
        mvx, mvy, /* precomputed offset */
        tw, th,   /* number of tile for one screen */
        TW2, TH2; /* size of TILE_W /2 and TILE_H/2 */

    ALLEGRO_BITMAP* prev_target = al_get_target_bitmap();
    al_set_target_bitmap(bmp);

    int bmp_w = al_get_bitmap_width(bmp);
    int bmp_h = al_get_bitmap_height(bmp);

    /* computing tw&th */
    tw = 1 + ((bmp_w - destx) / map->TILEW);
    th = 3 + ((bmp_h - desty) / (map->TILEH >> 1));

    TW2 = map->TILEW >> 1;
    TH2 = map->TILEH >> 1;

    mvx = destx - TW2 - map->X;
    mvy = desty - TH2 - map->Y;

    for (a = 0; a <= tw; a++) {
        for (b = 0; b <= th; b++) {
            x = (a * map->TILEW + ((b & 1) * TW2)) - (map->TILEW >> 1) + mvx;
            y = (b * TH2) + mvy;

            if ((a + map->ptanchorX) < map->XSIZE && (b + map->ptanchorY) < map->YSIZE) {
                if (map->grid[a + map->ptanchorX + (b + map->ptanchorY) * map->XSIZE].tilenumber == FALSE) {
                    al_draw_bitmap(map->colortile, (float)x, (float)y, 0);
                }

                else {
                    /* placeholder: add sprite blit */
                    float fx = (float)x;
                    float fy = (float)y;
                    float ftw = (float)map->TILEW;
                    float fth = (float)map->TILEH;

                    al_draw_filled_triangle(fx, fy + fth / 2.0f,
                                            fx + ftw, fy + fth / 2.0f,
                                            fx + ftw / 2.0f, fy,
                                            al_map_rgb(255, 255, 0));
                    al_draw_filled_triangle(fx, fy + fth / 2.0f,
                                            fx + ftw, fy + fth / 2.0f,
                                            fx + ftw / 2.0f, fy + fth,
                                            al_map_rgb(255, 0, 255));

                } /* if( map -> grid... ) ) */

                if (mode == 1) {
                    float fx = (float)x;
                    float fy = (float)y;
                    float ftw = (float)map->TILEW;
                    float fth = (float)map->TILEH;

                    al_draw_line(fx + ftw / 2.0f - 2, fy,
                                 fx, fy + fth / 2.0f - 1,
                                 map->wirecolor, 0);
                    al_draw_line(fx + ftw / 2.0f - 2, fy + fth - 1,
                                 fx, fy + fth / 2.0f,
                                 map->wirecolor, 0);
                    al_draw_line(fx + ftw / 2.0f + 1, fy + fth - 1,
                                 fx + ftw - 1, fy + fth / 2.0f,
                                 map->wirecolor, 0);
                    al_draw_line(fx + ftw / 2.0f + 1, fy,
                                 fx + ftw - 1, fy + fth / 2.0f - 1,
                                 map->wirecolor, 0);
                }

                /* TODO: Object-library TTL handling is not implemented here.
                 * Intended behavior:
                 *  - Check if object is in the library; if yes, refresh its TTL.
                 *  - If not, add it with the default TTL.
                 */

            } /* if( (a + ...) ) */

        } /* for( a... ) */
    } /* for( b... ) */

    /* TODO: sort the object list by X and by Y and blit all the sprites of the active list */

    /* restore previous target */
    if (prev_target)
        al_set_target_bitmap(prev_target);

    return TRUE;

} /* draw_map( ... ) */

/**
 *@brief Load the map from filename
 *@param map A MAP *pointer to point on the loaded map
 *@param filename The filename of the map to load
 *@return TRUE or FALSE
 */
int load_map(MAP** map, char* filename) {
    ALLEGRO_FILE* loaded;

    char temp_name[1024];
    int temp_XSIZE, temp_YSIZE, temp_TILEW, temp_TILEH;
    int temp_ptanchorX, temp_ptanchorY, temp_X, temp_Y;
    uint32_t temp_bgcolor, temp_wirecolor;

    int it = 0;

    loaded = al_fopen(filename, "rb");
    if (!loaded) {
        n_log(LOG_ERR, "could not open %s for reading", filename);
        return FALSE;
    }

    if (map && *map)
        free_map(map);

    /* reading name */
    it = al_fread32le(loaded);

    if (it > 1023) it = 1023;
    al_fread(loaded, temp_name, (size_t)it);
    temp_name[it] = '\0';

    /* reading states */
    temp_XSIZE = al_fread32le(loaded);
    temp_YSIZE = al_fread32le(loaded);
    temp_TILEW = al_fread32le(loaded);
    temp_TILEH = al_fread32le(loaded);
    temp_ptanchorX = al_fread32le(loaded);
    temp_ptanchorY = al_fread32le(loaded);
    temp_X = al_fread32le(loaded);
    temp_Y = al_fread32le(loaded);
    temp_bgcolor = (uint32_t)al_fread32le(loaded);
    temp_wirecolor = (uint32_t)al_fread32le(loaded);

    if (create_empty_map(map, temp_name,
                         temp_XSIZE, temp_YSIZE,
                         temp_TILEW, temp_TILEH,
                         2000, 2000, 2000, 2000, 2000) != TRUE) {
        al_fclose(loaded);
        return FALSE;
    }
    (*map)->XSIZE = temp_XSIZE;
    (*map)->YSIZE = temp_YSIZE;
    (*map)->TILEW = temp_TILEW;
    (*map)->TILEH = temp_TILEH;
    (*map)->ptanchorX = temp_ptanchorX;
    (*map)->ptanchorY = temp_ptanchorY;
    (*map)->X = temp_X;
    (*map)->Y = temp_Y;
    (*map)->bgcolor = al_map_rgba((unsigned char)((temp_bgcolor >> 24) & 0xFF),
                                  (unsigned char)((temp_bgcolor >> 16) & 0xFF),
                                  (unsigned char)((temp_bgcolor >> 8) & 0xFF),
                                  (unsigned char)(temp_bgcolor & 0xFF));
    (*map)->wirecolor = al_map_rgba((unsigned char)((temp_wirecolor >> 24) & 0xFF),
                                    (unsigned char)((temp_wirecolor >> 16) & 0xFF),
                                    (unsigned char)((temp_wirecolor >> 8) & 0xFF),
                                    (unsigned char)(temp_wirecolor & 0xFF));

    /* loading the grid */
    for (it = 0; it < (*map)->XSIZE; it++) {
        for (int it1 = 0; it1 < (*map)->YSIZE; it1++) {
            (*map)->grid[it + it1 * (*map)->XSIZE].tilenumber = al_fread32le(loaded);
            (*map)->grid[it + it1 * (*map)->XSIZE].ability = al_fread32le(loaded);
            (*map)->grid[it + it1 * (*map)->XSIZE].objectnumber = al_fread32le(loaded);
            (*map)->grid[it + it1 * (*map)->XSIZE].music = al_fread32le(loaded);
        }
    }

    /*load_animlib( loaded , & (*map ) -> libtiles );*/
    /*load_animlib( loaded , & (*map ) -> libanims );*/

    al_fclose(loaded);

    return TRUE;
} /* load_map( ... ) */

/**
 *@brief Save the map to filename
 *@param map A MAP *pointer
 *@param filename The filename of the file where to save the map
 *@return TRUE or FALSE
 */
int save_map(MAP* map, char* filename) {
    ALLEGRO_FILE* saved;

    int it = 0;

    saved = al_fopen(filename, "wb");
    if (!saved) {
        n_log(LOG_ERR, "could not open %s for writing", filename);
        return FALSE;
    }

    /* writing name */
    int name_len = (int)(strlen(map->name) + 1);
    al_fwrite32le(saved, name_len);
    al_fwrite(saved, map->name, (size_t)name_len);

    /* writing states */
    al_fwrite32le(saved, map->XSIZE);
    al_fwrite32le(saved, map->YSIZE);
    al_fwrite32le(saved, map->TILEW);
    al_fwrite32le(saved, map->TILEH);
    al_fwrite32le(saved, map->ptanchorX);
    al_fwrite32le(saved, map->ptanchorY);
    al_fwrite32le(saved, map->X);
    al_fwrite32le(saved, map->Y);

    /* pack ALLEGRO_COLOR into 32-bit RGBA */
    unsigned char r, g, b, a;
    al_unmap_rgba(map->bgcolor, &r, &g, &b, &a);
    al_fwrite32le(saved, (int32_t)(((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a));
    al_unmap_rgba(map->wirecolor, &r, &g, &b, &a);
    al_fwrite32le(saved, (int32_t)(((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a));

    /* saving the grid */
    for (it = 0; it < map->XSIZE; it++) {
        for (int it1 = 0; it1 < map->YSIZE; it1++) {
            al_fwrite32le(saved, map->grid[it + it1 * map->XSIZE].tilenumber);
            al_fwrite32le(saved, map->grid[it + it1 * map->XSIZE].ability);
            al_fwrite32le(saved, map->grid[it + it1 * map->XSIZE].objectnumber);
            al_fwrite32le(saved, map->grid[it + it1 * map->XSIZE].music);
        }
    }

    /*save_animlib( saved , map -> libtiles );*/
    /*save_animlib( saved , map -> libanims );*/

    al_fclose(saved);

    return TRUE;
} /* save_map( ... ) */

/**
 *@brief Free the map
 *@param map The map to free. The pointer will be set to NULL
 *@return TRUE or FALSE
 */
int free_map(MAP** map) {
    if (!(*map))
        return TRUE;
    Free((*map)->grid);
    Free((*map)->name);
    if ((*map)->colortile)
        al_destroy_bitmap((*map)->colortile);
    if ((*map)->mousemap)
        al_destroy_bitmap((*map)->mousemap);

    Free((*map));

    return TRUE;
} /* free_map( ... ) */

#endif /* HAVE_ALLEGRO - end of legacy API */

/*===========================================================================
 * New height-aware ISO_MAP API (Articles 747/748/934/1269/2026)
 * This section does NOT require Allegro and compiles everywhere.
 *=========================================================================*/

/**
 *@brief Create a new height-aware isometric map
 *@param width map width in tiles
 *@param height map height in tiles
 *@param num_terrains number of terrain types
 *@param max_height maximum height value per cell
 *@return new ISO_MAP or NULL on failure
 */
ISO_MAP* iso_map_new(int width, int height, int num_terrains, int max_height) {
    __n_assert(width > 0, return NULL);
    __n_assert(height > 0, return NULL);

    ISO_MAP* map = NULL;
    Malloc(map, ISO_MAP, 1);
    __n_assert(map, return NULL);

    map->width = width;
    map->height = height;
    map->num_terrains = num_terrains;
    map->max_height = max_height;

    size_t total = (size_t)width * (size_t)height;

    Malloc(map->terrain, int, total);
    if (!map->terrain) {
        Free(map);
        return NULL;
    }

    Malloc(map->heightmap, int, total);
    if (!map->heightmap) {
        Free(map->terrain);
        Free(map);
        return NULL;
    }

    Malloc(map->ability, int, total);
    if (!map->ability) {
        Free(map->heightmap);
        Free(map->terrain);
        Free(map);
        return NULL;
    }

    memset(map->terrain, 0, total * sizeof(int));
    memset(map->heightmap, 0, total * sizeof(int));
    for (size_t i = 0; i < total; i++) {
        map->ability[i] = WALK;
    }

    /* default classic 2:1 projection for 64px wide tiles */
    iso_map_set_projection(map, ISO_PROJ_CLASSIC, 64);

    map->segments = NULL; /* allocated on demand by iso_map_set_segments() */

    /* Overlay layer: allocated and zeroed, 0 = no overlay */
    Malloc(map->overlay, int, total);
    if (map->overlay) memset(map->overlay, 0, total * sizeof(int));

    /* default rendering flags */
    map->smooth_height = 0;
    map->smooth_slope_max = 1;
    map->show_grid = 0;
    map->hover_mx = -1;
    map->hover_my = -1;
    map->height_tint_intensity = 0.0f; /* disabled by default; game sets this */

    /* Ambient color defaults: full daylight (no tinting) */
    map->ambient_r = 1.0f;
    map->ambient_g = 1.0f;
    map->ambient_b = 1.0f;
    map->dynamic_light_map = NULL; /* caller-owned, set before draw */

    return map;
} /* iso_map_new() */

/**
 *@brief Free an ISO_MAP and set the pointer to NULL
 *@param map_ptr pointer to the ISO_MAP pointer
 */
void iso_map_free(ISO_MAP** map_ptr) {
    __n_assert(map_ptr, return);
    __n_assert(*map_ptr, return);
    ISO_MAP* map = *map_ptr;
    Free(map->terrain);
    Free(map->heightmap);
    Free(map->ability);
    Free(map->segments);
    Free(map->overlay);
    Free(*map_ptr);
} /* iso_map_free() */

/**
 *@brief Get the angle in degrees for a projection preset.
 *@param preset one of ISO_PROJ_CLASSIC, ISO_PROJ_ISOMETRIC, ISO_PROJ_STAGGERED, ISO_PROJ_MILITARY
 *@return angle in degrees
 */
static float _iso_preset_angle(int preset) {
    switch (preset) {
        case ISO_PROJ_ISOMETRIC:
            return 30.0f;
        case ISO_PROJ_STAGGERED:
            return 18.43f;
        case ISO_PROJ_MILITARY:
            return 45.0f;
        case ISO_PROJ_CLASSIC:
        default:
            return 26.565f;
    }
}

/**
 *@brief Set projection parameters from a preset and tile width
 *@param map the map
 *@param preset one of ISO_PROJ_CLASSIC, ISO_PROJ_ISOMETRIC, ISO_PROJ_STAGGERED, ISO_PROJ_MILITARY
 *@param tile_width full tile width in pixels (half_w = tile_width / 2)
 */
void iso_map_set_projection(ISO_MAP* map, int preset, float tile_width) {
    __n_assert(map, return);
    float hw = tile_width / 2.0f;
    float angle = _iso_preset_angle(preset);
    float hh = hw * tanf(angle * (float)M_PI / 180.0f);

    map->proj.half_w = hw;
    map->proj.half_h = hh;
    map->proj.angle_deg = angle;
    map->proj.target_angle = angle;
    map->proj.lerp_speed = 3.0f;
    map->proj.tile_lift = hh;
} /* iso_map_set_projection() */

/**
 *@brief Set the target projection for smooth interpolation.
 * Call iso_map_lerp_projection() each frame to animate toward it.
 *@param map the map
 *@param preset one of ISO_PROJ_CLASSIC, ISO_PROJ_ISOMETRIC, ISO_PROJ_STAGGERED, ISO_PROJ_MILITARY
 */
void iso_map_set_projection_target(ISO_MAP* map, int preset) {
    __n_assert(map, return);
    map->proj.target_angle = _iso_preset_angle(preset);
} /* iso_map_set_projection_target() */

/**
 *@brief Smoothly interpolate the projection angle toward the target.
 * Updates half_h from the new angle; half_w and tile_lift are unchanged.
 *@param map the map
 *@param dt time step in seconds (e.g. 1.0/60.0)
 */
void iso_map_lerp_projection(ISO_MAP* map, float dt) {
    __n_assert(map, return);
    float diff = map->proj.target_angle - map->proj.angle_deg;
    if (fabsf(diff) < 0.01f) {
        map->proj.angle_deg = map->proj.target_angle;
    } else {
        map->proj.angle_deg += diff * map->proj.lerp_speed * dt;
    }
    float rad = map->proj.angle_deg * (float)M_PI / 180.0f;
    map->proj.half_h = map->proj.half_w * sinf(rad) / cosf(rad);
    if (map->proj.half_h < 8.0f) map->proj.half_h = 8.0f;
    if (map->proj.half_h > map->proj.half_w * 2.0f) map->proj.half_h = map->proj.half_w * 2.0f;
} /* iso_map_lerp_projection() */

/**
 *@brief Get the display name for a projection preset.
 *@param preset projection ID
 *@return static string with the projection name
 */
const char* iso_projection_name(int preset) {
    switch (preset) {
        case ISO_PROJ_CLASSIC:
            return "Classic 2:1";
        case ISO_PROJ_ISOMETRIC:
            return "True Isometric";
        case ISO_PROJ_STAGGERED:
            return "Staggered";
        case ISO_PROJ_MILITARY:
            return "Military";
        default:
            return "Unknown";
    }
} /* iso_projection_name() */

/**
 *@brief Set custom projection parameters directly
 *@param map the map
 *@param half_w half-width of tile diamond
 *@param half_h half-height of tile diamond
 *@param tile_lift vertical pixel offset per height unit
 */
void iso_map_set_projection_custom(ISO_MAP* map, float half_w, float half_h, float tile_lift) {
    __n_assert(map, return);
    map->proj.half_w = half_w;
    map->proj.half_h = half_h;
    map->proj.tile_lift = tile_lift;
    map->proj.angle_deg = atanf(half_h / half_w) * 180.0f / (float)M_PI;
    map->proj.target_angle = map->proj.angle_deg;
} /* iso_map_set_projection_custom() */

/**
 *@brief Get the terrain type at a cell
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@return terrain type, or 0 if out of bounds
 */
int iso_map_get_terrain(const ISO_MAP* map, int mx, int my) {
    __n_assert(map, return 0);
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return 0;
    return map->terrain[my * map->width + mx];
} /* iso_map_get_terrain() */

/**
 *@brief Set the terrain type at a cell
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@param terrain terrain type index
 */
void iso_map_set_terrain(ISO_MAP* map, int mx, int my, int terrain) {
    __n_assert(map, return);
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return;
    map->terrain[my * map->width + mx] = terrain;
} /* iso_map_set_terrain() */

/**
 *@brief Get the height at a cell
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@return height value, or 0 if out of bounds
 */
int iso_map_get_height(const ISO_MAP* map, int mx, int my) {
    __n_assert(map, return 0);
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return 0;
    return map->heightmap[my * map->width + mx];
} /* iso_map_get_height() */

/**
 *@brief Set the height at a cell (clamped to [0, max_height])
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@param h height value
 */
void iso_map_set_height(ISO_MAP* map, int mx, int my, int h) {
    __n_assert(map, return);
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return;
    if (h < 0) h = 0;
    if (h > map->max_height) h = map->max_height;
    map->heightmap[my * map->width + mx] = h;
} /* iso_map_set_height() */

/**
 *@brief Get the ability at a cell
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@return ability value (WALK/SWIM/BLCK), or 0 if out of bounds
 */
int iso_map_get_ability(const ISO_MAP* map, int mx, int my) {
    __n_assert(map, return 0);
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return 0;
    return map->ability[my * map->width + mx];
} /* iso_map_get_ability() */

/**
 *@brief Set the ability at a cell
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@param ab ability value (WALK/SWIM/BLCK)
 */
void iso_map_set_ability(ISO_MAP* map, int mx, int my, int ab) {
    __n_assert(map, return);
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return;
    map->ability[my * map->width + mx] = ab;
} /* iso_map_set_ability() */

/**
 *@brief Convert map tile coordinates to screen pixel coordinates.
 * Uses the standard diamond isometric formula:
 *   sx = (mx - my) * half_w + half_w
 *   sy = (mx + my) * half_h - h * tile_lift
 *@param map the map (uses map->proj)
 *@param mx tile X
 *@param my tile Y
 *@param h height value at this tile
 *@param screen_x output X in pixels
 *@param screen_y output Y in pixels
 */
void iso_map_to_screen(const ISO_MAP* map, int mx, int my, int h, float* screen_x, float* screen_y) {
    __n_assert(map, return);
    __n_assert(screen_x, return);
    __n_assert(screen_y, return);
    *screen_x = ((float)mx - (float)my) * map->proj.half_w + map->proj.half_w;
    *screen_y = ((float)mx + (float)my) * map->proj.half_h - (float)h * map->proj.tile_lift;
} /* iso_map_to_screen() */

/**
 *@brief Convert screen pixel coordinates to map tile coordinates.
 * Inverse of iso_map_to_screen (without height, assumes h=0).
 * Uses the diamond center to find which tile a screen pixel belongs to.
 *@param map the map (uses map->proj)
 *@param screen_x X in pixels
 *@param screen_y Y in pixels
 *@param mx output tile X
 *@param my output tile Y
 */
void iso_screen_to_map(const ISO_MAP* map, float screen_x, float screen_y, int* mx, int* my) {
    __n_assert(map, return);
    __n_assert(mx, return);
    __n_assert(my, return);
    float tw = 2.0f * map->proj.half_w;
    float th = 2.0f * map->proj.half_h;
    *mx = (int)floorf(screen_x / tw + screen_y / th - 1.0f);
    *my = (int)floorf(screen_y / th - screen_x / tw + 1.0f);
} /* iso_screen_to_map() */

/**
 *@brief Convert map coordinates to screen coordinates (float version).
 * Maps fractional tile positions to world screen coordinates.
 * Unlike iso_map_to_screen (which returns the bounding box top-left),
 * this returns the actual diamond position: (mx, my) maps to the N vertex
 * and (mx+0.5, my+0.5) maps to the diamond center.
 *@param map the map (uses map->proj)
 *@param fmx fractional tile X
 *@param fmy fractional tile Y
 *@param h height value (float for smooth interpolation)
 *@param screen_x output X in pixels
 *@param screen_y output Y in pixels
 */
void iso_map_to_screen_f(const ISO_MAP* map, float fmx, float fmy, float h, float* screen_x, float* screen_y) {
    __n_assert(map, return);
    __n_assert(screen_x, return);
    __n_assert(screen_y, return);
    *screen_x = (fmx - fmy) * map->proj.half_w + 2.0f * map->proj.half_w;
    *screen_y = (fmx + fmy) * map->proj.half_h - h * map->proj.tile_lift;
} /* iso_map_to_screen_f() */

/**
 *@brief Project a tile corner to screen coordinates (canonical formula).
 *
 * Public version of the canonical vertex projection used by the renderer.
 * cam_px and cam_py must be integer-snapped pixel offsets:
 * floor(cam_x * zoom), floor(cam_y * zoom).  This eliminates sub-pixel
 * aliasing shimmer by keeping each vertex's fractional pixel position
 * constant across frames.
 *
 *@param map    the map (uses map->proj)
 *@param cx     corner X in tile grid (e.g. mx+1 for the East corner)
 *@param cy     corner Y in tile grid
 *@param fh     height (float, supports smooth-height interpolation)
 *@param cam_px integer-snapped camera pixel X: floor(cam_x * zoom)
 *@param cam_py integer-snapped camera pixel Y: floor(cam_y * zoom)
 *@param zoom   zoom factor
 *@param sx     output screen X
 *@param sy     output screen Y
 */
void iso_corner_to_screen(const ISO_MAP* map, int cx, int cy, float fh, float cam_px, float cam_py, float zoom, float* sx, float* sy) {
    __n_assert(map, return);
    __n_assert(sx, return);
    __n_assert(sy, return);
    float hw = map->proj.half_w;
    float hh = map->proj.half_h;
    float tl = map->proj.tile_lift;
    float wx = (float)(cx - cy) * hw + 2.0f * hw;
    float wy = (float)(cx + cy) * hh - fh * tl;
    *sx = wx * zoom + cam_px;
    *sy = wy * zoom + cam_py;
} /* iso_corner_to_screen() */

/**
 *@brief Test if pixel (px,py) is inside the isometric diamond.
 * The diamond is centered in a tile_w x tile_h rectangle.
 *@param px pixel X (0..tile_w-1)
 *@param py pixel Y (0..tile_h-1)
 *@param tile_w tile width in pixels
 *@param tile_h tile height in pixels
 *@return 1 if inside, 0 if outside
 */
int iso_is_in_diamond(int px, int py, int tile_w, int tile_h) {
    float cx = (float)tile_w / 2.0f;
    float cy = (float)tile_h / 2.0f;
    float dx = fabsf((float)px + 0.5f - cx) / ((float)tile_w / 2.0f);
    float dy = fabsf((float)py + 0.5f - cy) / ((float)tile_h / 2.0f);
    return (dx + dy) <= 1.0f;
} /* iso_is_in_diamond() */

/**
 *@brief Distance from pixel to the diamond edge.
 * Returns 0 at the edge, 1 at the center. Pixels outside the diamond
 * return 0.
 *@param px pixel X
 *@param py pixel Y
 *@param tile_w tile width in pixels
 *@param tile_h tile height in pixels
 *@return distance in [0, 1]
 */
float iso_diamond_dist(int px, int py, int tile_w, int tile_h) {
    float cx = (float)tile_w / 2.0f;
    float cy = (float)tile_h / 2.0f;
    float dx = fabsf((float)px + 0.5f - cx) / ((float)tile_w / 2.0f);
    float dy = fabsf((float)py + 0.5f - cy) / ((float)tile_h / 2.0f);
    float d = dx + dy;
    if (d > 1.0f) return 0.0f;
    return 1.0f - d;
} /* iso_diamond_dist() */

/**
 *@brief Height-aware screen-to-map conversion with diamond hit testing.
 * Iterates from max_height down to 0, testing each height layer for a
 * tile that matches the screen coordinates. Uses iso_is_in_diamond for
 * precise pixel-level hit detection within the diamond boundary.
 *@param map the map
 *@param screen_x world X coordinate (from camera screen-to-world)
 *@param screen_y world Y coordinate
 *@param tile_w tile width in pixels (for diamond test)
 *@param tile_h tile height in pixels (for diamond test)
 *@param mx output tile X
 *@param my output tile Y
 */
void iso_screen_to_map_height(const ISO_MAP* map, float screen_x, float screen_y, int tile_w, int tile_h, int* mx, int* my) {
    __n_assert(map, return);
    __n_assert(mx, return);
    __n_assert(my, return);

    float half_w = map->proj.half_w;
    float half_h = map->proj.half_h;
    float tile_lift = map->proj.tile_lift;
    float tw = half_w * 2.0f;
    float th = half_h * 2.0f;

    for (int h = map->max_height; h >= 0; h--) {
        float adj_sy = screen_y + (float)h * tile_lift;
        float fmx = screen_x / tw + adj_sy / th - 1.0f;
        float fmy = adj_sy / th - screen_x / tw + 1.0f;
        int tmx = (int)floorf(fmx);
        int tmy = (int)floorf(fmy);

        if (tmx < 0 || tmx >= map->width || tmy < 0 || tmy >= map->height)
            continue;

        if (iso_map_get_height(map, tmx, tmy) == h) {
            float tile_sx, tile_sy;
            iso_map_to_screen(map, tmx, tmy, h, &tile_sx, &tile_sy);
            float norm_x = (screen_x - tile_sx) / tw * (float)tile_w;
            float norm_y = (screen_y - tile_sy) / th * (float)tile_h;
            int lpx = (int)norm_x;
            int lpy = (int)norm_y;
            if (lpx >= 0 && lpx < tile_w && lpy >= 0 && lpy < tile_h &&
                iso_is_in_diamond(lpx, lpy, tile_w, tile_h)) {
                *mx = tmx;
                *my = tmy;
                return;
            }
        }
    }

    /* Fallback: flat projection (height 0) */
    *mx = (int)floorf(screen_x / tw + screen_y / th - 1.0f);
    *my = (int)floorf(screen_y / th - screen_x / tw + 1.0f);
} /* iso_screen_to_map_height() */

/**
 *@brief Height-aware screen-to-map conversion returning fractional tile coordinates.
 * Works like iso_screen_to_map_height but additionally outputs the exact
 * fractional tile position via out_fx/out_fy.
 *@param map the iso map
 *@param screen_x world-space X
 *@param screen_y world-space Y
 *@param tile_w tile bitmap width in pixels (for diamond hit test)
 *@param tile_h tile bitmap height in pixels
 *@param mx receives integer tile X
 *@param my receives integer tile Y
 *@param out_fx receives fractional tile X (NULL-safe)
 *@param out_fy receives fractional tile Y (NULL-safe)
 */
void iso_screen_to_map_height_f(const ISO_MAP* map, float screen_x, float screen_y, int tile_w, int tile_h, int* mx, int* my, float* out_fx, float* out_fy) {
    __n_assert(map, return);
    __n_assert(mx, return);
    __n_assert(my, return);

    float half_w = map->proj.half_w;
    float half_h = map->proj.half_h;
    float tile_lift = map->proj.tile_lift;
    float tw = half_w * 2.0f;
    float th = half_h * 2.0f;

    for (int h = map->max_height; h >= 0; h--) {
        float adj_sy = screen_y + (float)h * tile_lift;
        float fmx = screen_x / tw + adj_sy / th - 1.0f;
        float fmy = adj_sy / th - screen_x / tw + 1.0f;
        int tmx = (int)floorf(fmx);
        int tmy = (int)floorf(fmy);

        if (tmx < 0 || tmx >= map->width || tmy < 0 || tmy >= map->height)
            continue;

        if (iso_map_get_height(map, tmx, tmy) == h) {
            float tile_sx, tile_sy;
            iso_map_to_screen(map, tmx, tmy, h, &tile_sx, &tile_sy);
            float norm_x = (screen_x - tile_sx) / tw * (float)tile_w;
            float norm_y = (screen_y - tile_sy) / th * (float)tile_h;
            int lpx = (int)norm_x;
            int lpy = (int)norm_y;
            if (lpx >= 0 && lpx < tile_w && lpy >= 0 && lpy < tile_h &&
                iso_is_in_diamond(lpx, lpy, tile_w, tile_h)) {
                *mx = tmx;
                *my = tmy;
                if (out_fx || out_fy) {
                    /* Refine fractional coordinates using interpolated height
                     * so that iso_map_to_screen_f(fx, fy, interp_h) returns
                     * the original screen position.  Without this, sub-tile
                     * positions are off when the bilinear-interpolated height
                     * differs from the tile's integer height (e.g. at edges
                     * between tiles of different heights).
                     *
                     * Only refine in SMOOTH mode.  In CUT mode the rendered
                     * tile sits at the flat integer height, so the initial
                     * fmx/fmy (computed at that height) are already correct.
                     * Refining with bilinear-interpolated heights in CUT mode
                     * pulls the position toward tile corners and causes
                     * click-to-move to land at the wrong spot. */
                    float refined_fx = fmx;
                    float refined_fy = fmy;
                    if (map->smooth_height) {
                        /* Upper bounds: allow the full extent of the last tile.
                         * Tile tmx occupies [tmx, tmx+1) in map coords, and the
                         * last valid position must stay < map->width so that
                         * floor() never yields an out-of-bounds tile index. */
                        float map_max_fx = (float)map->width - 1e-4f;
                        float map_max_fy = (float)map->height - 1e-4f;
                        /* Tile-local bounds to prevent divergence: clamp each
                         * refinement step to the detected tile so that large
                         * height differences with neighbours cannot pull the
                         * position into an adjacent tile. */
                        float tile_min_fx = (float)tmx;
                        float tile_min_fy = (float)tmy;
                        float tile_max_fx = (float)(tmx + 1);
                        float tile_max_fy = (float)(tmy + 1);
                        if (tile_max_fx > map_max_fx) tile_max_fx = map_max_fx;
                        if (tile_max_fy > map_max_fy) tile_max_fy = map_max_fy;
                        for (int iter = 0; iter < 3; iter++) {
                            float interp_h = iso_map_interpolate_height(map, refined_fx, refined_fy);
                            float new_adj_sy = screen_y + interp_h * tile_lift;
                            refined_fx = screen_x / tw + new_adj_sy / th - 1.0f;
                            refined_fy = new_adj_sy / th - screen_x / tw + 1.0f;
                            /* Keep within detected tile */
                            if (refined_fx < tile_min_fx) refined_fx = tile_min_fx;
                            if (refined_fy < tile_min_fy) refined_fy = tile_min_fy;
                            if (refined_fx > tile_max_fx) refined_fx = tile_max_fx;
                            if (refined_fy > tile_max_fy) refined_fy = tile_max_fy;
                        }
                    }
                    /* Clamp to map bounds so border clicks stay valid */
                    float map_max_fx2 = (float)map->width - 1e-4f;
                    float map_max_fy2 = (float)map->height - 1e-4f;
                    if (refined_fx < 0.0f) refined_fx = 0.0f;
                    if (refined_fy < 0.0f) refined_fy = 0.0f;
                    if (refined_fx > map_max_fx2) refined_fx = map_max_fx2;
                    if (refined_fy > map_max_fy2) refined_fy = map_max_fy2;
                    if (out_fx) *out_fx = refined_fx;
                    if (out_fy) *out_fy = refined_fy;
                }
                return;
            }
        }
    }

    /* Fallback: flat projection (height 0) */
    float fmx = screen_x / tw + screen_y / th - 1.0f;
    float fmy = screen_y / th - screen_x / tw + 1.0f;
    *mx = (int)floorf(fmx);
    *my = (int)floorf(fmy);
    if (out_fx) *out_fx = fmx;
    if (out_fy) *out_fy = fmy;
} /* iso_screen_to_map_height_f() */

/**
 *@brief Bilinear height interpolation at fractional map coordinates.
 * Samples the 4 surrounding tile corners and interpolates for smooth terrain.
 *@param map the map
 *@param fx fractional tile X (e.g. 3.5 = halfway between tile 3 and 4)
 *@param fy fractional tile Y
 *@return interpolated height value
 */
float iso_map_interpolate_height(const ISO_MAP* map, float fx, float fy) {
    __n_assert(map, return 0.0f);

    /* Clamp to valid map range so border tiles interpolate correctly.
     * Allow the full extent of the last tile (up to width/height minus
     * a tiny epsilon) so that positions in the right half of border
     * tiles are not snapped to the tile corner. */
    if (fx < 0.0f) fx = 0.0f;
    if (fy < 0.0f) fy = 0.0f;
    float max_fx = (float)map->width - 1e-4f;
    float max_fy = (float)map->height - 1e-4f;
    if (fx > max_fx) fx = max_fx;
    if (fy > max_fy) fy = max_fy;

    int ix = (int)floorf(fx);
    int iy = (int)floorf(fy);
    float frac_x = fx - (float)ix;
    float frac_y = fy - (float)iy;

    /* Clamp neighbor indices for edge tiles */
    int ix1 = (ix + 1 < map->width) ? ix + 1 : ix;
    int iy1 = (iy + 1 < map->height) ? iy + 1 : iy;

    float h00 = (float)iso_map_get_height(map, ix, iy);
    float h10 = (float)iso_map_get_height(map, ix1, iy);
    float h01 = (float)iso_map_get_height(map, ix, iy1);
    float h11 = (float)iso_map_get_height(map, ix1, iy1);

    float top = h00 + (h10 - h00) * frac_x;
    float bot = h01 + (h11 - h01) * frac_x;
    return top + (bot - top) * frac_y;
} /* iso_map_interpolate_height() */

/**
 *@brief Compute terrain transition bitmasks for a cell (Article 934).
 * For a given terrain at (mx, my), checks all 8 neighbors. If a neighbor
 * has a higher-priority (lower index) terrain, the corresponding edge or
 * corner bit is set. Corner bits are only set when the adjacent edges
 * do NOT have the foreign terrain (to avoid redundant overlaps).
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@param edge_bits output: 4-bit edge mask (W, N, E, S)
 *@param corner_bits output: 4-bit corner mask (NW, NE, SE, SW)
 */
void iso_map_calc_transitions(const ISO_MAP* map, int mx, int my, int* edge_bits, int* corner_bits) {
    __n_assert(map, return);
    __n_assert(edge_bits, return);
    __n_assert(corner_bits, return);

    int center = iso_map_get_terrain(map, mx, my);
    *edge_bits = 0;
    *corner_bits = 0;

    /* Edge neighbors: W(-1,0), N(0,-1), E(+1,0), S(0,+1) */
    int w = iso_map_get_terrain(map, mx - 1, my);
    int n = iso_map_get_terrain(map, mx, my - 1);
    int e = iso_map_get_terrain(map, mx + 1, my);
    int s = iso_map_get_terrain(map, mx, my + 1);

    if (w != center) *edge_bits |= ISO_EDGE_W;
    if (n != center) *edge_bits |= ISO_EDGE_N;
    if (e != center) *edge_bits |= ISO_EDGE_E;
    if (s != center) *edge_bits |= ISO_EDGE_S;

    /* Corner neighbors: only set if adjacent edges don't already have that terrain */
    int nw = iso_map_get_terrain(map, mx - 1, my - 1);
    int ne = iso_map_get_terrain(map, mx + 1, my - 1);
    int se = iso_map_get_terrain(map, mx + 1, my + 1);
    int sw = iso_map_get_terrain(map, mx - 1, my + 1);

    if (nw != center && nw != w && nw != n) *corner_bits |= ISO_CORNER_NW;
    if (ne != center && ne != n && ne != e) *corner_bits |= ISO_CORNER_NE;
    if (se != center && se != e && se != s) *corner_bits |= ISO_CORNER_SE;
    if (sw != center && sw != s && sw != w) *corner_bits |= ISO_CORNER_SW;
} /* iso_map_calc_transitions() */

/**
 *@brief Check if terrain blending should occur between two adjacent cells
 *@param map the map
 *@param mx1 first tile X
 *@param my1 first tile Y
 *@param mx2 second tile X
 *@param my2 second tile Y
 *@return 1 if terrains differ (blending needed), 0 if same
 */
int iso_map_should_transition(const ISO_MAP* map, int mx1, int my1, int mx2, int my2) {
    __n_assert(map, return 0);
    return iso_map_get_terrain(map, mx1, my1) != iso_map_get_terrain(map, mx2, my2);
} /* iso_map_should_transition() */

/**
 *@brief Compute average corner heights for smooth tile rendering (Article 2026).
 * Each diamond corner is shared by 4 tiles. This averages the heights
 * of the sharing tiles to produce smooth corners.
 * North corner = average of (mx,my), (mx-1,my), (mx,my-1), (mx-1,my-1).
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@param h_n output: north (top) corner height
 *@param h_e output: east (right) corner height
 *@param h_s output: south (bottom) corner height
 *@param h_w output: west (left) corner height
 */
void iso_map_corner_heights(const ISO_MAP* map, int mx, int my, float* h_n, float* h_e, float* h_s, float* h_w) {
    __n_assert(map, return);
    __n_assert(h_n, return);
    __n_assert(h_e, return);
    __n_assert(h_s, return);
    __n_assert(h_w, return);

    float c = (float)iso_map_get_height(map, mx, my);
    float nw = (float)iso_map_get_height(map, mx - 1, my - 1);
    float nn = (float)iso_map_get_height(map, mx, my - 1);
    float ne = (float)iso_map_get_height(map, mx + 1, my - 1);
    float ee = (float)iso_map_get_height(map, mx + 1, my);
    float se = (float)iso_map_get_height(map, mx + 1, my + 1);
    float ss = (float)iso_map_get_height(map, mx, my + 1);
    float sw = (float)iso_map_get_height(map, mx - 1, my + 1);
    float ww = (float)iso_map_get_height(map, mx - 1, my);

    *h_n = (c + nw + nn + ww) / 4.0f;
    *h_e = (c + nn + ne + ee) / 4.0f;
    *h_s = (c + ee + se + ss) / 4.0f;
    *h_w = (c + ss + sw + ww) / 4.0f;
} /* iso_map_corner_heights() */

/**
 *@brief Save ISO_MAP to a binary file.
 * Format: 4-byte magic "ISOM", then int32 fields: width, height,
 * num_terrains, max_height, followed by terrain[], heightmap[], ability[].
 *@param map the map
 *@param filename output file path
 *@return TRUE on success, FALSE on failure
 */
int iso_map_save(const ISO_MAP* map, const char* filename) {
    __n_assert(map, return FALSE);
    __n_assert(filename, return FALSE);

    FILE* f = fopen(filename, "wb");
    if (!f) {
        n_log(LOG_ERR, "iso_map_save: could not open %s for writing", filename);
        return FALSE;
    }

    /* magic */
    fwrite("ISOM", 1, 4, f);

    /* header */
    int32_t hdr[4] = {map->width, map->height, map->num_terrains, map->max_height};
    fwrite(hdr, sizeof(int32_t), 4, f);

    size_t total = (size_t)map->width * (size_t)map->height;

    /* terrain layer */
    fwrite(map->terrain, sizeof(int), total, f);
    /* heightmap layer */
    fwrite(map->heightmap, sizeof(int), total, f);
    /* ability layer */
    fwrite(map->ability, sizeof(int), total, f);

    fclose(f);
    return TRUE;
} /* iso_map_save() */

/**
 *@brief Load ISO_MAP from a binary file
 *@param filename input file path
 *@return loaded ISO_MAP, or NULL on failure
 */
ISO_MAP* iso_map_load(const char* filename) {
    __n_assert(filename, return NULL);

    FILE* f = fopen(filename, "rb");
    if (!f) {
        n_log(LOG_ERR, "iso_map_load: could not open %s for reading", filename);
        return NULL;
    }

    /* check magic */
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 ||
        magic[0] != 'I' || magic[1] != 'S' || magic[2] != 'O' || magic[3] != 'M') {
        n_log(LOG_ERR, "iso_map_load: invalid file format");
        fclose(f);
        return NULL;
    }

    int32_t hdr[4];
    if (fread(hdr, sizeof(int32_t), 4, f) != 4) {
        n_log(LOG_ERR, "iso_map_load: truncated header");
        fclose(f);
        return NULL;
    }

    ISO_MAP* map = iso_map_new(hdr[0], hdr[1], hdr[2], hdr[3]);
    if (!map) {
        fclose(f);
        return NULL;
    }

    size_t total = (size_t)map->width * (size_t)map->height;
    size_t r = 0;
    r = fread(map->terrain, sizeof(int), total, f);
    if (r == total && !feof(f))
        r += fread(map->heightmap, sizeof(int), total, f);
    if (r == total * 2 && !feof(f))
        r += fread(map->ability, sizeof(int), total, f);

    if (r != total * 3) {
        n_log(LOG_ERR, "iso_map_load: truncated data");
        iso_map_free(&map);
        fclose(f);
        return NULL;
    }

    fclose(f);
    return map;
} /* iso_map_load() */

/**
 *@brief Randomize terrain and height for testing/demo purposes
 *@param map the map
 */
void iso_map_randomize(ISO_MAP* map) {
    __n_assert(map, return);
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            int idx = y * map->width + x;
            map->terrain[idx] = rand() % map->num_terrains;
            map->heightmap[idx] = rand() % (map->max_height + 1);
            map->ability[idx] = WALK;
        }
    }
} /* iso_map_randomize() */

/**
 *@brief Helper: clamp neighbor height for smooth slope rendering.
 * If the height difference exceeds smooth_slope_max, return self height
 * (creating a cliff instead of a slope).
 */
static inline float _smooth_neighbor_h(const ISO_MAP* map, float h_self, int nx, int ny) {
    float nh = (float)iso_map_get_height(map, nx, ny);
    if (fabsf(h_self - nh) > (float)map->smooth_slope_max) return h_self;
    return nh;
}

/**
 *@brief Compute smooth corner heights with slope clamping.
 * Like iso_map_corner_heights but clamps neighbors that exceed
 * smooth_slope_max to prevent slopes across cliffs.
 *@param map the map (uses map->smooth_slope_max)
 *@param mx tile X
 *@param my tile Y
 *@param h_n output north corner
 *@param h_e output east corner
 *@param h_s output south corner
 *@param h_w output west corner
 */
void iso_map_smooth_corner_heights(const ISO_MAP* map, int mx, int my, float* h_n, float* h_e, float* h_s, float* h_w) {
    __n_assert(map, return);
    __n_assert(h_n, return);
    __n_assert(h_e, return);
    __n_assert(h_s, return);
    __n_assert(h_w, return);

    float h = (float)iso_map_get_height(map, mx, my);

    *h_n = (h + _smooth_neighbor_h(map, h, mx - 1, my) + _smooth_neighbor_h(map, h, mx, my - 1) + _smooth_neighbor_h(map, h, mx - 1, my - 1)) / 4.0f;

    *h_e = (h + _smooth_neighbor_h(map, h, mx, my - 1) + _smooth_neighbor_h(map, h, mx + 1, my) + _smooth_neighbor_h(map, h, mx + 1, my - 1)) / 4.0f;

    *h_s = (h + _smooth_neighbor_h(map, h, mx + 1, my) + _smooth_neighbor_h(map, h, mx, my + 1) + _smooth_neighbor_h(map, h, mx + 1, my + 1)) / 4.0f;

    *h_w = (h + _smooth_neighbor_h(map, h, mx - 1, my) + _smooth_neighbor_h(map, h, mx, my + 1) + _smooth_neighbor_h(map, h, mx - 1, my + 1)) / 4.0f;
} /* iso_map_smooth_corner_heights() */

/*---------------------------------------------------------------------------
 * Per-tile segment API
 *-------------------------------------------------------------------------*/

int iso_map_set_segments(ISO_MAP* map, int mx, int my, const ISO_TILE_SEGMENT* segs, int count) {
    __n_assert(map, return 0);
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return 0;
    if (count < 0 || count > ISO_MAX_SEGMENTS_PER_TILE) return 0;

    /* Validate invariants */
    for (int i = 0; i < count; i++) {
        if (segs[i].bottom >= segs[i].top) return 0;
        if (i > 0 && segs[i - 1].top > segs[i].bottom) return 0;
    }

    /* Allocate segment array on first use */
    if (!map->segments) {
        size_t total = (size_t)map->width * (size_t)map->height;
        Malloc(map->segments, ISO_TILE_SEGMENTS, total);
        if (!map->segments) return 0;
        memset(map->segments, 0, total * sizeof(ISO_TILE_SEGMENTS));
    }

    int idx = my * map->width + mx;
    map->segments[idx].count = count;
    for (int i = 0; i < count; i++) map->segments[idx].segs[i] = segs[i];
    for (int i = count; i < ISO_MAX_SEGMENTS_PER_TILE; i++) {
        map->segments[idx].segs[i].bottom = 0;
        map->segments[idx].segs[i].top = 0;
        map->segments[idx].segs[i].upper_tile = -1;
        map->segments[idx].segs[i].lower_tile = -1;
    }

    /* Update derived heightmap */
    map->heightmap[idx] = (count > 0) ? segs[count - 1].top : 0;
    return 1;
}

const ISO_TILE_SEGMENTS* iso_map_get_segments(const ISO_MAP* map, int mx, int my) {
    if (!map || !map->segments) return NULL;
    if (mx < 0 || mx >= map->width || my < 0 || my >= map->height) return NULL;
    return &map->segments[my * map->width + mx];
}

static int _cmp_draw_entry(const void* a, const void* b) {
    const ISO_DRAW_ENTRY* ea = (const ISO_DRAW_ENTRY*)a;
    const ISO_DRAW_ENTRY* eb = (const ISO_DRAW_ENTRY*)b;
    /* Primary: isometric depth (back-to-front) */
    int depth_a = ea->my + ea->mx;
    int depth_b = eb->my + eb->mx;
    if (depth_a != depth_b) return depth_a - depth_b;
    /* Secondary: segment base height (lower drawn first) */
    if (ea->bottom != eb->bottom) return ea->bottom - eb->bottom;
    /* Tertiary: row then column */
    if (ea->my != eb->my) return ea->my - eb->my;
    return ea->mx - eb->mx;
}

int iso_map_build_draw_order(const ISO_MAP* map, ISO_DRAW_ENTRY* out, int max_entries) {
    __n_assert(map, return 0);
    __n_assert(out, return 0);

    int n = 0;
    for (int my = 0; my < map->height; my++) {
        for (int mx = 0; mx < map->width; mx++) {
            int idx = my * map->width + mx;
            if (map->segments && map->segments[idx].count > 0) {
                const ISO_TILE_SEGMENTS* ts = &map->segments[idx];
                for (int si = 0; si < ts->count && n < max_entries; si++) {
                    out[n].mx = mx;
                    out[n].my = my;
                    out[n].seg_idx = si;
                    out[n].bottom = ts->segs[si].bottom;
                    out[n].top = ts->segs[si].top;
                    /* underside: bottom > 0 and no segment directly below */
                    out[n].underside = (ts->segs[si].bottom > 0 &&
                                        (si == 0 || ts->segs[si - 1].top < ts->segs[si].bottom));
                    n++;
                }
            } else {
                /* No segments or count==0: single entry from heightmap */
                if (n >= max_entries) continue;
                int h = map->heightmap[idx];
                out[n].mx = mx;
                out[n].my = my;
                out[n].seg_idx = 0;
                out[n].bottom = 0;
                out[n].top = h;
                out[n].underside = 0;
                n++;
            }
        }
    }

    qsort(out, (size_t)n, sizeof(ISO_DRAW_ENTRY), _cmp_draw_entry);
    return n;
}

/**
 *@brief Check if terrain transition should render between two cells (height-aware).
 * In cut mode: only same-height tiles get transitions.
 * In smooth mode: tiles within smooth_slope_max get transitions.
 *@return 1 if transition should render, 0 otherwise
 */
int iso_map_should_transition_smooth(const ISO_MAP* map, int mx1, int my1, int mx2, int my2) {
    __n_assert(map, return 0);
    int h1 = iso_map_get_height(map, mx1, my1);
    int h2 = iso_map_get_height(map, mx2, my2);
    if (map->smooth_height)
        return abs(h1 - h2) <= map->smooth_slope_max;
    return h1 == h2;
} /* iso_map_should_transition_smooth() */

/**
 *@brief Compute per-terrain transition bitmasks with height filtering.
 * Iterates terrains above the base terrain and sets edge/corner bits
 * for each terrain that has higher visual priority than the cell.
 * edge_bits and corner_bits must be arrays of at least map->num_terrains.
 *@param map the map
 *@param mx tile X
 *@param my tile Y
 *@param edge_bits output array [num_terrains] of 4-bit edge masks
 *@param corner_bits output array [num_terrains] of 4-bit corner masks
 */
void iso_map_calc_transitions_full(const ISO_MAP* map, int mx, int my, int* edge_bits, int* corner_bits) {
    __n_assert(map, return);
    __n_assert(edge_bits, return);
    __n_assert(corner_bits, return);

    int base = iso_map_get_terrain(map, mx, my);
    memset(edge_bits, 0, sizeof(int) * (size_t)map->num_terrains);
    memset(corner_bits, 0, sizeof(int) * (size_t)map->num_terrains);

    /* Cardinal neighbors: only if height-connected */
    int t_w = iso_map_should_transition_smooth(map, mx, my, mx - 1, my)
                  ? iso_map_get_terrain(map, mx - 1, my)
                  : base;
    int t_n = iso_map_should_transition_smooth(map, mx, my, mx, my - 1)
                  ? iso_map_get_terrain(map, mx, my - 1)
                  : base;
    int t_e = iso_map_should_transition_smooth(map, mx, my, mx + 1, my)
                  ? iso_map_get_terrain(map, mx + 1, my)
                  : base;
    int t_s = iso_map_should_transition_smooth(map, mx, my, mx, my + 1)
                  ? iso_map_get_terrain(map, mx, my + 1)
                  : base;

    /* Diagonal neighbors */
    int t_nw = iso_map_should_transition_smooth(map, mx, my, mx - 1, my - 1)
                   ? iso_map_get_terrain(map, mx - 1, my - 1)
                   : base;
    int t_ne = iso_map_should_transition_smooth(map, mx, my, mx + 1, my - 1)
                   ? iso_map_get_terrain(map, mx + 1, my - 1)
                   : base;
    int t_se = iso_map_should_transition_smooth(map, mx, my, mx + 1, my + 1)
                   ? iso_map_get_terrain(map, mx + 1, my + 1)
                   : base;
    int t_sw = iso_map_should_transition_smooth(map, mx, my, mx - 1, my + 1)
                   ? iso_map_get_terrain(map, mx - 1, my + 1)
                   : base;

    for (int t = base + 1; t < map->num_terrains; t++) {
        if (t_w == t) edge_bits[t] |= ISO_EDGE_W;
        if (t_n == t) edge_bits[t] |= ISO_EDGE_N;
        if (t_e == t) edge_bits[t] |= ISO_EDGE_E;
        if (t_s == t) edge_bits[t] |= ISO_EDGE_S;

        if (t_nw == t && !(edge_bits[t] & ISO_EDGE_W) && !(edge_bits[t] & ISO_EDGE_N))
            corner_bits[t] |= ISO_CORNER_NW;
        if (t_ne == t && !(edge_bits[t] & ISO_EDGE_N) && !(edge_bits[t] & ISO_EDGE_E))
            corner_bits[t] |= ISO_CORNER_NE;
        if (t_se == t && !(edge_bits[t] & ISO_EDGE_E) && !(edge_bits[t] & ISO_EDGE_S))
            corner_bits[t] |= ISO_CORNER_SE;
        if (t_sw == t && !(edge_bits[t] & ISO_EDGE_S) && !(edge_bits[t] & ISO_EDGE_W))
            corner_bits[t] |= ISO_CORNER_SW;
    }
} /* iso_map_calc_transitions_full() */

/*===========================================================================
 * N_ISO_CAMERA
 *=========================================================================*/

/**
 *@brief Create a new 2D isometric camera.
 */
N_ISO_CAMERA* n_iso_camera_new(float zoom_min, float zoom_max) {
    N_ISO_CAMERA* cam = NULL;
    Malloc(cam, N_ISO_CAMERA, 1);
    __n_assert(cam, return NULL);
    cam->x = 0.0f;
    cam->y = 0.0f;
    cam->zoom = 1.0f;
    cam->zoom_min = zoom_min;
    cam->zoom_max = zoom_max;
    return cam;
} /* n_iso_camera_new() */

/**
 *@brief Free a camera and set the pointer to NULL.
 */
void n_iso_camera_free(N_ISO_CAMERA** cam) {
    __n_assert(cam && *cam, return);
    Free(*cam);
} /* n_iso_camera_free() */

/**
 *@brief Scroll the camera by (dx, dy) world units.
 */
void n_iso_camera_scroll(N_ISO_CAMERA* cam, float dx, float dy) {
    __n_assert(cam, return);
    cam->x += dx;
    cam->y += dy;
} /* n_iso_camera_scroll() */

/**
 *@brief Zoom the camera toward a screen-space point.
 * Adjusts the camera position so the world point under the mouse
 * stays fixed on screen after zooming.
 */
void n_iso_camera_zoom(N_ISO_CAMERA* cam, float dz, float mouse_x, float mouse_y) {
    __n_assert(cam, return);
    float old_zoom = cam->zoom;
    cam->zoom += dz;
    if (cam->zoom < cam->zoom_min) cam->zoom = cam->zoom_min;
    if (cam->zoom > cam->zoom_max) cam->zoom = cam->zoom_max;
    /* Adjust position so the world point under the mouse stays fixed */
    cam->x += mouse_x / cam->zoom - mouse_x / old_zoom;
    cam->y += mouse_y / cam->zoom - mouse_y / old_zoom;
} /* n_iso_camera_zoom() */

/**
 *@brief Center the camera so that a world point is at screen center.
 */
void n_iso_camera_center_on(N_ISO_CAMERA* cam, float world_x, float world_y, int screen_w, int screen_h) {
    __n_assert(cam, return);
    cam->x = -world_x + (float)screen_w / (2.0f * cam->zoom);
    cam->y = -world_y + (float)screen_h / (2.0f * cam->zoom);
} /* n_iso_camera_center_on() */

/**
 *@brief Smoothly follow a world-space target.
 */
void n_iso_camera_follow(N_ISO_CAMERA* cam, float target_x, float target_y, int screen_w, int screen_h, float smoothing, float dt) {
    __n_assert(cam, return);
    float target_cx = -target_x + (float)screen_w / (2.0f * cam->zoom);
    float target_cy = -target_y + (float)screen_h / (2.0f * cam->zoom);
    float t = smoothing * dt;
    if (t > 1.0f) t = 1.0f;
    cam->x += (target_cx - cam->x) * t;
    cam->y += (target_cy - cam->y) * t;
} /* n_iso_camera_follow() */

/**
 *@brief Convert screen pixel coordinates to world coordinates.
 */
void n_iso_camera_screen_to_world(const N_ISO_CAMERA* cam, float sx, float sy, float* wx, float* wy) {
    __n_assert(cam, return);
    __n_assert(wx, return);
    __n_assert(wy, return);
    *wx = sx / cam->zoom - cam->x;
    *wy = sy / cam->zoom - cam->y;
} /* n_iso_camera_screen_to_world() */

/**
 *@brief Convert world coordinates to screen pixel coordinates.
 */
void n_iso_camera_world_to_screen(const N_ISO_CAMERA* cam, float wx, float wy, float* sx, float* sy) {
    __n_assert(cam, return);
    __n_assert(sx, return);
    __n_assert(sy, return);
    *sx = (wx + cam->x) * cam->zoom;
    *sy = (wy + cam->y) * cam->zoom;
} /* n_iso_camera_world_to_screen() */

/*---------------------------------------------------------------------------
 * Height border edge visibility (public API, no Allegro dependency)
 *-------------------------------------------------------------------------*/

/**
 *@brief Compute which diamond edges of tile (mx,my) need a height border.
 *
 * An edge is marked for drawing when the neighbor in that direction has
 * a height difference > 1 or is out of bounds (map boundary).
 *
 * Edge-to-neighbor mapping (diamond isometric layout):
 *   NW (N→W, top-left)    → neighbor (mx-1, my)
 *   NE (N→E, top-right)   → neighbor (mx, my-1)
 *   SE (E→S, bottom-right) → neighbor (mx+1, my)
 *   SW (S→W, bottom-left)  → neighbor (mx, my+1)
 */
ISO_VISIBLE_EDGES iso_map_get_visible_edges(const ISO_MAP* map, int mx, int my) {
    ISO_VISIBLE_EDGES edges = {0};
    if (!map) return edges;

    int h = iso_map_get_height(map, mx, my);
    if (h == 0) return edges;

    /* NW edge: neighbor (mx-1, my) */
    if (mx <= 0) {
        /* At west boundary: use neighbor chunk data if available */
        if (map->neighbor_heights_west) {
            if (abs(h - map->neighbor_heights_west[my]) >= 1)
                edges.draw_nw = 1;
        } else {
            edges.draw_nw = 1; /* world boundary */
        }
    } else if (abs(h - iso_map_get_height(map, mx - 1, my)) >= 1) {
        edges.draw_nw = 1;
    }

    /* NE edge: neighbor (mx, my-1) */
    if (my <= 0) {
        if (map->neighbor_heights_north) {
            if (abs(h - map->neighbor_heights_north[mx]) >= 1)
                edges.draw_ne = 1;
        } else {
            edges.draw_ne = 1;
        }
    } else if (abs(h - iso_map_get_height(map, mx, my - 1)) >= 1) {
        edges.draw_ne = 1;
    }

    /* SE edge: neighbor (mx+1, my) */
    if (mx >= map->width - 1) {
        if (map->neighbor_heights_east) {
            if (abs(h - map->neighbor_heights_east[my]) >= 1)
                edges.draw_se = 1;
        } else {
            edges.draw_se = 1;
        }
    } else if (abs(h - iso_map_get_height(map, mx + 1, my)) >= 1) {
        edges.draw_se = 1;
    }

    /* SW edge: neighbor (mx, my+1) */
    if (my >= map->height - 1) {
        if (map->neighbor_heights_south) {
            if (abs(h - map->neighbor_heights_south[mx]) >= 1)
                edges.draw_sw = 1;
        } else {
            edges.draw_sw = 1;
        }
    } else if (abs(h - iso_map_get_height(map, mx, my + 1)) >= 1) {
        edges.draw_sw = 1;
    }

    return edges;
}

/**
 *@brief Check if a tile has geometry covering height level z.
 *
 * Returns 1 if any segment of (mx,my) has bottom <= z AND top > z,
 * meaning the tile's wall/surface extends strictly above z.
 * Used for occlusion: a tile drawn later in painter order covers
 * edges of earlier tiles when its wall passes above the edge height.
 *
 * Requires the occluding tile to be at least 2 height units taller
 * (top > z + 1) so that single-step stairs (+1 per tile) do not
 * suppress the NW/NE borders — the step only partially covers the
 * edge, and painter order handles the overlap visually.
 */
static int _tile_covers_height(const ISO_MAP* map, int mx, int my, int z) {
    if (!map || mx < 0 || mx >= map->width || my < 0 || my >= map->height)
        return 0;
    const ISO_TILE_SEGMENTS* ts = iso_map_get_segments(map, mx, my);
    if (ts && ts->count > 0) {
        for (int i = 0; i < ts->count; i++) {
            if (ts->segs[i].bottom <= z && ts->segs[i].top > z + 1)
                return 1;
        }
        return 0;
    }
    int h = iso_map_get_height(map, mx, my);
    return (h > z + 1) ? 1 : 0;
}

/**
 *@brief Per-segment edge visibility with adjacency + occlusion.
 *
 * ADJACENCY (per edge direction):
 *   For ALL segments (ground and floating): draw edge if no neighbour
 *   segment has the same top height.  Same top = connected surface =
 *   no border.  Different top = visible height step = border drawn.
 *   Range overlap is NOT used — overlapping ranges with different tops
 *   (e.g. {10,12} vs {10,11}) still have a visible step at the top.
 *
 * OCCLUSION (painter-order):
 *   NW and NE edges face away from the camera.  A tile drawn later
 *   (further south in isometric order) can visually cover these edges.
 *   - NW edge occluded if SW neighbour (mx, my+1) covers seg.top
 *   - NE edge occluded if SE neighbour (mx+1, my) covers seg.top
 *   SE and SW edges face the camera — never explicitly occluded
 *   (painter order handles them automatically).
 *
 * UNDERSIDE (floating segments only):
 *   draw_underside = 1 if bottom > 0 AND any side has the gap below
 *   exposed (no neighbour segment reaches up to seg.bottom).
 *
 * topVertex is never set — the struct has no field for it.
 */
ISO_VISIBLE_EDGES iso_map_get_visible_edges_segment(const ISO_MAP* map, int mx, int my, int seg_idx) {
    ISO_VISIBLE_EDGES edges = {0};
    if (!map) return edges;

    /* Get this tile's segment */
    ISO_TILE_SEGMENT seg;
    const ISO_TILE_SEGMENTS* ts = iso_map_get_segments(map, mx, my);
    if (ts && seg_idx >= 0 && seg_idx < ts->count) {
        seg = ts->segs[seg_idx];
    } else {
        /* Fallback: heightmap as single segment {0, h} */
        int h = iso_map_get_height(map, mx, my);
        if (h == 0) return edges;
        seg.bottom = 0;
        seg.top = h;
    }

    if (seg.top <= seg.bottom) return edges;

    /* Direction offsets: NW→(mx-1,my), NE→(mx,my-1), SE→(mx+1,my), SW→(mx,my+1) */
    static const int ddx[4] = {-1, 0, 1, 0};
    static const int ddy[4] = {0, -1, 0, 1};
    int* flags[4];
    flags[0] = &edges.draw_nw;
    flags[1] = &edges.draw_ne;
    flags[2] = &edges.draw_se;
    flags[3] = &edges.draw_sw;

    int any_side_exposed = 0;   /* for underside check */
    int ntop[4] = {0, 0, 0, 0}; /* neighbor top height per direction */

    for (int d = 0; d < 4; d++) {
        int nx = mx + ddx[d];
        int ny = my + ddy[d];
        int neighbor_matches = 0;
        int neighbor_covers_bottom = 0;

        if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) {
            /* Boundary: check neighbor chunk height arrays */
            int nh = -1;
            if (d == 0 && map->neighbor_heights_west) nh = map->neighbor_heights_west[my];
            if (d == 1 && map->neighbor_heights_north) nh = map->neighbor_heights_north[mx];
            if (d == 2 && map->neighbor_heights_east) nh = map->neighbor_heights_east[my];
            if (d == 3 && map->neighbor_heights_south) nh = map->neighbor_heights_south[mx];

            if (nh > 0) {
                /* Boundary treated as single segment {0,nh}: match if same top */
                neighbor_matches = (nh == seg.top);
                neighbor_covers_bottom = (nh >= seg.bottom);
                ntop[d] = nh;
            }
            /* else: world boundary → no match, not covered, ntop stays 0 */
        } else {
            const ISO_TILE_SEGMENTS* nts = iso_map_get_segments(map, nx, ny);
            if (nts && nts->count > 0) {
                /* Match if any neighbour segment has the same top height.
                 * This applies uniformly to ground and floating segments:
                 * same top = connected surface = no border.
                 * Different top = visible height step = border drawn. */
                for (int si = 0; si < nts->count; si++) {
                    if (nts->segs[si].top == seg.top) {
                        neighbor_matches = 1;
                    }
                    if (nts->segs[si].top >= seg.bottom) {
                        neighbor_covers_bottom = 1;
                    }
                }
                /* Use heightmap value as neighbor top for vertical lines */
                ntop[d] = iso_map_get_height(map, nx, ny);
            } else {
                /* No segment data: heightmap as single segment {0, h} */
                int nh = iso_map_get_height(map, nx, ny);
                if (nh > 0) {
                    neighbor_matches = (nh == seg.top);
                    neighbor_covers_bottom = (nh >= seg.bottom);
                }
                ntop[d] = nh;
            }
        }

        if (!neighbor_matches) *flags[d] = 1;
        if (!neighbor_covers_bottom) any_side_exposed = 1;
    }

    /* OCCLUSION: NW/NE edges face away from camera.
     * Suppress if the tile drawn later (south in painter order) has
     * geometry covering this segment's top face height.
     * NW edge: covered by SW neighbour (mx, my+1)
     * NE edge: covered by SE neighbour (mx+1, my) */
    if (edges.draw_nw && _tile_covers_height(map, mx, my + 1, seg.top))
        edges.draw_nw = 0;
    if (edges.draw_ne && _tile_covers_height(map, mx + 1, my, seg.top))
        edges.draw_ne = 0;

    /* Underside border for floating segments */
    if (seg.bottom > 0 && any_side_exposed) {
        edges.draw_underside = 1;
    }

    /* Store neighbor top heights for vertical corner line computation */
    edges.neighbor_top_nw = ntop[0];
    edges.neighbor_top_ne = ntop[1];
    edges.neighbor_top_se = ntop[2];
    edges.neighbor_top_sw = ntop[3];

    return edges;
}

/*===========================================================================
 * ISO_MAP rendering (requires Allegro 5)
 *=========================================================================*/
#ifdef HAVE_ALLEGRO

#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>

/**
 *@brief Project a tile corner to screen coordinates (canonical formula).
 *
 * Every screen vertex in the renderer must come from this function so that
 * two tiles sharing a corner get bit-identical screen positions regardless
 * of which tile's draw call is computing the vertex.
 *
 * The camera offset (cam_px, cam_py) must be integer-snapped pixel offsets
 * computed as floor(cam_x * zoom), floor(cam_y * zoom).  This ensures the
 * fractional pixel position of each vertex comes only from (wx * zoom),
 * which is constant per vertex, eliminating sub-pixel aliasing shimmer
 * during camera movement.
 *
 *@param hw     map->proj.half_w (unzoomed)
 *@param hh     map->proj.half_h (unzoomed)
 *@param tl     map->proj.tile_lift (unzoomed)
 *@param cx     corner X in tile grid (e.g. mx+1 for the East corner)
 *@param cy     corner Y in tile grid
 *@param fh     height (float, supports smooth-height interpolation)
 *@param cam_px integer-snapped camera pixel X: floor(cam_x * zoom)
 *@param cam_py integer-snapped camera pixel Y: floor(cam_y * zoom)
 *@param zoom   zoom factor
 *@param sx     output screen X
 *@param sy     output screen Y
 */
static inline void _iso_corner_to_screen(float hw, float hh, float tl, int cx, int cy, float fh, float cam_px, float cam_py, float zoom, float* sx, float* sy) {
    float wx = (float)(cx - cy) * hw + 2.0f * hw;
    float wy = (float)(cx + cy) * hh - fh * tl;
    *sx = wx * zoom + cam_px;
    *sy = wy * zoom + cam_py;
}

/**
 *@brief Compute a brightness tint color for a segment at a given height.
 * Higher segments are brighter (closer to white), lower segments are darker.
 * @param seg_top      top height of the segment
 * @param max_height   maximum height in the map (0 = no scaling)
 * @param intensity    tint intensity (0.0 = no tint/white, 1.0 = full range)
 * @return ALLEGRO_COLOR to use as multiply tint
 */
static inline ALLEGRO_COLOR _height_tint(int seg_top, int max_height, float intensity) {
    if (intensity <= 0.0f || max_height <= 0)
        return al_map_rgba_f(1.0f, 1.0f, 1.0f, 1.0f);
    float ratio = (float)seg_top / (float)max_height;
    /* brightness ranges from (1 - intensity) at height 0 to 1.0 at max_height */
    float b = 1.0f - intensity * (1.0f - ratio);
    if (b < 0.0f) b = 0.0f;
    if (b > 1.0f) b = 1.0f;
    return al_map_rgba_f(b, b, b, 1.0f);
}

/* Extended tint: combines height tint with ambient color and per-tile dynamic light.
 * ambient_r/g/b: global day/night ambient (1.0 = full daylight).
 * dyn_r/g/b: additive dynamic light contribution at this tile (0.0 = no light). */
static inline ALLEGRO_COLOR _compute_tile_tint(int seg_top, int max_height, float intensity, float ambient_r, float ambient_g, float ambient_b, float dyn_r, float dyn_g, float dyn_b) {
    /* Start with height-based brightness */
    float hb = 1.0f;
    if (intensity > 0.0f && max_height > 0) {
        float ratio = (float)seg_top / (float)max_height;
        hb = 1.0f - intensity * (1.0f - ratio);
        if (hb < 0.0f) hb = 0.0f;
        if (hb > 1.0f) hb = 1.0f;
    }

    /* Multiply by ambient color */
    float r = hb * ambient_r;
    float g = hb * ambient_g;
    float b = hb * ambient_b;

    /* Add dynamic light (additive boost) */
    r += dyn_r;
    g += dyn_g;
    b += dyn_b;

    /* Clamp to avoid overexposure */
    if (r > 1.0f) r = 1.0f;
    if (g > 1.0f) g = 1.0f;
    if (b > 1.0f) b = 1.0f;

    return al_map_rgba_f(r, g, b, 1.0f);
}

/**
 *@brief Draw a tile bitmap warped to 4 pre-computed screen vertices.
 * Uses two textured triangles mapped to the warped diamond shape.
 * Vertices must come from _iso_corner_to_screen for bit-identical
 * shared edges between adjacent tiles.
 */
static void _draw_tile_warped(ALLEGRO_BITMAP* bmp,
                              const float vx[4],
                              const float vy[4],
                              int tile_w,
                              int tile_h,
                              ALLEGRO_COLOR tint) {
    float un = (float)tile_w / 2.0f, vn = 0.0f;
    float ue = (float)tile_w, ve = (float)tile_h / 2.0f;
    float us = (float)tile_w / 2.0f, vs = (float)tile_h;
    float uw = 0.0f, vw = (float)tile_h / 2.0f;
    ALLEGRO_VERTEX vtx[6];
    vtx[0].x = vx[0];
    vtx[0].y = vy[0];
    vtx[0].z = 0;
    vtx[0].u = un;
    vtx[0].v = vn;
    vtx[0].color = tint;
    vtx[1].x = vx[1];
    vtx[1].y = vy[1];
    vtx[1].z = 0;
    vtx[1].u = ue;
    vtx[1].v = ve;
    vtx[1].color = tint;
    vtx[2].x = vx[2];
    vtx[2].y = vy[2];
    vtx[2].z = 0;
    vtx[2].u = us;
    vtx[2].v = vs;
    vtx[2].color = tint;
    vtx[3].x = vx[0];
    vtx[3].y = vy[0];
    vtx[3].z = 0;
    vtx[3].u = un;
    vtx[3].v = vn;
    vtx[3].color = tint;
    vtx[4].x = vx[2];
    vtx[4].y = vy[2];
    vtx[4].z = 0;
    vtx[4].u = us;
    vtx[4].v = vs;
    vtx[4].color = tint;
    vtx[5].x = vx[3];
    vtx[5].y = vy[3];
    vtx[5].z = 0;
    vtx[5].u = uw;
    vtx[5].v = vw;
    vtx[5].color = tint;

    al_draw_prim(vtx, NULL, bmp, 0, 6, ALLEGRO_PRIM_TRIANGLE_LIST);
}

/**
 *@brief Draw cliff side faces for a single segment {seg_bottom..seg_top}.
 * For ground segments (seg_bottom==0) uses neighbor-aware wall height.
 * For floating segments (seg_bottom>0) draws full segment height.
 */
static void _draw_segment_sides(const ISO_MAP* map,
                                float east_x,
                                float east_y,
                                float south_x,
                                float south_y,
                                float west_x,
                                float west_y,
                                int mx,
                                int my,
                                int seg_bottom,
                                int seg_top,
                                float lift,
                                float height_brightness,
                                int wall_terrain_override) {
    int base = (wall_terrain_override >= 0) ? wall_terrain_override
                                            : iso_map_get_terrain(map, mx, my);
    float east_shade = 0.55f;
    float south_shade = 0.35f;

    float cr, cg, cb;
    switch (base) {
        case 0:
            cr = 0.7f;
            cg = 0.25f;
            cb = 0.1f;
            break;
        case 1:
            cr = 0.5f;
            cg = 0.45f;
            cb = 0.35f;
            break;
        case 2:
            cr = 0.6f;
            cg = 0.55f;
            cb = 0.45f;
            break;
        default:
            cr = 0.4f;
            cg = 0.35f;
            cb = 0.3f;
            break;
    }
    ALLEGRO_COLOR east_col = al_map_rgba_f(cr * east_shade * height_brightness,
                                           cg * east_shade * height_brightness,
                                           cb * east_shade * height_brightness, 1.0f);
    ALLEGRO_COLOR south_col = al_map_rgba_f(cr * south_shade * height_brightness,
                                            cg * south_shade * height_brightness,
                                            cb * south_shade * height_brightness, 1.0f);
    al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);

    int east_face_h, south_face_h;
    if (seg_bottom == 0) {
        /* Ground segment: use neighbor-aware wall height (existing logic) */
        int h_e = iso_map_get_height(map, mx + 1, my);
        int h_s = iso_map_get_height(map, mx, my + 1);
        int h_se = iso_map_get_height(map, mx + 1, my + 1);

        east_face_h = seg_top - (h_e < h_se ? h_e : h_se);
        if (east_face_h < 1) east_face_h = seg_top - h_e;
        if (east_face_h < 1) east_face_h = seg_top - h_se;

        south_face_h = seg_top - (h_s < h_se ? h_s : h_se);
        if (south_face_h < 1) south_face_h = seg_top - h_s;
        if (south_face_h < 1) south_face_h = seg_top - h_se;
    } else {
        /* Floating segment: full wall from bottom to top */
        east_face_h = seg_top - seg_bottom;
        south_face_h = seg_top - seg_bottom;
    }

    /* No sub-pixel expansion needed: all vertices come from the canonical
     * _iso_corner_to_screen formula, so adjacent tiles sharing an edge
     * produce bit-identical vertex positions. */

    if (east_face_h > 0) {
        float drop = (float)east_face_h * lift;
        float v[8] = {east_x, east_y, south_x, south_y,
                      south_x, south_y + drop,
                      east_x, east_y + drop};
        al_draw_filled_polygon(v, 4, east_col);
        ALLEGRO_COLOR edge = al_map_rgba(0, 0, 0, 80);
        al_draw_line(east_x, east_y + drop, south_x, south_y + drop, edge, 1.0f);
    }

    if (south_face_h > 0) {
        float drop = (float)south_face_h * lift;
        float v[8] = {south_x, south_y, west_x, west_y,
                      west_x, west_y + drop,
                      south_x, south_y + drop};
        al_draw_filled_polygon(v, 4, south_col);
        ALLEGRO_COLOR edge = al_map_rgba(0, 0, 0, 80);
        al_draw_line(west_x, west_y + drop, south_x, south_y + drop, edge, 1.0f);
    }
}

/**
 *@brief Draw the underside face of a floating segment at 65% brightness.
 * The underside is a diamond drawn at the segment's bottom height.
 */
static void _draw_segment_underside(ALLEGRO_BITMAP* bmp,
                                    float base_dx,
                                    float base_dy,
                                    float phw,
                                    float phh,
                                    float tile_draw_w,
                                    float tile_draw_h,
                                    int tile_w,
                                    int tile_h) {
    al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
    /* Draw the tile bitmap with a darkened tint (65% brightness) */
    ALLEGRO_COLOR tint = al_map_rgba_f(0.65f, 0.65f, 0.65f, 1.0f);
    (void)phw;
    (void)phh;
    al_draw_tinted_scaled_bitmap(bmp, tint,
                                 0, 0, (float)tile_w, (float)tile_h,
                                 base_dx, base_dy, tile_draw_w, tile_draw_h, 0);
}

/**
 *@brief Check if an object is occluded by tiles drawn in front of it.
 * Tests tiles at higher depth (mx+my) in a forward cone to see if any
 * elevated tile's screen area overlaps the object's screen position.
 *@param map the map
 *@param obj_fx object fractional map X
 *@param obj_fy object fractional map Y
 *@param obj_fz object height
 *@param obj_sx object screen X (camera-transformed)
 *@param obj_sy object screen Y (camera-transformed)
 *@param sprite_h sprite height used for occlusion bounding
 *@param cam_px camera X offset in pixels
 *@param cam_py camera Y offset in pixels
 *@param zoom current zoom
 *@return 1 if occluded, 0 if visible
 */
static int _iso_object_check_occlusion(const ISO_MAP* map,
                                       float obj_fx,
                                       float obj_fy,
                                       float obj_fz,
                                       float obj_sx,
                                       float obj_sy,
                                       float sprite_h,
                                       float cam_px,
                                       float cam_py,
                                       float zoom) {
    float phw = map->proj.half_w * zoom;
    float phh = map->proj.half_h * zoom;
    float lift = map->proj.tile_lift * zoom;

    /* Entity head screen Y: sprite_h world units above feet */
    float head_sy = obj_sy - sprite_h * lift;

    int base_mx = (int)floorf(obj_fx);
    int base_my = (int)floorf(obj_fy);
    int check_range = map->max_height + 2;

    for (int dy = -1; dy <= check_range; dy++) {
        for (int dx = -1; dx <= check_range; dx++) {
            /* only check tiles drawn after the object in the for(my) for(mx) loop */
            if (dy < 0 || (dy == 0 && dx <= 0)) continue;

            int tmx = base_mx + dx;
            int tmy = base_my + dy;
            if (tmx < 0 || tmx >= map->width || tmy < 0 || tmy >= map->height) continue;

            int th = iso_map_get_height(map, tmx, tmy);
            /* tile must reach above the entity's head to occlude */
            if ((float)th < obj_fz + sprite_h) continue;

            /* compute tile screen position (cam_px already snapped) */
            float tsx, tsy;
            iso_map_to_screen(map, tmx, tmy, th, &tsx, &tsy);
            float tile_dx = tsx * zoom + cam_px;
            float tile_dy = tsy * zoom + cam_py;

            /* tile must cover the entity's head in screen space */
            float tile_left = tile_dx;
            float tile_right = tile_dx + 2.0f * phw;
            float tile_top = tile_dy;
            float tile_bot = tile_dy + 2.0f * phh + (float)th * lift;

            if (obj_sx >= tile_left && obj_sx <= tile_right &&
                tile_top <= head_sy && obj_sy <= tile_bot) {
                return 1;
            }
        }
    }
    return 0;
}

/**
 *@brief Draw black border outlines on the diamond edges of an elevated tile.
 *
 * Rules:
 * - Only draws edges where height difference > 1 or neighbor missing.
 * - NEVER draws the top vertex (N) where NW and NE edges meet —
 *   it is always hidden behind the tile surface itself.
 * - At corners E, S, W where two drawn edges meet, they join naturally.
 * - Occlusion is handled by painter's algorithm: tiles drawn later
 *   (closer to camera) paint over borders of earlier tiles.
 *
 * Diamond vertices: N=vx[0] (top), E=vx[1] (right), S=vx[2] (bottom), W=vx[3] (left)
 */
static void _draw_height_borders(const ISO_MAP* map,
                                 int mx,
                                 int my,
                                 int seg_idx,
                                 int seg_bottom,
                                 int seg_top,
                                 int has_underside_face,
                                 const float vx[4],
                                 const float vy[4],
                                 float lift) {
    ISO_VISIBLE_EDGES edges = iso_map_get_visible_edges_segment(map, mx, my, seg_idx);
    if (!edges.draw_nw && !edges.draw_ne && !edges.draw_se && !edges.draw_sw && !(has_underside_face && edges.draw_underside))
        return;

    ALLEGRO_COLOR border_col = al_map_rgba(0, 0, 0, 200);
    float thickness = 1.5f;

    /* Top face edges */
    if (edges.draw_se)
        al_draw_line(vx[1], vy[1], vx[2], vy[2], border_col, thickness);
    if (edges.draw_sw)
        al_draw_line(vx[2], vy[2], vx[3], vy[3], border_col, thickness);
    if (edges.draw_nw)
        al_draw_line(vx[0], vy[0], vx[3], vy[3], border_col, thickness);
    if (edges.draw_ne)
        al_draw_line(vx[0], vy[0], vx[1], vy[1], border_col, thickness);

    /* Vertical corner lines.
     *
     * For each drawn edge where seg_top > neighbor_top (A is higher):
     *   Draw a vertical line at BOTH endpoints of that edge,
     *   from tileToScreen(vertex, seg_top) down to
     *   tileToScreen(vertex, neighbor_top).
     *   Exception: NEVER draw at the North vertex (vx[0]/vy[0]).
     *
     * If seg_top <= neighbor_top: no vertical lines (wall belongs to N).
     *
     * vx[]/vy[] are already at tileToScreen(vertex, seg_top).
     * The bottom endpoint is (seg_top - HN) * lift pixels below.
     *
     * Diamond vertices: N=0, E=1, S=2, W=3.
     * Edge endpoints: NE=(N,E) SE=(E,S) SW=(S,W) NW=(W,N)
     *
     * Multiple edges may want to draw at the same vertex with different
     * drops.  We draw the longest line (smallest HN) at each vertex
     * to avoid duplicate shorter lines.
     */
    {
        /* Per-vertex: track the largest drop (longest line) requested.
         *
         * CONTINUITY CHECK: a vertical corner line at a vertex is only
         * drawn if the wall face ENDS at that vertex — i.e. the adjacent
         * tile in the wall's direction is NOT at the same height.
         * If the adjacent tile IS at the same height, the wall surface
         * continues past the vertex and the line is interior (hidden).
         *
         * For the east-facing wall (SE edge):
         *   E vertex: wall continues northward if NE neighbor same height → skip
         *   S vertex: wall continues southward if SW neighbor same height → skip
         * For the south-facing wall (SW edge):
         *   S vertex: wall continues eastward if SE neighbor same height → skip
         *   W vertex: wall continues westward if NW neighbor same height → skip
         * For NE edge → E vertex: wall continues if SE neighbor same height
         * For NW edge → W vertex: wall continues if SW neighbor same height
         */
        float drop_e = 0, drop_s = 0, drop_w = 0;
        int nt_nw = edges.neighbor_top_nw;
        int nt_ne = edges.neighbor_top_ne;
        int nt_se = edges.neighbor_top_se;
        int nt_sw = edges.neighbor_top_sw;

        /* Clamp neighbor tops to seg_bottom: vertical lines must not
         * extend below the segment's own base (floating segments). */
        int bot = seg_bottom;
        int cl_nw = nt_nw > bot ? nt_nw : bot;
        int cl_ne = nt_ne > bot ? nt_ne : bot;
        int cl_se = nt_se > bot ? nt_se : bot;
        int cl_sw = nt_sw > bot ? nt_sw : bot;

        /* NE edge (N,E): only E vertex (N is never drawn) */
        if (edges.draw_ne && seg_top > cl_ne) {
            /* E vertex: east wall continues if SE neighbor same height */
            if (nt_se != seg_top) {
                float d = (float)(seg_top - cl_ne) * lift;
                if (d > drop_e) drop_e = d;
            }
        }
        /* SE edge (E,S): E and S vertices */
        if (edges.draw_se && seg_top > cl_se) {
            float d = (float)(seg_top - cl_se) * lift;
            /* E vertex: east wall continues northward if NE neighbor same height */
            if (nt_ne != seg_top) {
                if (d > drop_e) drop_e = d;
            }
            /* S vertex: east wall continues southward if SW neighbor same height */
            if (nt_sw != seg_top) {
                if (d > drop_s) drop_s = d;
            }
        }
        /* SW edge (S,W): S and W vertices */
        if (edges.draw_sw && seg_top > cl_sw) {
            float d = (float)(seg_top - cl_sw) * lift;
            /* S vertex: south wall continues eastward if SE neighbor same height */
            if (nt_se != seg_top) {
                if (d > drop_s) drop_s = d;
            }
            /* W vertex: south wall continues westward if NW neighbor same height */
            if (nt_nw != seg_top) {
                if (d > drop_w) drop_w = d;
            }
        }
        /* NW edge (W,N): only W vertex (N is never drawn) */
        if (edges.draw_nw && seg_top > cl_nw) {
            /* W vertex: south wall continues if SW neighbor same height */
            if (nt_sw != seg_top) {
                float d = (float)(seg_top - cl_nw) * lift;
                if (d > drop_w) drop_w = d;
            }
        }

        if (drop_e > 0)
            al_draw_line(vx[1], vy[1], vx[1], vy[1] + drop_e, border_col, thickness);
        if (drop_s > 0)
            al_draw_line(vx[2], vy[2], vx[2], vy[2] + drop_s, border_col, thickness);
        if (drop_w > 0)
            al_draw_line(vx[3], vy[3], vx[3], vy[3] + drop_w, border_col, thickness);
    }

    /* Underside face border (floating segments only).
     * The underside diamond is at seg_bottom, offset below the top face
     * by (seg_top - seg_bottom) * lift pixels.  Only drawn if the
     * underside face itself was rendered (has_underside_face).
     * Only SE and SW edges — NW and NE are always hidden behind the
     * segment's own side walls from the camera angle. */
    if (has_underside_face && edges.draw_underside) {
        float dy = (float)(seg_top - seg_bottom) * lift;
        /* SE edge */
        al_draw_line(vx[1], vy[1] + dy, vx[2], vy[2] + dy, border_col, thickness);
        /* SW edge */
        al_draw_line(vx[2], vy[2] + dy, vx[3], vy[3] + dy, border_col, thickness);
    }
}

/**
 *@brief Draw the full ISO_MAP with segment-sorted rendering.
 *
 * When segment data is available, tiles are decomposed into individual
 * segments and sorted by (segment.bottom, my, mx).  This allows
 * multi-segment tiles (bridges, overhangs) to interleave correctly
 * with ground-level tiles in the painter's algorithm.
 *
 * Per-segment rendering order:
 * Step 0: Underside face (floating segments only, 65% tint)
 * Step 1: Cliff walls (between bottom and top)
 * Step 2: Tile top surface (flat or warped for ground segments)
 * Step 3: Terrain transitions (ground segments only)
 * Step 3.5: Height borders (ground segments only, Phase 12c extends this)
 * Step 4: Objects (depth-sorted, interleaved by segment sort key)
 * Step 5: Hover highlight
 * Step 6: Grid overlay
 * Step 7 (post-loop): Occluded object overlays
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
                  int num_objects) {
    __n_assert(map, return);
    __n_assert(tile_bitmaps, return);

    al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);

    float hw0 = map->proj.half_w;
    float hh0 = map->proj.half_h;
    float tl0 = map->proj.tile_lift;
    float phw = hw0 * zoom;
    float phh = hh0 * zoom;
    float lift = tl0 * zoom;
    float tile_draw_w = 2.0f * phw;
    float tile_draw_h = 2.0f * phh;

    /* cam_px / cam_py are pre-computed pixel offsets provided by the caller.
     * The caller is responsible for integer-snapping the base camera offset
     * (floorf(cam_x * zoom)) and adding any chunk pixel offset after the
     * snap.  This ensures all chunks share the same base snap and adjacent
     * chunk seams are pixel-perfect. */

    int tile_w = tile_bitmaps[0] ? al_get_bitmap_width(tile_bitmaps[0]) : 64;
    int tile_h = tile_bitmaps[0] ? al_get_bitmap_height(tile_bitmaps[0]) : 32;

    int num_edge_masks = ISO_NUM_EDGE_MASKS;

    /* Build segment draw order */
    int max_draw_entries = map->width * map->height * ISO_MAX_SEGMENTS_PER_TILE;
    ISO_DRAW_ENTRY* draw_order = (ISO_DRAW_ENTRY*)malloc((size_t)max_draw_entries * sizeof(ISO_DRAW_ENTRY));
    if (!draw_order) return;
    int num_entries = iso_map_build_draw_order(map, draw_order, max_draw_entries);

    /* Pre-process objects: compute screen positions and sort by depth.
     * Sort key matches segment draw order: (floor(fz), my, mx). */
    int obj_order_buf[64];
    int* obj_order = NULL;
    float* obj_sx_arr = NULL;
    float* obj_sy_arr = NULL;
    int nobj = (objects && num_objects > 0) ? num_objects : 0;

    if (nobj > 0) {
        obj_order = (nobj <= 64) ? obj_order_buf : (int*)malloc((size_t)nobj * sizeof(int));
        obj_sx_arr = (float*)malloc((size_t)nobj * sizeof(float));
        obj_sy_arr = (float*)malloc((size_t)nobj * sizeof(float));

        if (!obj_order || !obj_sx_arr || !obj_sy_arr) {
            n_log(LOG_ERR, "iso_map_draw: object allocation failed");
            if (obj_order && obj_order != obj_order_buf) free(obj_order);
            if (obj_sx_arr) free(obj_sx_arr);
            if (obj_sy_arr) free(obj_sy_arr);
            nobj = 0;
            obj_order = NULL;
            obj_sx_arr = NULL;
            obj_sy_arr = NULL;
        }
    }

    if (nobj > 0) {
        for (int i = 0; i < nobj; i++) {
            obj_order[i] = i;
            objects[i].is_occluded = 0;
            float wsx, wsy;
            iso_map_to_screen_f(map, objects[i].fx, objects[i].fy, objects[i].fz, &wsx, &wsy);
            obj_sx_arr[i] = wsx * zoom + cam_px;
            obj_sy_arr[i] = wsy * zoom + cam_py;
        }

        /* insertion sort by (depth, floor(fz), my, mx) to match segment draw order */
        for (int i = 1; i < nobj; i++) {
            int key = obj_order[i];
            int key_fz = (int)floorf(objects[key].fz);
            int key_my = (int)floorf(objects[key].fy);
            int key_mx = (int)floorf(objects[key].fx);
            int key_depth = key_my + key_mx;
            int j = i - 1;
            while (j >= 0) {
                int cj = obj_order[j];
                int cj_fz = (int)floorf(objects[cj].fz);
                int cj_my = (int)floorf(objects[cj].fy);
                int cj_mx = (int)floorf(objects[cj].fx);
                int cj_depth = cj_my + cj_mx;
                if (cj_depth > key_depth ||
                    (cj_depth == key_depth && cj_fz > key_fz) ||
                    (cj_depth == key_depth && cj_fz == key_fz && cj_my > key_my) ||
                    (cj_depth == key_depth && cj_fz == key_fz && cj_my == key_my && cj_mx > key_mx)) {
                    obj_order[j + 1] = obj_order[j];
                    j--;
                } else {
                    break;
                }
            }
            obj_order[j + 1] = key;
        }
    }
    /* ===== PASS 1: Ground segments only (bottom == 0), no objects =====
     * Draw all ground-level tiles first so they can never overdraw
     * elevated content (entities on bridges, floating segments, etc.). */
    for (int di = 0; di < num_entries; di++) {
        if (draw_order[di].bottom > 0) continue; /* skip elevated */

        int mx = draw_order[di].mx;
        int my = draw_order[di].my;
        int seg_top = draw_order[di].top;
        int h = seg_top;
        int base = iso_map_get_terrain(map, mx, my);

        /* Per-segment tile overrides: upper_tile for top face, lower_tile for walls */
        int top_terrain = base;
        int wall_terrain = base;
        {
            int si = draw_order[di].seg_idx;
            int tidx = my * map->width + mx;
            if (map->segments && tidx >= 0 && tidx < map->width * map->height &&
                si >= 0 && si < map->segments[tidx].count) {
                int ut = map->segments[tidx].segs[si].upper_tile;
                int lt = map->segments[tidx].segs[si].lower_tile;
                if (ut >= 0 && ut < map->num_terrains) top_terrain = ut;
                if (lt >= 0 && lt < map->num_terrains) wall_terrain = lt;
            }
        }

        float vx[4], vy[4];
        float base_dx, base_dy;

        if (map->smooth_height) {
            float h_n, h_e, h_s, h_w;
            iso_map_smooth_corner_heights(map, mx, my, &h_n, &h_e, &h_s, &h_w);
            _iso_corner_to_screen(hw0, hh0, tl0, mx, my, h_n, cam_px, cam_py, zoom, &vx[0], &vy[0]);
            _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my, h_e, cam_px, cam_py, zoom, &vx[1], &vy[1]);
            _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my + 1, h_s, cam_px, cam_py, zoom, &vx[2], &vy[2]);
            _iso_corner_to_screen(hw0, hh0, tl0, mx, my + 1, h_w, cam_px, cam_py, zoom, &vx[3], &vy[3]);
            base_dx = vx[3];
            base_dy = vy[0];
        } else {
            _iso_corner_to_screen(hw0, hh0, tl0, mx, my, (float)h, cam_px, cam_py, zoom, &vx[0], &vy[0]);
            _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my, (float)h, cam_px, cam_py, zoom, &vx[1], &vy[1]);
            _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my + 1, (float)h, cam_px, cam_py, zoom, &vx[2], &vy[2]);
            _iso_corner_to_screen(hw0, hh0, tl0, mx, my + 1, (float)h, cam_px, cam_py, zoom, &vx[3], &vy[3]);
            base_dx = vx[3];
            base_dy = vy[0];
        }

        /* Culling */
        float min_vy = fminf(fminf(vy[0], vy[1]), fminf(vy[2], vy[3]));
        float max_vy = fmaxf(fmaxf(vy[0], vy[1]), fmaxf(vy[2], vy[3]));
        float max_side = fmaxf((float)h, (float)map->max_height) * lift;
        if (vx[1] < 0 || vx[3] > (float)screen_w) continue;
        if (max_vy + max_side < 0 || min_vy > (float)screen_h) continue;

        /* Height-based tint + ambient + dynamic lighting for this segment */
        float amb_r = map->ambient_r > 0.0f ? map->ambient_r : 1.0f;
        float amb_g = map->ambient_g > 0.0f ? map->ambient_g : 1.0f;
        float amb_b = map->ambient_b > 0.0f ? map->ambient_b : 1.0f;
        float dyn_r = 0.0f, dyn_g = 0.0f, dyn_b = 0.0f;
        if (map->dynamic_light_map) {
            int lidx = (my * map->width + mx) * 3;
            dyn_r = map->dynamic_light_map[lidx];
            dyn_g = map->dynamic_light_map[lidx + 1];
            dyn_b = map->dynamic_light_map[lidx + 2];
        }
        ALLEGRO_COLOR seg_tint = _compute_tile_tint(seg_top, map->max_height,
                                                    map->height_tint_intensity,
                                                    amb_r, amb_g, amb_b,
                                                    dyn_r, dyn_g, dyn_b);
        float seg_brightness = 1.0f;
        if (map->height_tint_intensity > 0.0f && map->max_height > 0) {
            float ratio = (float)seg_top / (float)map->max_height;
            seg_brightness = 1.0f - map->height_tint_intensity * (1.0f - ratio);
        }
        /* Factor in ambient for wall brightness */
        seg_brightness *= (amb_r + amb_g + amb_b) / 3.0f;
        float wall_dyn = (dyn_r + dyn_g + dyn_b) / 3.0f;
        seg_brightness += wall_dyn;
        if (seg_brightness > 1.0f) seg_brightness = 1.0f;

        /* Top surface */
        al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
        if (map->smooth_height) {
            _draw_tile_warped(tile_bitmaps[top_terrain], vx, vy, tile_w, tile_h, seg_tint);
        } else {
            al_draw_tinted_scaled_bitmap(tile_bitmaps[top_terrain], seg_tint,
                                         0, 0, (float)tile_w, (float)tile_h,
                                         base_dx, base_dy, tile_draw_w, tile_draw_h, 0);
        }

        /* Cliff walls */
        if (seg_top > 0) {
            if (map->smooth_height) {
                int h_east = iso_map_get_height(map, mx + 1, my);
                int h_south = iso_map_get_height(map, mx, my + 1);
                int h_se = iso_map_get_height(map, mx + 1, my + 1);
                if ((h - h_east > map->smooth_slope_max) ||
                    (h - h_south > map->smooth_slope_max) ||
                    (h - h_se > map->smooth_slope_max)) {
                    float ce_x, ce_y, cs_x, cs_y, cw_x, cw_y;
                    _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my, (float)h, cam_px, cam_py, zoom, &ce_x, &ce_y);
                    _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my + 1, (float)h, cam_px, cam_py, zoom, &cs_x, &cs_y);
                    _iso_corner_to_screen(hw0, hh0, tl0, mx, my + 1, (float)h, cam_px, cam_py, zoom, &cw_x, &cw_y);
                    _draw_segment_sides(map, ce_x, ce_y, cs_x, cs_y, cw_x, cw_y,
                                        mx, my, 0, seg_top, lift, seg_brightness, wall_terrain);
                }
            } else {
                _draw_segment_sides(map, vx[1], vy[1], vx[2], vy[2], vx[3], vy[3],
                                    mx, my, 0, seg_top, lift, seg_brightness, wall_terrain);
            }
        }

        /* Terrain transitions (same tint as base tile) */
        if (transition_tiles) {
            int edge_bits_arr[16];
            int corner_bits_arr[16];
            int nt = map->num_terrains < 16 ? map->num_terrains : 16;
            memset(edge_bits_arr, 0, sizeof(int) * (size_t)nt);
            memset(corner_bits_arr, 0, sizeof(int) * (size_t)nt);
            iso_map_calc_transitions_full(map, mx, my, edge_bits_arr, corner_bits_arr);

            for (int t = base + 1; t < nt; t++) {
                if (edge_bits_arr[t] != 0 && transition_tiles[t] &&
                    transition_tiles[t][edge_bits_arr[t]] != NULL) {
                    al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
                    if (map->smooth_height) {
                        _draw_tile_warped(transition_tiles[t][edge_bits_arr[t]],
                                          vx, vy, tile_w, tile_h, seg_tint);
                    } else {
                        al_draw_tinted_scaled_bitmap(transition_tiles[t][edge_bits_arr[t]],
                                                     seg_tint,
                                                     0, 0, (float)tile_w, (float)tile_h,
                                                     base_dx, base_dy, tile_draw_w, tile_draw_h, 0);
                    }
                }

                int cidx = num_edge_masks + corner_bits_arr[t];
                if (corner_bits_arr[t] != 0 && cidx < num_masks &&
                    transition_tiles[t] && transition_tiles[t][cidx] != NULL) {
                    al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
                    if (map->smooth_height) {
                        _draw_tile_warped(transition_tiles[t][cidx],
                                          vx, vy, tile_w, tile_h, seg_tint);
                    } else {
                        al_draw_tinted_scaled_bitmap(transition_tiles[t][cidx],
                                                     seg_tint,
                                                     0, 0, (float)tile_w, (float)tile_h,
                                                     base_dx, base_dy, tile_draw_w, tile_draw_h, 0);
                    }
                }
            }
        }

        /* Overlay tile (drawn after terrain transitions, before walls/borders) */
        if (overlay_bitmaps && map->overlay && num_overlay_tiles > 0) {
            int ov_idx = my * map->width + mx;
            int ov_tile = map->overlay[ov_idx];
            if (ov_tile > 0 && ov_tile < num_overlay_tiles && overlay_bitmaps[ov_tile]) {
                al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
                if (map->smooth_height) {
                    _draw_tile_warped(overlay_bitmaps[ov_tile], vx, vy, tile_w, tile_h, seg_tint);
                } else {
                    al_draw_tinted_scaled_bitmap(overlay_bitmaps[ov_tile], seg_tint,
                                                 0, 0, (float)tile_w, (float)tile_h,
                                                 base_dx, base_dy, tile_draw_w, tile_draw_h, 0);
                }
            }
        }

        /* Height borders */
        if (seg_top > 0) {
            al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
            _draw_height_borders(map, mx, my, draw_order[di].seg_idx,
                                 0, seg_top,
                                 draw_order[di].underside,
                                 vx, vy, lift);
        }

        /* Hover highlight */
        if (mx == map->hover_mx && my == map->hover_my) {
            al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
            ALLEGRO_COLOR hc = player_mode
                                   ? al_map_rgba(100, 255, 100, 120)
                                   : al_map_rgba(255, 255, 100, 120);
            float verts[8] = {vx[0], vy[0], vx[1], vy[1],
                              vx[2], vy[2], vx[3], vy[3]};
            al_draw_filled_polygon(verts, 4, hc);
            ALLEGRO_COLOR oc = player_mode
                                   ? al_map_rgba(100, 255, 100, 200)
                                   : al_map_rgba(255, 255, 100, 200);
            al_draw_line(vx[0], vy[0], vx[1], vy[1], oc, 1.5f);
            al_draw_line(vx[1], vy[1], vx[2], vy[2], oc, 1.5f);
            al_draw_line(vx[2], vy[2], vx[3], vy[3], oc, 1.5f);
            al_draw_line(vx[3], vy[3], vx[0], vy[0], oc, 1.5f);
        }

        /* Grid overlay */
        if (map->show_grid) {
            al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
            ALLEGRO_COLOR gc = al_map_rgba(255, 255, 255, 60);
            al_draw_line(vx[0], vy[0], vx[1], vy[1], gc, 1.0f);
            al_draw_line(vx[1], vy[1], vx[2], vy[2], gc, 1.0f);
            al_draw_line(vx[2], vy[2], vx[3], vy[3], gc, 1.0f);
            al_draw_line(vx[3], vy[3], vx[0], vy[0], gc, 1.0f);

            if (h > 0) {
                ALLEGRO_COLOR hc2 = al_map_rgba(255, 200, 100,
                                                (unsigned char)(60 + h * 35));
                float dot_x = (vx[0] + vx[2]) / 2.0f;
                float dot_y = (vy[0] + vy[1] + vy[2] + vy[3]) / 4.0f;
                al_draw_filled_circle(dot_x, dot_y, 2.0f * zoom, hc2);
            }
        }
    }

    /* ===== PASS 2: Elevated segments + ALL objects (interleaved) =====
     * Objects are interleaved at every draw_order entry (ground and
     * elevated) to preserve correct depth ordering.  Only elevated
     * segments (bottom > 0) are actually rendered — ground segments
     * were already drawn in Pass 1. */
    int obj_cursor = 0;
    for (int di = 0; di < num_entries; di++) {
        int mx = draw_order[di].mx;
        int my = draw_order[di].my;
        int seg_bottom = draw_order[di].bottom;

        /* Object interleaving runs at every entry regardless of
         * ground/elevated — this ensures objects at ground-level depth
         * positions are drawn at the correct point in the sort order. */
        if (nobj > 0) {
            int seg_depth = my + mx;
            while (obj_cursor < nobj) {
                int oi = obj_order[obj_cursor];
                int obj_fz = (int)floorf(objects[oi].fz);
                int obj_my = (int)floorf(objects[oi].fy);
                int obj_mx = (int)floorf(objects[oi].fx);
                int obj_depth = obj_my + obj_mx;
                if (obj_depth > seg_depth ||
                    (obj_depth == seg_depth && obj_fz > seg_bottom) ||
                    (obj_depth == seg_depth && obj_fz == seg_bottom && obj_my > my) ||
                    (obj_depth == seg_depth && obj_fz == seg_bottom && obj_my == my && obj_mx >= mx))
                    break;
                if (objects[oi].draw) {
                    objects[oi].draw(obj_sx_arr[oi], obj_sy_arr[oi],
                                     zoom, 1.0f, objects[oi].user_data);
                }
                obj_cursor++;
            }
        }

        /* Ground segments already drawn in Pass 1 — skip rendering */
        if (seg_bottom == 0) continue;

        /* Elevated segment rendering */
        int seg_top = draw_order[di].top;
        int h = seg_top;
        int base = iso_map_get_terrain(map, mx, my);

        /* Per-segment tile overrides for elevated segments */
        int etop_terrain = base;
        int ewall_terrain = base;
        {
            int si = draw_order[di].seg_idx;
            int tidx = my * map->width + mx;
            if (map->segments && tidx >= 0 && tidx < map->width * map->height &&
                si >= 0 && si < map->segments[tidx].count) {
                int ut = map->segments[tidx].segs[si].upper_tile;
                int lt = map->segments[tidx].segs[si].lower_tile;
                if (ut >= 0 && ut < map->num_terrains) etop_terrain = ut;
                if (lt >= 0 && lt < map->num_terrains) ewall_terrain = lt;
            }
        }

        float vx[4], vy[4];
        float base_dx, base_dy;
        _iso_corner_to_screen(hw0, hh0, tl0, mx, my, (float)h, cam_px, cam_py, zoom, &vx[0], &vy[0]);
        _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my, (float)h, cam_px, cam_py, zoom, &vx[1], &vy[1]);
        _iso_corner_to_screen(hw0, hh0, tl0, mx + 1, my + 1, (float)h, cam_px, cam_py, zoom, &vx[2], &vy[2]);
        _iso_corner_to_screen(hw0, hh0, tl0, mx, my + 1, (float)h, cam_px, cam_py, zoom, &vx[3], &vy[3]);
        base_dx = vx[3];
        base_dy = vy[0];

        /* Culling — objects already handled above, safe to continue */
        float min_vy = fminf(fminf(vy[0], vy[1]), fminf(vy[2], vy[3]));
        float max_vy = fmaxf(fmaxf(vy[0], vy[1]), fmaxf(vy[2], vy[3]));
        float max_side = fmaxf((float)h, (float)map->max_height) * lift;
        if (vx[1] < 0 || vx[3] > (float)screen_w) continue;
        if (max_vy + max_side < 0 || min_vy > (float)screen_h) continue;

        /* Underside face for floating segments */
        if (draw_order[di].underside && tile_bitmaps[etop_terrain]) {
            float under_sx, under_sy;
            iso_map_to_screen(map, mx, my, seg_bottom, &under_sx, &under_sy);
            float under_dx = under_sx * zoom + cam_px;
            float under_dy = under_sy * zoom + cam_py;
            _draw_segment_underside(tile_bitmaps[etop_terrain], under_dx, under_dy,
                                    phw, phh, tile_draw_w, tile_draw_h,
                                    tile_w, tile_h);
        }

        /* Height-based tint for elevated segment */
        /* Elevated segment: same ambient + dynamic light from base tile */
        float eamb_r = map->ambient_r > 0.0f ? map->ambient_r : 1.0f;
        float eamb_g = map->ambient_g > 0.0f ? map->ambient_g : 1.0f;
        float eamb_b = map->ambient_b > 0.0f ? map->ambient_b : 1.0f;
        float edyn_r = 0.0f, edyn_g = 0.0f, edyn_b = 0.0f;
        if (map->dynamic_light_map) {
            int elidx = (my * map->width + mx) * 3;
            edyn_r = map->dynamic_light_map[elidx];
            edyn_g = map->dynamic_light_map[elidx + 1];
            edyn_b = map->dynamic_light_map[elidx + 2];
        }
        ALLEGRO_COLOR eseg_tint = _compute_tile_tint(seg_top, map->max_height,
                                                     map->height_tint_intensity,
                                                     eamb_r, eamb_g, eamb_b,
                                                     edyn_r, edyn_g, edyn_b);
        float eseg_brightness = 1.0f;
        if (map->height_tint_intensity > 0.0f && map->max_height > 0) {
            float ratio = (float)seg_top / (float)map->max_height;
            eseg_brightness = 1.0f - map->height_tint_intensity * (1.0f - ratio);
        }
        eseg_brightness *= (eamb_r + eamb_g + eamb_b) / 3.0f;
        eseg_brightness += (edyn_r + edyn_g + edyn_b) / 3.0f;
        if (eseg_brightness > 1.0f) eseg_brightness = 1.0f;

        /* Top surface (flat — elevated segments don't use smooth height) */
        al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
        al_draw_tinted_scaled_bitmap(tile_bitmaps[etop_terrain], eseg_tint,
                                     0, 0, (float)tile_w, (float)tile_h,
                                     base_dx, base_dy, tile_draw_w, tile_draw_h, 0);

        /* Cliff walls + height borders */
        if (seg_top > seg_bottom) {
            _draw_segment_sides(map, vx[1], vy[1], vx[2], vy[2], vx[3], vy[3],
                                mx, my, seg_bottom, seg_top, lift, eseg_brightness, ewall_terrain);
            al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
            _draw_height_borders(map, mx, my, draw_order[di].seg_idx,
                                 seg_bottom, seg_top,
                                 draw_order[di].underside,
                                 vx, vy, lift);
        }

        /* Hover highlight */
        if (mx == map->hover_mx && my == map->hover_my) {
            al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
            ALLEGRO_COLOR hc = player_mode
                                   ? al_map_rgba(100, 255, 100, 120)
                                   : al_map_rgba(255, 255, 100, 120);
            float verts[8] = {vx[0], vy[0], vx[1], vy[1],
                              vx[2], vy[2], vx[3], vy[3]};
            al_draw_filled_polygon(verts, 4, hc);
            ALLEGRO_COLOR oc = player_mode
                                   ? al_map_rgba(100, 255, 100, 200)
                                   : al_map_rgba(255, 255, 100, 200);
            al_draw_line(vx[0], vy[0], vx[1], vy[1], oc, 1.5f);
            al_draw_line(vx[1], vy[1], vx[2], vy[2], oc, 1.5f);
            al_draw_line(vx[2], vy[2], vx[3], vy[3], oc, 1.5f);
            al_draw_line(vx[3], vy[3], vx[0], vy[0], oc, 1.5f);
        }

        /* Grid overlay */
        if (map->show_grid) {
            al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
            ALLEGRO_COLOR gc = al_map_rgba(255, 255, 255, 60);
            al_draw_line(vx[0], vy[0], vx[1], vy[1], gc, 1.0f);
            al_draw_line(vx[1], vy[1], vx[2], vy[2], gc, 1.0f);
            al_draw_line(vx[2], vy[2], vx[3], vy[3], gc, 1.0f);
            al_draw_line(vx[3], vy[3], vx[0], vy[0], gc, 1.0f);

            if (h > 0) {
                ALLEGRO_COLOR hc2 = al_map_rgba(255, 200, 100,
                                                (unsigned char)(60 + h * 35));
                float dot_x = (vx[0] + vx[2]) / 2.0f;
                float dot_y = (vy[0] + vy[1] + vy[2] + vy[3]) / 4.0f;
                al_draw_filled_circle(dot_x, dot_y, 2.0f * zoom, hc2);
            }
        }
    }

    /* Draw any remaining objects past the last entry */
    while (obj_cursor < nobj) {
        int oi = obj_order[obj_cursor];
        if (objects[oi].draw) {
            objects[oi].draw(obj_sx_arr[oi], obj_sy_arr[oi],
                             zoom, 1.0f, objects[oi].user_data);
        }
        obj_cursor++;
    }

    /* Step 7: Occlusion detection and transparent overlay pass */
    for (int i = 0; i < nobj; i++) {
        int oi = obj_order[i];
        if (objects[oi].occluded_alpha <= 0.0f) continue;
        if (!objects[oi].draw) continue;

        if (_iso_object_check_occlusion(map,
                                        objects[oi].fx, objects[oi].fy, objects[oi].fz,
                                        obj_sx_arr[oi], obj_sy_arr[oi],
                                        objects[oi].sprite_h,
                                        cam_px, cam_py, zoom)) {
            objects[oi].is_occluded = 1;
            objects[oi].draw(obj_sx_arr[oi], obj_sy_arr[oi],
                             zoom, objects[oi].occluded_alpha, objects[oi].user_data);
        }
    }

    /* Clean up */
    free(draw_order);
    if (obj_order && obj_order != obj_order_buf) free(obj_order);
    if (obj_sx_arr) free(obj_sx_arr);
    if (obj_sy_arr) free(obj_sy_arr);
} /* iso_map_draw() */

/*---------------------------------------------------------------------------
 * Transition mask & tile generation (Article 934)
 *-------------------------------------------------------------------------*/

/**
 *@brief Mask a tile bitmap to the isometric diamond shape.
 * Pixels outside the diamond are set to fully transparent (0,0,0,0).
 *@param bmp the tile bitmap to mask
 *@param tile_w tile width in pixels
 *@param tile_h tile height in pixels
 */
void iso_mask_tile_to_diamond(ALLEGRO_BITMAP* bmp, int tile_w, int tile_h) {
    __n_assert(bmp, return);

    ALLEGRO_LOCKED_REGION* lr = al_lock_bitmap(bmp,
                                               ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE, ALLEGRO_LOCK_READWRITE);
    if (!lr) return;

    for (int py = 0; py < tile_h; py++) {
        unsigned char* row = (unsigned char*)lr->data + py * lr->pitch;
        for (int px = 0; px < tile_w; px++) {
            if (!iso_is_in_diamond(px, py, tile_w, tile_h)) {
                row[px * 4 + 0] = 0;
                row[px * 4 + 1] = 0;
                row[px * 4 + 2] = 0;
                row[px * 4 + 3] = 0;
            }
        }
    }
    al_unlock_bitmap(bmp);
}

/**
 *@brief Generate the 32 procedural transition alpha masks (16 edge + 16 corner).
 *
 * For EDGE masks (0-15): each bit corresponds to one side of the diamond
 * being "covered". The alpha fades from the edge inward using a gradient
 * that follows the diamond edge normals.
 *
 * For CORNER masks (16-31): each bit corresponds to one corner of the diamond
 * being "covered". The alpha fades radially from the diamond tip inward.
 */
void iso_generate_transition_masks(ALLEGRO_BITMAP** masks, int tile_w, int tile_h) {
    __n_assert(masks, return);

    float cx = (float)tile_w / 2.0f;
    float cy = (float)tile_h / 2.0f;

    ALLEGRO_STATE state;
    al_store_state(&state, ALLEGRO_STATE_TARGET_BITMAP | ALLEGRO_STATE_BLENDER);

    /* --- Edge masks (0..15) --- */
    for (int mask = 0; mask < ISO_NUM_EDGE_MASKS; mask++) {
        masks[mask] = al_create_bitmap(tile_w, tile_h);
        al_set_target_bitmap(masks[mask]);
        al_clear_to_color(al_map_rgba(0, 0, 0, 0));

        ALLEGRO_LOCKED_REGION* lr = al_lock_bitmap(masks[mask],
                                                   ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE, ALLEGRO_LOCK_WRITEONLY);
        if (!lr) continue;

        for (int py = 0; py < tile_h; py++) {
            unsigned char* row = (unsigned char*)lr->data + py * lr->pitch;
            for (int px = 0; px < tile_w; px++) {
                if (!iso_is_in_diamond(px, py, tile_w, tile_h)) {
                    row[px * 4 + 0] = 0;
                    row[px * 4 + 1] = 0;
                    row[px * 4 + 2] = 0;
                    row[px * 4 + 3] = 0;
                    continue;
                }

                float alpha = 0.0f;
                float nx = ((float)px + 0.5f - cx) / ((float)tile_w / 2.0f);
                float ny = ((float)py + 0.5f - cy) / ((float)tile_h / 2.0f);
                float gw = 0.65f;

                if (mask & ISO_EDGE_W) alpha = fmaxf(alpha, fminf(1.0f, fmaxf(0.0f, (-nx - ny) / gw)));
                if (mask & ISO_EDGE_N) alpha = fmaxf(alpha, fminf(1.0f, fmaxf(0.0f, (nx - ny) / gw)));
                if (mask & ISO_EDGE_E) alpha = fmaxf(alpha, fminf(1.0f, fmaxf(0.0f, (nx + ny) / gw)));
                if (mask & ISO_EDGE_S) alpha = fmaxf(alpha, fminf(1.0f, fmaxf(0.0f, (-nx + ny) / gw)));

                if (alpha > 1.0f) alpha = 1.0f;
                alpha = alpha * alpha * (3.0f - 2.0f * alpha);

                unsigned char a = (unsigned char)(alpha * 255.0f);
                row[px * 4 + 0] = a;
                row[px * 4 + 1] = a;
                row[px * 4 + 2] = a;
                row[px * 4 + 3] = a;
            }
        }
        al_unlock_bitmap(masks[mask]);
    }

    /* --- Corner masks (16..31) ---
     * In isometric diamond projection, diagonal neighbors map to diamond tips:
     *   NW (mx-1,my-1) = North tip  (0, -1) in normalized coords
     *   NE (mx+1,my-1) = East tip   (1,  0)
     *   SE (mx+1,my+1) = South tip  (0,  1)
     *   SW (mx-1,my+1) = West tip   (-1, 0) */
    for (int mask = 0; mask < ISO_NUM_CORNER_MASKS; mask++) {
        int idx = ISO_NUM_EDGE_MASKS + mask;
        masks[idx] = al_create_bitmap(tile_w, tile_h);
        al_set_target_bitmap(masks[idx]);
        al_clear_to_color(al_map_rgba(0, 0, 0, 0));

        ALLEGRO_LOCKED_REGION* lr = al_lock_bitmap(masks[idx],
                                                   ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE, ALLEGRO_LOCK_WRITEONLY);
        if (!lr) continue;

        for (int py = 0; py < tile_h; py++) {
            unsigned char* row = (unsigned char*)lr->data + py * lr->pitch;
            for (int px = 0; px < tile_w; px++) {
                if (!iso_is_in_diamond(px, py, tile_w, tile_h)) {
                    row[px * 4 + 0] = 0;
                    row[px * 4 + 1] = 0;
                    row[px * 4 + 2] = 0;
                    row[px * 4 + 3] = 0;
                    continue;
                }

                float alpha = 0.0f;
                float nx = ((float)px + 0.5f - cx) / ((float)tile_w / 2.0f);
                float ny = ((float)py + 0.5f - cy) / ((float)tile_h / 2.0f);
                float corner_radius = 0.55f;

                if (mask & ISO_CORNER_NW) {
                    float dy = ny + 1.0f;
                    float dist = sqrtf(nx * nx + dy * dy);
                    float c_alpha = fmaxf(0.0f, 1.0f - dist / corner_radius);
                    alpha = fmaxf(alpha, c_alpha);
                }
                if (mask & ISO_CORNER_NE) {
                    float dx = nx - 1.0f;
                    float dist = sqrtf(dx * dx + ny * ny);
                    float c_alpha = fmaxf(0.0f, 1.0f - dist / corner_radius);
                    alpha = fmaxf(alpha, c_alpha);
                }
                if (mask & ISO_CORNER_SE) {
                    float dy = ny - 1.0f;
                    float dist = sqrtf(nx * nx + dy * dy);
                    float c_alpha = fmaxf(0.0f, 1.0f - dist / corner_radius);
                    alpha = fmaxf(alpha, c_alpha);
                }
                if (mask & ISO_CORNER_SW) {
                    float dx = nx + 1.0f;
                    float dist = sqrtf(dx * dx + ny * ny);
                    float c_alpha = fmaxf(0.0f, 1.0f - dist / corner_radius);
                    alpha = fmaxf(alpha, c_alpha);
                }

                if (alpha > 1.0f) alpha = 1.0f;
                alpha = alpha * alpha * (3.0f - 2.0f * alpha);

                unsigned char a = (unsigned char)(alpha * 255.0f);
                row[px * 4 + 0] = a;
                row[px * 4 + 1] = a;
                row[px * 4 + 2] = a;
                row[px * 4 + 3] = a;
            }
        }
        al_unlock_bitmap(masks[idx]);
    }

    al_restore_state(&state);
    n_log(LOG_INFO, "Generated %d transition masks (%dx%d)", ISO_NUM_MASKS, tile_w, tile_h);
}

/**
 *@brief Pre-composite transition tiles (terrain texture * alpha mask).
 * tiles[t][m] = terrain t composited with mask m.
 */
void iso_generate_transition_tiles(ALLEGRO_BITMAP*** tiles,
                                   ALLEGRO_BITMAP** masks,
                                   ALLEGRO_BITMAP** tile_bitmaps,
                                   int num_terrains,
                                   int tile_w,
                                   int tile_h) {
    __n_assert(tiles, return);
    __n_assert(masks, return);
    __n_assert(tile_bitmaps, return);

    ALLEGRO_STATE state;
    al_store_state(&state, ALLEGRO_STATE_TARGET_BITMAP | ALLEGRO_STATE_BLENDER);

    for (int t = 0; t < num_terrains; t++) {
        for (int m = 0; m < ISO_NUM_MASKS; m++) {
            /* Skip mask 0 (no edges active) and mask 16 (no corners active) */
            if ((m < ISO_NUM_EDGE_MASKS && m == 0) ||
                (m >= ISO_NUM_EDGE_MASKS && m == ISO_NUM_EDGE_MASKS)) {
                tiles[t][m] = NULL;
                continue;
            }

            tiles[t][m] = al_create_bitmap(tile_w, tile_h);
            al_set_target_bitmap(tiles[t][m]);
            al_clear_to_color(al_map_rgba(0, 0, 0, 0));

            /* Draw the terrain tile first */
            al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);
            al_draw_bitmap(tile_bitmaps[t], 0, 0, 0);

            /* Multiply by the alpha mask: keeps terrain color
             * but applies the mask's alpha for the transition gradient */
            al_set_blender(ALLEGRO_ADD, ALLEGRO_DEST_COLOR, ALLEGRO_ZERO);
            al_draw_bitmap(masks[m], 0, 0, 0);
        }
    }

    al_restore_state(&state);
    n_log(LOG_INFO, "Pre-composited transition tiles for %d terrains x %d masks", num_terrains, ISO_NUM_MASKS);
}

#endif /* HAVE_ALLEGRO - iso_map_draw */

#ifdef N_ASTAR_H
/**
 *@brief Create an ASTAR_GRID from an ISO_MAP.
 * Each cell is walkable if its ability is WALK or SWIM, and the height
 * difference to all cardinal neighbors is within max_height_diff.
 * The cost of each cell is proportional to its height (higher = more costly).
 *@param map the iso map
 *@param max_height_diff maximum height difference that is still walkable
 *@return new ASTAR_GRID or NULL
 */
ASTAR_GRID* iso_map_to_astar_grid(const ISO_MAP* map, int max_height_diff, int start_x, int start_y) {
    __n_assert(map, return NULL);

    ASTAR_GRID* grid = n_astar_grid_new(map->width, map->height, 1);
    if (!grid) return NULL;

    int total = map->width * map->height;

    /* First pass: all cells unwalkable, set costs */
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            n_astar_grid_set_walkable(grid, x, y, 0, 0);
            int h = map->heightmap[y * map->width + x];
            int cost = ASTAR_COST_CARDINAL + h * 100;
            n_astar_grid_set_cost(grid, x, y, 0, cost);
        }
    }

    /* Flood-fill from (start_x, start_y) to mark reachable cells as walkable.
     * This ensures height constraints are checked per-edge (between consecutive
     * cells) rather than per-cell, so tiles on a plateau edge remain walkable
     * when approached from the same elevation. */
    if (start_x < 0 || start_x >= map->width || start_y < 0 || start_y >= map->height)
        return grid;

    int start_ab = map->ability[start_y * map->width + start_x];
    if (start_ab != WALK && start_ab != SWIM)
        return grid;

    int* queue = (int*)malloc((size_t)total * 2 * sizeof(int));
    if (!queue) {
        n_astar_grid_free(grid);
        return NULL;
    }

    uint8_t* visited = (uint8_t*)calloc((size_t)total, sizeof(uint8_t));
    if (!visited) {
        free(queue);
        n_astar_grid_free(grid);
        return NULL;
    }

    int qfront = 0, qback = 0;

    /* seed the start cell */
    visited[start_y * map->width + start_x] = 1;
    n_astar_grid_set_walkable(grid, start_x, start_y, 0, 1);
    queue[qback++] = start_x;
    queue[qback++] = start_y;

    /* 8-directional expansion (matches ASTAR_ALLOW_DIAGONAL) */
    int dirs[][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};

    while (qfront < qback) {
        int cx = queue[qfront++];
        int cy = queue[qfront++];
        int ch = map->heightmap[cy * map->width + cx];

        for (int d = 0; d < 8; d++) {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];
            if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
            if (visited[ny * map->width + nx]) continue;

            int nab = map->ability[ny * map->width + nx];
            if (nab != WALK && nab != SWIM) continue;

            int nh = map->heightmap[ny * map->width + nx];
            if (max_height_diff >= 0 && abs(ch - nh) > max_height_diff) continue;

            visited[ny * map->width + nx] = 1;
            n_astar_grid_set_walkable(grid, nx, ny, 0, 1);
            queue[qback++] = nx;
            queue[qback++] = ny;
        }
    }

    free(queue);
    free(visited);

    return grid;
}
#endif /* N_ASTAR_H */

#endif /* #ifndef NOISOENGINE */
