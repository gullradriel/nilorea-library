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
 *@file n_clipboard.c
 *@brief System clipboard: X11 selections on Linux, Win32 clipboard on Windows
 * (see n_clipboard.h).
 */

#include "nilorea/n_clipboard.h"
#include "nilorea/n_log.h"

#include <stdlib.h>
#include <string.h>

/* Android is __linux__ too, but the NDK ships no X11: fall through to the
 * unavailable stub so callers use their toolkit clipboard instead. */
#if defined(__linux__) && !defined(__ANDROID__)

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <pthread.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/*! duplicate n bytes into a fresh NUL-terminated string (avoids strndup feature-macro fuss) */
static char* clip_dupn(const char* s, size_t n) {
    char* out = malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

/*! owner connection: serves SelectionRequest for the selections we own */
static Display* s_dpy = NULL;
/*! unmapped window that owns the selections and receives their events */
static Window s_win = 0;
static pthread_t s_thread;
static int s_running = 0;
static int s_ready = 0;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
/*! published text per selection ([0]=CLIPBOARD, [1]=PRIMARY); NULL = we do not own it */
static char* s_text[2] = {NULL, NULL};

/* interned atoms */
static Atom A_CLIPBOARD, A_TARGETS, A_UTF8, A_TEXT, A_TEXTPLAIN, A_TEXTPLAIN_UTF8, A_INCR;

/*! X atom of a selection index */
static Atom clip_sel_atom(int which) {
    return (which == N_CLIPBOARD_PRIMARY) ? XA_PRIMARY : A_CLIPBOARD;
}

/*! selection index of an X atom, or -1 */
static int clip_sel_index(Atom a) {
    if (a == XA_PRIMARY)
        return N_CLIPBOARD_PRIMARY;
    if (a == A_CLIPBOARD)
        return N_CLIPBOARD_CLIPBOARD;
    return -1;
}

/*! whether target atom @p t is one of the text formats we serve */
static int clip_is_text_target(Atom t) {
    return t == A_UTF8 || t == XA_STRING || t == A_TEXT || t == A_TEXTPLAIN || t == A_TEXTPLAIN_UTF8;
}

/*! answer a SelectionRequest from another client with our published text or TARGETS */
static void clip_answer_request(const XSelectionRequestEvent* req) {
    XSelectionEvent resp;
    int idx = clip_sel_index(req->selection);
    resp.type = SelectionNotify;
    resp.display = req->display;
    resp.requestor = req->requestor;
    resp.selection = req->selection;
    resp.target = req->target;
    resp.time = req->time;
    resp.property = None; /* refused unless we fill it below */

    if (idx >= 0) {
        if (req->target == A_TARGETS) {
            Atom targets[6];
            targets[0] = A_TARGETS;
            targets[1] = A_UTF8;
            targets[2] = XA_STRING;
            targets[3] = A_TEXT;
            targets[4] = A_TEXTPLAIN;
            targets[5] = A_TEXTPLAIN_UTF8;
            XChangeProperty(s_dpy, req->requestor, req->property, XA_ATOM, 32, PropModeReplace,
                            (const unsigned char*)targets, (int)(sizeof(targets) / sizeof(targets[0])));
            resp.property = req->property;
        } else if (clip_is_text_target(req->target)) {
            const char* txt;
            pthread_mutex_lock(&s_lock);
            txt = s_text[idx];
            if (txt) {
                XChangeProperty(s_dpy, req->requestor, req->property, req->target, 8, PropModeReplace,
                                (const unsigned char*)txt, (int)strlen(txt));
                resp.property = req->property;
            }
            pthread_mutex_unlock(&s_lock);
        }
    }
    XSendEvent(s_dpy, req->requestor, False, 0, (XEvent*)&resp);
    XFlush(s_dpy);
}

/*! background loop: serve selection requests promptly even while the GUI is idle */
static void* clip_thread_main(void* arg) {
    (void)arg;
    int fd = ConnectionNumber(s_dpy);
    while (__atomic_load_n(&s_running, __ATOMIC_ACQUIRE)) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 200000; /* wake up to poll the stop flag */
        select(fd + 1, &fds, NULL, NULL, &tv);
        while (XPending(s_dpy)) {
            XEvent ev;
            XNextEvent(s_dpy, &ev);
            if (ev.type == SelectionRequest) {
                clip_answer_request(&ev.xselectionrequest);
            } else if (ev.type == SelectionClear) {
                int idx = clip_sel_index(ev.xselectionclear.selection);
                if (idx >= 0) {
                    pthread_mutex_lock(&s_lock);
                    free(s_text[idx]);
                    s_text[idx] = NULL;
                    pthread_mutex_unlock(&s_lock);
                }
            }
        }
    }
    return NULL;
}

