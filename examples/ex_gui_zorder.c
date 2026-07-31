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
 *@example ex_gui_zorder.c
 *@brief N_GUI mouse dispatch hits the topmost overlapping widget (headless).
 *@author Castagnier Mickael
 *@version 1.0
 */

#define ALLEGRO_UNSTABLE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nilorea/n_common.h"
#include "nilorea/n_gui.h"
#include "nilorea/n_log.h"

static int log_level = LOG_ERR;
static int failures = 0;
static int g_selected_row = -1; /* set by the datagrid's on_select callback */

static int process_args(int argc, char** argv) {
    int opt;
    while ((opt = getopt(argc, argv, "V:")) != -1) {
        switch (opt) {
            case 'V':
                if (!strcmp("LOG_NULL", optarg))
                    log_level = LOG_NULL;
                else if (!strcmp("LOG_NOTICE", optarg))
                    log_level = LOG_NOTICE;
                else if (!strcmp("LOG_INFO", optarg))
                    log_level = LOG_INFO;
                else if (!strcmp("LOG_ERR", optarg))
                    log_level = LOG_ERR;
                else if (!strcmp("LOG_DEBUG", optarg))
                    log_level = LOG_DEBUG;
                else
                    return -1;
                break;
            default:
                return -1;
        }
    }
    return 0;
}

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    }
}

/* datagrid row-select callback: record which row was selected */
static void on_grid_select(int widget_id, int row, void* user_data) {
    (void)widget_id;
    (void)user_data;
    g_selected_row = row;
}

/* synthesize a left-button press at (x,y) and feed it to the GUI */
static void click_at(N_GUI_CTX* gui, int x, int y) {
    ALLEGRO_EVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.button = 1;
    ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
    n_gui_process_event(gui, ev);
    ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
    n_gui_process_event(gui, ev);
}

int main(int argc, char** argv) {
    int i;
    set_log_level(LOG_ERR);
    if (process_args(argc, argv) != 0) {
        fprintf(stderr, "Usage: %s [-V LOG_LEVEL]\n", argv[0]);
        return 2;
    }
    set_log_level(log_level);

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
    n_gui_set_display_size(gui, 800.0f, 600.0f);

    {
        int win = n_gui_add_window(gui, "Panel", 0.0f, 0.0f, 800.0f, 600.0f);
        int split, grid;
        expect_true("window created", win >= 0);
        n_gui_window_set_flags(gui, win, N_GUI_WIN_FRAMELESS);

        /* a split pane added first (drawn underneath), then a datagrid placed
           over its left region (drawn on top) -- the exact Proxy-view shape */
        split = n_gui_add_splitpane(gui, win, 0.0f, 0.0f, 790.0f, 560.0f, N_GUI_SPLIT_VERTICAL, 0.5f, NULL, NULL);
        expect_true("splitpane created", split >= 0);
        grid = n_gui_add_datagrid(gui, win, 0.0f, 0.0f, 380.0f, 560.0f, on_grid_select, NULL);
        expect_true("datagrid created", grid >= 0);
        n_gui_datagrid_add_column(gui, grid, "#", 60.0f);
        n_gui_datagrid_add_column(gui, grid, "Name", 280.0f);
        for (i = 0; i < 20; i++) {
            char a[8], b[16];
            const char* vals[2];
            snprintf(a, sizeof(a), "%d", i + 1);
            snprintf(b, sizeof(b), "row %d", i + 1);
            vals[0] = a;
            vals[1] = b;
            n_gui_datagrid_add_row(gui, grid, vals);
        }

        /* click well inside the grid's data area (below the header), over the
           region the split pane also covers; the topmost widget (the datagrid)
           must receive it and select a row */
        g_selected_row = -1;
        click_at(gui, 30, 60);
        expect_true("datagrid received the click (topmost wins)", g_selected_row >= 0);
    }

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_zorder: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_zorder: all checks passed");
    return 0;
}
