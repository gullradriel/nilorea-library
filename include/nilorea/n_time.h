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

/**@file n_time.h
 * Timing utilities
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/03/05
 */

#ifndef __TIME_C_IMPLEMENTATION__
#define __TIME_C_IMPLEMENTATION__

#ifdef __cplusplus
extern "C" {
#endif

#include "n_common.h"

#include <time.h>
#include <limits.h>
#include "nilorea/n_windows.h"
#ifndef __windows__
#include <sys/time.h>
#endif

/**@defgroup N_TIME TIMERS: generally used headers, defines, timers, allocators,...
  @addtogroup N_TIME
  @{
  */

/*! Timing Structure */
typedef struct N_TIME {
    /*! time since last poll */
    time_t delta;
#ifdef __windows__
    /*! queryperformancefrequency storage */
    LARGE_INTEGER freq;
    /*! start time W32*/
    LARGE_INTEGER startTime;
    /*! current time W32*/
    LARGE_INTEGER currentTime;
#else
    /*! start time */
    struct timeval startTime;
    /*! current time */
    struct timeval currentTime;
#endif
} N_TIME;

/*! sleep for the given number of microseconds */
#if defined __windows__
void u_sleep(__int64 usec);
#else
void u_sleep(unsigned int usec);
#endif

/*! for the 'press a key to continue' */
void PAUSE(void);

/*! Init or restart from zero any N_TIME HiTimer */
int start_HiTimer(N_TIME* timer);

/*! Poll any N_TIME HiTimer, returning usec */
time_t get_usec(N_TIME* timer);

/*! Poll any N_TIME HiTimer, returning msec */
time_t get_msec(N_TIME* timer);

/*! Poll any N_TIME HiTimer, returning sec */
time_t get_sec(N_TIME* timer);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __COMMON_FOR_C_IMPLEMENTATION__ */
