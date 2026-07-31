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
 *@example ex_gui_placeholder.c
 *@brief N_GUI textarea placeholder/hint set, replace, and clear (headless).
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

/* read back the placeholder pointer stored on a textarea widget */
static const char* placeholder_of(N_GUI_CTX* gui, int id) {
    N_GUI_WIDGET* w = n_gui_get_widget(gui, id);
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data)
        return NULL;
    return ((N_GUI_TEXTAREA_DATA*)w->data)->placeholder;
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

    int ta = n_gui_add_textarea(gui, win, 10.0f, 10.0f, 300.0f, 24.0f, 0, 0, NULL, NULL);
    expect_true("textarea created", ta >= 0);

    /* a fresh textarea has no placeholder */
    expect_true("no placeholder initially", placeholder_of(gui, ta) == NULL);

    /* setting a placeholder stores a copy */
    n_gui_textarea_set_placeholder(gui, ta, "https://host/path?id=FUZZ");
    {
        const char* p = placeholder_of(gui, ta);
        expect_true("placeholder stored", p && strcmp(p, "https://host/path?id=FUZZ") == 0);
    }

    /* setting a second placeholder replaces the first (no leak, value updated) */
    n_gui_textarea_set_placeholder(gui, ta, "Enter a target URL");
    {
        const char* p = placeholder_of(gui, ta);
        expect_true("placeholder replaced", p && strcmp(p, "Enter a target URL") == 0);
    }

    /* the placeholder is independent of the field's text content */
    n_gui_textarea_set_text(gui, ta, "typed text");
    expect_true("placeholder survives set_text", placeholder_of(gui, ta) != NULL);

    /* an empty string clears the placeholder */
    n_gui_textarea_set_placeholder(gui, ta, "");
    expect_true("empty string clears", placeholder_of(gui, ta) == NULL);

    /* NULL clears the placeholder too */
    n_gui_textarea_set_placeholder(gui, ta, "again");
    expect_true("placeholder set again", placeholder_of(gui, ta) != NULL);
    n_gui_textarea_set_placeholder(gui, ta, NULL);
    expect_true("NULL clears", placeholder_of(gui, ta) == NULL);

    /* setting on a wrong-type widget warns and does not crash */
    {
        int lbl = n_gui_add_label(gui, win, "x", 0.0f, 0.0f, 50.0f, 20.0f, N_GUI_ALIGN_LEFT);
        n_gui_textarea_set_placeholder(gui, lbl, "ignored");
        expect_true("label is unaffected", n_gui_get_widget(gui, lbl) != NULL);
    }

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_placeholder: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_placeholder: all checks passed");
    return 0;
}
