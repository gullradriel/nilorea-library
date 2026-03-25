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
 *@example ex_gui_dictionary.c
 *@brief Nilorea Library gui dictionary search using n_gui widgets
 *@author Castagnier Mickael
 *@version 2.0
 *@date 10/02/2020
 */

#define WIDTH 800
#define HEIGHT 800

#define ALLEGRO_UNSTABLE 1

#include "nilorea/n_common.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"
#include "nilorea/n_pcre.h"
#include "nilorea/n_str.h"
#include "nilorea/n_gui.h"
#include <allegro5/allegro_ttf.h>

/* dictionnaries are from https://www.bragitoff.com/2016/03/english-dictionary-in-csv-format/ */

ALLEGRO_DISPLAY* display = NULL;

int DONE = 0,             /* Flag to check if we are always running */
    getoptret = 0,        /* launch parameter check */
    log_level = LOG_INFO; /* default LOG_LEVEL */

ALLEGRO_TIMER* fps_timer = NULL;
ALLEGRO_TIMER* logic_timer = NULL;

/*! dictionary definition for DICTIONARY_ENTRY */
typedef struct DICTIONARY_DEFINITION {
    /*! type of definition (verb, noun,...) */
    char* type;
    /*! content of definition */
    char* definition;
} DICTIONARY_DEFINITION;

/*! dictionary entry */
typedef struct DICTIONARY_ENTRY {
    /*! key of the entry */
    char* key;
    /*! list of DICTIONARY_DEFINITION for that entry */
    LIST* definitions;
} DICTIONARY_ENTRY;

/* application state */
static HASH_TABLE* dictionary = NULL;
static LIST* completion = NULL;
static size_t max_results = 100;
static char last_search[4096] = "";

/* GUI widget ids */
static int search_textarea_id = -1;
static int completion_listbox_id = -1;
static int definition_label_id = -1;
static int definition_win_id = -1;
static int status_win_id = -1;
static int status_label_id = -1;

/* forward declarations */
static void update_completion(N_GUI_CTX* gui, const char* text);
static void update_definitions(N_GUI_CTX* gui, const char* word);

void free_entry_def(void* entry_def) {
    DICTIONARY_DEFINITION* def = (DICTIONARY_DEFINITION*)entry_def;
    FreeNoLog(def->type);
    FreeNoLog(def->definition);
    FreeNoLog(def);
}
void free_entry(void* entry_ptr) {
    DICTIONARY_ENTRY* entry = (DICTIONARY_ENTRY*)entry_ptr;
    list_destroy(&entry->definitions);
    FreeNoLog(entry->key);
    FreeNoLog(entry);
}

/* callback: search text changed */
void on_search_change(int widget_id, const char* text, void* user_data) {
    (void)widget_id;
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    update_completion(gui, text);
    update_definitions(gui, text);
}

/* callback: completion list selection */
void on_completion_select(int widget_id, int index, int selected, void* user_data) {
    (void)widget_id;
    if (!selected) return;
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;

    const char* item_text = n_gui_listbox_get_item_text(gui, completion_listbox_id, index);
    if (item_text) {
        n_gui_textarea_set_text(gui, search_textarea_id, item_text);
        update_completion(gui, item_text);
        update_definitions(gui, item_text);
    }
}

/* callback: clear button */
void on_clear_click(int widget_id, void* user_data) {
    (void)widget_id;
    N_GUI_CTX* gui = (N_GUI_CTX*)user_data;
    n_gui_textarea_set_text(gui, search_textarea_id, "");
    update_completion(gui, "");
    update_definitions(gui, "");
}

static void update_completion(N_GUI_CTX* gui, const char* text) {
    if (strcmp(text, last_search) == 0) return;
    strncpy(last_search, text, sizeof(last_search) - 1);
    last_search[sizeof(last_search) - 1] = '\0';

    if (completion) {
        list_destroy(&completion);
    }
    completion = ht_get_completion_list(dictionary, text, max_results);

    n_gui_listbox_clear(gui, completion_listbox_id);
    if (completion) {
        list_foreach(node, completion) {
            char* entry_key = (char*)node->ptr;
            n_gui_listbox_add_item(gui, completion_listbox_id, entry_key);
        }
    }
}

static void update_definitions(N_GUI_CTX* gui, const char* word) {
    DICTIONARY_ENTRY* entry = NULL;
    if (ht_get_ptr(dictionary, word, (void**)&entry) == TRUE) {
        char buf[16384] = "";
        size_t offset = 0;
        list_foreach(node, entry->definitions) {
            DICTIONARY_DEFINITION* def = (DICTIONARY_DEFINITION*)node->ptr;
            int written = snprintf(buf + offset, sizeof(buf) - offset, "%s : %s\n", def->type, def->definition);
            if (written > 0) offset += (size_t)written;
            if (offset >= sizeof(buf) - 1) break;
        }
        n_gui_label_set_text(gui, definition_label_id, buf);
    } else {
        n_gui_label_set_text(gui, definition_label_id, "");
    }
}

