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
 *@example ex_gui_progressbar.c
 *@brief N_GUI_PROGRESSBAR value set/get and clamping regression (headless).
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

/* float equality within a small tolerance */
static int feq(float a, float b) {
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < 0.0001f;
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

    int pb = n_gui_add_progressbar(gui, win, 10.0f, 10.0f, 300.0f, 24.0f);
    expect_true("progressbar created", pb >= 0);
    expect_true("initial value is 0", feq(n_gui_progressbar_get_value(gui, pb), 0.0f));

    n_gui_progressbar_set_value(gui, pb, 0.5f);
    expect_true("value set to 0.5", feq(n_gui_progressbar_get_value(gui, pb), 0.5f));

    /* clamping above 1.0 and below 0.0 */
    n_gui_progressbar_set_value(gui, pb, 2.5f);
    expect_true("value clamped to 1.0", feq(n_gui_progressbar_get_value(gui, pb), 1.0f));
    n_gui_progressbar_set_value(gui, pb, -3.0f);
    expect_true("value clamped to 0.0", feq(n_gui_progressbar_get_value(gui, pb), 0.0f));

    /* overlay text setter must not crash, and NULL clears it */
    n_gui_progressbar_set_text(gui, pb, "Scanning 50%");
    n_gui_progressbar_set_text(gui, pb, NULL);

    /* the getter rejects a wrong-type widget */
    {
        int lbl = n_gui_add_label(gui, win, "x", 0.0f, 0.0f, 50.0f, 20.0f, N_GUI_ALIGN_LEFT);
        expect_true("get_value on a label is 0", feq(n_gui_progressbar_get_value(gui, lbl), 0.0f));
    }

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_progressbar: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_progressbar: all checks passed");
    return 0;
}
