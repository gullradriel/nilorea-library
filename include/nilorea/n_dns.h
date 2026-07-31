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
 *@file n_dns.h
 *@brief Minimal DNS message helpers: parse a query question and build an A-record response
 *@author Castagnier Mickael
 *@version 1.0
 */

#ifndef __NILOREA_DNS_HEADER
#define __NILOREA_DNS_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**@defgroup N_DNS DNS: minimal query parse and A-record response
  @addtogroup N_DNS
  @{
  */

/*! Maximum decoded QNAME length, including the terminating NUL. */
#define N_DNS_QNAME_MAX 256
/*! DNS resource-record type A (IPv4 address). */
#define N_DNS_TYPE_A 1

/**
 *@brief Parse the header id and the first question (QNAME + QTYPE) of a DNS message.
 *
 * The QNAME is written as a lowercase dot-separated string (no trailing dot).
 * Name compression is rejected in the question (a query has none). Any out
 * pointer may be NULL to skip it.
 *
 *@param msg The DNS message bytes. NULL returns -1.
 *@param len The message length in bytes.
 *@param id_out Out: the 16-bit transaction id, or NULL.
 *@param qname_out Out: the decoded question name, or NULL.
 *@param qname_sz Size of qname_out.
 *@param qtype_out Out: the question type (QTYPE), or NULL.
 *@return 0 on success, -1 on a malformed or truncated message.
 */
int n_dns_parse_query(const unsigned char* msg, size_t len, uint16_t* id_out, char* qname_out, size_t qname_sz, uint16_t* qtype_out);

/**
 *@brief Build a DNS response answering a query's first question with one A record.
 *
 * Echoes the query header (with QR/AA/RA set and RCODE 0) and question, then
 * appends a single A resource record pointing at @p ipv4_be. The address is taken
 * in network byte order (as returned by inet_addr) and copied verbatim into the
 * RDATA, so the helper is endianness-agnostic.
 *
 *@param query The received query bytes. NULL returns -1.
 *@param qlen The query length in bytes.
 *@param ipv4_be The answer IPv4 address, in network byte order.
 *@param ttl The record time-to-live in seconds.
 *@param out Output buffer for the response. NULL returns -1.
 *@param outsz Size of @p out.
 *@return The response length in bytes, or -1 on a malformed query or too-small buffer.
 */
int n_dns_build_a_response(const unsigned char* query, size_t qlen, uint32_t ipv4_be, uint32_t ttl, unsigned char* out, size_t outsz);

/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_DNS_HEADER */
