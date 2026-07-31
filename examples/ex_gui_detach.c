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
 *@example ex_gui_detach.c
 *@brief Interactive N_GUI native (detached) window demo.
 *
 * Three ordinary pop-up windows, each with a button that promotes it to a real
 * OS window and back. One panel uses the window manager's chrome (the default),
 * one keeps N_GUI's own chrome inside a frameless native window
 * (N_GUI_DETACH_OWN_CHROME), and one scales its widgets as the native window is
 * resized (N_GUI_DETACH_SCALE_CONTENT).
 *
 * The point of the demo is that nothing about the widgets changes: the same
 * buttons, slider, text area and list keep working, and the same widget ids
 * keep addressing them, whether the panel is a pop-up or its own window.
 *
 * Keys: ESC quits, F5 saves the layout, F9 loads it back.
 *
 * Options: -a detaches every panel at startup, -q SECONDS quits on its own.
 * Together they give the demo a smoke-test mode that exercises the detach,
 * draw and teardown paths under a sanitizer without a user at the keyboard.
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
#include "nilorea/n_gui.h"
#include "nilorea/n_log.h"

#define WIDTH 960
#define HEIGHT 640
#define LAYOUT_FILE "ex_gui_detach_layout.json"

/*! one demo panel: a window plus the button that detaches / attaches it */
typedef struct DEMO_PANEL {
    /*! window id */
    int win;
    /*! id of the panel's own detach / attach button */
    int btn;
    /*! options this panel detaches with (N_GUI_DETACH_*) */
    int detach_flags;
    /*! short name used in the status line */
    const char* name;
} DEMO_PANEL;

static int log_level = LOG_NOTICE;
static int DONE = 0;
/*! seconds after which the demo quits on its own (0 = run until ESC) */
static double quit_after = 0.0;
/*! 1 to detach every panel at startup (-a) */
static int auto_detach = 0;

static N_GUI_CTX* gui = NULL;
static DEMO_PANEL panels[3];
static int nb_panels = 0;
static int lbl_status = -1;
static int lbl_hint = -1;
/*! text area of the "Notes" panel, used by the copy/paste hint */
static int notes_txt = -1;

static void process_args(int argc, char** argv) {
    int opt;
    while ((opt = getopt(argc, argv, "haq:V:")) != -1) {
        switch (opt) {
            case 'a':
                auto_detach = 1;
                break;
            case 'q':
                quit_after = atof(optarg);
                break;
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
                else {
                    n_log(LOG_ERR, "%s is not a valid log level", optarg);
                    exit(1);
                }
                break;
            case 'h':
            default:
                printf(
                    "Usage: %s [-V LOG_LEVEL] [-a] [-q SECONDS]\n"
                    "  -a           detach every panel at startup\n"
                    "  -q SECONDS   quit after SECONDS (0 = until ESC)\n",
                    argv[0]);
                exit(0);
        }
    }
    set_log_level(log_level);
}

/*! find the panel a window id belongs to, or NULL */
static DEMO_PANEL* panel_of(int window_id) {
    for (int i = 0; i < nb_panels; i++) {
        if (panels[i].win == window_id) return &panels[i];
    }
    return NULL;
}

/*! keep each panel's button label in sync with what it will do next */
static void refresh_buttons(void) {
    for (int i = 0; i < nb_panels; i++) {
        int detached = n_gui_window_is_detached(gui, panels[i].win);
        n_gui_button_set_label(gui, panels[i].btn, detached ? "Attach to main window" : "Detach to its own window");
    }
    if (lbl_status >= 0) {
        char buf[192];
        char detail[128];
        detail[0] = '\0';
        for (int i = 0; i < nb_panels; i++) {
            char one[48];
            snprintf(one, sizeof(one), "%s%s:%s", detail[0] ? "  " : "", panels[i].name,
                     n_gui_window_is_detached(gui, panels[i].win) ? "native" : "popup");
            strncat(detail, one, sizeof(detail) - strlen(detail) - 1);
        }
        snprintf(buf, sizeof(buf), "%d native window(s)   %s", n_gui_count_detached(gui), detail);
        n_gui_label_set_text(gui, lbl_status, buf);
    }
}

/*! the per-panel button: toggle this panel between pop-up and native window */
static void on_toggle_detach(int widget_id, void* user_data) {
    (void)widget_id;
    DEMO_PANEL* p = (DEMO_PANEL*)user_data;
    if (!p) return;
    if (n_gui_window_is_detached(gui, p->win)) {
        n_gui_window_attach(gui, p->win);
        n_log(LOG_NOTICE, "%s is a pop-up again", p->name);
    } else if (n_gui_window_detach(gui, p->win, p->detach_flags) == 0) {
        n_log(LOG_NOTICE, "%s detached into a native window", p->name);
    } else {
        n_log(LOG_ERR, "%s could not be detached", p->name);
    }
    refresh_buttons();
}

