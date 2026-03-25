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
 *@file n_thread_pool.c
 *@brief Thread pool functions
 *@author Castagnier Mickael
 *@version 1.0
 *@date 30/04/2014
 */

#include <unistd.h>
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_thread_pool.h"
#include "nilorea/n_time.h"

#ifdef __linux__
#include <sys/sysinfo.h>
#endif
#include <pthread.h>
#include <string.h>
#include <errno.h>

/**
 *@brief get number of core of current system
 * @return The number of cores or 0 if the system command is not supported
 */
long int get_nb_cpu_cores() {
    long int nb_procs = 0;
#ifdef __windows__
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    nb_procs = (long int)sysinfo.dwNumberOfProcessors;
#else
    nb_procs = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return nb_procs;
}

/**
 * @brief Internal thread pool processing function
 * @param param params of a threaded process
 * @return NULL when exiting
 */
void* thread_pool_processing_function(void* param) {
    THREAD_POOL_NODE* node = (THREAD_POOL_NODE*)param;

    if (!node) {
        n_log(LOG_ERR, "Thread fatal error, no valid payload found, exiting thread function !");
        pthread_exit(NULL);
        return NULL;
    }

    n_log(LOG_DEBUG, "Thread %ld started", node->thr);

    int thread_state = 0;
    do {
        n_log(LOG_DEBUG, "Thread pool processing func waiting");

        // note: direct procs will automatically post th_start
        sem_wait(&node->th_start);

        pthread_mutex_lock(&node->lock);
        thread_state = node->thread_state;
        pthread_mutex_unlock(&node->lock);

        if (thread_state == RUNNING_THREAD) {
            n_log(LOG_INFO, "Thread pool running proc %p", node->func);
            void* (*func_to_run)(void* param) = NULL;
            void* param_to_run = NULL;
            pthread_mutex_lock(&node->lock);
            node->state = RUNNING_PROC;
            func_to_run = node->func;
            param_to_run = node->param;
            pthread_mutex_unlock(&node->lock);

            if (func_to_run) {
                func_to_run(param_to_run);
            }
            n_log(LOG_INFO, "Thread pool end proc %p", func_to_run);

            pthread_mutex_lock(&node->lock);
            node->func = NULL;
            node->param = NULL;
            node->state = IDLE_PROC;
            int type = node->type;
            node->type = -1;
            // NORMAL_PROC or DIRECT_PROC do not need to post th_end
            if (type & SYNCED_PROC)
                sem_post(&node->th_end);
            pthread_mutex_unlock(&node->lock);

            refresh_thread_pool(node->thread_pool);
        }
    } while (thread_state != EXITING_THREAD);

    n_log(LOG_DEBUG, "Thread %ld exiting...", node->thr);

    pthread_mutex_lock(&node->lock);
    node->thread_state = EXITED_THREAD;
    pthread_mutex_unlock(&node->lock);

    n_log(LOG_DEBUG, "Thread %ld exited", node->thr);

    pthread_exit(NULL);

    return NULL;
} /* thread_pool_processing_function */

/**
 * @brief Create a new pool of nbmaxthr threads
 * @param nbmaxthr number of active threads in the pool
 * @param nb_max_waiting max number of waiting procs in the pool. Zero for no limit
 * @return NULL or a new thread pool object
 */
