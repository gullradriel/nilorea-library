/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@example ex_gui.c
 *@brief Nilorea Library GUI system demo - all widget types including dropdown menus,
 * show/hide windows, auto-sizing, scrollbars, resizable windows, and bitmap skinning.
 *
 * Comprehensive demo showcasing every GUI widget available in the nilorea-library
 * n_gui module: buttons (regular + toggle), sliders, text areas, checkboxes,
 * scrollbars, listboxes (single + multi select), radio lists, comboboxes,
 * labels, hyperlinks, images, toggle-button radio groups, dropdown menus with
 * dynamic entries, automatic window sizing, auto-scrollbars, resizable windows,
 * and optional bitmap skinning for windows and widgets.
 *
 *@author Castagnier Mickael
 *@version 4.0
 *@date 02/03/2026
 */

#define WIDTH 1280
#define HEIGHT 900

#define ALLEGRO_UNSTABLE 1
#define HAVE_CJSON 1

#include "nilorea/n_gui.h"
#include "cJSON.h"

ALLEGRO_DISPLAY* display = NULL;

int DONE = 0,
    getoptret = 0,
    log_level = LOG_ERR;

static char theme_file[512] = "";

/* ---- application state driven by toggle buttons ---- */
static int player_mode = 0;
static int height_mode = 0;
static int show_grid = 0;
static int smooth_height = 0;
static int ghost_enabled = 0;
static int current_proj = 0; /* 0=Classic, 1=Iso, 2=Military, 3=Staggered */

static const char* proj_names[] = {"Classic 2:1", "True Isometric", "Military", "Staggered"};

/* widget ids for status display */
static int lbl_status = -1;
static int lbl_proj = -1;

/* projection toggle button ids for radio group */
static int proj_btn_ids[4] = {-1, -1, -1, -1};

/* window ids (needed for show/hide dropdown) */
#define NUM_WINDOWS 16
static int win_ids[NUM_WINDOWS];
static const char* win_names[NUM_WINDOWS];

/* dropdown menu widget id for show/hide */
static int dropmenu_windows_id = -1;

/* callback: button click */
void on_button_click(int widget_id, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Button %d clicked!", widget_id);
}

/* callback: slider change */
void on_slider_change(int widget_id, double value, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Slider %d value: %.2f", widget_id, value);
}

/* callback: checkbox toggle */
void on_checkbox_toggle(int widget_id, int checked, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Checkbox %d: %s", widget_id, checked ? "checked" : "unchecked");
}

/* callback: textarea change */
void on_text_change(int widget_id, const char* text, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Textarea %d: \"%s\"", widget_id, text);
}

/* callback: scrollbar scroll */
void on_scroll(int widget_id, double pos, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Scrollbar %d pos: %.2f", widget_id, pos);
}

/* callback: quit button */
void on_quit_click(int widget_id, void* user_data) {
    (void)widget_id;
    int* done = (int*)user_data;
    *done = 1;
}

/* callback: login button */
void on_login_click(int widget_id, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Login button %d clicked!", widget_id);
}

/* callback: listbox selection */
void on_listbox_select(int widget_id, int index, int selected, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Listbox %d: item %d %s", widget_id, index, selected ? "selected" : "deselected");
}

/* callback: radiolist selection */
void on_radiolist_select(int widget_id, int index, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Radiolist %d: selected item %d", widget_id, index);
}

/* callback: combobox selection */
void on_combobox_select(int widget_id, int index, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Combobox %d: selected item %d", widget_id, index);
}

/* callback: label link click */
void on_link_click(int widget_id, const char* link, void* user_data) {
    (void)user_data;
    n_log(LOG_NOTICE, "Link %d clicked: %s", widget_id, link);
}

/* ---- toggle button callbacks ---- */

void on_player_toggle(int widget_id, void* user_data) {
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    player_mode = n_gui_button_is_toggled(gui, widget_id);
    n_log(LOG_NOTICE, "Player mode: %s", player_mode ? "ON" : "OFF");
}

void on_height_toggle(int widget_id, void* user_data) {
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    height_mode = n_gui_button_is_toggled(gui, widget_id);
    n_log(LOG_NOTICE, "Height mode: %s", height_mode ? "ON" : "OFF");
}

void on_grid_toggle(int widget_id, void* user_data) {
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    show_grid = n_gui_button_is_toggled(gui, widget_id);
    n_log(LOG_NOTICE, "Grid: %s", show_grid ? "ON" : "OFF");
}

void on_smooth_toggle(int widget_id, void* user_data) {
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    smooth_height = n_gui_button_is_toggled(gui, widget_id);
    n_log(LOG_NOTICE, "Height rendering: %s", smooth_height ? "SMOOTH" : "CUT");
}

void on_ghost_toggle(int widget_id, void* user_data) {
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    ghost_enabled = n_gui_button_is_toggled(gui, widget_id);
    n_log(LOG_NOTICE, "DR Ghost: %s", ghost_enabled ? "ON" : "OFF");
}

/* projection buttons: act as a radio group using toggle buttons */
void on_proj_select(int widget_id, void* user_data) {
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    for (int i = 0; i < 4; i++) {
        if (proj_btn_ids[i] == widget_id) {
            current_proj = i;
            n_gui_button_set_toggled(gui, proj_btn_ids[i], 1);
        } else {
            n_gui_button_set_toggled(gui, proj_btn_ids[i], 0);
        }
    }
    n_log(LOG_NOTICE, "Projection: %s", proj_names[current_proj]);
}

/* ---- dropdown menu: show/hide windows ---- */

/* callback when a window toggle entry is clicked in the dropdown */
void on_window_toggle_click(int widget_id, int entry_index, int tag, void* user_data) {
    (void)widget_id;
    (void)entry_index;
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    n_gui_toggle_window(gui, tag);
    n_log(LOG_NOTICE, "Toggled window id %d", tag);
}

