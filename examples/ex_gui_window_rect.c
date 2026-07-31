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
 *@example ex_gui_window_rect.c
 *@brief Runtime window relayout regression test (headless): n_gui_window_set_rect
 *       moves/resizes a window and rescales its widgets from their normalized
 *       coordinates, and the window's norms follow so a later adaptive resize
 *       keeps the new proportions.
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

static void expect_close(const char* label, float got, float want) {
    if (fabsf(got - want) > 0.01f) {
        n_log(LOG_ERR, "%s: got %.3f, expected %.3f", label, got, want);
        failures++;
    }
}

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    }
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
    n_gui_set_display_size(gui, 800.0f, 600.0f);

    int win = n_gui_add_window(gui, "Panel", 100.0f, 50.0f, 400.0f, 300.0f);
    expect_true("window created", win >= 0);
    /* widget norms are captured relative to the window when it is added:
       (0.10, 0.10, 0.25, 0.10) of a 400x300 window */
    int btn = n_gui_add_button(gui, win, "B", 40.0f, 30.0f, 100.0f, 30.0f,
                               N_GUI_SHAPE_RECT, NULL, NULL);
    expect_true("button created", btn >= 0);

    /* grow the window to the full display: the widget keeps its proportions */
    n_gui_window_set_rect(gui, win, 0.0f, 0.0f, 800.0f, 600.0f);
    N_GUI_WINDOW* w = n_gui_get_window(gui, win);
    N_GUI_WIDGET* b = n_gui_get_widget(gui, btn);
    expect_true("window and widget still resolve", w != NULL && b != NULL);
    if (w && b) {
        expect_close("window x", w->x, 0.0f);
        expect_close("window y", w->y, 0.0f);
        expect_close("window w", w->w, 800.0f);
        expect_close("window h", w->h, 600.0f);
        expect_close("widget x scaled", b->x, 80.0f);
        expect_close("widget y scaled", b->y, 60.0f);
        expect_close("widget w scaled", b->w, 200.0f);
        expect_close("widget h scaled", b->h, 60.0f);
        /* window norms recaptured against the current display size */
        expect_close("window norm_w", w->norm_w, 1.0f);
        expect_close("window norm_h", w->norm_h, 1.0f);
    }

    /* shrink to a quarter width: proportions hold, and the reflow is a pure
       function of the rectangle (no drift from the previous call) */
    n_gui_window_set_rect(gui, win, 200.0f, 100.0f, 200.0f, 150.0f);
    w = n_gui_get_window(gui, win);
    b = n_gui_get_widget(gui, btn);
    if (w && b) {
        expect_close("shrunk window x", w->x, 200.0f);
        expect_close("shrunk widget x", b->x, 20.0f);
        expect_close("shrunk widget y", b->y, 15.0f);
        expect_close("shrunk widget w", b->w, 50.0f);
        expect_close("shrunk widget h", b->h, 15.0f);
        expect_close("shrunk window norm_x", w->norm_x, 0.25f);
    }

    /* a host that computes its own layout places widgets with
       n_gui_widget_set_rect, which also refreshes the norms so the widget keeps
       scaling with its window afterwards */
    {
        n_gui_widget_set_rect(gui, btn, 50.0f, 30.0f, 100.0f, 60.0f);
        b = n_gui_get_widget(gui, btn);
        w = n_gui_get_window(gui, win); /* 200x150 from the shrink above */
        expect_true("widget and window resolve", b != NULL && w != NULL);
        if (b) {
            expect_close("placed x", b->x, 50.0f);
            expect_close("placed w", b->w, 100.0f);
            expect_close("norms refreshed", b->norm_w, 0.5f);
        }
        /* the new position is what a later window resize scales from */
        n_gui_window_set_rect(gui, win, 0.0f, 0.0f, 400.0f, 300.0f);
        b = n_gui_get_widget(gui, btn);
        if (b) {
            expect_close("scaled from the placed x", b->x, 100.0f);
            expect_close("scaled from the placed w", b->w, 200.0f);
        }
        /* negative sizes clamp to zero rather than inverting the rectangle */
        n_gui_widget_set_rect(gui, btn, 10.0f, 10.0f, -5.0f, -5.0f);
        b = n_gui_get_widget(gui, btn);
        if (b) {
            expect_close("negative width clamped", b->w, 0.0f);
            expect_close("negative height clamped", b->h, 0.0f);
        }
        n_gui_widget_set_rect(gui, btn, 40.0f, 30.0f, 100.0f, 30.0f);
        n_gui_widget_set_rect(gui, 9999, 0.0f, 0.0f, 10.0f, 10.0f); /* unknown id: no-op */
    }

    /* a tab panel lays its buttons out when the tabs are added, so a host that
       moves the row afterwards needs n_gui_tab_relayout */
    {
        N_GUI_TAB_PANEL* tabs = n_gui_tab_create(gui, win, 4.0f, 4.0f, 80.0f, 26.0f, NULL, NULL);
        expect_true("tab panel created", tabs != NULL);
        if (tabs) {
            expect_true("tabs added", n_gui_tab_add(tabs, "One") == 0 && n_gui_tab_add(tabs, "Two") == 1);

            n_gui_tab_relayout(tabs, 10.0f, 40.0f, 60.0f, 20.0f);
            expect_close("panel origin x", tabs->x, 10.0f);
            expect_close("panel button w", tabs->button_w, 60.0f);
            {
                N_GUI_WIDGET* t0 = n_gui_get_widget(gui, tabs->button_ids[0]);
                N_GUI_WIDGET* t1 = n_gui_get_widget(gui, tabs->button_ids[1]);
                expect_true("tab buttons resolve", t0 != NULL && t1 != NULL);
                if (t0 && t1) {
                    expect_close("first tab x", t0->x, 10.0f);
                    expect_close("first tab y", t0->y, 40.0f);
                    expect_close("first tab w", t0->w, 60.0f);
                    expect_close("first tab h", t0->h, 20.0f);
                    expect_close("second tab follows the first", t1->x, 70.0f);
                }
            }

            /* <= 0 keeps the current size, and the refreshed norms make the
               buttons scale with the window like any other widget */
            n_gui_tab_relayout(tabs, 10.0f, 40.0f, -1.0f, 0.0f);
            expect_close("button w kept", tabs->button_w, 60.0f);
            expect_close("button h kept", tabs->button_h, 20.0f);
            n_gui_tab_free(&tabs);
            expect_true("tab panel freed", tabs == NULL);
        }
    }

    /* per-axis reflow: a page of fixed-height rows keeps its vertical layout
       while its width still follows the window */
    {
        int page = n_gui_add_window(gui, "Page", 0.0f, 0.0f, 400.0f, 200.0f);
        int r0 = n_gui_add_button(gui, page, "r0", 10.0f, 20.0f, 200.0f, 26.0f,
                                  N_GUI_SHAPE_RECT, NULL, NULL);
        int r1 = n_gui_add_button(gui, page, "r1", 10.0f, 60.0f, 200.0f, 26.0f,
                                  N_GUI_SHAPE_RECT, NULL, NULL);
        expect_true("page widgets created", page >= 0 && r0 >= 0 && r1 >= 0);

        n_gui_window_set_rect_axes(gui, page, 0.0f, 0.0f, 800.0f, 100.0f, 1, 0);
        {
            N_GUI_WIDGET* a = n_gui_get_widget(gui, r0);
            N_GUI_WIDGET* c = n_gui_get_widget(gui, r1);
            expect_true("page rows resolve", a != NULL && c != NULL);
            if (a && c) {
                expect_close("row width follows the window", a->w, 400.0f);
                expect_close("row height is kept", a->h, 26.0f);
                expect_close("row y is kept", c->y, 60.0f);
            }
        }
        /* the kept axis's norms describe the new window, so a later resize on
           that axis scales from the layout the host kept, not from a stale one */
        n_gui_window_set_rect_axes(gui, page, 0.0f, 0.0f, 800.0f, 200.0f, 1, 1);
        {
            N_GUI_WIDGET* c = n_gui_get_widget(gui, r1);
            if (c) expect_close("kept axis scales from its refreshed norm", c->y, 120.0f);
        }
    }

    /* degenerate sizes are clamped rather than producing a zero-area window */
    n_gui_window_set_rect(gui, win, 0.0f, 0.0f, 0.0f, -10.0f);
    w = n_gui_get_window(gui, win);
    if (w) {
        expect_true("width clamped", w->w >= 1.0f);
        expect_true("height clamped", w->h >= 1.0f);
    }

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_window_rect: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_window_rect: all checks passed");
    return 0;
}
