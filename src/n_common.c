/**
 *@file n_common.c
 *@brief Common function
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/03/05
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

#ifdef __windows__
#include <windows.h>
#else
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#ifdef __linux__
#include <string.h>
#include <linux/limits.h>
#include <unistd.h>
#endif
#endif

/**
 *@brief abort program with a text
 *@param format printf style format and args
 */
void n_abort(char const* format, ...) {
    char str[1024] = "";
    va_list args;
    va_start(args, format);

    vsnprintf(str, sizeof str, format, args);
    va_end(args);
    fprintf(stderr, "%s", str);
    exit(1);
}

/**
 *@brief abort program with a text
 *@param computer_name allocated buffer to hold the computer name
 *@param len size of computer_name variable
 *@return TRUE or FALSE
 */
int get_computer_name(char* computer_name, size_t len) {
    memset(computer_name, 0, len);
#ifdef WIN32
    TCHAR infoBuf[len];
    DWORD bufCharCount = len;
    if (GetComputerName(infoBuf, &bufCharCount)) {
        memcpy(computer_name, infoBuf, len);
    } else {
        strcpy(computer_name, "Unknown_Host_Name");
        return FALSE;
    }
#else
    if (gethostname(computer_name, len) == -1) {
        int error = errno;
        strcpy(computer_name, "Unknown_Host_Name");
        n_log(LOG_ERR, "%s on gethostname !", strerror(error));
        return FALSE;
    }
#endif
    return TRUE;
} /* get_computer_name */

/**
 *@brief test if file exist and if it's readable
 *@param filename Path/name of the file
 *@return TRUE or FALSE
 */
int file_exist(const char* filename) {
    FILE* file = NULL;
    if ((file = fopen(filename, "r")) != NULL) {
        fclose(file);
        return 1;
    }
    return 0;
} /* file_exist */

/**
 *@brief get current program running directory
 *@return A copy of the current program running directory inside a string
 */
char* get_prog_dir(void) {
    char strbuf[PATH_MAX] = "";

    int error = 0;
#ifdef __windows__
    unsigned long int bytes = GetModuleFileName(NULL, strbuf, PATH_MAX);
    error = errno;
    if (bytes != 0) {
        return strdup(dirname(strbuf));
    }
#else
    char procbuf[PATH_MAX] = "";
#ifdef __linux__
    sprintf(procbuf, "/proc/%d/exe", (int)getpid());
#elif defined __sun
    sprintf(procbuf, "/proc/%d/path/a.out", (int)getpid());
#endif
    ssize_t bytes = MIN(readlink(procbuf, strbuf, PATH_MAX), PATH_MAX - 1);
    error = errno;
    if (bytes >= 0) {
        strbuf[bytes] = '\0';
        return strdup(dirname(strbuf));
    }
#endif
    fprintf(stderr, "%s", strerror(error));
    return NULL;
} /* get_prog_dir */

/**
 *@brief get current program name
 *@return A copy of the current program name inside a string
 */
char* get_prog_name(void) {
    char strbuf[PATH_MAX] = "";
    int error = 0;
#ifdef __windows__
    unsigned long int bytes = GetModuleFileName(NULL, strbuf, PATH_MAX);
    error = errno;
    if (bytes != 0) {
        return strdup(basename(strbuf));
    }
#else
    char procbuf[PATH_MAX] = "";
#ifdef __linux__
    sprintf(procbuf, "/proc/%d/exe", (int)getpid());
#elif defined __sun
    sprintf(procbuf, "/proc/%d/path/a.out", (int)getpid());
#endif
    ssize_t bytes = MIN(readlink(procbuf, strbuf, PATH_MAX), PATH_MAX - 1);
    error = errno;
    if (bytes >= 0) {
        strbuf[bytes] = '\0';
        return strdup(basename(strbuf));
    }
#endif
    fprintf(stderr, "%s", strerror(error));
    return NULL;
} /* get_prog_dir */

/**
 * @brief launch a command abd return output and status
 * @param cmd The command to launch
 * @param read_buf_size popen read buf. Also serves as a minimum size for the dynamically allocated returned output if not already allocaded
 * @param nstr_output Pointer to a valid N_STR or NULL (see read_buf_size)
 * @param ret Command output if any. Should be initialized to -1 and tested against to check if it's valid.
 * @return TRUE or FALSE
 */
