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
 *@example ex_gui_glyph.c
 *@brief Button glyph regression test (headless): n_gui_button_set_glyph draws a
 *       primitive icon instead of the label on the small square buttons where a
 *       label does not fit, keeps the label for a later switch back, and ignores
 *       out-of-range values and non-button widgets.
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

static int failures = 0;

static void expect_eq(const char* label, int got, int want) {
    if (got != want) {
        n_log(LOG_ERR, "%s: got %d, expected %d", label, got, want);
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

    if (!al_init() || !al_init_font_addon() || !al_init_primitives_addon()) {
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
    n_gui_set_display_size(gui, 640.0f, 480.0f);

    int win = n_gui_add_window(gui, "Toolbar", 0.0f, 0.0f, 200.0f, 60.0f);
    expect_true("window created", win >= 0);

    /* a typical row of square action buttons: no room for a text label */
    int up = n_gui_add_button(gui, win, "Move up", 4.0f, 4.0f, 22.0f, 22.0f,
                              N_GUI_SHAPE_RECT, NULL, NULL);
    int down = n_gui_add_button(gui, win, "Move down", 30.0f, 4.0f, 22.0f, 22.0f,
                                N_GUI_SHAPE_RECT, NULL, NULL);
    int dup = n_gui_add_button(gui, win, "Duplicate", 56.0f, 4.0f, 22.0f, 22.0f,
                               N_GUI_SHAPE_RECT, NULL, NULL);
    expect_true("buttons created", up >= 0 && down >= 0 && dup >= 0);

    /* buttons draw their label until a glyph is set */
    expect_eq("default glyph", n_gui_button_get_glyph(gui, up), N_GUI_GLYPH_NONE);

    n_gui_button_set_glyph(gui, up, N_GUI_GLYPH_ARROW_UP);
    n_gui_button_set_glyph(gui, down, N_GUI_GLYPH_ARROW_DOWN);
    n_gui_button_set_glyph(gui, dup, N_GUI_GLYPH_COPY);
    expect_eq("up glyph", n_gui_button_get_glyph(gui, up), N_GUI_GLYPH_ARROW_UP);
    expect_eq("down glyph", n_gui_button_get_glyph(gui, down), N_GUI_GLYPH_ARROW_DOWN);
    expect_eq("copy glyph", n_gui_button_get_glyph(gui, dup), N_GUI_GLYPH_COPY);

    /* every defined glyph round-trips */
    for (int g = N_GUI_GLYPH_NONE; g < N_GUI_GLYPH_COUNT; g++) {
        n_gui_button_set_glyph(gui, up, g);
        expect_eq("glyph round-trip", n_gui_button_get_glyph(gui, up), g);
    }

    /* out-of-range values leave the glyph alone */
    n_gui_button_set_glyph(gui, up, N_GUI_GLYPH_CROSS);
    n_gui_button_set_glyph(gui, up, N_GUI_GLYPH_COUNT);
    n_gui_button_set_glyph(gui, up, -3);
    expect_eq("out-of-range ignored", n_gui_button_get_glyph(gui, up), N_GUI_GLYPH_CROSS);

    /* the label survives, so a caller can switch back to it */
    n_gui_button_set_glyph(gui, up, N_GUI_GLYPH_NONE);
    expect_eq("back to the label", n_gui_button_get_glyph(gui, up), N_GUI_GLYPH_NONE);

    /* non-button widgets and unknown ids are no-ops */
    {
        int lbl = n_gui_add_label(gui, win, "text", 4.0f, 30.0f, 100.0f, 18.0f, N_GUI_ALIGN_LEFT);
        n_gui_button_set_glyph(gui, lbl, N_GUI_GLYPH_PLUS);
        expect_eq("label unaffected", n_gui_button_get_glyph(gui, lbl), N_GUI_GLYPH_NONE);
        n_gui_button_set_glyph(gui, 9999, N_GUI_GLYPH_PLUS);
        expect_eq("unknown widget", n_gui_button_get_glyph(gui, 9999), N_GUI_GLYPH_NONE);
    }

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_glyph: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_glyph: all checks passed");
    return 0;
}
