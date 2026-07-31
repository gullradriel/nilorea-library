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
 *@file n_clock_sync.h
 *@brief Clock synchronization estimator for networked games
 *
 * Estimates the time offset between a local clock and a remote clock
 * using periodic sync request/response pairs. The offset adopted is the
 * sample with the lowest RTT (NTP-style — smallest one-way uncertainty);
 * absurd-RTT responses are rejected; consumers read the offset through
 * n_clock_sync_server_time, which slews across estimate changes instead
 * of stepping. Request cadence bursts until the estimator is warm.
 *
 * Usage:
 * @code
 *   N_CLOCK_SYNC *cs = n_clock_sync_new();
 *
 *   // Periodically:
 *   if (n_clock_sync_should_send(cs, local_now)) {
 *       n_clock_sync_mark_sent(cs, local_now);
 *       send_sync_request(local_now);  // include local_now in request
 *   }
 *
 *   // On response:
 *   n_clock_sync_process_response(cs, client_send_time, server_time, local_now);
 *
 *   // Convert local time to estimated server time:
 *   double srv_time = n_clock_sync_server_time(cs, local_now);
 *
 *   n_clock_sync_delete(&cs);
 * @endcode
 *
 *@author Castagnier Mickael
 *@version 1.1
 *@date 19/07/2026
 */

#ifndef __N_CLOCK_SYNC_HEADER
#define __N_CLOCK_SYNC_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup CLOCK_SYNC CLOCK_SYNC: client/server clock offset estimation
  @addtogroup CLOCK_SYNC
  @{
  */

#include "n_common.h"

/*! number of samples for the median filter */
#ifndef N_CLOCK_SYNC_SAMPLE_COUNT
#define N_CLOCK_SYNC_SAMPLE_COUNT 11
#endif

/*! default interval between sync requests in seconds */
#ifndef N_CLOCK_SYNC_INTERVAL
#define N_CLOCK_SYNC_INTERVAL 3.0
#endif

/*! accelerated interval used until N_CLOCK_SYNC_BURST_SAMPLES real
 *  responses have been collected — a fresh estimator converges in a
 *  couple of seconds instead of minutes */
#ifndef N_CLOCK_SYNC_BURST_INTERVAL
#define N_CLOCK_SYNC_BURST_INTERVAL 0.5
#endif

/*! number of REAL samples below which the burst interval applies */
#ifndef N_CLOCK_SYNC_BURST_SAMPLES
#define N_CLOCK_SYNC_BURST_SAMPLES 5
#endif

/*! responses with an RTT above this are rejected outright (retransmit
 *  storms / suspend-resume artefacts — not usable timing data) */
#ifndef N_CLOCK_SYNC_MAX_RTT
#define N_CLOCK_SYNC_MAX_RTT 10.0
#endif

/*! seconds over which n_clock_sync_server_time slews from the previous
 *  offset estimate to a new one (smoothstep). A consumer that anchors
 *  motion on this clock sees a bounded rate change instead of a step. */
#ifndef N_CLOCK_SYNC_SLEW_SECONDS
#define N_CLOCK_SYNC_SLEW_SECONDS 1.0
#endif

/*! clock synchronization estimator */
typedef struct N_CLOCK_SYNC {
    /*! circular buffer of offset estimates */
    double offset_samples[N_CLOCK_SYNC_SAMPLE_COUNT];
    /*! circular buffer of RTT values */
    double rtt_samples[N_CLOCK_SYNC_SAMPLE_COUNT];
    /*! current write position in circular buffer */
    int sample_index;
    /*! number of samples collected so far */
    int sample_count;

    /*! add to local time to get estimated server time (raw best-RTT
     *  target — n_clock_sync_server_time applies the slew on top) */
    double estimated_offset;
    /*! current estimated round-trip time */
    double estimated_rtt;

    /*! local time of last sync request sent */
    double last_sync_time;

    /*! TRUE while the buffer holds only a synthetic n_clock_sync_seed
     *  sample — flushed wholesale by the first real response so a
     *  low-synthetic-rtt seed can never pin the estimate on links whose
     *  real RTT exceeds the synthetic one */
    int synthetic;

    /*! effective offset at the moment estimated_offset last changed —
     *  the slew blends offset_prev → estimated_offset */
    double offset_prev;
    /*! local time at which estimated_offset last changed */
    double offset_change_time;

} N_CLOCK_SYNC;

/*! allocate and initialize a new clock sync estimator */
N_CLOCK_SYNC* n_clock_sync_new(void);

/*! free a clock sync estimator */
void n_clock_sync_delete(N_CLOCK_SYNC** cs);

/*! record a sync response: client_send_time is the local time the request was sent,
 *  server_time is the server's timestamp from the response,
 *  local_now is the current local time when the response was received.
 *  Updates estimated_offset and estimated_rtt.
 *  Returns TRUE on success, FALSE on error. */
int n_clock_sync_process_response(N_CLOCK_SYNC* cs, double client_send_time, double server_time, double local_now);

/*! Inject a synthetic (offset, rtt) sample, e.g. a server-supplied
 *  clock value at login, so the estimator is usable BEFORE the first
 *  request/response round-trip completes. Pass the believed offset
 *  (server_time - local_now) and a conservative synthetic rtt.
 *  RESETS the estimator to this single sample and adopts the offset
 *  immediately (no slew — at login there is nothing to slew from).
 *  The FIRST real response flushes the seed wholesale, so a seed with
 *  a synthetic rtt lower than the link's real RTT can never pin the
 *  estimate (offset selection is best-rtt, NTP-style). */
void n_clock_sync_seed(N_CLOCK_SYNC* cs, double offset, double rtt);

/*! get estimated server time given a local time value. When the
 *  underlying offset estimate changes, the returned time SLEWS from
 *  the previous estimate to the new one over N_CLOCK_SYNC_SLEW_SECONDS
 *  (smoothstep) instead of stepping — pure function of the stored
 *  state, safe to call at any rate. */
double n_clock_sync_server_time(const N_CLOCK_SYNC* cs, double local_now);

/*! check if it's time to send a new sync request (returns TRUE/FALSE).
 *  Uses N_CLOCK_SYNC_BURST_INTERVAL until N_CLOCK_SYNC_BURST_SAMPLES
 *  real responses have been collected, then N_CLOCK_SYNC_INTERVAL. */
int n_clock_sync_should_send(const N_CLOCK_SYNC* cs, double local_now);

/*! mark that a sync request was just sent */
void n_clock_sync_mark_sent(N_CLOCK_SYNC* cs, double local_now);

/**
  @}
  */

#ifdef __cplusplus
}
#endif
#endif /* __N_CLOCK_SYNC_HEADER */
