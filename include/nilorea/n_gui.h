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
 *@file n_gui.h
 *@brief GUI system: buttons, sliders, text areas, checkboxes, scrollbars, dropdown menus, windows
 *@author Castagnier Mickael
 *@version 3.0
 *@date 02/03/2026
 */

#ifndef __NILOREA_GUI__
#define __NILOREA_GUI__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup GUI GUI: allegro5 widget system with pseudo-windows

  ## Bitmap Skinning

  All widgets support optional bitmap overlays for visual customization.
  By default, every bitmap pointer is NULL and the standard color theme is used.
  To skin a widget element, call the corresponding `_set_bitmaps()` function
  after widget creation.

  Bitmaps are NOT owned by N_GUI, the caller is responsible for loading
  and destroying them. N_GUI only stores the pointer.

  ### Fallback Chain
  For widgets with per-state bitmaps (normal/hover/active):
  - If the state-specific bitmap is NULL, the normal bitmap is used.
  - If the normal bitmap is also NULL, the color theme is used.

  ### Supported Widgets
  - **Windows:** body background, titlebar background (n_gui_window_set_bitmaps())
  - **Buttons:** already supported via n_gui_add_button_bitmap()
  - **Sliders:** track, fill, handle with per-state variants (n_gui_slider_set_bitmaps())
  - **Scrollbars:** track, thumb with per-state variants (n_gui_scrollbar_set_bitmaps())
  - **Checkboxes:** box unchecked, box checked, box hover (n_gui_checkbox_set_bitmaps())
  - **Textareas:** background (n_gui_textarea_set_bitmap())
  - **Listbox/Radiolist/Combobox:** background, item background, item selected
  - **Dropmenus:** panel background, item hover (n_gui_dropmenu_set_bitmaps())
  - **Labels:** background (n_gui_label_set_bitmap())

  ### JSON Theme
  Bitmap assignments are programmatic (via setter functions), not part of the JSON
  theme file. The JSON theme remains the color/layout fallback.

   @addtogroup GUI
  @{
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include <nilorea/n_allegro5.h>
#include <nilorea/n_common.h>
#include <nilorea/n_str.h>
#include <nilorea/n_log.h>
#include <nilorea/n_list.h>
#include <nilorea/n_hash.h>

/*! maximum length for widget id/name strings */
#define N_GUI_ID_MAX 128
/*! maximum length for textarea content */
#define N_GUI_TEXT_MAX 4096

/* widget type enum */
/*! widget type: button */
#define N_GUI_TYPE_BUTTON 1
/*! widget type: slider */
#define N_GUI_TYPE_SLIDER 2
/*! widget type: text area */
#define N_GUI_TYPE_TEXTAREA 3
/*! widget type: checkbox */
#define N_GUI_TYPE_CHECKBOX 4
/*! widget type: scrollbar */
#define N_GUI_TYPE_SCROLLBAR 5
/*! widget type: listbox (selectable list) */
#define N_GUI_TYPE_LISTBOX 6
/*! widget type: radio list (single select radio buttons) */
#define N_GUI_TYPE_RADIOLIST 7
/*! widget type: combo box (dropdown) */
#define N_GUI_TYPE_COMBOBOX 8
/*! widget type: image display */
#define N_GUI_TYPE_IMAGE 9
/*! widget type: static text label (with optional hyperlink) */
#define N_GUI_TYPE_LABEL 10
/*! widget type: dropdown menu with static and dynamic entries */
#define N_GUI_TYPE_DROPMENU 11

/* shape flags */
/*! rectangle shape (default) */
#define N_GUI_SHAPE_RECT 0
/*! rounded rectangle shape */
#define N_GUI_SHAPE_ROUNDED 1
/*! bitmap-based rendering */
#define N_GUI_SHAPE_BITMAP 2

/* slider mode */
/*! slider uses raw start/end values */
#define N_GUI_SLIDER_VALUE 0
/*! slider uses 0-100 percentage */
#define N_GUI_SLIDER_PERCENT 1

/* slider orientation */
/*! horizontal slider (default) */
#define N_GUI_SLIDER_H 0
/*! vertical slider */
#define N_GUI_SLIDER_V 1

/* scrollbar orientation */
/*! horizontal scrollbar */
#define N_GUI_SCROLLBAR_H 0
/*! vertical scrollbar */
#define N_GUI_SCROLLBAR_V 1

/* list selection modes */
/*! no selection allowed (display only) */
#define N_GUI_SELECT_NONE 0
/*! single item selection */
#define N_GUI_SELECT_SINGLE 1
/*! multiple item selection */
#define N_GUI_SELECT_MULTIPLE 2

/* image scale modes */
/*! scale to fit within bounds, keep aspect ratio */
#define N_GUI_IMAGE_FIT 0
/*! stretch to fill bounds */
#define N_GUI_IMAGE_STRETCH 1
/*! draw at original size, centered */
#define N_GUI_IMAGE_CENTER 2

/* text alignment */
/*! left aligned text */
#define N_GUI_ALIGN_LEFT 0
/*! center aligned text */
#define N_GUI_ALIGN_CENTER 1
/*! right aligned text */
#define N_GUI_ALIGN_RIGHT 2
/*! justified text (spread words to fill width) */
#define N_GUI_ALIGN_JUSTIFIED 3

/* widget state flags */
/*! widget is idle / normal state */
#define N_GUI_STATE_IDLE 0
/*! mouse is hovering the widget */
#define N_GUI_STATE_HOVER 1
/*! widget is being pressed / dragged */
#define N_GUI_STATE_ACTIVE 2
/*! widget has keyboard focus */
#define N_GUI_STATE_FOCUSED 4
/*! widget scrollbar is being dragged */
#define N_GUI_STATE_SCROLLBAR_DRAG 8

/* window state flags */
/*! window is visible */
#define N_GUI_WIN_OPEN 1
/*! window is minimised (title bar only) */
#define N_GUI_WIN_MINIMISED 2
/*! window is being dragged */
#define N_GUI_WIN_DRAGGING 4
/*! window is being resized */
#define N_GUI_WIN_RESIZING 8
/*! window vertical auto-scrollbar is being dragged */
#define N_GUI_WIN_VSCROLL_DRAG 16
/*! window horizontal auto-scrollbar is being dragged */
#define N_GUI_WIN_HSCROLL_DRAG 32
/*! window is maximised (full display size) */
#define N_GUI_WIN_MAXIMISED 64

/* window feature flags (set via n_gui_window_set_flags) */
/*! enable automatic scrollbars when content exceeds window size */
#define N_GUI_WIN_AUTO_SCROLLBAR 16
/*! enable user-resizable window with a drag handle at bottom-right */
#define N_GUI_WIN_RESIZABLE 32
/*! disable window dragging (default:enable) */
#define N_GUI_WIN_FIXED_POSITION 64
/*! frameless window: no title bar drawn, drag via window body unless N_GUI_WIN_FIXED_POSITION is also set */
#define N_GUI_WIN_FRAMELESS 128
/*! enable minimize button on the title bar */
#define N_GUI_WIN_BTN_MINIMIZE 256
/*! enable maximize/restore button on the title bar */
#define N_GUI_WIN_BTN_MAXIMIZE 512
/*! enable close button on the title bar */
#define N_GUI_WIN_BTN_CLOSE 1024
/*! enable all three title bar buttons (minimize + maximize + close) */
#define N_GUI_WIN_BTN_ALL (N_GUI_WIN_BTN_MINIMIZE | N_GUI_WIN_BTN_MAXIMIZE | N_GUI_WIN_BTN_CLOSE)

/* titlebar button indices */
/*! no titlebar button */
#define N_GUI_TB_BTN_NONE 0
/*! minimize titlebar button */
#define N_GUI_TB_BTN_MINIMIZE 1
/*! maximize/restore titlebar button */
#define N_GUI_TB_BTN_MAXIMIZE 2
/*! close titlebar button */
#define N_GUI_TB_BTN_CLOSE 3

/* context-level resize modes (set via n_gui_set_resize_mode) */
/*! fixed virtual canvas with uniform scaling (default/existing behavior) */
#define N_GUI_RESIZE_VIRTUAL 0
/*! virtual size tracks display; windows adapt per their resize_policy */
#define N_GUI_RESIZE_ADAPTIVE 1

/* per-window resize policies (used in ADAPTIVE mode) */
/*! no adaptation: absolute position and size unchanged */
#define N_GUI_WIN_RESIZE_NONE 0
/*! reposition proportionally, keep pixel size */
#define N_GUI_WIN_RESIZE_MOVE 1
/*! reposition AND resize proportionally, child widgets scale too */
#define N_GUI_WIN_RESIZE_SCALE 2

/* dialog auto-adjust flags (set via n_gui_window_set_autofit) */
/*! auto-adjust window width to content */
#define N_GUI_AUTOFIT_W 1
/*! auto-adjust window height to content */
#define N_GUI_AUTOFIT_H 2
/*! auto-adjust both width and height (convenience: W|H) */
#define N_GUI_AUTOFIT_WH 3
/*! expand leftward instead of rightward when adjusting width */
#define N_GUI_AUTOFIT_EXPAND_LEFT 4
/*! expand upward instead of downward when adjusting height */
#define N_GUI_AUTOFIT_EXPAND_UP 8
/*! center the window on its insertion point after auto-fit (overrides EXPAND_LEFT/EXPAND_UP for centering) */
#define N_GUI_AUTOFIT_CENTER 16

/* window z-order modes */
/*! default z-order: window participates in normal raise/lower ordering */
#define N_GUI_ZORDER_NORMAL 0
/*! always drawn on top of normal windows, cannot be lowered behind them */
#define N_GUI_ZORDER_ALWAYS_ON_TOP 1
/*! always drawn behind normal windows, cannot be raised above them */
#define N_GUI_ZORDER_ALWAYS_BEHIND 2
/*! fixed z-value: window is sorted within a dedicated group between
 *  ALWAYS_BEHIND and NORMAL windows.  The draw order of groups is:
 *    ALWAYS_BEHIND < FIXED < NORMAL < ALWAYS_ON_TOP < POPUP.
 *  Within the FIXED group, windows are ordered by z_value (lower = drawn first / behind,
 *  higher = drawn last / on top).  z_value has no effect on other z-order modes.
 *  Windows with the same z_value preserve their relative list order (stable sort). */
#define N_GUI_ZORDER_FIXED 3
/*! popup/modal dialogs: drawn above EVERYTHING including ALWAYS_ON_TOP windows.
 *  Use for transient attention-demanding windows (confirmations, invitations,
 *  NPC dialogs, loot/reward popups, context menus). Unlike ALWAYS_ON_TOP,
 *  POPUP windows ARE raisable (n_gui_raise_window / click-to-raise) WITHIN
 *  their group, so the most recently opened/clicked popup wins among popups
 *  while none of them can ever be covered by HUD furniture. */
#define N_GUI_ZORDER_POPUP 4

/* text dimension bounding box */

/*! bounding box dimensions returned by n_gui_get_text_dims() */
typedef struct N_GUI_TEXT_DIMS {
    int x; /*!< bounding box x offset from draw origin */
    int y; /*!< bounding box y offset from draw origin */
    int w; /*!< bounding box width in pixels */
    int h; /*!< bounding box height in pixels */
} N_GUI_TEXT_DIMS;

/* color theme */

/*! Color theme for a widget */
typedef struct N_GUI_THEME {
    /*! background normal */
    ALLEGRO_COLOR bg_normal;
    /*! background hover */
    ALLEGRO_COLOR bg_hover;
    /*! background active/pressed */
    ALLEGRO_COLOR bg_active;
    /*! border normal */
    ALLEGRO_COLOR border_normal;
    /*! border hover */
    ALLEGRO_COLOR border_hover;
    /*! border active */
    ALLEGRO_COLOR border_active;
    /*! text normal */
    ALLEGRO_COLOR text_normal;
    /*! text hover */
    ALLEGRO_COLOR text_hover;
    /*! text active */
    ALLEGRO_COLOR text_active;
    /*! border thickness */
    float border_thickness;
    /*! corner radius for rounded shapes */
    float corner_rx;
    /*! corner radius Y for rounded shapes */
    float corner_ry;
    /*! text selection highlight colour (semi-transparent recommended) */
    ALLEGRO_COLOR selection_color;
} N_GUI_THEME;

/* button key modifier flags (use with n_gui_button_set_keycode) */
/*! mask of supported modifier flags for button keybind matching */
#define N_GUI_KEY_MOD_MASK (ALLEGRO_KEYMOD_SHIFT | ALLEGRO_KEYMOD_CTRL | ALLEGRO_KEYMOD_ALT | ALLEGRO_KEYMOD_ALTGR)

/*! maximum number of source widgets for a focused key binding */
#define N_GUI_KEY_SOURCES_MAX 8

/* per-widget data */

/*! button specific data */
typedef struct N_GUI_BUTTON_DATA {
    /*! label displayed on the button */
    char label[N_GUI_ID_MAX];
    /*! optional bitmap for the button (NULL = color theme) */
    ALLEGRO_BITMAP* bitmap;
    /*! optional bitmap for hover state */
    ALLEGRO_BITMAP* bitmap_hover;
    /*! optional bitmap for active/pressed state */
    ALLEGRO_BITMAP* bitmap_active;
    /*! shape type: N_GUI_SHAPE_RECT, N_GUI_SHAPE_ROUNDED, N_GUI_SHAPE_BITMAP */
    int shape;
    /*! toggle mode: 0 = momentary (default), 1 = toggle (stays clicked/unclicked) */
    int toggle_mode;
    /*! toggle state: 0 = off/unclicked, 1 = on/clicked (only used when toggle_mode=1) */
    int toggled;
    /*! bound keyboard keycode (0 = none). When this key is pressed and no text
     *  input widget has focus, the button's on_click callback is triggered. */
    int keycode;
    /*! required modifier key flags for the keybind (0 = no modifier requirement,
     *  matches any modifier state for backward compatibility).
     *  Use ALLEGRO_KEYMOD_SHIFT, ALLEGRO_KEYMOD_CTRL, ALLEGRO_KEYMOD_ALT,
     *  ALLEGRO_KEYMOD_ALTGR ORed together. When non-zero, only the specified
     *  modifier combination triggers the keybind (exact match on N_GUI_KEY_MOD_MASK bits). */
    int key_modifiers;
    /*! if 1, keycode fires only when the button itself or a source widget has focus.
     *  if 0 (default), keycode fires globally when no interactive widget has focus. */
    int key_focus_only;
    /*! widget IDs that can trigger this focused key binding. Terminated by -1.
     *  Only used when key_focus_only == 1. Up to N_GUI_KEY_SOURCES_MAX entries. */
    int key_sources[N_GUI_KEY_SOURCES_MAX];
    /*! absolute al_get_time() deadline until which the button is rendered in
     *  its pressed (N_GUI_STATE_ACTIVE) visual state after a keybind trigger.
     *  0.0 = no pending key-press flash. */
    double key_press_until;
    /*! callback on click (widget_id passed) */
    void (*on_click)(int widget_id, void* user_data);
    /*! user data for callback */
    void* user_data;
} N_GUI_BUTTON_DATA;

/*! slider specific data */
typedef struct N_GUI_SLIDER_DATA {
    /*! minimum value */
    double min_val;
    /*! maximum value */
    double max_val;
    /*! current value (always snapped to step) */
    double value;
    /*! step increment (0 is treated as 1). All values snap to nearest multiple of step from min_val. */
    double step;
    /*! mode: N_GUI_SLIDER_VALUE or N_GUI_SLIDER_PERCENT */
    int mode;
    /*! orientation: N_GUI_SLIDER_H or N_GUI_SLIDER_V */
    int orientation;
    /*! optional bitmap for the track/rail background (NULL = color theme) */
    ALLEGRO_BITMAP* track_bitmap;
    /*! optional bitmap for the filled portion of the track (NULL = color theme) */
    ALLEGRO_BITMAP* fill_bitmap;
    /*! optional bitmap for the draggable handle, normal state (NULL = color theme) */
    ALLEGRO_BITMAP* handle_bitmap;
    /*! optional bitmap for the handle on hover (NULL = fallback to handle_bitmap) */
    ALLEGRO_BITMAP* handle_hover_bitmap;
    /*! optional bitmap for the handle while dragging (NULL = fallback to handle_bitmap) */
    ALLEGRO_BITMAP* handle_active_bitmap;
    /*! callback on value change */
    void (*on_change)(int widget_id, double value, void* user_data);
    /*! user data for callback */
    void* user_data;
    /*! printf-style format string applied to the numeric value when the
     *  built-in readout draws. Empty (first byte '\0') selects the
     *  legacy defaults: "%.1f" for VALUE mode, "%.0f%%" for PERCENT.
     *  Set via n_gui_slider_set_value_format(). */
    char value_format[32];
    /*! draw the built-in value label next to/below the handle. Default
     *  1 (on) for backward compatibility. Set to 0 when the consumer
     *  is already painting the value elsewhere (e.g. a status bar) to
     *  avoid the double-render gotcha. */
    int value_visible;
} N_GUI_SLIDER_DATA;

/*! text area specific data */
typedef struct N_GUI_TEXTAREA_DATA {
    /*! text content (dynamically allocated, text_alloc bytes) */
    char* text;
    /*! current text length */
    size_t text_len;
    /*! allocated buffer size (char_limit + 1) */
    size_t text_alloc;
    /*! maximum character limit (0 = N_GUI_TEXT_MAX) */
    size_t char_limit;
    /*! 0 = single line, 1 = multiline */
    int multiline;
    /*! cursor position in text */
    size_t cursor_pos;
    /*! selection anchor position (where shift-click/shift-arrow started).
     *  When sel_start == sel_end, no selection is active. */
    size_t sel_start;
    /*! selection end position (tracks cursor during selection) */
    size_t sel_end;
    /*! scroll offset for long text */
    int scroll_y;
    /*! horizontal scroll offset for single-line text (pixels) */
    float scroll_x;
    /*! set to 1 when scroll was changed by mouse wheel, cleared on key input */
    int scroll_from_wheel;
    /*! timestamp of last cursor activity (for blink reset) */
    double cursor_time;
    /*! optional bitmap for the text area background (NULL = color theme) */
    ALLEGRO_BITMAP* bg_bitmap;
    /*! mask character for password fields (0 = no masking, e.g. '*') */
    char mask_char;
    /*! callback on text change */
    void (*on_change)(int widget_id, const char* text, void* user_data);
    /*! user data for callback */
    void* user_data;
} N_GUI_TEXTAREA_DATA;

/*! checkbox specific data */
typedef struct N_GUI_CHECKBOX_DATA {
    /*! label displayed next to the checkbox */
    char label[N_GUI_ID_MAX];
    /*! checked state */
    int checked;
    /*! optional bitmap for the checkbox square, unchecked state (NULL = color theme) */
    ALLEGRO_BITMAP* box_bitmap;
    /*! optional bitmap for the checkbox square, checked state (NULL = color theme) */
    ALLEGRO_BITMAP* box_checked_bitmap;
    /*! optional bitmap for the checkbox square on hover (NULL = fallback to box_bitmap/box_checked_bitmap) */
    ALLEGRO_BITMAP* box_hover_bitmap;
    /*! callback on toggle */
    void (*on_toggle)(int widget_id, int checked, void* user_data);
    /*! user data for callback */
    void* user_data;
} N_GUI_CHECKBOX_DATA;

/*! scrollbar specific data */
typedef struct N_GUI_SCROLLBAR_DATA {
    /*! orientation: N_GUI_SCROLLBAR_H or N_GUI_SCROLLBAR_V */
    int orientation;
    /*! total content size */
    double content_size;
    /*! visible viewport size */
    double viewport_size;
    /*! current scroll position */
    double scroll_pos;
    /*! shape type: N_GUI_SHAPE_RECT or N_GUI_SHAPE_ROUNDED */
    int shape;
    /*! optional bitmap for the scrollbar track background (NULL = color theme) */
    ALLEGRO_BITMAP* track_bitmap;
    /*! optional bitmap for the thumb/handle, normal state (NULL = color theme) */
    ALLEGRO_BITMAP* thumb_bitmap;
    /*! optional bitmap for the thumb on hover (NULL = fallback to thumb_bitmap) */
    ALLEGRO_BITMAP* thumb_hover_bitmap;
    /*! optional bitmap for the thumb while dragging (NULL = fallback to thumb_bitmap) */
    ALLEGRO_BITMAP* thumb_active_bitmap;
    /*! callback on scroll */
    void (*on_scroll)(int widget_id, double scroll_pos, void* user_data);
    /*! user data for callback */
    void* user_data;
} N_GUI_SCROLLBAR_DATA;

/*! a single item in a list/radio/combo widget */
typedef struct N_GUI_LISTITEM {
    /*! item display text */
    char text[N_GUI_ID_MAX];
    /*! selection state (for listbox) */
    int selected;
} N_GUI_LISTITEM;

/*! listbox specific data */
typedef struct N_GUI_LISTBOX_DATA {
    /*! dynamic array of items */
    N_GUI_LISTITEM* items;
    /*! number of items */
    size_t nb_items;
    /*! allocated capacity */
    size_t items_capacity;
    /*! selection mode: N_GUI_SELECT_NONE, N_GUI_SELECT_SINGLE, N_GUI_SELECT_MULTIPLE */
    int selection_mode;
    /*! scroll offset in items */
    int scroll_offset;
    /*! item height in pixels */
    float item_height;
    /*! optional bitmap for the listbox background (NULL = color theme) */
    ALLEGRO_BITMAP* bg_bitmap;
    /*! optional bitmap for per-item background, normal state (NULL = color theme) */
    ALLEGRO_BITMAP* item_bg_bitmap;
    /*! optional bitmap for per-item background, selected/highlighted (NULL = color theme) */
    ALLEGRO_BITMAP* item_selected_bitmap;
    /*! callback on selection change (widget_id, item_index, selected, user_data) */
    void (*on_select)(int widget_id, int index, int selected, void* user_data);
    /*! user data for callback */
    void* user_data;
    /*! index of the row the mouse is currently over, or -1. Updated
     *  in the event loop when the widget gains N_GUI_STATE_HOVER;
     *  consumed by the draw path to paint a hover highlight. */
    int hover_row;
} N_GUI_LISTBOX_DATA;

/*! radio list specific data */
typedef struct N_GUI_RADIOLIST_DATA {
    /*! dynamic array of items */
    N_GUI_LISTITEM* items;
    /*! number of items */
    size_t nb_items;
    /*! allocated capacity */
    size_t items_capacity;
    /*! currently selected index (-1 = none) */
    int selected_index;
    /*! scroll offset in items */
    int scroll_offset;
    /*! item height in pixels */
    float item_height;
    /*! optional bitmap for the radiolist background (NULL = color theme) */
    ALLEGRO_BITMAP* bg_bitmap;
    /*! optional bitmap for per-item background, normal state (NULL = color theme) */
    ALLEGRO_BITMAP* item_bg_bitmap;
    /*! optional bitmap for per-item background, selected/highlighted (NULL = color theme) */
    ALLEGRO_BITMAP* item_selected_bitmap;
    /*! callback on selection change (widget_id, selected_index, user_data) */
    void (*on_select)(int widget_id, int index, void* user_data);
    /*! user data for callback */
    void* user_data;
} N_GUI_RADIOLIST_DATA;

/* combobox feature flags */
/*! dropdown panel expands to fit the longest item text */
#define N_GUI_COMBOBOX_AUTO_WIDTH 1
/*! dropdown panel opens upward (above the widget) instead of downward.
 *  Useful for bottom-anchored comboboxes where the default downward panel
 *  would render off-screen. Without the flag, an auto-flip fallback still
 *  kicks in whenever the panel doesn't fit below and does fit above. */
#define N_GUI_COMBOBOX_EXPAND_UP 2

/*! combo box specific data */
typedef struct N_GUI_COMBOBOX_DATA {
    /*! dynamic array of items */
    N_GUI_LISTITEM* items;
    /*! number of items */
    size_t nb_items;
    /*! allocated capacity */
    size_t items_capacity;
    /*! currently selected index (-1 = none) */
    int selected_index;
    /*! is dropdown currently open */
    int is_open;
    /*! scroll offset in dropdown */
    int scroll_offset;
    /*! item height in dropdown */
    float item_height;
    /*! max visible items in dropdown */
    int max_visible;
    /*! optional bitmap for the combobox background (NULL = color theme) */
    ALLEGRO_BITMAP* bg_bitmap;
    /*! optional bitmap for per-item background, normal state (NULL = color theme) */
    ALLEGRO_BITMAP* item_bg_bitmap;
    /*! optional bitmap for per-item background, selected/highlighted (NULL = color theme) */
    ALLEGRO_BITMAP* item_selected_bitmap;
    /*! callback on selection change (widget_id, selected_index, user_data) */
    void (*on_select)(int widget_id, int index, void* user_data);
    /*! user data for callback */
    void* user_data;
    /*! bitmask of N_GUI_COMBOBOX_* flags */
    int flags;
} N_GUI_COMBOBOX_DATA;

/*! image widget specific data */
typedef struct N_GUI_IMAGE_DATA {
    /*! bitmap to display (not owned, not freed) */
    ALLEGRO_BITMAP* bitmap;
    /*! scale mode: N_GUI_IMAGE_FIT, N_GUI_IMAGE_STRETCH, N_GUI_IMAGE_CENTER */
    int scale_mode;
} N_GUI_IMAGE_DATA;

/*! static text label specific data */
typedef struct N_GUI_LABEL_DATA {
    /*! text to display */
    char text[N_GUI_TEXT_MAX];
    /*! optional hyperlink URL (empty string = no link) */
    char link[N_GUI_TEXT_MAX];
    /*! text alignment: N_GUI_ALIGN_LEFT, N_GUI_ALIGN_CENTER, N_GUI_ALIGN_RIGHT */
    int align;
    /*! vertical scroll offset for overflowing text (pixels) */
    float scroll_y;
    /*! selection start byte offset (-1 = no selection) */
    int sel_start;
    /*! selection end byte offset (-1 = no selection) */
    int sel_end;
    /*! 1 if mouse is actively dragging to select text */
    int sel_dragging;
    /*! optional bitmap for the label background (NULL = no background) */
    ALLEGRO_BITMAP* bg_bitmap;
    /*! callback when link is clicked (widget_id, link_url, user_data) */
    void (*on_link_click)(int widget_id, const char* link, void* user_data);
    /*! user data for callback */
    void* user_data;
} N_GUI_LABEL_DATA;

/*! A single entry in a dropdown menu (static or dynamic) */
typedef struct N_GUI_DROPMENU_ENTRY {
    /*! display text */
    char text[N_GUI_ID_MAX];
    /*! is this entry dynamic (rebuilt via callback each time menu opens) */
    int is_dynamic;
    /*! user-defined tag for the entry (e.g. a window id) */
    int tag;
    /*! callback when this entry is clicked (widget_id, entry_index, tag, user_data) */
    void (*on_click)(int widget_id, int entry_index, int tag, void* user_data);
    /*! user data for callback */
    void* user_data;
} N_GUI_DROPMENU_ENTRY;

/* dropmenu feature flags */
/*! dropdown panel opens upward (above the button) instead of downward.
 *  Auto-flip fallback (pick direction based on available space) also kicks in
 *  whenever the panel doesn't fit below and does fit above. */
#define N_GUI_DROPMENU_EXPAND_UP 1

/*! dropdown menu specific data */
typedef struct N_GUI_DROPMENU_DATA {
    /*! menu label (shown on the button) */
    char label[N_GUI_ID_MAX];
    /*! dynamic array of entries */
    N_GUI_DROPMENU_ENTRY* entries;
    /*! number of entries */
    size_t nb_entries;
    /*! allocated capacity */
    size_t entries_capacity;
    /*! is the menu currently open */
    int is_open;
    /*! scroll offset */
    int scroll_offset;
    /*! max visible items */
    int max_visible;
    /*! item height */
    float item_height;
    /*! optional bitmap for the dropdown panel background (NULL = color theme) */
    ALLEGRO_BITMAP* panel_bitmap;
    /*! optional bitmap for the hovered entry background (NULL = color theme) */
    ALLEGRO_BITMAP* item_hover_bitmap;
    /*! callback to rebuild dynamic entries each time the menu is opened.
     *  signature: void rebuild(int widget_id, void* user_data)
     *  The callback should call n_gui_dropmenu_clear_dynamic() then add dynamic entries. */
    void (*on_open)(int widget_id, void* user_data);
    /*! user data for on_open callback */
    void* on_open_user_data;
    /*! bitmask of N_GUI_DROPMENU_* flags */
    int flags;
} N_GUI_DROPMENU_DATA;

/* widget */

/*! A single GUI widget */
typedef struct N_GUI_WIDGET {
    /*! unique widget id */
    int id;
    /*! widget type (N_GUI_TYPE_*) */
    int type;
    /*! position x relative to parent window */
    float x;
    /*! position y relative to parent window */
    float y;
    /*! width */
    float w;
    /*! height */
    float h;
    /*! current state flags (N_GUI_STATE_*) */
    int state;
    /*! visibility flag */
    int visible;
    /*! enabled flag (1 = enabled, 0 = disabled: drawn dimmed and ignores input) */
    int enabled;
    /*! color theme for this widget */
    N_GUI_THEME theme;
    /*! font used by this widget (NULL = context default) */
    ALLEGRO_FONT* font;
    /*! widget-specific data (union via void pointer) */
    void* data;
    /*! tooltip text shown after the pointer rests on the widget ("" = none) */
    char tooltip[96];
    /*! normalized position x (fraction of parent window w, used for SCALE resize) */
    float norm_x;
    /*! normalized position y (fraction of parent window h, used for SCALE resize) */
    float norm_y;
    /*! normalized width (fraction of parent window w) */
    float norm_w;
    /*! normalized height (fraction of parent window h) */
    float norm_h;
} N_GUI_WIDGET;

/* titlebar buttons state */

/*! Titlebar button state for a window (minimize, maximize, close) */
typedef struct N_GUI_TB_BUTTONS {
    /*! which button is currently hovered (N_GUI_TB_BTN_*) */
    int hovered;
    /*! which button is currently pressed (N_GUI_TB_BTN_*) */
    int pressed;
    /*! saved x position before maximise (for restore) */
    float restore_x;
    /*! saved y position before maximise (for restore) */
    float restore_y;
    /*! saved width before maximise (for restore) */
    float restore_w;
    /*! saved height before maximise (for restore) */
    float restore_h;
    /*! theme for minimize and maximize buttons */
    N_GUI_THEME btn_theme;
    /*! theme for close button (defaults to red hover/active) */
    N_GUI_THEME close_theme;
    /*! optional bitmap for minimize button normal state (NULL = color theme) */
    ALLEGRO_BITMAP* minimize_bitmap;
    /*! optional bitmap for minimize button hover state */
    ALLEGRO_BITMAP* minimize_hover_bitmap;
    /*! optional bitmap for minimize button active state */
    ALLEGRO_BITMAP* minimize_active_bitmap;
    /*! optional bitmap for maximize button normal state (NULL = color theme) */
    ALLEGRO_BITMAP* maximize_bitmap;
    /*! optional bitmap for maximize button hover state */
    ALLEGRO_BITMAP* maximize_hover_bitmap;
    /*! optional bitmap for maximize button active state */
    ALLEGRO_BITMAP* maximize_active_bitmap;
    /*! optional bitmap for close button normal state (NULL = color theme) */
    ALLEGRO_BITMAP* close_bitmap;
    /*! optional bitmap for close button hover state */
    ALLEGRO_BITMAP* close_hover_bitmap;
    /*! optional bitmap for close button active state */
    ALLEGRO_BITMAP* close_active_bitmap;
    /*! callback on minimize button click (NULL = default: toggle N_GUI_WIN_MINIMISED) */
    void (*on_minimize)(int window_id, void* user_data);
    /*! user data for on_minimize callback */
    void* on_minimize_user_data;
    /*! callback on maximize button click (NULL = default: toggle N_GUI_WIN_MAXIMISED) */
    void (*on_maximize)(int window_id, void* user_data);
    /*! user data for on_maximize callback */
    void* on_maximize_user_data;
    /*! callback on close button click (NULL = default: clear N_GUI_WIN_OPEN) */
    void (*on_close)(int window_id, void* user_data);
    /*! user data for on_close callback */
    void* on_close_user_data;
} N_GUI_TB_BUTTONS;

/* pseudo window */

/*! A pseudo window that contains widgets */
typedef struct N_GUI_WINDOW {
    /*! unique window id */
    int id;
    /*! window title */
    char title[N_GUI_ID_MAX];
    /*! position x on screen */
    float x;
    /*! position y on screen */
    float y;
    /*! window width */
    float w;
    /*! window height */
    float h;
    /*! title bar height */
    float titlebar_h;
    /*! state flags (N_GUI_WIN_*) */
    int state;
    /*! feature flags (N_GUI_WIN_AUTO_SCROLLBAR, N_GUI_WIN_RESIZABLE, etc.) */
    int flags;
    /*! list of N_GUI_WIDGET* contained in this window */
    LIST* widgets;
    /*! color theme for this window chrome */
    N_GUI_THEME theme;
    /*! font for the title bar */
    ALLEGRO_FONT* font;
    /*! drag offset x (internal) */
    float drag_ox;
    /*! drag offset y (internal) */
    float drag_oy;
    /*! minimum width */
    float min_w;
    /*! minimum height */
    float min_h;
    /*! vertical scroll offset for auto-scrollbar (pixels) */
    float scroll_y;
    /*! horizontal scroll offset for auto-scrollbar (pixels) */
    float scroll_x;
    /*! total content height computed from widgets (internal) */
    float content_h;
    /*! total content width computed from widgets (internal) */
    float content_w;
    /*! z-order mode (N_GUI_ZORDER_NORMAL, _ALWAYS_ON_TOP, _ALWAYS_BEHIND, _FIXED) */
    int z_order;
    /*! z-value for N_GUI_ZORDER_FIXED mode (lower = behind, higher = on top) */
    int z_value;
    /*! optional bitmap for the window body background (NULL = color fill) */
    ALLEGRO_BITMAP* bg_bitmap;
    /*! optional bitmap for the titlebar background (NULL = color fill) */
    ALLEGRO_BITMAP* titlebar_bitmap;
    /*! scale mode for bg_bitmap: N_GUI_IMAGE_FIT, N_GUI_IMAGE_STRETCH, or N_GUI_IMAGE_CENTER */
    int bg_scale_mode;
    /*! bitmask of N_GUI_AUTOFIT_* flags (0 = no auto-fitting) */
    int autofit_flags;
    /*! padding/border around content for auto-fit (pixels, applied on each side) */
    float autofit_border;
    /*! original insertion point x for N_GUI_AUTOFIT_CENTER (set by n_gui_window_set_autofit) */
    float autofit_origin_x;
    /*! original insertion point y for N_GUI_AUTOFIT_CENTER (set by n_gui_window_set_autofit) */
    float autofit_origin_y;
    /*! per-window resize policy: N_GUI_WIN_RESIZE_NONE, _MOVE, or _SCALE */
    int resize_policy;
    /*! normalized position x (0.0-1.0 fraction of reference display width) */
    float norm_x;
    /*! normalized position y (0.0-1.0 fraction of reference display height) */
    float norm_y;
    /*! normalized width (fraction of reference display width, used in SCALE mode) */
    float norm_w;
    /*! normalized height (fraction of reference display height, used in SCALE mode) */
    float norm_h;
    /*! titlebar button state (minimize, maximize, close) */
    N_GUI_TB_BUTTONS tb_buttons;
    /*! optional callback invoked during n_gui_draw immediately after this
     *  window's own content is drawn, at the window's z-order (NULL = none).
     *  Lets a caller add custom immediate-mode rendering that is correctly
     *  occluded by any window drawn on top of this one. */
    void (*on_content_draw)(int window_id, void* user_data);
    /*! user data passed to on_content_draw */
    void* on_content_draw_data;
} N_GUI_WINDOW;

/* global style (all configurable sizes, colors, paddings) */

/*! Global style holding every configurable layout constant.
 *  Initialised with sensible defaults by n_gui_default_style().
 *  Can be serialised/deserialised to/from JSON with n_gui_style_to_json / n_gui_style_from_json. */
typedef struct N_GUI_STYLE {
    /* window chrome */
    /*! title bar height */
    float titlebar_h;
    /*! minimum window width (for resize) */
    float min_win_w;
    /*! minimum window height (for resize) */
    float min_win_h;
    /*! padding left of title text */
    float title_padding;
    /*! pixels reserved right of title for truncation */
    float title_max_w_reserve;

    /* titlebar buttons */
    /*! titlebar button size (0 = auto: titlebar_h - 4) */
    float tb_btn_size;
    /*! gap between titlebar buttons */
    float tb_btn_spacing;
    /*! right margin from window edge to rightmost button */
    float tb_btn_right_margin;
    /*! line thickness for titlebar button glyphs */
    float tb_btn_glyph_thickness;

    /* window auto-scrollbar */
    /*! scrollbar track width/height */
    float scrollbar_size;
    /*! minimum thumb dimension */
    float scrollbar_thumb_min;
    /*! thumb inset from track edge */
    float scrollbar_thumb_padding;
    /*! thumb corner radius */
    float scrollbar_thumb_corner_r;
    /*! scrollbar track colour */
    ALLEGRO_COLOR scrollbar_track_color;
    /*! scrollbar thumb colour */
    ALLEGRO_COLOR scrollbar_thumb_color;

    /* global display scrollbar */
    /*! global scrollbar track width/height */
    float global_scrollbar_size;
    /*! global minimum thumb dimension */
    float global_scrollbar_thumb_min;
    /*! global thumb inset from track edge */
    float global_scrollbar_thumb_padding;
    /*! global thumb corner radius */
    float global_scrollbar_thumb_corner_r;
    /*! global thumb border thickness */
    float global_scrollbar_border_thickness;
    /*! global scrollbar track colour */
    ALLEGRO_COLOR global_scrollbar_track_color;
    /*! global scrollbar thumb colour */
    ALLEGRO_COLOR global_scrollbar_thumb_color;
    /*! global scrollbar thumb border colour */
    ALLEGRO_COLOR global_scrollbar_thumb_border_color;

    /* resize grip */
    /*! grip area size */
    float grip_size;
    /*! grip line thickness */
    float grip_line_thickness;
    /*! grip line colour */
    ALLEGRO_COLOR grip_color;

    /* slider */
    /*! track width (vertical) or height (horizontal) */
    float slider_track_size;
    /*! track corner radius */
    float slider_track_corner_r;
    /*! track outline thickness */
    float slider_track_border_thickness;
    /*! minimum handle circle radius */
    float slider_handle_min_r;
    /*! handle circle offset from edge */
    float slider_handle_edge_offset;
    /*! handle border thickness */
    float slider_handle_border_thickness;
    /*! gap between slider end and value label */
    float slider_value_label_offset;

    /* text area */
    /*! inner padding */
    /*! seconds the pointer must rest on a widget before its tooltip shows */
    float tooltip_delay;
    /*! tooltip bubble background color */
    ALLEGRO_COLOR tooltip_bg;
    /*! tooltip bubble border color */
    ALLEGRO_COLOR tooltip_border;
    /*! tooltip text color */
    ALLEGRO_COLOR tooltip_fg;
    float textarea_padding;
    /*! cursor width in pixels */
    float textarea_cursor_width;
    /*! cursor blink period in seconds (full cycle on+off) */
    float textarea_cursor_blink_period;

    /* checkbox */
    /*! maximum box size */
    float checkbox_max_size;
    /*! checkmark inset from box edge */
    float checkbox_mark_margin;
    /*! checkmark line thickness */
    float checkbox_mark_thickness;
    /*! gap between box and label text */
    float checkbox_label_gap;
    /*! horizontal offset from box edge to label text */
    float checkbox_label_offset;

    /* radio list */
    /*! minimum outer circle radius */
    float radio_circle_min_r;
    /*! outer circle border thickness */
    float radio_circle_border_thickness;
    /*! inner filled circle shrink from outer */
    float radio_inner_offset;
    /*! gap between circle and label text */
    float radio_label_gap;

    /* list items (shared by listbox, radiolist, combobox, dropmenu) */
    /*! default item height for listbox */
    float listbox_default_item_height;
    /*! default item height for radiolist */
    float radiolist_default_item_height;
    /*! default max visible items for combobox dropdown */
    int combobox_max_visible;
    /*! default max visible items for dropmenu panel */
    int dropmenu_max_visible;
    /*! text padding inside list/combo/dropmenu items */
    float item_text_padding;
    /*! item highlight inset from left/right edges */
    float item_selection_inset;
    /*! min padding added to font height for item height */
    float item_height_pad;

    /* dropdown arrow (combobox & dropmenu) */
    /*! horizontal space reserved for the arrow on the right */
    float dropdown_arrow_reserve;
    /*! arrow stroke thickness */
    float dropdown_arrow_thickness;
    /*! vertical half-extent of the arrow chevron */
    float dropdown_arrow_half_h;
    /*! horizontal half-extent of the arrow chevron */
    float dropdown_arrow_half_w;
    /*! dropdown panel border thickness */
    float dropdown_border_thickness;

    /* label */
    /*! horizontal padding each side */
    float label_padding;
    /*! link underline thickness */
    float link_underline_thickness;
    /*! link colour (normal) */
    ALLEGRO_COLOR link_color_normal;
    /*! link colour (hover) */
    ALLEGRO_COLOR link_color_hover;

    /* scroll step */
    /*! pixels per mouse wheel notch for window auto-scroll */
    float scroll_step;
    /*! pixels per mouse wheel notch for global scroll */
    float global_scroll_step;

    /* combobox auto-width cap */
    /*! maximum dropdown width for N_GUI_COMBOBOX_AUTO_WIDTH (0 = display width) */
    float combobox_max_dropdown_width;
} N_GUI_STYLE;

/* gui context */

/*! A saved window position retained for a window not yet created at load
 *  time (see N_GUI_CTX::pending_layout). Applied on first matching create. */
typedef struct N_GUI_PENDING_GEOM {
    char title[N_GUI_ID_MAX]; /*!< window title to match on */
    float x;                  /*!< saved x (already display-scaled) */
    float y;                  /*!< saved y */
    unsigned char consumed;   /*!< 1 once applied to a created window */
} N_GUI_PENDING_GEOM;

/*! The top-level GUI context that holds all windows */
typedef struct N_GUI_CTX {
    /*! ordered list of N_GUI_WINDOW* (back to front) */
    LIST* windows;
    /*! hash table for fast widget lookup by id */
    HASH_TABLE* widgets_by_id;
    /*! next widget id to assign */
    int next_widget_id;
    /*! next window id to assign */
    int next_window_id;
    /*! default font (must be set before adding widgets) */
    ALLEGRO_FONT* default_font;
    /*! default theme (applied to new widgets/windows) */
    N_GUI_THEME default_theme;
    /*! id of the widget that currently has focus, or -1 */
    int focused_widget_id;
    /*! mouse state tracking */
    int mouse_x;
    /*! mouse state tracking */
    int mouse_y;
    /*! mouse button 1 state */
    int mouse_b1;
    /*! previous mouse button 1 state */
    int mouse_b1_prev;
    /*! widget the pointer is resting on for tooltip purposes (-1 = none) */
    int tooltip_widget_id;
    /*! time the tooltip hover was (re)armed, from al_get_time() */
    double tooltip_armed_at;
    /*! pointer x when the tooltip was last armed (movement re-arms) */
    int tooltip_anchor_x;
    /*! pointer y when the tooltip was last armed */
    int tooltip_anchor_y;
    /*! id of the combobox whose dropdown is currently open, or -1 */
    int open_combobox_id;
    /*! id of the widget whose scrollbar is being dragged, or -1 */
    int scrollbar_drag_widget_id;
    /*! id of the dropdown menu whose panel is currently open, or -1 */
    int open_dropmenu_id;
    /*! display/viewport width (set via n_gui_set_display_size) */
    float display_w;
    /*! display/viewport height */
    float display_h;
    /*! global horizontal scroll offset (when GUI exceeds display) */
    float global_scroll_x;
    /*! global vertical scroll offset (when GUI exceeds display) */
    float global_scroll_y;
    /*! total bounding box width of all windows (computed internally) */
    float gui_bounds_w;
    /*! total bounding box height of all windows (computed internally) */
    float gui_bounds_h;
    /*! DPI scale factor (1.0 = normal, 1.25 = 125%, 2.0 = HiDPI, etc.) */
    float dpi_scale;
    /*! virtual canvas width (0 = disabled / identity transform) */
    float virtual_w;
    /*! virtual canvas height (0 = disabled / identity transform) */
    float virtual_h;
    /*! computed uniform scale factor for virtual canvas */
    float gui_scale;
    /*! horizontal letterbox offset for virtual canvas */
    float gui_offset_x;
    /*! vertical letterbox offset for virtual canvas */
    float gui_offset_y;
    /*! 1 if global scrollbar vertical thumb is being dragged */
    int global_vscroll_drag;
    /*! 1 if global scrollbar horizontal thumb is being dragged */
    int global_hscroll_drag;
    /*! configurable style (sizes, colours, paddings) */
    N_GUI_STYLE style;
    /*! display pointer for clipboard operations (set via n_gui_set_display) */
    ALLEGRO_DISPLAY* display;
    /*! id of the label widget with the most recent text selection, or -1 */
    int selected_label_id;
    /*! context resize mode: N_GUI_RESIZE_VIRTUAL or N_GUI_RESIZE_ADAPTIVE */
    int resize_mode;
    /*! reference display width at the time normalized values were last captured */
    float ref_display_w;
    /*! reference display height at the time normalized values were last captured */
    float ref_display_h;
    /*! default theme for titlebar minimize/maximize buttons */
    N_GUI_THEME default_tb_btn_theme;
    /*! default theme for titlebar close button */
    N_GUI_THEME default_tb_close_theme;
    /*! layout entries for windows that did NOT exist when the saved layout
     *  was loaded (lazily-created panels). n_gui_add_window applies the
     *  saved (x,y) to the first window created later with a matching title,
     *  giving cross-session position restore without eager-creating every
     *  window. Owned by the ctx; freed in n_gui_destroy_ctx. */
    N_GUI_PENDING_GEOM* pending_layout;
    /*! number of valid entries in pending_layout */
    int pending_layout_count;
} N_GUI_CTX;

/* API */

/* style helpers */

/*! return the built-in default style */
N_GUI_STYLE n_gui_default_style(void);

/* theme helpers */

/*! return the built-in default theme */
N_GUI_THEME n_gui_default_theme(void);
/*! @brief return the built-in default theme for titlebar minimize/maximize buttons */
N_GUI_THEME n_gui_default_tb_btn_theme(void);
/*! @brief return the built-in default theme for the titlebar close button (red hover/active) */
N_GUI_THEME n_gui_default_tb_close_theme(void);
/*! create a custom theme from individual colors and properties */
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
    float corner_ry);

