/**
 *@file n_log.c
 *@brief Generic logging system
 *@author Castagnier Mickael
 *@date 2013-03-12
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"

#include <pthread.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef __windows__
#include <syslog.h>
#else
#include <time.h>
#include <windows.h>
#endif

static int terminal_support_colors = 0;

#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"

/*! internal struct to handle log types */
typedef struct LOG_LEVELS {
    /*! string of log type */
    char* c_name;
    /*! numeric value of log type */
    int c_val;
    /*! event log value */
    char* w_name;

} LOG_LEVELS;

/*! array of log levels */
static LOG_LEVELS prioritynames[] =
    {
        {"EMERG", LOG_EMERG, "ERROR"},
        {"ALERT", LOG_ALERT, "ERROR"},
        {"CRITICAL", LOG_CRIT, "ERROR"},
        {"ERR", LOG_ERR, "ERROR"},
        {"WARNING", LOG_WARNING, "WARNING"},
        {"NOTICE", LOG_NOTICE, "SUCCESS"},
        {"INFO", LOG_INFO, "INFORMATION"},
        {"DEBUG", LOG_DEBUG, "INFORMATION"},
        {NULL, -1, NULL}};

/*! static global maximum wanted log level value */
static int LOG_LEVEL = LOG_NULL;

/*! static global logging type ( STDERR, FILE, SYSJRNL ) */
static int LOG_TYPE = LOG_STDERR;

/*! static FILE handling if logging to file is enabled */
static FILE* log_file = NULL;

/*! static proc name, for windows event log */
char* proc_name = NULL;

/**
 *@brief Open connection to syslog or create internals for event log
 *@param identity Tag for syslog or NULL to use argv[0]
 *@return NULL or identity if success
 */
char* open_sysjrnl(char* identity) {
    __n_assert(identity, return NULL);
#ifndef __windows__
    /* LOG_CONS: log to console if no syslog available
     * LOG_PID: add pid of calling process to log
     * Local use 7 : compat with older logging systems
     */
    openlog(identity, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL7);
#endif
    proc_name = strdup(identity);
    return proc_name;
} /* open_sysjrnl */

/**
 * @brief Close syslog connection or clean internals for event log
 */
void close_sysjrnl(void) {
#ifndef __windows__
    closelog();
#endif
    FreeNoLog(proc_name);
} /* close_sysjrnl */

/**
 *@brief Set the global log level value ( static int LOG_LEVEL )
 *@param log_level Log level value. Supported: NOLOG,LOG_NOTICE/INFO/ERR/DEBUG,LOG_FILE/STDERR/SYSJRNL
 */
void set_log_level(const int log_level) {
    if (log_level == LOG_FILE) {
        LOG_TYPE = LOG_FILE;
        return;
    }
    if (log_level == LOG_STDERR) {
        LOG_TYPE = LOG_STDERR;
        return;
    }
    if (log_level == LOG_SYSJRNL) {
        LOG_TYPE = LOG_SYSJRNL;
        return;
    }
    LOG_LEVEL = log_level;
#ifdef __windows__
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(hOut, dwMode)) {
                // color support is enabled
                terminal_support_colors = 1;
            }
        }
    }
#else
    // Probably a terminal, may support colors
    if (isatty(fileno(stdout))) {
        terminal_support_colors = 1;
    }
#endif
} /* set_log_level() */

/**
 *@brief Get the global log level value
 *@return static int LOG_LEVEL
 */
int get_log_level(void) {
    return LOG_LEVEL;
} /* get_log_level() */

/**
 *@brief Set the logging to a file instead of stderr
 *@param file The filename where to log
 *@return TRUE or FALSE
 */
int set_log_file(char* file) {
    __n_assert(file, return FALSE);

    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }

    int fd = open(file, O_CREAT | O_APPEND | O_WRONLY, 0600);  // rw------- permissions
    if( fd == -1 )
    {
        n_log(LOG_ERR, "couldn't create %s with 0600 permission", _str(file));
        return FALSE;
    }

    log_file = fdopen(fd, "a");
    if( !log_file )
    {
        close(fd); 
        n_log(LOG_ERR, "fdopen returned an invalid file descriptor pointer for %s:%d", _str(file), fd); 
        return FALSE;
    }

    set_log_level(LOG_FILE);

    return TRUE;
} /* set_log_file */

