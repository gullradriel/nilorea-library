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
 *@file n_x509.c
 *@brief X.509 helpers implementation: CA generation and per-host leaf minting
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_x509.h"

#ifdef HAVE_OPENSSL

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"

/* inet_pton()/AF_INET/AF_INET6 live in <arpa/inet.h> on POSIX and in the
 * Winsock headers (<ws2tcpip.h>, pulled in via n_windows.h) on Windows. */
#ifdef __windows__
#include "nilorea/n_windows.h"
#else
#include <arpa/inet.h>
#endif
#include <string.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

/* Compatibility shims for OpenSSL 1.0.2 (RHEL 7 and friends). Everything below
 * was introduced in 1.1.0: the BN_rand top/bottom named constants, the
 * X509_getm_notBefore()/X509_getm_notAfter() mutable accessors, and the const
 * qualifier on the value argument of X509V3_EXT_conf_nid(). LibreSSL reports
 * OPENSSL_VERSION_NUMBER as 0x20000000L and already provides all of them, so it
 * takes the modern path. */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#ifndef BN_RAND_TOP_ANY
/*! BN_rand(): leave the most significant bit unconstrained */
#define BN_RAND_TOP_ANY (-1)
#endif
#ifndef BN_RAND_BOTTOM_ANY
/*! BN_rand(): leave the least significant bit unconstrained */
#define BN_RAND_BOTTOM_ANY 0
#endif
#ifndef X509_getm_notBefore
/*! pre-1.1.0 spelling of the mutable notBefore accessor */
#define X509_getm_notBefore(x) X509_get_notBefore(x)
#endif
#ifndef X509_getm_notAfter
/*! pre-1.1.0 spelling of the mutable notAfter accessor */
#define X509_getm_notAfter(x) X509_get_notAfter(x)
#endif
/*! pre-1.1.0 X509V3_EXT_conf_nid() declares value as char*, it does not write to it */
#define N_X509_EXT_VALUE(v) ((char*)(uintptr_t)(v))
#else
/*! 1.1.0 and later accept a const value directly */
#define N_X509_EXT_VALUE(v) (v)
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
/* OpenSSL 1.1.0 initialises itself on first use. Before that the EVP algorithm
 * table starts out empty, and while signing still works (X509_sign() is handed
 * the EVP_MD directly) anything that later VERIFIES the certificate has to look
 * the signature algorithm up by OID, which fails: X509_verify_cert() reports
 * "certificate signature failure" and ASN1_item_verify() sets "unknown message
 * digest algorithm". Populate the table once so a caller that uses n_x509 on
 * its own, without n_network and therefore without netw_init_openssl(), still
 * gets certificates that verify. */
static pthread_once_t n_x509_once = PTHREAD_ONCE_INIT;

static void n_x509_init_openssl(void) {
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
}

/*! populate the EVP algorithm table once, no-op from 1.1.0 on */
#define N_X509_INIT() ((void)pthread_once(&n_x509_once, n_x509_init_openssl))
#else
/*! 1.1.0 and later initialise themselves on first use */
#define N_X509_INIT() ((void)0)
#endif

/*! RSA modulus size in bits used for the CA and leaf keys */
#define N_X509_KEY_BITS 2048
/*! random serial number size in bits */
#define N_X509_SERIAL_BITS 64
/*! buffer size for a subjectAltName value string */
#define N_X509_SAN_BUF 300

/* Copy the contents of a memory BIO into a newly allocated N_STR. Returns 0 on
 * success, -1 on error. */
static int bio_to_nstr(BIO* bio, N_STR** out) {
    BUF_MEM* mem = NULL;
    BIO_get_mem_ptr(bio, &mem);
    if (!mem || !mem->data || mem->length == 0)
        return -1;
    *out = NULL;
    if (char_to_nstr_ex(mem->data, mem->length, out) != TRUE || !*out)
        return -1;
    return 0;
}

/* Serialize a private key to a PEM N_STR. Returns 0 on success, -1 on error. */
static int pkey_to_pem(EVP_PKEY* pkey, N_STR** out) {
    BIO* bio = BIO_new(BIO_s_mem());
    int rc = -1;
    if (!bio)
        return -1;
    if (PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL) == 1)
        rc = bio_to_nstr(bio, out);
    BIO_free(bio);
    return rc;
}