int main(int argc, char* argv[]) {
    set_log_level(log_level);

    n_log(LOG_NOTICE, "%s is starting ...", argv[0]);

    /* allegro 5 + addons loading */
    if (!al_init()) {
        n_abort("Could not init Allegro.\n");
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
    ALLEGRO_EVENT_QUEUE* event_queue = NULL;

    event_queue = al_create_event_queue();
    if (!event_queue) {
        fprintf(stderr, "failed to create event_queue!\n");
        al_destroy_display(display);
        return -1;
    }

    char ver_str[128] = "";
    while ((getoptret = getopt(argc, argv, "hvV:L:")) != EOF) {
        switch (getoptret) {
            case 'h':
                n_log(LOG_NOTICE, "\n    %s -h help -v version -V LOG_LEVEL (NOLOG,INFO,NOTICE,ERR,DEBUG) -L OPT_LOG_FILE\n", argv[0]);
                exit(TRUE);
            case 'v':
                sprintf(ver_str, "%s %s", __DATE__, __TIME__);
                exit(TRUE);
                break;
            case 'V':
                if (!strncmp("INFO", optarg, 6)) {
                    log_level = LOG_INFO;
                } else {
                    if (!strncmp("NOTICE", optarg, 7)) {
                        log_level = LOG_NOTICE;
                    } else {
                        if (!strncmp("ERR", optarg, 5)) {
                            log_level = LOG_ERR;
                        } else {
                            if (!strncmp("DEBUG", optarg, 5)) {
                                log_level = LOG_DEBUG;
                            } else {
                                n_log(LOG_ERR, "%s is not a valid log level\n", optarg);
                                exit(FALSE);
                            }
                        }
                    }
                }
                n_log(LOG_NOTICE, "LOG LEVEL UP TO: %d\n", log_level);
                set_log_level(log_level);
                break;
            case 'L':
                n_log(LOG_NOTICE, "LOG FILE: %s\n", optarg);
                set_log_file(optarg);
                break;
            case '?': {
                switch (optopt) {
                    case 'V':
                        n_log(LOG_ERR, "\nPlease specify a log level after -V. \nAvailable values: NOLOG,VERBOSE,NOTICE,ERROR,DEBUG\n\n");
                        break;
                    case 'L':
                        n_log(LOG_ERR, "\nPlease specify a log file after -L\n");
                    default:
                        break;
                }
            }
                __attribute__((fallthrough));
            default:
                n_log(LOG_ERR, "\n    %s -h help -v version -V DEBUGLEVEL (NOLOG,VERBOSE,NOTICE,ERROR,DEBUG) -L logfile\n", argv[0]);
                exit(FALSE);
        }
    }

    double fps = 60.0;
    double fps_delta_time = 1.0 / fps;

    fps_timer = al_create_timer(fps_delta_time);

    al_set_new_display_flags(ALLEGRO_OPENGL | ALLEGRO_WINDOWED);
    display = al_create_display(WIDTH, HEIGHT);
    if (!display) {
        n_abort("Unable to create display\n");
    }
    al_set_window_title(display, argv[0]);

    al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP);

    DONE = 0;

    enum APP_KEYS {
        KEY_ESC
    };
    int key[1] = {false};

    al_register_event_source(event_queue, al_get_display_event_source(display));
    al_start_timer(fps_timer);
    al_register_event_source(event_queue, al_get_timer_event_source(fps_timer));

    al_register_event_source(event_queue, al_get_keyboard_event_source());
    al_register_event_source(event_queue, al_get_mouse_event_source());

    ALLEGRO_FONT* font = NULL;
    font = al_load_font("DATAS/2Dumb.ttf", 18, 0);
    if (!font) {
        n_log(LOG_ERR, "Unable to load DATAS/2Dumb.ttf, using builtin font");
        font = al_create_builtin_font();
    }

    int do_draw = 0;

    /* Load dictionaries */
    dictionary = new_ht_trie(256, 32);
    FILE* dict_file = fopen("DATAS/dictionary.csv", "r");
    if (!dict_file) {
        n_abort("Unable to open DATAS/dictionary.csv\n");
    }
    char read_buf[16384] = "";
    char* entry_key = NULL;
    char* type = NULL;
    char* definition = NULL;
    N_PCRE* dico_regexp = npcre_new("\"(.*)\",\"(.*)\",\"(.*)\"", 0);

    while (fgets(read_buf, 16384, dict_file)) {
        if (npcre_match_capture(read_buf, dico_regexp)) {
            entry_key = strdup(_str((char*)dico_regexp->match_list[1]));
            type = strdup(_str((char*)dico_regexp->match_list[2]));
            definition = strdup(_str((char*)dico_regexp->match_list[3]));

            // n_log(LOG_DEBUG, "matched %s , %s , %s", entry_key, type, definition);

            DICTIONARY_ENTRY* entry = NULL;
            DICTIONARY_DEFINITION* entry_def = NULL;

            if (ht_get_ptr(dictionary, entry_key, (void**)&entry) == TRUE) {
                Malloc(entry_def, DICTIONARY_DEFINITION, 1);
                entry_def->type = strdup(type);
                entry_def->definition = strdup(definition);
                list_push(entry->definitions, entry_def, &free_entry_def);
            } else {
                Malloc(entry, DICTIONARY_ENTRY, 1);

                entry->definitions = new_generic_list(MAX_LIST_ITEMS);
                entry->key = strdup(entry_key);

                Malloc(entry_def, DICTIONARY_DEFINITION, 1);

                entry_def->type = strdup(type);
                entry_def->definition = strdup(definition);

                list_push(entry->definitions, entry_def, &free_entry_def);

                ht_put_ptr(dictionary, entry_key, entry, &free_entry, NULL);
            }
            FreeNoLog(entry_key);
            FreeNoLog(type);
            FreeNoLog(definition);

            npcre_clean_match(dico_regexp);
        }
    }
    fclose(dict_file);
    npcre_delete(&dico_regexp);

    n_log(LOG_NOTICE, "Dictionary loaded, starting GUI");

    /* ---- Create GUI ---- */
    N_GUI_CTX* gui = n_gui_new_ctx(font);

    /* set display size for global scrollbars (shown when GUI exceeds display) */
    n_gui_set_display_size(gui, (float)WIDTH, (float)HEIGHT);
    /* enable virtual canvas for resolution-independent scaling */
    n_gui_set_virtual_size(gui, (float)WIDTH, (float)HEIGHT);

    /* --- Search Window --- */
    int search_win = n_gui_add_window(gui, "Search", 20, 20, 400, 85);
    n_gui_add_label(gui, search_win, "Type to search:", 10, 5, 280, 20, N_GUI_ALIGN_LEFT);
    search_textarea_id = n_gui_add_textarea(gui, search_win, 10, 28, 300, 24, 0, 256, on_search_change, gui);
    n_gui_add_button(gui, search_win, "Clear", 320, 28, 70, 24, N_GUI_SHAPE_ROUNDED, on_clear_click, gui);
    n_gui_window_set_flags(gui, search_win, N_GUI_WIN_FIXED_POSITION);

    /* --- Completion Window --- */
    int completion_win = n_gui_add_window(gui, "Completions", 440, 20, 350, 690);
    completion_listbox_id = n_gui_add_listbox(gui, completion_win, 10, 10, 330, 650, N_GUI_SELECT_SINGLE, on_completion_select, gui);
    n_gui_window_set_flags(gui, completion_win, N_GUI_WIN_FIXED_POSITION);

    /* --- Definition Window --- */
    definition_win_id = n_gui_add_window(gui, "Definition", 20, 120, 400, 590);
    definition_label_id = n_gui_add_label(gui, definition_win_id, "", 5, 5, 390, 590, N_GUI_ALIGN_JUSTIFIED);
    n_gui_window_set_flags(gui, definition_win_id, N_GUI_WIN_FIXED_POSITION);

    /* --- Status Bar Window --- */
    status_win_id = n_gui_add_window(gui, "Help", 20, 720, 770, 60);
    status_label_id = n_gui_add_label(gui, status_win_id, "Type in Search box | Click completions to select | ESC to quit", 5, 2, 760, 20, N_GUI_ALIGN_LEFT);
    n_gui_window_set_flags(gui, status_win_id, N_GUI_WIN_FIXED_POSITION);

    /* initial completion */
    strncpy(last_search, "a", sizeof(last_search) - 1);
    update_completion(gui, "");

    al_clear_keyboard_state(NULL);
    al_flush_event_queue(event_queue);

    do {
        ALLEGRO_EVENT ev;
        al_wait_for_event(event_queue, &ev);

        /* pass events to the GUI.
         * Use gui_handled to prevent mouse events from reaching game logic
         * when the cursor is over a GUI window or widget. */
        int gui_handled = n_gui_process_event(gui, ev);
        (void)gui_handled;

        if (ev.type == ALLEGRO_EVENT_KEY_DOWN) {
            switch (ev.keyboard.keycode) {
                case ALLEGRO_KEY_ESCAPE:
                    key[KEY_ESC] = 1;
                    break;
                default:
                    break;
            }
        } else if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
            DONE = 1;
        } else if (ev.type == ALLEGRO_EVENT_DISPLAY_RESIZE) {
            al_acknowledge_resize(display);
            n_gui_set_display_size(gui, (float)al_get_display_width(display),
                                   (float)al_get_display_height(display));
        } else if (ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_IN || ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT) {
            al_clear_keyboard_state(display);
            al_flush_event_queue(event_queue);
        }

        if (ev.type == ALLEGRO_EVENT_TIMER) {
            do_draw = 1;
        }

        if (do_draw && al_is_event_queue_empty(event_queue)) {
            al_set_target_bitmap(al_get_backbuffer(display));
            al_clear_to_color(al_map_rgba(20, 20, 25, 255));

            /* draw all GUI windows and widgets */
            n_gui_draw(gui);

            al_flip_display();
            do_draw = 0;
        }

    } while (!key[KEY_ESC] && !DONE);

    destroy_ht(&dictionary);
    if (completion)
        list_destroy(&completion);

    n_gui_destroy_ctx(&gui);
    al_destroy_event_queue(event_queue);
    al_destroy_font(font);
    al_destroy_timer(fps_timer);
    al_uninstall_system();

    return 0;
}
