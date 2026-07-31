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
 *@file n_clipboard.h
 *@brief System clipboard: full X11 selection support on Linux, Win32 clipboard
 * on Windows/MinGW.
 *
 * On Linux/X11 a small backend owns the X11 CLIPBOARD and PRIMARY selections
 * itself and answers SelectionRequest with the full target set (UTF8_STRING,
 * STRING, TEXT, text/plain, text/plain;charset=utf-8, and TARGETS), so strict
 * consumers such as Chromium (which query TARGETS and want text/plain;charset=
 * utf-8) can paste text this process copies - which a UTF8_STRING-only backend
 * fails to satisfy. It also exposes the X11 PRIMARY selection (select-to-copy /
 * middle-click paste) that the ordinary clipboard API cannot reach. A background
 * thread on a dedicated X display connection serves requests promptly even while
 * the GUI is idle.
 *
 * On Windows (MinGW) N_CLIPBOARD_CLIPBOARD maps to the Win32 system clipboard
 * (CF_UNICODETEXT, UTF-8 <-> UTF-16 internally); Windows has no PRIMARY
 * selection, so N_CLIPBOARD_PRIMARY is a graceful no-op there (set does nothing,
 * get returns NULL). Text always crosses this API as UTF-8.
 *
 * On any other platform the backend is unavailable (n_clipboard_available()
 * returns 0) and the caller should fall back to its toolkit's own clipboard.
 *
 *@author Castagnier Mickael
 *@version 1.0
 *@date 20/07/2026
 */

#ifndef __N_CLIPBOARD_H
#define __N_CLIPBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/*! the Ctrl+C / Ctrl+V clipboard selection */
#define N_CLIPBOARD_CLIPBOARD 0
/*! the X11 PRIMARY selection (mouse select-to-copy / middle-click paste); a
 *  no-op on Windows, which has no PRIMARY selection */
#define N_CLIPBOARD_PRIMARY 1

/**
 * @brief Start the clipboard backend (idempotent; also started lazily on first use).
 * @return 0 on success, -1 when no backend is available on this platform.
 */
int n_clipboard_init(void);

/**
 * @brief Whether a real clipboard backend (X11 or Win32) is active on this platform.
 * @return 1 if available, 0 otherwise (caller should use its toolkit clipboard).
 */
int n_clipboard_available(void);

/**
 * @brief Own @p which selection and serve @p text to any app that requests it.
 * @param which N_CLIPBOARD_CLIPBOARD or N_CLIPBOARD_PRIMARY.
 * @param text The UTF-8 text to publish (copied). NULL/empty relinquishes it.
 * @return 0 on success, -1 on failure / no backend.
 */
int n_clipboard_set(int which, const char* text);

/**
 * @brief Fetch the current text of @p which selection from its owner.
 * @param which N_CLIPBOARD_CLIPBOARD or N_CLIPBOARD_PRIMARY.
 * @return A newly allocated UTF-8 string (free with free()), or NULL when empty,
 *         unavailable, or the owner did not answer in time.
 */
char* n_clipboard_get(int which);

/**
 * @brief Stop the backend thread and release the display connection.
 */
void n_clipboard_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* __N_CLIPBOARD_H */
