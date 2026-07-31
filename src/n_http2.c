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
 *@file n_http2.c
 *@brief HTTP/2 (RFC 7540) wire framing and HPACK (RFC 7541) header compression
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_http2.h"

#include <stdlib.h>
#include <string.h>

int n_http2_frame_parse(const unsigned char* buf, size_t len, N_H2_FRAME* frame, size_t* consumed) {
    uint32_t plen;
    if (!buf || !frame || !consumed)
        return -1;
    if (len < N_H2_FRAME_HEADER_LEN)
        return 0;
    plen = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | (uint32_t)buf[2];
    if (plen > N_H2_MAX_FRAME_PAYLOAD)
        return -1;
    if (len < (size_t)N_H2_FRAME_HEADER_LEN + plen)
        return 0;
    frame->length = plen;
    frame->type = buf[3];
    frame->flags = buf[4];
    frame->stream_id = (((uint32_t)buf[5] & 0x7F) << 24) | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 8) | (uint32_t)buf[8];
    frame->payload = (plen > 0) ? (buf + N_H2_FRAME_HEADER_LEN) : NULL;
    *consumed = (size_t)N_H2_FRAME_HEADER_LEN + plen;
    return 1;
}

size_t n_http2_frame_build_header(unsigned char* out, size_t out_cap, uint32_t length, int type, int flags, uint32_t stream_id) {
    if (!out || out_cap < N_H2_FRAME_HEADER_LEN)
        return 0;
    if (length > 0x00FFFFFFu)
        return 0;
    out[0] = (unsigned char)((length >> 16) & 0xFF);
    out[1] = (unsigned char)((length >> 8) & 0xFF);
    out[2] = (unsigned char)(length & 0xFF);
    out[3] = (unsigned char)(type & 0xFF);
    out[4] = (unsigned char)(flags & 0xFF);
    /* the reserved top bit of the stream id is always sent as 0 */
    out[5] = (unsigned char)((stream_id >> 24) & 0x7F);
    out[6] = (unsigned char)((stream_id >> 16) & 0xFF);
    out[7] = (unsigned char)((stream_id >> 8) & 0xFF);
    out[8] = (unsigned char)(stream_id & 0xFF);
    return N_H2_FRAME_HEADER_LEN;
}

int n_http2_settings_parse(const unsigned char* payload, size_t len, N_H2_SETTING* out, size_t max, size_t* count) {
    size_t i, n = 0;
    if (count)
        *count = 0;
    if (len % 6 != 0)
        return -1;
    if (!payload && len > 0)
        return -1;
    for (i = 0; i + 6 <= len && n < max; i += 6) {
        if (out) {
            out[n].id = (uint16_t)(((uint16_t)payload[i] << 8) | (uint16_t)payload[i + 1]);
            out[n].value = ((uint32_t)payload[i + 2] << 24) | ((uint32_t)payload[i + 3] << 16) | ((uint32_t)payload[i + 4] << 8) | (uint32_t)payload[i + 5];
        }
        n++;
    }
    if (count)
        *count = n;
    return 0;
}

/* ---- HPACK header compression (RFC 7541) ---- */

/*! HPACK static table names (RFC 7541 Appendix A, index 1..61) */
static const char* const hpack_static_name[61] = {
    ":authority", ":method", ":method", ":path", ":path", ":scheme", ":scheme",
    ":status", ":status", ":status", ":status", ":status", ":status", ":status",
    "accept-charset", "accept-encoding", "accept-language", "accept-ranges", "accept",
    "access-control-allow-origin", "age", "allow", "authorization", "cache-control",
    "content-disposition", "content-encoding", "content-language", "content-length",
    "content-location", "content-range", "content-type", "cookie", "date", "etag",
    "expect", "expires", "from", "host", "if-match", "if-modified-since", "if-none-match",
    "if-range", "if-unmodified-since", "last-modified", "link", "location", "max-forwards",
    "proxy-authenticate", "proxy-authorization", "range", "referer", "refresh", "retry-after",
    "server", "set-cookie", "strict-transport-security", "transfer-encoding", "user-agent",
    "vary", "via", "www-authenticate"};