THREAD_POOL* new_thread_pool(size_t nbmaxthr, size_t nb_max_waiting) {
    THREAD_POOL* thread_pool = NULL;

    Malloc(thread_pool, THREAD_POOL, 1);
    if (!thread_pool)
        return NULL;

    thread_pool->max_threads = nbmaxthr;
    thread_pool->nb_max_waiting = nb_max_waiting;
    thread_pool->nb_actives = 0;

    thread_pool->thread_list = (THREAD_POOL_NODE**)malloc(nbmaxthr * sizeof(THREAD_POOL_NODE*));
    if (!thread_pool->thread_list) {
        Free(thread_pool);
        return NULL;
    }

    thread_pool->waiting_list = new_generic_list(MAX_LIST_ITEMS);
    if (!thread_pool->waiting_list) {
        n_log(LOG_ERR, "Unable to initialize wait list");
        Free(thread_pool->thread_list);
        Free(thread_pool);
        return NULL;
    }

    pthread_mutex_init(&thread_pool->lock, NULL);

    if (sem_init(&thread_pool->nb_tasks, 0, 1) == -1) {
        int error = errno;
        n_log(LOG_ERR, "sem_init failed : %s on &thread_pool -> nb_tasks", strerror(error));
        list_destroy(&thread_pool->waiting_list);
        pthread_mutex_destroy(&thread_pool->lock);
        Free(thread_pool->thread_list);
        Free(thread_pool);
        return NULL;
    }

    size_t it = 0;
    for (it = 0; it < nbmaxthr; it++) {
        Malloc(thread_pool->thread_list[it], THREAD_POOL_NODE, 1);
        thread_pool->thread_list[it]->type = -1;
        thread_pool->thread_list[it]->state = IDLE_PROC;
        thread_pool->thread_list[it]->thread_state = RUNNING_THREAD;
        thread_pool->thread_list[it]->thread_pool = thread_pool;

        if (sem_init(&thread_pool->thread_list[it]->th_start, 0, 0) == -1) {
            int error = errno;
            n_log(LOG_ERR, "sem_init failed : %s on &thread_pool -> thread_list[ %zu ] -> th_start", strerror(error), it);
            Free(thread_pool->thread_list[it]);
            goto cleanup_error;
        }
        if (sem_init(&thread_pool->thread_list[it]->th_end, 0, 0) == -1) {
            int error = errno;
            n_log(LOG_ERR, "sem_init failed : %s on &thread_pool -> thread_list[ %zu] -> th_end", strerror(error), it);
            sem_destroy(&thread_pool->thread_list[it]->th_start);
            Free(thread_pool->thread_list[it]);
            goto cleanup_error;
        }

        thread_pool->thread_list[it]->func = NULL;
        thread_pool->thread_list[it]->param = NULL;

        pthread_mutex_init(&thread_pool->thread_list[it]->lock, NULL);

        if (pthread_create(&thread_pool->thread_list[it]->thr, NULL, thread_pool_processing_function, (void*)thread_pool->thread_list[it]) != 0) {
            n_log(LOG_ERR, "pthread_create failed : %s for it %zu", strerror(errno), it);
            pthread_mutex_destroy(&thread_pool->thread_list[it]->lock);
            sem_destroy(&thread_pool->thread_list[it]->th_start);
            sem_destroy(&thread_pool->thread_list[it]->th_end);
            Free(thread_pool->thread_list[it]);
            goto cleanup_error;
        }
    }
    return thread_pool;

cleanup_error:
    for (size_t j = 0; j < it; j++) {
        pthread_mutex_lock(&thread_pool->thread_list[j]->lock);
        thread_pool->thread_list[j]->thread_state = EXITING_THREAD;
        sem_post(&thread_pool->thread_list[j]->th_start);
        pthread_mutex_unlock(&thread_pool->thread_list[j]->lock);
        pthread_join(thread_pool->thread_list[j]->thr, NULL);
        pthread_mutex_destroy(&thread_pool->thread_list[j]->lock);
        sem_destroy(&thread_pool->thread_list[j]->th_start);
        sem_destroy(&thread_pool->thread_list[j]->th_end);
        Free(thread_pool->thread_list[j]);
    }
    list_destroy(&thread_pool->waiting_list);
    sem_destroy(&thread_pool->nb_tasks);
    pthread_mutex_destroy(&thread_pool->lock);
    Free(thread_pool->thread_list);
    Free(thread_pool);
    return NULL;
} /* new_thread_pool */

/**
 *@brief add a function and params to a thread pool
 *@param thread_pool The target thread pool
 *@param func_ptr The function pointer to launch
 *@param param Eventual parameter struct to pass to the function
 *@param mode NORMAL_PROC: add to an available active thread or queue in the waiting list. SYNCED_PROC: add to an available thread and wait for a start call, no waiting list. DIRECT_PROC: add to an available thread, no waiting list.
 *@return TRUE or FALSE
 */
