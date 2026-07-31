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
 *@file n_http2.h
 *@brief HTTP/2 (RFC 7540) wire framing plus HPACK header compression (RFC 7541)
 *@author Castagnier Mickael
 *@version 1.0
 *@date 2026
 *
 * The framing layer of HTTP/2: the client connection preface, the frame-type and
 * flag constants, a 9-byte frame-header parse/build, and a SETTINGS payload
 * reader. The frame codec is stateless and does no allocation, so a caller drives
 * its own reassembly buffer.
 *
 * On top of the framing sits HPACK (RFC 7541): an N_H2_HPACK codec context with a
 * per-direction dynamic table, a full decoder (indexed and literal fields, the
 * static and dynamic tables, Huffman-coded and literal strings, and dynamic table
 * size updates), and a self-contained encoder (literal fields without indexing,
 * no Huffman) that needs no shared state. The stream/connection state machine
 * builds on these two layers.
 */

#ifndef __NILOREA_HTTP2_HEADER
#define __NILOREA_HTTP2_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/*! the client connection preface sent before any HTTP/2 frame (RFC 7540 3.5) */
#define N_H2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
/*! length of the fixed frame header in bytes */
#define N_H2_FRAME_HEADER_LEN 9
/*! largest frame payload the parser accepts (the default SETTINGS_MAX_FRAME_SIZE) */
#define N_H2_MAX_FRAME_PAYLOAD 16384

/*! @name HTTP/2 frame types (RFC 7540 6) */
/*!@{*/
#define N_H2_DATA 0x0          /*!< DATA */
#define N_H2_HEADERS 0x1       /*!< HEADERS */
#define N_H2_PRIORITY 0x2      /*!< PRIORITY */
#define N_H2_RST_STREAM 0x3    /*!< RST_STREAM */
#define N_H2_SETTINGS 0x4      /*!< SETTINGS */
#define N_H2_PUSH_PROMISE 0x5  /*!< PUSH_PROMISE */
#define N_H2_PING 0x6          /*!< PING */
#define N_H2_GOAWAY 0x7        /*!< GOAWAY */
#define N_H2_WINDOW_UPDATE 0x8 /*!< WINDOW_UPDATE */
#define N_H2_CONTINUATION 0x9  /*!< CONTINUATION */
/*!@}*/

/*! @name HTTP/2 frame flags (RFC 7540 6) */
/*!@{*/
#define N_H2_FLAG_END_STREAM 0x1  /*!< DATA/HEADERS: last frame of the stream */
#define N_H2_FLAG_ACK 0x1         /*!< SETTINGS/PING: acknowledgement */
#define N_H2_FLAG_END_HEADERS 0x4 /*!< HEADERS/PUSH_PROMISE/CONTINUATION: header block complete */
#define N_H2_FLAG_PADDED 0x8      /*!< DATA/HEADERS/PUSH_PROMISE: a pad-length prefix is present */
#define N_H2_FLAG_PRIORITY 0x20   /*!< HEADERS: priority fields are present */
/*!@}*/

/*! @name SETTINGS parameter identifiers (RFC 7540 6.5.2) */
/*!@{*/
#define N_H2_SETTINGS_HEADER_TABLE_SIZE 0x1
#define N_H2_SETTINGS_ENABLE_PUSH 0x2
#define N_H2_SETTINGS_MAX_CONCURRENT_STREAMS 0x3
#define N_H2_SETTINGS_INITIAL_WINDOW_SIZE 0x4
#define N_H2_SETTINGS_MAX_FRAME_SIZE 0x5
#define N_H2_SETTINGS_MAX_HEADER_LIST_SIZE 0x6
/*!@}*/

/*! a parsed HTTP/2 frame; payload aliases the input buffer */
typedef struct N_H2_FRAME {
    uint32_t length;              /*!< payload length (24-bit) */
    int type;                     /*!< frame type (N_H2_*) */
    int flags;                    /*!< frame flags */
    uint32_t stream_id;           /*!< stream identifier (31-bit; the reserved bit is cleared) */
    const unsigned char* payload; /*!< pointer into the input buffer (may be NULL when length is 0) */
} N_H2_FRAME;

