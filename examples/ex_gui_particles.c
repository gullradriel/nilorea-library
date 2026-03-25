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
 *@example ex_gui_particles.c
 *@brief Nilorea Library gui particle api test with info GUI
 *@author Castagnier Mickael
 *@version 2.0
 *@date 24/10/2018
 */

#define WIDTH 640
#define HEIGHT 480

#define ALLEGRO_UNSTABLE 1

#include "nilorea/n_common.h"
#include "nilorea/n_particles.h"
#include "nilorea/n_anim.h"
#include "nilorea/n_gui.h"
#include <allegro5/allegro_ttf.h>

ALLEGRO_DISPLAY* display = NULL;

int DONE = 0,            /* Flag to check if we are always running */
    getoptret = 0,       /* launch parameter check */
    log_level = LOG_ERR; /* default LOG_LEVEL */

ALLEGRO_BITMAP* scr_buf = NULL;

ALLEGRO_TIMER* fps_timer = NULL;
ALLEGRO_TIMER* logic_timer = NULL;
LIST* active_object = NULL; /* list of active objects */
PARTICLE_SYSTEM* particle_system_effects = NULL;

/* GUI widget ids for info labels */
static int lbl_particle_count = -1;
static int lbl_particle_max = -1;
static int lbl_mouse_pos = -1;
static int lbl_spawn_info = -1;
static int lbl_fps_info = -1;

/* stats */
static int total_spawned = 0;
static int frame_count = 0;
static double fps_display = 0.0;
static double fps_accum = 0.0;

