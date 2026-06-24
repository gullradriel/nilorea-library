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
 *@example ex_network_ssl.c
 *@brief Network module example, interacting with SSL
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/09/2024
 */

#include "nilorea/n_list.h"
#include "nilorea/n_str.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_thread_pool.h"
#include "nilorea/n_signals.h"

#include <sys/stat.h>

char* port = NULL;
char* addr = NULL;
char* key = NULL;
char* cert = NULL;
char* ca_file = NULL;
char* root_dir = NULL;
LIST* routes = NULL;
int ip_version = NETWORK_IPALL;
int max_http_request_size = 16384;
int ssl_verify = 0;
int max_connections = 0; /* 0 = unlimited */
bool done = 0;

NETWORK *server = NULL, /*! Network for server mode, accepting incomming */
    *netw = NULL;       /*! Network for managing connections */

void usage(void) {
    fprintf(stderr,
            "     -p 'port'            : set the https server port\n"
            "     -k 'key file'        : SSL key file path\n"
            "     -c 'cert file'       : SSL certificate file path\n"
            "     -A 'CA file'         : optional, CA certificate file for chain verification\n"
            "     -e                   : optional, enable SSL peer certificate verification\n"
            "     -a 'address name/ip' : optional, specify where to bind interface\n"
            "     -i 'ipmode'          : optional, force 'ipv4' or 'ipv6', default supports both\n"
            "     -s 'size'            : optional, maximum http request size (default: %d)\n"
            "     -n 'count'           : optional, exit after count connections (0 = unlimited, default)\n"
            "     -d 'html root'       : optional, specify a different http root dir (default: ./DATAS/)\n"
            "     -v                   : version\n"
            "     -h                   : help\n"
            "     -V 'log level'       : optional, set the log level (default: LOG_ERR)\n",
            max_http_request_size);
}

void process_args(int argc_nb, char** argv_ptr, char** addr_ptr, char** port_ptr, char** key_ptr, char** cert_ptr, char** ca_file_ptr, int* ssl_verify_ptr, LIST* routes_ptr, int* ip_version_ptr, int* max_http_request_size_ptr, char** root_dir_ptr) {
    int getoptret = 0,
        log_level = LOG_ERR; /* default log level */

    if (argc_nb == 1) {
        fprintf(stderr, "No arguments given, help:\n");
        usage();
        exit(1);
    }
    while ((getoptret = getopt(argc_nb, argv_ptr, "hven:s:V:p:i:a:r:k:c:s:d:A:")) != EOF) {
        switch (getoptret) {
            case 'n':
                max_connections = atoi(optarg);
                break;
            case 'i':
                if (!strcmp("v4", optarg)) {
                    (*ip_version_ptr) = NETWORK_IPV4;
                    n_log(LOG_NOTICE, "IPV4 selected");
                } else if (!strcmp("v6", optarg)) {
                    (*ip_version_ptr) = NETWORK_IPV6;
                    n_log(LOG_NOTICE, "IPV6 selected");
                } else {
                    n_log(LOG_NOTICE, "IPV4/6 selected");
                }
                break;
            case 'v':
                fprintf(stderr, "Date de compilation : %s a %s.\n", __DATE__, __TIME__);
                exit(1);
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 8)) {
                    log_level = LOG_NULL;
                } else {
                    if (!strncmp("LOG_NOTICE", optarg, 10)) {
                        log_level = LOG_NOTICE;
                    } else {
                        if (!strncmp("LOG_INFO", optarg, 8)) {
                            log_level = LOG_INFO;
                        } else {
                            if (!strncmp("LOG_ERR", optarg, 7)) {
                                log_level = LOG_ERR;
                            } else {
                                if (!strncmp("LOG_DEBUG", optarg, 9)) {
                                    log_level = LOG_DEBUG;
                                } else {
                                    fprintf(stderr, "%s n'est pas un niveau de log valide.\n", optarg);
                                    exit(-1);
                                }
                            }
                        }
                    }
                }
                break;
            case 'e':
                (*ssl_verify_ptr) = 1;
                break;
            case 'p':
                (*port_ptr) = strdup(optarg);
                break;
            case 'r':
                list_push(routes_ptr, strdup(optarg), &free);
                break;
            case 'a':
                (*addr_ptr) = strdup(optarg);
                break;
            case 'k':
                (*key_ptr) = strdup(optarg);
                break;
            case 'c':
                (*cert_ptr) = strdup(optarg);
                break;
            case 'A':
                (*ca_file_ptr) = strdup(optarg);
                break;
            case 's':
                (*max_http_request_size_ptr) = atoi(optarg);
                break;
            case 'd':
                (*root_dir_ptr) = strdup(optarg);
                break;
            default:
            case '?': {
                if (optopt == 'd') {
                    fprintf(stderr, "\n      Missing html root directory\n");
                }
                if (optopt == 's') {
                    fprintf(stderr, "\n      Missing max http size string\n");
                }
                if (optopt == 'k') {
                    fprintf(stderr, "\n      Missing key file string\n");
                }
                if (optopt == 'c') {
                    fprintf(stderr, "\n      Missing certificate file string\n");
                }
                if (optopt == 'A') {
                    fprintf(stderr, "\n      Missing CA file string\n");
                }
                if (optopt == 'r') {
                    fprintf(stderr, "\n      Missing route string\n");
                }
                if (optopt == 'a') {
                    fprintf(stderr, "\n      Missing binding host/addr string\n");
                }
                if (optopt == 'i') {
                    fprintf(stderr, "\n      Missing ip version (v4 or v6) string \n");
                } else if (optopt == 'V') {
                    fprintf(stderr, "\n      Missing log level string\n");
                } else if (optopt == 'p') {
                    fprintf(stderr, "\n      Missing port\n");
                } else if (optopt != 's') {
                    fprintf(stderr, "\n      Unknow missing option %c\n", optopt);
                }
                usage();
                exit(1);
            }
            case 'h': {
                usage();
                exit(1);
            }
        }
    }
    set_log_level(log_level);
} /* void process_args( ... ) */

