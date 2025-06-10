/**
 *@example ex_gui.c
 *@brief Nilorea Library gui api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/10/2018
 */

#define WIDTH 640
#define HEIGHT 480

#define ALLEGRO_UNSTABLE 1

#include "nilorea/n_gui.h"

ALLEGRO_DISPLAY* display = NULL;

int DONE = 0,            /* Flag to check if we are always running */
    getoptret = 0,       /* launch parameter check */
    log_level = LOG_ERR; /* default LOG_LEVEL */

ALLEGRO_BITMAP* scr_buf = NULL;

ALLEGRO_TIMER* fps_timer = NULL;
ALLEGRO_TIMER* logic_timer = NULL;

// RESOURCES *resources = NULL ;

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
        n_log(LOG_ERR, "Unable to initialize audio addon, audio will be disabled or crashing !");
    }
    if (!al_init_acodec_addon()) {
        n_log(LOG_ERR, "Unable to initialize audio codec addon, audio will be disabled or crashing !");
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

    /* const char *html = "<div class=\"example\">Hello, <span>world!</span>__ this is\n"
        "free<br>\n"
        "text</br>\n"
        "zone</div>";

    N_GUI_DIALOG *dialog = NULL ;
    dialog = n_gui_load_dialog( "index.html" , "styles.css" ); */

    int mx = 0, my = 0, mouse_b1 = 0, mouse_b2 = 0;
    int do_draw = 0, do_logic = 0;

    al_clear_keyboard_state(NULL);
    al_flush_event_queue(event_queue);
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
            } else if (ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_IN || ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT) {
                al_clear_keyboard_state(display);
                al_flush_event_queue(event_queue);
            } else {
                /* Processing inputs */
                // get_keyboard( chat_line , ev );
            }
        } while (!al_is_event_queue_empty(event_queue));

        /* dev mod: right click to temporary delete a block
           left click to temporary add a block */
        int mouse_button = -1;
        if (mouse_b1 == 1)
            mouse_button = 1;
        if (mouse_b2 == 1)
            mouse_button = 2;

        if (do_logic == 1) {
            if (mouse_button == 1) {
            }
            do_logic = 0;
        }

        if (do_draw == 1) {
            al_acknowledge_resize(display);
            /* int w = al_get_display_width(  display );
int h = al_get_display_height( display ); */

            // clear buffer
            al_set_target_bitmap(scrbuf);
            al_clear_to_color(al_map_rgba(0, 0, 0, 255));

            // draw game

            // draw gui

            // draw buffer to screen
            al_set_target_bitmap(al_get_backbuffer(display));
            al_clear_to_color(al_map_rgba(0, 0, 0, 255));
            al_draw_bitmap(scrbuf, 0, 0, 0);

            /* mouse pointer */
            al_draw_line(mx - 5, my, mx + 5, my, al_map_rgb(255, 0, 0), 1);
            al_draw_line(mx, my + 5, mx, my - 5, al_map_rgb(255, 0, 0), 1);

            al_flip_display();
            do_draw = 0;
        }

    } while (!key[KEY_ESC] && !DONE);

    al_destroy_bitmap(scr_buf);

    al_uninstall_system();

    return 0;
}