/**
 * @brief Set the default theme on a context and sync scrollbar style colors.
 *
 * Sets ctx->default_theme and derives scrollbar track/thumb colors from
 * the theme's bg_normal and border_normal fields so scrollbars match the
 * active theme automatically.
 *
 * @param ctx   N_GUI context. Must not be NULL.
 * @param theme Theme to apply as the new default.
 */
void n_gui_set_default_theme(N_GUI_CTX* ctx, N_GUI_THEME theme);

/* text measurement */

/*! @brief get the bounding box dimensions of text rendered with the given font.
 *  Uses al_get_text_dimensions for accurate measurement including glyph overhangs. */
N_GUI_TEXT_DIMS n_gui_get_text_dims(ALLEGRO_FONT* font, const char* text);

/* context */

/*! create a new GUI context */
N_GUI_CTX* n_gui_new_ctx(ALLEGRO_FONT* default_font);
/*! destroy a GUI context and free all resources */
void n_gui_destroy_ctx(N_GUI_CTX** ctx);

/* window management */

/*! add a window to the GUI context */
int n_gui_add_window(N_GUI_CTX* ctx, const char* title, float x, float y, float w, float h);
/*! add a window that sizes itself automatically to fit its widgets (call n_gui_window_autosize after adding all widgets) */
int n_gui_add_window_auto(N_GUI_CTX* ctx, const char* title, float x, float y);
/*! get a window by id */
N_GUI_WINDOW* n_gui_get_window(N_GUI_CTX* ctx, int window_id);
/*! close (hide) a window */
void n_gui_close_window(N_GUI_CTX* ctx, int window_id);
/*! open (show) a window */
void n_gui_open_window(N_GUI_CTX* ctx, int window_id);
/*! toggle window visibility (show/hide) */
void n_gui_toggle_window(N_GUI_CTX* ctx, int window_id);
/*! check if window is visible */
int n_gui_window_is_open(N_GUI_CTX* ctx, int window_id);
/*! minimize a window (title bar only) */
void n_gui_minimize_window(N_GUI_CTX* ctx, int window_id);
/*! @brief toggle maximised state (full display or restore to previous size) */
void n_gui_maximize_window(N_GUI_CTX* ctx, int window_id);
/*! @brief check if a window is currently maximised */
int n_gui_window_is_maximised(N_GUI_CTX* ctx, int window_id);
/*! @brief set the close button callback for a window (NULL = default: hide window)
 * @param ctx the GUI context
 * @param window_id the target window
 * @param on_close callback receiving (window_id, user_data). Must call n_gui_close_window() itself if desired.
 * @param user_data user data passed to the callback */
