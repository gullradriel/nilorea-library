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
 *@example ex_signals.c
 *@brief Nilorea Library signals api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 04/12/2019
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include "nilorea/n_signals.h"
#include "nilorea/n_log.h"

#include <libgen.h>

int divide_by_zero(void);
void cause_segfault(void);
int stack_overflow(void);
void infinite_loop(void);
void illegal_instruction(void);
void cause_calamity(void);

char* progname = NULL;

int main(int argc, char* argv[]) {
    (void)argc;
    progname = argv[0];

    // set_log_file( "ex_signals.log" );
    set_log_level(LOG_DEBUG);
    set_log_level(LOG_STDERR);
    // set_log_level( LOG_FILE );

    n_log(LOG_DEBUG, "set_signal_handler");
    set_signal_handler(basename(progname));

    n_log(LOG_DEBUG, "cause_calamity");
    cause_calamity();

    return 0;
}

void cause_calamity(void) {
    /* uncomment one of the following error conditions to cause a calamity of
       your choosing! */
    stack_overflow();
    (void)divide_by_zero();
    cause_segfault();
    assert(false);
    infinite_loop();
    illegal_instruction();
}

int divide_by_zero(void) {
    int a = 1;
    int b = 0;
    return a / b;
}

void cause_segfault(void) {
    int* p = (int*)0x12345678;
    *p = 0;
}

// that function was made to generate an error. Temporary disabling infinite recursion detection
#pragma GCC diagnostic push
#if defined(__GNUC__) && (__GNUC__ >= 12)
#pragma GCC diagnostic ignored "-Winfinite-recursion"
#endif
int stack_overflow(void) {
    int foo = 0;
    (void)foo;
    foo = stack_overflow();
    return rand() % 1000;
}
#pragma GCC diagnostic pop

/* break out with ctrl+c to test SIGINT handling */
void infinite_loop(void) {
    int a = 1;
    int b = 1;
    int c = 1;
    while (a == 1) {
        b = c;
        c = a;
        a = b;
    }
}

void illegal_instruction(void) {
    /* I couldn't find an easy way to cause this one, so I'm cheating */
    raise(SIGILL);
}