/* Serialize a certificate to a PEM N_STR. Returns 0 on success, -1 on error. */
static int x509_to_pem(X509* x, N_STR** out) {
    BIO* bio = BIO_new(BIO_s_mem());
    int rc = -1;
    if (!bio)
        return -1;
    if (PEM_write_bio_X509(bio, x) == 1)
        rc = bio_to_nstr(bio, out);
    BIO_free(bio);
    return rc;
}

/* Assign a fresh random serial number to a certificate. 0 on success, -1 on error. */
static int set_random_serial(X509* x) {
    BIGNUM* bn = BN_new();
    int rc = -1;
    if (!bn)
        return -1;
    if (BN_rand(bn, N_X509_SERIAL_BITS, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY) == 1) {
        ASN1_INTEGER* serial = X509_get_serialNumber(x);
        if (serial && BN_to_ASN1_INTEGER(bn, serial) != NULL)
            rc = 0;
    }
    BN_free(bn);
    return rc;
}

/* Add a v3 extension to subject, using issuer for the extension context. */
static int add_ext(X509* issuer, X509* subject, int nid, const char* value) {
    X509V3_CTX ctx;
    X509_EXTENSION* ext;
    int rc = -1;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer, subject, NULL, NULL, 0);
    ext = X509V3_EXT_conf_nid(NULL, &ctx, nid, N_X509_EXT_VALUE(value));
    if (ext) {
        if (X509_add_ext(subject, ext, -1) == 1)
            rc = 0;
        X509_EXTENSION_free(ext);
    }
    return rc;
}

/* Set the common name (CN) entry on an X509 name. 0 on success, -1 on error. */
static int set_cn(X509_NAME* name, const char* cn) {
    if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)cn, -1, -1, 0) == 1)
        return 0;
    return -1;
}

/* Return 1 if host is a literal IPv4 or IPv6 address, 0 otherwise. */
static int host_is_ip(const char* host) {
    unsigned char buf[16];
    if (inet_pton(AF_INET, host, buf) == 1)
        return 1;
    if (inet_pton(AF_INET6, host, buf) == 1)
        return 1;
    return 0;
}

/* Generate an RSA keypair of the given bit size using the portable EVP keygen
 * API (works across OpenSSL 1.1.1 and 3.x). Returns a new EVP_PKEY or NULL. */
static EVP_PKEY* gen_rsa(int bits) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY* pkey = NULL;
    if (!ctx)
        return NULL;
    if (EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0 || EVP_PKEY_keygen(ctx, &pkey) <= 0)
        pkey = NULL;
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

int n_x509_keypair_pem(int bits, N_STR** key_pem) {
    EVP_PKEY* pkey;
    int rc;
    __n_assert(key_pem, return -1);
    N_X509_INIT();
    if (bits <= 0)
        bits = N_X509_KEY_BITS;
    pkey = gen_rsa(bits);
    if (!pkey) {
        n_log(LOG_ERR, "n_x509: RSA key generation failed (%d bits)", bits);
        return -1;
    }
    rc = pkey_to_pem(pkey, key_pem);
    EVP_PKEY_free(pkey);
    return rc;
}

int n_x509_generate_ca(const char* cn, int days, N_STR** ca_cert_pem, N_STR** ca_key_pem) {
    EVP_PKEY* pkey = NULL;
    X509* x = NULL;
    X509_NAME* name;
    int rc = -1;
    __n_assert(cn, return -1);
    __n_assert(ca_cert_pem, return -1);
    __n_assert(ca_key_pem, return -1);
    N_X509_INIT();
    if (days <= 0)
        days = 3650;

    pkey = gen_rsa(N_X509_KEY_BITS);
    if (!pkey) {
        n_log(LOG_ERR, "n_x509: CA key generation failed");
        goto cleanup;
    }
    x = X509_new();
    if (!x)
        goto cleanup;
    if (X509_set_version(x, 2) != 1)
        goto cleanup;
    if (set_random_serial(x) != 0)
        goto cleanup;
    X509_gmtime_adj(X509_getm_notBefore(x), 0);
    X509_gmtime_adj(X509_getm_notAfter(x), (long)days * 86400L);
    if (X509_set_pubkey(x, pkey) != 1)
        goto cleanup;
    name = X509_get_subject_name(x);
    if (set_cn(name, cn) != 0)
        goto cleanup;
    if (X509_set_issuer_name(x, name) != 1)
        goto cleanup;
    if (add_ext(x, x, NID_basic_constraints, "critical,CA:TRUE") != 0)
        goto cleanup;
    if (add_ext(x, x, NID_key_usage, "critical,keyCertSign,cRLSign") != 0)
        goto cleanup;
    if (add_ext(x, x, NID_subject_key_identifier, "hash") != 0)
        goto cleanup;
    if (X509_sign(x, pkey, EVP_sha256()) == 0) {
        n_log(LOG_ERR, "n_x509: CA self-sign failed");
        goto cleanup;
    }
    if (x509_to_pem(x, ca_cert_pem) != 0)
        goto cleanup;
    if (pkey_to_pem(pkey, ca_key_pem) != 0) {
        free_nstr(ca_cert_pem);
        goto cleanup;
    }
    rc = 0;

cleanup:
    if (x)
        X509_free(x);
    if (pkey)
        EVP_PKEY_free(pkey);
    return rc;
}

