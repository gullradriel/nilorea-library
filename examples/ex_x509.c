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
 *@example ex_x509.c
 *@brief Nilorea Library n_x509 CA generation and per-host leaf minting test
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_x509.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

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
                fprintf(stderr, "ex_x509\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

/* Verify that leaf_pem chains to ca_pem. Returns 1 on success, 0 otherwise. */
static int verify_chain(const N_STR* ca_pem, const N_STR* leaf_pem) {
    BIO* bca = BIO_new_mem_buf(ca_pem->data, (int)ca_pem->written);
    BIO* bleaf = BIO_new_mem_buf(leaf_pem->data, (int)leaf_pem->written);
    X509* ca = bca ? PEM_read_bio_X509(bca, NULL, NULL, NULL) : NULL;
    X509* leaf = bleaf ? PEM_read_bio_X509(bleaf, NULL, NULL, NULL) : NULL;
    X509_STORE* store = X509_STORE_new();
    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    int ok = 0;
    if (ca && leaf && store && ctx) {
        X509_STORE_add_cert(store, ca);
        if (X509_STORE_CTX_init(ctx, store, leaf, NULL) == 1)
            ok = (X509_verify_cert(ctx) == 1);
    }
    if (ctx)
        X509_STORE_CTX_free(ctx);
    if (store)
        X509_STORE_free(store);
    if (leaf)
        X509_free(leaf);
    if (ca)
        X509_free(ca);
    if (bleaf)
        BIO_free(bleaf);
    if (bca)
        BIO_free(bca);
    return ok;
}

int main(int argc, char** argv) {
    N_STR* ca_cert = NULL;
    N_STR* ca_key = NULL;
    N_STR* key = NULL;
    const char* hosts[] = {"example.com", "127.0.0.1"};
    int failures = 0;
    size_t i;

    set_log_level(LOG_NOTICE);
    process_args(argc, argv);

    if (n_x509_generate_ca("Nilorea Test CA", 3650, &ca_cert, &ca_key) != 0 || !ca_cert || !ca_key) {
        n_log(LOG_ERR, "CA generation failed");
        failures++;
    } else {
        n_log(LOG_NOTICE, "generated CA: cert %zu bytes, key %zu bytes", (size_t)ca_cert->written, (size_t)ca_key->written);
        for (i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
            N_STR* leaf_cert = NULL;
            N_STR* leaf_key = NULL;
            if (n_x509_mint_host_cert(hosts[i], ca_cert, ca_key, 825, &leaf_cert, &leaf_key) != 0 || !leaf_cert || !leaf_key) {
                n_log(LOG_ERR, "minting leaf for %s failed", hosts[i]);
                failures++;
            } else if (!verify_chain(ca_cert, leaf_cert)) {
                n_log(LOG_ERR, "leaf for %s does not verify against the CA", hosts[i]);
                failures++;
            } else {
                n_log(LOG_NOTICE, "minted and verified leaf for %s (%zu bytes)", hosts[i], (size_t)leaf_cert->written);
            }
            free_nstr(&leaf_cert);
            free_nstr(&leaf_key);
        }
    }

    if (n_x509_keypair_pem(2048, &key) != 0 || !key) {
        n_log(LOG_ERR, "standalone keypair generation failed");
        failures++;
    } else {
        n_log(LOG_NOTICE, "generated standalone key: %zu bytes", (size_t)key->written);
    }

    free_nstr(&key);
    free_nstr(&ca_cert);
    free_nstr(&ca_key);

    if (failures) {
        n_log(LOG_ERR, "ex_x509: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_x509: all checks passed");
    return 0;
}
