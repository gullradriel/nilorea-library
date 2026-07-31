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
 *@example ex_gui_datagrid.c
 *@brief N_GUI_DATAGRID rows, numeric sort, selection and right-click context
 *       callback regression (headless).
 *@author Castagnier Mickael
 *@version 1.0
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
static int failures = 0;

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

static void expect_streq(const char* label, const char* got, const char* want) {
    if (!got || strcmp(got, want) != 0) {
        n_log(LOG_ERR, "%s: got '%s', expected '%s'", label, got ? got : "(null)", want);
        failures++;
    }
}

/* column-layout-changed callback capture (fired on a header-border resize drag) */
static int g_cols_changed = 0;
static void on_cols_changed(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    g_cols_changed++;
}

/* right-click context callback capture */
static int g_ctx_calls = 0;
static int g_ctx_row = -1;
static int g_ctx_x = -1;
static int g_ctx_y = -1;
static void on_ctx(int widget_id, int row, int x, int y, void* user_data) {
    (void)widget_id;
    (void)user_data;
    g_ctx_calls++;
    g_ctx_row = row;
    g_ctx_x = x;
    g_ctx_y = y;
}

int main(int argc, char** argv) {
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

    int win = n_gui_add_window(gui, "Panel", 0.0f, 0.0f, 800.0f, 600.0f);
    expect_true("window created", win >= 0);
    /* frameless: no titlebar offset, so the context-menu hit test below has
       simple window-relative coordinates */
    n_gui_window_set_flags(gui, win, N_GUI_WIN_FRAMELESS);

    int dg = n_gui_add_datagrid(gui, win, 0.0f, 0.0f, 600.0f, 400.0f, NULL, NULL);
    expect_true("datagrid created", dg >= 0);

    expect_true("column 0 added", n_gui_datagrid_add_column(gui, dg, "#", 60.0f) == 0);
    expect_true("column 1 added", n_gui_datagrid_add_column(gui, dg, "Method", 100.0f) == 1);
    expect_true("column 2 added", n_gui_datagrid_add_column(gui, dg, "Status", 80.0f) == 2);

    {
        const char* r0[] = {"3", "GET", "200"};
        const char* r1[] = {"1", "POST", "404"};
        const char* r2[] = {"2", "GET", "500"};
        n_gui_datagrid_add_row(gui, dg, r0);
        n_gui_datagrid_add_row(gui, dg, r1);
        n_gui_datagrid_add_row(gui, dg, r2);
    }
    expect_true("row count is 3", n_gui_datagrid_get_row_count(gui, dg) == 3);
    expect_streq("cell(0,1) before sort", n_gui_datagrid_get_cell(gui, dg, 0, 1), "GET");

    /* columns are locked once rows exist */
    expect_true("late column rejected", n_gui_datagrid_add_column(gui, dg, "Late", 50.0f) == -1);

    /* selection round-trip (before sort, which clears selection) */
    n_gui_datagrid_set_selected(gui, dg, 2);
    expect_true("selected row is 2", n_gui_datagrid_get_selected(gui, dg) == 2);

    /* numeric ascending sort on the id column */
    n_gui_datagrid_sort(gui, dg, 0, 1);
    expect_streq("id asc row0", n_gui_datagrid_get_cell(gui, dg, 0, 0), "1");
    expect_streq("id asc row2", n_gui_datagrid_get_cell(gui, dg, 2, 0), "3");

    /* numeric sort on status: 200 < 404 < 500 (not lexical) */
    n_gui_datagrid_sort(gui, dg, 2, 1);
    expect_streq("status asc row0", n_gui_datagrid_get_cell(gui, dg, 0, 2), "200");
    expect_streq("status asc row2", n_gui_datagrid_get_cell(gui, dg, 2, 2), "500");

    /* descending */
    n_gui_datagrid_sort(gui, dg, 2, -1);
    expect_streq("status desc row0", n_gui_datagrid_get_cell(gui, dg, 0, 2), "500");

    /* right-click context callback: a right press over a data row selects it and
       fires on_context with the cursor position (the basis for GUI context menus).
       The window is frameless, so a data row r sits at y in [(r+1)*row_h, (r+2)*row_h). */
    {
        ALLEGRO_EVENT ev;
        float row_h = (float)al_get_font_line_height(font) + gui->style.item_height_pad;
        int cx = 30;                  /* inside column 0 (width 60) */
        int cy = (int)(row_h * 2.5f); /* header + row 1 center */
        n_gui_datagrid_set_on_context(gui, dg, on_ctx);
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
        ev.mouse.x = cx;
        ev.mouse.y = cy;
        ev.mouse.button = 2;
        n_gui_process_event(gui, ev);
        expect_true("context callback fired once", g_ctx_calls == 1);
        expect_true("context row is 1", g_ctx_row == 1);
        expect_true("context selects row 1", n_gui_datagrid_get_selected(gui, dg) == 1);
        expect_true("context x echoes cursor", g_ctx_x == cx);
        expect_true("context y echoes cursor", g_ctx_y == cy);

        /* a left click selects but must not fire the context callback */
        ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
        ev.mouse.button = 1;
        n_gui_process_event(gui, ev);
        ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
        n_gui_process_event(gui, ev);
        expect_true("left click does not fire context", g_ctx_calls == 1);
    }

    /* multi-row selection: enable it, build a selection programmatically, query it,
       then confirm a plain click collapses to one row and a sort clears it */
    {
        int sel[8];
        size_t k;
        ALLEGRO_EVENT ev;
        float row_h = (float)al_get_font_line_height(font) + gui->style.item_height_pad;
        n_gui_datagrid_set_multiselect(gui, dg, 1);
        n_gui_datagrid_clear_selection(gui, dg);
        expect_true("multiselect: none selected initially", n_gui_datagrid_get_selected_rows(gui, dg, NULL, 0) == 0);
        n_gui_datagrid_select_row(gui, dg, 0, 1);
        n_gui_datagrid_select_row(gui, dg, 2, 1);
        expect_true("multiselect: two rows selected", n_gui_datagrid_get_selected_rows(gui, dg, NULL, 0) == 2);
        expect_true("multiselect: row 0 is selected", n_gui_datagrid_is_row_selected(gui, dg, 0) == 1);
        expect_true("multiselect: row 1 is not selected", n_gui_datagrid_is_row_selected(gui, dg, 1) == 0);
        k = n_gui_datagrid_get_selected_rows(gui, dg, sel, 8);
        expect_true("multiselect: indices are 0 and 2, ascending", k == 2 && sel[0] == 0 && sel[1] == 2);
        n_gui_datagrid_select_row(gui, dg, 0, 0); /* deselect row 0 */
        expect_true("multiselect: one selected after deselect", n_gui_datagrid_get_selected_rows(gui, dg, NULL, 0) == 1);

        /* a plain left click (no Ctrl/Shift) collapses the selection to one row */
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
        ev.mouse.x = 30;
        ev.mouse.y = (int)(row_h * 1.5f); /* header + row 0 center */
        ev.mouse.button = 1;
        n_gui_process_event(gui, ev);
        ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
        n_gui_process_event(gui, ev);
        expect_true("multiselect: plain click selects exactly one", n_gui_datagrid_get_selected_rows(gui, dg, NULL, 0) == 1);
        expect_true("multiselect: plain click selects row 0", n_gui_datagrid_is_row_selected(gui, dg, 0) == 1);

        /* a sort changes row indices, so it clears the selection */
        n_gui_datagrid_select_row(gui, dg, 1, 1);
        n_gui_datagrid_sort(gui, dg, 0, -1);
        expect_true("multiselect: sort clears the selection", n_gui_datagrid_get_selected_rows(gui, dg, NULL, 0) == 0);

        /* with multi-select off, the query reports the single selected row */
        n_gui_datagrid_set_multiselect(gui, dg, 0);
        n_gui_datagrid_set_selected(gui, dg, 1);
        expect_true("single-select: get_selected_rows reports one", n_gui_datagrid_get_selected_rows(gui, dg, NULL, 0) == 1);
    }

    /* ---- per-row background tints: set, query, follow a sort, and clear ---- */
    {
        ALLEGRO_COLOR got;
        const char* r0[] = {"3", "GET", "200"};
        const char* r1[] = {"1", "POST", "404"};
        const char* r2[] = {"2", "GET", "500"};
        n_gui_datagrid_clear_rows(gui, dg);
        n_gui_datagrid_add_row(gui, dg, r0);
        n_gui_datagrid_add_row(gui, dg, r1);
        n_gui_datagrid_add_row(gui, dg, r2);

        expect_true("no tint by default", n_gui_datagrid_get_row_color(gui, dg, 0, NULL) == 0);
        n_gui_datagrid_set_row_color(gui, dg, 0, al_map_rgb(255, 0, 0)); /* id 3 -> red */
        n_gui_datagrid_set_row_color(gui, dg, 2, al_map_rgb(0, 0, 255)); /* id 2 -> blue */
        expect_true("row 0 tinted", n_gui_datagrid_get_row_color(gui, dg, 0, &got) == 1 && got.r > 0.9f && got.b < 0.1f);
        expect_true("row 1 untinted", n_gui_datagrid_get_row_color(gui, dg, 1, NULL) == 0);
        expect_true("row 2 tinted", n_gui_datagrid_get_row_color(gui, dg, 2, &got) == 1 && got.b > 0.9f && got.r < 0.1f);
        expect_true("out-of-range row has no tint", n_gui_datagrid_get_row_color(gui, dg, 9, NULL) == 0);

        /* a sort reorders the cells physically, so the tints must travel with
           their rows: after ascending on the id column the order is 1, 2, 3, so
           the blue row (id 2) lands at index 1 and the red row (id 3) at index 2 */
        n_gui_datagrid_sort(gui, dg, 0, 1);
        expect_streq("tint sort: row0 is id 1", n_gui_datagrid_get_cell(gui, dg, 0, 0), "1");
        expect_true("tint sort: row 0 (id 1) still untinted", n_gui_datagrid_get_row_color(gui, dg, 0, NULL) == 0);
        expect_true("tint sort: row 1 (id 2) keeps blue", n_gui_datagrid_get_row_color(gui, dg, 1, &got) == 1 && got.b > 0.9f);
        expect_true("tint sort: row 2 (id 3) keeps red", n_gui_datagrid_get_row_color(gui, dg, 2, &got) == 1 && got.r > 0.9f);

        /* a sort shrinks the cell buffer to nb_rows; the tint arrays shrink with
           it, so adding rows afterwards must still grow them safely */
        {
            const char* r3[] = {"9", "PUT", "301"};
            n_gui_datagrid_add_row(gui, dg, r3);
            n_gui_datagrid_set_row_color(gui, dg, 3, al_map_rgb(0, 255, 0));
            expect_true("tint after sort+add", n_gui_datagrid_get_row_color(gui, dg, 3, &got) == 1 && got.g > 0.9f);
            expect_true("tint after sort+add: row 2 kept red", n_gui_datagrid_get_row_color(gui, dg, 2, &got) == 1 && got.r > 0.9f);
        }

        n_gui_datagrid_clear_row_color(gui, dg, 2);
        expect_true("single tint cleared", n_gui_datagrid_get_row_color(gui, dg, 2, NULL) == 0);
        expect_true("other tint kept", n_gui_datagrid_get_row_color(gui, dg, 1, NULL) == 1);
        n_gui_datagrid_clear_row_colors(gui, dg);
        expect_true("all tints cleared", n_gui_datagrid_get_row_color(gui, dg, 1, NULL) == 0);

        /* clearing the rows drops their tints: rebuilt rows start untinted */
        n_gui_datagrid_set_row_color(gui, dg, 0, al_map_rgb(255, 255, 0));
        n_gui_datagrid_clear_rows(gui, dg);
        n_gui_datagrid_add_row(gui, dg, r0);
        expect_true("clear_rows drops tints", n_gui_datagrid_get_row_color(gui, dg, 0, NULL) == 0);
    }

    /* ---- scroll offset: set, read back, clamp, and survive a rebuild ---- */
    {
        int i;
        n_gui_datagrid_clear_rows(gui, dg);
        expect_true("scroll of empty grid is 0", n_gui_datagrid_get_scroll_offset(gui, dg) == 0);
        for (i = 0; i < 40; i++) {
            char a[16], b[16], c[16];
            const char* r[3];
            snprintf(a, sizeof(a), "%d", i);
            snprintf(b, sizeof(b), "M%d", i);
            snprintf(c, sizeof(c), "%d", 200 + i);
            r[0] = a;
            r[1] = b;
            r[2] = c;
            n_gui_datagrid_add_row(gui, dg, r);
        }
        n_gui_datagrid_set_scroll_offset(gui, dg, 12);
        expect_true("scroll offset set to 12", n_gui_datagrid_get_scroll_offset(gui, dg) == 12);
        /* the host preserves scroll across an in-place edit: save, clear+refill, restore */
        {
            int saved = n_gui_datagrid_get_scroll_offset(gui, dg);
            n_gui_datagrid_clear_rows(gui, dg);
            for (i = 0; i < 40; i++) {
                char a[16];
                const char* r[3];
                snprintf(a, sizeof(a), "%d", i);
                r[0] = a;
                r[1] = "M";
                r[2] = "200";
                n_gui_datagrid_add_row(gui, dg, r);
            }
            n_gui_datagrid_set_scroll_offset(gui, dg, saved);
            expect_true("scroll offset survives a clear/refill", n_gui_datagrid_get_scroll_offset(gui, dg) == 12);
        }
        /* an over-range offset clamps into the row range */
        n_gui_datagrid_set_scroll_offset(gui, dg, 9999);
        expect_true("scroll offset clamps to last row", n_gui_datagrid_get_scroll_offset(gui, dg) == 39);
        n_gui_datagrid_set_scroll_offset(gui, dg, -5);
        expect_true("scroll offset clamps to 0", n_gui_datagrid_get_scroll_offset(gui, dg) == 0);
    }

    /* ---- horizontal scroll: set, read back, clamp >= 0, survive a row refill ---- */
    {
        /* widen a column so the total content can exceed a typical pane (h-scroll is
           meaningful); the getter/setter are the persistence hook the host uses to keep
           the horizontal position across an in-place edit, like the vertical one above */
        n_gui_datagrid_set_column_width(gui, dg, 2, 400.0f);
        n_gui_datagrid_set_h_scroll(gui, dg, 80.0f);
        expect_true("h_scroll set to 80", n_gui_datagrid_get_h_scroll(gui, dg) == 80.0f);
        /* the horizontal position is column-based, so a row clear/refill keeps it */
        {
            const char* r[3] = {"1", "M", "200"};
            n_gui_datagrid_clear_rows(gui, dg);
            n_gui_datagrid_add_row(gui, dg, r);
            expect_true("h_scroll survives a row refill", n_gui_datagrid_get_h_scroll(gui, dg) == 80.0f);
        }
        /* a negative offset clamps to 0 (the draw path re-clamps the upper bound live) */
        n_gui_datagrid_set_h_scroll(gui, dg, -25.0f);
        expect_true("h_scroll clamps to 0", n_gui_datagrid_get_h_scroll(gui, dg) == 0.0f);
        n_gui_datagrid_set_h_scroll(gui, dg, 0.0f);
    }

    n_gui_datagrid_clear_rows(gui, dg);
    expect_true("cleared row count is 0", n_gui_datagrid_get_row_count(gui, dg) == 0);

    /* a sort reallocates the cell buffer to exactly nb_rows; adding more rows
       afterwards must still grow safely (regression: rows_cap was left stale, so
       the next add_row wrote past the shrunken buffer -> heap overflow). Stress
       the add -> sort -> add cycle so ASan catches any out-of-bounds write. */
    {
        int i;
        for (i = 0; i < 25; i++) {
            char a[16], b[16], c[16];
            const char* r[3];
            snprintf(a, sizeof(a), "%d", 25 - i);
            snprintf(b, sizeof(b), "GET%d", i);
            snprintf(c, sizeof(c), "%d", 200 + (i % 5));
            r[0] = a;
            r[1] = b;
            r[2] = c;
            n_gui_datagrid_add_row(gui, dg, r);
        }
        n_gui_datagrid_sort(gui, dg, 0, 1); /* shrinks the buffer to nb_rows */
        for (i = 0; i < 25; i++) {          /* push back past the shrunken cap */
            char a[16];
            const char* r[3];
            snprintf(a, sizeof(a), "%d", 100 + i);
            r[0] = a;
            r[1] = "POST";
            r[2] = "201";
            n_gui_datagrid_add_row(gui, dg, r);
        }
        expect_true("rows after sort+add", n_gui_datagrid_get_row_count(gui, dg) == 50);
        n_gui_datagrid_clear_rows(gui, dg);
    }

    /* ---- column customization: count/title, resize-by-drag, width, visibility,
       and reorder (the display-order indirection over the physical cells) ---- */
    expect_true("column count is 3", n_gui_datagrid_get_column_count(gui, dg) == 3);
    expect_streq("column 1 title", n_gui_datagrid_get_column_title(gui, dg, 1), "Method");
    expect_streq("out-of-range title", n_gui_datagrid_get_column_title(gui, dg, 9), "");

    /* dragging column 0's right border (at x=60) widens it and fires the callback */
    n_gui_datagrid_set_on_columns_changed(gui, dg, on_cols_changed);
    {
        ALLEGRO_EVENT ev;
        float row_h = (float)al_get_font_line_height(font) + gui->style.item_height_pad;
        float w0 = n_gui_datagrid_get_column_width(gui, dg, 0);
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
        ev.mouse.button = 1;
        ev.mouse.x = 60;
        ev.mouse.y = (int)(row_h * 0.5f);
        n_gui_process_event(gui, ev);
        ev.type = ALLEGRO_EVENT_MOUSE_AXES;
        ev.mouse.x = 100;
        n_gui_process_event(gui, ev);
        ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
        n_gui_process_event(gui, ev);
        expect_true("resize widened column 0", n_gui_datagrid_get_column_width(gui, dg, 0) > w0 + 20.0f);
        expect_true("on_columns_changed fired once", g_cols_changed == 1);
    }

    /* width setter and its minimum clamp */
    n_gui_datagrid_set_column_width(gui, dg, 2, 150.0f);
    expect_true("column width set", n_gui_datagrid_get_column_width(gui, dg, 2) == 150.0f);
    n_gui_datagrid_set_column_width(gui, dg, 2, 3.0f);
    expect_true("column width min clamp", n_gui_datagrid_get_column_width(gui, dg, 2) == 16.0f);

    /* visibility toggle */
    expect_true("col 1 visible by default", n_gui_datagrid_get_column_visible(gui, dg, 1) == 1);
    n_gui_datagrid_set_column_visible(gui, dg, 1, 0);
    expect_true("col 1 now hidden", n_gui_datagrid_get_column_visible(gui, dg, 1) == 0);

    /* reorder: identity, then move display position 0 to 2 -> order {1,2,0} */
    expect_true("display 0 -> phys 0", n_gui_datagrid_display_to_physical(gui, dg, 0) == 0);
    n_gui_datagrid_move_column(gui, dg, 0, 2);
    expect_true("after move display 2 -> phys 0", n_gui_datagrid_display_to_physical(gui, dg, 2) == 0);
    expect_true("after move display 0 -> phys 1", n_gui_datagrid_display_to_physical(gui, dg, 0) == 1);
    expect_true("after move display 1 -> phys 2", n_gui_datagrid_display_to_physical(gui, dg, 1) == 2);
    expect_true("out-of-range display maps to -1", n_gui_datagrid_display_to_physical(gui, dg, 9) == -1);

    /* guards: bad ids and out-of-range moves are no-ops */
    n_gui_datagrid_move_column(gui, dg, 0, 9);
    expect_true("count for bad id is 0", n_gui_datagrid_get_column_count(gui, -1) == 0);

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_datagrid: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_datagrid: all checks passed");
    return 0;
}