int n_clipboard_init(void) {
    if (s_ready)
        return 0;
    s_dpy = XOpenDisplay(NULL);
    if (!s_dpy) {
        n_log(LOG_INFO, "n_clipboard: no X11 display, clipboard backend disabled");
        return -1;
    }
    s_win = XCreateSimpleWindow(s_dpy, RootWindow(s_dpy, DefaultScreen(s_dpy)), 0, 0, 1, 1, 0, 0, 0);
    A_CLIPBOARD = XInternAtom(s_dpy, "CLIPBOARD", False);
    A_TARGETS = XInternAtom(s_dpy, "TARGETS", False);
    A_UTF8 = XInternAtom(s_dpy, "UTF8_STRING", False);
    A_TEXT = XInternAtom(s_dpy, "TEXT", False);
    A_TEXTPLAIN = XInternAtom(s_dpy, "text/plain", False);
    A_TEXTPLAIN_UTF8 = XInternAtom(s_dpy, "text/plain;charset=utf-8", False);
    A_INCR = XInternAtom(s_dpy, "INCR", False);
    (void)A_INCR;
    __atomic_store_n(&s_running, 1, __ATOMIC_RELEASE);
    if (pthread_create(&s_thread, NULL, clip_thread_main, NULL) != 0) {
        __atomic_store_n(&s_running, 0, __ATOMIC_RELEASE);
        XDestroyWindow(s_dpy, s_win);
        XCloseDisplay(s_dpy);
        s_dpy = NULL;
        n_log(LOG_ERR, "n_clipboard: could not start the serving thread");
        return -1;
    }
    s_ready = 1;
    n_log(LOG_INFO, "n_clipboard: X11 clipboard backend ready");
    return 0;
}

int n_clipboard_available(void) {
    if (!s_ready)
        n_clipboard_init();
    return s_ready;
}

int n_clipboard_set(int which, const char* text) {
    if (which != N_CLIPBOARD_CLIPBOARD && which != N_CLIPBOARD_PRIMARY)
        return -1;
    if (!n_clipboard_available())
        return -1;
    pthread_mutex_lock(&s_lock);
    free(s_text[which]);
    s_text[which] = (text && text[0]) ? clip_dupn(text, strlen(text)) : NULL;
    pthread_mutex_unlock(&s_lock);
    if (s_text[which])
        XSetSelectionOwner(s_dpy, clip_sel_atom(which), s_win, CurrentTime);
    else
        XSetSelectionOwner(s_dpy, clip_sel_atom(which), None, CurrentTime);
    XFlush(s_dpy);
    return 0;
}

char* n_clipboard_get(int which) {
    Display* d;
    Window w;
    Atom sel, prop;
    int fd, tries;
    char* result = NULL;
    if (which != N_CLIPBOARD_CLIPBOARD && which != N_CLIPBOARD_PRIMARY)
        return NULL;
    if (!n_clipboard_available())
        return NULL;
    /* fast path: we own it, so hand back our own copy without a round trip */
    pthread_mutex_lock(&s_lock);
    if (s_text[which]) {
        result = clip_dupn(s_text[which], strlen(s_text[which]));
        pthread_mutex_unlock(&s_lock);
        return result;
    }
    pthread_mutex_unlock(&s_lock);
    /* another client owns it: request the text on a private connection so the owner
       thread's connection is untouched */
    d = XOpenDisplay(NULL);
    if (!d)
        return NULL;
    w = XCreateSimpleWindow(d, RootWindow(d, DefaultScreen(d)), 0, 0, 1, 1, 0, 0, 0);
    sel = (which == N_CLIPBOARD_PRIMARY) ? XA_PRIMARY : XInternAtom(d, "CLIPBOARD", False);
    prop = XInternAtom(d, "NIL_CLIPBOARD_TARGET", False);
    if (XGetSelectionOwner(d, sel) == None) {
        XDestroyWindow(d, w);
        XCloseDisplay(d);
        return NULL;
    }
    XConvertSelection(d, sel, XInternAtom(d, "UTF8_STRING", False), prop, w, CurrentTime);
    XFlush(d);
    fd = ConnectionNumber(d);
    for (tries = 0; tries < 20 && !result; tries++) { /* up to ~2s */
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
                    Atom rtype = None;
                    int rfmt = 0;
                    unsigned long nitems = 0, after = 0;
                    unsigned char* data = NULL;
                    if (XGetWindowProperty(d, w, prop, 0, (~0L), False, AnyPropertyType, &rtype, &rfmt,
                                           &nitems, &after, &data) == Success &&
                        data) {
                        result = clip_dupn((const char*)data, nitems);
                    }
                    if (data)
                        XFree(data);
                    XDeleteProperty(d, w, prop);
                }
                tries = 20; /* got the reply (even if empty): stop waiting */
                break;
            }
        }
    }
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return result;
}