/*! HPACK static table values (empty where the entry has no value) */
static const char* const hpack_static_value[61] = {
    "", "GET", "POST", "/", "/index.html", "http", "https",
    "200", "204", "206", "304", "400", "404", "500",
    "", "gzip, deflate", "", "", "",
    "", "", "", "", "",
    "", "", "", "",
    "", "", "", "", "", "",
    "", "", "", "", "", "", "",
    "", "", "", "", "", "",
    "", "", "", "", "", "",
    "", "", "", "", "",
    "", "", ""};

/*! HPACK Huffman codes (RFC 7541 Appendix B), index = byte value 0..255 */
static const uint32_t hpack_huff_code[256] = {
    0x1ff8, 0x7fffd8, 0xfffffe2, 0xfffffe3, 0xfffffe4, 0xfffffe5, 0xfffffe6, 0xfffffe7,
    0xfffffe8, 0xffffea, 0x3ffffffc, 0xfffffe9, 0xfffffea, 0x3ffffffd, 0xfffffeb, 0xfffffec,
    0xfffffed, 0xfffffee, 0xfffffef, 0xffffff0, 0xffffff1, 0xffffff2, 0x3ffffffe, 0xffffff3,
    0xffffff4, 0xffffff5, 0xffffff6, 0xffffff7, 0xffffff8, 0xffffff9, 0xffffffa, 0xffffffb,
    0x14, 0x3f8, 0x3f9, 0xffa, 0x1ff9, 0x15, 0xf8, 0x7fa,
    0x3fa, 0x3fb, 0xf9, 0x7fb, 0xfa, 0x16, 0x17, 0x18,
    0x0, 0x1, 0x2, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
    0x1e, 0x1f, 0x5c, 0xfb, 0x7ffc, 0x20, 0xffb, 0x3fc,
    0x1ffa, 0x21, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
    0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72,
    0xfc, 0x73, 0xfd, 0x1ffb, 0x7fff0, 0x1ffc, 0x3ffc, 0x22,
    0x7ffd, 0x3, 0x23, 0x4, 0x24, 0x5, 0x25, 0x26,
    0x27, 0x6, 0x74, 0x75, 0x28, 0x29, 0x2a, 0x7,
    0x2b, 0x76, 0x2c, 0x8, 0x9, 0x2d, 0x77, 0x78,
    0x79, 0x7a, 0x7b, 0x7ffe, 0x7fc, 0x3ffd, 0x1ffd, 0xffffffc,
    0xfffe6, 0x3fffd2, 0xfffe7, 0xfffe8, 0x3fffd3, 0x3fffd4, 0x3fffd5, 0x7fffd9,
    0x3fffd6, 0x7fffda, 0x7fffdb, 0x7fffdc, 0x7fffdd, 0x7fffde, 0xffffeb, 0x7fffdf,
    0xffffec, 0xffffed, 0x3fffd7, 0x7fffe0, 0xffffee, 0x7fffe1, 0x7fffe2, 0x7fffe3,
    0x7fffe4, 0x1fffdc, 0x3fffd8, 0x7fffe5, 0x3fffd9, 0x7fffe6, 0x7fffe7, 0xffffef,
    0x3fffda, 0x1fffdd, 0xfffe9, 0x3fffdb, 0x3fffdc, 0x7fffe8, 0x7fffe9, 0x1fffde,
    0x7fffea, 0x3fffdd, 0x3fffde, 0xfffff0, 0x1fffdf, 0x3fffdf, 0x7fffeb, 0x7fffec,
    0x1fffe0, 0x1fffe1, 0x3fffe0, 0x1fffe2, 0x7fffed, 0x3fffe1, 0x7fffee, 0x7fffef,
    0xfffea, 0x3fffe2, 0x3fffe3, 0x3fffe4, 0x7ffff0, 0x3fffe5, 0x3fffe6, 0x7ffff1,
    0x3ffffe0, 0x3ffffe1, 0xfffeb, 0x7fff1, 0x3fffe7, 0x7ffff2, 0x3fffe8, 0x1ffffec,
    0x3ffffe2, 0x3ffffe3, 0x3ffffe4, 0x7ffffde, 0x7ffffdf, 0x3ffffe5, 0xfffff1, 0x1ffffed,
    0x7fff2, 0x1fffe3, 0x3ffffe6, 0x7ffffe0, 0x7ffffe1, 0x3ffffe7, 0x7ffffe2, 0xfffff2,
    0x1fffe4, 0x1fffe5, 0x3ffffe8, 0x3ffffe9, 0xffffffd, 0x7ffffe3, 0x7ffffe4, 0x7ffffe5,
    0xfffec, 0xfffff3, 0xfffed, 0x1fffe6, 0x3fffe9, 0x1fffe7, 0x1fffe8, 0x7ffff3,
    0x3fffea, 0x3fffeb, 0x1ffffee, 0x1ffffef, 0xfffff4, 0xfffff5, 0x3ffffea, 0x7ffff4,
    0x3ffffeb, 0x7ffffe6, 0x3ffffec, 0x3ffffed, 0x7ffffe7, 0x7ffffe8, 0x7ffffe9, 0x7ffffea,
    0x7ffffeb, 0xffffffe, 0x7ffffec, 0x7ffffed, 0x7ffffee, 0x7ffffef, 0x7fffff0, 0x3ffffee};