/* Exit handling  */
void action_on_sig(int recvd_signal) {
    (void)recvd_signal;
#ifndef __windows__
    static int nb_sigterm = 0;
    switch (recvd_signal) {
        /* We should not use these signals as they make the debugging going funky */
        case (SIGABRT):
            n_log(LOG_ERR, "Caught SIGABRT !");
            break;
        case (SIGINT):
            n_log(LOG_ERR, "Caught SIGINT !");
            break;
        case (SIGBUS):
            n_log(LOG_ERR, "Caught SIGBUS !");
            break;
        case (SIGFPE):
            n_log(LOG_ERR, "Caught SIGFPE !");
            break;
        case (SIGSEGV):
            n_log(LOG_ERR, "Caught SIGSEGV !");
            break;
        case (SIGSYS):
            n_log(LOG_ERR, "Caught SIGSYS !");
            break;
        case (SIGTERM):
            nb_sigterm++;
            if (nb_sigterm >= 2) {
                n_log(LOG_ERR, "Caught too much SIGTERM, trying _exit() !!");
                _exit(-1);
            }
            n_log(LOG_ERR, "Caught %d SIGTERM, exiting now !!", nb_sigterm);
            exit(-1);
        case (SIGUSR1):
            done = TRUE;
            n_log(LOG_ERR, "Caught SIGUSR1 !");
            break;
        case (SIGUSR2):
            done = TRUE;
            n_log(LOG_ERR, "Caught SIGUSR2 !");
            break;
        case (SIGHUP):
            n_log(LOG_NOTICE, "Caught SIGHUP !");
            break;
        default:
            n_log(LOG_ERR, "Caught unknow signal %d", recvd_signal);
            break;
    }
#endif
} /* action_on_sig() */

