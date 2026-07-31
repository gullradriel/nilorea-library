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
 *@file ex_clipboard.c
 *@brief n_clipboard example + self test: own the CLIPBOARD and PRIMARY selections,
 *       round-trip text through them, and prove a separate X client can fetch the
 *       text as text/plain;charset=utf-8 (the target Chromium requires) and query the
 *       served TARGETS - the case a UTF8_STRING-only backend fails.
 *@author Castagnier Mickael
 */

#include "nilorea/n_clipboard.h"
#include "nilorea/n_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg)                             \
    do {                                             \
        if (!(cond)) {                               \
            printf("ex_clipboard: FAIL: %s\n", msg); \
            failures++;                              \
        }                                            \
    } while (0)

#if defined(__linux__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <sys/select.h>

/* Fetch a target from a selection on a PRIVATE connection, which forces the request
 * through n_clipboard's serving thread (this process owns the selection). Returns a
 * malloc'd string or NULL. */
static char* fetch_target(const char* sel_name, const char* target_name) {
    Display* d = XOpenDisplay(NULL);
    Window w;
    Atom sel, target, prop;
    char* out = NULL;
    int fd, tries;
    if (!d)
        return NULL;
    w = XCreateSimpleWindow(d, RootWindow(d, DefaultScreen(d)), 0, 0, 1, 1, 0, 0, 0);
    sel = XInternAtom(d, sel_name, False);
    target = XInternAtom(d, target_name, False);
    prop = XInternAtom(d, "EX_CLIP_PROP", False);
    XConvertSelection(d, sel, target, prop, w, CurrentTime);
    XFlush(d);
    fd = ConnectionNumber(d);
    for (tries = 0; tries < 20 && !out; tries++) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        select(fd + 1, &fds, NULL, NULL, &tv);
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == SelectionNotify) {
                if (ev.xselection.property != None) {
                    Atom rt;
                    int rf;
                    unsigned long ni, ba;
                    unsigned char* data = NULL;
                    if (XGetWindowProperty(d, w, prop, 0, (~0L), False, AnyPropertyType, &rt, &rf, &ni, &ba, &data) == Success && data) {
                        out = malloc(ni + 1);
                        if (out) {
                            memcpy(out, data, ni);
                            out[ni] = '\0';
                        }
                        XFree(data);
                    }
                }
                tries = 20;
                break;
            }
        }
    }
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return out;
}
#endif

int main(void) {
    set_log_level(LOG_ERR);
    if (n_clipboard_init() != 0) {
        printf("ex_clipboard: SKIP (no X11 display)\n");
        return 0;
    }
    CHECK(n_clipboard_available() == 1, "available after init");

    /* own-selection round trip */
    n_clipboard_set(N_CLIPBOARD_CLIPBOARD, "hello clipboard");
    {
        char* c = n_clipboard_get(N_CLIPBOARD_CLIPBOARD);
        CHECK(c && strcmp(c, "hello clipboard") == 0, "clipboard round trip");
        free(c);
    }
    n_clipboard_set(N_CLIPBOARD_PRIMARY, "hello primary");
    {
        char* p = n_clipboard_get(N_CLIPBOARD_PRIMARY);
        CHECK(p && strcmp(p, "hello primary") == 0, "primary round trip");
        free(p);
    }

#if defined(__linux__)
    /* a separate X client fetches the text as the targets a strict consumer (Chromium)
       asks for, proving the serving thread answers them */
    {
        char* c = fetch_target("CLIPBOARD", "text/plain;charset=utf-8");
        CHECK(c && strcmp(c, "hello clipboard") == 0, "serve text/plain;charset=utf-8 (Chromium)");
        free(c);
        c = fetch_target("CLIPBOARD", "UTF8_STRING");
        CHECK(c && strcmp(c, "hello clipboard") == 0, "serve UTF8_STRING");
        free(c);
        c = fetch_target("PRIMARY", "STRING");
        CHECK(c && strcmp(c, "hello primary") == 0, "serve STRING on PRIMARY");
        free(c);
        /* TARGETS must be answered (Chromium queries it before pasting) */
        c = fetch_target("CLIPBOARD", "TARGETS");
        CHECK(c != NULL, "answer a TARGETS query");
        free(c);
    }
#endif

    n_clipboard_destroy();
    if (failures == 0)
        printf("ex_clipboard: all checks passed\n");
    else
        printf("ex_clipboard: %d checks FAILED\n", failures);
    return failures ? 1 : 0;
}