int add_threaded_process(THREAD_POOL* thread_pool, void* (*func_ptr)(void* param), void* param, int mode) {
    if (!thread_pool) {
        n_log(LOG_ERR, "thread_pool is not allocated, can't add processes to it !");
        return FALSE;
    }

    if (!thread_pool->thread_list) {
        n_log(LOG_ERR, "thread_pool thread_list is not allocated, can't add processes to it !");
        return FALSE;
    }

    /* validate that exactly one of NORMAL_PROC, SYNCED_PROC, DIRECT_PROC is set */
    int proc_mode = mode & (NORMAL_PROC | SYNCED_PROC | DIRECT_PROC);
    if (proc_mode != NORMAL_PROC && proc_mode != SYNCED_PROC && proc_mode != DIRECT_PROC) {
        n_log(LOG_ERR, "invalid mode %d: exactly one of NORMAL_PROC, SYNCED_PROC, DIRECT_PROC must be set", mode);
        return FALSE;
    }

    if (!(mode & NO_LOCK)) pthread_mutex_lock(&thread_pool->lock);

    size_t it = 0;
    while (it < thread_pool->max_threads) {
        pthread_mutex_lock(&thread_pool->thread_list[it]->lock);
        if (thread_pool->thread_list[it]->thread_state == RUNNING_THREAD && thread_pool->thread_list[it]->state == IDLE_PROC) {
            break;
        }
        pthread_mutex_unlock(&thread_pool->thread_list[it]->lock);
        it++;
    }
    // we have a free thread slot, and the lock on it
    if (it < thread_pool->max_threads) {
        if (mode & NORMAL_PROC || mode & DIRECT_PROC || mode & SYNCED_PROC) {
            thread_pool->thread_list[it]->func = func_ptr;
            thread_pool->thread_list[it]->param = param;
            thread_pool->thread_list[it]->state = WAITING_PROC;
            thread_pool->thread_list[it]->type = mode;
        } else {
            n_log(LOG_ERR, "unknown mode %d for thread %zu", mode, it);
            pthread_mutex_unlock(&thread_pool->thread_list[it]->lock);
            if (!(mode & NO_LOCK))
                pthread_mutex_unlock(&thread_pool->lock);
            return FALSE;
        }
        if (mode & NORMAL_PROC || mode & DIRECT_PROC)
            sem_post(&thread_pool->thread_list[it]->th_start);
        pthread_mutex_unlock(&thread_pool->thread_list[it]->lock);
        n_log(LOG_DEBUG, "proc %p(%p) added on thread %zu", func_ptr, param, it);
    } else {
        // all thread are occupied -> test waiting lists. not holding thread_list[ it ] lock because it was obligatory unlocked before

        // if already coming from queue, or if it should be part of a synced start, do not re-add && return FALSE
        // it's only an error if SYNCED_PROC mode
        int cancel_and_return = FALSE;
        if (mode & NO_QUEUE) {
            n_log(LOG_DEBUG, "Thread pool active threads are all busy and mode is NO_QUEUE, cannot add %p(%p) to pool %p", func_ptr, param, thread_pool);
            cancel_and_return = TRUE;
        } else if (mode & SYNCED_PROC) {
            n_log(LOG_ERR, "Thread pool active threads are all busy, cannot add SYNCED_PROC %p(%p) to pool %p", func_ptr, param, thread_pool);
            cancel_and_return = TRUE;
        } else if (mode & DIRECT_PROC) {
            n_log(LOG_ERR, "Thread pool active threads are all busy, cannot add DIRECT_PROC %p(%p) to pool %p", func_ptr, param, thread_pool);
            cancel_and_return = TRUE;
        }

        if (cancel_and_return) {
            if (!(mode & NO_LOCK))
                pthread_mutex_unlock(&thread_pool->lock);
            return FALSE;
        }

        // try adding to wait list
        if (thread_pool->nb_max_waiting == 0 || (thread_pool->waiting_list->nb_items < thread_pool->nb_max_waiting)) {
            THREAD_WAITING_PROC* proc = NULL;
            Malloc(proc, THREAD_WAITING_PROC, 1);
            if (!proc) {
                n_log(LOG_ERR, "Failed to allocate THREAD_WAITING_PROC");
                if (!(mode & NO_LOCK)) pthread_mutex_unlock(&thread_pool->lock);
                return FALSE;
            }
            proc->func = func_ptr;
            proc->param = param;
            list_push(thread_pool->waiting_list, proc, free);
            n_log(LOG_DEBUG, "Adding %p %p to waitlist", proc->func, proc->param);
        } else {
            n_log(LOG_ERR, "proc %p(%p) was dropped from waitlist because waitlist of thread pool %p is full", func_ptr, param, thread_pool);
            if (!(mode & NO_LOCK)) pthread_mutex_unlock(&thread_pool->lock);
            return FALSE;
        }
    }

    /* consume idle signal: pool now has work in progress */
    if (sem_trywait(&thread_pool->nb_tasks) != 0 && errno != EAGAIN) {
        int error = errno;
        n_log(LOG_ERR, "sem_trywait nb_tasks error in thread_pool %p: %s", thread_pool, strerror(error));
    }

    if (!(mode & NO_LOCK)) pthread_mutex_unlock(&thread_pool->lock);

    return TRUE;
} /* add_threaded_process */

/**
 * @brief Launch the process waiting for execution in the thread pool
 * @param thread_pool The thread pool to launch
 * @return TRUE or FALSE
 */
