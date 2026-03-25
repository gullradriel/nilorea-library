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

/**
 *@file n_gui.c
 *@brief GUI system: buttons, sliders, text areas, checkboxes, scrollbars, dropdown menus, windows
 *@author Castagnier Mickael
 *@version 3.0
 *@date 02/03/2026
 */

#include "nilorea/n_gui.h"

#ifdef HAVE_CJSON
#include "cJSON.h"
#endif

/* =========================================================================
 *  INTERNAL HELPERS
 * ========================================================================= */

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
            N_GUI_WIDGET* w = (N_GUI_WIDGET*)wgn->ptr;
            if (w && w->id == ctx->focused_widget_id) return win;
        }
    }
    return NULL;
}

/*! helper: get the bounding box width of text using al_get_text_dimensions.
 *  Drop-in replacement for al_get_text_width with more accurate measurement. */
static float _text_w(ALLEGRO_FONT* font, const char* text) {
    int bbx = 0, bby = 0, bbw = 0, bbh = 0;
    if (font && text) al_get_text_dimensions(font, text, &bbx, &bby, &bbw, &bbh);
    return (float)bbw;
}

/*! helper: effective title bar height (0 for frameless windows) */
static float _win_tbh(N_GUI_WINDOW* win) {
    return (win->flags & N_GUI_WIN_FRAMELESS) ? 0.0f : win->titlebar_h;
}

/*! helper: test if point is inside a rectangle */
static int _point_in_rect(float px, float py, float rx, float ry, float rw, float rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

/*! helper: compute scroll position from mouse coordinate using thumb-aware math.
 *  This is the formula used by the window auto-scrollbar — it maps the mouse
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
static void _destroy_widget(void* ptr) {
    if (!ptr) return;
    N_GUI_WIDGET* w = (N_GUI_WIDGET*)ptr;
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
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)node->ptr;
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
}

/*! helper: ensure items array has room for one more, returns 1 on success */
static int _items_grow(N_GUI_LISTITEM** items, size_t* nb, size_t* cap) {
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
    return wgt;
}

/* =========================================================================
 *  THEME
 * ========================================================================= */

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

/* =========================================================================
 *  STYLE DEFAULTS
 * ========================================================================= */

/**
 * @brief Build sensible default style (all configurable sizes, colours, paddings)
 */
N_GUI_STYLE n_gui_default_style(void) {
    N_GUI_STYLE s;

    /* window chrome */
    s.titlebar_h = 28.0f;
    s.min_win_w = 120.0f;
    s.min_win_h = 60.0f;
    s.title_padding = 8.0f;
    s.title_max_w_reserve = 16.0f;

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

    return s;
}

/* =========================================================================
 *  CONTEXT
 * ========================================================================= */

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
    ctx->focused_widget_id = -1;
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
    return ctx;
}

/**
 * @brief Destroy a GUI context and all its windows/widgets
 * @param ctx pointer to the context pointer (set to NULL)
 */
void n_gui_destroy_ctx(N_GUI_CTX** ctx) {
    __n_assert(ctx && *ctx, return);
    if ((*ctx)->windows) {
        list_destroy(&(*ctx)->windows);
    }
    if ((*ctx)->widgets_by_id) {
        destroy_ht(&(*ctx)->widgets_by_id);
    }
    Free((*ctx));
}

/* =========================================================================
 *  WINDOW MANAGEMENT
 * ========================================================================= */

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
    list_push(ctx->windows, win, _destroy_window);
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
    if (win) win->state &= ~N_GUI_WIN_OPEN;
}

/**
 * @brief Open (show) a window
 */
void n_gui_open_window(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (win) win->state |= N_GUI_WIN_OPEN;
}

/**
 * @brief Minimise a window (show title bar only)
 */
void n_gui_minimize_window(N_GUI_CTX* ctx, int window_id) {
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (win) win->state ^= N_GUI_WIN_MINIMISED;
}

/* Return the group ordinal for a window's z-order:
 *   0 = ALWAYS_BEHIND, 1 = FIXED, 2 = NORMAL, 3 = ALWAYS_ON_TOP.
 * Ordering is ALWAYS_BEHIND -> FIXED -> NORMAL -> ALWAYS_ON_TOP. */
static int _zorder_group(const N_GUI_WINDOW* w) {
    if (!w) return 2; /* treat NULL as NORMAL */
    switch (w->z_order) {
        case N_GUI_ZORDER_ALWAYS_BEHIND:
            return 0;
        case N_GUI_ZORDER_FIXED:
            return 1;
        case N_GUI_ZORDER_ALWAYS_ON_TOP:
            return 3;
        default:
            return 2;
    }
}

/**
 * @brief Sort windows list by z-order: ALWAYS_BEHIND first, then FIXED (by z_value),
 *        then NORMAL, then ALWAYS_ON_TOP. Within each group, preserve relative order.
 *        Uses stable insertion sort since window lists are typically small.
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
 *        Respects z-order constraints: ALWAYS_BEHIND and FIXED windows
 *        are not moved. After raising, the window list is re-sorted.
 */
void n_gui_raise_window(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return);
    LIST_NODE* node = _find_window_node(ctx, window_id);
    if (!node) return;
    N_GUI_WINDOW* win = (N_GUI_WINDOW*)node->ptr;
    /* only NORMAL windows can be freely raised */
    if (win->z_order != N_GUI_ZORDER_NORMAL) return;
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
 * @brief Set window z-order mode and value.
 *        Re-sorts the window list after changing.
 */
void n_gui_window_set_zorder(N_GUI_CTX* ctx, int window_id, int z_mode, int z_value) {
    __n_assert(ctx, return);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return;
    if (z_mode != N_GUI_ZORDER_NORMAL && z_mode != N_GUI_ZORDER_ALWAYS_ON_TOP &&
        z_mode != N_GUI_ZORDER_ALWAYS_BEHIND && z_mode != N_GUI_ZORDER_FIXED) {
        n_log(LOG_ERR, "n_gui_window_set_zorder: unknown z_mode %d for window %d (expected N_GUI_ZORDER_NORMAL, N_GUI_ZORDER_ALWAYS_ON_TOP, N_GUI_ZORDER_ALWAYS_BEHIND, or N_GUI_ZORDER_FIXED), falling back to N_GUI_ZORDER_NORMAL", z_mode, window_id);
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
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
    if (!win) return N_GUI_ZORDER_NORMAL;
    return win->z_order;
}

/**
 * @brief Get window z-value (for N_GUI_ZORDER_FIXED mode).
 */
int n_gui_window_get_zvalue(N_GUI_CTX* ctx, int window_id) {
    __n_assert(ctx, return 0);
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
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
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
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
    N_GUI_WINDOW* win = n_gui_get_window(ctx, window_id);
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
        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)node->ptr;
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
        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)node->ptr;
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
            N_GUI_LABEL_DATA* lb = (N_GUI_LABEL_DATA*)wgt->data;
            if (lb && lb->text[0] && lb->align != N_GUI_ALIGN_JUSTIFIED) {
                ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;
                if (font) {
                    float tw = _text_w(font,lb->text);
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

/* =========================================================================
 *  WIDGET CREATION
 * ========================================================================= */

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
    bd->toggle_mode = 0;
    bd->toggled = 0;
    bd->keycode = 0;
    bd->key_modifiers = 0;
    bd->on_click = on_click;
    bd->user_data = user_data;
    wgt->data = bd;

    list_push(win->widgets, wgt, _destroy_widget);
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
    wgt->data = sd;

    list_push(win->widgets, wgt, _destroy_widget);
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
    td->text[0] = '\0';
    td->text_len = 0;
    td->char_limit = (char_limit > 0 && char_limit < N_GUI_TEXT_MAX) ? char_limit : N_GUI_TEXT_MAX - 1;
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

    list_push(win->widgets, wgt, _destroy_widget);
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

    list_push(win->widgets, wgt, _destroy_widget);
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

    list_push(win->widgets, wgt, _destroy_widget);
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
    wgt->data = ld;

    list_push(win->widgets, wgt, _destroy_widget);
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

    list_push(win->widgets, wgt, _destroy_widget);
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
    cd->item_height = h;
    cd->max_visible = ctx->style.combobox_max_visible;
    cd->on_select = on_select;
    cd->user_data = user_data;
    wgt->data = cd;

    list_push(win->widgets, wgt, _destroy_widget);
    _register_widget(ctx, wgt);
    return wgt->id;
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

    list_push(win->widgets, wgt, _destroy_widget);
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

    list_push(win->widgets, wgt, _destroy_widget);
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

/* =========================================================================
 *  WIDGET ACCESS
 * ========================================================================= */

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
 * @brief Show or hide a widget
 */
void n_gui_set_widget_visible(N_GUI_CTX* ctx, int widget_id, int visible) {
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w) w->visible = visible;
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
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
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
            N_GUI_WIDGET* w = (N_GUI_WIDGET*)wgnode->ptr;
            if (w && w->id == widget_id) {
                if (win->state & N_GUI_WIN_OPEN) {
                    n_gui_raise_window(ctx, win->id);
                }
                return;
            }
        }
    }
}

