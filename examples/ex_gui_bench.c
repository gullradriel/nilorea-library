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
 *@example ex_gui_bench.c
 *@brief Headless-friendly performance benchmark for the n_gui system.
 *
 * Builds one heavy, representative context (a large datagrid, a multiline
 * textarea, a JSON syntax view, list widgets and many static labels/buttons)
 * and times two hot paths in isolation:
 *   - n_gui_draw()          : average wall-clock milliseconds per full frame
 *   - n_gui_process_event() : average microseconds per synthetic MOUSE_AXES
 *
 * It renders to the display back buffer without flipping, so the number
 * reflects the CPU cost of building the frame (text measurement, tokenization,
 * primitive submission) rather than vsync/present latency. This is the metric
 * the GUI optimisation work targets, and it is reproducible under Xvfb.
 *
 * Not a correctness regression: like the other ex_gui_* programs it is excluded
 * from run_tests.sh. Run it manually (optionally under xvfb-run) before and
 * after an optimisation to compare numbers. It self-terminates and frees every
 * resource, so it is also an ASan/LSan smoke test of the draw + teardown path.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/07/2026
 */

#define ALLEGRO_UNSTABLE 1

#include "nilorea/n_gui.h"

#define BENCH_DISPLAY_W 1600
#define BENCH_DISPLAY_H 1000

/* default workload sizes, overridable on the command line */
#define BENCH_DEFAULT_DRAW_ITERS 1000
#define BENCH_DEFAULT_EVENT_ITERS 20000

#define BENCH_GRID_ROWS 500
#define BENCH_GRID_COLS 8
#define BENCH_LIST_ITEMS 200
#define BENCH_SYNTAX_LINES 200

static int log_level = LOG_NOTICE;
static int draw_iters = BENCH_DEFAULT_DRAW_ITERS;
static int event_iters = BENCH_DEFAULT_EVENT_ITERS;

/* no-op callbacks: the benchmark never fires them, they only exist so the
 * widgets are built exactly as a real application would build them */
static void on_noop_select(int a, int b, int c, void* u) {
    (void)a;
    (void)b;
    (void)c;
    (void)u;
}
static void on_noop_grid_select(int a, int b, void* u) {
    (void)a;
    (void)b;
    (void)u;
}

static void process_args(int argc, char** argv) {
    int c = 0;
    while ((c = getopt(argc, argv, "V:n:e:h")) != -1) {
        switch (c) {
            case 'V':
                if (optarg) {
                    if (!strncmp("LOG_NULL", optarg, 8))
                        log_level = LOG_NULL;
                    else if (!strncmp("LOG_NOTICE", optarg, 10))
                        log_level = LOG_NOTICE;
                    else if (!strncmp("LOG_INFO", optarg, 8))
                        log_level = LOG_INFO;
                    else if (!strncmp("LOG_ERR", optarg, 7))
                        log_level = LOG_ERR;
                    else if (!strncmp("LOG_DEBUG", optarg, 9))
                        log_level = LOG_DEBUG;
                    else {
                        fprintf(stderr, "%s -V (LOG_NULL|LOG_NOTICE|LOG_INFO|LOG_ERR|LOG_DEBUG)\n", argv[0]);
                        exit(1);
                    }
                }
                break;
            case 'n':
                if (optarg) draw_iters = atoi(optarg);
                break;
            case 'e':
                if (optarg) event_iters = atoi(optarg);
                break;
            case 'h':
            default:
                fprintf(stderr, "%s [-V loglevel] [-n draw_iters] [-e event_iters]\n", argv[0]);
                exit(1);
        }
    }
    if (draw_iters < 1) draw_iters = 1;
    if (event_iters < 1) event_iters = 1;
}

/* build a datagrid with many rows and columns */
static void build_datagrid_window(N_GUI_CTX* gui) {
    int win = n_gui_add_window(gui, "Data Grid", 20, 20, 780, 940);
    int grid = n_gui_add_datagrid(gui, win, 10, 10, 760, 900, on_noop_grid_select, NULL);
    for (int c = 0; c < BENCH_GRID_COLS; c++) {
        char title[32];
        snprintf(title, sizeof(title), "Column %d", c);
        n_gui_datagrid_add_column(gui, grid, title, 90.0f);
    }
    for (int r = 0; r < BENCH_GRID_ROWS; r++) {
        char cells[BENCH_GRID_COLS][40];
        const char* values[BENCH_GRID_COLS];
        for (int c = 0; c < BENCH_GRID_COLS; c++) {
            if ((c & 1) == 0)
                snprintf(cells[c], sizeof(cells[c]), "%d", r * BENCH_GRID_COLS + c);
            else
                snprintf(cells[c], sizeof(cells[c]), "cell r%d c%d text", r, c);
            values[c] = cells[c];
        }
        n_gui_datagrid_add_row(gui, grid, values);
    }
}