void n_gui_window_set_close_callback(N_GUI_CTX* ctx, int window_id, void (*on_close)(int, void*), void* user_data);
/*! @brief set a content-draw callback for a window, invoked during n_gui_draw
 *  right after the window's own content is drawn (at the window's z-order)
 * @param ctx the GUI context
 * @param window_id the target window
 * @param on_content_draw callback receiving (window_id, user_data); NULL clears it
 * @param user_data user data passed to the callback */
void n_gui_window_set_content_draw_callback(N_GUI_CTX* ctx, int window_id, void (*on_content_draw)(int, void*), void* user_data);
/*! @brief set the minimize button callback for a window (NULL = default: toggle minimised)
 * @param ctx the GUI context
 * @param window_id the target window
 * @param on_minimize callback receiving (window_id, user_data)
 * @param user_data user data passed to the callback */
void n_gui_window_set_minimize_callback(N_GUI_CTX* ctx, int window_id, void (*on_minimize)(int, void*), void* user_data);
/*! @brief set the maximize button callback for a window (NULL = default: toggle maximised)
 * @param ctx the GUI context
 * @param window_id the target window
 * @param on_maximize callback receiving (window_id, user_data)
 * @param user_data user data passed to the callback */
void n_gui_window_set_maximize_callback(N_GUI_CTX* ctx, int window_id, void (*on_maximize)(int, void*), void* user_data);
/*! @brief set the theme for titlebar minimize/maximize buttons on a specific window */
void n_gui_window_set_tb_btn_theme(N_GUI_CTX* ctx, int window_id, N_GUI_THEME theme);
/*! @brief set the theme for the titlebar close button on a specific window */
void n_gui_window_set_tb_close_theme(N_GUI_CTX* ctx, int window_id, N_GUI_THEME theme);
/*! @brief set optional bitmap overlays on a titlebar button
 * @param ctx the GUI context
 * @param window_id the target window
 * @param btn_type N_GUI_TB_BTN_MINIMIZE, N_GUI_TB_BTN_MAXIMIZE, or N_GUI_TB_BTN_CLOSE
 * @param normal bitmap for normal state (NULL = color theme)
 * @param hover bitmap for hover state (NULL = fallback to normal)
 * @param active bitmap for active/pressed state (NULL = fallback to normal) */
