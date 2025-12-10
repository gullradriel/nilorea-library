/**
 *@file n_nodup_log.c
 *@brief generic no duplicate logging system
 *@author Castagnier Mickael
 *@date 2015-09-21
 */

/*! Feature test macro */
#ifndef _GNU_SOURCE
/*! GNU source */
#define _GNU_SOURCE
/*! flag to detect if gnu source was set from our source file */
#define _GNU_SOURCE_WAS_NOT_DEFINED
#endif
#include <stdio.h>
#include <stdarg.h>
#ifdef _GNU_SOURCE_WAS_NOT_DEFINED
#undef _GNU_SOURCE
#endif

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"

#include "nilorea/n_nodup_log.h"

/*! internal: no dup hash_table log save */
static HASH_TABLE* _n_nodup_table = NULL;

/**
 *@brief initialize the no duplicate logging system
 *@param max the max size of the internal hash table. Leave it to zero to keep the internal value in use ( 1024 )
 *@return TRUE or FALSE */
int init_nodup_log(size_t max) {
    if (_n_nodup_table != NULL) {
        n_log(LOG_ERR, "Could not allocate the internal hash table because it's already done");
        return FALSE;
    }
    if (max <= 0)
        max = 1024;

    _n_nodup_table = new_ht(max);

    if (_n_nodup_table == NULL) {
        n_log(LOG_ERR, "Could not allocate the internal hash with size %d", max);
        return FALSE;
    } else {
        n_log(LOG_DEBUG, "LOGGING: nodup system activated with a hash table of %d cells", max);
    }

    return TRUE;
} /* init_nodup_log() */

/**
 * @brief Empty the nodup internal table
 * @return TRUE or FALSE
 */
int empty_nodup_table() {
    __n_assert(_n_nodup_table, return FALSE);
    return empty_ht(_n_nodup_table);
} /* empty_nodup_table() */

/**
 * @brief Empty nodup logtable and close the no duplicate logging session
 * @return TRUE or FALSE
 */
int close_nodup_log() {
    __n_assert(_n_nodup_table, return FALSE);
    return destroy_ht(&_n_nodup_table);
} /* close_nodup_log() */

/**
 *@brief internal, get a key for a log entry
 *@param file log source file
 *@param func log source
 *@param line log source
 *@return The key for the the log entry
 */
static char* get_nodup_key(const char* file, const char* func, int line) {
    N_STR* nstr = NULL;
    char* ptr = NULL;
    nstrprintf(nstr, "%s%s%d", file, func, line);
    __n_assert(nstr, return NULL);
    ptr = nstr->data;
    Free(nstr);
    return ptr;
} /* get_nodup_key */

/**
 *@brief internal, get a key for an indexed log entry
 *@param file log source file
 *@param func log source
 *@param line log source
 *@param prefix prefix for log entry
 *@return The key for the the log entry
 */
static char* get_nodup_indexed_key(const char* file, const char* func, const char* prefix, int line) {
    N_STR* nstr = NULL;
    char* ptr = NULL;
    nstrprintf(nstr, "%s%s%s%d", file, func, prefix, line);
    __n_assert(nstr, return NULL);
    ptr = nstr->data;
    Free(nstr);
    return ptr;
} /* get_nodup_key */

/**
 * @brief check if a log was already done or not at the given line, func, file
 * @param log the line of log
 * @param file the file containing the log
 * @param func the name of calling func
 * @param line the line of the log
 * @return 0 if not existing 1 if existing with the same value , 2 if key exist
 and value is different
 */
int check_n_log_dup(const char* log, const char* file, const char* func, int line) {
    HASH_NODE* node = NULL;
    char* key = NULL;

    /* check if the nopdup session is on, else do a normal n_log */
    if (!_n_nodup_table) {
        return 3;
    }

    key = get_nodup_key(file, func, line);

    __n_assert(key, return FALSE);

    node = ht_get_node(_n_nodup_table, key);

    Free(key);

    if (node) {
        if (strcmp(log, node->data.string) == 0) {
            return 1;
        }
        return 2;
    }
    return 0;

} /* check_n_log_dup(...) */

/**
 * @brief check if a log was already done or not at the given line, func, file
 * @param log the line of log
 * @param file the file containing the log
 * @param func the name of calling func
 * @param line the line of the log
 * @param prefix prefix to subgroup logs
 * @return 0 if not existing 1 if existing with the same value , 2 if key exist
 and value is different
 */
int check_n_log_dup_indexed(const char* log, const char* file, const char* func, int line, const char* prefix) {
    HASH_NODE* node = NULL;
    char* key = NULL;

    /* check if the nopdup session is on, else do a normal n_log */
    if (!_n_nodup_table) {
        return 3;
    }

    key = get_nodup_indexed_key(file, func, prefix, line);

    __n_assert(key, return FALSE);

    node = ht_get_node(_n_nodup_table, key);

    Free(key)

        if (node) {
        if (strcmp(log, node->data.string) == 0) {
            return 1;
        }
        return 2;
    }
    return 0;
} /* check_nolog_dup_indexed() */

/**
 *@brief Logging function. log( level , const char *format , ... ) is a macro around _log
 *@param LEVEL Logging level
 *@param file File containing the emmited log
 *@param func Function emmiting the log
 *@param line Line of the log
 *@param format Format and string of the log, printf style
 */