int n_x509_mint_host_cert(const char* host, const N_STR* ca_cert_pem, const N_STR* ca_key_pem, int days, N_STR** leaf_cert_pem, N_STR** leaf_key_pem) {
    EVP_PKEY* leaf_key = NULL;
    EVP_PKEY* ca_key = NULL;
    X509* ca = NULL;
    X509* x = NULL;
    BIO* bio_cert = NULL;
    BIO* bio_key = NULL;
    X509_NAME* name;
    char san[N_X509_SAN_BUF];
    int rc = -1;
    __n_assert(host, return -1);
    __n_assert(ca_cert_pem && ca_cert_pem->data, return -1);
    __n_assert(ca_key_pem && ca_key_pem->data, return -1);
    __n_assert(leaf_cert_pem, return -1);
    __n_assert(leaf_key_pem, return -1);
    N_X509_INIT();
    if (days <= 0)
        days = 825;

    bio_cert = BIO_new_mem_buf(ca_cert_pem->data, (int)ca_cert_pem->written);
    bio_key = BIO_new_mem_buf(ca_key_pem->data, (int)ca_key_pem->written);
    if (!bio_cert || !bio_key)
        goto cleanup;
    ca = PEM_read_bio_X509(bio_cert, NULL, NULL, NULL);
    ca_key = PEM_read_bio_PrivateKey(bio_key, NULL, NULL, NULL);
    if (!ca || !ca_key) {
        n_log(LOG_ERR, "n_x509: could not parse CA certificate/key");
        goto cleanup;
    }

    leaf_key = gen_rsa(N_X509_KEY_BITS);
    if (!leaf_key)
        goto cleanup;
    x = X509_new();
    if (!x)
        goto cleanup;
    if (X509_set_version(x, 2) != 1)
        goto cleanup;
    if (set_random_serial(x) != 0)
        goto cleanup;
    X509_gmtime_adj(X509_getm_notBefore(x), 0);
    X509_gmtime_adj(X509_getm_notAfter(x), (long)days * 86400L);
    if (X509_set_pubkey(x, leaf_key) != 1)
        goto cleanup;
    name = X509_get_subject_name(x);
    if (set_cn(name, host) != 0)
        goto cleanup;
    if (X509_set_issuer_name(x, X509_get_subject_name(ca)) != 1)
        goto cleanup;
    if (add_ext(ca, x, NID_basic_constraints, "critical,CA:FALSE") != 0)
        goto cleanup;
    if (add_ext(ca, x, NID_key_usage, "critical,digitalSignature,keyEncipherment") != 0)
        goto cleanup;
    if (add_ext(ca, x, NID_ext_key_usage, "serverAuth") != 0)
        goto cleanup;
    snprintf(san, sizeof(san), "%s:%s", host_is_ip(host) ? "IP" : "DNS", host);
    if (add_ext(ca, x, NID_subject_alt_name, san) != 0)
        goto cleanup;
    if (X509_sign(x, ca_key, EVP_sha256()) == 0) {
        n_log(LOG_ERR, "n_x509: leaf signing failed");
        goto cleanup;
    }
    if (x509_to_pem(x, leaf_cert_pem) != 0)
        goto cleanup;
    if (pkey_to_pem(leaf_key, leaf_key_pem) != 0) {
        free_nstr(leaf_cert_pem);
        goto cleanup;
    }
    rc = 0;

cleanup:
    if (x)
        X509_free(x);
    if (ca)
        X509_free(ca);
    if (leaf_key)
        EVP_PKEY_free(leaf_key);
    if (ca_key)
        EVP_PKEY_free(ca_key);
    if (bio_cert)
        BIO_free(bio_cert);
    if (bio_key)
        BIO_free(bio_key);
    return rc;
}

#endif /* HAVE_OPENSSL */