/**
 *@brief return the current log_file
 *@return a valid FILE handle or NULL
 */
FILE* get_log_file(void) {
    return log_file;
} /*get_log_level() */

#ifndef _vscprintf
/**
 *@brief compute the size of a string made with format 'fmt' and arguments in the va_list 'ap', helper for vasprintf
 *@param format format to pass to printf
 *@param pargs va_list to use as printf parameter
 *@return -1 or the size of the string
 */
int _vscprintf_so(const char* format, va_list pargs) {
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(NULL, 0, format, argcopy);
    va_end(argcopy);
    return retval;
}
#endif

#ifndef vasprintf
/**
 *@brief snprintf from a va_list, helper for asprintf
 *@param strp string holding the result
 *@param fmt format to pass to printf
 *@param ap va_list to use as printf parameter
 *@return -1 or the size of the string
 */
int vasprintf(char** strp, const char* fmt, va_list ap) {
    long long int len = _vscprintf_so(fmt, ap);
    if (len == -1)
        return -1;
    char* str = NULL;
    Malloc(str, char, (size_t)len + 1 + sizeof(void*));  // len + EndOfString + padding
    if (!str)
        return -1;
    int r = vsnprintf(str, (size_t)(len + 1), fmt, ap); /* "secure" version of vsprintf */
    if (r == -1)
        return free(str), -1;
    *strp = str;
    return r;
}
#endif

#ifndef asprintf
/**
 *@brief snprintf from a va_list
 *@param strp string holding the result
 *@param fmt format to pass to printf
 *@param ... list of format parameters
 *@return -1 or the size of the string
 */
int asprintf(char* strp[], const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vasprintf(strp, fmt, ap);
    va_end(ap);
    return r;
}
#endif

/**
 *@brief Logging function. log( level , const char *format , ... ) is a macro around _log
 *@param level Logging level
 *@param file File containing the emmited log
 *@param func Function emmiting the log
 *@param line Line of the log
 *@param format Format and string of the log, printf style
 */
void _n_log(int level, const char* file, const char* func, int line, const char* format, ...) {
    FILE* out = NULL;

    if (level == LOG_NULL)
        return;

    if (!log_file)
        out = stderr;
    else
        out = log_file;

    int log_level = get_log_level();

    if (level <= log_level) {
        va_list args;
        char* syslogbuffer = NULL;
        char* eventbuffer = NULL;
#ifdef __windows__
        size_t needed = 0;
        char* name = "NULL";
        if (proc_name)
            name = proc_name;
#endif

        const char* color = "";
        const char* color_reset = "";

        if (terminal_support_colors) {
            color_reset = COLOR_RESET;
            switch (level) {
                case LOG_EMERG:
                    color = COLOR_RED;
                    break;
                case LOG_ALERT:
                    color = COLOR_MAGENTA;
                    break;
                case LOG_CRIT:
                    color = COLOR_RED;
                    break;
                case LOG_ERR:
                    color = COLOR_RED;
                    break;
                case LOG_WARNING:
                    color = COLOR_YELLOW;
                    break;
                case LOG_NOTICE:
                    color = COLOR_GREEN;
                    break;
                case LOG_INFO:
                    color = COLOR_CYAN;
                    break;
                case LOG_DEBUG:
                    color = COLOR_BLUE;
                    break;
                default:
                    color = COLOR_WHITE;
                    break;
            }
        }
        switch (LOG_TYPE) {
            case LOG_SYSJRNL:
                va_start(args, format);
                vasprintf(&syslogbuffer, format, args);
                va_end(args);
#ifdef __windows__
                needed = (unsigned long long)snprintf(NULL, 0, "start /B EventCreate /t %s /id 666 /l APPLICATION /so %s /d \"%s\" > NUL 2>&1", prioritynames[level].w_name, name, syslogbuffer);
                Malloc(eventbuffer, char, needed + 4);
                sprintf(eventbuffer, "start /B EventCreate /t %s /id 666 /l APPLICATION /so %s /d \"%s\" > NUL 2>&1", prioritynames[level].w_name, name, syslogbuffer);
                system(eventbuffer);
#else
                syslog(level, "%s->%s:%d %s", _str(file), _str(func), line, syslogbuffer);
#endif
                FreeNoLog(syslogbuffer);
                FreeNoLog(eventbuffer);
                break;
            default:
                fprintf(out, "%s%s:%jd:%s->%s:%d ", color, prioritynames[level].c_name, (intmax_t)time(NULL), _str(file), _str(func), line);
                va_start(args, format);
                vfprintf(out, format, args);
                va_end(args);
                fprintf(out, "%s\n", color_reset);
                break;
        }
        fflush(out);
    }
} /* _n_log( ... ) */

