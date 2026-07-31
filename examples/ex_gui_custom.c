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
 *@example ex_gui_custom.c
 *@brief N_GUI_TYPE_CUSTOM owner-draw widget: the paint callback fires during
 *       n_gui_draw with the widget's absolute rectangle (headless, memory bitmap).
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

/* the owner-draw callback: record that it fired, where, and with what size, then
   paint a filled rectangle plus a label inside the widget's rectangle. */
typedef struct {
    int calls;    /* how many times the callback ran */
    float last_x; /* absolute x it was handed */
    float last_y; /* absolute y it was handed */
    float seen_w; /* the widget width it observed */
    int had_font; /* was a usable font supplied */
} DRAW_STATE;

static void custom_draw(N_GUI_WIDGET* widget, float x, float y, ALLEGRO_FONT* font, void* user_data) {
    DRAW_STATE* st = (DRAW_STATE*)user_data;
    if (st) {
        st->calls++;
        st->last_x = x;
        st->last_y = y;
        st->seen_w = widget ? widget->w : 0.0f;
        st->had_font = (font != NULL);
    }
    if (widget) {
        al_draw_filled_rectangle(x, y, x + widget->w, y + widget->h, al_map_rgb(30, 120, 200));
        if (font)
            al_draw_text(font, al_map_rgb(255, 255, 255), x + 6.0f, y + 6.0f, 0, "custom");
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    if (process_args(argc, argv) != 0) {
        fprintf(stderr, "Usage: %s [-V LOG_LEVEL]\n", argv[0]);
        return 2;
    }
    set_log_level(log_level);

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
    n_gui_set_display_size(gui, 800.0f, 600.0f);

    int win = n_gui_add_window(gui, "Panel", 0.0f, 0.0f, 800.0f, 600.0f);
    expect_true("window created", win >= 0);

    DRAW_STATE st = {0, 0.0f, 0.0f, 0.0f, 0};
    int cw = n_gui_add_custom(gui, win, 40.0f, 50.0f, 260.0f, 120.0f, custom_draw, &st);
    expect_true("custom widget created", cw >= 0);

    N_GUI_WIDGET* w = n_gui_get_widget(gui, cw);
    expect_true("widget retrievable", w != NULL);
    expect_true("widget is CUSTOM type", w && w->type == N_GUI_TYPE_CUSTOM);
    expect_true("widget keeps its size", w && w->w == 260.0f && w->h == 120.0f);

    /* a NULL draw callback is allowed (reserves space, paints nothing) */
    int cw2 = n_gui_add_custom(gui, win, 0.0f, 0.0f, 10.0f, 10.0f, NULL, NULL);
    expect_true("NULL-draw custom widget created", cw2 >= 0);

    /* render one frame into a memory bitmap (no display needed) and confirm the
       paint callback fired with the widget's absolute position and size */
    al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
    ALLEGRO_BITMAP* target = al_create_bitmap(800, 600);
    expect_true("memory target created", target != NULL);
    if (target) {
        al_set_target_bitmap(target);
        al_clear_to_color(al_map_rgb(0, 0, 0));
        n_gui_draw(gui);
        al_set_target_bitmap(NULL);
        al_destroy_bitmap(target);
    }
    expect_true("draw callback fired", st.calls > 0);
    expect_true("callback saw the widget width", st.seen_w == 260.0f);
    expect_true("callback got a usable font", st.had_font == 1);
    /* the widget sits at window (0,0) + (40,50), so its absolute x/y are >= that */
    expect_true("callback got an absolute position", st.last_x >= 40.0f && st.last_y >= 50.0f);

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_custom: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_custom: all checks passed");
    return 0;
}
