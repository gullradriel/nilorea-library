/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
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
