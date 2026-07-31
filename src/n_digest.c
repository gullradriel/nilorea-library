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
 *@file n_digest.c
 *@brief Message digests, HMAC, PBKDF2, and AES-256-GCM AEAD over the OpenSSL EVP API
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_digest.h"

#ifdef HAVE_OPENSSL

#include "nilorea/n_common.h"
#include "nilorea/n_hex.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <limits.h>
#include <string.h>

/* One-shot EVP digest shared by the public wrappers. A NULL data pointer is
 * accepted only with len 0 (the digest of the empty input). */
static int n_digest_oneshot(const EVP_MD* md, const unsigned char* data, size_t len, unsigned char* out, const char* name) {
    if (!out || (!data && len > 0)) {
        n_log(LOG_ERR, "n_digest_%s: invalid arguments", name);
        return -1;
    }
    const unsigned char* in = data ? data : (const unsigned char*)"";
    if (EVP_Digest(in, len, out, NULL, md, NULL) != 1) {
        n_log(LOG_ERR, "n_digest_%s: EVP_Digest failed", name);
        return -1;
    }
    return 0;
}

int n_digest_md5(const unsigned char* data, size_t len, unsigned char* out) {
    return n_digest_oneshot(EVP_md5(), data, len, out, "md5");
}

int n_digest_sha1(const unsigned char* data, size_t len, unsigned char* out) {
    return n_digest_oneshot(EVP_sha1(), data, len, out, "sha1");
}

int n_digest_sha256(const unsigned char* data, size_t len, unsigned char* out) {
    return n_digest_oneshot(EVP_sha256(), data, len, out, "sha256");
}

int n_digest_sha384(const unsigned char* data, size_t len, unsigned char* out) {
    return n_digest_oneshot(EVP_sha384(), data, len, out, "sha384");
}

int n_digest_sha512(const unsigned char* data, size_t len, unsigned char* out) {
    return n_digest_oneshot(EVP_sha512(), data, len, out, "sha512");
}

N_STR* n_digest_sha256_hex(const unsigned char* data, size_t len) {
    unsigned char md[N_DIGEST_SHA256_LEN];
    if (n_digest_sha256(data, len, md) != 0) {
        return NULL;
    }
    return n_hex_encode(md, sizeof(md));
}

/* Shared HMAC one-shot: compute HMAC(md) of data under key into out, validating
   the produced length against the digest size. Returns 0 on success, -1 on error. */
static int n_hmac_oneshot(const EVP_MD* md, unsigned int expect_len, const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out, const char* name) {
    if (!out || (!key && key_len > 0) || (!data && data_len > 0)) {
        n_log(LOG_ERR, "n_hmac_%s: invalid arguments", name);
        return -1;
    }
    if (key_len > INT_MAX) {
        n_log(LOG_ERR, "n_hmac_%s: key length %zu too large", name, key_len);
        return -1;
    }
    const unsigned char* k = key ? key : (const unsigned char*)"";
    const unsigned char* in = data ? data : (const unsigned char*)"";
    unsigned int out_len = 0;
    const unsigned char* r = HMAC(md, k, (int)key_len, in, data_len, out, &out_len);
    if (!r || out_len != expect_len) {
        n_log(LOG_ERR, "n_hmac_%s: HMAC failed (out_len %u)", name, out_len);
        return -1;
    }
    return 0;
}

int n_hmac_sha1(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out) {
    return n_hmac_oneshot(EVP_sha1(), N_DIGEST_SHA1_LEN, key, key_len, data, data_len, out, "sha1");
}

int n_hmac_sha256(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out) {
    return n_hmac_oneshot(EVP_sha256(), N_DIGEST_SHA256_LEN, key, key_len, data, data_len, out, "sha256");
}

int n_hmac_sha384(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out) {
    return n_hmac_oneshot(EVP_sha384(), N_DIGEST_SHA384_LEN, key, key_len, data, data_len, out, "sha384");
}

int n_hmac_sha512(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out) {
    return n_hmac_oneshot(EVP_sha512(), N_DIGEST_SHA512_LEN, key, key_len, data, data_len, out, "sha512");
}

int n_digest_consttime_equal(const unsigned char* a, const unsigned char* b, size_t len) {
    if (!a || !b)
        return 0;
    return CRYPTO_memcmp(a, b, len) == 0 ? 1 : 0;
}

