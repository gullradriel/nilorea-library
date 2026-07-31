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
 *@example ex_digest.c
 *@brief n_digest regression: MD5/SHA/HMAC/PBKDF2 and constant-time-compare vectors.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_digest.h"
#include "nilorea/n_hex.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

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
                fprintf(stderr, "ex_digest\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

/* Hex-encode a raw digest and compare it to the expected lowercase hex string. */
static void expect_hex(const unsigned char* digest, size_t len, const char* want, const char* label) {
    N_STR* hex = n_hex_encode(digest, len);
    if (!hex) {
        n_log(LOG_ERR, "%s: hex encode failed", label);
        failures++;
        return;
    }
    if (strcmp(hex->data, want) != 0) {
        n_log(LOG_ERR, "%s: got '%s' expected '%s'", label, hex->data, want);
        failures++;
    }
    free_nstr(&hex);
}

static void test_digests(void) {
    const unsigned char* abc = (const unsigned char*)"abc";
    size_t n = 3;

    unsigned char md5[N_DIGEST_MD5_LEN];
    if (n_digest_md5(abc, n, md5) != 0)
        failures++;
    else
        expect_hex(md5, sizeof(md5), "900150983cd24fb0d6963f7d28e17f72", "md5(abc)");

    unsigned char sha1[N_DIGEST_SHA1_LEN];
    if (n_digest_sha1(abc, n, sha1) != 0)
        failures++;
    else
        expect_hex(sha1, sizeof(sha1), "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1(abc)");

    unsigned char sha256[N_DIGEST_SHA256_LEN];
    if (n_digest_sha256(abc, n, sha256) != 0)
        failures++;
    else
        expect_hex(sha256, sizeof(sha256), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(abc)");

    unsigned char sha384[N_DIGEST_SHA384_LEN];
    if (n_digest_sha384(abc, n, sha384) != 0)
        failures++;
    else
        expect_hex(sha384, sizeof(sha384),
                   "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
                   "sha384(abc)");

    unsigned char sha512[N_DIGEST_SHA512_LEN];
    if (n_digest_sha512(abc, n, sha512) != 0)
        failures++;
    else
        expect_hex(sha512, sizeof(sha512),
                   "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
                   "sha512(abc)");
}

static void test_sha256_hex(void) {
    N_STR* h = n_digest_sha256_hex((const unsigned char*)"abc", 3);
    if (!h || strcmp(h->data, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0) {
        n_log(LOG_ERR, "sha256_hex(abc): got '%s'", h ? h->data : "(null)");
        failures++;
    }
    free_nstr(&h);

    /* the empty input has a well-known SHA-256 digest */
    h = n_digest_sha256_hex((const unsigned char*)"", 0);
    if (!h || strcmp(h->data, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") != 0) {
        n_log(LOG_ERR, "sha256_hex(empty): got '%s'", h ? h->data : "(null)");
        failures++;
    }
    free_nstr(&h);
}

static void test_hmac(void) {
    const unsigned char* key = (const unsigned char*)"key";
    const unsigned char* msg = (const unsigned char*)"The quick brown fox jumps over the lazy dog";
    unsigned char out[N_DIGEST_SHA256_LEN];
    if (n_hmac_sha256(key, 3, msg, strlen((const char*)msg), out) != 0) {
        n_log(LOG_ERR, "hmac_sha256 failed");
        failures++;
        return;
    }
    expect_hex(out, sizeof(out), "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8", "hmac-sha256");
}

/* HMAC-SHA1/384/512 known-answer vectors (key="key", the pangram message). */
static void test_hmac_variants(void) {
    const unsigned char* key = (const unsigned char*)"key";
    const unsigned char* msg = (const unsigned char*)"The quick brown fox jumps over the lazy dog";
    size_t klen = 3, mlen = strlen((const char*)msg);

    unsigned char o1[N_DIGEST_SHA1_LEN];
    if (n_hmac_sha1(key, klen, msg, mlen, o1) != 0)
        failures++;
    else
        expect_hex(o1, sizeof(o1), "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9", "hmac-sha1");

    unsigned char o384[N_DIGEST_SHA384_LEN];
    if (n_hmac_sha384(key, klen, msg, mlen, o384) != 0)
        failures++;
    else
        expect_hex(o384, sizeof(o384),
                   "d7f4727e2c0b39ae0f1e40cc96f60242d5b7801841cea6fc592c5d3e1ae50700582a96cf35e1e554995fe4e03381c237",
                   "hmac-sha384");

    unsigned char o512[N_DIGEST_SHA512_LEN];
    if (n_hmac_sha512(key, klen, msg, mlen, o512) != 0)
        failures++;
    else
        expect_hex(o512, sizeof(o512),
                   "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb82f948a549f7b791a5b41915ee4d1ec3935357e4e2317250d0372afa2ebeeb3a",
                   "hmac-sha512");
}

/* Constant-time comparison: equal buffers match, a one-byte diff does not. */
static void test_consttime(void) {
    const unsigned char a[] = {1, 2, 3, 4, 5};
    const unsigned char b[] = {1, 2, 3, 4, 5};
    const unsigned char c[] = {1, 2, 3, 4, 6};
    if (n_digest_consttime_equal(a, b, sizeof(a)) != 1)
        failures++;
    if (n_digest_consttime_equal(a, c, sizeof(a)) != 0)
        failures++;
    if (n_digest_consttime_equal(NULL, b, sizeof(a)) != 0) /* NULL never matches */
        failures++;
}

/* PBKDF2-HMAC-SHA1 vectors from RFC 6070 and a SHA256 vector (RFC 7914 salt). */
static void test_pbkdf2(void) {
    unsigned char out[40];

    /* RFC 6070: P="password" S="salt" c=1 dkLen=20 */
    if (n_pbkdf2_hmac_sha1((const unsigned char*)"password", 8, (const unsigned char*)"salt", 4, 1, out, 20) != 0)
        failures++;
    else
        expect_hex(out, 20, "0c60c80f961f0e71f3a9b524af6012062fe037a6", "pbkdf2-sha1 c=1");

    /* RFC 6070: same but c=4096 */
    if (n_pbkdf2_hmac_sha1((const unsigned char*)"password", 8, (const unsigned char*)"salt", 4, 4096, out, 20) != 0)
        failures++;
    else
        expect_hex(out, 20, "4b007901b765489abead49d926f721d065a429c1", "pbkdf2-sha1 c=4096");

    /* PBKDF2-HMAC-SHA256: P="password" S="salt" c=1 dkLen=32 */
    if (n_pbkdf2_hmac_sha256((const unsigned char*)"password", 8, (const unsigned char*)"salt", 4, 1, out, 32) != 0)
        failures++;
    else
        expect_hex(out, 32, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b", "pbkdf2-sha256 c=1");

    /* invalid arguments must fail */
    if (n_pbkdf2_hmac_sha1((const unsigned char*)"p", 1, (const unsigned char*)"s", 1, 0, out, 20) == 0) /* zero iterations */
        failures++;
    if (n_pbkdf2_hmac_sha1((const unsigned char*)"p", 1, (const unsigned char*)"s", 1, 1, NULL, 20) == 0) /* NULL out */
        failures++;
}

/* AES-256-GCM: NIST test vector (Test Case 14), a round-trip, and tamper detection. */
static void test_aead(void) {
    unsigned char key[N_AEAD_AES256GCM_KEY_LEN] = {0};
    unsigned char iv[12] = {0};
    unsigned char pt[16] = {0};
    unsigned char ct[16];
    unsigned char tag[N_AEAD_AES256GCM_TAG_LEN];
    unsigned char back[16];

    /* Test Case 14: all-zero key/iv/plaintext */
    if (n_aead_aes256gcm_encrypt(key, iv, sizeof(iv), NULL, 0, pt, sizeof(pt), ct, tag) != 0) {
        failures++;
    } else {
        expect_hex(ct, sizeof(ct), "cea7403d4d606b6e074ec5d3baf39d18", "aes256gcm ct");
        expect_hex(tag, sizeof(tag), "d0d1c8a799996bf0265b98b5d48ab919", "aes256gcm tag");
    }

    /* round-trip with real data and AAD recovers the plaintext */
    {
        const unsigned char msg[] = "hello, world!";
        size_t mlen = sizeof(msg) - 1; /* drop the terminating NUL */
        unsigned char rk[N_AEAD_AES256GCM_KEY_LEN];
        unsigned char riv[12];
        unsigned char rct[16];
        unsigned char rtag[N_AEAD_AES256GCM_TAG_LEN];
        unsigned char rback[16];
        size_t i;
        for (i = 0; i < sizeof(rk); i++)
            rk[i] = (unsigned char)(i + 1);
        for (i = 0; i < sizeof(riv); i++)
            riv[i] = (unsigned char)(0x40 + i);
        if (n_aead_aes256gcm_encrypt(rk, riv, sizeof(riv), (const unsigned char*)"aad", 3, msg, mlen, rct, rtag) != 0)
            failures++;
        else if (n_aead_aes256gcm_decrypt(rk, riv, sizeof(riv), (const unsigned char*)"aad", 3, rct, mlen, rtag, rback) != 0)
            failures++;
        else if (memcmp(msg, rback, mlen) != 0)
            failures++;
        /* a flipped tag byte must fail authentication */
        rtag[0] ^= 0x01;
        if (n_aead_aes256gcm_decrypt(rk, riv, sizeof(riv), (const unsigned char*)"aad", 3, rct, mlen, rtag, rback) == 0)
            failures++;
    }

    /* decrypting Test Case 14 back returns the zero plaintext */
    if (n_aead_aes256gcm_decrypt(key, iv, sizeof(iv), NULL, 0, ct, sizeof(ct), tag, back) != 0)
        failures++;
    else if (memcmp(pt, back, sizeof(pt)) != 0)
        failures++;
}

static void test_errors(void) {
    unsigned char out[N_DIGEST_SHA256_LEN];
    if (n_digest_sha256(NULL, 4, out) == 0) { /* NULL data with len>0 must fail */
        n_log(LOG_ERR, "sha256(NULL,4) should fail");
        failures++;
    }
    if (n_digest_sha256((const unsigned char*)"x", 1, NULL) == 0) { /* NULL out must fail */
        n_log(LOG_ERR, "sha256 with NULL out should fail");
        failures++;
    }
    if (n_hmac_sha256((const unsigned char*)"k", 1, NULL, 4, out) == 0) {
        n_log(LOG_ERR, "hmac with NULL data,len>0 should fail");
        failures++;
    }
    /* the digest of the empty input is well defined */
    if (n_digest_sha256(NULL, 0, out) != 0) {
        n_log(LOG_ERR, "sha256(NULL,0) should succeed");
        failures++;
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    test_digests();
    test_sha256_hex();
    test_hmac();
    test_hmac_variants();
    test_consttime();
    test_pbkdf2();
    test_aead();
    test_errors();

    if (failures) {
        n_log(LOG_ERR, "ex_digest: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_digest: all checks passed");
    return 0;
}
