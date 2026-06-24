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
 *@example ex_gui_network.c
 *@brief GUI network chat demo using the n_gui and n_network modules.
 *
 * A simple TCP chat application with an Allegro5 GUI front-end.
 * Run as server:  ex_gui_network -p 8080
 * Run as client:  ex_gui_network -s 127.0.0.1 -p 8080
 *
 * Demonstrates:
 *  - n_gui: context, windows, labels, textareas, buttons, listbox
 *  - n_network: netw_make_listening, netw_accept_from_ex, netw_connect,
 *    netw_start_thr_engine, netw_add_msg, netw_get_msg, netw_get_state,
 *    netw_close, netw_new_pool, netw_pool_add, netw_pool_broadcast,
 *    netw_pool_nbclients, netw_destroy_pool
 *  - n_network_msg: create_msg, delete_msg, add_int_to_msg, add_strdup_to_msg,
 *    get_int_from_msg, get_str_from_msg, make_str_from_msg, make_msg_from_str,
 *    netmsg_make_string_msg, netw_msg_get_type, netw_get_string
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 06/03/2026
 */

#define WIDTH 800
#define HEIGHT 600

#define ALLEGRO_UNSTABLE 1

#include <getopt.h>
#include <sys/time.h>
#include <sys/types.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_time.h"
#include "nilorea/n_network.h"
#include "nilorea/n_network_msg.h"
#include "nilorea/n_gui.h"

/* custom message type for chat text */
#define NETMSG_CHAT 100

ALLEGRO_DISPLAY* display = NULL;

static int DONE = 0;
static int mode = -1; /* 0 = server, 1 = client */
static char* server_addr = NULL;
static char* port_str = NULL;

/* networking */
static NETWORK* netw_server = NULL;
static NETWORK* netw_client = NULL;
static NETWORK_POOL* pool = NULL;

/* gui widget ids */
static int win_chat = -1;
static int lbl_status = -1;
static int listbox_log = -1;
static int textarea_input = -1;
static int btn_send = -1;

/* server-only gui widget ids */
static int win_clients = -1;
static int lbl_clients_header = -1;
static int listbox_clients = -1;

static N_GUI_CTX* gui = NULL;

/**
 *@brief add a line to the chat log listbox
 *@param text the text to add
 */
void chat_log_add(const char* text) {
    if (gui && listbox_log >= 0) {
        n_gui_listbox_add_item(gui, listbox_log, text);
    }
}

/**
 *@brief refresh the connected clients listbox (server only)
 */
void refresh_clients_list(void) {
    if (!gui || listbox_clients < 0 || !pool) return;

    n_gui_listbox_clear(gui, listbox_clients);

    HT_FOREACH(node, pool->pool, {
        NETWORK* cn = (NETWORK*)node->data.ptr;
        if (cn) {
            char entry[256] = "";
            snprintf(entry, sizeof(entry), "%s:%s (sock %d)",
                     cn->link.ip ? cn->link.ip : "?",
                     cn->link.port ? cn->link.port : "?",
                     (int)cn->link.sock);
            n_gui_listbox_add_item(gui, listbox_clients, entry);
        }
    });

    char header[64] = "";
    snprintf(header, sizeof(header), "Connected clients: %zu", netw_pool_nbclients(pool));
    if (lbl_clients_header >= 0) {
        n_gui_label_set_text(gui, lbl_clients_header, header);
    }
}

/**
 *@brief build a chat network message
 *@param text the chat text
 *@return serialized N_STR ready to send, or NULL on error
 */
N_STR* build_chat_msg(const char* text) {
    NETW_MSG* msg = NULL;
    create_msg(&msg);
    if (!msg) return NULL;

    add_int_to_msg(msg, NETMSG_CHAT);
    add_strdup_to_msg(msg, text);

    N_STR* str = make_str_from_msg(msg);
    delete_msg(&msg);
    return str;
}

/**
 *@brief decode a received chat message
 *@param str the serialized message
 *@param out_text output: allocated chat text (caller must Free)
 *@return TRUE on success, FALSE on error
 */
int decode_chat_msg(N_STR* str, char** out_text) {
    NETW_MSG* msg = NULL;
    int type = 0;
    int ok = FALSE;

    if (out_text) {
        *out_text = NULL;
    }

    msg = make_msg_from_str(str);
    if (!msg) {
        return FALSE;
    }

    if (!get_int_from_msg(msg, &type)) {
        delete_msg(&msg);
        return FALSE;
    }

    if (type != NETMSG_CHAT) {
        delete_msg(&msg);
        return FALSE;
    }

    ok = get_str_from_msg(msg, out_text);
    if (!ok && out_text) {
        *out_text = NULL;
    }

    delete_msg(&msg);
    return ok;
}

