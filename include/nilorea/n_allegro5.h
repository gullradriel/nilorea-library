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
