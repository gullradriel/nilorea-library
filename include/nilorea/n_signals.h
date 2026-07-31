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

/**@file n_signals.h
 *  Signals general handling with stack printing, from https://gist.github.com/jvranish/4441299
 @author Castagnier Mickael
 *@version 1.0
 *@date 08/11/2018
 */

#ifndef __N_SIGNALS__
#define __N_SIGNALS__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup SIGNALS SIGNALS: signals handling and stack printing
  @addtogroup SIGNALS
  @{
  */

/*! Size of the signal handler alternate stack */
#define SIGALTSTACK_SIZE 65536
/*! Number of backtrace log lines */
#define MAX_STACK_FRAMES 32

/*! @brief install the backtrace signal handler */
void set_signal_handler(const char* progname);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __N_SIGNALS__ */
