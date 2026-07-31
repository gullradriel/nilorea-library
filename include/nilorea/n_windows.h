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
