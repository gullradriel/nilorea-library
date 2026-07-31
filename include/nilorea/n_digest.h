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
 *@file n_digest.h
 *@brief Message digests, HMAC, PBKDF2, and AES-256-GCM AEAD over the OpenSSL EVP API
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_DIGEST_HEADER
#define __NILOREA_DIGEST_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup N_DIGEST DIGEST: message digests and HMAC
  @addtogroup N_DIGEST
  @{
  */

#ifdef HAVE_OPENSSL

#include "nilorea/n_str.h"

#include <stddef.h>

/*! output size in bytes of an MD5 digest */
#define N_DIGEST_MD5_LEN 16
/*! output size in bytes of a SHA-1 digest */
#define N_DIGEST_SHA1_LEN 20
/*! output size in bytes of a SHA-256 digest */
#define N_DIGEST_SHA256_LEN 32
/*! output size in bytes of a SHA-384 digest */
#define N_DIGEST_SHA384_LEN 48
/*! output size in bytes of a SHA-512 digest */
#define N_DIGEST_SHA512_LEN 64

/*! @brief compute the MD5 digest of len bytes of data into out (N_DIGEST_MD5_LEN bytes). Returns 0 on success, -1 on error. */
int n_digest_md5(const unsigned char* data, size_t len, unsigned char* out);
/*! @brief compute the SHA-1 digest of len bytes of data into out (N_DIGEST_SHA1_LEN bytes). Returns 0 on success, -1 on error. */
int n_digest_sha1(const unsigned char* data, size_t len, unsigned char* out);
/*! @brief compute the SHA-256 digest of len bytes of data into out (N_DIGEST_SHA256_LEN bytes). Returns 0 on success, -1 on error. */
int n_digest_sha256(const unsigned char* data, size_t len, unsigned char* out);
/*! @brief compute the SHA-384 digest of len bytes of data into out (N_DIGEST_SHA384_LEN bytes). Returns 0 on success, -1 on error. */
int n_digest_sha384(const unsigned char* data, size_t len, unsigned char* out);
/*! @brief compute the SHA-512 digest of len bytes of data into out (N_DIGEST_SHA512_LEN bytes). Returns 0 on success, -1 on error. */
int n_digest_sha512(const unsigned char* data, size_t len, unsigned char* out);

/*! @brief compute the SHA-256 digest of data and return it as a lowercase hex N_STR; free with free_nstr. Returns NULL on error. */
N_STR* n_digest_sha256_hex(const unsigned char* data, size_t len);

/*! @brief compute HMAC-SHA1 of data under key into out (N_DIGEST_SHA1_LEN bytes). A NULL key or data is treated as empty when its length is 0. Returns 0 on success, -1 on error. */
int n_hmac_sha1(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out);
/*! @brief compute HMAC-SHA256 of data under key into out (N_DIGEST_SHA256_LEN bytes). A NULL key or data is treated as empty when its length is 0. Returns 0 on success, -1 on error. */
int n_hmac_sha256(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out);
/*! @brief compute HMAC-SHA384 of data under key into out (N_DIGEST_SHA384_LEN bytes). A NULL key or data is treated as empty when its length is 0. Returns 0 on success, -1 on error. */
int n_hmac_sha384(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out);
/*! @brief compute HMAC-SHA512 of data under key into out (N_DIGEST_SHA512_LEN bytes). A NULL key or data is treated as empty when its length is 0. Returns 0 on success, -1 on error. */
int n_hmac_sha512(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char* out);

/*! @brief constant-time comparison of two equal-length byte buffers (for verifying MACs/signatures without a timing side channel). Returns 1 when equal, 0 otherwise. */
int n_digest_consttime_equal(const unsigned char* a, const unsigned char* b, size_t len);

/*! @brief derive out_len key bytes from pass and salt with PBKDF2-HMAC-SHA1 over iterations rounds (RFC 2898). A NULL pass or salt is treated as empty when its length is 0. Returns 0 on success, -1 on error. */
int n_pbkdf2_hmac_sha1(const unsigned char* pass, size_t pass_len, const unsigned char* salt, size_t salt_len, unsigned int iterations, unsigned char* out, size_t out_len);
/*! @brief derive out_len key bytes from pass and salt with PBKDF2-HMAC-SHA256 over iterations rounds (RFC 2898). A NULL pass or salt is treated as empty when its length is 0. Returns 0 on success, -1 on error. */
int n_pbkdf2_hmac_sha256(const unsigned char* pass, size_t pass_len, const unsigned char* salt, size_t salt_len, unsigned int iterations, unsigned char* out, size_t out_len);

/*! key size in bytes for AES-256-GCM */
#define N_AEAD_AES256GCM_KEY_LEN 32
/*! authentication tag size in bytes for AES-256-GCM */
#define N_AEAD_AES256GCM_TAG_LEN 16

/*! @brief AES-256-GCM authenticated encryption. key is N_AEAD_AES256GCM_KEY_LEN bytes; iv is iv_len bytes (12 recommended); aad may be NULL when aad_len is 0. Writes pt_len ciphertext bytes to out_ct and N_AEAD_AES256GCM_TAG_LEN tag bytes to out_tag. Returns 0 on success, -1 on error. */
int n_aead_aes256gcm_encrypt(const unsigned char* key, const unsigned char* iv, size_t iv_len, const unsigned char* aad, size_t aad_len, const unsigned char* pt, size_t pt_len, unsigned char* out_ct, unsigned char* out_tag);
/*! @brief AES-256-GCM authenticated decryption. Arguments mirror n_aead_aes256gcm_encrypt; tag is the N_AEAD_AES256GCM_TAG_LEN expected tag. Writes ct_len plaintext bytes to out_pt only when the tag verifies. Returns 0 on success (authentic), -1 on error or a tag mismatch. */
int n_aead_aes256gcm_decrypt(const unsigned char* key, const unsigned char* iv, size_t iv_len, const unsigned char* aad, size_t aad_len, const unsigned char* ct, size_t ct_len, const unsigned char* tag, unsigned char* out_pt);

#endif /* HAVE_OPENSSL */

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_DIGEST_HEADER */
