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
 *@file n_clock_sync.c
 *@brief Clock synchronization estimator implementation
 *@author Castagnier Mickael
 *@version 1.1
 *@date 19/07/2026
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

/*! effective (slewed) offset at local_now — pure function of the stored
 *  state so the const getter needs no mutation. Blends offset_prev →
 *  estimated_offset with a smoothstep over N_CLOCK_SYNC_SLEW_SECONDS
 *  from the moment the estimate last changed. */
static double clock_sync_effective_offset(const N_CLOCK_SYNC* cs, double local_now) {
    if (cs->offset_prev == cs->estimated_offset) return cs->estimated_offset;
    double dt = local_now - cs->offset_change_time;
    if (dt >= N_CLOCK_SYNC_SLEW_SECONDS) return cs->estimated_offset;
    if (dt <= 0.0) return cs->offset_prev;
    double u = dt / N_CLOCK_SYNC_SLEW_SECONDS;
    u = u * u * (3.0 - 2.0 * u); /* smoothstep: C1-continuous rate */
    return cs->offset_prev + (cs->estimated_offset - cs->offset_prev) * u;
}

/*! store one (offset, rtt) sample and recompute the estimates.
 *
 *  Offset selection is NTP-style: the sample with the LOWEST rtt wins,
 *  because its one-way uncertainty (+/-rtt/2) is the smallest. A plain
 *  median is poisoned for minutes when the link is congested at
 *  startup (e.g. an MMO client receiving its login chunk burst: every
 *  early sample carries seconds of queueing delay, and the median
 *  needs a majority of clean samples to recover). estimated_rtt stays
 *  a median, it feeds timeout heuristics, where typical beats best.
 *
 *  local_now anchors the slew when the adopted offset changes:
 *  n_clock_sync_server_time blends from the previous effective offset
 *  instead of stepping. */
static void clock_sync_store_sample(N_CLOCK_SYNC* cs, double offset, double rtt, double local_now) {
    cs->offset_samples[cs->sample_index] = offset;
    cs->rtt_samples[cs->sample_index] = rtt;
    cs->sample_index = (cs->sample_index + 1) % N_CLOCK_SYNC_SAMPLE_COUNT;
    if (cs->sample_count < N_CLOCK_SYNC_SAMPLE_COUNT) {
        cs->sample_count++;
    }

    int best = 0;
    for (int i = 1; i < cs->sample_count; i++) {
        if (cs->rtt_samples[i] < cs->rtt_samples[best]) best = i;
    }
    double new_target = cs->offset_samples[best];
    if (new_target != cs->estimated_offset) {
        /* re-anchor the slew at the CURRENT effective offset so a
         * target change mid-slew stays continuous */
        cs->offset_prev = clock_sync_effective_offset(cs, local_now);
        cs->offset_change_time = local_now;
        cs->estimated_offset = new_target;
    }
    cs->estimated_rtt = median_of(cs->rtt_samples, cs->sample_count);
}

int n_clock_sync_process_response(N_CLOCK_SYNC* cs, double client_send_time, double server_time, double local_now) {
    __n_assert(cs, return FALSE);

    double rtt = local_now - client_send_time;
    if (rtt < 0.0) {
        n_log(LOG_ERR, "n_clock_sync: negative RTT (%.4f), ignoring sample", rtt);
        return FALSE;
    }
    if (rtt > N_CLOCK_SYNC_MAX_RTT) {
        n_log(LOG_WARNING, "n_clock_sync: absurd RTT (%.4f > %.1f), ignoring sample", rtt, (double)N_CLOCK_SYNC_MAX_RTT);
        return FALSE;
    }

    if (cs->synthetic) {
        /* First REAL response: flush the synthetic login seed wholesale.
         * Its conservative synthetic rtt can be lower than anything a
         * high-RTT link ever produces, which would pin the estimate on
         * the seed's one-shot (transit-skewed) offset forever. */
        cs->sample_index = 0;
        cs->sample_count = 0;
        cs->synthetic = 0;
    }

    double one_way = rtt / 2.0;
    double offset = server_time + one_way - local_now;

    clock_sync_store_sample(cs, offset, rtt, local_now);

    n_log(LOG_DEBUG, "n_clock_sync: sample %d, offset=%.4f rtt=%.4f (best offset=%.4f median rtt=%.4f)",
          cs->sample_count, offset, rtt, cs->estimated_offset, cs->estimated_rtt);

    return TRUE;
}

void n_clock_sync_seed(N_CLOCK_SYNC* cs, double offset, double rtt) {
    __n_assert(cs, return);
    if (rtt < 0.0) rtt = 0.0;
    /* Reset to the synthetic sample and adopt immediately — at login
     * there is no meaningful previous estimate to slew from. */
    cs->sample_index = 0;
    cs->sample_count = 0;
    clock_sync_store_sample(cs, offset, rtt, 0.0);
    cs->offset_prev = cs->estimated_offset;
    cs->offset_change_time = 0.0;
    cs->synthetic = 1;
    n_log(LOG_DEBUG, "n_clock_sync: seeded offset=%.4f (synthetic rtt=%.4f)", offset, rtt);
}

double n_clock_sync_server_time(const N_CLOCK_SYNC* cs, double local_now) {
    __n_assert(cs, return local_now);
    return local_now + clock_sync_effective_offset(cs, local_now);
}

int n_clock_sync_should_send(const N_CLOCK_SYNC* cs, double local_now) {
    __n_assert(cs, return FALSE);
    /* Burst until warm: a synthetic seed does not count as a real sample. */
    int real_samples = cs->synthetic ? 0 : cs->sample_count;
    double interval = (real_samples < N_CLOCK_SYNC_BURST_SAMPLES)
                          ? N_CLOCK_SYNC_BURST_INTERVAL
                          : N_CLOCK_SYNC_INTERVAL;
    return (local_now - cs->last_sync_time) >= interval;
}

void n_clock_sync_mark_sent(N_CLOCK_SYNC* cs, double local_now) {
    __n_assert(cs, return);
    cs->last_sync_time = local_now;
}

/**
  @}
  */
