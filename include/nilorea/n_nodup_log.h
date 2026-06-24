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
 *@file n_nodup_log.h
 *@brief Generic No Dup Log system
 *@author Castagnier Mickael
 *@date 2013-04-15
 */

#ifndef __NO_DUP_LOG_HEADER_GUARD__
#define __NO_DUP_LOG_HEADER_GUARD__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup LOGNODUP LOGGING NODUP: no duplicate logging to console, to file, to syslog
   @addtogroup LOGNODUP
  @{
*/

#include "n_common.h"
#include "n_log.h"
#include "n_str.h"
#include "n_list.h"
#include "n_hash.h"

/*! @brief init the nodup log internal hash table */
int init_nodup_log(size_t max);
/*! @brief empty the nodup table */
int empty_nodup_table();
/*! @brief close the nodup log session */
int close_nodup_log();

/*! nodup log macro helper */
#define n_nodup_log(__LEVEL__, ...)                                         \
    do {                                                                    \
        _n_nodup_log(__LEVEL__, __FILE__, __func__, __LINE__, __VA_ARGS__); \
    } while (0)

/*! nodup log indexed macro helper */
#define n_nodup_log_indexed(__LEVEL__, __PREF__, ...)                                         \
    do {                                                                                      \
        _n_nodup_log_indexed(__LEVEL__, __PREF__, __FILE__, __func__, __LINE__, __VA_ARGS__); \
    } while (0)

/*! @brief log a message only once per unique location */
void _n_nodup_log(int LEVEL, const char* file, const char* func, int line, const char* format, ...);
/*! @brief log a message only once per unique prefix and location */
void _n_nodup_log_indexed(int LEVEL, const char* prefix, const char* file, const char* func, int line, const char* format, ...);
/*! @brief dump the nodup log hash table to a file in human-readable form */
int dump_nodup_log(char* file);

/**
@}
*/

#ifdef __cplusplus
}
#endif
#endif
