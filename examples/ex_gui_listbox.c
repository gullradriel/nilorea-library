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
 *@example ex_gui_listbox.c
 *@brief Listbox horizontal-scroll regression test (headless): the content width
 *       tracks the widest item, the scroll offset clamps to it, a press on the
 *       horizontal track scrolls instead of selecting a row, and clearing the
 *       list resets the offset.
 *@author Castagnier Mickael
 *@version 1.0
 */

#define ALLEGRO_UNSTABLE 1

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_gui.h"

static int failures = 0;

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    }
}

static void mouse_button(N_GUI_CTX* gui, int down, float x, float y) {
    ALLEGRO_EVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? ALLEGRO_EVENT_MOUSE_BUTTON_DOWN : ALLEGRO_EVENT_MOUSE_BUTTON_UP;
    ev.mouse.button = 1;
    ev.mouse.x = (int)x;
    ev.mouse.y = (int)y;
    n_gui_process_event(gui, ev);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    set_log_level(LOG_ERR);

    if (!al_init() || !al_init_font_addon()) {
        n_log(LOG_ERR, "allegro init failed");
        return 1;
    }
    ALLEGRO_FONT* font = al_create_builtin_font();
    if (!font) {
        n_log(LOG_ERR, "al_create_builtin_font failed");
        return 1;
    }
    N_GUI_CTX* gui = n_gui_new_ctx(font);
    if (!gui) {
        n_log(LOG_ERR, "n_gui_new_ctx failed");
        al_destroy_font(font);
        return 1;
    }
    n_gui_set_display_size(gui, 400.0f, 300.0f);

    int win = n_gui_add_window(gui, "List", 0.0f, 0.0f, 200.0f, 120.0f);
    n_gui_window_set_flags(gui, win, N_GUI_WIN_FRAMELESS | N_GUI_WIN_FIXED_POSITION);
    int lb = n_gui_add_listbox(gui, win, 0.0f, 0.0f, 120.0f, 80.0f,
                               N_GUI_SELECT_SINGLE, NULL, NULL);
    expect_true("listbox created", win >= 0 && lb >= 0);

    /* an empty list needs no width, and short items stay within the widget */
    expect_true("empty content width", n_gui_listbox_content_width(gui, lb) == 0.0f);
    n_gui_listbox_add_item(gui, lb, "a");
    n_gui_listbox_add_item(gui, lb, "bb");
    float narrow = n_gui_listbox_content_width(gui, lb);
    expect_true("short items fit the widget", narrow > 0.0f && narrow < 120.0f);

    /* a long item is what makes the list overflow */
    n_gui_listbox_add_item(gui, lb,
        "GET https://api.example.com/v1/some/quite/long/resource/path?with=query");
    float wide = n_gui_listbox_content_width(gui, lb);
    expect_true("content width follows the widest item", wide > narrow);
    expect_true("content overflows the widget", wide > 120.0f);

    /* the offset is settable and never negative */
    n_gui_listbox_set_h_scroll(gui, lb, 40.0f);
    expect_true("h_scroll set", fabsf(n_gui_listbox_get_h_scroll(gui, lb) - 40.0f) < 0.01f);
    n_gui_listbox_set_h_scroll(gui, lb, -20.0f);
    expect_true("negative h_scroll clamped", n_gui_listbox_get_h_scroll(gui, lb) == 0.0f);

    /* a press on the horizontal track scrolls the list; it must NOT pick the
       row that happens to be under the scrollbar */
    {
        float sb = gui->style.scrollbar_size;
        float rows_h = 80.0f - sb;
        n_gui_listbox_set_h_scroll(gui, lb, 0.0f);
        mouse_button(gui, 1, 100.0f, rows_h + sb * 0.5f);
        expect_true("track press scrolled the list",
                    n_gui_listbox_get_h_scroll(gui, lb) > 0.0f);
        expect_true("track press selected nothing",
                    n_gui_listbox_get_selected(gui, lb) == -1);
        mouse_button(gui, 0, 100.0f, rows_h + sb * 0.5f);

        /* and a press on a row still selects that row */
        mouse_button(gui, 1, 40.0f, 4.0f);
        expect_true("row press selects", n_gui_listbox_get_selected(gui, lb) == 0);
        mouse_button(gui, 0, 40.0f, 4.0f);
    }

    /* refilling the list starts from the left again */
    n_gui_listbox_set_h_scroll(gui, lb, 30.0f);
    n_gui_listbox_clear(gui, lb);
    expect_true("clear resets the offset", n_gui_listbox_get_h_scroll(gui, lb) == 0.0f);
    expect_true("clear empties the content width",
                n_gui_listbox_content_width(gui, lb) == 0.0f);

    /* non-listbox widgets answer harmlessly */
    {
        int btn = n_gui_add_button(gui, win, "b", 0.0f, 90.0f, 40.0f, 20.0f,
                                   N_GUI_SHAPE_RECT, NULL, NULL);
        expect_true("button content width is 0", n_gui_listbox_content_width(gui, btn) == 0.0f);
        expect_true("button h_scroll is 0", n_gui_listbox_get_h_scroll(gui, btn) == 0.0f);
        n_gui_listbox_set_h_scroll(gui, btn, 10.0f); /* no-op */
        expect_true("button still has no h_scroll", n_gui_listbox_get_h_scroll(gui, btn) == 0.0f);
    }

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_listbox: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_listbox: all checks passed");
    return 0;
}