// Function to handle different URLs and return appropriate responses
void handle_request(NETWORK* netw_ptr, LIST* routes_ptr) {
    __n_assert(netw_ptr, return);
    __n_assert(routes_ptr, return);

    bool found = 0;
    char** split_results = NULL;
    char* http_url = NULL;
    N_STR* dynamic_request_answer = NULL;

    // Read request
    char* http_buffer = NULL;
    Alloca(http_buffer, (size_t)(max_http_request_size + 1));
    __n_assert(http_buffer, netw_close(&netw_ptr); return);

    // SSL_read reads up to max_http_request_size bytes (variable-length HTTP request).
    // recv_ssl_data is intentionally NOT used here: it loops until the entire buffer is
    // full, which would block indefinitely since HTTP requests are smaller than the buffer.
    int ssl_read_ret = SSL_read(netw_ptr->ssl, http_buffer, max_http_request_size);
    if (ssl_read_ret <= 0) {
        int ssl_error = SSL_get_error(netw_ptr->ssl, ssl_read_ret);
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            n_log(LOG_INFO, "SSL_read: peer closed the connection cleanly");
        } else {
            n_log(LOG_ERR, "SSL_read failed with SSL error %d", ssl_error);
        }
        netw_close(&netw_ptr);
        return;
    }
    // n_log( LOG_DEBUG , "http_request: %s" , http_buffer );

    // Extract URL from the request
    char url[4096] = "";
    netw_get_url_from_http_request(http_buffer, url, sizeof(url));
    n_log(LOG_DEBUG, "url: %s", url);

    // Handle the request based on the URL
    N_STR* origin = new_nstr(32);
    nstrprintf(origin, "%s:" SOCKET_SIZE_FORMAT, _str(netw_ptr->link.ip), netw_ptr->link.sock);

    NETWORK_HTTP_INFO http_request = netw_extract_http_info(http_buffer);
    N_STR* http_body = NULL;

    split_results = split(url, "?", 0);
    if (!split_results || !split_results[0]) {
        http_body = char_to_nstr("<html><body><h1>Bad Request</h1></body></html>");
        if (netw_build_http_response(&dynamic_request_answer, 400, "ex_network_ssl server", netw_guess_http_content_type(url), "", http_body) == FALSE) {
            n_log(LOG_ERR, "couldn't build a Bad Request answer for %s", url);
        }
        n_log(LOG_ERR, "%s: %s %s 400", _nstr(origin), http_request.type, url);
    } else {
        http_url = split_results[0];
        n_log(LOG_INFO, "%s: %s %s request...", _nstr(origin), http_request.type, url);
        if (strcmp("OPTIONS", http_request.type) == 0) {
            if (netw_build_http_response(&dynamic_request_answer, 200, "ex_network_ssl server", netw_guess_http_content_type(url), "Allow: OPTIONS, GET, POST\r\n", NULL) == FALSE) {
                n_log(LOG_ERR, "couldn't build an OPTION answer for %s", url);
            }
            n_log(LOG_INFO, "%s: %s %s 200", _nstr(origin), http_request.type, url);
        } else if (strcmp("GET", http_request.type) == 0) {
            char system_url[4096] = "";
            // example assume a root dir at DATAS
            if (!root_dir) {
                snprintf(system_url, sizeof(system_url), "./DATAS%s", http_url);
            } else {
                snprintf(system_url, sizeof(system_url), "%s%s", root_dir, http_url);
            }

            n_log(LOG_DEBUG, "%s: searching for file %s...", _nstr(origin), system_url);

            struct stat st;
            if (stat(system_url, &st) == 0 && S_ISREG(st.st_mode)) {
                n_log(LOG_DEBUG, "%s: file %s found !", _nstr(origin), system_url);
                http_body = file_to_nstr(system_url);
                if (!http_body) {
                    http_body = char_to_nstr("<html><body><h1>Internal Server Error</h1></body></html>");
                    if (netw_build_http_response(&dynamic_request_answer, 500, "ex_network_ssl server", netw_guess_http_content_type(url), "", http_body) == FALSE) {
                        n_log(LOG_ERR, "couldn't build an Internal Server Error answer for %s", url);
                    }
                    n_log(LOG_ERR, "%s: %s %s 500", _nstr(origin), http_request.type, url);
                } else {
                    if (netw_build_http_response(&dynamic_request_answer, 200, "ex_network_ssl server", netw_guess_http_content_type(url), "", http_body) == FALSE) {
                        n_log(LOG_ERR, "couldn't build an http answer for %s", url);
                    }
                    n_log(LOG_INFO, "%s: %s %s 200", _nstr(origin), http_request.type, url);
                }
            } else if (stat(system_url, &st) == 0 && S_ISDIR(st.st_mode)) {
                /* Directory: check for index.html and redirect */
                char index_path[4096 + 16] = "";
                size_t url_len = strlen(http_url);
                const char* slash = (url_len > 0 && http_url[url_len - 1] == '/') ? "" : "/";
                snprintf(index_path, sizeof(index_path), "%s%sindex.html", system_url, slash);
                struct stat idx_st;
                if (stat(index_path, &idx_st) == 0 && S_ISREG(idx_st.st_mode)) {
                    char location_header[4096] = "";
                    snprintf(location_header, sizeof(location_header), "Location: %s%sindex.html\r\n", http_url, slash);
                    if (netw_build_http_response(&dynamic_request_answer, 301, "ex_network_ssl server", "text/html", location_header, NULL) == FALSE) {
                        n_log(LOG_ERR, "couldn't build a redirect answer for %s", url);
                    }
                    n_log(LOG_INFO, "%s: %s %s 301 -> %s%sindex.html", _nstr(origin), http_request.type, url, http_url, slash);
                } else {
                    http_body = char_to_nstr("<html><body><h1>404 Not Found</h1></body></html>");
                    if (netw_build_http_response(&dynamic_request_answer, 404, "ex_network_ssl server", netw_guess_http_content_type(url), "", http_body) == FALSE) {
                        n_log(LOG_ERR, "couldn't build a NOT FOUND answer for %s", url);
                    }
                    n_log(LOG_ERR, "%s: %s %s 404", _nstr(origin), http_request.type, url);
                }
            } else {
                http_body = char_to_nstr("<html><body><h1>404 Not Found</h1></body></html>");
                if (netw_build_http_response(&dynamic_request_answer, 404, "ex_network_ssl server", netw_guess_http_content_type(url), "", http_body) == FALSE) {
                    n_log(LOG_ERR, "couldn't build a NOT FOUND answer for %s", url);
                }
                n_log(LOG_ERR, "%s: %s %s 404", _nstr(origin), http_request.type, url);
            }
        } else if (strcmp("POST", http_request.type) == 0) {
            // Parse virtual route
            found = 0;
            list_foreach(node, routes_ptr) {
                if (strcmp(node->ptr, http_url) == 0) {
                    // Handle 200 OK from virtual route
                    HASH_TABLE* post_data = netw_parse_post_data(http_request.body);
                    if (post_data) {
                        HT_FOREACH(hnode, post_data,
                                   {
                                       n_log(LOG_DEBUG, "%s: POST DATA: %s=%s", _nstr(origin), hnode->key, (char*)hnode->data.ptr);
                                   });
                        destroy_ht(&post_data);
                    }
                    http_body = char_to_nstr("{\"status\":\"ok\"}");
                    if (netw_build_http_response(&dynamic_request_answer, 200, "ex_network_ssl server", "application/json", "", http_body) == FALSE) {
                        n_log(LOG_ERR, "couldn't build a route 200 answer for %s", url);
                    }
                    found = 1;
                    n_log(LOG_INFO, "%s: %s virtual:%s 200", _nstr(origin), http_request.type, url);
                    break;
                }
            }
            if (!found) {
                http_body = char_to_nstr("<html><body><h1>404 Not Found</h1></body></html>");
                if (netw_build_http_response(&dynamic_request_answer, 404, "ex_network_ssl server", netw_guess_http_content_type(url), "", http_body) == FALSE) {
                    n_log(LOG_ERR, "couldn't build a NOT FOUND answer for %s", url);
                }
                n_log(LOG_ERR, "%s: %s %s 404", _nstr(origin), http_request.type, url);
            }
        } else {
            http_body = char_to_nstr("<html><body><h1>Bad Request</h1></body></html>");
            if (netw_build_http_response(&dynamic_request_answer, 400, "ex_network_ssl server", netw_guess_http_content_type(url), "", http_body) == FALSE) {
                n_log(LOG_ERR, "couldn't build a Bad Request answer for %s", url);
            }
            n_log(LOG_ERR, "%s: %s %s 400", _nstr(origin), http_request.type, url);
        }
        free_split_result(&split_results);
    }
    if (dynamic_request_answer) {
        if (dynamic_request_answer->written > UINT32_MAX) {
            n_log(LOG_ERR, "response too large to send for %s: %s %s (size: %zu)", _nstr(origin), http_request.type, url, dynamic_request_answer->written);
        } else if (send_ssl_data(netw_ptr, dynamic_request_answer->data, (uint32_t)dynamic_request_answer->written) < 0) {
            n_log(LOG_ERR, "failed to send response for %s: %s %s", _nstr(origin), http_request.type, url);
        }
        free_nstr(&dynamic_request_answer);
    } else {
        n_log(LOG_ERR, "couldn't build an answer for %s: %s %s", _nstr(origin), http_request.type, url);
    }
    netw_info_destroy(http_request);
    free_nstr(&origin);
    free_nstr(&http_body);
} /* handle_request */

