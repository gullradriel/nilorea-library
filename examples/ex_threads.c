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
 *@example ex_threads.c
 *@brief Nilorea Library thread pool api example
 *@author Castagnier Mickael
 *@version 1.0
 *@date 03/01/2019
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "nilorea/n_log.h"
#include "nilorea/n_time.h"
#include "nilorea/n_thread_pool.h"

void usage(void) {
    fprintf(stderr,
            "     -v version\n"
            "     -h help\n"
            "     -V LOG_LEVEL (LOG_DEBUG,INFO,NOTICE,ERR)\n");
}

void process_args(int argc, char** argv) {
    int getoptret = 0,
        log_level = LOG_ERR; /* default log level */

    while ((getoptret = getopt(argc, argv, "vhV:")) != EOF) {
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
                }
                usage();
                exit(1);
            }
            case 'h': {
                usage();
                exit(1);
            }
        } /* switch */
        set_log_level(log_level);
    }
} /* void process_args( ... ) */

void* occupy_thread(void* rest) {
    __n_assert(rest, return NULL);

    intptr_t sleep_value = (intptr_t)(rest);

    n_log(LOG_DEBUG, "Starting to sleep %d usecs on thread %lld", sleep_value, pthread_self());

    if (sleep_value < 1000000) {
        usleep((unsigned int)sleep_value);
    } else {
        usleep((unsigned int)((sleep_value) % 1000000));
        sleep((unsigned int)(sleep_value / 1000000));
    }

    n_log(LOG_DEBUG, "End of sleep %d usecs on thread %lld", sleep_value, pthread_self());

    return NULL;
}

int main(int argc, char** argv) {
    long int cores = get_nb_cpu_cores();
    int nb_active_threads = (cores > 0) ? (int)cores : 1;
    int nb_waiting_threads = 2 * nb_active_threads;
    int nb_total_threads = (nb_active_threads + nb_waiting_threads);

    set_log_level(LOG_INFO);

    // processing args and set log_level
    process_args(argc, argv);

    n_log(LOG_INFO, "Creating a new thread pool of %d active and %d waiting threads", nb_active_threads, nb_waiting_threads);
    THREAD_POOL* thread_pool = new_thread_pool((size_t)nb_active_threads, (size_t)nb_waiting_threads);

    n_log(LOG_INFO, "Adding new %d new tasks...", nb_total_threads);
    for (int it = 0; it < nb_total_threads; it++) {
        n_log(LOG_INFO, "adding task %d", it);
        // sleep time as a payload to the occupy_thread
        int sleep_value = 1 + rand() % 1000000;
        // add task and payload
        if (add_threaded_process(thread_pool, &occupy_thread, (void*)(intptr_t)sleep_value, NORMAL_PROC) == FALSE) {
            n_log(LOG_ERR, "Error adding client management to thread pool");
        }
    }
    n_log(LOG_INFO, "Adding tasks done. Waiting for pool thread to complete the tasks...");
    wait_for_threaded_pool(thread_pool);

    n_log(LOG_INFO, "Task completed. Destroying pool...");
    destroy_threaded_pool(&thread_pool, 1000);
    n_log(LOG_INFO, "Destroyed.");

    /* test SYNCED_PROC with start_threaded_pool and wait_for_synced_threaded_pool */
    n_log(LOG_INFO, "--- Synced thread pool test ---");
    thread_pool = new_thread_pool((size_t)nb_active_threads, (size_t)nb_waiting_threads);
    for (int it = 0; it < nb_active_threads; it++) {
        int sleep_value = 1 + rand() % 500000;
        if (add_threaded_process(thread_pool, &occupy_thread, (void*)(intptr_t)sleep_value, SYNCED_PROC) == FALSE) {
            n_log(LOG_ERR, "Error adding synced process %d", it);
        }
    }
    n_log(LOG_INFO, "Starting synced pool...");
    start_threaded_pool(thread_pool);

    n_log(LOG_INFO, "Waiting for synced pool...");
    wait_for_synced_threaded_pool(thread_pool);

    n_log(LOG_INFO, "Synced pool done. Testing refresh_thread_pool...");
    refresh_thread_pool(thread_pool);

    destroy_threaded_pool(&thread_pool, 1000);
    n_log(LOG_INFO, "All thread pool tests done.");

    exit(0);
} /* END_OF_MAIN() */