/**
 *@brief send button callback
 *@param widget_id the button widget id
 *@param user_data unused
 */
void on_send_click(int widget_id, void* user_data) {
    (void)widget_id;
    (void)user_data;

    const char* text = n_gui_textarea_get_text(gui, textarea_input);
    if (!text || text[0] == '\0') return;

    N_STR* msg_str = build_chat_msg(text);
    if (!msg_str) return;

    if (mode == 0 && pool) {
        /* server: broadcast to all clients */
        netw_pool_broadcast(pool, NULL, msg_str);
        char logbuf[512] = "";
        snprintf(logbuf, sizeof(logbuf), "[server] %s", text);
        chat_log_add(logbuf);
    } else if (mode == 1 && netw_client) {
        N_STR* copy = nstrdup(msg_str);
        if (!netw_add_msg(netw_client, copy)) {
            /* netw_add_msg did not take ownership of 'copy' on failure */
            free_nstr(&copy);
        }
        char logbuf[512] = "";
        snprintf(logbuf, sizeof(logbuf), "[me] %s", text);
        chat_log_add(logbuf);
    }
    free_nstr(&msg_str);
    n_gui_textarea_set_text(gui, textarea_input, "");
}

/**
 *@brief process incoming messages on a single network connection
 *@param netw the network to poll
 *@param prefix label prefix for log display
 */
void poll_network_msgs(NETWORK* netw, const char* prefix) {
    if (!netw) return;
    N_STR* incoming = NULL;
    while ((incoming = netw_get_msg(netw)) != NULL) {
        char* text = NULL;
        if (decode_chat_msg(incoming, &text) == TRUE && text) {
            char logbuf[512] = "";
            snprintf(logbuf, sizeof(logbuf), "[%s] %s", prefix, text);
            chat_log_add(logbuf);

            /* server: relay to all other clients */
            if (mode == 0 && pool) {
                N_STR* relay = build_chat_msg(text);
                if (relay) {
                    netw_pool_broadcast(pool, netw, relay);
                    free_nstr(&relay);
                }
            }
            Free(text);
        }
        free_nstr(&incoming);
    }
}

void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  Server: ex_gui_network -p PORT\n"
            "  Client: ex_gui_network -s ADDRESS -p PORT\n"
            "  Options:\n"
            "    -s addr  : server address to connect to (client mode)\n"
            "    -p port  : port number\n"
            "    -V level : log level (LOG_DEBUG, LOG_INFO, LOG_ERR)\n"
            "    -h       : help\n");
}