void n_gui_window_set_tb_button_bitmaps(N_GUI_CTX* ctx, int window_id, int btn_type, ALLEGRO_BITMAP* normal, ALLEGRO_BITMAP* hover, ALLEGRO_BITMAP* active);
/*! raise a window to the top of the stack (respects z-order constraints) */
void n_gui_raise_window(N_GUI_CTX* ctx, int window_id);
/*! lower a window to the bottom of the stack (respects z-order constraints) */
void n_gui_lower_window(N_GUI_CTX* ctx, int window_id);
/*! get the id of the frontmost open, non-minimized window (the last one in
 *  draw order), or -1 if no window is open. Because the window list is kept
 *  sorted by z-order group, this is the topmost window currently on screen. */
int n_gui_get_topmost_open_window(const N_GUI_CTX* ctx);
/*! give a window keyboard focus: raise it within its z-order group and focus
 *  its first focusable widget (clears focus if it has none) */
void n_gui_focus_window(N_GUI_CTX* ctx, int window_id);
/*! set window z-order mode and value
 * @param ctx the GUI context
 * @param window_id the window to configure
 * @param z_mode one of N_GUI_ZORDER_NORMAL, N_GUI_ZORDER_ALWAYS_ON_TOP, N_GUI_ZORDER_ALWAYS_BEHIND, N_GUI_ZORDER_FIXED, N_GUI_ZORDER_POPUP
 * @param z_value z-value for N_GUI_ZORDER_FIXED mode (lower = behind, higher = on top), ignored for other modes */
