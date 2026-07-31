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
 *@example ex_network_sni.c
 *@brief MITM-style SNI accept: server mints a per-host leaf, client checks it and evaluates its trust
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_str.h"
#include "nilorea/n_x509.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/x509.h>

/*! the host name the client requests via SNI */
#define SNI_HOST "test.host.example"
/*! fixed exchange message length (both messages are 17 chars + NUL) */
#define MSG_LEN 18

/*! CA used by the server to mint per-host leaves */
typedef struct CA_CTX {
    N_STR* cert;
    N_STR* key;
} CA_CTX;

/*! server worker context */
typedef struct SRV {
    NETWORK* listen;
    CA_CTX* ca;
    int ok;
} SRV;

/* Parse the -V LOG_LEVEL verbosity argument. */
void process_args(int argc, char** argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hvV:")) != EOF) {
        switch (opt) {
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 8))
                    set_log_level(LOG_NULL);
                else if (!strncmp("LOG_NOTICE", optarg, 10))
                    set_log_level(LOG_NOTICE);
                else if (!strncmp("LOG_INFO", optarg, 8))
                    set_log_level(LOG_INFO);
                else if (!strncmp("LOG_ERR", optarg, 7))
                    set_log_level(LOG_ERR);
                else if (!strncmp("LOG_DEBUG", optarg, 9))
                    set_log_level(LOG_DEBUG);
                else {
                    fprintf(stderr, "Unknown log level %s\n", optarg);
                    exit(1);
                }
                break;
            case 'v':
                fprintf(stderr, "ex_network_sni\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

/* SNI pick callback: mint a leaf for the requested host signed by the CA. */
static int pick_cb(const char* sni, N_STR** cert_pem, N_STR** key_pem, void* user_data) {
    CA_CTX* ca = (CA_CTX*)user_data;
    const char* host = (sni && sni[0]) ? sni : "localhost";
    n_log(LOG_DEBUG, "SNI pick for host '%s'", host);
    return n_x509_mint_host_cert(host, ca->cert, ca->key, 825, cert_pem, key_pem);
}

/* Server thread: accept one SNI connection, echo one message, close. */
static void* server_fn(void* p) {
    SRV* s = (SRV*)p;
    NETWORK* c = netw_accept_ssl_with_sni_cb(s->listen, pick_cb, s->ca);
    if (c) {
        char buf[64] = "";
        char reply[] = "hello-from-server";
        /* read exactly MSG_LEN, then reply MSG_LEN: recv_ssl_data waits for the
         * full count, so both sides must agree on the size or they deadlock */
        if (recv_ssl_data(c, buf, (uint32_t)MSG_LEN) > 0)
            n_log(LOG_NOTICE, "server received: %s", buf);
        send_ssl_data(c, reply, (uint32_t)MSG_LEN);
        s->ok = 1;
        netw_close(&c);
    }
    return NULL;
}

/* Bind a loopback listening socket on the first free port in a range. */
static NETWORK* bind_free(char* port_out, size_t port_out_len) {
    int p;
    for (p = 18300; p < 18400; p++) {
        NETWORK* l = NULL;
        char ps[16];
        snprintf(ps, sizeof(ps), "%d", p);
        if (netw_make_listening(&l, "127.0.0.1", ps, 5, NETWORK_IPALL) == TRUE && l) {
            snprintf(port_out, port_out_len, "%s", ps);
            return l;
        }
        if (l)
            netw_close(&l);
    }
    return NULL;
}

int main(int argc, char** argv) {
    CA_CTX ca = {NULL, NULL};
    NETWORK* listen = NULL;
    NETWORK* cli = NULL;
    SRV srv;
    pthread_t th;
    char port[16] = "";
    int failures = 0;

    set_log_level(LOG_NOTICE);
    process_args(argc, argv);

    /* initialize OpenSSL once in the main thread: the server and client threads
     * both use TLS, and the lazy init guard is not synchronized */
    netw_init_openssl();

    if (n_x509_generate_ca("Nilorea SNI Test CA", 3650, &ca.cert, &ca.key) != 0 || !ca.cert || !ca.key) {
        n_log(LOG_ERR, "CA generation failed");
        return 1;
    }

    listen = bind_free(port, sizeof(port));
    if (!listen) {
        n_log(LOG_ERR, "could not bind a loopback port");
        free_nstr(&ca.cert);
        free_nstr(&ca.key);
        return 1;
    }

    srv.listen = listen;
    srv.ca = &ca;
    srv.ok = 0;
    pthread_create(&th, NULL, server_fn, &srv);

    if (netw_ssl_connect_client_to(&cli, "127.0.0.1", port, NETWORK_IPALL, 5000) != TRUE || !cli) {
        n_log(LOG_ERR, "client connect failed");
        failures++;
    } else {
        netw_ssl_set_verify(cli, 0);
        if (netw_ssl_do_handshake(cli, SNI_HOST) != TRUE) {
            n_log(LOG_ERR, "client TLS handshake failed");
            failures++;
        } else {
            char msg[] = "hello-from-client";
            char buf[64] = "";
            X509* peer = NULL;
            send_ssl_data(cli, msg, (uint32_t)MSG_LEN);
            if (recv_ssl_data(cli, buf, (uint32_t)MSG_LEN) > 0)
                n_log(LOG_NOTICE, "client received: %s", buf);

            peer = SSL_get1_peer_certificate(cli->ssl);
            if (!peer) {
                n_log(LOG_ERR, "no server certificate presented");
                failures++;
            } else {
                char cn[256] = "";
                X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, cn, sizeof(cn));
                n_log(LOG_NOTICE, "server presented a certificate with CN=%s", cn);
                if (strcmp(cn, SNI_HOST) != 0) {
                    n_log(LOG_ERR, "certificate CN does not match the requested SNI host");
                    failures++;
                }
                X509_free(peer);
            }
            {
                char vrerr[128] = "";
                int trusted = netw_ssl_get_verify_result(cli, SNI_HOST, vrerr, sizeof(vrerr));
                /* the leaf is signed by a private test CA absent from the system
                 * trust store, so a trust evaluation must fail with a reason */
                n_log(LOG_NOTICE, "verify result: trusted=%d reason='%s'", trusted, vrerr);
                if (trusted != FALSE) {
                    n_log(LOG_ERR, "private-CA leaf was unexpectedly trusted");
                    failures++;
                }
                if (vrerr[0] == '\0') {
                    n_log(LOG_ERR, "verify failure did not report a reason");
                    failures++;
                }
            }
        }
    }

    netw_close(&cli);
    pthread_join(th, NULL);
    if (!srv.ok) {
        n_log(LOG_ERR, "server did not complete the exchange");
        failures++;
    }
    netw_close(&listen);
    free_nstr(&ca.cert);
    free_nstr(&ca.key);
    netw_unload_openssl();

    if (failures) {
        n_log(LOG_ERR, "ex_network_sni: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_network_sni: all checks passed");
    return 0;
}