int main(int argc, char** argv) {
    int log_level = LOG_ERR;
    int getoptret = 0;

    while ((getoptret = getopt(argc, argv, "hs:p:V:")) != EOF) {
        switch (getoptret) {
            case 's':
                server_addr = strdup(optarg);
                break;
            case 'p':
                port_str = strdup(optarg);
                break;
            case 'V':
                if (!strcmp("LOG_DEBUG", optarg))
                    log_level = LOG_DEBUG;
                else if (!strcmp("LOG_INFO", optarg))
                    log_level = LOG_INFO;
                else if (!strcmp("LOG_NOTICE", optarg))
                    log_level = LOG_NOTICE;
                else if (!strcmp("LOG_ERR", optarg))
                    log_level = LOG_ERR;
                break;
            default:
            case 'h':
                usage();
                exit(1);
        }
    }
    set_log_level(log_level);

    if (!port_str) {
        fprintf(stderr, "Port is required. Use -p PORT\n");
        usage();
        exit(1);
    }

    mode = server_addr ? 1 : 0;

    /* allegro init */
    if (!al_init()) {
        n_log(LOG_ERR, "al_init failed");
        return 1;
    }
    al_install_keyboard();
    al_install_mouse();
    al_init_primitives_addon();
    al_init_font_addon();
    al_init_ttf_addon();
    al_init_image_addon();

    al_set_new_display_flags(ALLEGRO_OPENGL | ALLEGRO_WINDOWED);
    display = al_create_display(WIDTH, HEIGHT);
    if (!display) {
        n_log(LOG_ERR, "Failed to create display");
        return 1;
    }
    al_set_window_title(display, mode == 0 ? "GUI Network - Server" : "GUI Network - Client");

    ALLEGRO_FONT* font = al_create_builtin_font();
    ALLEGRO_EVENT_QUEUE* event_queue = al_create_event_queue();
    ALLEGRO_TIMER* timer = al_create_timer(1.0 / 30.0);

    al_register_event_source(event_queue, al_get_display_event_source(display));
    al_register_event_source(event_queue, al_get_keyboard_event_source());
    al_register_event_source(event_queue, al_get_mouse_event_source());
    al_register_event_source(event_queue, al_get_timer_event_source(timer));

    /* create GUI */
    gui = n_gui_new_ctx(font);
    n_gui_set_display_size(gui, (float)WIDTH, (float)HEIGHT);

    /* main chat window */
    win_chat = n_gui_add_window(gui, mode == 0 ? "Chat Server" : "Chat Client", 10, 10, 780, 580);
    n_gui_window_set_flags(gui, win_chat, N_GUI_WIN_RESIZABLE | N_GUI_WIN_FIXED_POSITION);

    /* status label */
    lbl_status = n_gui_add_label(gui, win_chat, mode == 0 ? "Server: waiting..." : "Client: connecting...",
                                 10, 10, 760, 20, N_GUI_ALIGN_LEFT);

    /* chat log listbox */
    listbox_log = n_gui_add_listbox(gui, win_chat, 10, 40, 760, 420, N_GUI_SELECT_NONE, NULL, NULL);

    /* input textarea */
    textarea_input = n_gui_add_textarea(gui, win_chat, 10, 470, 660, 30, 0, 256, NULL, NULL);

    /* send button bound to Enter key (works even while typing in the single-line text input) */
    btn_send = n_gui_add_button(gui, win_chat, "Send", 680, 470, 90, 30, N_GUI_SHAPE_ROUNDED, on_send_click, NULL);
    n_gui_button_set_keycode(gui, btn_send, ALLEGRO_KEY_ENTER, 0);

    /* server: connected clients window */
    if (mode == 0) {
        win_clients = n_gui_add_window(gui, "Connected Clients", 800, 10, 290, 580);
        n_gui_window_set_flags(gui, win_clients, N_GUI_WIN_RESIZABLE | N_GUI_WIN_FIXED_POSITION);

        lbl_clients_header = n_gui_add_label(gui, win_clients, "Connected clients: 0",
                                             10, 10, 270, 20, N_GUI_ALIGN_LEFT);

        listbox_clients = n_gui_add_listbox(gui, win_clients, 10, 40, 270, 500, N_GUI_SELECT_NONE, NULL, NULL);
    }

    /* network setup */
    int net_ok = FALSE;
    if (mode == 0) {
        /* server */
        pool = netw_new_pool(128);
        if (!pool) {
            n_gui_label_set_text(gui, lbl_status, "Server: memory allocation failed");
            chat_log_add("Failed to allocate server connection pool");
        } else if (netw_make_listening(&netw_server, NULL, port_str, 10, NETWORK_IPALL) == TRUE) {
            netw_set_blocking(netw_server, 0);
            net_ok = TRUE;
            char buf[128] = "";
            snprintf(buf, sizeof(buf), "Server listening on port %s", port_str);
            n_gui_label_set_text(gui, lbl_status, buf);
            chat_log_add(buf);
        } else {
            n_gui_label_set_text(gui, lbl_status, "Server: bind failed");
            chat_log_add("Failed to bind server socket");
        }
    } else {
        /* client */
        if (netw_connect(&netw_client, server_addr, port_str, NETWORK_IPALL) == TRUE) {
            netw_start_thr_engine(netw_client);
            net_ok = TRUE;
            char buf[128] = "";
            snprintf(buf, sizeof(buf), "Connected to %s:%s", server_addr, port_str);
            n_gui_label_set_text(gui, lbl_status, buf);
            chat_log_add(buf);
        } else {
            n_gui_label_set_text(gui, lbl_status, "Client: connection failed");
            chat_log_add("Failed to connect to server");
        }
    }

    al_start_timer(timer);
    int redraw = 1;

    while (!DONE) {
        ALLEGRO_EVENT ev;
        al_wait_for_event(event_queue, &ev);

        if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
            DONE = 1;
            continue;
        }
        if (ev.type == ALLEGRO_EVENT_KEY_DOWN && ev.keyboard.keycode == ALLEGRO_KEY_ESCAPE) {
            DONE = 1;
            continue;
        }
        if (ev.type == ALLEGRO_EVENT_TIMER) {
            redraw = 1;

            /* server: accept new connections */
            if (mode == 0 && net_ok && netw_server) {
                int retval = 0;
                NETWORK* new_client = netw_accept_from_ex(netw_server, 0, 0, -1, &retval);
                if (new_client) {
                    netw_start_thr_engine(new_client);
                    if (netw_pool_add(pool, new_client) != TRUE) {
                        /* Failed to track this client in the pool: close it to avoid leaks */
                        chat_log_add("Failed to add new client to pool, closing connection");
                        netw_close(&new_client);
                    } else {
                        char buf[128] = "";
                        snprintf(buf, sizeof(buf), "Client connected from %s (total: %zu)",
                                 new_client->link.ip ? new_client->link.ip : "?",
                                 netw_pool_nbclients(pool));
                        chat_log_add(buf);
                        n_gui_label_set_text(gui, lbl_status, buf);
                        refresh_clients_list();
                    }
                }
            }

            /* poll messages */
            if (mode == 0 && pool) {
                /* server: poll each client in pool */
                LIST* dead_clients = new_generic_list(MAX_LIST_ITEMS);
                if (!dead_clients) {
                    n_log(LOG_ERR, "Could not allocate dead_clients list");
                } else {
                    HT_FOREACH(node, pool->pool, {
                        NETWORK* cn = (NETWORK*)node->data.ptr;
                        if (cn) {
                            uint32_t state = 0;
                            int thr_state = 0;
                            netw_get_state(cn, &state, &thr_state);
                            if (state & NETW_EXITED || state & NETW_ERROR || state & NETW_EXIT_ASKED) {
                                list_push(dead_clients, cn, NULL);
                            } else {
                                poll_network_msgs(cn, cn->link.ip ? cn->link.ip : "client");
                            }
                        }
                    });
                    /* remove and close dead connections outside of the iteration */
                    int had_dead = 0;
                    list_foreach(dead_node, dead_clients) {
                        NETWORK* cn = (NETWORK*)dead_node->ptr;
                        char disc_buf[128] = "";
                        snprintf(disc_buf, sizeof(disc_buf), "Client %s:%s disconnected",
                                 cn->link.ip ? cn->link.ip : "?",
                                 cn->link.port ? cn->link.port : "?");
                        chat_log_add(disc_buf);
                        netw_pool_remove(pool, cn);
                        netw_close(&cn);
                        had_dead = 1;
                    }
                    if (had_dead) {
                        refresh_clients_list();
                        char sbuf[64] = "";
                        snprintf(sbuf, sizeof(sbuf), "Connected clients: %zu", netw_pool_nbclients(pool));
                        n_gui_label_set_text(gui, lbl_status, sbuf);
                    }
                    list_destroy(&dead_clients);
                }
            } else if (mode == 1 && netw_client) {
                poll_network_msgs(netw_client, "server");
                /* check connection status */
                uint32_t state = 0;
                int thr_state = 0;
                netw_get_state(netw_client, &state, &thr_state);
                if (state & NETW_EXITED || state & NETW_ERROR) {
                    n_gui_label_set_text(gui, lbl_status, "Disconnected from server");
                    chat_log_add("Disconnected from server");
                    netw_close(&netw_client);
                }
            }
        }

        n_gui_process_event(gui, ev);

        if (redraw && al_is_event_queue_empty(event_queue)) {
            redraw = 0;
            al_clear_to_color(al_map_rgb(40, 40, 50));
            n_gui_draw(gui);
            al_flip_display();
        }
    }

    /* cleanup */
    if (netw_client) {
        netw_close(&netw_client);
    }
    if (pool) {
        /* netw_destroy_pool() does not close NETWORK objects; its internal
         * destroy callback only logs. Collect all pool clients first, then
         * close each one (netw_close also removes the client from the pool
         * via netw_pool_remove), and finally destroy the now-empty pool. */
        LIST* clients_to_close = new_generic_list(UNLIMITED_LIST_ITEMS);
        if (clients_to_close) {
            HT_FOREACH(node, pool->pool, {
                NETWORK* cn = (NETWORK*)node->data.ptr;
                if (cn) {
                    list_push(clients_to_close, cn, NULL);
                }
            });
            list_foreach(cnode, clients_to_close) {
                NETWORK* cn = (NETWORK*)cnode->ptr;
                netw_close(&cn);
            }
            list_destroy(&clients_to_close);
        } else {
            n_log(LOG_ERR, "failed to allocate client list for pool cleanup; pool clients may leak");
        }
        netw_destroy_pool(&pool);
    }
    if (netw_server) {
        netw_close(&netw_server);
    }
    netw_unload();

    n_gui_destroy_ctx(&gui);
    al_destroy_font(font);
    al_destroy_timer(timer);
    al_destroy_event_queue(event_queue);
    al_destroy_display(display);

    FreeNoLog(server_addr);
    FreeNoLog(port_str);

    return 0;
}
