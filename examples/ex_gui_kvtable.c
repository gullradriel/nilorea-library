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
 *   - n_gui_kvtable_clear() + repopulate reuses freed row slots instead
 *     of growing nb_rows (no unbounded widget accumulation on reload).
 *   - n_gui_kvtable_set_columns(0,0,0) single-column mode: value,
 *     description and enabled widgets (and headers) are hidden and the
 *     key column widens to fill the row; set_placeholders is applied.
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

    /* Reload-safe clear: repopulating the same row count reuses the freed
       slots instead of growing the widget array toward N_GUI_KV_MAX. */
    n_gui_kvtable_clear(kv);
    expect_true("clear -> 0 active rows", n_gui_kvtable_get_count(kv) == 0);
    n_gui_kvtable_add_row(kv, "a", "1", "", 1);
    n_gui_kvtable_add_row(kv, "b", "2", "", 1);
    n_gui_kvtable_add_row(kv, "c", "3", "", 1);
    int rows_after_first_fill = kv->nb_rows;
    expect_true("3 active rows after fill", n_gui_kvtable_get_count(kv) == 3);
    n_gui_kvtable_clear(kv);
    n_gui_kvtable_add_row(kv, "a", "1", "", 1);
    n_gui_kvtable_add_row(kv, "b", "2", "", 1);
    n_gui_kvtable_add_row(kv, "c", "3", "", 1);
    expect_true("3 active rows after reload", n_gui_kvtable_get_count(kv) == 3);
    expect_true("reload reuses slots (nb_rows does not grow)",
                kv->nb_rows == rows_after_first_fill);

    /* Single-column mode: only the key column and the remove button are shown;
       the value/description/enabled widgets (and their headers) are hidden, and
       the key column widens to fill the row. */
    n_gui_kvtable_set_columns(kv, 0, 0, 0);
    n_gui_kvtable_set_placeholders(kv, "host, .suffix, * or re:regex", NULL);
    n_gui_kvtable_clear(kv);
    n_gui_kvtable_add_row(kv, ".example.com", "", "", 1);
    {
        int active = -1;
        for (int i = 0; i < kv->nb_rows; i++) {
            if (kv->rows[i].active) {
                active = i;
                break;
            }
        }
        expect_true("single-column: one active row", active >= 0);
        if (active >= 0) {
            N_GUI_WIDGET* wk = n_gui_get_widget(gui, kv->rows[active].key_id);
            N_GUI_WIDGET* wv = n_gui_get_widget(gui, kv->rows[active].value_id);
            N_GUI_WIDGET* wd = n_gui_get_widget(gui, kv->rows[active].desc_id);
            N_GUI_WIDGET* we = n_gui_get_widget(gui, kv->rows[active].enabled_id);
            N_GUI_WIDGET* wr = n_gui_get_widget(gui, kv->rows[active].remove_id);
            const N_GUI_WINDOW* wwin = n_gui_get_window(gui, win);
            expect_true("single-column: key visible", wk && wk->visible);
            expect_true("single-column: remove visible", wr && wr->visible);
            expect_true("single-column: value hidden", wv && !wv->visible);
            expect_true("single-column: description hidden", wd && !wd->visible);
            expect_true("single-column: enabled hidden", we && !we->visible);
            expect_true("single-column: value header hidden",
                        !n_gui_get_widget(gui, kv->lbl_value)->visible);
            expect_true("single-column: key column widened",
                        wk && wwin && wk->w > wwin->w * 0.5f);
        }
    }

    /* Top offset reserves space above the header row (for a toolbar/heading in
       a shared window); the header (and rows) shift down by the scaled offset. */
    {
        N_GUI_WIDGET* lk_before = n_gui_get_widget(gui, kv->lbl_key);
        float y_before = lk_before ? lk_before->y : 0.0f;
        n_gui_kvtable_set_top_offset(kv, 50.0f);
        N_GUI_WIDGET* lk_after = n_gui_get_widget(gui, kv->lbl_key);
        expect_true("top_offset pushes the header row down",
                    lk_after && lk_after->y > y_before + 10.0f);
    }

    /* Row heights follow the window only when the window scales its widgets. The
       table above lives in a SCALE-policy window, so its rows scaled with the
       display resize (asserted earlier). A MOVE-policy window instead keeps the
       widgets' pixel layout, so the rows must keep their design height and a taller
       window simply shows more of them, rather than the same rows drawn fatter. */
    {
        int win2 = n_gui_add_window(gui, "MovePanel", 0.0f, 0.0f, 800.0f, 258.0f);
        n_gui_window_set_resize_policy(gui, win2, N_GUI_WIN_RESIZE_MOVE);
        N_GUI_KVTABLE* kv2 = n_gui_kvtable_create(gui, win2, 24.0f, 8.0f, NULL, NULL);
        expect_true("move-policy table created", kv2 != NULL);
        if (kv2) {
            n_gui_kvtable_add_row(kv2, "k", "v", "d", 1);
            N_GUI_WIDGET* r0 = n_gui_get_widget(gui, kv2->rows[0].key_id);
            float h_before = r0 ? r0->h : 0.0f;
            expect_true("move-policy row has a height", h_before > 0.0f);

            /* grow the window vertically only, then relayout */
            N_GUI_WINDOW* w2 = n_gui_get_window(gui, win2);
            if (w2) w2->h *= 2.0f;
            n_gui_kvtable_relayout(kv2);

            r0 = n_gui_get_widget(gui, kv2->rows[0].key_id);
            expect_close("move-policy row keeps its height", r0 ? r0->h : -1.0f, h_before, 0.5f);

            /* a row added after the growth matches the existing ones */
            n_gui_kvtable_add_row(kv2, "k2", "v2", "d2", 1);
            N_GUI_WIDGET* r1 = n_gui_get_widget(gui, kv2->rows[1].key_id);
            expect_close("move-policy new row matches", r1 ? r1->h : -1.0f, h_before, 0.5f);

            /* widths still follow the window: widen it and the key column grows */
            N_GUI_WIDGET* k_before = n_gui_get_widget(gui, kv2->rows[0].key_id);
            float w_before = k_before ? k_before->w : 0.0f;
            if (w2) w2->w *= 2.0f;
            n_gui_kvtable_relayout(kv2);
            N_GUI_WIDGET* k_after = n_gui_get_widget(gui, kv2->rows[0].key_id);
            expect_true("move-policy key column still widens",
                        k_after && k_after->w > w_before + 1.0f);

            n_gui_kvtable_free(&kv2);
        }
    }

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