void n_clipboard_destroy(void) {
    int i;
    if (!s_ready)
        return;
    __atomic_store_n(&s_running, 0, __ATOMIC_RELEASE);
    pthread_join(s_thread, NULL);
    if (s_dpy) {
        XDestroyWindow(s_dpy, s_win);
        XCloseDisplay(s_dpy);
        s_dpy = NULL;
    }
    for (i = 0; i < 2; i++) {
        free(s_text[i]);
        s_text[i] = NULL;
    }
    s_ready = 0;
}

#elif defined(__windows__) /* Win32 system clipboard (CLIPBOARD only, no PRIMARY selection) */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pthread.h>

/* Windows has a single system clipboard and no X11-style PRIMARY selection, so
 * N_CLIPBOARD_PRIMARY is a graceful no-op here (set does nothing, get returns
 * NULL); N_CLIPBOARD_CLIPBOARD maps to the Win32 clipboard via CF_UNICODETEXT.
 * The OS owns the data once SetClipboardData succeeds, so no serving thread is
 * needed. Text crosses this API as UTF-8 and is converted to/from UTF-16 here.
 * A process-local mutex serialises access so two threads in this process do not
 * fight over OpenClipboard (which is single-open per process). */

static int s_ready = 0;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/*! open the clipboard, retrying briefly since another process may hold it */
static int clip_open(void) {
    int tries;
    for (tries = 0; tries < 10; tries++) {
        if (OpenClipboard(NULL))
            return 1;
        Sleep(5);
    }
    return 0;
}

int n_clipboard_init(void) {
    s_ready = 1; /* the Win32 clipboard is always available */
    n_log(LOG_INFO, "n_clipboard: Win32 clipboard backend ready");
    return 0;
}

int n_clipboard_available(void) {
    if (!s_ready)
        n_clipboard_init();
    return s_ready;
}

int n_clipboard_set(int which, const char* text) {
    int wlen, rc = -1;
    HGLOBAL hmem;
    if (which != N_CLIPBOARD_CLIPBOARD && which != N_CLIPBOARD_PRIMARY)
        return -1;
    if (!n_clipboard_available())
        return -1;
    /* no PRIMARY selection on Windows: accept the call but do nothing */
    if (which == N_CLIPBOARD_PRIMARY)
        return 0;
    pthread_mutex_lock(&s_lock);
    if (!clip_open()) {
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    /* empty text relinquishes the clipboard: it is now cleared, nothing to set */
    if (!text || !text[0]) {
        CloseClipboard();
        pthread_mutex_unlock(&s_lock);
        return 0;
    }
    wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0); /* includes the NUL */
    if (wlen <= 0) {
        CloseClipboard();
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    hmem = GlobalAlloc(GMEM_MOVEABLE, (size_t)wlen * sizeof(wchar_t));
    if (hmem) {
        wchar_t* wbuf = (wchar_t*)GlobalLock(hmem);
        if (wbuf) {
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, wlen);
            GlobalUnlock(hmem);
            if (SetClipboardData(CF_UNICODETEXT, hmem)) {
                rc = 0; /* the system owns hmem now, do not free it */
            } else {
                GlobalFree(hmem); /* still ours on failure */
            }
        } else {
            GlobalFree(hmem);
        }
    }
    CloseClipboard();
    pthread_mutex_unlock(&s_lock);
    if (rc != 0)
        n_log(LOG_ERR, "n_clipboard: failed to set the Win32 clipboard");
    return rc;
}

char* n_clipboard_get(int which) {
    HANDLE h;
    char* out = NULL;
    if (which != N_CLIPBOARD_CLIPBOARD && which != N_CLIPBOARD_PRIMARY)
        return NULL;
    if (!n_clipboard_available())
        return NULL;
    if (which == N_CLIPBOARD_PRIMARY)
        return NULL; /* no PRIMARY selection on Windows */
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT))
        return NULL;
    pthread_mutex_lock(&s_lock);
    if (!clip_open()) {
        pthread_mutex_unlock(&s_lock);
        return NULL;
    }
    h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* wptr = (const wchar_t*)GlobalLock(h);
        if (wptr) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wptr, -1, NULL, 0, NULL, NULL); /* includes the NUL */
            if (len > 0) {
                out = malloc((size_t)len);
                if (out)
                    WideCharToMultiByte(CP_UTF8, 0, wptr, -1, out, len, NULL, NULL);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    pthread_mutex_unlock(&s_lock);
    return out;
}

void n_clipboard_destroy(void) {
    s_ready = 0;
}

#else /* no X11 or Win32 backend: unavailable, caller falls back to its toolkit clipboard */

int n_clipboard_init(void) {
    return -1;
}
int n_clipboard_available(void) {
    return 0;
}
int n_clipboard_set(int which, const char* text) {
    (void)which;
    (void)text;
    return -1;
}
char* n_clipboard_get(int which) {
    (void)which;
    return NULL;
}
void n_clipboard_destroy(void) {
}

#endif