static void on_detach_all(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    for (int i = 0; i < nb_panels; i++) {
        if (!n_gui_window_is_detached(gui, panels[i].win))
            n_gui_window_detach(gui, panels[i].win, panels[i].detach_flags);
    }
    refresh_buttons();
}

static void on_attach_all(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    for (int i = 0; i < nb_panels; i++) {
        n_gui_window_attach(gui, panels[i].win);
    }
    refresh_buttons();
}

/*! A native window's close button (the window manager's, or N_GUI's own when
 *  the panel keeps its chrome) lands here exactly like a pop-up's would. The
 *  display itself is released at the next n_gui_draw_detached, never from
 *  inside this callback. */
static void on_panel_close(int window_id, void* user_data) {
    (void)user_data;
    DEMO_PANEL* p = panel_of(window_id);
    n_gui_close_window(gui, window_id);
    n_log(LOG_NOTICE, "%s closed, use 'Show all panels' to bring it back", p ? p->name : "panel");
}

static void on_show_all(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    for (int i = 0; i < nb_panels; i++) {
        /* a panel closed while detached remembers that it wants a native
           window, so opening it re-creates one instead of a pop-up */
        n_gui_open_window(gui, panels[i].win);
    }
    refresh_buttons();
}

static void on_save_layout(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    if (n_gui_save_layout_json(gui, LAYOUT_FILE) == 0)
        n_log(LOG_NOTICE, "layout saved to %s (detached state included)", LAYOUT_FILE);
    else
        n_log(LOG_ERR, "could not save %s", LAYOUT_FILE);
}

static void on_load_layout(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;
    if (n_gui_load_layout_json(gui, LAYOUT_FILE) == 0)
        n_log(LOG_NOTICE, "layout loaded from %s", LAYOUT_FILE);
    else
        n_log(LOG_ERR, "could not load %s (save one first with F5)", LAYOUT_FILE);
    refresh_buttons();
}

static void on_slider_change(int widget_id, double value, void* user_data) {
    (void)widget_id;
    (void)user_data;
    n_log(LOG_INFO, "slider: %.1f", value);
}

/*! build one demo panel: a few ordinary widgets plus its detach button */
static int build_panel(const char* title, float x, float y, float w, float h, int detach_flags, const char* name) {
    int win = n_gui_add_window(gui, title, x, y, w, h);
    if (win < 0) return -1;
    n_gui_window_set_flags(gui, win, N_GUI_WIN_RESIZABLE | N_GUI_WIN_BTN_ALL);
    n_gui_window_set_close_callback(gui, win, on_panel_close, NULL);

    DEMO_PANEL* p = &panels[nb_panels];
    p->win = win;
    p->detach_flags = detach_flags;
    p->name = name;
    p->btn = n_gui_add_button(gui, win, "Detach to its own window",
                              10.0f, 10.0f, w - 20.0f, 26.0f, N_GUI_SHAPE_ROUNDED,
                              on_toggle_detach, p);
    n_gui_set_widget_tooltip(gui, p->btn, "Promote this panel to a real OS window, and back");
    nb_panels++;
    return win;
}