int n_popen(char* cmd, size_t read_buf_size, void** nstr_output, int* ret) {
    __n_assert(cmd, return FALSE);

    if (read_buf_size > INT_MAX) {
        n_log(LOG_ERR, "read_buf_size buffer size is too large for fgets (%zu>%d)", read_buf_size, INT_MAX);
        return FALSE;
    }

    N_STR* output_pointer = new_nstr(read_buf_size + 1);

    FILE* fp = NULL;
    int status = 0;
    char read_buf[read_buf_size];

    fp = popen(cmd, "r");
    int err = errno;
    if (fp == NULL) {
        n_log(LOG_ERR, "popen( %s ) returned NULL , %s (%d)", cmd, strerror(err), err);
        free_nstr(&output_pointer);
        return FALSE;
    }
    size_t length = 0;
    while (fgets(read_buf, (int)read_buf_size, fp) != NULL) {
        length = strlen(read_buf);
        if (read_buf[length - 1] == '\n') {
            read_buf[length - 1] = '\0';
        }
        nstrprintf_cat(output_pointer, "%s", read_buf);
    }
    status = pclose(fp);
    err = errno;
    if (status == -1) {
        n_log(LOG_ERR, "pclose( %s ) returned -1 , %s (%d)", cmd, strerror(err), err);
        if (WIFEXITED(status)) {
            n_log(LOG_ERR, "Child exited with RC=%d", WEXITSTATUS(status));
            (*ret) = WEXITSTATUS(status);
        }
        free_nstr(&output_pointer);
        return FALSE;
    }
    (*ret) = WEXITSTATUS(status);
    (*nstr_output) = output_pointer;
    return TRUE;
} /* n_popen( ... ) */

#ifndef __windows__
/**
 * @brief Handles SIGCHLD issues when forking
 * @param sig signal type handle
 */
void sigchld_handler(int sig) {
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
    n_log(LOG_DEBUG, "Signal %d", sig);
}

/**
 *@brief install signal SIGCHLD handler to reap zombie processes
 *@return TRUE or FALSE
 */
int sigchld_handler_installer() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;  // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        int error = errno;
        n_log(LOG_ERR, "sigaction error: %s", strerror(error));
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief log environment variables in syslog
 * @param loglevel the loglevel used to log the environment
 */
void log_environment(int loglevel) {
    extern char** environ;
    for (char** env = environ; *env != 0; env++) {
        n_log(loglevel, "env: %s", *env);
    }
}

/**
 *@brief Daemonize program
 *@return TRUE or FALSE
 */
int n_daemonize(void) {
    return n_daemonize_ex(N_DAEMON_NO_SIGCHLD_IGN | N_DAEMON_NO_SIGCHLD_HANDLER);
}

/**
 *@brief Daemonize program
 *@param mode flag made of a combination of N_DAEMON_NO_CLOSE, N_DAEMON_NO_STD_REDIRECT, N_DAEMON_NO_DOUBLE_FORK , N_DAEMON_NO_SETSID, N_DAEMON_NO_UMASK, N_DAEMON_NO_CHDIR, N_DAEMON_NO_SETSID, N_DAEMON_NO_SIGCHLD_IGN, N_DAEMON_NO_SIGCHLD_HANDLER or 0 for defaults
 *@return TRUE or FALSE
 */