/* build a multiline textarea filled with a few KB of wrapping text */
static void build_textarea_window(N_GUI_CTX* gui) {
    int win = n_gui_add_window(gui, "Text", 820, 20, 760, 460);
    int ta = n_gui_add_textarea(gui, win, 10, 10, 740, 430, 1, 6000, NULL, NULL);
    char buf[6001];
    size_t p = 0;
    int word = 0;
    while (p < sizeof(buf) - 16) {
        int n = snprintf(buf + p, sizeof(buf) - p, "word%d ", word++);
        if (n <= 0) break;
        p += (size_t)n;
        if ((word % 9) == 0 && p < sizeof(buf) - 2) {
            buf[p++] = '\n';
        }
    }
    buf[p] = '\0';
    n_gui_textarea_set_text(gui, ta, buf);
}

/* build a JSON syntax view with a couple hundred highlighted lines */
static void build_syntaxview_window(N_GUI_CTX* gui) {
    int win = n_gui_add_window(gui, "JSON", 820, 500, 760, 460);
    int sv = n_gui_add_syntaxview(gui, win, 10, 10, 740, 430, N_GUI_SYNTAX_JSON);
    /* ~200 lines of small, dense JSON so tokenization has real work to do */
    size_t cap = (size_t)BENCH_SYNTAX_LINES * 80 + 64;
    char* buf = NULL;
    Malloc(buf, char, cap);
    if (!buf) return;
    size_t p = 0;
    p += (size_t)snprintf(buf + p, cap - p, "{\n");
    for (int i = 0; i < BENCH_SYNTAX_LINES; i++) {
        p += (size_t)snprintf(buf + p, cap - p,
                              "  \"key_%d\": { \"id\": %d, \"name\": \"item_%d\", \"ok\": %s },\n",
                              i, i, i, (i & 1) ? "true" : "false");
    }
    p += (size_t)snprintf(buf + p, cap - p, "  \"end\": null\n}\n");
    buf[p] = '\0';
    n_gui_syntaxview_set_text(gui, sv, buf);
    FreeNoLog(buf);
}

/* build list widgets plus many static labels/buttons/checkboxes/sliders */
static void build_widgets_window(N_GUI_CTX* gui) {
    int win = n_gui_add_window(gui, "Widgets", 400, 100, 760, 800);

    int lb = n_gui_add_listbox(gui, win, 10, 10, 240, 400, N_GUI_SELECT_SINGLE, on_noop_select, NULL);
    for (int i = 0; i < BENCH_LIST_ITEMS; i++) {
        char item[48];
        snprintf(item, sizeof(item), "list item number %d", i);
        n_gui_listbox_add_item(gui, lb, item);
    }

    int rl = n_gui_add_radiolist(gui, win, 10, 420, 240, 300, NULL, NULL);
    for (int i = 0; i < 20; i++) {
        char item[32];
        snprintf(item, sizeof(item), "radio %d", i);
        n_gui_radiolist_add_item(gui, rl, item);
    }

    /* a column of static labels + buttons + checkboxes: exercises the
     * per-widget "measure the label every frame" path */
    float y = 10.0f;
    for (int i = 0; i < 20; i++) {
        char lab[48];
        snprintf(lab, sizeof(lab), "Static label %d value", i);
        n_gui_add_label(gui, win, lab, 270, y, 220, 20, N_GUI_ALIGN_LEFT);
        y += 24.0f;
    }
    y = 10.0f;
    for (int i = 0; i < 12; i++) {
        char lab[32];
        snprintf(lab, sizeof(lab), "Button %d", i);
        n_gui_add_button(gui, win, lab, 500, y, 120, 26, N_GUI_SHAPE_ROUNDED, NULL, NULL);
        y += 30.0f;
    }
    y = 10.0f;
    for (int i = 0; i < 10; i++) {
        char lab[32];
        snprintf(lab, sizeof(lab), "Check %d", i);
        n_gui_add_checkbox(gui, win, lab, 640, y, 100, 22, i & 1, NULL, NULL);
        y += 26.0f;
    }
    n_gui_add_slider(gui, win, 270, 520, 220, 24, 0.0, 100.0, 42.0, 0, NULL, NULL);
    n_gui_add_slider(gui, win, 270, 560, 220, 24, 0.0, 100.0, 73.0, 1, NULL, NULL);

    /* a justified multi-line label: the word-wrap + per-word measure path */
    n_gui_add_label(gui, win,
                    "This is a longer justified paragraph label used to exercise the "
                    "word wrapping and per-word text measurement path that runs every "
                    "single frame in the current implementation of the label draw code.",
                    270, 600, 470, 180, N_GUI_ALIGN_JUSTIFIED);
}

