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
 *@file n_http3.h
 *@brief Minimal blocking HTTP/3 (over QUIC) client
 *@author Castagnier Mickael
 *@version 1.0
 *
 * A small blocking HTTP/3 client built on ngtcp2 (QUIC transport) + nghttp3
 * (HTTP/3 framing and QPACK), using the OpenSSL native QUIC TLS handshake
 * through the ngtcp2 "ossl" crypto shim. It performs a single request/response
 * exchange over a fresh QUIC connection and returns the status, response
 * headers, and body.
 *
 * The whole implementation is compiled only when the library is built with
 * HAVE_HTTP3 defined (opt-in, because it pulls the ngtcp2/nghttp3/OpenSSL h3
 * stack). Without it the functions still exist but report that HTTP/3 is not
 * available, so callers link unconditionally and probe with n_http3_available().
 * The URL parser is a pure helper available in both builds.
 *
 * Active outbound requests must target only authorized hosts.
 */

#ifndef __NILOREA_HTTP3_HEADER
#define __NILOREA_HTTP3_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**@defgroup N_HTTP3 HTTP3: minimal blocking HTTP/3 client over QUIC
  @addtogroup N_HTTP3
  @{
  */

/*! Default per-request budget (milliseconds) when the caller passes <= 0. */
#define N_HTTP3_DEFAULT_TIMEOUT_MS 15000
/*! Hard cap on the response body kept in memory (bytes). */
#define N_HTTP3_BODY_CAP (16u * 1024u * 1024u)

/*! n_http3_request_ex flag: send the caller's extra headers WITHOUT the built-in
 *  connection-specific / framing filter (so an otherwise-forbidden header such as
 *  transfer-encoding, connection or upgrade reaches the wire). For protocol-conformance
 *  and request-smuggling security testing of how an HTTP/3 endpoint or a downgrading
 *  front-end handles an illegal framing header. Pseudo-headers (a name starting with ':')
 *  are still rejected. Use only against authorized targets. */
#define N_HTTP3_REQ_ALLOW_ILLEGAL_HEADERS 1u

/*! Result of an HTTP/3 exchange. Free the inner buffers with n_http3_response_free. */
typedef struct N_HTTP3_RESPONSE {
    int status;          /*!< HTTP status code, or 0 if none was received. */
    char* headers;       /*!< Response header block as joined "name: value\r\n" lines (lowercased names), or NULL. */
    unsigned char* body; /*!< Response body bytes (NOT NUL-terminated), or NULL. */
    size_t body_len;     /*!< Number of valid bytes in @ref body. */
    char* error;         /*!< Failure reason (heap), or NULL on a completed exchange. */
} N_HTTP3_RESPONSE;

/**
 *@brief Report whether HTTP/3 support was compiled into the library.
 *@return 1 when built with HAVE_HTTP3 (n_http3_request can run), 0 otherwise.
 */
int n_http3_available(void);

/**
 *@brief Split an https URL into host, port and path (pure; available in every build).
 *
 * Only the https scheme is accepted. When the URL omits the port, "443" is
 * written. When it omits the path, "/" is written. IPv6 literals in brackets
 * are accepted and the brackets are stripped from the host.
 *
 *@param url The absolute URL (must start with "https://"). NULL returns -1.
 *@param host Out buffer for the host. May be NULL to skip.
 *@param hostsz Size of @p host.
 *@param port Out buffer for the port (decimal string). May be NULL to skip.
 *@param portsz Size of @p port.
 *@param path Out buffer for the path (with a leading '/'). May be NULL to skip.
 *@param pathsz Size of @p path.
 *@return 0 on success, -1 on a malformed URL or a too-small buffer.
 */
int n_http3_parse_url(const char* url, char* host, size_t hostsz, char* port, size_t portsz, char* path, size_t pathsz);

/**
 *@brief Perform one blocking HTTP/3 request over a fresh QUIC connection.
 *
 * Resolves the host, opens a QUIC connection with ALPN "h3", sends the request,
 * and collects the response. On success @p out->status is set and @p out->error
 * is NULL; on failure the function returns -1 and @p out->error holds the reason.
 * The caller always frees @p out with n_http3_response_free.
 *
 *@param method The HTTP method (e.g. "GET", "POST"). NULL uses "GET".
 *@param url The absolute https URL. NULL/empty returns -1.
 *@param headers Optional extra request headers as "Name: Value\r\n" lines, or NULL.
 *@param body Optional request body bytes, or NULL.
 *@param body_len Length of @p body in bytes (0 when none).
 *@param timeout_ms Overall budget in ms; <= 0 uses N_HTTP3_DEFAULT_TIMEOUT_MS.
 *@param out Result, zeroed then filled. NULL returns -1.
 *@return 0 on a completed exchange (status received), -1 otherwise (out->error set).
 */
int n_http3_request(const char* method, const char* url, const char* headers, const unsigned char* body, size_t body_len, int timeout_ms, N_HTTP3_RESPONSE* out);

/**
 *@brief Perform one blocking HTTP/3 request, with request flags.
 *
 * As n_http3_request, plus @p flags. With N_HTTP3_REQ_ALLOW_ILLEGAL_HEADERS the caller's
 * extra headers bypass the connection-specific / framing filter, so a normally-forbidden
 * header (transfer-encoding, connection, upgrade, keep-alive, proxy-connection) is placed on
 * the HTTP/3 request as-is. This is for protocol-conformance and request-smuggling security
 * testing (how an endpoint or a downgrading front-end treats an illegal framing header);
 * pseudo-headers (names starting with ':') are still rejected. n_http3_request is exactly
 * n_http3_request_ex with @p flags 0.
 *
 *@param method The HTTP method (e.g. "GET", "POST"). NULL uses "GET".
 *@param url The absolute https URL. NULL/empty returns -1.
 *@param headers Optional extra request headers as "Name: Value\r\n" lines, or NULL.
 *@param body Optional request body bytes, or NULL.
 *@param body_len Length of @p body in bytes (0 when none).
 *@param timeout_ms Overall budget in ms; <= 0 uses N_HTTP3_DEFAULT_TIMEOUT_MS.
 *@param flags Bitwise OR of N_HTTP3_REQ_* flags (0 for the default, filtered behaviour).
 *@param out Result, zeroed then filled. NULL returns -1.
 *@return 0 on a completed exchange (status received), -1 otherwise (out->error set).
 */
int n_http3_request_ex(const char* method, const char* url, const char* headers, const unsigned char* body, size_t body_len, int timeout_ms, unsigned flags, N_HTTP3_RESPONSE* out);

/**
 *@brief Convenience wrapper: HTTP/3 GET of @p url.
 *@param url The absolute https URL. NULL/empty returns -1.
 *@param timeout_ms Overall budget in ms; <= 0 uses the default.
 *@param out Result (see n_http3_request). NULL returns -1.
 *@return 0 on a completed exchange, -1 otherwise.
 */
int n_http3_get(const char* url, int timeout_ms, N_HTTP3_RESPONSE* out);

/**
 *@brief Free the heap buffers held by @p r and zero the struct (safe on NULL / zeroed).
 *@param r The response to release.
 */
void n_http3_response_free(N_HTTP3_RESPONSE* r);

/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_HTTP3_HEADER */