void _n_nodup_log(int LEVEL, const char* file, const char* func, int line, const char* format, ...) {
    __n_assert(file, return);
    __n_assert(func, return);
    __n_assert(format, return);

    HASH_NODE* node = NULL;
    va_list args;

    char* syslogbuffer = NULL;

    va_start(args, format);
    if (vasprintf(&syslogbuffer, format, args) == -1) {
        int error = errno;
        n_log(LOG_ERR, "=>%s:%s:%d unable to parse '%s', %s", file, func, line, format, strerror(error));
    }
    va_end(args);

    char* key = get_nodup_key(file, func, line);
    int is_dup = check_n_log_dup(syslogbuffer, file, func, line);

    switch (is_dup) {
        /* new log entry for file,func,line */
        case 0:
            if (_n_nodup_table) {
                ht_put_string(_n_nodup_table, key, syslogbuffer);
            }
            _n_log(LEVEL, file, func, line, "%s", syslogbuffer);
            break;

        /* exising and same log entry, do nothing (maybe latter we will add a timeout to repeat logging)  */
        case 1:
            break;
        /* existing but different entry. We have to refresh and log it one time*/
        case 2:
            node = ht_get_node(_n_nodup_table, key);
            if (node && node->data.string) {
                Free(node->data.string);
                node->data.string = syslogbuffer;
            }
            _n_log(LEVEL, file, func, line, "%s", syslogbuffer);
            break;
        /* no nodup session started, normal loggin */
        case 3:
        default:
            _n_log(LEVEL, file, func, line, "%s", syslogbuffer);
            break;
    }

    Free(syslogbuffer);
    Free(key);

} /* _n_nodup_log() */

/**
 *@brief Logging function. log( level , const char *format , ... ) is a macro around _log
 *@param LEVEL Logging level
 *@param prefix prefix to subgroup logs
 *@param file File containing the emmited log
 *@param func Function emmiting the log
 *@param line Line of the log
 *@param format Format and string of the log, printf style
 */
void _n_nodup_log_indexed(int LEVEL, const char* prefix, const char* file, const char* func, int line, const char* format, ...) {
    HASH_NODE* node = NULL;
    va_list args;

    char* syslogbuffer = NULL;

    va_start(args, format);
    if (vasprintf(&syslogbuffer, format, args) == -1) {
        int error = errno;
        n_log(LOG_ERR, "=>%s:%s:%d unable to parse '%s:%s', %s", file, func, line, prefix, format, strerror(error));
    }
    va_end(args);

    char* key = get_nodup_indexed_key(file, func, prefix, line);
    int is_dup = check_n_log_dup_indexed(syslogbuffer, file, func, line, prefix);

    switch (is_dup) {
        /* new log entry for file,func,line */
        case 0:
            if (_n_nodup_table) {
                ht_put_string(_n_nodup_table, key, syslogbuffer);
            }
            _n_log(LEVEL, file, func, line, "%s", syslogbuffer);
            break;

        /* exising and same log entry, do nothing (maybe latter we will add a timeout to repeat logging)  */
        case 1:
            break;
        /* existing but different entry. We have to refresh and log it one time*/
        case 2:
            node = ht_get_node(_n_nodup_table, key);
            if (node && node->data.string) {
                Free(node->data.string);
                node->data.string = syslogbuffer;
            }
            _n_log(LEVEL, file, func, line, "%s", syslogbuffer);
            break;
        /* no nodup session started, normal loggin */
        case 3:
        default:
            _n_log(LEVEL, file, func, line, "%s", syslogbuffer);
            break;
    }

    Free(syslogbuffer);
    Free(key);

} /* _n_nodup_log_indexed() */

/**
 *@brief Dump the duplicate error log hash table in a file
 * The table is first written to a temporary file which is then renamed to the
 * final destination and permissions set via \c rename() and \c chmod().
 *@param file The path and filename to the dump file
 *@return TRUE or FALSE
 */
int dump_nodup_log(char* file) {
    FILE* out = NULL;
    HASH_NODE* hash_node = NULL;
    __n_assert(file, return FALSE);
    __n_assert(_n_nodup_table, return FALSE);

    char* tmpfile = NULL;
    n_log(LOG_DEBUG, "outfile:%s", file);
    strprintf(tmpfile, "%s.tmp", file);
    if (!tmpfile) {
        n_log(LOG_ERR, "could not create tmp file name from filename %s", _str(file));
        return FALSE;
    }

    int fd = open(tmpfile, O_CREAT | O_WRONLY | O_TRUNC, 0600);  // Only owner can read/write
    if (fd < 0) {
        n_log(LOG_ERR, "could not create file %s with 0600 permissions", _str(tmpfile));
        Free(tmpfile);
        return FALSE;
    }

    out = fdopen(fd, "wb");
    __n_assert(out, close(fd); Free(tmpfile); return FALSE);

    if (_n_nodup_table) {
        for (unsigned long int it = 0; it < _n_nodup_table->size; it++) {
            list_foreach(list_node, _n_nodup_table->hash_table[it]) {
                hash_node = (HASH_NODE*)list_node->ptr;
                fprintf(out, "%s\n", hash_node->data.string);
            }
        }
    }
    fclose(out);

    int error = 0;
    if (rename(tmpfile, file) != 0) {
        error = errno;
        n_log(LOG_ERR, "could not rename '%s' to '%s' , %s", tmpfile, file,
              strerror(error));
    }
    Free(tmpfile);

    return TRUE;

} /* dump_nodup_log() */
