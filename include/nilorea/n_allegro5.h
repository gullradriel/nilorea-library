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
 *@file n_allegro5.h
 *@brief Allegro5 helpers
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/05/2020
 */

#ifndef __NILOREA_ALLEGRO5_HELPER__
#define __NILOREA_ALLEGRO5_HELPER__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup ALLEGRO5 ALLEGRO5: allegro 5 helpers (user input,...)
   @addtogroup ALLEGRO5
  @{
  */

#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_native_dialog.h>
#include <allegro5/allegro_ttf.h>

/*! process keyboard event and append character to string */
int get_keyboard(ALLEGRO_USTR* str, ALLEGRO_EVENT event);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __NILOREA_ALLEGRO5_HELPER__ */
