/**
 *@example ex_network.c
 *@brief Network module example
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/05/2015
 */

#include <stdio.h>
#include <errno.h>

#include "ex_network.h"

#define SERVER 0
#define CLIENT 1

int NB_ATTEMPTS = 3;

NETWORK *netw_server = NULL, /*! Network for server mode, accepting incomming */
    *netw = NULL;            /*! Network for managing conenctions */

int mode = -1, ip_mode = NETWORK_IPALL;

static pthread_t netw_thr;

void usage(void) {
    fprintf(stderr,
            "     -a 'serveur address name/ip to bind (server mode) (optionnal)\n"
            "     -s 'serveur address name/ip to connect (client mode)\n"
            "     -p 'port': TCP port to use, server or client\n"
            "     -c 'SSL certificate file': optional, certificale file to enable SSL encryption\n"
            "     -k 'SSL key file': optional, key file to enable SSL encrpytion\n"
            "     -i 'ipmode': optional, ip version to use. Default support both ipv4 and ipv6. Values: ipv4, ipv6\n"
            "     -h: show that help file\n"
            "     -v: show the program version\n"
            "     -V 'log level': optional, set the log level. Default: LOG_ERR, values: LOG_INFO, LOG_NOTICE, LOG_ERR, LOG_DEBUG\n");
}