void n_gui_window_set_zorder(N_GUI_CTX* ctx, int window_id, int z_mode, int z_value);
/*! get window z-order mode */
int n_gui_window_get_zorder(N_GUI_CTX* ctx, int window_id);
/*! get window z-value (for N_GUI_ZORDER_FIXED mode) */
int n_gui_window_get_zvalue(N_GUI_CTX* ctx, int window_id);
/*! set feature flags on a window (N_GUI_WIN_AUTO_SCROLLBAR, N_GUI_WIN_RESIZABLE, ...)
 * @param ctx the GUI context
 * @param window_id id of the target window
 * @param flags bitmask of N_GUI_WIN_* flags to set */
void n_gui_window_set_flags(N_GUI_CTX* ctx, int window_id, int flags);
/*! get feature flags of a window */
int n_gui_window_get_flags(N_GUI_CTX* ctx, int window_id);
/*! recompute and apply minimum-fit size for a window based on its current widgets */
void n_gui_window_autosize(N_GUI_CTX* ctx, int window_id);
/*! configure auto-fit behavior for a dialog window
 * @param ctx the GUI context
 * @param window_id the window to configure
 * @param autofit_flags bitmask of N_GUI_AUTOFIT_W, N_GUI_AUTOFIT_H, N_GUI_AUTOFIT_EXPAND_LEFT, N_GUI_AUTOFIT_EXPAND_UP
 * @param border padding in pixels between content bounding box and window edge (on each side) */
void n_gui_window_set_autofit(N_GUI_CTX* ctx, int window_id, int autofit_flags, float border);
/*! trigger auto-fit recalculation for a window (call after adding/removing/resizing widgets)
 * @param ctx the GUI context
 * @param window_id the window to recalculate */
void n_gui_window_apply_autofit(N_GUI_CTX* ctx, int window_id);

/* resize mode */

/*! @brief set context-level resize mode: N_GUI_RESIZE_VIRTUAL (default) or N_GUI_RESIZE_ADAPTIVE */
void n_gui_set_resize_mode(N_GUI_CTX* ctx, int mode);

/*! @brief get current resize mode */
int n_gui_get_resize_mode(N_GUI_CTX* ctx);

/*! @brief set per-window resize policy (N_GUI_WIN_RESIZE_NONE / _MOVE / _SCALE).
 *  Automatically captures normalized coordinates from the window's current
 *  position/size relative to the current display size. */
void n_gui_window_set_resize_policy(N_GUI_CTX* ctx, int window_id, int policy);

/*! @brief get per-window resize policy */
int n_gui_window_get_resize_policy(N_GUI_CTX* ctx, int window_id);

/*! @brief recapture normalized coordinates for a window from its current absolute
 *  position/size. Call after programmatically moving/resizing a window if you
 *  want the new position to be the adaptive reference point. */
void n_gui_window_update_normalized(N_GUI_CTX* ctx, int window_id);

/*! @brief apply adaptive resize: reposition/resize all windows according to their
 *  policies for the new display dimensions. Called automatically from
 *  n_gui_set_display_size() when in ADAPTIVE mode, but can also be called
 *  manually. */
void n_gui_apply_adaptive_resize(N_GUI_CTX* ctx, float new_w, float new_h);

/* widget creation (returns widget id) */

/*! add a button widget */
int n_gui_add_button(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, int shape, void (*on_click)(int, void*), void* user_data);

/*! add a bitmap-based button widget */
int n_gui_add_button_bitmap(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, ALLEGRO_BITMAP* normal, ALLEGRO_BITMAP* hover, ALLEGRO_BITMAP* active, void (*on_click)(int, void*), void* user_data);

/*! toggle button creation (returns widget id) */
int n_gui_add_toggle_button(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, int shape, int initial_state, void (*on_click)(int, void*), void* user_data);

/*! check if button is toggled */
int n_gui_button_is_toggled(N_GUI_CTX* ctx, int widget_id);

