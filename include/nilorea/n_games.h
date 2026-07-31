/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 *@file n_games.h
 *@brief Games helpers functions
 *@author Castagnier Mickael
 *@version 2.0
 *@date 26/06/2015
 */

#ifndef __N_GAMES__
#define __N_GAMES__

#ifdef __cplusplus
extern "C" {
#endif
/**@defgroup GAME_ENV GAME APPS: game environment management utilities
   @addtogroup GAME_ENV
  @{
*/

#include "n_common.h"
#include "n_log.h"
#include "n_time.h"

#ifdef HAVE_ALLEGRO
#include <allegro5/allegro.h>
#endif

/*! full use of CPU */
#define CPU_USE_FULL -1
/*! let the other process have some times */
#define CPU_USE_NICE 0
/*! wait more */
#define CPU_USE_LESS 10

/*! display mode: windowed */
#define GFX_WINDOWED 0
/*! display mode: fullscreen */
#define GFX_FULLSCREEN 1
/*! display mode: fullscreen windowed */
#define GFX_FULLSCREEN_WINDOW 2

/*! Game Environment structure */
typedef struct GAME_ENV {
    /*! Store score value */
    int score,
        /*! Remaining lives */
        lifes,
        /*! wanna blur effect ? ( 0 or 1  ) */
        BLUR,
        /*! alpha value of black blit  ( 20 default ) */
        fade_value,
        /*! loop while DONE != 1 */
        DONE,
        /*! display mode: GFX_WINDOWED, GFX_FULLSCREEN, or GFX_FULLSCREEN_WINDOW */
        GFX_CONFIG_MODE,
        /*! status of the cpu mode , CPU_USE_FULL by default */
        CPU_MODE,
        /*! time between each loop */
        loop_time,
        /*! time before updating graphics in usec*/
        draw_time,
        /*! time before a new logic update is done */
        logic_time,
        /*! minimum number of particles */
        nb_min_particles,
        /*! =0 for doing nothing, =1 if logic is done, = 2 if drawing is done too */
        wait_for_slowing_down_cpu,
        /*! time between two graphic frames  20000 default */
        GFX_UPDATE_RATE,
        /*! time between two logic frames  20000 default */
        LOGIC_RATE,
        /*! Measured framerate */
        real_framerate,
        /*! Level number */
        level,
        /*! position x */
        x,
        /*! position y */
        y,
        /*! position z */
        z,
        /*! left attack trigger */
        left_attack,
        /*! right attack trigger */
        right_attack,
        /*! left attack position */
        left_attack_pos,
        /*! right attack position */
        right_attack_pos;

    /*! TIMER */
    N_TIME game_timer;

#ifdef HAVE_ALLEGRO
    /*! Allegro5 display */
    ALLEGRO_DISPLAY* display;
    /*! screen buffer bitmap */
    ALLEGRO_BITMAP* scrbuf;
    /*! event queue */
    ALLEGRO_EVENT_QUEUE* event_queue;
    /*! logic timer */
    ALLEGRO_TIMER* logic_timer;
    /*! drawing timer */
    ALLEGRO_TIMER* draw_timer;
#endif

} GAME_ENV;

/*! initialize a new game environment */
int init_game_env(GAME_ENV** game);

/*! destroy a game environment */
void destroy_game_env(GAME_ENV** game);

/*! load a game config from file */
GAME_ENV* load_game_config(char* config_name);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __N_GAMES__  */