/* Shared PBKDF2 one-shot over the OpenSSL PKCS5 API, validating argument ranges. */
static int n_pbkdf2_oneshot(const EVP_MD* md, const unsigned char* pass, size_t pass_len, const unsigned char* salt, size_t salt_len, unsigned int iterations, unsigned char* out, size_t out_len, const char* name) {
    if (!out || out_len == 0 || iterations == 0 || (!pass && pass_len > 0) || (!salt && salt_len > 0)) {
        n_log(LOG_ERR, "n_pbkdf2_%s: invalid arguments", name);
        return -1;
    }
    if (pass_len > INT_MAX || salt_len > INT_MAX || out_len > INT_MAX || iterations > (unsigned int)INT_MAX) {
        n_log(LOG_ERR, "n_pbkdf2_%s: argument too large", name);
        return -1;
    }
    const char* p = pass ? (const char*)pass : "";
    const unsigned char* s = salt ? salt : (const unsigned char*)"";
    if (PKCS5_PBKDF2_HMAC(p, (int)pass_len, s, (int)salt_len, (int)iterations, md, (int)out_len, out) != 1) {
        n_log(LOG_ERR, "n_pbkdf2_%s: PKCS5_PBKDF2_HMAC failed", name);
        return -1;
    }
    return 0;
}

int n_pbkdf2_hmac_sha1(const unsigned char* pass, size_t pass_len, const unsigned char* salt, size_t salt_len, unsigned int iterations, unsigned char* out, size_t out_len) {
    return n_pbkdf2_oneshot(EVP_sha1(), pass, pass_len, salt, salt_len, iterations, out, out_len, "sha1");
}

int n_pbkdf2_hmac_sha256(const unsigned char* pass, size_t pass_len, const unsigned char* salt, size_t salt_len, unsigned int iterations, unsigned char* out, size_t out_len) {
    return n_pbkdf2_oneshot(EVP_sha256(), pass, pass_len, salt, salt_len, iterations, out, out_len, "sha256");
}

int n_aead_aes256gcm_encrypt(const unsigned char* key, const unsigned char* iv, size_t iv_len, const unsigned char* aad, size_t aad_len, const unsigned char* pt, size_t pt_len, unsigned char* out_ct, unsigned char* out_tag) {
    EVP_CIPHER_CTX* ctx;
    int outl = 0;
    int rc = -1;
    if (!key || !iv || iv_len == 0 || !out_ct || !out_tag || (!pt && pt_len > 0) || (!aad && aad_len > 0) || iv_len > INT_MAX || pt_len > INT_MAX || aad_len > INT_MAX) {
        n_log(LOG_ERR, "n_aead_aes256gcm_encrypt: invalid arguments");
        return -1;
    }
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        n_log(LOG_ERR, "n_aead_aes256gcm_encrypt: EVP_CIPHER_CTX_new failed");
        return -1;
    }
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv_len, NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_encrypt: init failed");
        goto end;
    }
    if (aad_len > 0 && EVP_EncryptUpdate(ctx, NULL, &outl, aad, (int)aad_len) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_encrypt: AAD failed");
        goto end;
    }
    if (pt_len > 0 && EVP_EncryptUpdate(ctx, out_ct, &outl, pt, (int)pt_len) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_encrypt: encrypt failed");
        goto end;
    }
    if (EVP_EncryptFinal_ex(ctx, out_ct + outl, &outl) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, N_AEAD_AES256GCM_TAG_LEN, out_tag) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_encrypt: finalize failed");
        goto end;
    }
    rc = 0;
end:
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

int n_aead_aes256gcm_decrypt(const unsigned char* key, const unsigned char* iv, size_t iv_len, const unsigned char* aad, size_t aad_len, const unsigned char* ct, size_t ct_len, const unsigned char* tag, unsigned char* out_pt) {
    EVP_CIPHER_CTX* ctx;
    unsigned char tagbuf[N_AEAD_AES256GCM_TAG_LEN];
    int outl = 0;
    int rc = -1;
    if (!key || !iv || iv_len == 0 || !tag || !out_pt || (!ct && ct_len > 0) || (!aad && aad_len > 0) || iv_len > INT_MAX || ct_len > INT_MAX || aad_len > INT_MAX) {
        n_log(LOG_ERR, "n_aead_aes256gcm_decrypt: invalid arguments");
        return -1;
    }
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        n_log(LOG_ERR, "n_aead_aes256gcm_decrypt: EVP_CIPHER_CTX_new failed");
        return -1;
    }
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv_len, NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_decrypt: init failed");
        goto end;
    }
    if (aad_len > 0 && EVP_DecryptUpdate(ctx, NULL, &outl, aad, (int)aad_len) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_decrypt: AAD failed");
        goto end;
    }
    if (ct_len > 0 && EVP_DecryptUpdate(ctx, out_pt, &outl, ct, (int)ct_len) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_decrypt: decrypt failed");
        goto end;
    }
    memcpy(tagbuf, tag, N_AEAD_AES256GCM_TAG_LEN); /* SET_TAG wants a non-const buffer */
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, N_AEAD_AES256GCM_TAG_LEN, tagbuf) != 1) {
        n_log(LOG_ERR, "n_aead_aes256gcm_decrypt: set tag failed");
        goto end;
    }
    /* EVP_DecryptFinal_ex returns >0 only when the authentication tag verifies */
    if (EVP_DecryptFinal_ex(ctx, out_pt + outl, &outl) > 0)
        rc = 0;
end:
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

#endif /* HAVE_OPENSSL */