int main(int argc, char** argv) {
    set_log_level(log_level);
    process_args(argc, argv);
    set_log_level(log_level);

    if (!al_init()) {
        n_log(LOG_ERR, "could not init allegro");
        return 1;
    }
    al_init_primitives_addon();
    al_init_font_addon();

    /* Headless-safe: under a CI runner with no X server al_create_display
     * fails. Skip cleanly (exit 0) exactly like ex_gui_multiwin. */
    ALLEGRO_DISPLAY* display = al_create_display(BENCH_DISPLAY_W, BENCH_DISPLAY_H);
    if (!display) {
        n_log(LOG_NOTICE, "no display available (headless): skipping benchmark");
        return 0;
    }

    ALLEGRO_FONT* font = al_create_builtin_font();
    if (!font) {
        n_log(LOG_ERR, "could not create builtin font");
        al_destroy_display(display);
        return 1;
    }

    N_GUI_CTX* gui = n_gui_new_ctx(font);
    if (!gui) {
        n_log(LOG_ERR, "could not create gui context");
        al_destroy_font(font);
        al_destroy_display(display);
        return 1;
    }
    n_gui_set_display(gui, display);
    n_gui_set_display_size(gui, (float)BENCH_DISPLAY_W, (float)BENCH_DISPLAY_H);

    build_datagrid_window(gui);
    build_textarea_window(gui);
    build_syntaxview_window(gui);
    build_widgets_window(gui);

    n_log(LOG_NOTICE, "n_gui benchmark: %d windows, datagrid %dx%d, %d list items, %d syntax lines",
          4, BENCH_GRID_ROWS, BENCH_GRID_COLS, BENCH_LIST_ITEMS, BENCH_SYNTAX_LINES);

    al_set_target_backbuffer(display);

    /* warm-up: prime font glyph caches and any first-frame lazy work */
    for (int i = 0; i < 10; i++) {
        al_clear_to_color(al_map_rgb(30, 30, 35));
        n_gui_draw(gui);
    }

    /* draw benchmark: measure CPU cost of building each frame */
    double t0 = al_get_time();
    for (int i = 0; i < draw_iters; i++) {
        al_clear_to_color(al_map_rgb(30, 30, 35));
        n_gui_draw(gui);
    }
    double t1 = al_get_time();
    al_flip_display();
    double draw_ms = (t1 - t0) * 1000.0 / (double)draw_iters;

    /* event benchmark: sweep synthetic mouse-motion events across the GUI */
    ALLEGRO_EVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.mouse.display = display;
    double e0 = al_get_time();
    for (int i = 0; i < event_iters; i++) {
        ev.type = ALLEGRO_EVENT_MOUSE_AXES;
        ev.mouse.x = (i * 37) % BENCH_DISPLAY_W;
        ev.mouse.y = (i * 53) % BENCH_DISPLAY_H;
        ev.mouse.dz = 0;
        ev.mouse.dw = 0;
        n_gui_process_event(gui, ev);
    }
    double e1 = al_get_time();
    double event_us = (e1 - e0) * 1e6 / (double)event_iters;

    n_log(LOG_NOTICE, "RESULT draw:   %.4f ms/frame  (%d frames, %.1f fps-equivalent)",
          draw_ms, draw_iters, draw_ms > 0.0 ? 1000.0 / draw_ms : 0.0);
    n_log(LOG_NOTICE, "RESULT events: %.4f us/event  (%d MOUSE_AXES events)",
          event_us, event_iters);

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);
    al_destroy_display(display);

    return 0;
}