int start_threaded_pool(THREAD_POOL* thread_pool) {
    if (!thread_pool)
        return FALSE;

    if (!thread_pool->thread_list)
        return FALSE;

    int retval = TRUE;

    pthread_mutex_lock(&thread_pool->lock);
    for (size_t it = 0; it < thread_pool->max_threads; it++) {
        int to_run = 0;
        pthread_mutex_lock(&thread_pool->thread_list[it]->lock);
        if ((thread_pool->thread_list[it]->type & SYNCED_PROC) && thread_pool->thread_list[it]->state == WAITING_PROC) {
            to_run = 1;
        }
        pthread_mutex_unlock(&thread_pool->thread_list[it]->lock);
        if (to_run == 1) {
            if (sem_post(&thread_pool->thread_list[it]->th_start) != 0) {
                int error = errno;
                n_log(LOG_ERR, "sem_post th_start error in thread_pool %p , thread_list[ %zu ] : %s", thread_pool, it, strerror(error));
                retval = FALSE;
            }
        }
    }
    pthread_mutex_unlock(&thread_pool->lock);

    return retval;
} /* start_threaded_pool */

/**
 * @brief wait for all the launched process, blocking but light on the CPU as there is no polling
 * @param thread_pool The thread pool to wait
 * @return TRUE or FALSE
 */
int wait_for_synced_threaded_pool(THREAD_POOL* thread_pool) {
    __n_assert(thread_pool, return FALSE);
    __n_assert(thread_pool->thread_list, return FALSE);

    int retval = TRUE;
    for (size_t it = 0; it < thread_pool->max_threads; it++) {
        int is_synced = 0;
        pthread_mutex_lock(&thread_pool->thread_list[it]->lock);
        if (thread_pool->thread_list[it]->type & SYNCED_PROC) {
            is_synced = 1;
        }
        pthread_mutex_unlock(&thread_pool->thread_list[it]->lock);

        if (is_synced) {
            if (sem_wait(&thread_pool->thread_list[it]->th_end) == -1) {
                int error = errno;
                n_log(LOG_ERR, "sem_wait th_end error in thread_pool %p , thread_list[ %zu ] : %s", thread_pool, it, strerror(error));
                retval = FALSE;
            }
        }
    }
    return retval;
} /* wait_for_synced_threaded_pool */

/**
 * @brief Wait for the thread pool to become idle (no active threads, empty waiting list), blocking without polling
 * @param thread_pool The thread pool to wait on
 * @return TRUE or FALSE
 */
int wait_for_threaded_pool(THREAD_POOL* thread_pool) {
    __n_assert(thread_pool, return FALSE);
    __n_assert(thread_pool->thread_list, return FALSE);

    /* kick off draining the wait list before blocking */
    refresh_thread_pool(thread_pool);

    while (sem_wait(&thread_pool->nb_tasks) == -1) {
        int error = errno;
        if (error == EINTR) {
            continue;
        }
        n_log(LOG_ERR, "sem_wait nb_tasks error in thread_pool %p: %s", thread_pool, strerror(error));
        return FALSE;
    }
    /* restore the idle signal so it can be waited on again or checked */
    sem_post(&thread_pool->nb_tasks);
    return TRUE;
} /* wait_for_threaded_pool */

/**
 * @brief delete a thread_pool, exit the threads and free the structs
 * @param pool The THREAD_POOL *object to kill
 * @param delay time in usec between each check
 * @return TRUE or FALSE
 */