/*! replace the label text drawn on a button */
void n_gui_button_set_label(N_GUI_CTX* ctx, int widget_id, const char* label);
/*! skin a button or toggle button with caller-owned per-state bitmaps (switches its shape to N_GUI_SHAPE_BITMAP; toggled-on renders `active`) */
void n_gui_button_set_state_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* normal, ALLEGRO_BITMAP* hover, ALLEGRO_BITMAP* active);
/*! set (or clear with "" / NULL) the tooltip text shown after the pointer rests on a widget */
void n_gui_set_widget_tooltip(N_GUI_CTX* ctx, int widget_id, const char* text);
/*! tooltip phase: 0 = none (or tooltips disabled via style.tooltip_delay <= 0), 1 = arming (hosts with cached GUI renders must keep redrawing so the bubble can appear on time), 2 = shown (static - re-render once on the transition, then stop) */
int n_gui_tooltip_pending(N_GUI_CTX* ctx);
/*! set button toggled state */
void n_gui_button_set_toggled(N_GUI_CTX* ctx, int widget_id, int toggled);
/*! set button toggle mode */
void n_gui_button_set_toggle_mode(N_GUI_CTX* ctx, int widget_id, int toggle_mode);

/* button keyboard binding */
/*! bind a keyboard key with optional modifier requirements to a button.
 *  When the key is pressed (with matching modifiers) and no text input widget
 *  has focus, the button's on_click callback is triggered.
 *  Pass keycode 0 to unbind. Use ALLEGRO_KEY_* constants for the keycode.
 *  When modifiers is 0, any modifier state matches (no modifier requirement).
 *  When modifiers is non-zero, only an exact match of the specified modifiers
 *  (masked by N_GUI_KEY_MOD_MASK) triggers the keybind.
 *  Example: n_gui_button_set_keycode(ctx, id, ALLEGRO_KEY_A, ALLEGRO_KEYMOD_SHIFT | ALLEGRO_KEYMOD_CTRL)
 *  @param ctx GUI context
 *  @param widget_id button widget id
 *  @param keycode ALLEGRO_KEY_* constant, or 0 to unbind
 *  @param modifiers ALLEGRO_KEYMOD_* flags ORed together, or 0 for no requirement */
void n_gui_button_set_keycode(N_GUI_CTX* ctx, int widget_id, int keycode, int modifiers);

/*! bind a keyboard key to a button that fires only when specific widgets have focus.
 *  Unlike n_gui_button_set_keycode (global), this binding triggers only when the
 *  button itself or one of the listed source widgets currently has focus.
 *  This allows e.g. ENTER in a URL textarea to trigger a Send button without
 *  interfering with ENTER in other textareas.
 *  @param ctx GUI context
 *  @param widget_id button widget id
 *  @param keycode ALLEGRO_KEY_* constant, or 0 to unbind
 *  @param modifiers ALLEGRO_KEYMOD_* flags ORed together, or 0 for no requirement
 *  @param sources array of widget IDs whose focus can trigger this binding
 *  @param source_count number of entries in sources (max N_GUI_KEY_SOURCES_MAX) */
void n_gui_button_set_keycode_focused(N_GUI_CTX* ctx, int widget_id, int keycode, int modifiers, const int* sources, int source_count);

/*! add a horizontal slider widget */
int n_gui_add_slider(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, double min_val, double max_val, double initial, int mode, void (*on_change)(int, double, void*), void* user_data);

/*! add a vertical slider (same as n_gui_add_slider but oriented vertically) */
int n_gui_add_vslider(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, double min_val, double max_val, double initial, int mode, void (*on_change)(int, double, void*), void* user_data);

/*! add a text area widget */
int n_gui_add_textarea(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int multiline, size_t char_limit, void (*on_change)(int, const char*, void*), void* user_data);

/*! add a checkbox widget */
int n_gui_add_checkbox(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, int initial_checked, void (*on_toggle)(int, int, void*), void* user_data);

/*! add a scrollbar widget */
int n_gui_add_scrollbar(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int orientation, int shape, double content_size, double viewport_size, void (*on_scroll)(int, double, void*), void* user_data);

/* widget access */

/*! get a widget by id */
N_GUI_WIDGET* n_gui_get_widget(N_GUI_CTX* ctx, int widget_id);
/*! set the theme on a widget */
void n_gui_set_widget_theme(N_GUI_CTX* ctx, int widget_id, N_GUI_THEME theme);
/*! reset all widget and window themes to ctx->default_theme */
void n_gui_reset_all_widget_themes(N_GUI_CTX* ctx);
/*! set widget visibility */
void n_gui_set_widget_visible(N_GUI_CTX* ctx, int widget_id, int visible);
/*! set widget enabled state (0 = disabled: drawn dimmed and ignores all input) */
void n_gui_set_widget_enabled(N_GUI_CTX* ctx, int widget_id, int enabled);
/*! check if a widget is enabled */
int n_gui_is_widget_enabled(N_GUI_CTX* ctx, int widget_id);
/*! set keyboard focus to a specific widget (-1 to clear focus)
 * @param ctx the GUI context
 * @param widget_id the widget to focus (-1 to clear focus) */
void n_gui_set_focus(N_GUI_CTX* ctx, int widget_id);

/* slider helpers */
/*! get slider current value */
double n_gui_slider_get_value(N_GUI_CTX* ctx, int widget_id);
/*! set slider value */
void n_gui_slider_set_value(N_GUI_CTX* ctx, int widget_id, double value);
/*! set slider min/max range, clamping the current value if needed */
void n_gui_slider_set_range(N_GUI_CTX* ctx, int widget_id, double min_val, double max_val);
/*! set slider step increment (0 is treated as 1). Value is re-snapped. */
void n_gui_slider_set_step(N_GUI_CTX* ctx, int widget_id, double step);
/*! Override the built-in value readout's printf format string. Pass
 *  NULL or "" to restore the default ("%.1f" in VALUE mode, "%.0f%%"
 *  in PERCENT mode). Format must consume the slider's `double value`
 *  (use %f/%g/%e or cast with a precision width, a single %d works
 *  too via stringification fallbacks). Buffer cap is 32 bytes. */
void n_gui_slider_set_value_format(N_GUI_CTX* ctx, int widget_id, const char* fmt);
/*! Toggle the built-in value label. Default 1 (visible). Set to 0
 *  when the caller is already painting the value elsewhere so the
 *  slider doesn't double-render it. */
void n_gui_slider_set_value_visible(N_GUI_CTX* ctx, int widget_id, int visible);

/* textarea helpers */
/*! get text area content */
const char* n_gui_textarea_get_text(N_GUI_CTX* ctx, int widget_id);
/*! set text area content */
void n_gui_textarea_set_text(N_GUI_CTX* ctx, int widget_id, const char* text);
/*! scroll a multiline textarea to the bottom */
void n_gui_textarea_scroll_to_bottom(N_GUI_CTX* ctx, int widget_id);
/*! set the text selection range (byte offsets into text content) */
void n_gui_textarea_set_selection(N_GUI_CTX* ctx, int widget_id, size_t start, size_t end);
/*! scroll a multiline textarea so that the given byte offset is vertically centered */
void n_gui_textarea_scroll_to_offset(N_GUI_CTX* ctx, int widget_id, size_t byte_offset);
/*! return the current text length in bytes (0 if widget is invalid) */
size_t n_gui_textarea_get_text_length(N_GUI_CTX* ctx, int widget_id);

/* checkbox helpers */
/*! check if checkbox is checked */
int n_gui_checkbox_is_checked(N_GUI_CTX* ctx, int widget_id);
/*! set checkbox checked state */
void n_gui_checkbox_set_checked(N_GUI_CTX* ctx, int widget_id, int checked);

/* scrollbar helpers */
/*! get scrollbar position */
double n_gui_scrollbar_get_pos(N_GUI_CTX* ctx, int widget_id);
/*! set scrollbar position */
void n_gui_scrollbar_set_pos(N_GUI_CTX* ctx, int widget_id, double pos);
/*! set scrollbar content and viewport sizes */
void n_gui_scrollbar_set_sizes(N_GUI_CTX* ctx, int widget_id, double content_size, double viewport_size);

/* listbox */

/*! add a listbox widget */
int n_gui_add_listbox(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, int selection_mode, void (*on_select)(int, int, int, void*), void* user_data);
/*! add an item to a listbox */
int n_gui_listbox_add_item(N_GUI_CTX* ctx, int widget_id, const char* text);
/*! remove an item from a listbox by index */
int n_gui_listbox_remove_item(N_GUI_CTX* ctx, int widget_id, int index);
/*! clear all items from a listbox */
void n_gui_listbox_clear(N_GUI_CTX* ctx, int widget_id);
/*! get number of items in a listbox */
int n_gui_listbox_get_count(N_GUI_CTX* ctx, int widget_id);
/*! get item text by index */
const char* n_gui_listbox_get_item_text(N_GUI_CTX* ctx, int widget_id, int index);
/*! get the first selected item index */
int n_gui_listbox_get_selected(N_GUI_CTX* ctx, int widget_id);
/*! check if an item is selected */
int n_gui_listbox_is_selected(N_GUI_CTX* ctx, int widget_id, int index);
/*! set item selection state */
void n_gui_listbox_set_selected(N_GUI_CTX* ctx, int widget_id, int index, int selected);
/*! get the current scroll offset (in items) */
int n_gui_listbox_get_scroll_offset(N_GUI_CTX* ctx, int widget_id);
/*! set the scroll offset (in items), clamps to valid range */
void n_gui_listbox_set_scroll_offset(N_GUI_CTX* ctx, int widget_id, int offset);

/* radiolist */

/*! add a radio list widget */
int n_gui_add_radiolist(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, void (*on_select)(int, int, void*), void* user_data);
/*! add an item to a radio list */
int n_gui_radiolist_add_item(N_GUI_CTX* ctx, int widget_id, const char* text);
/*! remove all items from a radio list */
void n_gui_radiolist_clear(N_GUI_CTX* ctx, int widget_id);
/*! get the selected item index */
int n_gui_radiolist_get_selected(N_GUI_CTX* ctx, int widget_id);
/*! set the selected item index */
void n_gui_radiolist_set_selected(N_GUI_CTX* ctx, int widget_id, int index);

/* combobox */

/*! add a combo box widget */
int n_gui_add_combobox(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, void (*on_select)(int, int, void*), void* user_data);
/*! add an item to a combo box */
int n_gui_combobox_add_item(N_GUI_CTX* ctx, int widget_id, const char* text);
/*! remove all items from a combobox */
void n_gui_combobox_clear(N_GUI_CTX* ctx, int widget_id);
/*! get the selected item index */
int n_gui_combobox_get_selected(N_GUI_CTX* ctx, int widget_id);
/*! get the text of the combobox item at `index` ("" if out of range / not a combobox) */
const char* n_gui_combobox_get_item_text(N_GUI_CTX* ctx, int widget_id, int index);
/*! set the selected item index */
void n_gui_combobox_set_selected(N_GUI_CTX* ctx, int widget_id, int index);

/*! set combobox feature flags (bitmask of N_GUI_COMBOBOX_* values) */
void n_gui_combobox_set_flags(N_GUI_CTX* ctx, int widget_id, int flags);

/* image */

/*! add an image widget */
int n_gui_add_image(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, ALLEGRO_BITMAP* bitmap, int scale_mode);
/*! set the bitmap on an image widget */
void n_gui_image_set_bitmap(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bitmap);

/* label (static text, optional hyperlink) */

/*! add a text label widget */
int n_gui_add_label(N_GUI_CTX* ctx, int window_id, const char* text, float x, float y, float w, float h, int align);
/*! add a text label with hyperlink */
int n_gui_add_label_link(N_GUI_CTX* ctx, int window_id, const char* text, const char* link, float x, float y, float w, float h, int align, void (*on_link_click)(int, const char*, void*), void* user_data);
/*! set label text content */
void n_gui_label_set_text(N_GUI_CTX* ctx, int widget_id, const char* text);
/*! set label hyperlink URL */
void n_gui_label_set_link(N_GUI_CTX* ctx, int widget_id, const char* link);

