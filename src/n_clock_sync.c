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
 *@file n_clock_sync.c
 *@brief Clock synchronization estimator implementation
 *@author Castagnier Mickael
 *@version 1.0
 *@date 12/03/2026
 */

#include "nilorea/n_clock_sync.h"
#include "nilorea/n_log.h"
#include "nilorea/n_common.h"
#include <string.h>
#include <stdlib.h>

/**@defgroup CLOCK_SYNC CLOCK_SYNC: client/server clock offset estimation
  @addtogroup CLOCK_SYNC
  @{
  */

/*! comparison function for qsort on doubles */
static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/*! compute the median of an array of doubles */
static double median_of(const double* samples, int count) {
    if (count <= 0) return 0.0;

    double tmp[N_CLOCK_SYNC_SAMPLE_COUNT];
    int n = (count < N_CLOCK_SYNC_SAMPLE_COUNT) ? count : N_CLOCK_SYNC_SAMPLE_COUNT;
    memcpy(tmp, samples, (size_t)n * sizeof(double));
    qsort(tmp, (size_t)n, sizeof(double), cmp_double);

    if (n % 2 == 1) {
        return tmp[n / 2];
    }
    return (tmp[n / 2 - 1] + tmp[n / 2]) / 2.0;
}

N_CLOCK_SYNC* n_clock_sync_new(void) {
    N_CLOCK_SYNC* cs = NULL;
    Malloc(cs, N_CLOCK_SYNC, 1);
    __n_assert(cs, return NULL);

    memset(cs, 0, sizeof(N_CLOCK_SYNC));
    cs->sample_index = 0;
    cs->sample_count = 0;
    cs->estimated_offset = 0.0;
    cs->estimated_rtt = 0.0;
    /* Set last_sync_time far enough in the past so first should_send returns TRUE */
    cs->last_sync_time = -N_CLOCK_SYNC_INTERVAL;

    return cs;
}

void n_clock_sync_delete(N_CLOCK_SYNC** cs) {
    __n_assert(cs, return);
    __n_assert(*cs, return);
    Free(*cs);
    *cs = NULL;
}

int n_clock_sync_process_response(N_CLOCK_SYNC* cs, double client_send_time, double server_time, double local_now) {
    __n_assert(cs, return FALSE);

    double rtt = local_now - client_send_time;
    if (rtt < 0.0) {
        n_log(LOG_ERR, "n_clock_sync: negative RTT (%.4f), ignoring sample", rtt);
        return FALSE;
    }

    double one_way = rtt / 2.0;
    double offset = server_time + one_way - local_now;

    /* Store in circular buffers */
    cs->offset_samples[cs->sample_index] = offset;
    cs->rtt_samples[cs->sample_index] = rtt;
    cs->sample_index = (cs->sample_index + 1) % N_CLOCK_SYNC_SAMPLE_COUNT;
    if (cs->sample_count < N_CLOCK_SYNC_SAMPLE_COUNT) {
        cs->sample_count++;
    }

    /* Compute medians */
    cs->estimated_offset = median_of(cs->offset_samples, cs->sample_count);
    cs->estimated_rtt = median_of(cs->rtt_samples, cs->sample_count);

    n_log(LOG_DEBUG, "n_clock_sync: sample %d, offset=%.4f rtt=%.4f (median offset=%.4f rtt=%.4f)",
          cs->sample_count, offset, rtt, cs->estimated_offset, cs->estimated_rtt);

    return TRUE;
}

double n_clock_sync_server_time(N_CLOCK_SYNC* cs, double local_now) {
    __n_assert(cs, return local_now);
    return local_now + cs->estimated_offset;
}

int n_clock_sync_should_send(N_CLOCK_SYNC* cs, double local_now) {
    __n_assert(cs, return FALSE);
    return (local_now - cs->last_sync_time) >= N_CLOCK_SYNC_INTERVAL;
}

void n_clock_sync_mark_sent(N_CLOCK_SYNC* cs, double local_now) {
    __n_assert(cs, return);
    cs->last_sync_time = local_now;
}

/**
  @}
  */
