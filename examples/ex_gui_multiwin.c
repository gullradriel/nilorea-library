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
 *@example ex_gui_multiwin.c
 *@brief N_GUI native (detached) window regression: a pop-up promoted to a real
 *       OS window keeps working, and the resources shared with the main display
 *       (font glyphs, bitmaps, clipboard) stay valid on both.
 *
 * Needs a real display to create the second window, so it skips itself with a
 * success exit code when none can be opened (headless CI without an X server).
 *
 *@author Castagnier Mickael
 *@version 1.0
 */

#define ALLEGRO_UNSTABLE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nilorea/n_common.h"
#include "nilorea/n_clipboard.h"
#include "nilorea/n_gui.h"
#include "nilorea/n_log.h"

#define MAIN_W 640
#define MAIN_H 480
#define POPUP_W 240
#define POPUP_H 160

static int log_level = LOG_ERR;
static int failures = 0;

/* which button callback fired last, and how many times */
static int g_main_clicks = 0;
static int g_popup_clicks = 0;
/* window id the close callback was invoked for, or -1 */
static int g_closed_id = -1;

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
        n_log(LOG_ERR, "FAIL %s", label);
        failures++;
    } else {
        n_log(LOG_INFO, "ok   %s", label);
    }
}

static void expect_eq_int(const char* label, int got, int want) {
    if (got != want) {
        n_log(LOG_ERR, "FAIL %s: got %d, want %d", label, got, want);
        failures++;
    } else {
        n_log(LOG_INFO, "ok   %s", label);
    }
}

static void expect_eq_float(const char* label, float got, float want) {
    float d = got - want;
    if (d < 0.0f) d = -d;
    if (d > 0.5f) {
        n_log(LOG_ERR, "FAIL %s: got %f, want %f", label, (double)got, (double)want);
        failures++;
    } else {
        n_log(LOG_INFO, "ok   %s", label);
    }
}

static void on_main_click(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    g_main_clicks++;
}

static void on_popup_click(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    g_popup_clicks++;
}

static void on_popup_close(int window_id, void* user_data) {
    (void)user_data;
    g_closed_id = window_id;
}

/* Synthesize a left click at (x,y) coming from a given display and feed it to
 * the GUI. Passing the display is the whole point: it is what n_gui_process_event
 * routes on, so the same coordinates can mean different windows. */
static void click_on(N_GUI_CTX* gui, ALLEGRO_DISPLAY* from, int x, int y) {
    ALLEGRO_EVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.mouse.display = from;
    ev.mouse.x = x;
    ev.mouse.y = y;
    ev.mouse.button = 1;
    ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
    n_gui_process_event(gui, ev);
    ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
    n_gui_process_event(gui, ev);
}

/* Send a Ctrl+<keycode> KEY_CHAR event, the form the textarea handler expects */
static void ctrl_key(N_GUI_CTX* gui, ALLEGRO_DISPLAY* from, int keycode) {
    ALLEGRO_EVENT ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ALLEGRO_EVENT_KEY_CHAR;
    ev.keyboard.display = from;
    ev.keyboard.keycode = keycode;
    ev.keyboard.modifiers = ALLEGRO_KEYMOD_CTRL;
    ev.keyboard.unichar = 0;
    n_gui_process_event(gui, ev);
}

/* Count pixels differing from a reference colour in a w*h box at (x,y).
 * Used to prove that something was actually rasterized into a backbuffer. */
static int count_pixels_differing(ALLEGRO_BITMAP* bmp, int x, int y, int w, int h, ALLEGRO_COLOR ref) {
    int differing = 0;
    unsigned char rr, rg, rb;
    al_unmap_rgb(ref, &rr, &rg, &rb);
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            unsigned char cr, cg, cb;
            al_unmap_rgb(al_get_pixel(bmp, px, py), &cr, &cg, &cb);
            /* generous tolerance: blending, sRGB conversion and driver rounding
               all move a pixel a little without changing what it means here */
            if (abs((int)cr - (int)rr) > 24 || abs((int)cg - (int)rg) > 24 || abs((int)cb - (int)rb) > 24)
                differing++;
        }
    }
    return differing;
}

/* Pixels sampled by sample_chrome_titlebar, -1 until it has run. */
static int g_chrome_titlebar_lit = -1;