/* callback to rebuild dynamic entries when the dropdown opens */
void on_windows_menu_open(int widget_id, void* user_data) {
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;

    /* clear old dynamic entries */
    n_gui_dropmenu_clear_dynamic(gui, widget_id);

    /* add one dynamic entry per window showing its show/hide state */
    for (int i = 0; i < NUM_WINDOWS; i++) {
        if (win_ids[i] < 0) continue;
        char buf[N_GUI_ID_MAX];
        int is_open = n_gui_window_is_open(gui, win_ids[i]);
        snprintf(buf, sizeof(buf), "[%s] %s", is_open ? "X" : " ", win_names[i]);
        n_gui_dropmenu_add_dynamic_entry(gui, widget_id, buf, win_ids[i],
                                         on_window_toggle_click, gui);
    }
}

int main(int argc, char* argv[]) {
    set_log_level(LOG_NOTICE);

    n_log(LOG_NOTICE, "%s is starting ...", argv[0]);

    /* allegro 5 + addons loading */
    if (!al_init()) {
        n_abort("Could not init Allegro.\n");
    }
    if (!al_install_audio()) {
        n_log(LOG_ERR, "Unable to initialize audio addon");
    }
    if (!al_init_acodec_addon()) {
        n_log(LOG_ERR, "Unable to initialize audio codec addon");
    }
    if (!al_init_image_addon()) {
        n_abort("Unable to initialize image addon\n");
    }
    if (!al_init_primitives_addon()) {
        n_abort("Unable to initialize primitives addon\n");
    }
    if (!al_init_font_addon()) {
        n_abort("Unable to initialize font addon\n");
    }
    if (!al_init_ttf_addon()) {
        n_abort("Unable to initialize ttf_font addon\n");
    }
    if (!al_install_keyboard()) {
        n_abort("Unable to initialize keyboard handler\n");
    }
    if (!al_install_mouse()) {
        n_abort("Unable to initialize mouse handler\n");
    }

    /* parse command line */
    char ver_str[128] = "";
    while ((getoptret = getopt(argc, argv, "hvV:L:t:")) != EOF) {
        switch (getoptret) {
            case 'h':
                n_log(LOG_NOTICE, "\n    %s -h help -v version -V DEBUGLEVEL -L logfile -t theme.json", argv[0]);
                exit(TRUE);
            case 'v':
                sprintf(ver_str, "%s %s", __DATE__, __TIME__);
                exit(TRUE);
                break;
            case 'V':
                if (!strncmp("NOTICE", optarg, 6)) {
                    log_level = LOG_INFO;
                } else if (!strncmp("VERBOSE", optarg, 7)) {
                    log_level = LOG_NOTICE;
                } else if (!strncmp("ERROR", optarg, 5)) {
                    log_level = LOG_ERR;
                } else if (!strncmp("DEBUG", optarg, 5)) {
                    log_level = LOG_DEBUG;
                } else {
                    n_log(LOG_ERR, "%s is not a valid log level", optarg);
                    exit(FALSE);
                }
                set_log_level(log_level);
                break;
            case 'L':
                set_log_file(optarg);
                break;
            case 't':
                strncpy(theme_file, optarg, sizeof(theme_file) - 1);
                theme_file[sizeof(theme_file) - 1] = '\0';
                break;
            default:
                n_log(LOG_ERR, "\n    %s -h help -v version -V DEBUGLEVEL -L logfile -t theme.json", argv[0]);
                exit(FALSE);
        }
    }

    ALLEGRO_EVENT_QUEUE* event_queue = al_create_event_queue();
    if (!event_queue) {
        n_abort("Failed to create event queue!\n");
    }

    al_set_new_display_flags(ALLEGRO_OPENGL | ALLEGRO_WINDOWED | ALLEGRO_RESIZABLE);
    display = al_create_display(WIDTH, HEIGHT);
    if (!display) {
        n_abort("Unable to create display\n");
    }
    al_set_window_title(display, "Nilorea GUI Demo - All Widgets + Dropdown Menus");

    ALLEGRO_TIMER* fps_timer = al_create_timer(1.0 / 60.0);

    al_register_event_source(event_queue, al_get_display_event_source(display));
    al_register_event_source(event_queue, al_get_timer_event_source(fps_timer));
    al_register_event_source(event_queue, al_get_keyboard_event_source());
    al_register_event_source(event_queue, al_get_mouse_event_source());
    al_hide_mouse_cursor(display);

    /* load a built-in font */
    ALLEGRO_FONT* font = al_create_builtin_font();
    if (!font) {
        n_abort("Unable to create builtin font\n");
    }

    /* ---- create the GUI ---- */
    N_GUI_CTX* gui = n_gui_new_ctx(font);

    /* set display size for global scrollbars (shown when GUI exceeds display) */
    n_gui_set_display_size(gui, (float)WIDTH, (float)HEIGHT);

    /* set display pointer for clipboard operations (copy/paste in text areas) */
    n_gui_set_display(gui, display);

    /* enable virtual canvas: widget coordinates stay in WIDTH x HEIGHT space,
     * the GUI scales uniformly to fit the actual display size */
    n_gui_set_virtual_size(gui, (float)WIDTH, (float)HEIGHT);

    /* detect and apply DPI scale */
    float dpi = n_gui_detect_dpi_scale(gui, display);
    n_log(LOG_NOTICE, "DPI scale: %.2f", dpi);

    /* load theme file if specified on command line */
    if (theme_file[0]) {
        if (n_gui_load_theme_json(gui, theme_file) == 0) {
            n_log(LOG_NOTICE, "Loaded theme: %s", theme_file);
        } else {
            n_log(LOG_ERR, "Failed to load theme: %s", theme_file);
        }
    }

    int win_idx = 0;

    /* --- Window 0: Menu Bar (frameless, fixed position) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Menu", 0, 0, (float)WIDTH, 28);
    win_names[win_idx] = "Menu";
    int win_menu = win_ids[win_idx];
    n_gui_window_set_flags(gui, win_menu, N_GUI_WIN_FRAMELESS | N_GUI_WIN_FIXED_POSITION);
    win_idx++;

    /* dropdown: Windows (show/hide) */
    dropmenu_windows_id = n_gui_add_dropmenu(gui, win_menu,
                                             "Windows", 10, 2, 120, 22,
                                             on_windows_menu_open, gui);

    /* dropdown: File (static entries) */
    int dm_file = n_gui_add_dropmenu(gui, win_menu,
                                     "File", 140, 2, 100, 22, NULL, NULL);
    n_gui_dropmenu_add_entry(gui, dm_file, "New", 0, NULL, NULL);
    n_gui_dropmenu_add_entry(gui, dm_file, "Open", 1, NULL, NULL);
    n_gui_dropmenu_add_entry(gui, dm_file, "Save", 2, NULL, NULL);

    /* --- Window 1: Buttons --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Buttons", 20, 60, 300, 280);
    win_names[win_idx] = "Buttons";
    int win1 = win_ids[win_idx];
    win_idx++;

    n_gui_add_button(gui, win1, "Click Me!", 20, 20, 120, 36, N_GUI_SHAPE_RECT, on_button_click, NULL);
    n_gui_add_button(gui, win1, "Rounded", 160, 20, 120, 36, N_GUI_SHAPE_ROUNDED, on_button_click, NULL);
    n_gui_add_button(gui, win1, "Quit", 20, 80, 260, 40, N_GUI_SHAPE_ROUNDED, on_quit_click, &DONE);

    /* custom themed button */
    int btn_custom = n_gui_add_button(gui, win1, "Custom Theme", 20, 140, 260, 36, N_GUI_SHAPE_ROUNDED, on_button_click, NULL);
    N_GUI_THEME red_theme = n_gui_make_theme(
        al_map_rgba(120, 30, 30, 230), al_map_rgba(160, 40, 40, 240), al_map_rgba(200, 60, 60, 255),
        al_map_rgba(200, 80, 80, 255), al_map_rgba(230, 100, 100, 255), al_map_rgba(255, 120, 120, 255),
        al_map_rgba(255, 220, 220, 255), al_map_rgba(255, 255, 255, 255), al_map_rgba(255, 255, 255, 255),
        2.0f, 8.0f, 8.0f);
    n_gui_set_widget_theme(gui, btn_custom, red_theme);

    {
        int btn_dis = n_gui_add_button(gui, win1, "Disabled Button", 20, 196, 260, 30, N_GUI_SHAPE_RECT, on_button_click, NULL);
        n_gui_set_widget_enabled(gui, btn_dis, 0);
    }

    /* --- Window 2: Sliders (resizable) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Sliders (resizable)", 340, 60, 320, 200);
    win_names[win_idx] = "Sliders (resizable)";
    int win2 = win_ids[win_idx];
    n_gui_window_set_flags(gui, win2, N_GUI_WIN_RESIZABLE);
    win_idx++;

    n_gui_add_slider(gui, win2, 20, 20, 180, 24, 0.0, 100.0, 50.0, N_GUI_SLIDER_VALUE, on_slider_change, NULL);
    n_gui_add_slider(gui, win2, 20, 60, 180, 24, 0.0, 100.0, 25.0, N_GUI_SLIDER_PERCENT, on_slider_change, NULL);
    {
        int sld_dis = n_gui_add_slider(gui, win2, 20, 100, 180, 24, -50.0, 50.0, 0.0, N_GUI_SLIDER_VALUE, on_slider_change, NULL);
        n_gui_set_widget_enabled(gui, sld_dis, 0);
    }

    /* vertical sliders */
    n_gui_add_vslider(gui, win2, 240, 10, 24, 120, 0.0, 100.0, 75.0, N_GUI_SLIDER_VALUE, on_slider_change, NULL);
    n_gui_add_vslider(gui, win2, 275, 10, 24, 120, 0.0, 100.0, 40.0, N_GUI_SLIDER_PERCENT, on_slider_change, NULL);

    /* --- Window 3: Text Areas --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Text Areas", 20, 360, 350, 280);
    win_names[win_idx] = "Text Areas";
    int win3 = win_ids[win_idx];
    win_idx++;

    n_gui_add_textarea(gui, win3, 20, 20, 310, 28, 0, 64, on_text_change, NULL);
    int ta_multi = n_gui_add_textarea(gui, win3, 20, 60, 310, 160, 1, 512, on_text_change, NULL);
    n_gui_textarea_set_text(gui, ta_multi, "Hello! This is a\nmultiline text area.\nType here...");

    /* --- Window 4: Checkboxes (auto-sized) --- */
    win_ids[win_idx] = n_gui_add_window_auto(gui, "Checkboxes (auto)", 680, 60);
    win_names[win_idx] = "Checkboxes (auto)";
    int win4 = win_ids[win_idx];
    win_idx++;

    n_gui_add_checkbox(gui, win4, "Enable sound", 20, 20, 260, 28, 1, on_checkbox_toggle, NULL);
    n_gui_add_checkbox(gui, win4, "Fullscreen", 20, 56, 260, 28, 0, on_checkbox_toggle, NULL);
    {
        int cb_fps = n_gui_add_checkbox(gui, win4, "Show FPS (disabled)", 20, 92, 260, 28, 1, on_checkbox_toggle, NULL);
        n_gui_set_widget_enabled(gui, cb_fps, 0);
    }
    {
        int cb_vsync = n_gui_add_checkbox(gui, win4, "VSync (hidden)", 20, 128, 260, 28, 0, on_checkbox_toggle, NULL);
        n_gui_set_widget_visible(gui, cb_vsync, 0);
    }
    /* auto-size to fit the widgets */
    n_gui_window_autosize(gui, win4);

    /* --- Window 5: Scrollbars --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Scrollbars", 390, 360, 320, 280);
    win_names[win_idx] = "Scrollbars";
    int win5 = win_ids[win_idx];
    win_idx++;

    n_gui_add_scrollbar(gui, win5, 20, 20, 260, 20, N_GUI_SCROLLBAR_H, N_GUI_SHAPE_ROUNDED,
                        1000.0, 200.0, on_scroll, NULL);
    n_gui_add_scrollbar(gui, win5, 280, 20, 20, 220, N_GUI_SCROLLBAR_V, N_GUI_SHAPE_ROUNDED,
                        500.0, 100.0, on_scroll, NULL);
    n_gui_add_scrollbar(gui, win5, 20, 60, 260, 16, N_GUI_SCROLLBAR_H, N_GUI_SHAPE_RECT,
                        2000.0, 500.0, on_scroll, NULL);
    n_gui_add_scrollbar(gui, win5, 20, 100, 260, 16, N_GUI_SCROLLBAR_H, N_GUI_SHAPE_RECT,
                        100.0, 100.0, on_scroll, NULL);

    /* --- Window 6: Listbox (single select) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Listbox (Single)", 730, 360, 260, 240);
    win_names[win_idx] = "Listbox (Single)";
    int win6 = win_ids[win_idx];
    win_idx++;

    int lb_single = n_gui_add_listbox(gui, win6, 10, 10, 240, 180, N_GUI_SELECT_SINGLE, on_listbox_select, NULL);
    n_gui_listbox_add_item(gui, lb_single, "Apple");
    n_gui_listbox_add_item(gui, lb_single, "Banana");
    n_gui_listbox_add_item(gui, lb_single, "Cherry");
    n_gui_listbox_add_item(gui, lb_single, "Date");
    n_gui_listbox_add_item(gui, lb_single, "Elderberry");
    n_gui_listbox_add_item(gui, lb_single, "Fig");
    n_gui_listbox_add_item(gui, lb_single, "Grape");
    n_gui_listbox_add_item(gui, lb_single, "Honeydew");
    n_gui_listbox_add_item(gui, lb_single, "Kiwi");
    n_gui_listbox_add_item(gui, lb_single, "Lemon");
    n_gui_listbox_add_item(gui, lb_single, "Mango");
    n_gui_listbox_add_item(gui, lb_single, "Nectarine");
    n_gui_listbox_add_item(gui, lb_single, "Orange");
    n_gui_listbox_add_item(gui, lb_single, "Papaya");
    n_gui_listbox_add_item(gui, lb_single, "Quince");
    n_gui_listbox_set_selected(gui, lb_single, 2, 1);

    /* --- Window 7: Listbox (multi select) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Listbox (Multi)", 1000, 360, 260, 240);
    win_names[win_idx] = "Listbox (Multi)";
    int win7 = win_ids[win_idx];
    win_idx++;

    int lb_multi = n_gui_add_listbox(gui, win7, 10, 10, 240, 180, N_GUI_SELECT_MULTIPLE, on_listbox_select, NULL);
    n_gui_listbox_add_item(gui, lb_multi, "Red");
    n_gui_listbox_add_item(gui, lb_multi, "Green");
    n_gui_listbox_add_item(gui, lb_multi, "Blue");
    n_gui_listbox_add_item(gui, lb_multi, "Yellow");
    n_gui_listbox_add_item(gui, lb_multi, "Cyan");
    n_gui_listbox_add_item(gui, lb_multi, "Magenta");
    n_gui_listbox_add_item(gui, lb_multi, "White");
    n_gui_listbox_add_item(gui, lb_multi, "Black");
    n_gui_listbox_add_item(gui, lb_multi, "Orange");
    n_gui_listbox_add_item(gui, lb_multi, "Pink");
    n_gui_listbox_add_item(gui, lb_multi, "Brown");
    n_gui_listbox_add_item(gui, lb_multi, "Purple");
    n_gui_listbox_set_selected(gui, lb_multi, 0, 1);
    n_gui_listbox_set_selected(gui, lb_multi, 2, 1);

    /* --- Window 8: Radiolist --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Radio List", 730, 60, 260, 230);
    win_names[win_idx] = "Radio List";
    int win8 = win_ids[win_idx];
    win_idx++;

    int radio1 = n_gui_add_radiolist(gui, win8, 10, 10, 240, 170, on_radiolist_select, NULL);
    n_gui_radiolist_add_item(gui, radio1, "Option A");
    n_gui_radiolist_add_item(gui, radio1, "Option B");
    n_gui_radiolist_add_item(gui, radio1, "Option C");
    n_gui_radiolist_add_item(gui, radio1, "Option D");
    n_gui_radiolist_add_item(gui, radio1, "Option E");
    n_gui_radiolist_add_item(gui, radio1, "Option F");
    n_gui_radiolist_add_item(gui, radio1, "Option G");
    n_gui_radiolist_add_item(gui, radio1, "Option H");
    n_gui_radiolist_add_item(gui, radio1, "Option I");
    n_gui_radiolist_add_item(gui, radio1, "Option J");
    n_gui_radiolist_set_selected(gui, radio1, 1);

    /* --- Window 9: Combobox + Labels + Image --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Combo / Labels / Image", 1000, 60, 260, 280);
    win_names[win_idx] = "Combo / Labels / Image";
    int win9 = win_ids[win_idx];
    win_idx++;

    /* combobox */
    int combo1 = n_gui_add_combobox(gui, win9, 10, 10, 240, 24, on_combobox_select, NULL);
    n_gui_combobox_add_item(gui, combo1, "Tiny");
    n_gui_combobox_add_item(gui, combo1, "Small");
    n_gui_combobox_add_item(gui, combo1, "Medium");
    n_gui_combobox_add_item(gui, combo1, "Large");
    n_gui_combobox_add_item(gui, combo1, "Extra Large");
    n_gui_combobox_add_item(gui, combo1, "XXL");
    n_gui_combobox_add_item(gui, combo1, "XXXL");
    n_gui_combobox_add_item(gui, combo1, "Huge");
    n_gui_combobox_add_item(gui, combo1, "Gigantic");
    n_gui_combobox_add_item(gui, combo1, "Colossal");
    n_gui_combobox_set_selected(gui, combo1, 2);

    /* static labels */
    n_gui_add_label(gui, win9, "Left aligned label", 10, 50, 240, 20, N_GUI_ALIGN_LEFT);
    n_gui_add_label(gui, win9, "Centered label", 10, 74, 240, 20, N_GUI_ALIGN_CENTER);
    n_gui_add_label(gui, win9, "Right aligned label", 10, 98, 240, 20, N_GUI_ALIGN_RIGHT);
    n_gui_add_label(gui, win9, "Justified: this text spreads across width evenly", 10, 122, 240, 20, N_GUI_ALIGN_JUSTIFIED);

    /* hyperlink label */
    n_gui_add_label_link(gui, win9,
                         "Click this link!", "https://example.com",
                         10, 150, 240, 20, N_GUI_ALIGN_LEFT,
                         on_link_click, NULL);

    /* image widgets with different scale modes */
    ALLEGRO_BITMAP* img_192 = al_load_bitmap("DATAS/img/android-chrome-192x192.png");
    ALLEGRO_BITMAP* img_32 = al_load_bitmap("DATAS/img/favicon-32x32.png");
    n_gui_add_label(gui, win9, "Image FIT:", 10, 180, 80, 16, N_GUI_ALIGN_LEFT);
    n_gui_add_image(gui, win9, 10, 198, 80, 80, img_192, N_GUI_IMAGE_FIT);
    n_gui_add_label(gui, win9, "STRETCH:", 100, 180, 70, 16, N_GUI_ALIGN_LEFT);
    n_gui_add_image(gui, win9, 100, 198, 70, 80, img_192, N_GUI_IMAGE_STRETCH);
    n_gui_add_label(gui, win9, "CENTER:", 180, 180, 70, 16, N_GUI_ALIGN_LEFT);
    n_gui_add_image(gui, win9, 180, 198, 70, 80, img_32, N_GUI_IMAGE_CENTER);

    /* --- Window 10: Mode Toggles (stays clicked/unclicked) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Mode Toggles", 20, 660, 300, 230);
    win_names[win_idx] = "Mode Toggles";
    int win10 = win_ids[win_idx];
    win_idx++;

    n_gui_add_label(gui, win10, "Click or press key to toggle on/off:", 20, 10, 260, 20, N_GUI_ALIGN_LEFT);

    {
        int btn_id;
        btn_id = n_gui_add_toggle_button(gui, win10, "Player Mode [Ctrl+P]",
                                         20, 40, 260, 32, N_GUI_SHAPE_ROUNDED, 0,
                                         on_player_toggle, gui);
        n_gui_button_set_keycode(gui, btn_id, ALLEGRO_KEY_P, ALLEGRO_KEYMOD_CTRL);

        btn_id = n_gui_add_toggle_button(gui, win10, "Height Mode [Ctrl+H]",
                                         20, 80, 260, 32, N_GUI_SHAPE_ROUNDED, 0,
                                         on_height_toggle, gui);
        n_gui_button_set_keycode(gui, btn_id, ALLEGRO_KEY_H, ALLEGRO_KEYMOD_CTRL);

        btn_id = n_gui_add_toggle_button(gui, win10, "Grid [Shift+G]",
                                         20, 120, 260, 32, N_GUI_SHAPE_ROUNDED, 0,
                                         on_grid_toggle, gui);
        n_gui_button_set_keycode(gui, btn_id, ALLEGRO_KEY_G, ALLEGRO_KEYMOD_SHIFT);

        btn_id = n_gui_add_toggle_button(gui, win10, "Smooth Height [Shift+V]",
                                         20, 160, 260, 32, N_GUI_SHAPE_ROUNDED, 0,
                                         on_smooth_toggle, gui);
        n_gui_button_set_keycode(gui, btn_id, ALLEGRO_KEY_V, ALLEGRO_KEYMOD_SHIFT);
    }

    /* --- Window 11: Projection Selection (toggle radio group) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Projection", 340, 660, 280, 230);
    win_names[win_idx] = "Projection";
    int win11 = win_ids[win_idx];
    win_idx++;

    n_gui_add_label(gui, win11, "Select projection:", 20, 10, 240, 20, N_GUI_ALIGN_LEFT);

    proj_btn_ids[0] = n_gui_add_toggle_button(gui, win11, "Classic 2:1 [Ctrl+F1]",
                                              20, 40, 240, 30, N_GUI_SHAPE_ROUNDED, 1,
                                              on_proj_select, gui);
    n_gui_button_set_keycode(gui, proj_btn_ids[0], ALLEGRO_KEY_F1, ALLEGRO_KEYMOD_CTRL);
    proj_btn_ids[1] = n_gui_add_toggle_button(gui, win11, "True Isometric [Shift+F2]",
                                              20, 78, 240, 30, N_GUI_SHAPE_ROUNDED, 0,
                                              on_proj_select, gui);
    n_gui_button_set_keycode(gui, proj_btn_ids[1], ALLEGRO_KEY_F2, ALLEGRO_KEYMOD_SHIFT);
    proj_btn_ids[2] = n_gui_add_toggle_button(gui, win11, "Military [Ctrl+F3]",
                                              20, 116, 240, 30, N_GUI_SHAPE_ROUNDED, 0,
                                              on_proj_select, gui);
    n_gui_button_set_keycode(gui, proj_btn_ids[2], ALLEGRO_KEY_F3, ALLEGRO_KEYMOD_CTRL);
    proj_btn_ids[3] = n_gui_add_toggle_button(gui, win11, "Staggered [Shift+F4]",
                                              20, 154, 240, 30, N_GUI_SHAPE_ROUNDED, 0,
                                              on_proj_select, gui);
    n_gui_button_set_keycode(gui, proj_btn_ids[3], ALLEGRO_KEY_F4, ALLEGRO_KEYMOD_SHIFT);

    /* custom theme: green for active/toggled state */
    N_GUI_THEME green_theme = n_gui_make_theme(
        al_map_rgba(40, 60, 40, 230), al_map_rgba(50, 80, 50, 240), al_map_rgba(30, 120, 30, 255),
        al_map_rgba(60, 100, 60, 255), al_map_rgba(70, 140, 70, 255), al_map_rgba(40, 160, 40, 255),
        al_map_rgba(200, 255, 200, 255), al_map_rgba(255, 255, 255, 255), al_map_rgba(255, 255, 255, 255),
        2.0f, 8.0f, 8.0f);
    for (int i = 0; i < 4; i++) {
        n_gui_set_widget_theme(gui, proj_btn_ids[i], green_theme);
    }

    /* --- Window 12: Status Display --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Toggle Status", 640, 660, 620, 230);
    win_names[win_idx] = "Toggle Status";
    int win12 = win_ids[win_idx];
    win_idx++;

    lbl_status = n_gui_add_label(gui, win12, "Waiting...", 20, 10, 580, 20, N_GUI_ALIGN_LEFT);
    lbl_proj = n_gui_add_label(gui, win12, "Projection: Classic 2:1", 20, 40, 580, 20, N_GUI_ALIGN_LEFT);

    n_gui_add_label(gui, win12, "Toggle buttons stay visually pressed when ON.", 20, 80, 580, 20, N_GUI_ALIGN_LEFT);
    n_gui_add_label(gui, win12, "Click again to turn OFF. Projection uses radio-group pattern.", 20, 100, 580, 20, N_GUI_ALIGN_LEFT);
    n_gui_add_label(gui, win12, "The state persists: no need to hold the mouse button down.", 20, 120, 580, 20, N_GUI_ALIGN_LEFT);

    /* --- Window 13: Scrollable Content (auto scrollbar demo) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Scrollable (auto scrollbar)", 390, 270, 280, 80);
    win_names[win_idx] = "Scrollable (auto scrollbar)";
    int win_scroll = win_ids[win_idx];
    n_gui_window_set_flags(gui, win_scroll, N_GUI_WIN_AUTO_SCROLLBAR | N_GUI_WIN_RESIZABLE);
    win_idx++;

    /* add many labels to overflow the window height */
    for (int i = 0; i < 20; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Scrollable line %d - content that extends beyond window", i + 1);
        n_gui_add_label(gui, win_scroll, buf, 10, (float)(i * 22), 400, 20, N_GUI_ALIGN_LEFT);
    }

    /* --- Window 14: Bitmap Skinning Demo ---
     * Demonstrates optional bitmap overlays on widgets and windows.
     * Placeholder bitmaps are created programmatically as colored rectangles
     * with gradients to show where the skinning takes effect.
     * In a real application, use al_load_bitmap() with actual image files.
     * Bitmaps are NOT owned by N_GUI — caller must keep them alive while
     * the GUI uses them and destroy them AFTER n_gui_destroy_ctx(). */
    ALLEGRO_BITMAP* skin_win_bg = NULL;
    ALLEGRO_BITMAP* skin_win_tb = NULL;
    ALLEGRO_BITMAP* skin_sld_track = NULL;
    ALLEGRO_BITMAP* skin_sld_fill = NULL;
    ALLEGRO_BITMAP* skin_sld_handle = NULL;
    ALLEGRO_BITMAP* skin_sld_handle_h = NULL;
    ALLEGRO_BITMAP* skin_chk_unchecked = NULL;
    ALLEGRO_BITMAP* skin_chk_checked = NULL;
    {
        /* create placeholder skin bitmaps programmatically */
        ALLEGRO_BITMAP* prev_target = al_get_target_bitmap();

        /* window background: blue gradient */
        skin_win_bg = al_create_bitmap(64, 64);
        al_set_target_bitmap(skin_win_bg);
        for (int row = 0; row < 64; row++) {
            float t = (float)row / 63.0f;
            al_draw_line(0, (float)row, 64, (float)row,
                         al_map_rgba((unsigned char)(20 + 40 * t), (unsigned char)(40 + 60 * t), (unsigned char)(80 + 80 * t), 230), 1.0f);
        }

        /* titlebar: dark gradient */
        skin_win_tb = al_create_bitmap(64, 16);
        al_set_target_bitmap(skin_win_tb);
        for (int row = 0; row < 16; row++) {
            float t = (float)row / 15.0f;
            al_draw_line(0, (float)row, 64, (float)row,
                         al_map_rgba((unsigned char)(60 + 40 * t), (unsigned char)(30 + 30 * t), (unsigned char)(100 + 50 * t), 240), 1.0f);
        }

        /* slider track: dark rounded bar */
        skin_sld_track = al_create_bitmap(32, 8);
        al_set_target_bitmap(skin_sld_track);
        al_clear_to_color(al_map_rgba(50, 50, 60, 200));

        /* slider fill: bright bar */
        skin_sld_fill = al_create_bitmap(32, 8);
        al_set_target_bitmap(skin_sld_fill);
        al_clear_to_color(al_map_rgba(100, 180, 255, 230));

        /* slider handle: circle-ish */
        skin_sld_handle = al_create_bitmap(16, 16);
        al_set_target_bitmap(skin_sld_handle);
        al_clear_to_color(al_map_rgba(0, 0, 0, 0));
        al_draw_filled_circle(8, 8, 7, al_map_rgba(200, 200, 220, 240));

        /* slider handle hover */
        skin_sld_handle_h = al_create_bitmap(16, 16);
        al_set_target_bitmap(skin_sld_handle_h);
        al_clear_to_color(al_map_rgba(0, 0, 0, 0));
        al_draw_filled_circle(8, 8, 7, al_map_rgba(150, 220, 255, 255));

        /* checkbox bitmaps */
        skin_chk_unchecked = al_create_bitmap(16, 16);
        al_set_target_bitmap(skin_chk_unchecked);
        al_clear_to_color(al_map_rgba(60, 60, 80, 220));
        al_draw_rectangle(0.5f, 0.5f, 15.5f, 15.5f, al_map_rgba(150, 150, 200, 255), 1.0f);

        skin_chk_checked = al_create_bitmap(16, 16);
        al_set_target_bitmap(skin_chk_checked);
        al_clear_to_color(al_map_rgba(80, 140, 255, 240));
        al_draw_rectangle(0.5f, 0.5f, 15.5f, 15.5f, al_map_rgba(150, 150, 200, 255), 1.0f);
        al_draw_line(3, 8, 7, 13, al_map_rgb(255, 255, 255), 2.0f);
        al_draw_line(7, 13, 13, 3, al_map_rgb(255, 255, 255), 2.0f);

        al_set_target_bitmap(prev_target);

        /* create the skinned window */
        win_ids[win_idx] = n_gui_add_window(gui, "Bitmap Skinning", 690, 60, 300, 240);
        win_names[win_idx] = "Bitmap Skinning";
        int win_skin = win_ids[win_idx];
        win_idx++;

        /* apply window bitmaps */
        n_gui_window_set_bitmaps(gui, win_skin, skin_win_bg, skin_win_tb, N_GUI_IMAGE_STRETCH);

        /* skinned slider */
        int skin_slider = n_gui_add_slider(gui, win_skin, 20, 20, 200, 24, 0.0, 100.0, 60.0,
                                           N_GUI_SLIDER_VALUE, on_slider_change, NULL);
        n_gui_slider_set_bitmaps(gui, skin_slider, skin_sld_track, skin_sld_fill, skin_sld_handle, skin_sld_handle_h, NULL);

        /* skinned checkbox */
        int skin_chk = n_gui_add_checkbox(gui, win_skin, "Skinned checkbox", 20, 60, 200, 24, 0,
                                          on_checkbox_toggle, NULL);
        n_gui_checkbox_set_bitmaps(gui, skin_chk, skin_chk_unchecked, skin_chk_checked, NULL);

        /* label with background (reuses sld_track bitmap) */
        int skin_label = n_gui_add_label(gui, win_skin, "Label with bg bitmap", 20, 100, 200, 24, N_GUI_ALIGN_CENTER);
        n_gui_label_set_bitmap(gui, skin_label, skin_sld_track);

        /* regular button for comparison */
        n_gui_add_button(gui, win_skin, "Normal Button", 20, 140, 200, 30, N_GUI_SHAPE_ROUNDED, on_button_click, NULL);

        n_gui_add_label(gui, win_skin, "Window bg, titlebar, slider, checkbox,", 20, 180, 260, 16, N_GUI_ALIGN_LEFT);
        n_gui_add_label(gui, win_skin, "and label are all bitmap-skinned above.", 20, 198, 260, 16, N_GUI_ALIGN_LEFT);
    }

    /* --- Window 15: Login Dialog (autofit + centered + set_focus demo) --- */
    win_ids[win_idx] = n_gui_add_window(gui, "Login", 640, 450, 1, 1);
    win_names[win_idx] = "Login Dialog";
    {
        int win_login = win_ids[win_idx];
        win_idx++;

        /* configure auto-fit: adjust both W and H, centered on insertion point, 10px border */
        n_gui_window_set_autofit(gui, win_login, N_GUI_AUTOFIT_WH | N_GUI_AUTOFIT_CENTER, 10.0f);

        /* add widgets */
        n_gui_add_label(gui, win_login, "Username:", 10, 10, 80, 25, N_GUI_ALIGN_LEFT);
        int txt_user = n_gui_add_textarea(gui, win_login, 100, 10, 200, 25, 0, 64, on_text_change, NULL);
        n_gui_add_label(gui, win_login, "Password:", 10, 45, 80, 25, N_GUI_ALIGN_LEFT);
        n_gui_add_textarea(gui, win_login, 100, 45, 200, 25, 0, 64, on_text_change, NULL);
        n_gui_add_button(gui, win_login, "Login", 100, 85, 100, 30, N_GUI_SHAPE_ROUNDED, on_login_click, NULL);

        /* trigger autofit calculation */
        n_gui_window_apply_autofit(gui, win_login);

        /* set initial focus to username field */
        n_gui_set_focus(gui, txt_user);
    }

    /* ---- main loop ---- */
    al_start_timer(fps_timer);
    int do_draw = 0;
    int mx = 0, my = 0;

    al_clear_keyboard_state(NULL);
    al_flush_event_queue(event_queue);

    do {
        ALLEGRO_EVENT ev;
        al_wait_for_event(event_queue, &ev);

        if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
            DONE = 1;
        }
        if (ev.type == ALLEGRO_EVENT_KEY_DOWN && ev.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
            DONE = 1;
        }

        /* handle display resize: update display size (the virtual canvas
         * transform auto-updates, scaling all widgets uniformly) */
        if (ev.type == ALLEGRO_EVENT_DISPLAY_RESIZE) {
            al_acknowledge_resize(display);
            int new_w = al_get_display_width(display);
            int new_h = al_get_display_height(display);
            n_gui_set_display_size(gui, (float)new_w, (float)new_h);
        }

        /* pass events to the GUI, check if the event was consumed.
         * Use gui_handled to prevent mouse events from reaching game logic
         * when the cursor is over a GUI window or widget.
         * Example: if (!gui_handled && ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) { ... } */
        int gui_handled = n_gui_process_event(gui, ev);
        (void)gui_handled;

        if (ev.type == ALLEGRO_EVENT_MOUSE_AXES) {
            mx = ev.mouse.x;
            my = ev.mouse.y;
        }

        if (ev.type == ALLEGRO_EVENT_TIMER) {
            /* update toggle status labels */
            char status_buf[256];
            snprintf(status_buf, sizeof(status_buf),
                     "Player:%s  Height:%s  Grid:%s  Smooth:%s  Ghost:%s",
                     player_mode ? "ON" : "off",
                     height_mode ? "ON" : "off",
                     show_grid ? "ON" : "off",
                     smooth_height ? "ON" : "off",
                     ghost_enabled ? "ON" : "off");
            n_gui_label_set_text(gui, lbl_status, status_buf);

            char proj_buf[128];
            snprintf(proj_buf, sizeof(proj_buf), "Projection: %s", proj_names[current_proj]);
            n_gui_label_set_text(gui, lbl_proj, proj_buf);

            do_draw = 1;
        }

        if (do_draw && al_is_event_queue_empty(event_queue)) {
            al_set_target_bitmap(al_get_backbuffer(display));
            al_clear_to_color(al_map_rgba(30, 30, 35, 255));

            /* draw all GUI windows and widgets */
            n_gui_draw(gui);

            /* mouse cursor crosshair (transform screen coords to virtual) */
            {
                float vmx, vmy;
                n_gui_screen_to_virtual(gui, (float)mx, (float)my, &vmx, &vmy);
                /* apply the virtual canvas transform for drawing */
                ALLEGRO_TRANSFORM overlay_tf;
                al_identity_transform(&overlay_tf);
                if (gui->virtual_w > 0 && gui->gui_scale > 0) {
                    al_scale_transform(&overlay_tf, gui->gui_scale, gui->gui_scale);
                    al_translate_transform(&overlay_tf, gui->gui_offset_x, gui->gui_offset_y);
                }
                al_use_transform(&overlay_tf);
                /* ensure crosshair is at least 1 physical pixel thick */
                float cross_thick = 1.0f;
                if (gui->gui_scale > 0.01f && gui->gui_scale < 1.0f) {
                    cross_thick = 1.0f / gui->gui_scale;
                }
                al_draw_line(vmx - 6, vmy, vmx + 6, vmy, al_map_rgb(255, 100, 100), cross_thick);
                al_draw_line(vmx, vmy - 6, vmx, vmy + 6, al_map_rgb(255, 100, 100), cross_thick);
            }

            /* instructions (drawn in virtual space) */
            {
                /* explicitly set virtual canvas transform so this block is
                 * self-contained and does not depend on previous drawing state */
                ALLEGRO_TRANSFORM instr_tf;
                al_identity_transform(&instr_tf);
                if (gui->virtual_w > 0 && gui->virtual_h > 0 && gui->gui_scale > 0) {
                    al_scale_transform(&instr_tf, gui->gui_scale, gui->gui_scale);
                    al_translate_transform(&instr_tf, gui->gui_offset_x, gui->gui_offset_y);
                }
                al_use_transform(&instr_tf);
                float virt_h = (gui->virtual_h > 0) ? gui->virtual_h : (float)al_get_display_height(display);
                al_draw_text(font, al_map_rgb(180, 180, 180), 10, virt_h - 18, 0,
                             "Drag windows | Click widgets | Scroll lists | Resize corners/display | 'Windows' dropdown | ESC to quit");
                /* restore identity transform */
                ALLEGRO_TRANSFORM identity;
                al_identity_transform(&identity);
                al_use_transform(&identity);
            }

            al_flip_display();
            do_draw = 0;
        }
    } while (!DONE);

    /* cleanup — destroy GUI context first, then bitmaps (N_GUI does not own them) */
    n_gui_destroy_ctx(&gui);
    if (img_192) al_destroy_bitmap(img_192);
    if (img_32) al_destroy_bitmap(img_32);
    /* skin bitmaps: destroy after GUI context since N_GUI only stores pointers */
    if (skin_win_bg) al_destroy_bitmap(skin_win_bg);
    if (skin_win_tb) al_destroy_bitmap(skin_win_tb);
    if (skin_sld_track) al_destroy_bitmap(skin_sld_track);
    if (skin_sld_fill) al_destroy_bitmap(skin_sld_fill);
    if (skin_sld_handle) al_destroy_bitmap(skin_sld_handle);
    if (skin_sld_handle_h) al_destroy_bitmap(skin_sld_handle_h);
    if (skin_chk_unchecked) al_destroy_bitmap(skin_chk_unchecked);
    if (skin_chk_checked) al_destroy_bitmap(skin_chk_checked);
    al_destroy_font(font);
    al_destroy_timer(fps_timer);
    al_destroy_event_queue(event_queue);
    al_destroy_display(display);
    al_uninstall_system();

    return 0;
}
