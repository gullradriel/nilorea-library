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
 *@file n_x509.h
 *@brief X.509 helpers: self-signed CA generation and per-host leaf minting
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_X509_HEADER
#define __NILOREA_X509_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup N_X509 X509: certificate authority and per-host leaf minting
  @addtogroup N_X509
  @{
  */

#ifdef HAVE_OPENSSL

#include "nilorea/n_str.h"

/*! @brief Generate an RSA private key and return it as a PEM string.
 *  @param bits RSA modulus size in bits (for example 2048).
 *  @param key_pem out: newly allocated PEM private key, owned by the caller.
 *  @return 0 on success, -1 on error. */
int n_x509_keypair_pem(int bits, N_STR** key_pem);

/*! @brief Generate a self-signed certificate authority (CA) keypair and cert.
 *  The CA is meant to be installed by the user in their browser/OS trust store
 *  so a MITM proxy can present host certificates it signs. The certificate has
 *  basicConstraints CA:TRUE and keyUsage keyCertSign,cRLSign and is signed with
 *  SHA-256.
 *  @param cn common name for the CA subject/issuer (for example "Fissure CA").
 *  @param days validity in days from now.
 *  @param ca_cert_pem out: newly allocated CA certificate PEM, owned by the caller.
 *  @param ca_key_pem out: newly allocated CA private key PEM, owned by the caller.
 *  @return 0 on success, -1 on error. */
int n_x509_generate_ca(const char* cn, int days, N_STR** ca_cert_pem, N_STR** ca_key_pem);

/*! @brief Mint a per-host leaf certificate signed by the given CA.
 *  The leaf has basicConstraints CA:FALSE, extendedKeyUsage serverAuth, a
 *  subjectAltName of DNS:host (or IP:host when host is a literal IP address),
 *  a fresh random serial, and is signed with the CA key using SHA-256. Cache
 *  the result per host to avoid re-minting on every connection.
 *  @param host the requested host name (or IP) to certify.
 *  @param ca_cert_pem the CA certificate PEM (issuer).
 *  @param ca_key_pem the CA private key PEM (signer).
 *  @param days validity in days from now.
 *  @param leaf_cert_pem out: newly allocated leaf certificate PEM, owned by the caller.
 *  @param leaf_key_pem out: newly allocated leaf private key PEM, owned by the caller.
 *  @return 0 on success, -1 on error. */
int n_x509_mint_host_cert(const char* host, const N_STR* ca_cert_pem, const N_STR* ca_key_pem, int days, N_STR** leaf_cert_pem, N_STR** leaf_key_pem);

#endif /* HAVE_OPENSSL */

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_X509_HEADER */