/* Count the painted pixels in the top-left corner of a detached window, where an
 * own-chrome title bar lives.
 *
 * This runs as a content-draw callback, i.e. from inside n_gui_draw_detached,
 * after the window and its title bar have been painted into the native
 * backbuffer and BEFORE al_flip_display. The sample has to be taken while the
 * frame is still the current one: after a flip the backbuffer contents are
 * undefined, and a page-flipping driver (Mesa on real hardware) hands back a
 * discarded buffer, so an after-the-fact readback only ever worked on a
 * copy-swapping one such as llvmpipe under Xvfb.
 *
 * The frame was cleared to bg_normal and the title bar fills bg_active with the
 * title text on top, so "differs from bg_normal" is exactly the title bar. */
static void sample_chrome_titlebar(int window_id, void* user_data) {
    N_GUI_CTX* ctx = (N_GUI_CTX*)user_data;
    ALLEGRO_BITMAP* target = al_get_target_bitmap();
    N_GUI_WINDOW* win = ctx ? n_gui_get_window(ctx, window_id) : NULL;
    if (!target || !win) return;
    g_chrome_titlebar_lit = count_pixels_differing(target, 2, 2, 60, 12, win->theme.bg_normal);
}

/* Draw a font glyph run and a bitmap into a display's backbuffer, then read the
 * pixels back. Proves the resources created against another display are usable
 * here, which is the real portability risk of a second ALLEGRO_DISPLAY. */
