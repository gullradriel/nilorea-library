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
 *@example ex_gui_reentrant.c
 *@brief n_gui_process_event re-entrancy regression test (headless).
 *
 * A button on_click that opens/raises another window reorders ctx->windows
 * (n_gui_raise_window removes + re-pushes + re-sorts the window list). The
 * mouse-release handler in n_gui_process_event iterates ctx->windows while
 * firing that callback; firing it inline freed the LIST_NODE the iterator had
 * already cached as "next", a use-after-free that crashed on the following
 * step. This test reproduces the exact click (press then release over the
 * button) headlessly and asserts the callback fired and the process survives,
 * which under ASan fails loudly if the deferred-dispatch fix regresses.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 28/06/2026
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

/* the button callback raises the popup window, reordering ctx->windows */
static N_GUI_CTX* g_gui = NULL;
static int g_popup_win = -1;
static int g_click_count = 0;

static void on_open_popup(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    g_click_count++;
    if (g_gui && g_popup_win >= 0) {
        n_gui_open_window(g_gui, g_popup_win);
        n_gui_raise_window(g_gui, g_popup_win); /* removes + re-pushes + re-sorts */
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

/* Build a left-button mouse event at (x, y). */
static ALLEGRO_EVENT mouse_event(int type, int x, int y) {
    ALLEGRO_EVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = (unsigned int)type;
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.button = 1;
    return ev;
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
    g_gui = gui;
    n_gui_set_display_size(gui, 800.0f, 600.0f);

    /* Window A (frameless so the button's screen coords are trivial) holds the
       button; the popup P sits clear of the button and after A in z-order, so
       its node is the iterator's cached "next" when the click fires. */
    int win_a = n_gui_add_window(gui, "A", 0.0f, 0.0f, 200.0f, 150.0f);
    n_gui_window_set_flags(gui, win_a, N_GUI_WIN_FRAMELESS);
    int btn = n_gui_add_button(gui, win_a, "Open", 10.0f, 10.0f, 80.0f, 30.0f, N_GUI_SHAPE_ROUNDED, on_open_popup, NULL);
    n_gui_open_window(gui, win_a);

    g_popup_win = n_gui_add_window(gui, "P", 300.0f, 0.0f, 200.0f, 150.0f);
    n_gui_window_set_zorder(gui, g_popup_win, N_GUI_ZORDER_POPUP, 0);
    n_gui_open_window(gui, g_popup_win);

    expect_true("windows + button created", win_a >= 0 && g_popup_win >= 0 && btn >= 0);

    /* Press then release over the button centre (50, 25). The release fires the
       callback that raises P; before the fix this freed the cached "next" node
       and the loop dereferenced it. */
    {
        ALLEGRO_EVENT down = mouse_event(ALLEGRO_EVENT_MOUSE_BUTTON_DOWN, 50, 25);
        ALLEGRO_EVENT up = mouse_event(ALLEGRO_EVENT_MOUSE_BUTTON_UP, 50, 25);
        n_gui_process_event(gui, down);
        n_gui_process_event(gui, up); /* must not crash */
    }

    expect_true("button callback fired exactly once", g_click_count == 1);
    expect_true("popup is open after the click", n_gui_window_is_open(gui, g_popup_win));

    /* A second identical click must also be safe and fire again. */
    {
        ALLEGRO_EVENT down = mouse_event(ALLEGRO_EVENT_MOUSE_BUTTON_DOWN, 50, 25);
        ALLEGRO_EVENT up = mouse_event(ALLEGRO_EVENT_MOUSE_BUTTON_UP, 50, 25);
        n_gui_process_event(gui, down);
        n_gui_process_event(gui, up);
    }
    expect_true("button callback fired again", g_click_count == 2);

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures > 0) {
        n_log(LOG_ERR, "ex_gui_reentrant: %d check(s) failed", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_reentrant: all checks passed");
    return 0;
}