/*! HPACK Huffman code lengths in bits (RFC 7541 Appendix B) */
static const uint8_t hpack_huff_len[256] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28,
    28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    6, 10, 10, 12, 13, 6, 8, 11, 10, 10, 8, 11, 8, 6, 6, 6,
    5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 8, 15, 6, 12, 10,
    13, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 8, 7, 8, 13, 19, 13, 14, 6,
    15, 5, 6, 5, 6, 5, 6, 6, 6, 5, 7, 7, 6, 6, 6, 5,
    6, 7, 6, 5, 5, 6, 7, 7, 7, 7, 7, 15, 11, 14, 13, 28,
    20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
    24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24,
    22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22, 24, 21, 22, 23, 23,
    21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
    26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26, 27, 27, 26, 24, 25,
    19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27,
    20, 24, 20, 21, 22, 21, 21, 23, 22, 22, 25, 25, 24, 24, 26, 23,
    26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26};

/*! one HPACK dynamic table entry (owned strings and its accounted size) */
typedef struct H2_DYN_ENTRY {
    char* name;
    char* value;
    size_t size; /* name_len + value_len + 32 (RFC 7541 4.1) */
} H2_DYN_ENTRY;

/*! HPACK codec context: the dynamic table (index 0 = most recently added) */
struct N_H2_HPACK {
    H2_DYN_ENTRY* ents; /* dynamic array, position 0 = newest = table index 62 */
    size_t nents;
    size_t cap;
    size_t cur_size; /* sum of entry sizes */
    size_t max_size; /* current effective size cap (<= limit) */
    size_t limit;    /* the advertised SETTINGS_HEADER_TABLE_SIZE upper bound */
};

/*! @brief find the Huffman symbol for an accumulated code of nbits bits, or -1 */
static int hpack_huff_lookup(uint32_t code, int nbits) {
    int i;
    for (i = 0; i < 256; i++) {
        if ((int)hpack_huff_len[i] == nbits && hpack_huff_code[i] == code)
            return i;
    }
    return -1;
}

