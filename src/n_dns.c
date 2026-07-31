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
 *@file n_dns.c
 *@brief Minimal DNS message helpers implementation.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_dns.h"

#include <string.h>

/*! DNS header size in bytes (id, flags, and the four section counts). */
#define N_DNS_HEADER_LEN 12

/* Read a big-endian 16-bit value at msg[off]. */
static uint16_t n_dns_u16(const unsigned char* msg, size_t off) {
    return (uint16_t)(((unsigned)msg[off] << 8) | (unsigned)msg[off + 1]);
}

int n_dns_parse_query(const unsigned char* msg, size_t len, uint16_t* id_out, char* qname_out, size_t qname_sz, uint16_t* qtype_out) {
    size_t off = N_DNS_HEADER_LEN;
    size_t out = 0;
    if (!msg || len < N_DNS_HEADER_LEN)
        return -1;
    if (n_dns_u16(msg, 4) < 1) /* QDCOUNT */
        return -1;
    if (id_out)
        *id_out = n_dns_u16(msg, 0);

    /* decode the first QNAME (a sequence of length-prefixed labels ending in 0) */
    for (;;) {
        unsigned label_len;
        if (off >= len)
            return -1;
        label_len = msg[off];
        if (label_len == 0) {
            off++;
            break;
        }
        if ((label_len & 0xC0u) != 0) /* compression is not valid in a question */
            return -1;
        off++;
        if (off + label_len > len)
            return -1;
        if (qname_out) {
            if (out != 0) {
                if (out + 1 >= qname_sz)
                    return -1;
                qname_out[out++] = '.';
            }
            for (unsigned i = 0; i < label_len; i++) {
                unsigned char c = msg[off + i];
                if (out + 1 >= qname_sz)
                    return -1;
                if (c >= 'A' && c <= 'Z')
                    c = (unsigned char)(c + 32); /* lowercase for case-insensitive matching */
                qname_out[out++] = (char)c;
            }
        }
        off += label_len;
    }
    if (qname_out) {
        if (out >= qname_sz)
            return -1;
        qname_out[out] = '\0';
    }

    if (off + 4 > len) /* QTYPE + QCLASS */
        return -1;
    if (qtype_out)
        *qtype_out = n_dns_u16(msg, off);
    return 0;
}

int n_dns_build_a_response(const unsigned char* query, size_t qlen, uint32_t ipv4_be, uint32_t ttl, unsigned char* out, size_t outsz) {
    size_t qend = N_DNS_HEADER_LEN;
    size_t pos;
    uint16_t qflags;
    uint16_t rflags;
    if (!query || qlen < N_DNS_HEADER_LEN || !out)
        return -1;

    /* walk the first question's QNAME to find where the question section ends */
    for (;;) {
        unsigned label_len;
        if (qend >= qlen)
            return -1;
        label_len = query[qend];
        if (label_len == 0) {
            qend++;
            break;
        }
        if ((label_len & 0xC0u) != 0)
            return -1;
        qend += (size_t)label_len + 1;
    }
    qend += 4; /* QTYPE + QCLASS */
    if (qend > qlen)
        return -1;
    if (outsz < qend + 16) /* the question plus a 16-byte A answer */
        return -1;

    /* copy the header and question verbatim, then rewrite the header */
    memcpy(out, query, qend);
    qflags = n_dns_u16(query, 2);
    /* QR=1, echo opcode + RD, set AA=1 and RA=1, RCODE=0 */
    rflags = (uint16_t)(0x8000u | (qflags & 0x7800u) | 0x0400u | (qflags & 0x0100u) | 0x0080u);
    out[2] = (unsigned char)(rflags >> 8);
    out[3] = (unsigned char)(rflags & 0xFFu);
    out[4] = 0;
    out[5] = 1; /* QDCOUNT = 1 */
    out[6] = 0;
    out[7] = 1; /* ANCOUNT = 1 */
    out[8] = 0;
    out[9] = 0; /* NSCOUNT = 0 */
    out[10] = 0;
    out[11] = 0; /* ARCOUNT = 0 */

    /* answer resource record: name -> pointer to the question at offset 12 */
    pos = qend;
    out[pos++] = 0xC0;
    out[pos++] = 0x0C;
    out[pos++] = 0x00;
    out[pos++] = 0x01; /* TYPE  = A */
    out[pos++] = 0x00;
    out[pos++] = 0x01; /* CLASS = IN */
    out[pos++] = (unsigned char)((ttl >> 24) & 0xFFu);
    out[pos++] = (unsigned char)((ttl >> 16) & 0xFFu);
    out[pos++] = (unsigned char)((ttl >> 8) & 0xFFu);
    out[pos++] = (unsigned char)(ttl & 0xFFu);
    out[pos++] = 0x00;
    out[pos++] = 0x04;              /* RDLENGTH = 4 */
    memcpy(out + pos, &ipv4_be, 4); /* RDATA: the address in network byte order */
    pos += 4;

    return (int)pos;
}