/**
 *@brief Open a thread-safe logging file
 *@param log A TS_LOG handler
 *@param pathname The file path (if any) and name
 *@param opt Options for opening (please never forget to use "w")
 *@return TRUE on success , FALSE on error , -1000 if already open
 */
int open_safe_logging(TS_LOG** log, char* pathname, char* opt) {
    if (!log) {
        n_log(LOG_ERR, "Invalid log pointer (NULL)");
        return FALSE;
    }

    if (*log) {
        if ((*log)->file) {
            n_log(LOG_ERR, "Log file '%s' already open", _str(pathname));
            return -1000;  // already open
        }
        n_log(LOG_ERR, "Log struct already allocated without file");
        return FALSE;
    }

    if (!pathname || !opt) {
        n_log(LOG_ERR, "Invalid pathname or mode (pathname=%s, opt=%s)", _str(pathname), _str(opt));
        return FALSE;
    }

    Malloc((*log), TS_LOG, 1);
    if (!(*log)) {
        n_log(LOG_ERR, "Failed to allocate TS_LOG structure");
        return FALSE;
    }

    pthread_mutex_init(&(*log)->LOG_MUTEX, NULL);

    int flags = 0;
    if (strcmp(opt, "a") == 0 || strcmp(opt, "a+") == 0) {
        flags = O_CREAT | O_APPEND | O_WRONLY;
    } else if (strcmp(opt, "w") == 0 || strcmp(opt, "w+") == 0) {
        flags = O_CREAT | O_TRUNC | O_WRONLY;
    } else {
        n_log(LOG_ERR, "Unsupported open mode '%s' for pathname '%s'", _str(opt), _str(pathname));
        free(*log);
        *log = NULL;
        return FALSE;
    }

    int fd = open(pathname, flags, 0600);  // rw-------
    if (fd == -1) {
        n_log(LOG_ERR, "Failed to open '%s' with 0600 permissions", _str(pathname));
        free(*log);
        *log = NULL;
        return FALSE;
    }

    (*log)->file = fdopen(fd, opt);
    if (!(*log)->file) {
        n_log(LOG_ERR, "fdopen failed for '%s' (fd=%d, mode='%s')", _str(pathname), fd, _str(opt));
        close(fd);
        free(*log);
        *log = NULL;
        return FALSE;
    }

    return TRUE;
} /* open_safe_logging */

/**
 *@brief write to a thread-safe logging file
 *@param log A TS_LOG handler
 *@param pat Pattern for writting (i.e "%d %d %s")
 *@return TRUE on success, FALSE on error
 */
int write_safe_log(TS_LOG* log, char* pat, ...) {
    /* argument list */
    va_list arg;
    char str[2048] = "";

    if (!log)
        return FALSE;

    va_start(arg, pat);

    vsnprintf(str, sizeof(str), pat, arg);

    va_end(arg);

    pthread_mutex_lock(&log->LOG_MUTEX);
    fprintf(log->file, "%s", str);
    fflush(log->file);
    pthread_mutex_unlock(&log->LOG_MUTEX);

    return TRUE;

} /* write_safe_log( ... ) */

/**
 *@brief close a thread-safe logging file
 *@param log A TS_LOG handler
 *@return TRUE on success, FALSE on error
 */
int close_safe_logging(TS_LOG* log) {
    if (!log)
        return FALSE;

    pthread_mutex_lock(&log->LOG_MUTEX);
    fflush(log->file);
    pthread_mutex_unlock(&log->LOG_MUTEX);

    pthread_mutex_destroy(&log->LOG_MUTEX);

    fclose(log->file);

    return TRUE;

} /* close_safe_logging( ...) */