int main(int argc, char* argv[]) {
    set_log_level(LOG_NOTICE);

    N_STR* log_file = NULL;
    nstrprintf(log_file, "%s.log", argv[0]);
    /*set_log_file( _nstr( log_file ) );*/
    free_nstr(&log_file);

    n_log(LOG_NOTICE, "%s is starting ...", argv[0]);

    /* allegro 5 + addons loading */
    if (!al_init()) {
        n_abort("Could not init Allegro.\n");
    }
    if (!al_install_audio()) {
        n_abort("Unable to initialize audio addon\n");
    }
    if (!al_init_acodec_addon()) {
        n_abort("Unable to initialize acoded addon\n");
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
                n_log(LOG_NOTICE, "\n    %s -h help -v version -V DEBUGLEVEL (NOLOG,VERBOSE,NOTICE,ERROR,DEBUG)", argv[0]);
                exit(TRUE);
            case 'v':
                sprintf(ver_str, "%s %s", __DATE__, __TIME__);
                exit(TRUE);
                break;
            case 'V':
                if (!strncmp("NOTICE", optarg, 6)) {
                    log_level = LOG_INFO;
                } else {
                    if (!strncmp("VERBOSE", optarg, 7)) {
                        log_level = LOG_NOTICE;
                    } else {
                        if (!strncmp("ERROR", optarg, 5)) {
                            log_level = LOG_ERR;
                        } else {
                            if (!strncmp("DEBUG", optarg, 5)) {
                                log_level = LOG_DEBUG;
                            } else {
                                n_log(LOG_ERR, "%s is not a valid log level", optarg);
                                exit(FALSE);
                            }
                        }
                    }
                }
                n_log(LOG_NOTICE, "LOG LEVEL UP TO: %d", log_level);
                set_log_level(log_level);
                break;
            case 'L':
                n_log(LOG_NOTICE, "LOG FILE: %s", optarg);
                set_log_file(optarg);
                break;
            case '?': {
                switch (optopt) {
                    case 'V':
                        n_log(LOG_ERR, "\nPlease specify a log level after -V. \nAvailable values: NOLOG,VERBOSE,NOTICE,ERROR,DEBUG");
                        break;
                    case 'L':
                        n_log(LOG_ERR, "\nPlease specify a log file after -L");
                    default:
                        break;
                }
            }
                __attribute__((fallthrough));
            default:
                n_log(LOG_ERR, "\n    %s -h help -v version -V DEBUGLEVEL (NOLOG,VERBOSE,NOTICE,ERROR,DEBUG) -L logfile", argv[0]);
                exit(FALSE);
        }
    }

    fps_timer = al_create_timer(1.0 / 30.0);
    logic_timer = al_create_timer(1.0 / 50.0);

    al_set_new_display_flags(ALLEGRO_OPENGL | ALLEGRO_WINDOWED);
    display = al_create_display(WIDTH, HEIGHT);
    if (!display) {
        n_abort("Unable to create display\n");
    }
    al_set_window_title(display, argv[0]);

    al_set_new_bitmap_flags(ALLEGRO_VIDEO_BITMAP);

    DONE = 0;

    active_object = new_generic_list(MAX_LIST_ITEMS);

    enum APP_KEYS {
        KEY_UP,
        KEY_DOWN,
        KEY_LEFT,
        KEY_RIGHT,
        KEY_ESC,
        KEY_SPACE,
        KEY_CTRL
    };
    int key[7] = {false, false, false, false, false, false, false};

    al_register_event_source(event_queue, al_get_display_event_source(display));

    al_start_timer(fps_timer);
    al_start_timer(logic_timer);
    al_register_event_source(event_queue, al_get_timer_event_source(fps_timer));
    al_register_event_source(event_queue, al_get_timer_event_source(logic_timer));

    al_register_event_source(event_queue, al_get_keyboard_event_source());
    al_register_event_source(event_queue, al_get_mouse_event_source());

    ALLEGRO_BITMAP* scrbuf = al_create_bitmap(WIDTH, HEIGHT);

    al_hide_mouse_cursor(display);

    init_particle_system(&particle_system_effects, 10000, 0, 0, 0, 100);

    int mx = 0, my = 0, mouse_b1 = 0, mouse_b2 = 0;
    int do_draw = 0, do_logic = 0;

    /* ---- Create Info GUI ---- */
    ALLEGRO_FONT* gui_font = al_create_builtin_font();
    N_GUI_CTX* gui = n_gui_new_ctx(gui_font);
    n_gui_set_display_size(gui, (float)WIDTH, (float)HEIGHT);
    n_gui_set_virtual_size(gui, (float)WIDTH, (float)HEIGHT);

    int info_win = n_gui_add_window(gui, "Particle Info", 10, 10, 220, 180);
    lbl_particle_count = n_gui_add_label(gui, info_win, "Active: 0", 10, 10, 200, 16, N_GUI_ALIGN_LEFT);
    lbl_particle_max = n_gui_add_label(gui, info_win, "Max: 10000", 10, 30, 200, 16, N_GUI_ALIGN_LEFT);
    lbl_spawn_info = n_gui_add_label(gui, info_win, "Spawned: 0", 10, 50, 200, 16, N_GUI_ALIGN_LEFT);
    lbl_mouse_pos = n_gui_add_label(gui, info_win, "Mouse: 0, 0", 10, 70, 200, 16, N_GUI_ALIGN_LEFT);
    lbl_fps_info = n_gui_add_label(gui, info_win, "FPS: 0", 10, 90, 200, 16, N_GUI_ALIGN_LEFT);
    n_gui_add_label(gui, info_win, "Left click to spawn", 10, 118, 200, 16, N_GUI_ALIGN_LEFT);

    al_clear_keyboard_state(NULL);
    al_flush_event_queue(event_queue);

    double last_fps_time = al_get_time();

    int w = al_get_display_width(display);
    int h = al_get_display_height(display);

    do {
        do {
            ALLEGRO_EVENT ev;

            al_wait_for_event(event_queue, &ev);

            if (ev.type == ALLEGRO_EVENT_KEY_DOWN) {
                switch (ev.keyboard.keycode) {
                    case ALLEGRO_KEY_UP:
                        key[KEY_UP] = 1;
                        break;
                    case ALLEGRO_KEY_DOWN:
                        key[KEY_DOWN] = 1;
                        break;
                    case ALLEGRO_KEY_LEFT:
                        key[KEY_LEFT] = 1;
                        break;
                    case ALLEGRO_KEY_RIGHT:
                        key[KEY_RIGHT] = 1;
                        break;
                    case ALLEGRO_KEY_ESCAPE:
                        key[KEY_ESC] = 1;
                        break;
                    case ALLEGRO_KEY_SPACE:
                        key[KEY_SPACE] = 1;
                        break;
                    case ALLEGRO_KEY_LCTRL:
                    case ALLEGRO_KEY_RCTRL:
                        key[KEY_CTRL] = 1;
                    default:
                        break;
                }
            } else if (ev.type == ALLEGRO_EVENT_KEY_UP) {
                switch (ev.keyboard.keycode) {
                    case ALLEGRO_KEY_UP:
                        key[KEY_UP] = 0;
                        break;
                    case ALLEGRO_KEY_DOWN:
                        key[KEY_DOWN] = 0;
                        break;
                    case ALLEGRO_KEY_LEFT:
                        key[KEY_LEFT] = 0;
                        break;
                    case ALLEGRO_KEY_RIGHT:
                        key[KEY_RIGHT] = 0;
                        break;
                    case ALLEGRO_KEY_ESCAPE:
                        key[KEY_ESC] = 0;
                        break;
                    case ALLEGRO_KEY_SPACE:
                        key[KEY_SPACE] = 0;
                        break;
                    case ALLEGRO_KEY_LCTRL:
                    case ALLEGRO_KEY_RCTRL:
                        key[KEY_CTRL] = 0;
                    default:
                        break;
                }
            } else if (ev.type == ALLEGRO_EVENT_TIMER) {
                if (al_get_timer_event_source(fps_timer) == ev.any.source) {
                    do_draw = 1;
                } else if (al_get_timer_event_source(logic_timer) == ev.any.source) {
                    do_logic = 1;
                }
            } else if (ev.type == ALLEGRO_EVENT_MOUSE_AXES) {
                mx = ev.mouse.x;
                my = ev.mouse.y;
            } else if (ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
                if (ev.mouse.button == 1)
                    mouse_b1 = 1;
                if (ev.mouse.button == 2)
                    mouse_b2 = 1;
            } else if (ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP) {
                if (ev.mouse.button == 1)
                    mouse_b1 = 0;
                if (ev.mouse.button == 2)
                    mouse_b2 = 0;
            } else if (ev.type == ALLEGRO_EVENT_DISPLAY_RESIZE) {
                al_acknowledge_resize(display);
                w = al_get_display_width(display);
                h = al_get_display_height(display);
                n_gui_set_display_size(gui, (float)w, (float)h);
            } else if (ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_IN || ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT) {
                al_clear_keyboard_state(display);
                al_flush_event_queue(event_queue);
            }

            /* pass events to the GUI.
             * n_gui_wants_mouse() is checked below before game mouse actions. */
            n_gui_process_event(gui, ev);

        } while (!al_is_event_queue_empty(event_queue));

        /* dev mod: right click to temporary delete a block
           left click to temporary add a block */
        int mouse_button = -1;
        if (mouse_b1 == 1)
            mouse_button = 1;
        if (mouse_b2 == 1)
            mouse_button = 2;

        if (do_logic == 1) {
            manage_particle(particle_system_effects);
            if (mouse_button == 1 && !n_gui_wants_mouse(gui)) {
                PHYSICS tmp_part = {0};
                tmp_part.sz = 10;
                for (int it = 0; it < 100; it++) {
                    VECTOR3D_SET(tmp_part.speed,
                                 500 - rand() % 1000,
                                 500 - rand() % 1000,
                                 0.0);
                    VECTOR3D_SET(tmp_part.position, mx, my, 0.0);
                    VECTOR3D_SET(tmp_part.acceleration, 0.0, 0.0, 0.0);
                    VECTOR3D_SET(tmp_part.orientation, 0.0, 0.0, 0.0);
                    if (add_particle(particle_system_effects, -1, PIXEL_PART, 1000 + rand() % 3000, 1 + rand() % 3,
                                     al_map_rgba((unsigned char)(55 + rand() % 200), (unsigned char)(55 + rand() % 200), (unsigned char)(55 + rand() % 200), (unsigned char)(10 + rand() % 245)), tmp_part) == TRUE) {
                        total_spawned++;
                    }
                }
            }
            do_logic = 0;
        }

        if (do_draw == 1) {
            al_set_target_bitmap(scrbuf);
            al_clear_to_color(al_map_rgba(0, 0, 0, 255));
            draw_particle(particle_system_effects, 0, 0, w, h, 50);

            al_set_target_bitmap(al_get_backbuffer(display));

            al_clear_to_color(al_map_rgba(0, 0, 0, 255));
            al_draw_bitmap(scrbuf, 0, 0, 0);

            /* update info labels */
            frame_count++;
            double now = al_get_time();
            fps_accum += (now - last_fps_time);
            last_fps_time = now;
            if (fps_accum >= 1.0) {
                fps_display = frame_count / fps_accum;
                frame_count = 0;
                fps_accum = 0.0;
            }

            char buf[128];
            size_t active_count = 0;
            size_t max_count = 0;
            if (particle_system_effects && particle_system_effects->list) {
                active_count = particle_system_effects->list->nb_items;
                max_count = particle_system_effects->list->nb_max_items;
            }

            snprintf(buf, sizeof(buf), "Active: %zu", active_count);
            n_gui_label_set_text(gui, lbl_particle_count, buf);

            snprintf(buf, sizeof(buf), "Max: %zu", max_count);
            n_gui_label_set_text(gui, lbl_particle_max, buf);

            snprintf(buf, sizeof(buf), "Spawned: %d", total_spawned);
            n_gui_label_set_text(gui, lbl_spawn_info, buf);

            snprintf(buf, sizeof(buf), "Mouse: %d, %d", mx, my);
            n_gui_label_set_text(gui, lbl_mouse_pos, buf);

            snprintf(buf, sizeof(buf), "FPS: %.1f", fps_display);
            n_gui_label_set_text(gui, lbl_fps_info, buf);

            /* draw GUI on top of particles */
            n_gui_draw(gui);

            /* mouse pointer */
            al_draw_line((float)(mx - 5), (float)my, (float)(mx + 5), (float)my, al_map_rgb(255, 0, 0), 1);
            al_draw_line((float)mx, (float)(my + 5), (float)mx, (float)(my - 5), al_map_rgb(255, 0, 0), 1);

            al_flip_display();
            do_draw = 0;
        }

    } while (!key[KEY_ESC] && !DONE);

    list_destroy(&active_object);
    n_gui_destroy_ctx(&gui);
    al_destroy_font(gui_font);
    al_destroy_bitmap(scrbuf);
    al_destroy_bitmap(scr_buf);

    al_uninstall_system();

    return 0;
}
