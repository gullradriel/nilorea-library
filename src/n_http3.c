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
 *@file n_http3.c
 *@brief Implementation of the minimal blocking HTTP/3 client (see n_http3.h)
 *@author Castagnier Mickael
 *@version 1.0
 *
 * The QUIC transport is driven by ngtcp2 and the HTTP/3 framing + QPACK by
 * nghttp3; TLS 1.3 is handled by OpenSSL through the ngtcp2 "ossl" crypto shim
 * (the OpenSSL native QUIC handshake). Everything below the pure helpers is
 * compiled only under HAVE_HTTP3.
 */

#include "nilorea/n_http3.h"
#include "nilorea/n_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- */
/* pure helpers (available in every build)                          */
/* ---------------------------------------------------------------- */

void n_http3_response_free(N_HTTP3_RESPONSE* r) {
    if (!r) {
        return;
    }
    free(r->headers);
    free(r->body);
    free(r->error);
    r->headers = NULL;
    r->body = NULL;
    r->error = NULL;
    r->body_len = 0;
    r->status = 0;
}

int n_http3_parse_url(const char* url, char* host, size_t hostsz, char* port, size_t portsz, char* path, size_t pathsz) {
    const char* p;
    const char* host_start;
    const char* slash;
    const char* hostend;
    const char* colon;
    size_t hlen;
    size_t plen;
    if (!url || strncmp(url, "https://", 8) != 0) {
        return -1;
    }
    p = url + 8;
    host_start = p;
    slash = strchr(p, '/');
    hostend = slash ? slash : (p + strlen(p));

    /* IPv6 literal in brackets: [::1]:443 */
    if (*host_start == '[') {
        const char* rb = memchr(host_start, ']', (size_t)(hostend - host_start));
        if (!rb) {
            return -1;
        }
        hlen = (size_t)(rb - host_start - 1);
        host_start++;
        colon = (rb + 1 < hostend && rb[1] == ':') ? (rb + 1) : NULL;
    } else {
        colon = memchr(host_start, ':', (size_t)(hostend - host_start));
        hlen = colon ? (size_t)(colon - host_start) : (size_t)(hostend - host_start);
    }
    if (hlen == 0) {
        return -1;
    }
    if (host) {
        if (hlen + 1 > hostsz) {
            return -1;
        }
        memcpy(host, host_start, hlen);
        host[hlen] = '\0';
    }
    if (port) {
        if (colon) {
            plen = (size_t)(hostend - colon - 1);
            if (plen == 0 || plen + 1 > portsz) {
                return -1;
            }
            memcpy(port, colon + 1, plen);
            port[plen] = '\0';
        } else {
            if (portsz < 4) {
                return -1;
            }
            memcpy(port, "443", 4);
        }
    }
    if (path) {
        if (slash) {
            plen = strlen(slash);
            if (plen + 1 > pathsz) {
                return -1;
            }
            memcpy(path, slash, plen + 1);
        } else {
            if (pathsz < 2) {
                return -1;
            }
            memcpy(path, "/", 2);
        }
    }
    return 0;
}

int n_http3_get(const char* url, int timeout_ms, N_HTTP3_RESPONSE* out) {
    return n_http3_request("GET", url, NULL, NULL, 0, timeout_ms, out);
}

/* ================================================================ */
#ifdef HAVE_HTTP3
/* ================================================================ */

#include "nilorea/n_network.h" /* portable SOCKET / closesocket / INVALID_SOCKET + socket includes */

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <nghttp3/nghttp3.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h> /* WSAPoll, already pulled by n_network.h on Windows */
#else
#include <poll.h>
#endif

/*! Maximum response header lines kept. */
#define H3_MAX_HEADERS 256
/*! Send datagram scratch size (a QUIC datagram fits well under this). */
#define H3_SEND_BUF 1500
/*! Receive scratch size (a UDP datagram fits well under this). */
#define H3_RECV_BUF 65536

/*! Request body cursor fed to nghttp3's data reader. */
typedef struct h3_body {
    const uint8_t* data; /*!< Body bytes (not owned). */
    size_t len;          /*!< Total length. */
} h3_body;

/*! One collected response header line. */
typedef struct h3_hdr {
    char* name;
    char* value;
} h3_hdr;

