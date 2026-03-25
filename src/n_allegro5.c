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