int n_daemonize_ex(int mode) {
    int error = 0;

    /* Fork off the parent process */
    pid_t pid = fork();
    error = errno;
    if (pid < 0) {
        n_log(LOG_ERR, "Error: unable to fork child: %s", strerror(error));
        exit(1);
    }
    if (pid > 0) {
        n_log(LOG_NOTICE, "Child started successfuly !");
        exit(0);
    }

    if (!(mode & N_DAEMON_NO_SETSID)) {
        /* On success: The child process becomes session leader
         * setsid is detaching the process from the terminal */
        if (setsid() < 0) {
            error = errno;
            n_log(LOG_ERR, "Error: unable to set session leader with setsid(): %s", strerror(error));
            exit(1);
        } else {
            n_log(LOG_NOTICE, "Session leader set !");
        }
    }

    if (!(mode & N_DAEMON_NO_SIGCHLD_IGN)) {
        /* Ignore signal sent from child to parent process */
        signal(SIGCHLD, SIG_IGN);
    }

    if (!(mode & N_DAEMON_NO_SIGCHLD_HANDLER)) {
        /* catching signal */
        sigchld_handler_installer();
    }

    if (!(mode & N_DAEMON_NO_DOUBLE_FORK)) {
        /* Double fork to detach from the parent, and be adopted by init process */
        pid = fork();
        error = errno;
        if (pid < 0) {
            n_log(LOG_ERR, "Error: unable to double fork child: %s", strerror(error));
            exit(1);
        }
        if (pid > 0) {
            n_log(LOG_NOTICE, "Double fork child started successfuly !");
            exit(0);
        }
    }

    if (!(mode & N_DAEMON_NO_UMASK)) {
        /* reset umask */
        umask(0);
    }

    if (!(mode & N_DAEMON_NO_CHDIR)) {
        /* set working directory */
        if (chdir("/") < 0) {
            error = errno;
            n_log(LOG_ERR, "couldn't chdir() to '/': %s", strerror(error));
            return errno;
        }
    }

    if (!(mode & N_DAEMON_NO_CLOSE)) {
        /* Close all open file descriptors except standard ones (stdin, stdout, stderr) */
        for (int x = 3; x < sysconf(_SC_OPEN_MAX); x++) {
            close(x);
        }
    }

    if (!(mode & N_DAEMON_NO_STD_REDIRECT)) {
        /* redirect stdin,stdout,stderr to /dev/null */
        int fd = open("/dev/null", O_RDWR, 0);
        error = errno;
        if (fd != -1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) {
                close(fd);
            }
        } else {
            n_log(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
        }
    }

    return TRUE;
} /* n_daemonize(...) */

/**
 *@brief Non blocking system call
 *@param command to call
 *@param infp stdin file descriptor holder or NULL
 *@param outfp stdout file descriptor holder or NULL
 *@return The system command pid or -1
 */
pid_t system_nb(const char* command, int* infp, int* outfp) {
    __n_assert(command, return -1);

    int p_stdin[2] = {-1, -1};
    int p_stdout[2] = {-1, -1};
    pid_t pid = -1;

    if (outfp != NULL) {
        if (pipe(p_stdin) != 0)
            return -1;
    }
    if (infp != NULL) {
        if (pipe(p_stdout) != 0) {
            if (outfp != NULL) {
                close(p_stdin[0]);
                close(p_stdin[1]);
            }
            return (-1);
        }
    }

    pid = fork();

    if (pid < 0) {
        n_log(LOG_ERR, "Couldn't fork command %s", command);
        if (outfp != NULL) {
            close(p_stdin[0]);
            close(p_stdin[1]);
        }
        if (infp != NULL) {
            close(p_stdout[0]);
            close(p_stdout[1]);
        }
        return pid;
    }
    if (pid == 0) {
        if (outfp != NULL) {
            close(p_stdin[1]);
            dup2(p_stdin[0], 0);
        }
        if (infp != NULL) {
            close(p_stdout[0]);
            dup2(p_stdout[1], 1);
        }
        execlp("sh", "sh", "-c", command, (char*)NULL);
        // should never get here
        int error = errno;
        n_log(LOG_ERR, "%s:%d: exec failed: %s", __FILE__, __LINE__, strerror(error));
        exit(42);
    }
    if (infp != NULL) {
        close(p_stdout[1]);
        *infp = p_stdout[0];
    }
    if (outfp != NULL) {
        close(p_stdin[0]);
        *outfp = p_stdin[1];
    }
    return pid;
} /* system_nb */

#endif /* ifndef __windows__ */

/**
 *@brief store a hidden version of a string
 *@param buf the variable which will receive the string
 */

void N_HIDE_STR(char* buf, ...) {
    size_t it = 0;
    va_list args;

    va_start(args, buf);

    int arg = 0;

    while ((arg = va_arg(args, int))) {
        buf[it] = (char)arg;
        it++;
    }
    buf[it] = '\0';
    va_end(args);
} /* N_HIDE_STR */

/**
 * @brief get extension of path+filename
 * @param path the path + filename
 * @return a pointer to the first extension character or NULL
 */
char* n_get_file_extension(char path[]) {
    char* result = NULL;
    size_t i = 0, n = 0;

    __n_assert(path, return "");

    n = strlen(path);
    i = n - 1;
    while ((i > 0) && (path[i] != '.') && (path[i] != '/') && (path[i] != '\\')) {
        i--;
    }
    if ((i > 0) && (i < n - 1) && (path[i] == '.') && (path[i - 1] != '/') && (path[i - 1] != '\\')) {
        result = path + i;
    } else {
        result = path + n;
    }
    return result;
}
