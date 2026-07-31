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
 *@example ex_dns.c
 *@brief n_dns regression: parse a DNS query question and build an A-record response.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_dns.h"
#include "nilorea/n_log.h"

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
                fprintf(stderr, "ex_dns\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

/* Build a minimal DNS query (header + one question) for @p name / @p qtype. */
static size_t build_query(const char* name, uint16_t qtype, uint16_t id, unsigned char* out) {
    size_t pos = 0;
    const char* p = name;
    out[pos++] = (unsigned char)(id >> 8);
    out[pos++] = (unsigned char)(id & 0xFF);
    out[pos++] = 0x01;
    out[pos++] = 0x00; /* flags: RD set */
    out[pos++] = 0x00;
    out[pos++] = 0x01; /* QDCOUNT = 1 */
    out[pos++] = 0x00;
    out[pos++] = 0x00; /* ANCOUNT */
    out[pos++] = 0x00;
    out[pos++] = 0x00; /* NSCOUNT */
    out[pos++] = 0x00;
    out[pos++] = 0x00; /* ARCOUNT */
    while (*p) {
        const char* dot = strchr(p, '.');
        size_t llen = dot ? (size_t)(dot - p) : strlen(p);
        out[pos++] = (unsigned char)llen;
        memcpy(out + pos, p, llen);
        pos += llen;
        if (!dot)
            break;
        p = dot + 1;
    }
    out[pos++] = 0x00; /* root label */
    out[pos++] = (unsigned char)(qtype >> 8);
    out[pos++] = (unsigned char)(qtype & 0xFF);
    out[pos++] = 0x00;
    out[pos++] = 0x01; /* QCLASS = IN */
    return pos;
}

static void test_parse(void) {
    unsigned char q[512];
    size_t qlen;
    uint16_t id = 0;
    uint16_t qtype = 0;
    char name[N_DNS_QNAME_MAX];

    qlen = build_query("abc123.oast.example.com", N_DNS_TYPE_A, 0x1234, q);
    if (n_dns_parse_query(q, qlen, &id, name, sizeof(name), &qtype) != 0) {
        n_log(LOG_ERR, "parse of a valid query failed");
        failures++;
        return;
    }
    if (id != 0x1234) {
        n_log(LOG_ERR, "parse id: got %u expected 0x1234", (unsigned)id);
        failures++;
    }
    if (strcmp(name, "abc123.oast.example.com") != 0) {
        n_log(LOG_ERR, "parse qname: got '%s'", name);
        failures++;
    }
    if (qtype != N_DNS_TYPE_A) {
        n_log(LOG_ERR, "parse qtype: got %u", (unsigned)qtype);
        failures++;
    }

    /* names are lowercased for case-insensitive matching */
    qlen = build_query("Token.OAST.Test", N_DNS_TYPE_A, 1, q);
    if (n_dns_parse_query(q, qlen, NULL, name, sizeof(name), NULL) != 0 || strcmp(name, "token.oast.test") != 0) {
        n_log(LOG_ERR, "parse lowercasing failed: '%s'", name);
        failures++;
    }
}

static void test_build_response(void) {
    unsigned char q[512];
    unsigned char resp[512];
    size_t qlen;
    int rlen;
    unsigned char ip[4] = {127, 0, 0, 1};
    uint32_t ipv4_be;
    memcpy(&ipv4_be, ip, 4);

    qlen = build_query("tok.oast.test", N_DNS_TYPE_A, 0xBEEF, q);
    rlen = n_dns_build_a_response(q, qlen, ipv4_be, 60, resp, sizeof(resp));
    if (rlen <= 0) {
        n_log(LOG_ERR, "build response failed (%d)", rlen);
        failures++;
        return;
    }
    /* response length = question bytes + 16-byte A answer */
    if ((size_t)rlen != qlen + 16) {
        n_log(LOG_ERR, "response length: got %d expected %zu", rlen, qlen + 16);
        failures++;
    }
    /* QR bit set, id echoed, ANCOUNT = 1 */
    if ((resp[2] & 0x80) == 0) {
        n_log(LOG_ERR, "response QR bit not set");
        failures++;
    }
    if (resp[0] != 0xBE || resp[1] != 0xEF) {
        n_log(LOG_ERR, "response id not echoed");
        failures++;
    }
    if (resp[6] != 0x00 || resp[7] != 0x01) {
        n_log(LOG_ERR, "response ANCOUNT != 1");
        failures++;
    }
    /* the answer starts at qlen: a compression pointer to the question */
    if (resp[qlen] != 0xC0 || resp[qlen + 1] != 0x0C) {
        n_log(LOG_ERR, "answer name is not a pointer to the question");
        failures++;
    }
    /* the trailing 4 RDATA bytes are the IP in order */
    if (resp[rlen - 4] != 127 || resp[rlen - 3] != 0 || resp[rlen - 2] != 0 || resp[rlen - 1] != 1) {
        n_log(LOG_ERR, "answer RDATA is not 127.0.0.1");
        failures++;
    }

    /* the built response parses back to the same question */
    {
        char name[N_DNS_QNAME_MAX];
        if (n_dns_parse_query(resp, (size_t)rlen, NULL, name, sizeof(name), NULL) != 0 || strcmp(name, "tok.oast.test") != 0) {
            n_log(LOG_ERR, "response question does not round-trip: '%s'", name);
            failures++;
        }
    }
}

static void test_errors(void) {
    unsigned char q[512];
    unsigned char resp[512];
    size_t qlen;
    unsigned char ip[4] = {10, 0, 0, 1};
    uint32_t ipv4_be;
    char name[N_DNS_QNAME_MAX];
    memcpy(&ipv4_be, ip, 4);

    /* NULL / too-short messages are rejected */
    if (n_dns_parse_query(NULL, 12, NULL, name, sizeof(name), NULL) != -1) {
        n_log(LOG_ERR, "parse NULL should fail");
        failures++;
    }
    if (n_dns_parse_query(q, 5, NULL, name, sizeof(name), NULL) != -1) {
        n_log(LOG_ERR, "parse too-short should fail");
        failures++;
    }

    /* a compression pointer in the question is rejected */
    qlen = build_query("x.test", N_DNS_TYPE_A, 1, q);
    q[12] = 0xC0; /* corrupt the first label length into a pointer */
    if (n_dns_parse_query(q, qlen, NULL, name, sizeof(name), NULL) != -1) {
        n_log(LOG_ERR, "parse with compression should fail");
        failures++;
    }

    /* a too-small output buffer is rejected */
    qlen = build_query("x.test", N_DNS_TYPE_A, 1, q);
    if (n_dns_build_a_response(q, qlen, ipv4_be, 60, resp, 8) != -1) {
        n_log(LOG_ERR, "build with tiny buffer should fail");
        failures++;
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    test_parse();
    test_build_response();
    test_errors();

    if (failures) {
        n_log(LOG_ERR, "ex_dns: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_dns: all checks passed");
    return 0;
}
