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
 *@example ex_gui_layout_persist.c
 *@brief Widget-state persistence round-trip for n_gui_save/load_layout_json:
 *       a keyed splitpane's ratio and a keyed datagrid's column layout survive a
 *       save + reload into a fresh context, including the lazily-created-widget path
 *       (n_gui_widget_set_persist_key applies the pending state). Headless.
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

static int feq(float a, float b) {
    return fabsf(a - b) < 0.01f;
}

/* Build a window "W1" with a keyed splitpane and a keyed 3-column datagrid. */
static void build_window(N_GUI_CTX* gui) {
    int win = n_gui_add_window(gui, "W1", 0.0f, 0.0f, 400.0f, 300.0f);
    int sp = n_gui_add_splitpane(gui, win, 0, 0, 400, 300, N_GUI_SPLIT_VERTICAL, 0.5f, NULL, NULL);
    int dg = n_gui_add_datagrid(gui, win, 0, 0, 400, 200, NULL, NULL);
    n_gui_datagrid_add_column(gui, dg, "A", 80.0f);
    n_gui_datagrid_add_column(gui, dg, "B", 90.0f);
    n_gui_datagrid_add_column(gui, dg, "C", 100.0f);
    /* keying AFTER a load applies any pending state; before a load it is a no-op */
    n_gui_widget_set_persist_key(gui, sp, "split.main");
    n_gui_widget_set_persist_key(gui, dg, "grid.main");
}