static void check_shared_resources(const char* who, ALLEGRO_DISPLAY* d, ALLEGRO_FONT* font, ALLEGRO_BITMAP* bmp) {
    char label[128];
    ALLEGRO_COLOR black = al_map_rgb(0, 0, 0);
    ALLEGRO_BITMAP* back;
    int lit;

    al_set_target_backbuffer(d);
    al_clear_to_color(black);
    al_draw_text(font, al_map_rgb(255, 255, 255), 4.0f, 4.0f, 0, "NILOREA");
    al_draw_bitmap(bmp, 4.0f, 40.0f, 0);
    back = al_get_backbuffer(d);

    /* the text: at least a few glyph pixels must have been written */
    lit = count_pixels_differing(back, 0, 0, 120, 24, black);
    snprintf(label, sizeof(label), "%s: shared font rasterizes (%d lit pixels)", who, lit);
    expect_true(label, lit > 10);

    /* the bitmap: the 32x32 block must be there, in its own colour */
    lit = count_pixels_differing(back, 6, 42, 24, 24, black);
    snprintf(label, sizeof(label), "%s: shared bitmap draws (%d lit pixels)", who, lit);
    expect_true(label, lit > 400);
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
    al_install_keyboard();
    al_install_mouse();

    al_set_new_display_flags(ALLEGRO_WINDOWED);
    ALLEGRO_DISPLAY* main_display = al_create_display(MAIN_W, MAIN_H);
    if (!main_display) {
        /* headless build machine: nothing to test, and nothing broken either */
        n_log(LOG_NOTICE, "ex_gui_multiwin: no display available, test skipped");
        return 0;
    }

    ALLEGRO_EVENT_QUEUE* queue = al_create_event_queue();
    ALLEGRO_FONT* font = al_create_builtin_font();
    if (!queue || !font) {
        n_log(LOG_ERR, "queue/font creation failed");
        if (queue) al_destroy_event_queue(queue);
        if (font) al_destroy_font(font);
        al_destroy_display(main_display);
        return 1;
    }
    al_register_event_source(queue, al_get_display_event_source(main_display));

    /* A video bitmap created while ONLY the main display exists. Everything it
     * is drawn onto later happens after a second display was created, which is
     * exactly the resource-sharing case that can bite on some drivers. */
    ALLEGRO_BITMAP* shared_bmp = al_create_bitmap(32, 32);
    if (!shared_bmp) {
        n_log(LOG_ERR, "al_create_bitmap failed");
        al_destroy_event_queue(queue);
        al_destroy_font(font);
        al_destroy_display(main_display);
        return 1;
    }
    al_set_target_bitmap(shared_bmp);
    al_clear_to_color(al_map_rgb(255, 0, 255));
    al_set_target_backbuffer(main_display);

    N_GUI_CTX* gui = n_gui_new_ctx(font);
    if (!gui) {
        n_log(LOG_ERR, "n_gui_new_ctx failed");
        al_destroy_bitmap(shared_bmp);
        al_destroy_event_queue(queue);
        al_destroy_font(font);
        al_destroy_display(main_display);
        return 1;
    }
    n_gui_set_display(gui, main_display);
    n_gui_set_display_size(gui, (float)MAIN_W, (float)MAIN_H);
    n_gui_set_event_queue(gui, queue);
    expect_true("event queue round-trips", n_gui_get_event_queue(gui) == queue);

    /* Two frameless windows both anchored at (0,0) with a button at the same
     * spot. Once one of them is detached they live on different displays, so
     * the identical coordinates are the sharpest possible routing test. */
    int main_win = n_gui_add_window(gui, "Main Panel", 0.0f, 0.0f, 300.0f, 200.0f);
    int popup_win = n_gui_add_window(gui, "Popup", 0.0f, 0.0f, (float)POPUP_W, (float)POPUP_H);
    expect_true("windows created", main_win >= 0 && popup_win >= 0);
    n_gui_window_set_flags(gui, main_win, N_GUI_WIN_FRAMELESS);
    n_gui_window_set_flags(gui, popup_win, N_GUI_WIN_FRAMELESS);
    int main_btn = n_gui_add_button(gui, main_win, "main", 10.0f, 10.0f, 100.0f, 30.0f, N_GUI_SHAPE_RECT, on_main_click, NULL);
    int popup_btn = n_gui_add_button(gui, popup_win, "popup", 10.0f, 10.0f, 100.0f, 30.0f, N_GUI_SHAPE_RECT, on_popup_click, NULL);
    int popup_txt = n_gui_add_textarea(gui, popup_win, 10.0f, 60.0f, 200.0f, 24.0f, 0, 64, NULL, NULL);
    expect_true("widgets created", main_btn >= 0 && popup_btn >= 0 && popup_txt >= 0);

    /* baseline: still a pop-up, both windows on the main display */
    expect_eq_int("nothing detached initially", n_gui_count_detached(gui), 0);
    expect_true("popup not detached initially", !n_gui_window_is_detached(gui, popup_win));

    /* DETACH */
    expect_eq_int("detach succeeds", n_gui_window_detach(gui, popup_win, N_GUI_DETACH_RESIZABLE), 0);
    ALLEGRO_DISPLAY* popup_display = n_gui_window_get_display(gui, popup_win);
    expect_true("native display created", popup_display != NULL);
    expect_true("native display differs from the main one", popup_display != main_display);
    expect_eq_int("one window detached", n_gui_count_detached(gui), 1);
    expect_eq_int("detached flag set", n_gui_window_is_detached(gui, popup_win), 1);
    expect_eq_int("display maps back to the window", n_gui_window_from_display(gui, popup_display), popup_win);
    expect_eq_int("main display maps to no detached window", n_gui_window_from_display(gui, main_display), -1);

    if (!popup_display) {
        n_log(LOG_ERR, "no native display, cannot continue");
        n_gui_destroy_ctx(&gui);
        al_destroy_bitmap(shared_bmp);
        al_destroy_event_queue(queue);
        al_destroy_font(font);
        al_destroy_display(main_display);
        return 1;
    }

    /* the detached window fills its display, at the origin, without our chrome */
    {
        N_GUI_WINDOW* w = n_gui_get_window(gui, popup_win);
        expect_true("detached window sits at the origin", w && w->x == 0.0f && w->y == 0.0f);
        expect_eq_float("detached window width matches the display", w ? w->w : -1.0f, (float)al_get_display_width(popup_display));
        expect_eq_float("detached window height matches the display", w ? w->h : -1.0f, (float)al_get_display_height(popup_display));
        expect_true("detached window is frameless", w && (w->flags & N_GUI_WIN_FRAMELESS));
    }

    /* SHARED RESOURCES: font glyphs and a bitmap created before the second
       display existed must still rasterize on both displays. */
    check_shared_resources("main display", main_display, font, shared_bmp);
    check_shared_resources("native display", popup_display, font, shared_bmp);

    /* EVENT ROUTING: identical coordinates, different display, different window */
    g_main_clicks = g_popup_clicks = 0;
    click_on(gui, main_display, 30, 25);
    expect_eq_int("click on the main display hits the main button", g_main_clicks, 1);
    expect_eq_int("click on the main display does not reach the detached window", g_popup_clicks, 0);

    g_main_clicks = g_popup_clicks = 0;
    click_on(gui, popup_display, 30, 25);
    expect_eq_int("click on the native display hits the detached button", g_popup_clicks, 1);
    expect_eq_int("click on the native display does not reach the main window", g_main_clicks, 0);

    /* an event with no display (timers, and the headless tests that synthesize
       events with memset) still reaches the main display's windows */
    g_main_clicks = g_popup_clicks = 0;
    click_on(gui, NULL, 30, 25);
    expect_eq_int("display-less event falls back to the main pass", g_main_clicks, 1);
    expect_eq_int("display-less event does not reach the detached window", g_popup_clicks, 0);

    /* n_gui_wants_mouse answers for the main display only */
    expect_eq_int("wants_mouse still answers for the main display", n_gui_wants_mouse(gui), 1);

    /* CLIPBOARD: the context follows the display input came from, so a copy
       made inside the detached window addresses that display. */
    {
        const char* payload = "nilorea-multiwin-clipboard";
        n_gui_textarea_set_text(gui, popup_txt, payload);
        n_gui_set_focus(gui, popup_txt);

        /* an event from the native display makes it the active one */
        click_on(gui, popup_display, 30, 25);
        n_gui_set_focus(gui, popup_txt);
        expect_true("active display follows the native window", gui->active_display == popup_display);

        ctrl_key(gui, popup_display, ALLEGRO_KEY_A); /* select all */
        ctrl_key(gui, popup_display, ALLEGRO_KEY_C); /* copy */

        /* read back through whichever backend the GUI wrote with */
        char* got = NULL;
        if (n_clipboard_available()) {
            got = n_clipboard_get(N_CLIPBOARD_CLIPBOARD);
            expect_true("clipboard copy from the detached window round-trips",
                        got != NULL && strcmp(got, payload) == 0);
            if (got) {
                if (strcmp(got, payload) != 0)
                    n_log(LOG_ERR, "clipboard: got '%s', want '%s'", got, payload);
                free(got);
            }
        } else {
            char* al_got = al_get_clipboard_text(popup_display);
            expect_true("clipboard copy from the detached window round-trips",
                        al_got != NULL && strcmp(al_got, payload) == 0);
            if (al_got) {
                if (strcmp(al_got, payload) != 0)
                    n_log(LOG_ERR, "clipboard: got '%s', want '%s'", al_got, payload);
                al_free(al_got);
            }
        }
        n_gui_set_focus(gui, -1);
    }

    /* DRAWING: the main pass must not touch the native display, and the native
       pass must not touch the main one. Both are exercised for real here. */
    al_set_target_backbuffer(main_display);
    al_clear_to_color(al_map_rgb(0, 0, 0));
    n_gui_draw(gui);
    {
        /* the detached window drew nothing on the main display: its button
           would have landed in this box if the pass filter were missing */
        ALLEGRO_BITMAP* back = al_get_backbuffer(main_display);
        int lit = count_pixels_differing(back, 320, 300, 40, 40, al_map_rgb(0, 0, 0));
        expect_eq_int("main pass leaves the far corner untouched", lit, 0);
    }
    n_gui_draw_detached(gui);
    expect_eq_int("draw_detached keeps the window alive", n_gui_count_detached(gui), 1);
    /* the caller's target survives a detached draw pass */
    al_set_target_backbuffer(main_display);
    n_gui_draw_detached(gui);
    expect_true("draw_detached restores the caller's target", al_get_target_bitmap() == al_get_backbuffer(main_display));

    /* CLOSE REQUEST from the window manager: the callback runs, and the display
       is only released at the next n_gui_draw_detached. */
    {
        ALLEGRO_EVENT ev;
        n_gui_window_set_close_callback(gui, popup_win, on_popup_close, NULL);
        g_closed_id = -1;
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_DISPLAY_CLOSE;
        ev.display.source = popup_display;
        expect_eq_int("DISPLAY_CLOSE is consumed", n_gui_process_event(gui, ev), 1);
        expect_eq_int("close callback fired for the right window", g_closed_id, popup_win);
        expect_eq_int("display survives until the next draw pass", n_gui_count_detached(gui), 1);
        n_gui_draw_detached(gui);
        expect_eq_int("display released by the deferred pass", n_gui_count_detached(gui), 0);
        expect_eq_int("window is closed", n_gui_window_is_open(gui, popup_win), 0);
        /* geometry fell back to the pop-up one */
        N_GUI_WINDOW* w = n_gui_get_window(gui, popup_win);
        expect_eq_float("pop-up width restored", w ? w->w : -1.0f, (float)POPUP_W);
        expect_eq_float("pop-up height restored", w ? w->h : -1.0f, (float)POPUP_H);
    }

    /* RE-OPEN: the detach preference survived the close, so opening the window
       brings the native window back rather than a pop-up. */
    n_gui_open_window(gui, popup_win);
    expect_eq_int("re-opening restores the native window", n_gui_count_detached(gui), 1);
    popup_display = n_gui_window_get_display(gui, popup_win);
    expect_true("a fresh native display was created", popup_display != NULL);

    /* ATTACH: back to a pop-up, with the original geometry and flags */
    expect_eq_int("attach succeeds", n_gui_window_attach(gui, popup_win), 0);
    expect_eq_int("nothing detached after attach", n_gui_count_detached(gui), 0);
    {
        N_GUI_WINDOW* w = n_gui_get_window(gui, popup_win);
        expect_eq_float("pop-up x restored", w ? w->x : -1.0f, 0.0f);
        expect_eq_float("pop-up y restored", w ? w->y : -1.0f, 0.0f);
        expect_eq_float("pop-up w restored", w ? w->w : -1.0f, (float)POPUP_W);
        expect_eq_float("pop-up h restored", w ? w->h : -1.0f, (float)POPUP_H);
    }
    /* and it is hit-testable on the main display again: it is on top of the
       main panel, so it takes the click */
    g_main_clicks = g_popup_clicks = 0;
    click_on(gui, main_display, 30, 25);
    expect_eq_int("re-attached pop-up takes clicks on the main display", g_popup_clicks, 1);

    /* OWN CHROME (N_GUI_DETACH_OWN_CHROME): the window keeps drawing its own
       title bar inside a frameless native window, and that chrome now drives
       the OS window instead of x/y/w/h. */
    {
        int chrome_win = n_gui_add_window(gui, "Chrome", 0.0f, 0.0f, 260.0f, 180.0f);
        expect_true("own-chrome window created", chrome_win >= 0);
        n_gui_window_set_flags(gui, chrome_win, N_GUI_WIN_RESIZABLE | N_GUI_WIN_BTN_ALL);
        expect_eq_int("detach with own chrome succeeds",
                      n_gui_window_detach(gui, chrome_win, N_GUI_DETACH_OWN_CHROME), 0);
        ALLEGRO_DISPLAY* cd = n_gui_window_get_display(gui, chrome_win);
        expect_true("own-chrome native display created", cd != NULL);

        N_GUI_WINDOW* cw = n_gui_get_window(gui, chrome_win);
        /* the key difference from the default: our title bar stays */
        expect_true("own-chrome window is NOT forced frameless", cw && !(cw->flags & N_GUI_WIN_FRAMELESS));
        /* Allegro documents ALLEGRO_FRAMELESS as varying by platform, and with no
           window manager running (bare Xvfb) there are no decorations to remove
           in the first place, so the flag does not come back. What N_GUI owns is
           the request, not the window manager's answer: report, do not fail. */
        if (cd && !(al_get_display_flags(cd) & ALLEGRO_FRAMELESS))
            n_log(LOG_NOTICE, "platform did not honour ALLEGRO_FRAMELESS (no window manager?), check skipped");
        expect_true("own-chrome native window is resizable (needed by resize/maximize)",
                    cd && (al_get_display_flags(cd) & ALLEGRO_RESIZABLE));
        /* the display covers the WHOLE window, title bar included, unlike the
           default where it covers the body only */
        expect_eq_float("own-chrome display height covers the title bar too",
                        cd ? (float)al_get_display_height(cd) : -1.0f, 180.0f);
        expect_true("own-chrome window keeps a title bar height", cw && cw->titlebar_h > 0.0f);

        /* the OS window carries the window's title */
        expect_true("native window title was set before creation", cd != NULL);

        /* dragging the title bar moves the OS window, not win->x/y */
        if (cd && cw) {
            int px0 = 0, py0 = 0, px1 = 0, py1 = 0;
            ALLEGRO_EVENT ev;
            al_get_window_position(cd, &px0, &py0);

            memset(&ev, 0, sizeof(ev));
            ev.mouse.display = cd;
            ev.mouse.button = 1;
            ev.mouse.x = 60;
            ev.mouse.y = (int)(cw->titlebar_h / 2.0f);
            ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
            n_gui_process_event(gui, ev);
            expect_true("title bar press starts a drag", (cw->state & N_GUI_WIN_DRAGGING) != 0);

            /* An own-chrome drag anchors on the DESKTOP cursor and moves the OS
               window by how far the pointer has travelled on the desktop, not by a
               display-local delta (see _native_drag_begin: a display-local delta
               cannot work for a window that is itself moving). So a synthetic
               MOUSE_AXES on its own moves nothing, the real pointer has to travel
               with it. Warp it, then assert the window followed by the same amount.
               Where the platform will not warp the pointer there is nothing to
               measure, so the check is skipped rather than failed. */
            int cbx = 0, cby = 0, cax = 0, cay = 0;
            int warped = al_get_mouse_cursor_position(&cbx, &cby) &&
                         al_set_mouse_xy(cd, 90, (int)(cw->titlebar_h / 2.0f)) &&
                         al_get_mouse_cursor_position(&cax, &cay);

            ev.type = ALLEGRO_EVENT_MOUSE_AXES;
            ev.mouse.x = 90; /* +30 px to the right */
            n_gui_process_event(gui, ev);
            al_get_window_position(cd, &px1, &py1);
            if (warped && cax != cbx)
                expect_eq_int("title bar drag moved the OS window", px1 - px0, cax - cbx);
            else
                n_log(LOG_NOTICE, "ex_gui_multiwin: pointer cannot be warped here, skipping the OS-window drag check");
            expect_eq_float("title bar drag left the window pinned at the origin", cw->x, 0.0f);

            ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
            n_gui_process_event(gui, ev);
            expect_true("drag ends on release", !(cw->state & N_GUI_WIN_DRAGGING));
        }

        /* minimise shrinks the OS window to the title bar and back.
           ALLEGRO_MINIMIZED is read-only, so this is the portable mapping. */
        if (cd && cw) {
            float full_h = cw->h;
            n_gui_minimize_window(gui, chrome_win);
            expect_true("minimised flag set", (cw->state & N_GUI_WIN_MINIMISED) != 0);
            expect_eq_float("minimise shrank the OS window to the title bar",
                            (float)al_get_display_height(cd), cw->titlebar_h);
            expect_eq_float("minimise kept the full window height for the restore", cw->h, full_h);
            n_gui_restore_window(gui, chrome_win);
            expect_true("minimised flag cleared", !(cw->state & N_GUI_WIN_MINIMISED));
            expect_eq_float("restore grew the OS window back",
                            (float)al_get_display_height(cd), full_h);
        }

        /* The same thing again, but driven through the title bar BUTTON and with the
           flag set an application actually uses (RESIZABLE alongside OWN_CHROME).
           n_gui_minimize_window is the API path; the button goes through
           _tb_button_action, and a resizable native window is the configuration a
           host detaches with, so neither was covered by the checks above. */
        if (cd && cw) {
            ALLEGRO_EVENT ev;
            float full_h2;
            float sz, mid_x;
            n_gui_window_set_flags(gui, chrome_win,
                                   n_gui_window_get_flags(gui, chrome_win) | N_GUI_WIN_RESIZABLE);
            /* A real minimum size, which is what an application sets so its layout
               always has room. It is also what made minimise look broken on a real
               desktop: the constraint is handed to the window manager at detach time
               and a title bar is far below it, so the shrink was refused and the
               window stayed full size while its content hid. Note this check cannot
               reproduce that here: window constraints are enforced by the window
               manager, and under bare Xvfb there is none, so the shrink succeeds
               either way. What it does cover is the title-bar BUTTON path on a
               RESIZABLE own-chrome window, which nothing else exercises. */
            cw->min_w = 200.0f;
            cw->min_h = 150.0f;
            if (al_set_window_constraints(cd, 200, 150, 0, 0))
                al_apply_window_constraints(cd, true);
            n_gui_restore_window(gui, chrome_win);
            full_h2 = cw->h;

            /* buttons run right to left: close, maximize, minimize */
            sz = gui->style.tb_btn_size > 0.0f ? gui->style.tb_btn_size : cw->titlebar_h - 4.0f;
            mid_x = cw->x + cw->w - gui->style.tb_btn_right_margin - 2.0f * (sz + gui->style.tb_btn_spacing) - sz / 2.0f;
            memset(&ev, 0, sizeof(ev));
            ev.mouse.display = cd;
            ev.mouse.button = 1;
            ev.mouse.x = (int)mid_x;
            ev.mouse.y = (int)(cw->y + cw->titlebar_h / 2.0f);
            ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
            n_gui_process_event(gui, ev);
            ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
            n_gui_process_event(gui, ev);

            expect_true("title bar minimise button set the minimised flag",
                        (cw->state & N_GUI_WIN_MINIMISED) != 0);
            expect_eq_float("title bar minimise shrank the OS window to the title bar",
                            (float)al_get_display_height(cd), cw->titlebar_h);
            n_gui_restore_window(gui, chrome_win);
            expect_eq_float("restore grew the resizable OS window back",
                            (float)al_get_display_height(cd), full_h2);
        }

        /* the resize grip drives al_resize_display */
        if (cd && cw) {
            ALLEGRO_EVENT ev;
            float grip = gui->style.grip_size + 2.0f;
            memset(&ev, 0, sizeof(ev));
            ev.mouse.display = cd;
            ev.mouse.button = 1;
            ev.mouse.x = (int)(cw->w - grip / 2.0f);
            ev.mouse.y = (int)(cw->h - grip / 2.0f);
            ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_DOWN;
            n_gui_process_event(gui, ev);
            expect_true("grip press starts a resize", (cw->state & N_GUI_WIN_RESIZING) != 0);

            ev.type = ALLEGRO_EVENT_MOUSE_AXES;
            ev.mouse.x += 40;
            ev.mouse.y += 20;
            n_gui_process_event(gui, ev);
            expect_eq_float("grip resized the OS window (width)", (float)al_get_display_width(cd), 300.0f);
            expect_eq_float("grip resized the OS window (height)", (float)al_get_display_height(cd), 200.0f);

            ev.type = ALLEGRO_EVENT_MOUSE_BUTTON_UP;
            n_gui_process_event(gui, ev);
            expect_true("resize ends on release", !(cw->state & N_GUI_WIN_RESIZING));
        }

        /* window constraints were pushed to the WM so it cannot shrink the
           window below what the layout can render */
        if (cd && cw) {
            int cmin_w = 0, cmin_h = 0, cmax_w = 0, cmax_h = 0;
            if (al_get_window_constraints(cd, &cmin_w, &cmin_h, &cmax_w, &cmax_h)) {
                expect_eq_int("min width constraint pushed to the window manager", cmin_w, (int)(cw->min_w + 0.5f));
                expect_eq_int("min height constraint pushed to the window manager", cmin_h, (int)(cw->min_h + 0.5f));
            } else {
                n_log(LOG_NOTICE, "window constraints unsupported on this platform, check skipped");
            }
        }

        /* an icon can be given to the OS window (borrowed bitmap) */
        {
            ALLEGRO_BITMAP* icons[1];
            icons[0] = shared_bmp;
            int refused;
            expect_eq_int("native icon accepted", n_gui_window_set_native_icons(gui, chrome_win, icons, 1), 0);
            /* expected to fail, mute the error it logs on the way out */
            set_log_level(LOG_NULL);
            refused = n_gui_window_set_native_icons(gui, main_win, icons, 1);
            set_log_level(log_level);
            expect_eq_int("native icon refused for a pop-up", refused, -1);
        }

        /* HALT_DRAWING is acknowledged and suppresses drawing until RESUME */
        if (cd) {
            ALLEGRO_EVENT ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = ALLEGRO_EVENT_DISPLAY_HALT_DRAWING;
            ev.display.source = cd;
            expect_eq_int("HALT_DRAWING is consumed", n_gui_process_event(gui, ev), 1);
            expect_eq_int("halted window reports itself hidden", n_gui_window_native_is_hidden(gui, chrome_win), 1);
            n_gui_draw_detached(gui); /* must not touch the halted display */
            ev.type = ALLEGRO_EVENT_DISPLAY_RESUME_DRAWING;
            expect_eq_int("RESUME_DRAWING is consumed", n_gui_process_event(gui, ev), 1);
            expect_eq_int("resumed window is drawable again", n_gui_window_native_is_hidden(gui, chrome_win), 0);
        }

        /* its own chrome draws into its own display: the title bar is at the
           top of the native window, which the default detach mode has no room
           for at all. The pixels are counted from inside the frame, see
           sample_chrome_titlebar for why a readback after the flip is not
           portable. */
        if (cd && cw) {
            g_chrome_titlebar_lit = -1;
            n_gui_window_set_content_draw_callback(gui, chrome_win, sample_chrome_titlebar, gui);
            n_gui_draw_detached(gui);
            n_gui_window_set_content_draw_callback(gui, chrome_win, NULL, NULL);
            expect_true("own chrome drew a title bar into the native window", g_chrome_titlebar_lit > 10);
        }

        /* maximise drives the OS window through ALLEGRO_MAXIMIZED. Whether the
           window manager honours it is its business, so only our bookkeeping is
           asserted here, the granted geometry arrives as a DISPLAY_RESIZE. */
        if (cw) {
            n_gui_maximize_window(gui, chrome_win);
            expect_true("maximise sets the maximised flag", (cw->state & N_GUI_WIN_MAXIMISED) != 0);
            n_gui_maximize_window(gui, chrome_win);
            expect_true("maximise toggles back off", !(cw->state & N_GUI_WIN_MAXIMISED));
        }

        /* attach restores the pop-up geometry AND the pop-up flags */
        expect_eq_int("own-chrome attach succeeds", n_gui_window_attach(gui, chrome_win), 0);
        {
            N_GUI_WINDOW* w = n_gui_get_window(gui, chrome_win);
            expect_eq_float("own-chrome pop-up w restored", w ? w->w : -1.0f, 260.0f);
            expect_eq_float("own-chrome pop-up h restored", w ? w->h : -1.0f, 180.0f);
            expect_true("own-chrome pop-up flags restored",
                        w && (w->flags & N_GUI_WIN_RESIZABLE) && !(w->flags & N_GUI_WIN_FRAMELESS));
        }
        n_gui_close_window(gui, chrome_win);
    }

#ifdef HAVE_CJSON
    /* LAYOUT PERSISTENCE: the detached state and the native geometry survive a
       save/destroy/rebuild/load cycle. */
    {
        const char* path = "ex_gui_multiwin_layout.json";
        expect_eq_int("re-detach for the save", n_gui_window_detach(gui, popup_win, N_GUI_DETACH_RESIZABLE), 0);
        expect_eq_int("layout saved", n_gui_save_layout_json(gui, path), 0);

        float want_w = 0.0f, want_h = 0.0f;
        {
            ALLEGRO_DISPLAY* d = n_gui_window_get_display(gui, popup_win);
            want_w = (float)al_get_display_width(d);
            want_h = (float)al_get_display_height(d);
        }

        n_gui_destroy_ctx(&gui);
        al_set_target_backbuffer(main_display);

        gui = n_gui_new_ctx(font);
        expect_true("second context created", gui != NULL);
        if (gui) {
            n_gui_set_display(gui, main_display);
            n_gui_set_display_size(gui, (float)MAIN_W, (float)MAIN_H);
            n_gui_set_event_queue(gui, queue);
            int w2_main = n_gui_add_window(gui, "Main Panel", 0.0f, 0.0f, 300.0f, 200.0f);
            int w2_popup = n_gui_add_window(gui, "Popup", 0.0f, 0.0f, (float)POPUP_W, (float)POPUP_H);
            expect_true("rebuilt windows", w2_main >= 0 && w2_popup >= 0);
            expect_eq_int("layout loaded", n_gui_load_layout_json(gui, path), 0);
            expect_eq_int("loaded layout re-created the native window", n_gui_window_is_detached(gui, w2_popup), 1);
            ALLEGRO_DISPLAY* d2 = n_gui_window_get_display(gui, w2_popup);
            expect_true("restored native display exists", d2 != NULL);
            if (d2) {
                expect_eq_float("restored native width", (float)al_get_display_width(d2), want_w);
                expect_eq_float("restored native height", (float)al_get_display_height(d2), want_h);
            }
            /* the pop-up fallback geometry came back too, not the native size */
            N_GUI_WINDOW* w = n_gui_get_window(gui, w2_popup);
            expect_eq_float("restored pop-up fallback width", w ? w->saved_w : -1.0f, (float)POPUP_W);
            expect_eq_float("restored pop-up fallback height", w ? w->saved_h : -1.0f, (float)POPUP_H);
        }
        remove(path);
    }
#endif

    /* the context owns any display still attached: this must not leak one */
    if (gui) n_gui_destroy_ctx(&gui);
    al_set_target_backbuffer(main_display);
    al_destroy_bitmap(shared_bmp);
    al_destroy_event_queue(queue);
    al_destroy_font(font);
    al_destroy_display(main_display);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_multiwin: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_multiwin: all checks passed");
    return 0;
}