int destroy_threaded_pool(THREAD_POOL** pool, unsigned int delay) {
    __n_assert(pool && (*pool), return FALSE);
    __n_assert((*pool)->thread_list, return FALSE);

    int state = 0, DONE = 0;
    int max_retries = 1000;

    while (!DONE) {
        DONE = 1;
        pthread_mutex_lock(&(*pool)->lock);
        for (size_t it = 0; it < (*pool)->max_threads; it++) {
            pthread_mutex_lock(&(*pool)->thread_list[it]->lock);
            state = (*pool)->thread_list[it]->state;
            int thread_state = (*pool)->thread_list[it]->thread_state;
            pthread_mutex_unlock(&(*pool)->thread_list[it]->lock);

            if (thread_state == EXITING_THREAD || thread_state == EXITED_THREAD) {
                continue;
            }

            if (state == IDLE_PROC) {
                pthread_mutex_lock(&(*pool)->thread_list[it]->lock);
                (*pool)->thread_list[it]->thread_state = EXITING_THREAD;
                sem_post(&(*pool)->thread_list[it]->th_start);
                pthread_mutex_unlock(&(*pool)->thread_list[it]->lock);
            } else {
                DONE = 0;
            }
        }
        pthread_mutex_unlock(&(*pool)->lock);

        if (!DONE) {
            max_retries--;
            if (max_retries <= 0) {
                pthread_mutex_lock(&(*pool)->lock);
                for (size_t it = 0; it < (*pool)->max_threads; it++) {
                    pthread_mutex_lock(&(*pool)->thread_list[it]->lock);
                    if ((*pool)->thread_list[it]->thread_state != EXITING_THREAD && (*pool)->thread_list[it]->thread_state != EXITED_THREAD) {
                        (*pool)->thread_list[it]->thread_state = EXITING_THREAD;
                        sem_post(&(*pool)->thread_list[it]->th_start);
                    }
                    pthread_mutex_unlock(&(*pool)->thread_list[it]->lock);
                }
                pthread_mutex_unlock(&(*pool)->lock);
                break;
            }
        }

        u_sleep(delay);
    }

    /* join threads without holding pool lock to avoid deadlock:
     * threads may call refresh_thread_pool() which needs pool->lock */
    for (size_t it = 0; it < (*pool)->max_threads; it++) {
        pthread_join((*pool)->thread_list[it]->thr, NULL);
    }

    /* all threads have exited, safe to clean up without locking */
    for (size_t it = 0; it < (*pool)->max_threads; it++) {
        pthread_mutex_destroy(&(*pool)->thread_list[it]->lock);
        sem_destroy(&(*pool)->thread_list[it]->th_start);
        sem_destroy(&(*pool)->thread_list[it]->th_end);
        Free((*pool)->thread_list[it]);
    }
    Free((*pool)->thread_list);
    list_destroy(&(*pool)->waiting_list);

    sem_destroy(&(*pool)->nb_tasks);

    pthread_mutex_destroy(&(*pool)->lock);

    Free((*pool));

    return TRUE;
} /* destroy_threaded_pool */

/**
 * @brief try to add some waiting DIRECT_PROCs on some free thread slots, else do nothing
 * @param thread_pool The thread pool to refresh
 * @return TRUE or FALSE
 */
int refresh_thread_pool(THREAD_POOL* thread_pool) {
    __n_assert(thread_pool, return FALSE);
    __n_assert(thread_pool->waiting_list, return FALSE);

    /* Trying to empty the wait list */
    int push_status = 0;
    pthread_mutex_lock(&thread_pool->lock);
    if (thread_pool->waiting_list && thread_pool->waiting_list->start)
        push_status = 1;
    while (push_status == 1) {
        LIST_NODE* node = thread_pool->waiting_list->start;
        LIST_NODE* next_node = NULL;
        if (node && node->ptr) {
            THREAD_WAITING_PROC* proc = (THREAD_WAITING_PROC*)node->ptr;
            if (proc) {
                if (add_threaded_process(thread_pool, proc->func, proc->param, NORMAL_PROC | NO_QUEUE | NO_LOCK) == TRUE) {
                    THREAD_WAITING_PROC* procptr = NULL;
                    next_node = node->next;
                    procptr = remove_list_node(thread_pool->waiting_list, node, THREAD_WAITING_PROC);
                    n_log(LOG_DEBUG, "waitlist: adding %p,%p to %p", procptr->func, procptr->param, thread_pool);
                    Free(procptr);
                    node = next_node;
                } else {
                    n_log(LOG_DEBUG, "waitlist: cannot add proc %p from waiting list of %p, all active threads are busy !", proc, thread_pool);
                    push_status = 0;
                }
            } else {
                n_log(LOG_ERR, "waitlist: trying to add invalid NULL proc on thread pool %p !", thread_pool);
                push_status = 0;
            }
        } else {
            push_status = 0;
        }
    }  // while( push_status == 1 )

    // update statistics: count threads with assigned work (waiting to run or running)
    thread_pool->nb_actives = 0;
    for (size_t it = 0; it < thread_pool->max_threads; it++) {
        pthread_mutex_lock(&thread_pool->thread_list[it]->lock);
        if (thread_pool->thread_list[it]->state == RUNNING_PROC || thread_pool->thread_list[it]->state == WAITING_PROC)
            thread_pool->nb_actives++;
        pthread_mutex_unlock(&thread_pool->thread_list[it]->lock);
    }

    /* signal idle: no active/waiting threads and waiting list is empty */
    if (thread_pool->nb_actives == 0 && thread_pool->waiting_list && thread_pool->waiting_list->nb_items == 0) {
        int value = 0;
        sem_getvalue(&thread_pool->nb_tasks, &value);
        if (value == 0) {
            sem_post(&thread_pool->nb_tasks);
        }
    }

    pthread_mutex_unlock(&thread_pool->lock);

    return TRUE;
}  // refresh_thread_pool()
