/**
 *@file n_gui.h
 *@brief GUI headers
 *@author Castagnier Mickael
 *@version 1.0
 *@date 15/06/2024
 */

#ifndef __NILOREA_GUI__
#define __NILOREA_GUI__

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup GUI GUI: macros, headers, defines, timers, allocators,...
  @addtogroup GUI
  @{
  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/*! maximum size of a single token content */
#define N_GUI_MAX_TOKEN_SIZE 1024

#include <nilorea/n_allegro5.h>
#include <nilorea/n_common.h>
#include <nilorea/n_str.h>
#include <nilorea/n_log.h>
#include <nilorea/n_list.h>
#include <nilorea/n_hash.h>
#include <nilorea/n_trees.h>

/*! N_GUI_CTX codes definition */
#define N_ENUM_N_GUI_TOKEN(_)     \
    _(N_GUI_TOKEN_TAG_OPEN, 2)    \
    _(N_GUI_TOKEN_TAG_CLOSE, 4)   \
    _(N_GUI_TOKEN_TAG_NAME, 8)    \
    _(N_GUI_TOKEN_ATTR_NAME, 16)  \
    _(N_GUI_TOKEN_ATTR_VALUE, 32) \
    _(N_GUI_TOKEN_TEXT, 64)       \
    _(N_GUI_TOKEN_EOF, 128)

/*! Network codes declaration */
N_ENUM_DECLARE(N_ENUM_N_GUI_TOKEN, __N_GUI_TOKEN);

/*! structure of a N_GUI_DIALOG or N_GUI_WIDGET layout */
typedef struct N_GUI_LAYOUT {
    size_t
        /*! minimum width */
        min_width,
        /*! maximum width */
        max_width,
        /*! minimum height */
        min_height,
        /*! maximum height */
        max_height,
        /*! horizontal align type */
        horizontal_align,
        /*! vertical align type */
        vertical_align,
        /*! font size */
        font_size,
        /*! font style */
        font_style,
        /*! margin top */
        margin_top,
        /*! margin bottom */
        margin_bottom,
        /*! margin left */
        margin_left,
        /*! margin right */
        margin_right;

    /*! background color */
    ALLEGRO_COLOR background_color;

    N_STR
    /*! font file string */
    *font,
        /*! background image file string */
        *background_image;
} N_GUI_LAYOUT;

/*! structure of a N_GUI_DIALOG widget */
typedef struct N_GUI_WIDGET {
    /*! layout of the widget */
    N_GUI_LAYOUT layout;
    /*! type of widget */
    size_t type;
    size_t
        /*! computed x position */
        x,
        /*! computed y position */
        y,
        /*! computed width */
        w,
        /*! computed height */
        h;
} N_GUI_WIDGET;

/*! structure of a N_GUI_DIALOG */
typedef struct N_GUI_DIALOG {
    /*! tree of widgets */
    TREE* tree;
    /*! hash table of styles */
    HASH_TABLE* styles;
} N_GUI_DIALOG;

/*! structure of a N_GUI_TOKENIZER token */
typedef struct N_GUI_TOKEN {
    /*! type of token */
    size_t type;
    /*! token content */
    char value[N_GUI_MAX_TOKEN_SIZE];
} N_GUI_TOKEN;

/*! structure of a N_GUI_DIALOG tokenizer */
typedef struct N_GUI_TOKENIZER {
    /*! pointer to the input text */
    const char* input;
    /*! position in the input text */
    size_t position;
} N_GUI_TOKENIZER;

// Function prototypes
void n_gui_init_tokenizer(N_GUI_TOKENIZER* tokenizer, const char* input);
char n_gui_next_char(N_GUI_TOKENIZER* tokenizer);
char n_gui_peek_char(N_GUI_TOKENIZER* tokenizer);
int n_gui_is_eof(N_GUI_TOKENIZER* tokenizer);
N_GUI_TOKEN n_gui_next_token(N_GUI_TOKENIZER* tokenizer, bool* inside_open_tag);
void n_gui_print_token(N_GUI_TOKEN token);
N_GUI_DIALOG* n_gui_load_dialog(char* html, char* css);

#ifdef __cplusplus
}
#endif
/**
  @}
  */

#endif  // header guard
