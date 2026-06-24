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
 *@example ex_gui_kvtable.c
 *@brief N_GUI_KVTABLE adaptive-resize regression test.
 *
 * Headless test (no display, builtin font) that exercises the kvtable
 * layout across N_GUI_WIN_RESIZE_SCALE display resizes and clear/add
 * cycles. Asserts:
 *   - "+" button sits below the last active row at all sizes.
 *   - New rows added after a resize match the height of previously
 *     scaled rows.
 *   - After clear-all + add, the "+" button does not overlap the
 *     "Key" header label.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 20/05/26
 */

#define ALLEGRO_UNSTABLE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_gui.h"

static int log_level = LOG_ERR;

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

static int failures = 0;

static void expect_close(const char* label, float got, float want, float tol) {
    float d = got - want;
    if (d < 0) d = -d;
    if (d > tol) {
        n_log(LOG_ERR, "%s: got %.3f, expected %.3f (tol %.3f)",
              label, got, want, tol);
        failures++;
    } else {
        n_log(LOG_INFO, "%s: %.3f ~= %.3f OK", label, got, want);
    }
}

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    } else {
        n_log(LOG_INFO, "%s: OK", label);
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    if (process_args(argc, argv) != 0) {
        fprintf(stderr, "Usage: %s [-V LOG_LEVEL]\n", argv[0]);
        return 2;
    }
    set_log_level(log_level);

    if (!al_init()) {
        n_log(LOG_ERR, "al_init failed");
        return 1;
    }
    if (!al_init_font_addon()) {
        n_log(LOG_ERR, "al_init_font_addon failed");
        return 1;
    }

    /* Builtin font does not need a display. */
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

    /* Reference display matches the parent panel size used in Hook's
       request panel (params/headers tab). Set display size BEFORE
       switching to adaptive mode so ref_display_w/h is captured. */
    n_gui_set_display_size(gui, 800.0f, 258.0f);
    n_gui_set_resize_mode(gui, N_GUI_RESIZE_ADAPTIVE);

    int win = n_gui_add_window(gui, "Panel", 0.0f, 0.0f, 800.0f, 258.0f);
    expect_true("window created", win >= 0);
    n_gui_window_set_resize_policy(gui, win, N_GUI_WIN_RESIZE_SCALE);

    N_GUI_KVTABLE* kv = n_gui_kvtable_create(gui, win, 28.0f, 4.0f, NULL, NULL);
    expect_true("kvtable created", kv != NULL);

    /* Initial: one empty row, like Hook does. */
    n_gui_kvtable_add_row(kv, "", "", "", 1);

    N_GUI_WIDGET* add_btn = n_gui_get_widget(gui, kv->btn_add);
    N_GUI_WIDGET* row0_key = n_gui_get_widget(gui, kv->rows[0].key_id);
    expect_true("add button widget exists", add_btn != NULL);
    expect_true("row 0 key widget exists", row0_key != NULL);

    /* At initial size, "+" button must sit strictly below row 0. */
    expect_true("+ below row 0 (initial)",
                add_btn->y >= row0_key->y + row0_key->h - 0.5f);

    /* Resize the display: window scales by 2x in both dimensions. */
    n_gui_set_display_size(gui, 1600.0f, 516.0f);

    /* After resize via apply_adaptive_resize, fetch fresh pointers
       (same widgets, but their x/y/w/h have moved). */
    add_btn = n_gui_get_widget(gui, kv->btn_add);
    row0_key = n_gui_get_widget(gui, kv->rows[0].key_id);
    expect_true("+ still exists after resize", add_btn != NULL);

    /* The bug was: after resize, "+" reverted to the same y as row 0.
       It should remain at or below row 0's bottom edge. */
    expect_true("+ below row 0 (after resize)",
                add_btn->y >= row0_key->y + row0_key->h - 0.5f);

    /* Record row 0's scaled height for later comparison. */
    float scaled_row_h = row0_key->h;
    n_log(LOG_INFO, "row 0 scaled height after resize: %.3f", scaled_row_h);

    /* Clear all rows then add new ones, mimicking Hook's "delete all
       lines then add new lines" path on a maximized window. */
    for (int i = kv->nb_rows - 1; i >= 0; i--) {
        n_gui_kvtable_remove_row(kv, i);
    }
    n_gui_kvtable_add_row(kv, "k", "v", "d", 1);
    n_gui_kvtable_add_row(kv, "k2", "v2", "d2", 1);

    add_btn = n_gui_get_widget(gui, kv->btn_add);
    N_GUI_WIDGET* lbl_key = n_gui_get_widget(gui, kv->lbl_key);

    /* "+" must NOT overlap the "Key" header label. */
    expect_true("+ button below Key header (after clear+add)",
                add_btn->y >= lbl_key->y + lbl_key->h - 0.5f);

    /* New rows must match the scaled row height (within rounding tolerance). */
    int last_active = -1;
    for (int i = 0; i < kv->nb_rows; i++) {
        if (kv->rows[i].active) last_active = i;
    }
    expect_true("at least one active row exists", last_active >= 0);
    if (last_active >= 0) {
        N_GUI_WIDGET* new_key = n_gui_get_widget(gui, kv->rows[last_active].key_id);
        expect_close("new row height matches scaled row height",
                     new_key->h, scaled_row_h, 0.5f);
        /* "+" sits immediately below the last active row. */
        expect_true("+ directly below last active row",
                    add_btn->y >= new_key->y + new_key->h - 0.5f);
    }

    /* Public relayout entry-point smoke test. */
    n_gui_kvtable_relayout(kv);
    add_btn = n_gui_get_widget(gui, kv->btn_add);
    expect_true("+ still placed after manual relayout",
                add_btn != NULL && add_btn->y > 0.0f);

    n_gui_kvtable_free(&kv);
    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures > 0) {
        n_log(LOG_ERR, "ex_gui_kvtable: %d check(s) failed", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_kvtable: all checks passed");
    return 0;
}
