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
