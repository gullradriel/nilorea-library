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
 *@file n_allegro5.c
 *@brief Allegro5 helpers
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/05/2020
 */

#include "nilorea/n_common.h"
#include "nilorea/n_allegro5.h"

/**
 *@brief update a keyboard buffer from an event
 *@param str A string to hold the inputs
 *@param event The event in which we'll try to read an input
 *@return TRUE or FALSE
 */
int get_keyboard(ALLEGRO_USTR* str, ALLEGRO_EVENT event) {
    __n_assert(str, return FALSE);
    if (event.type == ALLEGRO_EVENT_KEY_CHAR) {
        if (event.keyboard.unichar >= 32) {
            al_ustr_append_chr(str, event.keyboard.unichar);
            return TRUE;
        } else if (event.keyboard.keycode == ALLEGRO_KEY_BACKSPACE) {
            int pos = (int)al_ustr_size(str);
            if (al_ustr_prev(str, &pos))
                al_ustr_truncate(str, pos);
            return TRUE;
        }
    }
    return FALSE;
}