/* Find a widget id in a window by walking the context's id space is awkward; instead
 * we rebuild deterministically and re-fetch by known creation order via the getters. */
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

    char path[128];
    snprintf(path, sizeof(path), "ex_gui_layout_persist_%d.json", (int)getpid());

    /* ---- session 1: build, adjust, save ---- */
    int sp_id = -1, dg_id = -1;
    {
        N_GUI_CTX* gui = n_gui_new_ctx(font);
        expect_true("ctx 1 created", gui != NULL);
        n_gui_set_display_size(gui, 1000.0f, 800.0f);

        int win = n_gui_add_window(gui, "W1", 10.0f, 20.0f, 400.0f, 300.0f);
        sp_id = n_gui_add_splitpane(gui, win, 0, 0, 400, 300, N_GUI_SPLIT_VERTICAL, 0.5f, NULL, NULL);
        dg_id = n_gui_add_datagrid(gui, win, 0, 0, 400, 200, NULL, NULL);
        n_gui_datagrid_add_column(gui, dg_id, "A", 80.0f);
        n_gui_datagrid_add_column(gui, dg_id, "B", 90.0f);
        n_gui_datagrid_add_column(gui, dg_id, "C", 100.0f);
        n_gui_widget_set_persist_key(gui, sp_id, "split.main");
        n_gui_widget_set_persist_key(gui, dg_id, "grid.main");
        expect_true("persist key round-trips", n_gui_widget_get_persist_key(gui, sp_id) && strcmp(n_gui_widget_get_persist_key(gui, sp_id), "split.main") == 0);

        /* the user adjusts: drag the divider, resize + hide + reorder columns */
        n_gui_splitpane_set_ratio(gui, sp_id, 0.3f);
        n_gui_datagrid_set_column_width(gui, dg_id, 0, 123.0f);
        n_gui_datagrid_set_column_visible(gui, dg_id, 1, 0); /* hide column B */
        n_gui_datagrid_move_column(gui, dg_id, 2, 0);        /* move C to the front */
        expect_true("column C moved to display 0", n_gui_datagrid_display_to_physical(gui, dg_id, 0) == 2);

        /* the user also reduces the window to its title bar (the minimise icon) */
        n_gui_open_window(gui, win);
        expect_true("window not minimised initially", n_gui_window_is_minimised(gui, win) == 0);
        n_gui_minimize_window(gui, win);
        expect_true("window minimised", n_gui_window_is_minimised(gui, win) == 1);

        expect_true("save layout ok", n_gui_save_layout_json(gui, path) == 0);
        n_gui_destroy_ctx(&gui);
    }

    /* ---- session 2: load into a FRESH ctx BEFORE the window exists (lazy path) ---- */
    {
        N_GUI_CTX* gui = n_gui_new_ctx(font);
        expect_true("ctx 2 created", gui != NULL);
        n_gui_set_display_size(gui, 1000.0f, 800.0f);

        /* load first: the window/widgets do not exist yet, so state is stashed pending */
        expect_true("load layout ok", n_gui_load_layout_json(gui, path) == 0);

        int win = n_gui_add_window(gui, "W1", 0.0f, 0.0f, 400.0f, 300.0f);
        int sp = n_gui_add_splitpane(gui, win, 0, 0, 400, 300, N_GUI_SPLIT_VERTICAL, 0.5f, NULL, NULL);
        int dg = n_gui_add_datagrid(gui, win, 0, 0, 400, 200, NULL, NULL);
        n_gui_datagrid_add_column(gui, dg, "A", 80.0f);
        n_gui_datagrid_add_column(gui, dg, "B", 90.0f);
        n_gui_datagrid_add_column(gui, dg, "C", 100.0f);

        /* window geometry restored on create (pending_layout) */
        N_GUI_WINDOW* w = n_gui_get_window(gui, win);
        expect_true("window x restored", w && feq(w->x, 10.0f));
        expect_true("window y restored", w && feq(w->y, 20.0f));

        /* before keying, defaults still hold */
        expect_true("ratio default before key", feq(n_gui_splitpane_get_ratio(gui, sp), 0.5f));

        /* keying applies the pending state */
        n_gui_widget_set_persist_key(gui, sp, "split.main");
        n_gui_widget_set_persist_key(gui, dg, "grid.main");

        expect_true("splitpane ratio restored", feq(n_gui_splitpane_get_ratio(gui, sp), 0.3f));
        expect_true("column A width restored", feq(n_gui_datagrid_get_column_width(gui, dg, 0), 123.0f));
        expect_true("column B hidden restored", n_gui_datagrid_get_column_visible(gui, dg, 1) == 0);
        expect_true("column order restored (C first)", n_gui_datagrid_display_to_physical(gui, dg, 0) == 2);

        /* the minimised state survives the round-trip, and restore always clears it
           (so an "open this dialog" action can show the content unconditionally) */
        expect_true("minimised state restored", n_gui_window_is_minimised(gui, win) == 1);
        n_gui_restore_window(gui, win);
        expect_true("restore clears minimised", n_gui_window_is_minimised(gui, win) == 0);
        n_gui_restore_window(gui, win); /* idempotent on an already-restored window */
        expect_true("restore is idempotent", n_gui_window_is_minimised(gui, win) == 0);

        /* a widget with a key that has no saved entry keeps its defaults (no crash) */
        int sp2 = n_gui_add_splitpane(gui, win, 0, 0, 100, 100, N_GUI_SPLIT_HORIZONTAL, 0.42f, NULL, NULL);
        n_gui_widget_set_persist_key(gui, sp2, "split.unsaved");
        expect_true("unsaved key keeps default", feq(n_gui_splitpane_get_ratio(gui, sp2), 0.42f));

        n_gui_destroy_ctx(&gui);
    }

    /* ---- guards: a non-persisted context saves/loads cleanly, keying an unknown id is safe ---- */
    {
        N_GUI_CTX* gui = n_gui_new_ctx(font);
        n_gui_widget_set_persist_key(gui, 9999, "nope"); /* invalid id: no crash */
        n_gui_restore_window(gui, 9999);                 /* invalid id: no crash */
        expect_true("is_minimised on invalid id is 0", n_gui_window_is_minimised(gui, 9999) == 0);
        expect_true("get_persist_key on invalid id is NULL", n_gui_widget_get_persist_key(gui, 9999) == NULL);
        build_window(gui); /* exercises the build helper (no prior load: keys are no-ops) */
        n_gui_destroy_ctx(&gui);
    }

    unlink(path);
    al_destroy_font(font);

    if (failures) {
        printf("ex_gui_layout_persist: %d FAILURE(S)\n", failures);
        n_log(LOG_ERR, "ex_gui_layout_persist: %d failure(s)", failures);
        return 1;
    }
    printf("ex_gui_layout_persist: all checks passed\n");
    return 0;
}
