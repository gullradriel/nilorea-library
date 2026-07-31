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
 *@file n_gui.c
 *@brief GUI system: buttons, sliders, text areas, checkboxes, scrollbars, dropdown menus, windows
 *@author Castagnier Mickael
 *@version 3.0
 *@date 02/03/2026
 */

#include "nilorea/n_gui.h"
#include "nilorea/n_clipboard.h"

#include <limits.h> /* INT_MIN, the "native window position unknown" marker */

#ifdef HAVE_CJSON
#include "cJSON.h"
#endif

/* Clipboard helpers: prefer the n_clipboard backend (full X11 target negotiation so
 * strict apps like Chromium can paste, plus the PRIMARY selection for select-to-copy /
 * middle-click), and fall back to Allegro's clipboard (CLIPBOARD only) where n_clipboard
 * has no backend (non-X11). */
static void _ngui_clip_set(ALLEGRO_DISPLAY* display, int which, const char* text) {
    if (n_clipboard_available()) {
        n_clipboard_set(which, text);
        return;
    }
    if (which == N_CLIPBOARD_CLIPBOARD && display && text)
        al_set_clipboard_text(display, text);
}

/* Returns a malloc'd string (free with al_free/free) or NULL. n_clipboard_get returns a
 * malloc'd buffer; al_get_clipboard_text returns an al_malloc'd buffer - both are freed
 * with al_free on this path, so the caller frees uniformly with al_free. */
static char* _ngui_clip_get(ALLEGRO_DISPLAY* display, int which) {
    if (n_clipboard_available()) {
        char* s = n_clipboard_get(which);
        if (s) {
            char* a = al_malloc(strlen(s) + 1); /* re-home onto al_malloc so callers al_free uniformly */
            if (a)
                strcpy(a, s);
            free(s);
            return a;
        }
        return NULL;
    }
    return (which == N_CLIPBOARD_CLIPBOARD && display) ? al_get_clipboard_text(display) : NULL;
}

/* Duration (seconds) of the "pressed" visual flash applied to a button when
 * it is activated through its bound keyboard shortcut. Long enough for the
 * user to perceive a press, short enough to feel snappy. */
#define N_GUI_KEY_PRESS_FLASH_SEC 0.12

/* Small tail (seconds) added to an animation deadline in ctx->anim_until so
 * n_gui_needs_redraw keeps returning non-zero just past the deadline, which
 * guarantees the one frame that renders the post-animation state (the button
 * back to normal, the tooltip bubble revealed) is actually drawn before an
 * event-driven host is allowed to stop redrawing. */
#define N_GUI_ANIM_TAIL_SEC 0.05

/* INTERNAL HELPERS */

/*! helper: clamp a double between lo and hi */
static double _clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*! helper: snap a slider value to the nearest valid step from min_val.
 *  Uses round() to avoid floating-point drift (e.g. 0.1 steps never produce 0.30000000000000004).
 *  Result is clamped to [min_val, max_val]. step <= 0 is treated as 1. */
static double _slider_snap_value(double val, double min_val, double max_val, double step) {
    if (step <= 0.0) step = 1.0;
    double steps = round((val - min_val) / step);
    double snapped = min_val + steps * step;
    if (snapped < min_val) snapped = min_val;
    if (snapped > max_val) snapped = max_val;
    return snapped;
}

/*! @brief get the bounding box dimensions of text rendered with the given font.
 *  Uses al_get_text_dimensions for accurate measurement including glyph overhangs. */
N_GUI_TEXT_DIMS n_gui_get_text_dims(ALLEGRO_FONT* font, const char* text) {
    N_GUI_TEXT_DIMS d = {0, 0, 0, 0};
    if (font && text) al_get_text_dimensions(font, text, &d.x, &d.y, &d.w, &d.h);
    return d;
}

/*! helper: check if a widget type is focusable via Tab navigation */
static int _is_focusable_type(int type) {
    return type == N_GUI_TYPE_TEXTAREA ||
           type == N_GUI_TYPE_SLIDER ||
           type == N_GUI_TYPE_CHECKBOX ||
           type == N_GUI_TYPE_LISTBOX ||
           type == N_GUI_TYPE_RADIOLIST ||
           type == N_GUI_TYPE_COMBOBOX ||
           type == N_GUI_TYPE_SCROLLBAR ||
           type == N_GUI_TYPE_DROPMENU;
}

/*! helper: find the window containing a focused widget */
static N_GUI_WINDOW* _find_focused_window(N_GUI_CTX* ctx) {
    if (ctx->focused_widget_id < 0) return NULL;
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!win || !(win->state & N_GUI_WIN_OPEN)) continue;
        list_foreach(wgn, win->widgets) {
            const N_GUI_WIDGET* w = (const N_GUI_WIDGET*)wgn->ptr;
            if (w && w->id == ctx->focused_widget_id) return win;
        }
    }
    return NULL;
}

/*! helper: strip CR from a string in-place, normalizing CRLF to LF.
 *  Used at text entry points so all rendering code only needs to handle LF. */
static void _normalize_crlf(char* s) {
    if (!s) return;
    char* dst = s;
    for (char* src = s; *src; src++) {
        if (*src != '\r') *dst++ = *src;
    }
    *dst = '\0';
}

/* Text-measurement memoization.
 *
 * al_get_text_width / al_get_text_dimensions walk the string glyph by glyph
 * (a font glyph lookup per character) and are called per item, per cell, per
 * run, per word and per character every frame, over text that is almost always
 * identical from one frame to the next. This small direct-mapped cache turns
 * those repeats into a hash + compare.
 *
 * The cache is file scope on purpose: GUI drawing is single-threaded (the
 * _draw_* path and the hit tests take no locks), so a global cache is safe and
 * lets _text_w / _text_dims stay plain (font, text) helpers without threading a
 * context through every draw function. Each entry is a pure function of the
 * font metrics and the string, so sharing the cache across contexts is correct.
 *
 * The key includes the font's line height, so two different-size fonts that
 * happen to reuse the same freed address never return each other's metrics; the
 * only residual staleness is a same-size font reused at a freed address, which
 * at worst yields a stale extent for one string until that slot is reclaimed.
 * Strings at or beyond N_GUI_MEAS_KEY_MAX bypass the cache and measure directly.
 */
#define N_GUI_MEAS_CACHE_SIZE 1024u /* power of two */
#define N_GUI_MEAS_KEY_MAX 32       /* strings this long or longer bypass the cache */

typedef struct N_GUI_MEAS_ENTRY {
    const ALLEGRO_FONT* font; /* NULL: slot empty */
    int line_h;               /* part of the key: separates same-address fonts of different sizes */
    unsigned int hash;
    unsigned short len;
    char key[N_GUI_MEAS_KEY_MAX];
    float adv_w;             /* al_get_text_width result */
    int dx, dy, dw, dh;      /* al_get_text_dimensions result */
    unsigned char have_adv;  /* adv_w is populated */
    unsigned char have_dims; /* dx/dy/dw/dh are populated */
} N_GUI_MEAS_ENTRY;

static N_GUI_MEAS_ENTRY _ngui_meas_cache[N_GUI_MEAS_CACHE_SIZE];

/*! find (or claim) the cache slot for (font, text[len]). On a slot miss the
 *  entry is reset to this key with both metrics marked absent. Callers must
 *  have ruled out len >= N_GUI_MEAS_KEY_MAX. */
static N_GUI_MEAS_ENTRY* _ngui_meas_slot(ALLEGRO_FONT* font, const char* text, size_t len) {
    unsigned int h = 2166136261u; /* FNV-1a */
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)text[i];
        h *= 16777619u;
    }
    int line_h = al_get_font_line_height(font);
    h ^= (unsigned int)line_h * 2654435761u;
    N_GUI_MEAS_ENTRY* e = &_ngui_meas_cache[h & (N_GUI_MEAS_CACHE_SIZE - 1u)];
    if (e->font == font && e->line_h == line_h && e->hash == h &&
        e->len == (unsigned short)len && memcmp(e->key, text, len) == 0) {
        return e; /* hit */
    }
    /* claim the slot for this key */
    e->font = font;
    e->line_h = line_h;
    e->hash = h;
    e->len = (unsigned short)len;
    memcpy(e->key, text, len);
    e->have_adv = 0;
    e->have_dims = 0;
    return e;
}

/*! helper: get the advance width of text.
 *  Uses al_get_text_width which returns the advance width (includes space
 *  characters), unlike al_get_text_dimensions which returns the bounding box
 *  (zero width for spaces since they have no visible pixels). Memoized. */
static float _text_w(ALLEGRO_FONT* font, const char* text) {
    if (!font || !text || !text[0]) return 0.0f;
    size_t len = strlen(text);
    if (len >= N_GUI_MEAS_KEY_MAX) return (float)al_get_text_width(font, text);
    N_GUI_MEAS_ENTRY* e = _ngui_meas_slot(font, text, len);
    if (!e->have_adv) {
        e->adv_w = (float)al_get_text_width(font, text);
        e->have_adv = 1;
    }
    return e->adv_w;
}

/*! helper: memoized al_get_text_dimensions (bounding box x/y/w/h). Same result
 *  as calling al_get_text_dimensions directly; used by the per-widget label
 *  centring paths that re-measure unchanging text every frame. */
static void _text_dims(ALLEGRO_FONT* font, const char* text, int* x, int* y, int* w, int* h) {
    int bx = 0, by = 0, bw = 0, bh = 0;
    if (font && text && text[0]) {
        size_t len = strlen(text);
        if (len >= N_GUI_MEAS_KEY_MAX) {
            al_get_text_dimensions(font, text, &bx, &by, &bw, &bh);
        } else {
            N_GUI_MEAS_ENTRY* e = _ngui_meas_slot(font, text, len);
            if (!e->have_dims) {
                al_get_text_dimensions(font, text, &e->dx, &e->dy, &e->dw, &e->dh);
                e->have_dims = 1;
            }
            bx = e->dx;
            by = e->dy;
            bw = e->dw;
            bh = e->dh;
        }
    }
    if (x) *x = bx;
    if (y) *y = by;
    if (w) *w = bw;
    if (h) *h = bh;
}

/*! helper: effective title bar height (0 for frameless windows) */
static float _win_tbh(const N_GUI_WINDOW* win) {
    return (win->flags & N_GUI_WIN_FRAMELESS) ? 0.0f : win->titlebar_h;
}

/*! helper: whether a window takes part in the pass currently being processed or
 *  drawn. Pop-up windows (native == NULL) belong to the main display's pass,
 *  detached ones only to their own display's pass. With no window detached
 *  ctx->pass_display stays NULL and this is always true, so the single-display
 *  path behaves exactly as it did before display routing existed. */
static int _win_on_pass(const N_GUI_CTX* ctx, const N_GUI_WINDOW* win) {
    return win && win->native == ctx->pass_display;
}

/*! helper: whether a window is detached AND keeps drawing its own chrome
 *  (N_GUI_DETACH_OWN_CHROME). Such a window fills a frameless native window
 *  title bar included, so the title bar drag, the resize grip and the three
 *  title bar buttons have to act on the OS window instead of on x/y/w/h. */
static int _native_own_chrome(const N_GUI_WINDOW* win) {
    return (win && win->native && (win->detach_flags & N_GUI_DETACH_OWN_CHROME)) ? 1 : 0;
}

/*! helper: anchor an own-chrome window's title bar drag on the desktop cursor.
 *
 *  Mouse coordinates in an Allegro event are relative to the display the event
 *  came from, so moving the OS window changes the coordinate system the NEXT
 *  events are expressed in. Deriving each move from those coordinates therefore
 *  feeds every move back into the following one: the events already queued when
 *  al_set_window_position runs were generated against the OLD position, and each
 *  of them re-applies the travel the previous one already applied. Draining a
 *  burst of three motion events moves the window three times as far as the
 *  pointer went, then the correction events send it back, which is the jitter
 *  and the run-away seen when dragging a detached window around the desktop.
 *
 *  al_get_mouse_cursor_position reports the pointer in desktop coordinates,
 *  which no window move can alter. Capturing it once with the window position at
 *  the grab turns the drag into an absolute mapping (window = grab position +
 *  cursor travel), so a stale event carries no state and a backlog simply
 *  converges on the current pointer instead of compounding. */
static void _native_drag_begin(N_GUI_WINDOW* win) {
    if (!_native_own_chrome(win)) return;
    win->native_drag_anchored = 0;
    int cx = 0, cy = 0;
    if (!al_get_mouse_cursor_position(&cx, &cy)) return;
    int wx = 0, wy = 0;
    al_get_window_position(win->native, &wx, &wy);
    win->native_drag_cx = cx;
    win->native_drag_cy = cy;
    win->native_drag_wx = wx;
    win->native_drag_wy = wy;
    win->native_drag_anchored = 1;
}

/*! helper: mirror an own-chrome window's minimised state onto its OS window by
 *  shrinking the display to the title bar and growing it back.
 *
 *  Allegro's ALLEGRO_MINIMIZED is documented read-only (and "very
 *  platform-dependent"), so there is no portable iconify call to use. Shrinking
 *  is the honest mapping anyway: N_GUI minimised means "title bar only", which
 *  is exactly what the user then sees on the desktop.
 *
 *  win->h keeps the full height throughout, it is what the window grows back to
 *  and what the layout persists. The DISPLAY_RESIZE handler skips its geometry
 *  update while the window is minimised for the same reason. */
static void _native_apply_minimised(N_GUI_WINDOW* win) {
    if (!_native_own_chrome(win)) return;
    int minimised = (win->state & N_GUI_WIN_MINIMISED) ? 1 : 0;
    int want_w = (int)(win->w + 0.5f);
    int want_h = minimised ? (int)(_win_tbh(win) + 0.5f) : (int)(win->h + 0.5f);
    int resizable = (al_get_display_flags(win->native) & ALLEGRO_RESIZABLE) ? 1 : 0;
    if (want_w < 1) want_w = 1;
    if (want_h < 1) want_h = 1;

    /* The minimum-height constraint set at detach time exists to stop the window
     * manager shrinking a window below what its layout can render, but a title-bar
     * height is deliberately below it: with a real min_h in place the window manager
     * simply refuses the resize, and the window stayed full size while its content
     * hid, which looked like minimise doing nothing. So relax the constraint before
     * shrinking and restore it once the window has grown back. */
    if (resizable && minimised) {
        int cmin_w = (int)(win->min_w + 0.5f);
        if (cmin_w < 1) cmin_w = 1;
        if (al_set_window_constraints(win->native, cmin_w, want_h, 0, 0))
            al_apply_window_constraints(win->native, true);
    }

    if (want_w != al_get_display_width(win->native) || want_h != al_get_display_height(win->native)) {
        al_resize_display(win->native, want_w, want_h);
    }

    if (resizable && !minimised) {
        int cmin_w = (int)(win->min_w + 0.5f);
        int cmin_h = (int)(win->min_h + 0.5f);
        if (cmin_w < 1) cmin_w = 1;
        if (cmin_h < 1) cmin_h = 1;
        if (al_set_window_constraints(win->native, cmin_w, cmin_h, 0, 0))
            al_apply_window_constraints(win->native, true);
    }
}

/*! helper: the monitor rectangle a native window sits on.
 *
 *  al_get_display_adapter answers this in one call but it only exists from
 *  Allegro 5.2.10 on, and builds still target 5.2.6 (Debian 11). The window
 *  position is enough to derive it: pick the monitor whose rectangle contains
 *  the window origin, and fall back to the first adapter when the origin sits
 *  outside every reported monitor (window manager has not placed the window
 *  yet, or an adapter reports no info). Returns 1 when info was filled. */
static int _native_monitor_info(ALLEGRO_DISPLAY* display, ALLEGRO_MONITOR_INFO* info) {
    if (!display || !info) return 0;
    int wx = 0, wy = 0;
    al_get_window_position(display, &wx, &wy);
    int nb_adapters = al_get_num_video_adapters();
    for (int adapter = 0; adapter < nb_adapters; adapter++) {
        ALLEGRO_MONITOR_INFO mi;
        if (!al_get_monitor_info(adapter, &mi)) continue;
        if (wx >= mi.x1 && wx < mi.x2 && wy >= mi.y1 && wy < mi.y2) {
            *info = mi;
            return 1;
        }
    }
    return al_get_monitor_info(0, info) ? 1 : 0;
}

/*! helper: the display an event originates from, or NULL when the event carries
 *  no display (timer, user and joystick events) or the field is unset, as in the
 *  headless tests that synthesize events with memset. */
static ALLEGRO_DISPLAY* _event_display(const ALLEGRO_EVENT* event) {
    switch (event->type) {
        case ALLEGRO_EVENT_MOUSE_AXES:
        case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
        case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
        case ALLEGRO_EVENT_MOUSE_ENTER_DISPLAY:
        case ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY:
        case ALLEGRO_EVENT_MOUSE_WARPED:
            return event->mouse.display;
        case ALLEGRO_EVENT_KEY_DOWN:
        case ALLEGRO_EVENT_KEY_UP:
        case ALLEGRO_EVENT_KEY_CHAR:
            return event->keyboard.display;
        case ALLEGRO_EVENT_TOUCH_BEGIN:
        case ALLEGRO_EVENT_TOUCH_END:
        case ALLEGRO_EVENT_TOUCH_MOVE:
        case ALLEGRO_EVENT_TOUCH_CANCEL:
            return event->touch.display;
        case ALLEGRO_EVENT_DISPLAY_EXPOSE:
        case ALLEGRO_EVENT_DISPLAY_RESIZE:
        case ALLEGRO_EVENT_DISPLAY_CLOSE:
        case ALLEGRO_EVENT_DISPLAY_LOST:
        case ALLEGRO_EVENT_DISPLAY_FOUND:
        case ALLEGRO_EVENT_DISPLAY_SWITCH_IN:
        case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
        case ALLEGRO_EVENT_DISPLAY_ORIENTATION:
        case ALLEGRO_EVENT_DISPLAY_HALT_DRAWING:
        case ALLEGRO_EVENT_DISPLAY_RESUME_DRAWING:
            return event->display.source;
        default:
            /* timer, user, joystick, and the monitor hotplug events, none of
             * which name a display we could route on */
            return NULL;
    }
}

/*! forward declaration: unregister and destroy a window's native display,
 *  leaving the window's geometry untouched (see the native window section) */
static void _release_native_window(N_GUI_CTX* ctx, N_GUI_WINDOW* win);

/*! helper: the display to use for clipboard and mouse-cursor calls. Follows the
 *  display that last received input, so a copy/paste or a cursor change inside a
 *  detached window addresses that window rather than the host's main display.
 *  Falls back to the display given to n_gui_set_display. */
static ALLEGRO_DISPLAY* _ctx_io_display(const N_GUI_CTX* ctx) {
    return ctx->active_display ? ctx->active_display : ctx->display;
}

/*! helper: test if point is inside a rectangle */
static int _point_in_rect(float px, float py, float rx, float ry, float rw, float rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

/*! helper: is the pointer over any open window that takes part in the current
 *  pass? O(windows) and used to short-circuit the per-widget tooltip and
 *  datagrid-cursor scans on motion events when the pointer is over empty
 *  background (the common case when the GUI is composited over a game). Both of
 *  those scans only ever match widgets that live inside a window, so skipping
 *  them off-window yields the same "nothing under the pointer" result. */
static int _pointer_over_window(const N_GUI_CTX* ctx, float px, float py) {
    list_foreach(wnode, ctx->windows) {
        const N_GUI_WINDOW* win = (const N_GUI_WINDOW*)wnode->ptr;
        if (!win || !(win->state & N_GUI_WIN_OPEN)) continue;
        if (!_win_on_pass(ctx, win)) continue;
        float win_h_check = (win->state & N_GUI_WIN_MINIMISED) ? _win_tbh(win) : win->h;
        if (_point_in_rect(px, py, win->x, win->y, win->w, win_h_check)) return 1;
    }
    return 0;
}

/*! helper: compute scroll position from mouse coordinate using thumb-aware math.
 *  This is the formula used by the window auto-scrollbar, it maps the mouse
 *  to where the center of the thumb should be, giving smooth proportional feel.
 *  @param mouse        current mouse coordinate (x or y)
 *  @param track_start  pixel start of the scrollbar track
 *  @param track_length pixel length of the scrollbar track
 *  @param viewport     visible portion size
 *  @param content      total content size
 *  @param thumb_min    minimum thumb size in pixels
 *  @return             scroll position in [0, content - viewport]
 */
static float _scrollbar_calc_scroll(float mouse, float track_start, float track_length, float viewport, float content, float thumb_min) {
    float ratio = viewport / content;
    if (ratio > 1.0f) ratio = 1.0f;
    float thumb = ratio * track_length;
    if (thumb < thumb_min) thumb = thumb_min;
    float max_scroll = content - viewport;
    float track_range = track_length - thumb;
    if (track_range <= 0 || max_scroll <= 0) return 0;
    float pos_ratio = (mouse - track_start - thumb / 2.0f) / track_range;
    if (pos_ratio < 0) pos_ratio = 0;
    if (pos_ratio > 1) pos_ratio = 1;
    return pos_ratio * max_scroll;
}

/*! helper: integer variant of _scrollbar_calc_scroll for item-based widgets.
 *  @param mouse          current mouse coordinate
 *  @param track_start    pixel start of the scrollbar track
 *  @param track_length   pixel length of the scrollbar track
 *  @param visible_items  number of visible items
 *  @param total_items    total number of items
 *  @param thumb_min      minimum thumb size in pixels
 *  @return               scroll offset in [0, total_items - visible_items]
 */
static int _scrollbar_calc_scroll_int(float mouse, float track_start, float track_length, int visible_items, int total_items, float thumb_min) {
    int max_off = total_items - visible_items;
    if (max_off <= 0) return 0;
    float scroll = _scrollbar_calc_scroll(mouse, track_start, track_length,
                                          (float)visible_items, (float)total_items, thumb_min);
    int offset = (int)(scroll + 0.5f);
    if (offset < 0) offset = 0;
    if (offset > max_off) offset = max_off;
    return offset;
}

/*! helper: find the parent window of a widget by id, returning the window's
 *  content origin (accounting for scroll offsets).
 *  @param ctx    GUI context
 *  @param wgt_id widget id to search for
 *  @param ox     [out] content origin X
 *  @param oy     [out] content origin Y
 *  @return       pointer to the parent window, or NULL if not found
 */
static N_GUI_WINDOW* _find_widget_window(N_GUI_CTX* ctx, int wgt_id, float* ox, float* oy) {
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!(win->state & N_GUI_WIN_OPEN)) continue;
        list_foreach(wgn, win->widgets) {
            if (((N_GUI_WIDGET*)wgn->ptr)->id == wgt_id) {
                *ox = win->x - win->scroll_x;
                *oy = win->y + _win_tbh(win) - win->scroll_y;
                return win;
            }
        }
    }
    *ox = 0;
    *oy = 0;
    return NULL;
}

/*! helper: destroy a single widget (called by list destructor) */
/*! forward declaration: count the lines in a NUL-terminated text */
static int _text_line_count(const char* s);
/*! forward declaration: (re)build a syntaxview's cached line count and
 *  headers-end index when stale. Defined near the syntaxview draw code. */
static void _syntaxview_ensure_lines(N_GUI_SYNTAXVIEW_DATA* yd);
/*! forward declaration: (re)build a syntaxview's cached content width when
 *  stale. Defined near the syntaxview draw code. */
static void _syntaxview_ensure_width(N_GUI_SYNTAXVIEW_DATA* yd, ALLEGRO_FONT* font, const N_GUI_STYLE* style);
/*! forward declaration: syntaxview horizontal-scroll metrics, shared by the draw
 *  path and every mouse handler. Defined near the syntaxview draw code. */
static void _syntaxview_hmetrics(const N_GUI_WIDGET* wgt, N_GUI_SYNTAXVIEW_DATA* yd, ALLEGRO_FONT* font, const N_GUI_STYLE* style, float* content_w, float* pane_w, int* need_hsb, int* visible);
/*! forward declaration: clamp a syntaxview's horizontal scroll to its content.
 *  Defined near the syntaxview draw code. */
static void _syntaxview_clamp_h(N_GUI_SYNTAXVIEW_DATA* yd, float content_w, float pane_w);
/*! forward declaration: pixel width of the first n bytes of a line. Defined
 *  near the syntaxview draw code. */
static float _syntax_prefix_w(ALLEGRO_FONT* font, const char* s, int n);

static void _destroy_widget(void* ptr) {
    if (!ptr) return;
    N_GUI_WIDGET* w = (N_GUI_WIDGET*)ptr;
    FreeNoLog(w->tooltip);
    if (w->data) {
        /* free inner allocations for types that have them */
        if (w->type == N_GUI_TYPE_LISTBOX) {
            N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
            FreeNoLog(ld->items);
        } else if (w->type == N_GUI_TYPE_RADIOLIST) {
            N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)w->data;
            FreeNoLog(rd->items);
        } else if (w->type == N_GUI_TYPE_COMBOBOX) {
            N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)w->data;
            FreeNoLog(cd->items);
        } else if (w->type == N_GUI_TYPE_DROPMENU) {
            N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
            FreeNoLog(dd->entries);
        } else if (w->type == N_GUI_TYPE_TEXTAREA) {
            N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)w->data;
            FreeNoLog(td->text);
            FreeNoLog(td->placeholder);
        } else if (w->type == N_GUI_TYPE_HEXVIEW) {
            N_GUI_HEXVIEW_DATA* hd = (N_GUI_HEXVIEW_DATA*)w->data;
            FreeNoLog(hd->data);
        } else if (w->type == N_GUI_TYPE_SYNTAXVIEW) {
            N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)w->data;
            FreeNoLog(yd->text);
        } else if (w->type == N_GUI_TYPE_DATAGRID) {
            N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
            if (gd->cells) {
                size_t ci;
                for (ci = 0; ci < gd->nb_rows * gd->nb_cols; ci++)
                    FreeNoLog(gd->cells[ci]);
                FreeNoLog(gd->cells);
            }
            FreeNoLog(gd->cols);
            FreeNoLog(gd->col_order);
            FreeNoLog(gd->row_sel);
            FreeNoLog(gd->row_color);
            FreeNoLog(gd->row_has_color);
        }
        FreeNoLog(w->data);
    }
    FreeNoLog(w);
}

/*! helper: destroy a single window (called by list destructor) */
static void _destroy_window(void* ptr) {
    if (!ptr) return;
    N_GUI_WINDOW* win = (N_GUI_WINDOW*)ptr;
    if (win->widgets) {
        list_destroy(&win->widgets);
    }
    FreeNoLog(win);
}

/*! helper: find a window node in the context list by id */
static LIST_NODE* _find_window_node(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return NULL);
    __n_assert(ctx->windows, return NULL);
    list_foreach(node, ctx->windows) {
        const N_GUI_WINDOW* win = (const N_GUI_WINDOW*)node->ptr;
        if (win && win->id == window_id) return node;
    }
    return NULL;
}

/*! helper: register a widget in the hash table */
static void _register_widget(N_GUI_CTX* ctx, N_GUI_WIDGET* w) {
    __n_assert(ctx, return);
    __n_assert(w, return);
    char key[32];
    snprintf(key, sizeof(key), "%d", w->id);
    ht_put_ptr(ctx->widgets_by_id, key, w, NULL, NULL);
    ctx->dirty = 1; /* a new widget changes what will be drawn */
}

/*! helper: ensure items array has room for one more, returns 1 on success */
static int _items_grow(N_GUI_LISTITEM** items, const size_t* nb, size_t* cap) {
    if (*nb >= *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap) * 2;
        if (new_cap < *cap) return 0; /* overflow */
        N_GUI_LISTITEM* tmp = NULL;
        Malloc(tmp, N_GUI_LISTITEM, new_cap);
        if (!tmp) return 0;
        if (*items && *nb > 0) {
            memcpy(tmp, *items, (*nb) * sizeof(N_GUI_LISTITEM));
        }
        FreeNoLog(*items);
        *items = tmp;
        *cap = new_cap;
    }
    return 1;
}

/*! helper: allocate and initialise a base widget */
static N_GUI_WIDGET* _new_widget(N_GUI_CTX* ctx, int type, float x, float y, float w, float h) {
    __n_assert(ctx, return NULL);
    N_GUI_WIDGET* wgt = NULL;
    Malloc(wgt, N_GUI_WIDGET, 1);
    __n_assert(wgt, return NULL);
    wgt->id = ctx->next_widget_id++;
    wgt->type = type;
    wgt->x = x;
    wgt->y = y;
    wgt->w = w;
    wgt->h = h;
    wgt->state = N_GUI_STATE_IDLE;
    wgt->visible = 1;
    wgt->enabled = 1;
    wgt->theme = ctx->default_theme;
    wgt->font = NULL;
    wgt->data = NULL;
    wgt->tooltip = NULL;
    return wgt;
}

/* THEME */

/**
 * @brief Build a sensible default colour theme
 * @return a N_GUI_THEME with dark-ish colours
 */
N_GUI_THEME n_gui_default_theme(void) {
    N_GUI_THEME t;
    t.bg_normal = al_map_rgba(50, 50, 60, 230);
    t.bg_hover = al_map_rgba(70, 70, 85, 240);
    t.bg_active = al_map_rgba(90, 90, 110, 250);
    t.border_normal = al_map_rgba(120, 120, 140, 255);
    t.border_hover = al_map_rgba(160, 160, 180, 255);
    t.border_active = al_map_rgba(200, 200, 220, 255);
    t.text_normal = al_map_rgba(220, 220, 220, 255);
    t.text_hover = al_map_rgba(255, 255, 255, 255);
    t.text_active = al_map_rgba(255, 255, 255, 255);
    t.border_thickness = 1.0f;
    t.corner_rx = 4.0f;
    t.corner_ry = 4.0f;
    t.selection_color = al_map_rgba(50, 100, 200, 120);
    return t;
}

/**
 *@brief Create a custom theme with explicit colours
 *@param bg background colour
 *@param bg_hover background colour on hover
 *@param bg_active background colour when active
 *@param border border colour
 *@param border_hover border colour on hover
 *@param border_active border colour when active
 *@param text text colour
 *@param text_hover text colour on hover
 *@param text_active text colour when active
 *@param border_thickness border thickness in pixels
 *@param corner_rx horizontal corner radius
 *@param corner_ry vertical corner radius
 *@return configured N_GUI_THEME
 */
N_GUI_THEME n_gui_make_theme(
    ALLEGRO_COLOR bg,
    ALLEGRO_COLOR bg_hover,
    ALLEGRO_COLOR bg_active,
    ALLEGRO_COLOR border,
    ALLEGRO_COLOR border_hover,
    ALLEGRO_COLOR border_active,
    ALLEGRO_COLOR text,
    ALLEGRO_COLOR text_hover,
    ALLEGRO_COLOR text_active,
    float border_thickness,
    float corner_rx,
    float corner_ry) {
    N_GUI_THEME t;
    t.bg_normal = bg;
    t.bg_hover = bg_hover;
    t.bg_active = bg_active;
    t.border_normal = border;
    t.border_hover = border_hover;
    t.border_active = border_active;
    t.text_normal = text;
    t.text_hover = text_hover;
    t.text_active = text_active;
    t.border_thickness = border_thickness;
    t.corner_rx = corner_rx;
    t.corner_ry = corner_ry;
    t.selection_color = al_map_rgba(50, 100, 200, 120);
    return t;
}

/**
 * @brief Helper: return a copy of c with the given alpha (0..1).
 */
static ALLEGRO_COLOR _color_with_alpha(ALLEGRO_COLOR c, float a) {
    c.a = a;
    return c;
}

/**
 * @brief Build a default theme for titlebar minimize/maximize buttons.
 *
 * Semi-transparent, blends with the titlebar background.
 */
N_GUI_THEME n_gui_default_tb_btn_theme(void) {
    N_GUI_THEME t;
    t.bg_normal = al_map_rgba(0, 0, 0, 0);
    t.bg_hover = al_map_rgba(255, 255, 255, 40);
    t.bg_active = al_map_rgba(255, 255, 255, 80);
    t.border_normal = al_map_rgba(0, 0, 0, 0);
    t.border_hover = al_map_rgba(0, 0, 0, 0);
    t.border_active = al_map_rgba(0, 0, 0, 0);
    t.text_normal = al_map_rgba(200, 200, 200, 255);
    t.text_hover = al_map_rgba(255, 255, 255, 255);
    t.text_active = al_map_rgba(255, 255, 255, 255);
    t.border_thickness = 0;
    t.corner_rx = 2.0f;
    t.corner_ry = 2.0f;
    t.selection_color = al_map_rgba(0, 0, 0, 0);
    return t;
}

/**
 * @brief Build a default theme for the titlebar close button.
 *
 * Same as the btn theme but with red hover/active backgrounds.
 */
N_GUI_THEME n_gui_default_tb_close_theme(void) {
    N_GUI_THEME t = n_gui_default_tb_btn_theme();
    t.bg_hover = al_map_rgba(232, 17, 35, 220);
    t.bg_active = al_map_rgba(200, 10, 25, 255);
    return t;
}

void n_gui_set_default_theme(N_GUI_CTX* ctx, N_GUI_THEME theme) {
    if (!ctx) return;
    ctx->default_theme = theme;

    /* Derive scrollbar colors from the theme so they stay in sync */
    ctx->style.scrollbar_track_color =
        _color_with_alpha(theme.bg_normal, 0.78f);
    ctx->style.scrollbar_thumb_color =
        _color_with_alpha(theme.border_normal, 0.86f);
    ctx->style.global_scrollbar_track_color =
        _color_with_alpha(theme.bg_active, 0.78f);
    ctx->style.global_scrollbar_thumb_color =
        _color_with_alpha(theme.border_normal, 0.86f);
    ctx->style.global_scrollbar_thumb_border_color =
        _color_with_alpha(theme.text_normal, 0.50f);

    /* Derive titlebar button glyph colors from the theme */
    ctx->default_tb_btn_theme.text_normal = _color_with_alpha(theme.text_normal, 0.80f);
    ctx->default_tb_btn_theme.text_hover = theme.text_hover;
    ctx->default_tb_btn_theme.text_active = theme.text_active;
    ctx->default_tb_close_theme.text_normal = _color_with_alpha(theme.text_normal, 0.80f);
    ctx->default_tb_close_theme.text_hover = theme.text_hover;
    ctx->default_tb_close_theme.text_active = theme.text_active;
}

/* STYLE DEFAULTS */

/**
 * @brief Build sensible default style (all configurable sizes, colours, paddings)
 */
N_GUI_STYLE n_gui_default_style(void) {
    N_GUI_STYLE s;

    /* tooltips */
    s.tooltip_delay = 0.6f;
    s.tooltip_bg = al_map_rgba(28, 30, 36, 240);
    s.tooltip_border = al_map_rgb(110, 115, 130);
    s.tooltip_fg = al_map_rgb(225, 225, 230);

    /* window chrome */
    s.titlebar_h = 28.0f;
    s.min_win_w = 120.0f;
    s.min_win_h = 60.0f;
    s.title_padding = 8.0f;
    s.title_max_w_reserve = 16.0f;

    /* titlebar buttons */
    s.tb_btn_size = 0.0f;
    s.tb_btn_spacing = 2.0f;
    s.tb_btn_right_margin = 4.0f;
    s.tb_btn_glyph_thickness = 1.5f;

    /* window auto-scrollbar */
    s.scrollbar_size = 12.0f;
    s.scrollbar_thumb_min = 16.0f;
    s.scrollbar_thumb_padding = 2.0f;
    s.scrollbar_thumb_corner_r = 3.0f;
    s.scrollbar_track_color = al_map_rgba(40, 40, 50, 200);
    s.scrollbar_thumb_color = al_map_rgba(120, 120, 140, 220);

    /* global display scrollbar */
    s.global_scrollbar_size = 14.0f;
    s.global_scrollbar_thumb_min = 20.0f;
    s.global_scrollbar_thumb_padding = 2.0f;
    s.global_scrollbar_thumb_corner_r = 4.0f;
    s.global_scrollbar_border_thickness = 1.0f;
    s.global_scrollbar_track_color = al_map_rgba(30, 30, 40, 200);
    s.global_scrollbar_thumb_color = al_map_rgba(100, 100, 120, 220);
    s.global_scrollbar_thumb_border_color = al_map_rgba(140, 140, 160, 255);

    /* resize grip */
    s.grip_size = 12.0f;
    s.grip_line_thickness = 1.0f;
    s.grip_color = al_map_rgba(160, 160, 180, 200);

    /* slider */
    s.slider_track_size = 6.0f;
    s.slider_track_corner_r = 3.0f;
    s.slider_track_border_thickness = 1.0f;
    s.slider_handle_min_r = 4.0f;
    s.slider_handle_edge_offset = 2.0f;
    s.slider_handle_border_thickness = 1.5f;
    s.slider_value_label_offset = 8.0f;

    /* text area */
    s.textarea_padding = 4.0f;
    s.textarea_cursor_width = 2.0f;
    s.textarea_cursor_blink_period = 1.0f;

    /* checkbox */
    s.checkbox_max_size = 20.0f;
    s.checkbox_mark_margin = 4.0f;
    s.checkbox_mark_thickness = 2.0f;
    s.checkbox_label_gap = 10.0f;
    s.checkbox_label_offset = 6.0f;

    /* radio list */
    s.radio_circle_min_r = 4.0f;
    s.radio_circle_border_thickness = 1.5f;
    s.radio_inner_offset = 3.0f;
    s.radio_label_gap = 6.0f;

    /* list items */
    s.listbox_default_item_height = 20.0f;
    s.radiolist_default_item_height = 22.0f;
    s.combobox_max_visible = 6;
    s.dropmenu_max_visible = 12;
    s.shape_mode = -1; /* -1 = per-widget shape (buttons keep their own); the
                          dropmenu panel still defaults to rounded. Set to
                          N_GUI_SHAPE_ROUNDED / N_GUI_SHAPE_RECT to force a global
                          round or square look across shape-aware widgets. */
    s.item_text_padding = 6.0f;
    s.item_selection_inset = 1.0f;
    s.item_height_pad = 4.0f;

    /* dropdown arrow */
    s.dropdown_arrow_reserve = 16.0f;
    s.dropdown_arrow_thickness = 1.5f;
    s.dropdown_arrow_half_h = 3.0f;
    s.dropdown_arrow_half_w = 5.0f;
    s.dropdown_border_thickness = 1.0f;

    /* label */
    s.label_padding = 4.0f;
    s.link_underline_thickness = 1.0f;
    s.link_color_normal = al_map_rgba(80, 140, 220, 255);
    s.link_color_hover = al_map_rgba(120, 180, 255, 255);

    /* scroll step */
    s.scroll_step = 20.0f;
    s.global_scroll_step = 30.0f;

    /* combobox auto-width cap */
    s.combobox_max_dropdown_width = 0.0f; /* 0 = clamp to display width */

    return s;
}

/* CONTEXT */

/**
 * @brief Create a new GUI context
 * @param default_font the default ALLEGRO_FONT to use
 * @return a new N_GUI_CTX or NULL on error
 */
N_GUI_CTX* n_gui_new_ctx(ALLEGRO_FONT* default_font) {
    __n_assert(default_font, return NULL);
    N_GUI_CTX* ctx = NULL;
    Malloc(ctx, N_GUI_CTX, 1);
    __n_assert(ctx, return NULL);
    ctx->windows = new_generic_list(UNLIMITED_LIST_ITEMS);
    if (!ctx->windows) {
        n_log(LOG_ERR, "n_gui_new_ctx: failed to allocate window list");
        Free(ctx);
        return NULL;
    }
    ctx->widgets_by_id = new_ht(256);
    if (!ctx->widgets_by_id) {
        n_log(LOG_ERR, "n_gui_new_ctx: failed to allocate widget hash table");
        list_destroy(&ctx->windows);
        Free(ctx);
        return NULL;
    }
    ctx->next_widget_id = 1;
    ctx->next_window_id = 1;
    ctx->default_font = default_font;
    ctx->default_theme = n_gui_default_theme();
    ctx->default_tb_btn_theme = n_gui_default_tb_btn_theme();
    ctx->default_tb_close_theme = n_gui_default_tb_close_theme();
    ctx->focused_widget_id = -1;
    ctx->cursor_shape = ALLEGRO_SYSTEM_MOUSE_CURSOR_DEFAULT;
    ctx->tooltip_widget_id = -1;
    ctx->tooltip_armed_at = 0.0;
    ctx->tooltip_anchor_x = -10000;
    ctx->tooltip_anchor_y = -10000;
    ctx->mouse_x = 0;
    ctx->mouse_y = 0;
    ctx->mouse_b1 = 0;
    ctx->mouse_b1_prev = 0;
    ctx->open_combobox_id = -1;
    ctx->scrollbar_drag_widget_id = -1;
    ctx->open_dropmenu_id = -1;
    ctx->display_w = 0;
    ctx->display_h = 0;
    ctx->global_scroll_x = 0;
    ctx->global_scroll_y = 0;
    ctx->gui_bounds_w = 0;
    ctx->gui_bounds_h = 0;
    ctx->dpi_scale = 1.0f;
    ctx->virtual_w = 0;
    ctx->virtual_h = 0;
    ctx->gui_scale = 1.0f;
    ctx->gui_offset_x = 0;
    ctx->gui_offset_y = 0;
    ctx->global_vscroll_drag = 0;
    ctx->global_hscroll_drag = 0;
    ctx->style = n_gui_default_style();
    ctx->display = NULL;
    ctx->selected_label_id = -1;
    ctx->selected_syntaxview_id = -1;
    ctx->resize_mode = N_GUI_RESIZE_VIRTUAL;
    ctx->ref_display_w = 0.0f;
    ctx->ref_display_h = 0.0f;
    ctx->pending_layout = NULL;
    ctx->pending_layout_count = 0;
    ctx->pending_widgets = NULL;
    ctx->pending_widgets_count = 0;
    ctx->event_queue = NULL;
    ctx->pass_display = NULL;
    ctx->active_display = NULL;
    ctx->dirty = 1; /* the empty context still needs its first frame drawn */
    ctx->anim_until = 0.0;
    ctx->prev_over_win = 0;
    return ctx;
}

void n_gui_mark_dirty(N_GUI_CTX* ctx) {
    if (ctx) ctx->dirty = 1;
}

int n_gui_needs_redraw(N_GUI_CTX* ctx) {
    if (!ctx) return 1;
    if (ctx->dirty) return 1;
    /* a timed animation (button key-press flash, tooltip reveal) is pending */
    if (ctx->anim_until > 0.0 && al_get_time() < ctx->anim_until) return 1;
    /* a focused text area blinks its caret, so it must keep redrawing */
    if (ctx->focused_widget_id >= 0) {
        const N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
        if (fw && fw->type == N_GUI_TYPE_TEXTAREA) return 1;
    }
    return 0;
}

/**
 * @brief Destroy a GUI context and all its windows/widgets
 * @param ctx pointer to the context pointer (set to NULL)
 */
void n_gui_destroy_ctx(N_GUI_CTX** ctx) {
    __n_assert(ctx && *ctx, return);
    n_clipboard_destroy(); /* stop the X11 clipboard serving thread, if it was started */
    if ((*ctx)->windows) {
        /* Release native windows first: the list destructor only gets a void*
         * and cannot reach the context's event queue to unregister their event
         * sources. Done before list_destroy so no display outlives the context. */
        list_foreach(dnode, (*ctx)->windows) {
            N_GUI_WINDOW* dwin = (N_GUI_WINDOW*)dnode->ptr;
            if (dwin && dwin->native) {
                _release_native_window(*ctx, dwin);
            }
        }
        list_destroy(&(*ctx)->windows);
    }
    if ((*ctx)->widgets_by_id) {
        destroy_ht(&(*ctx)->widgets_by_id);
    }
    FreeNoLog((*ctx)->pending_layout);
    FreeNoLog((*ctx)->pending_widgets);
    Free((*ctx));
}

/* ADAPTIVE RESIZE HELPERS */

/** @brief capture normalized coords for a window and its widgets from current absolutes */
static void _n_gui_window_capture_normalized(const N_GUI_CTX* ctx, N_GUI_WINDOW* win) {
    float dw = ctx->ref_display_w;
    float dh = ctx->ref_display_h;
    if (dw <= 0) dw = ctx->display_w;
    if (dh <= 0) dh = ctx->display_h;
    if (dw <= 0 || dh <= 0) return;

    win->norm_x = win->x / dw;
    win->norm_y = win->y / dh;
    win->norm_w = win->w / dw;
    win->norm_h = win->h / dh;

    /* capture widget normalized coords relative to window */
    if (win->resize_policy == N_GUI_WIN_RESIZE_SCALE && win->w > 0 && win->h > 0) {
        list_foreach(wnode, win->widgets) {
            N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wnode->ptr;
            wgt->norm_x = wgt->x / win->w;
            wgt->norm_y = wgt->y / win->h;
            wgt->norm_w = wgt->w / win->w;
            wgt->norm_h = wgt->h / win->h;
        }
    }
}

/** @brief capture normalized coords for a single widget relative to its parent window */
static void _n_gui_widget_capture_normalized(const N_GUI_WINDOW* win, N_GUI_WIDGET* wgt) {
    if (win->w > 0 && win->h > 0) {
        wgt->norm_x = wgt->x / win->w;
        wgt->norm_y = wgt->y / win->h;
        wgt->norm_w = wgt->w / win->w;
        wgt->norm_h = wgt->h / win->h;
    }
}

/* WINDOW MANAGEMENT */

static void _sort_windows_by_zorder(N_GUI_CTX* ctx);

/**
 * @brief Add a new pseudo-window to the context
 * @return the window id, or -1 on error
 */
int n_gui_add_window(N_GUI_CTX* ctx, const char* title, float x, float y, float w, float h) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = NULL;
    Malloc(win, N_GUI_WINDOW, 1);
    __n_assert(win, return -1);
    win->id = ctx->next_window_id++;
    if (title) {
        strncpy(win->title, title, N_GUI_ID_MAX - 1);
        win->title[N_GUI_ID_MAX - 1] = '\0';
    } else {
        win->title[0] = '\0';
    }
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->titlebar_h = ctx->style.titlebar_h;
    win->state = N_GUI_WIN_OPEN;
    win->widgets = new_generic_list(UNLIMITED_LIST_ITEMS);
    if (!win->widgets) {
        n_log(LOG_ERR, "n_gui_add_window: failed to allocate widget list");
        Free(win);
        return -1;
    }
    win->theme = ctx->default_theme;
    win->font = NULL;
    win->drag_ox = 0;
    win->drag_oy = 0;
    win->min_w = ctx->style.min_win_w;
    win->min_h = ctx->style.min_win_h;
    win->flags = 0;
    win->scroll_x = 0.0f;
    win->scroll_y = 0.0f;
    win->content_w = 0.0f;
    win->content_h = 0.0f;
    win->z_order = N_GUI_ZORDER_NORMAL;
    win->z_value = 0;
    win->autofit_flags = 0;
    win->autofit_border = 0.0f;
    win->autofit_origin_x = x;
    win->autofit_origin_y = y;
    win->resize_policy = N_GUI_WIN_RESIZE_NONE;
    win->norm_x = 0.0f;
    win->norm_y = 0.0f;
    win->norm_w = 0.0f;
    win->norm_h = 0.0f;
    memset(&win->tb_buttons, 0, sizeof(N_GUI_TB_BUTTONS));
    win->tb_buttons.btn_theme = ctx->default_tb_btn_theme;
    win->tb_buttons.close_theme = ctx->default_tb_close_theme;
    win->native = NULL;
    win->detach_flags = N_GUI_DETACH_NONE;
    win->want_native = 0;
    win->native_close_pending = 0;
    win->saved_x = x;
    win->saved_y = y;
    win->saved_w = w;
    win->saved_h = h;
    win->saved_flags = 0;
    win->native_w = 0.0f;
    win->native_h = 0.0f;
    win->native_pos_x = INT_MIN;
    win->native_pos_y = INT_MIN;
    win->native_drag_cx = 0;
    win->native_drag_cy = 0;
    win->native_drag_wx = 0;
    win->native_drag_wy = 0;
    win->native_drag_anchored = 0;
    win->native_halted = 0;
    /* Cross-session restore for lazily-created windows: if the loaded layout
     * carried geometry for a window that did NOT exist at load time (so it
     * couldn't be matched then), apply that saved position, and size when the
     * file recorded one, the first time a window with this title is created.
     * Consume-once so subsequent destroy+recreate rebuilds keep the caller's
     * (possibly user-dragged/resized) geometry instead of snapping back to the
     * file. Of the persisted state bits only minimised/maximised are applied: they
     * describe how the window is displayed and must survive a restart, whereas
     * N_GUI_WIN_OPEN stays caller-driven here, a lazily-created window is created
     * because something is about to open it, so forcing OPEN at creation time would
     * be meaningless (windows that already exist at load time do restore OPEN). */
    if (ctx->pending_layout && win->title[0]) {
        for (int i = 0; i < ctx->pending_layout_count; i++) {
            if (!ctx->pending_layout[i].consumed &&
                strncmp(ctx->pending_layout[i].title, win->title, N_GUI_ID_MAX) == 0) {
                win->x = ctx->pending_layout[i].x;
                win->y = ctx->pending_layout[i].y;
                if (ctx->display_w > 0.0f && (win->x < 0.0f || win->x > ctx->display_w)) win->x = 0.0f;
                if (ctx->display_h > 0.0f && (win->y < 0.0f || win->y > ctx->display_h)) win->y = 0.0f;
                /* size only when the file carried one; clamp to the display so a
                 * layout saved on a larger screen cannot exceed a smaller one */
                if (ctx->pending_layout[i].w > 0.0f && ctx->pending_layout[i].h > 0.0f) {
                    win->w = ctx->pending_layout[i].w;
                    win->h = ctx->pending_layout[i].h;
                    if (ctx->display_w > 0.0f && win->w > ctx->display_w) win->w = ctx->display_w;
                    if (ctx->display_h > 0.0f && win->h > ctx->display_h) win->h = ctx->display_h;
                }
                win->state = (win->state & ~(N_GUI_WIN_MINIMISED | N_GUI_WIN_MAXIMISED)) |
                             (ctx->pending_layout[i].state & (N_GUI_WIN_MINIMISED | N_GUI_WIN_MAXIMISED));
                ctx->pending_layout[i].consumed = 1;
                break;
            }
        }
    }
    list_push(ctx->windows, win, _destroy_window);
    if (ctx->resize_mode == N_GUI_RESIZE_ADAPTIVE) {
        _n_gui_window_capture_normalized(ctx, win);
    }
    /* Re-sort so z-order groups hold from the moment of creation: without
     * this, a freshly added NORMAL window sits at the list tail and is
     * drawn ABOVE every ALWAYS_ON_TOP window until some later
     * raise/lower/set_zorder call happens to re-sort the list. The sort is
     * stable, so the new window still lands on top of its own group. */
    _sort_windows_by_zorder(ctx);
    ctx->dirty = 1; /* a new window changes what will be drawn */
    return win->id;
}

/**
 * @brief Get a window pointer by id
 */
N_GUI_WINDOW* n_gui_get_window(N_GUI_CTX* ctx, int window_id) {
    LIST_NODE* node = _find_window_node(ctx, window_id);
    if (node) return (N_GUI_WINDOW*)node->ptr;
    return NULL;
}

/**
 * @brief Close (hide) a window
 */
void n_gui_close_window(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->state &= ~N_GUI_WIN_OPEN;
    /* A native window has no "hidden" state: closing it means taking the OS
     * window down. want_native survives, so re-opening brings it back as a
     * native window rather than as a pop-up. Deferred to the next
     * n_gui_draw_detached because a caller may well close from inside an event
     * callback, with events for this display still queued. */
    if (win->native) win->native_close_pending = 1;
}

/**
 * @brief Open (show) a window
 */
void n_gui_open_window(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->state |= N_GUI_WIN_OPEN;
    if (win->native) {
        win->native_close_pending = 0;
        return;
    }
    /* re-create the native window a previous close took down, and honour the
     * detached state a loaded layout asked for */
    if (win->want_native && ctx && ctx->event_queue) {
        n_gui_window_detach(ctx, window_id, win->detach_flags);
    }
}

/**
 * @brief Minimise a window (show title bar only)
 */
void n_gui_minimize_window(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->state ^= N_GUI_WIN_MINIMISED;
    _native_apply_minimised(win);
}

/**
 * @brief Restore a minimised window to its full size
 *
 * Unlike n_gui_minimize_window, which toggles, this always clears the minimised
 * state, so a caller that is about to show a window (an "open this dialog" action)
 * can guarantee the content is visible without knowing the current state. A window
 * that is not minimised is left untouched, and the maximised state is not affected.
 *
 * @param ctx the gui context
 * @param window_id the id of the window to restore
 */
void n_gui_restore_window(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->state &= ~N_GUI_WIN_MINIMISED;
    _native_apply_minimised(win);
}

/**
 * @brief Check if a window is currently minimised
 * @param ctx the gui context
 * @param window_id the id of the window to query
 * @return 1 if the window is minimised, 0 otherwise (or on a bad id)
 */
int n_gui_window_is_minimised(N_GUI_CTX* ctx, int window_id) {
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    return (win && (win->state & N_GUI_WIN_MINIMISED)) ? 1 : 0;
}

/**
 * @brief Toggle maximised state (full display or restore to previous size).
 *
 * When maximising: saves current position/size, expands to fill the display
 * (or virtual canvas if enabled), and clears minimised state.
 * When restoring: returns to the saved position/size.
 */
void n_gui_maximize_window(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;

    /* An own-chrome window is maximised by the window manager, not by expanding
     * into the main display's coordinate space. ALLEGRO_MAXIMIZED is one of the
     * three flags Allegro documents as changeable after creation (and it needs
     * ALLEGRO_RESIZABLE, which n_gui_window_detach always sets for own chrome).
     * Not every window manager honours it, so the geometry the WM actually
     * grants arrives as a DISPLAY_RESIZE and our own bookkeeping follows that
     * rather than assuming a size. */
    if (_native_own_chrome(win)) {
        int now_max = (win->state & N_GUI_WIN_MAXIMISED) ? 1 : 0;
        if (!now_max) {
            /* remember what to grow back from, the WM restores the size itself
               but our pop-up fallback geometry has to survive either way */
            win->tb_buttons.restore_x = win->x;
            win->tb_buttons.restore_y = win->y;
            win->tb_buttons.restore_w = win->w;
            win->tb_buttons.restore_h = win->h;
            win->state &= ~N_GUI_WIN_MINIMISED;
            _native_apply_minimised(win);
        }
        if (!al_set_display_flag(win->native, ALLEGRO_MAXIMIZED, now_max ? false : true)) {
            n_log(LOG_DEBUG, "n_gui_maximize_window: window manager refused ALLEGRO_MAXIMIZED for window %d", window_id);
            /* fall back to resizing the window to its adapter's work area, so
               the button still does something on a WM without the hint */
            if (!now_max) {
                ALLEGRO_MONITOR_INFO mi;
                if (_native_monitor_info(win->native, &mi)) {
                    al_set_window_position(win->native, mi.x1, mi.y1);
                    al_resize_display(win->native, mi.x2 - mi.x1, mi.y2 - mi.y1);
                }
            } else {
                al_resize_display(win->native, (int)(win->tb_buttons.restore_w + 0.5f),
                                  (int)(win->tb_buttons.restore_h + 0.5f));
            }
        }
        if (now_max)
            win->state &= ~N_GUI_WIN_MAXIMISED;
        else
            win->state |= N_GUI_WIN_MAXIMISED;
        return;
    }

    if (win->state & N_GUI_WIN_MAXIMISED) {
        /* restore */
        win->x = win->tb_buttons.restore_x;
        win->y = win->tb_buttons.restore_y;
        win->w = win->tb_buttons.restore_w;
        win->h = win->tb_buttons.restore_h;
        win->state &= ~N_GUI_WIN_MAXIMISED;
    } else {
        /* save current geometry */
        win->tb_buttons.restore_x = win->x;
        win->tb_buttons.restore_y = win->y;
        win->tb_buttons.restore_w = win->w;
        win->tb_buttons.restore_h = win->h;
        /* clear minimised */
        win->state &= ~N_GUI_WIN_MINIMISED;
        /* expand to display/virtual size */
        win->x = 0;
        win->y = 0;
        win->w = (ctx->virtual_w > 0) ? ctx->virtual_w : ctx->display_w;
        win->h = (ctx->virtual_h > 0) ? ctx->virtual_h : ctx->display_h;
        win->state |= N_GUI_WIN_MAXIMISED;
    }
    if (ctx->resize_mode == N_GUI_RESIZE_ADAPTIVE) {
        _n_gui_window_capture_normalized(ctx, win);
    }
}

/**
 * @brief Check if a window is currently maximised.
 * @return 1 if maximised, 0 if not (or invalid id)
 */
int n_gui_window_is_maximised(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return 0);
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return 0;
    return (win->state & N_GUI_WIN_MAXIMISED) ? 1 : 0;
}

/* NATIVE (DETACHED) WINDOWS */

/**
 * @brief Set the event queue used to register native window event sources.
 */
void n_gui_set_event_queue(N_GUI_CTX* ctx, ALLEGRO_EVENT_QUEUE* queue) {
    __n_assert(ctx, return);
    ctx->event_queue = queue;
}

/**
 * @brief Get the event queue previously given to n_gui_set_event_queue.
 */
// cppcheck-suppress constParameterPointer ; public API uses non-const for consistency
ALLEGRO_EVENT_QUEUE* n_gui_get_event_queue(N_GUI_CTX* ctx) {
    __n_assert(ctx, return NULL);
    return ctx->event_queue;
}

/*! helper: unregister and destroy a window's native display. The window's
 *  geometry and flags are left alone, the callers decide whether the window
 *  goes back to being a pop-up (n_gui_window_attach) or simply disappears
 *  with the context (n_gui_destroy_ctx). */
static void _release_native_window(N_GUI_CTX* ctx, N_GUI_WINDOW* win) {
    if (!win || !win->native) return;
    ALLEGRO_DISPLAY* d = win->native;
    /* remember where the user left the window, so re-creating it (and a saved
     * layout) puts it back on the same spot of the desktop */
    {
        int wx = 0, wy = 0;
        al_get_window_position(d, &wx, &wy);
        win->native_pos_x = wx;
        win->native_pos_y = wy;
        win->native_w = (float)al_get_display_width(d);
        win->native_h = (float)al_get_display_height(d);
    }
    /* Clear the back-pointer BEFORE destroying: al_destroy_display can pump the
     * platform event loop, and anything that re-enters the GUI in the meantime
     * must not find a window still claiming a half-destroyed display. */
    win->native = NULL;
    win->native_close_pending = 0;
    if (ctx->active_display == d) ctx->active_display = NULL;
    if (ctx->pass_display == d) ctx->pass_display = NULL;
    if (ctx->event_queue) {
        al_unregister_event_source(ctx->event_queue, al_get_display_event_source(d));
    }
    al_destroy_display(d);
    /* al_destroy_display leaves no current target when it destroyed the target
     * display, restore the host's main display so the caller's next draw call
     * does not land nowhere. */
    if (ctx->display) {
        al_set_target_backbuffer(ctx->display);
    }
}

/**
 * @brief Promote a pop-up window to a native OS window.
 */
int n_gui_window_detach(N_GUI_CTX* ctx, int window_id, int detach_flags) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) {
        n_log(LOG_ERR, "n_gui_window_detach: no window with id %d", window_id);
        return -1;
    }
    if (win->native) {
        win->detach_flags = detach_flags;
        return 0; /* already detached */
    }
    if (!ctx->event_queue) {
        n_log(LOG_ERR, "n_gui_window_detach: no event queue set, call n_gui_set_event_queue first");
        return -1;
    }

    /* How much of the window the native display has to show. With the OS
     * chrome (the default) that is the body only, the window manager draws the
     * title bar. With N_GUI_DETACH_OWN_CHROME the window keeps drawing its own
     * title bar inside a frameless native window, so the display covers the
     * whole window. A previously known native size wins in both cases, so a
     * close/open cycle and a restored layout bring the window back at the size
     * the user left it. */
    int own_chrome = (detach_flags & N_GUI_DETACH_OWN_CHROME) ? 1 : 0;
    float shown_h = own_chrome ? win->h : (win->h - _win_tbh(win));
    if (shown_h < 1.0f) shown_h = win->h;
    int dw = (int)(win->w + 0.5f);
    int dh = (int)(shown_h + 0.5f);
    if (win->native_w > 0.0f && win->native_h > 0.0f) {
        dw = (int)(win->native_w + 0.5f);
        dh = (int)(win->native_h + 0.5f);
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    /* al_create_display makes the new display current and retargets drawing to
     * its backbuffer, restore whatever the caller was drawing into */
    ALLEGRO_BITMAP* prev_target = al_get_target_bitmap();
    int prev_new_flags = al_get_new_display_flags();
    int new_flags = ALLEGRO_WINDOWED;
    if (detach_flags & N_GUI_DETACH_RESIZABLE) new_flags |= ALLEGRO_RESIZABLE;
    if (own_chrome) {
        /* frameless so only our chrome shows, and resizable because
         * al_resize_display and the ALLEGRO_MAXIMIZED flag both need it: that
         * is how the grip and the maximise button drive the OS window. */
        new_flags |= ALLEGRO_FRAMELESS | ALLEGRO_RESIZABLE;
    }
    al_set_new_display_flags(new_flags);
    /* Title and position are set BEFORE creation so the window never appears
     * with Allegro's default title or at the default spot and then visibly
     * jumps. al_set_window_title after the fact stays as the fallback for
     * platforms that ignore the new-window hints. */
    if (win->title[0]) al_set_new_window_title(win->title);
    int prev_pos_x = 0, prev_pos_y = 0;
    al_get_new_window_position(&prev_pos_x, &prev_pos_y);
    if (win->native_pos_x != INT_MIN && win->native_pos_y != INT_MIN) {
        al_set_new_window_position(win->native_pos_x, win->native_pos_y);
    }
    ALLEGRO_DISPLAY* d = al_create_display(dw, dh);
    al_set_new_display_flags(prev_new_flags);
    al_set_new_window_position(prev_pos_x, prev_pos_y);
    al_set_new_window_title("");
    if (prev_target) al_set_target_bitmap(prev_target);
    if (!d) {
        n_log(LOG_ERR, "n_gui_window_detach: al_create_display(%d, %d) failed for window %d", dw, dh, window_id);
        return -1;
    }
    if (win->title[0]) al_set_window_title(d, win->title);
    if (win->native_pos_x != INT_MIN && win->native_pos_y != INT_MIN) {
        al_set_window_position(d, win->native_pos_x, win->native_pos_y);
    }
    /* Keep the window manager from shrinking the window below what the layout
     * can render. Constraints are hints and only apply to resizable displays,
     * so this is best-effort by design: the grip and DISPLAY_RESIZE handler
     * still clamp on our side. A zero max means "no upper bound". */
    if (new_flags & ALLEGRO_RESIZABLE) {
        int cmin_w = (int)(win->min_w + 0.5f);
        int cmin_h = (int)((own_chrome ? win->min_h : win->min_h - _win_tbh(win)) + 0.5f);
        if (cmin_w < 1) cmin_w = 1;
        if (cmin_h < 1) cmin_h = 1;
        if (al_set_window_constraints(d, cmin_w, cmin_h, 0, 0)) {
            al_apply_window_constraints(d, true);
        }
    }
    al_register_event_source(ctx->event_queue, al_get_display_event_source(d));

    /* remember the pop-up geometry so attach can put it back exactly */
    win->saved_x = win->x;
    win->saved_y = win->y;
    win->saved_w = win->w;
    win->saved_h = win->h;
    win->saved_flags = win->flags;

    win->native = d;
    win->detach_flags = detach_flags;
    win->want_native = 1;
    win->native_close_pending = 0;
    /* The window fills its display. With the OS chrome our own title bar (and
     * with it the drag handle and the three buttons) goes away because the
     * window manager already provides them; with N_GUI_DETACH_OWN_CHROME the
     * title bar stays and drives the OS window instead. */
    if (!own_chrome) {
        win->flags |= N_GUI_WIN_FRAMELESS;
    }
    win->x = 0.0f;
    win->y = 0.0f;
    win->w = (float)dw;
    win->h = (float)dh;
    win->native_w = (float)dw;
    win->native_h = (float)dh;
    win->state |= N_GUI_WIN_OPEN;
    win->state &= ~(N_GUI_WIN_MINIMISED | N_GUI_WIN_MAXIMISED | N_GUI_WIN_DRAGGING |
                    N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG);
    win->native_drag_anchored = 0;
    win->tb_buttons.pressed = N_GUI_TB_BTN_NONE;
    win->tb_buttons.hovered = N_GUI_TB_BTN_NONE;
    win->scroll_x = 0.0f;
    win->scroll_y = 0.0f;
    /* capture widget norms against the new size so a user resize of the native
     * window can scale the content (N_GUI_DETACH_SCALE_CONTENT) */
    if (win->w > 0.0f && win->h > 0.0f) {
        list_foreach(wgn, win->widgets) {
            N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
            if (wgt) _n_gui_widget_capture_normalized(win, wgt);
        }
    }
    n_log(LOG_DEBUG, "n_gui_window_detach: window %d ('%s') detached into a %dx%d native window", window_id, win->title, dw, dh);
    return 0;
}

/**
 * @brief Return a detached window to being an in-display pop-up.
 */
int n_gui_window_attach(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) {
        n_log(LOG_ERR, "n_gui_window_attach: no window with id %d", window_id);
        return -1;
    }
    if (!win->native) {
        win->want_native = 0;
        return 0; /* not detached */
    }
    _release_native_window(ctx, win);
    win->want_native = 0;
    win->x = win->saved_x;
    win->y = win->saved_y;
    win->w = win->saved_w;
    win->h = win->saved_h;
    win->flags = win->saved_flags;
    win->scroll_x = 0.0f;
    win->scroll_y = 0.0f;
    if (win->w > 0.0f && win->h > 0.0f) {
        list_foreach(wgn, win->widgets) {
            N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
            if (wgt) _n_gui_widget_capture_normalized(win, wgt);
        }
    }
    n_log(LOG_DEBUG, "n_gui_window_attach: window %d ('%s') is a pop-up again", window_id, win->title);
    return 0;
}

/**
 * @brief Check whether a window currently lives in a native OS window.
 */
int n_gui_window_is_detached(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return 0);
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    return (win && win->native) ? 1 : 0;
}

/**
 * @brief Get the native display backing a window, or NULL when it is a pop-up.
 */
ALLEGRO_DISPLAY* n_gui_window_get_display(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return NULL);
    // cppcheck-suppress constVariablePointer ; returned from non-const API
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    return win ? win->native : NULL;
}

/**
 * @brief Find the window detached into a given display.
 */
// cppcheck-suppress constParameterPointer ; public API uses non-const for consistency
// cppcheck-suppress constParameter ; same check under the pre-2.11 cppcheck id used by CI
int n_gui_window_from_display(N_GUI_CTX* ctx, ALLEGRO_DISPLAY* display) {
    __n_assert(ctx, return -1);
    if (!display) return -1;
    list_foreach(wnode, ctx->windows) {
        const N_GUI_WINDOW* win = (const N_GUI_WINDOW*)wnode->ptr;
        if (win && win->native == display) return win->id;
    }
    return -1;
}

/**
 * @brief Count the windows currently detached into native OS windows.
 */
int n_gui_count_detached(N_GUI_CTX* ctx) {
    __n_assert(ctx, return 0);
    int n = 0;
    list_foreach(wnode, ctx->windows) {
        const N_GUI_WINDOW* win = (const N_GUI_WINDOW*)wnode->ptr;
        if (win && win->native) n++;
    }
    return n;
}

/**
 * @brief Give a detached window's OS window a taskbar / title bar icon.
 */
int n_gui_window_set_native_icons(N_GUI_CTX* ctx, int window_id, ALLEGRO_BITMAP** icons, int num_icons) {
    __n_assert(ctx, return -1);
    __n_assert(icons, return -1);
    if (num_icons < 1) {
        n_log(LOG_ERR, "n_gui_window_set_native_icons: num_icons must be >= 1");
        return -1;
    }
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win || !win->native) {
        n_log(LOG_ERR, "n_gui_window_set_native_icons: window %d is not detached", window_id);
        return -1;
    }
    if (num_icons == 1)
        al_set_display_icon(win->native, icons[0]);
    else
        al_set_display_icons(win->native, num_icons, icons);
    return 0;
}

/**
 * @brief Check whether a detached window is currently not drawable.
 */
int n_gui_window_native_is_hidden(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return 0);
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win || !win->native) return 0;
    if (win->native_halted) return 1;
    return (al_get_display_flags(win->native) & ALLEGRO_MINIMIZED) ? 1 : 0;
}

/**
 * @brief Set the close button callback for a window.
 */
void n_gui_window_set_close_callback(N_GUI_CTX* ctx, int window_id, void (*on_close)(int, void*), void* user_data) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->tb_buttons.on_close = on_close;
    win->tb_buttons.on_close_user_data = user_data;
}

/**
 * @brief Set the content-draw callback for a window.
 *
 * The callback runs during n_gui_draw immediately after the window's own
 * content is drawn, at the window's z-order, so any custom rendering it
 * performs is occluded by windows that draw on top of this one.
 */
void n_gui_window_set_content_draw_callback(N_GUI_CTX* ctx, int window_id, void (*on_content_draw)(int, void*), void* user_data) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->on_content_draw = on_content_draw;
    win->on_content_draw_data = user_data;
}

/**
 * @brief Set the minimize button callback for a window.
 */
void n_gui_window_set_minimize_callback(N_GUI_CTX* ctx, int window_id, void (*on_minimize)(int, void*), void* user_data) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->tb_buttons.on_minimize = on_minimize;
    win->tb_buttons.on_minimize_user_data = user_data;
}

/**
 * @brief Set the maximize button callback for a window.
 */
void n_gui_window_set_maximize_callback(N_GUI_CTX* ctx, int window_id, void (*on_maximize)(int, void*), void* user_data) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->tb_buttons.on_maximize = on_maximize;
    win->tb_buttons.on_maximize_user_data = user_data;
}

/**
 * @brief Set the theme for titlebar minimize/maximize buttons on a specific window.
 */
void n_gui_window_set_tb_btn_theme(N_GUI_CTX* ctx, int window_id, N_GUI_THEME theme) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->tb_buttons.btn_theme = theme;
}

/**
 * @brief Set the theme for the titlebar close button on a specific window.
 */
void n_gui_window_set_tb_close_theme(N_GUI_CTX* ctx, int window_id, N_GUI_THEME theme) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->tb_buttons.close_theme = theme;
}

/**
 * @brief Set optional bitmap overlays on a titlebar button.
 */
void n_gui_window_set_tb_button_bitmaps(N_GUI_CTX* ctx, int window_id, int btn_type, ALLEGRO_BITMAP* normal, ALLEGRO_BITMAP* hover, ALLEGRO_BITMAP* active) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    if (btn_type == N_GUI_TB_BTN_MINIMIZE) {
        win->tb_buttons.minimize_bitmap = normal;
        win->tb_buttons.minimize_hover_bitmap = hover;
        win->tb_buttons.minimize_active_bitmap = active;
    } else if (btn_type == N_GUI_TB_BTN_MAXIMIZE) {
        win->tb_buttons.maximize_bitmap = normal;
        win->tb_buttons.maximize_hover_bitmap = hover;
        win->tb_buttons.maximize_active_bitmap = active;
    } else if (btn_type == N_GUI_TB_BTN_CLOSE) {
        win->tb_buttons.close_bitmap = normal;
        win->tb_buttons.close_hover_bitmap = hover;
        win->tb_buttons.close_active_bitmap = active;
    }
}

/* Return the group ordinal for a window's z-order:
 *   0 = ALWAYS_BEHIND, 1 = FIXED, 2 = NORMAL, 3 = ALWAYS_ON_TOP, 4 = POPUP.
 * Ordering is ALWAYS_BEHIND -> FIXED -> NORMAL -> ALWAYS_ON_TOP -> POPUP. */
static int _zorder_group(const N_GUI_WINDOW* w) {
    if (!w) return 2; /* treat NULL as NORMAL */
    switch (w->z_order) {
        case N_GUI_ZORDER_ALWAYS_BEHIND:
            return 0;
        case N_GUI_ZORDER_FIXED:
            return 1;
        case N_GUI_ZORDER_ALWAYS_ON_TOP:
            return 3;
        case N_GUI_ZORDER_POPUP:
            return 4;
        default:
            return 2;
    }
}

/**
 * @brief Sort windows list by z-order: ALWAYS_BEHIND first, then FIXED (by z_value),
 *        then NORMAL, then ALWAYS_ON_TOP, then POPUP. Within each group, preserve
 *        relative order. Uses stable insertion sort since window lists are typically small.
 */
static void _sort_windows_by_zorder(N_GUI_CTX* ctx) {
    if (!ctx || !ctx->windows || ctx->windows->nb_items < 2) return;

    /* Sort by (group, z_value) so that group ordering is always respected:
     * ALWAYS_BEHIND windows always precede FIXED, which always precede NORMAL,
     * which always precede ALWAYS_ON_TOP, regardless of z_value magnitude.
     * Within the FIXED group, windows are ordered by z_value (lower = behind).
     * Within the same (group, z_value), list order is preserved (stable sort). */

    /* collect into array for stable sort */
    int n = (int)ctx->windows->nb_items;
    N_GUI_WINDOW** arr = NULL;
    Malloc(arr, N_GUI_WINDOW*, (size_t)n);
    if (!arr) return;

    int idx = 0;
    list_foreach(node, ctx->windows) {
        arr[idx++] = (N_GUI_WINDOW*)node->ptr;
    }

    /* stable insertion sort by (group, z_value) */
    for (int i = 1; i < n; i++) {
        N_GUI_WINDOW* tmp = arr[i];
        int gi = _zorder_group(tmp);
        int j = i - 1;
        while (j >= 0) {
            int gj = _zorder_group(arr[j]);
            /* z_value secondary sort applies only within the FIXED group (ordinal 1) */
            if (gj > gi || (gj == gi && gi == 1 && arr[j]->z_value > tmp->z_value)) {
                arr[j + 1] = arr[j];
                j--;
            } else {
                break;
            }
        }
        arr[j + 1] = tmp;
    }

    /* rebuild list in sorted order */
    /* detach all nodes without destroying windows */
    while (ctx->windows->nb_items > 0) {
        LIST_NODE* node = ctx->windows->start;
        node->destroy_func = NULL;
        remove_list_node_f(ctx->windows, node);
    }
    for (int i = 0; i < n; i++) {
        list_push(ctx->windows, arr[i], _destroy_window);
    }
    Free(arr);
}

/**
 * @brief Bring a window to the front (top of draw order).
 *        Respects z-order constraints: ALWAYS_BEHIND, FIXED and
 *        ALWAYS_ON_TOP windows are not moved. NORMAL and POPUP windows
 *        are raised WITHIN their own z-group (the stable re-sort keeps
 *        group boundaries). After raising, the window list is re-sorted.
 */
void n_gui_raise_window(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return);
    LIST_NODE* node = _find_window_node(ctx, window_id);
    if (!node) return;
    N_GUI_WINDOW* win = (N_GUI_WINDOW*)node->ptr;
    /* only NORMAL and POPUP windows can be freely raised (within their group) */
    if (win->z_order != N_GUI_ZORDER_NORMAL && win->z_order != N_GUI_ZORDER_POPUP) return;
    /* remove from list without destroying, then push to end */
    node->destroy_func = NULL;
    remove_list_node_f(ctx->windows, node);
    list_push(ctx->windows, win, _destroy_window);
    _sort_windows_by_zorder(ctx);
}

/**
 * @brief Lower a window to the bottom of the draw order.
 *        Only NORMAL windows can be lowered.
 */
void n_gui_lower_window(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return);
    LIST_NODE* node = _find_window_node(ctx, window_id);
    if (!node) return;
    N_GUI_WINDOW* win = (N_GUI_WINDOW*)node->ptr;
    if (win->z_order != N_GUI_ZORDER_NORMAL) return;
    node->destroy_func = NULL;
    remove_list_node_f(ctx->windows, node);
    list_unshift(ctx->windows, win, _destroy_window);
    _sort_windows_by_zorder(ctx);
}

/**
 * @brief Get the id of the frontmost open, non-minimized window.
 *
 * The window list is kept sorted by z-order group with the topmost group
 * last, so the last open window in list order is the one drawn on top.
 * Returns -1 when no window is open.
 */
int n_gui_get_topmost_open_window(const N_GUI_CTX* ctx) {
    __n_assert(ctx, return -1);
    if (!ctx->windows) return -1;
    for (const LIST_NODE* node = ctx->windows->end; node; node = node->prev) {
        const N_GUI_WINDOW* win = (const N_GUI_WINDOW*)node->ptr;
        if (win && (win->state & N_GUI_WIN_OPEN) && !(win->state & N_GUI_WIN_MINIMISED)) {
            return win->id;
        }
    }
    return -1;
}

/**
 * @brief Give a window keyboard focus and bring it to the front.
 *
 * Raises the window within its z-order group (so it is drawn on top of its
 * peers) and moves keyboard focus to its first focusable, visible, enabled
 * widget. If the window has no focusable widget, focus is cleared so global
 * key bindings still fire. Does nothing if the window id is unknown.
 */
void n_gui_focus_window(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    n_gui_raise_window(ctx, window_id);
    int target = -1;
    if (win->widgets) {
        list_foreach(node, win->widgets) {
            const N_GUI_WIDGET* w = (const N_GUI_WIDGET*)node->ptr;
            if (w && w->visible && w->enabled && _is_focusable_type(w->type)) {
                target = w->id;
                break;
            }
        }
    }
    n_gui_set_focus(ctx, target);
}

/**
 * @brief Set window z-order mode and value.
 *        Re-sorts the window list after changing.
 */
void n_gui_window_set_zorder(N_GUI_CTX* ctx, int window_id, int z_mode, int z_value) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    if (z_mode != N_GUI_ZORDER_NORMAL && z_mode != N_GUI_ZORDER_ALWAYS_ON_TOP &&
        z_mode != N_GUI_ZORDER_ALWAYS_BEHIND && z_mode != N_GUI_ZORDER_FIXED &&
        z_mode != N_GUI_ZORDER_POPUP) {
        n_log(LOG_ERR, "n_gui_window_set_zorder: unknown z_mode %d for window %d (expected N_GUI_ZORDER_NORMAL, N_GUI_ZORDER_ALWAYS_ON_TOP, N_GUI_ZORDER_ALWAYS_BEHIND, N_GUI_ZORDER_FIXED, or N_GUI_ZORDER_POPUP), falling back to N_GUI_ZORDER_NORMAL", z_mode, window_id);
        z_mode = N_GUI_ZORDER_NORMAL;
    }
    win->z_order = z_mode;
    win->z_value = z_value;
    _sort_windows_by_zorder(ctx);
}

/**
 * @brief Get window z-order mode.
 */
int n_gui_window_get_zorder(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return N_GUI_ZORDER_NORMAL);
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return N_GUI_ZORDER_NORMAL;
    return win->z_order;
}

/**
 * @brief Get window z-value (for N_GUI_ZORDER_FIXED mode).
 */
int n_gui_window_get_zvalue(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return 0);
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return 0;
    return win->z_value;
}

/**
 * @brief Add a window with automatic sizing (use n_gui_window_autosize after adding widgets)
 * @return the window id, or -1 on error
 */
int n_gui_add_window_auto(N_GUI_CTX* ctx, const char* title, float x, float y) {
    /* start with minimum size, will be expanded by n_gui_window_autosize */
    return n_gui_add_window(ctx, title, x, y, ctx->style.min_win_w, ctx->style.min_win_h);
}

/**
 * @brief Toggle window visibility (show if hidden, hide if shown)
 */
void n_gui_toggle_window(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (win) win->state ^= N_GUI_WIN_OPEN;
}

/**
 * @brief Check if a window is currently visible
 * @return 1 if open, 0 if closed
 */
int n_gui_window_is_open(N_GUI_CTX* ctx, int window_id) {
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (win) return (win->state & N_GUI_WIN_OPEN) ? 1 : 0;
    return 0;
}

/**
 * @brief Set feature flags on a window
 */
void n_gui_window_set_flags(N_GUI_CTX* ctx, int window_id, int flags) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (win) win->flags = flags;
}

/**
 * @brief Get feature flags of a window
 * @return the flags, or 0
 */
int n_gui_window_get_flags(N_GUI_CTX* ctx, int window_id) {
    const N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (win) return win->flags;
    return 0;
}

/**
 * @brief Recompute and apply minimum-fit size for a window based on its current widgets.
 * Adds padding around the furthest widget extents.
 */
void n_gui_window_autosize(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win || !win->widgets) return;

    float max_right = 0.0f;
    float max_bottom = 0.0f;
    float pad = 20.0f;

    list_foreach(node, win->widgets) {
        const N_GUI_WIDGET* wgt = (const N_GUI_WIDGET*)node->ptr;
        if (!wgt) continue;
        float r = wgt->x + wgt->w;
        float b = wgt->y + wgt->h;
        if (r > max_right) max_right = r;
        if (b > max_bottom) max_bottom = b;
    }

    float new_w = max_right + pad;
    float new_h = max_bottom + pad + _win_tbh(win);
    if (new_w < win->min_w) new_w = win->min_w;
    if (new_h < win->min_h) new_h = win->min_h;
    win->w = new_w;
    win->h = new_h;
}

/**
 * @brief Configure auto-fit behavior for a dialog window.
 *        Store the flags and border value in the window struct.
 */
void n_gui_window_set_autofit(N_GUI_CTX* ctx, int window_id, int autofit_flags, float border) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) {
        n_log(LOG_ERR, "n_gui_window_set_autofit: window %d not found", window_id);
        return;
    }
    win->autofit_flags = autofit_flags;
    win->autofit_border = border;
    /* capture current position as the insertion point for centering */
    win->autofit_origin_x = win->x;
    win->autofit_origin_y = win->y;
}

/**
 * @brief Trigger auto-fit recalculation for a window.
 *        Computes content bounding box from visible widgets and adjusts
 *        window dimensions based on autofit_flags and autofit_border.
 */
void n_gui_window_apply_autofit(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    if (win->autofit_flags == 0) return;
    if (!win->widgets) return;

    /* compute content bounding box from visible widgets */
    float max_right = 0.0f;
    float max_bottom = 0.0f;
    list_foreach(node, win->widgets) {
        const N_GUI_WIDGET* wgt = (const N_GUI_WIDGET*)node->ptr;
        if (!wgt || !wgt->visible) continue;
        float r = wgt->x + wgt->w;
        float b = wgt->y + wgt->h;
        if (r > max_right) max_right = r;
        if (b > max_bottom) max_bottom = b;
    }

    float border = win->autofit_border;
    float tbh = _win_tbh(win);

    int center = (win->autofit_flags & N_GUI_AUTOFIT_CENTER) ? 1 : 0;

    /* apply width adjustment */
    if (win->autofit_flags & N_GUI_AUTOFIT_W) {
        float needed_w = max_right + border * 2.0f;
        if (needed_w < win->min_w) needed_w = win->min_w;
        if (center) {
            win->x = win->autofit_origin_x - needed_w / 2.0f;
        } else if (win->autofit_flags & N_GUI_AUTOFIT_EXPAND_LEFT) {
            win->x -= (needed_w - win->w);
        }
        win->w = needed_w;
    }

    /* apply height adjustment */
    if (win->autofit_flags & N_GUI_AUTOFIT_H) {
        float needed_h = max_bottom + border * 2.0f + tbh;
        if (needed_h < win->min_h) needed_h = win->min_h;
        if (center) {
            win->y = win->autofit_origin_y - needed_h / 2.0f;
        } else if (win->autofit_flags & N_GUI_AUTOFIT_EXPAND_UP) {
            win->y -= (needed_h - win->h);
        }
        win->h = needed_h;
    }

    /* update content extents for scrollbar calculations */
    win->content_w = max_right;
    win->content_h = max_bottom;
}

/*! helper: recompute content extents for a window (for scrollbar support) */
static void _window_update_content_size(N_GUI_WINDOW* win, ALLEGRO_FONT* default_font) {
    if (!win || !win->widgets) return;
    float max_right = 0.0f;
    float max_bottom = 0.0f;
    list_foreach(node, win->widgets) {
        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)node->ptr;
        if (!wgt || !wgt->visible) continue;
        float r = wgt->x + wgt->w;
        float b = wgt->y + wgt->h;
        /* for labels in auto-scrollbar windows, use actual text pixel width
         * so that horizontal scrollbar appears when text overflows */
        if (wgt->type == N_GUI_TYPE_LABEL && (win->flags & N_GUI_WIN_AUTO_SCROLLBAR)) {
            const N_GUI_LABEL_DATA* lb = (const N_GUI_LABEL_DATA*)wgt->data;
            if (lb && lb->text[0] && lb->align != N_GUI_ALIGN_JUSTIFIED) {
                ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;
                if (font) {
                    float tw = _text_w(font, lb->text);
                    float text_r = wgt->x + tw + 8.0f; /* small padding */
                    if (text_r > r) r = text_r;
                }
            }
        }
        if (r > max_right) max_right = r;
        if (b > max_bottom) max_bottom = b;
    }
    win->content_w = max_right;
    win->content_h = max_bottom;
}

/* WIDGET CREATION */

/**
 * @brief Add a button widget to a window
 * @return widget id, or -1 on error
 */
int n_gui_add_button(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, int shape, void (*on_click)(int, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_BUTTON, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_BUTTON_DATA* bd = NULL;
    Malloc(bd, N_GUI_BUTTON_DATA, 1);
    __n_assert(bd, Free(wgt); return -1);
    if (label) {
        strncpy(bd->label, label, N_GUI_ID_MAX - 1);
        bd->label[N_GUI_ID_MAX - 1] = '\0';
    }
    bd->bitmap = NULL;
    bd->bitmap_hover = NULL;
    bd->bitmap_active = NULL;
    bd->shape = shape;
    bd->glyph = N_GUI_GLYPH_NONE;
    bd->toggle_mode = 0;
    bd->toggled = 0;
    bd->keycode = 0;
    bd->key_modifiers = 0;
    bd->on_click = on_click;
    bd->user_data = user_data;
    wgt->data = bd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a bitmap-based button
 * @return widget id, or -1 on error
 */
int n_gui_add_button_bitmap(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, ALLEGRO_BITMAP* normal, ALLEGRO_BITMAP* hover, ALLEGRO_BITMAP* active, void (*on_click)(int, void*), void* user_data) {
    int id = n_gui_add_button(ctx, window_id, label, x, y, w, h, N_GUI_SHAPE_BITMAP, on_click, user_data);
    if (id < 0) return -1;
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, id);
    if (wgt && wgt->data) {
        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
        bd->bitmap = normal;
        bd->bitmap_hover = hover;
        bd->bitmap_active = active;
    }
    return id;
}

/**
 * @brief Add a toggle button widget (stays clicked/unclicked on single click)
 * @param ctx the GUI context
 * @param window_id id of the parent window
 * @param label button label text
 * @param x horizontal position
 * @param y vertical position
 * @param w widget width
 * @param h widget height
 * @param shape button shape (N_GUI_SHAPE_*)
 * @param initial_state 0 = off, 1 = on
 * @param on_click callback invoked on click (or NULL)
 * @param user_data user data passed to on_click callback
 * @return widget id, or -1 on error
 */
int n_gui_add_toggle_button(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, int shape, int initial_state, void (*on_click)(int, void*), void* user_data) {
    int id = n_gui_add_button(ctx, window_id, label, x, y, w, h, shape, on_click, user_data);
    if (id < 0) return -1;
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, id);
    if (wgt && wgt->data) {
        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
        bd->toggle_mode = 1;
        bd->toggled = initial_state ? 1 : 0;
    }
    return id;
}

/**
 * @brief Check if a toggle button is currently in the "on" state
 * @return 1 if toggled on, 0 if off or not a toggle button
 */
int n_gui_button_is_toggled(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        return ((N_GUI_BUTTON_DATA*)w->data)->toggled;
    }
    return 0;
}

/**
 * @brief Replace the label text drawn on a button.
 * No-op on non-button widgets or NULL label.
 */
void n_gui_button_set_label(N_GUI_CTX* ctx, int widget_id, const char* label) {
    if (!label) return;
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)w->data;
        snprintf(bd->label, sizeof(bd->label), "%s", label);
    }
}

/**
 * @brief Draw a glyph on a button instead of its text label.
 *
 * The glyph is built from primitives in the button's text colour, so it needs
 * no bitmap, follows the active theme, and stays crisp at any button size.
 * Meant for the small square buttons where a label does not fit: a row of
 * move-up / move-down / duplicate actions, arrow toolbars, close buttons.
 *
 * The label is kept, so a caller can set both and switch back with
 * N_GUI_GLYPH_NONE. Out-of-range glyph values and non-button widgets are
 * ignored.
 *
 * @param ctx the GUI context
 * @param widget_id the button widget
 * @param glyph one of the N_GUI_GLYPH_* values
 */
void n_gui_button_set_glyph(N_GUI_CTX* ctx, int widget_id, int glyph) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (glyph < 0 || glyph >= N_GUI_GLYPH_COUNT) return;
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        ((N_GUI_BUTTON_DATA*)w->data)->glyph = glyph;
        if (ctx) ctx->dirty = 1;
    }
}

/**
 * @brief Get the glyph currently drawn on a button
 * @param ctx the GUI context
 * @param widget_id the button widget
 * @return the N_GUI_GLYPH_* value, or N_GUI_GLYPH_NONE when the button draws
 *         its label or the widget is not a button
 */
int n_gui_button_get_glyph(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        return ((const N_GUI_BUTTON_DATA*)w->data)->glyph;
    }
    return N_GUI_GLYPH_NONE;
}

/**
 * @brief Set (or clear) the tooltip text of any widget.
 *
 * The tooltip appears after the pointer rests on the widget for
 * style.tooltip_delay seconds and hides on movement, click, or leave.
 *
 * @param ctx the GUI context
 * @param widget_id id of the target widget
 * @param text tooltip text; NULL or "" clears it
 */
void n_gui_set_widget_tooltip(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w) return;
    FreeNoLog(w->tooltip); /* replace any previous value */
    w->tooltip = (text && text[0]) ? strdup(text) : NULL;
}

/* Apply a saved datagrid column layout (order/visibility/width) to a live grid:
 * visible/width are per-column at index d, order[d] is the physical column shown at
 * display position d (grids start in identity display order). Mirrors the capture in
 * n_gui_save_layout_json so the round-trip is exact. */
static void _datagrid_apply_columns(N_GUI_CTX* ctx, int widget_id, int ncols, const int* order, const int* visible, const float* width) {
    int dp;
    for (dp = 0; dp < ncols; dp++) {
        n_gui_datagrid_set_column_visible(ctx, widget_id, dp, visible[dp]);
        if (width[dp] > 0.0f)
            n_gui_datagrid_set_column_width(ctx, widget_id, dp, width[dp]);
    }
    for (dp = 0; dp < ncols; dp++) {
        int cur;
        for (cur = dp; cur < ncols; cur++)
            if (n_gui_datagrid_display_to_physical(ctx, widget_id, cur) == order[dp])
                break;
        if (cur < ncols && cur != dp)
            n_gui_datagrid_move_column(ctx, widget_id, cur, dp);
    }
}

/* Apply the first unconsumed pending-widget entry matching (window title, key) to a
 * freshly keyed widget, then mark it consumed (the widget-level analog of the
 * pending_layout apply in n_gui_add_window). */
static void _apply_pending_widget(N_GUI_CTX* ctx, const char* title, const N_GUI_WIDGET* wgt) {
    int i;
    if (!ctx || !ctx->pending_widgets || !title || !wgt || !wgt->persist_key[0])
        return;
    for (i = 0; i < ctx->pending_widgets_count; i++) {
        N_GUI_PENDING_WIDGET* p = &ctx->pending_widgets[i];
        if (p->consumed) continue;
        if (strncmp(p->title, title, N_GUI_ID_MAX) != 0) continue;
        if (strncmp(p->key, wgt->persist_key, N_GUI_ID_MAX) != 0) continue;
        if (p->kind != wgt->type) continue; /* the widget type changed since the save */
        if (wgt->type == N_GUI_TYPE_SPLITPANE) {
            n_gui_splitpane_set_ratio(ctx, wgt->id, p->ratio);
        } else if (wgt->type == N_GUI_TYPE_DATAGRID) {
            int live = (int)n_gui_datagrid_get_column_count(ctx, wgt->id);
            if (live == p->ncols && p->ncols > 0)
                _datagrid_apply_columns(ctx, wgt->id, p->ncols, p->order, p->visible, p->width);
        }
        p->consumed = 1;
        return;
    }
}

void n_gui_widget_set_persist_key(N_GUI_CTX* ctx, int widget_id, const char* key) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    float ox = 0.0f, oy = 0.0f;
    const N_GUI_WINDOW* win;
    if (!w) return;
    snprintf(w->persist_key, sizeof(w->persist_key), "%s", key ? key : "");
    if (!w->persist_key[0]) return;
    /* if a saved layout was already loaded, restore this widget's state now */
    win = _find_widget_window(ctx, widget_id, &ox, &oy);
    if (win)
        _apply_pending_widget(ctx, win->title, w);
}

const char* n_gui_widget_get_persist_key(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    return w ? w->persist_key : NULL;
}

/**
 * @brief Report the tooltip phase.
 *
 * Hosts that cache the GUI render must keep invalidating/redrawing while
 * this returns 1 (arming), or the bubble can never appear - it shows only
 * after the pointer STOPS, when no more input events trigger redraws. A
 * steady 2 (shown) needs one re-render on the transition and none after;
 * re-rendering every tick during 2 would burn the cache for nothing.
 *
 * @param ctx the GUI context
 * @return 0 = none/disabled, 1 = arming, 2 = shown
 */
int n_gui_tooltip_pending(N_GUI_CTX* ctx) {
    if (!ctx || ctx->style.tooltip_delay <= 0.0f || ctx->tooltip_widget_id < 0) return 0;
    return (al_get_time() - ctx->tooltip_armed_at >= (double)ctx->style.tooltip_delay) ? 2 : 1;
}

/*! helper: top-most visible tooltip-bearing widget under a gui-space point (-1 = none) */
static int _tooltip_widget_at(N_GUI_CTX* ctx, float px, float py) {
    int found = -1;
    /* ctx->windows is ordered back to front: the last hit wins */
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!win || !(win->state & N_GUI_WIN_OPEN)) continue;
        /* display routing: only windows on the pass the pointer is on */
        if (!_win_on_pass(ctx, win)) continue;
        float ox = win->x - win->scroll_x;
        float oy = win->y + _win_tbh(win) - win->scroll_y;
        list_foreach(wgn, win->widgets) {
            const N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
            if (!wgt || !wgt->visible || !wgt->tooltip || !wgt->tooltip[0]) continue;
            if (_point_in_rect(px, py, ox + wgt->x, oy + wgt->y, wgt->w, wgt->h))
                found = wgt->id;
        }
    }
    return found;
}

/**
 * @brief Skin a button (or toggle button) with per-state bitmaps.
 *
 * Switches the button shape to N_GUI_SHAPE_BITMAP and assigns the three
 * state bitmaps; a toggled-on toggle button renders `active` persistently
 * (the draw path already maps toggled to the ACTIVE visual state). The
 * bitmaps stay owned by the caller, exactly like n_gui_add_button_bitmap;
 * the label keeps drawing on top, so pass "" at creation for icon-only
 * buttons. No-op on non-button widgets.
 *
 * @param ctx the GUI context
 * @param widget_id id of the target button widget
 * @param normal bitmap for the idle state
 * @param hover bitmap for the hover state (NULL = normal)
 * @param active bitmap for the pressed / toggled-on state (NULL = normal)
 */
void n_gui_button_set_state_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* normal, ALLEGRO_BITMAP* hover, ALLEGRO_BITMAP* active) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)w->data;
        bd->shape = N_GUI_SHAPE_BITMAP;
        bd->bitmap = normal;
        bd->bitmap_hover = hover;
        bd->bitmap_active = active;
    }
}

/**
 * @brief Set the toggle state of a button
 */
void n_gui_button_set_toggled(N_GUI_CTX* ctx, int widget_id, int toggled) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        ((N_GUI_BUTTON_DATA*)w->data)->toggled = toggled ? 1 : 0;
    }
}

/**
 * @brief Enable or disable toggle mode on a button
 * @param ctx the GUI context
 * @param widget_id id of the target button widget
 * @param toggle_mode 0 = momentary, 1 = toggle
 */
void n_gui_button_set_toggle_mode(N_GUI_CTX* ctx, int widget_id, int toggle_mode) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)w->data;
        bd->toggle_mode = toggle_mode ? 1 : 0;
        if (!bd->toggle_mode) bd->toggled = 0;
    }
}

/**
 * @brief Bind a keyboard key with optional modifier requirements to a button.
 *
 * When the key is pressed (with matching modifiers) and no text input widget
 * has focus, the button's on_click callback is triggered (and toggle state
 * flipped for toggle buttons). Pass keycode 0 to unbind.
 * When modifiers is 0, any modifier state matches (no modifier requirement).
 * When modifiers is non-zero, only an exact match triggers the keybind.
 * Example: ALLEGRO_KEY_A with ALLEGRO_KEYMOD_SHIFT | ALLEGRO_KEYMOD_CTRL
 */
void n_gui_button_set_keycode(N_GUI_CTX* ctx, int widget_id, int keycode, int modifiers) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)w->data;
        bd->keycode = keycode;
        bd->key_modifiers = modifiers & N_GUI_KEY_MOD_MASK;
    }
}

/*! @brief Set a focused key binding on a button. */
void n_gui_button_set_keycode_focused(N_GUI_CTX* ctx, int widget_id, int keycode, int modifiers, const int* sources, int source_count) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_BUTTON && w->data) {
        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)w->data;
        bd->keycode = keycode;
        bd->key_modifiers = modifiers & N_GUI_KEY_MOD_MASK;
        bd->key_focus_only = 1;
        int count = source_count < N_GUI_KEY_SOURCES_MAX ? source_count : N_GUI_KEY_SOURCES_MAX;
        for (int i = 0; i < count; i++) {
            bd->key_sources[i] = sources[i];
        }
        if (count < N_GUI_KEY_SOURCES_MAX) {
            bd->key_sources[count] = -1;
        }
    }
}

/**
 * @brief Add a slider widget
 * @return widget id, or -1 on error
 */
int n_gui_add_slider(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, double min_val, double max_val, double initial, int mode, void (*on_change)(int, double, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_SLIDER, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_SLIDER_DATA* sd = NULL;
    Malloc(sd, N_GUI_SLIDER_DATA, 1);
    __n_assert(sd, Free(wgt); return -1);
    if (mode == N_GUI_SLIDER_PERCENT) {
        sd->min_val = 0.0;
        sd->max_val = 100.0;
    } else {
        sd->min_val = min_val;
        sd->max_val = max_val;
    }
    sd->step = 0.0; /* 0 = no step constraint (treated as continuous, snaps with step=1 only when explicitly set) */
    sd->value = _clamp(initial, sd->min_val, sd->max_val);
    sd->mode = mode;
    sd->orientation = N_GUI_SLIDER_H;
    sd->on_change = on_change;
    sd->user_data = user_data;
    sd->value_format[0] = '\0'; /* empty = legacy defaults */
    sd->value_visible = 1;      /* built-in readout on by default */
    wgt->data = sd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a vertical slider widget
 * @return widget id, or -1 on error
 */
int n_gui_add_vslider(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, double min_val, double max_val, double initial, int mode, void (*on_change)(int, double, void*), void* user_data) {
    int id = n_gui_add_slider(ctx, window_id, x, y, w, h, min_val, max_val, initial, mode, on_change, user_data);
    if (id < 0) return -1;
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, id);
    if (wgt && wgt->data) {
        ((N_GUI_SLIDER_DATA*)wgt->data)->orientation = N_GUI_SLIDER_V;
    }
    return id;
}

/**
 * @brief Add a text area widget
 * @return widget id, or -1 on error
 */
int n_gui_add_textarea(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int multiline, size_t char_limit, void (*on_change)(int, const char*, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_TEXTAREA, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_TEXTAREA_DATA* td = NULL;
    Malloc(td, N_GUI_TEXTAREA_DATA, 1);
    __n_assert(td, Free(wgt); return -1);
    memset(td, 0, sizeof(*td));
    td->char_limit = (char_limit > 0) ? char_limit : N_GUI_TEXT_MAX - 1;
    td->text_alloc = td->char_limit + 1;
    Malloc(td->text, char, td->text_alloc);
    __n_assert(td->text, Free(td); Free(wgt); return -1);
    td->text[0] = '\0';
    td->text_len = 0;
    td->multiline = multiline;
    td->cursor_pos = 0;
    td->sel_start = 0;
    td->sel_end = 0;
    td->scroll_y = 0;
    td->scroll_x = 0.0f;
    td->cursor_time = 0.0;
    td->bg_bitmap = NULL;
    td->mask_char = '\0';
    td->on_change = on_change;
    td->user_data = user_data;
    wgt->data = td;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a checkbox widget
 * @return widget id, or -1 on error
 */
int n_gui_add_checkbox(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, int initial_checked, void (*on_toggle)(int, int, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_CHECKBOX, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_CHECKBOX_DATA* cd = NULL;
    Malloc(cd, N_GUI_CHECKBOX_DATA, 1);
    __n_assert(cd, Free(wgt); return -1);
    if (label) {
        strncpy(cd->label, label, N_GUI_ID_MAX - 1);
        cd->label[N_GUI_ID_MAX - 1] = '\0';
    }
    cd->checked = initial_checked ? 1 : 0;
    cd->on_toggle = on_toggle;
    cd->user_data = user_data;
    wgt->data = cd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a scrollbar widget
 * @return widget id, or -1 on error
 */
int n_gui_add_scrollbar(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int orientation, int shape, double content_size, double viewport_size, void (*on_scroll)(int, double, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_SCROLLBAR, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_SCROLLBAR_DATA* sb = NULL;
    Malloc(sb, N_GUI_SCROLLBAR_DATA, 1);
    __n_assert(sb, Free(wgt); return -1);
    sb->orientation = orientation;
    sb->content_size = content_size > 0 ? content_size : 1;
    sb->viewport_size = viewport_size > 0 ? viewport_size : 1;
    sb->scroll_pos = 0;
    sb->shape = shape;
    sb->on_scroll = on_scroll;
    sb->user_data = user_data;
    wgt->data = sb;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a listbox widget
 * @param ctx the GUI context
 * @param window_id id of the parent window
 * @param x horizontal position
 * @param y vertical position
 * @param w widget width
 * @param h widget height
 * @param selection_mode N_GUI_SELECT_NONE, N_GUI_SELECT_SINGLE, or N_GUI_SELECT_MULTIPLE
 * @param on_select callback invoked when an item is selected (or NULL)
 * @param user_data user data passed to on_select callback
 * @return widget id, or -1 on error
 */
int n_gui_add_listbox(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int selection_mode, void (*on_select)(int, int, int, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_LISTBOX, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_LISTBOX_DATA* ld = NULL;
    Malloc(ld, N_GUI_LISTBOX_DATA, 1);
    __n_assert(ld, Free(wgt); return -1);
    ld->items = NULL;
    ld->nb_items = 0;
    ld->items_capacity = 0;
    ld->selection_mode = selection_mode;
    ld->scroll_offset = 0;
    ld->item_height = ctx->style.listbox_default_item_height;
    ld->on_select = on_select;
    ld->user_data = user_data;
    ld->hover_row = -1;
    wgt->data = ld;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a split pane widget: a draggable divider between two regions
 * @param ctx the GUI context
 * @param window_id the parent window id
 * @param x widget x within the window content area
 * @param y widget y within the window content area
 * @param w widget width
 * @param h widget height
 * @param orientation N_GUI_SPLIT_VERTICAL or N_GUI_SPLIT_HORIZONTAL
 * @param ratio initial divider position as a fraction (0..1)
 * @param on_change callback fired while the divider is dragged, or NULL
 * @param user_data user data passed to on_change
 * @return widget id, or -1 on error
 */
int n_gui_add_splitpane(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int orientation, float ratio, void (*on_change)(int, float, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_SPLITPANE, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_SPLITPANE_DATA* sd = NULL;
    Malloc(sd, N_GUI_SPLITPANE_DATA, 1);
    __n_assert(sd, Free(wgt); return -1);
    sd->orientation = (orientation == N_GUI_SPLIT_HORIZONTAL) ? N_GUI_SPLIT_HORIZONTAL : N_GUI_SPLIT_VERTICAL;
    sd->min_ratio = 0.05f;
    sd->max_ratio = 0.95f;
    sd->ratio = (ratio < sd->min_ratio) ? sd->min_ratio : (ratio > sd->max_ratio) ? sd->max_ratio
                                                                                  : ratio;
    sd->divider = 6.0f;
    sd->on_change = on_change;
    sd->user_data = user_data;
    wgt->data = sd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a read-only hexadecimal byte viewer
 * @param ctx the GUI context
 * @param window_id the parent window id
 * @param x widget x within the window content area
 * @param y widget y within the window content area
 * @param w widget width
 * @param h widget height
 * @return widget id, or -1 on error
 */
int n_gui_add_hexview(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_HEXVIEW, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_HEXVIEW_DATA* hd = NULL;
    Malloc(hd, N_GUI_HEXVIEW_DATA, 1);
    __n_assert(hd, Free(wgt); return -1);
    hd->data = NULL;
    hd->len = 0;
    hd->bytes_per_row = 16;
    hd->scroll_offset = 0;
    wgt->data = hd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a read-only syntax-highlighted text view
 * @param ctx the GUI context
 * @param window_id the parent window id
 * @param x widget x within the window content area
 * @param y widget y within the window content area
 * @param w widget width
 * @param h widget height
 * @param mode highlighting mode (N_GUI_SYNTAX_PLAIN, _HTTP, _JSON, _XML, _YAML, _JS or _DIFF)
 * @return widget id, or -1 on error
 */
int n_gui_add_syntaxview(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int mode) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_SYNTAXVIEW, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_SYNTAXVIEW_DATA* yd = NULL;
    Malloc(yd, N_GUI_SYNTAXVIEW_DATA, 1);
    __n_assert(yd, Free(wgt); return -1);
    yd->text = NULL;
    yd->len = 0;
    yd->mode = mode;
    yd->scroll_offset = 0;
    yd->h_scroll = 0.0f;
    yd->h_scroll_dragging = 0;
    yd->sel_start = -1;
    yd->sel_end = -1;
    yd->sel_dragging = 0;
    yd->lines_valid = 0;
    yd->cached_nb_lines = 0;
    yd->cached_headers_end = 0;
    yd->width_valid = 0;
    yd->cached_content_w = 0.0f;
    yd->metrics_font = NULL;
    wgt->data = yd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a sortable data grid widget
 * @param ctx the GUI context
 * @param window_id the parent window id
 * @param x widget x within the window content area
 * @param y widget y within the window content area
 * @param w widget width
 * @param h widget height
 * @param on_select callback fired on row selection, or NULL
 * @param user_data user data passed to on_select
 * @return widget id, or -1 on error
 */
int n_gui_add_datagrid(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, void (*on_select)(int, int, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_DATAGRID, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_DATAGRID_DATA* gd = NULL;
    Malloc(gd, N_GUI_DATAGRID_DATA, 1);
    __n_assert(gd, Free(wgt); return -1);
    gd->cols = NULL;
    gd->nb_cols = 0;
    gd->cols_cap = 0;
    gd->col_order = NULL;
    gd->col_resize_col = -1;
    gd->col_resize_x0 = 0.0f;
    gd->col_resize_w0 = 0.0f;
    gd->on_columns_changed = NULL;
    gd->cells = NULL;
    gd->nb_rows = 0;
    gd->rows_cap = 0;
    gd->sort_col = -1;
    gd->sort_dir = 1;
    gd->selected_row = -1;
    gd->multiselect = 0;
    gd->row_sel = NULL;
    gd->anchor_row = -1;
    gd->row_color = NULL;
    gd->row_has_color = NULL;
    gd->scroll_offset = 0;
    gd->h_scroll = 0.0f;
    gd->h_scroll_dragging = 0;
    gd->on_select = on_select;
    gd->on_context = NULL;
    gd->user_data = user_data;
    wgt->data = gd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a radio list widget (single selection with radio bullets)
 * @return widget id, or -1 on error
 */
int n_gui_add_radiolist(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, void (*on_select)(int, int, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_RADIOLIST, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_RADIOLIST_DATA* rd = NULL;
    Malloc(rd, N_GUI_RADIOLIST_DATA, 1);
    __n_assert(rd, Free(wgt); return -1);
    rd->items = NULL;
    rd->nb_items = 0;
    rd->items_capacity = 0;
    rd->selected_index = -1;
    rd->scroll_offset = 0;
    rd->item_height = ctx->style.radiolist_default_item_height;
    rd->on_select = on_select;
    rd->user_data = user_data;
    wgt->data = rd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a combo box widget (dropdown selector)
 * @return widget id, or -1 on error
 */
int n_gui_add_combobox(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, void (*on_select)(int, int, void*), void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_COMBOBOX, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_COMBOBOX_DATA* cd = NULL;
    Malloc(cd, N_GUI_COMBOBOX_DATA, 1);
    __n_assert(cd, Free(wgt); return -1);
    cd->items = NULL;
    cd->nb_items = 0;
    cd->items_capacity = 0;
    cd->selected_index = -1;
    cd->is_open = 0;
    cd->scroll_offset = 0;
    cd->highlight_index = -1;
    cd->item_height = h;
    cd->max_visible = ctx->style.combobox_max_visible;
    cd->on_select = on_select;
    cd->user_data = user_data;
    cd->flags = 0;
    wgt->data = cd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Set combobox feature flags.
 * @param ctx       GUI context. Must not be NULL.
 * @param widget_id Combobox widget id.
 * @param flags     Bitmask of N_GUI_COMBOBOX_* values.
 */
void n_gui_combobox_set_flags(N_GUI_CTX* ctx, int widget_id, int flags) {
    __n_assert(ctx, return);
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, widget_id);
    __n_assert(wgt, return);
    if (wgt->type != N_GUI_TYPE_COMBOBOX) return;
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)wgt->data;
    __n_assert(cd, return);
    cd->flags = flags;
}

/**
 * @brief Add an image display widget
 * @param ctx the GUI context
 * @param window_id id of the parent window
 * @param x horizontal position
 * @param y vertical position
 * @param w widget width
 * @param h widget height
 * @param bitmap the bitmap to display (not owned by the widget)
 * @param scale_mode N_GUI_IMAGE_FIT, N_GUI_IMAGE_STRETCH, or N_GUI_IMAGE_CENTER
 * @return widget id, or -1 on error
 */
int n_gui_add_image(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, ALLEGRO_BITMAP* bitmap, int scale_mode) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_IMAGE, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_IMAGE_DATA* id = NULL;
    Malloc(id, N_GUI_IMAGE_DATA, 1);
    __n_assert(id, Free(wgt); return -1);
    id->bitmap = bitmap;
    id->scale_mode = scale_mode;
    wgt->data = id;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a static text label
 * @return widget id, or -1 on error
 */
int n_gui_add_label(N_GUI_CTX* ctx, int window_id, const char* text, float x, float y, float w, float h, int align) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_LABEL, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_LABEL_DATA* lb = NULL;
    Malloc(lb, N_GUI_LABEL_DATA, 1);
    __n_assert(lb, Free(wgt); return -1);
    lb->text[0] = '\0';
    if (text) {
        strncpy(lb->text, text, N_GUI_TEXT_MAX - 1);
        lb->text[N_GUI_TEXT_MAX - 1] = '\0';
        _normalize_crlf(lb->text); /* normalize CRLF for Allegro5 compatibility */
    }
    lb->link[0] = '\0';
    lb->align = align;
    lb->scroll_y = 0.0f;
    lb->sel_start = -1;
    lb->sel_end = -1;
    lb->sel_dragging = 0;
    lb->on_link_click = NULL;
    lb->user_data = NULL;
    wgt->data = lb;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Add a static text label with hyperlink
 * @return widget id, or -1 on error
 */
int n_gui_add_label_link(N_GUI_CTX* ctx, int window_id, const char* text, const char* link, float x, float y, float w, float h, int align, void (*on_link_click)(int, const char*, void*), void* user_data) {
    int wid = n_gui_add_label(ctx, window_id, text, x, y, w, h, align);
    if (wid < 0) return -1;
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, wid);
    if (wgt && wgt->data) {
        N_GUI_LABEL_DATA* lb = (N_GUI_LABEL_DATA*)wgt->data;
        if (link) {
            strncpy(lb->link, link, N_GUI_TEXT_MAX - 1);
            lb->link[N_GUI_TEXT_MAX - 1] = '\0';
        }
        lb->on_link_click = on_link_click;
        lb->user_data = user_data;
    }
    return wid;
}

/* WIDGET ACCESS */

/**
 * @brief Get a widget pointer by id
 */
N_GUI_WIDGET* n_gui_get_widget(N_GUI_CTX* ctx, int widget_id) {
    __n_assert(ctx, return NULL);
    char key[32];
    snprintf(key, sizeof(key), "%d", widget_id);
    void* ptr = NULL;
    if (ht_get_ptr(ctx->widgets_by_id, key, &ptr) == TRUE && ptr) {
        return (N_GUI_WIDGET*)ptr;
    }
    return NULL;
}

/**
 * @brief Override the theme of a specific widget
 */
void n_gui_set_widget_theme(N_GUI_CTX* ctx, int widget_id, N_GUI_THEME theme) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w) w->theme = theme;
}

/**
 * @brief Reset all widget and window themes to ctx->default_theme.
 *
 * Iterates every window and every widget, setting their theme to the
 * context default.  Call this after changing ctx->default_theme so that
 * all existing widgets pick up the new palette.  Widgets that need a
 * custom theme (accent buttons, status labels, etc.) must be re-themed
 * individually after this call.
 */
void n_gui_reset_all_widget_themes(N_GUI_CTX* ctx) {
    if (!ctx || !ctx->windows) return;
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!win) continue;
        win->theme = ctx->default_theme;
        win->tb_buttons.btn_theme = ctx->default_tb_btn_theme;
        win->tb_buttons.close_theme = ctx->default_tb_close_theme;
        if (!win->widgets) continue;
        list_foreach(wgn, win->widgets) {
            N_GUI_WIDGET* w = (N_GUI_WIDGET*)wgn->ptr;
            if (w) w->theme = ctx->default_theme;
        }
    }
}

/**
 * @brief Show or hide a widget
 */
void n_gui_set_widget_visible(N_GUI_CTX* ctx, int widget_id, int visible) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w) w->visible = visible;
}

/**
 * @brief Move and resize a widget after creation.
 *
 * Widgets are placed by the n_gui_add_* call that creates them, which is all a
 * static dialog needs. A host that computes its own layout (a resizable panel,
 * the two sides of a split pane) has to place them again on every relayout, and
 * reaching into N_GUI_WIDGET from application code would skip the normalized
 * coordinates the resize passes read.
 *
 * The rectangle is applied and the widget's norms are recaptured against its
 * window, so n_gui_apply_adaptive_resize and n_gui_window_set_rect keep scaling
 * it correctly from its new position. The owning window is found whether it is
 * open or closed, so the pages of a tab panel can be laid out while hidden.
 *
 * @param ctx the GUI context
 * @param widget_id the widget to place
 * @param x new x, relative to the window's content area
 * @param y new y, relative to the window's content area
 * @param w new width (negative is clamped to 0)
 * @param h new height (negative is clamped to 0)
 */
void n_gui_widget_set_rect(N_GUI_CTX* ctx, int widget_id, float x, float y, float w, float h) {
    __n_assert(ctx, return);
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, widget_id);
    if (!wgt) return;
    if (w < 0.0f) w = 0.0f;
    if (h < 0.0f) h = 0.0f;
    wgt->x = x;
    wgt->y = y;
    wgt->w = w;
    wgt->h = h;
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        int found = 0;
        if (!win) continue;
        list_foreach(wgn, win->widgets) {
            const N_GUI_WIDGET* it = (const N_GUI_WIDGET*)wgn->ptr;
            if (it && it->id == widget_id) {
                found = 1;
                break;
            }
        }
        if (found) {
            _n_gui_widget_capture_normalized(win, wgt);
            break;
        }
    }
    ctx->dirty = 1;
}

/**
 * @brief Enable or disable a widget.
 * Disabled widgets are drawn dimmed and ignore all mouse/keyboard input.
 */
void n_gui_set_widget_enabled(N_GUI_CTX* ctx, int widget_id, int enabled) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w) w->enabled = enabled;
}

/**
 * @brief Check if a widget is enabled
 * @return 1 if enabled, 0 if disabled
 */
int n_gui_is_widget_enabled(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w) return w->enabled;
    return 0;
}

/**
 * @brief Set keyboard focus to a specific widget.
 *        Pass widget_id = -1 to clear focus.
 *        If the widget is a textarea, resets cursor blink timer.
 *        Raises the parent window if it exists and is open.
 */
void n_gui_set_focus(N_GUI_CTX* ctx, int widget_id) {
    __n_assert(ctx, return);

    /* clear focus from previously focused widget */
    if (ctx->focused_widget_id >= 0) {
        N_GUI_WIDGET* prev = n_gui_get_widget(ctx, ctx->focused_widget_id);
        if (prev) prev->state &= ~N_GUI_STATE_FOCUSED;
    }

    if (widget_id < 0) {
        ctx->focused_widget_id = -1;
        return;
    }

    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, widget_id);
    if (!wgt) {
        n_log(LOG_ERR, "n_gui_set_focus: widget %d not found", widget_id);
        ctx->focused_widget_id = -1;
        return;
    }

    ctx->focused_widget_id = widget_id;
    wgt->state |= N_GUI_STATE_FOCUSED;

    /* reset cursor blink timer for textareas */
    if (wgt->type == N_GUI_TYPE_TEXTAREA && wgt->data) {
        N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)wgt->data;
        td->cursor_time = al_get_time();
    }

    /* raise the parent window */
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!win || !win->widgets) continue;
        list_foreach(wgnode, win->widgets) {
            const N_GUI_WIDGET* w = (const N_GUI_WIDGET*)wgnode->ptr;
            if (w && w->id == widget_id) {
                if (win->state & N_GUI_WIN_OPEN) {
                    n_gui_raise_window(ctx, win->id);
                }
                return;
            }
        }
    }
}

/* slider helpers */

/**
 *@brief get the current value of a slider widget
 *@param ctx GUI context
 *@param widget_id id of the slider widget
 *@return current slider value
 */
double n_gui_slider_get_value(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_SLIDER && w->data) {
        return ((N_GUI_SLIDER_DATA*)w->data)->value;
    }
    return 0.0;
}

/**
 *@brief set the value of a slider widget
 *@param ctx GUI context
 *@param widget_id id of the slider widget
 *@param value new value to set
 */
void n_gui_slider_set_value(N_GUI_CTX* ctx, int widget_id, double value) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_SLIDER && w->data) {
        N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)w->data;
        if (sd->step > 0.0)
            sd->value = _slider_snap_value(value, sd->min_val, sd->max_val, sd->step);
        else
            sd->value = _clamp(value, sd->min_val, sd->max_val);
    }
}

/**
 *@brief set slider min/max range, clamping the current value if needed
 *@param ctx GUI context
 *@param widget_id id of the slider widget
 *@param min_val new minimum value
 *@param max_val new maximum value
 */
void n_gui_slider_set_range(N_GUI_CTX* ctx, int widget_id, double min_val, double max_val) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_SLIDER && w->data) {
        N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)w->data;
        sd->min_val = min_val;
        sd->max_val = max_val;
        if (sd->step > 0.0)
            sd->value = _slider_snap_value(sd->value, min_val, max_val, sd->step);
        else
            sd->value = _clamp(sd->value, min_val, max_val);
    }
}

/**
 *@brief set slider step increment. 0 means continuous (no snapping).
 *       When step > 0, the current value is immediately snapped.
 *@param ctx GUI context
 *@param widget_id id of the slider widget
 *@param step step increment (0 = continuous)
 */
void n_gui_slider_set_step(N_GUI_CTX* ctx, int widget_id, double step) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_SLIDER && w->data) {
        N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)w->data;
        sd->step = step;
        if (step > 0.0) {
            sd->value = _slider_snap_value(sd->value, sd->min_val, sd->max_val, step);
        }
    }
}

void n_gui_slider_set_value_format(N_GUI_CTX* ctx, int widget_id, const char* fmt) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SLIDER || !w->data) return;
    N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)w->data;
    if (!fmt || !fmt[0]) {
        sd->value_format[0] = '\0';
    } else {
        /* snprintf truncates safely at the buffer bound. */
        snprintf(sd->value_format, sizeof(sd->value_format), "%s", fmt);
    }
}

void n_gui_slider_set_value_visible(N_GUI_CTX* ctx, int widget_id, int visible) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SLIDER || !w->data) return;
    N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)w->data;
    sd->value_visible = visible ? 1 : 0;
}

/* textarea helpers */

/**
 *@brief get the text content of a textarea widget
 *@param ctx GUI context
 *@param widget_id id of the textarea widget
 *@return pointer to the text content
 */
const char* n_gui_textarea_get_text(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_TEXTAREA && w->data) {
        return ((N_GUI_TEXTAREA_DATA*)w->data)->text;
    }
    return "";
}

/**
 *@brief set the text content of a textarea widget
 *@param ctx GUI context
 *@param widget_id id of the textarea widget
 *@param text the text to set, or NULL to clear
 */
void n_gui_textarea_set_text(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_TEXTAREA && w->data) {
        N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)w->data;
        if (text) {
            strncpy(td->text, text, td->char_limit);
            td->text[td->char_limit] = '\0';
            _normalize_crlf(td->text); /* normalize CRLF for Allegro5 compatibility */
            td->text_len = strlen(td->text);
            td->cursor_pos = td->text_len;
        } else {
            td->text[0] = '\0';
            td->text_len = 0;
            td->cursor_pos = 0;
        }
        td->scroll_x = 0.0f;
    }
}

/* checkbox helpers */

/**
 *@brief check if a checkbox widget is checked
 *@param ctx GUI context
 *@param widget_id id of the checkbox widget
 *@return 1 if checked, 0 otherwise
 */
int n_gui_checkbox_is_checked(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_CHECKBOX && w->data) {
        return ((N_GUI_CHECKBOX_DATA*)w->data)->checked;
    }
    return 0;
}

/**
 *@brief set the checked state of a checkbox widget
 *@param ctx GUI context
 *@param widget_id id of the checkbox widget
 *@param checked 1 to check, 0 to uncheck
 */
void n_gui_checkbox_set_checked(N_GUI_CTX* ctx, int widget_id, int checked) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_CHECKBOX && w->data) {
        ((N_GUI_CHECKBOX_DATA*)w->data)->checked = checked ? 1 : 0;
    }
}

/* scrollbar helpers */

/**
 *@brief get the current scroll position of a scrollbar widget
 *@param ctx GUI context
 *@param widget_id id of the scrollbar widget
 *@return current scroll position
 */
double n_gui_scrollbar_get_pos(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_SCROLLBAR && w->data) {
        return ((N_GUI_SCROLLBAR_DATA*)w->data)->scroll_pos;
    }
    return 0.0;
}

/**
 *@brief set the scroll position of a scrollbar widget
 *@param ctx GUI context
 *@param widget_id id of the scrollbar widget
 *@param pos new scroll position
 */
void n_gui_scrollbar_set_pos(N_GUI_CTX* ctx, int widget_id, double pos) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_SCROLLBAR && w->data) {
        N_GUI_SCROLLBAR_DATA* sb = (N_GUI_SCROLLBAR_DATA*)w->data;
        double max_scroll = sb->content_size - sb->viewport_size;
        if (max_scroll < 0) max_scroll = 0;
        sb->scroll_pos = _clamp(pos, 0, max_scroll);
    }
}

/**
 *@brief set the content and viewport sizes of a scrollbar widget
 *@param ctx GUI context
 *@param widget_id id of the scrollbar widget
 *@param content_size total size of the content
 *@param viewport_size visible viewport size
 */
void n_gui_scrollbar_set_sizes(N_GUI_CTX* ctx, int widget_id, double content_size, double viewport_size) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_SCROLLBAR && w->data) {
        N_GUI_SCROLLBAR_DATA* sb = (N_GUI_SCROLLBAR_DATA*)w->data;
        sb->content_size = content_size > 0 ? content_size : 1;
        sb->viewport_size = viewport_size > 0 ? viewport_size : 1;
        double max_scroll = sb->content_size - sb->viewport_size;
        if (max_scroll < 0) max_scroll = 0;
        sb->scroll_pos = _clamp(sb->scroll_pos, 0, max_scroll);
    }
}

/* listbox helpers */

/**
 *@brief add an item to a listbox widget
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@param text text of the item to add
 *@return index of the new item or -1 on error
 */
int n_gui_listbox_add_item(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return -1;
    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
    if (!_items_grow(&ld->items, &ld->nb_items, &ld->items_capacity)) return -1;
    N_GUI_LISTITEM* item = &ld->items[ld->nb_items];
    item->text[0] = '\0';
    if (text) {
        strncpy(item->text, text, N_GUI_ID_MAX - 1);
        item->text[N_GUI_ID_MAX - 1] = '\0';
    }
    item->selected = 0;
    ld->nb_items++;
    return (int)(ld->nb_items - 1);
}

/**
 *@brief remove an item from a listbox widget
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@param index index of the item to remove
 *@return 0 on success, -1 on error
 */
int n_gui_listbox_remove_item(N_GUI_CTX* ctx, int widget_id, int index) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return -1;
    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
    if (index < 0 || (size_t)index >= ld->nb_items) return -1;
    if ((size_t)index < ld->nb_items - 1) {
        memmove(&ld->items[index], &ld->items[index + 1],
                (ld->nb_items - (size_t)index - 1) * sizeof(N_GUI_LISTITEM));
    }
    ld->nb_items--;
    /* clamp scroll_offset after removal */
    if (ld->nb_items == 0) {
        ld->scroll_offset = 0;
    } else if (ld->scroll_offset > 0 && (size_t)ld->scroll_offset >= ld->nb_items) {
        ld->scroll_offset = (int)(ld->nb_items - 1);
    }
    return 0;
}

/**
 *@brief remove all items from a listbox widget
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 */
void n_gui_listbox_clear(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LISTBOX && w->data) {
        N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
        ld->nb_items = 0;
        ld->scroll_offset = 0;
        ld->h_scroll = 0.0f;
        ld->h_scroll_dragging = 0;
    }
}

/**
 *@brief get the number of items in a listbox widget
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@return number of items
 */
int n_gui_listbox_get_count(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LISTBOX && w->data) {
        return (int)((N_GUI_LISTBOX_DATA*)w->data)->nb_items;
    }
    return 0;
}

/**
 *@brief get the text of a listbox item
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@param index index of the item
 *@return pointer to the item text, or empty string on error
 */
const char* n_gui_listbox_get_item_text(N_GUI_CTX* ctx, int widget_id, int index) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LISTBOX && w->data) {
        N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
        if (index >= 0 && (size_t)index < ld->nb_items) {
            return ld->items[index].text;
        }
    }
    return "";
}

/**
 *@brief get the index of the first selected item in a listbox
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@return index of the first selected item, or -1 if none
 */
int n_gui_listbox_get_selected(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LISTBOX && w->data) {
        const N_GUI_LISTBOX_DATA* ld = (const N_GUI_LISTBOX_DATA*)w->data;
        for (size_t i = 0; i < ld->nb_items; i++) {
            if (ld->items[i].selected) return (int)i;
        }
    }
    return -1;
}

/**
 *@brief check if a listbox item is selected
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@param index index of the item to check
 *@return 1 if selected, 0 otherwise
 */
int n_gui_listbox_is_selected(N_GUI_CTX* ctx, int widget_id, int index) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LISTBOX && w->data) {
        N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
        if (index >= 0 && (size_t)index < ld->nb_items) {
            return ld->items[index].selected;
        }
    }
    return 0;
}

/**
 *@brief set the selection state of a listbox item
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@param index index of the item
 *@param selected 1 to select, 0 to deselect
 */
void n_gui_listbox_set_selected(N_GUI_CTX* ctx, int widget_id, int index, int selected) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return;
    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
    if (index < 0 || (size_t)index >= ld->nb_items) return;
    if (ld->selection_mode == N_GUI_SELECT_SINGLE && selected) {
        for (size_t i = 0; i < ld->nb_items; i++) ld->items[i].selected = 0;
    }
    ld->items[index].selected = selected ? 1 : 0;
}

int n_gui_listbox_get_scroll_offset(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return 0;
    const N_GUI_LISTBOX_DATA* ld = (const N_GUI_LISTBOX_DATA*)w->data;
    return ld->scroll_offset;
}

void n_gui_listbox_set_scroll_offset(N_GUI_CTX* ctx, int widget_id, int offset) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return;
    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
    int max_offset = (int)ld->nb_items - (int)(w->h / ld->item_height);
    if (max_offset < 0) max_offset = 0;
    if (offset < 0) offset = 0;
    if (offset > max_offset) offset = max_offset;
    ld->scroll_offset = offset;
}

/**
 *@brief width the widest item of a listbox needs to be drawn in full
 *
 * Text padding included, so it can be compared directly with the room left for
 * the items. Past that width the listbox scrolls horizontally rather than
 * cutting the text short with an ellipsis.
 *
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@return the content width in pixels, or 0 for an empty list, a listbox with no
 *        usable font, or a widget that is not a listbox
 */
float n_gui_listbox_content_width(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    float cw = 0.0f;
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return 0.0f;
    {
        const N_GUI_LISTBOX_DATA* ld = (const N_GUI_LISTBOX_DATA*)w->data;
        ALLEGRO_FONT* font = w->font ? w->font : ctx->default_font;
        size_t i;
        if (!font) return 0.0f;
        for (i = 0; i < ld->nb_items; i++) {
            float tw = _text_w(font, ld->items[i].text);
            if (tw > cw) cw = tw;
        }
        if (cw > 0.0f) cw += ctx->style.item_text_padding * 2.0f;
    }
    return cw;
}

/**
 *@brief get a listbox's horizontal scroll offset in pixels (0 = leftmost)
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@return the offset, or 0 when the widget is not a listbox
 */
float n_gui_listbox_get_h_scroll(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return 0.0f;
    return ((const N_GUI_LISTBOX_DATA*)w->data)->h_scroll;
}

/**
 *@brief set a listbox's horizontal scroll offset in pixels
 *
 * Clamped to >= 0 here; the draw path re-clamps to the live content width every
 * frame, so a value left over from a wider list is harmless.
 *
 *@param ctx GUI context
 *@param widget_id id of the listbox widget
 *@param px the offset in pixels
 */
void n_gui_listbox_set_h_scroll(N_GUI_CTX* ctx, int widget_id, float px) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return;
    if (px < 0.0f) px = 0.0f;
    ((N_GUI_LISTBOX_DATA*)w->data)->h_scroll = px;
    if (ctx) ctx->dirty = 1;
}

/* split pane helpers */

/**
 *@brief get the current divider ratio of a split pane
 *@param ctx GUI context
 *@param widget_id id of the split pane widget
 *@return the divider ratio (0..1), or 0.5 if the widget is invalid
 */
float n_gui_splitpane_get_ratio(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SPLITPANE || !w->data) return 0.5f;
    return ((const N_GUI_SPLITPANE_DATA*)w->data)->ratio;
}

/**
 *@brief set the divider ratio of a split pane (clamped to its min/max)
 *@param ctx GUI context
 *@param widget_id id of the split pane widget
 *@param ratio the requested divider ratio (0..1)
 */
void n_gui_splitpane_set_ratio(N_GUI_CTX* ctx, int widget_id, float ratio) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SPLITPANE || !w->data) return;
    N_GUI_SPLITPANE_DATA* sd = (N_GUI_SPLITPANE_DATA*)w->data;
    if (ratio < sd->min_ratio) ratio = sd->min_ratio;
    if (ratio > sd->max_ratio) ratio = sd->max_ratio;
    sd->ratio = ratio;
}

/**
 *@brief set the minimum and maximum allowed divider ratio of a split pane
 *@param ctx GUI context
 *@param widget_id id of the split pane widget
 *@param min_ratio lower clamp (0..1)
 *@param max_ratio upper clamp (0..1)
 */
void n_gui_splitpane_set_limits(N_GUI_CTX* ctx, int widget_id, float min_ratio, float max_ratio) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SPLITPANE || !w->data) return;
    N_GUI_SPLITPANE_DATA* sd = (N_GUI_SPLITPANE_DATA*)w->data;
    if (min_ratio < 0.0f) min_ratio = 0.0f;
    if (max_ratio > 1.0f) max_ratio = 1.0f;
    if (min_ratio > max_ratio) min_ratio = max_ratio;
    sd->min_ratio = min_ratio;
    sd->max_ratio = max_ratio;
    if (sd->ratio < min_ratio) sd->ratio = min_ratio;
    if (sd->ratio > max_ratio) sd->ratio = max_ratio;
}

/* hex view helpers */

/**
 *@brief set (copy) the bytes shown by a hex viewer
 *@param ctx GUI context
 *@param widget_id id of the hex viewer widget
 *@param data the bytes to display, or NULL to clear
 *@param len number of bytes in data
 */
void n_gui_hexview_set_data(N_GUI_CTX* ctx, int widget_id, const unsigned char* data, size_t len) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_HEXVIEW || !w->data) return;
    N_GUI_HEXVIEW_DATA* hd = (N_GUI_HEXVIEW_DATA*)w->data;
    FreeNoLog(hd->data);
    hd->len = 0;
    hd->scroll_offset = 0;
    if (data && len > 0) {
        Malloc(hd->data, unsigned char, len);
        if (hd->data) {
            memcpy(hd->data, data, len);
            hd->len = len;
        }
    }
}

/**
 *@brief get the number of bytes currently held by a hex viewer
 *@param ctx GUI context
 *@param widget_id id of the hex viewer widget
 *@return the byte count, or 0 if the widget is invalid
 */
size_t n_gui_hexview_get_length(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_HEXVIEW || !w->data) return 0;
    return ((const N_GUI_HEXVIEW_DATA*)w->data)->len;
}

/* syntax view helpers */

/**
 *@brief set (copy) the text shown by a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@param text the text to display, or NULL to clear
 */
void n_gui_syntaxview_set_text(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return;
    N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)w->data;
    FreeNoLog(yd->text);
    yd->len = 0;
    yd->scroll_offset = 0;
    yd->h_scroll = 0.0f;
    yd->h_scroll_dragging = 0;
    /* the cached line count / headers-end / width no longer describe this text */
    yd->lines_valid = 0;
    yd->width_valid = 0;
    /* the byte offsets no longer refer to anything, so drop any selection */
    yd->sel_start = -1;
    yd->sel_end = -1;
    yd->sel_dragging = 0;
    if (ctx && ctx->selected_syntaxview_id == widget_id)
        ctx->selected_syntaxview_id = -1;
    if (text && text[0]) {
        size_t len = strlen(text);
        Malloc(yd->text, char, len + 1);
        if (yd->text) {
            memcpy(yd->text, text, len + 1);
            yd->len = len;
        }
    }
}

/**
 *@brief set the highlighting mode of a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@param mode one of the N_GUI_SYNTAX_* modes (PLAIN, HTTP, JSON, XML, YAML, JS, DIFF)
 */
void n_gui_syntaxview_set_mode(N_GUI_CTX* ctx, int widget_id, int mode) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return;
    ((N_GUI_SYNTAXVIEW_DATA*)w->data)->mode = mode;
}

/**
 *@brief get the number of text lines in a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@return the line count, or 0 if the widget is invalid
 */
int n_gui_syntaxview_get_line_count(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return 0;
    return _text_line_count(((const N_GUI_SYNTAXVIEW_DATA*)w->data)->text);
}

/**
 *@brief get the text currently shown by a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@return a borrowed pointer to the text (do not free), or NULL when empty/invalid
 */
const char* n_gui_syntaxview_get_text(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return NULL;
    return ((const N_GUI_SYNTAXVIEW_DATA*)w->data)->text;
}

/**
 *@brief get a copy of the currently selected text in a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@return a newly allocated copy of the selected substring (caller frees with Free()),
 *        or NULL when nothing is selected or the widget is invalid
 */
char* n_gui_syntaxview_get_selected_text(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return NULL;
    const N_GUI_SYNTAXVIEW_DATA* yd = (const N_GUI_SYNTAXVIEW_DATA*)w->data;
    if (!yd->text || yd->sel_start < 0 || yd->sel_end < 0 || yd->sel_start == yd->sel_end)
        return NULL;
    int lo = yd->sel_start < yd->sel_end ? yd->sel_start : yd->sel_end;
    int hi = yd->sel_start < yd->sel_end ? yd->sel_end : yd->sel_start;
    if ((size_t)lo > yd->len) lo = (int)yd->len;
    if ((size_t)hi > yd->len) hi = (int)yd->len;
    int n = hi - lo;
    if (n <= 0) return NULL;
    char* out = NULL;
    Malloc(out, char, (size_t)n + 1);
    if (!out) return NULL;
    memcpy(out, yd->text + lo, (size_t)n);
    out[n] = '\0';
    return out;
}

/**
 *@brief set the selection range of a syntax view (byte offsets into the text)
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@param start selection start byte offset (clamped to the text length)
 *@param end selection end byte offset (clamped to the text length); start == end clears
 */
void n_gui_syntaxview_set_selection(N_GUI_CTX* ctx, int widget_id, int start, int end) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return;
    N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)w->data;
    int len = (int)yd->len;
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    yd->sel_start = start;
    yd->sel_end = end;
    if (ctx && start != end)
        ctx->selected_syntaxview_id = widget_id;
}

/**
 *@brief select the entire text of a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 */
void n_gui_syntaxview_select_all(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return;
    n_gui_syntaxview_set_selection(ctx, widget_id, 0, (int)((const N_GUI_SYNTAXVIEW_DATA*)w->data)->len);
}

/**
 *@brief scroll a syntax view so the line holding a byte offset is vertically centered,
 *       and horizontally so the offset itself is inside the visible width
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@param byte_offset byte offset into the text (clamped to the text length)
 */
void n_gui_syntaxview_scroll_to_offset(N_GUI_CTX* ctx, int widget_id, int byte_offset) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return;
    N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)w->data;
    if (!yd->text || yd->len == 0) return;
    if (byte_offset < 0) byte_offset = 0;
    if ((size_t)byte_offset > yd->len) byte_offset = (int)yd->len;

    int line = 0;
    int line_start = 0;
    for (int it = 0; it < byte_offset; it++) {
        if (yd->text[it] == '\n') {
            line++;
            line_start = it + 1;
        }
    }

    /* mirror the row math of _draw_syntaxview so the clamp agrees with it */
    ALLEGRO_FONT* font = w->font ? w->font : (ctx ? ctx->default_font : NULL);
    if (!font) return;
    float pad = ctx->style.textarea_padding;
    float content_w = 0.0f, pane_w = w->w;
    int need_hsb = 0, visible = 1;
    _syntaxview_hmetrics(w, yd, font, &ctx->style, &content_w, &pane_w, &need_hsb, &visible);
    int max_off = yd->cached_nb_lines - visible;
    if (max_off < 0) max_off = 0;
    int target = line - visible / 2;
    if (target > max_off) target = max_off;
    if (target < 0) target = 0;
    yd->scroll_offset = target;

    /* A hit far along a wide line is on screen only once the view is panned to
       it: leave the pan alone while the offset already shows, otherwise put it
       a third of the way in, which keeps some of its context on both sides. */
    if (need_hsb) {
        float x = _syntax_prefix_w(font, yd->text + line_start, byte_offset - line_start);
        float view_w = pane_w - pad * 2.0f;
        if (view_w < 1.0f) view_w = 1.0f;
        if (x < yd->h_scroll || x > yd->h_scroll + view_w) {
            yd->h_scroll = x - view_w / 3.0f;
        }
        _syntaxview_clamp_h(yd, content_w, pane_w);
    }
}

/**
 *@brief pixel width the widest line of a syntax view needs, padding included
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@return the content width in pixels, or 0 if the widget is invalid or empty
 */
float n_gui_syntaxview_content_width(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return 0.0f;
    ALLEGRO_FONT* font = w->font ? w->font : (ctx ? ctx->default_font : NULL);
    _syntaxview_ensure_width((N_GUI_SYNTAXVIEW_DATA*)w->data, font, &ctx->style);
    return ((const N_GUI_SYNTAXVIEW_DATA*)w->data)->cached_content_w;
}

/**
 *@brief get the horizontal scroll offset of a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@return the offset in pixels, or 0 if the widget is invalid
 */
float n_gui_syntaxview_get_h_scroll(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return 0.0f;
    return ((const N_GUI_SYNTAXVIEW_DATA*)w->data)->h_scroll;
}

/**
 *@brief set the horizontal scroll offset of a syntax view
 *@param ctx GUI context
 *@param widget_id id of the syntax view widget
 *@param px the offset in pixels (negative values are clamped to 0, the upper
 *          bound is applied by the draw path against the live content width)
 */
void n_gui_syntaxview_set_h_scroll(N_GUI_CTX* ctx, int widget_id, float px) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SYNTAXVIEW || !w->data) return;
    if (px < 0.0f) px = 0.0f;
    ((N_GUI_SYNTAXVIEW_DATA*)w->data)->h_scroll = px;
}

/* data grid helpers */

/*! true when every character of a non-empty string is a decimal digit */
static int _cell_is_num(const char* s) {
    if (!s || !*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

/*! compare two cell strings: numerically when both are integers, else case-insensitive */
static int _cell_cmp(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    if (_cell_is_num(a) && _cell_is_num(b)) {
        long la = atol(a), lb = atol(b);
        return (la < lb) ? -1 : (la > lb) ? 1
                                          : 0;
    }
    return strcasecmp(a, b);
}

int n_gui_datagrid_add_column(N_GUI_CTX* ctx, int widget_id, const char* title, float width) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return -1;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (gd->nb_rows > 0) return -1; /* columns must be defined before rows */
    if (gd->nb_cols >= gd->cols_cap) {
        size_t nc = gd->cols_cap ? gd->cols_cap * 2 : 4;
        N_GUI_DATAGRID_COL* tmp = NULL;
        size_t* ord = NULL;
        Malloc(tmp, N_GUI_DATAGRID_COL, nc);
        if (!tmp) return -1;
        if (gd->cols && gd->nb_cols > 0)
            memcpy(tmp, gd->cols, gd->nb_cols * sizeof(N_GUI_DATAGRID_COL));
        FreeNoLog(gd->cols);
        gd->cols = tmp;
        /* the display-order table grows alongside the columns */
        Malloc(ord, size_t, nc);
        if (ord) {
            if (gd->col_order && gd->nb_cols > 0)
                memcpy(ord, gd->col_order, gd->nb_cols * sizeof(size_t));
            FreeNoLog(gd->col_order);
            gd->col_order = ord;
        }
        gd->cols_cap = nc;
    }
    snprintf(gd->cols[gd->nb_cols].title, N_GUI_ID_MAX, "%s", title ? title : "");
    gd->cols[gd->nb_cols].width = width > 0 ? width : 80.0f;
    gd->cols[gd->nb_cols].visible = 1;
    if (gd->col_order)
        gd->col_order[gd->nb_cols] = gd->nb_cols; /* new column appended at its own display position */
    return (int)gd->nb_cols++;
}

/*! physical column index shown at display position @p dp (identity when no order). */
static size_t _datagrid_phys(const N_GUI_DATAGRID_DATA* gd, size_t dp) {
    return (gd->col_order && dp < gd->nb_cols) ? gd->col_order[dp] : dp;
}

/*! helper: ensure the per-row selection array exists, sized to rows_cap */
static void _datagrid_ensure_sel(N_GUI_DATAGRID_DATA* gd) {
    if (!gd || gd->rows_cap == 0 || gd->row_sel) return;
    Malloc(gd->row_sel, unsigned char, gd->rows_cap);
    if (gd->row_sel) memset(gd->row_sel, 0, gd->rows_cap);
}

/*! helper: ensure the per-row tint arrays exist, sized to rows_cap (rows start
 *  untinted); returns 1 when both arrays are available */
static int _datagrid_ensure_colors(N_GUI_DATAGRID_DATA* gd) {
    if (!gd || gd->rows_cap == 0) return 0;
    if (gd->row_color && gd->row_has_color) return 1;
    if (!gd->row_color) Malloc(gd->row_color, ALLEGRO_COLOR, gd->rows_cap);
    if (!gd->row_has_color) Malloc(gd->row_has_color, unsigned char, gd->rows_cap);
    if (!gd->row_color || !gd->row_has_color) return 0;
    memset(gd->row_has_color, 0, gd->rows_cap);
    return 1;
}

int n_gui_datagrid_add_row(N_GUI_CTX* ctx, int widget_id, const char** values) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return -1;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (gd->nb_cols == 0) return -1;
    if (gd->nb_rows >= gd->rows_cap) {
        size_t nr = gd->rows_cap ? gd->rows_cap * 2 : 16;
        char** tmp = NULL;
        Malloc(tmp, char*, nr * gd->nb_cols);
        if (!tmp) return -1;
        if (gd->cells && gd->nb_rows > 0)
            memcpy(tmp, gd->cells, gd->nb_rows * gd->nb_cols * sizeof(char*));
        FreeNoLog(gd->cells);
        gd->cells = tmp;
        gd->rows_cap = nr;
        /* grow the per-row selection array in lockstep (new rows start unselected) */
        if (gd->row_sel) {
            unsigned char* ts = NULL;
            Malloc(ts, unsigned char, nr);
            if (ts) {
                memset(ts, 0, nr);
                if (gd->nb_rows > 0) memcpy(ts, gd->row_sel, gd->nb_rows);
                FreeNoLog(gd->row_sel);
                gd->row_sel = ts;
            }
        }
        /* grow the per-row tint arrays in lockstep (new rows start untinted) */
        if (gd->row_color && gd->row_has_color) {
            ALLEGRO_COLOR* tc = NULL;
            unsigned char* th = NULL;
            Malloc(tc, ALLEGRO_COLOR, nr);
            Malloc(th, unsigned char, nr);
            if (tc && th) {
                memset(th, 0, nr);
                if (gd->nb_rows > 0) {
                    memcpy(tc, gd->row_color, gd->nb_rows * sizeof(ALLEGRO_COLOR));
                    memcpy(th, gd->row_has_color, gd->nb_rows);
                }
                FreeNoLog(gd->row_color);
                FreeNoLog(gd->row_has_color);
                gd->row_color = tc;
                gd->row_has_color = th;
            } else {
                FreeNoLog(tc);
                FreeNoLog(th);
            }
        }
    }
    {
        size_t c;
        for (c = 0; c < gd->nb_cols; c++) {
            const char* v = (values && values[c]) ? values[c] : "";
            gd->cells[gd->nb_rows * gd->nb_cols + c] = strdup(v);
        }
    }
    return (int)gd->nb_rows++;
}

void n_gui_datagrid_clear_rows(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (gd->cells) {
        size_t ci;
        for (ci = 0; ci < gd->nb_rows * gd->nb_cols; ci++)
            FreeNoLog(gd->cells[ci]);
    }
    gd->nb_rows = 0;
    gd->selected_row = -1;
    if (gd->row_sel) memset(gd->row_sel, 0, gd->rows_cap);
    /* the tints belong to the rows that are going away */
    if (gd->row_has_color) memset(gd->row_has_color, 0, gd->rows_cap);
    gd->anchor_row = -1;
    gd->scroll_offset = 0;
}

int n_gui_datagrid_get_row_count(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return 0;
    return (int)((const N_GUI_DATAGRID_DATA*)w->data)->nb_rows;
}

const char* n_gui_datagrid_get_cell(N_GUI_CTX* ctx, int widget_id, int row, int col) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return NULL;
    const N_GUI_DATAGRID_DATA* gd = (const N_GUI_DATAGRID_DATA*)w->data;
    if (row < 0 || (size_t)row >= gd->nb_rows || col < 0 || (size_t)col >= gd->nb_cols) return NULL;
    return gd->cells[(size_t)row * gd->nb_cols + (size_t)col];
}

int n_gui_datagrid_get_selected(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return -1;
    return ((const N_GUI_DATAGRID_DATA*)w->data)->selected_row;
}

void n_gui_datagrid_set_selected(N_GUI_CTX* ctx, int widget_id, int row) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (row < -1 || (size_t)row >= gd->nb_rows) return;
    gd->selected_row = row;
    if (row >= 0 && gd->on_select) gd->on_select(widget_id, row, gd->user_data);
}

void n_gui_datagrid_set_on_context(N_GUI_CTX* ctx, int widget_id, void (*on_context)(int, int, int, int, void*)) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    ((N_GUI_DATAGRID_DATA*)w->data)->on_context = on_context;
}

void n_gui_datagrid_set_multiselect(N_GUI_CTX* ctx, int widget_id, int enabled) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    gd->multiselect = enabled ? 1 : 0;
    if (!enabled) {
        if (gd->row_sel) memset(gd->row_sel, 0, gd->rows_cap);
        gd->anchor_row = -1;
    } else {
        _datagrid_ensure_sel(gd);
    }
}

int n_gui_datagrid_is_row_selected(N_GUI_CTX* ctx, int widget_id, int row) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return 0;
    const N_GUI_DATAGRID_DATA* gd = (const N_GUI_DATAGRID_DATA*)w->data;
    if (row < 0 || (size_t)row >= gd->nb_rows) return 0;
    if (gd->multiselect && gd->row_sel) return gd->row_sel[row] ? 1 : 0;
    return (row == gd->selected_row) ? 1 : 0;
}

size_t n_gui_datagrid_get_selected_rows(N_GUI_CTX* ctx, int widget_id, int* out, size_t max) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return 0;
    const N_GUI_DATAGRID_DATA* gd = (const N_GUI_DATAGRID_DATA*)w->data;
    if (gd->multiselect && gd->row_sel) {
        size_t r, n = 0;
        for (r = 0; r < gd->nb_rows; r++) {
            if (gd->row_sel[r]) {
                if (out && n < max) out[n] = (int)r;
                n++;
            }
        }
        return n;
    }
    if (gd->selected_row >= 0 && (size_t)gd->selected_row < gd->nb_rows) {
        if (out && max > 0) out[0] = gd->selected_row;
        return 1;
    }
    return 0;
}

void n_gui_datagrid_clear_selection(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (gd->row_sel) memset(gd->row_sel, 0, gd->rows_cap);
    gd->selected_row = -1;
    gd->anchor_row = -1;
}

void n_gui_datagrid_select_row(N_GUI_CTX* ctx, int widget_id, int row, int selected) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (row < 0 || (size_t)row >= gd->nb_rows) return;
    _datagrid_ensure_sel(gd);
    if (!gd->row_sel) return;
    gd->row_sel[row] = selected ? 1 : 0;
    if (selected) {
        gd->selected_row = row;
        gd->anchor_row = row;
    } else if (gd->selected_row == row) {
        gd->selected_row = -1;
    }
}

void n_gui_datagrid_set_row_color(N_GUI_CTX* ctx, int widget_id, int row, ALLEGRO_COLOR color) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (row < 0 || (size_t)row >= gd->nb_rows) return;
    if (!_datagrid_ensure_colors(gd)) return;
    gd->row_color[row] = color;
    gd->row_has_color[row] = 1;
}

void n_gui_datagrid_clear_row_color(N_GUI_CTX* ctx, int widget_id, int row) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (row < 0 || (size_t)row >= gd->nb_rows || !gd->row_has_color) return;
    gd->row_has_color[row] = 0;
}

int n_gui_datagrid_get_row_color(N_GUI_CTX* ctx, int widget_id, int row, ALLEGRO_COLOR* out) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return 0;
    const N_GUI_DATAGRID_DATA* gd = (const N_GUI_DATAGRID_DATA*)w->data;
    if (row < 0 || (size_t)row >= gd->nb_rows) return 0;
    if (!gd->row_has_color || !gd->row_color || !gd->row_has_color[row]) return 0;
    if (out) *out = gd->row_color[row];
    return 1;
}

void n_gui_datagrid_clear_row_colors(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (gd->row_has_color) memset(gd->row_has_color, 0, gd->rows_cap);
}

int n_gui_datagrid_get_scroll_offset(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return 0;
    return ((const N_GUI_DATAGRID_DATA*)w->data)->scroll_offset;
}

void n_gui_datagrid_set_scroll_offset(N_GUI_CTX* ctx, int widget_id, int offset) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    /* clamp to a valid first row; the draw path re-clamps to the live viewport
       height each frame, so passing a stale offset after a rebuild is safe */
    if (offset < 0) offset = 0;
    if ((size_t)offset >= gd->nb_rows) offset = gd->nb_rows ? (int)gd->nb_rows - 1 : 0;
    gd->scroll_offset = offset;
}

float n_gui_datagrid_get_h_scroll(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return 0.0f;
    return ((const N_GUI_DATAGRID_DATA*)w->data)->h_scroll;
}

void n_gui_datagrid_set_h_scroll(N_GUI_CTX* ctx, int widget_id, float px) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    /* clamp >= 0 only; the draw path re-clamps to the live content width each frame, so a
       value larger than the current maximum after a rebuild is harmless */
    if (px < 0.0f) px = 0.0f;
    ((N_GUI_DATAGRID_DATA*)w->data)->h_scroll = px;
}

/*! fetch a data grid's column table for an API call, or NULL on a bad id. */
static N_GUI_DATAGRID_DATA* _datagrid_data(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return NULL;
    return (N_GUI_DATAGRID_DATA*)w->data;
}

size_t n_gui_datagrid_get_column_count(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    return gd ? gd->nb_cols : 0;
}

const char* n_gui_datagrid_get_column_title(N_GUI_CTX* ctx, int widget_id, int col) {
    N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    if (!gd || col < 0 || (size_t)col >= gd->nb_cols) return "";
    return gd->cols[col].title;
}

void n_gui_datagrid_set_column_visible(N_GUI_CTX* ctx, int widget_id, int col, int visible) {
    N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    if (!gd || col < 0 || (size_t)col >= gd->nb_cols) return;
    gd->cols[col].visible = visible ? 1 : 0;
}

int n_gui_datagrid_get_column_visible(N_GUI_CTX* ctx, int widget_id, int col) {
    N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    if (!gd || col < 0 || (size_t)col >= gd->nb_cols) return 0;
    return gd->cols[col].visible;
}

void n_gui_datagrid_set_column_width(N_GUI_CTX* ctx, int widget_id, int col, float width) {
    N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    if (!gd || col < 0 || (size_t)col >= gd->nb_cols) return;
    gd->cols[col].width = width < 16.0f ? 16.0f : width;
}

float n_gui_datagrid_get_column_width(N_GUI_CTX* ctx, int widget_id, int col) {
    N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    if (!gd || col < 0 || (size_t)col >= gd->nb_cols) return 0.0f;
    return gd->cols[col].width;
}

int n_gui_datagrid_display_to_physical(N_GUI_CTX* ctx, int widget_id, int display_pos) {
    const N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    if (!gd || display_pos < 0 || (size_t)display_pos >= gd->nb_cols) return -1;
    return (int)_datagrid_phys(gd, (size_t)display_pos);
}

void n_gui_datagrid_move_column(N_GUI_CTX* ctx, int widget_id, int from, int to) {
    N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    size_t f, t, moved, i;
    if (!gd || !gd->col_order) return;
    if (from < 0 || (size_t)from >= gd->nb_cols || to < 0 || (size_t)to >= gd->nb_cols || from == to) return;
    f = (size_t)from;
    t = (size_t)to;
    moved = gd->col_order[f];
    if (f < t) /* shift the span (f, t] left, then drop moved at t */
        for (i = f; i < t; i++) gd->col_order[i] = gd->col_order[i + 1];
    else /* shift the span [t, f) right, then drop moved at t */
        for (i = f; i > t; i--) gd->col_order[i] = gd->col_order[i - 1];
    gd->col_order[t] = moved;
}

void n_gui_datagrid_set_on_columns_changed(N_GUI_CTX* ctx, int widget_id, void (*cb)(int, void*)) {
    N_GUI_DATAGRID_DATA* gd = _datagrid_data(ctx, widget_id);
    if (gd) gd->on_columns_changed = cb;
}

void n_gui_datagrid_sort(N_GUI_CTX* ctx, int widget_id, int col, int dir) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DATAGRID || !w->data) return;
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)w->data;
    if (col < 0 || (size_t)col >= gd->nb_cols || gd->nb_rows < 2) {
        gd->sort_col = col;
        gd->sort_dir = (dir < 0) ? -1 : 1;
        return;
    }
    {
        size_t n = gd->nb_rows, cols = gd->nb_cols;
        size_t* idx = NULL;
        char** nc = NULL;
        size_t i, j;
        int d = (dir < 0) ? -1 : 1;
        Malloc(idx, size_t, n);
        Malloc(nc, char*, n* cols);
        if (!idx || !nc) {
            FreeNoLog(idx);
            FreeNoLog(nc);
            return;
        }
        for (i = 0; i < n; i++) idx[i] = i;
        /* insertion sort on the index array (stable, fine for grid sizes) */
        for (i = 1; i < n; i++) {
            size_t key = idx[i];
            const char* ka = gd->cells[key * cols + (size_t)col];
            j = i;
            while (j > 0 && d * _cell_cmp(gd->cells[idx[j - 1] * cols + (size_t)col], ka) > 0) {
                idx[j] = idx[j - 1];
                j--;
            }
            idx[j] = key;
        }
        for (i = 0; i < n; i++)
            for (j = 0; j < cols; j++)
                nc[i * cols + j] = gd->cells[idx[i] * cols + j];
        FreeNoLog(gd->cells);
        gd->cells = nc;
        /* the per-row tints are indexed by row, so they must be permuted with the
           cells (and shrunk to n like the cell buffer below) or every highlight
           would detach from the row it belongs to on the first sort */
        if (gd->row_color && gd->row_has_color) {
            ALLEGRO_COLOR* tc = NULL;
            unsigned char* th = NULL;
            Malloc(tc, ALLEGRO_COLOR, n);
            Malloc(th, unsigned char, n);
            if (tc && th) {
                for (i = 0; i < n; i++) {
                    tc[i] = gd->row_color[idx[i]];
                    th[i] = gd->row_has_color[idx[i]];
                }
                FreeNoLog(gd->row_color);
                FreeNoLog(gd->row_has_color);
                gd->row_color = tc;
                gd->row_has_color = th;
            } else {
                FreeNoLog(tc);
                FreeNoLog(th);
            }
        }
        /* nc holds exactly n rows; keep rows_cap in step or the next add_row
           would treat the stale (larger) capacity as free space and write past
           the reallocated buffer (heap overflow -> later double free) */
        gd->rows_cap = n;
        FreeNoLog(idx);
        gd->sort_col = col;
        gd->sort_dir = d;
        gd->selected_row = -1;
        /* row indices changed: clear the selection rather than mis-map it */
        if (gd->row_sel) memset(gd->row_sel, 0, gd->rows_cap);
        gd->anchor_row = -1;
    }
}

/* progress bar */

int n_gui_add_progressbar(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_PROGRESSBAR, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_PROGRESSBAR_DATA* pd = NULL;
    Malloc(pd, N_GUI_PROGRESSBAR_DATA, 1);
    __n_assert(pd, Free(wgt); return -1);
    pd->value = 0.0f;
    pd->text[0] = '\0';
    wgt->enabled = 0; /* display only: not interactive/focusable */
    wgt->data = pd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

void n_gui_progressbar_set_value(N_GUI_CTX* ctx, int widget_id, float value) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_PROGRESSBAR || !w->data)
        return;
    if (value < 0.0f)
        value = 0.0f;
    if (value > 1.0f)
        value = 1.0f;
    ((N_GUI_PROGRESSBAR_DATA*)w->data)->value = value;
}

float n_gui_progressbar_get_value(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_PROGRESSBAR || !w->data)
        return 0.0f;
    return ((const N_GUI_PROGRESSBAR_DATA*)w->data)->value;
}

void n_gui_progressbar_set_text(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    N_GUI_PROGRESSBAR_DATA* pd;
    if (!w || w->type != N_GUI_TYPE_PROGRESSBAR || !w->data)
        return;
    pd = (N_GUI_PROGRESSBAR_DATA*)w->data;
    pd->text[0] = '\0';
    if (text) {
        strncpy(pd->text, text, N_GUI_TEXT_MAX - 1);
        pd->text[N_GUI_TEXT_MAX - 1] = '\0';
    }
}

/* custom (owner-draw) widget */

/**
 * @brief add an owner-draw custom widget; returns the widget id or -1
 */
int n_gui_add_custom(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, N_GUI_CUSTOM_DRAW draw, void* user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_CUSTOM, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_CUSTOM_DATA* cd = NULL;
    Malloc(cd, N_GUI_CUSTOM_DATA, 1);
    __n_assert(cd, Free(wgt); return -1);
    cd->draw = draw;
    cd->user_data = user_data;
    wgt->data = cd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/* radiolist helpers */

/**
 *@brief add an item to a radiolist widget
 *@param ctx GUI context
 *@param widget_id id of the radiolist widget
 *@param text text of the item to add
 *@return index of the new item or -1 on error
 */
int n_gui_radiolist_add_item(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_RADIOLIST || !w->data) return -1;
    N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)w->data;
    if (!_items_grow(&rd->items, &rd->nb_items, &rd->items_capacity)) return -1;
    N_GUI_LISTITEM* item = &rd->items[rd->nb_items];
    item->text[0] = '\0';
    if (text) {
        strncpy(item->text, text, N_GUI_ID_MAX - 1);
        item->text[N_GUI_ID_MAX - 1] = '\0';
    }
    item->selected = 0;
    rd->nb_items++;
    return (int)(rd->nb_items - 1);
}

/**
 *@brief remove all items from a radiolist widget
 *@param ctx GUI context
 *@param widget_id id of the radiolist widget
 */
void n_gui_radiolist_clear(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_RADIOLIST && w->data) {
        N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)w->data;
        rd->nb_items = 0;
        rd->selected_index = -1;
        rd->scroll_offset = 0;
    }
}

/**
 *@brief get the selected item index in a radiolist widget
 *@param ctx GUI context
 *@param widget_id id of the radiolist widget
 *@return index of the selected item, or -1 if none
 */
int n_gui_radiolist_get_selected(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_RADIOLIST && w->data) {
        return ((N_GUI_RADIOLIST_DATA*)w->data)->selected_index;
    }
    return -1;
}

/**
 *@brief set the selected item in a radiolist widget
 *@param ctx GUI context
 *@param widget_id id of the radiolist widget
 *@param index index of the item to select, or -1 for none
 */
void n_gui_radiolist_set_selected(N_GUI_CTX* ctx, int widget_id, int index) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_RADIOLIST && w->data) {
        N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)w->data;
        if (index >= -1 && (index == -1 || (size_t)index < rd->nb_items)) {
            rd->selected_index = index;
        }
    }
}

/* combobox helpers */

/**
 *@brief remove all items from a combobox widget
 *@param ctx GUI context
 *@param widget_id id of the combobox widget
 */
void n_gui_combobox_clear(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_COMBOBOX || !w->data) return;
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)w->data;
    if (cd->items && cd->nb_items > 0) {
        memset(cd->items, 0, cd->nb_items * sizeof(N_GUI_LISTITEM));
    }
    cd->nb_items = 0;
    cd->selected_index = -1;
    cd->scroll_offset = 0;
    cd->highlight_index = -1;
    cd->is_open = 0;
}

int n_gui_combobox_add_item(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_COMBOBOX || !w->data) return -1;
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)w->data;
    if (!_items_grow(&cd->items, &cd->nb_items, &cd->items_capacity)) return -1;
    N_GUI_LISTITEM* item = &cd->items[cd->nb_items];
    item->text[0] = '\0';
    if (text) {
        strncpy(item->text, text, N_GUI_ID_MAX - 1);
        item->text[N_GUI_ID_MAX - 1] = '\0';
    }
    item->selected = 0;
    cd->nb_items++;
    return (int)(cd->nb_items - 1);
}

/**
 *@brief get the selected item index in a combobox widget
 *@param ctx GUI context
 *@param widget_id id of the combobox widget
 *@return index of the selected item, or -1 if none
 */
int n_gui_combobox_get_selected(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_COMBOBOX && w->data) {
        return ((N_GUI_COMBOBOX_DATA*)w->data)->selected_index;
    }
    return -1;
}

const char* n_gui_combobox_get_item_text(N_GUI_CTX* ctx, int widget_id, int index) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_COMBOBOX && w->data) {
        N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)w->data;
        if (index >= 0 && (size_t)index < cd->nb_items) {
            return cd->items[index].text;
        }
    }
    return "";
}

/**
 *@brief set the selected item in a combobox widget
 *@param ctx GUI context
 *@param widget_id id of the combobox widget
 *@param index index of the item to select, or -1 for none
 */
void n_gui_combobox_set_selected(N_GUI_CTX* ctx, int widget_id, int index) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_COMBOBOX && w->data) {
        N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)w->data;
        if (index >= -1 && (index == -1 || (size_t)index < cd->nb_items)) {
            cd->selected_index = index;
        }
    }
}

/* image helpers */

/**
 *@brief set the bitmap of an image widget
 *@param ctx GUI context
 *@param widget_id id of the image widget
 *@param bitmap pointer to the ALLEGRO_BITMAP to set
 */
void n_gui_image_set_bitmap(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bitmap) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_IMAGE && w->data) {
        ((N_GUI_IMAGE_DATA*)w->data)->bitmap = bitmap;
    }
}

/* label helpers */

/**
 *@brief set the text of a label widget
 *@param ctx GUI context
 *@param widget_id id of the label widget
 *@param text the text to set, or NULL to clear
 */
void n_gui_label_set_text(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LABEL && w->data) {
        N_GUI_LABEL_DATA* lb = (N_GUI_LABEL_DATA*)w->data;
        if (text) {
            strncpy(lb->text, text, N_GUI_TEXT_MAX - 1);
            lb->text[N_GUI_TEXT_MAX - 1] = '\0';
            _normalize_crlf(lb->text); /* normalize CRLF for Allegro5 compatibility */
        } else {
            lb->text[0] = '\0';
        }
    }
}

/**
 *@brief set the link URL of a label widget
 *@param ctx GUI context
 *@param widget_id id of the label widget
 *@param link the URL to set, or NULL to clear
 */
void n_gui_label_set_link(N_GUI_CTX* ctx, int widget_id, const char* link) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LABEL && w->data) {
        N_GUI_LABEL_DATA* lb = (N_GUI_LABEL_DATA*)w->data;
        if (link) {
            strncpy(lb->link, link, N_GUI_TEXT_MAX - 1);
            lb->link[N_GUI_TEXT_MAX - 1] = '\0';
        } else {
            lb->link[0] = '\0';
        }
    }
}

/* DROPDOWN MENU */

/*! helper: ensure dropmenu entries array has room for one more */
static int _dropmenu_entries_grow(N_GUI_DROPMENU_ENTRY** entries, const size_t* nb, size_t* cap) {
    if (*nb >= *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap) * 2;
        if (new_cap < *cap) return 0; /* overflow */
        N_GUI_DROPMENU_ENTRY* tmp = NULL;
        Malloc(tmp, N_GUI_DROPMENU_ENTRY, new_cap);
        if (!tmp) return 0;
        if (*entries && *nb > 0) {
            memcpy(tmp, *entries, (*nb) * sizeof(N_GUI_DROPMENU_ENTRY));
        }
        FreeNoLog(*entries);
        *entries = tmp;
        *cap = new_cap;
    }
    return 1;
}

/**
 * @brief Add a dropdown menu widget
 * @return widget id, or -1 on error
 */
int n_gui_add_dropmenu(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, void (*on_open)(int, void*), void* on_open_user_data) {
    __n_assert(ctx, return -1);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    __n_assert(win, return -1);

    N_GUI_WIDGET* wgt = _new_widget(ctx, N_GUI_TYPE_DROPMENU, x, y, w, h);
    __n_assert(wgt, return -1);

    N_GUI_DROPMENU_DATA* dd = NULL;
    Malloc(dd, N_GUI_DROPMENU_DATA, 1);
    __n_assert(dd, Free(wgt); return -1);
    dd->label[0] = '\0';
    if (label) {
        strncpy(dd->label, label, N_GUI_ID_MAX - 1);
        dd->label[N_GUI_ID_MAX - 1] = '\0';
    }
    dd->entries = NULL;
    dd->nb_entries = 0;
    dd->entries_capacity = 0;
    dd->is_open = 0;
    dd->scroll_offset = 0;
    dd->highlight_index = -1;
    dd->max_visible = ctx->style.dropmenu_max_visible;
    dd->item_height = h;
    dd->on_open = on_open;
    dd->on_open_user_data = on_open_user_data;
    dd->panel_bitmap = NULL;
    dd->item_hover_bitmap = NULL;
    dd->flags = 0;
    wgt->data = dd;
    wgt->norm_x = 0.0f;
    wgt->norm_y = 0.0f;
    wgt->norm_w = 0.0f;
    wgt->norm_h = 0.0f;

    list_push(win->widgets, wgt, _destroy_widget);
    _n_gui_widget_capture_normalized(win, wgt);
    _register_widget(ctx, wgt);
    return wgt->id;
}

/**
 * @brief Set dropmenu feature flags (bitmask of N_GUI_DROPMENU_* values)
 */
void n_gui_dropmenu_set_flags(N_GUI_CTX* ctx, int widget_id, int flags) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) return;
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    dd->flags = flags;
}

/**
 * @brief Replace the button label shown on a dropmenu.
 *
 * The label is the text drawn on the collapsed dropmenu button (before the
 * arrow). Useful when a single dropmenu widget is reused to present different
 * menus (a pooled toolbar category button, for instance). A NULL label clears
 * it. The text is truncated to N_GUI_ID_MAX-1 bytes.
 *
 * @param ctx the GUI context
 * @param widget_id the dropmenu widget id
 * @param label new button label, or NULL to clear
 */
void n_gui_dropmenu_set_label(N_GUI_CTX* ctx, int widget_id, const char* label) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) return;
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    if (label) {
        snprintf(dd->label, sizeof(dd->label), "%s", label);
    } else {
        dd->label[0] = '\0';
    }
}

/**
 * @brief Add a static entry to a dropdown menu
 * @return entry index, or -1 on error
 */
int n_gui_dropmenu_add_entry(N_GUI_CTX* ctx, int widget_id, const char* text, int tag, void (*on_click)(int, int, int, void*), void* user_data) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) return -1;
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    if (!_dropmenu_entries_grow(&dd->entries, &dd->nb_entries, &dd->entries_capacity)) return -1;
    N_GUI_DROPMENU_ENTRY* e = &dd->entries[dd->nb_entries];
    memset(e, 0, sizeof(*e));
    if (text) {
        strncpy(e->text, text, N_GUI_ID_MAX - 1);
        e->text[N_GUI_ID_MAX - 1] = '\0';
    }
    e->is_dynamic = 0;
    e->tag = tag;
    e->on_click = on_click;
    e->user_data = user_data;
    dd->nb_entries++;
    return (int)(dd->nb_entries - 1);
}

/**
 * @brief Add a dynamic entry (rebuilt each time menu opens)
 * @return entry index, or -1 on error
 */
int n_gui_dropmenu_add_dynamic_entry(N_GUI_CTX* ctx, int widget_id, const char* text, int tag, void (*on_click)(int, int, int, void*), void* user_data) {
    int idx = n_gui_dropmenu_add_entry(ctx, widget_id, text, tag, on_click, user_data);
    if (idx < 0)
        return idx;

    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w) {
        n_log(LOG_ERR, "Invalid NULL widget for ctx %p widget_id %d", ctx, widget_id);
        return -1;
    }

    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    if (!dd) {
        n_log(LOG_ERR, "Widget has NULL data for ctx %p widget_id %d", ctx, widget_id);
        return -1;
    }

    dd->entries[idx].is_dynamic = 1;

    return idx;
}

/**
 * @brief Remove all dynamic entries (keep static ones)
 */
void n_gui_dropmenu_clear_dynamic(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) return;
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    /* compact: keep only non-dynamic entries */
    size_t write = 0;
    for (size_t i = 0; i < dd->nb_entries; i++) {
        if (!dd->entries[i].is_dynamic) {
            if (write != i) {
                dd->entries[write] = dd->entries[i];
            }
            write++;
        }
    }
    dd->nb_entries = write;
    /* clamp scroll_offset after removal */
    if (dd->nb_entries == 0) {
        dd->scroll_offset = 0;
    } else {
        int max_vis = dd->max_visible > 0 ? dd->max_visible : 8;
        int dm_max = (int)dd->nb_entries - max_vis;
        if (dm_max < 0) dm_max = 0;
        if (dd->scroll_offset > dm_max) dd->scroll_offset = dm_max;
    }
}

/**
 * @brief Remove all entries from a dropdown menu
 */
void n_gui_dropmenu_clear(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) return;
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    dd->nb_entries = 0;
    dd->scroll_offset = 0;
}

/**
 * @brief Update text of an existing entry
 */
void n_gui_dropmenu_set_entry_text(N_GUI_CTX* ctx, int widget_id, int index, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) return;
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    if (index < 0 || (size_t)index >= dd->nb_entries) return;
    if (text) {
        strncpy(dd->entries[index].text, text, N_GUI_ID_MAX - 1);
        dd->entries[index].text[N_GUI_ID_MAX - 1] = '\0';
    }
}

/**
 * @brief Get number of entries in a dropdown menu
 */
int n_gui_dropmenu_get_count(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_DROPMENU && w->data) {
        return (int)((N_GUI_DROPMENU_DATA*)w->data)->nb_entries;
    }
    return 0;
}

/**
 * @brief Width the open drop-down panel needs to show its entries.
 *
 * The collapsed dropmenu button is often narrow (it only shows a short category
 * label), but the floating panel must be wide enough that its entries read in
 * full. This returns the larger of the button width and the widest entry text,
 * padded by the item text padding on both sides and, when the entry count
 * overflows the visible window, the scrollbar width. The draw and hit-test paths
 * share it so the panel background, the clickable item area, and the scrollbar
 * all use one width.
 *
 * @param ctx the GUI context
 * @param widget_id the dropmenu widget id
 * @return the panel width in pixels, or 0 for a non-dropmenu widget
 */
float n_gui_dropmenu_panel_width(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) return 0.0f;
    const N_GUI_DROPMENU_DATA* dd = (const N_GUI_DROPMENU_DATA*)w->data;
    ALLEGRO_FONT* font = w->font ? w->font : ctx->default_font;
    float pad = ctx->style.item_text_padding;
    float sb = ctx->style.scrollbar_size;
    float pw = w->w;
    int need_sb = ((int)dd->nb_entries > dd->max_visible);
    size_t i;
    if (sb < 10.0f) sb = 10.0f;
    if (!font) return pw;
    for (i = 0; i < dd->nb_entries; i++) {
        float tw = _text_w(font, dd->entries[i].text) + pad * 2.0f + (need_sb ? sb : 0.0f);
        if (tw > pw) pw = tw;
    }
    return pw;
}

/* BITMAP SKINNING - setter functions */

/**
 * @brief Set optional bitmap overlays on a window's body and titlebar.
 * @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *       for the window's lifetime and destroy them after the GUI context is destroyed.
 */
void n_gui_window_set_bitmaps(N_GUI_CTX* ctx, int window_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* titlebar, int bg_scale_mode) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) {
        n_log(LOG_WARNING, "n_gui_window_set_bitmaps: window %d not found", window_id);
        return;
    }
    win->bg_bitmap = bg;
    win->titlebar_bitmap = titlebar;
    win->bg_scale_mode = bg_scale_mode;
}

/**
 * @brief Set optional bitmap overlays on a slider widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_slider_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* track, ALLEGRO_BITMAP* fill, ALLEGRO_BITMAP* handle, ALLEGRO_BITMAP* handle_hover, ALLEGRO_BITMAP* handle_active) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SLIDER || !w->data) {
        n_log(LOG_WARNING, "n_gui_slider_set_bitmaps: widget %d is not a slider", widget_id);
        return;
    }
    N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)w->data;
    sd->track_bitmap = track;
    sd->fill_bitmap = fill;
    sd->handle_bitmap = handle;
    sd->handle_hover_bitmap = handle_hover;
    sd->handle_active_bitmap = handle_active;
}

/**
 * @brief Set optional bitmap overlays on a scrollbar widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_scrollbar_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* track, ALLEGRO_BITMAP* thumb, ALLEGRO_BITMAP* thumb_hover, ALLEGRO_BITMAP* thumb_active) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_SCROLLBAR || !w->data) {
        n_log(LOG_WARNING, "n_gui_scrollbar_set_bitmaps: widget %d is not a scrollbar", widget_id);
        return;
    }
    N_GUI_SCROLLBAR_DATA* sb = (N_GUI_SCROLLBAR_DATA*)w->data;
    sb->track_bitmap = track;
    sb->thumb_bitmap = thumb;
    sb->thumb_hover_bitmap = thumb_hover;
    sb->thumb_active_bitmap = thumb_active;
}

/**
 * @brief Set optional bitmap overlays on a checkbox widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_checkbox_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* box, ALLEGRO_BITMAP* box_checked, ALLEGRO_BITMAP* box_hover) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_CHECKBOX || !w->data) {
        n_log(LOG_WARNING, "n_gui_checkbox_set_bitmaps: widget %d is not a checkbox", widget_id);
        return;
    }
    N_GUI_CHECKBOX_DATA* cd = (N_GUI_CHECKBOX_DATA*)w->data;
    cd->box_bitmap = box;
    cd->box_checked_bitmap = box_checked;
    cd->box_hover_bitmap = box_hover;
}

/**
 * @brief Set optional background bitmap on a textarea widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_textarea_set_bitmap(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data) {
        n_log(LOG_WARNING, "n_gui_textarea_set_bitmap: widget %d is not a textarea", widget_id);
        return;
    }
    ((N_GUI_TEXTAREA_DATA*)w->data)->bg_bitmap = bg;
}

/**
 * @brief Set a mask character for password-style input.
 * @param mask The character to display instead of actual text (e.g. '*').
 *             Set to '\0' to disable masking.
 */
void n_gui_textarea_set_mask_char(N_GUI_CTX* ctx, int widget_id, char mask) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data) {
        n_log(LOG_WARNING, "n_gui_textarea_set_mask_char: widget %d is not a textarea", widget_id);
        return;
    }
    ((N_GUI_TEXTAREA_DATA*)w->data)->mask_char = mask;
}

/**
 * @brief Set placeholder/hint text shown dimmed while a textarea is empty.
 */
void n_gui_textarea_set_placeholder(N_GUI_CTX* ctx, int widget_id, const char* text) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    N_GUI_TEXTAREA_DATA* td = NULL;
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data) {
        n_log(LOG_WARNING, "n_gui_textarea_set_placeholder: widget %d is not a textarea", widget_id);
        return;
    }
    td = (N_GUI_TEXTAREA_DATA*)w->data;
    FreeNoLog(td->placeholder);
    if (text && text[0])
        td->placeholder = strdup(text);
}

/**
 * @brief Set optional bitmap overlays on a listbox widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_listbox_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* item_bg, ALLEGRO_BITMAP* item_selected) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) {
        n_log(LOG_WARNING, "n_gui_listbox_set_bitmaps: widget %d is not a listbox", widget_id);
        return;
    }
    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
    ld->bg_bitmap = bg;
    ld->item_bg_bitmap = item_bg;
    ld->item_selected_bitmap = item_selected;
}

/**
 * @brief Set optional bitmap overlays on a radiolist widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_radiolist_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* item_bg, ALLEGRO_BITMAP* item_selected) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_RADIOLIST || !w->data) {
        n_log(LOG_WARNING, "n_gui_radiolist_set_bitmaps: widget %d is not a radiolist", widget_id);
        return;
    }
    N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)w->data;
    rd->bg_bitmap = bg;
    rd->item_bg_bitmap = item_bg;
    rd->item_selected_bitmap = item_selected;
}

/**
 * @brief Set optional bitmap overlays on a combobox widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_combobox_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* item_bg, ALLEGRO_BITMAP* item_selected) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_COMBOBOX || !w->data) {
        n_log(LOG_WARNING, "n_gui_combobox_set_bitmaps: widget %d is not a combobox", widget_id);
        return;
    }
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)w->data;
    cd->bg_bitmap = bg;
    cd->item_bg_bitmap = item_bg;
    cd->item_selected_bitmap = item_selected;
}

/**
 * @brief Set optional bitmap overlays on a dropmenu widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_dropmenu_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* panel, ALLEGRO_BITMAP* item_hover) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_DROPMENU || !w->data) {
        n_log(LOG_WARNING, "n_gui_dropmenu_set_bitmaps: widget %d is not a dropmenu", widget_id);
        return;
    }
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)w->data;
    dd->panel_bitmap = panel;
    dd->item_hover_bitmap = item_hover;
}

/**
 * @brief Set optional background bitmap on a label widget.
 * @note Bitmaps are NOT owned by N_GUI.
 */
void n_gui_label_set_bitmap(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LABEL || !w->data) {
        n_log(LOG_WARNING, "n_gui_label_set_bitmap: widget %d is not a label", widget_id);
        return;
    }
    ((N_GUI_LABEL_DATA*)w->data)->bg_bitmap = bg;
}

/* DRAWING - individual widget renderers */

/*! helper: draw a bitmap into a rectangle, respecting the given scale mode.
 *  mode: N_GUI_IMAGE_FIT, N_GUI_IMAGE_STRETCH, or N_GUI_IMAGE_CENTER. */
static void _draw_bitmap_scaled(ALLEGRO_BITMAP* bmp, float dx, float dy, float dw, float dh, int mode) {
    float bw = (float)al_get_bitmap_width(bmp);
    float bh = (float)al_get_bitmap_height(bmp);
    if (mode == N_GUI_IMAGE_STRETCH) {
        al_draw_scaled_bitmap(bmp, 0, 0, bw, bh, dx, dy, dw, dh, 0);
    } else if (mode == N_GUI_IMAGE_CENTER) {
        al_draw_bitmap(bmp, dx + (dw - bw) / 2.0f, dy + (dh - bh) / 2.0f, 0);
    } else {
        /* N_GUI_IMAGE_FIT: maintain aspect ratio */
        float sx = dw / bw;
        float sy = dh / bh;
        float s = (sx < sy) ? sx : sy;
        float rw = bw * s;
        float rh = bh * s;
        al_draw_scaled_bitmap(bmp, 0, 0, bw, bh, dx + (dw - rw) / 2.0f, dy + (dh - rh) / 2.0f, rw, rh, 0);
    }
}

/*! helper: select per-state bitmap with fallback chain.
 *  Returns the appropriate bitmap for the given state, falling back to normal_bmp.
 *  Returns NULL if all pointers are NULL. */
static ALLEGRO_BITMAP* _select_state_bitmap(int state, ALLEGRO_BITMAP* normal_bmp, ALLEGRO_BITMAP* hover_bmp, ALLEGRO_BITMAP* active_bmp) {
    if ((state & N_GUI_STATE_ACTIVE) && active_bmp) return active_bmp;
    if ((state & N_GUI_STATE_HOVER) && hover_bmp) return hover_bmp;
    if (normal_bmp) return normal_bmp;
    return NULL;
}

/*! helper: set clipping rectangle in world space, transforming to screen space.
 *  This accounts for the current Allegro transform (e.g. global scroll) so that
 *  clipping works correctly even when a translation transform is active.
 *  The resulting clip rect is also intersected with the current (parent) clip
 *  to avoid drawing outside the parent's visible area. */
static void _set_clipping_rect_transformed(int wx, int wy, int ww, int wh) {
    /* Transform all 4 corners from world space to screen space so that the
     * axis-aligned bounding box is computed correctly for any affine transform
     * (including negative scaling, rotation, or shear). */
    const ALLEGRO_TRANSFORM* tf = al_get_current_transform();
    float cx[4], cy[4];
    cx[0] = (float)wx;
    cy[0] = (float)wy;
    cx[1] = (float)(wx + ww);
    cy[1] = (float)wy;
    cx[2] = (float)wx;
    cy[2] = (float)(wy + wh);
    cx[3] = (float)(wx + ww);
    cy[3] = (float)(wy + wh);
    for (int i = 0; i < 4; i++)
        al_transform_coordinates(tf, &cx[i], &cy[i]);

    float min_x = cx[0], max_x = cx[0];
    float min_y = cy[0], max_y = cy[0];
    for (int i = 1; i < 4; i++) {
        if (cx[i] < min_x) min_x = cx[i];
        if (cx[i] > max_x) max_x = cx[i];
        if (cy[i] < min_y) min_y = cy[i];
        if (cy[i] > max_y) max_y = cy[i];
    }

    int new_x = (int)min_x;
    int new_y = (int)min_y;
    int new_w = (int)(max_x - min_x);
    int new_h = (int)(max_y - min_y);

    /* intersect with the current (parent) clipping rectangle */
    int pcx, pcy, pcw, pch;
    al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);

    int right = new_x + new_w;
    int bottom = new_y + new_h;
    int prev_right = pcx + pcw;
    int prev_bottom = pcy + pch;

    if (new_x < pcx) new_x = pcx;
    if (new_y < pcy) new_y = pcy;
    if (right > prev_right) right = prev_right;
    if (bottom > prev_bottom) bottom = prev_bottom;

    new_w = right - new_x;
    new_h = bottom - new_y;
    if (new_w < 0) new_w = 0;
    if (new_h < 0) new_h = 0;

    al_set_clipping_rectangle(new_x, new_y, new_w, new_h);
}

/*! helper: draw text truncated to a maximum pixel width.
 *  If the text is wider than max_w, it is clipped and "..." is appended. */
static void _draw_text_truncated(ALLEGRO_FONT* font, ALLEGRO_COLOR color, float x, float y, float max_w, const char* text) {
    if (!font || !text || !text[0]) return;
    float tw = _text_w(font, text);
    if (tw <= max_w) {
        al_draw_text(font, color, x, y, 0, text);
        return;
    }
    /* find how many chars fit + "...", stepping one UTF-8 sequence at a
     * time so a multi-byte codepoint is never measured or kept halfway */
    float ellipsis_w = _text_w(font, "...");
    float avail = max_w - ellipsis_w;
    if (avail < 0) avail = 0;
    size_t len = strlen(text);
    char buf[N_GUI_TEXT_MAX];
    size_t fit = 0;
    size_t i = 0;
    while (i < len) {
        size_t step = 1;
        unsigned char lead = (unsigned char)text[i];
        if ((lead & 0xE0) == 0xC0)
            step = 2;
        else if ((lead & 0xF0) == 0xE0)
            step = 3;
        else if ((lead & 0xF8) == 0xF0)
            step = 4;
        if (i + step > len || i + step > (size_t)(N_GUI_TEXT_MAX - 5)) break;
        memcpy(buf + i, text + i, step);
        buf[i + step] = '\0';
        float cw = _text_w(font, buf);
        if (cw > avail) break;
        fit = i + step;
        i += step;
    }
    buf[fit] = '.';
    buf[fit + 1] = '.';
    buf[fit + 2] = '.';
    buf[fit + 3] = '\0';
    al_draw_text(font, color, x, y, 0, buf);
}

/*! helper: draw justified text within a given width, with multi-line word wrapping.
 *  Words are packed into lines using minimum spacing; full lines are justified
 *  (gaps spread evenly), the last line is left-aligned.  Only shows "..." when a
 *  single word is wider than max_w. */
static void _draw_text_justified(ALLEGRO_FONT* font, ALLEGRO_COLOR color, float x, float y, float max_w, float max_h, const char* text) {
    if (!font || !text || !text[0]) return;

    /* split text into words */
    char buf[N_GUI_TEXT_MAX];
    snprintf(buf, N_GUI_TEXT_MAX, "%s", text);

    char* words[256];
    float word_widths[256];
    int nwords = 0;
    char* tok = strtok(buf, " \t");
    while (tok && nwords < 256) {
        words[nwords] = tok;
        word_widths[nwords] = _text_w(font, tok);
        nwords++;
        tok = strtok(NULL, " \t");
    }
    if (nwords == 0) return;
    if (nwords == 1) {
        _draw_text_truncated(font, color, x, y, max_w, text);
        return;
    }

    float fh = (float)al_get_font_line_height(font);
    float space_w = _text_w(font, " ");
    float cy = y;
    int line_start = 0;

    while (line_start < nwords) {
        /* stop if there is no vertical space for another line */
        if (cy + fh > y + max_h + 0.5f) break;

        /* pack as many words as fit on this line with minimum (single-space) gaps */
        float line_words_w = word_widths[line_start];
        int line_end = line_start + 1;
        for (int i = line_start + 1; i < nwords; i++) {
            float test_w = line_words_w + space_w + word_widths[i];
            if (test_w > max_w) break;
            line_words_w = test_w;
            line_end = i + 1;
        }

        int words_on_line = line_end - line_start;
        int is_last_line = (line_end >= nwords);

        if (words_on_line == 1) {
            /* single word: truncate with "..." if it exceeds the line width */
            if (word_widths[line_start] > max_w) {
                _draw_text_truncated(font, color, x, cy, max_w, words[line_start]);
            } else {
                al_draw_text(font, color, x, cy, 0, words[line_start]);
            }
        } else if (is_last_line) {
            /* last line: left-align with normal spacing */
            float cx = x;
            for (int i = line_start; i < line_end; i++) {
                al_draw_text(font, color, cx, cy, 0, words[i]);
                cx += word_widths[i] + space_w;
            }
        } else {
            /* full line: justify by distributing extra space between words */
            float total_word_w = 0;
            for (int i = line_start; i < line_end; i++) total_word_w += word_widths[i];
            float total_space = max_w - total_word_w;
            if (total_space < 0) total_space = 0;
            float gap = total_space / (float)(words_on_line - 1);
            float cx = x;
            for (int i = line_start; i < line_end; i++) {
                al_draw_text(font, color, cx, cy, 0, words[i]);
                cx += word_widths[i] + gap;
            }
        }

        line_start = line_end;
        cy += fh;
    }
}

/*! return the byte length of a UTF-8 character from its lead byte */
static int _utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1; /* invalid byte, treat as single byte */
}

/*! helper: compute the total content height of multiline textarea text
 *  (with line-wrap), in pixels. Mirrors the drawing logic. */
static float _textarea_content_height(const N_GUI_TEXTAREA_DATA* td, ALLEGRO_FONT* font, float widget_w, float pad) {
    if (!font || td->text_len == 0) return 0;
    float fh = (float)al_get_font_line_height(font);
    float cx = 0;
    float cy = fh; /* at least one line */
    for (size_t i = 0; i < td->text_len;) {
        if (td->text[i] == '\n') {
            cx = 0;
            cy += fh;
            i++;
            continue;
        }
        int clen = _utf8_char_len((unsigned char)td->text[i]);
        if (i + (size_t)clen > td->text_len) clen = (int)(td->text_len - i);
        char ch[5];
        memcpy(ch, &td->text[i], (size_t)clen);
        ch[clen] = '\0';
        float cw = _text_w(font, ch);
        if (cx + cw > widget_w - pad * 2) {
            cx = 0;
            cy += fh;
        }
        cx += cw;
        i += (size_t)clen;
    }
    return cy;
}

/**
 *@brief scroll a multiline textarea to the bottom
 *@param ctx GUI context
 *@param widget_id id of the textarea widget
 */
void n_gui_textarea_scroll_to_bottom(N_GUI_CTX* ctx, int widget_id) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data) return;
    N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)w->data;
    if (!td->multiline || td->text_len == 0) return;

    float pad = ctx->style.textarea_padding;
    ALLEGRO_FONT* font = w->font ? w->font : ctx->default_font;
    float view_h = w->h - pad * 2;
    float ch = _textarea_content_height(td, font, w->w, pad);
    float max_sy = ch - view_h;
    if (max_sy < 0) max_sy = 0;
    td->scroll_y = (int)max_sy;
} /* n_gui_textarea_scroll_to_bottom */

/**
 *@brief set the text selection range (byte offsets into text content)
 *@param ctx GUI context
 *@param widget_id id of the textarea widget
 *@param start selection start byte offset
 *@param end selection end byte offset
 */
void n_gui_textarea_set_selection(N_GUI_CTX* ctx, int widget_id, size_t start, size_t end) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data) return;
    N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)w->data;
    if (start > td->text_len) start = td->text_len;
    if (end > td->text_len) end = td->text_len;
    td->sel_start = start;
    td->sel_end = end;
    td->cursor_pos = end;
} /* n_gui_textarea_set_selection */

/**
 *@brief scroll a multiline textarea so that the given byte offset is vertically centered
 *@param ctx GUI context
 *@param widget_id id of the textarea widget
 *@param byte_offset byte offset in text to center on screen
 */
void n_gui_textarea_scroll_to_offset(N_GUI_CTX* ctx, int widget_id, size_t byte_offset) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data) return;
    N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)w->data;
    if (!td->multiline || td->text_len == 0) return;

    float pad = ctx->style.textarea_padding;
    ALLEGRO_FONT* font = w->font ? w->font : ctx->default_font;
    if (!font) return;
    float fh = (float)al_get_font_line_height(font);
    float view_h = w->h - pad * 2;

    /* Account for scrollbar width, matching the draw function logic:
       first check if scrollbar is needed, then use the narrower width */
    float content_h_initial = _textarea_content_height(td, font, w->w, pad);
    float sb_size = (content_h_initial > view_h) ? ctx->style.scrollbar_size : 0;
    float text_area_w = w->w - sb_size;

    /* compute pixel Y of the target byte offset using line-wrap logic
       with the same text_area_w the draw function will use */
    float cx = 0;
    float cy = 0;
    size_t target = (byte_offset <= td->text_len) ? byte_offset : td->text_len;
    for (size_t i = 0; i < target && i < td->text_len;) {
        if (td->text[i] == '\n') {
            cx = 0;
            cy += fh;
            i++;
            continue;
        }
        int clen = _utf8_char_len((unsigned char)td->text[i]);
        if (i + (size_t)clen > td->text_len) clen = (int)(td->text_len - i);
        char ch[5];
        memcpy(ch, &td->text[i], (size_t)clen);
        ch[clen] = '\0';
        float cw = _text_w(font, ch);
        if (cx + cw > text_area_w - pad * 2) {
            cx = 0;
            cy += fh;
        }
        cx += cw;
        i += (size_t)clen;
    }

    /* recompute content_h with the same text_area_w for consistent max_sy */
    float content_h = _textarea_content_height(td, font, text_area_w, pad);
    float max_sy = content_h - view_h;
    if (max_sy < 0) max_sy = 0;
    float ideal = cy - view_h / 2 + fh / 2;
    if (ideal < 0) ideal = 0;
    if (ideal > max_sy) ideal = max_sy;
    td->scroll_y = (int)ideal;
    td->scroll_from_wheel = 0;
} /* n_gui_textarea_scroll_to_offset */

/**
 *@brief return the current text length in bytes
 *@param ctx GUI context
 *@param widget_id id of the textarea widget
 *@return text length in bytes, or 0 if widget is invalid
 */
size_t n_gui_textarea_get_text_length(N_GUI_CTX* ctx, int widget_id) {
    const N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_TEXTAREA || !w->data) return 0;
    const N_GUI_TEXTAREA_DATA* td = (const N_GUI_TEXTAREA_DATA*)w->data;
    return td->text_len;
} /* n_gui_textarea_get_text_length */

/*! helper: find the byte offset in justified text closest to a click position.
 *  text_x, text_y = top-left of the text area.
 *  Returns -1 on error. */
static int _justified_char_at_pos(const char* text, ALLEGRO_FONT* font, float max_w, float text_x, float text_y, float scroll_y, float click_mx, float click_my) {
    if (!font || !text || !text[0]) return -1;

    /* split text into words, tracking their byte offsets in original text */
    size_t text_len = strlen(text);
    char buf[N_GUI_TEXT_MAX];
    snprintf(buf, N_GUI_TEXT_MAX, "%s", text);

    /* word start byte offsets in original text */
    int word_starts[256];
    int word_lens[256]; /* byte lengths */
    float word_widths[256];
    int nwords = 0;

    size_t i = 0;
    while (i < text_len && nwords < 256) {
        /* skip whitespace */
        while (i < text_len && (text[i] == ' ' || text[i] == '\t')) i++;
        if (i >= text_len) break;
        size_t ws = i;
        while (i < text_len && text[i] != ' ' && text[i] != '\t') i++;
        int wlen = (int)(i - ws);
        word_starts[nwords] = (int)ws;
        word_lens[nwords] = wlen;
        char tmp[N_GUI_TEXT_MAX];
        memcpy(tmp, &text[ws], (size_t)wlen);
        tmp[wlen] = '\0';
        word_widths[nwords] = _text_w(font, tmp);
        nwords++;
    }
    if (nwords == 0) return 0;

    float fh = (float)al_get_font_line_height(font);
    float space_w = _text_w(font, " ");
    float cy = text_y - scroll_y;
    float click_y_content = click_my;

    /* find the target line */
    int line_start = 0;
    while (line_start < nwords) {
        float line_words_w = word_widths[line_start];
        int line_end = line_start + 1;
        for (int j = line_start + 1; j < nwords; j++) {
            float test_w = line_words_w + space_w + word_widths[j];
            if (test_w > max_w) break;
            line_words_w = test_w;
            line_end = j + 1;
        }

        int words_on_line = line_end - line_start;
        int is_last_line = (line_end >= nwords);

        if (click_y_content >= cy && click_y_content < cy + fh) {
            /* this is the target line - find character */
            float click_x_rel = click_mx - text_x;
            if (click_x_rel < 0) return word_starts[line_start];

            /* compute gap for this line */
            float gap = space_w;
            if (!is_last_line && words_on_line > 1) {
                float total_word_w = 0;
                for (int j = line_start; j < line_end; j++) total_word_w += word_widths[j];
                float total_space = max_w - total_word_w;
                if (total_space < 0) total_space = 0;
                gap = total_space / (float)(words_on_line - 1);
            }

            /* walk through words on this line */
            float cx = 0;
            for (int j = line_start; j < line_end; j++) {
                /* check within this word */
                if (click_x_rel >= cx && click_x_rel < cx + word_widths[j]) {
                    /* find character within word */
                    float wx = 0;
                    int best_off = 0;
                    float best_dist = click_x_rel - cx;
                    if (best_dist < 0) best_dist = -best_dist;
                    for (int k = 0; k < word_lens[j];) {
                        int clen = _utf8_char_len((unsigned char)text[word_starts[j] + k]);
                        if (k + clen > word_lens[j]) clen = word_lens[j] - k;
                        char ch[5];
                        memcpy(ch, &text[word_starts[j] + k], (size_t)clen);
                        ch[clen] = '\0';
                        float cw = _text_w(font, ch);
                        wx += cw;
                        float dist = (click_x_rel - cx) - wx;
                        if (dist < 0) dist = -dist;
                        if (dist < best_dist) {
                            best_dist = dist;
                            best_off = k + clen;
                        }
                        k += clen;
                    }
                    return word_starts[j] + best_off;
                }
                /* check in gap between words */
                if (j < line_end - 1 && click_x_rel >= cx + word_widths[j] && click_x_rel < cx + word_widths[j] + gap) {
                    /* in the gap: snap to end of current word or start of next */
                    float mid = cx + word_widths[j] + gap / 2.0f;
                    if (click_x_rel < mid)
                        return word_starts[j] + word_lens[j];
                    else
                        return word_starts[j + 1];
                }
                cx += word_widths[j] + gap;
            }
            /* past end of line */
            return word_starts[line_end - 1] + word_lens[line_end - 1];
        }

        cy += fh;
        line_start = line_end;
    }
    /* below all text: return end */
    return (int)text_len;
}

/*! helper: draw selection highlight rectangles over justified/wrapped text.
 *  Mirrors the word-wrap layout of _draw_text_justified. */
static void _draw_justified_selection(ALLEGRO_FONT* font, float x, float y, float max_w, float max_h, const char* text, int sel_start, int sel_end, ALLEGRO_COLOR sel_color) {
    if (!font || !text || !text[0] || sel_start == sel_end) return;
    int slo = sel_start < sel_end ? sel_start : sel_end;
    int shi = sel_start < sel_end ? sel_end : sel_start;
    size_t text_len = strlen(text);
    if ((size_t)slo > text_len) slo = (int)text_len;
    if ((size_t)shi > text_len) shi = (int)text_len;

    /* split into words with byte offsets */
    int word_starts[256], word_lens[256];
    float word_widths[256];
    int nwords = 0;
    size_t i = 0;
    while (i < text_len && nwords < 256) {
        while (i < text_len && (text[i] == ' ' || text[i] == '\t')) i++;
        if (i >= text_len) break;
        size_t ws = i;
        while (i < text_len && text[i] != ' ' && text[i] != '\t') i++;
        int wlen = (int)(i - ws);
        word_starts[nwords] = (int)ws;
        word_lens[nwords] = wlen;
        char tmp[N_GUI_TEXT_MAX];
        memcpy(tmp, &text[ws], (size_t)wlen);
        tmp[wlen] = '\0';
        word_widths[nwords] = _text_w(font, tmp);
        nwords++;
    }
    if (nwords == 0) return;

    float fh = (float)al_get_font_line_height(font);
    float space_w = _text_w(font, " ");
    float cy = y;
    int line_start = 0;

    while (line_start < nwords) {
        if (cy + fh > y + max_h + 0.5f) break;
        float line_words_w = word_widths[line_start];
        int line_end = line_start + 1;
        for (int j = line_start + 1; j < nwords; j++) {
            float test_w = line_words_w + space_w + word_widths[j];
            if (test_w > max_w) break;
            line_words_w = test_w;
            line_end = j + 1;
        }
        int words_on_line = line_end - line_start;
        int is_last_line = (line_end >= nwords);

        /* compute gap for this line */
        float gap = space_w;
        if (!is_last_line && words_on_line > 1) {
            float total_word_w = 0;
            for (int j = line_start; j < line_end; j++) total_word_w += word_widths[j];
            float total_space = max_w - total_word_w;
            if (total_space < 0) total_space = 0;
            gap = total_space / (float)(words_on_line - 1);
        }

        /* walk words and draw selection rects where overlapping */
        float cx = x;
        for (int j = line_start; j < line_end; j++) {
            int ws2 = word_starts[j];
            int we = ws2 + word_lens[j];
            /* check if selection overlaps this word */
            if (slo < we && shi > ws2) {
                /* find pixel range within the word */
                float sx1 = 0, sx2 = word_widths[j];
                if (slo > ws2) {
                    char tmp2[N_GUI_TEXT_MAX];
                    int off = slo - ws2;
                    memcpy(tmp2, &text[ws2], (size_t)off);
                    tmp2[off] = '\0';
                    sx1 = _text_w(font, tmp2);
                }
                if (shi < we) {
                    char tmp2[N_GUI_TEXT_MAX];
                    int off = shi - ws2;
                    memcpy(tmp2, &text[ws2], (size_t)off);
                    tmp2[off] = '\0';
                    sx2 = _text_w(font, tmp2);
                }
                al_draw_filled_rectangle(cx + sx1, cy, cx + sx2, cy + fh, sel_color);
            }
            /* also highlight the gap (space) between words if selected */
            if (j < line_end - 1) {
                int gap_start = we;               /* byte after word end */
                int gap_end = word_starts[j + 1]; /* byte of next word start */
                if (slo < gap_end && shi > gap_start) {
                    al_draw_filled_rectangle(cx + word_widths[j], cy,
                                             cx + word_widths[j] + gap, cy + fh, sel_color);
                }
            }
            cx += word_widths[j] + gap;
        }

        cy += fh;
        line_start = line_end;
    }
}

/*! helper: compute the total content height of justified/wrapped label text,
 *  in pixels. Mirrors the _draw_text_justified word-wrap logic. */
static float _label_content_height(const char* text, ALLEGRO_FONT* font, float max_w) {
    if (!font || !text || !text[0]) return 0;
    float fh = (float)al_get_font_line_height(font);
    float space_w = _text_w(font, " ");

    char buf[N_GUI_TEXT_MAX];
    snprintf(buf, N_GUI_TEXT_MAX, "%s", text);

    float word_widths[256];
    int nwords = 0;
    const char* tok = strtok(buf, " \t");
    while (tok && nwords < 256) {
        word_widths[nwords] = _text_w(font, tok);
        nwords++;
        tok = strtok(NULL, " \t");
    }
    if (nwords == 0) return 0;
    if (nwords == 1) return fh;

    int line_start = 0;
    int nlines = 0;
    while (line_start < nwords) {
        float line_words_w = word_widths[line_start];
        int line_end = line_start + 1;
        for (int i = line_start + 1; i < nwords; i++) {
            float test_w = line_words_w + space_w + word_widths[i];
            if (test_w > max_w) break;
            line_words_w = test_w;
            line_end = i + 1;
        }
        nlines++;
        line_start = line_end;
    }
    return (float)nlines * fh;
}

/*! helper: draw a mini vertical scrollbar inside a widget.
 *  content_h = total content height, view_h = visible area height,
 *  scroll_y = current scroll offset. Draws at the right edge of the area
 *  [area_x .. area_x+area_w], [area_y .. area_y+view_h]. */
static void _draw_widget_vscrollbar(float area_x, float area_y, float area_w, float view_h, float content_h, float scroll_y, N_GUI_STYLE* style) {
    float sb_size = style->scrollbar_size;
    float sb_x = area_x + area_w - sb_size;

    /* track */
    al_draw_filled_rectangle(sb_x, area_y, sb_x + sb_size, area_y + view_h,
                             style->scrollbar_track_color);

    /* thumb */
    float ratio = view_h / content_h;
    if (ratio > 1.0f) ratio = 1.0f;
    float thumb_h = ratio * view_h;
    if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
    float max_scroll = content_h - view_h;
    float pos_ratio = (max_scroll > 0) ? scroll_y / max_scroll : 0;
    float thumb_y = area_y + pos_ratio * (view_h - thumb_h);
    al_draw_filled_rounded_rectangle(sb_x + style->scrollbar_thumb_padding, thumb_y,
                                     sb_x + sb_size - style->scrollbar_thumb_padding,
                                     thumb_y + thumb_h,
                                     style->scrollbar_thumb_corner_r, style->scrollbar_thumb_corner_r,
                                     style->scrollbar_thumb_color);
}

/*! helper: compute minimum line thickness so it stays >= 1 physical pixel.
 *  Extracts the scale factor from the current Allegro transform using the
 *  lengths of the basis vectors so that rotation and non-uniform scaling are
 *  handled correctly (m[0][0] alone can be near 0 for a 90-degree rotation). */
static float _min_thickness(float requested) {
    const ALLEGRO_TRANSFORM* tf = al_get_current_transform();
    /* length of each axis basis vector gives the true scale for that axis */
    float sx = hypotf(tf->m[0][0], tf->m[0][1]);
    float sy = hypotf(tf->m[1][0], tf->m[1][1]);
    /* use the larger component to avoid overly thick lines on the minor axis */
    float scale = (sx > sy) ? sx : sy;
    if (scale < 0.01f) scale = 0.01f;
    /* ensure the line is at least 1 physical pixel */
    float min_t = 1.0f / scale;
    return (requested > min_t) ? requested : min_t;
}

/*! helper: pick colour based on widget state */
static ALLEGRO_COLOR _bg_for_state(const N_GUI_THEME* t, int state) {
    if (state & N_GUI_STATE_ACTIVE) return t->bg_active;
    if (state & N_GUI_STATE_HOVER) return t->bg_hover;
    return t->bg_normal;
}

static ALLEGRO_COLOR _border_for_state(const N_GUI_THEME* t, int state) {
    if (state & N_GUI_STATE_ACTIVE) return t->border_active;
    if (state & N_GUI_STATE_HOVER) return t->border_hover;
    return t->border_normal;
}

static ALLEGRO_COLOR _text_for_state(const N_GUI_THEME* t, int state) {
    if (state & N_GUI_STATE_ACTIVE) return t->text_active;
    if (state & N_GUI_STATE_HOVER) return t->text_hover;
    return t->text_normal;
}

/*! draw a themed rectangle or rounded rectangle.
 *  For thin outlines, inset the border by half the thickness so that
 *  all four corners render symmetrically (avoids Allegro pixel-alignment
 *  artifact on the bottom-right corner). */
static void _draw_themed_rect(N_GUI_THEME* t, int state, float x, float y, float w, float h, int rounded) {
    ALLEGRO_COLOR bg = _bg_for_state(t, state);
    ALLEGRO_COLOR bd = _border_for_state(t, state);
    float thickness = _min_thickness(t->border_thickness);
    float ht = thickness * 0.5f;
    if (rounded) {
        al_draw_filled_rounded_rectangle(x + ht, y + ht, x + w - ht, y + h - ht, t->corner_rx, t->corner_ry, bg);
        al_draw_rounded_rectangle(x + ht, y + ht, x + w - ht, y + h - ht, t->corner_rx, t->corner_ry, bd, thickness);
    } else {
        al_draw_filled_rectangle(x, y, x + w, y + h, bg);
        al_draw_rectangle(x + ht, y + ht, x + w - ht, y + h - ht, bd, thickness);
    }
}

/*! resolve whether a shape-aware widget draws rounded: the global shape_mode
 *  override (style->shape_mode) wins when set to ROUNDED or RECT, otherwise the
 *  widget's own shape decides. */
static int _shape_rounded(const N_GUI_STYLE* style, int widget_shape) {
    if (style->shape_mode == N_GUI_SHAPE_ROUNDED) return 1;
    if (style->shape_mode == N_GUI_SHAPE_RECT) return 0;
    return (widget_shape == N_GUI_SHAPE_ROUNDED) ? 1 : 0;
}

/*! draw a button widget */
/*! draw an N_GUI_GLYPH_* icon centred on (cx, cy), sized to a half-extent of s.
 *  fg is the button's text colour and bg its current background, so a glyph
 *  built from overlapping shapes (COPY) can mask what it covers. */
static void _draw_glyph(int glyph, float cx, float cy, float s, ALLEGRO_COLOR fg, ALLEGRO_COLOR bg, float thick) {
    float hw = s;        /* triangle half-width */
    float hh = s * 0.8f; /* triangle half-height */
    /* Stroke glyphs are drawn heavy enough to read as icons next to the filled
       arrows: a hairline plus or cross beside a solid triangle looks broken.
       The style thickness stays the floor, so a theme can still ask for more. */
    float stroke = s * 0.42f;
    if (stroke < thick) stroke = thick;
    thick = stroke;
    switch (glyph) {
        case N_GUI_GLYPH_ARROW_UP:
            al_draw_filled_triangle(cx, cy - hh, cx - hw, cy + hh, cx + hw, cy + hh, fg);
            break;
        case N_GUI_GLYPH_ARROW_DOWN:
            al_draw_filled_triangle(cx, cy + hh, cx - hw, cy - hh, cx + hw, cy - hh, fg);
            break;
        case N_GUI_GLYPH_ARROW_LEFT:
            al_draw_filled_triangle(cx - hh, cy, cx + hh, cy - hw, cx + hh, cy + hw, fg);
            break;
        case N_GUI_GLYPH_ARROW_RIGHT:
            al_draw_filled_triangle(cx + hh, cy, cx - hh, cy - hw, cx - hh, cy + hw, fg);
            break;
        case N_GUI_GLYPH_PLUS:
            al_draw_line(cx - s, cy, cx + s, cy, fg, thick);
            al_draw_line(cx, cy - s, cx, cy + s, fg, thick);
            break;
        case N_GUI_GLYPH_MINUS:
            al_draw_line(cx - s, cy, cx + s, cy, fg, thick);
            break;
        case N_GUI_GLYPH_CROSS:
            al_draw_line(cx - s, cy - s, cx + s, cy + s, fg, thick);
            al_draw_line(cx + s, cy - s, cx - s, cy + s, fg, thick);
            break;
        case N_GUI_GLYPH_COPY: {
            /* two offset sheets: the back one peeks out top-left, the front one
               is filled with the background so the overlap reads as depth */
            float off = s * 0.45f;
            al_draw_rectangle(cx - s, cy - s, cx + s - off * 2.0f, cy + s - off * 2.0f, fg, thick);
            al_draw_filled_rectangle(cx - s + off * 2.0f, cy - s + off * 2.0f, cx + s, cy + s, bg);
            al_draw_rectangle(cx - s + off * 2.0f, cy - s + off * 2.0f, cx + s, cy + s, fg, thick);
            break;
        }
        default:
            break;
    }
}

static void _draw_button(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, const N_GUI_STYLE* style) {
    N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    /* For toggle buttons, use the active visual state when toggled on */
    int draw_state = wgt->state;
    if (bd->toggle_mode && bd->toggled) {
        draw_state |= N_GUI_STATE_ACTIVE;
    }
    /* Flash the pressed visual when a keybind trigger is still within its
     * visual-feedback window, so a keyboard-activated button looks pressed
     * just like a mouse-clicked one. */
    if (bd->key_press_until > 0.0 && al_get_time() < bd->key_press_until) {
        draw_state |= N_GUI_STATE_ACTIVE;
    }

    if (bd->shape == N_GUI_SHAPE_BITMAP && bd->bitmap) {
        ALLEGRO_BITMAP* bmp = bd->bitmap;
        if ((draw_state & N_GUI_STATE_ACTIVE) && bd->bitmap_active)
            bmp = bd->bitmap_active;
        else if ((draw_state & N_GUI_STATE_HOVER) && bd->bitmap_hover)
            bmp = bd->bitmap_hover;
        al_draw_scaled_bitmap(bmp, 0, 0,
                              (float)al_get_bitmap_width(bmp), (float)al_get_bitmap_height(bmp),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        int rounded = _shape_rounded(style, bd->shape);
        _draw_themed_rect(&wgt->theme, draw_state, ax, ay, wgt->w, wgt->h, rounded);
    }

    if (bd->glyph != N_GUI_GLYPH_NONE) {
        /* a glyph replaces the label: it is drawn from primitives in the
           button's text colour, so it follows the theme and any size */
        float smaller = (wgt->w < wgt->h) ? wgt->w : wgt->h;
        float s = smaller * 0.25f;
        if (s < 2.0f) s = 2.0f;
        _draw_glyph(bd->glyph, ax + wgt->w * 0.5f, ay + wgt->h * 0.5f, s,
                    _text_for_state(&wgt->theme, draw_state),
                    _bg_for_state(&wgt->theme, draw_state),
                    _min_thickness(style->tb_btn_glyph_thickness));
    } else if (font && bd->label[0]) {
        ALLEGRO_COLOR tc = _text_for_state(&wgt->theme, draw_state);
        int bbx = 0, bby = 0, bbw = 0, bbh = 0;
        _text_dims(font, bd->label, &bbx, &bby, &bbw, &bbh);
        float tw = (float)bbw;
        float th = (float)bbh;
        float pad = style->label_padding;
        float max_text_w = wgt->w - pad * 2.0f;
        if (tw > max_text_w) {
            _draw_text_truncated(font, tc, ax + pad, ay + (wgt->h - th) / 2.0f - (float)bby, max_text_w, bd->label);
        } else {
            al_draw_text(font, tc, ax + (wgt->w - tw) / 2.0f - (float)bbx, ay + (wgt->h - th) / 2.0f - (float)bby, 0, bd->label);
        }
    }
}

/*! draw a slider widget (horizontal or vertical) */
static void _draw_slider(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    double range = sd->max_val - sd->min_val;
    double ratio = (range > 0) ? (sd->value - sd->min_val) / range : 0;

    if (sd->orientation == N_GUI_SLIDER_V) {
        /* vertical slider */
        float track_w = style->slider_track_size;
        float track_x = ax + (wgt->w - track_w) / 2.0f;
        if (sd->track_bitmap) {
            al_draw_scaled_bitmap(sd->track_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(sd->track_bitmap), (float)al_get_bitmap_height(sd->track_bitmap),
                                  track_x, ay, track_w, wgt->h, 0);
        } else {
            al_draw_filled_rounded_rectangle(track_x, ay, track_x + track_w, ay + wgt->h, style->slider_track_corner_r, style->slider_track_corner_r, wgt->theme.bg_normal);
            al_draw_rounded_rectangle(track_x, ay, track_x + track_w, ay + wgt->h, style->slider_track_corner_r, style->slider_track_corner_r, wgt->theme.border_normal, _min_thickness(style->slider_track_border_thickness));
        }

        /* fill from bottom up */
        float fill_h = (float)(ratio * (double)wgt->h);
        if (sd->fill_bitmap) {
            al_draw_scaled_bitmap(sd->fill_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(sd->fill_bitmap), (float)al_get_bitmap_height(sd->fill_bitmap),
                                  track_x, ay + wgt->h - fill_h, track_w, fill_h, 0);
        } else {
            al_draw_filled_rounded_rectangle(track_x, ay + wgt->h - fill_h, track_x + track_w, ay + wgt->h, style->slider_track_corner_r, style->slider_track_corner_r, wgt->theme.bg_active);
        }

        /* handle */
        float hy = ay + wgt->h - fill_h;
        float handle_r = wgt->w / 2.0f - style->slider_handle_edge_offset;
        if (handle_r < style->slider_handle_min_r) handle_r = style->slider_handle_min_r;
        ALLEGRO_BITMAP* hbmp = _select_state_bitmap(wgt->state, sd->handle_bitmap, sd->handle_hover_bitmap, sd->handle_active_bitmap);
        if (hbmp) {
            float hd = handle_r * 2.0f;
            al_draw_scaled_bitmap(hbmp, 0, 0,
                                  (float)al_get_bitmap_width(hbmp), (float)al_get_bitmap_height(hbmp),
                                  ax + wgt->w / 2.0f - handle_r, hy - handle_r, hd, hd, 0);
        } else {
            ALLEGRO_COLOR hc = _bg_for_state(&wgt->theme, wgt->state);
            ALLEGRO_COLOR hb = _border_for_state(&wgt->theme, wgt->state);
            al_draw_filled_circle(ax + wgt->w / 2.0f, hy, handle_r, hc);
            al_draw_circle(ax + wgt->w / 2.0f, hy, handle_r, hb, _min_thickness(style->slider_handle_border_thickness));
        }

        /* value label, respects value_visible + value_format */
        if (font && sd->value_visible) {
            char val_str[32];
            if (sd->value_format[0]) {
                /* caller-supplied printf format stored in value_format, intentional non-literal */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
                snprintf(val_str, sizeof(val_str), sd->value_format, sd->value);
#pragma GCC diagnostic pop
            } else if (sd->mode == N_GUI_SLIDER_PERCENT) {
                snprintf(val_str, sizeof(val_str), "%.0f%%", sd->value);
            } else {
                snprintf(val_str, sizeof(val_str), "%.1f", sd->value);
            }
            int vbbx = 0, vbby = 0, vbbw = 0, vbbh = 0;
            _text_dims(font, val_str, &vbbx, &vbby, &vbbw, &vbbh);
            al_draw_text(font, wgt->theme.text_normal, ax + (wgt->w - (float)vbbw) / 2.0f - (float)vbbx, ay + wgt->h + (float)style->slider_value_label_offset, 0, val_str);
        }
        return;
    }

    /* horizontal slider (original) */

    /* track */
    float track_h = style->slider_track_size;
    float track_y = ay + (wgt->h - track_h) / 2.0f;
    if (sd->track_bitmap) {
        al_draw_scaled_bitmap(sd->track_bitmap, 0, 0,
                              (float)al_get_bitmap_width(sd->track_bitmap), (float)al_get_bitmap_height(sd->track_bitmap),
                              ax, track_y, wgt->w, track_h, 0);
    } else {
        al_draw_filled_rounded_rectangle(ax, track_y, ax + wgt->w, track_y + track_h, style->slider_track_corner_r, style->slider_track_corner_r, wgt->theme.bg_normal);
        al_draw_rounded_rectangle(ax, track_y, ax + wgt->w, track_y + track_h, style->slider_track_corner_r, style->slider_track_corner_r, wgt->theme.border_normal, _min_thickness(style->slider_track_border_thickness));
    }

    /* fill */
    float fill_w = (float)(ratio * (double)wgt->w);
    if (sd->fill_bitmap) {
        al_draw_scaled_bitmap(sd->fill_bitmap, 0, 0,
                              (float)al_get_bitmap_width(sd->fill_bitmap), (float)al_get_bitmap_height(sd->fill_bitmap),
                              ax, track_y, fill_w, track_h, 0);
    } else {
        al_draw_filled_rounded_rectangle(ax, track_y, ax + fill_w, track_y + track_h, style->slider_track_corner_r, style->slider_track_corner_r, wgt->theme.bg_active);
    }

    /* handle */
    float hx = ax + fill_w;
    float handle_r = wgt->h / 2.0f - style->slider_handle_edge_offset;
    if (handle_r < style->slider_handle_min_r) handle_r = style->slider_handle_min_r;
    ALLEGRO_BITMAP* hbmp = _select_state_bitmap(wgt->state, sd->handle_bitmap, sd->handle_hover_bitmap, sd->handle_active_bitmap);
    if (hbmp) {
        float hd = handle_r * 2.0f;
        al_draw_scaled_bitmap(hbmp, 0, 0,
                              (float)al_get_bitmap_width(hbmp), (float)al_get_bitmap_height(hbmp),
                              hx - handle_r, ay + wgt->h / 2.0f - handle_r, hd, hd, 0);
    } else {
        ALLEGRO_COLOR hc = _bg_for_state(&wgt->theme, wgt->state);
        ALLEGRO_COLOR hb = _border_for_state(&wgt->theme, wgt->state);
        al_draw_filled_circle(hx, ay + wgt->h / 2.0f, handle_r, hc);
        al_draw_circle(hx, ay + wgt->h / 2.0f, handle_r, hb, _min_thickness(style->slider_handle_border_thickness));
    }

    /* value label, respects value_visible + value_format */
    if (font && sd->value_visible) {
        char val_str[32];
        if (sd->value_format[0]) {
            /* caller-supplied printf format stored in value_format, intentional non-literal */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
            snprintf(val_str, sizeof(val_str), sd->value_format, sd->value);
#pragma GCC diagnostic pop
        } else if (sd->mode == N_GUI_SLIDER_PERCENT) {
            snprintf(val_str, sizeof(val_str), "%.0f%%", sd->value);
        } else {
            snprintf(val_str, sizeof(val_str), "%.1f", sd->value);
        }
        int sbbx = 0, sbby = 0, sbbw = 0, sbbh = 0;
        _text_dims(font, val_str, &sbbx, &sbby, &sbbw, &sbbh);
        float th = (float)sbbh;
        al_draw_text(font, wgt->theme.text_normal, ax + wgt->w + style->slider_value_label_offset, ay + (wgt->h - th) / 2.0f - (float)sbby, 0, val_str);
    }
}

/*! helper: find the byte position in a textarea closest to the given mouse
 *  coordinates (mx, my in screen space). ax, ay are the widget's screen-space
 *  top-left corner. Returns the byte offset into td->text. */
static size_t _textarea_pos_from_mouse(const N_GUI_TEXTAREA_DATA* td, ALLEGRO_FONT* font, float mx, float my, float ax, float ay, float widget_w, float widget_h, float pad, float scrollbar_size) {
    if (!font || td->text_len == 0) return 0;
    float fh = (float)al_get_font_line_height(font);

    if (!td->multiline) {
        float click_x = mx - (ax + pad) + td->scroll_x;
        if (click_x < 0) click_x = 0;
        /* build display text for width calculation (masked if mask_char set) */
        char display[N_GUI_TEXT_MAX];
        if (td->mask_char) {
            size_t n = td->text_len;
            if (n >= N_GUI_TEXT_MAX) n = N_GUI_TEXT_MAX - 1;
            memset(display, td->mask_char, n);
            display[n] = '\0';
        } else {
            memcpy(display, td->text, td->text_len);
            display[td->text_len] = '\0';
        }
        size_t best = 0;
        float best_dist = click_x;
        char ctmp[N_GUI_TEXT_MAX];
        for (size_t ci = 0; ci < td->text_len;) {
            int clen = _utf8_char_len((unsigned char)td->text[ci]);
            if (ci + (size_t)clen > td->text_len) clen = (int)(td->text_len - ci);
            size_t pos = ci + (size_t)clen;
            memcpy(ctmp, display, pos);
            ctmp[pos] = '\0';
            float tw = _text_w(font, ctmp);
            float dist = click_x - tw;
            if (dist < 0) dist = -dist;
            if (dist < best_dist) {
                best_dist = dist;
                best = pos;
            } else {
                break;
            }
            ci += (size_t)clen;
        }
        return best;
    }

    /* multiline */
    float view_h = widget_h - pad * 2;
    float content_h = _textarea_content_height(td, font, widget_w, pad);
    float sb_size = (content_h > view_h) ? scrollbar_size : 0.0f;
    float text_area_w = widget_w - sb_size;
    float avail_w = text_area_w - pad * 2;
    float click_x_rel = mx - (ax + pad);
    float click_y_content = my - (ay + pad) + (float)td->scroll_y;
    if (click_x_rel < 0) click_x_rel = 0;
    if (click_y_content < 0) click_y_content = 0;
    int target_line = (int)(click_y_content / fh);

    float cur_cx = 0;
    int cur_line = 0;
    size_t best_pos = 0;
    float best_dist = click_x_rel;
    int found_line = (target_line == 0) ? 1 : 0;

    for (size_t ci = 0; ci < td->text_len;) {
        if (cur_line > target_line) break;
        if (td->text[ci] == '\n') {
            if (cur_line == target_line) break;
            cur_cx = 0;
            cur_line++;
            if (cur_line == target_line) {
                found_line = 1;
                best_pos = ci + 1;
                best_dist = click_x_rel;
            }
            ci++;
            continue;
        }
        int clen = _utf8_char_len((unsigned char)td->text[ci]);
        if (ci + (size_t)clen > td->text_len) clen = (int)(td->text_len - ci);
        char ch2[5];
        memcpy(ch2, &td->text[ci], (size_t)clen);
        ch2[clen] = '\0';
        float cw = _text_w(font, ch2);
        if (cur_cx + cw > avail_w) {
            if (cur_line == target_line) break;
            cur_cx = 0;
            cur_line++;
            if (cur_line == target_line) {
                found_line = 1;
                best_pos = ci;
                best_dist = click_x_rel;
            }
        }
        if (cur_line == target_line) {
            float dist = click_x_rel - (cur_cx + cw);
            if (dist < 0) dist = -dist;
            if (dist < best_dist) {
                best_dist = dist;
                best_pos = ci + (size_t)clen;
            }
        }
        cur_cx += cw;
        ci += (size_t)clen;
    }
    if (!found_line) best_pos = td->text_len;
    return best_pos;
}

/*! return 1 if the textarea has an active selection */
static int _textarea_has_selection(const N_GUI_TEXTAREA_DATA* td) {
    return td->sel_start != td->sel_end;
}

/*! get the ordered selection range (lo, hi) */
static void _textarea_sel_range(const N_GUI_TEXTAREA_DATA* td, size_t* lo, size_t* hi) {
    if (td->sel_start <= td->sel_end) {
        *lo = td->sel_start;
        *hi = td->sel_end;
    } else {
        *lo = td->sel_end;
        *hi = td->sel_start;
    }
}

/*! draw a textarea's placeholder/hint at (tx,ty), dimmed, while the field is
 *  empty; a no-op once any text has been entered or when no placeholder is set */
static void _draw_textarea_placeholder(const N_GUI_TEXTAREA_DATA* td, const N_GUI_WIDGET* wgt, ALLEGRO_FONT* font, float tx, float ty) {
    float tr, tg, tb, br, bg, bb;
    ALLEGRO_COLOR dim;
    if (!font || !td->placeholder || !td->placeholder[0] || td->text_len != 0)
        return;
    al_unmap_rgb_f(wgt->theme.text_normal, &tr, &tg, &tb);
    al_unmap_rgb_f(wgt->theme.bg_normal, &br, &bg, &bb);
    dim = al_map_rgb_f((tr + br) * 0.5f, (tg + bg) * 0.5f, (tb + bb) * 0.5f);
    al_draw_text(font, dim, tx, ty, 0, td->placeholder);
}

/*! draw a textarea widget */
static void _draw_textarea(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    int focused = (wgt->state & N_GUI_STATE_FOCUSED) ? 1 : 0;
    int hovered = (wgt->state & N_GUI_STATE_HOVER) ? 1 : 0;

    /* background, focused wins over hover wins over normal */
    if (td->bg_bitmap) {
        al_draw_scaled_bitmap(td->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(td->bg_bitmap), (float)al_get_bitmap_height(td->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        ALLEGRO_COLOR bg = wgt->theme.bg_normal;
        if (focused)
            bg = wgt->theme.bg_active;
        else if (hovered)
            bg = wgt->theme.bg_hover;
        al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, bg);
    }
    ALLEGRO_COLOR bd = wgt->theme.border_normal;
    if (focused)
        bd = wgt->theme.border_active;
    else if (hovered)
        bd = wgt->theme.border_hover;
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, bd, _min_thickness(wgt->theme.border_thickness));

    if (!font) return;
    float fh = (float)al_get_font_line_height(font);
    float pad = style->textarea_padding;

    /* cursor blink: visible during first half of blink period */
    int cursor_visible = 0;
    if (focused) {
        double now = al_get_time();
        double elapsed = now - td->cursor_time;
        float period = style->textarea_cursor_blink_period;
        if (period <= 0.0f) period = 1.0f;
        double phase = elapsed - (double)(int)(elapsed / (double)period) * (double)period;
        cursor_visible = (phase < (double)period * 0.5) ? 1 : 0;
    }
    float cursor_w = _min_thickness(style->textarea_cursor_width);

    if (td->multiline) {
        float view_h = wgt->h - pad * 2;
        /* two-pass: first check if scrollbar is needed at full width,
         * then recompute content_h at the narrower width if so */
        float content_h = _textarea_content_height(td, font, wgt->w, pad);
        int need_scrollbar = (content_h > view_h) ? 1 : 0;
        float sb_size = need_scrollbar ? style->scrollbar_size : 0;
        float text_area_w = wgt->w - sb_size;
        if (need_scrollbar) {
            content_h = _textarea_content_height(td, font, text_area_w, pad);
        }

        /* auto-scroll to keep cursor visible (skip if user just scrolled with mouse wheel) */
        if (focused && !td->scroll_from_wheel) {
            /* compute cursor Y position within content */
            float cur_cx = 0;
            float cur_cy = 0;
            for (size_t i = 0; i < td->cursor_pos && i < td->text_len;) {
                if (td->text[i] == '\n') {
                    cur_cx = 0;
                    cur_cy += fh;
                    i++;
                    continue;
                }
                int clen = _utf8_char_len((unsigned char)td->text[i]);
                if (i + (size_t)clen > td->text_len) clen = (int)(td->text_len - i);
                char ch[5];
                memcpy(ch, &td->text[i], (size_t)clen);
                ch[clen] = '\0';
                float cw2 = _text_w(font, ch);
                if (cur_cx + cw2 > text_area_w - pad * 2) {
                    cur_cx = 0;
                    cur_cy += fh;
                }
                cur_cx += cw2;
                i += (size_t)clen;
            }
            /* ensure cursor line is within visible region */
            if (cur_cy < (float)td->scroll_y) {
                td->scroll_y = (int)cur_cy;
            }
            if (cur_cy + fh > (float)td->scroll_y + view_h) {
                td->scroll_y = (int)(cur_cy + fh - view_h);
            }
        }

        /* clamp scroll_y */
        if (need_scrollbar) {
            float max_sy = content_h - view_h;
            if (max_sy < 0) max_sy = 0;
            if ((float)td->scroll_y > max_sy) td->scroll_y = (int)max_sy;
            if (td->scroll_y < 0) td->scroll_y = 0;
        } else {
            td->scroll_y = 0;
        }

        /* clip to widget bounds (text area only, leaving room for scrollbar) */
        int pcx, pcy, pcw, pch;
        al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
        _set_clipping_rect_transformed((int)ax, (int)ay, (int)text_area_w, (int)wgt->h);
        /* selection range */
        size_t sel_lo = 0, sel_hi = 0;
        int has_sel = _textarea_has_selection(td);
        if (has_sel) _textarea_sel_range(td, &sel_lo, &sel_hi);
        ALLEGRO_COLOR sel_bg = wgt->theme.selection_color;

        /* simple line-wrap drawing with UTF-8 support */
        float cx = ax + pad;
        float cy = ay + pad - (float)td->scroll_y;
        /* cursor position tracked during layout */
        float cursor_cx = cx;
        float cursor_cy = cy;
        _draw_textarea_placeholder(td, wgt, font, cx, cy);
        /* Consecutive drawable characters on one visual line share a colour and
           a baseline, so they are accumulated into a run buffer and emitted with
           a single al_draw_text instead of one call per glyph. The layout walk
           (per-character wrap, cursor tracking, selection rects) is unchanged;
           only the text emission is batched. A run is flushed at a wrap, a
           newline, when the buffer fills, or at the end. Selection rects stay
           per-character and are drawn during the walk, i.e. before the run they
           belong to is flushed, so the text still lands on top of them. */
        char run[256];
        int run_len = 0;
        float run_x = cx, run_y = cy;
        int run_vis = 0;
        for (size_t i = 0; i < td->text_len;) {
            if (i == td->cursor_pos) {
                cursor_cx = cx;
                cursor_cy = cy;
            }
            if (td->text[i] == '\n') {
                if (run_len > 0) {
                    if (run_vis) {
                        run[run_len] = '\0';
                        al_draw_text(font, wgt->theme.text_normal, run_x, run_y, 0, run);
                    }
                    run_len = 0;
                }
                /* draw selection highlight on newline (small rect at end of line) */
                if (has_sel && i >= sel_lo && i < sel_hi && cy + fh > ay && cy < ay + wgt->h) {
                    float space_w = _text_w(font, " ");
                    al_draw_filled_rectangle(cx, cy, cx + space_w, cy + fh, sel_bg);
                }
                cx = ax + pad;
                cy += fh;
                i++;
                continue;
            }
            int clen = _utf8_char_len((unsigned char)td->text[i]);
            if (i + (size_t)clen > td->text_len) clen = (int)(td->text_len - i);
            char ch[5];
            memcpy(ch, &td->text[i], (size_t)clen);
            ch[clen] = '\0';
            float cw = _text_w(font, ch);
            if (cx + cw > ax + text_area_w - pad) {
                /* wrap: the run so far ends on the current line */
                if (run_len > 0) {
                    if (run_vis) {
                        run[run_len] = '\0';
                        al_draw_text(font, wgt->theme.text_normal, run_x, run_y, 0, run);
                    }
                    run_len = 0;
                }
                cx = ax + pad;
                cy += fh;
                if (i == td->cursor_pos) {
                    cursor_cx = cx;
                    cursor_cy = cy;
                }
            }
            int vis = (cy + fh > ay && cy < ay + wgt->h);
            if (vis) {
                /* draw selection highlight behind character */
                if (has_sel && i >= sel_lo && i < sel_hi) {
                    al_draw_filled_rectangle(cx, cy, cx + cw, cy + fh, sel_bg);
                }
                /* start a run here if none is open */
                if (run_len == 0) {
                    run_x = cx;
                    run_y = cy;
                    run_vis = 1;
                }
                /* flush and restart if the buffer would overflow */
                if (run_len + clen >= (int)sizeof(run) - 1) {
                    run[run_len] = '\0';
                    al_draw_text(font, wgt->theme.text_normal, run_x, run_y, 0, run);
                    run_len = 0;
                    run_x = cx;
                    run_y = cy;
                }
                memcpy(run + run_len, ch, (size_t)clen);
                run_len += clen;
            }
            cx += cw;
            /* check cursor positions within the multi-byte character */
            for (int b = 1; b < clen; b++) {
                if (i + (size_t)b == td->cursor_pos) {
                    cursor_cx = cx;
                    cursor_cy = cy;
                }
            }
            i += (size_t)clen;
        }
        /* flush the trailing run */
        if (run_len > 0 && run_vis) {
            run[run_len] = '\0';
            al_draw_text(font, wgt->theme.text_normal, run_x, run_y, 0, run);
        }
        /* cursor is after all text when cursor_pos >= text_len */
        if (td->cursor_pos >= td->text_len) {
            cursor_cx = cx;
            cursor_cy = cy;
        }
        /* cursor */
        if (cursor_visible) {
            al_draw_filled_rectangle(cursor_cx, cursor_cy, cursor_cx + cursor_w, cursor_cy + fh, wgt->theme.text_active);
        }
        al_set_clipping_rectangle(pcx, pcy, pcw, pch);

        /* draw scrollbar outside clipping region */
        if (need_scrollbar) {
            _draw_widget_vscrollbar(ax, ay + pad, wgt->w, view_h, content_h, (float)td->scroll_y, style);
        }
    } else {
        /* single line with horizontal scrolling */
        float ty = ay + (wgt->h - fh) / 2.0f;
        float inner_w = wgt->w - pad * 2;

        /* build display text (masked if mask_char is set) */
        char display_text[N_GUI_TEXT_MAX];
        if (td->mask_char && td->text_len > 0) {
            size_t n = td->text_len;
            if (n >= N_GUI_TEXT_MAX) n = N_GUI_TEXT_MAX - 1;
            memset(display_text, td->mask_char, n);
            display_text[n] = '\0';
        } else {
            memcpy(display_text, td->text, td->text_len);
            display_text[td->text_len] = '\0';
        }

        /* compute cursor pixel offset from text start */
        char tmp[N_GUI_TEXT_MAX];
        size_t cpos = td->cursor_pos;
        if (cpos > td->text_len) cpos = td->text_len;
        memcpy(tmp, display_text, cpos);
        tmp[cpos] = '\0';
        float cursor_px = _text_w(font, tmp);

        /* adjust scroll_x so cursor stays visible within the inner area */
        if (cursor_px - td->scroll_x < 0) {
            td->scroll_x = cursor_px;
        }
        if (cursor_px - td->scroll_x > inner_w - cursor_w) {
            td->scroll_x = cursor_px - inner_w + cursor_w;
        }
        if (td->scroll_x < 0) td->scroll_x = 0;

        /* clip text to widget inner bounds */
        int pcx, pcy, pcw, pch;
        al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
        _set_clipping_rect_transformed((int)(ax + pad), (int)ay, (int)inner_w, (int)wgt->h);
        /* draw selection highlight for single-line */
        if (_textarea_has_selection(td)) {
            size_t sl, sh;
            _textarea_sel_range(td, &sl, &sh);
            char stmp[N_GUI_TEXT_MAX];
            memcpy(stmp, display_text, sl);
            stmp[sl] = '\0';
            float sel_x1 = _text_w(font, stmp);
            memcpy(stmp, display_text, sh);
            stmp[sh] = '\0';
            float sel_x2 = _text_w(font, stmp);
            float sx1 = ax + pad + sel_x1 - td->scroll_x;
            float sx2 = ax + pad + sel_x2 - td->scroll_x;
            al_draw_filled_rectangle(sx1, ty, sx2, ty + fh, wgt->theme.selection_color);
        }
        _draw_textarea_placeholder(td, wgt, font, ax + pad, ty);
        al_draw_text(font, wgt->theme.text_normal, ax + pad - td->scroll_x, ty, 0, display_text);
        /* cursor */
        if (cursor_visible) {
            float cx = ax + pad + cursor_px - td->scroll_x;
            al_draw_filled_rectangle(cx, ty, cx + cursor_w, ty + fh, wgt->theme.text_active);
        }
        al_set_clipping_rectangle(pcx, pcy, pcw, pch);
    }
}

/*! draw a checkbox widget */
static void _draw_checkbox(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_CHECKBOX_DATA* cd = (N_GUI_CHECKBOX_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    float box_size = wgt->h < style->checkbox_max_size ? wgt->h : style->checkbox_max_size;
    float box_y = ay + (wgt->h - box_size) / 2.0f;

    /* select bitmap: hover > checked/unchecked > color theme */
    ALLEGRO_BITMAP* box_bmp = NULL;
    if ((wgt->state & N_GUI_STATE_HOVER) && cd->box_hover_bitmap) {
        box_bmp = cd->box_hover_bitmap;
    } else if (cd->checked && cd->box_checked_bitmap) {
        box_bmp = cd->box_checked_bitmap;
    } else if (cd->box_bitmap) {
        box_bmp = cd->box_bitmap;
    }

    if (box_bmp) {
        al_draw_scaled_bitmap(box_bmp, 0, 0,
                              (float)al_get_bitmap_width(box_bmp), (float)al_get_bitmap_height(box_bmp),
                              ax, box_y, box_size, box_size, 0);
    } else {
        _draw_themed_rect(&wgt->theme, wgt->state, ax, box_y, box_size, box_size, 0);

        if (cd->checked) {
            /* draw checkmark as two lines */
            ALLEGRO_COLOR tc = wgt->theme.text_active;
            float m = style->checkbox_mark_margin;
            al_draw_line(ax + m, box_y + box_size / 2.0f,
                         ax + box_size / 2.0f, box_y + box_size - m, tc, _min_thickness(style->checkbox_mark_thickness));
            al_draw_line(ax + box_size / 2.0f, box_y + box_size - m,
                         ax + box_size - m, box_y + m, tc, _min_thickness(style->checkbox_mark_thickness));
        }
    }

    if (font && cd->label[0]) {
        int cbbx = 0, cbby = 0, cbbw = 0, cbbh = 0;
        _text_dims(font, cd->label, &cbbx, &cbby, &cbbw, &cbbh);
        float fh = (float)cbbh;
        float label_max_w = wgt->w - box_size - style->checkbox_label_gap;
        _draw_text_truncated(font, _text_for_state(&wgt->theme, wgt->state),
                             ax + box_size + style->checkbox_label_offset, ay + (wgt->h - fh) / 2.0f - (float)cbby, label_max_w, cd->label);
    }
}

/*! draw a scrollbar widget */
static void _draw_scrollbar(N_GUI_WIDGET* wgt, float ox, float oy, const N_GUI_STYLE* style) {
    N_GUI_SCROLLBAR_DATA* sb = (N_GUI_SCROLLBAR_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    int rounded = (sb->shape == N_GUI_SHAPE_ROUNDED) ? 1 : 0;

    /* track */
    if (sb->track_bitmap) {
        al_draw_scaled_bitmap(sb->track_bitmap, 0, 0,
                              (float)al_get_bitmap_width(sb->track_bitmap), (float)al_get_bitmap_height(sb->track_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        _draw_themed_rect(&wgt->theme, N_GUI_STATE_IDLE, ax, ay, wgt->w, wgt->h, rounded);
    }

    /* compute thumb */
    double ratio = sb->viewport_size / sb->content_size;
    if (ratio > 1.0) ratio = 1.0;
    double max_scroll = sb->content_size - sb->viewport_size;
    if (max_scroll < 0) max_scroll = 0;
    double pos_ratio = (max_scroll > 0) ? sb->scroll_pos / max_scroll : 0;

    float thumb_x, thumb_y, thumb_w, thumb_h;
    if (sb->orientation == N_GUI_SCROLLBAR_V) {
        thumb_h = (float)(ratio * (double)wgt->h);
        if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
        thumb_w = wgt->w - style->scrollbar_thumb_padding * 2;
        thumb_x = ax + style->scrollbar_thumb_padding;
        float track_range = wgt->h - thumb_h;
        thumb_y = ay + (float)(pos_ratio * (double)track_range);
    } else {
        thumb_w = (float)(ratio * (double)wgt->w);
        if (thumb_w < style->scrollbar_thumb_min) thumb_w = style->scrollbar_thumb_min;
        thumb_h = wgt->h - style->scrollbar_thumb_padding * 2;
        thumb_y = ay + style->scrollbar_thumb_padding;
        float track_range = wgt->w - thumb_w;
        thumb_x = ax + (float)(pos_ratio * (double)track_range);
    }

    ALLEGRO_BITMAP* tbmp = _select_state_bitmap(wgt->state, sb->thumb_bitmap, sb->thumb_hover_bitmap, sb->thumb_active_bitmap);
    if (tbmp) {
        al_draw_scaled_bitmap(tbmp, 0, 0,
                              (float)al_get_bitmap_width(tbmp), (float)al_get_bitmap_height(tbmp),
                              thumb_x, thumb_y, thumb_w, thumb_h, 0);
    } else {
        _draw_themed_rect(&wgt->theme, wgt->state, thumb_x, thumb_y, thumb_w, thumb_h, rounded);
    }
}

/*! draw a row-indexed vertical scrollbar (shared by hexview, syntaxview and
 *  datagrid) at the right edge of [ax,ay,w,h]. nb_rows total, visible per page,
 *  scroll_offset is the first visible row. */
static void _draw_rows_scrollbar(float ax, float ay, float w, float h, int nb_rows, int visible, int scroll_offset, const N_GUI_STYLE* style, N_GUI_THEME* theme) {
    float sb_w = style->scrollbar_size;
    float sb_x = ax + w - sb_w;
    int max_off = nb_rows - visible;
    float ratio, thumb_h, track_range, pos_ratio, thumb_y;
    if (max_off < 0) max_off = 0;
    /* use the dedicated scrollbar colours (like _draw_widget_vscrollbar) so the
       track and thumb stay visible regardless of the widget bg/hover theme */
    al_draw_filled_rectangle(sb_x, ay, ax + w, ay + h, style->scrollbar_track_color);
    al_draw_line(sb_x, ay, sb_x, ay + h, theme->border_normal, 1.0f);
    ratio = (nb_rows > 0) ? (float)visible / (float)nb_rows : 1.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    thumb_h = ratio * h;
    if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
    track_range = h - thumb_h;
    pos_ratio = (max_off > 0) ? (float)scroll_offset / (float)max_off : 0.0f;
    thumb_y = ay + pos_ratio * track_range;
    al_draw_filled_rounded_rectangle(sb_x + style->scrollbar_thumb_padding, thumb_y, ax + w - style->scrollbar_thumb_padding, thumb_y + thumb_h, style->scrollbar_thumb_corner_r, style->scrollbar_thumb_corner_r, style->scrollbar_thumb_color);
}

/*! draw a horizontal scrollbar along the bottom edge of a widget: @p track_w is the width
 *  of the scrollable pane (from @p ax), @p bar_y its top, @p content_w the total content
 *  width, @p h_scroll the current pixel offset. Mirrors _draw_rows_scrollbar but pixel-based. */
static void _draw_cols_scrollbar(float ax, float bar_y, float track_w, float content_w, float h_scroll, const N_GUI_STYLE* style, N_GUI_THEME* theme) {
    float sb_h = style->scrollbar_size;
    float max_off = content_w - track_w;
    float ratio, thumb_w, track_range, pos_ratio, thumb_x;
    if (max_off < 0) max_off = 0;
    al_draw_filled_rectangle(ax, bar_y, ax + track_w, bar_y + sb_h, style->scrollbar_track_color);
    al_draw_line(ax, bar_y, ax + track_w, bar_y, theme->border_normal, 1.0f);
    ratio = (content_w > 0) ? track_w / content_w : 1.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    thumb_w = ratio * track_w;
    if (thumb_w < style->scrollbar_thumb_min) thumb_w = style->scrollbar_thumb_min;
    track_range = track_w - thumb_w;
    pos_ratio = (max_off > 0) ? h_scroll / max_off : 0.0f;
    thumb_x = ax + pos_ratio * track_range;
    al_draw_filled_rounded_rectangle(thumb_x + style->scrollbar_thumb_padding, bar_y + style->scrollbar_thumb_padding, thumb_x + thumb_w - style->scrollbar_thumb_padding, bar_y + sb_h - style->scrollbar_thumb_padding, style->scrollbar_thumb_corner_r, style->scrollbar_thumb_corner_r, style->scrollbar_thumb_color);
}

/*! Compute the datagrid horizontal-scroll metrics used identically by the draw path and the
 *  mouse handlers, so the thumb geometry never drifts between them: @p content_w = left pad +
 *  the total width of the visible columns; @p pane_w = the viewport width available to the
 *  columns (widget width minus a vertical scrollbar when the rows overflow); @p need_hsb = 1
 *  when the content is wider than the pane. Any output pointer may be NULL. */
static void _datagrid_hmetrics(const N_GUI_WIDGET* wgt, const N_GUI_DATAGRID_DATA* gd, ALLEGRO_FONT* font, const N_GUI_STYLE* style, float* content_w, float* pane_w, int* need_hsb) {
    float fh = font ? (float)al_get_font_line_height(font) : 16.0f;
    float pad = style->item_text_padding;
    float row_h = fh + style->item_height_pad;
    float data_h = wgt->h - row_h; /* minus the header row */
    int visible = (int)(data_h / row_h);
    int need_sb;
    float cw = pad, pw;
    size_t c;
    if (visible < 1) visible = 1;
    need_sb = ((int)gd->nb_rows > visible) ? 1 : 0;
    for (c = 0; c < gd->nb_cols; c++) {
        size_t pc = _datagrid_phys(gd, c);
        if (gd->cols[pc].visible) cw += gd->cols[pc].width;
    }
    pw = wgt->w - (need_sb ? style->scrollbar_size : 0.0f);
    if (content_w) *content_w = cw;
    if (pane_w) *pane_w = pw;
    if (need_hsb) *need_hsb = (cw > pw + 1.0f) ? 1 : 0;
}

/*! draw a read-only hex viewer: offset, hex bytes, and an ASCII gutter */
static void _draw_hexview(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_HEXVIEW_DATA* hd = (N_GUI_HEXVIEW_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;
    float fh = font ? (float)al_get_font_line_height(font) : 16.0f;
    float pad = style->textarea_padding;
    float row_h = fh + 2.0f;
    int bpr = hd->bytes_per_row > 0 ? hd->bytes_per_row : 16;
    int nb_rows = (int)((hd->len + (size_t)bpr - 1) / (size_t)bpr);
    int visible = (int)((wgt->h - pad * 2.0f) / row_h);
    int need_sb, max_off, i, pcx, pcy, pcw, pch;
    float sb_w;

    al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.bg_normal);
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, _min_thickness(wgt->theme.border_thickness));
    if (visible < 1) visible = 1;
    need_sb = (nb_rows > visible) ? 1 : 0;
    sb_w = need_sb ? style->scrollbar_size : 0.0f;
    max_off = nb_rows - visible;
    if (max_off < 0) max_off = 0;
    if (hd->scroll_offset > max_off) hd->scroll_offset = max_off;
    if (hd->scroll_offset < 0) hd->scroll_offset = 0;

    al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
    al_set_clipping_rectangle((int)ax, (int)ay, (int)(wgt->w - sb_w), (int)wgt->h);
    for (i = 0; i < visible; i++) {
        int row = hd->scroll_offset + i;
        size_t base = (size_t)row * (size_t)bpr;
        char line[256];
        char asc[32];
        int n = 0, j, a = 0;
        float ty = ay + pad + (float)i * row_h;
        if (base >= hd->len) break;
        n += snprintf(line + n, sizeof(line) - (size_t)n, "%08lX  ", (unsigned long)base);
        for (j = 0; j < bpr; j++) {
            if (base + (size_t)j < hd->len) {
                unsigned char c = hd->data[base + (size_t)j];
                n += snprintf(line + n, sizeof(line) - (size_t)n, "%02X ", c);
                asc[a++] = (c >= 32 && c < 127) ? (char)c : '.';
            } else {
                n += snprintf(line + n, sizeof(line) - (size_t)n, "   ");
            }
        }
        asc[a] = '\0';
        snprintf(line + n, sizeof(line) - (size_t)n, " |%s|", asc);
        al_draw_text(font, wgt->theme.text_normal, ax + pad, ty, 0, line);
    }
    al_set_clipping_rectangle(pcx, pcy, pcw, pch);
    if (need_sb)
        _draw_rows_scrollbar(ax, ay, wgt->w, wgt->h, nb_rows, visible, hd->scroll_offset, style, &wgt->theme);
}

/*! count the lines in a NUL-terminated text (0 for empty) */
static int _text_line_count(const char* s) {
    int n = 0;
    if (!s || !s[0]) return 0;
    n = 1;
    while (*s) {
        if (*s == '\n') n++;
        s++;
    }
    return n;
}

/*! draw a run of text and advance the pen x by its width. Runs longer than the
 *  stack buffer are drawn in successive chunks rather than cut short, which a
 *  whole-line mode (plain text, a diff line) would otherwise do to any long
 *  line. Chunks never split a UTF-8 sequence. */
/*! the horizontal window one syntax line is drawn into, plus the pen carried
 *  between its coloured runs. A run that ends left of x_min is measured but not
 *  drawn, and the run that takes the pen past x_max ends the line. Without that
 *  a single very long line costs one al_draw_text plus one uncached measurement
 *  per 511 bytes on every frame, nearly all of it off screen: measured on a
 *  1 MB line, 151 ms per frame before, 0.5 ms after. */
typedef struct SYNTAX_RUN {
    /*! font every run of the line is drawn with */
    ALLEGRO_FONT* font;
    /*! pen x, advanced by the width of each run */
    float cx;
    /*! pen y, constant for the whole line */
    float y;
    /*! left edge of the visible area */
    float x_min;
    /*! right edge of the visible area */
    float x_max;
    /*! 1 once the pen passed x_max, so the tokenizer stops walking the line */
    int done;
} SYNTAX_RUN;

/*! draw one coloured run of a syntax line and advance the pen past it. Runs
 *  outside the window are measured but not drawn (the pen still has to land in
 *  the right place), and the run that crosses the right edge sets done. */
static void _syntax_run(SYNTAX_RUN* r, ALLEGRO_COLOR col, const char* s, int len) {
    char buf[512];
    const int chunk_max = (int)sizeof(buf) - 1;
    if (!r || r->done || len <= 0) return;
    while (len > 0) {
        int take = (len > chunk_max) ? chunk_max : len;
        float w;
        if (len > chunk_max) {
            /* back off to a lead byte so a multi-byte glyph is not cut in two */
            while (take > 0 && ((unsigned char)s[take] & 0xC0) == 0x80) take--;
            if (take == 0) take = chunk_max;
        }
        memcpy(buf, s, (size_t)take);
        buf[take] = '\0';
        w = _text_w(r->font, buf);
        if (r->cx + w >= r->x_min && r->cx <= r->x_max)
            al_draw_text(r->font, col, r->cx, r->y, 0, buf);
        r->cx += w;
        if (r->cx > r->x_max) {
            r->done = 1;
            return;
        }
        s += take;
        len -= take;
    }
}

/*! whether the n bytes at s are a keyword after which an expression (and
 *  therefore a regex literal) can start, e.g. return /re/ */
static int _syntax_js_regex_kw(const char* s, int n) {
    static const char* kw[] = {"await", "case", "delete", "do", "else", "in",
                               "instanceof", "new", "of", "return", "throw",
                               "typeof", "void", "yield", NULL};
    for (size_t it = 0; kw[it]; it++) {
        if ((int)strlen(kw[it]) == n && strncmp(s, kw[it], (size_t)n) == 0) return 1;
    }
    return 0;
}

/*! whether a '/' may start a regex literal after last significant char c
 *  (start of line, after an operator, an opener, or a separator) */
static int _syntax_js_regex_possible(char c) {
    if (c == 0) return 1;
    return strchr("(,=:[!&|?;{}<>+-*%~^", c) != NULL;
}

/*! whether the n bytes at s form a JavaScript keyword or literal name */
static int _syntax_js_keyword(const char* s, int n) {
    static const char* kw[] = {"async", "await", "break", "case", "catch", "class", "const",
                               "continue", "debugger", "default", "delete", "do", "else",
                               "export", "extends", "false", "finally", "for", "function",
                               "if", "import", "in", "instanceof", "let", "new", "null",
                               "of", "return", "static", "super", "switch", "this", "throw",
                               "true", "try", "typeof", "undefined", "var", "void", "while",
                               "with", "yield", NULL};
    for (size_t it = 0; kw[it]; it++) {
        if ((int)strlen(kw[it]) == n && strncmp(s, kw[it], (size_t)n) == 0) return 1;
    }
    return 0;
}

/*! unified diff line colours. Added and removed carry meaning the way an HTTP
 *  status colour does, so they must not shift with the theme; they are picked
 *  mid-range to stay legible on light and dark backgrounds alike. */
static ALLEGRO_COLOR _syntax_diff_add(void) {
    return al_map_rgb(64, 160, 72);
}
/*! see _syntax_diff_add: colour of a removed line */
static ALLEGRO_COLOR _syntax_diff_del(void) {
    return al_map_rgb(200, 72, 72);
}
/*! see _syntax_diff_add: colour of a hunk header line */
static ALLEGRO_COLOR _syntax_diff_hunk(void) {
    return al_map_rgb(96, 148, 200);
}

/*! draw one text line with mode-specific highlighting starting at (x,y), inside
 *  the horizontal window [x_min,x_max] (see SYNTAX_RUN) */
static void _syntax_draw_line(ALLEGRO_FONT* font, N_GUI_THEME* th, float x, float y, const char* s, int n, int mode, int line_idx, int in_headers, float x_min, float x_max) {
    SYNTAX_RUN r;
    r.font = font;
    r.cx = x;
    r.y = y;
    r.x_min = x_min;
    r.x_max = x_max;
    r.done = 0;
    if (mode == N_GUI_SYNTAX_DIFF) {
        /* the marker is the first byte of the line, so one look decides the
           colour of the whole line; anything else is context */
        if (n >= 2 && s[0] == '@' && s[1] == '@') {
            _syntax_run(&r, _syntax_diff_hunk(), s, n);
        } else if (n >= 1 && s[0] == '+') {
            _syntax_run(&r, _syntax_diff_add(), s, n);
        } else if (n >= 1 && s[0] == '-') {
            _syntax_run(&r, _syntax_diff_del(), s, n);
        } else {
            _syntax_run(&r, th->text_normal, s, n);
        }
        return;
    }
    if (mode == N_GUI_SYNTAX_HTTP) {
        if (line_idx == 0) {
            _syntax_run(&r, th->text_active, s, n);
            return;
        }
        if (in_headers && n > 0) {
            int c = 0;
            while (c < n && s[c] != ':') c++;
            if (c > 0 && c < n) {
                _syntax_run(&r, th->border_active, s, c);
                _syntax_run(&r, th->text_normal, s + c, n - c);
                return;
            }
        }
        _syntax_run(&r, th->text_normal, s, n);
        return;
    }
    if (mode == N_GUI_SYNTAX_JSON) {
        int i = 0;
        while (i < n && !r.done) {
            char c = s[i];
            if (c == '"') {
                int j = i + 1;
                while (j < n && s[j] != '"') {
                    if (s[j] == '\\' && j + 1 < n) j++;
                    j++;
                }
                if (j < n) j++;
                _syntax_run(&r, th->text_active, s + i, j - i);
                i = j;
            } else if ((c >= '0' && c <= '9') || (c == '-' && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9')) {
                int j = i + 1;
                while (j < n && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || s[j] == '+' || s[j] == '-')) j++;
                _syntax_run(&r, th->border_active, s + i, j - i);
                i = j;
            } else if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
                _syntax_run(&r, th->border_hover, s + i, 1);
                i++;
            } else {
                int j = i + 1;
                while (j < n && !(s[j] == '"' || (s[j] >= '0' && s[j] <= '9') || s[j] == '{' || s[j] == '}' || s[j] == '[' || s[j] == ']' || s[j] == ':' || s[j] == ',')) j++;
                _syntax_run(&r, th->text_normal, s + i, j - i);
                i = j;
            }
        }
        return;
    }
    if (mode == N_GUI_SYNTAX_XML) {
        int i = 0;
        while (i < n && !r.done) {
            if (s[i] == '<') {
                if (i + 4 <= n && memcmp(s + i, "<!--", 4) == 0) {
                    /* comment: color until --> or the end of the line */
                    int j = i + 4;
                    while (j + 3 <= n && memcmp(s + j, "-->", 3) != 0) j++;
                    j = (j + 3 <= n) ? j + 3 : n;
                    _syntax_run(&r, th->border_hover, s + i, j - i);
                    i = j;
                    continue;
                }
                /* '<' plus optional '/', '!' or '?' */
                int j = i + 1;
                while (j < n && (s[j] == '/' || s[j] == '!' || s[j] == '?')) j++;
                _syntax_run(&r, th->border_hover, s + i, j - i);
                i = j;
                /* element name (j continues from the delimiter run) */
                while (j < n && (isalnum((unsigned char)s[j]) || s[j] == ':' || s[j] == '-' || s[j] == '_')) j++;
                if (j > i) _syntax_run(&r, th->border_active, s + i, j - i);
                i = j;
                /* attributes until '>' (quote aware) */
                while (i < n && s[i] != '>' && !r.done) {
                    char q = s[i];
                    if (q == '"' || q == '\'') {
                        j = i + 1;
                        while (j < n && s[j] != q) j++;
                        if (j < n) j++;
                        _syntax_run(&r, th->text_active, s + i, j - i);
                        i = j;
                    } else {
                        j = i + 1;
                        while (j < n && s[j] != '>' && s[j] != '"' && s[j] != '\'') j++;
                        _syntax_run(&r, th->text_normal, s + i, j - i);
                        i = j;
                    }
                }
                if (i < n) {
                    _syntax_run(&r, th->border_hover, s + i, 1);
                    i++;
                }
            } else {
                int j = i + 1;
                while (j < n && s[j] != '<') j++;
                _syntax_run(&r, th->text_normal, s + i, j - i);
                i = j;
            }
        }
        return;
    }
    if (mode == N_GUI_SYNTAX_YAML) {
        int i = 0;
        /* leading indentation, then '- ' list markers */
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i > 0) _syntax_run(&r, th->text_normal, s, i);
        while (i < n && s[i] == '-' && (i + 1 >= n || s[i + 1] == ' ')) {
            _syntax_run(&r, th->border_hover, s + i, 1);
            i++;
            if (i < n) {
                _syntax_run(&r, th->text_normal, s + i, 1);
                i++;
            }
        }
        if (i < n && s[i] == '#') {
            _syntax_run(&r, th->border_hover, s + i, n - i);
            return;
        }
        /* key: up to the first ':' followed by a space or the line end, outside quotes */
        {
            int key_end = -1;
            char q = 0;
            for (int j = i; j < n; j++) {
                if (q) {
                    if (s[j] == q) q = 0;
                    continue;
                }
                if (s[j] == '"' || s[j] == '\'') {
                    q = s[j];
                    continue;
                }
                if (s[j] == '#') break;
                if (s[j] == ':' && (j + 1 >= n || s[j + 1] == ' ')) {
                    key_end = j;
                    break;
                }
            }
            if (key_end >= 0) {
                _syntax_run(&r, th->border_active, s + i, key_end - i);
                _syntax_run(&r, th->border_hover, s + key_end, 1);
                i = key_end + 1;
            }
        }
        /* value: quoted runs, a trailing comment, plain text */
        while (i < n && !r.done) {
            char c = s[i];
            if (c == '"' || c == '\'') {
                int j = i + 1;
                while (j < n && s[j] != c) j++;
                if (j < n) j++;
                _syntax_run(&r, th->text_active, s + i, j - i);
                i = j;
            } else if (c == '#') {
                _syntax_run(&r, th->border_hover, s + i, n - i);
                i = n;
            } else {
                int j = i + 1;
                while (j < n && s[j] != '"' && s[j] != '\'' && s[j] != '#') j++;
                _syntax_run(&r, th->text_normal, s + i, j - i);
                i = j;
            }
        }
        return;
    }
    if (mode == N_GUI_SYNTAX_JS) {
        int i = 0;
        char last_sig = 0;
        int kw_regex = 0;
        while (i < n && !r.done) {
            char c = s[i];
            if (c == '/' && i + 1 < n && s[i + 1] == '/') {
                _syntax_run(&r, th->border_hover, s + i, n - i);
                return;
            }
            if (c == '/' && i + 1 < n && s[i + 1] == '*') {
                int j = i + 2;
                while (j + 2 <= n && memcmp(s + j, "*/", 2) != 0) j++;
                j = (j + 2 <= n) ? j + 2 : n;
                _syntax_run(&r, th->border_hover, s + i, j - i);
                i = j;
            } else if (c == '/' && (kw_regex || _syntax_js_regex_possible(last_sig))) {
                /* regex literal: escapes and character classes honored */
                int j = i + 1;
                int in_class = 0;
                int closed = 0;
                while (j < n) {
                    if (s[j] == '\\' && j + 1 < n) {
                        j += 2;
                        continue;
                    }
                    if (in_class) {
                        if (s[j] == ']') in_class = 0;
                    } else if (s[j] == '[') {
                        in_class = 1;
                    } else if (s[j] == '/') {
                        j++;
                        closed = 1;
                        break;
                    }
                    j++;
                }
                if (closed) {
                    while (j < n && isalpha((unsigned char)s[j])) j++;
                }
                _syntax_run(&r, th->text_active, s + i, j - i);
                last_sig = '/';
                kw_regex = 0;
                i = j;
            } else if (c == '"' || c == '\'' || c == '`') {
                int j = i + 1;
                while (j < n && s[j] != c) {
                    if (s[j] == '\\' && j + 1 < n) j++;
                    j++;
                }
                if (j < n) j++;
                _syntax_run(&r, th->text_active, s + i, j - i);
                last_sig = c;
                kw_regex = 0;
                i = j;
            } else if (c >= '0' && c <= '9') {
                int j = i + 1;
                while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '.')) j++;
                _syntax_run(&r, th->border_active, s + i, j - i);
                last_sig = s[j - 1];
                kw_regex = 0;
                i = j;
            } else if (isalpha((unsigned char)c) || c == '_' || c == '$') {
                int j = i + 1;
                while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '$')) j++;
                _syntax_run(&r, _syntax_js_keyword(s + i, j - i) ? th->border_active : th->text_normal, s + i, j - i);
                last_sig = s[j - 1];
                kw_regex = _syntax_js_regex_kw(s + i, j - i);
                i = j;
            } else if (c == '{' || c == '}' || c == '[' || c == ']' || c == '(' || c == ')' || c == ';' || c == ',' || c == ':') {
                _syntax_run(&r, th->border_hover, s + i, 1);
                last_sig = c;
                kw_regex = 0;
                i++;
            } else {
                _syntax_run(&r, th->text_normal, s + i, 1);
                if (c != ' ' && c != '\t') {
                    last_sig = c;
                    kw_regex = 0;
                }
                i++;
            }
        }
        return;
    }
    _syntax_run(&r, th->text_normal, s, n);
}

/*! pixel width of the first @p n bytes of @p s in @p font, of any length: the
 *  measurement needs a NUL-terminated copy, so it accumulates over chunks of the
 *  same size _syntax_run draws in, which keeps the two in step. A single fixed
 *  buffer instead would silently stop measuring at its end, and put a selection
 *  highlight or a click far from the glyphs it belongs to on a long line.
 *  @p limit stops the walk once the width passes it, for callers that only need
 *  to know where something lands on screen (anything past the right edge is
 *  clipped to the same pixel); pass 0 or less to measure the whole prefix. */
static float _syntax_prefix_w_lim(ALLEGRO_FONT* font, const char* s, int n, float limit) {
    char buf[512];
    const int chunk_max = (int)sizeof(buf) - 1;
    float w = 0.0f;
    if (!font || !s || n <= 0) return 0.0f;
    while (n > 0) {
        if (limit > 0.0f && w > limit) break;
        int take = (n > chunk_max) ? chunk_max : n;
        if (n > chunk_max) {
            /* back off to a lead byte so a multi-byte glyph is not cut in two */
            while (take > 0 && ((unsigned char)s[take] & 0xC0) == 0x80) take--;
            if (take == 0) take = chunk_max;
        }
        memcpy(buf, s, (size_t)take);
        buf[take] = '\0';
        w += _text_w(font, buf);
        s += take;
        n -= take;
    }
    return w;
}

/*! exact pixel width of the first n bytes of s, see _syntax_prefix_w_lim */
static float _syntax_prefix_w(ALLEGRO_FONT* font, const char* s, int n) {
    return _syntax_prefix_w_lim(font, s, n, 0.0f);
}

/*! Map a mouse position over a syntax view to a byte offset into its text. The view
 *  does not wrap, so one source line maps to one display row; the row is chosen from
 *  the y position (plus the scroll offset) and the column from the x position by
 *  measuring incremental prefix widths of that line. The offset returned indexes the
 *  real text buffer (a trailing CR is treated as the end of the visible line). */
static int _syntaxview_offset_from_mouse(const N_GUI_SYNTAXVIEW_DATA* yd, ALLEGRO_FONT* font, float mx, float my, float ax, float ay, float pad) {
    if (!yd || !yd->text || !font) return 0;
    float fh = (float)al_get_font_line_height(font);
    float row_h = fh + 2.0f;
    int row = (int)((my - ay - pad) / row_h);
    if (row < 0) row = 0;
    int target_line = yd->scroll_offset + row;
    const char* p = yd->text;
    int ln = 0;
    while (ln < target_line && *p) {
        const char* nl = strchr(p, '\n');
        if (!nl) {
            p += strlen(p);
            break;
        }
        p = nl + 1;
        ln++;
    }
    size_t line_start = (size_t)(p - yd->text);
    const char* nl = strchr(p, '\n');
    int dlen = nl ? (int)(nl - p) : (int)strlen(p);
    if (dlen > 0 && p[dlen - 1] == '\r') dlen--;
    float target = mx - (ax + pad) + yd->h_scroll;
    if (target <= 0.0f) return (int)line_start;
    /* Walk in chunks first, then column by column inside the one chunk that
       holds the target. Measuring every prefix from the line start instead is
       quadratic in the line length, which a very long line turns into a visible
       stall on a single click. */
    {
        const int chunk_max = 511;
        int base = 0, col;
        float base_w = 0.0f, prevw;
        while (base + chunk_max < dlen) {
            int take = chunk_max;
            float cw;
            while (take > 0 && ((unsigned char)p[base + take] & 0xC0) == 0x80) take--;
            if (take == 0) take = chunk_max;
            cw = _syntax_prefix_w(font, p + base, take);
            if (base_w + cw >= target) break;
            base_w += cw;
            base += take;
        }
        prevw = base_w;
        for (col = 1; base + col <= dlen; col++) {
            float ww = base_w + _syntax_prefix_w(font, p + base, col);
            if (ww >= target)
                return (int)line_start + base + ((target - prevw < ww - target) ? (col - 1) : col);
            prevw = ww;
        }
    }
    return (int)line_start + dlen;
}

/*! drop any active syntax-view text selection (used when focus moves to another
 *  selectable widget, so only one selection is live at a time). */
static void _clear_syntaxview_selection(N_GUI_CTX* ctx) {
    if (!ctx || ctx->selected_syntaxview_id < 0) return;
    N_GUI_WIDGET* pv = n_gui_get_widget(ctx, ctx->selected_syntaxview_id);
    if (pv && pv->type == N_GUI_TYPE_SYNTAXVIEW && pv->data) {
        N_GUI_SYNTAXVIEW_DATA* pyd = (N_GUI_SYNTAXVIEW_DATA*)pv->data;
        pyd->sel_start = -1;
        pyd->sel_end = -1;
        pyd->sel_dragging = 0;
    }
    ctx->selected_syntaxview_id = -1;
}

/*! draw a read-only syntax-highlighted text view */
/*! (re)build a syntaxview's cached line count and first-blank-line index.
 *  Both are pure functions of the text, so they are computed once per text
 *  change instead of scanning the whole buffer twice on every frame (and on
 *  every scroll/click event). Invalidated by clearing lines_valid in
 *  n_gui_syntaxview_set_text. */
static void _syntaxview_ensure_lines(N_GUI_SYNTAXVIEW_DATA* yd) {
    if (!yd || yd->lines_valid) return;
    int nb = 0;
    int headers_end = 0x7fffffff;
    const char* s = yd->text;
    if (s && s[0]) {
        /* line count: 1 + number of '\n' (matches _text_line_count) */
        nb = 1;
        for (const char* t = s; *t; t++)
            if (*t == '\n') nb++;
        /* first blank line: HTTP headers end there */
        const char* q = s;
        int ln = 0;
        while (*q) {
            const char* nl = strchr(q, '\n');
            int len = nl ? (int)(nl - q) : (int)strlen(q);
            if (len == 0 || (len == 1 && q[0] == '\r')) {
                headers_end = ln;
                break;
            }
            ln++;
            if (!nl) break;
            q = nl + 1;
        }
    }
    yd->cached_nb_lines = nb;
    yd->cached_headers_end = headers_end;
    yd->lines_valid = 1;
}

/*! (re)build a syntax view's cached content width: the pixel width of its widest
 *  line, plus the padding on both sides. Measured once per text (and once per
 *  font change), not once per frame the way the listbox can afford to: a
 *  reformatted document runs to tens of thousands of lines. */
static void _syntaxview_ensure_width(N_GUI_SYNTAXVIEW_DATA* yd, ALLEGRO_FONT* font, const N_GUI_STYLE* style) {
    const char* p;
    float maxw = 0.0f;
    if (!yd) return;
    if (yd->width_valid && yd->metrics_font == font) return;
    p = yd->text;
    while (p && *p) {
        const char* nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        float w;
        if (len > 0 && p[len - 1] == '\r') len--;
        w = _syntax_prefix_w(font, p, len);
        if (w > maxw) maxw = w;
        if (!nl) break;
        p = nl + 1;
    }
    yd->cached_content_w = (maxw > 0.0f) ? maxw + style->textarea_padding * 2.0f : 0.0f;
    yd->metrics_font = font;
    yd->width_valid = 1;
}

/*! Syntax view horizontal-scroll metrics, computed identically by the draw path
 *  and the mouse handlers so the thumb and the hit test never drift apart.
 *  @p content_w is the width the widest line needs, @p pane_w the room left for
 *  the text (widget width minus a vertical scrollbar), @p need_hsb whether the
 *  content overflows that room, and @p visible the number of lines that fit
 *  above the horizontal scrollbar. Any output pointer may be NULL. */
static void _syntaxview_hmetrics(const N_GUI_WIDGET* wgt, N_GUI_SYNTAXVIEW_DATA* yd, ALLEGRO_FONT* font, const N_GUI_STYLE* style, float* content_w, float* pane_w, int* need_hsb, int* visible) {
    float fh = font ? (float)al_get_font_line_height(font) : 16.0f;
    float row_h = fh + 2.0f;
    float pad = style->textarea_padding;
    float cw, pw;
    int vis, need_vsb, hsb;

    _syntaxview_ensure_lines(yd);
    _syntaxview_ensure_width(yd, font, style);
    cw = yd->cached_content_w;

    if (row_h <= 0.0f) row_h = 1.0f;
    vis = (int)((wgt->h - pad * 2.0f) / row_h);
    if (vis < 1) vis = 1;
    need_vsb = (yd->cached_nb_lines > vis) ? 1 : 0;
    pw = wgt->w - (need_vsb ? style->scrollbar_size : 0.0f);
    hsb = (cw > pw + 1.0f) ? 1 : 0;
    /* a horizontal scrollbar steals a row, which can be what makes the vertical
       one necessary; resolve that here rather than letting the draw path and the
       hit test disagree by a row */
    if (hsb) {
        vis = (int)((wgt->h - style->scrollbar_size - pad * 2.0f) / row_h);
        if (vis < 1) vis = 1;
        if (!need_vsb && yd->cached_nb_lines > vis) {
            pw = wgt->w - style->scrollbar_size;
            hsb = (cw > pw + 1.0f) ? 1 : 0;
        }
    }

    if (content_w) *content_w = cw;
    if (pane_w) *pane_w = pw;
    if (need_hsb) *need_hsb = hsb;
    if (visible) *visible = vis;
}

/*! clamp a syntax view's horizontal scroll to the room its content actually needs */
static void _syntaxview_clamp_h(N_GUI_SYNTAXVIEW_DATA* yd, float content_w, float pane_w) {
    float max_off = content_w - pane_w;
    if (max_off < 0.0f) max_off = 0.0f;
    if (yd->h_scroll > max_off) yd->h_scroll = max_off;
    if (yd->h_scroll < 0.0f) yd->h_scroll = 0.0f;
}

static void _draw_syntaxview(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;
    float fh = font ? (float)al_get_font_line_height(font) : 16.0f;
    float pad = style->textarea_padding;
    float row_h = fh + 2.0f;
    int nb_lines, visible = 1;
    int need_sb, need_hsb = 0, max_off, headers_end, line_no, drawn, pcx, pcy, pcw, pch;
    int has_sel, sel_lo = 0, sel_hi = 0;
    float sb_w, sb_h, content_w = 0.0f, pane_w = wgt->w, text_x;
    const char* p;

    _syntaxview_hmetrics(wgt, yd, font, style, &content_w, &pane_w, &need_hsb, &visible);
    nb_lines = yd->cached_nb_lines;
    _syntaxview_clamp_h(yd, content_w, pane_w);
    text_x = ax + pad - yd->h_scroll;

    al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.bg_normal);
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, _min_thickness(wgt->theme.border_thickness));
    need_sb = (nb_lines > visible) ? 1 : 0;
    sb_w = need_sb ? style->scrollbar_size : 0.0f;
    sb_h = need_hsb ? style->scrollbar_size : 0.0f;
    max_off = nb_lines - visible;
    if (max_off < 0) max_off = 0;
    if (yd->scroll_offset > max_off) yd->scroll_offset = max_off;
    if (yd->scroll_offset < 0) yd->scroll_offset = 0;
    if (!yd->text) return;

    /* first blank line (HTTP headers end there), cached with the line count */
    headers_end = yd->cached_headers_end;

    /* order the selection endpoints once (byte offsets into the text) */
    has_sel = (yd->sel_start >= 0 && yd->sel_end >= 0 && yd->sel_start != yd->sel_end);
    if (has_sel) {
        sel_lo = yd->sel_start < yd->sel_end ? yd->sel_start : yd->sel_end;
        sel_hi = yd->sel_start < yd->sel_end ? yd->sel_end : yd->sel_start;
        if ((size_t)sel_lo > yd->len) sel_lo = (int)yd->len;
        if ((size_t)sel_hi > yd->len) sel_hi = (int)yd->len;
    }

    al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
    al_set_clipping_rectangle((int)ax, (int)ay, (int)(wgt->w - sb_w), (int)(wgt->h - sb_h));
    /* skip to the first visible line */
    p = yd->text;
    line_no = 0;
    while (line_no < yd->scroll_offset && p) {
        const char* nl = strchr(p, '\n');
        if (!nl) {
            p = NULL;
            break;
        }
        p = nl + 1;
        line_no++;
    }
    /* pass 1: selection highlights, drawn as primitives behind the text. Kept
       out of the held text pass below because drawing a primitive while bitmap
       drawing is held is undefined in Allegro. Only walks when a selection
       exists, so the common (no-selection) frame skips it entirely. */
    if (has_sel && p) {
        const char* sp = p;
        /* widths are measured from the line start, so the right edge of the
           view sits at the pan plus the pane width */
        float sel_lim = yd->h_scroll + pane_w;
        for (int sdrawn = 0; sp && *sp && sdrawn < visible; sdrawn++) {
            const char* nl = strchr(sp, '\n');
            int len = nl ? (int)(nl - sp) : (int)strlen(sp);
            float ty = ay + pad + (float)sdrawn * row_h;
            if (len > 0 && sp[len - 1] == '\r') len--;
            int ls = (int)(sp - yd->text);
            int le = ls + len;
            int a = sel_lo > ls ? sel_lo : ls;
            int b = sel_hi < le ? sel_hi : le;
            if (a < b) {
                /* stop measuring past the right edge: the rectangle is clipped
                   there anyway, and a selection over a very long line would
                   otherwise re-measure the whole line on every frame */
                float x0 = text_x + _syntax_prefix_w_lim(font, sp, a - ls, sel_lim);
                float x1 = text_x + _syntax_prefix_w_lim(font, sp, b - ls, sel_lim);
                al_draw_filled_rectangle(x0, ty, x1, ty + row_h, wgt->theme.selection_color);
            }
            if (!nl) break;
            sp = nl + 1;
        }
    }
    /* pass 2: the text runs, batched into one held block. Every visible line's
       coloured runs share the font glyph atlas, so holding collapses them into
       a single flush instead of one draw call per run. */
    al_hold_bitmap_drawing(true);
    for (drawn = 0; p && *p && drawn < visible; drawn++, line_no++) {
        const char* nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        int in_headers = (line_no <= headers_end);
        float ty = ay + pad + (float)drawn * row_h;
        if (len > 0 && p[len - 1] == '\r') len--;
        _syntax_draw_line(font, &wgt->theme, text_x, ty, p, len, yd->mode, line_no, in_headers, ax, ax + pane_w);
        if (!nl) break;
        p = nl + 1;
    }
    al_hold_bitmap_drawing(false);
    al_set_clipping_rectangle(pcx, pcy, pcw, pch);
    if (need_sb)
        _draw_rows_scrollbar(ax, ay, wgt->w, wgt->h - sb_h, nb_lines, visible, yd->scroll_offset, style, &wgt->theme);
    if (need_hsb)
        _draw_cols_scrollbar(ax, ay + wgt->h - sb_h, pane_w, content_w, yd->h_scroll, style, &wgt->theme);
}

/*! draw a sortable data grid: fixed header row plus scrollable data rows */
static void _draw_progressbar(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    const N_GUI_PROGRESSBAR_DATA* pd = (const N_GUI_PROGRESSBAR_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;
    float r = 3.0f;
    float fillw;
    (void)style;
    if (!pd)
        return;
    /* track */
    al_draw_filled_rounded_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, r, r, wgt->theme.bg_normal);
    /* filled portion, proportional to value */
    fillw = pd->value * wgt->w;
    if (fillw > 0.0f) {
        if (fillw < 1.0f)
            fillw = 1.0f;
        al_draw_filled_rounded_rectangle(ax, ay, ax + fillw, ay + wgt->h, r, r, wgt->theme.bg_active);
    }
    /* border */
    al_draw_rounded_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, r, r, wgt->theme.border_normal, 1.0f);
    /* optional centered overlay text */
    if (font && pd->text[0]) {
        float th = (float)al_get_font_line_height(font);
        al_draw_text(font, wgt->theme.text_normal, ax + wgt->w / 2.0f, ay + (wgt->h - th) / 2.0f, ALLEGRO_ALIGN_CENTER, pd->text);
    }
}

static void _draw_datagrid(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;
    float fh = font ? (float)al_get_font_line_height(font) : 16.0f;
    float pad = style->item_text_padding;
    float row_h = fh + style->item_height_pad;
    float header_h = row_h;
    float data_y = ay + header_h;
    float data_h = wgt->h - header_h;
    int visible = (int)(data_h / row_h);
    int nb_rows = (int)gd->nb_rows;
    int need_sb, max_off, i, pcx, pcy, pcw, pch, need_hsb = 0;
    float sb_w, sb_h = 0.0f, cx, content_w = 0.0f, pane_w = 0.0f, maxh;
    size_t c;

    al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.bg_normal);
    if (visible < 1) visible = 1;
    need_sb = (nb_rows > visible) ? 1 : 0;
    sb_w = need_sb ? style->scrollbar_size : 0.0f;
    /* horizontal scroll: when the columns are wider than the pane, reserve a scrollbar along
       the bottom (shrinking the data rows), and clamp the horizontal offset to the content */
    _datagrid_hmetrics(wgt, gd, font, style, &content_w, &pane_w, &need_hsb);
    if (need_hsb) {
        sb_h = style->scrollbar_size;
        data_h -= sb_h;
        visible = (int)(data_h / row_h);
        if (visible < 1) visible = 1;
    }
    maxh = content_w - pane_w;
    if (maxh < 0) maxh = 0;
    if (gd->h_scroll > maxh) gd->h_scroll = maxh;
    if (gd->h_scroll < 0) gd->h_scroll = 0;
    max_off = nb_rows - visible;
    if (max_off < 0) max_off = 0;
    if (gd->scroll_offset > max_off) gd->scroll_offset = max_off;
    if (gd->scroll_offset < 0) gd->scroll_offset = 0;

    /* header */
    al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + header_h, wgt->theme.bg_active);
    al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
    al_set_clipping_rectangle((int)ax, (int)ay, (int)(wgt->w - sb_w), (int)header_h);
    cx = ax + pad - gd->h_scroll;
    al_hold_bitmap_drawing(true);
    for (c = 0; c < gd->nb_cols; c++) {
        size_t pc = _datagrid_phys(gd, c);
        char title[N_GUI_ID_MAX + 4];
        const char* ind = "";
        if (!gd->cols[pc].visible) continue; /* hidden column: no header cell, no gap */
        if ((int)pc == gd->sort_col) ind = gd->sort_dir < 0 ? " v" : " ^";
        snprintf(title, sizeof(title), "%s%s", gd->cols[pc].title, ind);
        _draw_text_truncated(font, wgt->theme.text_active, cx, ay + (header_h - fh) * 0.5f, gd->cols[pc].width - pad, title);
        cx += gd->cols[pc].width;
    }
    al_hold_bitmap_drawing(false);
    al_set_clipping_rectangle(pcx, pcy, pcw, pch);

    /* data rows */
    al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
    al_set_clipping_rectangle((int)ax, (int)data_y, (int)(wgt->w - sb_w), (int)data_h);
    /* pass 1: row backgrounds (tint + selection). All primitives, drawn before
       the held text pass so no primitive is issued while bitmap drawing is held. */
    for (i = 0; i < visible; i++) {
        int row = gd->scroll_offset + i;
        float ry = data_y + (float)i * row_h;
        int sel, tinted;
        if (row >= nb_rows) break;
        sel = (row == gd->selected_row || (gd->multiselect && gd->row_sel && gd->row_sel[row])) ? 1 : 0;
        tinted = (gd->row_has_color && gd->row_color && gd->row_has_color[row]) ? 1 : 0;
        if (tinted) {
            ALLEGRO_COLOR t = gd->row_color[row];
            al_draw_filled_rectangle(ax, ry, ax + wgt->w - sb_w, ry + row_h, t);
        }
        if (sel) {
            if (tinted) {
                /* an opaque selection fill would hide the tint: darken the tinted
                   row and outline it instead, so it reads as both */
                al_draw_filled_rectangle(ax, ry, ax + wgt->w - sb_w, ry + row_h, al_map_rgba_f(0.0f, 0.0f, 0.0f, 0.35f));
                al_draw_rectangle(ax + 1.0f, ry + 1.0f, ax + wgt->w - sb_w - 1.0f, ry + row_h - 1.0f, wgt->theme.border_active, 2.0f);
            } else {
                al_draw_filled_rectangle(ax, ry, ax + wgt->w - sb_w, ry + row_h, wgt->theme.bg_hover);
            }
        }
    }
    /* pass 2: cell text, batched into one held block (all cells share the font
       glyph atlas). The text colour is recomputed here from the same sel/tint
       state used in pass 1. */
    al_hold_bitmap_drawing(true);
    for (i = 0; i < visible; i++) {
        int row = gd->scroll_offset + i;
        float ry = data_y + (float)i * row_h;
        int sel, tinted;
        ALLEGRO_COLOR tint_text = wgt->theme.text_normal;
        if (row >= nb_rows) break;
        sel = (row == gd->selected_row || (gd->multiselect && gd->row_sel && gd->row_sel[row])) ? 1 : 0;
        tinted = (gd->row_has_color && gd->row_color && gd->row_has_color[row]) ? 1 : 0;
        if (tinted) {
            ALLEGRO_COLOR t = gd->row_color[row];
            /* pick the text color from the tint luminance so a light highlight
               (yellow, cyan) keeps dark text and a dark one keeps light text */
            float lum = 0.299f * t.r + 0.587f * t.g + 0.114f * t.b;
            tint_text = (lum > 0.55f) ? al_map_rgb(0, 0, 0) : al_map_rgb(255, 255, 255);
            if (sel) tint_text = al_map_rgb(255, 255, 255);
        }
        cx = ax + pad - gd->h_scroll;
        for (c = 0; c < gd->nb_cols; c++) {
            size_t pc = _datagrid_phys(gd, c);
            const char* cell = gd->cells[(size_t)row * gd->nb_cols + pc];
            ALLEGRO_COLOR col = tinted ? tint_text : (sel ? wgt->theme.text_active : wgt->theme.text_normal);
            if (!gd->cols[pc].visible) continue;
            _draw_text_truncated(font, col, cx, ry + (row_h - fh) * 0.5f, gd->cols[pc].width - pad, cell ? cell : "");
            cx += gd->cols[pc].width;
        }
    }
    al_hold_bitmap_drawing(false);
    al_set_clipping_rectangle(pcx, pcy, pcw, pch);

    /* vertical separators between the visible columns (header top to grid bottom), so
       column boundaries read as a grid and the resizable edges are visible. A
       semi-transparent black overlay reads as a bit darker than whatever is behind it
       (the header cells or a row), so the line never blends into the header background
       the way a border-colored line does. */
    {
        ALLEGRO_COLOR sep = al_map_rgba_f(0.0f, 0.0f, 0.0f, 0.35f);
        float vx = ax - gd->h_scroll;
        size_t vc;
        for (vc = 0; vc < gd->nb_cols; vc++) {
            size_t pc = _datagrid_phys(gd, vc);
            if (!gd->cols[pc].visible) continue;
            vx += gd->cols[pc].width;
            if (vx >= ax + wgt->w - sb_w) break; /* past the right border / scrollbar */
            if (vx <= ax) continue;              /* separator scrolled off the left */
            al_draw_line(vx, ay, vx, ay + wgt->h - sb_h, sep, 1.0f);
        }
    }
    al_draw_line(ax, ay + header_h, ax + wgt->w - sb_w, ay + header_h, wgt->theme.border_normal, 1.0f);
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, _min_thickness(wgt->theme.border_thickness));
    if (need_sb)
        _draw_rows_scrollbar(ax, data_y, wgt->w, data_h, nb_rows, visible, gd->scroll_offset, style, &wgt->theme);
    if (need_hsb)
        _draw_cols_scrollbar(ax, ay + wgt->h - sb_h, pane_w, content_w, gd->h_scroll, style, &wgt->theme);
}

/*! test whether a point falls on a split pane's draggable divider band.
 *  The widget spans the whole region it splits, so this is what tells a press
 *  meant for the divider apart from a press anywhere else over the two panes.
 *  ax/ay are the widget's absolute top-left corner. */
static int _splitpane_on_divider(const N_GUI_WIDGET* wgt, float px, float py, float ax, float ay) {
    const N_GUI_SPLITPANE_DATA* sd;
    float band;
    if (!wgt || wgt->type != N_GUI_TYPE_SPLITPANE || !wgt->data) return 0;
    sd = (const N_GUI_SPLITPANE_DATA*)wgt->data;
    band = sd->divider * 2.0f;
    if (sd->orientation == N_GUI_SPLIT_VERTICAL) {
        float dcx = ax + sd->ratio * wgt->w;
        return (px >= dcx - band && px <= dcx + band);
    }
    {
        float dcy = ay + sd->ratio * wgt->h;
        return (py >= dcy - band && py <= dcy + band);
    }
}

/*! draw a split pane: only the divider bar is painted (the app fills the two
 *  regions with its own widgets, positioned from the divider ratio) */
static void _draw_splitpane(N_GUI_WIDGET* wgt, float ox, float oy) {
    const N_GUI_SPLITPANE_DATA* sd = (const N_GUI_SPLITPANE_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    int hovered = (wgt->state & (N_GUI_STATE_HOVER | N_GUI_STATE_ACTIVE)) ? 1 : 0;
    ALLEGRO_COLOR bar = hovered ? wgt->theme.border_active : wgt->theme.border_normal;
    float th = sd->divider;
    if (sd->orientation == N_GUI_SPLIT_VERTICAL) {
        float dx = ax + sd->ratio * wgt->w - th * 0.5f;
        float gx = dx + th * 0.5f;
        al_draw_filled_rectangle(dx, ay, dx + th, ay + wgt->h, wgt->theme.bg_normal);
        al_draw_line(gx, ay + wgt->h * 0.5f - th * 1.5f, gx, ay + wgt->h * 0.5f + th * 1.5f, bar, 2.0f);
    } else {
        float dy = ay + sd->ratio * wgt->h - th * 0.5f;
        float gy = dy + th * 0.5f;
        al_draw_filled_rectangle(ax, dy, ax + wgt->w, dy + th, wgt->theme.bg_normal);
        al_draw_line(ax + wgt->w * 0.5f - th * 1.5f, gy, ax + wgt->w * 0.5f + th * 1.5f, gy, bar, 2.0f);
    }
}

/*! Listbox horizontal-scroll metrics, computed identically by the draw path and
 *  the mouse handlers so the thumb and the hit test never drift apart.
 *  @p content_w is the width the widest item needs (text padding included),
 *  @p pane_w the room left for the items (widget width minus a vertical
 *  scrollbar), @p need_hsb whether the content overflows that room, and
 *  @p visible the number of rows that fit above the horizontal scrollbar.
 *  Any output pointer may be NULL. */
static void _listbox_hmetrics(const N_GUI_WIDGET* wgt, const N_GUI_LISTBOX_DATA* ld, ALLEGRO_FONT* font, const N_GUI_STYLE* style, float* content_w, float* pane_w, int* need_hsb, int* visible) {
    float fh = font ? (float)al_get_font_line_height(font) : 16.0f;
    float ih = ld->item_height > fh ? ld->item_height : fh + style->item_height_pad;
    float cw = 0.0f, pw;
    int vis, need_vsb, hsb;
    size_t i;

    if (ih <= 0.0f) ih = 1.0f;
    for (i = 0; i < ld->nb_items; i++) {
        float tw = font ? _text_w(font, ld->items[i].text) : 0.0f;
        if (tw > cw) cw = tw;
    }
    if (cw > 0.0f) cw += style->item_text_padding * 2.0f;

    /* A horizontal scrollbar steals a row, which can be what makes the vertical
       one necessary; resolve that in one extra pass rather than letting the draw
       path and the hit test disagree by a row. */
    vis = (int)(wgt->h / ih);
    need_vsb = ((int)ld->nb_items > vis) ? 1 : 0;
    pw = wgt->w - (need_vsb ? style->scrollbar_size : 0.0f);
    hsb = (cw > pw + 1.0f) ? 1 : 0;
    if (hsb) {
        vis = (int)((wgt->h - style->scrollbar_size) / ih);
        if (!need_vsb && (int)ld->nb_items > vis) {
            pw = wgt->w - style->scrollbar_size;
            hsb = (cw > pw + 1.0f) ? 1 : 0;
        }
    }
    if (vis < 0) vis = 0;

    if (content_w) *content_w = cw;
    if (pane_w) *pane_w = pw;
    if (need_hsb) *need_hsb = hsb;
    if (visible) *visible = vis;
}

/*! clamp a listbox's horizontal scroll to the room its content actually needs */
static void _listbox_clamp_h(N_GUI_LISTBOX_DATA* ld, float content_w, float pane_w) {
    float max_off = content_w - pane_w;
    if (max_off < 0.0f) max_off = 0.0f;
    if (ld->h_scroll > max_off) ld->h_scroll = max_off;
    if (ld->h_scroll < 0.0f) ld->h_scroll = 0.0f;
}

/*! draw a listbox widget */
static void _draw_listbox(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    /* background */
    if (ld->bg_bitmap) {
        al_draw_scaled_bitmap(ld->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(ld->bg_bitmap), (float)al_get_bitmap_height(ld->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.bg_normal);
    }
    /* the frame is drawn LAST (see below), not here: when wgt->h is an exact multiple
       of the row height the last row's selection / hover fill reaches ay + wgt->h and
       would paint over the bottom border if the frame were drawn first. */

    if (!font) {
        /* no font: nothing else draws, so frame now and return */
        al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, wgt->theme.border_thickness);
        return;
    }
    float fh = (float)al_get_font_line_height(font);
    float ih = ld->item_height > fh ? ld->item_height : fh + style->item_height_pad;
    /* Canonicalise: a listbox created with a too-small item_height
       (or with the style default of 20 when the active font is
       taller) would otherwise draw rows at `ih` but have every hit-
       test / hover-row / DnD helper read the raw `ld->item_height`,
       producing off-by-N row mismatches. Write the effective value
       back so all consumers agree on one pixel height. */
    ld->item_height = ih;
    float pad = style->item_text_padding;

    /* Horizontal overflow: when the widest item does not fit, the rows scroll
       sideways under a horizontal scrollbar instead of being cut short with an
       ellipsis, which would otherwise hide the tail of a long path or URL for
       good. */
    float content_w = 0.0f, pane_w = wgt->w;
    int need_hsb = 0;
    int visible_count = 0;
    _listbox_hmetrics(wgt, ld, font, style, &content_w, &pane_w, &need_hsb, &visible_count);
    _listbox_clamp_h(ld, content_w, pane_w);
    float hsb_h = need_hsb ? style->scrollbar_size : 0.0f;
    float rows_h = wgt->h - hsb_h;

    /* clamp scroll_offset to valid range */
    int max_off = (int)ld->nb_items - visible_count;
    if (max_off < 0) max_off = 0;
    if (ld->scroll_offset > max_off) ld->scroll_offset = max_off;
    if (ld->scroll_offset < 0) ld->scroll_offset = 0;

    int need_scrollbar = ((int)ld->nb_items > visible_count) ? 1 : 0;
    float sb_w = need_scrollbar ? style->scrollbar_size : 0;
    float item_area_w = wgt->w - sb_w;

    /* pass 1: item backgrounds (selection / hover tint / skin bitmaps), all
       primitives and non-font bitmaps, drawn before the held text pass */
    for (int i = 0; i < visible_count && (size_t)(i + ld->scroll_offset) < ld->nb_items; i++) {
        int idx = i + ld->scroll_offset;
        float iy = ay + (float)i * ih;
        const N_GUI_LISTITEM* item = &ld->items[idx];

        int row_hovered = (ld->hover_row == idx) ? 1 : 0;
        if (item->selected && ld->item_selected_bitmap) {
            al_draw_scaled_bitmap(ld->item_selected_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(ld->item_selected_bitmap), (float)al_get_bitmap_height(ld->item_selected_bitmap),
                                  ax + style->item_selection_inset, iy, item_area_w - style->item_selection_inset * 2, ih, 0);
        } else if (item->selected) {
            al_draw_filled_rectangle(ax + style->item_selection_inset, iy, ax + item_area_w - style->item_selection_inset, iy + ih, wgt->theme.bg_active);
        } else if (row_hovered) {
            /* Hover tint on the row under the cursor (only when the
               row isn't selected, selection wins). */
            al_draw_filled_rectangle(ax + style->item_selection_inset, iy,
                                     ax + item_area_w - style->item_selection_inset, iy + ih,
                                     wgt->theme.bg_hover);
        } else if (ld->item_bg_bitmap) {
            al_draw_scaled_bitmap(ld->item_bg_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(ld->item_bg_bitmap), (float)al_get_bitmap_height(ld->item_bg_bitmap),
                                  ax + style->item_selection_inset, iy, item_area_w - style->item_selection_inset * 2, ih, 0);
        }
    }
    /* pass 2: item labels, batched into one held block (shared font atlas).
       While the list scrolls horizontally the text is drawn in full at the
       scrolled offset and the clipping rectangle cuts it; the ellipsis
       truncation only applies when there is no horizontal scrollbar to reach
       the rest with. */
    {
        float item_max_w = item_area_w - pad * 2.0f;
        int pcx, pcy, pcw, pch;
        if (need_hsb) {
            al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
            al_set_clipping_rectangle((int)ax, (int)ay, (int)item_area_w, (int)rows_h);
        }
        al_hold_bitmap_drawing(true);
        for (int i = 0; i < visible_count && (size_t)(i + ld->scroll_offset) < ld->nb_items; i++) {
            int idx = i + ld->scroll_offset;
            float iy = ay + (float)i * ih;
            const N_GUI_LISTITEM* item = &ld->items[idx];
            ALLEGRO_COLOR tc = item->selected ? wgt->theme.text_active : wgt->theme.text_normal;
            if (need_hsb) {
                al_draw_text(font, tc, ax + pad - ld->h_scroll, iy + (ih - fh) / 2.0f, 0, item->text);
            } else {
                _draw_text_truncated(font, tc, ax + pad, iy + (ih - fh) / 2.0f, item_max_w, item->text);
            }
        }
        al_hold_bitmap_drawing(false);
        if (need_hsb) al_set_clipping_rectangle(pcx, pcy, pcw, pch);
    }

    /* draw scrollbar indicator when items overflow */
    if (need_scrollbar) {
        float sb_x = ax + wgt->w - sb_w;
        /* the vertical track stops above the horizontal one so the two do not
           overlap in the corner */
        float sb_h = rows_h;
        /* scrollbar track */
        al_draw_filled_rectangle(sb_x, ay, ax + wgt->w, ay + sb_h, wgt->theme.bg_normal);
        al_draw_line(sb_x, ay, sb_x, ay + sb_h, wgt->theme.border_normal, 1.0f);
        /* thumb */
        float ratio = (float)visible_count / (float)ld->nb_items;
        float thumb_h = ratio * sb_h;
        if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
        float track_range = sb_h - thumb_h;
        float pos_ratio = (max_off > 0) ? (float)ld->scroll_offset / (float)max_off : 0;
        float thumb_y = ay + pos_ratio * track_range;
        float thumb_pad = style->scrollbar_thumb_padding;
        al_draw_filled_rounded_rectangle(sb_x + thumb_pad, thumb_y, ax + wgt->w - thumb_pad, thumb_y + thumb_h,
                                         2.0f, 2.0f, wgt->theme.bg_hover);
    }

    /* horizontal scrollbar along the bottom when the widest item overflows */
    if (need_hsb) {
        _draw_cols_scrollbar(ax, ay + wgt->h - style->scrollbar_size, pane_w,
                             content_w, ld->h_scroll, style, &wgt->theme);
    }

    /* frame last, over the rows and scrollbar, so no row fill can eat an edge */
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, wgt->theme.border_thickness);
}

/*! draw a radiolist widget */
static void _draw_radiolist(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, const N_GUI_STYLE* style) {
    N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    int hovered = (wgt->state & N_GUI_STATE_HOVER) ? 1 : 0;

    /* background, honour hover so the radiolist behaves like the
       other interactive widgets even though per-row hover is not
       tracked separately (no consumer today). */
    if (rd->bg_bitmap) {
        al_draw_scaled_bitmap(rd->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(rd->bg_bitmap), (float)al_get_bitmap_height(rd->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        ALLEGRO_COLOR bg = hovered ? wgt->theme.bg_hover : wgt->theme.bg_normal;
        al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, bg);
    }
    ALLEGRO_COLOR bd = hovered ? wgt->theme.border_hover : wgt->theme.border_normal;
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, bd, wgt->theme.border_thickness);

    if (!font) return;
    float fh = (float)al_get_font_line_height(font);
    float ih = rd->item_height > fh ? rd->item_height : fh + style->item_height_pad;
    int visible_count = (int)(wgt->h / ih);
    float radio_r = (ih - style->item_height_pad * 2) / 2.0f;
    if (radio_r < style->radio_circle_min_r) radio_r = style->radio_circle_min_r;
    float pad = style->radio_label_gap;

    /* clamp scroll_offset to valid range */
    int max_off = (int)rd->nb_items - visible_count;
    if (max_off < 0) max_off = 0;
    if (rd->scroll_offset > max_off) rd->scroll_offset = max_off;
    if (rd->scroll_offset < 0) rd->scroll_offset = 0;

    int need_scrollbar = ((int)rd->nb_items > visible_count) ? 1 : 0;

    /* pass 1: item backgrounds and radio circles (skin bitmaps + primitives),
       drawn before the held label pass so no primitive runs inside a hold */
    for (int i = 0; i < visible_count && (size_t)(i + rd->scroll_offset) < rd->nb_items; i++) {
        int idx = i + rd->scroll_offset;
        float iy = ay + (float)i * ih;
        float cy = iy + ih / 2.0f;
        float cx = ax + pad + radio_r;

        /* item background bitmap */
        if (idx == rd->selected_index && rd->item_selected_bitmap) {
            al_draw_scaled_bitmap(rd->item_selected_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(rd->item_selected_bitmap), (float)al_get_bitmap_height(rd->item_selected_bitmap),
                                  ax, iy, wgt->w, ih, 0);
        } else if (rd->item_bg_bitmap) {
            al_draw_scaled_bitmap(rd->item_bg_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(rd->item_bg_bitmap), (float)al_get_bitmap_height(rd->item_bg_bitmap),
                                  ax, iy, wgt->w, ih, 0);
        }

        /* outer circle */
        al_draw_circle(cx, cy, radio_r, wgt->theme.border_normal, _min_thickness(style->radio_circle_border_thickness));

        /* inner filled circle if selected */
        if (idx == rd->selected_index) {
            al_draw_filled_circle(cx, cy, radio_r - style->radio_inner_offset, wgt->theme.text_active);
        }
    }
    /* pass 2: labels, batched into one held block (shared font atlas) */
    al_hold_bitmap_drawing(true);
    for (int i = 0; i < visible_count && (size_t)(i + rd->scroll_offset) < rd->nb_items; i++) {
        int idx = i + rd->scroll_offset;
        float iy = ay + (float)i * ih;
        float cx = ax + pad + radio_r;
        al_draw_text(font, wgt->theme.text_normal,
                     cx + radio_r + pad, iy + (ih - fh) / 2.0f, 0, rd->items[idx].text);
    }
    al_hold_bitmap_drawing(false);

    /* draw scrollbar indicator when items overflow */
    if (need_scrollbar) {
        float sb_w = style->scrollbar_size;
        float sb_x = ax + wgt->w - sb_w;
        al_draw_filled_rectangle(sb_x, ay, ax + wgt->w, ay + wgt->h, wgt->theme.bg_normal);
        al_draw_line(sb_x, ay, sb_x, ay + wgt->h, wgt->theme.border_normal, 1.0f);
        float ratio = (float)visible_count / (float)rd->nb_items;
        float thumb_h = ratio * wgt->h;
        if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
        float track_range = wgt->h - thumb_h;
        float pos_ratio = (max_off > 0) ? (float)rd->scroll_offset / (float)max_off : 0;
        float thumb_y = ay + pos_ratio * track_range;
        float thumb_pad = style->scrollbar_thumb_padding;
        al_draw_filled_rounded_rectangle(sb_x + thumb_pad, thumb_y, ax + wgt->w - thumb_pad, thumb_y + thumb_h,
                                         2.0f, 2.0f, wgt->theme.bg_hover);
    }
}

/*! draw a combobox widget (closed state) */
static void _draw_combobox(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, const N_GUI_STYLE* style) {
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    if (cd->bg_bitmap) {
        al_draw_scaled_bitmap(cd->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(cd->bg_bitmap), (float)al_get_bitmap_height(cd->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        int rounded = _shape_rounded(style, N_GUI_SHAPE_RECT);
        _draw_themed_rect(&wgt->theme, wgt->state, ax, ay, wgt->w, wgt->h, rounded);
    }

    if (!font) return;
    float pad = style->item_text_padding;

    /* display selected item text (truncated to fit) */
    const char* display_text = "";
    if (cd->selected_index >= 0 && (size_t)cd->selected_index < cd->nb_items) {
        display_text = cd->items[cd->selected_index].text;
    }
    int cbbbx = 0, cbbby = 0, cbbbw = 0, cbbbh = 0;
    _text_dims(font, display_text[0] ? display_text : "Ay", &cbbbx, &cbbby, &cbbbw, &cbbbh);
    float fh = (float)cbbbh;
    float max_text_w = wgt->w - pad - style->dropdown_arrow_reserve;
    _draw_text_truncated(font, _text_for_state(&wgt->theme, wgt->state),
                         ax + pad, ay + (wgt->h - fh) / 2.0f - (float)cbbby, max_text_w, display_text);

    /* dropdown arrow */
    float arrow_x = ax + wgt->w - style->dropdown_arrow_reserve;
    float arrow_y = ay + wgt->h / 2.0f;
    ALLEGRO_COLOR ac = wgt->theme.text_normal;
    al_draw_line(arrow_x, arrow_y - style->dropdown_arrow_half_h, arrow_x + style->dropdown_arrow_half_w, arrow_y + style->dropdown_arrow_half_h, ac, _min_thickness(style->dropdown_arrow_thickness));
    al_draw_line(arrow_x + style->dropdown_arrow_half_w, arrow_y + style->dropdown_arrow_half_h, arrow_x + style->dropdown_arrow_half_w * 2, arrow_y - style->dropdown_arrow_half_h, ac, _min_thickness(style->dropdown_arrow_thickness));
}

/*! draw the combobox dropdown overlay (called after all windows) */
/* Compute the effective top-Y of an open dropdown / dropmenu panel.
 * widget_top    = oy + wgt->y (absolute Y of the widget's top edge)
 * widget_bottom = oy + wgt->y + wgt->h (absolute Y of the widget's bottom edge)
 * panel_h       = intended panel height
 * expand_up     = non-zero if caller requests upward expansion (flag set)
 *
 * When expand_up is set, open upward if there is room above; fall back to
 * downward if not. When expand_up is clear, open downward; auto-flip upward
 * only if the panel doesn't fit below and does fit above. If neither fits,
 * whichever side has more free space wins. Keeps draw + click paths in sync. */
static float _dropdown_panel_y(const N_GUI_CTX* ctx, float widget_top, float widget_bottom, float panel_h, int expand_up) {
    float ay_down = widget_bottom;
    float ay_up = widget_top - panel_h;
    int fits_down = (ctx->display_h <= 0) || (ay_down + panel_h <= (float)ctx->display_h);
    int fits_up = (ay_up >= 0.0f);
    if (expand_up) {
        return fits_up ? ay_up : (fits_down ? ay_down : ay_up);
    }
    if (!fits_down && fits_up) return ay_up;
    if (!fits_down && !fits_up) {
        /* neither fits, pick the side with more room */
        float space_down = (ctx->display_h > 0) ? ((float)ctx->display_h - widget_bottom) : panel_h;
        float space_up = widget_top;
        return (space_up > space_down) ? ay_up : ay_down;
    }
    return ay_down;
}

static void _draw_combobox_dropdown(N_GUI_CTX* ctx) {
    if (ctx->open_combobox_id < 0) return;
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, ctx->open_combobox_id);
    if (!wgt || !wgt->data) return;
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)wgt->data;
    if (!cd->is_open || cd->nb_items == 0) return;

    /* find the window containing this widget to get absolute position */
    float ox = 0, oy = 0;
    const N_GUI_WINDOW* owner = _find_widget_window(ctx, wgt->id, &ox, &oy);
    if (!owner) return;
    /* the overlay belongs to its window's display, not to every pass */
    if (!_win_on_pass(ctx, owner)) return;

    ALLEGRO_FONT* font = wgt->font ? wgt->font : ctx->default_font;
    if (!font) return;

    float ax = ox + wgt->x;
    float fh = (float)al_get_font_line_height(font);
    float ih = cd->item_height > fh ? cd->item_height : fh + ctx->style.item_height_pad;
    int vis = (int)cd->nb_items;
    if (vis > cd->max_visible) vis = cd->max_visible;
    float dropdown_h = (float)vis * ih;
    float ay = _dropdown_panel_y(ctx, oy + wgt->y, oy + wgt->y + wgt->h, dropdown_h,
                                 cd->flags & N_GUI_COMBOBOX_EXPAND_UP);
    float pad = ctx->style.item_text_padding;

    /* dropdown width, auto-expand if flag is set */
    float dropdown_w = wgt->w;
    if (cd->flags & N_GUI_COMBOBOX_AUTO_WIDTH) {
        float max_item_w = 0;
        for (size_t i = 0; i < cd->nb_items; i++) {
            float tw = _text_w(font, cd->items[i].text);
            if (tw > max_item_w) max_item_w = tw;
        }
        float needed = max_item_w + pad * 2;
        if (needed > dropdown_w) dropdown_w = needed;
        /* apply cap */
        float cap = ctx->style.combobox_max_dropdown_width;
        if (cap <= 0) cap = ctx->display_w > 0 ? (float)ctx->display_w : 4096.0f;
        if (dropdown_w > cap) dropdown_w = cap;
        /* clamp to display right edge */
        if (ctx->display_w > 0 && ax + dropdown_w > (float)ctx->display_w) {
            dropdown_w = (float)ctx->display_w - ax;
            if (dropdown_w < wgt->w) dropdown_w = wgt->w;
        }
    }

    /* scrollbar geometry, only shown when items exceed max_visible */
    int need_scrollbar = ((int)cd->nb_items > cd->max_visible);
    float sb_size = ctx->style.scrollbar_size;
    if (sb_size < 10.0f) sb_size = 10.0f;
    float item_area_w = need_scrollbar ? dropdown_w - sb_size : dropdown_w;

    /* dropdown background */
    if (cd->bg_bitmap) {
        al_draw_scaled_bitmap(cd->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(cd->bg_bitmap), (float)al_get_bitmap_height(cd->bg_bitmap),
                              ax, ay, dropdown_w, dropdown_h, 0);
    } else {
        al_draw_filled_rectangle(ax, ay, ax + dropdown_w, ay + dropdown_h, wgt->theme.bg_normal);
    }
    /* the border is drawn LAST (after the items and the scrollbar), not here: a
       selection / hover fill on the last visible item reaches ay + dropdown_h, and
       drawing the frame first let that fill paint over the bottom edge so the list
       looked unbordered. Same class of fix as the datagrid, which already frames last. */

    /* clip item drawing to the dropdown area */
    int prev_cx = 0, prev_cy = 0, prev_cw = 0, prev_ch = 0;
    al_get_clipping_rectangle(&prev_cx, &prev_cy, &prev_cw, &prev_ch);
    al_set_clipping_rectangle((int)ax, (int)ay, (int)dropdown_w, (int)dropdown_h);

    /* items: pass 1 draws the selection / hover backgrounds (primitives + skin
       bitmaps), pass 2 draws the labels inside one held block */
    float max_text_w = item_area_w - pad * 2;
    for (int i = 0; i < vis && (size_t)(i + cd->scroll_offset) < cd->nb_items; i++) {
        int idx = i + cd->scroll_offset;
        float iy = ay + (float)i * ih;
        /* the navigation highlight (moved by hover, wheel, and Up/Down) */
        int hovered = (idx == cd->highlight_index);

        if (idx == cd->selected_index) {
            if (cd->item_selected_bitmap) {
                al_draw_scaled_bitmap(cd->item_selected_bitmap, 0, 0,
                                      (float)al_get_bitmap_width(cd->item_selected_bitmap), (float)al_get_bitmap_height(cd->item_selected_bitmap),
                                      ax + ctx->style.item_selection_inset, iy, item_area_w - ctx->style.item_selection_inset * 2, ih, 0);
            } else {
                al_draw_filled_rectangle(ax + ctx->style.item_selection_inset, iy, ax + item_area_w - ctx->style.item_selection_inset, iy + ih, wgt->theme.bg_active);
            }
        } else if (hovered) {
            if (cd->item_bg_bitmap) {
                al_draw_scaled_bitmap(cd->item_bg_bitmap, 0, 0,
                                      (float)al_get_bitmap_width(cd->item_bg_bitmap), (float)al_get_bitmap_height(cd->item_bg_bitmap),
                                      ax + ctx->style.item_selection_inset, iy, item_area_w - ctx->style.item_selection_inset * 2, ih, 0);
            } else {
                al_draw_filled_rectangle(ax + ctx->style.item_selection_inset, iy, ax + item_area_w - ctx->style.item_selection_inset, iy + ih, wgt->theme.bg_hover);
            }
        }
    }
    al_hold_bitmap_drawing(true);
    for (int i = 0; i < vis && (size_t)(i + cd->scroll_offset) < cd->nb_items; i++) {
        int idx = i + cd->scroll_offset;
        float iy = ay + (float)i * ih;
        ALLEGRO_COLOR tc = (idx == cd->selected_index) ? wgt->theme.text_active : wgt->theme.text_normal;
        _draw_text_truncated(font, tc, ax + pad, iy + (ih - fh) / 2.0f, max_text_w, cd->items[idx].text);
    }
    al_hold_bitmap_drawing(false);

    al_set_clipping_rectangle(prev_cx, prev_cy, prev_cw, prev_ch);

    /* scrollbar */
    if (need_scrollbar) {
        float sb_x = ax + dropdown_w - sb_size;
        /* track */
        al_draw_filled_rectangle(sb_x, ay, sb_x + sb_size, ay + dropdown_h,
                                 ctx->style.scrollbar_track_color);
        /* thumb, proportional to visible / total, positioned by scroll_offset */
        float ratio = (float)cd->max_visible / (float)cd->nb_items;
        if (ratio > 1.0f) ratio = 1.0f;
        float thumb_h = ratio * dropdown_h;
        float thumb_min = ctx->style.scrollbar_thumb_min;
        if (thumb_min < 12.0f) thumb_min = 12.0f;
        if (thumb_h < thumb_min) thumb_h = thumb_min;
        int max_offset = (int)cd->nb_items - cd->max_visible;
        float pos_ratio = (max_offset > 0) ? (float)cd->scroll_offset / (float)max_offset : 0.0f;
        float thumb_y = ay + pos_ratio * (dropdown_h - thumb_h);
        float thumb_pad = ctx->style.scrollbar_thumb_padding;
        float corner_r = ctx->style.scrollbar_thumb_corner_r;
        al_draw_filled_rounded_rectangle(sb_x + thumb_pad, thumb_y,
                                         sb_x + sb_size - thumb_pad,
                                         thumb_y + thumb_h,
                                         corner_r, corner_r,
                                         ctx->style.scrollbar_thumb_color);
    }

    /* frame last, over the items and scrollbar, so no row fill can eat an edge */
    al_draw_rectangle(ax, ay, ax + dropdown_w, ay + dropdown_h, wgt->theme.border_active, _min_thickness(ctx->style.dropdown_border_thickness));
}

/*! draw a dropdown menu button (closed state) */
static void _draw_dropmenu(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, const N_GUI_STYLE* style) {
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    int draw_state = wgt->state;
    if (dd->is_open) draw_state |= N_GUI_STATE_ACTIVE;

    _draw_themed_rect(&wgt->theme, draw_state, ax, ay, wgt->w, wgt->h, style->shape_mode != N_GUI_SHAPE_RECT);
    /* dropmenu panel and button default to rounded (shape_mode != RECT) */

    if (font && dd->label[0]) {
        ALLEGRO_COLOR tc = _text_for_state(&wgt->theme, draw_state);
        int dbbx = 0, dbby = 0, dbbw = 0, dbbh = 0;
        _text_dims(font, dd->label, &dbbx, &dbby, &dbbw, &dbbh);
        float fh = (float)dbbh;
        float pad = style->item_text_padding;
        float max_text_w = wgt->w - pad - style->dropdown_arrow_reserve;
        _draw_text_truncated(font, tc, ax + pad, ay + (wgt->h - fh) / 2.0f - (float)dbby, max_text_w, dd->label);
    }

    /* dropdown arrow: a down-chevron when closed, flipped to an up-chevron while
       the panel is open so the button shows its open/closed state at a glance */
    float arrow_x = ax + wgt->w - style->dropdown_arrow_reserve;
    float arrow_y = ay + wgt->h / 2.0f;
    ALLEGRO_COLOR ac = wgt->theme.text_normal;
    float tip = dd->is_open ? -style->dropdown_arrow_half_h : style->dropdown_arrow_half_h;
    al_draw_line(arrow_x, arrow_y - tip, arrow_x + style->dropdown_arrow_half_w, arrow_y + tip, ac, _min_thickness(style->dropdown_arrow_thickness));
    al_draw_line(arrow_x + style->dropdown_arrow_half_w, arrow_y + tip, arrow_x + style->dropdown_arrow_half_w * 2, arrow_y - tip, ac, _min_thickness(style->dropdown_arrow_thickness));
}

/*! draw the dropdown menu panel overlay (called after all windows) */
static void _draw_dropmenu_panel(N_GUI_CTX* ctx) {
    if (ctx->open_dropmenu_id < 0) return;
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, ctx->open_dropmenu_id);
    if (!wgt || !wgt->data) return;
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)wgt->data;
    if (!dd->is_open || dd->nb_entries == 0) return;

    /* find the window containing this widget to get absolute position */
    float ox = 0, oy = 0;
    const N_GUI_WINDOW* owner = _find_widget_window(ctx, wgt->id, &ox, &oy);
    if (!owner) return;
    /* the overlay belongs to its window's display, not to every pass */
    if (!_win_on_pass(ctx, owner)) return;

    ALLEGRO_FONT* font = wgt->font ? wgt->font : ctx->default_font;
    if (!font) return;

    float ax = ox + wgt->x;
    float fh = (float)al_get_font_line_height(font);
    float ih = dd->item_height > fh ? dd->item_height : fh + ctx->style.item_height_pad;
    int vis = (int)dd->nb_entries;
    if (vis > dd->max_visible) vis = dd->max_visible;
    float panel_h = (float)vis * ih;
    float ay = _dropdown_panel_y(ctx, oy + wgt->y, oy + wgt->y + wgt->h, panel_h,
                                 dd->flags & N_GUI_DROPMENU_EXPAND_UP);
    float pad = ctx->style.item_text_padding;
    /* the panel widens beyond the (often narrow) button so entries are not clipped */
    float panel_w = n_gui_dropmenu_panel_width(ctx, wgt->id);

    /* scrollbar geometry, only shown when entries exceed max_visible */
    int need_scrollbar = ((int)dd->nb_entries > dd->max_visible);
    float sb_size = ctx->style.scrollbar_size;
    if (sb_size < 10.0f) sb_size = 10.0f;
    float item_area_w = need_scrollbar ? panel_w - sb_size : panel_w;

    /* panel background (rounded to match the button unless the GUI is square) */
    int panel_rounded = (ctx->style.shape_mode != N_GUI_SHAPE_RECT);
    float prx = panel_rounded ? wgt->theme.corner_rx : 0.0f;
    float pry = panel_rounded ? wgt->theme.corner_ry : 0.0f;
    if (dd->panel_bitmap) {
        al_draw_scaled_bitmap(dd->panel_bitmap, 0, 0,
                              (float)al_get_bitmap_width(dd->panel_bitmap), (float)al_get_bitmap_height(dd->panel_bitmap),
                              ax, ay, panel_w, panel_h, 0);
    } else if (panel_rounded) {
        al_draw_filled_rounded_rectangle(ax, ay, ax + panel_w, ay + panel_h, prx, pry, wgt->theme.bg_normal);
    } else {
        al_draw_filled_rectangle(ax, ay, ax + panel_w, ay + panel_h, wgt->theme.bg_normal);
    }
    /* the border is drawn LAST (after the entries and the scrollbar), not here: a
       hover fill on the last visible entry reaches ay + panel_h, and drawing the frame
       first let that fill paint over the bottom edge so the panel looked unbordered. */

    /* entries: pass 1 draws the hover backgrounds (primitives / skin bitmaps),
       pass 2 draws the labels inside one held block */
    float max_text_w = item_area_w - pad * 2.0f;
    for (int i = 0; i < vis && (size_t)(i + dd->scroll_offset) < dd->nb_entries; i++) {
        int idx = i + dd->scroll_offset;
        float iy = ay + (float)i * ih;
        /* the navigation highlight (moved by hover, wheel, and Up/Down) */
        int hovered = (idx == dd->highlight_index);

        if (hovered) {
            if (dd->item_hover_bitmap) {
                al_draw_scaled_bitmap(dd->item_hover_bitmap, 0, 0,
                                      (float)al_get_bitmap_width(dd->item_hover_bitmap), (float)al_get_bitmap_height(dd->item_hover_bitmap),
                                      ax + ctx->style.item_selection_inset, iy, item_area_w - ctx->style.item_selection_inset * 2, ih, 0);
            } else {
                al_draw_filled_rectangle(ax + ctx->style.item_selection_inset, iy, ax + item_area_w - ctx->style.item_selection_inset, iy + ih, wgt->theme.bg_hover);
            }
        }
    }
    al_hold_bitmap_drawing(true);
    for (int i = 0; i < vis && (size_t)(i + dd->scroll_offset) < dd->nb_entries; i++) {
        int idx = i + dd->scroll_offset;
        float iy = ay + (float)i * ih;
        int hovered = (idx == dd->highlight_index);
        ALLEGRO_COLOR tc = hovered ? wgt->theme.text_hover : wgt->theme.text_normal;
        _draw_text_truncated(font, tc, ax + pad, iy + (ih - fh) / 2.0f, max_text_w, dd->entries[idx].text);
    }
    al_hold_bitmap_drawing(false);

    /* scrollbar */
    if (need_scrollbar) {
        float sb_x = ax + panel_w - sb_size;
        /* track */
        al_draw_filled_rectangle(sb_x, ay, sb_x + sb_size, ay + panel_h,
                                 ctx->style.scrollbar_track_color);
        /* thumb, proportional to visible / total, positioned by scroll_offset */
        float ratio = (float)dd->max_visible / (float)dd->nb_entries;
        if (ratio > 1.0f) ratio = 1.0f;
        float thumb_h = ratio * panel_h;
        float thumb_min = ctx->style.scrollbar_thumb_min;
        if (thumb_min < 12.0f) thumb_min = 12.0f;
        if (thumb_h < thumb_min) thumb_h = thumb_min;
        int max_offset = (int)dd->nb_entries - dd->max_visible;
        float pos_ratio = (max_offset > 0) ? (float)dd->scroll_offset / (float)max_offset : 0.0f;
        float thumb_y = ay + pos_ratio * (panel_h - thumb_h);
        float thumb_pad = ctx->style.scrollbar_thumb_padding;
        float corner_r = ctx->style.scrollbar_thumb_corner_r;
        al_draw_filled_rounded_rectangle(sb_x + thumb_pad, thumb_y,
                                         sb_x + sb_size - thumb_pad,
                                         thumb_y + thumb_h,
                                         corner_r, corner_r,
                                         ctx->style.scrollbar_thumb_color);
    }

    /* frame last, over the entries and scrollbar, so no hover fill can eat an edge */
    if (panel_rounded)
        al_draw_rounded_rectangle(ax, ay, ax + panel_w, ay + panel_h, prx, pry, wgt->theme.border_active, _min_thickness(ctx->style.dropdown_border_thickness));
    else
        al_draw_rectangle(ax, ay, ax + panel_w, ay + panel_h, wgt->theme.border_active, _min_thickness(ctx->style.dropdown_border_thickness));
}

/*! draw an image widget */
static void _draw_image(N_GUI_WIDGET* wgt, float ox, float oy) {
    N_GUI_IMAGE_DATA* id = (N_GUI_IMAGE_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;

    /* border */
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, wgt->theme.border_thickness);

    if (!id->bitmap) return;
    float bw = (float)al_get_bitmap_width(id->bitmap);
    float bh = (float)al_get_bitmap_height(id->bitmap);

    if (id->scale_mode == N_GUI_IMAGE_STRETCH) {
        al_draw_scaled_bitmap(id->bitmap, 0, 0, bw, bh, ax, ay, wgt->w, wgt->h, 0);
    } else if (id->scale_mode == N_GUI_IMAGE_CENTER) {
        float dx = ax + (wgt->w - bw) / 2.0f;
        float dy = ay + (wgt->h - bh) / 2.0f;
        al_draw_bitmap(id->bitmap, dx, dy, 0);
    } else {
        /* N_GUI_IMAGE_FIT */
        float scale_x = wgt->w / bw;
        float scale_y = wgt->h / bh;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;
        float dw = bw * scale;
        float dh = bh * scale;
        float dx = ax + (wgt->w - dw) / 2.0f;
        float dy = ay + (wgt->h - dh) / 2.0f;
        al_draw_scaled_bitmap(id->bitmap, 0, 0, bw, bh, dx, dy, dw, dh, 0);
    }
}

/*! draw a label widget.
 *  win_w > 0 means the parent window is resizable and the label may expand
 *  horizontally to fill the available window width. */
/*! helper: find the byte offset in a label's text closest to a given pixel x position.
 *  Returns -1 if the label has no text or font. */
static int _label_char_at_x(const N_GUI_LABEL_DATA* lb, ALLEGRO_FONT* font, float click_x) {
    if (!font || !lb->text[0]) return -1;
    if (click_x <= 0) return 0;
    size_t len = strlen(lb->text);
    char tmp[N_GUI_TEXT_MAX];
    int best = 0;
    float best_dist = click_x;
    for (size_t i = 0; i < len;) {
        int clen = _utf8_char_len((unsigned char)lb->text[i]);
        if (i + (size_t)clen > len) clen = (int)(len - i);
        size_t pos = i + (size_t)clen;
        memcpy(tmp, lb->text, pos);
        tmp[pos] = '\0';
        float tw = _text_w(font, tmp);
        float dist = click_x - tw;
        if (dist < 0) dist = -dist;
        if (dist < best_dist) {
            best_dist = dist;
            best = (int)pos;
        }
        if (tw > click_x) break;
        i += (size_t)clen;
    }
    return best;
}

/*! helper: compute the screen-space x origin where label text starts,
 *  accounting for alignment. ax is the widget's screen-space left edge.
 *  win_w is the label_win_w hint (0 for fixed windows). */
static float _label_text_origin_x(const N_GUI_LABEL_DATA* lb, ALLEGRO_FONT* font, float ax, float wgt_w, float win_w, float wgt_x, float label_padding) {
    float effective_w = wgt_w;
    if (win_w > 0) {
        float max_from_win = win_w - wgt_x;
        if (max_from_win > effective_w) effective_w = max_from_win;
    }
    int lbbx = 0, lbby = 0, lbbw = 0, lbbh = 0;
    _text_dims(font, lb->text, &lbbx, &lbby, &lbbw, &lbbh);
    float tw = (float)lbbw;
    float max_text_w = effective_w - label_padding * 2;
    if (lb->align == N_GUI_ALIGN_CENTER) {
        if (tw > max_text_w) return ax + label_padding;
        return ax + (effective_w - tw) / 2.0f - (float)lbbx;
    }
    if (lb->align == N_GUI_ALIGN_RIGHT) {
        if (tw > max_text_w) return ax + label_padding;
        return ax + effective_w - tw - label_padding - (float)lbbx;
    }
    /* LEFT or JUSTIFIED (justified single-line fallback) */
    return ax + label_padding;
}

static void _draw_label(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, float win_w, N_GUI_STYLE* style) {
    N_GUI_LABEL_DATA* lb = (N_GUI_LABEL_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    /* optional background bitmap */
    if (lb->bg_bitmap) {
        al_draw_scaled_bitmap(lb->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(lb->bg_bitmap), (float)al_get_bitmap_height(lb->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    }

    if (!font || !lb->text[0]) return;

    int lbbbx = 0, lbbby = 0, lbbbw = 0, lbbbh = 0;
    _text_dims(font, lb->text, &lbbbx, &lbbby, &lbbbw, &lbbbh);
    float fh = (float)lbbbh;

    /* effective widget width: expand into available window space when resizable */
    float effective_w = wgt->w;
    if (win_w > 0) {
        float max_from_win = win_w - wgt->x;
        if (max_from_win > effective_w) effective_w = max_from_win;
    }

    int is_link = (lb->link[0] != '\0') ? 1 : 0;
    ALLEGRO_COLOR tc;
    if (is_link) {
        /* links: show in a distinctive colour, underlined on hover */
        if (wgt->state & N_GUI_STATE_HOVER) {
            tc = style->link_color_hover;
        } else {
            tc = style->link_color_normal;
        }
    } else {
        tc = _text_for_state(&wgt->theme, wgt->state);
    }

    float tw = (float)lbbbw;
    float max_text_w = effective_w - style->label_padding * 2; /* padding each side */
    float tx;

    if (lb->align == N_GUI_ALIGN_JUSTIFIED) {
        float lpad = style->label_padding;
        float view_h = wgt->h - lpad;
        float content_h = _label_content_height(lb->text, font, max_text_w);
        int need_scrollbar = (content_h > view_h) ? 1 : 0;
        float sb_size = need_scrollbar ? style->scrollbar_size : 0;
        float text_w = max_text_w - sb_size;

        /* clamp scroll_y */
        if (need_scrollbar) {
            float max_sy = content_h - view_h;
            if (max_sy < 0) max_sy = 0;
            if (lb->scroll_y > max_sy) lb->scroll_y = max_sy;
            if (lb->scroll_y < 0) lb->scroll_y = 0;
        } else {
            lb->scroll_y = 0;
        }

        /* clip to widget bounds (minus scrollbar) */
        int pcx, pcy, pcw, pch;
        al_get_clipping_rectangle(&pcx, &pcy, &pcw, &pch);
        _set_clipping_rect_transformed((int)ax, (int)ay, (int)(wgt->w - sb_size), (int)wgt->h);

        float text_y = ay + lpad / 2.0f - lb->scroll_y;
        /* draw selection highlight for justified text */
        if (lb->sel_start >= 0 && lb->sel_end >= 0 && lb->sel_start != lb->sel_end) {
            _draw_justified_selection(font, ax + lpad, text_y, text_w, content_h + fh,
                                      lb->text, lb->sel_start, lb->sel_end, wgt->theme.selection_color);
        }
        /* pass a large avail_h so _draw_text_justified doesn't truncate; clipping handles visibility */
        _draw_text_justified(font, tc, ax + lpad, text_y, text_w, content_h + fh, lb->text);

        /* underline for links on hover */
        if (is_link && (wgt->state & N_GUI_STATE_HOVER)) {
            float draw_w = tw < text_w ? tw : text_w;
            al_draw_line(ax + lpad, text_y + fh, ax + lpad + draw_w, text_y + fh, tc, _min_thickness(style->link_underline_thickness));
        }

        al_set_clipping_rectangle(pcx, pcy, pcw, pch);

        /* draw scrollbar */
        if (need_scrollbar) {
            _draw_widget_vscrollbar(ax, ay + lpad / 2.0f, wgt->w, view_h, content_h, lb->scroll_y, style);
        }
        return;
    }

    float ty = ay + (wgt->h - fh) / 2.0f - (float)lbbby;

    if (lb->align == N_GUI_ALIGN_CENTER) {
        if (tw > max_text_w) {
            _draw_text_truncated(font, tc, ax + style->label_padding, ty, max_text_w, lb->text);
            tx = ax + style->label_padding;
        } else {
            tx = ax + (effective_w - tw) / 2.0f - (float)lbbbx;
            al_draw_text(font, tc, tx, ty, 0, lb->text);
        }
    } else if (lb->align == N_GUI_ALIGN_RIGHT) {
        if (tw > max_text_w) {
            _draw_text_truncated(font, tc, ax + style->label_padding, ty, max_text_w, lb->text);
            tx = ax + style->label_padding;
        } else {
            tx = ax + effective_w - tw - style->label_padding - (float)lbbbx;
            al_draw_text(font, tc, tx, ty, 0, lb->text);
        }
    } else {
        /* N_GUI_ALIGN_LEFT */
        tx = ax + style->label_padding;
        if (tw > max_text_w) {
            _draw_text_truncated(font, tc, tx, ty, max_text_w, lb->text);
        } else {
            al_draw_text(font, tc, tx, ty, 0, lb->text);
        }
    }

    /* draw selection highlight for non-justified labels */
    if (lb->sel_start >= 0 && lb->sel_end >= 0 && lb->sel_start != lb->sel_end) {
        int slo = lb->sel_start < lb->sel_end ? lb->sel_start : lb->sel_end;
        int shi = lb->sel_start < lb->sel_end ? lb->sel_end : lb->sel_start;
        size_t tlen = strlen(lb->text);
        if ((size_t)slo > tlen) slo = (int)tlen;
        if ((size_t)shi > tlen) shi = (int)tlen;
        char stmp[N_GUI_TEXT_MAX];
        memcpy(stmp, lb->text, (size_t)slo);
        stmp[slo] = '\0';
        float sx1 = _text_w(font, stmp);
        memcpy(stmp, lb->text, (size_t)shi);
        stmp[shi] = '\0';
        float sx2 = _text_w(font, stmp);
        al_draw_filled_rectangle(tx + sx1, ty, tx + sx2, ty + fh, wgt->theme.selection_color);
    }

    /* underline for links on hover */
    if (is_link && (wgt->state & N_GUI_STATE_HOVER)) {
        float draw_w = tw < max_text_w ? tw : max_text_w;
        al_draw_line(tx, ty + fh, tx + draw_w, ty + fh, tc, _min_thickness(style->link_underline_thickness));
    }
}

/*! draw a single widget.
 *  win_w is the parent window width (>0 for resizable windows, 0 otherwise)
 *  so that labels can expand to fill available space. */
/* owner-draw custom widget: delegate the interior to the user callback, handing it
   the widget's absolute top-left and the effective font. Input is not routed here. */
static void _draw_custom(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font) {
    N_GUI_CUSTOM_DATA* cd = (N_GUI_CUSTOM_DATA*)wgt->data;
    if (!cd || !cd->draw) return;
    cd->draw(wgt, ox + wgt->x, oy + wgt->y, wgt->font ? wgt->font : default_font, cd->user_data);
}

static void _draw_widget(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, float win_w, N_GUI_STYLE* style) {
    if (!wgt || !wgt->visible) return;

    /* disabled widgets: force idle state visually, draw with reduced opacity */
    int saved_state = wgt->state;
    if (!wgt->enabled) {
        wgt->state = N_GUI_STATE_IDLE;
    }

    switch (wgt->type) {
        case N_GUI_TYPE_BUTTON:
            _draw_button(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_SLIDER:
            _draw_slider(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_TEXTAREA:
            _draw_textarea(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_CHECKBOX:
            _draw_checkbox(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_SCROLLBAR:
            _draw_scrollbar(wgt, ox, oy, style);
            break;
        case N_GUI_TYPE_LISTBOX:
            _draw_listbox(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_RADIOLIST:
            _draw_radiolist(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_COMBOBOX:
            _draw_combobox(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_IMAGE:
            _draw_image(wgt, ox, oy);
            break;
        case N_GUI_TYPE_LABEL:
            _draw_label(wgt, ox, oy, default_font, win_w, style);
            break;
        case N_GUI_TYPE_DROPMENU:
            _draw_dropmenu(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_SPLITPANE:
            _draw_splitpane(wgt, ox, oy);
            break;
        case N_GUI_TYPE_HEXVIEW:
            _draw_hexview(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_SYNTAXVIEW:
            _draw_syntaxview(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_DATAGRID:
            _draw_datagrid(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_PROGRESSBAR:
            _draw_progressbar(wgt, ox, oy, default_font, style);
            break;
        case N_GUI_TYPE_CUSTOM:
            _draw_custom(wgt, ox, oy, default_font);
            break;
        default:
            break;
    }

    /* restore state and draw dimming overlay for disabled widgets */
    if (!wgt->enabled) {
        wgt->state = saved_state;
        float dx = ox + wgt->x;
        float dy = oy + wgt->y;
        al_draw_filled_rectangle(dx, dy, dx + wgt->w, dy + wgt->h,
                                 al_map_rgba(0, 0, 0, 120));
    }
}

/* TITLEBAR BUTTONS (minimize, maximize, close) */

/*! return the effective titlebar button size (0 = auto from titlebar_h) */
static float _tb_btn_resolved_size(const N_GUI_WINDOW* win, const N_GUI_STYLE* style) {
    if (style->tb_btn_size > 0) return style->tb_btn_size;
    return win->titlebar_h - 4.0f;
}

/*! compute the total width consumed by enabled titlebar buttons (0 if none/frameless) */
static float _tb_buttons_area_width(const N_GUI_WINDOW* win, const N_GUI_STYLE* style) {
    if (win->flags & N_GUI_WIN_FRAMELESS) return 0.0f;
    int count = 0;
    if (win->flags & N_GUI_WIN_BTN_MINIMIZE) count++;
    if (win->flags & N_GUI_WIN_BTN_MAXIMIZE) count++;
    if (win->flags & N_GUI_WIN_BTN_CLOSE) count++;
    if (count == 0) return 0.0f;
    float sz = _tb_btn_resolved_size(win, style);
    return (float)count * sz + (float)(count - 1) * style->tb_btn_spacing + style->tb_btn_right_margin;
}

/*! compute the absolute rect for a titlebar button.
 *  Layout right-to-left: close rightmost, maximize next, minimize leftmost.
 *  Returns 1 if the button is enabled, 0 if not. */
static int _tb_button_rect(const N_GUI_WINDOW* win, const N_GUI_STYLE* style, int btn_type, float* out_x, float* out_y, float* out_w, float* out_h) {
    if (win->flags & N_GUI_WIN_FRAMELESS) return 0;
    int flag = 0;
    if (btn_type == N_GUI_TB_BTN_MINIMIZE)
        flag = N_GUI_WIN_BTN_MINIMIZE;
    else if (btn_type == N_GUI_TB_BTN_MAXIMIZE)
        flag = N_GUI_WIN_BTN_MAXIMIZE;
    else if (btn_type == N_GUI_TB_BTN_CLOSE)
        flag = N_GUI_WIN_BTN_CLOSE;
    if (!(win->flags & flag)) return 0;

    float sz = _tb_btn_resolved_size(win, style);
    float right_edge = win->x + win->w - style->tb_btn_right_margin;

    /* position index from the right: close=0, maximize=1, minimize=2 (but only for enabled buttons) */
    int pos = 0;
    if (btn_type == N_GUI_TB_BTN_CLOSE) {
        pos = 0;
    } else if (btn_type == N_GUI_TB_BTN_MAXIMIZE) {
        pos = (win->flags & N_GUI_WIN_BTN_CLOSE) ? 1 : 0;
    } else { /* MINIMIZE */
        pos = 0;
        if (win->flags & N_GUI_WIN_BTN_CLOSE) pos++;
        if (win->flags & N_GUI_WIN_BTN_MAXIMIZE) pos++;
    }

    *out_x = right_edge - sz - (float)pos * (sz + style->tb_btn_spacing);
    *out_y = win->y + (win->titlebar_h - sz) / 2.0f;
    *out_w = sz;
    *out_h = sz;
    return 1;
}

/*! draw the titlebar buttons for a window */
static void _draw_tb_buttons(N_GUI_WINDOW* win, const N_GUI_STYLE* style) {
    const int btn_types[] = {N_GUI_TB_BTN_MINIMIZE, N_GUI_TB_BTN_MAXIMIZE, N_GUI_TB_BTN_CLOSE};
    for (int i = 0; i < 3; i++) {
        int bt = btn_types[i];
        float bx, by, bw, bh;
        if (!_tb_button_rect(win, style, bt, &bx, &by, &bw, &bh)) continue;

        /* determine visual state */
        int state = N_GUI_STATE_IDLE;
        if (win->tb_buttons.pressed == bt)
            state = N_GUI_STATE_ACTIVE;
        else if (win->tb_buttons.hovered == bt)
            state = N_GUI_STATE_HOVER;

        /* select theme */
        N_GUI_THEME* theme = (bt == N_GUI_TB_BTN_CLOSE) ? &win->tb_buttons.close_theme : &win->tb_buttons.btn_theme;

        /* select bitmap */
        ALLEGRO_BITMAP* bmp = NULL;
        if (bt == N_GUI_TB_BTN_MINIMIZE) {
            if (state == N_GUI_STATE_ACTIVE && win->tb_buttons.minimize_active_bitmap)
                bmp = win->tb_buttons.minimize_active_bitmap;
            else if (state == N_GUI_STATE_HOVER && win->tb_buttons.minimize_hover_bitmap)
                bmp = win->tb_buttons.minimize_hover_bitmap;
            else
                bmp = win->tb_buttons.minimize_bitmap;
        } else if (bt == N_GUI_TB_BTN_MAXIMIZE) {
            if (state == N_GUI_STATE_ACTIVE && win->tb_buttons.maximize_active_bitmap)
                bmp = win->tb_buttons.maximize_active_bitmap;
            else if (state == N_GUI_STATE_HOVER && win->tb_buttons.maximize_hover_bitmap)
                bmp = win->tb_buttons.maximize_hover_bitmap;
            else
                bmp = win->tb_buttons.maximize_bitmap;
        } else { /* CLOSE */
            if (state == N_GUI_STATE_ACTIVE && win->tb_buttons.close_active_bitmap)
                bmp = win->tb_buttons.close_active_bitmap;
            else if (state == N_GUI_STATE_HOVER && win->tb_buttons.close_hover_bitmap)
                bmp = win->tb_buttons.close_hover_bitmap;
            else
                bmp = win->tb_buttons.close_bitmap;
        }

        if (bmp) {
            al_draw_scaled_bitmap(bmp, 0, 0,
                                  (float)al_get_bitmap_width(bmp), (float)al_get_bitmap_height(bmp),
                                  bx, by, bw, bh, 0);
        } else {
            /* draw themed background */
            _draw_themed_rect(theme, state, bx, by, bw, bh, 1);

            /* draw glyph */
            ALLEGRO_COLOR gc = _text_for_state(theme, state);
            float cx = bx + bw / 2.0f;
            float cy = by + bh / 2.0f;
            float g = bw * 0.22f; /* glyph half-extent */
            float thick = style->tb_btn_glyph_thickness;

            if (bt == N_GUI_TB_BTN_MINIMIZE) {
                /* horizontal line in lower third */
                al_draw_line(cx - g, cy + g * 0.5f, cx + g, cy + g * 0.5f, gc, thick);
            } else if (bt == N_GUI_TB_BTN_MAXIMIZE) {
                if (win->state & N_GUI_WIN_MAXIMISED) {
                    /* restore icon: two overlapping rectangles */
                    float off = g * 0.4f;
                    al_draw_rectangle(cx - g + off, cy - g, cx + g, cy + g - off, gc, thick);
                    al_draw_rectangle(cx - g, cy - g + off, cx + g - off, cy + g, gc, thick);
                } else {
                    /* single rectangle outline */
                    al_draw_rectangle(cx - g, cy - g, cx + g, cy + g, gc, thick);
                }
            } else { /* CLOSE: X shape */
                al_draw_line(cx - g, cy - g, cx + g, cy + g, gc, thick);
                al_draw_line(cx + g, cy - g, cx - g, cy + g, gc, thick);
            }
        }
    }
}

/*! draw a window chrome + its widgets */
static void _draw_window(N_GUI_WINDOW* win, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    if (!(win->state & N_GUI_WIN_OPEN)) return;

    ALLEGRO_FONT* font = win->font ? win->font : default_font;
    int frameless = (win->flags & N_GUI_WIN_FRAMELESS) ? 1 : 0;
    float tbh = frameless ? 0.0f : win->titlebar_h;

    /* title bar (skip for frameless windows) */
    if (!frameless) {
        if (win->titlebar_bitmap) {
            al_draw_scaled_bitmap(win->titlebar_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(win->titlebar_bitmap), (float)al_get_bitmap_height(win->titlebar_bitmap),
                                  win->x, win->y, win->w, tbh, 0);
        } else {
            ALLEGRO_COLOR tb_bg = win->theme.bg_active;
            al_draw_filled_rounded_rectangle(win->x, win->y, win->x + win->w, win->y + tbh,
                                             win->theme.corner_rx, win->theme.corner_ry, tb_bg);
        }
        if (font && win->title[0]) {
            int tbbx = 0, tbby = 0, tbbw = 0, tbbh = 0;
            _text_dims(font, win->title, &tbbx, &tbby, &tbbw, &tbbh);
            float fh = (float)tbbh;
            float btn_area = _tb_buttons_area_width(win, style);
            float max_title_w = win->w - style->title_max_w_reserve - btn_area;
            _draw_text_truncated(font, win->theme.text_active,
                                 win->x + style->title_padding, win->y + (tbh - fh) / 2.0f - (float)tbby, max_title_w, win->title);
        }
        _draw_tb_buttons(win, style);
    }

    ALLEGRO_COLOR tb_bd = win->theme.border_normal;

    /* body (if not minimised) */
    if (!(win->state & N_GUI_WIN_MINIMISED)) {
        float body_y = win->y + tbh;
        float body_h = win->h - tbh;

        /* determine if scrollbars are needed */
        int need_vscroll = 0;
        int need_hscroll = 0;
        float scrollbar_size = style->scrollbar_size;

        if (win->flags & N_GUI_WIN_AUTO_SCROLLBAR) {
            _window_update_content_size(win, default_font);
            if (win->content_h > body_h) need_vscroll = 1;
            if (win->content_w > win->w) need_hscroll = 1;
            /* adjust for scrollbar space */
            if (need_vscroll && win->content_w > (win->w - scrollbar_size)) need_hscroll = 1;
            if (need_hscroll && win->content_h > (body_h - scrollbar_size)) need_vscroll = 1;
            /* vertical-only panels: suppress the horizontal scrollbar (content wider than
               the window is clipped rather than horizontally scrolled) */
            if (win->flags & N_GUI_WIN_NO_HSCROLL) {
                need_hscroll = 0;
                win->scroll_x = 0;
            }
        }

        float content_area_w = win->w - (need_vscroll ? scrollbar_size : 0);
        float content_area_h = body_h - (need_hscroll ? scrollbar_size : 0);

        /* clamp scroll */
        if (need_vscroll) {
            float max_sy = win->content_h - content_area_h;
            if (max_sy < 0) max_sy = 0;
            if (win->scroll_y > max_sy) win->scroll_y = max_sy;
            if (win->scroll_y < 0) win->scroll_y = 0;
        } else {
            win->scroll_y = 0;
        }
        if (need_hscroll) {
            float max_sx = win->content_w - content_area_w;
            if (max_sx < 0) max_sx = 0;
            if (win->scroll_x > max_sx) win->scroll_x = max_sx;
            if (win->scroll_x < 0) win->scroll_x = 0;
        } else {
            win->scroll_x = 0;
        }

        /* Body background. It starts one pixel ABOVE body_y so it tucks under the
         * title bar. A window at a fractional y (a centred pop-up lands on .5 often
         * enough) rasterises the title bar's bottom edge and the body's top edge to
         * different device rows, which left a one-pixel strip painted by neither and
         * showing whatever was behind the window. The overlap is hidden under the
         * title bar, which is drawn opaque just above, and only the fill is nudged:
         * body_y itself still drives the widget origin, the scroll maths and hit
         * testing, so nothing moves. */
        float bg_top = frameless ? body_y : body_y - 1.0f;
        if (win->bg_bitmap) {
            _draw_bitmap_scaled(win->bg_bitmap, win->x, bg_top, win->w, body_h + (body_y - bg_top), win->bg_scale_mode);
        } else {
            al_draw_filled_rectangle(win->x, bg_top, win->x + win->w, win->y + win->h,
                                     win->theme.bg_normal);
        }
        al_draw_rounded_rectangle(win->x, win->y, win->x + win->w, win->y + win->h,
                                  win->theme.corner_rx, win->theme.corner_ry, tb_bd,
                                  _min_thickness(win->theme.border_thickness));

        /* draw widgets with clipping to content area */
        float ox = win->x - win->scroll_x;
        float oy = body_y - win->scroll_y;

        /* set clipping rectangle to window content area */
        int prev_cx, prev_cy, prev_cw, prev_ch;
        al_get_clipping_rectangle(&prev_cx, &prev_cy, &prev_cw, &prev_ch);
        /* Frameless windows have no chrome, their content is edge-to-edge
         * by design (drag-handle holders for HUD icons), so skip the
         * thumb-padding inset that would clip the leftmost pixels. */
        int clip_pad = frameless ? 0 : (int)style->scrollbar_thumb_padding;
        _set_clipping_rect_transformed((int)win->x + clip_pad, (int)body_y,
                                       (int)content_area_w - clip_pad, (int)content_area_h);

        /* pass width hint so labels can adapt:
         * - auto-scrollbar windows: use content width so labels are never truncated
         *   (the clipping rect + scrollbars handle overflow)
         * - resizable windows (without auto-scrollbar): use window width so labels expand
         * - fixed windows: 0 (use widget's own width) */
        float label_win_w = 0;
        if (win->flags & N_GUI_WIN_AUTO_SCROLLBAR)
            label_win_w = win->content_w + style->title_max_w_reserve + style->label_padding;
        else if (win->flags & N_GUI_WIN_RESIZABLE)
            label_win_w = win->w;
        list_foreach(node, win->widgets) {
            _draw_widget((N_GUI_WIDGET*)node->ptr, ox, oy, default_font, label_win_w, style);
        }

        /* restore clipping */
        al_set_clipping_rectangle(prev_cx, prev_cy, prev_cw, prev_ch);

        /* draw automatic scrollbars */
        if (need_vscroll) {
            float sb_x = win->x + win->w - scrollbar_size;
            float sb_y = body_y;
            float sb_h = content_area_h;
            /* leave room for resize corner when no horizontal scrollbar */
            if ((win->flags & N_GUI_WIN_RESIZABLE) && !need_hscroll) sb_h -= scrollbar_size;

            /* track */
            al_draw_filled_rectangle(sb_x, sb_y, sb_x + scrollbar_size, sb_y + sb_h,
                                     style->scrollbar_track_color);

            /* thumb */
            float ratio = content_area_h / win->content_h;
            if (ratio > 1.0f) ratio = 1.0f;
            float thumb_h = ratio * sb_h;
            if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
            float max_scroll = win->content_h - content_area_h;
            float pos_ratio = (max_scroll > 0) ? win->scroll_y / max_scroll : 0;
            float thumb_y = sb_y + pos_ratio * (sb_h - thumb_h);
            al_draw_filled_rounded_rectangle(sb_x + style->scrollbar_thumb_padding, thumb_y, sb_x + scrollbar_size - style->scrollbar_thumb_padding,
                                             thumb_y + thumb_h, style->scrollbar_thumb_corner_r, style->scrollbar_thumb_corner_r,
                                             style->scrollbar_thumb_color);
        }
        if (need_hscroll) {
            float sb_x = win->x;
            float sb_y = win->y + win->h - scrollbar_size;
            float sb_w = content_area_w;
            /* leave room for resize corner when no vertical scrollbar */
            if ((win->flags & N_GUI_WIN_RESIZABLE) && !need_vscroll) sb_w -= scrollbar_size;

            /* track */
            al_draw_filled_rectangle(sb_x, sb_y, sb_x + sb_w, sb_y + scrollbar_size,
                                     style->scrollbar_track_color);

            /* thumb */
            float ratio = content_area_w / win->content_w;
            if (ratio > 1.0f) ratio = 1.0f;
            float thumb_w = ratio * sb_w;
            if (thumb_w < style->scrollbar_thumb_min) thumb_w = style->scrollbar_thumb_min;
            float max_scroll = win->content_w - content_area_w;
            float pos_ratio = (max_scroll > 0) ? win->scroll_x / max_scroll : 0;
            float thumb_x = sb_x + pos_ratio * (sb_w - thumb_w);
            al_draw_filled_rounded_rectangle(thumb_x, sb_y + style->scrollbar_thumb_padding, thumb_x + thumb_w,
                                             sb_y + scrollbar_size - style->scrollbar_thumb_padding, style->scrollbar_thumb_corner_r, style->scrollbar_thumb_corner_r,
                                             style->scrollbar_thumb_color);
        }

        /* draw resize handle if window is resizable */
        if (win->flags & N_GUI_WIN_RESIZABLE) {
            float rx = win->x + win->w;
            float ry = win->y + win->h;
            float grip_size = style->grip_size;
            ALLEGRO_COLOR grip_color = style->grip_color;
            /* three diagonal lines in bottom-right corner */
            float grip_thick = _min_thickness(style->grip_line_thickness);
            al_draw_line(rx - grip_size, ry - 2, rx - 2, ry - grip_size, grip_color, grip_thick);
            al_draw_line(rx - grip_size + 4, ry - 2, rx - 2, ry - grip_size + 4, grip_color, grip_thick);
            al_draw_line(rx - grip_size + 8, ry - 2, rx - 2, ry - grip_size + 8, grip_color, grip_thick);
        }
    } else {
        al_draw_rounded_rectangle(win->x, win->y, win->x + win->w, win->y + tbh,
                                  win->theme.corner_rx, win->theme.corner_ry, tb_bd,
                                  _min_thickness(win->theme.border_thickness));
    }
}

/*! helper: compute the bounding box of all open windows */
static void _compute_gui_bounds(N_GUI_CTX* ctx) {
    float max_r = 0, max_b = 0;
    list_foreach(wnode, ctx->windows) {
        const N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!(win->state & N_GUI_WIN_OPEN)) continue;
        /* detached windows sit in their own display and never contribute to the
         * main display's global scroll bounds */
        if (!_win_on_pass(ctx, win)) continue;
        float r = win->x + win->w;
        float b = win->y + win->h;
        if (r > max_r) max_r = r;
        if (b > max_b) max_b = b;
    }
    ctx->gui_bounds_w = max_r;
    ctx->gui_bounds_h = max_b;
}

/*! helper: al_do_multiline_text callback that just counts the wrapped lines. */
static bool _tooltip_line_cb(int line_num, const char* line, int size, void* extra) {
    (void)line_num;
    (void)line;
    (void)size;
    (*(int*)extra)++;
    return true;
}

/*! helper: draw the hover-tooltip bubble for the currently armed tooltip widget,
 *  but ONLY in the render pass of the display that owns that widget's window. A
 *  detached (native) window is a separate Allegro display, so a tooltip for a widget
 *  on it must be drawn into that display with that window's local mouse coordinates,
 *  exactly like the combobox/dropmenu overlays; drawing it once in the main pass (as
 *  before) put the bubble under the native window and it did not follow the window.
 *  Called from both the main pass and each detached pass; the pass_display guard makes
 *  it draw exactly once, on the right display.
 *
 *  The bubble is kept ENTIRELY inside its widget's window (intersected with the
 *  display): a tooltip wider than the window is WORD-WRAPPED to the window width and
 *  the bubble is shifted left/up (flipping above the cursor) so the full text always
 *  shows and nothing is clipped at the window edge - unless the window is genuinely too
 *  small to hold even the wrapped text, in which case it is best-effort clamped. */
static void _draw_tooltip(N_GUI_CTX* ctx) {
    if (!(ctx->style.tooltip_delay > 0.0f && ctx->tooltip_widget_id >= 0 && (al_get_time() - ctx->tooltip_armed_at) >= (double)ctx->style.tooltip_delay))
        return;
    {
        float ox = 0.0f, oy = 0.0f;
        N_GUI_WINDOW* tip_win = _find_widget_window(ctx, ctx->tooltip_widget_id, &ox, &oy);
        ALLEGRO_DISPLAY* owner = tip_win ? tip_win->native : NULL;
        N_GUI_WIDGET* tip_wgt = n_gui_get_widget(ctx, ctx->tooltip_widget_id);
        /* only draw in the pass of the display this widget's window lives on */
        if (owner != ctx->pass_display)
            return;
        if (tip_wgt && tip_wgt->visible && tip_wgt->tooltip && tip_wgt->tooltip[0] && ctx->default_font) {
            float disp_w = (owner ? (float)al_get_display_width(owner) : (float)ctx->display_w);
            float disp_h = (owner ? (float)al_get_display_height(owner) : (float)ctx->display_h);
            /* clamp rectangle: the widget's window bounds intersected with the display,
               falling back to the whole display if that rect is unusable */
            float cl = 0.0f, ct = 0.0f, cr = disp_w, cb = disp_h;
            if (tip_win) {
                if (tip_win->x > cl) cl = tip_win->x;
                if (tip_win->y > ct) ct = tip_win->y;
                if (disp_w > 0.0f && tip_win->x + tip_win->w < cr) cr = tip_win->x + tip_win->w;
                if (disp_h > 0.0f && tip_win->y + tip_win->h < cb) cb = tip_win->y + tip_win->h;
                if (cr - cl < 8.0f || cb - ct < 8.0f) {
                    cl = 0.0f;
                    ct = 0.0f;
                    cr = disp_w;
                    cb = disp_h;
                }
            }
            {
                float line_h = (float)al_get_font_line_height(ctx->default_font) + 2.0f;
                float max_text_w = (cr - cl) - 4.0f - 16.0f; /* window inner width minus a 2px margin and the bubble's 8px h-pad each side */
                float natural_w, text_w, bw, bh, bx, by;
                int nlines = 1;
                if (max_text_w < 1.0f) max_text_w = 1.0f; /* window too tiny: best effort */
                natural_w = _text_w(ctx->default_font, tip_wgt->tooltip);
                if (natural_w <= max_text_w) {
                    text_w = natural_w; /* fits on one line */
                } else {
                    text_w = max_text_w; /* wrap to the available width */
                    nlines = 0;
                    al_do_multiline_text(ctx->default_font, max_text_w, tip_wgt->tooltip, _tooltip_line_cb, &nlines);
                    if (nlines < 1) nlines = 1;
                }
                bw = text_w + 16.0f;
                bh = (float)nlines * line_h + 10.0f;
                bx = (float)ctx->mouse_x + 14.0f;
                by = (float)ctx->mouse_y + 20.0f;
                /* keep the whole bubble inside the clamp rect: shift left, and flip above
                   the cursor when it would overflow the bottom */
                if (bx + bw > cr) bx = cr - bw;
                if (bx < cl) bx = cl;
                if (by + bh > cb) by = (float)ctx->mouse_y - bh - 6.0f;
                if (by + bh > cb) by = cb - bh;
                if (by < ct) by = ct;
                al_draw_filled_rounded_rectangle(bx, by, bx + bw, by + bh, 4.0f, 4.0f, ctx->style.tooltip_bg);
                al_draw_rounded_rectangle(bx, by, bx + bw, by + bh, 4.0f, 4.0f, ctx->style.tooltip_border, 1.0f);
                al_draw_multiline_text(ctx->default_font, ctx->style.tooltip_fg, bx + 8.0f, by + 5.0f, max_text_w, line_h, ALLEGRO_ALIGN_LEFT, tip_wgt->tooltip);
            }
        }
    }
}

/**
 * @brief Draw all visible windows and their widgets.
 *
 * When a display size has been set (via n_gui_set_display_size) and the total
 * GUI bounding box exceeds it, global scrollbars are drawn at the edges of the
 * display and all windows are offset by the global scroll position.
 *
 * @param ctx the GUI context
 */
void n_gui_draw(N_GUI_CTX* ctx) {
    __n_assert(ctx, return);

    /* this is the main display's pass: detached windows are skipped here and
     * drawn by n_gui_draw_detached into their own displays */
    ctx->pass_display = NULL;

    /* compute bounds and determine if global scrollbars are needed.
     * When virtual canvas is active, compare bounds against virtual dimensions
     * (since window positions are in virtual coordinates). */
    _compute_gui_bounds(ctx);
    float scrollbar_size = ctx->style.global_scrollbar_size;
    int need_global_vscroll = 0;
    int need_global_hscroll = 0;

    /* gate on both dimensions: virtual canvas is only active when both are set */
    int virtual_active = (ctx->virtual_w > 0 && ctx->virtual_h > 0);
    float eff_w = virtual_active ? ctx->virtual_w : ctx->display_w;
    float eff_h = virtual_active ? ctx->virtual_h : ctx->display_h;

    if (eff_w > 0 && eff_h > 0) {
        if (ctx->gui_bounds_h > eff_h) need_global_vscroll = 1;
        if (ctx->gui_bounds_w > eff_w) need_global_hscroll = 1;
        /* account for scrollbar space */
        if (need_global_vscroll && ctx->gui_bounds_w > (eff_w - scrollbar_size)) need_global_hscroll = 1;
        if (need_global_hscroll && ctx->gui_bounds_h > (eff_h - scrollbar_size)) need_global_vscroll = 1;

        /* clamp global scroll */
        float view_w = eff_w - (need_global_vscroll ? scrollbar_size : 0);
        float view_h = eff_h - (need_global_hscroll ? scrollbar_size : 0);
        if (need_global_vscroll) {
            float max_sy = ctx->gui_bounds_h - view_h;
            if (max_sy < 0) max_sy = 0;
            if (ctx->global_scroll_y > max_sy) ctx->global_scroll_y = max_sy;
            if (ctx->global_scroll_y < 0) ctx->global_scroll_y = 0;
        } else {
            ctx->global_scroll_y = 0;
        }
        if (need_global_hscroll) {
            float max_sx = ctx->gui_bounds_w - view_w;
            if (max_sx < 0) max_sx = 0;
            if (ctx->global_scroll_x > max_sx) ctx->global_scroll_x = max_sx;
            if (ctx->global_scroll_x < 0) ctx->global_scroll_x = 0;
        } else {
            ctx->global_scroll_x = 0;
        }
    }

    /* apply global scroll in virtual units, then scale + offset to physical display */
    ALLEGRO_TRANSFORM global_tf, prev_tf;
    al_copy_transform(&prev_tf, al_get_current_transform());
    al_identity_transform(&global_tf);
    al_translate_transform(&global_tf, -ctx->global_scroll_x, -ctx->global_scroll_y);
    if (ctx->virtual_w > 0 && ctx->virtual_h > 0 && ctx->gui_scale > 0) {
        al_scale_transform(&global_tf, ctx->gui_scale, ctx->gui_scale);
        al_translate_transform(&global_tf, ctx->gui_offset_x, ctx->gui_offset_y);
    }
    al_compose_transform(&global_tf, &prev_tf);
    al_use_transform(&global_tf);

    /* clip to the content area (excluding global scrollbar space) */
    int prev_cx, prev_cy, prev_cw, prev_ch;
    int set_clip = 0;
    if (need_global_vscroll || need_global_hscroll) {
        al_get_clipping_rectangle(&prev_cx, &prev_cy, &prev_cw, &prev_ch);
        /* clipping is in physical pixels; map virtual scrollbar area to screen */
        float clip_vw = eff_w - (need_global_vscroll ? scrollbar_size : 0);
        float clip_vh = eff_h - (need_global_hscroll ? scrollbar_size : 0);
        if (ctx->virtual_w > 0 && ctx->virtual_h > 0 && ctx->gui_scale > 0) {
            al_set_clipping_rectangle((int)ctx->gui_offset_x, (int)ctx->gui_offset_y,
                                      (int)(clip_vw * ctx->gui_scale),
                                      (int)(clip_vh * ctx->gui_scale));
        } else {
            al_set_clipping_rectangle(0, 0, (int)clip_vw, (int)clip_vh);
        }
        set_clip = 1;
    }

    /* auto-apply autofit for windows that have it configured */
    list_foreach(anode, ctx->windows) {
        const N_GUI_WINDOW* awin = (N_GUI_WINDOW*)anode->ptr;
        if (awin && awin->autofit_flags && _win_on_pass(ctx, awin)) {
            n_gui_window_apply_autofit(ctx, awin->id);
        }
    }

    list_foreach(node, ctx->windows) {
        N_GUI_WINDOW* dwin = (N_GUI_WINDOW*)node->ptr;
        if (!_win_on_pass(ctx, dwin)) continue;
        _draw_window(dwin, ctx->default_font, &ctx->style);
        /* Per-window custom rendering, drawn at this window's z-order so it
         * is occluded by any window drawn later in the loop. */
        if (dwin && dwin->on_content_draw && (dwin->state & N_GUI_WIN_OPEN) && !(dwin->state & N_GUI_WIN_MINIMISED)) {
            dwin->on_content_draw(dwin->id, dwin->on_content_draw_data);
        }
    }
    /* draw combobox dropdown overlay on top of everything */
    _draw_combobox_dropdown(ctx);
    /* draw dropdown menu panel overlay on top of everything */
    _draw_dropmenu_panel(ctx);

    /* restore transform and clipping */
    if (set_clip) {
        al_set_clipping_rectangle(prev_cx, prev_cy, prev_cw, prev_ch);
    }

    /* draw global scrollbars: apply virtual canvas transform (without scroll)
     * so scrollbars appear at the edges of the virtual canvas area */
    if (need_global_vscroll || need_global_hscroll) {
        ALLEGRO_TRANSFORM sb_tf;
        al_identity_transform(&sb_tf);
        if (ctx->virtual_w > 0 && ctx->virtual_h > 0 && ctx->gui_scale > 0) {
            al_scale_transform(&sb_tf, ctx->gui_scale, ctx->gui_scale);
            al_translate_transform(&sb_tf, ctx->gui_offset_x, ctx->gui_offset_y);
        }
        al_compose_transform(&sb_tf, &prev_tf);
        al_use_transform(&sb_tf);
    }

    if (need_global_vscroll) {
        float sb_x = eff_w - scrollbar_size;
        float sb_y = 0;
        float sb_h = eff_h - (need_global_hscroll ? scrollbar_size : 0);
        float view_h = sb_h;

        /* track */
        al_draw_filled_rectangle(sb_x, sb_y, sb_x + scrollbar_size, sb_y + sb_h,
                                 ctx->style.global_scrollbar_track_color);

        /* thumb */
        float ratio = view_h / ctx->gui_bounds_h;
        if (ratio > 1.0f) ratio = 1.0f;
        float thumb_h = ratio * sb_h;
        if (thumb_h < ctx->style.global_scrollbar_thumb_min) thumb_h = ctx->style.global_scrollbar_thumb_min;
        float max_scroll = ctx->gui_bounds_h - view_h;
        float pos_ratio = (max_scroll > 0) ? ctx->global_scroll_y / max_scroll : 0;
        float thumb_y = sb_y + pos_ratio * (sb_h - thumb_h);
        al_draw_filled_rounded_rectangle(sb_x + ctx->style.global_scrollbar_thumb_padding, thumb_y, sb_x + scrollbar_size - ctx->style.global_scrollbar_thumb_padding,
                                         thumb_y + thumb_h, ctx->style.global_scrollbar_thumb_corner_r, ctx->style.global_scrollbar_thumb_corner_r,
                                         ctx->style.global_scrollbar_thumb_color);
        al_draw_rounded_rectangle(sb_x + ctx->style.global_scrollbar_thumb_padding, thumb_y, sb_x + scrollbar_size - ctx->style.global_scrollbar_thumb_padding,
                                  thumb_y + thumb_h, ctx->style.global_scrollbar_thumb_corner_r, ctx->style.global_scrollbar_thumb_corner_r,
                                  ctx->style.global_scrollbar_thumb_border_color, ctx->style.global_scrollbar_border_thickness);
    }
    if (need_global_hscroll) {
        float sb_x = 0;
        float sb_y = eff_h - scrollbar_size;
        float sb_w = eff_w - (need_global_vscroll ? scrollbar_size : 0);
        float view_w = sb_w;

        /* track */
        al_draw_filled_rectangle(sb_x, sb_y, sb_x + sb_w, sb_y + scrollbar_size,
                                 ctx->style.global_scrollbar_track_color);

        /* thumb */
        float ratio = view_w / ctx->gui_bounds_w;
        if (ratio > 1.0f) ratio = 1.0f;
        float thumb_w = ratio * sb_w;
        if (thumb_w < ctx->style.global_scrollbar_thumb_min) thumb_w = ctx->style.global_scrollbar_thumb_min;
        float max_scroll = ctx->gui_bounds_w - view_w;
        float pos_ratio = (max_scroll > 0) ? ctx->global_scroll_x / max_scroll : 0;
        float thumb_x = sb_x + pos_ratio * (sb_w - thumb_w);
        al_draw_filled_rounded_rectangle(thumb_x, sb_y + ctx->style.global_scrollbar_thumb_padding, thumb_x + thumb_w,
                                         sb_y + scrollbar_size - ctx->style.global_scrollbar_thumb_padding, ctx->style.global_scrollbar_thumb_corner_r, ctx->style.global_scrollbar_thumb_corner_r,
                                         ctx->style.global_scrollbar_thumb_color);
        al_draw_rounded_rectangle(thumb_x, sb_y + ctx->style.global_scrollbar_thumb_padding, thumb_x + thumb_w,
                                  sb_y + scrollbar_size - ctx->style.global_scrollbar_thumb_padding, ctx->style.global_scrollbar_thumb_corner_r, ctx->style.global_scrollbar_thumb_corner_r,
                                  ctx->style.global_scrollbar_thumb_border_color, ctx->style.global_scrollbar_border_thickness);
    }

    /* tooltip bubble: after the pointer rested on a tooltip widget (main-display pass;
     * a widget on a detached native window is drawn in that window's pass instead) */
    _draw_tooltip(ctx);

    al_use_transform(&prev_tf);

    /* The current state is now on the back buffer, so clear the dirty bit. Done
     * last, after the draw-time adjustments above (scroll clamping, autofit,
     * bounds), so those are reflected in this frame. n_gui_needs_redraw stays
     * true while an animation is pending or new input/state arrives. */
    ctx->dirty = 0;
}

/**
 * @brief Render and flip every detached window, and release the ones whose
 * native window was closed.
 *
 * Call once per frame, after the event queue has been drained: this is the safe
 * point where a display whose close was requested is actually destroyed, so no
 * already-queued event can name a freed display.
 *
 * Each native window is drawn with an identity transform, the virtual canvas,
 * global scroll and global scrollbars belong to the main display only. The
 * caller's target bitmap is restored before returning.
 */
void n_gui_draw_detached(N_GUI_CTX* ctx) {
    __n_assert(ctx, return);
    __n_assert(ctx->windows, return);

    ALLEGRO_BITMAP* prev_target = al_get_target_bitmap();

    /* Deferred release pass, before drawing so a window closed this frame does
     * not get one last frame painted into a display that is about to go away.
     * Collected first because _release_native_window can pump the platform
     * event loop, which must not happen while iterating the window list. */
    int pending = 0;
    list_foreach(cnode, ctx->windows) {
        const N_GUI_WINDOW* cwin = (const N_GUI_WINDOW*)cnode->ptr;
        if (cwin && cwin->native && cwin->native_close_pending) pending = 1;
    }
    while (pending) {
        N_GUI_WINDOW* victim = NULL;
        pending = 0;
        list_foreach(cnode, ctx->windows) {
            N_GUI_WINDOW* cwin = (N_GUI_WINDOW*)cnode->ptr;
            if (cwin && cwin->native && cwin->native_close_pending) {
                victim = cwin;
                break;
            }
        }
        if (!victim) break;
        /* keep the pop-up geometry the window will fall back to, and let
         * want_native survive so re-opening restores the native window */
        int keep_want = victim->want_native;
        _release_native_window(ctx, victim);
        victim->x = victim->saved_x;
        victim->y = victim->saved_y;
        victim->w = victim->saved_w;
        victim->h = victim->saved_h;
        victim->flags = victim->saved_flags;
        victim->want_native = keep_want;
        list_foreach(cnode, ctx->windows) {
            const N_GUI_WINDOW* cwin = (const N_GUI_WINDOW*)cnode->ptr;
            if (cwin && cwin->native && cwin->native_close_pending) pending = 1;
        }
    }

    list_foreach(node, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)node->ptr;
        if (!win || !win->native) continue;
        if (!(win->state & N_GUI_WIN_OPEN)) continue;
        /* the OS took the surface away (Android), drawing into it is undefined */
        if (win->native_halted) continue;
        /* the window manager iconified the window: ALLEGRO_MINIMIZED is
         * read-only, so this is the only way to know, and there is nothing to
         * paint into a display the user cannot see */
        if (al_get_display_flags(win->native) & ALLEGRO_MINIMIZED) continue;

        al_set_target_backbuffer(win->native);

        ALLEGRO_TRANSFORM id_tf, prev_tf;
        al_copy_transform(&prev_tf, al_get_current_transform());
        al_identity_transform(&id_tf);
        al_use_transform(&id_tf);

        al_clear_to_color(win->theme.bg_normal);

        ctx->pass_display = win->native;
        if (win->autofit_flags) {
            n_gui_window_apply_autofit(ctx, win->id);
        }
        _draw_window(win, ctx->default_font, &ctx->style);
        if (win->on_content_draw && !(win->state & N_GUI_WIN_MINIMISED)) {
            win->on_content_draw(win->id, win->on_content_draw_data);
        }
        /* an open combobox/dropmenu overlay, and a tooltip for a widget on this
         * detached window, draw on the display they live on */
        _draw_combobox_dropdown(ctx);
        _draw_dropmenu_panel(ctx);
        _draw_tooltip(ctx);
        ctx->pass_display = NULL;

        al_use_transform(&prev_tf);
        al_flip_display();
    }

    if (prev_target) al_set_target_bitmap(prev_target);
}

/* VIRTUAL CANVAS / DISPLAY / DPI */

/**
 * @brief Set the virtual canvas size for resolution-independent scaling.
 *
 * When enabled (w > 0 && h > 0), all widget coordinates are treated as
 * virtual pixels.  An ALLEGRO_TRANSFORM is applied in n_gui_draw() and
 * mouse input is reverse-transformed in n_gui_process_event().
 * Pass (0, 0) to disable (identity transform, legacy behaviour).
 */
void n_gui_set_virtual_size(N_GUI_CTX* ctx, float w, float h) {
    __n_assert(ctx, return);
    if ((w > 0) != (h > 0)) {
        n_log(LOG_ERR, "n_gui_set_virtual_size: both dimensions must be > 0 (got w=%.1f, h=%.1f); virtual canvas not changed", w, h);
        return;
    }
    ctx->virtual_w = w;
    ctx->virtual_h = h;
    n_gui_update_transform(ctx);
}

/**
 * @brief Recalculate scale and offset from virtual canvas to physical display.
 *
 * Uses uniform (aspect-preserving) scaling with centred letterboxing,
 * identical to the approach used by BlockBlaster's update_view_offset().
 * Called automatically by n_gui_set_display_size() when virtual canvas is set.
 */
void n_gui_update_transform(N_GUI_CTX* ctx) {
    __n_assert(ctx, return);

    if (ctx->virtual_w <= 0 || ctx->virtual_h <= 0 ||
        ctx->display_w <= 0 || ctx->display_h <= 0) {
        ctx->gui_scale = 1.0f;
        ctx->gui_offset_x = 0.0f;
        ctx->gui_offset_y = 0.0f;
        return;
    }

    float sx = ctx->display_w / ctx->virtual_w;
    float sy = ctx->display_h / ctx->virtual_h;
    ctx->gui_scale = (sx < sy) ? sx : sy;
    if (ctx->gui_scale < 0.01f) ctx->gui_scale = 0.01f;

    float scaled_w = ctx->virtual_w * ctx->gui_scale;
    float scaled_h = ctx->virtual_h * ctx->gui_scale;
    ctx->gui_offset_x = (ctx->display_w - scaled_w) * 0.5f;
    ctx->gui_offset_y = (ctx->display_h - scaled_h) * 0.5f;
}

/**
 * @brief Convert physical screen coordinates to virtual canvas coordinates.
 *
 * If virtual canvas is disabled (virtual_w/virtual_h == 0), outputs equal inputs.
 */
void n_gui_screen_to_virtual(const N_GUI_CTX* ctx, float sx, float sy, float* vx, float* vy) {
    __n_assert(ctx, return);

    if (ctx->virtual_w <= 0 || ctx->virtual_h <= 0 || ctx->gui_scale <= 0) {
        if (vx) *vx = sx;
        if (vy) *vy = sy;
        return;
    }

    float scale = ctx->gui_scale;
    if (vx) *vx = (sx - ctx->gui_offset_x) / scale;
    if (vy) *vy = (sy - ctx->gui_offset_y) / scale;
}

/* ADAPTIVE RESIZE PUBLIC API */

/**
 * @brief Set context-level resize mode: N_GUI_RESIZE_VIRTUAL (default) or N_GUI_RESIZE_ADAPTIVE.
 * When switching to ADAPTIVE, the virtual canvas is disabled and normalized
 * coordinates are captured for all existing windows.
 */
void n_gui_set_resize_mode(N_GUI_CTX* ctx, int mode) {
    __n_assert(ctx, return);
    ctx->resize_mode = mode;

    if (mode == N_GUI_RESIZE_ADAPTIVE) {
        /* disable virtual canvas scaling, virtual tracks display */
        ctx->virtual_w = 0;
        ctx->virtual_h = 0;
        ctx->gui_scale = 1.0f;
        ctx->gui_offset_x = 0.0f;
        ctx->gui_offset_y = 0.0f;

        /* set reference display size */
        ctx->ref_display_w = ctx->display_w;
        ctx->ref_display_h = ctx->display_h;

        /* capture normalized coords for all windows */
        list_foreach(wnode, ctx->windows) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            _n_gui_window_capture_normalized(ctx, win);
        }
    }
}

/**
 * @brief Get current resize mode.
 * @return N_GUI_RESIZE_VIRTUAL or N_GUI_RESIZE_ADAPTIVE
 */
// cppcheck-suppress constParameterPointer ; public API uses non-const for consistency
int n_gui_get_resize_mode(N_GUI_CTX* ctx) {
    __n_assert(ctx, return 0);
    return ctx->resize_mode;
}

/**
 * @brief Set per-window resize policy (N_GUI_WIN_RESIZE_NONE / _MOVE / _SCALE).
 * Automatically captures normalized coordinates from the window's current
 * position/size relative to the current display size.
 */
void n_gui_window_set_resize_policy(N_GUI_CTX* ctx, int window_id, int policy) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    win->resize_policy = policy;
    _n_gui_window_capture_normalized(ctx, win);
}

/**
 * @brief Get per-window resize policy.
 * @return N_GUI_WIN_RESIZE_NONE, _MOVE, or _SCALE
 */
// cppcheck-suppress constParameterPointer ; public API uses non-const for consistency
int n_gui_window_get_resize_policy(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return 0);
    // cppcheck-suppress constVariablePointer ; returned from non-const API
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return 0;
    return win->resize_policy;
}

/**
 * @brief Recapture normalized coordinates for a window from its current absolute
 * position/size. Call after programmatically moving/resizing a window if you
 * want the new position to be the adaptive reference point.
 */
void n_gui_window_update_normalized(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;

    /* update reference to current display size before recapture */
    ctx->ref_display_w = ctx->display_w;
    ctx->ref_display_h = ctx->display_h;
    _n_gui_window_capture_normalized(ctx, win);
}

/**
 * @brief Move and resize a window at runtime, reflowing its widgets.
 *
 * The adaptive pass only runs on a display resize, so a host that resizes a
 * window itself (a split-pane divider drag, a collapsing panel) had no way to
 * make the widgets inside follow. This applies the new rectangle and rescales
 * every child widget from its normalized coordinates, which every widget
 * captures relative to its window when it is added, whatever the window's
 * resize policy.
 *
 * The window's own normalized geometry is recaptured against the current
 * display size, so a later n_gui_apply_adaptive_resize keeps the proportions
 * the caller just set instead of snapping back to the creation layout. Widget
 * norms are left alone: they already describe the widgets relative to the
 * window and are what this call reads.
 *
 * Helper-owned widget groups do their own math from the window size and cannot
 * see this call: after resizing a window that hosts one, also call
 * n_gui_kvtable_relayout() / n_gui_tab_relayout() / n_gui_sectionlist_relayout().
 *
 * @param ctx the GUI context
 * @param window_id the window to move/resize
 * @param x new window x
 * @param y new window y
 * @param w new window width (clamped to at least 1)
 * @param h new window height (clamped to at least 1)
 */
void n_gui_window_set_rect(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h) {
    n_gui_window_set_rect_axes(ctx, window_id, x, y, w, h, 1, 1);
}

/**
 * @brief n_gui_window_set_rect with per-axis control over the widget reflow.
 *
 * Stretching a window's widgets along both axes is right for a canvas or a
 * body of text, and wrong for a stack of fixed-height rows: a settings page
 * squeezed into a short pane ends up with 9-pixel rows instead of a scrollbar.
 * Passing scale_y = 0 keeps the widgets' vertical layout, so a shorter window
 * simply shows less of the page (and scrolls it, with
 * N_GUI_WIN_AUTO_SCROLLBAR) while its width still tracks the window. That is
 * the same rule n_gui_kvtable applies to its rows.
 *
 * The skipped axis keeps its normalized coordinates as they were, so the
 * widgets stay where the host put them rather than drifting.
 *
 * @param ctx the GUI context
 * @param window_id the window to move/resize
 * @param x new window x
 * @param y new window y
 * @param w new window width (clamped to at least 1)
 * @param h new window height (clamped to at least 1)
 * @param scale_x non-zero to rescale the widgets horizontally
 * @param scale_y non-zero to rescale the widgets vertically
 */
void n_gui_window_set_rect_axes(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int scale_x, int scale_y) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;

    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;

    list_foreach(wgn, win->widgets) {
        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
        if (!wgt) continue;
        if (scale_x) {
            wgt->x = wgt->norm_x * w;
            wgt->w = wgt->norm_w * w;
        } else if (w > 0.0f) {
            /* keep the pixel layout, and keep the norms describing it */
            wgt->norm_x = wgt->x / w;
            wgt->norm_w = wgt->w / w;
        }
        if (scale_y) {
            wgt->y = wgt->norm_y * h;
            wgt->h = wgt->norm_h * h;
        } else if (h > 0.0f) {
            wgt->norm_y = wgt->y / h;
            wgt->norm_h = wgt->h / h;
        }
    }

    if (ctx->display_w > 0.0f && ctx->display_h > 0.0f) {
        win->norm_x = x / ctx->display_w;
        win->norm_y = y / ctx->display_h;
        win->norm_w = w / ctx->display_w;
        win->norm_h = h / ctx->display_h;
    }
    ctx->dirty = 1;
}

/**
 * @brief Apply adaptive resize: reposition/resize all windows according to their
 * policies for the new display dimensions. Called automatically from
 * n_gui_set_display_size() when in ADAPTIVE mode, but can also be called manually.
 */
void n_gui_apply_adaptive_resize(N_GUI_CTX* ctx, float new_w, float new_h) {
    __n_assert(ctx, return);
    if (new_w <= 0 || new_h <= 0) return;

    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        /* a detached window is sized by its own OS window, not by the main
         * display: resizing it here would fight the window manager */
        if (win->native) continue;

        switch (win->resize_policy) {
            case N_GUI_WIN_RESIZE_MOVE:
                win->x = win->norm_x * new_w;
                win->y = win->norm_y * new_h;
                /* size unchanged */
                break;

            case N_GUI_WIN_RESIZE_SCALE: {
                win->x = win->norm_x * new_w;
                win->y = win->norm_y * new_h;
                win->w = win->norm_w * new_w;
                win->h = win->norm_h * new_h;

                /* enforce minimums only on user-resizable (non-frameless) windows */
                if (!(win->flags & N_GUI_WIN_FRAMELESS)) {
                    if (win->w < win->min_w) win->w = win->min_w;
                    if (win->h < win->min_h) win->h = win->min_h;
                }

                /* scale child widgets proportionally to new window size */
                list_foreach(wgn, win->widgets) {
                    N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                    wgt->x = wgt->norm_x * win->w;
                    wgt->y = wgt->norm_y * win->h;
                    wgt->w = wgt->norm_w * win->w;
                    wgt->h = wgt->norm_h * win->h;
                }
                break;
            }

            case N_GUI_WIN_RESIZE_NONE:
            default:
                /* do nothing */
                break;
        }
    }

    /* keep ref_display_w/h at the ORIGINAL creation size so norms remain
       stable across multiple resizes (always relative to initial layout) */
}

/**
 * @brief Set the display (viewport) size for global scrollbar computation.
 * Call on startup and whenever ALLEGRO_EVENT_DISPLAY_RESIZE is received.
 * When virtual canvas is enabled, also recalculates the virtual-to-display transform.
 */
void n_gui_set_display_size(N_GUI_CTX* ctx, float w, float h) {
    __n_assert(ctx, return);
    ctx->display_w = w;
    ctx->display_h = h;
    if (ctx->resize_mode == N_GUI_RESIZE_ADAPTIVE) {
        n_gui_apply_adaptive_resize(ctx, w, h);
    } else {
        if (ctx->virtual_w > 0 && ctx->virtual_h > 0) {
            n_gui_update_transform(ctx);
        }
    }
}

/**
 * @brief Set the global widget shape (round or square GUI).
 * @param ctx GUI context. Ignored when NULL.
 * @param shape_mode N_GUI_SHAPE_ROUNDED for a rounded look, anything else for
 *        rectangular. Shape-aware widgets (the dropmenu button + panel, and the
 *        buttons/comboboxes that consult it) redraw accordingly on the next frame.
 */
void n_gui_set_shape_mode(N_GUI_CTX* ctx, int shape_mode) {
    __n_assert(ctx, return);
    ctx->style.shape_mode = (shape_mode == N_GUI_SHAPE_ROUNDED) ? N_GUI_SHAPE_ROUNDED : N_GUI_SHAPE_RECT;
}

/**
 * @brief Get the global widget shape.
 * @param ctx GUI context.
 * @return N_GUI_SHAPE_ROUNDED or N_GUI_SHAPE_RECT (rounded when @p ctx is NULL).
 */
int n_gui_get_shape_mode(N_GUI_CTX* ctx) {
    return ctx ? ctx->style.shape_mode : N_GUI_SHAPE_ROUNDED;
}

/**
 * @brief Set the display pointer for clipboard operations (copy/paste).
 */
void n_gui_set_display(N_GUI_CTX* ctx, ALLEGRO_DISPLAY* display) {
    __n_assert(ctx, return);
    ctx->display = display;
}

/**
 * @brief Set DPI scale factor manually (default 1.0)
 */
void n_gui_set_dpi_scale(N_GUI_CTX* ctx, float scale) {
    __n_assert(ctx, return);
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 8.0f) scale = 8.0f;
    ctx->dpi_scale = scale;
}

/**
 * @brief Get current DPI scale factor
 */
float n_gui_get_dpi_scale(const N_GUI_CTX* ctx) {
    __n_assert(ctx, return 1.0f);
    return ctx->dpi_scale;
}

/**
 * @brief Detect and apply DPI scale from an Allegro display.
 *
 * Detection strategy (cross-platform):
 * - Compare the physical pixel size (framebuffer) to the logical window size.
 *   On HiDPI displays these differ (e.g. a 1920-logical window may have a
 *   3840-pixel framebuffer on a 2x Retina display).
 * - On Android, al_get_display_option(ALLEGRO_DEFAULT_DISPLAY_ADAPTER) and
 *   the framebuffer/window ratio gives the density scale.
 * - On Windows with per-monitor DPI awareness, the ratio reflects the OS
 *   scaling percentage (125% = 1.25, 150% = 1.5, etc.).
 * - On Linux/X11/Wayland, the ratio picks up Xft.dpi or Wayland scale factor
 *   if Allegro was built with the appropriate backend.
 *
 * If the framebuffer and window sizes are both reported identically (no HiDPI
 * support from the Allegro build), the function falls back to 1.0.
 *
 * @return the detected scale factor (also stored in ctx->dpi_scale)
 */
float n_gui_detect_dpi_scale(N_GUI_CTX* ctx, ALLEGRO_DISPLAY* display) {
    __n_assert(ctx, return 1.0f);
    __n_assert(display, return 1.0f);

    float scale = 1.0f;

    /* Method: compare physical pixel size to logical window size.
     * On HiDPI displays, al_get_display_width returns the logical size while
     * the backbuffer bitmap has the physical pixel size. */
    int logical_w = al_get_display_width(display);
    int logical_h = al_get_display_height(display);
    ALLEGRO_BITMAP* backbuffer = al_get_backbuffer(display);
    if (backbuffer && logical_w > 0 && logical_h > 0) {
        int physical_w = al_get_bitmap_width(backbuffer);
        int physical_h = al_get_bitmap_height(backbuffer);
        float scale_x = (float)physical_w / (float)logical_w;
        float scale_y = (float)physical_h / (float)logical_h;
        scale = (scale_x > scale_y) ? scale_x : scale_y;
        if (scale < 0.5f) scale = 1.0f; /* sanity */
    }

#ifdef ALLEGRO_ANDROID
    /* On Android, use the display DPI option if available.
     * Android standard baseline is 160 DPI; scale = actual_dpi / 160. */
    int dpi = al_get_display_option(display, ALLEGRO_DEFAULT_DISPLAY_ADAPTER);
    if (dpi <= 0) {
        /* fallback: try the pixel ratio we already computed */
    } else {
        float android_scale = (float)dpi / 160.0f;
        if (android_scale >= 0.5f) scale = android_scale;
    }
#endif

    if (scale < 0.25f) scale = 0.25f;
    if (scale > 8.0f) scale = 8.0f;
    ctx->dpi_scale = scale;
    return scale;
}

/* EVENT PROCESSING */

/*! handle slider drag (horizontal or vertical) */
static void _slider_update_from_mouse(N_GUI_WIDGET* wgt, float mx, float my, float win_x, float win_content_y) {
    N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)wgt->data;
    if (wgt->w <= 0) return;
    if (wgt->h <= 0) return;
    double ratio;

    if (sd->orientation == N_GUI_SLIDER_V) {
        float ay = win_content_y + wgt->y;
        float local_y = my - ay;
        /* vertical slider: bottom = max, top = min, so invert */
        ratio = 1.0 - (double)local_y / (double)wgt->h;
    } else {
        float ax = win_x + wgt->x;
        float local_x = mx - ax;
        ratio = (double)local_x / (double)wgt->w;
    }
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    double new_val = sd->min_val + ratio * (sd->max_val - sd->min_val);
    if (sd->step > 0.0)
        new_val = _slider_snap_value(new_val, sd->min_val, sd->max_val, sd->step);
    else
        new_val = _clamp(new_val, sd->min_val, sd->max_val);
    if (new_val != sd->value) {
        sd->value = new_val;
        if (sd->on_change) {
            sd->on_change(wgt->id, sd->value, sd->user_data);
        }
    }
}

/*! handle scrollbar drag */
static void _scrollbar_update_from_mouse(N_GUI_WIDGET* wgt, float mx, float my, float win_x, float win_content_y, const N_GUI_STYLE* style) {
    N_GUI_SCROLLBAR_DATA* sb = (N_GUI_SCROLLBAR_DATA*)wgt->data;
    float ax = win_x + wgt->x;
    float ay = win_content_y + wgt->y;

    double ratio_viewport = sb->viewport_size / sb->content_size;
    if (ratio_viewport > 1.0) ratio_viewport = 1.0;
    double max_scroll = sb->content_size - sb->viewport_size;
    if (max_scroll < 0) max_scroll = 0;

    double pos_ratio;
    if (sb->orientation == N_GUI_SCROLLBAR_V) {
        float thumb_h = (float)(ratio_viewport * (double)wgt->h);
        if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
        float track_range = wgt->h - thumb_h;
        if (track_range <= 0) return;
        pos_ratio = (double)(my - ay - thumb_h / 2.0f) / (double)track_range;
    } else {
        float thumb_w = (float)(ratio_viewport * (double)wgt->w);
        if (thumb_w < style->scrollbar_thumb_min) thumb_w = style->scrollbar_thumb_min;
        float track_range = wgt->w - thumb_w;
        if (track_range <= 0) return;
        pos_ratio = (double)(mx - ax - thumb_w / 2.0f) / (double)track_range;
    }
    if (pos_ratio < 0.0) pos_ratio = 0.0;
    if (pos_ratio > 1.0) pos_ratio = 1.0;
    sb->scroll_pos = pos_ratio * max_scroll;
    if (sb->on_scroll) {
        sb->on_scroll(wgt->id, sb->scroll_pos, sb->user_data);
    }
}

/*! encode a Unicode code point into UTF-8, return the number of bytes written (0 on error) */
static int _utf8_encode(int cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp < 0x110000) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/*! clear the selection (set both anchors to cursor_pos) */
static void _textarea_clear_selection(N_GUI_TEXTAREA_DATA* td) {
    td->sel_start = td->cursor_pos;
    td->sel_end = td->cursor_pos;
}

/*! delete the currently selected text, move cursor to selection start */
static void _textarea_delete_selection(N_GUI_WIDGET* wgt) {
    N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)wgt->data;
    if (!_textarea_has_selection(td)) return;
    size_t lo, hi;
    _textarea_sel_range(td, &lo, &hi);
    size_t del_len = hi - lo;
    memmove(&td->text[lo], &td->text[hi], td->text_len - hi + 1);
    td->text_len -= del_len;
    td->cursor_pos = lo;
    _textarea_clear_selection(td);
    if (td->on_change) td->on_change(wgt->id, td->text, td->user_data);
}

/*! copy selected text to clipboard. Returns 1 on success. */
static int _textarea_copy_to_clipboard(const N_GUI_TEXTAREA_DATA* td, ALLEGRO_DISPLAY* display) {
    if (!display || !_textarea_has_selection(td)) return 0;
    size_t lo, hi;
    _textarea_sel_range(td, &lo, &hi);
    size_t len = hi - lo;
    char* tmp = NULL;
    Malloc(tmp, char, len + 1);
    if (!tmp) return 0;
    memcpy(tmp, &td->text[lo], len);
    tmp[len] = '\0';
    _ngui_clip_set(display, N_CLIPBOARD_CLIPBOARD, tmp);
    FreeNoLog(tmp);
    return 1;
}

/*! insert clipboard text at the cursor, replacing any selection. Shared by Ctrl+V and
 *  middle-click paste. @p clip is NUL-terminated; CR is stripped and, in single-line
 *  mode, newlines are dropped. Respects the char limit and fires on_change. */
static void _textarea_paste_clip(N_GUI_WIDGET* wgt, N_GUI_TEXTAREA_DATA* td, const char* clip) {
    if (!clip) return;
    /* delete selection first if any */
    if (_textarea_has_selection(td)) {
        _textarea_delete_selection(wgt);
    }
    size_t clip_len = strlen(clip);
    /* filter out non-printable except newline in multiline mode;
     * skip \r to normalize CRLF for Allegro5 compatibility */
    char filtered[N_GUI_TEXT_MAX];
    size_t flen = 0;
    for (size_t ci = 0; ci < clip_len && flen < N_GUI_TEXT_MAX - 1; ci++) {
        if (clip[ci] == '\r') {
            continue; /* strip CR, CRLF becomes just LF */
        } else if (clip[ci] == '\n') {
            if (td->multiline) filtered[flen++] = '\n';
        } else if ((unsigned char)clip[ci] >= 32 || ((unsigned char)clip[ci] & 0xC0) == 0x80) {
            filtered[flen++] = clip[ci];
        } else if (((unsigned char)clip[ci] & 0xC0) == 0xC0) {
            filtered[flen++] = clip[ci]; /* UTF-8 lead byte */
        }
    }
    filtered[flen] = '\0';
    if (flen > 0 && td->text_len + flen <= td->char_limit) {
        memmove(&td->text[td->cursor_pos + flen], &td->text[td->cursor_pos], td->text_len - td->cursor_pos + 1);
        memcpy(&td->text[td->cursor_pos], filtered, flen);
        td->cursor_pos += flen;
        td->text_len += flen;
        _textarea_clear_selection(td);
        if (td->on_change) td->on_change(wgt->id, td->text, td->user_data);
    }
}

/*! publish the active text selection (syntax view, label, or focused textarea) to the
 *  X11 PRIMARY selection, implementing select-to-copy: selecting text with the mouse or
 *  keyboard makes it available for a middle-click paste, without disturbing CLIPBOARD.
 *  No-op when no X11 clipboard backend is present (so the CLIPBOARD fallback is never
 *  clobbered by a selection). */
static void _ngui_publish_primary_selection(N_GUI_CTX* ctx) {
    if (!ctx || !_ctx_io_display(ctx) || !n_clipboard_available()) return;
    char* sel = NULL;
    /* a syntax view holds its own selection id */
    if (ctx->selected_syntaxview_id >= 0) {
        sel = n_gui_syntaxview_get_selected_text(ctx, ctx->selected_syntaxview_id);
    }
    /* a label selection */
    if (!sel && ctx->selected_label_id >= 0) {
        N_GUI_WIDGET* lw = n_gui_get_widget(ctx, ctx->selected_label_id);
        if (lw && lw->type == N_GUI_TYPE_LABEL && lw->data) {
            N_GUI_LABEL_DATA* lb = (N_GUI_LABEL_DATA*)lw->data;
            if (lb->sel_start >= 0 && lb->sel_end >= 0 && lb->sel_start != lb->sel_end) {
                int slo = lb->sel_start < lb->sel_end ? lb->sel_start : lb->sel_end;
                int shi = lb->sel_start < lb->sel_end ? lb->sel_end : lb->sel_start;
                size_t tlen = strlen(lb->text);
                if ((size_t)slo > tlen) slo = (int)tlen;
                if ((size_t)shi > tlen) shi = (int)tlen;
                int len = shi - slo;
                if (len > 0) {
                    Malloc(sel, char, (size_t)len + 1);
                    if (sel) {
                        memcpy(sel, &lb->text[slo], (size_t)len);
                        sel[len] = '\0';
                    }
                }
            }
        }
    }
    /* the focused textarea's selection */
    if (!sel && ctx->focused_widget_id >= 0) {
        const N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
        if (fw && fw->type == N_GUI_TYPE_TEXTAREA && fw->data) {
            const N_GUI_TEXTAREA_DATA* td = (const N_GUI_TEXTAREA_DATA*)fw->data;
            if (_textarea_has_selection(td)) {
                size_t lo, hi;
                _textarea_sel_range(td, &lo, &hi);
                size_t len = hi - lo;
                Malloc(sel, char, len + 1);
                if (sel) {
                    memcpy(sel, &td->text[lo], len);
                    sel[len] = '\0';
                }
            }
        }
    }
    if (sel) {
        if (sel[0])
            _ngui_clip_set(_ctx_io_display(ctx), N_CLIPBOARD_PRIMARY, sel);
        Free(sel);
    }
}

/*! handle textarea key input.
 *  Returns 1 if the event was consumed, 0 if it should be passed through
 *  (e.g. ENTER in single-line mode falls through to button keybinds). */
static int _textarea_handle_key(N_GUI_WIDGET* wgt, ALLEGRO_EVENT* ev, ALLEGRO_FONT* font, float pad, float sb_size, N_GUI_CTX* ctx) {
    N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)wgt->data;

    /* reset blink timer so cursor stays visible during typing */
    td->cursor_time = al_get_time();
    /* clear mouse-wheel scroll flag so auto-scroll resumes following the cursor */
    td->scroll_from_wheel = 0;

    int shift = (ev->keyboard.modifiers & ALLEGRO_KEYMOD_SHIFT) ? 1 : 0;
    int ctrl = (ev->keyboard.modifiers & ALLEGRO_KEYMOD_CTRL) ? 1 : 0;

    /* Ctrl+A: select all */
    if (ctrl && ev->keyboard.keycode == ALLEGRO_KEY_A) {
        td->sel_start = 0;
        td->sel_end = td->text_len;
        td->cursor_pos = td->text_len;
        _ngui_publish_primary_selection(ctx); /* select-to-copy: mirror the selection to PRIMARY */
        return 1;
    }

    /* Ctrl+C: copy */
    if (ctrl && ev->keyboard.keycode == ALLEGRO_KEY_C) {
        if (ctx && _ctx_io_display(ctx)) {
            _textarea_copy_to_clipboard(td, _ctx_io_display(ctx));
        }
        return 1;
    }

    /* Ctrl+X: cut */
    if (ctrl && ev->keyboard.keycode == ALLEGRO_KEY_X) {
        if (ctx && _ctx_io_display(ctx) && _textarea_has_selection(td)) {
            _textarea_copy_to_clipboard(td, _ctx_io_display(ctx));
            _textarea_delete_selection(wgt);
        }
        return 1;
    }

    /* Ctrl+V: paste */
    if (ctrl && ev->keyboard.keycode == ALLEGRO_KEY_V) {
        if (ctx && _ctx_io_display(ctx)) {
            char* clip = _ngui_clip_get(_ctx_io_display(ctx), N_CLIPBOARD_CLIPBOARD);
            if (clip) {
                _textarea_paste_clip(wgt, td, clip);
                al_free(clip);
            }
        }
        return 1;
    }

    if (ev->keyboard.keycode == ALLEGRO_KEY_BACKSPACE) {
        if (_textarea_has_selection(td)) {
            _textarea_delete_selection(wgt);
        } else if (td->cursor_pos > 0 && td->text_len > 0) {
            size_t erase_start = td->cursor_pos;
            int cont_count = 0;
            do {
                erase_start--;
                cont_count++;
            } while (erase_start > 0 && cont_count <= 3 && ((unsigned char)td->text[erase_start] & 0xC0) == 0x80);
            size_t erase_len = td->cursor_pos - erase_start;
            memmove(&td->text[erase_start], &td->text[td->cursor_pos], td->text_len - td->cursor_pos + 1);
            td->cursor_pos = erase_start;
            td->text_len -= erase_len;
            _textarea_clear_selection(td);
            if (td->on_change) td->on_change(wgt->id, td->text, td->user_data);
        }
        return 1;
    }
    if (ev->keyboard.keycode == ALLEGRO_KEY_DELETE) {
        if (_textarea_has_selection(td)) {
            _textarea_delete_selection(wgt);
        } else if (td->cursor_pos < td->text_len) {
            int clen = _utf8_char_len((unsigned char)td->text[td->cursor_pos]);
            if (td->cursor_pos + (size_t)clen > td->text_len) clen = (int)(td->text_len - td->cursor_pos);
            memmove(&td->text[td->cursor_pos], &td->text[td->cursor_pos + (size_t)clen], td->text_len - td->cursor_pos - (size_t)clen + 1);
            td->text_len -= (size_t)clen;
            _textarea_clear_selection(td);
            if (td->on_change) td->on_change(wgt->id, td->text, td->user_data);
        }
        return 1;
    }
    if (ev->keyboard.keycode == ALLEGRO_KEY_LEFT) {
        if (!shift && _textarea_has_selection(td)) {
            /* collapse selection to left edge */
            size_t lo, hi;
            _textarea_sel_range(td, &lo, &hi);
            td->cursor_pos = lo;
            _textarea_clear_selection(td);
        } else if (td->cursor_pos > 0) {
            if (shift && !_textarea_has_selection(td)) {
                td->sel_start = td->cursor_pos;
            }
            int skip = 0;
            do {
                td->cursor_pos--;
            } while (skip++ < 3 && td->cursor_pos > 0 && ((unsigned char)td->text[td->cursor_pos] & 0xC0) == 0x80);
            if (shift)
                td->sel_end = td->cursor_pos;
            else
                _textarea_clear_selection(td);
        }
        return 1;
    }
    if (ev->keyboard.keycode == ALLEGRO_KEY_RIGHT) {
        if (!shift && _textarea_has_selection(td)) {
            /* collapse selection to right edge */
            size_t lo, hi;
            _textarea_sel_range(td, &lo, &hi);
            td->cursor_pos = hi;
            _textarea_clear_selection(td);
        } else if (td->cursor_pos < td->text_len) {
            if (shift && !_textarea_has_selection(td)) {
                td->sel_start = td->cursor_pos;
            }
            int clen = _utf8_char_len((unsigned char)td->text[td->cursor_pos]);
            td->cursor_pos += (size_t)clen;
            if (td->cursor_pos > td->text_len) td->cursor_pos = td->text_len;
            if (shift)
                td->sel_end = td->cursor_pos;
            else
                _textarea_clear_selection(td);
        }
        return 1;
    }
    if (ev->keyboard.keycode == ALLEGRO_KEY_HOME) {
        if (shift && !_textarea_has_selection(td)) {
            td->sel_start = td->cursor_pos;
        }
        td->cursor_pos = 0;
        if (shift)
            td->sel_end = td->cursor_pos;
        else
            _textarea_clear_selection(td);
        return 1;
    }
    if (ev->keyboard.keycode == ALLEGRO_KEY_END) {
        if (shift && !_textarea_has_selection(td)) {
            td->sel_start = td->cursor_pos;
        }
        td->cursor_pos = td->text_len;
        if (shift)
            td->sel_end = td->cursor_pos;
        else
            _textarea_clear_selection(td);
        return 1;
    }

    /* UP/DOWN arrow keys: move cursor to previous/next visual line in multiline mode */
    if (ev->keyboard.keycode == ALLEGRO_KEY_UP || ev->keyboard.keycode == ALLEGRO_KEY_DOWN) {
        if (!td->multiline || !font) return 1;
        if (shift && !_textarea_has_selection(td)) {
            td->sel_start = td->cursor_pos;
        }

        /* compute wrap width consistently with rendering: subtract scrollbar
         * width when content overflows, then subtract padding. */
        float base_wrap_w = wgt->w - pad * 2;
        float text_area_h = wgt->h - pad * 2;
        float content_h = _textarea_content_height(td, font, wgt->w, pad);
        float wrap_w = (content_h > text_area_h)
                           ? (wgt->w - sb_size - pad * 2)
                           : base_wrap_w;

        /* pass 1: find the cursor's visual line number and x offset */
        float cx = 0;
        int line = 0;
        int cursor_line = 0;
        float cursor_x = 0;

        for (size_t i = 0; i < td->text_len;) {
            if (i == td->cursor_pos) {
                cursor_line = line;
                cursor_x = cx;
            }
            if (td->text[i] == '\n') {
                cx = 0;
                line++;
                i++;
                continue;
            }
            int clen = _utf8_char_len((unsigned char)td->text[i]);
            if (i + (size_t)clen > td->text_len) clen = (int)(td->text_len - i);
            char ch[5];
            memcpy(ch, &td->text[i], (size_t)clen);
            ch[clen] = '\0';
            float cw = _text_w(font, ch);
            if (cx + cw > wrap_w) {
                cx = 0;
                line++;
                if (i == td->cursor_pos) {
                    cursor_line = line;
                    cursor_x = cx;
                }
            }
            cx += cw;
            i += (size_t)clen;
        }
        if (td->cursor_pos >= td->text_len) {
            cursor_line = line;
            cursor_x = cx;
        }
        int total_lines = line;

        /* determine target line */
        int target_line;
        if (ev->keyboard.keycode == ALLEGRO_KEY_UP) {
            if (cursor_line <= 0) {
                td->cursor_pos = 0;
                return 1;
            }
            target_line = cursor_line - 1;
        } else {
            if (cursor_line >= total_lines) {
                td->cursor_pos = td->text_len;
                return 1;
            }
            target_line = cursor_line + 1;
        }

        /* pass 2: find the byte position on target_line closest to cursor_x */
        cx = 0;
        line = 0;
        size_t best_pos = 0;
        float best_dist = 1e9f;

        /* check position 0 */
        if (line == target_line) {
            best_dist = (cursor_x >= 0) ? cursor_x : -cursor_x;
            best_pos = 0;
        }

        for (size_t i = 0; i < td->text_len;) {
            if (td->text[i] == '\n') {
                if (line >= target_line) break;
                cx = 0;
                line++;
                if (line == target_line) {
                    float dist = (cursor_x >= cx) ? (cursor_x - cx) : (cx - cursor_x);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_pos = i + 1;
                    }
                }
                i++;
                continue;
            }
            int clen = _utf8_char_len((unsigned char)td->text[i]);
            if (i + (size_t)clen > td->text_len) clen = (int)(td->text_len - i);
            char ch[5];
            memcpy(ch, &td->text[i], (size_t)clen);
            ch[clen] = '\0';
            float cw = _text_w(font, ch);
            if (cx + cw > wrap_w) {
                if (line >= target_line) break;
                cx = 0;
                line++;
                if (line == target_line) {
                    float dist = (cursor_x >= cx) ? (cursor_x - cx) : (cx - cursor_x);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_pos = i;
                    }
                }
            }
            cx += cw;
            /* check position after this character */
            if (line == target_line) {
                float dist = (cursor_x >= cx) ? (cursor_x - cx) : (cx - cursor_x);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_pos = i + (size_t)clen;
                    if (best_pos > td->text_len) best_pos = td->text_len;
                }
            }
            i += (size_t)clen;
        }

        td->cursor_pos = best_pos;
        if (shift)
            td->sel_end = td->cursor_pos;
        else
            _textarea_clear_selection(td);
        return 1;
    }

    if (ev->keyboard.keycode == ALLEGRO_KEY_ENTER) {
        if (td->multiline) {
            if (_textarea_has_selection(td)) {
                _textarea_delete_selection(wgt);
            }
            if (td->text_len < td->char_limit) {
                memmove(&td->text[td->cursor_pos + 1], &td->text[td->cursor_pos], td->text_len - td->cursor_pos + 1);
                td->text[td->cursor_pos] = '\n';
                td->cursor_pos++;
                td->text_len++;
                _textarea_clear_selection(td);
                if (td->on_change) td->on_change(wgt->id, td->text, td->user_data);
            }
        }
        /* in single-line mode, let ENTER pass through to button keybinds */
        return td->multiline ? 1 : 0;
    }

    /* printable character: encode Unicode code point as UTF-8 */
    if (ev->keyboard.unichar >= 32) {
        /* delete selection first */
        if (_textarea_has_selection(td)) {
            _textarea_delete_selection(wgt);
        }
        char utf8[4];
        int utf8_len = _utf8_encode(ev->keyboard.unichar, utf8);
        if (utf8_len > 0 && td->text_len + (size_t)utf8_len <= td->char_limit) {
            memmove(&td->text[td->cursor_pos + (size_t)utf8_len], &td->text[td->cursor_pos], td->text_len - td->cursor_pos + 1);
            memcpy(&td->text[td->cursor_pos], utf8, (size_t)utf8_len);
            td->cursor_pos += (size_t)utf8_len;
            td->text_len += (size_t)utf8_len;
            _textarea_clear_selection(td);
            if (td->on_change) td->on_change(wgt->id, td->text, td->user_data);
        }
    }
    return 1;
}

/**
 * @brief Dispatch a titlebar button action, invoke the user callback if set,
 *        otherwise fall back to the built-in minimize / maximize / close behaviour.
 * @param ctx the GUI context (used for the default close/min/max paths)
 * @param win the target window
 * @param btn_type one of N_GUI_TB_BTN_MINIMIZE, _MAXIMIZE, _CLOSE
 */
static void _tb_button_action(N_GUI_CTX* ctx, N_GUI_WINDOW* win, int btn_type) {
    /* An own-chrome window draws these buttons instead of the window manager, so
     * closing has to take the OS window with it: otherwise the content is hidden and
     * an undismissable OS window is left on screen. Minimise and maximise already
     * mirror themselves onto the native window (see _native_apply_minimised). */
    int own_native = (win->native != NULL) && _native_own_chrome(win);

    if (btn_type == N_GUI_TB_BTN_MINIMIZE) {
        if (win->tb_buttons.on_minimize)
            win->tb_buttons.on_minimize(win->id, win->tb_buttons.on_minimize_user_data);
        else
            n_gui_minimize_window(ctx, win->id); /* mirrors onto the OS window itself */
    } else if (btn_type == N_GUI_TB_BTN_MAXIMIZE) {
        if (win->tb_buttons.on_maximize)
            win->tb_buttons.on_maximize(win->id, win->tb_buttons.on_maximize_user_data);
        else
            n_gui_maximize_window(ctx, win->id);
    } else if (btn_type == N_GUI_TB_BTN_CLOSE) {
        if (win->tb_buttons.on_close)
            win->tb_buttons.on_close(win->id, win->tb_buttons.on_close_user_data);
        else
            n_gui_close_window(ctx, win->id);
        /* Release the OS window too, on the same deferred path a window-manager
         * close request takes. Only when the window actually ended up closed, so a
         * close callback that keeps it open (a confirmation, say) still wins. */
        if (own_native && !(win->state & N_GUI_WIN_OPEN))
            win->native_close_pending = 1;
    }
}

/*! helper: move an open list panel's highlight cursor (combobox / dropmenu) by
 *  @p delta items and scroll the visible window so the highlighted item stays in
 *  view. Shared by the wheel and the Up/Down keys so both drive the same cursor.
 *  @param highlight  in/out highlight index (-1 = none; seeded from @p fallback)
 *  @param scroll     in/out first-visible-item offset
 *  @param nb         total item count
 *  @param max_visible number of items the panel shows at once
 *  @param delta      signed step (negative = up, positive = down)
 *  @param fallback   index to start from when @p highlight is -1 (e.g. the selected
 *                    item, or the first visible item) */
static void _list_panel_highlight_step(int* highlight, int* scroll, int nb, int max_visible, int delta, int fallback) {
    int hi;
    if (!highlight || !scroll || nb <= 0)
        return;
    hi = *highlight;
    if (hi < 0) {
        /* nothing highlighted yet: seed so the first step lands on the natural item
           (a downward step -> the first item, an upward step -> the last, or the
           given fallback such as the combobox's selected value) */
        if (fallback >= 0)
            hi = fallback;
        else
            hi = (delta >= 0) ? -1 : nb;
    }
    hi += delta;
    if (hi < 0)
        hi = 0;
    if (hi >= nb)
        hi = nb - 1;
    *highlight = hi;
    /* keep the highlighted item within the visible window */
    if (hi < *scroll)
        *scroll = hi;
    else if (max_visible > 0 && hi >= *scroll + max_visible)
        *scroll = hi - max_visible + 1;
    if (*scroll < 0)
        *scroll = 0;
}

/*! Whether the pointer at (px,py) rests on a datagrid column-resize border: the
 *  header row of a visible datagrid, within a few pixels of a visible column's right
 *  edge. Mirrors the hit test that starts a resize drag on mouse-down, so the caller
 *  can show a horizontal-resize cursor there for discoverability. The topmost datagrid
 *  under the point wins (windows are ordered back to front). */
static int _datagrid_resize_hover(N_GUI_CTX* ctx, float px, float py) {
    int over = 0;
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        float ox, oy;
        if (!win || !(win->state & N_GUI_WIN_OPEN)) continue;
        if (!_win_on_pass(ctx, win)) continue;
        ox = win->x - win->scroll_x;
        oy = win->y + _win_tbh(win) - win->scroll_y;
        list_foreach(wgn, win->widgets) {
            const N_GUI_WIDGET* wgt = (const N_GUI_WIDGET*)wgn->ptr;
            const N_GUI_DATAGRID_DATA* gd;
            ALLEGRO_FONT* font;
            float ax, ay, fh, row_h;
            if (!wgt || !wgt->visible || wgt->type != N_GUI_TYPE_DATAGRID || !wgt->data) continue;
            ax = ox + wgt->x;
            ay = oy + wgt->y;
            if (!_point_in_rect(px, py, ax, ay, wgt->w, wgt->h)) continue;
            gd = (const N_GUI_DATAGRID_DATA*)wgt->data;
            font = wgt->font ? wgt->font : ctx->default_font;
            fh = font ? (float)al_get_font_line_height(font) : 16.0f;
            row_h = fh + ctx->style.item_height_pad;
            /* this (topmost so far) grid claims the point: its verdict overrides a
               lower grid's, whether or not the point lands on a border */
            over = 0;
            if (py < ay + row_h) { /* header row only */
                float bx = ax - gd->h_scroll;
                for (size_t c = 0; c < gd->nb_cols; c++) {
                    size_t pc = _datagrid_phys(gd, c);
                    float right;
                    if (!gd->cols[pc].visible) continue;
                    right = bx + gd->cols[pc].width;
                    if (px >= right - 4.0f && px <= right + 4.0f) {
                        over = 1;
                        break;
                    }
                    bx = right;
                }
            }
        }
    }
    return over;
}

/*! report the split-pane divider under a point, for the mouse cursor shape:
 *  0 = none, 1 = vertical divider (drags left/right), 2 = horizontal divider
 *  (drags up/down). Windows are walked back to front and widgets bottom to
 *  top, so the topmost claimant wins. A split pane off its band is
 *  click-through and therefore does not hide a divider beneath it, while any
 *  other widget under the point does take the press and clears the verdict. */
static int _splitpane_cursor_hover(N_GUI_CTX* ctx, float px, float py) {
    int want = 0;
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        float ox, oy, win_h;
        if (!win || !(win->state & N_GUI_WIN_OPEN)) continue;
        if (!_win_on_pass(ctx, win)) continue;
        win_h = (win->state & N_GUI_WIN_MINIMISED) ? _win_tbh(win) : win->h;
        if (!_point_in_rect(px, py, win->x, win->y, win->w, win_h)) continue;
        ox = win->x - win->scroll_x;
        oy = win->y + _win_tbh(win) - win->scroll_y;
        /* a window covering the point hides whatever is below it */
        want = 0;
        list_foreach(wgn, win->widgets) {
            const N_GUI_WIDGET* wgt = (const N_GUI_WIDGET*)wgn->ptr;
            float ax, ay;
            if (!wgt || !wgt->visible || !wgt->enabled) continue;
            ax = ox + wgt->x;
            ay = oy + wgt->y;
            if (!_point_in_rect(px, py, ax, ay, wgt->w, wgt->h)) continue;
            if (wgt->type != N_GUI_TYPE_SPLITPANE) {
                want = 0;
            } else if (_splitpane_on_divider(wgt, px, py, ax, ay)) {
                want = (((const N_GUI_SPLITPANE_DATA*)wgt->data)->orientation == N_GUI_SPLIT_VERTICAL) ? 1 : 2;
            }
        }
    }
    return want;
}

/**
 * @brief Process an allegro event through the GUI system
 * @param ctx the GUI context
 * @param event the allegro event to process
 * @return 1 if the event was consumed by the GUI (mouse over a window/widget,
 *         active capture operation, keyboard input to focused widget, etc.),
 *         0 if the event was not handled by the GUI and can be processed by
 *         the application (e.g. game logic).
 */
int n_gui_process_event(N_GUI_CTX* ctx, ALLEGRO_EVENT event) {
    __n_assert(ctx, return 0);

    int event_consumed = 0;

    /* Display routing. Resolve which pass this event belongs to: a detached
     * window's display, or the main one (NULL, which also covers events that
     * name no display and the headless tests that synthesize events by hand).
     * Windows not on the pass are skipped by every hit test below, so a click
     * on a native window cannot reach a pop-up that happens to sit at the same
     * coordinates on the main display. Allegro already delivers mouse
     * coordinates relative to the display the event came from, and a detached
     * window sits at (0,0) in its own display, so the coordinate maths in the
     * hit tests is the same for both kinds of pass. */
    {
        ALLEGRO_DISPLAY* src = _event_display(&event);
        ctx->pass_display = NULL;
        if (src && src != ctx->display) {
            list_foreach(pnode, ctx->windows) {
                const N_GUI_WINDOW* pwin = (const N_GUI_WINDOW*)pnode->ptr;
                if (pwin && pwin->native == src) {
                    ctx->pass_display = src;
                    break;
                }
            }
        }
        /* the display that owns the input drives clipboard and cursor calls */
        if (src) ctx->active_display = src;
    }

    /* Redraw bookkeeping for n_gui_needs_redraw: these event types can change
     * what is drawn (a click/keystroke changes hover, focus, selection, scroll,
     * or a widget value through a callback; a display resize/expose changes
     * geometry). Motion is handled in its own block below, where the
     * over-window test is already computed, so idle movement over the game
     * background does not force a redraw. */
    switch (event.type) {
        case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
        case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
        case ALLEGRO_EVENT_KEY_DOWN:
        case ALLEGRO_EVENT_KEY_CHAR:
        case ALLEGRO_EVENT_KEY_UP:
        case ALLEGRO_EVENT_MOUSE_WARPED:
        case ALLEGRO_EVENT_DISPLAY_RESIZE:
        case ALLEGRO_EVENT_DISPLAY_EXPOSE:
        case ALLEGRO_EVENT_DISPLAY_CLOSE:
        case ALLEGRO_EVENT_DISPLAY_SWITCH_IN:
        case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
        case ALLEGRO_EVENT_DISPLAY_HALT_DRAWING:
        case ALLEGRO_EVENT_DISPLAY_RESUME_DRAWING:
            ctx->dirty = 1;
            break;
        default:
            break;
    }

    /* Native window events. The window manager owns a detached window's frame,
     * so a close request, a resize and an acknowledge all arrive here rather
     * than through our own title bar buttons. */
    if (ctx->pass_display) {
        N_GUI_WINDOW* nwin = NULL;
        list_foreach(nnode, ctx->windows) {
            N_GUI_WINDOW* w = (N_GUI_WINDOW*)nnode->ptr;
            if (w && w->native == ctx->pass_display) {
                nwin = w;
                break;
            }
        }
        if (nwin && event.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
            /* Same contract as the pop-up close button: a caller-supplied
             * callback decides what happens, the default is to close the
             * window. Either way the display itself is released at the next
             * n_gui_draw_detached, never from inside event processing. */
            if (nwin->tb_buttons.on_close) {
                nwin->tb_buttons.on_close(nwin->id, nwin->tb_buttons.on_close_user_data);
                /* the callback may have closed, re-attached or destroyed
                 * nothing at all: only force the release when it left the
                 * window both open and detached */
                if (nwin->native && (nwin->state & N_GUI_WIN_OPEN)) {
                    nwin->state &= ~N_GUI_WIN_OPEN;
                    nwin->native_close_pending = 1;
                }
            } else {
                nwin->state &= ~N_GUI_WIN_OPEN;
                nwin->native_close_pending = 1;
            }
            ctx->pass_display = NULL;
            return 1;
        }
        /* Drawing halt / resume. Mandatory on Android (and harmless elsewhere):
         * the OS takes the surface away and Allegro requires the acknowledge
         * before it will release it. Drawing into a halted display is undefined,
         * so n_gui_draw_detached skips the window until it resumes. */
        if (nwin && event.type == ALLEGRO_EVENT_DISPLAY_HALT_DRAWING) {
            nwin->native_halted = 1;
            al_acknowledge_drawing_halt(nwin->native);
            ctx->pass_display = NULL;
            return 1;
        }
        if (nwin && event.type == ALLEGRO_EVENT_DISPLAY_RESUME_DRAWING) {
            al_acknowledge_drawing_resume(nwin->native);
            nwin->native_halted = 0;
            ctx->pass_display = NULL;
            return 1;
        }
        if (nwin && event.type == ALLEGRO_EVENT_DISPLAY_RESIZE) {
            al_acknowledge_resize(nwin->native);
            float new_w = (float)al_get_display_width(nwin->native);
            float new_h = (float)al_get_display_height(nwin->native);
            /* A minimised own-chrome window is deliberately shrunk to its title
             * bar, so this resize does not describe the window's real size.
             * Take the width but leave the height alone, it is what the window
             * grows back to (see _native_apply_minimised). */
            if ((nwin->state & N_GUI_WIN_MINIMISED) && _native_own_chrome(nwin)) {
                if (new_w > 0.0f) {
                    nwin->w = new_w;
                    nwin->native_w = new_w;
                }
                ctx->pass_display = NULL;
                return 1;
            }
            if (new_w > 0.0f && new_h > 0.0f) {
                if ((nwin->detach_flags & N_GUI_DETACH_SCALE_CONTENT) && nwin->w > 0.0f && nwin->h > 0.0f) {
                    list_foreach(wgn, nwin->widgets) {
                        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                        if (!wgt) continue;
                        wgt->x = wgt->norm_x * new_w;
                        wgt->y = wgt->norm_y * new_h;
                        wgt->w = wgt->norm_w * new_w;
                        wgt->h = wgt->norm_h * new_h;
                    }
                }
                nwin->w = new_w;
                nwin->h = new_h;
                nwin->native_w = new_w;
                nwin->native_h = new_h;
            }
            ctx->pass_display = NULL;
            return 1;
        }
    }

    /* When the display loses focus (e.g. OS window is moved to another monitor via the
     * title bar, or another window steals focus), examples typically call
     * al_flush_event_queue() which discards any pending MOUSE_BUTTON_UP events.
     * Without resetting our state here, the GUI would keep its drag/resize flags set
     * and continue moving/resizing windows on the next MOUSE_AXES event, which is the
     * root cause of GUI windows being unintentionally resized when the OS window is
     * moved between monitors. Scoped to the pass so switching away from a native
     * window does not cancel a drag in progress on the main display. */
    if (event.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT) {
        ctx->mouse_b1 = 0;
        ctx->mouse_b1_prev = 0;
        list_foreach(wnode, ctx->windows) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            if (!_win_on_pass(ctx, win)) continue;
            win->state &= ~(N_GUI_WIN_DRAGGING | N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG);
            win->native_drag_anchored = 0;
            win->tb_buttons.pressed = N_GUI_TB_BTN_NONE;
            win->tb_buttons.hovered = N_GUI_TB_BTN_NONE;
        }
        ctx->global_vscroll_drag = 0;
        ctx->global_hscroll_drag = 0;
        ctx->pass_display = NULL;
        return 0;
    }

    /* track mouse */
    if (event.type == ALLEGRO_EVENT_MOUSE_AXES) {
        ctx->mouse_x = event.mouse.x;
        ctx->mouse_y = event.mouse.y;
        /* Over-window test, computed once and shared by the tooltip and
         * datagrid-cursor scans below. Both scans only ever match widgets that
         * live inside a window, so when the pointer is over empty background
         * (the common case for a GUI composited over a game, at the mouse
         * sample rate) they would each walk every widget just to return
         * "nothing". Skipping them off-window is the same result for O(windows)
         * instead of O(widgets). Only needed when no button is held (both scans
         * are already gated on that). */
        int over_win = (!ctx->mouse_b1) ? _pointer_over_window(ctx, (float)ctx->mouse_x, (float)ctx->mouse_y) : 0;
        /* redraw bookkeeping: a motion changes what is drawn only when it is
         * over a window (hover / tooltip / cursor), during a drag, on a wheel
         * tick, or when it just left the GUI (the hover state must be cleared
         * one last time). Motion over the game background otherwise leaves the
         * GUI unchanged, so an idle event-driven host is not forced to redraw. */
        if (over_win || ctx->mouse_b1 || ctx->prev_over_win || event.mouse.dz != 0 || event.mouse.dw != 0)
            ctx->dirty = 1;
        if (!ctx->mouse_b1) ctx->prev_over_win = over_win;
        /* tooltip arming: track the hovered tooltip widget; any movement
         * re-arms the delay so the bubble shows once the pointer rests.
         * Disabled hosts (style.tooltip_delay <= 0, e.g. touch screens where
         * the emulated cursor parks on the last tap forever) and held-button
         * drags skip the whole widget scan - it would run at the touch
         * sample rate in the middle of a stroke for nothing */
        if (ctx->style.tooltip_delay > 0.0f && !ctx->mouse_b1) {
            int tip_id = over_win ? _tooltip_widget_at(ctx, (float)ctx->mouse_x, (float)ctx->mouse_y) : -1;
            int moved = (abs(ctx->mouse_x - ctx->tooltip_anchor_x) > 2) || (abs(ctx->mouse_y - ctx->tooltip_anchor_y) > 2);
            if (tip_id != ctx->tooltip_widget_id || moved) {
                ctx->tooltip_widget_id = tip_id;
                ctx->tooltip_armed_at = al_get_time();
                ctx->tooltip_anchor_x = ctx->mouse_x;
                ctx->tooltip_anchor_y = ctx->mouse_y;
                /* keep needs_redraw true until the bubble is due to appear, so an
                 * idle host still draws the reveal frame once the pointer rests */
                if (tip_id >= 0) {
                    double due = ctx->tooltip_armed_at + (double)ctx->style.tooltip_delay + N_GUI_ANIM_TAIL_SEC;
                    if (due > ctx->anim_until) ctx->anim_until = due;
                }
            }
        } else if (ctx->tooltip_widget_id >= 0) {
            ctx->tooltip_widget_id = -1;
        }
        /* discoverability: show a resize cursor while the pointer rests where a drag
           would resize something -- a datagrid column border, or a split pane divider
           (left/right for a vertical divider, up/down for a horizontal one) -- and the
           default cursor elsewhere. Skipped while a button is held so a resize/scroll
           drag keeps the cursor it started with. Only the applied shape is re-set, to
           avoid churn. */
        if (_ctx_io_display(ctx) && !ctx->mouse_b1) {
            int want = ALLEGRO_SYSTEM_MOUSE_CURSOR_DEFAULT;
            if (over_win) {
                int divider = _splitpane_cursor_hover(ctx, (float)ctx->mouse_x, (float)ctx->mouse_y);
                if (divider == 1) {
                    want = ALLEGRO_SYSTEM_MOUSE_CURSOR_RESIZE_E;
                } else if (divider == 2) {
                    want = ALLEGRO_SYSTEM_MOUSE_CURSOR_RESIZE_N;
                } else if (_datagrid_resize_hover(ctx, (float)ctx->mouse_x, (float)ctx->mouse_y)) {
                    want = ALLEGRO_SYSTEM_MOUSE_CURSOR_RESIZE_E;
                }
            }
            if (want != ctx->cursor_shape) {
                al_set_system_mouse_cursor(_ctx_io_display(ctx), (ALLEGRO_SYSTEM_MOUSE_CURSOR)want);
                ctx->cursor_shape = want;
            }
        }
    }
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && event.mouse.button == 1) {
        ctx->tooltip_widget_id = -1; /* a click dismisses the tooltip */
        ctx->mouse_b1_prev = ctx->mouse_b1;
        ctx->mouse_b1 = 1;
        ctx->mouse_x = event.mouse.x;
        ctx->mouse_y = event.mouse.y;
    }
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP && event.mouse.button == 1) {
        ctx->mouse_b1_prev = ctx->mouse_b1;
        ctx->mouse_b1 = 0;
        /* end a column-resize drag: notify the host so it can persist the layout */
        if (ctx->scrollbar_drag_widget_id >= 0) {
            N_GUI_WIDGET* up_wgt = n_gui_get_widget(ctx, ctx->scrollbar_drag_widget_id);
            if (up_wgt && up_wgt->type == N_GUI_TYPE_LISTBOX && up_wgt->data) {
                ((N_GUI_LISTBOX_DATA*)up_wgt->data)->h_scroll_dragging = 0;
            }
            if (up_wgt && up_wgt->type == N_GUI_TYPE_SYNTAXVIEW && up_wgt->data) {
                ((N_GUI_SYNTAXVIEW_DATA*)up_wgt->data)->h_scroll_dragging = 0;
            }
            if (up_wgt && up_wgt->type == N_GUI_TYPE_DATAGRID && up_wgt->data) {
                N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)up_wgt->data;
                gd->h_scroll_dragging = 0; /* end any horizontal-scrollbar drag */
                if (gd->col_resize_col >= 0) {
                    gd->col_resize_col = -1;
                    if (gd->on_columns_changed)
                        gd->on_columns_changed(up_wgt->id, gd->user_data);
                }
            }
        }
        ctx->scrollbar_drag_widget_id = -1;
    }
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && event.mouse.button == 2) {
        /* right-click: keep the cursor position current so the context-menu hit
           test below works even with no preceding MOUSE_AXES event */
        ctx->tooltip_widget_id = -1;
        ctx->mouse_x = event.mouse.x;
        ctx->mouse_y = event.mouse.y;
    }

    /* Unified widget scrollbar drag: update scroll position while mouse held */
    if (ctx->scrollbar_drag_widget_id >= 0 && ctx->mouse_b1 &&
        (event.type == ALLEGRO_EVENT_MOUSE_AXES || event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN)) {
        N_GUI_WIDGET* drag_wgt = n_gui_get_widget(ctx, ctx->scrollbar_drag_widget_id);
        if (drag_wgt && drag_wgt->data) {
            float d_ox = 0, d_oy = 0;
            const N_GUI_WINDOW* d_win = _find_widget_window(ctx, drag_wgt->id, &d_ox, &d_oy);
            if (d_win) {
                ALLEGRO_FONT* d_font = drag_wgt->font ? drag_wgt->font : ctx->default_font;
                float d_fh = d_font ? (float)al_get_font_line_height(d_font) : 16.0f;
                float d_my = (float)ctx->mouse_y;
                float d_mx = (float)ctx->mouse_x;
                switch (drag_wgt->type) {
                    case N_GUI_TYPE_COMBOBOX: {
                        N_GUI_COMBOBOX_DATA* cbd = (N_GUI_COMBOBOX_DATA*)drag_wgt->data;
                        if (cbd->is_open && (int)cbd->nb_items > cbd->max_visible) {
                            float ih = cbd->item_height > d_fh ? cbd->item_height : d_fh + ctx->style.item_height_pad;
                            float d_ay = d_oy + drag_wgt->y + drag_wgt->h;
                            float dd_h = (float)cbd->max_visible * ih;
                            cbd->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, dd_h, cbd->max_visible, (int)cbd->nb_items, ctx->style.scrollbar_thumb_min);
                        }
                        break;
                    }
                    case N_GUI_TYPE_DROPMENU: {
                        N_GUI_DROPMENU_DATA* dmd = (N_GUI_DROPMENU_DATA*)drag_wgt->data;
                        if (dmd->is_open && (int)dmd->nb_entries > dmd->max_visible) {
                            float ih = dmd->item_height > d_fh ? dmd->item_height : d_fh + ctx->style.item_height_pad;
                            float d_ay = d_oy + drag_wgt->y + drag_wgt->h;
                            float dd_h = (float)dmd->max_visible * ih;
                            dmd->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, dd_h, dmd->max_visible, (int)dmd->nb_entries, ctx->style.scrollbar_thumb_min);
                        }
                        break;
                    }
                    case N_GUI_TYPE_LISTBOX: {
                        N_GUI_LISTBOX_DATA* lbd = (N_GUI_LISTBOX_DATA*)drag_wgt->data;
                        float d_ax = d_ox + drag_wgt->x;
                        float d_ay = d_oy + drag_wgt->y;
                        float lb_content_w = 0.0f, lb_pane_w = drag_wgt->w;
                        int lb_need_hsb = 0, visible = 0;
                        _listbox_hmetrics(drag_wgt, lbd, d_font, &ctx->style, &lb_content_w, &lb_pane_w, &lb_need_hsb, &visible);
                        if (lbd->h_scroll_dragging) {
                            lbd->h_scroll = _scrollbar_calc_scroll(d_mx, d_ax, lb_pane_w, lb_pane_w, lb_content_w, ctx->style.scrollbar_thumb_min);
                            _listbox_clamp_h(lbd, lb_content_w, lb_pane_w);
                        } else {
                            float rows_h = drag_wgt->h - (lb_need_hsb ? ctx->style.scrollbar_size : 0.0f);
                            lbd->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, rows_h, visible, (int)lbd->nb_items, ctx->style.scrollbar_thumb_min);
                        }
                        break;
                    }
                    case N_GUI_TYPE_RADIOLIST: {
                        N_GUI_RADIOLIST_DATA* rld = (N_GUI_RADIOLIST_DATA*)drag_wgt->data;
                        float ih = rld->item_height > d_fh ? rld->item_height : d_fh + ctx->style.item_height_pad;
                        float d_ay = d_oy + drag_wgt->y;
                        int visible = (int)(drag_wgt->h / ih);
                        rld->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, drag_wgt->h, visible, (int)rld->nb_items, ctx->style.scrollbar_thumb_min);
                        break;
                    }
                    case N_GUI_TYPE_TEXTAREA: {
                        N_GUI_TEXTAREA_DATA* tatd = (N_GUI_TEXTAREA_DATA*)drag_wgt->data;
                        if (tatd->multiline) {
                            float ta_pad = ctx->style.textarea_padding;
                            float ta_view_h = drag_wgt->h - ta_pad * 2;
                            float ta_sb_w = ctx->style.scrollbar_size;
                            float ta_text_w = drag_wgt->w - ta_sb_w;
                            float ta_content_h = _textarea_content_height(tatd, d_font, ta_text_w, ta_pad);
                            float d_ay = d_oy + drag_wgt->y + ta_pad;
                            tatd->scroll_y = (int)_scrollbar_calc_scroll(
                                d_my, d_ay, ta_view_h,
                                ta_view_h, ta_content_h,
                                ctx->style.scrollbar_thumb_min);
                        }
                        break;
                    }
                    case N_GUI_TYPE_SPLITPANE: {
                        N_GUI_SPLITPANE_DATA* spd = (N_GUI_SPLITPANE_DATA*)drag_wgt->data;
                        float d_ax = d_ox + drag_wgt->x;
                        float d_ay = d_oy + drag_wgt->y;
                        float r;
                        if (spd->orientation == N_GUI_SPLIT_VERTICAL)
                            r = (drag_wgt->w > 0) ? ((float)ctx->mouse_x - d_ax) / drag_wgt->w : spd->ratio;
                        else
                            r = (drag_wgt->h > 0) ? (d_my - d_ay) / drag_wgt->h : spd->ratio;
                        if (r < spd->min_ratio) r = spd->min_ratio;
                        if (r > spd->max_ratio) r = spd->max_ratio;
                        spd->ratio = r;
                        if (spd->on_change) spd->on_change(drag_wgt->id, r, spd->user_data);
                        break;
                    }
                    case N_GUI_TYPE_HEXVIEW: {
                        N_GUI_HEXVIEW_DATA* hd = (N_GUI_HEXVIEW_DATA*)drag_wgt->data;
                        float row_h = d_fh + 2.0f;
                        int bpr = hd->bytes_per_row > 0 ? hd->bytes_per_row : 16;
                        int nb_rows = (int)((hd->len + (size_t)bpr - 1) / (size_t)bpr);
                        int visible = (int)((drag_wgt->h - ctx->style.textarea_padding * 2.0f) / row_h);
                        float d_ay = d_oy + drag_wgt->y;
                        if (visible < 1) visible = 1;
                        hd->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, drag_wgt->h, visible, nb_rows, ctx->style.scrollbar_thumb_min);
                        break;
                    }
                    case N_GUI_TYPE_SYNTAXVIEW: {
                        N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)drag_wgt->data;
                        float d_ax = d_ox + drag_wgt->x;
                        float d_ay = d_oy + drag_wgt->y;
                        float sv_content_w = 0.0f, sv_pane_w = drag_wgt->w;
                        int sv_need_hsb = 0, visible = 1;
                        _syntaxview_hmetrics(drag_wgt, yd, d_font, &ctx->style, &sv_content_w, &sv_pane_w, &sv_need_hsb, &visible);
                        if (yd->h_scroll_dragging) {
                            yd->h_scroll = _scrollbar_calc_scroll(d_mx, d_ax, sv_pane_w, sv_pane_w, sv_content_w, ctx->style.scrollbar_thumb_min);
                            _syntaxview_clamp_h(yd, sv_content_w, sv_pane_w);
                        } else {
                            float rows_h = drag_wgt->h - (sv_need_hsb ? ctx->style.scrollbar_size : 0.0f);
                            yd->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, rows_h, visible, yd->cached_nb_lines, ctx->style.scrollbar_thumb_min);
                        }
                        break;
                    }
                    case N_GUI_TYPE_DATAGRID: {
                        N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)drag_wgt->data;
                        float row_h = d_fh + ctx->style.item_height_pad;
                        float header_h = row_h;
                        float data_h = drag_wgt->h - header_h;
                        int visible = (int)(data_h / row_h);
                        float d_ay = d_oy + drag_wgt->y + header_h;
                        if (visible < 1) visible = 1;
                        if (gd->col_resize_col >= 0 && (size_t)gd->col_resize_col < gd->nb_cols) {
                            /* a header-border drag resizes the column live */
                            float nw = gd->col_resize_w0 + ((float)d_mx - gd->col_resize_x0);
                            if (nw < 16.0f) nw = 16.0f;
                            gd->cols[gd->col_resize_col].width = nw;
                        } else if (gd->h_scroll_dragging) {
                            /* dragging the horizontal scrollbar thumb: map the cursor x to a
                               pixel offset over the content width */
                            float d_ax = d_ox + drag_wgt->x;
                            float dg_content_w = 0.0f, dg_pane_w = 0.0f;
                            _datagrid_hmetrics(drag_wgt, gd, d_font, &ctx->style, &dg_content_w, &dg_pane_w, NULL);
                            gd->h_scroll = _scrollbar_calc_scroll((float)d_mx, d_ax, dg_pane_w, dg_pane_w, dg_content_w, ctx->style.scrollbar_thumb_min);
                        } else {
                            gd->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, data_h, visible, (int)gd->nb_rows, ctx->style.scrollbar_thumb_min);
                        }
                        break;
                    }
                    default:
                        break;
                }
                return 1;
            }
        }
    }

    /* screen-space mouse (for global scrollbar interaction) */
    float screen_mx = (float)ctx->mouse_x;
    float screen_my = (float)ctx->mouse_y;
    /* Virtual-space mouse: reverse the virtual canvas transform first.
     * The virtual canvas and the global scroll belong to the main display only,
     * n_gui_draw_detached paints a native window under an identity transform.
     * Applying either on a detached pass would offset every hit test in that
     * window by the main display's letterbox and scroll. */
    float virtual_mx = screen_mx;
    float virtual_my = screen_my;
    if (!ctx->pass_display && ctx->virtual_w > 0 && ctx->virtual_h > 0 && ctx->gui_scale > 0) {
        virtual_mx = (screen_mx - ctx->gui_offset_x) / ctx->gui_scale;
        virtual_my = (screen_my - ctx->gui_offset_y) / ctx->gui_scale;
    }
    /* GUI-space mouse (offset by global scroll) */
    float mx = virtual_mx;
    float my = virtual_my;
    if (!ctx->pass_display) {
        mx += ctx->global_scroll_x;
        my += ctx->global_scroll_y;
    }
    int just_pressed = (ctx->mouse_b1 == 1 && ctx->mouse_b1_prev == 0 &&
                        event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN)
                           ? 1
                           : 0;
    int just_released = (ctx->mouse_b1 == 0 && ctx->mouse_b1_prev == 1 &&
                         event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP)
                            ? 1
                            : 0;
    /* a right mouse-button press, used to raise a widget's context menu */
    int just_right_pressed = (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN &&
                              event.mouse.button == 2)
                                 ? 1
                                 : 0;
    /* a middle mouse-button press pastes the PRIMARY selection into the focused text
     * field, mirroring the middle-click paste of native X11 apps (Allegro button 3) */
    int just_middle_pressed = (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN &&
                               event.mouse.button == 3)
                                  ? 1
                                  : 0;
    if (just_middle_pressed && ctx->focused_widget_id >= 0 && _ctx_io_display(ctx)) {
        N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
        if (fw && fw->type == N_GUI_TYPE_TEXTAREA && (fw->state & N_GUI_STATE_FOCUSED) && fw->data) {
            char* clip = _ngui_clip_get(_ctx_io_display(ctx), N_CLIPBOARD_PRIMARY);
            if (clip) {
                _textarea_paste_clip(fw, (N_GUI_TEXTAREA_DATA*)fw->data, clip);
                al_free(clip);
            }
        }
    }

    /* effective dimensions: gate on both dimensions for virtual canvas */
    int virtual_active = (ctx->virtual_w > 0 && ctx->virtual_h > 0);
    float eff_w = virtual_active ? ctx->virtual_w : ctx->display_w;
    float eff_h = virtual_active ? ctx->virtual_h : ctx->display_h;

    /* handle global scrollbar drag */
    if (ctx->global_vscroll_drag || ctx->global_hscroll_drag) {
        if (ctx->mouse_b1 && (event.type == ALLEGRO_EVENT_MOUSE_AXES || event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN)) {
            float scrollbar_size = ctx->style.global_scrollbar_size;
            _compute_gui_bounds(ctx);
            if (ctx->global_vscroll_drag && eff_h > 0) {
                int need_hscroll = (ctx->gui_bounds_w > eff_w) ? 1 : 0;
                float sb_h = eff_h - (need_hscroll ? scrollbar_size : 0);
                float view_h = sb_h;
                float ratio = view_h / ctx->gui_bounds_h;
                if (ratio > 1.0f) ratio = 1.0f;
                float thumb_h = ratio * sb_h;
                if (thumb_h < ctx->style.global_scrollbar_thumb_min) thumb_h = ctx->style.global_scrollbar_thumb_min;
                float max_scroll = ctx->gui_bounds_h - view_h;
                float track_range = sb_h - thumb_h;
                if (track_range > 0 && max_scroll > 0) {
                    float pos_ratio = (virtual_my - thumb_h / 2.0f) / track_range;
                    if (pos_ratio < 0) pos_ratio = 0;
                    if (pos_ratio > 1) pos_ratio = 1;
                    ctx->global_scroll_y = pos_ratio * max_scroll;
                }
            }
            if (ctx->global_hscroll_drag && eff_w > 0) {
                int need_vscroll = (ctx->gui_bounds_h > eff_h) ? 1 : 0;
                float sb_w = eff_w - (need_vscroll ? scrollbar_size : 0);
                float view_w = sb_w;
                float ratio = view_w / ctx->gui_bounds_w;
                if (ratio > 1.0f) ratio = 1.0f;
                float thumb_w = ratio * sb_w;
                if (thumb_w < ctx->style.global_scrollbar_thumb_min) thumb_w = ctx->style.global_scrollbar_thumb_min;
                float max_scroll = ctx->gui_bounds_w - view_w;
                float track_range = sb_w - thumb_w;
                if (track_range > 0 && max_scroll > 0) {
                    float pos_ratio = (virtual_mx - thumb_w / 2.0f) / track_range;
                    if (pos_ratio < 0) pos_ratio = 0;
                    if (pos_ratio > 1) pos_ratio = 1;
                    ctx->global_scroll_x = pos_ratio * max_scroll;
                }
            }
            return 1;
        }
        if (just_released) {
            ctx->global_vscroll_drag = 0;
            ctx->global_hscroll_drag = 0;
        }
        return 1;
    }

    /* check global scrollbar click */
    if (just_pressed && eff_w > 0 && eff_h > 0) {
        float scrollbar_size = ctx->style.global_scrollbar_size;
        _compute_gui_bounds(ctx);
        int need_vscroll = (ctx->gui_bounds_h > eff_h) ? 1 : 0;
        int need_hscroll = (ctx->gui_bounds_w > eff_w) ? 1 : 0;
        if (need_vscroll && ctx->gui_bounds_w > (eff_w - scrollbar_size)) need_hscroll = 1;
        if (need_hscroll && ctx->gui_bounds_h > (eff_h - scrollbar_size)) need_vscroll = 1;

        if (need_vscroll) {
            float sb_x = eff_w - scrollbar_size;
            float sb_h = eff_h - (need_hscroll ? scrollbar_size : 0);
            if (_point_in_rect(virtual_mx, virtual_my, sb_x, 0, scrollbar_size, sb_h)) {
                ctx->global_vscroll_drag = 1;
                return 1;
            }
        }
        if (need_hscroll) {
            float sb_y = eff_h - scrollbar_size;
            float sb_w = eff_w - (need_vscroll ? scrollbar_size : 0);
            if (_point_in_rect(virtual_mx, virtual_my, 0, sb_y, sb_w, scrollbar_size)) {
                ctx->global_hscroll_drag = 1;
                return 1;
            }
        }
    }

    /* handle open combobox dropdown first (it overlays everything). Like the
       dropmenu above, a hover over the open panel is consumed so the widgets
       beneath the floating list never receive hover/focus. */
    if (ctx->open_combobox_id >= 0 &&
        (just_pressed || event.type == ALLEGRO_EVENT_MOUSE_AXES)) {
        N_GUI_WIDGET* cb_wgt = n_gui_get_widget(ctx, ctx->open_combobox_id);
        if (cb_wgt && cb_wgt->data) {
            N_GUI_COMBOBOX_DATA* cbd = (N_GUI_COMBOBOX_DATA*)cb_wgt->data;

            /* find absolute position of the combobox (account for window scroll) */
            float cb_ox = 0, cb_oy = 0;
            _find_widget_window(ctx, cb_wgt->id, &cb_ox, &cb_oy);

            ALLEGRO_FONT* cb_font = cb_wgt->font ? cb_wgt->font : ctx->default_font;
            float cb_fh = cb_font ? (float)al_get_font_line_height(cb_font) : 16.0f;
            float cb_ih = cbd->item_height > cb_fh ? cbd->item_height : cb_fh + ctx->style.item_height_pad;
            float cb_ax = cb_ox + cb_wgt->x;
            float cb_pad = ctx->style.item_text_padding;
            int cb_vis = (int)cbd->nb_items;
            if (cb_vis > cbd->max_visible) cb_vis = cbd->max_visible;
            float cb_dd_h = (float)cb_vis * cb_ih;
            float cb_ay = _dropdown_panel_y(ctx, cb_oy + cb_wgt->y, cb_oy + cb_wgt->y + cb_wgt->h,
                                            cb_dd_h, cbd->flags & N_GUI_COMBOBOX_EXPAND_UP);

            /* compute effective dropdown width (mirrors _draw_combobox_dropdown) */
            float cb_dd_w = cb_wgt->w;
            if ((cbd->flags & N_GUI_COMBOBOX_AUTO_WIDTH) && cb_font) {
                float cb_max_tw = 0;
                for (size_t ci = 0; ci < cbd->nb_items; ci++) {
                    float tw = _text_w(cb_font, cbd->items[ci].text);
                    if (tw > cb_max_tw) cb_max_tw = tw;
                }
                float cb_needed = cb_max_tw + cb_pad * 2;
                if (cb_needed > cb_dd_w) cb_dd_w = cb_needed;
                float cb_cap = ctx->style.combobox_max_dropdown_width;
                if (cb_cap <= 0) cb_cap = ctx->display_w > 0 ? (float)ctx->display_w : 4096.0f;
                if (cb_dd_w > cb_cap) cb_dd_w = cb_cap;
                if (ctx->display_w > 0 && cb_ax + cb_dd_w > (float)ctx->display_w) {
                    cb_dd_w = (float)ctx->display_w - cb_ax;
                    if (cb_dd_w < cb_wgt->w) cb_dd_w = cb_wgt->w;
                }
            }

            if (!just_pressed) {
                /* hover or wheel over the open panel. A move sets the highlight to the
                   item under the pointer; the wheel steps the highlight and scrolls to
                   keep it visible. Consume either way so nothing beneath is affected. */
                if (_point_in_rect(mx, my, cb_ax, cb_ay, cb_dd_w, cb_dd_h)) {
                    if (event.type == ALLEGRO_EVENT_MOUSE_AXES && event.mouse.dz != 0) {
                        _list_panel_highlight_step(&cbd->highlight_index, &cbd->scroll_offset,
                                                   (int)cbd->nb_items, cbd->max_visible,
                                                   -event.mouse.dz, cbd->selected_index);
                    } else {
                        int cb_hi = cbd->scroll_offset + (int)((my - cb_ay) / cb_ih);
                        if (cb_hi >= 0 && (size_t)cb_hi < cbd->nb_items) cbd->highlight_index = cb_hi;
                    }
                    return 1;
                }
            } else if (_point_in_rect(mx, my, cb_ax, cb_ay, cb_dd_w, cb_dd_h)) {
                /* Check if click is on the scrollbar area (right edge) */
                int cb_need_sb = ((int)cbd->nb_items > cbd->max_visible);
                float cb_sb_size = ctx->style.scrollbar_size;
                if (cb_sb_size < 10.0f) cb_sb_size = 10.0f;
                float cb_item_w = cb_need_sb ? cb_dd_w - cb_sb_size : cb_dd_w;

                if (cb_need_sb && mx >= cb_ax + cb_item_w) {
                    /* Clicked on scrollbar, scroll to position, start drag */
                    cbd->scroll_offset = _scrollbar_calc_scroll_int(my, cb_ay, cb_dd_h, cbd->max_visible, (int)cbd->nb_items, ctx->style.scrollbar_thumb_min);
                    ctx->scrollbar_drag_widget_id = cb_wgt->id;
                    return 1; /* consumed, keep dropdown open */
                }

                /* Clicked on item area, select item */
                int clicked_idx = cbd->scroll_offset + (int)((my - cb_ay) / cb_ih);
                if (clicked_idx >= 0 && (size_t)clicked_idx < cbd->nb_items) {
                    cbd->selected_index = clicked_idx;
                    if (cbd->on_select) cbd->on_select(cb_wgt->id, clicked_idx, cbd->user_data);
                }
                cbd->is_open = 0;
                ctx->open_combobox_id = -1;
                return 1; /* event consumed by dropdown */
            } else {
                /* clicked outside dropdown - close it */
                cbd->is_open = 0;
                ctx->open_combobox_id = -1;
                /* fall through to normal processing */
            }
        } else {
            ctx->open_combobox_id = -1;
        }
    }

    /* handle an open dropdown menu panel first. While a menu is open its floating
       panel visually overlaps the widgets beneath it, so ANY mouse event over the
       panel (a hover as well as a click) is consumed here: otherwise a hover would
       fall through to the general widget logic below and highlight/focus whatever
       sits under the panel. */
    if (ctx->open_dropmenu_id >= 0 &&
        (just_pressed || event.type == ALLEGRO_EVENT_MOUSE_AXES)) {
        N_GUI_WIDGET* dm_wgt = n_gui_get_widget(ctx, ctx->open_dropmenu_id);
        if (dm_wgt && dm_wgt->data) {
            N_GUI_DROPMENU_DATA* dmd = (N_GUI_DROPMENU_DATA*)dm_wgt->data;

            /* find absolute position of the dropmenu widget (account for window scroll) */
            float dm_ox = 0, dm_oy = 0;
            _find_widget_window(ctx, dm_wgt->id, &dm_ox, &dm_oy);

            ALLEGRO_FONT* dm_font = dm_wgt->font ? dm_wgt->font : ctx->default_font;
            float dm_fh = dm_font ? (float)al_get_font_line_height(dm_font) : 16.0f;
            float dm_ih = dmd->item_height > dm_fh ? dmd->item_height : dm_fh + ctx->style.item_height_pad;
            float dm_ax = dm_ox + dm_wgt->x;
            int dm_vis = (int)dmd->nb_entries;
            if (dm_vis > dmd->max_visible) dm_vis = dmd->max_visible;
            float dm_panel_h = (float)dm_vis * dm_ih;
            float dm_ay = _dropdown_panel_y(ctx, dm_oy + dm_wgt->y, dm_oy + dm_wgt->y + dm_wgt->h,
                                            dm_panel_h, dmd->flags & N_GUI_DROPMENU_EXPAND_UP);
            /* the open panel is widened to fit its entries; hit-test the same width */
            float dm_panel_w = n_gui_dropmenu_panel_width(ctx, dm_wgt->id);
            int over_panel = _point_in_rect(mx, my, dm_ax, dm_ay, dm_panel_w, dm_panel_h);

            if (!just_pressed) {
                /* hover or wheel while the menu is open. Over the panel: a move sets the
                   highlight to the entry under the pointer, the wheel steps the highlight
                   and scrolls to keep it visible, and the event is consumed so nothing
                   beneath is affected. Off the panel it falls through so other widgets
                   update hover normally. */
                if (over_panel) {
                    if (event.type == ALLEGRO_EVENT_MOUSE_AXES && event.mouse.dz != 0) {
                        _list_panel_highlight_step(&dmd->highlight_index, &dmd->scroll_offset,
                                                   (int)dmd->nb_entries, dmd->max_visible,
                                                   -event.mouse.dz, -1);
                    } else {
                        int dm_hi = dmd->scroll_offset + (int)((my - dm_ay) / dm_ih);
                        if (dm_hi >= 0 && (size_t)dm_hi < dmd->nb_entries) dmd->highlight_index = dm_hi;
                    }
                    return 1;
                }
            } else if (over_panel) {
                /* check if click is on the scrollbar area (right edge) */
                int dm_need_sb = ((int)dmd->nb_entries > dmd->max_visible);
                float dm_sb_size = ctx->style.scrollbar_size;
                if (dm_sb_size < 10.0f) dm_sb_size = 10.0f;
                float dm_item_w = dm_need_sb ? dm_panel_w - dm_sb_size : dm_panel_w;

                if (dm_need_sb && mx >= dm_ax + dm_item_w) {
                    /* Clicked on scrollbar, scroll to position, start drag */
                    dmd->scroll_offset = _scrollbar_calc_scroll_int(my, dm_ay, dm_panel_h, dmd->max_visible, (int)dmd->nb_entries, ctx->style.scrollbar_thumb_min);
                    ctx->scrollbar_drag_widget_id = dm_wgt->id;
                    return 1; /* consumed, keep panel open */
                }

                /* clicked on item area */
                int clicked_idx = dmd->scroll_offset + (int)((my - dm_ay) / dm_ih);
                if (clicked_idx >= 0 && (size_t)clicked_idx < dmd->nb_entries) {
                    N_GUI_DROPMENU_ENTRY* entry = &dmd->entries[clicked_idx];
                    if (entry->on_click) {
                        entry->on_click(dm_wgt->id, clicked_idx, entry->tag, entry->user_data);
                    }
                }
                dmd->is_open = 0;
                ctx->open_dropmenu_id = -1;
                return 1; /* event consumed by dropdown menu */
            } else {
                /* clicked outside panel - close the menu */
                dmd->is_open = 0;
                ctx->open_dropmenu_id = -1;
                /* if click landed on the button itself, consume event so widget handler doesn't re-open */
                float dm_btn_ax = dm_ox + dm_wgt->x;
                float dm_btn_ay = dm_oy + dm_wgt->y;
                if (_point_in_rect(mx, my, dm_btn_ax, dm_btn_ay, dm_wgt->w, dm_wgt->h)) {
                    return 1;
                }
                /* fall through to normal processing */
            }
        } else {
            ctx->open_dropmenu_id = -1;
        }
    }

    /* iterate windows back-to-front, but for hit testing we want front-to-back
       (last in list = front), so we walk backward */
    if (event.type == ALLEGRO_EVENT_MOUSE_AXES ||
        event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP) {
        /* reset hover for all widgets and titlebar buttons */
        list_foreach(wnode, ctx->windows) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            /* display routing: only windows on the pass this event belongs to */
            if (!_win_on_pass(ctx, win)) continue;
            if (!(win->state & N_GUI_WIN_OPEN)) continue;
            win->tb_buttons.hovered = N_GUI_TB_BTN_NONE;
            list_foreach(wgn, win->widgets) {
                N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                if (!wgt) continue;
                wgt->state &= ~N_GUI_STATE_HOVER;
                /* also clear listbox per-row hover so a widget that
                   now has no pointer over it doesn't keep painting
                   a hover highlight from the previous frame */
                if (wgt->type == N_GUI_TYPE_LISTBOX && wgt->data) {
                    ((N_GUI_LISTBOX_DATA*)wgt->data)->hover_row = -1;
                }
            }
        }

        /* check if any window has an active drag/resize/scroll (mouse capture) */
        LIST_NODE* captured_wnode = NULL;
        for (LIST_NODE* wnode = ctx->windows->end; wnode; wnode = wnode->prev) {
            const N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            /* display routing: only windows on the pass this event belongs to */
            if (!_win_on_pass(ctx, win)) continue;
            if (!(win->state & N_GUI_WIN_OPEN)) continue;
            if (win->state & (N_GUI_WIN_DRAGGING | N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG)) {
                captured_wnode = wnode;
                break;
            }
        }

        /* find topmost window hit (iterate from end), skip if captured */
        LIST_NODE* hit_wnode = captured_wnode;
        if (!hit_wnode) {
            for (LIST_NODE* wnode = ctx->windows->end; wnode; wnode = wnode->prev) {
                N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
                /* display routing: only windows on the pass this event belongs to */
                if (!_win_on_pass(ctx, win)) continue;
                if (!(win->state & N_GUI_WIN_OPEN)) continue;

                float win_h_check = (win->state & N_GUI_WIN_MINIMISED) ? _win_tbh(win) : win->h;
                if (_point_in_rect(mx, my, win->x, win->y, win->w, win_h_check)) {
                    hit_wnode = wnode;
                    break;
                }
            }
        }

        if (hit_wnode) {
            event_consumed = 1;
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)hit_wnode->ptr;

            /* only process new clicks/interactions when not in a captured operation */
            if (!captured_wnode) {
                /* bring to front on click */
                if (just_pressed) {
                    n_gui_raise_window(ctx, win->id);
                    /* re-fetch since node may have moved */
                    win = n_gui_get_window(ctx, win->id);
                    if (!win) return 1;

                    /* clear focus from previously focused widget on a new primary click
                     * inside a window; textarea clicks will re-set focus below */
                    if (ctx->focused_widget_id >= 0) {
                        N_GUI_WIDGET* prev_fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
                        if (prev_fw) prev_fw->state &= ~N_GUI_STATE_FOCUSED;
                        ctx->focused_widget_id = -1;
                    }
                }

                /* check auto-scrollbar clicks before resize handle */
                int auto_scrollbar_hit = 0;
                if ((win->flags & N_GUI_WIN_AUTO_SCROLLBAR) && !(win->state & N_GUI_WIN_MINIMISED) && just_pressed) {
                    float scrollbar_size = ctx->style.scrollbar_size;
                    float body_h = win->h - _win_tbh(win);
                    float body_y = win->y + _win_tbh(win);
                    int need_vscroll = 0, need_hscroll = 0;
                    _window_update_content_size(win, ctx->default_font);
                    if (win->content_h > body_h) need_vscroll = 1;
                    if (win->content_w > win->w) need_hscroll = 1;
                    if (need_vscroll && win->content_w > (win->w - scrollbar_size)) need_hscroll = 1;
                    if (need_hscroll && win->content_h > (body_h - scrollbar_size)) need_vscroll = 1;
                    float content_area_w = win->w - (need_vscroll ? scrollbar_size : 0);
                    float content_area_h = body_h - (need_hscroll ? scrollbar_size : 0);

                    /* vertical auto-scrollbar track */
                    if (need_vscroll) {
                        float sb_x = win->x + win->w - scrollbar_size;
                        float sb_y = body_y;
                        float sb_h = content_area_h;
                        /* leave room for resize corner when no horizontal scrollbar */
                        if ((win->flags & N_GUI_WIN_RESIZABLE) && !need_hscroll) sb_h -= scrollbar_size;
                        if (_point_in_rect(mx, my, sb_x, sb_y, scrollbar_size, sb_h)) {
                            win->state |= N_GUI_WIN_VSCROLL_DRAG;
                            auto_scrollbar_hit = 1;
                            win->scroll_y = _scrollbar_calc_scroll(my, sb_y, sb_h, content_area_h, win->content_h, ctx->style.scrollbar_thumb_min);
                        }
                    }
                    /* horizontal auto-scrollbar track */
                    if (need_hscroll && !auto_scrollbar_hit) {
                        float sb_x = win->x;
                        float sb_y = win->y + win->h - scrollbar_size;
                        float sb_w = content_area_w;
                        /* leave room for resize corner when no vertical scrollbar */
                        if ((win->flags & N_GUI_WIN_RESIZABLE) && !need_vscroll) sb_w -= scrollbar_size;
                        if (_point_in_rect(mx, my, sb_x, sb_y, sb_w, scrollbar_size)) {
                            win->state |= N_GUI_WIN_HSCROLL_DRAG;
                            auto_scrollbar_hit = 1;
                            win->scroll_x = _scrollbar_calc_scroll(mx, sb_x, sb_w, content_area_w, win->content_w, ctx->style.scrollbar_thumb_min);
                        }
                    }
                }

                /* check resize handle (bottom-right corner, 14x14 px) - skip if auto-scrollbar was hit or maximised */
                if (!auto_scrollbar_hit && (win->flags & N_GUI_WIN_RESIZABLE) && !(win->state & (N_GUI_WIN_MINIMISED | N_GUI_WIN_MAXIMISED))) {
                    float grip = ctx->style.grip_size + 2.0f;
                    float rx = win->x + win->w - grip;
                    float ry = win->y + win->h - grip;
                    /* when auto-scrollbars are visible, restrict resize to the corner square only */
                    int in_resize_area = 0;
                    if (win->flags & N_GUI_WIN_AUTO_SCROLLBAR) {
                        float scrollbar_size = ctx->style.scrollbar_size;
                        float body_h = win->h - _win_tbh(win);
                        int need_vscroll = 0, need_hscroll = 0;
                        _window_update_content_size(win, ctx->default_font);
                        if (win->content_h > body_h) need_vscroll = 1;
                        if (win->content_w > win->w) need_hscroll = 1;
                        if (need_vscroll && win->content_w > (win->w - scrollbar_size)) need_hscroll = 1;
                        if (need_hscroll && win->content_h > (body_h - scrollbar_size)) need_vscroll = 1;
                        if (need_vscroll || need_hscroll) {
                            /* resize corner is only the small square at bottom-right where scrollbars don't reach */
                            float corner_x = win->x + win->w - scrollbar_size;
                            float corner_y = win->y + win->h - scrollbar_size;
                            in_resize_area = just_pressed && _point_in_rect(mx, my, corner_x, corner_y, scrollbar_size, scrollbar_size);
                        } else {
                            in_resize_area = just_pressed && _point_in_rect(mx, my, rx, ry, grip, grip);
                        }
                    } else {
                        in_resize_area = just_pressed && _point_in_rect(mx, my, rx, ry, grip, grip);
                    }
                    if (in_resize_area) {
                        win->state |= N_GUI_WIN_RESIZING;
                        win->drag_ox = win->w - (mx - win->x);
                        win->drag_oy = win->h - (my - win->y);
                    }
                }

                /* title bar: buttons / drag */
                if (!(win->state & (N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG)) &&
                    _point_in_rect(mx, my, win->x, win->y, win->w, _win_tbh(win))) {
                    /* check titlebar buttons first */
                    int tb_btn_hit = N_GUI_TB_BTN_NONE;
                    float tbx, tby, tbw, tbhh;
                    if (_tb_button_rect(win, &ctx->style, N_GUI_TB_BTN_CLOSE, &tbx, &tby, &tbw, &tbhh) &&
                        _point_in_rect(mx, my, tbx, tby, tbw, tbhh)) {
                        tb_btn_hit = N_GUI_TB_BTN_CLOSE;
                    } else if (_tb_button_rect(win, &ctx->style, N_GUI_TB_BTN_MAXIMIZE, &tbx, &tby, &tbw, &tbhh) &&
                               _point_in_rect(mx, my, tbx, tby, tbw, tbhh)) {
                        tb_btn_hit = N_GUI_TB_BTN_MAXIMIZE;
                    } else if (_tb_button_rect(win, &ctx->style, N_GUI_TB_BTN_MINIMIZE, &tbx, &tby, &tbw, &tbhh) &&
                               _point_in_rect(mx, my, tbx, tby, tbw, tbhh)) {
                        tb_btn_hit = N_GUI_TB_BTN_MINIMIZE;
                    }

                    /* hover tracking */
                    win->tb_buttons.hovered = tb_btn_hit;

                    if (just_pressed && tb_btn_hit != N_GUI_TB_BTN_NONE) {
                        /* button press, do NOT start drag */
                        win->tb_buttons.pressed = tb_btn_hit;
                    } else if (just_pressed && !(win->flags & N_GUI_WIN_FIXED_POSITION) && !(win->state & N_GUI_WIN_MAXIMISED)) {
                        /* start drag */
                        win->state |= N_GUI_WIN_DRAGGING;
                        win->drag_ox = mx - win->x;
                        win->drag_oy = my - win->y;
                        _native_drag_begin(win);
                    }

                    /* release handling for titlebar buttons */
                    if (just_released && win->tb_buttons.pressed != N_GUI_TB_BTN_NONE) {
                        int btn = win->tb_buttons.pressed;
                        win->tb_buttons.pressed = N_GUI_TB_BTN_NONE;
                        if (_tb_button_rect(win, &ctx->style, btn, &tbx, &tby, &tbw, &tbhh) &&
                            _point_in_rect(mx, my, tbx, tby, tbw, tbhh)) {
                            _tb_button_action(ctx, win, btn);
                        }
                    }
                } else if (!(win->state & (N_GUI_WIN_MINIMISED | N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG))) {
                    /* widget area (account for scroll offsets) */
                    float content_x = win->x - win->scroll_x;
                    float content_y = win->y + _win_tbh(win) - win->scroll_y;

                    /* iterate topmost-first (last added draws on top) so the
                       widget under the cursor that the user actually sees gets
                       the event, then break -- only the top widget is hit. A
                       forward walk would hand a click to an earlier-added widget
                       that merely overlaps (e.g. a split pane drawn under a grid). */
                    for (LIST_NODE* wgn = win->widgets ? win->widgets->end : NULL; wgn; wgn = wgn->prev) {
                        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                        if (!wgt || !wgt->visible || !wgt->enabled) continue;
                        float ax = content_x + wgt->x;
                        float ay = content_y + wgt->y;

                        if (_point_in_rect(mx, my, ax, ay, wgt->w, wgt->h)) {
                            /* A split pane paints only its divider but spans the
                               whole region it splits, so a press that misses the
                               divider band must fall through to whatever sits
                               beneath it (another split pane crossing it, a grid,
                               the panes' own widgets) instead of being swallowed.
                               Same rule the wheel path applies further down. It
                               also keeps HOVER (and with it the divider highlight)
                               limited to the band. */
                            if (wgt->type == N_GUI_TYPE_SPLITPANE &&
                                !_splitpane_on_divider(wgt, mx, my, ax, ay)) {
                                continue;
                            }
                            wgt->state |= N_GUI_STATE_HOVER;

                            /* listbox: translate mouse-Y into the row
                               index under the cursor so the draw path
                               can paint a hover highlight. */
                            if (wgt->type == N_GUI_TYPE_LISTBOX && wgt->data) {
                                N_GUI_LISTBOX_DATA* ld =
                                    (N_GUI_LISTBOX_DATA*)wgt->data;
                                float ih = ld->item_height > 0.0f
                                               ? ld->item_height
                                               : 20.0f;
                                int row_in_view = (int)((my - ay) / ih);
                                int row = row_in_view + ld->scroll_offset;
                                if (row >= 0 && (size_t)row < ld->nb_items) {
                                    ld->hover_row = row;
                                } else {
                                    ld->hover_row = -1;
                                }
                            }

                            if (just_pressed) {
                                wgt->state |= N_GUI_STATE_ACTIVE;

                                /* set focus on any interactive widget type */
                                if (wgt->type == N_GUI_TYPE_SLIDER ||
                                    wgt->type == N_GUI_TYPE_CHECKBOX ||
                                    wgt->type == N_GUI_TYPE_LISTBOX ||
                                    wgt->type == N_GUI_TYPE_RADIOLIST ||
                                    wgt->type == N_GUI_TYPE_COMBOBOX ||
                                    wgt->type == N_GUI_TYPE_SCROLLBAR ||
                                    wgt->type == N_GUI_TYPE_DROPMENU ||
                                    wgt->type == N_GUI_TYPE_TEXTAREA) {
                                    wgt->state |= N_GUI_STATE_FOCUSED;
                                    ctx->focused_widget_id = wgt->id;
                                }

                                if (wgt->type == N_GUI_TYPE_BUTTON) {
                                    /* click callback on release */
                                }
                                if (wgt->type == N_GUI_TYPE_CHECKBOX) {
                                    N_GUI_CHECKBOX_DATA* ckd = (N_GUI_CHECKBOX_DATA*)wgt->data;
                                    ckd->checked = !ckd->checked;
                                    if (ckd->on_toggle) ckd->on_toggle(wgt->id, ckd->checked, ckd->user_data);
                                }
                                if (wgt->type == N_GUI_TYPE_SLIDER) {
                                    _slider_update_from_mouse(wgt, mx, my, content_x, content_y);
                                }
                                if (wgt->type == N_GUI_TYPE_SCROLLBAR) {
                                    _scrollbar_update_from_mouse(wgt, mx, my, content_x, content_y, &ctx->style);
                                }
                                if (wgt->type == N_GUI_TYPE_TEXTAREA) {
                                    /* clear any label selection when clicking a textarea */
                                    if (ctx->selected_label_id >= 0) {
                                        N_GUI_WIDGET* prev = n_gui_get_widget(ctx, ctx->selected_label_id);
                                        if (prev && prev->type == N_GUI_TYPE_LABEL && prev->data) {
                                            N_GUI_LABEL_DATA* plb = (N_GUI_LABEL_DATA*)prev->data;
                                            plb->sel_start = -1;
                                            plb->sel_end = -1;
                                            plb->sel_dragging = 0;
                                        }
                                        ctx->selected_label_id = -1;
                                    }
                                    _clear_syntaxview_selection(ctx);
                                    N_GUI_TEXTAREA_DATA* click_td = (N_GUI_TEXTAREA_DATA*)wgt->data;
                                    click_td->cursor_time = al_get_time();
                                    ALLEGRO_FONT* tf = wgt->font ? wgt->font : ctx->default_font;
                                    if (tf) {
                                        /* check if click is on the scrollbar area */
                                        float text_pad = ctx->style.textarea_padding;
                                        if (click_td->multiline) {
                                            float ta_view_h = wgt->h - text_pad * 2;
                                            float ta_content_h = _textarea_content_height(click_td, tf, wgt->w, text_pad);
                                            int ta_need_sb = (ta_content_h > ta_view_h) ? 1 : 0;
                                            float ta_sb_w = ta_need_sb ? ctx->style.scrollbar_size : 0;
                                            if (ta_need_sb && mx > ax + wgt->w - ta_sb_w) {
                                                /* scrollbar track click: jump + start drag */
                                                float ta_text_w = wgt->w - ta_sb_w;
                                                ta_content_h = _textarea_content_height(click_td, tf, ta_text_w, text_pad);
                                                click_td->scroll_y = (int)_scrollbar_calc_scroll(
                                                    my, ay + text_pad, ta_view_h,
                                                    ta_view_h, ta_content_h,
                                                    ctx->style.scrollbar_thumb_min);
                                                ctx->scrollbar_drag_widget_id = wgt->id;
                                                goto textarea_click_done;
                                            }
                                        }
                                        size_t pos = _textarea_pos_from_mouse(click_td, tf, mx, my,
                                                                              ax, ay, wgt->w, wgt->h,
                                                                              text_pad, ctx->style.scrollbar_size);
                                        click_td->cursor_pos = pos;
                                        click_td->sel_start = pos;
                                        click_td->sel_end = pos;
                                    }
                                textarea_click_done:;
                                }
                                if (wgt->type == N_GUI_TYPE_LABEL) {
                                    N_GUI_LABEL_DATA* click_lb = (N_GUI_LABEL_DATA*)wgt->data;
                                    if (click_lb) {
                                        _clear_syntaxview_selection(ctx);
                                        /* clear previous label selection */
                                        if (ctx->selected_label_id >= 0 && ctx->selected_label_id != wgt->id) {
                                            N_GUI_WIDGET* prev = n_gui_get_widget(ctx, ctx->selected_label_id);
                                            if (prev && prev->type == N_GUI_TYPE_LABEL && prev->data) {
                                                N_GUI_LABEL_DATA* plb = (N_GUI_LABEL_DATA*)prev->data;
                                                plb->sel_start = -1;
                                                plb->sel_end = -1;
                                                plb->sel_dragging = 0;
                                            }
                                        }
                                        ALLEGRO_FONT* lf = wgt->font ? wgt->font : ctx->default_font;
                                        if (lf) {
                                            int char_pos;
                                            if (click_lb->align == N_GUI_ALIGN_JUSTIFIED) {
                                                float lpad = ctx->style.label_padding;
                                                float effective_w = wgt->w;
                                                float lww = 0;
                                                if (win->flags & N_GUI_WIN_AUTO_SCROLLBAR)
                                                    lww = win->content_w + ctx->style.title_max_w_reserve + lpad;
                                                else if (win->flags & N_GUI_WIN_RESIZABLE)
                                                    lww = win->w;
                                                if (lww > 0) {
                                                    float mfw = lww - wgt->x;
                                                    if (mfw > effective_w) effective_w = mfw;
                                                }
                                                float max_text_w = effective_w - lpad * 2;
                                                float content_h = _label_content_height(click_lb->text, lf, max_text_w);
                                                float view_h = wgt->h - lpad;
                                                float sb_sz = (content_h > view_h) ? ctx->style.scrollbar_size : 0;
                                                float tw2 = max_text_w - sb_sz;
                                                char_pos = _justified_char_at_pos(click_lb->text, lf, tw2,
                                                                                  ax + lpad, ay + lpad / 2.0f, click_lb->scroll_y, mx, my);
                                            } else {
                                                float lww = 0;
                                                if (win->flags & N_GUI_WIN_AUTO_SCROLLBAR)
                                                    lww = win->content_w + ctx->style.title_max_w_reserve + ctx->style.label_padding;
                                                else if (win->flags & N_GUI_WIN_RESIZABLE)
                                                    lww = win->w;
                                                float text_ox = _label_text_origin_x(click_lb, lf, ax, wgt->w, lww, wgt->x, ctx->style.label_padding);
                                                float click_x = mx - text_ox;
                                                char_pos = _label_char_at_x(click_lb, lf, click_x);
                                            }
                                            if (char_pos >= 0) {
                                                click_lb->sel_start = char_pos;
                                                click_lb->sel_end = char_pos;
                                                click_lb->sel_dragging = 1;
                                                ctx->selected_label_id = wgt->id;
                                            }
                                        }
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_SYNTAXVIEW) {
                                    N_GUI_SYNTAXVIEW_DATA* click_yd = (N_GUI_SYNTAXVIEW_DATA*)wgt->data;
                                    ALLEGRO_FONT* yf = wgt->font ? wgt->font : ctx->default_font;
                                    /* clear a selection held by a label */
                                    if (ctx->selected_label_id >= 0) {
                                        N_GUI_WIDGET* prev = n_gui_get_widget(ctx, ctx->selected_label_id);
                                        if (prev && prev->type == N_GUI_TYPE_LABEL && prev->data) {
                                            N_GUI_LABEL_DATA* plb = (N_GUI_LABEL_DATA*)prev->data;
                                            plb->sel_start = -1;
                                            plb->sel_end = -1;
                                            plb->sel_dragging = 0;
                                        }
                                        ctx->selected_label_id = -1;
                                    }
                                    /* clear a selection held by another syntax view */
                                    if (ctx->selected_syntaxview_id >= 0 && ctx->selected_syntaxview_id != wgt->id) {
                                        N_GUI_WIDGET* prev = n_gui_get_widget(ctx, ctx->selected_syntaxview_id);
                                        if (prev && prev->type == N_GUI_TYPE_SYNTAXVIEW && prev->data) {
                                            N_GUI_SYNTAXVIEW_DATA* pyd = (N_GUI_SYNTAXVIEW_DATA*)prev->data;
                                            pyd->sel_start = -1;
                                            pyd->sel_end = -1;
                                            pyd->sel_dragging = 0;
                                        }
                                    }
                                    if (click_yd && yf && click_yd->text) {
                                        float ypad = ctx->style.textarea_padding;
                                        float cl_content_w = 0.0f, cl_pane_w = wgt->w, cl_rows_h;
                                        int cl_need_hsb = 0, cl_visible = 1;
                                        int on_bar;
                                        _syntaxview_hmetrics(wgt, click_yd, yf, &ctx->style, &cl_content_w, &cl_pane_w, &cl_need_hsb, &cl_visible);
                                        cl_rows_h = wgt->h - (cl_need_hsb ? ctx->style.scrollbar_size : 0.0f);
                                        /* a press on either scrollbar drives that
                                           scrollbar (below), not the text cursor */
                                        on_bar = (cl_need_hsb && my > ay + cl_rows_h) ||
                                                 (click_yd->cached_nb_lines > cl_visible && mx > ax + wgt->w - ctx->style.scrollbar_size);
                                        if (!on_bar) {
                                            int off = _syntaxview_offset_from_mouse(click_yd, yf, mx, my, ax, ay, ypad);
                                            click_yd->sel_start = off;
                                            click_yd->sel_end = off;
                                            click_yd->sel_dragging = 1;
                                            ctx->selected_syntaxview_id = wgt->id;
                                        }
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_COMBOBOX) {
                                    N_GUI_COMBOBOX_DATA* cbd = (N_GUI_COMBOBOX_DATA*)wgt->data;
                                    cbd->is_open = !cbd->is_open;
                                    if (cbd->is_open) {
                                        ctx->open_combobox_id = wgt->id;
                                        /* start the keyboard/wheel highlight on the current value */
                                        cbd->highlight_index = cbd->selected_index;
                                        /* auto-scroll to center the selected item in the visible area */
                                        if (cbd->selected_index >= 0 && cbd->nb_items > (size_t)cbd->max_visible) {
                                            int target = cbd->selected_index - cbd->max_visible / 2;
                                            if (target < 0) target = 0;
                                            int max_off = (int)cbd->nb_items - cbd->max_visible;
                                            if (target > max_off) target = max_off;
                                            cbd->scroll_offset = target;
                                        } else {
                                            cbd->scroll_offset = 0;
                                        }
                                    } else {
                                        ctx->open_combobox_id = -1;
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_LISTBOX) {
                                    N_GUI_LISTBOX_DATA* lbd = (N_GUI_LISTBOX_DATA*)wgt->data;
                                    ALLEGRO_FONT* lf = wgt->font ? wgt->font : ctx->default_font;
                                    float lfh = lf ? (float)al_get_font_line_height(lf) : 16.0f;
                                    float lih = lbd->item_height > lfh ? lbd->item_height : lfh + ctx->style.item_height_pad;
                                    float lb_content_w = 0.0f, lb_pane_w = wgt->w;
                                    int lb_need_hsb = 0;
                                    int lb_visible = 0;
                                    float lb_rows_h;
                                    _listbox_hmetrics(wgt, lbd, lf, &ctx->style, &lb_content_w, &lb_pane_w, &lb_need_hsb, &lb_visible);
                                    lb_rows_h = wgt->h - (lb_need_hsb ? ctx->style.scrollbar_size : 0.0f);
                                    int lb_need_sb = ((int)lbd->nb_items > lb_visible) ? 1 : 0;
                                    float lb_sb_w = lb_need_sb ? ctx->style.scrollbar_size : 0;
                                    /* clamp scroll_offset */
                                    int lb_max_off = (int)lbd->nb_items - lb_visible;
                                    if (lb_max_off < 0) lb_max_off = 0;
                                    if (lbd->scroll_offset > lb_max_off) lbd->scroll_offset = lb_max_off;
                                    if (lbd->scroll_offset < 0) lbd->scroll_offset = 0;
                                    /* skip click if on either scrollbar area */
                                    if (lb_need_hsb && my > ay + lb_rows_h) {
                                        /* horizontal track click: jump + start drag */
                                        lbd->h_scroll = _scrollbar_calc_scroll(mx, ax, lb_pane_w, lb_pane_w, lb_content_w, ctx->style.scrollbar_thumb_min);
                                        _listbox_clamp_h(lbd, lb_content_w, lb_pane_w);
                                        lbd->h_scroll_dragging = 1;
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    } else if (lb_need_sb && mx > ax + wgt->w - lb_sb_w) {
                                        /* scrollbar track click: jump scroll position + start drag */
                                        lbd->scroll_offset = _scrollbar_calc_scroll_int(my, ay, lb_rows_h, lb_visible, (int)lbd->nb_items, ctx->style.scrollbar_thumb_min);
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    } else {
                                        int clicked_idx = lbd->scroll_offset + (int)((my - ay) / lih);
                                        if (clicked_idx >= 0 && (size_t)clicked_idx < lbd->nb_items) {
                                            if (lbd->selection_mode == N_GUI_SELECT_SINGLE) {
                                                for (size_t si = 0; si < lbd->nb_items; si++) lbd->items[si].selected = 0;
                                                lbd->items[clicked_idx].selected = 1;
                                                if (lbd->on_select) lbd->on_select(wgt->id, clicked_idx, 1, lbd->user_data);
                                            } else if (lbd->selection_mode == N_GUI_SELECT_MULTIPLE) {
                                                lbd->items[clicked_idx].selected = !lbd->items[clicked_idx].selected;
                                                if (lbd->on_select) lbd->on_select(wgt->id, clicked_idx, lbd->items[clicked_idx].selected, lbd->user_data);
                                            }
                                            /* N_GUI_SELECT_NONE: no selection change */
                                        }
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_SPLITPANE) {
                                    /* the scan above only lets a press through
                                       when it landed on the divider band */
                                    ctx->scrollbar_drag_widget_id = wgt->id;
                                }
                                if (wgt->type == N_GUI_TYPE_HEXVIEW) {
                                    N_GUI_HEXVIEW_DATA* hd = (N_GUI_HEXVIEW_DATA*)wgt->data;
                                    ALLEGRO_FONT* hf = wgt->font ? wgt->font : ctx->default_font;
                                    float hfh = hf ? (float)al_get_font_line_height(hf) : 16.0f;
                                    float row_h = hfh + 2.0f;
                                    int bpr = hd->bytes_per_row > 0 ? hd->bytes_per_row : 16;
                                    int nb_rows = (int)((hd->len + (size_t)bpr - 1) / (size_t)bpr);
                                    int hv_visible = (int)((wgt->h - ctx->style.textarea_padding * 2.0f) / row_h);
                                    int hv_need_sb;
                                    float hv_sb_w;
                                    if (hv_visible < 1) hv_visible = 1;
                                    hv_need_sb = (nb_rows > hv_visible) ? 1 : 0;
                                    hv_sb_w = hv_need_sb ? ctx->style.scrollbar_size : 0.0f;
                                    if (hv_need_sb && mx > ax + wgt->w - hv_sb_w) {
                                        hd->scroll_offset = _scrollbar_calc_scroll_int(my, ay, wgt->h, hv_visible, nb_rows, ctx->style.scrollbar_thumb_min);
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_SYNTAXVIEW) {
                                    N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)wgt->data;
                                    ALLEGRO_FONT* yf = wgt->font ? wgt->font : ctx->default_font;
                                    float yv_content_w = 0.0f, yv_pane_w = wgt->w, yv_rows_h;
                                    int yv_need_hsb = 0, yv_visible = 1, yv_need_sb;
                                    float yv_sb_w;
                                    _syntaxview_hmetrics(wgt, yd, yf, &ctx->style, &yv_content_w, &yv_pane_w, &yv_need_hsb, &yv_visible);
                                    yv_rows_h = wgt->h - (yv_need_hsb ? ctx->style.scrollbar_size : 0.0f);
                                    yv_need_sb = (yd->cached_nb_lines > yv_visible) ? 1 : 0;
                                    yv_sb_w = yv_need_sb ? ctx->style.scrollbar_size : 0.0f;
                                    if (yv_need_hsb && my > ay + yv_rows_h) {
                                        /* horizontal track click: jump + start drag */
                                        yd->h_scroll = _scrollbar_calc_scroll(mx, ax, yv_pane_w, yv_pane_w, yv_content_w, ctx->style.scrollbar_thumb_min);
                                        _syntaxview_clamp_h(yd, yv_content_w, yv_pane_w);
                                        yd->h_scroll_dragging = 1;
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    } else if (yv_need_sb && mx > ax + wgt->w - yv_sb_w) {
                                        yd->scroll_offset = _scrollbar_calc_scroll_int(my, ay, yv_rows_h, yv_visible, yd->cached_nb_lines, ctx->style.scrollbar_thumb_min);
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_DATAGRID) {
                                    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)wgt->data;
                                    ALLEGRO_FONT* gf = wgt->font ? wgt->font : ctx->default_font;
                                    float gfh = gf ? (float)al_get_font_line_height(gf) : 16.0f;
                                    float row_h = gfh + ctx->style.item_height_pad;
                                    float header_h = row_h;
                                    float data_h = wgt->h - header_h;
                                    int dg_visible = (int)(data_h / row_h);
                                    int dg_need_sb;
                                    float dg_sb_w;
                                    if (dg_visible < 1) dg_visible = 1;
                                    float dg_content_w = 0.0f, dg_pane_w = 0.0f, dg_hsb_h;
                                    int dg_need_hsb = 0;
                                    dg_need_sb = ((int)gd->nb_rows > dg_visible) ? 1 : 0;
                                    dg_sb_w = dg_need_sb ? ctx->style.scrollbar_size : 0.0f;
                                    _datagrid_hmetrics(wgt, gd, gf, &ctx->style, &dg_content_w, &dg_pane_w, &dg_need_hsb);
                                    dg_hsb_h = dg_need_hsb ? ctx->style.scrollbar_size : 0.0f;
                                    if (dg_need_hsb && my > ay + wgt->h - dg_hsb_h && mx < ax + dg_pane_w) {
                                        /* click/drag on the horizontal scrollbar along the bottom */
                                        gd->h_scroll = _scrollbar_calc_scroll(mx, ax, dg_pane_w, dg_pane_w, dg_content_w, ctx->style.scrollbar_thumb_min);
                                        gd->h_scroll_dragging = 1;
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    } else if (dg_need_sb && mx > ax + wgt->w - dg_sb_w && my >= ay + header_h) {
                                        gd->scroll_offset = _scrollbar_calc_scroll_int(my, ay + header_h, data_h, dg_visible, (int)gd->nb_rows, ctx->style.scrollbar_thumb_min);
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    } else if (my < ay + header_h) {
                                        /* header click: a drag within a small zone of a
                                           visible column's right border resizes it, else
                                           clicking a header cell sorts by that column */
                                        float bx = ax - gd->h_scroll;
                                        size_t cc;
                                        int hit = -1, resize_col = -1;
                                        for (cc = 0; cc < gd->nb_cols; cc++) {
                                            size_t pc = _datagrid_phys(gd, cc);
                                            float right;
                                            if (!gd->cols[pc].visible) continue;
                                            right = bx + gd->cols[pc].width;
                                            if (mx >= right - 4.0f && mx <= right + 4.0f) {
                                                resize_col = (int)pc;
                                                break;
                                            }
                                            if (mx >= bx && mx < right) {
                                                hit = (int)pc;
                                                break;
                                            }
                                            bx = right;
                                        }
                                        if (resize_col >= 0) {
                                            gd->col_resize_col = resize_col;
                                            gd->col_resize_x0 = (float)mx;
                                            gd->col_resize_w0 = gd->cols[resize_col].width;
                                            ctx->scrollbar_drag_widget_id = wgt->id;
                                        } else if (hit >= 0) {
                                            int dir = (gd->sort_col == hit && gd->sort_dir > 0) ? -1 : 1;
                                            n_gui_datagrid_sort(ctx, wgt->id, hit, dir);
                                        }
                                    } else {
                                        int clicked = gd->scroll_offset + (int)((my - (ay + header_h)) / row_h);
                                        if (clicked >= 0 && (size_t)clicked < gd->nb_rows) {
                                            if (gd->multiselect) {
                                                /* Ctrl toggles a row, Shift selects the range from the
                                                   anchor, a plain click selects only the clicked row */
                                                int ctrl = 0, shift = 0;
                                                if (al_is_keyboard_installed()) {
                                                    ALLEGRO_KEYBOARD_STATE kb;
                                                    al_get_keyboard_state(&kb);
                                                    ctrl = al_key_down(&kb, ALLEGRO_KEY_LCTRL) || al_key_down(&kb, ALLEGRO_KEY_RCTRL);
                                                    shift = al_key_down(&kb, ALLEGRO_KEY_LSHIFT) || al_key_down(&kb, ALLEGRO_KEY_RSHIFT);
                                                }
                                                _datagrid_ensure_sel(gd);
                                                if (gd->row_sel) {
                                                    if (shift && gd->anchor_row >= 0 && (size_t)gd->anchor_row < gd->nb_rows) {
                                                        int lo = gd->anchor_row < clicked ? gd->anchor_row : clicked;
                                                        int hi = gd->anchor_row < clicked ? clicked : gd->anchor_row;
                                                        int rr;
                                                        memset(gd->row_sel, 0, gd->rows_cap);
                                                        for (rr = lo; rr <= hi; rr++) gd->row_sel[rr] = 1;
                                                    } else if (ctrl) {
                                                        gd->row_sel[clicked] = gd->row_sel[clicked] ? 0 : 1;
                                                        gd->anchor_row = clicked;
                                                    } else {
                                                        memset(gd->row_sel, 0, gd->rows_cap);
                                                        gd->row_sel[clicked] = 1;
                                                        gd->anchor_row = clicked;
                                                    }
                                                }
                                            }
                                            gd->selected_row = clicked;
                                            if (gd->on_select) gd->on_select(wgt->id, clicked, gd->user_data);
                                        }
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_RADIOLIST) {
                                    N_GUI_RADIOLIST_DATA* rld = (N_GUI_RADIOLIST_DATA*)wgt->data;
                                    ALLEGRO_FONT* rf = wgt->font ? wgt->font : ctx->default_font;
                                    float rfh = rf ? (float)al_get_font_line_height(rf) : 16.0f;
                                    float rih = rld->item_height > rfh ? rld->item_height : rfh + ctx->style.item_height_pad;
                                    int rl_visible = (int)(wgt->h / rih);
                                    int rl_need_sb = ((int)rld->nb_items > rl_visible) ? 1 : 0;
                                    float rl_sb_w = rl_need_sb ? ctx->style.scrollbar_size : 0;
                                    /* clamp scroll_offset */
                                    int rl_max_off = (int)rld->nb_items - rl_visible;
                                    if (rl_max_off < 0) rl_max_off = 0;
                                    if (rld->scroll_offset > rl_max_off) rld->scroll_offset = rl_max_off;
                                    if (rld->scroll_offset < 0) rld->scroll_offset = 0;
                                    /* skip click if on the scrollbar area */
                                    if (rl_need_sb && mx > ax + wgt->w - rl_sb_w) {
                                        /* scrollbar track click: jump scroll position + start drag */
                                        rld->scroll_offset = _scrollbar_calc_scroll_int(my, ay, wgt->h, rl_visible, (int)rld->nb_items, ctx->style.scrollbar_thumb_min);
                                        ctx->scrollbar_drag_widget_id = wgt->id;
                                    } else {
                                        int clicked_idx = rld->scroll_offset + (int)((my - ay) / rih);
                                        if (clicked_idx >= 0 && (size_t)clicked_idx < rld->nb_items) {
                                            rld->selected_index = clicked_idx;
                                            if (rld->on_select) rld->on_select(wgt->id, clicked_idx, rld->user_data);
                                        }
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_LABEL) {
                                    N_GUI_LABEL_DATA* lbl = (N_GUI_LABEL_DATA*)wgt->data;
                                    if (lbl->link[0] && lbl->on_link_click) {
                                        lbl->on_link_click(wgt->id, lbl->link, lbl->user_data);
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_DROPMENU) {
                                    N_GUI_DROPMENU_DATA* dmd = (N_GUI_DROPMENU_DATA*)wgt->data;
                                    dmd->is_open = !dmd->is_open;
                                    if (dmd->is_open) {
                                        /* close any other open dropdown/combobox first */
                                        if (ctx->open_combobox_id >= 0) {
                                            N_GUI_WIDGET* old_cb = n_gui_get_widget(ctx, ctx->open_combobox_id);
                                            if (old_cb && old_cb->data) ((N_GUI_COMBOBOX_DATA*)old_cb->data)->is_open = 0;
                                            ctx->open_combobox_id = -1;
                                        }
                                        if (ctx->open_dropmenu_id >= 0 && ctx->open_dropmenu_id != wgt->id) {
                                            N_GUI_WIDGET* old_dm = n_gui_get_widget(ctx, ctx->open_dropmenu_id);
                                            if (old_dm && old_dm->data) ((N_GUI_DROPMENU_DATA*)old_dm->data)->is_open = 0;
                                        }
                                        ctx->open_dropmenu_id = wgt->id;
                                        /* nothing pre-highlighted until the user hovers, wheels, or arrows */
                                        dmd->highlight_index = -1;
                                        /* call on_open callback to rebuild dynamic entries */
                                        if (dmd->on_open) {
                                            dmd->on_open(wgt->id, dmd->on_open_user_data);
                                        }
                                    } else {
                                        ctx->open_dropmenu_id = -1;
                                    }
                                }
                            }
                            if (just_right_pressed && wgt->type == N_GUI_TYPE_DATAGRID && wgt->data) {
                                /* right-click a data row: select it and raise the
                                   context callback with the cursor position */
                                N_GUI_DATAGRID_DATA* gdc = (N_GUI_DATAGRID_DATA*)wgt->data;
                                ALLEGRO_FONT* gf = wgt->font ? wgt->font : ctx->default_font;
                                float gfh = gf ? (float)al_get_font_line_height(gf) : 16.0f;
                                float row_h = gfh + ctx->style.item_height_pad;
                                float header_h = row_h;
                                if (my >= ay + header_h) {
                                    int clicked = gdc->scroll_offset + (int)((my - (ay + header_h)) / row_h);
                                    if (clicked >= 0 && (size_t)clicked < gdc->nb_rows) {
                                        /* right-clicking a row outside the current multi-selection
                                           selects just that row; right-clicking inside it keeps the
                                           whole selection so the menu can act on all of it */
                                        if (gdc->multiselect) {
                                            _datagrid_ensure_sel(gdc);
                                            if (gdc->row_sel && !gdc->row_sel[clicked]) {
                                                memset(gdc->row_sel, 0, gdc->rows_cap);
                                                gdc->row_sel[clicked] = 1;
                                                gdc->anchor_row = clicked;
                                            }
                                        }
                                        gdc->selected_row = clicked;
                                        if (gdc->on_context) gdc->on_context(wgt->id, clicked, (int)mx, (int)my, gdc->user_data);
                                    }
                                }
                            }
                            break; /* only top widget gets the event */
                        }
                    }

                    /* frameless windows: drag from empty body area */
                    if ((win->flags & N_GUI_WIN_FRAMELESS) && just_pressed &&
                        !(win->flags & N_GUI_WIN_FIXED_POSITION) &&
                        !(win->state & (N_GUI_WIN_DRAGGING | N_GUI_WIN_MAXIMISED))) {
                        win->state |= N_GUI_WIN_DRAGGING;
                        win->drag_ox = mx - win->x;
                        win->drag_oy = my - win->y;
                        _native_drag_begin(win);
                    }
                }

            } /* end if (!captured_wnode) */

            /* clear titlebar button press if released outside the button */
            if (just_released && win->tb_buttons.pressed != N_GUI_TB_BTN_NONE) {
                win->tb_buttons.pressed = N_GUI_TB_BTN_NONE;
            }

            /* window dragging */
            if ((win->state & N_GUI_WIN_DRAGGING) && ctx->mouse_b1 && !(win->flags & N_GUI_WIN_FIXED_POSITION)) {
                if (_native_own_chrome(win)) {
                    /* Move the OS window and keep the pseudo-window pinned at
                     * the display origin. The target is absolute: the position
                     * the window sat at when the title bar was grabbed, plus how
                     * far the pointer has travelled on the desktop since. See
                     * _native_drag_begin for why a display-local delta cannot be
                     * used here. */
                    int nx = 0, ny = 0;
                    int have_target = 0;
                    if (win->native_drag_anchored) {
                        int cx = 0, cy = 0;
                        if (al_get_mouse_cursor_position(&cx, &cy)) {
                            nx = win->native_drag_wx + (cx - win->native_drag_cx);
                            ny = win->native_drag_wy + (cy - win->native_drag_cy);
                            have_target = 1;
                        }
                    }
                    if (!have_target) {
                        /* no desktop cursor coordinates on this platform: fall
                         * back to the display-local delta, which drags correctly
                         * as long as the events are not stale */
                        int dx = (int)(mx - win->drag_ox);
                        int dy = (int)(my - win->drag_oy);
                        if (dx != 0 || dy != 0) {
                            int wx = 0, wy = 0;
                            al_get_window_position(win->native, &wx, &wy);
                            nx = wx + dx;
                            ny = wy + dy;
                            have_target = 1;
                        }
                    }
                    /* only talk to the window manager on a real change, every
                     * al_set_window_position is a round trip */
                    if (have_target && (nx != win->native_pos_x || ny != win->native_pos_y)) {
                        al_set_window_position(win->native, nx, ny);
                        win->native_pos_x = nx;
                        win->native_pos_y = ny;
                    }
                } else {
                    win->x = mx - win->drag_ox;
                    win->y = my - win->drag_oy;
                }
            }
            if (just_released && (win->state & N_GUI_WIN_DRAGGING) && !(win->flags & N_GUI_WIN_FIXED_POSITION)) {
                win->state &= ~N_GUI_WIN_DRAGGING;
                win->native_drag_anchored = 0;
                if (_native_own_chrome(win)) {
                    /* the window manager is free to have constrained the move
                     * (screen edges, panels), so record where the window really
                     * ended up rather than the last position asked for */
                    int wx = 0, wy = 0;
                    al_get_window_position(win->native, &wx, &wy);
                    win->native_pos_x = wx;
                    win->native_pos_y = wy;
                }
                if (ctx->resize_mode == N_GUI_RESIZE_ADAPTIVE) {
                    _n_gui_window_capture_normalized(ctx, win);
                }
            }

            /* window resizing */
            if ((win->state & N_GUI_WIN_RESIZING) && ctx->mouse_b1) {
                float new_w = (mx - win->x) + win->drag_ox;
                float new_h = (my - win->y) + win->drag_oy;
                if (new_w < win->min_w) new_w = win->min_w;
                if (new_h < win->min_h) new_h = win->min_h;
                if (_native_own_chrome(win)) {
                    /* the grip resizes the OS window; only act on a real change
                     * so a motion-rate stream of al_resize_display calls (each
                     * one a round trip to the window manager) is avoided */
                    int nw = (int)(new_w + 0.5f);
                    int nh = (int)(new_h + 0.5f);
                    if (nw != (int)(win->w + 0.5f) || nh != (int)(win->h + 0.5f)) {
                        al_resize_display(win->native, nw, nh);
                        win->native_w = (float)nw;
                        win->native_h = (float)nh;
                    }
                    /* the DISPLAY_RESIZE that follows confirms these, setting
                       them now keeps the frame being drawn consistent */
                    new_w = (float)nw;
                    new_h = (float)nh;
                }
                win->w = new_w;
                win->h = new_h;
            }
            if (just_released && (win->state & N_GUI_WIN_RESIZING)) {
                win->state &= ~N_GUI_WIN_RESIZING;
                if (ctx->resize_mode == N_GUI_RESIZE_ADAPTIVE) {
                    _n_gui_window_capture_normalized(ctx, win);
                }
            }

            /* auto-scrollbar dragging */
            if ((win->state & (N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG)) && ctx->mouse_b1) {
                float scrollbar_size = ctx->style.scrollbar_size;
                float body_h = win->h - _win_tbh(win);
                float body_y = win->y + _win_tbh(win);
                int need_vscroll = 0, need_hscroll = 0;
                _window_update_content_size(win, ctx->default_font);
                if (win->content_h > body_h) need_vscroll = 1;
                if (win->content_w > win->w) need_hscroll = 1;
                if (need_vscroll && win->content_w > (win->w - scrollbar_size)) need_hscroll = 1;
                if (need_hscroll && win->content_h > (body_h - scrollbar_size)) need_vscroll = 1;
                float content_area_w = win->w - (need_vscroll ? scrollbar_size : 0);
                float content_area_h = body_h - (need_hscroll ? scrollbar_size : 0);

                if (win->state & N_GUI_WIN_VSCROLL_DRAG) {
                    float sb_y = body_y;
                    float sb_h = content_area_h;
                    if ((win->flags & N_GUI_WIN_RESIZABLE) && !need_hscroll) sb_h -= scrollbar_size;
                    win->scroll_y = _scrollbar_calc_scroll(my, sb_y, sb_h, content_area_h, win->content_h, ctx->style.scrollbar_thumb_min);
                }
                if (win->state & N_GUI_WIN_HSCROLL_DRAG) {
                    float sb_x = win->x;
                    float sb_w = content_area_w;
                    if ((win->flags & N_GUI_WIN_RESIZABLE) && !need_vscroll) sb_w -= scrollbar_size;
                    win->scroll_x = _scrollbar_calc_scroll(mx, sb_x, sb_w, content_area_w, win->content_w, ctx->style.scrollbar_thumb_min);
                }
            }
            if (just_released && (win->state & (N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG))) {
                win->state &= ~(N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG);
            }
        } else {
            /* clicked outside all windows */
            if (just_pressed) {
                if (ctx->focused_widget_id != -1) {
                    N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
                    if (fw) fw->state &= ~N_GUI_STATE_FOCUSED;
                    ctx->focused_widget_id = -1;
                }
            }
        }

        /* handle active slider/scrollbar drag even outside widget bounds */
        if (ctx->mouse_b1 && event.type == ALLEGRO_EVENT_MOUSE_AXES) {
            list_foreach(wnode, ctx->windows) {
                N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
                /* display routing: only windows on the pass this event belongs to */
                if (!_win_on_pass(ctx, win)) continue;
                if (!(win->state & N_GUI_WIN_OPEN)) continue;
                float content_x = win->x - win->scroll_x;
                float content_y = win->y + _win_tbh(win) - win->scroll_y;
                list_foreach(wgn, win->widgets) {
                    N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                    if (!wgt) continue;
                    if (wgt->state & N_GUI_STATE_ACTIVE) {
                        if (wgt->type == N_GUI_TYPE_SLIDER) {
                            _slider_update_from_mouse(wgt, mx, my, content_x, content_y);
                        }
                        if (wgt->type == N_GUI_TYPE_SCROLLBAR) {
                            _scrollbar_update_from_mouse(wgt, mx, my, content_x, content_y, &ctx->style);
                        }
                        if (wgt->type == N_GUI_TYPE_SYNTAXVIEW) {
                            N_GUI_SYNTAXVIEW_DATA* drag_yd = (N_GUI_SYNTAXVIEW_DATA*)wgt->data;
                            if (drag_yd && drag_yd->sel_dragging) {
                                ALLEGRO_FONT* yf = wgt->font ? wgt->font : ctx->default_font;
                                if (yf && drag_yd->text) {
                                    float yax = content_x + wgt->x;
                                    float yay = content_y + wgt->y;
                                    drag_yd->sel_end = _syntaxview_offset_from_mouse(drag_yd, yf, mx, my, yax, yay, ctx->style.textarea_padding);
                                }
                            }
                        }
                        if (wgt->type == N_GUI_TYPE_LABEL) {
                            N_GUI_LABEL_DATA* drag_lb = (N_GUI_LABEL_DATA*)wgt->data;
                            if (drag_lb && drag_lb->sel_dragging) {
                                ALLEGRO_FONT* lf = wgt->font ? wgt->font : ctx->default_font;
                                if (lf) {
                                    float lax = content_x + wgt->x;
                                    float lay = content_y + wgt->y;
                                    int char_pos;
                                    if (drag_lb->align == N_GUI_ALIGN_JUSTIFIED) {
                                        float lpad = ctx->style.label_padding;
                                        float effective_w = wgt->w;
                                        float lww = 0;
                                        if (win->flags & N_GUI_WIN_AUTO_SCROLLBAR)
                                            lww = win->content_w + ctx->style.title_max_w_reserve + lpad;
                                        else if (win->flags & N_GUI_WIN_RESIZABLE)
                                            lww = win->w;
                                        if (lww > 0) {
                                            float mfw = lww - wgt->x;
                                            if (mfw > effective_w) effective_w = mfw;
                                        }
                                        float max_text_w = effective_w - lpad * 2;
                                        float content_h2 = _label_content_height(drag_lb->text, lf, max_text_w);
                                        float view_h = wgt->h - lpad;
                                        float sb_sz = (content_h2 > view_h) ? ctx->style.scrollbar_size : 0;
                                        float tw2 = max_text_w - sb_sz;
                                        char_pos = _justified_char_at_pos(drag_lb->text, lf, tw2,
                                                                          lax + lpad, lay + lpad / 2.0f, drag_lb->scroll_y, mx, my);
                                    } else {
                                        float lww = 0;
                                        if (win->flags & N_GUI_WIN_AUTO_SCROLLBAR)
                                            lww = win->content_w + ctx->style.title_max_w_reserve + ctx->style.label_padding;
                                        else if (win->flags & N_GUI_WIN_RESIZABLE)
                                            lww = win->w;
                                        float text_ox = _label_text_origin_x(drag_lb, lf, lax, wgt->w, lww, wgt->x, ctx->style.label_padding);
                                        float click_x = mx - text_ox;
                                        char_pos = _label_char_at_x(drag_lb, lf, click_x);
                                    }
                                    if (char_pos >= 0) {
                                        drag_lb->sel_end = char_pos;
                                    }
                                }
                            }
                        }
                        if (wgt->type == N_GUI_TYPE_TEXTAREA) {
                            N_GUI_TEXTAREA_DATA* drag_td = (N_GUI_TEXTAREA_DATA*)wgt->data;
                            ALLEGRO_FONT* tf = wgt->font ? wgt->font : ctx->default_font;
                            if (tf && drag_td) {
                                float ta_ax = content_x + wgt->x;
                                float ta_ay = content_y + wgt->y;
                                float text_pad = ctx->style.textarea_padding;
                                size_t pos = _textarea_pos_from_mouse(drag_td, tf, mx, my,
                                                                      ta_ax, ta_ay, wgt->w, wgt->h,
                                                                      text_pad, ctx->style.scrollbar_size);
                                drag_td->cursor_pos = pos;
                                drag_td->sel_end = pos;
                                drag_td->cursor_time = al_get_time();
                            }
                        }
                    }
                }
            }
        }

        /* release active state on mouse up */
        if (just_released) {
            /* A button's on_click may open/raise a window, which reorders
             * ctx->windows (n_gui_raise_window removes + re-pushes + re-sorts).
             * Firing it inside the loops below would free or relink the node the
             * list_foreach iterator has already cached as "next", giving a
             * use-after-free on the following step. Defer the single released
             * button's callback until after both loops finish. */
            N_GUI_BUTTON_DATA* pending_click = NULL;
            int pending_click_id = -1;
            list_foreach(wnode, ctx->windows) {
                N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
                /* display routing: only windows on the pass this event belongs to */
                if (!_win_on_pass(ctx, win)) continue;
                list_foreach(wgn, win->widgets) {
                    N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                    if (!wgt) continue;
                    if (wgt->state & N_GUI_STATE_ACTIVE) {
                        /* fire button callback on release if still hovering */
                        if (wgt->type == N_GUI_TYPE_BUTTON) {
                            float content_x = win->x - win->scroll_x;
                            float content_y = win->y + _win_tbh(win) - win->scroll_y;
                            float ax = content_x + wgt->x;
                            float ay = content_y + wgt->y;
                            if (_point_in_rect(mx, my, ax, ay, wgt->w, wgt->h)) {
                                N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
                                /* Toggle mode: flip toggled state on each click */
                                if (bd->toggle_mode) {
                                    bd->toggled = !bd->toggled;
                                }
                                if (bd->on_click && !pending_click) {
                                    pending_click = bd;
                                    pending_click_id = wgt->id;
                                }
                            }
                        }
                        /* stop label drag selection on release */
                        if (wgt->type == N_GUI_TYPE_LABEL) {
                            N_GUI_LABEL_DATA* rel_lb = (N_GUI_LABEL_DATA*)wgt->data;
                            if (rel_lb) rel_lb->sel_dragging = 0;
                        }
                        /* stop syntax-view drag selection on release */
                        if (wgt->type == N_GUI_TYPE_SYNTAXVIEW) {
                            N_GUI_SYNTAXVIEW_DATA* rel_yd = (N_GUI_SYNTAXVIEW_DATA*)wgt->data;
                            if (rel_yd) rel_yd->sel_dragging = 0;
                        }
                        wgt->state &= ~N_GUI_STATE_ACTIVE;
                    }
                }
            }
            if (pending_click && pending_click->on_click)
                pending_click->on_click(pending_click_id, pending_click->user_data);
            /* select-to-copy: a finished mouse drag-selection goes to PRIMARY */
            _ngui_publish_primary_selection(ctx);
        }
    }

    /* Tab / Shift+Tab: focus navigation across widgets in the active window.
     * Ctrl+Tab in a textarea inserts a literal tab, handled below by the textarea key handler. */
    /* keyboard navigation for an open combobox / dropmenu panel: Up/Down (and
       Home/End) move the highlight, Enter commits it, Escape closes without a change.
       Keyed off the open-panel state (not focus) so it works the moment the panel
       opens. KEY_CHAR so a held key auto-repeats. Takes priority over Tab below. */
    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_CHAR &&
        (ctx->open_combobox_id >= 0 || ctx->open_dropmenu_id >= 0)) {
        int kc = event.keyboard.keycode;
        if (ctx->open_combobox_id >= 0) {
            N_GUI_WIDGET* w = n_gui_get_widget(ctx, ctx->open_combobox_id);
            N_GUI_COMBOBOX_DATA* cd = (w && w->data) ? (N_GUI_COMBOBOX_DATA*)w->data : NULL;
            if (cd && cd->is_open) {
                int nb = (int)cd->nb_items;
                if (kc == ALLEGRO_KEY_UP) {
                    _list_panel_highlight_step(&cd->highlight_index, &cd->scroll_offset, nb, cd->max_visible, -1, cd->selected_index);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_DOWN) {
                    _list_panel_highlight_step(&cd->highlight_index, &cd->scroll_offset, nb, cd->max_visible, 1, cd->selected_index);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_HOME) {
                    _list_panel_highlight_step(&cd->highlight_index, &cd->scroll_offset, nb, cd->max_visible, -nb, cd->selected_index);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_END) {
                    _list_panel_highlight_step(&cd->highlight_index, &cd->scroll_offset, nb, cd->max_visible, nb, cd->selected_index);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_ENTER || kc == ALLEGRO_KEY_PAD_ENTER) {
                    if (cd->highlight_index >= 0 && cd->highlight_index < nb) {
                        cd->selected_index = cd->highlight_index;
                        if (cd->on_select) cd->on_select(w->id, cd->selected_index, cd->user_data);
                    }
                    cd->is_open = 0;
                    ctx->open_combobox_id = -1;
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_ESCAPE) {
                    cd->is_open = 0;
                    ctx->open_combobox_id = -1;
                    event_consumed = 1;
                }
            }
        }
        if (!event_consumed && ctx->open_dropmenu_id >= 0) {
            N_GUI_WIDGET* w = n_gui_get_widget(ctx, ctx->open_dropmenu_id);
            N_GUI_DROPMENU_DATA* dd = (w && w->data) ? (N_GUI_DROPMENU_DATA*)w->data : NULL;
            if (dd && dd->is_open) {
                int nb = (int)dd->nb_entries;
                if (kc == ALLEGRO_KEY_UP) {
                    _list_panel_highlight_step(&dd->highlight_index, &dd->scroll_offset, nb, dd->max_visible, -1, -1);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_DOWN) {
                    _list_panel_highlight_step(&dd->highlight_index, &dd->scroll_offset, nb, dd->max_visible, 1, -1);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_HOME) {
                    _list_panel_highlight_step(&dd->highlight_index, &dd->scroll_offset, nb, dd->max_visible, -nb, -1);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_END) {
                    _list_panel_highlight_step(&dd->highlight_index, &dd->scroll_offset, nb, dd->max_visible, nb, -1);
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_ENTER || kc == ALLEGRO_KEY_PAD_ENTER) {
                    if (dd->highlight_index >= 0 && dd->highlight_index < nb) {
                        N_GUI_DROPMENU_ENTRY* entry = &dd->entries[dd->highlight_index];
                        if (entry->on_click) entry->on_click(w->id, dd->highlight_index, entry->tag, entry->user_data);
                    }
                    dd->is_open = 0;
                    ctx->open_dropmenu_id = -1;
                    event_consumed = 1;
                } else if (kc == ALLEGRO_KEY_ESCAPE) {
                    dd->is_open = 0;
                    ctx->open_dropmenu_id = -1;
                    event_consumed = 1;
                }
            }
        }
    }

    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_CHAR && event.keyboard.keycode == ALLEGRO_KEY_TAB) {
        int ctrl = (event.keyboard.modifiers & ALLEGRO_KEYMOD_CTRL) ? 1 : 0;
        int shift = (event.keyboard.modifiers & ALLEGRO_KEYMOD_SHIFT) ? 1 : 0;

        /* Ctrl+Tab in a focused textarea inserts a tab character, skip navigation */
        int is_ctrl_tab_in_textarea = 0;
        if (ctrl && ctx->focused_widget_id >= 0) {
            const N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
            if (fw && fw->type == N_GUI_TYPE_TEXTAREA && (fw->state & N_GUI_STATE_FOCUSED))
                is_ctrl_tab_in_textarea = 1;
        }

        if (!is_ctrl_tab_in_textarea) {
            /* find the window that contains the focused widget (or the topmost window) */
            N_GUI_WINDOW* tab_win = NULL;
            if (ctx->focused_widget_id >= 0) {
                tab_win = _find_focused_window(ctx);
            }
            if (!tab_win && ctx->windows && ctx->windows->end) {
                /* no focus yet, use topmost open window */
                for (LIST_NODE* wn = ctx->windows->end; wn; wn = wn->prev) {
                    N_GUI_WINDOW* w = (N_GUI_WINDOW*)wn->ptr;
                    /* display routing: only windows on the pass this event belongs to */
                    if (!_win_on_pass(ctx, w)) continue;
                    if (w && (w->state & N_GUI_WIN_OPEN) && !(w->state & N_GUI_WIN_MINIMISED)) {
                        tab_win = w;
                        break;
                    }
                }
            }

            if (tab_win && tab_win->widgets && tab_win->widgets->nb_items > 0) {
                /* build an ordered list of focusable widget ids */
                int focusable_ids[512];
                int nfocusable = 0;
                list_foreach(wgn, tab_win->widgets) {
                    const N_GUI_WIDGET* w = (N_GUI_WIDGET*)wgn->ptr;
                    if (w && w->visible && w->enabled && _is_focusable_type(w->type) && nfocusable < 512) {
                        focusable_ids[nfocusable++] = w->id;
                    }
                }
                if (nfocusable > 0) {
                    /* find current position */
                    int cur_idx = -1;
                    for (int i = 0; i < nfocusable; i++) {
                        if (focusable_ids[i] == ctx->focused_widget_id) {
                            cur_idx = i;
                            break;
                        }
                    }
                    int next_idx;
                    if (shift) {
                        next_idx = (cur_idx <= 0) ? nfocusable - 1 : cur_idx - 1;
                    } else {
                        next_idx = (cur_idx < 0 || cur_idx >= nfocusable - 1) ? 0 : cur_idx + 1;
                    }
                    n_gui_set_focus(ctx, focusable_ids[next_idx]);
                }
            }
            event_consumed = 1;
        }
    }

    /* keyboard events -> focused textarea */
    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_CHAR && ctx->focused_widget_id != -1) {
        N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
        if (fw && fw->type == N_GUI_TYPE_TEXTAREA && (fw->state & N_GUI_STATE_FOCUSED)) {
            ALLEGRO_FONT* ta_font = fw->font ? fw->font : ctx->default_font;
            float ta_pad = ctx->style.textarea_padding;
            float ta_sb = ctx->style.scrollbar_size;
            if (_textarea_handle_key(fw, &event, ta_font, ta_pad, ta_sb, ctx)) {
                event_consumed = 1;
                /* select-to-copy: a shift-navigation selection goes to PRIMARY */
                if (event.keyboard.modifiers & ALLEGRO_KEYMOD_SHIFT)
                    _ngui_publish_primary_selection(ctx);
            }
        }
    }

    /* keyboard events -> focused non-textarea widgets (slider, listbox, radiolist, combobox, scrollbar, checkbox) */
    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_CHAR && ctx->focused_widget_id >= 0) {
        N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
        if (fw && fw->visible && fw->enabled && (fw->state & N_GUI_STATE_FOCUSED) && fw->data) {
            int kc = event.keyboard.keycode;

            /* slider keyboard */
            if (fw->type == N_GUI_TYPE_SLIDER) {
                N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)fw->data;
                double step = sd->step > 0.0 ? sd->step : (sd->max_val - sd->min_val) / 20.0;
                if (step <= 0) step = 1.0;
                double new_val = sd->value;
                int handled = 0;

                if (sd->orientation == N_GUI_SLIDER_H) {
                    if (kc == ALLEGRO_KEY_RIGHT) {
                        new_val += step;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_LEFT) {
                        new_val -= step;
                        handled = 1;
                    }
                } else {
                    if (kc == ALLEGRO_KEY_UP) {
                        new_val += step;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_DOWN) {
                        new_val -= step;
                        handled = 1;
                    }
                }
                if (kc == ALLEGRO_KEY_HOME) {
                    new_val = sd->min_val;
                    handled = 1;
                } else if (kc == ALLEGRO_KEY_END) {
                    new_val = sd->max_val;
                    handled = 1;
                }

                if (handled) {
                    if (sd->step > 0.0)
                        new_val = _slider_snap_value(new_val, sd->min_val, sd->max_val, sd->step);
                    else
                        new_val = _clamp(new_val, sd->min_val, sd->max_val);
                    if (new_val != sd->value) {
                        sd->value = new_val;
                        if (sd->on_change) sd->on_change(fw->id, sd->value, sd->user_data);
                    }
                    event_consumed = 1;
                }
            }

            /* listbox keyboard */
            if (fw->type == N_GUI_TYPE_LISTBOX) {
                N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)fw->data;
                if (ld->selection_mode != N_GUI_SELECT_NONE && ld->nb_items > 0) {
                    int handled = 0;
                    /* find first selected item for single-select navigation */
                    int cur_sel = -1;
                    if (ld->selection_mode == N_GUI_SELECT_SINGLE) {
                        for (size_t i = 0; i < ld->nb_items; i++) {
                            if (ld->items[i].selected) {
                                cur_sel = (int)i;
                                break;
                            }
                        }
                    }
                    int new_sel = cur_sel;
                    if (kc == ALLEGRO_KEY_UP && cur_sel > 0) {
                        new_sel = cur_sel - 1;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_DOWN && cur_sel < (int)ld->nb_items - 1) {
                        new_sel = cur_sel + 1;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_DOWN && cur_sel < 0) {
                        new_sel = 0;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_HOME) {
                        new_sel = 0;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_END) {
                        new_sel = (int)ld->nb_items - 1;
                        handled = 1;
                    }

                    if (handled && ld->selection_mode == N_GUI_SELECT_SINGLE && new_sel != cur_sel) {
                        for (size_t i = 0; i < ld->nb_items; i++) ld->items[i].selected = 0;
                        ld->items[new_sel].selected = 1;
                        if (ld->on_select) ld->on_select(fw->id, new_sel, 1, ld->user_data);
                        /* auto-scroll to keep selection visible */
                        ALLEGRO_FONT* lf = fw->font ? fw->font : ctx->default_font;
                        float lfh = lf ? (float)al_get_font_line_height(lf) : 16.0f;
                        float lih = ld->item_height > lfh ? ld->item_height : lfh + ctx->style.item_height_pad;
                        int visible = (int)(fw->h / lih);
                        if (new_sel < ld->scroll_offset) ld->scroll_offset = new_sel;
                        if (new_sel >= ld->scroll_offset + visible) ld->scroll_offset = new_sel - visible + 1;
                        event_consumed = 1;
                    }
                }
            }

            /* radiolist keyboard */
            if (fw->type == N_GUI_TYPE_RADIOLIST) {
                N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)fw->data;
                if (rd->nb_items > 0) {
                    int cur_sel = rd->selected_index;
                    int new_sel = cur_sel;
                    int handled = 0;
                    if (kc == ALLEGRO_KEY_UP && cur_sel > 0) {
                        new_sel = cur_sel - 1;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_DOWN && cur_sel < (int)rd->nb_items - 1) {
                        new_sel = cur_sel + 1;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_DOWN && cur_sel < 0) {
                        new_sel = 0;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_HOME) {
                        new_sel = 0;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_END) {
                        new_sel = (int)rd->nb_items - 1;
                        handled = 1;
                    }

                    if (handled && new_sel != cur_sel) {
                        rd->selected_index = new_sel;
                        if (rd->on_select) rd->on_select(fw->id, new_sel, rd->user_data);
                        /* auto-scroll to keep selection visible */
                        ALLEGRO_FONT* rf = fw->font ? fw->font : ctx->default_font;
                        float rfh = rf ? (float)al_get_font_line_height(rf) : 16.0f;
                        float rih = rd->item_height > rfh ? rd->item_height : rfh + ctx->style.item_height_pad;
                        int visible = (int)(fw->h / rih);
                        if (new_sel < rd->scroll_offset) rd->scroll_offset = new_sel;
                        if (new_sel >= rd->scroll_offset + visible) rd->scroll_offset = new_sel - visible + 1;
                        event_consumed = 1;
                    }
                }
            }

            /* combobox keyboard (when closed) */
            if (fw->type == N_GUI_TYPE_COMBOBOX) {
                N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)fw->data;
                if (!cd->is_open && cd->nb_items > 0) {
                    int cur_sel = cd->selected_index;
                    int new_sel = cur_sel;
                    int handled = 0;
                    if (kc == ALLEGRO_KEY_UP && cur_sel > 0) {
                        new_sel = cur_sel - 1;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_DOWN && cur_sel < (int)cd->nb_items - 1) {
                        new_sel = cur_sel + 1;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_DOWN && cur_sel < 0) {
                        new_sel = 0;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_HOME) {
                        new_sel = 0;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_END) {
                        new_sel = (int)cd->nb_items - 1;
                        handled = 1;
                    }

                    if (handled && new_sel != cur_sel) {
                        cd->selected_index = new_sel;
                        if (cd->on_select) cd->on_select(fw->id, new_sel, cd->user_data);
                        event_consumed = 1;
                    }
                }
            }

            /* scrollbar keyboard */
            if (fw->type == N_GUI_TYPE_SCROLLBAR) {
                N_GUI_SCROLLBAR_DATA* sb = (N_GUI_SCROLLBAR_DATA*)fw->data;
                double max_scroll = sb->content_size - sb->viewport_size;
                if (max_scroll > 0) {
                    double step = max_scroll / 20.0;
                    if (step <= 0) step = 1.0;
                    double new_pos = sb->scroll_pos;
                    int handled = 0;
                    if (sb->orientation == N_GUI_SCROLLBAR_V) {
                        if (kc == ALLEGRO_KEY_UP) {
                            new_pos -= step;
                            handled = 1;
                        } else if (kc == ALLEGRO_KEY_DOWN) {
                            new_pos += step;
                            handled = 1;
                        }
                    } else {
                        if (kc == ALLEGRO_KEY_LEFT) {
                            new_pos -= step;
                            handled = 1;
                        } else if (kc == ALLEGRO_KEY_RIGHT) {
                            new_pos += step;
                            handled = 1;
                        }
                    }
                    if (kc == ALLEGRO_KEY_HOME) {
                        new_pos = 0;
                        handled = 1;
                    } else if (kc == ALLEGRO_KEY_END) {
                        new_pos = max_scroll;
                        handled = 1;
                    }

                    if (handled) {
                        if (new_pos < 0) new_pos = 0;
                        if (new_pos > max_scroll) new_pos = max_scroll;
                        if (new_pos != sb->scroll_pos) {
                            sb->scroll_pos = new_pos;
                            if (sb->on_scroll) sb->on_scroll(fw->id, sb->scroll_pos, sb->user_data);
                        }
                        event_consumed = 1;
                    }
                }
            }

            /* checkbox keyboard (Space/Enter to toggle) */
            if (fw->type == N_GUI_TYPE_CHECKBOX) {
                if (kc == ALLEGRO_KEY_SPACE || kc == ALLEGRO_KEY_ENTER) {
                    N_GUI_CHECKBOX_DATA* cd = (N_GUI_CHECKBOX_DATA*)fw->data;
                    cd->checked = !cd->checked;
                    if (cd->on_toggle) cd->on_toggle(fw->id, cd->checked, cd->user_data);
                    event_consumed = 1;
                }
            }
        }
    }

    /* Ctrl+C on label selection: copy selected text from the most recently selected label */
    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_DOWN &&
        (event.keyboard.modifiers & ALLEGRO_KEYMOD_CTRL) &&
        event.keyboard.keycode == ALLEGRO_KEY_C && _ctx_io_display(ctx) &&
        ctx->selected_label_id >= 0) {
        N_GUI_WIDGET* sel_wgt = n_gui_get_widget(ctx, ctx->selected_label_id);
        if (sel_wgt && sel_wgt->type == N_GUI_TYPE_LABEL && sel_wgt->data) {
            N_GUI_LABEL_DATA* lb = (N_GUI_LABEL_DATA*)sel_wgt->data;
            if (lb->sel_start >= 0 && lb->sel_end >= 0 && lb->sel_start != lb->sel_end) {
                int slo = lb->sel_start < lb->sel_end ? lb->sel_start : lb->sel_end;
                int shi = lb->sel_start < lb->sel_end ? lb->sel_end : lb->sel_start;
                size_t tlen = strlen(lb->text);
                if ((size_t)slo > tlen) slo = (int)tlen;
                if ((size_t)shi > tlen) shi = (int)tlen;
                int len = shi - slo;
                if (len > 0) {
                    char clip[N_GUI_TEXT_MAX];
                    memcpy(clip, &lb->text[slo], (size_t)len);
                    clip[len] = '\0';
                    _ngui_clip_set(_ctx_io_display(ctx), N_CLIPBOARD_CLIPBOARD, clip);
                    event_consumed = 1;
                }
            }
        }
    }

    /* Ctrl+A selects all and Ctrl+C copies the selection of the active syntax view */
    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_DOWN &&
        (event.keyboard.modifiers & ALLEGRO_KEYMOD_CTRL) &&
        ctx->selected_syntaxview_id >= 0 &&
        (event.keyboard.keycode == ALLEGRO_KEY_C || event.keyboard.keycode == ALLEGRO_KEY_A)) {
        if (event.keyboard.keycode == ALLEGRO_KEY_A) {
            n_gui_syntaxview_select_all(ctx, ctx->selected_syntaxview_id);
            _ngui_publish_primary_selection(ctx); /* select-to-copy: mirror to PRIMARY */
            event_consumed = 1;
        } else if (_ctx_io_display(ctx)) {
            char* sel = n_gui_syntaxview_get_selected_text(ctx, ctx->selected_syntaxview_id);
            if (sel) {
                _ngui_clip_set(_ctx_io_display(ctx), N_CLIPBOARD_CLIPBOARD, sel);
                Free(sel);
                event_consumed = 1;
            }
        }
    }

    /* keyboard events -> button key bindings.
     * Two passes:
     * 1. Focused bindings (key_focus_only=1): fire only when the focused widget
     *    is the button itself or one of the button's key_sources. These are
     *    checked first and bypass the widget_focused guard so they work even
     *    when a textarea has focus.
     * 2. Global bindings (key_focus_only=0): fire when no interactive widget
     *    has focus. Exception: ENTER passes through single-line textareas. */
    if (event.type == ALLEGRO_EVENT_KEY_DOWN && !event_consumed) {
        int kc = event.keyboard.keycode;
        int ev_mods = event.keyboard.modifiers & N_GUI_KEY_MOD_MASK;

        /* Pass 1: focused key bindings, check before the widget_focused guard */
        for (LIST_NODE* wnode = ctx->windows->end; wnode && !event_consumed; wnode = wnode->prev) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            /* display routing: only windows on the pass this event belongs to */
            if (!_win_on_pass(ctx, win)) continue;
            if (!(win->state & N_GUI_WIN_OPEN)) {
                continue;
            }
            list_foreach(wgn, win->widgets) {
                N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                if (!wgt || !wgt->visible || !wgt->enabled || wgt->type != N_GUI_TYPE_BUTTON) {
                    continue;
                }
                N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
                if (!bd || !bd->key_focus_only || bd->keycode != kc) {
                    continue;
                }
                if (bd->key_modifiers != 0 && ev_mods != bd->key_modifiers) {
                    continue;
                }
                /* check if focused widget is this button or a listed source */
                int focus_match = (ctx->focused_widget_id == wgt->id);
                if (!focus_match) {
                    for (int i = 0; i < N_GUI_KEY_SOURCES_MAX && bd->key_sources[i] >= 0; i++) {
                        if (ctx->focused_widget_id == bd->key_sources[i]) {
                            focus_match = 1;
                            break;
                        }
                    }
                }
                if (!focus_match) {
                    continue;
                }
                if (bd->toggle_mode) {
                    bd->toggled = !bd->toggled;
                }
                /* keybind-triggered press: flash the active visual briefly so the
                 * user sees the button react just like on a mouse click. */
                bd->key_press_until = al_get_time() + N_GUI_KEY_PRESS_FLASH_SEC;
                if (bd->key_press_until + N_GUI_ANIM_TAIL_SEC > ctx->anim_until) ctx->anim_until = bd->key_press_until + N_GUI_ANIM_TAIL_SEC; /* keep redrawing through the flash */
                if (bd->on_click) {
                    bd->on_click(wgt->id, bd->user_data);
                }
                event_consumed = 1;
                break;
            }
            if (event_consumed) {
                /* the on_click callback may have mutated ctx->windows,
                 * n_gui_add_window's z-order re-sort rebuilds the list and
                 * frees every node, so wnode must not be stepped by the
                 * for-increment. Exit the window loop without touching it. */
                break;
            }
        }

        /* Pass 2: global key bindings, skip when an interactive widget has focus.
         * Exception: for single-line textareas, ENTER passes through. */
        if (!event_consumed) {
            int widget_focused = 0;
            if (ctx->focused_widget_id >= 0) {
                N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
                if (fw && (fw->state & N_GUI_STATE_FOCUSED)) {
                    if (fw->type == N_GUI_TYPE_TEXTAREA) {
                        const N_GUI_TEXTAREA_DATA* ftd = (N_GUI_TEXTAREA_DATA*)fw->data;
                        /* for single-line textareas, allow ENTER to pass through */
                        if (ftd && (ftd->multiline || kc != ALLEGRO_KEY_ENTER)) {
                            widget_focused = 1;
                        }
                    } else if (_is_focusable_type(fw->type)) {
                        widget_focused = 1;
                    }
                }
            }
            if (!widget_focused) {
                /* Iterate windows from frontmost (end of list) to backmost (start)
                 * and stop as soon as a button consumes the event. */
                for (LIST_NODE* wnode = ctx->windows->end; wnode && !event_consumed; wnode = wnode->prev) {
                    N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
                    /* display routing: only windows on the pass this event belongs to */
                    if (!_win_on_pass(ctx, win)) continue;
                    if (!(win->state & N_GUI_WIN_OPEN)) {
                        continue;
                    }
                    list_foreach(wgn, win->widgets) {
                        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                        if (!wgt || !wgt->visible || !wgt->enabled || wgt->type != N_GUI_TYPE_BUTTON) {
                            continue;
                        }
                        N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
                        if (bd && !bd->key_focus_only && bd->keycode == kc) {
                            if (bd->key_modifiers != 0 && ev_mods != bd->key_modifiers) {
                                continue;
                            }
                            if (bd->toggle_mode) {
                                bd->toggled = !bd->toggled;
                            }
                            /* keybind-triggered press: flash the active visual
                             * briefly so the user sees the button react just
                             * like on a mouse click. */
                            bd->key_press_until = al_get_time() + N_GUI_KEY_PRESS_FLASH_SEC;
                            if (bd->key_press_until + N_GUI_ANIM_TAIL_SEC > ctx->anim_until) ctx->anim_until = bd->key_press_until + N_GUI_ANIM_TAIL_SEC; /* keep redrawing through the flash */
                            if (bd->on_click) {
                                bd->on_click(wgt->id, bd->user_data);
                            }
                            event_consumed = 1;
                            break;
                        }
                    }
                    if (event_consumed) {
                        /* same node-invalidation guard as pass 1 above:
                         * the callback may have rebuilt ctx->windows. */
                        break;
                    }
                }
            }
        }
    }

    /* mouse wheel scrolling for listbox/radiolist/combobox/dropmenu and auto-scrollbar windows.
     * Iterate from topmost window (end of list) to bottommost (start) so that the
     * frontmost window under the cursor receives the scroll event first. */
    if (event.type == ALLEGRO_EVENT_MOUSE_AXES && event.mouse.dz != 0) {
        int scroll_consumed = 0;

        /* An open combobox consumes the wheel to step its highlight (scrolling to
           follow), even when the pointer is not directly over the floating panel. */
        if (ctx->open_combobox_id >= 0) {
            N_GUI_WIDGET* cb_wgt = n_gui_get_widget(ctx, ctx->open_combobox_id);
            if (cb_wgt && cb_wgt->data) {
                N_GUI_COMBOBOX_DATA* cbd = (N_GUI_COMBOBOX_DATA*)cb_wgt->data;
                if (cbd->is_open && cbd->nb_items > 0) {
                    _list_panel_highlight_step(&cbd->highlight_index, &cbd->scroll_offset,
                                               (int)cbd->nb_items, cbd->max_visible,
                                               -event.mouse.dz, cbd->selected_index);
                    scroll_consumed = 1;
                }
            }
        }
        /* Likewise an open dropmenu panel. */
        if (!scroll_consumed && ctx->open_dropmenu_id >= 0) {
            N_GUI_WIDGET* dm_wgt = n_gui_get_widget(ctx, ctx->open_dropmenu_id);
            if (dm_wgt && dm_wgt->data) {
                N_GUI_DROPMENU_DATA* dmd = (N_GUI_DROPMENU_DATA*)dm_wgt->data;
                if (dmd->is_open && dmd->nb_entries > 0) {
                    _list_panel_highlight_step(&dmd->highlight_index, &dmd->scroll_offset,
                                               (int)dmd->nb_entries, dmd->max_visible,
                                               -event.mouse.dz, -1);
                    scroll_consumed = 1;
                }
            }
        }
        if (scroll_consumed) return 1;

        for (LIST_NODE* wnode = ctx->windows->end; wnode; wnode = wnode->prev) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            /* display routing: only windows on the pass this event belongs to */
            if (!_win_on_pass(ctx, win)) continue;
            if (!(win->state & N_GUI_WIN_OPEN) || (win->state & N_GUI_WIN_MINIMISED)) continue;

            float win_h = win->h;
            if (!_point_in_rect(mx, my, win->x, win->y, win->w, win_h)) continue;

            float content_x = win->x;
            float content_y = win->y + _win_tbh(win);

            /* first check if a widget wants the scroll */
            list_foreach(wgn, win->widgets) {
                N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                if (!wgt || !wgt->visible || !wgt->enabled) continue;
                float ax = content_x + wgt->x - win->scroll_x;
                float ay = content_y + wgt->y - win->scroll_y;
                if (!_point_in_rect(mx, my, ax, ay, wgt->w, wgt->h)) continue;

                if (wgt->type == N_GUI_TYPE_LISTBOX) {
                    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)wgt->data;
                    ALLEGRO_FONT* lf = wgt->font ? wgt->font : ctx->default_font;
                    float lb_content_w = 0.0f, lb_pane_w = wgt->w;
                    int lb_need_hsb = 0, visible_count = 0;
                    int max_off;
                    _listbox_hmetrics(wgt, ld, lf, &ctx->style, &lb_content_w, &lb_pane_w, &lb_need_hsb, &visible_count);
                    /* a tilt wheel (or shift-wheel, which Allegro reports the
                       same way) pans a horizontally scrolling list */
                    if (lb_need_hsb && event.mouse.dw != 0) {
                        ld->h_scroll += (float)event.mouse.dw * ctx->style.scroll_step;
                        _listbox_clamp_h(ld, lb_content_w, lb_pane_w);
                    }
                    ld->scroll_offset -= event.mouse.dz;
                    if (ld->scroll_offset < 0) ld->scroll_offset = 0;
                    max_off = (int)ld->nb_items - visible_count;
                    if (max_off < 0) max_off = 0;
                    if (ld->scroll_offset > max_off) ld->scroll_offset = max_off;
                    scroll_consumed = 1;
                }
                if (wgt->type == N_GUI_TYPE_HEXVIEW) {
                    N_GUI_HEXVIEW_DATA* hd = (N_GUI_HEXVIEW_DATA*)wgt->data;
                    ALLEGRO_FONT* hf = wgt->font ? wgt->font : ctx->default_font;
                    float hfh = hf ? (float)al_get_font_line_height(hf) : 16.0f;
                    float row_h = hfh + 2.0f;
                    int bpr = hd->bytes_per_row > 0 ? hd->bytes_per_row : 16;
                    int nb_rows = (int)((hd->len + (size_t)bpr - 1) / (size_t)bpr);
                    int visible_count = (int)((wgt->h - ctx->style.textarea_padding * 2.0f) / row_h);
                    int max_off;
                    if (visible_count < 1) visible_count = 1;
                    max_off = nb_rows - visible_count;
                    if (max_off < 0) max_off = 0;
                    hd->scroll_offset -= event.mouse.dz;
                    if (hd->scroll_offset < 0) hd->scroll_offset = 0;
                    if (hd->scroll_offset > max_off) hd->scroll_offset = max_off;
                    scroll_consumed = 1;
                }
                if (wgt->type == N_GUI_TYPE_SYNTAXVIEW) {
                    N_GUI_SYNTAXVIEW_DATA* yd = (N_GUI_SYNTAXVIEW_DATA*)wgt->data;
                    ALLEGRO_FONT* yf = wgt->font ? wgt->font : ctx->default_font;
                    float sv_content_w = 0.0f, sv_pane_w = wgt->w;
                    int sv_need_hsb = 0, visible_count = 1;
                    int max_off;
                    _syntaxview_hmetrics(wgt, yd, yf, &ctx->style, &sv_content_w, &sv_pane_w, &sv_need_hsb, &visible_count);
                    /* a tilt wheel (or shift-wheel, which Allegro reports the
                       same way) pans a line too wide for the view */
                    if (sv_need_hsb && event.mouse.dw != 0) {
                        yd->h_scroll += (float)event.mouse.dw * ctx->style.scroll_step;
                        _syntaxview_clamp_h(yd, sv_content_w, sv_pane_w);
                    }
                    max_off = yd->cached_nb_lines - visible_count;
                    if (max_off < 0) max_off = 0;
                    yd->scroll_offset -= event.mouse.dz;
                    if (yd->scroll_offset < 0) yd->scroll_offset = 0;
                    if (yd->scroll_offset > max_off) yd->scroll_offset = max_off;
                    scroll_consumed = 1;
                }
                if (wgt->type == N_GUI_TYPE_DATAGRID) {
                    N_GUI_DATAGRID_DATA* gd = (N_GUI_DATAGRID_DATA*)wgt->data;
                    ALLEGRO_FONT* gf = wgt->font ? wgt->font : ctx->default_font;
                    float gfh = gf ? (float)al_get_font_line_height(gf) : 16.0f;
                    float row_h = gfh + ctx->style.item_height_pad;
                    float data_h = wgt->h - row_h;
                    int visible_count = (int)(data_h / row_h);
                    int max_off;
                    if (visible_count < 1) visible_count = 1;
                    max_off = (int)gd->nb_rows - visible_count;
                    if (max_off < 0) max_off = 0;
                    /* Shift+wheel scrolls horizontally (the columns), plain wheel vertically */
                    int dg_shift = 0;
                    if (al_is_keyboard_installed()) {
                        ALLEGRO_KEYBOARD_STATE kb;
                        al_get_keyboard_state(&kb);
                        dg_shift = al_key_down(&kb, ALLEGRO_KEY_LSHIFT) || al_key_down(&kb, ALLEGRO_KEY_RSHIFT);
                    }
                    if (dg_shift) {
                        gd->h_scroll -= (float)event.mouse.dz * (row_h * 2.0f);
                        if (gd->h_scroll < 0) gd->h_scroll = 0;
                        /* the draw path clamps the upper bound to the live content width */
                    } else {
                        gd->scroll_offset -= event.mouse.dz;
                        if (gd->scroll_offset < 0) gd->scroll_offset = 0;
                        if (gd->scroll_offset > max_off) gd->scroll_offset = max_off;
                    }
                    scroll_consumed = 1;
                }
                if (wgt->type == N_GUI_TYPE_RADIOLIST) {
                    N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)wgt->data;
                    rd->scroll_offset -= event.mouse.dz;
                    if (rd->scroll_offset < 0) rd->scroll_offset = 0;
                    ALLEGRO_FONT* rf = wgt->font ? wgt->font : ctx->default_font;
                    float rfh = rf ? (float)al_get_font_line_height(rf) : 16.0f;
                    float rih = rd->item_height > rfh ? rd->item_height : rfh + ctx->style.item_height_pad;
                    int visible_count = (int)(wgt->h / rih);
                    int max_off = (int)rd->nb_items - visible_count;
                    if (max_off < 0) max_off = 0;
                    if (rd->scroll_offset > max_off) rd->scroll_offset = max_off;
                    scroll_consumed = 1;
                }
                if (wgt->type == N_GUI_TYPE_TEXTAREA) {
                    N_GUI_TEXTAREA_DATA* std = (N_GUI_TEXTAREA_DATA*)wgt->data;
                    if (std->multiline) {
                        float scroll_step = ctx->style.scroll_step;
                        std->scroll_y -= (int)((float)event.mouse.dz * scroll_step);
                        std->scroll_from_wheel = 1;
                        /* clamp is done at draw time */
                        scroll_consumed = 1;
                    }
                }
                if (wgt->type == N_GUI_TYPE_SLIDER) {
                    N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)wgt->data;
                    double step = sd->step > 0.0 ? sd->step : (sd->max_val - sd->min_val) / 20.0;
                    if (step <= 0) step = 1.0;
                    double new_val = sd->value + (double)event.mouse.dz * step;
                    if (sd->step > 0.0)
                        new_val = _slider_snap_value(new_val, sd->min_val, sd->max_val, sd->step);
                    else
                        new_val = _clamp(new_val, sd->min_val, sd->max_val);
                    if (new_val != sd->value) {
                        sd->value = new_val;
                        if (sd->on_change) {
                            sd->on_change(wgt->id, sd->value, sd->user_data);
                        }
                    }
                    scroll_consumed = 1;
                }
                if (wgt->type == N_GUI_TYPE_SCROLLBAR) {
                    N_GUI_SCROLLBAR_DATA* sbd = (N_GUI_SCROLLBAR_DATA*)wgt->data;
                    double max_scroll = sbd->content_size - sbd->viewport_size;
                    if (max_scroll > 0) {
                        double step = max_scroll / 20.0;
                        if (step <= 0) step = 1.0;
                        sbd->scroll_pos -= (double)event.mouse.dz * step;
                        if (sbd->scroll_pos < 0) sbd->scroll_pos = 0;
                        if (sbd->scroll_pos > max_scroll) sbd->scroll_pos = max_scroll;
                        if (sbd->on_scroll) {
                            sbd->on_scroll(wgt->id, sbd->scroll_pos, sbd->user_data);
                        }
                        scroll_consumed = 1;
                    }
                }
                if (wgt->type == N_GUI_TYPE_COMBOBOX) {
                    N_GUI_COMBOBOX_DATA* cbd = (N_GUI_COMBOBOX_DATA*)wgt->data;
                    if (cbd->is_open && cbd->nb_items > 0) {
                        cbd->scroll_offset -= event.mouse.dz;
                        if (cbd->scroll_offset < 0) cbd->scroll_offset = 0;
                        int max_vis = cbd->max_visible > 0 ? cbd->max_visible : 8;
                        int max_off = (int)cbd->nb_items - max_vis;
                        if (max_off < 0) max_off = 0;
                        if (cbd->scroll_offset > max_off) cbd->scroll_offset = max_off;
                        scroll_consumed = 1;
                    }
                }
                if (wgt->type == N_GUI_TYPE_DROPMENU) {
                    N_GUI_DROPMENU_DATA* dmd = (N_GUI_DROPMENU_DATA*)wgt->data;
                    if (dmd->is_open && dmd->nb_entries > 0) {
                        dmd->scroll_offset -= event.mouse.dz;
                        if (dmd->scroll_offset < 0) dmd->scroll_offset = 0;
                        int max_vis = dmd->max_visible > 0 ? dmd->max_visible : 8;
                        int max_off = (int)dmd->nb_entries - max_vis;
                        if (max_off < 0) max_off = 0;
                        if (dmd->scroll_offset > max_off) dmd->scroll_offset = max_off;
                        scroll_consumed = 1;
                    }
                }
                if (wgt->type == N_GUI_TYPE_LABEL) {
                    N_GUI_LABEL_DATA* sld = (N_GUI_LABEL_DATA*)wgt->data;
                    if (sld->align == N_GUI_ALIGN_JUSTIFIED) {
                        float scroll_step = ctx->style.scroll_step;
                        sld->scroll_y -= (float)event.mouse.dz * scroll_step;
                        /* clamp is done at draw time */
                        scroll_consumed = 1;
                    }
                }
                /* Stop at the first widget under the cursor that actually consumes
                 * the wheel. A non-scrolling widget that merely overlaps the cursor
                 * (e.g. a splitpane spanning a datagrid it sits beneath) must not
                 * swallow the event: keep scanning so a scrollable widget drawn on
                 * top of it still receives the scroll. */
                if (scroll_consumed) break;
            }

            /* if no widget consumed the scroll, let the window auto-scroll */
            if (!scroll_consumed && (win->flags & N_GUI_WIN_AUTO_SCROLLBAR)) {
                float scroll_step = ctx->style.scroll_step;
                win->scroll_y -= (float)event.mouse.dz * scroll_step;
                /* clamp is done at draw time */
                scroll_consumed = 1;
            }
            if (scroll_consumed) event_consumed = 1;
            break; /* only one window gets scroll */
        }

        /* if no window consumed the scroll and global scrollbars are available,
         * scroll the global viewport */
        if (!scroll_consumed && eff_w > 0 && eff_h > 0) {
            _compute_gui_bounds(ctx);
            if (ctx->gui_bounds_h > eff_h || ctx->gui_bounds_w > eff_w) {
                float scroll_step = ctx->style.global_scroll_step;
                ctx->global_scroll_y -= (float)event.mouse.dz * scroll_step;
                /* clamp is done at draw time */
                event_consumed = 1;
            }
        }
    }

    return event_consumed;
}

/**
 * @brief Check if the mouse is currently over any open GUI window.
 * Use this to prevent mouse clicks from passing through the GUI to the game.
 * @param ctx the GUI context
 * @return 1 if the mouse is over a GUI window, 0 otherwise
 */
int n_gui_wants_mouse(N_GUI_CTX* ctx) {
    __n_assert(ctx, return 0);
    float mx = (float)ctx->mouse_x;
    float my = (float)ctx->mouse_y;
    /* transform to virtual space when virtual canvas is active */
    if (ctx->virtual_w > 0 && ctx->virtual_h > 0 && ctx->gui_scale > 0) {
        mx = (mx - ctx->gui_offset_x) / ctx->gui_scale;
        my = (my - ctx->gui_offset_y) / ctx->gui_scale;
    }
    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!(win->state & N_GUI_WIN_OPEN)) continue;
        /* the question this answers is "should the host let the main display's
         * mouse through to game logic", so detached windows never count: they
         * own their display entirely and the host draws nothing else there */
        if (win->native) continue;
        float win_h = (win->state & N_GUI_WIN_MINIMISED) ? _win_tbh(win) : win->h;
        if (_point_in_rect(mx, my, win->x, win->y, win->w, win_h)) {
            return 1;
        }
    }
    return 0;
}

/* JSON THEME I/O (requires cJSON) */

#ifdef HAVE_CJSON

#ifndef __windows__
#include <fcntl.h>
#include <unistd.h>

/* Open a file for writing with explicit owner-only write permission (0644)
 * instead of the world-writable 0666 that fopen() requests before the umask
 * is applied. On Windows the POSIX mode bits do not apply, so fopen() is used
 * directly. */
static FILE* _gui_fopen_write(const char* path, const char* mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return NULL;
    FILE* f = fdopen(fd, mode);
    if (!f)
        close(fd);
    return f;
}
#else
#define _gui_fopen_write(path, mode) fopen((path), (mode))
#endif

/*! helper: add an RGBA colour as a JSON array [r,g,b,a] (0-255) */
static void _json_add_color(cJSON* parent, const char* name, ALLEGRO_COLOR c) {
    unsigned char r, g, b, a;
    al_unmap_rgba(c, &r, &g, &b, &a);
    cJSON* arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(r));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(g));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(b));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(a));
    cJSON_AddItemToObject(parent, name, arr);
}

/*! helper: read an RGBA colour from a JSON array [r,g,b,a] */
static ALLEGRO_COLOR _json_get_color(cJSON* parent, const char* name, ALLEGRO_COLOR fallback) {
    cJSON* arr = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 4) return fallback;
    int r = (int)cJSON_GetArrayItem(arr, 0)->valuedouble;
    int g = (int)cJSON_GetArrayItem(arr, 1)->valuedouble;
    int b = (int)cJSON_GetArrayItem(arr, 2)->valuedouble;
    int a = (int)cJSON_GetArrayItem(arr, 3)->valuedouble;
    return al_map_rgba((unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a);
}

/*! helper: read a float from JSON, return fallback if missing */
static float _json_get_float(cJSON* parent, const char* name, float fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!item || !cJSON_IsNumber(item)) return fallback;
    return (float)item->valuedouble;
}

/*! helper: read an int from JSON, return fallback if missing */
static int _json_get_int(cJSON* parent, const char* name, int fallback) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (!item || !cJSON_IsNumber(item)) return fallback;
    return (int)item->valuedouble;
}

/**
 *@brief save the current theme and style to a JSON file
 *@param ctx GUI context
 *@param filepath path to the output JSON file
 *@return 0 on success, -1 on error
 */
int n_gui_save_theme_json(N_GUI_CTX* ctx, const char* filepath) {
    __n_assert(ctx, return -1);
    __n_assert(filepath, return -1);

    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;

    /* theme colours */
    cJSON* theme = cJSON_CreateObject();
    N_GUI_THEME* t = &ctx->default_theme;
    _json_add_color(theme, "bg_normal", t->bg_normal);
    _json_add_color(theme, "bg_hover", t->bg_hover);
    _json_add_color(theme, "bg_active", t->bg_active);
    _json_add_color(theme, "border_normal", t->border_normal);
    _json_add_color(theme, "border_hover", t->border_hover);
    _json_add_color(theme, "border_active", t->border_active);
    _json_add_color(theme, "text_normal", t->text_normal);
    _json_add_color(theme, "text_hover", t->text_hover);
    _json_add_color(theme, "text_active", t->text_active);
    _json_add_color(theme, "selection_color", t->selection_color);
    cJSON_AddNumberToObject(theme, "border_thickness", t->border_thickness);
    cJSON_AddNumberToObject(theme, "corner_rx", t->corner_rx);
    cJSON_AddNumberToObject(theme, "corner_ry", t->corner_ry);
    cJSON_AddItemToObject(root, "theme", theme);

    /* style values */
    cJSON* sty = cJSON_CreateObject();
    N_GUI_STYLE* s = &ctx->style;
    cJSON_AddNumberToObject(sty, "titlebar_h", s->titlebar_h);
    cJSON_AddNumberToObject(sty, "min_win_w", s->min_win_w);
    cJSON_AddNumberToObject(sty, "min_win_h", s->min_win_h);
    cJSON_AddNumberToObject(sty, "title_padding", s->title_padding);
    cJSON_AddNumberToObject(sty, "title_max_w_reserve", s->title_max_w_reserve);

    cJSON_AddNumberToObject(sty, "tb_btn_size", s->tb_btn_size);
    cJSON_AddNumberToObject(sty, "tb_btn_spacing", s->tb_btn_spacing);
    cJSON_AddNumberToObject(sty, "tb_btn_right_margin", s->tb_btn_right_margin);
    cJSON_AddNumberToObject(sty, "tb_btn_glyph_thickness", s->tb_btn_glyph_thickness);

    cJSON_AddNumberToObject(sty, "scrollbar_size", s->scrollbar_size);
    cJSON_AddNumberToObject(sty, "scrollbar_thumb_min", s->scrollbar_thumb_min);
    cJSON_AddNumberToObject(sty, "scrollbar_thumb_padding", s->scrollbar_thumb_padding);
    cJSON_AddNumberToObject(sty, "scrollbar_thumb_corner_r", s->scrollbar_thumb_corner_r);
    _json_add_color(sty, "scrollbar_track_color", s->scrollbar_track_color);
    _json_add_color(sty, "scrollbar_thumb_color", s->scrollbar_thumb_color);

    cJSON_AddNumberToObject(sty, "global_scrollbar_size", s->global_scrollbar_size);
    cJSON_AddNumberToObject(sty, "global_scrollbar_thumb_min", s->global_scrollbar_thumb_min);
    cJSON_AddNumberToObject(sty, "global_scrollbar_thumb_padding", s->global_scrollbar_thumb_padding);
    cJSON_AddNumberToObject(sty, "global_scrollbar_thumb_corner_r", s->global_scrollbar_thumb_corner_r);
    cJSON_AddNumberToObject(sty, "global_scrollbar_border_thickness", s->global_scrollbar_border_thickness);
    _json_add_color(sty, "global_scrollbar_track_color", s->global_scrollbar_track_color);
    _json_add_color(sty, "global_scrollbar_thumb_color", s->global_scrollbar_thumb_color);
    _json_add_color(sty, "global_scrollbar_thumb_border_color", s->global_scrollbar_thumb_border_color);

    cJSON_AddNumberToObject(sty, "grip_size", s->grip_size);
    cJSON_AddNumberToObject(sty, "grip_line_thickness", s->grip_line_thickness);
    _json_add_color(sty, "grip_color", s->grip_color);

    cJSON_AddNumberToObject(sty, "slider_track_size", s->slider_track_size);
    cJSON_AddNumberToObject(sty, "slider_track_corner_r", s->slider_track_corner_r);
    cJSON_AddNumberToObject(sty, "slider_track_border_thickness", s->slider_track_border_thickness);
    cJSON_AddNumberToObject(sty, "slider_handle_min_r", s->slider_handle_min_r);
    cJSON_AddNumberToObject(sty, "slider_handle_edge_offset", s->slider_handle_edge_offset);
    cJSON_AddNumberToObject(sty, "slider_handle_border_thickness", s->slider_handle_border_thickness);
    cJSON_AddNumberToObject(sty, "slider_value_label_offset", s->slider_value_label_offset);

    cJSON_AddNumberToObject(sty, "textarea_padding", s->textarea_padding);
    cJSON_AddNumberToObject(sty, "textarea_cursor_width", s->textarea_cursor_width);
    cJSON_AddNumberToObject(sty, "textarea_cursor_blink_period", s->textarea_cursor_blink_period);

    cJSON_AddNumberToObject(sty, "checkbox_max_size", s->checkbox_max_size);
    cJSON_AddNumberToObject(sty, "checkbox_mark_margin", s->checkbox_mark_margin);
    cJSON_AddNumberToObject(sty, "checkbox_mark_thickness", s->checkbox_mark_thickness);
    cJSON_AddNumberToObject(sty, "checkbox_label_gap", s->checkbox_label_gap);
    cJSON_AddNumberToObject(sty, "checkbox_label_offset", s->checkbox_label_offset);

    cJSON_AddNumberToObject(sty, "radio_circle_min_r", s->radio_circle_min_r);
    cJSON_AddNumberToObject(sty, "radio_circle_border_thickness", s->radio_circle_border_thickness);
    cJSON_AddNumberToObject(sty, "radio_inner_offset", s->radio_inner_offset);
    cJSON_AddNumberToObject(sty, "radio_label_gap", s->radio_label_gap);

    cJSON_AddNumberToObject(sty, "listbox_default_item_height", s->listbox_default_item_height);
    cJSON_AddNumberToObject(sty, "radiolist_default_item_height", s->radiolist_default_item_height);
    cJSON_AddNumberToObject(sty, "combobox_max_visible", s->combobox_max_visible);
    cJSON_AddNumberToObject(sty, "dropmenu_max_visible", s->dropmenu_max_visible);
    cJSON_AddNumberToObject(sty, "shape_mode", s->shape_mode);
    cJSON_AddNumberToObject(sty, "item_text_padding", s->item_text_padding);
    cJSON_AddNumberToObject(sty, "item_selection_inset", s->item_selection_inset);
    cJSON_AddNumberToObject(sty, "item_height_pad", s->item_height_pad);

    cJSON_AddNumberToObject(sty, "dropdown_arrow_reserve", s->dropdown_arrow_reserve);
    cJSON_AddNumberToObject(sty, "dropdown_arrow_thickness", s->dropdown_arrow_thickness);
    cJSON_AddNumberToObject(sty, "dropdown_arrow_half_h", s->dropdown_arrow_half_h);
    cJSON_AddNumberToObject(sty, "dropdown_arrow_half_w", s->dropdown_arrow_half_w);
    cJSON_AddNumberToObject(sty, "dropdown_border_thickness", s->dropdown_border_thickness);

    cJSON_AddNumberToObject(sty, "label_padding", s->label_padding);
    cJSON_AddNumberToObject(sty, "link_underline_thickness", s->link_underline_thickness);
    _json_add_color(sty, "link_color_normal", s->link_color_normal);
    _json_add_color(sty, "link_color_hover", s->link_color_hover);

    cJSON_AddNumberToObject(sty, "scroll_step", s->scroll_step);
    cJSON_AddNumberToObject(sty, "global_scroll_step", s->global_scroll_step);
    cJSON_AddNumberToObject(sty, "combobox_max_dropdown_width", s->combobox_max_dropdown_width);
    cJSON_AddItemToObject(root, "style", sty);

    /* write to file */
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return -1;

    FILE* fp = _gui_fopen_write(filepath, "w");
    if (!fp) {
        cJSON_free(json_str);
        return -1;
    }
    fprintf(fp, "%s\n", json_str);
    fclose(fp);
    cJSON_free(json_str);
    return 0;
}

/**
 *@brief load a theme and style from a JSON file
 *@param ctx GUI context
 *@param filepath path to the input JSON file
 *@return 0 on success, -1 on error
 */
int n_gui_load_theme_json(N_GUI_CTX* ctx, const char* filepath) {
    __n_assert(ctx, return -1);
    __n_assert(filepath, return -1);

    FILE* fp = fopen(filepath, "r");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 1024 * 1024) { /* sanity: max 1 MB */
        fclose(fp);
        return -1;
    }

    char* buf = NULL;
    Malloc(buf, char, (size_t)fsize + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    buf[nread] = '\0';

    cJSON* root = cJSON_Parse(buf);
    FreeNoLog(buf);
    if (!root) return -1;

    /* parse theme colours */
    cJSON* theme = cJSON_GetObjectItemCaseSensitive(root, "theme");
    if (theme) {
        N_GUI_THEME* t = &ctx->default_theme;
        t->bg_normal = _json_get_color(theme, "bg_normal", t->bg_normal);
        t->bg_hover = _json_get_color(theme, "bg_hover", t->bg_hover);
        t->bg_active = _json_get_color(theme, "bg_active", t->bg_active);
        t->border_normal = _json_get_color(theme, "border_normal", t->border_normal);
        t->border_hover = _json_get_color(theme, "border_hover", t->border_hover);
        t->border_active = _json_get_color(theme, "border_active", t->border_active);
        t->text_normal = _json_get_color(theme, "text_normal", t->text_normal);
        t->text_hover = _json_get_color(theme, "text_hover", t->text_hover);
        t->text_active = _json_get_color(theme, "text_active", t->text_active);
        t->selection_color = _json_get_color(theme, "selection_color", t->selection_color);
        t->border_thickness = _json_get_float(theme, "border_thickness", t->border_thickness);
        t->corner_rx = _json_get_float(theme, "corner_rx", t->corner_rx);
        t->corner_ry = _json_get_float(theme, "corner_ry", t->corner_ry);

        /* apply to all existing windows and widgets */
        list_foreach(wnode, ctx->windows) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            win->theme = *t;
            list_foreach(wgn, win->widgets) {
                N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                if (wgt) wgt->theme = *t;
            }
        }
    }

    /* parse style values */
    cJSON* sty = cJSON_GetObjectItemCaseSensitive(root, "style");
    if (sty) {
        N_GUI_STYLE* s = &ctx->style;
        s->titlebar_h = _json_get_float(sty, "titlebar_h", s->titlebar_h);
        s->min_win_w = _json_get_float(sty, "min_win_w", s->min_win_w);
        s->min_win_h = _json_get_float(sty, "min_win_h", s->min_win_h);
        s->title_padding = _json_get_float(sty, "title_padding", s->title_padding);
        s->title_max_w_reserve = _json_get_float(sty, "title_max_w_reserve", s->title_max_w_reserve);

        s->tb_btn_size = _json_get_float(sty, "tb_btn_size", s->tb_btn_size);
        s->tb_btn_spacing = _json_get_float(sty, "tb_btn_spacing", s->tb_btn_spacing);
        s->tb_btn_right_margin = _json_get_float(sty, "tb_btn_right_margin", s->tb_btn_right_margin);
        s->tb_btn_glyph_thickness = _json_get_float(sty, "tb_btn_glyph_thickness", s->tb_btn_glyph_thickness);

        s->scrollbar_size = _json_get_float(sty, "scrollbar_size", s->scrollbar_size);
        s->scrollbar_thumb_min = _json_get_float(sty, "scrollbar_thumb_min", s->scrollbar_thumb_min);
        s->scrollbar_thumb_padding = _json_get_float(sty, "scrollbar_thumb_padding", s->scrollbar_thumb_padding);
        s->scrollbar_thumb_corner_r = _json_get_float(sty, "scrollbar_thumb_corner_r", s->scrollbar_thumb_corner_r);
        s->scrollbar_track_color = _json_get_color(sty, "scrollbar_track_color", s->scrollbar_track_color);
        s->scrollbar_thumb_color = _json_get_color(sty, "scrollbar_thumb_color", s->scrollbar_thumb_color);

        s->global_scrollbar_size = _json_get_float(sty, "global_scrollbar_size", s->global_scrollbar_size);
        s->global_scrollbar_thumb_min = _json_get_float(sty, "global_scrollbar_thumb_min", s->global_scrollbar_thumb_min);
        s->global_scrollbar_thumb_padding = _json_get_float(sty, "global_scrollbar_thumb_padding", s->global_scrollbar_thumb_padding);
        s->global_scrollbar_thumb_corner_r = _json_get_float(sty, "global_scrollbar_thumb_corner_r", s->global_scrollbar_thumb_corner_r);
        s->global_scrollbar_border_thickness = _json_get_float(sty, "global_scrollbar_border_thickness", s->global_scrollbar_border_thickness);
        s->global_scrollbar_track_color = _json_get_color(sty, "global_scrollbar_track_color", s->global_scrollbar_track_color);
        s->global_scrollbar_thumb_color = _json_get_color(sty, "global_scrollbar_thumb_color", s->global_scrollbar_thumb_color);
        s->global_scrollbar_thumb_border_color = _json_get_color(sty, "global_scrollbar_thumb_border_color", s->global_scrollbar_thumb_border_color);

        s->grip_size = _json_get_float(sty, "grip_size", s->grip_size);
        s->grip_line_thickness = _json_get_float(sty, "grip_line_thickness", s->grip_line_thickness);
        s->grip_color = _json_get_color(sty, "grip_color", s->grip_color);

        s->slider_track_size = _json_get_float(sty, "slider_track_size", s->slider_track_size);
        s->slider_track_corner_r = _json_get_float(sty, "slider_track_corner_r", s->slider_track_corner_r);
        s->slider_track_border_thickness = _json_get_float(sty, "slider_track_border_thickness", s->slider_track_border_thickness);
        s->slider_handle_min_r = _json_get_float(sty, "slider_handle_min_r", s->slider_handle_min_r);
        s->slider_handle_edge_offset = _json_get_float(sty, "slider_handle_edge_offset", s->slider_handle_edge_offset);
        s->slider_handle_border_thickness = _json_get_float(sty, "slider_handle_border_thickness", s->slider_handle_border_thickness);
        s->slider_value_label_offset = _json_get_float(sty, "slider_value_label_offset", s->slider_value_label_offset);

        s->textarea_padding = _json_get_float(sty, "textarea_padding", s->textarea_padding);
        s->textarea_cursor_width = _json_get_float(sty, "textarea_cursor_width", s->textarea_cursor_width);
        s->textarea_cursor_blink_period = _json_get_float(sty, "textarea_cursor_blink_period", s->textarea_cursor_blink_period);

        s->checkbox_max_size = _json_get_float(sty, "checkbox_max_size", s->checkbox_max_size);
        s->checkbox_mark_margin = _json_get_float(sty, "checkbox_mark_margin", s->checkbox_mark_margin);
        s->checkbox_mark_thickness = _json_get_float(sty, "checkbox_mark_thickness", s->checkbox_mark_thickness);
        s->checkbox_label_gap = _json_get_float(sty, "checkbox_label_gap", s->checkbox_label_gap);
        s->checkbox_label_offset = _json_get_float(sty, "checkbox_label_offset", s->checkbox_label_offset);

        s->radio_circle_min_r = _json_get_float(sty, "radio_circle_min_r", s->radio_circle_min_r);
        s->radio_circle_border_thickness = _json_get_float(sty, "radio_circle_border_thickness", s->radio_circle_border_thickness);
        s->radio_inner_offset = _json_get_float(sty, "radio_inner_offset", s->radio_inner_offset);
        s->radio_label_gap = _json_get_float(sty, "radio_label_gap", s->radio_label_gap);

        s->listbox_default_item_height = _json_get_float(sty, "listbox_default_item_height", s->listbox_default_item_height);
        s->radiolist_default_item_height = _json_get_float(sty, "radiolist_default_item_height", s->radiolist_default_item_height);
        s->combobox_max_visible = _json_get_int(sty, "combobox_max_visible", s->combobox_max_visible);
        s->dropmenu_max_visible = _json_get_int(sty, "dropmenu_max_visible", s->dropmenu_max_visible);
        s->shape_mode = _json_get_int(sty, "shape_mode", s->shape_mode);
        s->item_text_padding = _json_get_float(sty, "item_text_padding", s->item_text_padding);
        s->item_selection_inset = _json_get_float(sty, "item_selection_inset", s->item_selection_inset);
        s->item_height_pad = _json_get_float(sty, "item_height_pad", s->item_height_pad);

        s->dropdown_arrow_reserve = _json_get_float(sty, "dropdown_arrow_reserve", s->dropdown_arrow_reserve);
        s->dropdown_arrow_thickness = _json_get_float(sty, "dropdown_arrow_thickness", s->dropdown_arrow_thickness);
        s->dropdown_arrow_half_h = _json_get_float(sty, "dropdown_arrow_half_h", s->dropdown_arrow_half_h);
        s->dropdown_arrow_half_w = _json_get_float(sty, "dropdown_arrow_half_w", s->dropdown_arrow_half_w);
        s->dropdown_border_thickness = _json_get_float(sty, "dropdown_border_thickness", s->dropdown_border_thickness);

        s->label_padding = _json_get_float(sty, "label_padding", s->label_padding);
        s->link_underline_thickness = _json_get_float(sty, "link_underline_thickness", s->link_underline_thickness);
        s->link_color_normal = _json_get_color(sty, "link_color_normal", s->link_color_normal);
        s->link_color_hover = _json_get_color(sty, "link_color_hover", s->link_color_hover);

        s->scroll_step = _json_get_float(sty, "scroll_step", s->scroll_step);
        s->global_scroll_step = _json_get_float(sty, "global_scroll_step", s->global_scroll_step);
        s->combobox_max_dropdown_width = _json_get_float(sty, "combobox_max_dropdown_width", s->combobox_max_dropdown_width);
    }

    cJSON_Delete(root);
    return 0;
}

/* persistent state bits, transient DRAGGING/RESIZING/SCROLL-DRAG are never serialised */
#define _N_GUI_WIN_STATE_PERSIST_MASK (N_GUI_WIN_OPEN | N_GUI_WIN_MINIMISED | N_GUI_WIN_MAXIMISED)

/**
 *@brief save the geometry + persistent state of every window to a JSON file
 *@param ctx GUI context
 *@param filepath path to the output JSON file
 *@return 0 on success, -1 on error
 */
int n_gui_save_layout_json(N_GUI_CTX* ctx, const char* filepath) {
    __n_assert(ctx, return -1);
    __n_assert(filepath, return -1);

    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddNumberToObject(root, "version", 1);
    /* Display size at save time, the loader scales saved (x,y,w,h) by
     * (current_display / saved_display) so a dragged HUD stays in its
     * relative spot when the user relaunches on a bigger / smaller
     * screen. Missing or zero saved dims disable the scaling (legacy
     * files and in-process tests behave as before). */
    cJSON_AddNumberToObject(root, "display_w", ctx->display_w);
    cJSON_AddNumberToObject(root, "display_h", ctx->display_h);
    cJSON* wins = cJSON_CreateArray();
    if (!wins) {
        cJSON_Delete(root);
        return -1;
    }

    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!win) continue;
        /* Closed windows are persisted too: many UI modules treat
         * close/open as a visibility toggle (editor panels, mod tools,
         * NPC dialog popups). Without persisting their closed geometry,
         * the user's dragged position would be lost the moment the
         * window is hidden, and most modules close their windows
         * before logout. The destroy+recreate pattern (combat HUD on
         * target change, hotbar on assignment) is now safe because the
         * loader has a dedupe pre-pass that drops stale closed entries
         * when a same-title open entry follows in the list (see
         * test_load_stale_closed_dedupe).
         *
         * Multiple OPEN windows sharing a title still resolve via the
         * loader's per-context match counter (first-seen wins), so the
         * save order here just needs to be deterministic. */
        cJSON* wj = cJSON_CreateObject();
        if (!wj) continue;
        /* A detached window's live x/y/w/h describe its native display, not a
         * spot on the main display. Persist the pop-up geometry it falls back
         * to (and the pop-up flags, since detaching forces frameless), with the
         * native size and desktop position carried separately. */
        int detached = (win->native != NULL);
        float px = detached ? win->saved_x : win->x;
        float py = detached ? win->saved_y : win->y;
        float pw = detached ? win->saved_w : win->w;
        float ph = detached ? win->saved_h : win->h;
        int pflags = detached ? win->saved_flags : win->flags;
        int npx = win->native_pos_x;
        int npy = win->native_pos_y;
        float nw = win->native_w;
        float nh = win->native_h;
        if (detached) {
            al_get_window_position(win->native, &npx, &npy);
            nw = (float)al_get_display_width(win->native);
            nh = (float)al_get_display_height(win->native);
        }
        cJSON_AddStringToObject(wj, "title", win->title);
        cJSON_AddNumberToObject(wj, "x", px);
        cJSON_AddNumberToObject(wj, "y", py);
        cJSON_AddNumberToObject(wj, "w", pw);
        cJSON_AddNumberToObject(wj, "h", ph);
        cJSON_AddNumberToObject(wj, "state", win->state & _N_GUI_WIN_STATE_PERSIST_MASK);
        cJSON_AddNumberToObject(wj, "flags", pflags);
        if (win->want_native) {
            cJSON_AddNumberToObject(wj, "native", 1);
            cJSON_AddNumberToObject(wj, "detach_flags", win->detach_flags);
            if (nw > 0.0f && nh > 0.0f) {
                cJSON_AddNumberToObject(wj, "native_w", nw);
                cJSON_AddNumberToObject(wj, "native_h", nh);
            }
            if (npx != INT_MIN && npy != INT_MIN) {
                cJSON_AddNumberToObject(wj, "native_pos_x", npx);
                cJSON_AddNumberToObject(wj, "native_pos_y", npy);
            }
        }
        cJSON_AddNumberToObject(wj, "z_order", win->z_order);
        cJSON_AddNumberToObject(wj, "z_value", win->z_value);
        cJSON_AddNumberToObject(wj, "norm_x", win->norm_x);
        cJSON_AddNumberToObject(wj, "norm_y", win->norm_y);
        cJSON_AddNumberToObject(wj, "norm_w", win->norm_w);
        cJSON_AddNumberToObject(wj, "norm_h", win->norm_h);
        cJSON_AddNumberToObject(wj, "autofit_flags", win->autofit_flags);
        cJSON_AddNumberToObject(wj, "autofit_border", win->autofit_border);
        cJSON_AddNumberToObject(wj, "resize_policy", win->resize_policy);

        /* User-adjustable state of this window's KEYED widgets (splitpane ratios,
         * datagrid column layouts). Only widgets given a persist key are saved. */
        {
            cJSON* warr = NULL;
            list_foreach(wgn, win->widgets) {
                N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                cJSON* w2;
                if (!wgt || !wgt->persist_key[0] || !wgt->data)
                    continue;
                if (wgt->type != N_GUI_TYPE_SPLITPANE && wgt->type != N_GUI_TYPE_DATAGRID)
                    continue;
                if (!warr && !(warr = cJSON_CreateArray()))
                    break;
                w2 = cJSON_CreateObject();
                if (!w2)
                    continue;
                cJSON_AddStringToObject(w2, "key", wgt->persist_key);
                if (wgt->type == N_GUI_TYPE_SPLITPANE) {
                    cJSON_AddStringToObject(w2, "kind", "splitpane");
                    cJSON_AddNumberToObject(w2, "ratio", ((N_GUI_SPLITPANE_DATA*)wgt->data)->ratio);
                } else { /* N_GUI_TYPE_DATAGRID */
                    int nc = (int)n_gui_datagrid_get_column_count(ctx, wgt->id);
                    cJSON* jo;
                    cJSON* jv;
                    cJSON* jw;
                    int c;
                    if (nc > N_GUI_PERSIST_MAX_COLS)
                        nc = N_GUI_PERSIST_MAX_COLS;
                    cJSON_AddStringToObject(w2, "kind", "datagrid");
                    cJSON_AddNumberToObject(w2, "ncols", nc);
                    jo = cJSON_AddArrayToObject(w2, "order");
                    jv = cJSON_AddArrayToObject(w2, "visible");
                    jw = cJSON_AddArrayToObject(w2, "width");
                    for (c = 0; c < nc; c++) {
                        int phys = n_gui_datagrid_display_to_physical(ctx, wgt->id, c);
                        if (jo) cJSON_AddItemToArray(jo, cJSON_CreateNumber(phys >= 0 ? phys : c));
                        if (jv) cJSON_AddItemToArray(jv, cJSON_CreateNumber(n_gui_datagrid_get_column_visible(ctx, wgt->id, c)));
                        if (jw) cJSON_AddItemToArray(jw, cJSON_CreateNumber((double)n_gui_datagrid_get_column_width(ctx, wgt->id, c)));
                    }
                }
                cJSON_AddItemToArray(warr, w2);
            }
            if (warr)
                cJSON_AddItemToObject(wj, "widgets", warr);
        }
        cJSON_AddItemToArray(wins, wj);
    }
    cJSON_AddItemToObject(root, "windows", wins);

    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return -1;

    FILE* fp = _gui_fopen_write(filepath, "w");
    if (!fp) {
        cJSON_free(json_str);
        return -1;
    }
    fprintf(fp, "%s\n", json_str);
    fclose(fp);
    cJSON_free(json_str);
    return 0;
}

/**
 *@brief load a JSON window layout file and apply it to matching windows
 *@param ctx GUI context
 *@param filepath path to the input JSON file
 *@return 0 on success, -1 on error
 */
int n_gui_load_layout_json(N_GUI_CTX* ctx, const char* filepath) {
    __n_assert(ctx, return -1);
    __n_assert(filepath, return -1);

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        n_log(LOG_DEBUG, "n_gui_load_layout_json: %s not found (first run?)", filepath);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 4 * 1024 * 1024) { /* sanity: max 4 MB */
        fclose(fp);
        return -1;
    }

    char* buf = NULL;
    Malloc(buf, char, (size_t)fsize + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t nread = fread(buf, 1, (size_t)fsize, fp);
    fclose(fp);
    buf[nread] = '\0';

    cJSON* root = cJSON_Parse(buf);
    FreeNoLog(buf);
    if (!root) return -1;

    cJSON* wins = cJSON_GetObjectItemCaseSensitive(root, "windows");
    if (!wins || !cJSON_IsArray(wins)) {
        cJSON_Delete(root);
        return -1;
    }

    /* Cross-session proportional scaling: if the layout was saved with
     * a different display size than the current one, scale every loaded
     * (x,y,w,h) by the size ratio. Falls back to 1.0 (no scaling) when
     * the file was saved pre-scaling support (display_w/h absent or 0)
     * or when the current context display size is 0 (in-process tests). */
    float saved_w = _json_get_float(root, "display_w", 0.0f);
    float saved_h = _json_get_float(root, "display_h", 0.0f);
    float sx = 1.0f, sy = 1.0f;
    if (saved_w > 0.0f && ctx->display_w > 0.0f) sx = ctx->display_w / saved_w;
    if (saved_h > 0.0f && ctx->display_h > 0.0f) sy = ctx->display_h / saved_h;

    /* Build a per-title match counter on the context side so duplicate titles
     * consume their JSON entries in creation order (first-seen-first-match). */
    int n_entries = cJSON_GetArraySize(wins);
    int* consumed = NULL;
    if (n_entries > 0) {
        Malloc(consumed, int, (size_t)n_entries);
        if (!consumed) {
            cJSON_Delete(root);
            return -1;
        }
        memset(consumed, 0, sizeof(int) * (size_t)n_entries);
    }

    /* Dedupe pre-pass for stale closed windows. Old saves (from before
     * the save-side skip-closed-windows fix) accumulated one entry per
     * rebuild cycle: N-1 closed stragglers with state=0 plus the live
     * one with state=1, all sharing a title. The matcher assigns JSON
     * entries to context windows in list order, so the stale entries
     * would land on the live window and apply state=0, hiding the HUD
     * until the next rebuild re-opens it (the "text appears only after
     * resize" symptom).
     *
     * We ONLY mark a closed entry as consumed when a later same-title
     * entry is marked OPEN. That preserves legitimate cases where an
     * app creates multiple windows sharing a title (uncommon but valid). */
    for (int i = 0; i < n_entries; i++) {
        if (consumed[i]) continue;
        cJSON* wi = cJSON_GetArrayItem(wins, i);
        if (!wi) continue;
        cJSON* state_i = cJSON_GetObjectItemCaseSensitive(wi, "state");
        int is_open_i = state_i && cJSON_IsNumber(state_i) &&
                        (((int)state_i->valuedouble) & N_GUI_WIN_OPEN);
        if (is_open_i) continue;
        cJSON* ti = cJSON_GetObjectItemCaseSensitive(wi, "title");
        if (!ti || !cJSON_IsString(ti) || !ti->valuestring) continue;
        for (int j = i + 1; j < n_entries; j++) {
            cJSON* wj = cJSON_GetArrayItem(wins, j);
            if (!wj) continue;
            cJSON* state_j = cJSON_GetObjectItemCaseSensitive(wj, "state");
            int is_open_j = state_j && cJSON_IsNumber(state_j) &&
                            (((int)state_j->valuedouble) & N_GUI_WIN_OPEN);
            if (!is_open_j) continue;
            cJSON* tj = cJSON_GetObjectItemCaseSensitive(wj, "title");
            if (tj && cJSON_IsString(tj) && tj->valuestring &&
                strcmp(ti->valuestring, tj->valuestring) == 0) {
                consumed[i] = 1;
                break;
            }
        }
    }

    list_foreach(wnode, ctx->windows) {
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!win) continue;
        cJSON* match = NULL;
        int match_idx = -1;
        int idx = 0;
        cJSON* wj = NULL;
        cJSON_ArrayForEach(wj, wins) {
            if (!consumed || consumed[idx] == 0) {
                cJSON* t = cJSON_GetObjectItemCaseSensitive(wj, "title");
                if (t && cJSON_IsString(t) && t->valuestring &&
                    strncmp(t->valuestring, win->title, N_GUI_ID_MAX) == 0) {
                    match = wj;
                    match_idx = idx;
                    break;
                }
            }
            idx++;
        }
        if (!match) continue;
        if (consumed) consumed[match_idx] = 1;

        /* Only (x,y) participate in cross-session scaling, they express
         * *placement* in screen coordinates and should track the display
         * proportionally. (w,h) are intrinsic to the widget layout (HUD
         * bar widths, editor panel sizes, etc.), so they are loaded
         * verbatim and clamped below. Scaling w/h across sessions caused
         * cascading corruption: repeated save/load at different display
         * sizes produced insane dimensions (HUD Player w=65 instead of
         * 220, Chat Input h=11 instead of 22). */
        win->x = _json_get_float(match, "x", win->x) * sx;
        win->y = _json_get_float(match, "y", win->y) * sy;
        win->w = _json_get_float(match, "w", win->w);
        win->h = _json_get_float(match, "h", win->h);

        /* Sanity clamp: if the saved file was produced by an earlier
         * buggy build (or if the display shrank dramatically), the
         * values may be way out of range. Loaded geometry entirely
         * outside the current display is reset to (0, 0) so the window
         * stays reachable, the user can drag it from there or hit the
         * settings Reset button. Width/height clamped to the display so
         * giant bogus dimensions can't blow the layout. Minimums keep
         * the window clickable. */
        if (ctx->display_w > 0.0f) {
            if (win->x < 0.0f || win->x > ctx->display_w) win->x = 0.0f;
            if (win->w < 20.0f) win->w = 20.0f;
            if (win->w > ctx->display_w) win->w = ctx->display_w;
        }
        if (ctx->display_h > 0.0f) {
            if (win->y < 0.0f || win->y > ctx->display_h) win->y = 0.0f;
            if (win->h < 20.0f) win->h = 20.0f;
            if (win->h > ctx->display_h) win->h = ctx->display_h;
        }
        /* keep only persistent state bits from the file; preserve any
         * transient bits already on the window (there shouldn't be any at load time). */
        int saved_state = _json_get_int(match, "state", win->state & _N_GUI_WIN_STATE_PERSIST_MASK);
        win->state = (win->state & ~_N_GUI_WIN_STATE_PERSIST_MASK) |
                     (saved_state & _N_GUI_WIN_STATE_PERSIST_MASK);
        win->flags = _json_get_int(match, "flags", win->flags);
        win->z_order = _json_get_int(match, "z_order", win->z_order);
        win->z_value = _json_get_int(match, "z_value", win->z_value);
        win->norm_x = _json_get_float(match, "norm_x", win->norm_x);
        win->norm_y = _json_get_float(match, "norm_y", win->norm_y);
        win->norm_w = _json_get_float(match, "norm_w", win->norm_w);
        win->norm_h = _json_get_float(match, "norm_h", win->norm_h);
        win->autofit_flags = _json_get_int(match, "autofit_flags", win->autofit_flags);
        win->autofit_border = _json_get_float(match, "autofit_border", win->autofit_border);
        win->resize_policy = _json_get_int(match, "resize_policy", win->resize_policy);

        /* Native (detached) window restore. The geometry applied above is the
         * pop-up fallback, keep it as such. The native window itself is only
         * created when the context has an event queue and the window is open,
         * otherwise want_native carries the intent and the next
         * n_gui_open_window (which the host calls once its queue exists) does
         * it. Native size and desktop position are NOT scaled by the display
         * ratio: they are desktop coordinates, not main-display ones. */
        if (_json_get_int(match, "native", 0)) {
            win->saved_x = win->x;
            win->saved_y = win->y;
            win->saved_w = win->w;
            win->saved_h = win->h;
            win->saved_flags = win->flags;
            win->detach_flags = _json_get_int(match, "detach_flags", N_GUI_DETACH_NONE);
            win->native_w = _json_get_float(match, "native_w", 0.0f);
            win->native_h = _json_get_float(match, "native_h", 0.0f);
            win->native_pos_x = _json_get_int(match, "native_pos_x", INT_MIN);
            win->native_pos_y = _json_get_int(match, "native_pos_y", INT_MIN);
            win->want_native = 1;
            if (ctx->event_queue && !win->native && (win->state & N_GUI_WIN_OPEN)) {
                n_gui_window_detach(ctx, win->id, win->detach_flags);
            }
        }
    }

    /* Retain entries that matched NO live window (lazily-created panels not
     * yet built) so n_gui_add_window can apply their position when the
     * window is created later. Skip titles that DO have a live window (those
     * are eager windows already placed above; re-applying on a later rebuild
     * would fight the caller's preserve-position logic). */
    FreeNoLog(ctx->pending_layout);
    ctx->pending_layout = NULL;
    ctx->pending_layout_count = 0;
    if (n_entries > 0) {
        int npend = 0;
        for (int i = 0; i < n_entries; i++)
            if (consumed && !consumed[i]) npend++;
        if (npend > 0) {
            Malloc(ctx->pending_layout, N_GUI_PENDING_GEOM, (size_t)npend);
            if (ctx->pending_layout) {
                int k = 0;
                for (int i = 0; i < n_entries && k < npend; i++) {
                    if (consumed && consumed[i]) continue;
                    cJSON* wi = cJSON_GetArrayItem(wins, i);
                    cJSON* t = wi ? cJSON_GetObjectItemCaseSensitive(wi, "title") : NULL;
                    if (!t || !cJSON_IsString(t) || !t->valuestring || !t->valuestring[0])
                        continue;
                    /* Skip if a live window already owns this title. */
                    int live = 0;
                    list_foreach(lwn, ctx->windows) {
                        const N_GUI_WINDOW* lw = (N_GUI_WINDOW*)lwn->ptr;
                        if (lw && strncmp(lw->title, t->valuestring, N_GUI_ID_MAX) == 0) {
                            live = 1;
                            break;
                        }
                    }
                    if (live) continue;
                    strncpy(ctx->pending_layout[k].title, t->valuestring, N_GUI_ID_MAX - 1);
                    ctx->pending_layout[k].title[N_GUI_ID_MAX - 1] = '\0';
                    ctx->pending_layout[k].x = _json_get_float(wi, "x", 0.0f) * sx;
                    ctx->pending_layout[k].y = _json_get_float(wi, "y", 0.0f) * sy;
                    ctx->pending_layout[k].w = _json_get_float(wi, "w", 0.0f) * sx;
                    ctx->pending_layout[k].h = _json_get_float(wi, "h", 0.0f) * sy;
                    ctx->pending_layout[k].state = _json_get_int(wi, "state", 0) & _N_GUI_WIN_STATE_PERSIST_MASK;
                    ctx->pending_layout[k].consumed = 0;
                    k++;
                }
                ctx->pending_layout_count = k;
            }
        }
    }

    /* Stash every saved per-window widget state (keyed splitpanes/datagrids) as
     * pending, applied to each widget when n_gui_widget_set_persist_key is next
     * called with a matching window title + key. This covers lazily-created widgets
     * (Fissure builds its views/popups after the layout is loaded). */
    FreeNoLog(ctx->pending_widgets);
    ctx->pending_widgets = NULL;
    ctx->pending_widgets_count = 0;
    {
        int nw = 0;
        cJSON* wi;
        cJSON_ArrayForEach(wi, wins) {
            cJSON* warr = cJSON_GetObjectItemCaseSensitive(wi, "widgets");
            if (warr && cJSON_IsArray(warr))
                nw += cJSON_GetArraySize(warr);
        }
        if (nw > 0) {
            Malloc(ctx->pending_widgets, N_GUI_PENDING_WIDGET, (size_t)nw);
            if (ctx->pending_widgets) {
                int k = 0;
                cJSON* wi2;
                cJSON_ArrayForEach(wi2, wins) {
                    cJSON* t = cJSON_GetObjectItemCaseSensitive(wi2, "title");
                    cJSON* warr = cJSON_GetObjectItemCaseSensitive(wi2, "widgets");
                    cJSON* we;
                    const char* title = (t && cJSON_IsString(t) && t->valuestring) ? t->valuestring : "";
                    if (!warr || !cJSON_IsArray(warr) || !title[0])
                        continue;
                    cJSON_ArrayForEach(we, warr) {
                        cJSON* jk = cJSON_GetObjectItemCaseSensitive(we, "key");
                        cJSON* jkind = cJSON_GetObjectItemCaseSensitive(we, "kind");
                        N_GUI_PENDING_WIDGET* p;
                        const char* kind;
                        if (k >= nw)
                            break;
                        if (!jk || !cJSON_IsString(jk) || !jk->valuestring || !jk->valuestring[0])
                            continue;
                        if (!jkind || !cJSON_IsString(jkind) || !jkind->valuestring)
                            continue;
                        kind = jkind->valuestring;
                        p = &ctx->pending_widgets[k];
                        memset(p, 0, sizeof(*p));
                        strncpy(p->title, title, N_GUI_ID_MAX - 1);
                        strncpy(p->key, jk->valuestring, N_GUI_ID_MAX - 1);
                        if (strcmp(kind, "splitpane") == 0) {
                            p->kind = N_GUI_TYPE_SPLITPANE;
                            p->ratio = _json_get_float(we, "ratio", 0.5f);
                        } else if (strcmp(kind, "datagrid") == 0) {
                            cJSON* jo = cJSON_GetObjectItemCaseSensitive(we, "order");
                            cJSON* jv = cJSON_GetObjectItemCaseSensitive(we, "visible");
                            cJSON* jw = cJSON_GetObjectItemCaseSensitive(we, "width");
                            int nc = _json_get_int(we, "ncols", 0);
                            int c;
                            p->kind = N_GUI_TYPE_DATAGRID;
                            if (nc < 0) nc = 0;
                            if (nc > N_GUI_PERSIST_MAX_COLS) nc = N_GUI_PERSIST_MAX_COLS;
                            p->ncols = nc;
                            for (c = 0; c < nc; c++) {
                                const cJSON* eo = (jo && cJSON_IsArray(jo)) ? cJSON_GetArrayItem(jo, c) : NULL;
                                const cJSON* ev = (jv && cJSON_IsArray(jv)) ? cJSON_GetArrayItem(jv, c) : NULL;
                                const cJSON* ew = (jw && cJSON_IsArray(jw)) ? cJSON_GetArrayItem(jw, c) : NULL;
                                p->order[c] = eo ? (int)eo->valuedouble : c;
                                p->visible[c] = (ev && ((int)ev->valuedouble)) ? 1 : (ev ? 0 : 1);
                                p->width[c] = ew ? (float)ew->valuedouble : 0.0f;
                            }
                        } else {
                            continue; /* unknown kind: leave the slot for the next entry */
                        }
                        k++;
                    }
                }
                ctx->pending_widgets_count = k;
            }
        }
    }

    FreeNoLog(consumed);
    cJSON_Delete(root);
    return 0;
}

#else /* !HAVE_CJSON */

/**
 *@brief save the current theme and style to a JSON file (stub, requires cJSON)
 *@param ctx GUI context
 *@param filepath path to the output JSON file
 *@return -1 (always fails without cJSON)
 */
int n_gui_save_theme_json(N_GUI_CTX* ctx, const char* filepath) {
    (void)ctx;
    (void)filepath;
    n_log(LOG_ERR, "n_gui_save_theme_json: cJSON not available (compile with -DHAVE_CJSON)");
    return -1;
}

/**
 *@brief load a theme and style from a JSON file (stub, requires cJSON)
 *@param ctx GUI context
 *@param filepath path to the input JSON file
 *@return -1 (always fails without cJSON)
 */
int n_gui_load_theme_json(N_GUI_CTX* ctx, const char* filepath) {
    (void)ctx;
    (void)filepath;
    n_log(LOG_ERR, "n_gui_load_theme_json: cJSON not available (compile with -DHAVE_CJSON)");
    return -1;
}

/**
 *@brief save the window layout to a JSON file (stub, requires cJSON)
 *@param ctx GUI context
 *@param filepath path to the output JSON file
 *@return -1 (always fails without cJSON)
 */
int n_gui_save_layout_json(N_GUI_CTX* ctx, const char* filepath) {
    (void)ctx;
    (void)filepath;
    n_log(LOG_ERR, "n_gui_save_layout_json: cJSON not available (compile with -DHAVE_CJSON)");
    return -1;
}

/**
 *@brief load a window layout from a JSON file (stub, requires cJSON)
 *@param ctx GUI context
 *@param filepath path to the input JSON file
 *@return -1 (always fails without cJSON)
 */
int n_gui_load_layout_json(N_GUI_CTX* ctx, const char* filepath) {
    (void)ctx;
    (void)filepath;
    n_log(LOG_ERR, "n_gui_load_layout_json: cJSON not available (compile with -DHAVE_CJSON)");
    return -1;
}

#endif /* HAVE_CJSON */

static void _n_gui_tab_button_clicked(int widget_id, void* user_data) {
    N_GUI_TAB_PANEL* panel = (N_GUI_TAB_PANEL*)user_data;
    if (!panel) return;
    int clicked = -1;
    for (int i = 0; i < panel->nb_tabs; i++) {
        if (panel->button_ids[i] == widget_id) {
            clicked = i;
            break;
        }
    }
    if (clicked < 0) return;
    n_gui_tab_set_active(panel, clicked);
    if (panel->on_tab_change) panel->on_tab_change(clicked, panel->user_data);
}

N_GUI_TAB_PANEL* n_gui_tab_create(N_GUI_CTX* ctx, int window_id, float x, float y, float button_w, float button_h, void (*on_tab_change)(int, void*), void* user_data) {
    if (!ctx) return NULL;
    N_GUI_TAB_PANEL* panel = NULL;
    Malloc(panel, N_GUI_TAB_PANEL, 1);
    if (!panel) return NULL;
    panel->ctx = ctx;
    panel->parent_window_id = window_id;
    panel->nb_tabs = 0;
    panel->active_tab = -1;
    panel->x = x;
    panel->y = y;
    panel->button_w = button_w;
    panel->button_h = button_h;
    panel->on_tab_change = on_tab_change;
    panel->user_data = user_data;
    for (int i = 0; i < N_GUI_TAB_MAX; i++) {
        panel->button_ids[i] = -1;
        panel->content_window_ids[i] = -1;
    }
    return panel;
}

int n_gui_tab_add(N_GUI_TAB_PANEL* panel, const char* label) {
    if (!panel || !label || panel->nb_tabs >= N_GUI_TAB_MAX) return -1;
    int idx = panel->nb_tabs;
    float bx = panel->x + (float)idx * panel->button_w;
    int btn_id = n_gui_add_toggle_button(panel->ctx, panel->parent_window_id,
                                         label, bx, panel->y, panel->button_w, panel->button_h,
                                         N_GUI_SHAPE_RECT, (idx == 0) ? 1 : 0,
                                         _n_gui_tab_button_clicked, panel);
    if (btn_id < 0) return -1;
    panel->button_ids[idx] = btn_id;
    panel->nb_tabs++;
    if (idx == 0) panel->active_tab = 0;
    return idx;
}

void n_gui_tab_set_content_window(N_GUI_TAB_PANEL* panel, int tab_index, int window_id) {
    if (!panel || tab_index < 0 || tab_index >= panel->nb_tabs) return;
    panel->content_window_ids[tab_index] = window_id;
}

void n_gui_tab_set_active(N_GUI_TAB_PANEL* panel, int index) {
    if (!panel || index < 0 || index >= panel->nb_tabs) return;
    for (int i = 0; i < panel->nb_tabs; i++) {
        n_gui_button_set_toggled(panel->ctx, panel->button_ids[i], (i == index) ? 1 : 0);
        if (panel->content_window_ids[i] >= 0) {
            if (i == index)
                n_gui_open_window(panel->ctx, panel->content_window_ids[i]);
            else
                n_gui_close_window(panel->ctx, panel->content_window_ids[i]);
        }
    }
    panel->active_tab = index;
}

int n_gui_tab_get_active(const N_GUI_TAB_PANEL* panel) {
    return panel ? panel->active_tab : -1;
}

/**
 * @brief Move/resize a tab panel's button row after creation.
 *
 * The buttons are laid out once, when each tab is added. A host that owns its
 * own layout (a resizable panel, a split-pane pane) needs to re-place the row
 * afterwards; this re-applies the origin and button size to every tab and
 * refreshes their normalized coordinates so they keep following their window.
 * The content windows are not touched: they are ordinary windows, position
 * them with n_gui_window_set_rect().
 *
 * @param panel the tab panel
 * @param x new x of the first tab button, in the parent window's coordinates
 * @param y new y of the button row
 * @param button_w new per-button width (<= 0 keeps the current one)
 * @param button_h new button height (<= 0 keeps the current one)
 */
void n_gui_tab_relayout(N_GUI_TAB_PANEL* panel, float x, float y, float button_w, float button_h) {
    if (!panel) return;
    const N_GUI_WINDOW* win = n_gui_get_window(panel->ctx, panel->parent_window_id);
    panel->x = x;
    panel->y = y;
    if (button_w > 0.0f) panel->button_w = button_w;
    if (button_h > 0.0f) panel->button_h = button_h;
    for (int i = 0; i < panel->nb_tabs; i++) {
        N_GUI_WIDGET* wgt = n_gui_get_widget(panel->ctx, panel->button_ids[i]);
        if (!wgt) continue;
        wgt->x = panel->x + (float)i * panel->button_w;
        wgt->y = panel->y;
        wgt->w = panel->button_w;
        wgt->h = panel->button_h;
        if (win) _n_gui_widget_capture_normalized(win, wgt);
    }
    if (panel->ctx) panel->ctx->dirty = 1;
}

void n_gui_tab_free(N_GUI_TAB_PANEL** panel) {
    if (!panel || !*panel) return;
    FreeNoLog(*panel);
    *panel = NULL;
}

/* TREE VIEW */

static void _n_gui_tree_listbox_selected(int widget_id, int index, int selected, void* user_data) {
    (void)widget_id;
    N_GUI_TREE* tree = (N_GUI_TREE*)user_data;
    if (!tree || !selected) return;
    if (index < 0 || index >= tree->nb_visible) return;
    int node_idx = tree->visible_map[index];
    if (node_idx < 0 || node_idx >= tree->nb_nodes) return;
    if (tree->nodes[node_idx].has_children) n_gui_tree_toggle_expand(tree, node_idx);
    if (tree->on_select) tree->on_select(node_idx, tree->user_data);
}

N_GUI_TREE* n_gui_tree_create(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, void (*on_select)(int, void*), void* user_data) {
    if (!ctx) return NULL;
    N_GUI_TREE* tree = NULL;
    Malloc(tree, N_GUI_TREE, 1);
    if (!tree) return NULL;
    tree->ctx = ctx;
    tree->window_id = window_id;
    tree->nb_nodes = 0;
    tree->nb_visible = 0;
    tree->on_select = on_select;
    tree->user_data = user_data;
    tree->on_drop = NULL;
    tree->drop_user_data = NULL;
    tree->drag_source_node = -1;
    tree->drag_armed_node = -1;
    tree->drag_hover_node = -1;
    tree->drop_position = N_GUI_DROP_INTO;
    tree->drag_active = 0;
    tree->drag_press_x = 0.0f;
    tree->drag_press_y = 0.0f;
    tree->mouse_b1_prev = 0;
    tree->listbox_id = n_gui_add_listbox(ctx, window_id, x, y, w, h,
                                         N_GUI_SELECT_SINGLE, _n_gui_tree_listbox_selected, tree);
    if (tree->listbox_id < 0) {
        FreeNoLog(tree);
        return NULL;
    }
    return tree;
}

static int _n_gui_tree_node_visible(N_GUI_TREE* tree, int node_index) {
    int pi = tree->nodes[node_index].parent_index;
    while (pi >= 0) {
        if (!tree->nodes[pi].expanded) return 0;
        pi = tree->nodes[pi].parent_index;
    }
    return 1;
}

int n_gui_tree_add_node(N_GUI_TREE* tree, const char* label, int parent_index, void* user_data) {
    if (!tree || !label || tree->nb_nodes >= N_GUI_TREE_MAX) return -1;
    if (parent_index >= tree->nb_nodes) return -1;
    int idx = tree->nb_nodes;
    N_GUI_TREE_NODE* node = &tree->nodes[idx];
    snprintf(node->label, sizeof(node->label), "%s", label);
    node->parent_index = parent_index;
    node->expanded = 0;
    node->has_children = 0;
    node->user_data = user_data;
    if (parent_index < 0) {
        node->depth = 0;
    } else {
        node->depth = tree->nodes[parent_index].depth + 1;
        tree->nodes[parent_index].has_children = 1;
    }
    tree->nb_nodes++;
    n_gui_tree_rebuild(tree);
    return idx;
}

void n_gui_tree_clear(N_GUI_TREE* tree) {
    if (!tree) return;
    tree->nb_nodes = 0;
    tree->nb_visible = 0;
    n_gui_listbox_clear(tree->ctx, tree->listbox_id);
}

void n_gui_tree_remove_node(N_GUI_TREE* tree, int node_index) {
    if (!tree) return;
    if (node_index < 0 || node_index >= tree->nb_nodes) return;

    /* Mark the target and every descendant for removal. Iterate until
       no new marks appear so children of already-marked parents are
       captured regardless of insertion order. */
    char removed[N_GUI_TREE_MAX];
    memset(removed, 0, (size_t)tree->nb_nodes);
    removed[node_index] = 1;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < tree->nb_nodes; i++) {
            if (removed[i]) continue;
            int p = tree->nodes[i].parent_index;
            if (p >= 0 && removed[p]) {
                removed[i] = 1;
                changed = 1;
            }
        }
    }

    /* Build old-index -> new-index map for surviving nodes. */
    int remap[N_GUI_TREE_MAX];
    int new_count = 0;
    for (int i = 0; i < tree->nb_nodes; i++) {
        remap[i] = removed[i] ? -1 : new_count++;
    }

    /* Compact nodes in place, translating parent_index through remap. */
    for (int i = 0; i < tree->nb_nodes; i++) {
        if (removed[i]) continue;
        int dst = remap[i];
        if (dst != i) tree->nodes[dst] = tree->nodes[i];
        if (tree->nodes[dst].parent_index >= 0) {
            tree->nodes[dst].parent_index = remap[tree->nodes[dst].parent_index];
        }
    }
    tree->nb_nodes = new_count;

    /* Recompute has_children flags from scratch, any node whose
       only children were removed must no longer show the marker. */
    for (int i = 0; i < tree->nb_nodes; i++) {
        tree->nodes[i].has_children = 0;
    }
    for (int i = 0; i < tree->nb_nodes; i++) {
        int p = tree->nodes[i].parent_index;
        if (p >= 0) tree->nodes[p].has_children = 1;
    }

    n_gui_tree_rebuild(tree);
}

void n_gui_tree_set_label(N_GUI_TREE* tree, int node_index, const char* new_label) {
    if (!tree || !new_label) return;
    if (node_index < 0 || node_index >= tree->nb_nodes) return;
    snprintf(tree->nodes[node_index].label,
             sizeof(tree->nodes[node_index].label), "%s", new_label);
    n_gui_tree_rebuild(tree);
}

int n_gui_tree_get_selection(N_GUI_TREE* tree) {
    if (!tree) return -1;
    int row = n_gui_listbox_get_selected(tree->ctx, tree->listbox_id);
    if (row < 0 || row >= tree->nb_visible) return -1;
    return tree->visible_map[row];
}

void n_gui_tree_set_selection(N_GUI_TREE* tree, int node_index) {
    if (!tree) return;
    if (node_index < 0 || node_index >= tree->nb_nodes) return;

    /* Expand every ancestor so node_index will appear in visible_map. */
    int p = tree->nodes[node_index].parent_index;
    while (p >= 0) {
        tree->nodes[p].expanded = 1;
        p = tree->nodes[p].parent_index;
    }
    n_gui_tree_rebuild(tree);

    /* Find the visible row for the node and mark it selected. */
    for (int row = 0; row < tree->nb_visible; row++) {
        if (tree->visible_map[row] == node_index) {
            n_gui_listbox_set_selected(tree->ctx, tree->listbox_id, row, 1);
            n_gui_listbox_set_scroll_offset(tree->ctx, tree->listbox_id, row);
            return;
        }
    }
}

int n_gui_tree_find_by_user_data(const N_GUI_TREE* tree, const void* ptr) {
    if (!tree || !ptr) return -1;
    for (int i = 0; i < tree->nb_nodes; i++) {
        if (tree->nodes[i].user_data == ptr) return i;
    }
    return -1;
}

void n_gui_tree_toggle_expand(N_GUI_TREE* tree, int node_index) {
    if (!tree || node_index < 0 || node_index >= tree->nb_nodes) return;
    if (!tree->nodes[node_index].has_children) return;
    tree->nodes[node_index].expanded = !tree->nodes[node_index].expanded;
    n_gui_tree_rebuild(tree);
}

void n_gui_tree_rebuild(N_GUI_TREE* tree) {
    if (!tree) return;
    /* Remember the logical node that was selected before we rewrite
       the listbox. Without this, any rebuild triggered by user
       interaction (e.g. clicking a folder to expand it) drops the
       selection because n_gui_listbox_clear wipes per-row state, so
       folder nodes never appear visually selected. Requests weren't
       affected because clicking a leaf doesn't trigger a rebuild. */
    int saved_node = n_gui_tree_get_selection(tree);
    int saved_scroll = n_gui_listbox_get_scroll_offset(tree->ctx, tree->listbox_id);
    n_gui_listbox_clear(tree->ctx, tree->listbox_id);
    tree->nb_visible = 0;
    for (int i = 0; i < tree->nb_nodes; i++) {
        if (!_n_gui_tree_node_visible(tree, i)) continue;
        N_GUI_TREE_NODE* node = &tree->nodes[i];
        char display[256];
        char indent[64];
        int pad = node->depth * 2;
        if (pad >= (int)sizeof(indent) - 1) pad = (int)sizeof(indent) - 2;
        memset(indent, ' ', (size_t)pad);
        indent[pad] = '\0';
        const char* marker = node->has_children ? (node->expanded ? "v " : "> ") : "  ";
        snprintf(display, sizeof(display), "%s%s%s", indent, marker, node->label);
        n_gui_listbox_add_item(tree->ctx, tree->listbox_id, display);
        if (tree->nb_visible < N_GUI_TREE_MAX) {
            tree->visible_map[tree->nb_visible] = i;
            tree->nb_visible++;
        }
    }
    /* Restore selection if the saved node is still visible. If the
       node was removed, collapsed-behind-an-ancestor or otherwise
       gone, leave the selection cleared, callers that need a
       specific new selection use n_gui_tree_set_selection. */
    if (saved_node >= 0) {
        for (int row = 0; row < tree->nb_visible; row++) {
            if (tree->visible_map[row] == saved_node) {
                n_gui_listbox_set_selected(tree->ctx, tree->listbox_id,
                                           row, 1);
                break;
            }
        }
    }
    /* Restore scroll position, clamped to valid range */
    if (saved_scroll > 0) {
        n_gui_listbox_set_scroll_offset(tree->ctx, tree->listbox_id, saved_scroll);
    }
}

void n_gui_tree_free(N_GUI_TREE** tree) {
    if (!tree || !*tree) return;
    FreeNoLog(*tree);
    *tree = NULL;
}

/* TREE VIEW STATE (survives a rebuild that replaces every node) */

/**
 * @brief Write the path of a node into a buffer.
 *
 * The path is the node's label preceded by its ancestors', joined by
 * N_GUI_TREE_PATH_SEP (a unit separator, which cannot occur inside a label, so
 * the path is unambiguous). It identifies a node across a rebuild, which node
 * indices and user_data pointers do not when the model behind the tree was
 * reloaded from disk.
 *
 * @param tree the tree
 * @param node_index the node to describe
 * @param buf destination buffer
 * @param bufsz size of @p buf
 * @return 0 on success, -1 on an invalid node or a path that does not fit
 */
int n_gui_tree_node_path(const N_GUI_TREE* tree, int node_index, char* buf, size_t bufsz) {
    int chain[N_GUI_TREE_MAX];
    int depth = 0;
    size_t used = 0;
    int i;

    if (!tree || !buf || bufsz == 0) return -1;
    if (node_index < 0 || node_index >= tree->nb_nodes) return -1;

    /* walk up to the root, then emit in root-first order */
    for (i = node_index; i >= 0 && depth < N_GUI_TREE_MAX; i = tree->nodes[i].parent_index) {
        chain[depth++] = i;
        if (tree->nodes[i].parent_index < 0) break;
    }

    buf[0] = '\0';
    for (i = depth - 1; i >= 0; i--) {
        // cppcheck-suppress uninitvar ; node_index is validated above so the walk always stores at least chain[0], and this loop only reads what it stored
        const char* label = tree->nodes[chain[i]].label;
        size_t len = strlen(label);
        size_t need = len + (used > 0 ? 1u : 0u);
        if (used + need + 1 > bufsz) return -1;
        if (used > 0) buf[used++] = N_GUI_TREE_PATH_SEP;
        memcpy(buf + used, label, len);
        used += len;
        buf[used] = '\0';
    }
    return 0;
}

int n_gui_tree_find_by_path(const N_GUI_TREE* tree, const char* path) {
    char buf[N_GUI_TREE_PATH_MAX];
    int i;
    if (!tree || !path) return -1;
    for (i = 0; i < tree->nb_nodes; i++) {
        if (n_gui_tree_node_path(tree, i, buf, sizeof(buf)) != 0) continue;
        if (strcmp(buf, path) == 0) return i;
    }
    return -1;
}

int n_gui_tree_state_save(const N_GUI_TREE* tree, N_GUI_TREE_STATE* out) {
    char buf[N_GUI_TREE_PATH_MAX];
    int i, sel;

    if (!tree || !out) return -1;
    memset(out, 0, sizeof(*out));

    for (i = 0; i < tree->nb_nodes && out->nb_expanded < N_GUI_TREE_STATE_MAX; i++) {
        if (!tree->nodes[i].expanded) continue;
        if (n_gui_tree_node_path(tree, i, buf, sizeof(buf)) != 0) continue;
        out->expanded[out->nb_expanded] = strdup(buf);
        if (out->expanded[out->nb_expanded]) out->nb_expanded++;
    }

    sel = n_gui_tree_get_selection((N_GUI_TREE*)tree);
    if (sel >= 0 && n_gui_tree_node_path(tree, sel, buf, sizeof(buf)) == 0) {
        out->selected = strdup(buf);
    }

    out->scroll_offset = n_gui_listbox_get_scroll_offset(tree->ctx, tree->listbox_id);
    out->h_scroll = n_gui_listbox_get_h_scroll(tree->ctx, tree->listbox_id);
    return 0;
}

void n_gui_tree_state_apply(N_GUI_TREE* tree, const N_GUI_TREE_STATE* state) {
    char buf[N_GUI_TREE_PATH_MAX];
    int i, p;

    if (!tree || !state) return;

    for (i = 0; i < tree->nb_nodes; i++) {
        if (n_gui_tree_node_path(tree, i, buf, sizeof(buf)) != 0) continue;
        for (p = 0; p < state->nb_expanded; p++) {
            if (state->expanded[p] && strcmp(state->expanded[p], buf) == 0) {
                tree->nodes[i].expanded = 1;
                break;
            }
        }
    }
    n_gui_tree_rebuild(tree);

    /* Selection first: it expands the ancestors of the selected node and
       scrolls to reveal it, which would otherwise undo the scroll restore. */
    if (state->selected) {
        int sel = n_gui_tree_find_by_path(tree, state->selected);
        if (sel >= 0) n_gui_tree_set_selection(tree, sel);
    }
    n_gui_listbox_set_scroll_offset(tree->ctx, tree->listbox_id, state->scroll_offset);
    n_gui_listbox_set_h_scroll(tree->ctx, tree->listbox_id, state->h_scroll);
}

void n_gui_tree_state_free(N_GUI_TREE_STATE* state) {
    int i;
    if (!state) return;
    for (i = 0; i < state->nb_expanded; i++) {
        FreeNoLog(state->expanded[i]);
    }
    state->nb_expanded = 0;
    FreeNoLog(state->selected);
}

/* TREE DRAG-REORDER */

/*! pixels of mouse movement before a press becomes a drag */
#define _N_GUI_TREE_DRAG_THRESHOLD_PX 4.0f

void n_gui_tree_set_on_drop(N_GUI_TREE* tree,
                            n_gui_tree_on_drop_t on_drop,
                            void* user_data) {
    if (!tree) return;
    tree->on_drop = on_drop;
    tree->drop_user_data = user_data;
    tree->drag_source_node = -1;
    tree->drag_armed_node = -1;
    tree->drag_hover_node = -1;
    tree->drag_active = 0;
}

/*! Return the listbox's absolute (screen-space) bounds and geometry
 *  via out params. `out_item_area_w` is the row content width (total
 *  width minus the scrollbar column, when one is needed) and matches
 *  where the selection / hover highlight actually paints.
 *  `out_inset` is the horizontal padding applied to row highlights,
 *  kept in sync with _draw_listbox so DnD indicators line up exactly.
 *  Returns 1 on success, 0 on failure. */
static int _n_gui_tree_listbox_bounds(N_GUI_TREE* tree,
                                      float* out_x,
                                      float* out_y,
                                      float* out_w,
                                      float* out_h,
                                      float* out_item_area_w,
                                      float* out_inset,
                                      float* out_item_h,
                                      int* out_scroll_off) {
    if (!tree) return 0;
    const N_GUI_WIDGET* w = n_gui_get_widget(tree->ctx, tree->listbox_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return 0;
    const N_GUI_WINDOW* win = n_gui_get_window(tree->ctx, tree->window_id);
    if (!win) return 0;
    const N_GUI_LISTBOX_DATA* ld = (const N_GUI_LISTBOX_DATA*)w->data;
    const N_GUI_STYLE* style = &tree->ctx->style;

    *out_x = win->x + w->x;
    *out_y = win->y + win->titlebar_h + w->y;
    *out_w = w->w;
    *out_h = w->h;
    *out_item_h = (ld->item_height > 0.0f) ? ld->item_height : 20.0f;
    *out_scroll_off = ld->scroll_offset;

    /* Match _draw_listbox: the scrollbar column is reserved when the
       number of items overflows the visible area. Subtract it from
       the content-area width so hit-testing and overlay indicators
       stop exactly where row highlights stop. */
    int visible_count = (int)(w->h / *out_item_h);
    int need_scrollbar = ((int)ld->nb_items > visible_count) ? 1 : 0;
    float sb_w = need_scrollbar ? style->scrollbar_size : 0.0f;
    *out_item_area_w = w->w - sb_w;
    *out_inset = style->item_selection_inset;
    return 1;
}

/*! Hit-test the mouse against the listbox's visible rows and fill
 *  the out params. Returns 1 if the mouse is over a valid row, 0
 *  otherwise. Points inside the scrollbar column are excluded so
 *  the user cannot start a drag by grabbing the scrollbar. */
static int _n_gui_tree_hit_row(N_GUI_TREE* tree, float mx, float my, int* out_node_idx, int* out_drop_pos) {
    float lx, ly, lw, lh, iw, inset, ih;
    int scroll;
    if (!_n_gui_tree_listbox_bounds(tree, &lx, &ly, &lw, &lh,
                                    &iw, &inset, &ih, &scroll))
        return 0;
    (void)lw; /* scrollbar column is excluded via iw below */
    (void)inset;
    if (mx < lx || mx > lx + iw || my < ly || my > ly + lh) return 0;

    int row_in_view = (int)((my - ly) / ih);
    int actual_row = row_in_view + scroll;
    if (actual_row < 0 || actual_row >= tree->nb_visible) return 0;

    int node_idx = tree->visible_map[actual_row];
    if (node_idx < 0 || node_idx >= tree->nb_nodes) return 0;

    /* Tighter INTO zone: top 40% -> BEFORE, middle 20% -> INTO,
       bottom 40% -> AFTER. Previously the middle 40% was INTO,
       which made it too easy to accidentally re-nest a folder
       when the user was trying to move it back out between
       siblings. */
    float y_in_row = (my - ly) - (float)row_in_view * ih;
    int pos;
    if (y_in_row < ih * 0.4f)
        pos = N_GUI_DROP_BEFORE;
    else if (y_in_row > ih * 0.6f)
        pos = N_GUI_DROP_AFTER;
    else
        pos = N_GUI_DROP_INTO;

    *out_node_idx = node_idx;
    *out_drop_pos = pos;
    return 1;
}

void n_gui_tree_tick(N_GUI_TREE* tree) {
    if (!tree || !tree->ctx) return;
    if (!tree->on_drop) return;

    const N_GUI_CTX* ctx = tree->ctx;
    int b1 = ctx->mouse_b1;
    int b1_prev = tree->mouse_b1_prev;
    tree->mouse_b1_prev = b1;

    float mx = (float)ctx->mouse_x;
    float my = (float)ctx->mouse_y;

    int hit_node = -1;
    int pos = N_GUI_DROP_INTO;
    int hit = _n_gui_tree_hit_row(tree, mx, my, &hit_node, &pos);

    /* Mouse press: arm the drag if we're over a row. */
    if (b1 && !b1_prev) {
        if (hit) {
            tree->drag_armed_node = hit_node;
            tree->drag_press_x = mx;
            tree->drag_press_y = my;
        } else {
            tree->drag_armed_node = -1;
        }
        tree->drag_active = 0;
        tree->drag_source_node = -1;
        tree->drag_hover_node = -1;
    }

    /* While mouse is held: promote to active drag once past threshold. */
    if (b1 && tree->drag_armed_node >= 0) {
        if (!tree->drag_active) {
            float dx = mx - tree->drag_press_x;
            float dy = my - tree->drag_press_y;
            if (dx * dx + dy * dy >= _N_GUI_TREE_DRAG_THRESHOLD_PX *
                                         _N_GUI_TREE_DRAG_THRESHOLD_PX) {
                tree->drag_active = 1;
                tree->drag_source_node = tree->drag_armed_node;
            }
        }
        if (tree->drag_active) {
            tree->drag_hover_node = hit ? hit_node : -1;
            tree->drop_position = hit ? pos : N_GUI_DROP_INTO;
        }
    }

    /* Mouse release: fire callback if we actually dragged. */
    if (!b1 && b1_prev) {
        if (tree->drag_active && tree->drag_source_node >= 0 && tree->drag_hover_node >= 0 && tree->drag_source_node != tree->drag_hover_node) {
            tree->on_drop(tree->drag_source_node,
                          tree->drag_hover_node,
                          tree->drop_position,
                          tree->drop_user_data);
        }
        tree->drag_source_node = -1;
        tree->drag_armed_node = -1;
        tree->drag_hover_node = -1;
        tree->drag_active = 0;
    }
}

void n_gui_tree_draw_overlay(N_GUI_TREE* tree) {
    if (!tree || !tree->ctx) return;
    if (!tree->drag_active || tree->drag_hover_node < 0) return;

    float lx, ly, lw, lh, iw, inset, ih;
    int scroll;
    if (!_n_gui_tree_listbox_bounds(tree, &lx, &ly, &lw, &lh,
                                    &iw, &inset, &ih, &scroll))
        return;
    (void)lw; /* use iw (item area) so we stop at the scrollbar column */

    /* Find the visible-row index of drag_hover_node, translate to
       absolute Y using the listbox's scroll offset. */
    int row_in_view = -1;
    for (int row = 0; row < tree->nb_visible; row++) {
        if (tree->visible_map[row] == tree->drag_hover_node) {
            row_in_view = row - scroll;
            break;
        }
    }
    if (row_in_view < 0) return;

    float row_y = ly + (float)row_in_view * ih;
    if (row_y < ly || row_y + ih > ly + lh) {
        /* Off-screen, don't draw. */
        return;
    }

    /* Align horizontal span with the selection/hover highlights so
       the indicator looks exactly like part of the row, not a
       stripe floating over the scrollbar or past the text area. */
    float left = lx + inset;
    float right = lx + iw - inset;

    N_GUI_WIDGET* w = n_gui_get_widget(tree->ctx, tree->listbox_id);
    ALLEGRO_COLOR colour = w ? w->theme.border_active
                             : tree->ctx->default_theme.border_active;
    const float thickness = 2.0f;
    const float half = thickness * 0.5f;

    if (tree->drop_position == N_GUI_DROP_INTO) {
        /* Inset by half the stroke so the OUTER edge of the outline
           coincides with (left, row_y) .. (right, row_y+ih), the
           same bounds the selection fill uses. Without this the
           outline straddles the boundary and looks wider than the
           selection rectangle. */
        al_draw_rectangle(left + half, row_y + half,
                          right - half, row_y + ih - half,
                          colour, thickness);
    } else {
        /* Line sits exactly on the row boundary (between rows N-1/N
           for BEFORE, N/N+1 for AFTER). The stroke straddles that
           boundary so the bar reads as "between two rows" rather
           than "inside one row". */
        float line_y = (tree->drop_position == N_GUI_DROP_BEFORE)
                           ? row_y
                           : row_y + ih;
        al_draw_line(left, line_y,
                     right, line_y,
                     colour, thickness);
    }
}

/* KEY-VALUE TABLE */

static void _n_gui_kv_add_clicked(int widget_id, void* user_data) {
    (void)widget_id;
    N_GUI_KVTABLE* table = (N_GUI_KVTABLE*)user_data;
    if (!table) return;
    n_gui_kvtable_add_row(table, "", "", "", 1);
}

static void _n_gui_kv_remove_clicked(int widget_id, void* user_data) {
    N_GUI_KVTABLE* table = (N_GUI_KVTABLE*)user_data;
    if (!table) return;
    for (int i = 0; i < table->nb_rows; i++) {
        if (table->rows[i].remove_id == widget_id && table->rows[i].active) {
            n_gui_kvtable_remove_row(table, i);
            if (table->on_remove) table->on_remove(i, table->user_data);
            return;
        }
    }
}

static void _n_gui_kv_up_clicked(int widget_id, void* user_data) {
    N_GUI_KVTABLE* table = (N_GUI_KVTABLE*)user_data;
    if (!table) return;
    for (int i = 0; i < table->nb_rows; i++) {
        if (table->rows[i].up_id == widget_id && table->rows[i].active) {
            n_gui_kvtable_move_row(table, i, -1);
            return;
        }
    }
}

static void _n_gui_kv_down_clicked(int widget_id, void* user_data) {
    N_GUI_KVTABLE* table = (N_GUI_KVTABLE*)user_data;
    if (!table) return;
    for (int i = 0; i < table->nb_rows; i++) {
        if (table->rows[i].down_id == widget_id && table->rows[i].active) {
            n_gui_kvtable_move_row(table, i, 1);
            return;
        }
    }
}

static void _n_gui_kv_dup_clicked(int widget_id, void* user_data) {
    N_GUI_KVTABLE* table = (N_GUI_KVTABLE*)user_data;
    if (!table) return;
    for (int i = 0; i < table->nb_rows; i++) {
        if (table->rows[i].dup_id == widget_id && table->rows[i].active) {
            n_gui_kvtable_duplicate_row(table, i);
            return;
        }
    }
}

/* One row's editable content, detached from its widgets so rows can be moved
   and copied around without any pointer aliasing between the textareas. */
typedef struct N_GUI_KV_CONTENT {
    char* key;   /*!< owned copy of the key text */
    char* value; /*!< owned copy of the value text */
    char* desc;  /*!< owned copy of the description text */
    int enabled; /*!< enabled checkbox state */
} N_GUI_KV_CONTENT;

static char* _n_gui_kv_dup_text(N_GUI_CTX* ctx, int widget_id) {
    const char* t = n_gui_textarea_get_text(ctx, widget_id);
    return strdup(t ? t : "");
}

static void _n_gui_kv_content_grab(N_GUI_KVTABLE* table, int idx, N_GUI_KV_CONTENT* out) {
    const N_GUI_KV_ROW* row = &table->rows[idx];
    out->key = _n_gui_kv_dup_text(table->ctx, row->key_id);
    out->value = _n_gui_kv_dup_text(table->ctx, row->value_id);
    out->desc = _n_gui_kv_dup_text(table->ctx, row->desc_id);
    out->enabled = n_gui_checkbox_is_checked(table->ctx, row->enabled_id);
}

static void _n_gui_kv_content_put(N_GUI_KVTABLE* table, int idx, const N_GUI_KV_CONTENT* in) {
    const N_GUI_KV_ROW* row = &table->rows[idx];
    n_gui_textarea_set_text(table->ctx, row->key_id, in->key ? in->key : "");
    n_gui_textarea_set_text(table->ctx, row->value_id, in->value ? in->value : "");
    n_gui_textarea_set_text(table->ctx, row->desc_id, in->desc ? in->desc : "");
    n_gui_checkbox_set_checked(table->ctx, row->enabled_id, in->enabled);
}

static void _n_gui_kv_content_free(N_GUI_KV_CONTENT* c) {
    FreeNoLog(c->key);
    FreeNoLog(c->value);
    FreeNoLog(c->desc);
}

/* Fill @p out with the active row slots in display order, returning the count. */
static int _n_gui_kv_active_order(const N_GUI_KVTABLE* table, int* out) {
    int n = 0;
    for (int i = 0; i < table->nb_rows; i++) {
        if (table->rows[i].active) out[n++] = i;
    }
    return n;
}

/* Enable/disable the move buttons of every active row so the first row cannot
   be moved up and the last cannot be moved down. */
static void _n_gui_kv_refresh_actions(N_GUI_KVTABLE* table) {
    int order[N_GUI_KV_MAX];
    int n = _n_gui_kv_active_order(table, order);
    for (int p = 0; p < n; p++) {
        const N_GUI_KV_ROW* row = &table->rows[order[p]];
        n_gui_set_widget_enabled(table->ctx, row->up_id, (p > 0) ? 1 : 0);
        n_gui_set_widget_enabled(table->ctx, row->down_id, (p < n - 1) ? 1 : 0);
    }
}

/* Apply full layout to all KV table widgets: positions, sizes, and norms.
   Scales horizontal metrics by the parent window's current width relative to
   the width captured at create time, so the table tracks the window the same
   way other SCALE-policy widgets do. Vertical metrics scale only under
   N_GUI_WIN_RESIZE_SCALE (see below). Updating norm_x/y/w/h after each call
   keeps n_gui_apply_adaptive_resize in sync on the next display resize. */
static void _n_gui_kv_apply_layout(N_GUI_KVTABLE* table) {
    if (!table) return;
    const N_GUI_WINDOW* win = n_gui_get_window(table->ctx, table->window_id);
    if (!win) return;

    /* Widths always follow the window: a wider dialog gives the text columns more
     * room. Heights follow it only when the window scales its widgets
     * (N_GUI_WIN_RESIZE_SCALE), where the rows must stay consistent with the
     * already-scaled widgets around them. Under _MOVE / _NONE the rows keep their
     * design height instead, so a user-resizable dialog that grows taller reveals
     * MORE rows rather than fatter ones (the window's scrollbar takes over past
     * that). Fixed-size dialogs are unaffected either way: there win->h ==
     * init_win_h, so the factor is 1. */
    float sx = (table->init_win_w > 0.0f) ? (win->w / table->init_win_w) : 1.0f;
    /* the two ways a window scales the widgets around the table: the embedded
     * SCALE resize policy, and a detached native window carrying
     * N_GUI_DETACH_SCALE_CONTENT (which rescales widgets from their norms) */
    int scales_content = (win->resize_policy == N_GUI_WIN_RESIZE_SCALE) ||
                         (win->native && (win->detach_flags & N_GUI_DETACH_SCALE_CONTENT));
    float sy = (scales_content && table->init_win_h > 0.0f) ? (win->h / table->init_win_h) : 1.0f;

    float pad_x = table->padding * sx;
    float pad_y = table->padding * sy;
    float hdr_h_outer = table->header_height * sy;
    float row_h = table->row_height * sy;
    float gap = 6.0f * sx;

    float total_w = win->w - pad_x * 2.0f;
    if (total_w < 0.0f) total_w = 0.0f;

    float rh_inner = row_h - 4.0f * sy;
    if (rh_inner < 1.0f) rh_inner = 1.0f;
    float rw_inner = table->row_height * sx - 4.0f * sx;
    if (rw_inner < 1.0f) rw_inner = 1.0f;
    float lbl_h = 18.0f * sy;
    if (lbl_h < 1.0f) lbl_h = 1.0f;
    float chk_w = rw_inner;
    float rem_w = rw_inner;
    float act_w = rw_inner;

    /* Column layout: the key column and the remove ("x") button are always
       shown; Value, Description and the enabled checkbox are optional. A full
       three-text-column table keeps the historical 0.27*width proportions; a
       reduced table divides the remaining width evenly so a single-value list
       fills the row. */
    int text_cols = 1 + (table->show_value ? 1 : 0) + (table->show_desc ? 1 : 0);
    float trailing = (table->show_enabled ? (chk_w + gap) : 0.0f) + gap + rem_w +
                     (table->show_actions ? 3.0f * (act_w + gap) : 0.0f);
    float col_w;
    /* the historical 0.27 proportions leave no room for the action buttons, so
       a table that shows them divides the remaining width instead */
    if (text_cols == 3 && !table->show_actions) {
        col_w = total_w * 0.27f;
    } else {
        float avail = total_w - trailing;
        if (avail < 0.0f) avail = 0.0f;
        col_w = avail / (float)text_cols;
    }
    if (col_w < 0.0f) col_w = 0.0f;

    /* Reserve top_offset above the header so a caller can share the window with
       a toolbar/heading without overlapping the table. */
    float top = pad_y + table->top_offset * sy;

    N_GUI_WIDGET* w;

    /* Header labels (hidden columns are hidden and skipped). */
    float hx = pad_x;
    w = n_gui_get_widget(table->ctx, table->lbl_key);
    if (w) {
        w->x = hx;
        w->y = top;
        w->w = col_w;
        w->h = lbl_h;
        _n_gui_widget_capture_normalized(win, w);
    }
    hx += col_w;
    n_gui_set_widget_visible(table->ctx, table->lbl_value, table->show_value);
    if (table->show_value) {
        w = n_gui_get_widget(table->ctx, table->lbl_value);
        if (w) {
            w->x = hx;
            w->y = top;
            w->w = col_w;
            w->h = lbl_h;
            _n_gui_widget_capture_normalized(win, w);
        }
        hx += col_w;
    }
    n_gui_set_widget_visible(table->ctx, table->lbl_desc, table->show_desc);
    if (table->show_desc) {
        w = n_gui_get_widget(table->ctx, table->lbl_desc);
        if (w) {
            w->x = hx;
            w->y = top;
            w->w = col_w;
            w->h = lbl_h;
            _n_gui_widget_capture_normalized(win, w);
        }
        hx += col_w;
    }
    n_gui_set_widget_visible(table->ctx, table->lbl_enabled, table->show_enabled);
    if (table->show_enabled) {
        w = n_gui_get_widget(table->ctx, table->lbl_enabled);
        if (w) {
            w->x = hx + gap;
            w->y = top;
            w->w = chk_w;
            w->h = lbl_h;
            _n_gui_widget_capture_normalized(win, w);
        }
    }

    /* Active rows below the header; hidden columns are hidden and skipped. */
    float cy = top + hdr_h_outer;
    for (int i = 0; i < table->nb_rows; i++) {
        if (!table->rows[i].active) continue;
        const N_GUI_KV_ROW* row = &table->rows[i];
        float fx = pad_x;
        w = n_gui_get_widget(table->ctx, row->key_id);
        if (w) {
            w->x = fx;
            w->y = cy;
            w->w = col_w;
            w->h = rh_inner;
            _n_gui_widget_capture_normalized(win, w);
        }
        fx += col_w;
        n_gui_set_widget_visible(table->ctx, row->value_id, table->show_value);
        if (table->show_value) {
            w = n_gui_get_widget(table->ctx, row->value_id);
            if (w) {
                w->x = fx;
                w->y = cy;
                w->w = col_w;
                w->h = rh_inner;
                _n_gui_widget_capture_normalized(win, w);
            }
            fx += col_w;
        }
        n_gui_set_widget_visible(table->ctx, row->desc_id, table->show_desc);
        if (table->show_desc) {
            w = n_gui_get_widget(table->ctx, row->desc_id);
            if (w) {
                w->x = fx;
                w->y = cy;
                w->w = col_w;
                w->h = rh_inner;
                _n_gui_widget_capture_normalized(win, w);
            }
            fx += col_w;
        }
        n_gui_set_widget_visible(table->ctx, row->enabled_id, table->show_enabled);
        if (table->show_enabled) {
            w = n_gui_get_widget(table->ctx, row->enabled_id);
            if (w) {
                w->x = fx + gap;
                w->y = cy;
                w->w = chk_w;
                w->h = rh_inner;
                _n_gui_widget_capture_normalized(win, w);
            }
            fx += gap + chk_w;
        }
        /* per-row actions, left of the remove button and in reading order:
           move up, move down, duplicate */
        n_gui_set_widget_visible(table->ctx, row->up_id, table->show_actions);
        n_gui_set_widget_visible(table->ctx, row->down_id, table->show_actions);
        n_gui_set_widget_visible(table->ctx, row->dup_id, table->show_actions);
        if (table->show_actions) {
            const int act_ids[3] = {row->up_id, row->down_id, row->dup_id};
            for (int a = 0; a < 3; a++) {
                fx += gap;
                w = n_gui_get_widget(table->ctx, act_ids[a]);
                if (w) {
                    w->x = fx;
                    w->y = cy;
                    w->w = act_w;
                    w->h = rh_inner;
                    _n_gui_widget_capture_normalized(win, w);
                }
                fx += act_w;
            }
        }
        fx += gap;
        w = n_gui_get_widget(table->ctx, row->remove_id);
        if (w) {
            w->x = fx;
            w->y = cy;
            w->w = rem_w;
            w->h = rh_inner;
            _n_gui_widget_capture_normalized(win, w);
        }
        cy += row_h;
    }

    /* "+" add-row button anchored below the last active row. A table with row
       actions draws it as a glyph like the rest of its buttons, which reads far
       better than a font '+' at this size. */
    w = n_gui_get_widget(table->ctx, table->btn_add);
    if (w) {
        float btn_w = 30.0f * sx;
        w->x = pad_x;
        w->y = cy;
        w->w = btn_w;
        w->h = rh_inner;
        _n_gui_widget_capture_normalized(win, w);
    }
}

N_GUI_KVTABLE* n_gui_kvtable_create(N_GUI_CTX* ctx, int window_id, float row_height, float padding, void (*on_remove)(int, void*), void* user_data) {
    if (!ctx) return NULL;
    N_GUI_KVTABLE* table = NULL;
    Malloc(table, N_GUI_KVTABLE, 1);
    if (!table) return NULL;
    memset(table, 0, sizeof(*table));
    table->ctx = ctx;
    table->window_id = window_id;
    table->nb_rows = 0;
    table->nb_active = 0;
    table->row_height = row_height;
    table->padding = padding;
    table->header_height = 18.0f + 2.0f;
    table->on_remove = on_remove;
    table->user_data = user_data;
    table->show_value = 1; /* full three-column table by default (backward compatible) */
    table->show_desc = 1;
    table->show_enabled = 1;
    table->key_placeholder = NULL;
    table->value_placeholder = NULL;
    table->top_offset = 0.0f;

    /* Capture parent window dimensions for later scale ratios. */
    const N_GUI_WINDOW* wptr = n_gui_get_window(ctx, window_id);
    table->init_win_w = wptr ? wptr->w : 0.0f;
    table->init_win_h = wptr ? wptr->h : 0.0f;

    /* Widgets are added at placeholder coords; _n_gui_kv_apply_layout
       sets final positions, sizes, and normalized coords below. */
    float rh_inner = row_height - 4.0f;
    if (rh_inner < 1.0f) rh_inner = 1.0f;
    float hdr_h_label = 18.0f;
    table->lbl_key = n_gui_add_label(ctx, window_id, "Key",
                                     padding, padding, 100.0f, hdr_h_label, N_GUI_ALIGN_LEFT);
    table->lbl_value = n_gui_add_label(ctx, window_id, "Value",
                                       padding, padding, 100.0f, hdr_h_label, N_GUI_ALIGN_LEFT);
    table->lbl_desc = n_gui_add_label(ctx, window_id, "Description",
                                      padding, padding, 100.0f, hdr_h_label, N_GUI_ALIGN_LEFT);
    table->lbl_enabled = n_gui_add_label(ctx, window_id, "On",
                                         padding, padding, rh_inner, hdr_h_label, N_GUI_ALIGN_LEFT);

    table->btn_add = n_gui_add_button(ctx, window_id,
                                      "+", padding, padding, 30.0f, rh_inner,
                                      N_GUI_SHAPE_ROUNDED, _n_gui_kv_add_clicked, table);

    _n_gui_kv_apply_layout(table);
    return table;
}

int n_gui_kvtable_add_row(N_GUI_KVTABLE* table,
                          const char* key,
                          const char* value,
                          const char* description,
                          int enabled) {
    if (!table) return -1;
    N_GUI_CTX* ctx = table->ctx;
    int win = table->window_id;
    float pad = table->padding;
    float rh = table->row_height - 4.0f;
    if (rh < 1.0f) rh = 1.0f;

    /* Reuse a slot freed by remove/clear before growing the array, so repeated
       clear+repopulate cycles do not accumulate widgets toward N_GUI_KV_MAX. */
    int idx = -1;
    for (int i = 0; i < table->nb_rows; i++) {
        if (!table->rows[i].active) {
            idx = i;
            break;
        }
    }
    N_GUI_KV_ROW* row;
    if (idx >= 0) {
        row = &table->rows[idx];
        n_gui_set_widget_visible(ctx, row->key_id, 1);
        n_gui_set_widget_visible(ctx, row->value_id, 1);
        n_gui_set_widget_visible(ctx, row->desc_id, 1);
        n_gui_set_widget_visible(ctx, row->enabled_id, 1);
        n_gui_set_widget_visible(ctx, row->remove_id, 1);
        n_gui_set_widget_visible(ctx, row->up_id, table->show_actions);
        n_gui_set_widget_visible(ctx, row->down_id, table->show_actions);
        n_gui_set_widget_visible(ctx, row->dup_id, table->show_actions);
        n_gui_textarea_set_text(ctx, row->key_id, key ? key : "");
        n_gui_textarea_set_text(ctx, row->value_id, value ? value : "");
        n_gui_textarea_set_text(ctx, row->desc_id, description ? description : "");
        n_gui_checkbox_set_checked(ctx, row->enabled_id, enabled);
    } else {
        if (table->nb_rows >= N_GUI_KV_MAX) return -1;
        idx = table->nb_rows;
        row = &table->rows[idx];
        /* Placeholder coords; _n_gui_kv_apply_layout assigns the real positions,
           sizes, and norms below so the new widgets match the current scale of
           the parent window. */
        row->key_id = n_gui_add_textarea(ctx, win, pad, pad, 100.0f, rh, 0, 256, NULL, NULL);
        row->value_id = n_gui_add_textarea(ctx, win, pad, pad, 100.0f, rh, 0, 1024, NULL, NULL);
        row->desc_id = n_gui_add_textarea(ctx, win, pad, pad, 100.0f, rh, 0, 256, NULL, NULL);
        row->enabled_id = n_gui_add_checkbox(ctx, win, "", pad, pad, rh, rh, enabled, NULL, NULL);
        row->remove_id = n_gui_add_button(ctx, win, "x", pad, pad, rh, rh,
                                          N_GUI_SHAPE_RECT, _n_gui_kv_remove_clicked, table);
        /* Row actions. The buttons exist on every row but stay hidden until
           n_gui_kvtable_set_row_actions turns them on, so a table can gain them
           at any point without rebuilding its rows. They are square and tiny:
           glyphs rather than labels, with the action name in a tooltip. */
        row->up_id = n_gui_add_button(ctx, win, "Move up", pad, pad, rh, rh,
                                      N_GUI_SHAPE_RECT, _n_gui_kv_up_clicked, table);
        row->down_id = n_gui_add_button(ctx, win, "Move down", pad, pad, rh, rh,
                                        N_GUI_SHAPE_RECT, _n_gui_kv_down_clicked, table);
        row->dup_id = n_gui_add_button(ctx, win, "Duplicate", pad, pad, rh, rh,
                                       N_GUI_SHAPE_RECT, _n_gui_kv_dup_clicked, table);
        n_gui_button_set_glyph(ctx, row->up_id, N_GUI_GLYPH_ARROW_UP);
        n_gui_button_set_glyph(ctx, row->down_id, N_GUI_GLYPH_ARROW_DOWN);
        n_gui_button_set_glyph(ctx, row->dup_id, N_GUI_GLYPH_COPY);
        n_gui_set_widget_tooltip(ctx, row->up_id, "Move up");
        n_gui_set_widget_tooltip(ctx, row->down_id, "Move down");
        n_gui_set_widget_tooltip(ctx, row->dup_id, "Duplicate");
        n_gui_set_widget_visible(ctx, row->up_id, table->show_actions);
        n_gui_set_widget_visible(ctx, row->down_id, table->show_actions);
        n_gui_set_widget_visible(ctx, row->dup_id, table->show_actions);
        if (table->show_actions) {
            n_gui_button_set_glyph(ctx, row->remove_id, N_GUI_GLYPH_CROSS);
            n_gui_set_widget_tooltip(ctx, row->remove_id, "Remove");
        }
        if (key) n_gui_textarea_set_text(ctx, row->key_id, key);
        if (value) n_gui_textarea_set_text(ctx, row->value_id, value);
        if (description) n_gui_textarea_set_text(ctx, row->desc_id, description);
        if (table->key_placeholder) n_gui_textarea_set_placeholder(ctx, row->key_id, table->key_placeholder);
        if (table->value_placeholder) n_gui_textarea_set_placeholder(ctx, row->value_id, table->value_placeholder);
        table->nb_rows++;
    }
    row->active = 1;
    table->nb_active++;
    _n_gui_kv_apply_layout(table);
    if (table->show_actions) _n_gui_kv_refresh_actions(table);
    return idx;
}

void n_gui_kvtable_remove_row(N_GUI_KVTABLE* table, int row_index) {
    if (!table || row_index < 0 || row_index >= table->nb_rows) return;
    if (!table->rows[row_index].active) return;
    N_GUI_KV_ROW* row = &table->rows[row_index];
    n_gui_set_widget_visible(table->ctx, row->key_id, 0);
    n_gui_set_widget_visible(table->ctx, row->value_id, 0);
    n_gui_set_widget_visible(table->ctx, row->desc_id, 0);
    n_gui_set_widget_visible(table->ctx, row->enabled_id, 0);
    n_gui_set_widget_visible(table->ctx, row->remove_id, 0);
    n_gui_set_widget_visible(table->ctx, row->up_id, 0);
    n_gui_set_widget_visible(table->ctx, row->down_id, 0);
    n_gui_set_widget_visible(table->ctx, row->dup_id, 0);
    row->active = 0;
    table->nb_active--;
    _n_gui_kv_apply_layout(table);
    if (table->show_actions) _n_gui_kv_refresh_actions(table);
}

void n_gui_kvtable_clear(N_GUI_KVTABLE* table) {
    if (!table) return;
    for (int i = 0; i < table->nb_rows; i++) {
        if (!table->rows[i].active) continue;
        N_GUI_KV_ROW* row = &table->rows[i];
        n_gui_set_widget_visible(table->ctx, row->key_id, 0);
        n_gui_set_widget_visible(table->ctx, row->value_id, 0);
        n_gui_set_widget_visible(table->ctx, row->desc_id, 0);
        n_gui_set_widget_visible(table->ctx, row->enabled_id, 0);
        n_gui_set_widget_visible(table->ctx, row->remove_id, 0);
        n_gui_set_widget_visible(table->ctx, row->up_id, 0);
        n_gui_set_widget_visible(table->ctx, row->down_id, 0);
        n_gui_set_widget_visible(table->ctx, row->dup_id, 0);
        row->active = 0;
    }
    table->nb_active = 0;
    /* nb_rows is kept: the freed slots (and their widgets) are reused by
       n_gui_kvtable_add_row so a reload does not grow the widget count. */
    _n_gui_kv_apply_layout(table);
}

void n_gui_kvtable_set_columns(N_GUI_KVTABLE* table, int show_value, int show_desc, int show_enabled) {
    if (!table) return;
    table->show_value = show_value ? 1 : 0;
    table->show_desc = show_desc ? 1 : 0;
    table->show_enabled = show_enabled ? 1 : 0;
    _n_gui_kv_apply_layout(table);
}

/**
 * @brief Show or hide the per-row move-up / move-down / duplicate buttons.
 *
 * Off by default, so existing tables are unchanged. Turn it on for tables whose
 * row ORDER carries meaning (query parameters, request headers) or where copying
 * a row is a common edit. The buttons exist on every row from creation and are
 * only revealed here, so a table can gain or lose them at any time without
 * rebuilding its rows. The text columns reflow to make room, and the remove
 * button switches to a cross glyph so the trailing cluster reads as one set of
 * icons.
 *
 * @param table the KV table
 * @param enabled 1 to show the actions, 0 to hide them
 */
void n_gui_kvtable_set_row_actions(N_GUI_KVTABLE* table, int enabled) {
    if (!table) return;
    table->show_actions = enabled ? 1 : 0;
    for (int i = 0; i < table->nb_rows; i++) {
        const N_GUI_KV_ROW* row = &table->rows[i];
        n_gui_button_set_glyph(table->ctx, row->remove_id,
                               table->show_actions ? N_GUI_GLYPH_CROSS : N_GUI_GLYPH_NONE);
        n_gui_set_widget_tooltip(table->ctx, row->remove_id,
                                 table->show_actions ? "Remove" : "");
    }
    n_gui_button_set_glyph(table->ctx, table->btn_add,
                           table->show_actions ? N_GUI_GLYPH_PLUS : N_GUI_GLYPH_NONE);
    n_gui_set_widget_tooltip(table->ctx, table->btn_add,
                             table->show_actions ? "Add row" : "");
    _n_gui_kv_apply_layout(table);
    if (table->show_actions) _n_gui_kv_refresh_actions(table);
}

/**
 * @brief Move a row one position up or down among the active rows.
 *
 * Row slots are stable (widgets are bound to them and freed slots are reused),
 * so the CONTENTS are swapped with the neighbouring active row rather than the
 * slots themselves. Everything that reads the table by walking the rows in slot
 * order therefore sees the new order with no extra bookkeeping.
 *
 * @param table the KV table
 * @param row_index the row to move
 * @param direction negative to move up, positive to move down
 * @return the row index the contents landed on, or -1 when the row is invalid,
 *         inactive, or already at that end of the table
 */
int n_gui_kvtable_move_row(N_GUI_KVTABLE* table, int row_index, int direction) {
    int order[N_GUI_KV_MAX];
    int n, pos = -1, target;
    N_GUI_KV_CONTENT a, b;
    if (!table || row_index < 0 || row_index >= table->nb_rows) return -1;
    if (!table->rows[row_index].active || direction == 0) return -1;

    n = _n_gui_kv_active_order(table, order);
    for (int p = 0; p < n; p++) {
        if (order[p] == row_index) {
            pos = p;
            break;
        }
    }
    if (pos < 0) return -1;
    target = (direction < 0) ? pos - 1 : pos + 1;
    if (target < 0 || target >= n) return -1;

    _n_gui_kv_content_grab(table, order[pos], &a);
    _n_gui_kv_content_grab(table, order[target], &b);
    _n_gui_kv_content_put(table, order[pos], &b);
    _n_gui_kv_content_put(table, order[target], &a);
    _n_gui_kv_content_free(&a);
    _n_gui_kv_content_free(&b);

    _n_gui_kv_refresh_actions(table);
    if (table->ctx) table->ctx->dirty = 1;
    return order[target];
}

/**
 * @brief Insert a copy of a row directly after it.
 *
 * A new row is appended (reusing a freed slot when there is one, which may sit
 * anywhere in the array), then every active row's content is rewritten in the
 * wanted order. Going through detached copies of the text keeps the rewrite
 * correct whatever slot the new row landed in, instead of depending on the
 * freed-slot order.
 *
 * @param table the KV table
 * @param row_index the row to copy
 * @return the row index holding the copy, or -1 when the row is invalid or the
 *         table is full (N_GUI_KV_MAX rows)
 */
int n_gui_kvtable_duplicate_row(N_GUI_KVTABLE* table, int row_index) {
    N_GUI_KV_CONTENT snap[N_GUI_KV_MAX];
    int order[N_GUI_KV_MAX];
    int n, pos = -1, added;
    if (!table || row_index < 0 || row_index >= table->nb_rows) return -1;
    if (!table->rows[row_index].active) return -1;

    n = _n_gui_kv_active_order(table, order);
    for (int p = 0; p < n; p++) {
        if (order[p] == row_index) {
            pos = p;
            break;
        }
    }
    if (pos < 0) return -1;
    for (int p = 0; p < n; p++) _n_gui_kv_content_grab(table, order[p], &snap[p]);

    added = n_gui_kvtable_add_row(table, "", "", "", 1);
    if (added < 0) {
        for (int p = 0; p < n; p++) _n_gui_kv_content_free(&snap[p]);
        return -1;
    }

    /* rewrite every active row: the snapshot with the source repeated at pos+1 */
    {
        int order2[N_GUI_KV_MAX];
        int n2 = _n_gui_kv_active_order(table, order2);
        int copy_slot = -1;
        for (int p = 0; p < n2; p++) {
            int src = (p <= pos) ? p : p - 1; /* pos+1 repeats the source */
            if (src >= n) src = n - 1;
            _n_gui_kv_content_put(table, order2[p], &snap[src]);
            if (p == pos + 1) copy_slot = order2[p];
        }
        for (int p = 0; p < n; p++) _n_gui_kv_content_free(&snap[p]);
        if (table->show_actions) _n_gui_kv_refresh_actions(table);
        if (table->ctx) table->ctx->dirty = 1;
        return copy_slot;
    }
}

void n_gui_kvtable_set_placeholders(N_GUI_KVTABLE* table, const char* key_hint, const char* value_hint) {
    if (!table) return;
    FreeNoLog(table->key_placeholder);
    FreeNoLog(table->value_placeholder);
    table->key_placeholder = key_hint ? strdup(key_hint) : NULL;
    table->value_placeholder = value_hint ? strdup(value_hint) : NULL;
    for (int i = 0; i < table->nb_rows; i++) {
        n_gui_textarea_set_placeholder(table->ctx, table->rows[i].key_id,
                                       table->key_placeholder ? table->key_placeholder : "");
        n_gui_textarea_set_placeholder(table->ctx, table->rows[i].value_id,
                                       table->value_placeholder ? table->value_placeholder : "");
    }
}

void n_gui_kvtable_set_top_offset(N_GUI_KVTABLE* table, float top_offset) {
    if (!table) return;
    table->top_offset = top_offset;
    _n_gui_kv_apply_layout(table);
}

int n_gui_kvtable_get_count(const N_GUI_KVTABLE* table) {
    return table ? table->nb_active : 0;
}

void n_gui_kvtable_relayout(N_GUI_KVTABLE* table) {
    _n_gui_kv_apply_layout(table);
}

void n_gui_kvtable_free(N_GUI_KVTABLE** table) {
    if (!table || !*table) return;
    FreeNoLog((*table)->key_placeholder);
    FreeNoLog((*table)->value_placeholder);
    FreeNoLog(*table);
    *table = NULL;
}

/* SECTION LIST (foldable accordion).
   An app-level helper that owns widget ids and re-stacks them with a running
   cursor plus per-widget visibility, exactly like the kvtable above; the core
   window/widget engine is untouched, so N_GUI_WIN_AUTO_SCROLLBAR keeps working
   from the visible-widget bounding box (a folded section's hidden members do
   not count toward content height). Relayout only mutates existing widgets in
   place (visibility + position), so it is safe to call from a widget callback. */

/* Rebuild a header's label with the ASCII fold glyph for its current state. */
static void _n_gui_sectionlist_set_header_label(N_GUI_SECTIONLIST* sl, int idx) {
    N_GUI_SECTION* s = &sl->sections[idx];
    char label[80];
    snprintf(label, sizeof(label), "%s %s", s->folded ? "+" : "-", s->title);
    n_gui_button_set_label(sl->ctx, s->header_id, label);
}

/* Internal header on_click: flip the matching section's fold state, relayout,
   and notify the app via on_toggle (so it can persist + resize its window). */
static void _n_gui_sectionlist_header_clicked(int widget_id, void* user_data) {
    N_GUI_SECTIONLIST* sl = (N_GUI_SECTIONLIST*)user_data;
    if (!sl) return;
    for (int i = 0; i < sl->nb_sections; i++) {
        if (sl->sections[i].header_id == widget_id) {
            sl->sections[i].folded = !sl->sections[i].folded;
            n_gui_sectionlist_relayout(sl);
            if (sl->on_toggle) sl->on_toggle(i, sl->sections[i].folded, sl->user_data);
            return;
        }
    }
}

N_GUI_SECTIONLIST* n_gui_sectionlist_create(N_GUI_CTX* ctx, int window_id, float x, float y0, float width, float header_h, float gap, void (*on_toggle)(int, int, void*), void* user_data) {
    if (!ctx) return NULL;
    N_GUI_SECTIONLIST* sl = NULL;
    Malloc(sl, N_GUI_SECTIONLIST, 1);
    if (!sl) return NULL;
    memset(sl, 0, sizeof(*sl));
    sl->ctx = ctx;
    sl->window_id = window_id;
    sl->x = x;
    sl->y0 = y0;
    sl->width = width;
    sl->header_h = header_h;
    sl->gap = gap;
    sl->on_toggle = on_toggle;
    sl->user_data = user_data;
    sl->nb_sections = 0;
    return sl;
}

int n_gui_sectionlist_add_section(N_GUI_SECTIONLIST* sl, const char* title, float header_y, int folded) {
    if (!sl || !title || sl->nb_sections >= N_GUI_SECTIONLIST_MAX) return -1;
    int idx = sl->nb_sections;
    N_GUI_SECTION* s = &sl->sections[idx];
    memset(s, 0, sizeof(*s));
    strncpy(s->title, title, sizeof(s->title) - 1);
    s->title[sizeof(s->title) - 1] = '\0';
    s->folded = folded ? 1 : 0;
    s->nb_widgets = 0;
    s->natural_h = 0.0f;
    char label[80];
    snprintf(label, sizeof(label), "%s %s", s->folded ? "+" : "-", s->title);
    s->header_id = n_gui_add_button(sl->ctx, sl->window_id, label, sl->x, header_y, sl->width, sl->header_h, N_GUI_SHAPE_ROUNDED, _n_gui_sectionlist_header_clicked, sl);
    sl->nb_sections++;
    return idx;
}

void n_gui_sectionlist_add_widget(N_GUI_SECTIONLIST* sl, int section_index, int widget_id) {
    if (!sl || section_index < 0 || section_index >= sl->nb_sections) return;
    N_GUI_SECTION* s = &sl->sections[section_index];
    if (s->nb_widgets >= N_GUI_SECTION_WIDGETS_MAX) return;
    const N_GUI_WIDGET* w = n_gui_get_widget(sl->ctx, widget_id);
    const N_GUI_WIDGET* hdr = n_gui_get_widget(sl->ctx, s->header_id);
    if (!w || !hdr) return;
    /* member offsets are captured relative to the section's build-time content
       top; relayout then translates the whole block to the live cursor */
    float content_top0 = hdr->y + sl->header_h + sl->gap;
    float dy = w->y - content_top0;
    s->widget_ids[s->nb_widgets] = widget_id;
    s->widget_dy[s->nb_widgets] = dy;
    s->nb_widgets++;
    float bottom = dy + w->h;
    if (bottom > s->natural_h) s->natural_h = bottom;
}

void n_gui_sectionlist_set_first_inset(N_GUI_SECTIONLIST* sl, float inset) {
    if (!sl) return;
    sl->first_inset = (inset > 0.0f) ? inset : 0.0f;
}

void n_gui_sectionlist_relayout(N_GUI_SECTIONLIST* sl) {
    if (!sl) return;
    const N_GUI_WINDOW* win = n_gui_get_window(sl->ctx, sl->window_id);
    float cur = sl->y0;
    for (int i = 0; i < sl->nb_sections; i++) {
        const N_GUI_SECTION* s = &sl->sections[i];
        N_GUI_WIDGET* hdr = n_gui_get_widget(sl->ctx, s->header_id);
        if (hdr) {
            /* the first header may be inset from the left so a caller button can
             * sit before it on the same row */
            float inset = (i == 0) ? sl->first_inset : 0.0f;
            hdr->x = sl->x + inset;
            hdr->y = cur;
            hdr->w = sl->width - inset;
            if (win) _n_gui_widget_capture_normalized(win, hdr);
        }
        cur += sl->header_h + sl->gap;
        for (int k = 0; k < s->nb_widgets; k++) {
            N_GUI_WIDGET* w = n_gui_get_widget(sl->ctx, s->widget_ids[k]);
            if (!w) continue;
            if (s->folded) {
                n_gui_set_widget_visible(sl->ctx, s->widget_ids[k], 0);
            } else {
                n_gui_set_widget_visible(sl->ctx, s->widget_ids[k], 1);
                w->y = cur + s->widget_dy[k];
                if (win) _n_gui_widget_capture_normalized(win, w);
            }
        }
        if (!s->folded) cur += s->natural_h + sl->gap;
        _n_gui_sectionlist_set_header_label(sl, i);
    }
}

void n_gui_sectionlist_set_folded(N_GUI_SECTIONLIST* sl, int section_index, int folded) {
    if (!sl || section_index < 0 || section_index >= sl->nb_sections) return;
    sl->sections[section_index].folded = folded ? 1 : 0;
    n_gui_sectionlist_relayout(sl);
}

int n_gui_sectionlist_is_folded(const N_GUI_SECTIONLIST* sl, int section_index) {
    if (!sl || section_index < 0 || section_index >= sl->nb_sections) return 0;
    return sl->sections[section_index].folded;
}

float n_gui_sectionlist_content_height(const N_GUI_SECTIONLIST* sl) {
    if (!sl) return 0.0f;
    float cur = sl->y0;
    for (int i = 0; i < sl->nb_sections; i++) {
        cur += sl->header_h + sl->gap;
        if (!sl->sections[i].folded) cur += sl->sections[i].natural_h + sl->gap;
    }
    return cur;
}

int n_gui_sectionlist_get_count(const N_GUI_SECTIONLIST* sl) {
    return sl ? sl->nb_sections : 0;
}

void n_gui_sectionlist_free(N_GUI_SECTIONLIST** sl) {
    if (!sl || !*sl) return;
    FreeNoLog(*sl);
    *sl = NULL;
}