/*! @brief decode a Huffman-coded string of len bytes into a fresh C string */
static int hpack_huff_decode(const unsigned char* s, size_t len, char** out) {
    size_t cap = len * 2 + 16;
    char* r = malloc(cap);
    size_t rlen = 0;
    uint32_t code = 0;
    int nbits = 0;
    size_t i;
    if (!r)
        return -1;
    for (i = 0; i < len; i++) {
        int bit;
        for (bit = 7; bit >= 0; bit--) {
            int sym;
            code = (code << 1) | (uint32_t)((s[i] >> bit) & 1);
            nbits++;
            if (nbits > 30) {
                free(r);
                return -1;
            }
            sym = hpack_huff_lookup(code, nbits);
            if (sym >= 0) {
                if (rlen + 1 >= cap) {
                    char* nr;
                    cap = cap * 2 + 16;
                    nr = realloc(r, cap);
                    if (!nr) {
                        free(r);
                        return -1;
                    }
                    r = nr;
                }
                r[rlen++] = (char)sym;
                code = 0;
                nbits = 0;
            }
        }
    }
    /* the trailing padding must be fewer than 8 bits and all 1s (an EOS prefix) */
    if (nbits > 7) {
        free(r);
        return -1;
    }
    if (nbits > 0) {
        uint32_t mask = (uint32_t)((1u << nbits) - 1u);
        if ((code & mask) != mask) {
            free(r);
            return -1;
        }
    }
    r[rlen] = '\0';
    *out = r;
    return 0;
}

/*! @brief decode an HPACK integer with an N-bit prefix; advances *pos */
static int hpack_int_decode(const unsigned char* buf, size_t len, size_t* pos, int prefix_bits, uint64_t* out) {
    uint64_t max_prefix = (uint64_t)((1u << prefix_bits) - 1u);
    uint64_t val;
    unsigned m = 0;
    if (*pos >= len)
        return -1;
    val = (uint64_t)buf[*pos] & max_prefix;
    (*pos)++;
    if (val < max_prefix) {
        *out = val;
        return 0;
    }
    for (;;) {
        unsigned char b;
        if (*pos >= len)
            return -1;
        b = buf[*pos];
        (*pos)++;
        val += (uint64_t)(b & 0x7f) << m;
        m += 7;
        if (m > 63)
            return -1; /* overflow guard */
        if (!(b & 0x80))
            break;
    }
    *out = val;
    return 0;
}

/*! @brief decode an HPACK string (Huffman or literal); allocates *out */
static int hpack_str_decode(const unsigned char* buf, size_t len, size_t* pos, char** out) {
    int huff;
    uint64_t slen;
    const unsigned char* s;
    if (*pos >= len)
        return -1;
    huff = (buf[*pos] & 0x80) != 0;
    if (hpack_int_decode(buf, len, pos, 7, &slen) != 0)
        return -1;
    if (slen > len || *pos + (size_t)slen > len)
        return -1;
    s = buf + *pos;
    *pos += (size_t)slen;
    if (huff)
        return hpack_huff_decode(s, (size_t)slen, out);
    {
        char* r = malloc((size_t)slen + 1);
        if (!r)
            return -1;
        if (slen > 0)
            memcpy(r, s, (size_t)slen);
        r[slen] = '\0';
        *out = r;
        return 0;
    }
}

/*! @brief encode an HPACK integer with an N-bit prefix and high-bit flags */
static size_t hpack_int_encode(unsigned char* out, size_t cap, int prefix_bits, unsigned char flags, uint64_t value) {
    unsigned char max_prefix = (unsigned char)((1u << prefix_bits) - 1u);
    size_t n = 0;
    if (value < max_prefix) {
        if (cap < 1)
            return 0;
        out[0] = (unsigned char)(flags | (unsigned char)value);
        return 1;
    }
    if (cap < 1)
        return 0;
    out[n++] = (unsigned char)(flags | max_prefix);
    value -= max_prefix;
    while (value >= 128) {
        if (n >= cap)
            return 0;
        out[n++] = (unsigned char)((value & 0x7f) | 0x80);
        value >>= 7;
    }
    if (n >= cap)
        return 0;
    out[n++] = (unsigned char)value;
    return n;
}

