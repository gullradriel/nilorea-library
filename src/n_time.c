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

/**@file n_time.c
 *@brief Common time functions
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/03/05
 */

#include "nilorea/n_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(__windows__)
/**
 *@brief u_sleep for windows
 *@param used Number of usec to sleep
 */
void u_sleep(__int64 usec) {
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10 * usec);  // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}
#else
/**
 *@brief wrapper around usleep for API consistency
 *@param usec Number of usec to sleep
 */
void u_sleep(unsigned int usec) {
    usleep(usec);
}
#endif

/**
 *@brief make a pause in a terminal
 */
void PAUSE(void) {
    char k[2] = "";

    printf("Press enter to continue...");

    int n = scanf("%c", k);
    int error = errno;
    if (n == EOF) {
        n_log(LOG_DEBUG, "Enter key was pressed !");
    } else if (error != 0) {
        n_log(LOG_ERR, "error %s when waiting for a key !", strerror(error));
    } else {
        n_log(LOG_DEBUG, "No matching characters");
    }

    printf("\n");
} /* PAUSE(...) */

/**
 *@brief Initialize or restart from zero any N_TIME HiTimer
 *@param timer Any N_TIMER *timer you wanna start or reset
 *@return TRUE or FALSE
 */
int start_HiTimer(N_TIME* timer) {
    __n_assert(timer, return FALSE);
    timer->delta = 0;

#if defined(__windows__)
    if (QueryPerformanceFrequency((LARGE_INTEGER*)&timer->freq) == 0)
        return FALSE;
    if (QueryPerformanceCounter(&timer->startTime) == 0)
        return FALSE;
#else
    if (gettimeofday(&timer->startTime, 0) != 0)
        return FALSE;
#endif

    return TRUE;
} /* init_HiTimer(...) */

/**
 *@brief Poll any N_TIME HiTimer, returning usec, and moving currentTime to startTime
 *@param timer Any N_TIMER *timer you wanna poll
 *@return The elapsed number of usec for the given N_TIME *timer
 */
time_t get_usec(N_TIME* timer) {
    __n_assert(timer, return 0);
#ifdef __windows__
    QueryPerformanceCounter((LARGE_INTEGER*)&timer->currentTime);
    timer->delta = 1000000 * (timer->currentTime.QuadPart - timer->startTime.QuadPart) / timer->freq.QuadPart;
    timer->startTime = timer->currentTime;
#else
    gettimeofday(&timer->currentTime, 0);
    timer->delta = (timer->currentTime.tv_sec - timer->startTime.tv_sec) * 1000000 + (timer->currentTime.tv_usec - timer->startTime.tv_usec);
    timer->startTime.tv_sec = timer->currentTime.tv_sec;
    timer->startTime.tv_usec = timer->currentTime.tv_usec;
#endif

    return timer->delta;
} /* get_usec( ... ) */

/**
 *@brief Poll any N_TIME HiTimer, returning msec, and moving currentTime to startTime
 *@param timer Any N_TIMER *timer you wanna poll
 *@return The elapsed number of msec for the given N_TIME *timer
 */
time_t get_msec(N_TIME* timer) {
    __n_assert(timer, return 0);
#ifdef __windows__
    QueryPerformanceCounter((LARGE_INTEGER*)&timer->currentTime);
    timer->delta = 1000 * (timer->currentTime.QuadPart - timer->startTime.QuadPart) / timer->freq.QuadPart;
    timer->startTime = timer->currentTime;
#else
    gettimeofday(&timer->currentTime, 0);
    timer->delta = (timer->currentTime.tv_sec - timer->startTime.tv_sec) * 1000 + (timer->currentTime.tv_usec - timer->startTime.tv_usec) / 1000;
    timer->startTime.tv_sec = timer->currentTime.tv_sec;
    timer->startTime.tv_usec = timer->currentTime.tv_usec;
#endif

    return timer->delta;
} /* get_msec(...) */

/**
 *@brief Poll any N_TIME HiTimer, returning sec, and moving currentTime to startTime
 *@param timer Any N_TIMER *timer you wanna poll
 *@return The elapsed number of sec for the given N_TIME *timer
 */
time_t get_sec(N_TIME* timer) {
    __n_assert(timer, return 0);
#ifdef __windows__
    QueryPerformanceCounter((LARGE_INTEGER*)&timer->currentTime);
    timer->delta = (timer->currentTime.QuadPart - timer->startTime.QuadPart) / timer->freq.QuadPart;
    timer->startTime = timer->currentTime;
#else
    gettimeofday(&timer->currentTime, 0);
    timer->delta = (timer->currentTime.tv_sec - timer->startTime.tv_sec) + (timer->currentTime.tv_usec - timer->startTime.tv_usec) / 1000000;
    timer->startTime.tv_sec = timer->currentTime.tv_sec;
    timer->startTime.tv_usec = timer->currentTime.tv_usec;
#endif
    return timer->delta;
} /* get_sec(...) */