/*! Live client state for one HTTP/3 exchange. */
typedef struct h3_client {
    SOCKET fd;
    ngtcp2_conn* conn;
    ngtcp2_crypto_conn_ref conn_ref;
    SSL_CTX* ssl_ctx;
    SSL* ssl;
    ngtcp2_crypto_ossl_ctx* ossl_ctx;
    nghttp3_conn* h3;

    struct sockaddr_storage local_addr;
    socklen_t local_addrlen;
    struct sockaddr_storage remote_addr;
    socklen_t remote_addrlen;

    int64_t stream_id; /*!< request bidi stream, -1 until opened */
    int handshake_done;
    int stream_done;

    const char* host;
    const char* authority;
    const char* method;
    const char* path;
    const char* extra_headers; /*!< caller "Name: Value\r\n" lines, or NULL */
    int allow_illegal;         /*!< 1 to bypass the connection-specific / framing header filter (security testing) */
    h3_body req_body;
    int has_body;

    char status[8];
    h3_hdr headers[H3_MAX_HEADERS];
    int nheaders;
    uint8_t* body;
    size_t body_len;
    size_t body_cap;
    int oom; /*!< a collection allocation failed */
} h3_client;

/* ---------------------------------------------------------------- */
/* small platform + string helpers                                  */
/* ---------------------------------------------------------------- */

static uint64_t h3_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NGTCP2_SECONDS + (uint64_t)ts.tv_nsec;
}

static int h3_set_nonblocking(SOCKET fd) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(fd, (long)FIONBIO, &on) == 0 ? 0 : -1;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static int h3_would_block(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

/* poll the socket for readability. returns >0 readable, 0 timeout, -1 error. */
static int h3_poll_read(SOCKET fd, int timeout_ms) {
#ifdef _WIN32
    WSAPOLLFD pfd;
    pfd.fd = fd;
    pfd.events = POLLRDNORM;
    pfd.revents = 0;
    return WSAPoll(&pfd, 1, timeout_ms);
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return poll(&pfd, 1, timeout_ms);
#endif
}

static char* h3_dupn(const uint8_t* s, size_t n) {
    char* p = malloc(n + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ---------------------------------------------------------------- */
/* ngtcp2 <-> tls glue                                              */
/* ---------------------------------------------------------------- */

/* signature must match ngtcp2_crypto_conn_ref.get_conn, so ref stays non-const */
/* cppcheck-suppress constParameterCallback */
static ngtcp2_conn* h3_get_conn(ngtcp2_crypto_conn_ref* ref) {
    const h3_client* c = (const h3_client*)ref->user_data;
    return c->conn;
}

static void h3_rand_cb(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx* rctx) {
    (void)rctx;
    if (RAND_bytes(dest, (int)destlen) != 1) {
        size_t i;
        for (i = 0; i < destlen; i++) {
            dest[i] = (uint8_t)rand();
        }
    }
}

static int h3_get_new_cid_cb(ngtcp2_conn* conn, ngtcp2_cid* cid, uint8_t* token, size_t cidlen, void* user_data) {
    (void)conn;
    (void)user_data;
    if (RAND_bytes(cid->data, (int)cidlen) != 1) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    cid->datalen = cidlen;
    if (RAND_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN) != 1) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
}

static int h3_handshake_completed_cb(ngtcp2_conn* conn, void* user_data) {
    (void)conn;
    ((h3_client*)user_data)->handshake_done = 1;
    return 0;
}

static int h3_recv_stream_data_cb(ngtcp2_conn* conn, uint32_t flags, int64_t stream_id, uint64_t offset, const uint8_t* data, size_t datalen, void* user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)user_data;
    int fin = (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0;
    nghttp3_ssize nconsumed;
    (void)offset;
    (void)stream_user_data;
    if (!c->h3) {
        return 0;
    }
    nconsumed = nghttp3_conn_read_stream(c->h3, stream_id, data, datalen, fin);
    if (nconsumed < 0) {
        n_log(LOG_ERR, "nghttp3_conn_read_stream: %s", nghttp3_strerror((int)nconsumed));
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    ngtcp2_conn_extend_max_stream_offset(conn, stream_id, (uint64_t)nconsumed);
    ngtcp2_conn_extend_max_offset(conn, (uint64_t)nconsumed);
    return 0;
}

static int h3_acked_stream_data_offset_cb(ngtcp2_conn* conn, int64_t stream_id, uint64_t offset, uint64_t datalen, void* user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)user_data;
    (void)conn;
    (void)offset;
    (void)stream_user_data;
    if (c->h3) {
        nghttp3_conn_add_ack_offset(c->h3, stream_id, datalen);
    }
    return 0;
}

static int h3_stream_close_cb(ngtcp2_conn* conn, uint32_t flags, int64_t stream_id, uint64_t app_error_code, void* user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)user_data;
    (void)conn;
    (void)stream_user_data;
    if (!(flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)) {
        app_error_code = NGHTTP3_H3_NO_ERROR;
    }
    if (c->h3) {
        int rv = nghttp3_conn_close_stream(c->h3, stream_id, app_error_code);
        if (rv != 0 && rv != NGHTTP3_ERR_STREAM_NOT_FOUND) {
            n_log(LOG_ERR, "nghttp3_conn_close_stream: %s", nghttp3_strerror(rv));
            return NGTCP2_ERR_CALLBACK_FAILURE;
        }
    }
    if (stream_id == c->stream_id) {
        c->stream_done = 1;
    }
    return 0;
}

static int h3_stream_reset_cb(ngtcp2_conn* conn, int64_t stream_id, uint64_t final_size, uint64_t app_error_code, void* user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)user_data;
    (void)conn;
    (void)final_size;
    (void)app_error_code;
    (void)stream_user_data;
    if (c->h3) {
        nghttp3_conn_shutdown_stream_read(c->h3, stream_id);
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* nghttp3 callbacks                                                */
/* ---------------------------------------------------------------- */

static int h3_recv_header_cb(nghttp3_conn* conn, int64_t stream_id, int32_t token, nghttp3_rcbuf* name, nghttp3_rcbuf* value, uint8_t flags, void* conn_user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)conn_user_data;
    nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
    nghttp3_vec v = nghttp3_rcbuf_get_buf(value);
    (void)conn;
    (void)stream_id;
    (void)flags;
    (void)stream_user_data;
    if (token == NGHTTP3_QPACK_TOKEN__STATUS || (n.len == 7 && memcmp(n.base, ":status", 7) == 0)) {
        snprintf(c->status, sizeof(c->status), "%.*s", (int)v.len, (const char*)v.base);
        return 0;
    }
    if (c->nheaders < H3_MAX_HEADERS) {
        char* hn = h3_dupn(n.base, n.len);
        char* hv = h3_dupn(v.base, v.len);
        if (hn && hv) {
            c->headers[c->nheaders].name = hn;
            c->headers[c->nheaders].value = hv;
            c->nheaders++;
        } else {
            free(hn);
            free(hv);
            c->oom = 1;
        }
    }
    return 0;
}

static int h3_recv_data_cb(nghttp3_conn* conn, int64_t stream_id, const uint8_t* data, size_t datalen, void* conn_user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)conn_user_data;
    (void)conn;
    (void)stream_id;
    (void)stream_user_data;
    if (c->body_len + datalen > c->body_cap) {
        size_t newcap = c->body_cap ? c->body_cap : 65536;
        uint8_t* nb;
        while (newcap < c->body_len + datalen) {
            newcap *= 2;
        }
        if (newcap > N_HTTP3_BODY_CAP) {
            newcap = N_HTTP3_BODY_CAP;
        }
        if (c->body_len + datalen > newcap) {
            datalen = newcap - c->body_len; /* body exceeds the cap: keep what fits */
        }
        if (datalen == 0) {
            return 0;
        }
        nb = realloc(c->body, newcap);
        if (!nb) {
            c->oom = 1;
            return NGHTTP3_ERR_CALLBACK_FAILURE;
        }
        c->body = nb;
        c->body_cap = newcap;
    }
    if (datalen) {
        memcpy(c->body + c->body_len, data, datalen);
        c->body_len += datalen;
    }
    return 0;
}