/* dropdown menu */

/*! add a dropdown menu widget (button that opens a menu panel with entries)
 * @param ctx the GUI context
 * @param window_id id of the parent window
 * @param label button label text
 * @param x horizontal position
 * @param y vertical position
 * @param w widget width
 * @param h widget height
 * @param on_open callback invoked when the menu is opened (or NULL)
 * @param on_open_user_data user data passed to on_open callback
 * @return widget id, or -1 on error */
int n_gui_add_dropmenu(N_GUI_CTX* ctx, int window_id, const char* label, float x, float y, float w, float h, void (*on_open)(int, void*), void* on_open_user_data);
/*! add a static entry to a dropdown menu */
int n_gui_dropmenu_add_entry(N_GUI_CTX* ctx, int widget_id, const char* text, int tag, void (*on_click)(int, int, int, void*), void* user_data);
/*! add a dynamic entry (rebuilt each time menu opens) */
int n_gui_dropmenu_add_dynamic_entry(N_GUI_CTX* ctx, int widget_id, const char* text, int tag, void (*on_click)(int, int, int, void*), void* user_data);
/*! remove all dynamic entries (call from on_open callback before re-adding) */
void n_gui_dropmenu_clear_dynamic(N_GUI_CTX* ctx, int widget_id);
/*! remove all entries */
void n_gui_dropmenu_clear(N_GUI_CTX* ctx, int widget_id);
/*! update text of an existing entry by index */
void n_gui_dropmenu_set_entry_text(N_GUI_CTX* ctx, int widget_id, int index, const char* text);
/*! get number of entries */
int n_gui_dropmenu_get_count(N_GUI_CTX* ctx, int widget_id);
/*! set dropmenu feature flags (bitmask of N_GUI_DROPMENU_* values) */
void n_gui_dropmenu_set_flags(N_GUI_CTX* ctx, int widget_id, int flags);

/* bitmap skinning */

/*! set optional bitmap overlays on a window's body and titlebar.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the window's lifetime and destroy them after the GUI context is destroyed.
 *  @param ctx the GUI context
 *  @param window_id id of the target window
 *  @param bg body background bitmap (NULL = color fill)
 *  @param titlebar titlebar background bitmap (NULL = color fill)
 *  @param bg_scale_mode scale mode for bg: N_GUI_IMAGE_FIT, N_GUI_IMAGE_STRETCH, or N_GUI_IMAGE_CENTER */
void n_gui_window_set_bitmaps(N_GUI_CTX* ctx, int window_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* titlebar, int bg_scale_mode);

/*! set optional bitmap overlays on a slider widget.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_slider_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* track, ALLEGRO_BITMAP* fill, ALLEGRO_BITMAP* handle, ALLEGRO_BITMAP* handle_hover, ALLEGRO_BITMAP* handle_active);

/*! set optional bitmap overlays on a scrollbar widget.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_scrollbar_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* track, ALLEGRO_BITMAP* thumb, ALLEGRO_BITMAP* thumb_hover, ALLEGRO_BITMAP* thumb_active);

/*! set optional bitmap overlays on a checkbox widget.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_checkbox_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* box, ALLEGRO_BITMAP* box_checked, ALLEGRO_BITMAP* box_hover);

/*! set optional background bitmap on a textarea widget.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_textarea_set_bitmap(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg);

/*! set a mask character for password-style input (e.g. '*').
 *  Set to '\0' to disable masking.  Only affects single-line textareas.
 *  @param ctx the GUI context
 *  @param widget_id id of the textarea widget */
void n_gui_textarea_set_mask_char(N_GUI_CTX* ctx, int widget_id, char mask);

/*! set optional bitmap overlays on a listbox widget.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_listbox_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* item_bg, ALLEGRO_BITMAP* item_selected);

/*! set optional bitmap overlays on a radiolist widget.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_radiolist_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* item_bg, ALLEGRO_BITMAP* item_selected);

/*! set optional bitmap overlays on a combobox widget.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_combobox_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg, ALLEGRO_BITMAP* item_bg, ALLEGRO_BITMAP* item_selected);

/*! set optional bitmap overlays on a dropmenu widget.
 *  Pass NULL for any bitmap you don't want to set.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_dropmenu_set_bitmaps(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* panel, ALLEGRO_BITMAP* item_hover);

/*! set optional background bitmap on a label widget.
 *  @note Bitmaps are NOT owned by N_GUI. The caller must keep them alive
 *        for the widget's lifetime and destroy them after the GUI context is destroyed. */
void n_gui_label_set_bitmap(N_GUI_CTX* ctx, int widget_id, ALLEGRO_BITMAP* bg);

/* JSON theme I/O */

/*! Save the full theme (default_theme + style) to a JSON file.
 *  Returns 0 on success, -1 on error. */
int n_gui_save_theme_json(N_GUI_CTX* ctx, const char* filepath);

/*! Load a JSON theme file and apply it to the context.
 *  Returns 0 on success, -1 on error. */
int n_gui_load_theme_json(N_GUI_CTX* ctx, const char* filepath);

/* JSON window layout I/O */

/*! Save the geometry + persistent state of every window in the context to a JSON file.
 *  Persists per-window: title, x, y, w, h, state (persistent bits only),
 *  z_order, z_value, flags, norm_x/y/w/h, autofit_flags, autofit_border,
 *  resize_policy. Widget content and callbacks are NOT saved, they are
 *  domain data owned by the calling program.
 *  Returns 0 on success, -1 on error. */
int n_gui_save_layout_json(N_GUI_CTX* ctx, const char* filepath);

/*! Load a JSON window layout file and apply it to matching windows in the context.
 *  Matching is by window title (IDs are not stable across runs). If multiple
 *  windows share a title, they are matched in creation order (first-seen-first-match).
 *  Unknown titles in the file are ignored (defensive for versions). Missing titles
 *  in the context are ignored (no-op for windows not yet created).
 *  A missing file is treated as a successful no-op (returns -1 with LOG_DEBUG).
 *  Returns 0 on success, -1 on error. */
int n_gui_load_layout_json(N_GUI_CTX* ctx, const char* filepath);

/* virtual canvas / display / DPI */

/*! set the virtual canvas size for resolution-independent scaling.
 *  When set (w > 0 && h > 0), all widget coordinates are in virtual space.
 *  An ALLEGRO_TRANSFORM is applied during draw and mouse input is reverse-
 *  transformed automatically. Pass (0, 0) to disable (identity transform).
 *  This is opt-in: the virtual canvas is enabled only when both w and h are > 0,
 *  and disabled when both are 0 (legacy behaviour). */
void n_gui_set_virtual_size(N_GUI_CTX* ctx, float w, float h);

/*! recalculate scale and offset from virtual canvas to display.
 *  Called automatically by n_gui_set_display_size when virtual canvas is set.
 *  You should not normally need to call this directly. */
void n_gui_update_transform(N_GUI_CTX* ctx);

/*! convert physical screen coordinates to virtual canvas coordinates.
 *  If virtual canvas is disabled, outputs equal inputs.
 *  @param ctx GUI context
 *  @param sx  screen X (physical pixels)
 *  @param sy  screen Y (physical pixels)
 *  @param vx  output virtual X (may be NULL)
 *  @param vy  output virtual Y (may be NULL) */
void n_gui_screen_to_virtual(const N_GUI_CTX* ctx, float sx, float sy, float* vx, float* vy);

/*! set the display (viewport) size so global scrollbars can be computed.
 *  Call this on startup and whenever the display is resized.
 *  When virtual canvas is enabled, also updates the virtual-to-display transform. */
void n_gui_set_display_size(N_GUI_CTX* ctx, float w, float h);
/*! set the display pointer for clipboard operations (copy/paste) */
void n_gui_set_display(N_GUI_CTX* ctx, ALLEGRO_DISPLAY* display);

/*! set DPI scale factor manually (default is 1.0).
 *  Affects global scrollbar rendering and can be queried by the application. */
void n_gui_set_dpi_scale(N_GUI_CTX* ctx, float scale);

/*! get current DPI scale factor */
float n_gui_get_dpi_scale(const N_GUI_CTX* ctx);

/*! detect and apply DPI scale from the given allegro display.
 *  Works on Linux (X11/Wayland), Windows, and Android.
 *  Returns the detected scale factor. */
float n_gui_detect_dpi_scale(N_GUI_CTX* ctx, ALLEGRO_DISPLAY* display);

/* event processing & drawing */

/*! process an allegro event for the GUI */
int n_gui_process_event(N_GUI_CTX* ctx, ALLEGRO_EVENT event);
/*! draw all visible GUI windows and widgets */
void n_gui_draw(N_GUI_CTX* ctx);
/*! check if the mouse is currently over any open GUI window (use to prevent
 *  click-through to the game world) */
int n_gui_wants_mouse(N_GUI_CTX* ctx);

/*! maximum number of tabs in a single tab panel */
#define N_GUI_TAB_MAX 16

/*! tab panel built from toggle buttons with content window management */
typedef struct N_GUI_TAB_PANEL {
    N_GUI_CTX* ctx;                        /*!< GUI context */
    int parent_window_id;                  /*!< parent window for tab buttons */
    int button_ids[N_GUI_TAB_MAX];         /*!< toggle button widget IDs */
    int content_window_ids[N_GUI_TAB_MAX]; /*!< content window IDs (-1 = none) */
    int nb_tabs;                           /*!< number of tabs */
    int active_tab;                        /*!< currently active tab index */
    float x;                               /*!< x origin of first tab button */
    float y;                               /*!< y origin of tab button row */
    float button_w;                        /*!< width of each tab button */
    float button_h;                        /*!< height of each tab button */
    void (*on_tab_change)(int, void*);     /*!< callback on tab switch (may be NULL) */
    void* user_data;                       /*!< user data for callback */
} N_GUI_TAB_PANEL;

/*! create a tab panel in an existing window */
N_GUI_TAB_PANEL* n_gui_tab_create(N_GUI_CTX* ctx, int window_id, float x, float y, float button_w, float button_h, void (*on_tab_change)(int, void*), void* user_data);
/*! add a tab to the panel, returns tab index */
int n_gui_tab_add(N_GUI_TAB_PANEL* panel, const char* label);
/*! associate a content window with a tab */
void n_gui_tab_set_content_window(N_GUI_TAB_PANEL* panel, int tab_index, int window_id);
/*! set the active tab (toggles buttons, opens/closes content windows) */
void n_gui_tab_set_active(N_GUI_TAB_PANEL* panel, int index);
/*! get the active tab index */
int n_gui_tab_get_active(N_GUI_TAB_PANEL* panel);
/*! free a tab panel (does not destroy the N_GUI widgets) */
void n_gui_tab_free(N_GUI_TAB_PANEL** panel);

/*! maximum number of foldable sections in a section list */
#define N_GUI_SECTIONLIST_MAX 16

/*! maximum number of member widgets per section */
#define N_GUI_SECTION_WIDGETS_MAX 48

/*! a single foldable section: a clickable header plus the member widgets it shows/hides */
typedef struct N_GUI_SECTION {
    char title[64];                             /*!< section title (header label minus the fold glyph) */
    int header_id;                              /*!< clickable header button widget id */
    int widget_ids[N_GUI_SECTION_WIDGETS_MAX];  /*!< member widget ids */
    float widget_dy[N_GUI_SECTION_WIDGETS_MAX]; /*!< each member's y offset from the section content top, captured at add time */
    int nb_widgets;                             /*!< number of member widgets */
    int folded;                                 /*!< 1 = collapsed (members hidden), 0 = expanded */
    float natural_h;                            /*!< stacked height of the members when expanded */
} N_GUI_SECTION;

