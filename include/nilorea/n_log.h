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
 *@file n_log.h
 *@brief Generic log system
 *@author Castagnier Mickael
 *@date 2013-04-15
 */

#ifndef __LOG_HEADER_GUARD__
#define __LOG_HEADER_GUARD__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup LOG LOGGING: logging to console, to file, to syslog, to event log
  @addtogroup LOG
  @{
  */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pthread.h>
#include "n_common.h"

/*! no log output */
#define LOG_NULL -1
/*! internal, logging to file */
#define LOG_FILE -3
/*! internal, default LOG_TYPE */
#define LOG_STDERR -4
/*! to sysjrnl */
#define LOG_SYSJRNL 100

#if defined(__linux__) || defined(__sun)

#include <syslog.h>
#include <pthread.h>

#ifdef __sun
#include <sys/varargs.h>
#else
#include <stdarg.h>
#endif

#else

#include <stdarg.h>

/*! system is unusable */
#define LOG_EMERG 0
/*! action must be taken immediately */
#define LOG_ALERT 1
/*! critical conditions */
#define LOG_CRIT 2
/*! error conditions */
#define LOG_ERR 3
/*! warning conditions */
#define LOG_WARNING 4
/*! normal but significant condition */
#define LOG_NOTICE 5
/*! informational */
#define LOG_INFO 6
/*! debug-level messages */
#define LOG_DEBUG 7

#endif

/*! Logging function wrapper to get line and func */
#define n_log(__LEVEL__, ...)                                         \
    do {                                                              \
        _n_log(__LEVEL__, __FILE__, __func__, __LINE__, __VA_ARGS__); \
    } while (0)

/*! ThreadSafe LOGging structure */
typedef struct TS_LOG {
    /*! mutex for thread-safe writting */
    pthread_mutex_t LOG_MUTEX;
    /*! File handler */
    FILE* file;
} TS_LOG;

/*! Open a syslog / set name for event log */
char* open_sysjrnl(char* identity);
/*! close syslog / free name for event log */
void close_sysjrnl(void);
/*! Set global log level. Possible values: NOLOG , NOTICE , VERBOSE , ERROR , DEBUG */
void set_log_level(const int log_level);
/*! Return the global log level. Possible values: NOLOG , NOTICE , VERBOSE , ERROR , DEBUG */
int get_log_level(void);
/*! set a file as standard log output */
int set_log_file(char* file);
/*! get the log file name if any */
FILE* get_log_file(void);
/*! Full log function. Must be wrapped in a MACRO to get the correct file/func/line information */
void _n_log(int level, const char* file, const char* func, int line, const char* format, ...);

/*! Open a thread-safe logging file */
int open_safe_logging(TS_LOG** log, char* pathname, char* opt);
/*! write to a thread-safe logging file */
int write_safe_log(TS_LOG* log, char* pat, ...);
/*! close a thread-safe logging file */
int close_safe_logging(TS_LOG* log);

/**
@}
*/

#ifdef __cplusplus
}
#endif
#endif