int main(int argc, char** argv) {
    set_log_level(log_level);
    process_args(argc, argv);

    if (!al_init()) {
        n_log(LOG_ERR, "unable to initialize allegro");
        return 1;
    }
    if (!al_init_primitives_addon() || !al_init_font_addon()) {
        n_log(LOG_ERR, "unable to initialize allegro addons");
        return 1;
    }
    al_install_keyboard();
    al_install_mouse();

    ALLEGRO_EVENT_QUEUE* queue = al_create_event_queue();
    if (!queue) {
        n_log(LOG_ERR, "unable to create the event queue");
        return 1;
    }

    al_set_new_display_flags(ALLEGRO_WINDOWED | ALLEGRO_RESIZABLE);
    ALLEGRO_DISPLAY* display = al_create_display(WIDTH, HEIGHT);
    if (!display) {
        n_log(LOG_ERR, "unable to create a display, is a graphical session available?");
        al_destroy_event_queue(queue);
        return 1;
    }
    al_set_window_title(display, "Nilorea GUI, detachable windows");

    ALLEGRO_TIMER* fps_timer = al_create_timer(1.0 / 60.0);
    al_register_event_source(queue, al_get_display_event_source(display));
    al_register_event_source(queue, al_get_timer_event_source(fps_timer));
    al_register_event_source(queue, al_get_keyboard_event_source());
    al_register_event_source(queue, al_get_mouse_event_source());

    ALLEGRO_FONT* font = al_create_builtin_font();
    if (!font) {
        n_log(LOG_ERR, "unable to create the builtin font");
        al_destroy_timer(fps_timer);
        al_destroy_display(display);
        al_destroy_event_queue(queue);
        return 1;
    }

    gui = n_gui_new_ctx(font);
    if (!gui) {
        n_log(LOG_ERR, "unable to create the GUI context");
        al_destroy_font(font);
        al_destroy_timer(fps_timer);
        al_destroy_display(display);
        al_destroy_event_queue(queue);
        return 1;
    }
    n_gui_set_display(gui, display);
    n_gui_set_display_size(gui, (float)WIDTH, (float)HEIGHT);
    /* the virtual canvas keeps the main display's layout resolution-independent;
       a native window is always drawn 1:1 in its own display */
    n_gui_set_virtual_size(gui, (float)WIDTH, (float)HEIGHT);

    /* REQUIRED before detaching: the context registers each native window's
       event source into this queue, so its events reach n_gui_process_event */
    n_gui_set_event_queue(gui, queue);

    /* control panel */
    {
        int win = n_gui_add_window(gui, "Control", 20.0f, 20.0f, 500.0f, 210.0f);
        n_gui_window_set_flags(gui, win, N_GUI_WIN_BTN_MINIMIZE);
        n_gui_add_button(gui, win, "Detach all panels", 10.0f, 10.0f, 235.0f, 26.0f,
                         N_GUI_SHAPE_ROUNDED, on_detach_all, NULL);
        n_gui_add_button(gui, win, "Attach all panels", 255.0f, 10.0f, 235.0f, 26.0f,
                         N_GUI_SHAPE_ROUNDED, on_attach_all, NULL);
        n_gui_add_button(gui, win, "Show all panels", 10.0f, 44.0f, 235.0f, 26.0f,
                         N_GUI_SHAPE_ROUNDED, on_show_all, NULL);
        n_gui_add_button(gui, win, "Save layout (F5)", 255.0f, 44.0f, 235.0f, 26.0f,
                         N_GUI_SHAPE_ROUNDED, on_save_layout, NULL);
        n_gui_add_button(gui, win, "Load layout (F9)", 255.0f, 78.0f, 235.0f, 26.0f,
                         N_GUI_SHAPE_ROUNDED, on_load_layout, NULL);
        lbl_status = n_gui_add_label(gui, win, "", 10.0f, 116.0f, 480.0f, 16.0f, N_GUI_ALIGN_LEFT);
        lbl_hint = n_gui_add_label(gui, win,
                                   "Close a native window: 'Show all panels' brings it back.",
                                   10.0f, 138.0f, 480.0f, 16.0f, N_GUI_ALIGN_LEFT);
        (void)lbl_hint;
    }

    /* Panel 1, the default: the window manager draws the title bar */
    {
        int win = build_panel("Mixer (OS chrome)", 20.0f, 250.0f, 300.0f, 220.0f,
                              N_GUI_DETACH_RESIZABLE, "Mixer");
        n_gui_add_label(gui, win, "Volume", 10.0f, 46.0f, 100.0f, 16.0f, N_GUI_ALIGN_LEFT);
        int sld = n_gui_add_slider(gui, win, 10.0f, 66.0f, 230.0f, 22.0f,
                                   0.0, 100.0, 65.0, N_GUI_SLIDER_PERCENT, on_slider_change, NULL);
        n_gui_slider_set_step(gui, sld, 5.0);
        n_gui_add_checkbox(gui, win, "Mute", 10.0f, 96.0f, 120.0f, 20.0f, 0, NULL, NULL);
        n_gui_add_progressbar(gui, win, 10.0f, 124.0f, 280.0f, 18.0f);
        n_gui_add_label(gui, win, "The OS draws this frame.",
                        10.0f, 150.0f, 280.0f, 16.0f, N_GUI_ALIGN_LEFT);
    }

    /* Panel 2, N_GUI_DETACH_OWN_CHROME: our own title bar inside a frameless
       native window, so the panel keeps its skin on the desktop */
    {
        int win = build_panel("Notes (N_GUI chrome)", 340.0f, 250.0f, 300.0f, 220.0f,
                              N_GUI_DETACH_RESIZABLE | N_GUI_DETACH_OWN_CHROME, "Notes");
        notes_txt = n_gui_add_textarea(gui, win, 10.0f, 46.0f, 280.0f, 100.0f, 1, 512, NULL, NULL);
        n_gui_textarea_set_text(gui, notes_txt,
                                "Copy and paste still reach\n"
                                "the right display.\n\n"
                                "Drag me by my own title bar.");
        n_gui_add_label(gui, win, "Frameless window, N_GUI chrome.",
                        10.0f, 152.0f, 280.0f, 16.0f, N_GUI_ALIGN_LEFT);
    }

    /* Panel 3, N_GUI_DETACH_SCALE_CONTENT: widgets follow the native window */
    {
        int win = build_panel("Files (scaling)", 660.0f, 250.0f, 280.0f, 220.0f,
                              N_GUI_DETACH_RESIZABLE | N_GUI_DETACH_SCALE_CONTENT, "Files");
        int lb = n_gui_add_listbox(gui, win, 10.0f, 46.0f, 260.0f, 120.0f,
                                   N_GUI_SELECT_SINGLE, NULL, NULL);
        n_gui_listbox_add_item(gui, lb, "n_gui.c");
        n_gui_listbox_add_item(gui, lb, "n_network.c");
        n_gui_listbox_add_item(gui, lb, "n_reactor.c");
        n_gui_listbox_add_item(gui, lb, "n_str.c");
        n_gui_listbox_add_item(gui, lb, "n_hash.c");
        n_gui_listbox_add_item(gui, lb, "n_list.c");
        n_gui_add_label(gui, win, "Widgets scale with it.",
                        10.0f, 172.0f, 260.0f, 16.0f, N_GUI_ALIGN_LEFT);
    }

    if (auto_detach) {
        on_detach_all(-1, NULL);
    }
    refresh_buttons();

    n_log(LOG_NOTICE, "ex_gui_detach: click a panel's button to give it its own OS window. ESC quits.");

    al_start_timer(fps_timer);
    int do_draw = 0;
    double started_at = al_get_time();
    al_flush_event_queue(queue);

    while (!DONE) {
        ALLEGRO_EVENT ev;
        al_wait_for_event(queue, &ev);

        /* Only the MAIN display's close ends the demo. A native window's close
           belongs to the panel that owns it and is handled by N_GUI, which
           routes it to on_panel_close. */
        if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE && ev.display.source == display) {
            DONE = 1;
        }
        if (ev.type == ALLEGRO_EVENT_KEY_DOWN) {
            if (ev.keyboard.keycode == ALLEGRO_KEY_ESCAPE)
                DONE = 1;
            else if (ev.keyboard.keycode == ALLEGRO_KEY_F5)
                on_save_layout(-1, NULL);
            else if (ev.keyboard.keycode == ALLEGRO_KEY_F9)
                on_load_layout(-1, NULL);
        }
        /* main display resize only, a native window's resize is N_GUI's job */
        if (ev.type == ALLEGRO_EVENT_DISPLAY_RESIZE && ev.display.source == display) {
            al_acknowledge_resize(display);
            n_gui_set_display_size(gui, (float)al_get_display_width(display),
                                   (float)al_get_display_height(display));
        }

        /* Every event goes through the GUI, which routes it to the display it
           came from. This is what makes a click inside a native window reach
           that window's widgets and nothing else. */
        n_gui_process_event(gui, ev);

        if (ev.type == ALLEGRO_EVENT_TIMER) {
            refresh_buttons();
            do_draw = 1;
            if (quit_after > 0.0 && (al_get_time() - started_at) >= quit_after) {
                n_log(LOG_NOTICE, "ex_gui_detach: -q reached, quitting");
                DONE = 1;
            }
        }

        if (do_draw && al_is_event_queue_empty(queue)) {
            /* main display: pop-up panels only */
            al_set_target_backbuffer(display);
            al_clear_to_color(al_map_rgb(28, 30, 36));
            n_gui_draw(gui);
            {
                ALLEGRO_TRANSFORM tf;
                al_identity_transform(&tf);
                if (gui->virtual_w > 0 && gui->gui_scale > 0) {
                    al_scale_transform(&tf, gui->gui_scale, gui->gui_scale);
                    al_translate_transform(&tf, gui->gui_offset_x, gui->gui_offset_y);
                }
                al_use_transform(&tf);
                al_draw_text(font, al_map_rgb(170, 175, 185), 10.0f, (float)HEIGHT - 18.0f, 0,
                             "Detach a panel to give it a real OS window | F5 save layout | F9 load layout | ESC quit");
                al_identity_transform(&tf);
                al_use_transform(&tf);
            }
            al_flip_display();

            /* native windows: renders and flips each one, and releases the ones
               whose close was requested. Called after the queue is drained, so
               no queued event can name a display that is about to go away. */
            n_gui_draw_detached(gui);

            do_draw = 0;
        }
    }

    /* the context owns every native window still open and destroys them here */
    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);
    al_destroy_timer(fps_timer);
    al_destroy_event_queue(queue);
    al_destroy_display(display);

    n_log(LOG_NOTICE, "ex_gui_detach: done");
    return 0;
}
