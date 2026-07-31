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
 *@example ex_gui_splitpane.c
 *@brief N_GUI_SPLITPANE ratio and clamping regression test (headless).
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

static void expect_close(const char* label, float got, float want, float tol) {
    float d = got - want;
    if (d < 0) d = -d;
    if (d > tol) {
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

    int sp = n_gui_add_splitpane(gui, win, 0.0f, 0.0f, 800.0f, 600.0f, N_GUI_SPLIT_VERTICAL, 0.5f, NULL, NULL);
    expect_true("splitpane created", sp >= 0);
    expect_close("initial ratio", n_gui_splitpane_get_ratio(gui, sp), 0.5f, 0.001f);

    n_gui_splitpane_set_ratio(gui, sp, 0.8f);
    expect_close("set ratio 0.8", n_gui_splitpane_get_ratio(gui, sp), 0.8f, 0.001f);

    /* default clamp is [0.05, 0.95] */
    n_gui_splitpane_set_ratio(gui, sp, 2.0f);
    expect_close("clamp high to 0.95", n_gui_splitpane_get_ratio(gui, sp), 0.95f, 0.001f);
    n_gui_splitpane_set_ratio(gui, sp, -1.0f);
    expect_close("clamp low to 0.05", n_gui_splitpane_get_ratio(gui, sp), 0.05f, 0.001f);

    /* tighten the limits and confirm the stored ratio is pulled into range */
    n_gui_splitpane_set_limits(gui, sp, 0.2f, 0.7f);
    expect_close("ratio pulled to new min", n_gui_splitpane_get_ratio(gui, sp), 0.2f, 0.001f);
    n_gui_splitpane_set_ratio(gui, sp, 0.9f);
    expect_close("clamp to new max 0.7", n_gui_splitpane_get_ratio(gui, sp), 0.7f, 0.001f);

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_splitpane: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_splitpane: all checks passed");
    return 0;
}