/*! @brief encode a literal (non-Huffman) string with its 7-bit length prefix */
static size_t hpack_str_encode(unsigned char* out, size_t cap, const char* s) {
    size_t slen = s ? strlen(s) : 0;
    size_t n = hpack_int_encode(out, cap, 7, 0x00, (uint64_t)slen);
    if (n == 0)
        return 0;
    if (n + slen > cap)
        return 0;
    if (slen > 0)
        memcpy(out + n, s, slen);
    return n + slen;
}

N_H2_HPACK* n_http2_hpack_new(size_t max_table_size) {
    N_H2_HPACK* ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    if (max_table_size == 0)
        max_table_size = N_H2_HPACK_DEFAULT_TABLE_SIZE;
    ctx->max_size = max_table_size;
    ctx->limit = max_table_size;
    return ctx;
}

/*! @brief drop the oldest dynamic entries until the table fits max_size */
static void hpack_evict(N_H2_HPACK* ctx) {
    while (ctx->cur_size > ctx->max_size && ctx->nents > 0) {
        H2_DYN_ENTRY* e = &ctx->ents[ctx->nents - 1];
        ctx->cur_size -= e->size;
        free(e->name);
        free(e->value);
        ctx->nents--;
    }
}

void n_http2_hpack_free(N_H2_HPACK** pctx) {
    N_H2_HPACK* ctx;
    if (!pctx || !*pctx)
        return;
    ctx = *pctx;
    while (ctx->nents > 0) {
        ctx->nents--;
        free(ctx->ents[ctx->nents].name);
        free(ctx->ents[ctx->nents].value);
    }
    free(ctx->ents);
    free(ctx);
    *pctx = NULL;
}

void n_http2_hpack_set_max_size(N_H2_HPACK* ctx, size_t max_table_size) {
    if (!ctx)
        return;
    ctx->limit = max_table_size;
    if (ctx->max_size > max_table_size)
        ctx->max_size = max_table_size;
    hpack_evict(ctx);
}

/*! @brief insert (name, value) at the front of the dynamic table, evicting as
 *  needed; both strings are copied. An entry larger than max_size empties the
 *  table without being stored (RFC 7541 4.4). */
static int hpack_dyn_add(N_H2_HPACK* ctx, const char* name, const char* value) {
    size_t esize = strlen(name) + strlen(value) + 32;
    H2_DYN_ENTRY e;
    if (esize > ctx->max_size) {
        /* evict everything; the entry is not added */
        while (ctx->nents > 0) {
            ctx->nents--;
            free(ctx->ents[ctx->nents].name);
            free(ctx->ents[ctx->nents].value);
        }
        ctx->cur_size = 0;
        return 0;
    }
    ctx->cur_size += esize;
    hpack_evict(ctx);
    if (ctx->nents + 1 > ctx->cap) {
        size_t nc = ctx->cap ? ctx->cap * 2 : 8;
        H2_DYN_ENTRY* ne = realloc(ctx->ents, nc * sizeof(*ne));
        if (!ne) {
            ctx->cur_size -= esize;
            return -1;
        }
        ctx->ents = ne;
        ctx->cap = nc;
    }
    e.name = strdup(name);
    e.value = strdup(value);
    e.size = esize;
    if (!e.name || !e.value) {
        free(e.name);
        free(e.value);
        ctx->cur_size -= esize;
        return -1;
    }
    memmove(&ctx->ents[1], &ctx->ents[0], ctx->nents * sizeof(ctx->ents[0]));
    ctx->ents[0] = e;
    ctx->nents++;
    return 0;
}

/*! @brief resolve a table index to its name/value (const, not owned). Returns 0
 *  on success, -1 when the index is out of range. */
static int hpack_lookup(const N_H2_HPACK* ctx, uint64_t idx, const char** name, const char** value) {
    if (idx == 0)
        return -1;
    if (idx <= 61) {
        *name = hpack_static_name[idx - 1];
        *value = hpack_static_value[idx - 1];
        return 0;
    }
    idx -= 62;
    if (idx >= ctx->nents)
        return -1;
    *name = ctx->ents[idx].name;
    *value = ctx->ents[idx].value;
    return 0;
}