/*! structure of a NETWORK_SSL_THREAD_PARAMS */
typedef struct NETWORK_SSL_THREAD_PARAMS {
    /*! network to use for the receiving thread */
    NETWORK* netw;
    /*! virtual routes for the server */
    LIST* routes;
} NETWORK_SSL_THREAD_PARAMS;

void* ssl_network_thread(void* params) {
    __n_assert(params, return NULL);
    NETWORK_SSL_THREAD_PARAMS* ssl_params = (NETWORK_SSL_THREAD_PARAMS*)params;
    handle_request(ssl_params->netw, ssl_params->routes);
    netw_close(&ssl_params->netw);
    Free(ssl_params);
    return NULL;
}

int main(int argc, char* argv[]) {
    int exit_code = 0;
    THREAD_POOL* thread_pool = NULL;
    routes = new_generic_list(MAX_LIST_ITEMS);
    __n_assert(routes, n_log(LOG_ERR, "could not allocate list !"); exit(1));

    /* processing args and set log_level */
    process_args(argc, argv, &addr, &port, &key, &cert, &ca_file, &ssl_verify, routes, &ip_version, &max_http_request_size, &root_dir);

    if (!port) {
        n_log(LOG_ERR, "No port given. Exiting.");
        exit_code = 1;
        goto clean_and_exit;
    }
    if (!key) {
        n_log(LOG_ERR, "No key given. Exiting.");
        exit_code = 1;
        goto clean_and_exit;
    }
    if (!cert) {
        n_log(LOG_ERR, "No certificate given. Exiting.");
        exit_code = 1;
        goto clean_and_exit;
    }
    /*
    if (routes->nb_items == 0) {
        n_log(LOG_ERR, "No route given. Exiting.");
        exit_code = 1;
        goto clean_and_exit;
    }
    */

#ifndef __windows__
    errno = 0;
    signal(SIGPIPE, SIG_IGN);
    /* initializing signal catching */
    struct sigaction signal_catcher;

    /* quit on sig */
    signal_catcher.sa_handler = action_on_sig;
    sigemptyset(&signal_catcher.sa_mask);
    signal_catcher.sa_flags = 0;

    sigaction(SIGTERM, &signal_catcher, NULL);
    sigaction(SIGUSR1, &signal_catcher, NULL);
#endif

    long int cores = get_nb_cpu_cores();
    int nb_active_threads = (cores > 0) ? (int)cores : 1;
    int nb_waiting_threads = 10 * nb_active_threads;
    n_log(LOG_INFO, "Creating a new thread pool of %d active and %d waiting threads", nb_active_threads, nb_waiting_threads);
    thread_pool = new_thread_pool((size_t)nb_active_threads, (size_t)nb_waiting_threads);

    n_log(LOG_INFO, "Creating listening network for %s:%s %d", _str(addr), _str(port), ip_version);
    /* create listening network */
    if (netw_make_listening(&server, addr, port, SOMAXCONN, ip_version) == FALSE) {
        n_log(LOG_ERR, "Fatal error with network initialization");
        exit(-1);
    }

    if (ca_file) {
        n_log(LOG_INFO, "Using SSL with certificate chain verification (CA: %s)", ca_file);
        netw_set_crypto_chain(server, key, cert, ca_file);
    } else {
        netw_set_crypto(server, key, cert);
    }
    if (ssl_verify) {
        n_log(LOG_INFO, "SSL peer certificate verification enabled");
        netw_ssl_set_verify(server, 1);
    }
    int accepted_count = 0;
    while (!done) {
        n_log(LOG_DEBUG, "Blocking on accept...");
        /* get any accepted client on a network */
        int return_code = 0;
        netw = netw_accept_from_ex(server, 0, 0, 0, &return_code);
        if (!netw) {
            if (return_code == EINTR) {
                n_log(LOG_INFO, "accept exited after catching a signal");
                goto clean_and_exit;
            } else {
                n_log(LOG_ERR, "error on accept, NULL netw returned !");
            }
        } else {
            n_log(LOG_INFO, "accepted SSL connection on socket %d", netw->link.sock);
            NETWORK_SSL_THREAD_PARAMS* netw_ssl_params = NULL;
            Malloc(netw_ssl_params, NETWORK_SSL_THREAD_PARAMS, 1);
            netw_ssl_params->netw = netw;
            netw_ssl_params->routes = routes;
            if (add_threaded_process(thread_pool, &ssl_network_thread, (void*)netw_ssl_params, DIRECT_PROC) == FALSE) {
                n_log(LOG_ERR, "Error adding client management to thread pool");
            }
            accepted_count++;
            if (max_connections > 0 && accepted_count >= max_connections) {
                n_log(LOG_NOTICE, "reached %d connections, exiting", max_connections);
                break;
            }
        }
    }
clean_and_exit:
    if (thread_pool) {
        wait_for_threaded_pool(thread_pool);
        destroy_threaded_pool(&thread_pool, 1000);
    }
    netw_close(&server);
    netw_unload();
    list_destroy(&routes);
    FreeNoLog(addr);
    FreeNoLog(port);
    FreeNoLog(key);
    FreeNoLog(cert);
    FreeNoLog(ca_file);
    FreeNoLog(root_dir);
    exit(exit_code);
}