static int h3_deferred_consume_cb(nghttp3_conn* conn, int64_t stream_id, size_t consumed, void* conn_user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)conn_user_data;
    (void)conn;
    (void)stream_user_data;
    ngtcp2_conn_extend_max_stream_offset(c->conn, stream_id, consumed);
    ngtcp2_conn_extend_max_offset(c->conn, consumed);
    return 0;
}

static int h3_end_stream_cb(nghttp3_conn* conn, int64_t stream_id, void* conn_user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)conn_user_data;
    (void)conn;
    (void)stream_user_data;
    if (stream_id == c->stream_id) {
        c->stream_done = 1;
    }
    return 0;
}

static int h3_stream_close_h3_cb(nghttp3_conn* conn, int64_t stream_id, uint64_t app_error_code, void* conn_user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)conn_user_data;
    (void)conn;
    (void)app_error_code;
    (void)stream_user_data;
    if (stream_id == c->stream_id) {
        c->stream_done = 1;
    }
    return 0;
}

/* request-body data reader: hand nghttp3 the whole buffer once, then EOF */
static nghttp3_ssize h3_body_read_cb(nghttp3_conn* conn, int64_t stream_id, nghttp3_vec* vec, size_t veccnt, uint32_t* pflags, void* conn_user_data, void* stream_user_data) {
    h3_client* c = (h3_client*)conn_user_data;
    (void)conn;
    (void)stream_id;
    (void)veccnt;
    (void)stream_user_data;
    *pflags = NGHTTP3_DATA_FLAG_EOF;
    if (c->req_body.len == 0) {
        return 0;
    }
    vec[0].base = (uint8_t*)c->req_body.data;
    vec[0].len = c->req_body.len;
    return 1;
}

/* ---------------------------------------------------------------- */
/* socket + connection setup                                        */
/* ---------------------------------------------------------------- */

