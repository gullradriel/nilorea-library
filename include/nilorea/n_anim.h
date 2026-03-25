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
 *@file n_anim.h
 *@brief Animations graphics and animations parameters
 *@author Castagnier Mickael aka Gull Ra Driel
 *@version 1.0
 *@date 30/12/2016
 */

#ifndef __N_ANIM_HEADER
#define __N_ANIM_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include "n_common.h"
#include "n_log.h"
#include "n_str.h"
#include "allegro5/allegro.h"

/**@defgroup ANIM_LIB GRAPHICS: animation library, loading structures and blit funcs
  @addtogroup ANIM_LIB
  @{
  */

/*! struct of the properties of a frame in an animation */
typedef struct ANIM_FRAME {
    int x,
        y;
    unsigned int duration;
} ANIM_FRAME;

/*! struct of an animation */
typedef struct ANIM_GFX {
    /*! each frame properties */
    ANIM_FRAME* frames;
    /*! bitmap with a list of lil' bitmap inside */
    ALLEGRO_BITMAP* bmp;

    /*! blit type: masked with color, blended, brut */
    unsigned int type,
        /*! width of a frame */
        w,
        /*! height of a frame */
        h,
        /*! nb frames in anim */
        nb_frames;
} ANIM_GFX;

/*! structure of a library of gfxs */
typedef struct ANIM_LIB {
    /*! Stack of gfxs */
    ANIM_GFX** gfxs;

    /*! size of the stack */
    unsigned int nb_max_gfxs;

    /*! name of the anim library */
    char* name;
} ANIM_LIB;

/*! animation properties */
typedef struct ANIM_DATA {
    unsigned int id,
        /*! x coordinate of the animation */
        x,
        /*! y coordinate of the animation */
        y,
        /*! id of the current frame */
        frame,
        /*! elapsed time since last frame change */
        elapsed;

    /*! pointer to an anim gfx library*/
    ANIM_LIB* lib;
} ANIM_DATA;

/*! create a new animation library */
ANIM_LIB* create_anim_library(char* name, unsigned int size);
/*! delete a bitmap from the library */
int delete_bmp_from_lib(ANIM_LIB* lib, unsigned int id);
/*! add a bitmap to the library */
int add_bmp_to_lib(ANIM_LIB* lib, unsigned int pos, char* file, char* resfile);
/*! update animation frame */
int update_anim(ANIM_DATA* data, unsigned int delta_t);
/*! draw current animation frame */
int draw_anim(ANIM_DATA* data, int x, int y);
/*! destroy an animation library */
int destroy_anim_lib(ANIM_LIB** lib);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif  // header guard
