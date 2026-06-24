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

/**@file n_user.h
 * Windows headers order include
 *@author Castagnier Mickael
 *@version 1.0
 *@date 20/02/2006
 */

#ifndef N_WINDOWS_HEADERS
#define N_WINDOWS_HEADERS

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#if (_WIN32_WINNT < 0x0501)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
/* #pragma comment(lib, "ws2_32.lib") */
#endif

#ifdef __cplusplus
}
#endif

#endif
