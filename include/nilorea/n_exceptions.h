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
 *@file n_exceptions.h
 *@brief Exception management for C
 *@author Castagnier Mickael
 *@version 1.0
 *@date 20/11/09
 */

#ifndef ____EXCEPTION___MANAGEMENT__FOR_C
#define ____EXCEPTION___MANAGEMENT__FOR_C

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup EXCEPTIONS EXCEPTIONS: C++ style try,catch,endtry,throw
   @addtogroup EXCEPTIONS
  @{
*/

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

/*! Possibly Throwed value, everything is fine */
#define NO_EXCEPTION 0
/*! Possibly Throwed value, we checked an out of bound operation */
#define ARRAY_EXCEPTION 2
/*! Possibly Throwed value, we check a divide by zero operation */
#define DIVZERO_EXCEPTION 4
/*! Possibly Throwed value, we checked an overflow in our arrays */
#define OVERFLOW_EXCEPTION 8
/*! Possibly Throwed value, we checked an error during a char * parsing */
#define PARSING_EXCEPTION 16

/*! General exception, we just detected an error an decided to go back where we're safe */
#define GENERAL_EXCEPTION (ARRAY_EXCEPTION | DIVZERO_EXCEPTION | OVERFLOW_EXCEPTION | PARSING_EXCEPTION)

/*! Exception stack structure */
typedef struct ExceptionContextList {
    /*! saving the current context */
    jmp_buf context;
    /*! pointer to the next stacked exception */
    struct ExceptionContextList* head;
} ExceptionContextList;

/*! Globale variable for exception stack management */
extern __thread ExceptionContextList* __Exceptions;

/*! @brief add an exception context to the stack */
void push_exception(void);
/*! @brief pop an exception context from the stack */
void pop_exception(int ex);

/*! Macro for replacing try */
#define Try                                     \
    {                                           \
        volatile int __exc_;                    \
        push_exception();                       \
        __exc_ = setjmp(__Exceptions->context); \
        if (__exc_ == NO_EXCEPTION) {
/*! Macro for replacing catch */
#define Catch(X)        \
    }                   \
    if (__exc_ & (X)) { \
        __exc_ = 0;

/*! Macro for replacing catch */
#define CatchAll()     \
    }                  \
    if (__exc_ != 0) { \
        __exc_ = 0;

/*! Macro helper for closing the try-catch block */
#define EndTry             \
    }                      \
    pop_exception(__exc_); \
    }

/*! Macro helper for adding exception throwing in custom functions.
    Note: longjmp with value 0 is replaced with 1 by the C standard,
    so Throw(NO_EXCEPTION) would actually trigger a catch. Avoid it. */
#define Throw(X)                                                         \
    do {                                                                 \
        int __exc_val_ = (X);                                            \
        n_log(LOG_ERR, "Exception thrown: %d", __exc_val_);              \
        if (__Exceptions) {                                              \
            longjmp(__Exceptions->context, __exc_val_ ? __exc_val_ : 1); \
        } else {                                                         \
            n_log(LOG_ERR, "Throw called outside Try block!");           \
            abort();                                                     \
        }                                                                \
    } while (0)

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef ____EXCEPTION___MANAGEMENT__FOR_C  */
