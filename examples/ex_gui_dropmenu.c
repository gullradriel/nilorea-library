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
 *@example ex_gui_dropmenu.c
 *@brief N_GUI_TYPE_DROPMENU entry, label, and clear regression test (headless).
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
static int click_tag = -1;
static int combo_selected = -1;

static void on_combo(int widget_id, int index, void* user_data) {
    (void)widget_id;
    (void)user_data;
    combo_selected = index;
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

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    }
}

static void on_entry(int widget_id, int entry_index, int tag, void* user_data) {
    (void)widget_id;
    (void)entry_index;
    (void)user_data;
    click_tag = tag;
}

/* the dropmenu label lives in N_GUI_DROPMENU_DATA (a public struct), so a headless
   test can read it back through the widget accessor to check the label setter */
static const char* dropmenu_label(N_GUI_CTX* gui, int id) {
    N_GUI_WIDGET* w = n_gui_get_widget(gui, id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data)
        return NULL;
    return ((N_GUI_DROPMENU_DATA*)w->data)->label;
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
    /* frameless so widget client coordinates line up with screen coordinates,
       which the mouse-event consumption checks below depend on */
    n_gui_window_set_flags(gui, win, N_GUI_WIN_FRAMELESS);

    int dm = n_gui_add_dropmenu(gui, win, "Capture", 0.0f, 0.0f, 120.0f, 28.0f, NULL, NULL);
    expect_true("dropmenu created", dm >= 0);
    expect_true("initial label is Capture", dropmenu_label(gui, dm) && strcmp(dropmenu_label(gui, dm), "Capture") == 0);
    expect_true("new dropmenu is empty", n_gui_dropmenu_get_count(gui, dm) == 0);

    /* entries carry a tag delivered to the click callback */
    expect_true("add entry 0", n_gui_dropmenu_add_entry(gui, dm, "Proxy", 11, on_entry, NULL) == 0);
    expect_true("add entry 1", n_gui_dropmenu_add_entry(gui, dm, "Browser", 22, on_entry, NULL) == 1);
    expect_true("count is 2", n_gui_dropmenu_get_count(gui, dm) == 2);

    /* the label setter (used to reuse one widget for different categories) */
    n_gui_dropmenu_set_label(gui, dm, "Auth & session");
    expect_true("label updated", dropmenu_label(gui, dm) && strcmp(dropmenu_label(gui, dm), "Auth & session") == 0);
    n_gui_dropmenu_set_label(gui, dm, NULL);
    expect_true("label cleared", dropmenu_label(gui, dm) && dropmenu_label(gui, dm)[0] == '\0');

    /* re-labelling and re-filling does not disturb the entry count */
    n_gui_dropmenu_set_label(gui, dm, "Recon");
    expect_true("count still 2 after relabel", n_gui_dropmenu_get_count(gui, dm) == 2);

    /* set_label on a non-dropmenu widget is a safe no-op */
    int btn = n_gui_add_button(gui, win, "Btn", 0.0f, 40.0f, 60.0f, 24.0f, N_GUI_SHAPE_ROUNDED, NULL, NULL);
    n_gui_dropmenu_set_label(gui, btn, "ignored");
    expect_true("set_label ignores non-dropmenu", dropmenu_label(gui, btn) == NULL);

    /* the open panel widens beyond a narrow trigger button so long entries are not
       truncated (a category button shows a short label but its entries can be long) */
    {
        int narrow = n_gui_add_dropmenu(gui, win, "Cap", 0.0f, 80.0f, 40.0f, 24.0f, NULL, NULL);
        float long_w;
        expect_true("narrow dropmenu created", narrow >= 0);
        /* a non-dropmenu widget has no panel width */
        expect_true("panel width 0 for non-dropmenu", n_gui_dropmenu_panel_width(gui, btn) == 0.0f);
        /* with no entries the panel is exactly the button width */
        expect_true("empty panel width equals button width", n_gui_dropmenu_panel_width(gui, narrow) == 40.0f);
        n_gui_dropmenu_add_entry(gui, narrow, "DOM Invader proxy", 1, on_entry, NULL);
        n_gui_dropmenu_add_entry(gui, narrow, "X", 2, on_entry, NULL);
        long_w = (float)al_get_text_width(font, "DOM Invader proxy");
        /* the panel grows past the 40px button to fit the longest entry text */
        expect_true("panel widens past the narrow button", n_gui_dropmenu_panel_width(gui, narrow) > 40.0f);
        expect_true("panel fits the widest entry", n_gui_dropmenu_panel_width(gui, narrow) >= long_w);
        n_gui_dropmenu_clear(gui, narrow);
    }

    /* a hover over an OPEN dropmenu panel is consumed and must not leak through to
       the widget sitting beneath the floating panel. The dropmenu (0,0,120,28) opens
       its panel downward; a button placed under that panel must stay un-hovered when
       the mouse is over the open panel, but hover normally when the menu is closed. */
    {
        N_GUI_WIDGET* dmw = n_gui_get_widget(gui, dm);
        int under = n_gui_add_button(gui, win, "Under", 70.0f, 32.0f, 100.0f, 40.0f, N_GUI_SHAPE_ROUNDED, NULL, NULL);
        N_GUI_WIDGET* underw = n_gui_get_widget(gui, under);
        ALLEGRO_EVENT ev;
        int consumed;
        expect_true("dropmenu has entries for the panel", n_gui_dropmenu_get_count(gui, dm) == 2);
        /* control: menu CLOSED, a hover over the under-button hovers it */
        gui->open_dropmenu_id = -1;
        ((N_GUI_DROPMENU_DATA*)dmw->data)->is_open = 0;
        underw->state &= ~N_GUI_STATE_HOVER;
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_MOUSE_AXES;
        ev.mouse.x = 100;
        ev.mouse.y = 40;
        n_gui_process_event(gui, ev);
        expect_true("button hovers when no menu is open", underw && (underw->state & N_GUI_STATE_HOVER) != 0);
        /* menu OPEN: clear the stale hover from the control step, then the same hover
           falls over the panel; it must be consumed and leave the button un-hovered */
        gui->open_dropmenu_id = dm;
        ((N_GUI_DROPMENU_DATA*)dmw->data)->is_open = 1;
        underw->state &= ~N_GUI_STATE_HOVER;
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_MOUSE_AXES;
        ev.mouse.x = 100;
        ev.mouse.y = 40;
        consumed = n_gui_process_event(gui, ev);
        expect_true("hover over the open panel is consumed", consumed == 1);
        expect_true("widget beneath the open panel is NOT hovered", underw && (underw->state & N_GUI_STATE_HOVER) == 0);
        /* close it again so the remaining checks are unaffected */
        gui->open_dropmenu_id = -1;
        ((N_GUI_DROPMENU_DATA*)dmw->data)->is_open = 0;
    }

    /* clearing drops every entry */
    n_gui_dropmenu_clear(gui, dm);
    expect_true("count is 0 after clear", n_gui_dropmenu_get_count(gui, dm) == 0);

    /* the entry click callback receives the entry tag */
    n_gui_dropmenu_add_entry(gui, dm, "Findings", 42, on_entry, NULL);
    on_entry(dm, 0, 42, NULL);
    expect_true("callback saw tag 42", click_tag == 42);

    /* wheel and keyboard move the open panel's highlight cursor. Rebuild a list long
       enough to scroll, open it, and drive the highlight without a real display: the
       events are dispatched with the pointer far from the panel so the open-panel wheel
       and key handlers (not the geometry-dependent hover path) do the work. */
    {
        N_GUI_WIDGET* dmw = n_gui_get_widget(gui, dm);
        N_GUI_DROPMENU_DATA* dmd = dmw ? (N_GUI_DROPMENU_DATA*)dmw->data : NULL;
        ALLEGRO_EVENT ev;
        int i;
        n_gui_dropmenu_clear(gui, dm);
        for (i = 0; i < 5; i++) {
            char name[16];
            snprintf(name, sizeof(name), "Item %d", i);
            n_gui_dropmenu_add_entry(gui, dm, name, 100 + i, on_entry, NULL);
        }
        expect_true("dropmenu has data", dmd != NULL);
        dmd->max_visible = 3; /* force a scrolling list (5 entries, 3 visible) */
        dmd->is_open = 1;
        dmd->highlight_index = -1;
        gui->open_dropmenu_id = dm;

        /* wheel down (dz -1) with the cursor away from the panel: first step reveals
           entry 0, and the highlight follows the wheel */
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_MOUSE_AXES;
        ev.mouse.x = 5000;
        ev.mouse.y = 5000;
        ev.mouse.dz = -1;
        expect_true("wheel over open menu is consumed", n_gui_process_event(gui, ev) == 1);
        expect_true("wheel down highlights the first entry", dmd->highlight_index == 0);
        n_gui_process_event(gui, ev);
        n_gui_process_event(gui, ev);
        expect_true("wheel steps the highlight down", dmd->highlight_index == 2);
        expect_true("panel scrolled to keep the highlight visible", dmd->scroll_offset == 0);
        n_gui_process_event(gui, ev); /* to 3, now below the 3-item window */
        expect_true("highlight advanced to 3", dmd->highlight_index == 3);
        expect_true("panel scrolled down to follow", dmd->scroll_offset == 1);

        /* Down / Up arrow keys move the same cursor */
        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_KEY_CHAR;
        ev.keyboard.keycode = ALLEGRO_KEY_DOWN;
        expect_true("Down key is consumed", n_gui_process_event(gui, ev) == 1);
        expect_true("Down key advances the highlight", dmd->highlight_index == 4);
        ev.keyboard.keycode = ALLEGRO_KEY_UP;
        n_gui_process_event(gui, ev);
        expect_true("Up key moves the highlight back", dmd->highlight_index == 3);
        ev.keyboard.keycode = ALLEGRO_KEY_HOME;
        n_gui_process_event(gui, ev);
        expect_true("Home jumps to the first entry", dmd->highlight_index == 0);
        ev.keyboard.keycode = ALLEGRO_KEY_END;
        n_gui_process_event(gui, ev);
        expect_true("End jumps to the last entry", dmd->highlight_index == 4);

        /* Enter activates the highlighted entry (tag 104) and closes the menu */
        click_tag = -1;
        ev.keyboard.keycode = ALLEGRO_KEY_ENTER;
        expect_true("Enter is consumed", n_gui_process_event(gui, ev) == 1);
        expect_true("Enter activates the highlighted entry", click_tag == 104);
        expect_true("Enter closes the menu", dmd->is_open == 0 && gui->open_dropmenu_id == -1);

        /* Escape on a reopened menu closes it without activating anything */
        dmd->is_open = 1;
        dmd->highlight_index = 2;
        gui->open_dropmenu_id = dm;
        click_tag = -1;
        ev.keyboard.keycode = ALLEGRO_KEY_ESCAPE;
        expect_true("Escape is consumed", n_gui_process_event(gui, ev) == 1);
        expect_true("Escape closes the menu", dmd->is_open == 0 && gui->open_dropmenu_id == -1);
        expect_true("Escape activates nothing", click_tag == -1);
    }

    /* the same wheel/key highlight drives a combobox dropdown (the parallel path) */
    {
        int cb = n_gui_add_combobox(gui, win, 0.0f, 40.0f, 160.0f, 28.0f, on_combo, NULL);
        N_GUI_WIDGET* cbw = n_gui_get_widget(gui, cb);
        N_GUI_COMBOBOX_DATA* cbd = cbw ? (N_GUI_COMBOBOX_DATA*)cbw->data : NULL;
        ALLEGRO_EVENT ev;
        int i;
        expect_true("combobox created", cb >= 0 && cbd != NULL);
        for (i = 0; i < 5; i++) {
            char name[16];
            snprintf(name, sizeof(name), "Opt %d", i);
            n_gui_combobox_add_item(gui, cb, name);
        }
        n_gui_combobox_set_selected(gui, cb, 2);
        /* open the panel the way the click handler does: highlight starts on the value */
        cbd->max_visible = 3;
        cbd->is_open = 1;
        cbd->highlight_index = cbd->selected_index;
        gui->open_combobox_id = cb;

        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_MOUSE_AXES;
        ev.mouse.x = 5000;
        ev.mouse.y = 5000;
        ev.mouse.dz = -1;
        expect_true("combobox wheel is consumed", n_gui_process_event(gui, ev) == 1);
        expect_true("wheel steps down from the selected value", cbd->highlight_index == 3);

        memset(&ev, 0, sizeof(ev));
        ev.type = ALLEGRO_EVENT_KEY_CHAR;
        ev.keyboard.keycode = ALLEGRO_KEY_UP;
        n_gui_process_event(gui, ev);
        expect_true("Up moves the combobox highlight", cbd->highlight_index == 2);
        ev.keyboard.keycode = ALLEGRO_KEY_HOME;
        n_gui_process_event(gui, ev);
        expect_true("Home moves to the first option", cbd->highlight_index == 0);

        /* Enter commits the highlighted option (selects it and fires on_select) */
        combo_selected = -1;
        ev.keyboard.keycode = ALLEGRO_KEY_ENTER;
        expect_true("combobox Enter is consumed", n_gui_process_event(gui, ev) == 1);
        expect_true("Enter selects the highlighted option", n_gui_combobox_get_selected(gui, cb) == 0);
        expect_true("Enter fires on_select", combo_selected == 0);
        expect_true("Enter closes the combobox", cbd->is_open == 0 && gui->open_combobox_id == -1);
    }

    /* global shape mode: round vs square GUI toggle (setter/getter round-trip) */
    n_gui_set_shape_mode(gui, N_GUI_SHAPE_RECT);
    expect_true("shape mode set to rect", n_gui_get_shape_mode(gui) == N_GUI_SHAPE_RECT);
    n_gui_set_shape_mode(gui, N_GUI_SHAPE_ROUNDED);
    expect_true("shape mode set to rounded", n_gui_get_shape_mode(gui) == N_GUI_SHAPE_ROUNDED);
    expect_true("get_shape_mode NULL-safe", n_gui_get_shape_mode(NULL) == N_GUI_SHAPE_ROUNDED);

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);

    if (failures) {
        n_log(LOG_ERR, "ex_gui_dropmenu: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_gui_dropmenu: all checks passed");
    return 0;
}