/*! one SETTINGS parameter (identifier and value) */
typedef struct N_H2_SETTING {
    uint16_t id;    /*!< parameter identifier (N_H2_SETTINGS_*) */
    uint32_t value; /*!< parameter value */
} N_H2_SETTING;

/*! @brief parse one HTTP/2 frame from a byte buffer (stateless). Returns 1 and
 *  fills frame + *consumed (header + payload) on a complete frame, 0 when more
 *  bytes are needed, -1 when the payload exceeds N_H2_MAX_FRAME_PAYLOAD. */
int n_http2_frame_parse(const unsigned char* buf, size_t len, N_H2_FRAME* frame, size_t* consumed);

/*! @brief write a 9-byte HTTP/2 frame header into out. Returns N_H2_FRAME_HEADER_LEN,
 *  or 0 when out_cap is too small or length exceeds 24 bits. */
size_t n_http2_frame_build_header(unsigned char* out, size_t out_cap, uint32_t length, int type, int flags, uint32_t stream_id);

/*! @brief parse a SETTINGS frame payload (6-byte id/value entries) into out.
 *  Returns 0 on success (*count set, <= max), -1 when the payload length is not a
 *  multiple of 6. */
int n_http2_settings_parse(const unsigned char* payload, size_t len, N_H2_SETTING* out, size_t max, size_t* count);

/*! the default HPACK dynamic table size (SETTINGS_HEADER_TABLE_SIZE default) */
#define N_H2_HPACK_DEFAULT_TABLE_SIZE 4096

/*! one HPACK header field; after a decode both strings are owned (heap) */
typedef struct N_H2_HEADER {
    char* name;  /*!< header field name (owned after decode) */
    char* value; /*!< header field value (owned after decode) */
} N_H2_HEADER;

/*! opaque HPACK codec context holding one direction's dynamic table (RFC 7541) */
typedef struct N_H2_HPACK N_H2_HPACK;

/*! @brief create an HPACK codec context with the given dynamic table size limit
 *  (0 uses N_H2_HPACK_DEFAULT_TABLE_SIZE). Returns NULL on allocation failure. */
N_H2_HPACK* n_http2_hpack_new(size_t max_table_size);

/*! @brief free an HPACK codec context and NULL the caller's pointer */
void n_http2_hpack_free(N_H2_HPACK** ctx);

/*! @brief change the dynamic table size limit (a SETTINGS_HEADER_TABLE_SIZE
 *  change), evicting entries as needed so the table fits. */
void n_http2_hpack_set_max_size(N_H2_HPACK* ctx, size_t max_table_size);

/*! @brief decode an HPACK header block into out[] (owned name/value strings).
 *
 *  Handles indexed fields (static and dynamic table), the three literal forms
 *  (with/without/never indexing), Huffman-coded and literal strings, and dynamic
 *  table size updates. On success *count holds the number of headers written
 *  (<= max_out) and the dynamic table is updated. Returns 0 on success, -1 on a
 *  malformed block or when more than max_out headers are produced. Free the
 *  decoded headers with n_http2_hpack_headers_free.
 *
 *  @param ctx codec context (its dynamic table is read and updated)
 *  @param block the header block fragment bytes
 *  @param len length of block
 *  @param out array receiving the decoded headers
 *  @param max_out capacity of out
 *  @param count receives the number of headers decoded
 *  @return 0 on success, -1 on error */
int n_http2_hpack_decode(N_H2_HPACK* ctx, const unsigned char* block, size_t len, N_H2_HEADER* out, size_t max_out, size_t* count);

/*! @brief encode a header list into a block using literal representations without
 *  indexing and without Huffman (a valid, interoperable, self-contained encoding
 *  that needs no dynamic-table state). Returns the number of bytes written, or 0
 *  when out_cap is too small or an argument is invalid.
 *
 *  @param headers the header fields to encode (name/value C strings)
 *  @param n number of headers
 *  @param out destination buffer
 *  @param out_cap capacity of out
 *  @return bytes written, or 0 on error */
size_t n_http2_hpack_encode(const N_H2_HEADER* headers, size_t n, unsigned char* out, size_t out_cap);

/*! @brief free the owned name/value strings of a decoded header array (does not
 *  free the array itself) */
void n_http2_hpack_headers_free(N_H2_HEADER* headers, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_HTTP2_HEADER */