void process_args(int argc, char** argv, char** address, char** server, char** port, int* nb, int* ip_version, char** ssl_key_file, char** ssl_cert_file) {
    int getoptret = 0,
        log_level = LOG_ERR; /* default log level */

    /* Arguments optionnels */
    /* -v version
     * -V log level
     * -h help
     * -a address name/ip to bind to (server mode, NULL if not specified)
     * -s serveur address name/ip to connect (client mode)
     * -p port
     * -i v4 ip version (default support both ipv4 and ipv6 )
     * -i v6 ip version (   "       "     "    "    "   "   )
     */
    if (argc == 1) {
        fprintf(stderr, "No arguments given, help:\n");
        usage();
        exit(1);
    }
    while ((getoptret = getopt(argc, argv, "hvs:V:p:n:i:a:c:k:")) != EOF) {
        switch (getoptret) {
            case 'i':
                if (!strcmp("v4", optarg)) {
                    (*ip_version) = NETWORK_IPV4;
                    n_log(LOG_NOTICE, "IPV4 selected");
                } else if (!strcmp("v6", optarg)) {
                    (*ip_version) = NETWORK_IPV6;
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
            case 'k':
                (*ssl_key_file) = strdup(optarg);
                break;
            case 'c':
                (*ssl_cert_file) = strdup(optarg);
                break;
            case 's':
                (*server) = strdup(optarg);
                break;
            case 'a':
                (*address) = strdup(optarg);
                break;
            case 'n':
                (*nb) = atoi(optarg);
                break;
            case 'p':
                (*port) = strdup(optarg);
                break;
            default:
            case '?': {
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

int main(int argc, char** argv) {
    char* addr = NULL;
    char* srv = NULL;
    char* port = NULL;
    char* ssl_key_file = NULL;
    char* ssl_cert_file = NULL;

    set_log_level(LOG_DEBUG);

    /* processing args and set log_level */
    process_args(argc, argv, &addr, &srv, &port, &NB_ATTEMPTS, &ip_mode, &ssl_key_file, &ssl_cert_file);

    if (!port) {
        n_log(LOG_ERR, "No port given. Exiting.");
        exit(-1);
    }

    if (srv && addr) {
        n_log(LOG_ERR, "Please specify only one of the following options: -a (server, addr to bind to) or -s (server on which to connect to)");
    }

    if (srv) {
        n_log(LOG_INFO, "Client mode, connecting to %s:%s", srv, port);
        mode = CLIENT;
    } else {
        n_log(LOG_INFO, "Server mode , waiting client on port %s", port);
        mode = SERVER;
    }

#ifdef __linux__
    if (sigchld_handler_installer() == FALSE) {
        exit(-1);
    }
#endif
    if (mode == SERVER) {
        n_log(LOG_INFO, "Creating listening network for %s:%s %d", _str(addr), _str(port), ip_mode);
        /* create listening network */
        if (netw_make_listening(&netw_server, addr, port, 10, ip_mode) == FALSE) {
            n_log(LOG_ERR, "Fatal error with network initialization");
            netw_unload();
            exit(-1);
        }

#ifdef HAVE_OPENSSL
        if (ssl_key_file && ssl_cert_file) {
            netw_set_crypto(netw_server, ssl_key_file, ssl_cert_file);
        }
#endif

        int it = 0;
        for (it = 0; it < NB_ATTEMPTS / 2; it++) {
            n_log(LOG_INFO, "Blocking on accept...");
            /* get any accepted client on a network */
            if (!(netw = netw_accept_from(netw_server))) {
                n_log(LOG_ERR, "Error on accept");
            } else {
                /* someone is connected. starting some dialog */
                int error = 0;
                int pthread_error = 0;
                errno = 0;
                pthread_error = pthread_create(&netw_thr, NULL, &manage_client, (void*)netw);
                error = errno;
                if (pthread_error != 0) {
                    n_log(LOG_ERR, "Error creating client management pthread:%d , error: %s", pthread_error, strerror(error));
                    netw_unload();
                    exit(-1);
                }
                pthread_join(netw_thr, NULL);
            }
        }
        /* testing with thread pool  && non blocking */
        int error = 0;
        THREAD_POOL* thread_pool = new_thread_pool(2, 128);
        while (it < NB_ATTEMPTS) {
            /* get any accepted client on a network */
            if ((netw = netw_accept_from_ex(netw_server, 0, 0, 0, &error))) {
                /* someone is connected. starting some dialog */
                if (add_threaded_process(thread_pool, &manage_client, (void*)netw, DIRECT_PROC) == FALSE) {
                    n_log(LOG_ERR, "Error adding client management to thread pool");
                }
                it++;
            } else {
                n_log(LOG_DEBUG, "Waiting connections...");
                u_sleep(250000);
            }
            refresh_thread_pool(thread_pool);
        }
        n_log(LOG_NOTICE, "Waiting thread_pool...");
        wait_for_threaded_pool(thread_pool, 500000);
        n_log(LOG_NOTICE, "Destroying thread_pool...");
        destroy_threaded_pool(&thread_pool, 500000);
        n_log(LOG_NOTICE, "Wait client close ack to close server");
        // setting a 30s wait close timeout to wait for last datas to be received by peer (optional)
        netw_setsockopt(netw_server, NETWORK_WAIT_CLOSE_TIMEOUT, 30);
        netw_close(&netw_server);
    } else if (mode == CLIENT) {
        for (int it = 1; it <= NB_ATTEMPTS; it++) {
            if (ssl_key_file && ssl_cert_file) {
#ifdef HAVE_OPENSSL
                if (netw_ssl_connect(&netw, srv, port, ip_mode, ssl_key_file, ssl_cert_file) != TRUE) {
                    /* there were some error when trying to connect */
                    n_log(LOG_ERR, "Unable to connect to %s:%s", srv, port);
                    netw_unload();
                    exit(1);
                } else {
                    n_log(LOG_NOTICE, "SSL Attempt %d: Connected to %s:%s", it, srv, port);
                }
#else
                n_log(LOG_ERR, "trying to use SSL with %s:%s, but program was compiled without OpenSSL support !!!");
                n_log(LOG_ERR, "ENCRYPTION IS DISABLED !!!");
                n_log(LOG_ERR, "EXITING !!!");
                netw_unload();
                exit(1);
#endif
            } else {
                if (netw_connect(&netw, srv, port, ip_mode) != TRUE) {
                    /* there were some error when trying to connect */
                    n_log(LOG_ERR, "Unable to connect to %s:%s", srv, port);
                    netw_unload();
                    exit(1);
                }
                n_log(LOG_NOTICE, "Attempt %d: Connected to %s:%s", it, srv, port);
            }

            /* backgrounding network send / recv */
            netw_start_thr_engine(netw);

            N_STR *sended_data = NULL, *recved_data = NULL, *hostname = NULL, *tmpstr = NULL;

            sended_data = char_to_nstr("SENDING DATAS...");
            send_net_datas(netw, sended_data);

            free_nstr(&sended_data);

            /* let's check for an answer: test each 250000 usec, with
             * a limit of 1000000 usec */
            n_log(LOG_INFO, "waiting for datas back from server...");
            tmpstr = netw_wait_msg(netw, 25000, 10000000);

            if (tmpstr) {
                get_net_datas(tmpstr, &hostname, &recved_data);
                n_log(LOG_NOTICE, "RECEIVED DATAS: %s - %s", recved_data->data, hostname->data);
                free_nstr(&tmpstr);
                free_nstr(&recved_data);
                free_nstr(&hostname);
            } else {
                n_log(LOG_ERR, "Error getting back answer from server");
            }
            n_log(LOG_NOTICE, "Closing client in 3 seconds. See synchronisation on server side...");
            sleep(3);
            netw_close(&netw);
        }
    }

    FreeNoLog(srv);
    FreeNoLog(addr);
    FreeNoLog(port)

        netw_unload();

    n_log(LOG_INFO, "Exiting network example");

    exit(0);
} /* END_OF_MAIN() */
