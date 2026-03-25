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
 *@file n_exceptions.c
 *@brief ExceptionContextList management for C
 *@author Castagnier Mickael
 *@version 1.0
 *@date 20/11/09
 */

#include <pthread.h>
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_exceptions.h"

__thread ExceptionContextList* __Exceptions = NULL;

/**
 *@brief Function to push an exception in the list
 */
void push_exception(void) {
    ExceptionContextList* _new = (ExceptionContextList*)malloc(sizeof(ExceptionContextList));

    if (_new) {
        _new->head = __Exceptions;
        __Exceptions = _new;
    } else {
        n_log(LOG_ERR, "Cannot even add a new exception context !!");
    }
} /* push_exception() */

/**
 *@brief Function to pop an exception in the list
 *@param ex Type of exception to pop
 */
void pop_exception(int ex) {
    if (!__Exceptions)
        return;
    ExceptionContextList* head = NULL;
    head = __Exceptions->head;
    Free(__Exceptions);
    __Exceptions = head;

    if (ex != NO_EXCEPTION) {
        if (__Exceptions) {
            longjmp(__Exceptions->context, ex);
        }
    }
} /* pop_exception */
