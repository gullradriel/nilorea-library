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
 *@example ex_network_connect_abort.c
 *@brief Interrupt an in-progress connect with netw_set_connect_abort_cb.
 *
 * A connect to an unresponsive host (a dropped SYN) otherwise blocks until the
 * connect timeout. Registering a connect-abort callback makes the connect wait
 * wake every NETW_CONNECT_ABORT_POLL_MS to poll it, so returning non-zero from
 * the callback aborts the attempt at once. This example dials a TEST-NET-1
 * (RFC 5737, 192.0.2.0/24, not routed) address, which is guaranteed to have no
 * listener, with a long connect timeout and a callback that asks to abort: the
 * connect returns promptly rather than after the timeout.
 *@author Castagnier Mickael
 *@version 1.0
 *@date 19/07/2026
 */

#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_time.h"

/*! Blackhole target: RFC 5737 documentation range, never has a listener. */
#define BLACKHOLE_HOST "192.0.2.1"
/*! Blackhole port (any). */
#define BLACKHOLE_PORT "80"
/*! Long connect budget so an un-aborted attempt would clearly out-last an aborted one. */
#define LONG_CONNECT_TIMEOUT_MS 20000
/*! An aborted connect must return in well under the connect budget. */
#define ABORT_MAX_MS 3000

/*! Count of times the abort callback was polled (proves it ran). */
static int g_polls = 0;

/*! Connect-abort callback that always asks to abort, counting its polls. */
static int abort_now(void* ctx) {
    (void)ctx;
    g_polls++;
    return 1;
}

int main(void) {
    set_log_level(LOG_INFO);

    int fails = 0;

    /* 1) With an abort callback registered, a connect to an unresponsive host
       returns quickly instead of blocking for the whole connect timeout. */
    netw_set_connect_abort_cb(abort_now, NULL);

    NETWORK* netw = NULL;
    N_TIME timer;
    start_HiTimer(&timer);
    int rc = netw_connect_to(&netw, (char*)BLACKHOLE_HOST, (char*)BLACKHOLE_PORT, NETWORK_IPV4, LONG_CONNECT_TIMEOUT_MS);
    time_t elapsed_us = get_usec(&timer);
    long elapsed_ms = (long)(elapsed_us / 1000);

    n_log(LOG_INFO, "aborted connect: rc=%d elapsed=%ldms polls=%d", rc, elapsed_ms, g_polls);
    if (rc != FALSE) {
        n_log(LOG_ERR, "FAIL: connect to %s should not succeed", BLACKHOLE_HOST);
        fails++;
    }
    if (elapsed_ms >= ABORT_MAX_MS) {
        n_log(LOG_ERR, "FAIL: aborted connect took %ldms, expected < %dms", elapsed_ms, ABORT_MAX_MS);
        fails++;
    }
    if (g_polls == 0) {
        n_log(LOG_ERR, "FAIL: abort callback was never polled");
        fails++;
    }
    if (netw)
        netw_close(&netw);

    /* 2) Removing the callback restores the default: connect blocks up to its
       (here deliberately short) timeout and then fails. Kept short so the
       example stays fast; only the return value is asserted, so an environment
       that fast-fails an unroutable address does not turn this into a flake. */
    netw_set_connect_abort_cb(NULL, NULL);
    netw = NULL;
    g_polls = 0;
    start_HiTimer(&timer);
    rc = netw_connect_to(&netw, (char*)BLACKHOLE_HOST, (char*)BLACKHOLE_PORT, NETWORK_IPV4, 800);
    elapsed_ms = (long)(get_usec(&timer) / 1000);
    n_log(LOG_INFO, "no-callback connect: rc=%d elapsed=%ldms polls=%d", rc, elapsed_ms, g_polls);
    if (rc != FALSE) {
        n_log(LOG_ERR, "FAIL: connect to %s should not succeed", BLACKHOLE_HOST);
        fails++;
    }
    if (g_polls != 0) {
        n_log(LOG_ERR, "FAIL: callback polled after being removed");
        fails++;
    }
    if (netw)
        netw_close(&netw);

    if (fails == 0)
        n_log(LOG_INFO, "ex_network_connect_abort: all checks passed");
    else
        n_log(LOG_ERR, "ex_network_connect_abort: %d check(s) FAILED", fails);
    return fails ? 1 : 0;
}