/*! a vertical accordion of foldable sections inside one window; folding hides a
 *  section's widgets and re-stacks the rest, leaving the window's
 *  N_GUI_WIN_AUTO_SCROLLBAR to absorb any overflow */
typedef struct N_GUI_SECTIONLIST {
    N_GUI_CTX* ctx;                                /*!< GUI context */
    int window_id;                                 /*!< window holding the headers and members */
    float x;                                       /*!< content-local x of every header and the layout origin */
    float y0;                                      /*!< content-local y of the first header */
    float width;                                   /*!< header width */
    float header_h;                                /*!< header button height */
    float gap;                                     /*!< vertical gap between rows */
    N_GUI_SECTION sections[N_GUI_SECTIONLIST_MAX]; /*!< section storage */
    int nb_sections;                               /*!< number of sections */
    float first_inset;                             /*!< content-local left inset applied to the FIRST header only, so a button can sit before it on the same row (0 = none) */
    void (*on_toggle)(int, int, void*);            /*!< callback(section_index, folded, user_data) on a header click (may be NULL) */
    void* user_data;                               /*!< user data for the callback */
} N_GUI_SECTIONLIST;

/*! create an empty section list anchored at (x,y0) in window_id; sections stack downward from y0 */
N_GUI_SECTIONLIST* n_gui_sectionlist_create(N_GUI_CTX* ctx, int window_id, float x, float y0, float width, float header_h, float gap, void (*on_toggle)(int, int, void*), void* user_data);
/*! add a section with a header button at header_y (members added next are captured relative to it); returns the section index or -1 */
int n_gui_sectionlist_add_section(N_GUI_SECTIONLIST* sl, const char* title, float header_y, int folded);
/*! register an existing widget as a member of a section (captures its current y as the fold offset) */
void n_gui_sectionlist_add_widget(N_GUI_SECTIONLIST* sl, int section_index, int widget_id);
/*! set a left inset applied to the FIRST section's header only, so a caller can place a button before it on the same row; relayout re-applies it */
void n_gui_sectionlist_set_first_inset(N_GUI_SECTIONLIST* sl, float inset);
/*! re-stack all sections from y0 by current fold state: hide folded members, show and reposition expanded ones */
void n_gui_sectionlist_relayout(N_GUI_SECTIONLIST* sl);
/*! set a section's folded state and relayout; does NOT fire on_toggle (for programmatic restore) */
void n_gui_sectionlist_set_folded(N_GUI_SECTIONLIST* sl, int section_index, int folded);
/*! query a section's folded state (1 = folded, 0 = expanded) */
int n_gui_sectionlist_is_folded(const N_GUI_SECTIONLIST* sl, int section_index);
/*! total content height in pixels under the current fold state, for sizing/scrolling the host window */
float n_gui_sectionlist_content_height(const N_GUI_SECTIONLIST* sl);
/*! number of sections in the list */
int n_gui_sectionlist_get_count(const N_GUI_SECTIONLIST* sl);
/*! free the section list (does not destroy the N_GUI widgets or window) */
void n_gui_sectionlist_free(N_GUI_SECTIONLIST** sl);

/*! maximum number of nodes in a tree view */
#define N_GUI_TREE_MAX 2048

/*! a single node in the tree view */
typedef struct N_GUI_TREE_NODE {
    char label[128];  /*!< display text */
    int parent_index; /*!< parent node index, or -1 for root */
    int expanded;     /*!< 1 if expanded */
    int depth;        /*!< depth in tree (0 = root) */
    int has_children; /*!< 1 if node has children */
    void* user_data;  /*!< user data for this node */
} N_GUI_TREE_NODE;

/*! drop position relative to a tree row (drag-and-drop) */
#define N_GUI_DROP_BEFORE 0
#define N_GUI_DROP_INTO 1
#define N_GUI_DROP_AFTER 2

/*! drag-reorder drop callback.
 *  Called when the user drags node src_node and releases the mouse
 *  over dst_node at @p position (N_GUI_DROP_BEFORE / INTO / AFTER).
 *  Return 1 to accept; the caller is responsible for mutating the
 *  underlying data model and rebuilding the tree. Return 0 to reject
 *  (widget clears its drag state silently). */
typedef int (*n_gui_tree_on_drop_t)(int src_node, int dst_node, int position, void* user_data);

/*! tree view built from an N_GUI listbox */
typedef struct N_GUI_TREE {
    N_GUI_CTX* ctx;                        /*!< GUI context */
    int listbox_id;                        /*!< listbox widget ID */
    int window_id;                         /*!< owning window for coord math */
    N_GUI_TREE_NODE nodes[N_GUI_TREE_MAX]; /*!< node storage */
    int nb_nodes;                          /*!< number of nodes */
    int visible_map[N_GUI_TREE_MAX];       /*!< visible row to node index */
    int nb_visible;                        /*!< number of visible rows */
    void (*on_select)(int, void*);         /*!< selection callback */
    void* user_data;                       /*!< user data for callback */

    /*! drag-reorder state (managed by n_gui_tree_tick) */
    n_gui_tree_on_drop_t on_drop; /*!< drop callback, or NULL */
    void* drop_user_data;         /*!< user data for on_drop */
    int drag_source_node;         /*!< node being dragged, or -1 */
    int drag_armed_node;          /*!< pressed-but-not-yet-dragging */
    int drag_hover_node;          /*!< current hover node, or -1 */
    int drop_position;            /*!< N_GUI_DROP_BEFORE / INTO / AFTER */
    int drag_active;              /*!< 1 once past threshold */
    float drag_press_x;           /*!< mouse x at press time */
    float drag_press_y;           /*!< mouse y at press time */
    int mouse_b1_prev;            /*!< previous frame's b1 state */
} N_GUI_TREE;

/*! create a tree view in an existing window */
N_GUI_TREE* n_gui_tree_create(N_GUI_CTX* ctx, int window_id, float x, float y, float w, float h, void (*on_select)(int, void*), void* user_data);
/*! add a node to the tree */
int n_gui_tree_add_node(N_GUI_TREE* tree, const char* label, int parent_index, void* user_data);
/*! remove all nodes and clear the backing listbox; tree object remains valid */
void n_gui_tree_clear(N_GUI_TREE* tree);
/*! remove a node and its entire subtree; remaining node indices are
 *  compacted (callers holding indices across the call must discard or
 *  re-resolve them) */
void n_gui_tree_remove_node(N_GUI_TREE* tree, int node_index);
/*! change the display label of a node */
void n_gui_tree_set_label(N_GUI_TREE* tree, int node_index, const char* new_label);
/*! get the index of the currently selected node, or -1 when nothing
 *  is selected or the selection points at a hidden row */
int n_gui_tree_get_selection(N_GUI_TREE* tree);
/*! programmatically select a node; ancestors are expanded so the node
 *  is visible, and the listbox scrolls to reveal it. Does not fire
 *  the on_select callback. No-op if node_index is out of range. */
void n_gui_tree_set_selection(N_GUI_TREE* tree, int node_index);
/*! find the first node whose user_data pointer equals ptr, or -1.
 *  Used to re-resolve a node index after a rebuild compacted the
 *  array: the caller stashes the stable user_data pointer before
 *  the rebuild, then calls this to find the new index. */
int n_gui_tree_find_by_user_data(const N_GUI_TREE* tree, const void* ptr);
/*! toggle expand/collapse of a node */
void n_gui_tree_toggle_expand(N_GUI_TREE* tree, int node_index);
/*! rebuild the listbox to reflect current tree state */
void n_gui_tree_rebuild(N_GUI_TREE* tree);
/*! free a tree view (does not destroy the N_GUI listbox) */
void n_gui_tree_free(N_GUI_TREE** tree);

/*! install (or clear, with NULL) a drag-reorder drop callback.
 *  Drag state is only tracked when on_drop is non-NULL. */
void n_gui_tree_set_on_drop(N_GUI_TREE* tree,
                            n_gui_tree_on_drop_t on_drop,
                            void* user_data);

/*! per-frame poll: reads ctx->mouse_* to track drag state and fires
 *  the on_drop callback on release. Call once per frame while the
 *  tree is visible, before n_gui_draw(). No-op when on_drop is NULL. */
void n_gui_tree_tick(N_GUI_TREE* tree);

/*! per-frame overlay drawing: draws the active drop-indicator on top
 *  of the listbox. Call after n_gui_draw(). No-op when no drag is
 *  in progress. */
void n_gui_tree_draw_overlay(N_GUI_TREE* tree);

/*! maximum number of rows in a KV table */
#define N_GUI_KV_MAX 128

/*! a single row in the KV table */
typedef struct N_GUI_KV_ROW {
    int key_id;     /*!< textarea widget ID for the key */
    int value_id;   /*!< textarea widget ID for the value */
    int desc_id;    /*!< textarea widget ID for the description */
    int enabled_id; /*!< checkbox widget ID for the enabled toggle */
    int remove_id;  /*!< button widget ID for the remove action */
    int active;     /*!< 1 if active, 0 if removed */
} N_GUI_KV_ROW;

/*! key-value table built from textareas, checkboxes, and buttons */
typedef struct N_GUI_KVTABLE {
    N_GUI_CTX* ctx;                  /*!< GUI context */
    int window_id;                   /*!< parent window */
    N_GUI_KV_ROW rows[N_GUI_KV_MAX]; /*!< row storage */
    int nb_rows;                     /*!< total rows (including removed) */
    int nb_active;                   /*!< active row count */
    float row_height;                /*!< unscaled row height in pixels */
    float padding;                   /*!< unscaled padding in pixels */
    float header_height;             /*!< unscaled column-header row height */
    float init_win_w;                /*!< parent window width at create time */
    float init_win_h;                /*!< parent window height at create time */
    int lbl_key;                     /*!< label widget ID for "Key" header */
    int lbl_value;                   /*!< label widget ID for "Value" header */
    int lbl_desc;                    /*!< label widget ID for "Description" header */
    int lbl_enabled;                 /*!< label widget ID for "On" header */
    int btn_add;                     /*!< button widget ID for "+" add row */
    void (*on_remove)(int, void*);   /*!< callback on row removal */
    void* user_data;                 /*!< user data for callback */
} N_GUI_KVTABLE;

/*! create a KV table in an existing window */
N_GUI_KVTABLE* n_gui_kvtable_create(N_GUI_CTX* ctx, int window_id, float row_height, float padding, void (*on_remove)(int, void*), void* user_data);
/*! add a row to the KV table */
int n_gui_kvtable_add_row(N_GUI_KVTABLE* table,
                          const char* key,
                          const char* value,
                          const char* description,
                          int enabled);
/*! remove a row by index (hides widgets, marks inactive) */
void n_gui_kvtable_remove_row(N_GUI_KVTABLE* table, int row_index);
/*! get the number of active rows */
int n_gui_kvtable_get_count(N_GUI_KVTABLE* table);
/*! re-apply layout to all KV table widgets (positions, sizes, normalized
 *  coords). Called automatically on create/add/remove; callers should
 *  invoke it after any manual window size change the kvtable cannot see. */
void n_gui_kvtable_relayout(N_GUI_KVTABLE* table);
/*! free a KV table (does not destroy the N_GUI widgets) */
void n_gui_kvtable_free(N_GUI_KVTABLE** table);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_GUI__ */
