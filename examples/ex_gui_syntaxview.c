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
 *@example ex_gui_syntaxview.c
 *@brief N_GUI_SYNTAXVIEW line-count, mode, and text-selection/copy regression test (headless).
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

    int sv = n_gui_add_syntaxview(gui, win, 0.0f, 0.0f, 600.0f, 400.0f, N_GUI_SYNTAX_HTTP);
    expect_true("syntaxview created", sv >= 0);
    expect_true("empty line count is zero", n_gui_syntaxview_get_line_count(gui, sv) == 0);

    const char* http =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Accept: */*\r\n"
        "\r\n"
        "body line";
    n_gui_syntaxview_set_text(gui, sv, http);
    /* five lines: request, two headers, blank, body */
    expect_true("http line count is 5", n_gui_syntaxview_get_line_count(gui, sv) == 5);

    /* text-selection and copy API (byte offsets into the text) */
    expect_true("no selection after set_text", n_gui_syntaxview_get_selected_text(gui, sv) == NULL);
    {
        const char* got = n_gui_syntaxview_get_text(gui, sv);
        expect_true("get_text round-trips", got && strcmp(got, http) == 0);
    }
    n_gui_syntaxview_set_selection(gui, sv, 0, 3);
    {
        char* sel = n_gui_syntaxview_get_selected_text(gui, sv);
        expect_true("selection [0,3) is GET", sel && strcmp(sel, "GET") == 0);
        Free(sel);
    }
    n_gui_syntaxview_set_selection(gui, sv, 3, 0); /* reversed range selects the same text */
    {
        char* sel = n_gui_syntaxview_get_selected_text(gui, sv);
        expect_true("reversed [3,0) is GET", sel && strcmp(sel, "GET") == 0);
        Free(sel);
    }
    n_gui_syntaxview_set_selection(gui, sv, 5, 5); /* an empty range clears the selection */
    expect_true("empty range clears selection", n_gui_syntaxview_get_selected_text(gui, sv) == NULL);
    n_gui_syntaxview_set_selection(gui, sv, 0, 100000); /* out-of-range clamps to the length */
    {
        char* sel = n_gui_syntaxview_get_selected_text(gui, sv);
        expect_true("over-long end clamps to text", sel && strcmp(sel, http) == 0);
        Free(sel);
    }
    n_gui_syntaxview_select_all(gui, sv);
    {
        char* sel = n_gui_syntaxview_get_selected_text(gui, sv);
        expect_true("select_all returns full text", sel && strcmp(sel, http) == 0);
        Free(sel);
    }
    n_gui_syntaxview_select_all(gui, sv); /* set_text must drop a live selection */
    n_gui_syntaxview_set_text(gui, sv, http);
    expect_true("set_text clears selection", n_gui_syntaxview_get_selected_text(gui, sv) == NULL);

    n_gui_syntaxview_set_mode(gui, sv, N_GUI_SYNTAX_JSON);
    n_gui_syntaxview_set_text(gui, sv, "{\n  \"a\": 1,\n  \"b\": \"x\"\n}");
    expect_true("json line count is 4", n_gui_syntaxview_get_line_count(gui, sv) == 4);

    /* the newer modes accept text like any other mode */
    n_gui_syntaxview_set_mode(gui, sv, N_GUI_SYNTAX_XML);
    n_gui_syntaxview_set_text(gui, sv, "<root>\n  <a x=\"1\"/>\n  <!-- c -->\n</root>");
    expect_true("xml line count is 4", n_gui_syntaxview_get_line_count(gui, sv) == 4);
    n_gui_syntaxview_set_mode(gui, sv, N_GUI_SYNTAX_YAML);
    n_gui_syntaxview_set_text(gui, sv, "key: value\nlist:\n  - one # note\n");
    expect_true("yaml line count is 4", n_gui_syntaxview_get_line_count(gui, sv) == 4);
    n_gui_syntaxview_set_mode(gui, sv, N_GUI_SYNTAX_JS);
    n_gui_syntaxview_set_text(gui, sv, "function f() {\n  return 1; // done\n}\n");
    expect_true("js line count is 4", n_gui_syntaxview_get_line_count(gui, sv) == 4);

    /* scroll_to_offset centers the line holding the byte offset */
    {
        char big[4096];
        size_t pos = 0;
        int line60_offset = 0;
        for (int ln = 0; ln < 100 && pos + 16 < sizeof(big); ln++) {
            if (ln == 60) line60_offset = (int)pos;
            pos += (size_t)snprintf(big + pos, sizeof(big) - pos, "line %03d\n", ln);
        }
        n_gui_syntaxview_set_mode(gui, sv, N_GUI_SYNTAX_PLAIN);
        n_gui_syntaxview_set_text(gui, sv, big);
        N_GUI_WIDGET* w = n_gui_get_widget(gui, sv);
        N_GUI_SYNTAXVIEW_DATA* yd = w ? (N_GUI_SYNTAXVIEW_DATA*)w->data : NULL;
        expect_true("scroll data reachable", yd != NULL);
        if (yd) {
            expect_true("initial scroll is zero", yd->scroll_offset == 0);
            n_gui_syntaxview_scroll_to_offset(gui, sv, line60_offset);
            expect_true("scroll moved toward line 60",
                        yd->scroll_offset > 0 && yd->scroll_offset <= 60);
            n_gui_syntaxview_scroll_to_offset(gui, sv, 0);
            expect_true("scroll back to top", yd->scroll_offset == 0);
            n_gui_syntaxview_scroll_to_offset(gui, sv, 100000);
            expect_true("over-long offset clamps",
                        yd->scroll_offset > 0 &&
                            yd->scroll_offset < n_gui_syntaxview_get_line_count(gui, sv));
        }
    }

    n_gui_syntaxview_set_text(gui, sv, NULL);
    expect_true("cleared line count is zero", n_gui_syntaxview_get_line_count(gui, sv) == 0);

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_syntaxview: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_syntaxview: all checks passed");
    return 0;
}