void n_http2_hpack_headers_free(N_H2_HEADER* headers, size_t n) {
    size_t i;
    if (!headers)
        return;
    for (i = 0; i < n; i++) {
        free(headers[i].name);
        free(headers[i].value);
        headers[i].name = NULL;
        headers[i].value = NULL;
    }
}

int n_http2_hpack_decode(N_H2_HPACK* ctx, const unsigned char* block, size_t len, N_H2_HEADER* out, size_t max_out, size_t* count) {
    size_t pos = 0, n = 0;
    if (count)
        *count = 0;
    if (!ctx || !out || (!block && len > 0))
        return -1;
    while (pos < len) {
        unsigned char b = block[pos];
        char* name = NULL;
        char* value = NULL;
        if (b & 0x80) {
            /* indexed header field (6.1) */
            uint64_t idx;
            const char *cn, *cv;
            if (hpack_int_decode(block, len, &pos, 7, &idx) != 0)
                goto fail;
            if (hpack_lookup(ctx, idx, &cn, &cv) != 0)
                goto fail;
            name = strdup(cn);
            value = strdup(cv);
        } else if (b & 0x40) {
            /* literal with incremental indexing (6.2.1) */
            uint64_t idx;
            if (hpack_int_decode(block, len, &pos, 6, &idx) != 0)
                goto fail;
            if (idx != 0) {
                const char *cn, *cv;
                if (hpack_lookup(ctx, idx, &cn, &cv) != 0)
                    goto fail;
                name = strdup(cn);
            } else if (hpack_str_decode(block, len, &pos, &name) != 0) {
                goto fail;
            }
            if (hpack_str_decode(block, len, &pos, &value) != 0)
                goto fail;
            if (name && value)
                hpack_dyn_add(ctx, name, value);
        } else if (b & 0x20) {
            /* dynamic table size update (6.3) */
            uint64_t newsize;
            if (hpack_int_decode(block, len, &pos, 5, &newsize) != 0)
                goto fail;
            if (newsize > ctx->limit)
                goto fail;
            ctx->max_size = (size_t)newsize;
            hpack_evict(ctx);
            continue; /* no header field produced */
        } else {
            /* literal without indexing (6.2.2) or never indexed (6.2.3), 4-bit prefix */
            uint64_t idx;
            if (hpack_int_decode(block, len, &pos, 4, &idx) != 0)
                goto fail;
            if (idx != 0) {
                const char *cn, *cv;
                if (hpack_lookup(ctx, idx, &cn, &cv) != 0)
                    goto fail;
                name = strdup(cn);
            } else if (hpack_str_decode(block, len, &pos, &name) != 0) {
                goto fail;
            }
            if (hpack_str_decode(block, len, &pos, &value) != 0)
                goto fail;
        }
        if (!name || !value)
            goto fail;
        if (n >= max_out)
            goto fail; /* fail: frees the current name/value */
        out[n].name = name;
        out[n].value = value;
        n++;
        continue;
    fail:
        free(name);
        free(value);
        n_http2_hpack_headers_free(out, n);
        return -1;
    }
    if (count)
        *count = n;
    return 0;
}

size_t n_http2_hpack_encode(const N_H2_HEADER* headers, size_t n, unsigned char* out, size_t out_cap) {
    size_t pos = 0, i;
    if (!headers || !out)
        return 0;
    for (i = 0; i < n; i++) {
        size_t w;
        if (!headers[i].name)
            return 0;
        if (pos + 1 > out_cap)
            return 0;
        /* literal without indexing, new name: first byte 0x00 (4-bit index = 0) */
        out[pos++] = 0x00;
        w = hpack_str_encode(out + pos, out_cap - pos, headers[i].name);
        if (w == 0)
            return 0;
        pos += w;
        w = hpack_str_encode(out + pos, out_cap - pos, headers[i].value ? headers[i].value : "");
        if (w == 0)
            return 0;
        pos += w;
    }
    return pos;
}
