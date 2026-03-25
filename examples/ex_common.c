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
 *@example ex_common.c
 *@brief Nilorea Library common api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 03/01/2019
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#ifndef __windows__
#include <sys/wait.h>
#endif

void usage(void) {
    fprintf(stderr,
            "     -v version\n"
            "     -V log level: LOG_INFO, LOG_NOTICE, LOG_ERR, LOG_DEBUG\n"
            "     -h help\n");
}

void process_args(int argc, char** argv) {
    int getoptret = 0,
        log_level = LOG_DEBUG; /* default log level */

    /* Arguments optionnels */
    /* -v version
     * -V log level
     * -h help
     */
    while ((getoptret = getopt(argc, argv, "hvV:")) != EOF) {
        switch (getoptret) {
            case 'v':
                fprintf(stderr, "Date de compilation : %s a %s.\n", __DATE__, __TIME__);
                exit(1);
            case 'V':
                if (!strcmp("LOG_NULL", optarg))
                    log_level = LOG_NULL;
                else if (!strcmp("LOG_NOTICE", optarg))
                    log_level = LOG_NOTICE;
                else if (!strcmp("LOG_INFO", optarg))
                    log_level = LOG_INFO;
                else if (!strcmp("LOG_ERR", optarg))
                    log_level = LOG_ERR;
                else if (!strcmp("LOG_DEBUG", optarg))
                    log_level = LOG_DEBUG;
                else {
                    fprintf(stderr, "%s n'est pas un niveau de log valide.\n", optarg);
                    exit(-1);
                }
                break;
            default:
            case '?': {
                if (optopt == 'V') {
                    fprintf(stderr, "\n      Missing log level\n");
                } else if (optopt == 'p') {
                    fprintf(stderr, "\n      Missing port\n");
                } else if (optopt != 's') {
                    fprintf(stderr, "\n      Unknow missing option %c\n", optopt);
                }
                usage();
                exit(1);
            }
            case 'h': {
                usage();
                exit(1);
            }
        }
    }
    set_log_level(log_level);
} /* void process_args( ... ) */

FORCE_INLINE void inlined_func(void) {
    n_log(LOG_DEBUG, "Inlined func");
}

int main(int argc, char** argv) {
    /* processing args and set log_level */
    process_args(argc, argv);

    inlined_func();

    n_log(LOG_INFO, "TRUE,true values: %d,%d / FALSE,false value: %d,%d", TRUE, true, FALSE, false);

    n_log(LOG_INFO, "_str(\"TEST\")=\"%s\" , _str(NULL)=\"%s\"", _str("TEST"), _str(NULL));
    n_log(LOG_INFO, "_strw(\"TEST\")=\"%s\" , _strw(NULL)=\"%s\"", _str("TEST"), _str(NULL));
    N_STR* nstr = char_to_nstr("TEST");
    n_log(LOG_INFO, "_nstr(nstrpointer)=\"%s\"", _nstr(nstr));
    n_log(LOG_INFO, "_nstrp(nstrpointer)=\"%s\"", _nstrp(nstr));
    free_nstr(&nstr);
    n_log(LOG_INFO, "_nstr(nstrpointer_freed)=\"%s\"", _nstr(nstr));
    n_log(LOG_INFO, "_nstrp(nstrpointer_freed)=\"%s\"", _nstrp(nstr));

    char* data = NULL;
    __n_assert(data, n_log(LOG_INFO, "data is NULL"));
    Malloc(data, char, 1024);
    __n_assert(data, n_log(LOG_INFO, "data is NULL"));
    Free(data);
    Malloc(data, char, 1024);
    if (Realloc(data, char, 2048) == FALSE) {
        n_log(LOG_ERR, "could not realloc data to 2048");
    }
    Free(data);
    Malloc(data, char, 1024);
    if (Reallocz(data, char, 1024, 2048) == FALSE) {
        n_log(LOG_ERR, "could not reallocz data to 2048");
    }
    FreeNoLog(data);
    Alloca(data, 2048);
    // Free( data ); alloca should not be free else it's double freeing on exit

    n_log(LOG_INFO, "next_odd(10):%d  , next_odd(11):%d", next_odd(10), next_odd(11));
    n_log(LOG_INFO, "next_even(10):%d  , next_even(11):%d", next_even(10), next_even(11));

    init_error_check();
    ifzero(0) endif;
    ifzero(1) endif;
    ifnull(data) endif;
    ifnull(NULL) endif;
    iffalse(FALSE) endif;
    iftrue(TRUE) endif;
    checkerror();
error:
    if (get_error()) {
        n_log(LOG_INFO, "got an error while processing test");
    } else {
        n_log(LOG_INFO, "All tests for checkerror() are OK");
    }

    if (file_exist(argv[0])) {
        n_log(LOG_INFO, "%s exists !", argv[0]);
    }

    char *dir = NULL, *name = NULL;
    dir = get_prog_dir();
    name = get_prog_name();
    n_log(LOG_INFO, "From %s/%s", dir, name);
    Free(dir);
    Free(name);

    N_STR* out = NULL;
    int ret = -1;
    if (n_popen("ls -ltr", 2048, (void*)&out, &ret) == TRUE) {
        n_log(LOG_INFO, "ls returned %d : %s", ret, _nstr(out));
    } else {
        n_log(LOG_ERR, "popen s returned an error");
    }
    free_nstr(&out);

    char hidden_str[47] = "";
    N_HIDE_STR(hidden_str, 'T', 'h', 'i', 's', ' ', 's', 't', 'r', 'i', 'n', 'g', ' ', 'w', 'i', 'l', 'l', ' ', 'b', 'e', ' ', 'h', 'i', 'd', 'd', 'e', 'n', ' ', 'i', 'n', ' ', 't', 'h', 'e', ' ', 'f', 'i', 'n', 'a', 'l', ' ', 'b', 'i', 'n', 'a', 'r', 'y', '\0');
    printf("hidden str:%s\n", hidden_str);

    /* test get_computer_name */
    char computer_name[256] = "";
    get_computer_name(computer_name, sizeof(computer_name));
    n_log(LOG_INFO, "computer name: %s", computer_name);

    /* test n_get_file_extension */
    const char* ext = n_get_file_extension("archive.tar.gz");
    n_log(LOG_INFO, "n_get_file_extension(\"archive.tar.gz\"): %s", _str(ext));
    ext = n_get_file_extension("noextension");
    n_log(LOG_INFO, "n_get_file_extension(\"noextension\"): %s", _str(ext));

    /* test log_environment */
    log_environment(LOG_DEBUG);

#ifndef __windows__
    /* test sigchld_handler_installer */
    if (sigchld_handler_installer() == TRUE) {
        n_log(LOG_INFO, "sigchld_handler_installer succeeded");
    }

    n_log(LOG_INFO, "before system_nb( sleep 3 )");
    int pid = system_nb("sleep 3", NULL, NULL);
    n_log(LOG_INFO, "after system_nb( sleep 3 )");
    n_log(LOG_INFO, "wait for nb sys call");
    wait(&pid);
    n_log(LOG_INFO, "done");
    /* n_daemonize() forks and detaches — disabled so ASan/LSan can check
     * the rest of the example for leaks (the grandchild's exit is invisible
     * to the sanitizer). */
    /* n_daemonize(); */
#endif

    exit(0);
}