/* ---- slider helpers ---- */

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

/* ---- textarea helpers ---- */

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

/* ---- checkbox helpers ---- */

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

/* ---- scrollbar helpers ---- */

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

/* ---- listbox helpers ---- */

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
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (w && w->type == N_GUI_TYPE_LISTBOX && w->data) {
        N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
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
    N_GUI_WIDGET* w = n_gui_get_widget(ctx, widget_id);
    if (!w || w->type != N_GUI_TYPE_LISTBOX || !w->data) return 0;
    N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)w->data;
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

/* ---- radiolist helpers ---- */

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

/* ---- combobox helpers ---- */

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

/* ---- image helpers ---- */

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

/* ---- label helpers ---- */

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

/* =========================================================================
 *  DROPDOWN MENU
 * ========================================================================= */

/*! helper: ensure dropmenu entries array has room for one more */
static int _dropmenu_entries_grow(N_GUI_DROPMENU_ENTRY** entries, size_t* nb, size_t* cap) {
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
    dd->max_visible = ctx->style.dropmenu_max_visible;
    dd->item_height = h;
    dd->on_open = on_open;
    dd->on_open_user_data = on_open_user_data;
    wgt->data = dd;

    list_push(win->widgets, wgt, _destroy_widget);
    _register_widget(ctx, wgt);
    return wgt->id;
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

/* =========================================================================
 *  BITMAP SKINNING - setter functions
 * ========================================================================= */

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

/* =========================================================================
 *  DRAWING - individual widget renderers
 * ========================================================================= */

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
    float tw = _text_w(font,text);
    if (tw <= max_w) {
        al_draw_text(font, color, x, y, 0, text);
        return;
    }
    /* find how many chars fit + "..." */
    float ellipsis_w = _text_w(font,"...");
    float avail = max_w - ellipsis_w;
    if (avail < 0) avail = 0;
    size_t len = strlen(text);
    char buf[N_GUI_TEXT_MAX];
    size_t fit = 0;
    for (size_t i = 0; i < len && i < (size_t)(N_GUI_TEXT_MAX - 5); i++) {
        buf[i] = text[i];
        buf[i + 1] = '\0';
        float cw = _text_w(font,buf);
        if (cw > avail) break;
        fit = i + 1;
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
        word_widths[nwords] = _text_w(font,tok);
        nwords++;
        tok = strtok(NULL, " \t");
    }
    if (nwords == 0) return;
    if (nwords == 1) {
        _draw_text_truncated(font, color, x, y, max_w, text);
        return;
    }

    float fh = (float)al_get_font_line_height(font);
    float space_w = _text_w(font," ");
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
static float _textarea_content_height(N_GUI_TEXTAREA_DATA* td, ALLEGRO_FONT* font, float widget_w, float pad) {
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
        float cw = _text_w(font,ch);
        if (cx + cw > widget_w - pad * 2) {
            cx = 0;
            cy += fh;
        }
        cx += cw;
        i += (size_t)clen;
    }
    return cy;
}

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
        word_widths[nwords] = _text_w(font,tmp);
        nwords++;
    }
    if (nwords == 0) return 0;

    float fh = (float)al_get_font_line_height(font);
    float space_w = _text_w(font," ");
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
                        float cw = _text_w(font,ch);
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
        word_widths[nwords] = _text_w(font,tmp);
        nwords++;
    }
    if (nwords == 0) return;

    float fh = (float)al_get_font_line_height(font);
    float space_w = _text_w(font," ");
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
                    sx1 = _text_w(font,tmp2);
                }
                if (shi < we) {
                    char tmp2[N_GUI_TEXT_MAX];
                    int off = shi - ws2;
                    memcpy(tmp2, &text[ws2], (size_t)off);
                    tmp2[off] = '\0';
                    sx2 = _text_w(font,tmp2);
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
    float space_w = _text_w(font," ");

    char buf[N_GUI_TEXT_MAX];
    snprintf(buf, N_GUI_TEXT_MAX, "%s", text);

    float word_widths[256];
    int nwords = 0;
    char* tok = strtok(buf, " \t");
    while (tok && nwords < 256) {
        word_widths[nwords] = _text_w(font,tok);
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
static ALLEGRO_COLOR _bg_for_state(N_GUI_THEME* t, int state) {
    if (state & N_GUI_STATE_ACTIVE) return t->bg_active;
    if (state & N_GUI_STATE_HOVER) return t->bg_hover;
    return t->bg_normal;
}

static ALLEGRO_COLOR _border_for_state(N_GUI_THEME* t, int state) {
    if (state & N_GUI_STATE_ACTIVE) return t->border_active;
    if (state & N_GUI_STATE_HOVER) return t->border_hover;
    return t->border_normal;
}

static ALLEGRO_COLOR _text_for_state(N_GUI_THEME* t, int state) {
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

/*! draw a button widget */
static void _draw_button(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    /* For toggle buttons, use the active visual state when toggled on */
    int draw_state = wgt->state;
    if (bd->toggle_mode && bd->toggled) {
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
        int rounded = (bd->shape == N_GUI_SHAPE_ROUNDED) ? 1 : 0;
        _draw_themed_rect(&wgt->theme, draw_state, ax, ay, wgt->w, wgt->h, rounded);
    }

    if (font && bd->label[0]) {
        ALLEGRO_COLOR tc = _text_for_state(&wgt->theme, draw_state);
        int bbx = 0, bby = 0, bbw = 0, bbh = 0;
        al_get_text_dimensions(font, bd->label, &bbx, &bby, &bbw, &bbh);
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
        /* ---- vertical slider ---- */
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

        /* value label */
        if (font) {
            char val_str[32];
            if (sd->mode == N_GUI_SLIDER_PERCENT)
                snprintf(val_str, sizeof(val_str), "%.0f%%", sd->value);
            else
                snprintf(val_str, sizeof(val_str), "%.1f", sd->value);
            int vbbx = 0, vbby = 0, vbbw = 0, vbbh = 0;
            al_get_text_dimensions(font, val_str, &vbbx, &vbby, &vbbw, &vbbh);
            al_draw_text(font, wgt->theme.text_normal, ax + (wgt->w - (float)vbbw) / 2.0f - (float)vbbx, ay + wgt->h + (float)style->slider_value_label_offset, 0, val_str);
        }
        return;
    }

    /* ---- horizontal slider (original) ---- */

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

    /* value label */
    if (font) {
        char val_str[32];
        if (sd->mode == N_GUI_SLIDER_PERCENT) {
            snprintf(val_str, sizeof(val_str), "%.0f%%", sd->value);
        } else {
            snprintf(val_str, sizeof(val_str), "%.1f", sd->value);
        }
        int sbbx = 0, sbby = 0, sbbw = 0, sbbh = 0;
        al_get_text_dimensions(font, val_str, &sbbx, &sbby, &sbbw, &sbbh);
        float th = (float)sbbh;
        al_draw_text(font, wgt->theme.text_normal, ax + wgt->w + style->slider_value_label_offset, ay + (wgt->h - th) / 2.0f - (float)sbby, 0, val_str);
    }
}

/*! helper: find the byte position in a textarea closest to the given mouse
 *  coordinates (mx, my in screen space). ax, ay are the widget's screen-space
 *  top-left corner. Returns the byte offset into td->text. */
static size_t _textarea_pos_from_mouse(N_GUI_TEXTAREA_DATA* td, ALLEGRO_FONT* font, float mx, float my, float ax, float ay, float widget_w, float widget_h, float pad, float scrollbar_size) {
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
            float tw = _text_w(font,ctmp);
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
        float cw = _text_w(font,ch2);
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
static int _textarea_has_selection(N_GUI_TEXTAREA_DATA* td) {
    return td->sel_start != td->sel_end;
}

/*! get the ordered selection range (lo, hi) */
static void _textarea_sel_range(N_GUI_TEXTAREA_DATA* td, size_t* lo, size_t* hi) {
    if (td->sel_start <= td->sel_end) {
        *lo = td->sel_start;
        *hi = td->sel_end;
    } else {
        *lo = td->sel_end;
        *hi = td->sel_start;
    }
}

/*! draw a textarea widget */
static void _draw_textarea(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_TEXTAREA_DATA* td = (N_GUI_TEXTAREA_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    int focused = (wgt->state & N_GUI_STATE_FOCUSED) ? 1 : 0;

    /* background */
    if (td->bg_bitmap) {
        al_draw_scaled_bitmap(td->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(td->bg_bitmap), (float)al_get_bitmap_height(td->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        ALLEGRO_COLOR bg = wgt->theme.bg_normal;
        al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, bg);
    }
    ALLEGRO_COLOR bd = focused ? wgt->theme.border_active : wgt->theme.border_normal;
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
        float content_h = _textarea_content_height(td, font, wgt->w, pad);
        int need_scrollbar = (content_h > view_h) ? 1 : 0;
        float sb_size = need_scrollbar ? style->scrollbar_size : 0;
        float text_area_w = wgt->w - sb_size;

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
                float cw2 = _text_w(font,ch);
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
        for (size_t i = 0; i < td->text_len;) {
            if (i == td->cursor_pos) {
                cursor_cx = cx;
                cursor_cy = cy;
            }
            if (td->text[i] == '\n') {
                /* draw selection highlight on newline (small rect at end of line) */
                if (has_sel && i >= sel_lo && i < sel_hi && cy + fh > ay && cy < ay + wgt->h) {
                    float space_w = _text_w(font," ");
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
            float cw = _text_w(font,ch);
            if (cx + cw > ax + text_area_w - pad) {
                cx = ax + pad;
                cy += fh;
                if (i == td->cursor_pos) {
                    cursor_cx = cx;
                    cursor_cy = cy;
                }
            }
            if (cy + fh > ay && cy < ay + wgt->h) {
                /* draw selection highlight behind character */
                if (has_sel && i >= sel_lo && i < sel_hi) {
                    al_draw_filled_rectangle(cx, cy, cx + cw, cy + fh, sel_bg);
                }
                al_draw_text(font, wgt->theme.text_normal, cx, cy, 0, ch);
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
        float cursor_px = _text_w(font,tmp);

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
            float sel_x1 = _text_w(font,stmp);
            memcpy(stmp, display_text, sh);
            stmp[sh] = '\0';
            float sel_x2 = _text_w(font,stmp);
            float sx1 = ax + pad + sel_x1 - td->scroll_x;
            float sx2 = ax + pad + sel_x2 - td->scroll_x;
            al_draw_filled_rectangle(sx1, ty, sx2, ty + fh, wgt->theme.selection_color);
        }
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
        al_get_text_dimensions(font, cd->label, &cbbx, &cbby, &cbbw, &cbbh);
        float fh = (float)cbbh;
        float label_max_w = wgt->w - box_size - style->checkbox_label_gap;
        _draw_text_truncated(font, _text_for_state(&wgt->theme, wgt->state),
                             ax + box_size + style->checkbox_label_offset, ay + (wgt->h - fh) / 2.0f - (float)cbby, label_max_w, cd->label);
    }
}

/*! draw a scrollbar widget */
static void _draw_scrollbar(N_GUI_WIDGET* wgt, float ox, float oy, N_GUI_STYLE* style) {
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
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, wgt->theme.border_thickness);

    if (!font) return;
    float fh = (float)al_get_font_line_height(font);
    float ih = ld->item_height > fh ? ld->item_height : fh + style->item_height_pad;
    int visible_count = (int)(wgt->h / ih);
    float pad = style->item_text_padding;

    /* clamp scroll_offset to valid range */
    int max_off = (int)ld->nb_items - visible_count;
    if (max_off < 0) max_off = 0;
    if (ld->scroll_offset > max_off) ld->scroll_offset = max_off;
    if (ld->scroll_offset < 0) ld->scroll_offset = 0;

    int need_scrollbar = ((int)ld->nb_items > visible_count) ? 1 : 0;
    float sb_w = need_scrollbar ? style->scrollbar_size : 0;
    float item_area_w = wgt->w - sb_w;

    for (int i = 0; i < visible_count && (size_t)(i + ld->scroll_offset) < ld->nb_items; i++) {
        int idx = i + ld->scroll_offset;
        float iy = ay + (float)i * ih;
        N_GUI_LISTITEM* item = &ld->items[idx];

        if (item->selected && ld->item_selected_bitmap) {
            al_draw_scaled_bitmap(ld->item_selected_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(ld->item_selected_bitmap), (float)al_get_bitmap_height(ld->item_selected_bitmap),
                                  ax + style->item_selection_inset, iy, item_area_w - style->item_selection_inset * 2, ih, 0);
        } else if (item->selected) {
            al_draw_filled_rectangle(ax + style->item_selection_inset, iy, ax + item_area_w - style->item_selection_inset, iy + ih, wgt->theme.bg_active);
        } else if (ld->item_bg_bitmap) {
            al_draw_scaled_bitmap(ld->item_bg_bitmap, 0, 0,
                                  (float)al_get_bitmap_width(ld->item_bg_bitmap), (float)al_get_bitmap_height(ld->item_bg_bitmap),
                                  ax + style->item_selection_inset, iy, item_area_w - style->item_selection_inset * 2, ih, 0);
        }
        float item_max_w = item_area_w - pad * 2.0f;
        _draw_text_truncated(font, item->selected ? wgt->theme.text_active : wgt->theme.text_normal,
                             ax + pad, iy + (ih - fh) / 2.0f, item_max_w, item->text);
    }

    /* draw scrollbar indicator when items overflow */
    if (need_scrollbar) {
        float sb_x = ax + wgt->w - sb_w;
        /* scrollbar track */
        al_draw_filled_rectangle(sb_x, ay, ax + wgt->w, ay + wgt->h, wgt->theme.bg_normal);
        al_draw_line(sb_x, ay, sb_x, ay + wgt->h, wgt->theme.border_normal, 1.0f);
        /* thumb */
        float ratio = (float)visible_count / (float)ld->nb_items;
        float thumb_h = ratio * wgt->h;
        if (thumb_h < style->scrollbar_thumb_min) thumb_h = style->scrollbar_thumb_min;
        float track_range = wgt->h - thumb_h;
        float pos_ratio = (max_off > 0) ? (float)ld->scroll_offset / (float)max_off : 0;
        float thumb_y = ay + pos_ratio * track_range;
        float thumb_pad = style->scrollbar_thumb_padding;
        al_draw_filled_rounded_rectangle(sb_x + thumb_pad, thumb_y, ax + wgt->w - thumb_pad, thumb_y + thumb_h,
                                         2.0f, 2.0f, wgt->theme.bg_hover);
    }
}

/*! draw a radiolist widget */
static void _draw_radiolist(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    /* background */
    if (rd->bg_bitmap) {
        al_draw_scaled_bitmap(rd->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(rd->bg_bitmap), (float)al_get_bitmap_height(rd->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.bg_normal);
    }
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + wgt->h, wgt->theme.border_normal, wgt->theme.border_thickness);

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

        /* label */
        al_draw_text(font, wgt->theme.text_normal,
                     cx + radio_r + pad, iy + (ih - fh) / 2.0f, 0, rd->items[idx].text);
    }

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
static void _draw_combobox(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    if (cd->bg_bitmap) {
        al_draw_scaled_bitmap(cd->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(cd->bg_bitmap), (float)al_get_bitmap_height(cd->bg_bitmap),
                              ax, ay, wgt->w, wgt->h, 0);
    } else {
        int rounded = 0;
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
    al_get_text_dimensions(font, display_text[0] ? display_text : "Ay", &cbbbx, &cbbby, &cbbbw, &cbbbh);
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
static void _draw_combobox_dropdown(N_GUI_CTX* ctx) {
    if (ctx->open_combobox_id < 0) return;
    N_GUI_WIDGET* wgt = n_gui_get_widget(ctx, ctx->open_combobox_id);
    if (!wgt || !wgt->data) return;
    N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)wgt->data;
    if (!cd->is_open || cd->nb_items == 0) return;

    /* find the window containing this widget to get absolute position */
    float ox = 0, oy = 0;
    if (!_find_widget_window(ctx, wgt->id, &ox, &oy)) return;

    ALLEGRO_FONT* font = wgt->font ? wgt->font : ctx->default_font;
    if (!font) return;

    float ax = ox + wgt->x;
    float ay = oy + wgt->y + wgt->h;
    float fh = (float)al_get_font_line_height(font);
    float ih = cd->item_height > fh ? cd->item_height : fh + ctx->style.item_height_pad;
    int vis = (int)cd->nb_items;
    if (vis > cd->max_visible) vis = cd->max_visible;
    float dropdown_h = (float)vis * ih;
    float pad = ctx->style.item_text_padding;

    /* scrollbar geometry — only shown when items exceed max_visible */
    int need_scrollbar = ((int)cd->nb_items > cd->max_visible);
    float sb_size = ctx->style.scrollbar_size;
    if (sb_size < 10.0f) sb_size = 10.0f;
    float item_area_w = need_scrollbar ? wgt->w - sb_size : wgt->w;

    /* dropdown background */
    if (cd->bg_bitmap) {
        al_draw_scaled_bitmap(cd->bg_bitmap, 0, 0,
                              (float)al_get_bitmap_width(cd->bg_bitmap), (float)al_get_bitmap_height(cd->bg_bitmap),
                              ax, ay, wgt->w, dropdown_h, 0);
    } else {
        al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + dropdown_h, wgt->theme.bg_normal);
    }
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + dropdown_h, wgt->theme.border_active, _min_thickness(ctx->style.dropdown_border_thickness));

    /* items */
    for (int i = 0; i < vis && (size_t)(i + cd->scroll_offset) < cd->nb_items; i++) {
        int idx = i + cd->scroll_offset;
        float iy = ay + (float)i * ih;
        int hovered = _point_in_rect((float)ctx->mouse_x, (float)ctx->mouse_y,
                                     ax, iy, item_area_w, ih);

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

        ALLEGRO_COLOR tc = (idx == cd->selected_index) ? wgt->theme.text_active : wgt->theme.text_normal;
        al_draw_text(font, tc, ax + pad, iy + (ih - fh) / 2.0f, 0, cd->items[idx].text);
    }

    /* scrollbar */
    if (need_scrollbar) {
        float sb_x = ax + wgt->w - sb_size;
        /* track */
        al_draw_filled_rectangle(sb_x, ay, sb_x + sb_size, ay + dropdown_h,
                                 ctx->style.scrollbar_track_color);
        /* thumb — proportional to visible / total, positioned by scroll_offset */
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
}

/*! draw a dropdown menu button (closed state) */
static void _draw_dropmenu(N_GUI_WIDGET* wgt, float ox, float oy, ALLEGRO_FONT* default_font, N_GUI_STYLE* style) {
    N_GUI_DROPMENU_DATA* dd = (N_GUI_DROPMENU_DATA*)wgt->data;
    float ax = ox + wgt->x;
    float ay = oy + wgt->y;
    ALLEGRO_FONT* font = wgt->font ? wgt->font : default_font;

    int draw_state = wgt->state;
    if (dd->is_open) draw_state |= N_GUI_STATE_ACTIVE;

    _draw_themed_rect(&wgt->theme, draw_state, ax, ay, wgt->w, wgt->h, 0);

    if (font && dd->label[0]) {
        ALLEGRO_COLOR tc = _text_for_state(&wgt->theme, draw_state);
        int dbbx = 0, dbby = 0, dbbw = 0, dbbh = 0;
        al_get_text_dimensions(font, dd->label, &dbbx, &dbby, &dbbw, &dbbh);
        float fh = (float)dbbh;
        float pad = style->item_text_padding;
        float max_text_w = wgt->w - pad - style->dropdown_arrow_reserve;
        _draw_text_truncated(font, tc, ax + pad, ay + (wgt->h - fh) / 2.0f - (float)dbby, max_text_w, dd->label);
    }

    /* dropdown arrow */
    float arrow_x = ax + wgt->w - style->dropdown_arrow_reserve;
    float arrow_y = ay + wgt->h / 2.0f;
    ALLEGRO_COLOR ac = wgt->theme.text_normal;
    al_draw_line(arrow_x, arrow_y - style->dropdown_arrow_half_h, arrow_x + style->dropdown_arrow_half_w, arrow_y + style->dropdown_arrow_half_h, ac, _min_thickness(style->dropdown_arrow_thickness));
    al_draw_line(arrow_x + style->dropdown_arrow_half_w, arrow_y + style->dropdown_arrow_half_h, arrow_x + style->dropdown_arrow_half_w * 2, arrow_y - style->dropdown_arrow_half_h, ac, _min_thickness(style->dropdown_arrow_thickness));
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
    if (!_find_widget_window(ctx, wgt->id, &ox, &oy)) return;

    ALLEGRO_FONT* font = wgt->font ? wgt->font : ctx->default_font;
    if (!font) return;

    float ax = ox + wgt->x;
    float ay = oy + wgt->y + wgt->h;
    float fh = (float)al_get_font_line_height(font);
    float ih = dd->item_height > fh ? dd->item_height : fh + ctx->style.item_height_pad;
    int vis = (int)dd->nb_entries;
    if (vis > dd->max_visible) vis = dd->max_visible;
    float panel_h = (float)vis * ih;
    float pad = ctx->style.item_text_padding;

    /* scrollbar geometry — only shown when entries exceed max_visible */
    int need_scrollbar = ((int)dd->nb_entries > dd->max_visible);
    float sb_size = ctx->style.scrollbar_size;
    if (sb_size < 10.0f) sb_size = 10.0f;
    float item_area_w = need_scrollbar ? wgt->w - sb_size : wgt->w;

    /* panel background */
    if (dd->panel_bitmap) {
        al_draw_scaled_bitmap(dd->panel_bitmap, 0, 0,
                              (float)al_get_bitmap_width(dd->panel_bitmap), (float)al_get_bitmap_height(dd->panel_bitmap),
                              ax, ay, wgt->w, panel_h, 0);
    } else {
        al_draw_filled_rectangle(ax, ay, ax + wgt->w, ay + panel_h, wgt->theme.bg_normal);
    }
    al_draw_rectangle(ax, ay, ax + wgt->w, ay + panel_h, wgt->theme.border_active, _min_thickness(ctx->style.dropdown_border_thickness));

    /* entries */
    float max_text_w = item_area_w - pad * 2.0f;
    for (int i = 0; i < vis && (size_t)(i + dd->scroll_offset) < dd->nb_entries; i++) {
        int idx = i + dd->scroll_offset;
        float iy = ay + (float)i * ih;
        int hovered = _point_in_rect((float)ctx->mouse_x, (float)ctx->mouse_y,
                                     ax, iy, item_area_w, ih);

        if (hovered) {
            if (dd->item_hover_bitmap) {
                al_draw_scaled_bitmap(dd->item_hover_bitmap, 0, 0,
                                      (float)al_get_bitmap_width(dd->item_hover_bitmap), (float)al_get_bitmap_height(dd->item_hover_bitmap),
                                      ax + ctx->style.item_selection_inset, iy, item_area_w - ctx->style.item_selection_inset * 2, ih, 0);
            } else {
                al_draw_filled_rectangle(ax + ctx->style.item_selection_inset, iy, ax + item_area_w - ctx->style.item_selection_inset, iy + ih, wgt->theme.bg_hover);
            }
        }

        ALLEGRO_COLOR tc = hovered ? wgt->theme.text_hover : wgt->theme.text_normal;
        _draw_text_truncated(font, tc, ax + pad, iy + (ih - fh) / 2.0f, max_text_w, dd->entries[idx].text);
    }

    /* scrollbar */
    if (need_scrollbar) {
        float sb_x = ax + wgt->w - sb_size;
        /* track */
        al_draw_filled_rectangle(sb_x, ay, sb_x + sb_size, ay + panel_h,
                                 ctx->style.scrollbar_track_color);
        /* thumb — proportional to visible / total, positioned by scroll_offset */
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
static int _label_char_at_x(N_GUI_LABEL_DATA* lb, ALLEGRO_FONT* font, float click_x) {
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
        float tw = _text_w(font,tmp);
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
static float _label_text_origin_x(N_GUI_LABEL_DATA* lb, ALLEGRO_FONT* font, float ax, float wgt_w, float win_w, float wgt_x, float label_padding) {
    float effective_w = wgt_w;
    if (win_w > 0) {
        float max_from_win = win_w - wgt_x;
        if (max_from_win > effective_w) effective_w = max_from_win;
    }
    int lbbx = 0, lbby = 0, lbbw = 0, lbbh = 0;
    al_get_text_dimensions(font, lb->text, &lbbx, &lbby, &lbbw, &lbbh);
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
    al_get_text_dimensions(font, lb->text, &lbbbx, &lbbby, &lbbbw, &lbbbh);
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
        float sx1 = _text_w(font,stmp);
        memcpy(stmp, lb->text, (size_t)shi);
        stmp[shi] = '\0';
        float sx2 = _text_w(font,stmp);
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
            al_get_text_dimensions(font, win->title, &tbbx, &tbby, &tbbw, &tbbh);
            float fh = (float)tbbh;
            float max_title_w = win->w - style->title_max_w_reserve;
            _draw_text_truncated(font, win->theme.text_active,
                                 win->x + style->title_padding, win->y + (tbh - fh) / 2.0f - (float)tbby, max_title_w, win->title);
        }
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

        /* body background */
        if (win->bg_bitmap) {
            _draw_bitmap_scaled(win->bg_bitmap, win->x, body_y, win->w, body_h, win->bg_scale_mode);
        } else {
            al_draw_filled_rectangle(win->x, body_y, win->x + win->w, win->y + win->h,
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
        _set_clipping_rect_transformed((int)win->x + (int)style->scrollbar_thumb_padding, (int)body_y,
                                       (int)content_area_w - (int)style->scrollbar_thumb_padding, (int)content_area_h);

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
        N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
        if (!(win->state & N_GUI_WIN_OPEN)) continue;
        float r = win->x + win->w;
        float b = win->y + win->h;
        if (r > max_r) max_r = r;
        if (b > max_b) max_b = b;
    }
    ctx->gui_bounds_w = max_r;
    ctx->gui_bounds_h = max_b;
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
        N_GUI_WINDOW* awin = (N_GUI_WINDOW*)anode->ptr;
        if (awin && awin->autofit_flags) {
            n_gui_window_apply_autofit(ctx, awin->id);
        }
    }

    list_foreach(node, ctx->windows) {
        _draw_window((N_GUI_WINDOW*)node->ptr, ctx->default_font, &ctx->style);
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

    al_use_transform(&prev_tf);
}

/* =========================================================================
 *  VIRTUAL CANVAS / DISPLAY / DPI
 * ========================================================================= */

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
void n_gui_screen_to_virtual(N_GUI_CTX* ctx, float sx, float sy, float* vx, float* vy) {
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

/**
 * @brief Set the display (viewport) size for global scrollbar computation.
 * Call on startup and whenever ALLEGRO_EVENT_DISPLAY_RESIZE is received.
 * When virtual canvas is enabled, also recalculates the virtual-to-display transform.
 */
void n_gui_set_display_size(N_GUI_CTX* ctx, float w, float h) {
    __n_assert(ctx, return);
    ctx->display_w = w;
    ctx->display_h = h;
    if (ctx->virtual_w > 0 && ctx->virtual_h > 0) {
        n_gui_update_transform(ctx);
    }
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
float n_gui_get_dpi_scale(N_GUI_CTX* ctx) {
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

/* =========================================================================
 *  EVENT PROCESSING
 * ========================================================================= */

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
static void _scrollbar_update_from_mouse(N_GUI_WIDGET* wgt, float mx, float my, float win_x, float win_content_y, N_GUI_STYLE* style) {
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
static int _textarea_copy_to_clipboard(N_GUI_TEXTAREA_DATA* td, ALLEGRO_DISPLAY* display) {
    if (!display || !_textarea_has_selection(td)) return 0;
    size_t lo, hi;
    _textarea_sel_range(td, &lo, &hi);
    char tmp[N_GUI_TEXT_MAX];
    size_t len = hi - lo;
    if (len >= N_GUI_TEXT_MAX) len = N_GUI_TEXT_MAX - 1;
    memcpy(tmp, &td->text[lo], len);
    tmp[len] = '\0';
    al_set_clipboard_text(display, tmp);
    return 1;
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
        return 1;
    }

    /* Ctrl+C: copy */
    if (ctrl && ev->keyboard.keycode == ALLEGRO_KEY_C) {
        if (ctx && ctx->display) {
            _textarea_copy_to_clipboard(td, ctx->display);
        }
        return 1;
    }

    /* Ctrl+X: cut */
    if (ctrl && ev->keyboard.keycode == ALLEGRO_KEY_X) {
        if (ctx && ctx->display && _textarea_has_selection(td)) {
            _textarea_copy_to_clipboard(td, ctx->display);
            _textarea_delete_selection(wgt);
        }
        return 1;
    }

    /* Ctrl+V: paste */
    if (ctrl && ev->keyboard.keycode == ALLEGRO_KEY_V) {
        if (ctx && ctx->display) {
            char* clip = al_get_clipboard_text(ctx->display);
            if (clip) {
                /* delete selection first if any */
                if (_textarea_has_selection(td)) {
                    _textarea_delete_selection(wgt);
                }
                size_t clip_len = strlen(clip);
                /* filter out non-printable except newline in multiline mode */
                char filtered[N_GUI_TEXT_MAX];
                size_t flen = 0;
                for (size_t ci = 0; ci < clip_len && flen < N_GUI_TEXT_MAX - 1; ci++) {
                    if (clip[ci] == '\n') {
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
            float cw = _text_w(font,ch);
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
                    size_t pos = i + 1;
                    float dist = (cursor_x >= cx) ? (cursor_x - cx) : (cx - cursor_x);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_pos = pos;
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
            float cw = _text_w(font,ch);
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

    /* When the display loses focus (e.g. OS window is moved to another monitor via the
     * title bar, or another window steals focus), examples typically call
     * al_flush_event_queue() which discards any pending MOUSE_BUTTON_UP events.
     * Without resetting our state here, the GUI would keep its drag/resize flags set
     * and continue moving/resizing windows on the next MOUSE_AXES event, which is the
     * root cause of GUI windows being unintentionally resized when the OS window is
     * moved between monitors. */
    if (event.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT) {
        ctx->mouse_b1 = 0;
        ctx->mouse_b1_prev = 0;
        list_foreach(wnode, ctx->windows) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            win->state &= ~(N_GUI_WIN_DRAGGING | N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG);
        }
        ctx->global_vscroll_drag = 0;
        ctx->global_hscroll_drag = 0;
        return 0;
    }

    /* track mouse */
    if (event.type == ALLEGRO_EVENT_MOUSE_AXES) {
        ctx->mouse_x = event.mouse.x;
        ctx->mouse_y = event.mouse.y;
    }
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN && event.mouse.button == 1) {
        ctx->mouse_b1_prev = ctx->mouse_b1;
        ctx->mouse_b1 = 1;
        ctx->mouse_x = event.mouse.x;
        ctx->mouse_y = event.mouse.y;
    }
    if (event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP && event.mouse.button == 1) {
        ctx->mouse_b1_prev = ctx->mouse_b1;
        ctx->mouse_b1 = 0;
        ctx->scrollbar_drag_widget_id = -1;
    }

    /* Unified widget scrollbar drag: update scroll position while mouse held */
    if (ctx->scrollbar_drag_widget_id >= 0 && ctx->mouse_b1 &&
        (event.type == ALLEGRO_EVENT_MOUSE_AXES || event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN)) {
        N_GUI_WIDGET* drag_wgt = n_gui_get_widget(ctx, ctx->scrollbar_drag_widget_id);
        if (drag_wgt && drag_wgt->data) {
            float d_ox = 0, d_oy = 0;
            N_GUI_WINDOW* d_win = _find_widget_window(ctx, drag_wgt->id, &d_ox, &d_oy);
            if (d_win) {
                ALLEGRO_FONT* d_font = drag_wgt->font ? drag_wgt->font : ctx->default_font;
                float d_fh = d_font ? (float)al_get_font_line_height(d_font) : 16.0f;
                float d_my = (float)ctx->mouse_y;
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
                        float ih = lbd->item_height > d_fh ? lbd->item_height : d_fh + ctx->style.item_height_pad;
                        float d_ay = d_oy + drag_wgt->y;
                        int visible = (int)(drag_wgt->h / ih);
                        lbd->scroll_offset = _scrollbar_calc_scroll_int(d_my, d_ay, drag_wgt->h, visible, (int)lbd->nb_items, ctx->style.scrollbar_thumb_min);
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
    /* virtual-space mouse: reverse the virtual canvas transform first */
    float virtual_mx = screen_mx;
    float virtual_my = screen_my;
    if (ctx->virtual_w > 0 && ctx->virtual_h > 0 && ctx->gui_scale > 0) {
        virtual_mx = (screen_mx - ctx->gui_offset_x) / ctx->gui_scale;
        virtual_my = (screen_my - ctx->gui_offset_y) / ctx->gui_scale;
    }
    /* GUI-space mouse (offset by global scroll) */
    float mx = virtual_mx + ctx->global_scroll_x;
    float my = virtual_my + ctx->global_scroll_y;
    int just_pressed = (ctx->mouse_b1 == 1 && ctx->mouse_b1_prev == 0 &&
                        event.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN)
                           ? 1
                           : 0;
    int just_released = (ctx->mouse_b1 == 0 && ctx->mouse_b1_prev == 1 &&
                         event.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP)
                            ? 1
                            : 0;

    /* effective dimensions: gate on both dimensions for virtual canvas */
    int virtual_active = (ctx->virtual_w > 0 && ctx->virtual_h > 0);
    float eff_w = virtual_active ? ctx->virtual_w : ctx->display_w;
    float eff_h = virtual_active ? ctx->virtual_h : ctx->display_h;

    /* ---- handle global scrollbar drag ---- */
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

    /* ---- check global scrollbar click ---- */
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

    /* ---- handle open combobox dropdown first (it overlays everything) ---- */
    if (just_pressed && ctx->open_combobox_id >= 0) {
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
            float cb_ay = cb_oy + cb_wgt->y + cb_wgt->h;
            int cb_vis = (int)cbd->nb_items;
            if (cb_vis > cbd->max_visible) cb_vis = cbd->max_visible;
            float cb_dd_h = (float)cb_vis * cb_ih;

            if (_point_in_rect(mx, my, cb_ax, cb_ay, cb_wgt->w, cb_dd_h)) {
                /* Check if click is on the scrollbar area (right edge) */
                int cb_need_sb = ((int)cbd->nb_items > cbd->max_visible);
                float cb_sb_size = ctx->style.scrollbar_size;
                if (cb_sb_size < 10.0f) cb_sb_size = 10.0f;
                float cb_item_w = cb_need_sb ? cb_wgt->w - cb_sb_size : cb_wgt->w;

                if (cb_need_sb && mx >= cb_ax + cb_item_w) {
                    /* Clicked on scrollbar — scroll to position, start drag */
                    cbd->scroll_offset = _scrollbar_calc_scroll_int(my, cb_ay, cb_dd_h, cbd->max_visible, (int)cbd->nb_items, ctx->style.scrollbar_thumb_min);
                    ctx->scrollbar_drag_widget_id = cb_wgt->id;
                    return 1; /* consumed, keep dropdown open */
                }

                /* Clicked on item area — select item */
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

    /* ---- handle open dropdown menu panel first ---- */
    if (just_pressed && ctx->open_dropmenu_id >= 0) {
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
            float dm_ay = dm_oy + dm_wgt->y + dm_wgt->h;
            int dm_vis = (int)dmd->nb_entries;
            if (dm_vis > dmd->max_visible) dm_vis = dmd->max_visible;
            float dm_panel_h = (float)dm_vis * dm_ih;

            if (_point_in_rect(mx, my, dm_ax, dm_ay, dm_wgt->w, dm_panel_h)) {
                /* check if click is on the scrollbar area (right edge) */
                int dm_need_sb = ((int)dmd->nb_entries > dmd->max_visible);
                float dm_sb_size = ctx->style.scrollbar_size;
                if (dm_sb_size < 10.0f) dm_sb_size = 10.0f;
                float dm_item_w = dm_need_sb ? dm_wgt->w - dm_sb_size : dm_wgt->w;

                if (dm_need_sb && mx >= dm_ax + dm_item_w) {
                    /* Clicked on scrollbar — scroll to position, start drag */
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
        /* reset hover for all widgets */
        list_foreach(wnode, ctx->windows) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
            if (!(win->state & N_GUI_WIN_OPEN)) continue;
            list_foreach(wgn, win->widgets) {
                N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                if (wgt) wgt->state &= ~N_GUI_STATE_HOVER;
            }
        }

        /* check if any window has an active drag/resize/scroll (mouse capture) */
        LIST_NODE* captured_wnode = NULL;
        for (LIST_NODE* wnode = ctx->windows->end; wnode; wnode = wnode->prev) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
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

                /* check resize handle (bottom-right corner, 14x14 px) - skip if auto-scrollbar was hit */
                if (!auto_scrollbar_hit && (win->flags & N_GUI_WIN_RESIZABLE) && !(win->state & N_GUI_WIN_MINIMISED)) {
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

                /* title bar: drag / minimize */
                if (!(win->state & (N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG)) &&
                    _point_in_rect(mx, my, win->x, win->y, win->w, _win_tbh(win))) {
                    if (just_pressed && !(win->flags & N_GUI_WIN_FIXED_POSITION)) {
                        /* double click could minimize - for now, start drag */
                        win->state |= N_GUI_WIN_DRAGGING;
                        win->drag_ox = mx - win->x;
                        win->drag_oy = my - win->y;
                    }
                } else if (!(win->state & (N_GUI_WIN_MINIMISED | N_GUI_WIN_RESIZING | N_GUI_WIN_VSCROLL_DRAG | N_GUI_WIN_HSCROLL_DRAG))) {
                    /* widget area (account for scroll offsets) */
                    float content_x = win->x - win->scroll_x;
                    float content_y = win->y + _win_tbh(win) - win->scroll_y;

                    list_foreach(wgn, win->widgets) {
                        N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                        if (!wgt || !wgt->visible || !wgt->enabled) continue;
                        float ax = content_x + wgt->x;
                        float ay = content_y + wgt->y;

                        if (_point_in_rect(mx, my, ax, ay, wgt->w, wgt->h)) {
                            wgt->state |= N_GUI_STATE_HOVER;

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
                                    N_GUI_TEXTAREA_DATA* click_td = (N_GUI_TEXTAREA_DATA*)wgt->data;
                                    click_td->cursor_time = al_get_time();
                                    ALLEGRO_FONT* tf = wgt->font ? wgt->font : ctx->default_font;
                                    if (tf) {
                                        float text_pad = ctx->style.textarea_padding;
                                        size_t pos = _textarea_pos_from_mouse(click_td, tf, mx, my,
                                                                              ax, ay, wgt->w, wgt->h,
                                                                              text_pad, ctx->style.scrollbar_size);
                                        click_td->cursor_pos = pos;
                                        click_td->sel_start = pos;
                                        click_td->sel_end = pos;
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_LABEL) {
                                    N_GUI_LABEL_DATA* click_lb = (N_GUI_LABEL_DATA*)wgt->data;
                                    if (click_lb) {
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
                                if (wgt->type == N_GUI_TYPE_COMBOBOX) {
                                    N_GUI_COMBOBOX_DATA* cbd = (N_GUI_COMBOBOX_DATA*)wgt->data;
                                    cbd->is_open = !cbd->is_open;
                                    if (cbd->is_open) {
                                        ctx->open_combobox_id = wgt->id;
                                    } else {
                                        ctx->open_combobox_id = -1;
                                    }
                                }
                                if (wgt->type == N_GUI_TYPE_LISTBOX) {
                                    N_GUI_LISTBOX_DATA* lbd = (N_GUI_LISTBOX_DATA*)wgt->data;
                                    ALLEGRO_FONT* lf = wgt->font ? wgt->font : ctx->default_font;
                                    float lfh = lf ? (float)al_get_font_line_height(lf) : 16.0f;
                                    float lih = lbd->item_height > lfh ? lbd->item_height : lfh + ctx->style.item_height_pad;
                                    int lb_visible = (int)(wgt->h / lih);
                                    int lb_need_sb = ((int)lbd->nb_items > lb_visible) ? 1 : 0;
                                    float lb_sb_w = lb_need_sb ? ctx->style.scrollbar_size : 0;
                                    /* clamp scroll_offset */
                                    int lb_max_off = (int)lbd->nb_items - lb_visible;
                                    if (lb_max_off < 0) lb_max_off = 0;
                                    if (lbd->scroll_offset > lb_max_off) lbd->scroll_offset = lb_max_off;
                                    if (lbd->scroll_offset < 0) lbd->scroll_offset = 0;
                                    /* skip click if on the scrollbar area */
                                    if (lb_need_sb && mx > ax + wgt->w - lb_sb_w) {
                                        /* scrollbar track click: jump scroll position + start drag */
                                        lbd->scroll_offset = _scrollbar_calc_scroll_int(my, ay, wgt->h, lb_visible, (int)lbd->nb_items, ctx->style.scrollbar_thumb_min);
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
                                        /* call on_open callback to rebuild dynamic entries */
                                        if (dmd->on_open) {
                                            dmd->on_open(wgt->id, dmd->on_open_user_data);
                                        }
                                    } else {
                                        ctx->open_dropmenu_id = -1;
                                    }
                                }
                            }
                            break; /* only top widget gets the event */
                        }
                    }

                    /* frameless windows: drag from empty body area */
                    if ((win->flags & N_GUI_WIN_FRAMELESS) && just_pressed &&
                        !(win->flags & N_GUI_WIN_FIXED_POSITION) &&
                        !(win->state & N_GUI_WIN_DRAGGING)) {
                        win->state |= N_GUI_WIN_DRAGGING;
                        win->drag_ox = mx - win->x;
                        win->drag_oy = my - win->y;
                    }
                }

            } /* end if (!captured_wnode) */

            /* window dragging */
            if ((win->state & N_GUI_WIN_DRAGGING) && ctx->mouse_b1 && !(win->flags & N_GUI_WIN_FIXED_POSITION)) {
                win->x = mx - win->drag_ox;
                win->y = my - win->drag_oy;
            }
            if (just_released && (win->state & N_GUI_WIN_DRAGGING) && !(win->flags & N_GUI_WIN_FIXED_POSITION)) {
                win->state &= ~N_GUI_WIN_DRAGGING;
            }

            /* window resizing */
            if ((win->state & N_GUI_WIN_RESIZING) && ctx->mouse_b1) {
                float new_w = (mx - win->x) + win->drag_ox;
                float new_h = (my - win->y) + win->drag_oy;
                if (new_w < win->min_w) new_w = win->min_w;
                if (new_h < win->min_h) new_h = win->min_h;
                win->w = new_w;
                win->h = new_h;
            }
            if (just_released && (win->state & N_GUI_WIN_RESIZING)) {
                win->state &= ~N_GUI_WIN_RESIZING;
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
            list_foreach(wnode, ctx->windows) {
                N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
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
                                if (bd->on_click) bd->on_click(wgt->id, bd->user_data);
                            }
                        }
                        /* stop label drag selection on release */
                        if (wgt->type == N_GUI_TYPE_LABEL) {
                            N_GUI_LABEL_DATA* rel_lb = (N_GUI_LABEL_DATA*)wgt->data;
                            if (rel_lb) rel_lb->sel_dragging = 0;
                        }
                        wgt->state &= ~N_GUI_STATE_ACTIVE;
                    }
                }
            }
        }
    }

    /* Tab / Shift+Tab: focus navigation across widgets in the active window.
     * Ctrl+Tab in a textarea inserts a literal tab — handled below by the textarea key handler. */
    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_CHAR && event.keyboard.keycode == ALLEGRO_KEY_TAB) {
        int ctrl = (event.keyboard.modifiers & ALLEGRO_KEYMOD_CTRL) ? 1 : 0;
        int shift = (event.keyboard.modifiers & ALLEGRO_KEYMOD_SHIFT) ? 1 : 0;

        /* Ctrl+Tab in a focused textarea inserts a tab character — skip navigation */
        int is_ctrl_tab_in_textarea = 0;
        if (ctrl && ctx->focused_widget_id >= 0) {
            N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
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
                /* no focus yet — use topmost open window */
                for (LIST_NODE* wn = ctx->windows->end; wn; wn = wn->prev) {
                    N_GUI_WINDOW* w = (N_GUI_WINDOW*)wn->ptr;
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
                    N_GUI_WIDGET* w = (N_GUI_WIDGET*)wgn->ptr;
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
            }
        }
    }

    /* keyboard events -> focused non-textarea widgets (slider, listbox, radiolist, combobox, scrollbar, checkbox) */
    if (!event_consumed && event.type == ALLEGRO_EVENT_KEY_CHAR && ctx->focused_widget_id >= 0) {
        N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
        if (fw && fw->visible && fw->enabled && (fw->state & N_GUI_STATE_FOCUSED) && fw->data) {
            int kc = event.keyboard.keycode;

            /* ---- slider keyboard ---- */
            if (fw->type == N_GUI_TYPE_SLIDER) {
                N_GUI_SLIDER_DATA* sd = (N_GUI_SLIDER_DATA*)fw->data;
                double step = sd->step > 0.0 ? sd->step : (sd->max_val - sd->min_val) / 20.0;
                if (step <= 0) step = 1.0;
                double new_val = sd->value;
                int handled = 0;

                if (sd->orientation == N_GUI_SLIDER_H) {
                    if (kc == ALLEGRO_KEY_RIGHT) { new_val += step; handled = 1; }
                    else if (kc == ALLEGRO_KEY_LEFT) { new_val -= step; handled = 1; }
                } else {
                    if (kc == ALLEGRO_KEY_UP) { new_val += step; handled = 1; }
                    else if (kc == ALLEGRO_KEY_DOWN) { new_val -= step; handled = 1; }
                }
                if (kc == ALLEGRO_KEY_HOME) { new_val = sd->min_val; handled = 1; }
                else if (kc == ALLEGRO_KEY_END) { new_val = sd->max_val; handled = 1; }

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

            /* ---- listbox keyboard ---- */
            if (fw->type == N_GUI_TYPE_LISTBOX) {
                N_GUI_LISTBOX_DATA* ld = (N_GUI_LISTBOX_DATA*)fw->data;
                if (ld->selection_mode != N_GUI_SELECT_NONE && ld->nb_items > 0) {
                    int handled = 0;
                    /* find first selected item for single-select navigation */
                    int cur_sel = -1;
                    if (ld->selection_mode == N_GUI_SELECT_SINGLE) {
                        for (size_t i = 0; i < ld->nb_items; i++) {
                            if (ld->items[i].selected) { cur_sel = (int)i; break; }
                        }
                    }
                    int new_sel = cur_sel;
                    if (kc == ALLEGRO_KEY_UP && cur_sel > 0) { new_sel = cur_sel - 1; handled = 1; }
                    else if (kc == ALLEGRO_KEY_DOWN && cur_sel < (int)ld->nb_items - 1) { new_sel = cur_sel + 1; handled = 1; }
                    else if (kc == ALLEGRO_KEY_DOWN && cur_sel < 0) { new_sel = 0; handled = 1; }
                    else if (kc == ALLEGRO_KEY_HOME) { new_sel = 0; handled = 1; }
                    else if (kc == ALLEGRO_KEY_END) { new_sel = (int)ld->nb_items - 1; handled = 1; }

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

            /* ---- radiolist keyboard ---- */
            if (fw->type == N_GUI_TYPE_RADIOLIST) {
                N_GUI_RADIOLIST_DATA* rd = (N_GUI_RADIOLIST_DATA*)fw->data;
                if (rd->nb_items > 0) {
                    int cur_sel = rd->selected_index;
                    int new_sel = cur_sel;
                    int handled = 0;
                    if (kc == ALLEGRO_KEY_UP && cur_sel > 0) { new_sel = cur_sel - 1; handled = 1; }
                    else if (kc == ALLEGRO_KEY_DOWN && cur_sel < (int)rd->nb_items - 1) { new_sel = cur_sel + 1; handled = 1; }
                    else if (kc == ALLEGRO_KEY_DOWN && cur_sel < 0) { new_sel = 0; handled = 1; }
                    else if (kc == ALLEGRO_KEY_HOME) { new_sel = 0; handled = 1; }
                    else if (kc == ALLEGRO_KEY_END) { new_sel = (int)rd->nb_items - 1; handled = 1; }

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

            /* ---- combobox keyboard (when closed) ---- */
            if (fw->type == N_GUI_TYPE_COMBOBOX) {
                N_GUI_COMBOBOX_DATA* cd = (N_GUI_COMBOBOX_DATA*)fw->data;
                if (!cd->is_open && cd->nb_items > 0) {
                    int cur_sel = cd->selected_index;
                    int new_sel = cur_sel;
                    int handled = 0;
                    if (kc == ALLEGRO_KEY_UP && cur_sel > 0) { new_sel = cur_sel - 1; handled = 1; }
                    else if (kc == ALLEGRO_KEY_DOWN && cur_sel < (int)cd->nb_items - 1) { new_sel = cur_sel + 1; handled = 1; }
                    else if (kc == ALLEGRO_KEY_DOWN && cur_sel < 0) { new_sel = 0; handled = 1; }
                    else if (kc == ALLEGRO_KEY_HOME) { new_sel = 0; handled = 1; }
                    else if (kc == ALLEGRO_KEY_END) { new_sel = (int)cd->nb_items - 1; handled = 1; }

                    if (handled && new_sel != cur_sel) {
                        cd->selected_index = new_sel;
                        if (cd->on_select) cd->on_select(fw->id, new_sel, cd->user_data);
                        event_consumed = 1;
                    }
                }
            }

            /* ---- scrollbar keyboard ---- */
            if (fw->type == N_GUI_TYPE_SCROLLBAR) {
                N_GUI_SCROLLBAR_DATA* sb = (N_GUI_SCROLLBAR_DATA*)fw->data;
                double max_scroll = sb->content_size - sb->viewport_size;
                if (max_scroll > 0) {
                    double step = max_scroll / 20.0;
                    if (step <= 0) step = 1.0;
                    double new_pos = sb->scroll_pos;
                    int handled = 0;
                    if (sb->orientation == N_GUI_SCROLLBAR_V) {
                        if (kc == ALLEGRO_KEY_UP) { new_pos -= step; handled = 1; }
                        else if (kc == ALLEGRO_KEY_DOWN) { new_pos += step; handled = 1; }
                    } else {
                        if (kc == ALLEGRO_KEY_LEFT) { new_pos -= step; handled = 1; }
                        else if (kc == ALLEGRO_KEY_RIGHT) { new_pos += step; handled = 1; }
                    }
                    if (kc == ALLEGRO_KEY_HOME) { new_pos = 0; handled = 1; }
                    else if (kc == ALLEGRO_KEY_END) { new_pos = max_scroll; handled = 1; }

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

            /* ---- checkbox keyboard (Space/Enter to toggle) ---- */
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
        event.keyboard.keycode == ALLEGRO_KEY_C && ctx->display &&
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
                    al_set_clipboard_text(ctx->display, clip);
                    event_consumed = 1;
                }
            }
        }
    }

    /* keyboard events -> button key bindings.
     * Skip when an interactive widget has focus (so navigation keys don't trigger buttons).
     * Exception: for single-line textareas, ENTER passes through so it can
     * trigger button keybinds (e.g. send button in a chat input). */
    if (event.type == ALLEGRO_EVENT_KEY_DOWN && !event_consumed) {
        int widget_focused = 0;
        if (ctx->focused_widget_id >= 0) {
            N_GUI_WIDGET* fw = n_gui_get_widget(ctx, ctx->focused_widget_id);
            if (fw && (fw->state & N_GUI_STATE_FOCUSED)) {
                if (fw->type == N_GUI_TYPE_TEXTAREA) {
                    N_GUI_TEXTAREA_DATA* ftd = (N_GUI_TEXTAREA_DATA*)fw->data;
                    /* for single-line textareas, allow ENTER to pass through */
                    if (ftd && (ftd->multiline || event.keyboard.keycode != ALLEGRO_KEY_ENTER)) {
                        widget_focused = 1;
                    }
                } else if (_is_focusable_type(fw->type)) {
                    widget_focused = 1;
                }
            }
        }
        if (!widget_focused) {
            int kc = event.keyboard.keycode;
            int ev_mods = event.keyboard.modifiers & N_GUI_KEY_MOD_MASK;
            /* Iterate windows from frontmost (end of list) to backmost (start)
             * and stop as soon as a button consumes the event. */
            for (LIST_NODE* wnode = ctx->windows->end; wnode && !event_consumed; wnode = wnode->prev) {
                N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
                if (!(win->state & N_GUI_WIN_OPEN)) {
                    continue;
                }
                list_foreach(wgn, win->widgets) {
                    N_GUI_WIDGET* wgt = (N_GUI_WIDGET*)wgn->ptr;
                    if (!wgt || !wgt->visible || !wgt->enabled || wgt->type != N_GUI_TYPE_BUTTON) {
                        continue;
                    }
                    N_GUI_BUTTON_DATA* bd = (N_GUI_BUTTON_DATA*)wgt->data;
                    if (bd && bd->keycode == kc) {
                        /* check modifier requirements:
                         * key_modifiers == 0 means no requirement (any mods match, backward compat)
                         * key_modifiers != 0 means exact match on the masked modifier bits */
                        if (bd->key_modifiers != 0 && ev_mods != bd->key_modifiers) {
                            continue;
                        }
                        if (bd->toggle_mode) {
                            bd->toggled = !bd->toggled;
                        }
                        if (bd->on_click) {
                            bd->on_click(wgt->id, bd->user_data);
                        }
                        event_consumed = 1;
                        break; /* stop scanning other widgets in this window */
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

        /* Check if an open combobox dropdown should consume the scroll */
        if (ctx->open_combobox_id >= 0) {
            N_GUI_WIDGET* cb_wgt = n_gui_get_widget(ctx, ctx->open_combobox_id);
            if (cb_wgt && cb_wgt->data) {
                N_GUI_COMBOBOX_DATA* cbd = (N_GUI_COMBOBOX_DATA*)cb_wgt->data;
                if (cbd->is_open && (int)cbd->nb_items > cbd->max_visible) {
                    cbd->scroll_offset -= event.mouse.dz;
                    if (cbd->scroll_offset < 0) cbd->scroll_offset = 0;
                    int max_off = (int)cbd->nb_items - cbd->max_visible;
                    if (cbd->scroll_offset > max_off) cbd->scroll_offset = max_off;
                    scroll_consumed = 1;
                }
            }
        }
        /* Check if an open dropmenu panel should consume the scroll */
        if (!scroll_consumed && ctx->open_dropmenu_id >= 0) {
            N_GUI_WIDGET* dm_wgt = n_gui_get_widget(ctx, ctx->open_dropmenu_id);
            if (dm_wgt && dm_wgt->data) {
                N_GUI_DROPMENU_DATA* dmd = (N_GUI_DROPMENU_DATA*)dm_wgt->data;
                if (dmd->is_open && (int)dmd->nb_entries > dmd->max_visible) {
                    dmd->scroll_offset -= event.mouse.dz;
                    if (dmd->scroll_offset < 0) dmd->scroll_offset = 0;
                    int max_off = (int)dmd->nb_entries - dmd->max_visible;
                    if (max_off < 0) max_off = 0;
                    if (dmd->scroll_offset > max_off) dmd->scroll_offset = max_off;
                    scroll_consumed = 1;
                }
            }
        }
        if (scroll_consumed) return 1;

        for (LIST_NODE* wnode = ctx->windows->end; wnode; wnode = wnode->prev) {
            N_GUI_WINDOW* win = (N_GUI_WINDOW*)wnode->ptr;
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
                    ld->scroll_offset -= event.mouse.dz;
                    if (ld->scroll_offset < 0) ld->scroll_offset = 0;
                    ALLEGRO_FONT* lf = wgt->font ? wgt->font : ctx->default_font;
                    float lfh = lf ? (float)al_get_font_line_height(lf) : 16.0f;
                    float lih = ld->item_height > lfh ? ld->item_height : lfh + ctx->style.item_height_pad;
                    int visible_count = (int)(wgt->h / lih);
                    int max_off = (int)ld->nb_items - visible_count;
                    if (max_off < 0) max_off = 0;
                    if (ld->scroll_offset > max_off) ld->scroll_offset = max_off;
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
                break;
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
        float win_h = (win->state & N_GUI_WIN_MINIMISED) ? _win_tbh(win) : win->h;
        if (_point_in_rect(mx, my, win->x, win->y, win->w, win_h)) {
            return 1;
        }
    }
    return 0;
}

/* =========================================================================
 *  JSON THEME I/O (requires cJSON)
 * ========================================================================= */

#ifdef HAVE_CJSON

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
    cJSON_AddItemToObject(root, "style", sty);

    /* write to file */
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return -1;

    FILE* fp = fopen(filepath, "w");
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
    }

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

#endif /* HAVE_CJSON */