static SOCKET h3_udp_connect(const char* host, const char* port, struct sockaddr_storage* remote, socklen_t* remotelen) {
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    struct addrinfo* rp;
    SOCKET fd = INVALID_SOCKET;
    int rv;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    rv = getaddrinfo(host, port, &hints, &res);
    if (rv != 0) {
        n_log(LOG_ERR, "getaddrinfo(%s:%s) failed", host, port);
        return INVALID_SOCKET;
    }
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) {
            continue;
        }
        if (connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            memcpy(remote, rp->ai_addr, rp->ai_addrlen);
            *remotelen = (socklen_t)rp->ai_addrlen;
            break;
        }
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (fd == INVALID_SOCKET) {
        n_log(LOG_ERR, "could not open a UDP socket to %s:%s", host, port);
        return INVALID_SOCKET;
    }
    if (h3_set_nonblocking(fd) != 0) {
        closesocket(fd);
        return INVALID_SOCKET;
    }
    return fd;
}

static int h3_setup_tls(h3_client* c) {
    static const unsigned char alpn[] = "\x02h3";
    c->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!c->ssl_ctx) {
        return -1;
    }
    SSL_CTX_set_min_proto_version(c->ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(c->ssl_ctx, TLS1_3_VERSION);
    c->ssl = SSL_new(c->ssl_ctx);
    if (!c->ssl) {
        return -1;
    }
    c->conn_ref.get_conn = h3_get_conn;
    c->conn_ref.user_data = c;
    SSL_set_app_data(c->ssl, &c->conn_ref);
    if (ngtcp2_crypto_ossl_configure_client_session(c->ssl) != 0) {
        n_log(LOG_ERR, "ngtcp2_crypto_ossl_configure_client_session failed");
        return -1;
    }
    SSL_set_connect_state(c->ssl);
    if (SSL_set_alpn_protos(c->ssl, alpn, sizeof(alpn) - 1) != 0) {
        return -1;
    }
    SSL_set_tlsext_host_name(c->ssl, c->host);
    SSL_set1_host(c->ssl, c->host);
    if (ngtcp2_crypto_ossl_ctx_new(&c->ossl_ctx, NULL) != 0) {
        return -1;
    }
    ngtcp2_crypto_ossl_ctx_set_ssl(c->ossl_ctx, c->ssl);
    return 0;
}

static int h3_setup_conn(h3_client* c) {
    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    ngtcp2_cid dcid;
    ngtcp2_cid scid;
    ngtcp2_path path;
    ngtcp2_callbacks cb;
    int rv;

    ngtcp2_settings_default(&settings);
    settings.initial_ts = h3_now_ns();

    ngtcp2_transport_params_default(&params);
    params.initial_max_streams_bidi = 0;
    params.initial_max_streams_uni = 3;
    params.initial_max_data = 1024 * 1024;
    params.initial_max_stream_data_bidi_local = 256 * 1024;
    params.initial_max_stream_data_bidi_remote = 256 * 1024;
    params.initial_max_stream_data_uni = 256 * 1024;

    dcid.datalen = 16;
    scid.datalen = 16;
    if (RAND_bytes(dcid.data, 16) != 1 || RAND_bytes(scid.data, 16) != 1) {
        return -1;
    }

    path.local.addr = (ngtcp2_sockaddr*)&c->local_addr;
    path.local.addrlen = c->local_addrlen;
    path.remote.addr = (ngtcp2_sockaddr*)&c->remote_addr;
    path.remote.addrlen = c->remote_addrlen;
    path.user_data = NULL;

    memset(&cb, 0, sizeof(cb));
    cb.client_initial = ngtcp2_crypto_client_initial_cb;
    cb.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
    cb.encrypt = ngtcp2_crypto_encrypt_cb;
    cb.decrypt = ngtcp2_crypto_decrypt_cb;
    cb.hp_mask = ngtcp2_crypto_hp_mask_cb;
    cb.recv_retry = ngtcp2_crypto_recv_retry_cb;
    cb.update_key = ngtcp2_crypto_update_key_cb;
    cb.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
    cb.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
    cb.get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb;
    cb.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
    cb.rand = h3_rand_cb;
    cb.get_new_connection_id = h3_get_new_cid_cb;
    cb.handshake_completed = h3_handshake_completed_cb;
    cb.recv_stream_data = h3_recv_stream_data_cb;
    cb.acked_stream_data_offset = h3_acked_stream_data_offset_cb;
    cb.stream_close = h3_stream_close_cb;
    cb.stream_reset = h3_stream_reset_cb;

    rv = ngtcp2_conn_client_new(&c->conn, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1, &cb, &settings, &params, NULL, c);
    if (rv != 0) {
        n_log(LOG_ERR, "ngtcp2_conn_client_new: %s", ngtcp2_strerror(rv));
        return -1;
    }
    ngtcp2_conn_set_tls_native_handle(c->conn, c->ossl_ctx);
    return 0;
}

