/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@file n_clock_sync.h
 *@brief Clock synchronization estimator for networked games
 *
 * Estimates the time offset between a local clock and a remote clock
 * using periodic sync request/response pairs.  The offset is computed
 * as the median of recent samples to reject outliers caused by
 * network jitter.
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
 *@version 1.0
 *@date 12/03/2026
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

    /*! add to local time to get estimated server time */
    double estimated_offset;
    /*! current estimated round-trip time */
    double estimated_rtt;

    /*! local time of last sync request sent */
    double last_sync_time;

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

/*! get estimated server time given a local time value */
double n_clock_sync_server_time(N_CLOCK_SYNC* cs, double local_now);

/*! check if it's time to send a new sync request (returns TRUE/FALSE) */
int n_clock_sync_should_send(N_CLOCK_SYNC* cs, double local_now);

/*! mark that a sync request was just sent */
void n_clock_sync_mark_sent(N_CLOCK_SYNC* cs, double local_now);

/**
  @}
  */

#ifdef __cplusplus
}
#endif
#endif /* __N_CLOCK_SYNC_HEADER */