/* lowercase a header name in place (ASCII) */
static void h3_lower(char* s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            s[i] = (char)(s[i] - 'A' + 'a');
        }
    }
}

/* a header name HTTP/3 forbids (connection-specific) or that we set ourselves */
static int h3_header_banned(const char* name, size_t n) {
    static const char* banned[] = {"host", "connection", "transfer-encoding", "keep-alive", "upgrade", "proxy-connection", "content-length", NULL};
    int i;
    for (i = 0; banned[i]; i++) {
        if (strlen(banned[i]) == n && memcmp(name, banned[i], n) == 0) {
            return 1;
        }
    }
    return 0;
}

static int h3_setup_h3(h3_client* c) {
    nghttp3_settings h3settings;
    nghttp3_callbacks h3cb;
    nghttp3_nv nva[4 + H3_MAX_HEADERS];
    size_t nvlen = 0;
    /* a pointer to clen is kept in nva until submit_request, so it must live to the end of the function */
    /* cppcheck-suppress variableScope */
    char clen[32];
    char* hdr_copy = NULL;
    int64_t ctrl_id = -1;
    int64_t enc_id = -1;
    int64_t dec_id = -1;
    int64_t stream_id = -1;
    nghttp3_data_reader dr;
    const nghttp3_data_reader* drp = NULL;
    int rv;

    nghttp3_settings_default(&h3settings);
    memset(&h3cb, 0, sizeof(h3cb));
    h3cb.recv_header = h3_recv_header_cb;
    h3cb.recv_data = h3_recv_data_cb;
    h3cb.deferred_consume = h3_deferred_consume_cb;
    h3cb.end_stream = h3_end_stream_cb;
    h3cb.stream_close = h3_stream_close_h3_cb;

    rv = nghttp3_conn_client_new(&c->h3, &h3cb, &h3settings, NULL, c);
    if (rv != 0) {
        n_log(LOG_ERR, "nghttp3_conn_client_new: %s", nghttp3_strerror(rv));
        return -1;
    }

    if (ngtcp2_conn_open_uni_stream(c->conn, &ctrl_id, NULL) != 0 || nghttp3_conn_bind_control_stream(c->h3, ctrl_id) != 0) {
        return -1;
    }
    if (ngtcp2_conn_open_uni_stream(c->conn, &enc_id, NULL) != 0 || ngtcp2_conn_open_uni_stream(c->conn, &dec_id, NULL) != 0 || nghttp3_conn_bind_qpack_streams(c->h3, enc_id, dec_id) != 0) {
        return -1;
    }
    if (ngtcp2_conn_open_bidi_stream(c->conn, &stream_id, NULL) != 0) {
        return -1;
    }
    c->stream_id = stream_id;

    nva[nvlen++] = (nghttp3_nv){(uint8_t*)":method", (uint8_t*)c->method, 7, strlen(c->method), NGHTTP3_NV_FLAG_NONE};
    nva[nvlen++] = (nghttp3_nv){(uint8_t*)":scheme", (uint8_t*)"https", 7, 5, NGHTTP3_NV_FLAG_NONE};
    nva[nvlen++] = (nghttp3_nv){(uint8_t*)":authority", (uint8_t*)c->authority, 10, strlen(c->authority), NGHTTP3_NV_FLAG_NONE};
    nva[nvlen++] = (nghttp3_nv){(uint8_t*)":path", (uint8_t*)c->path, 5, strlen(c->path), NGHTTP3_NV_FLAG_NONE};

    if (c->has_body) {
        int wl = snprintf(clen, sizeof(clen), "%zu", c->req_body.len);
        if (wl > 0) {
            nva[nvlen++] = (nghttp3_nv){(uint8_t*)"content-length", (uint8_t*)clen, 14, (size_t)wl, NGHTTP3_NV_FLAG_NONE};
        }
    }

    /* caller-supplied extra headers ("Name: Value\r\n" lines) */
    if (c->extra_headers && c->extra_headers[0]) {
        hdr_copy = strdup(c->extra_headers);
        if (hdr_copy) {
            char* save = NULL;
            char* line;
            for (line = strtok_r(hdr_copy, "\r\n", &save); line && nvlen < (sizeof(nva) / sizeof(nva[0])); line = strtok_r(NULL, "\r\n", &save)) {
                char* colon = strchr(line, ':');
                char* val;
                size_t nlen;
                if (!colon) {
                    continue;
                }
                *colon = '\0';
                nlen = strlen(line);
                h3_lower(line, nlen);
                /* a pseudo-header (':...') is never caller-injectable; the connection-specific /
                   framing filter is bypassed when allow_illegal is set (security testing) */
                if (nlen == 0 || line[0] == ':' || (!c->allow_illegal && h3_header_banned(line, nlen))) {
                    continue;
                }
                val = colon + 1;
                while (*val == ' ' || *val == '\t') {
                    val++;
                }
                nva[nvlen++] = (nghttp3_nv){(uint8_t*)line, (uint8_t*)val, nlen, strlen(val), NGHTTP3_NV_FLAG_NONE};
            }
        }
    }

    if (c->has_body) {
        dr.read_data = h3_body_read_cb;
        drp = &dr;
    }
    rv = nghttp3_conn_submit_request(c->h3, stream_id, nva, nvlen, drp, c);
    free(hdr_copy);
    if (rv != 0) {
        n_log(LOG_ERR, "nghttp3_conn_submit_request: %s", nghttp3_strerror(rv));
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* read / write pumps + event loop                                  */
/* ---------------------------------------------------------------- */

static int h3_write(h3_client* c) {
    uint8_t buf[H3_SEND_BUF];
    ngtcp2_path_storage ps;
    ngtcp2_pkt_info pi;
    uint64_t ts = h3_now_ns();
    nghttp3_vec vec[16];
    size_t max_udp;

    ngtcp2_path_storage_zero(&ps);
    memset(&pi, 0, sizeof(pi));
    max_udp = ngtcp2_conn_get_max_tx_udp_payload_size(c->conn);
    if (max_udp > sizeof(buf)) {
        max_udp = sizeof(buf);
    }

    for (;;) {
        int64_t stream_id = -1;
        int fin = 0;
        nghttp3_ssize sveccnt = 0;
        ngtcp2_ssize ndatalen = 0;
        uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_MORE;
        ngtcp2_ssize nwrite;

        if (c->h3 && ngtcp2_conn_get_max_data_left(c->conn)) {
            sveccnt = nghttp3_conn_writev_stream(c->h3, &stream_id, &fin, vec, 16);
            if (sveccnt < 0) {
                n_log(LOG_ERR, "nghttp3_conn_writev_stream: %s", nghttp3_strerror((int)sveccnt));
                return -1;
            }
        }
        if (fin) {
            flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
        }
        nwrite = ngtcp2_conn_writev_stream(c->conn, &ps.path, &pi, buf, max_udp, &ndatalen, flags, stream_id, (const ngtcp2_vec*)vec, (size_t)sveccnt, ts);
        if (nwrite < 0) {
            switch (nwrite) {
                case NGTCP2_ERR_STREAM_DATA_BLOCKED:
                    if (c->h3 && stream_id >= 0) {
                        nghttp3_conn_block_stream(c->h3, stream_id);
                    }
                    continue;
                case NGTCP2_ERR_STREAM_SHUT_WR:
                    if (c->h3 && stream_id >= 0) {
                        nghttp3_conn_shutdown_stream_write(c->h3, stream_id);
                    }
                    continue;
                case NGTCP2_ERR_WRITE_MORE:
                    if (c->h3 && stream_id >= 0 && ndatalen >= 0) {
                        if (nghttp3_conn_add_write_offset(c->h3, stream_id, (size_t)ndatalen) != 0) {
                            return -1;
                        }
                    }
                    continue;
                default:
                    n_log(LOG_ERR, "ngtcp2_conn_writev_stream: %s", ngtcp2_strerror((int)nwrite));
                    return -1;
            }
        }
        if (c->h3 && stream_id >= 0 && ndatalen >= 0) {
            if (nghttp3_conn_add_write_offset(c->h3, stream_id, (size_t)ndatalen) != 0) {
                return -1;
            }
        }
        if (nwrite == 0) {
            return 0; /* nothing more to send now */
        }
        for (;;) {
            ssize_t s = send(c->fd, (const char*)buf, (size_t)nwrite, 0);
            if (s < 0) {
                if (h3_would_block()) {
                    break; /* buffer full / interrupted: QUIC will retransmit */
                }
                n_log(LOG_ERR, "send failed on the QUIC socket");
                return -1;
            }
            break;
        }
    }
}

static int h3_read(h3_client* c) {
    uint8_t buf[H3_RECV_BUF];
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t sslen = sizeof(ss);
        ngtcp2_path path;
        ngtcp2_pkt_info pi;
        int rv;
        ssize_t n = recvfrom(c->fd, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&ss, &sslen);
        if (n < 0) {
            if (h3_would_block()) {
                return 0;
            }
            n_log(LOG_ERR, "recvfrom failed on the QUIC socket");
            return -1;
        }
        path.local.addr = (ngtcp2_sockaddr*)&c->local_addr;
        path.local.addrlen = c->local_addrlen;
        path.remote.addr = (ngtcp2_sockaddr*)&ss;
        path.remote.addrlen = sslen;
        path.user_data = NULL;
        memset(&pi, 0, sizeof(pi));
        rv = ngtcp2_conn_read_pkt(c->conn, &path, &pi, buf, (size_t)n, h3_now_ns());
        if (rv != 0) {
            if (rv == NGTCP2_ERR_DRAINING || rv == NGTCP2_ERR_CLOSING) {
                c->stream_done = 1;
                return 0;
            }
            n_log(LOG_ERR, "ngtcp2_conn_read_pkt: %s", ngtcp2_strerror(rv));
            return -1;
        }
    }
}

static int h3_run(h3_client* c, int timeout_ms) {
    uint64_t start = h3_now_ns();
    uint64_t budget_ns = (uint64_t)timeout_ms * NGTCP2_MILLISECONDS;
    for (;;) {
        ngtcp2_tstamp expiry;
        uint64_t now;
        int timeout;
        int pr;

        if (h3_write(c) != 0) {
            return -1;
        }
        if (c->handshake_done && !c->h3) {
            if (h3_setup_h3(c) != 0) {
                return -1;
            }
            continue; /* flush the request immediately */
        }
        if (c->stream_done) {
            return 0;
        }
        if (h3_now_ns() - start > budget_ns) {
            n_log(LOG_ERR, "n_http3: timed out after %d ms waiting for the response", timeout_ms);
            return -1;
        }
        expiry = ngtcp2_conn_get_expiry(c->conn);
        now = h3_now_ns();
        if (expiry == UINT64_MAX) {
            timeout = 1000;
        } else if (expiry <= now) {
            timeout = 0;
        } else {
            uint64_t d = (expiry - now) / NGTCP2_MILLISECONDS;
            timeout = d > 1000 ? 1000 : (int)d;
        }
        pr = h3_poll_read(c->fd, timeout);
        if (pr < 0) {
            if (h3_would_block()) {
                continue;
            }
            n_log(LOG_ERR, "poll failed on the QUIC socket");
            return -1;
        }
        if (pr == 0) {
            int rv = ngtcp2_conn_handle_expiry(c->conn, h3_now_ns());
            if (rv != 0) {
                n_log(LOG_ERR, "ngtcp2_conn_handle_expiry: %s", ngtcp2_strerror(rv));
                return -1;
            }
            continue;
        }
        if (h3_read(c) != 0) {
            return -1;
        }
    }
}

static void h3_close_conn(h3_client* c) {
    uint8_t buf[H3_SEND_BUF];
    ngtcp2_path_storage ps;
    ngtcp2_pkt_info pi;
    ngtcp2_ccerr ccerr;
    ngtcp2_ssize n;
    if (!c->conn) {
        return;
    }
    ngtcp2_path_storage_zero(&ps);
    memset(&pi, 0, sizeof(pi));
    ngtcp2_ccerr_default(&ccerr);
    n = ngtcp2_conn_write_connection_close(c->conn, &ps.path, &pi, buf, sizeof(buf), &ccerr, h3_now_ns());
    if (n > 0) {
        ssize_t s = send(c->fd, (const char*)buf, (size_t)n, 0);
        (void)s;
    }
}

static void h3_client_cleanup(h3_client* c) {
    int i;
    for (i = 0; i < c->nheaders; i++) {
        free(c->headers[i].name);
        free(c->headers[i].value);
    }
    free(c->body);
    if (c->h3) {
        nghttp3_conn_del(c->h3);
    }
    if (c->conn) {
        ngtcp2_conn_del(c->conn);
    }
    if (c->ossl_ctx) {
        ngtcp2_crypto_ossl_ctx_del(c->ossl_ctx);
    }
    if (c->ssl) {
        SSL_free(c->ssl);
    }
    if (c->ssl_ctx) {
        SSL_CTX_free(c->ssl_ctx);
    }
    if (c->fd != INVALID_SOCKET) {
        closesocket(c->fd);
    }
}

/* build out->headers by joining the collected header lines */
static char* h3_join_headers(const h3_client* c) {
    size_t total = 1;
    char* out;
    char* p;
    int i;
    for (i = 0; i < c->nheaders; i++) {
        total += strlen(c->headers[i].name) + 2 + strlen(c->headers[i].value) + 2;
    }
    out = malloc(total);
    if (!out) {
        return NULL;
    }
    p = out;
    for (i = 0; i < c->nheaders; i++) {
        int wl = snprintf(p, total - (size_t)(p - out), "%s: %s\r\n", c->headers[i].name, c->headers[i].value);
        if (wl > 0) {
            p += wl;
        }
    }
    *p = '\0';
    return out;
}

static int h3_fail(N_HTTP3_RESPONSE* out, const char* msg) {
    if (out && !out->error) {
        out->error = strdup(msg);
    }
    return -1;
}

int n_http3_available(void) {
    return 1;
}

int n_http3_request(const char* method, const char* url, const char* headers, const unsigned char* body, size_t body_len, int timeout_ms, N_HTTP3_RESPONSE* out) {
    return n_http3_request_ex(method, url, headers, body, body_len, timeout_ms, 0u, out);
}

int n_http3_request_ex(const char* method, const char* url, const char* headers, const unsigned char* body, size_t body_len, int timeout_ms, unsigned flags, N_HTTP3_RESPONSE* out) {
    static int ossl_ready = 0;
    h3_client c;
    char host[256];
    char port[16];
    char path[2048];
    char authority[300];
    int ret;

    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!url || !url[0]) {
        return h3_fail(out, "empty url");
    }
    if (n_http3_parse_url(url, host, sizeof(host), port, sizeof(port), path, sizeof(path)) != 0) {
        return h3_fail(out, "could not parse the https url");
    }
    if (timeout_ms <= 0) {
        timeout_ms = N_HTTP3_DEFAULT_TIMEOUT_MS;
    }
    if (strcmp(port, "443") == 0) {
        snprintf(authority, sizeof(authority), "%s", host);
    } else {
        snprintf(authority, sizeof(authority), "%s:%s", host, port);
    }

    if (!ossl_ready) {
        if (ngtcp2_crypto_ossl_init() != 0) {
            return h3_fail(out, "ngtcp2_crypto_ossl_init failed");
        }
#ifdef _WIN32
        {
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa); /* ensure winsock is up (idempotent per process) */
        }
#endif
        ossl_ready = 1;
    }

    memset(&c, 0, sizeof(c));
    c.stream_id = -1;
    c.host = host;
    c.authority = authority;
    c.method = (method && method[0]) ? method : "GET";
    c.path = path;
    c.extra_headers = headers;
    c.allow_illegal = (flags & N_HTTP3_REQ_ALLOW_ILLEGAL_HEADERS) ? 1 : 0;
    if (body && body_len > 0) {
        c.req_body.data = body;
        c.req_body.len = body_len;
        c.has_body = 1;
    }

    c.fd = h3_udp_connect(host, port, &c.remote_addr, &c.remote_addrlen);
    if (c.fd == INVALID_SOCKET) {
        h3_client_cleanup(&c);
        return h3_fail(out, "could not open a QUIC (UDP) socket to the host");
    }
    c.local_addrlen = sizeof(c.local_addr);
    if (getsockname(c.fd, (struct sockaddr*)&c.local_addr, &c.local_addrlen) != 0) {
        h3_client_cleanup(&c);
        return h3_fail(out, "getsockname failed");
    }
    if (h3_setup_tls(&c) != 0) {
        h3_client_cleanup(&c);
        return h3_fail(out, "TLS setup for QUIC failed");
    }
    if (h3_setup_conn(&c) != 0) {
        h3_client_cleanup(&c);
        return h3_fail(out, "QUIC connection setup failed");
    }

    if (h3_run(&c, timeout_ms) != 0) {
        h3_close_conn(&c);
        h3_client_cleanup(&c);
        return h3_fail(out, "the HTTP/3 exchange failed (see the log)");
    }
    h3_close_conn(&c);

    out->status = c.status[0] ? atoi(c.status) : 0;
    out->headers = h3_join_headers(&c);
    if (c.body && c.body_len) {
        out->body = malloc(c.body_len);
        if (out->body) {
            memcpy(out->body, c.body, c.body_len);
            out->body_len = c.body_len;
        }
    }
    ret = out->status ? 0 : h3_fail(out, "no HTTP status was received");
    if (c.oom && !out->error) {
        out->error = strdup("some response data was dropped (out of memory or body cap)");
    }
    h3_client_cleanup(&c);
    return ret;
}

/* ================================================================ */
#else /* !HAVE_HTTP3 */
/* ================================================================ */

int n_http3_available(void) {
    return 0;
}

int n_http3_request(const char* method, const char* url, const char* headers, const unsigned char* body, size_t body_len, int timeout_ms, N_HTTP3_RESPONSE* out) {
    (void)method;
    (void)url;
    (void)headers;
    (void)body;
    (void)body_len;
    (void)timeout_ms;
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->error = strdup("HTTP/3 support was not compiled in (build the library with HAVE_HTTP3)");
    return -1;
}

int n_http3_request_ex(const char* method, const char* url, const char* headers, const unsigned char* body, size_t body_len, int timeout_ms, unsigned flags, N_HTTP3_RESPONSE* out) {
    (void)flags;
    return n_http3_request(method, url, headers, body, body_len, timeout_ms, out);
}

#endif /* HAVE_HTTP3 */
