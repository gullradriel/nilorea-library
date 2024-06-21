/**\file n_gui.h
 * GUI headers 
 *\author Castagnier Mickael
 *\version 1.0
 *\date 15/06/2024
 */

#ifndef __NILOREA_GUI__
#define __NILOREA_GUI__

#ifdef __cplusplus
extern "C"
{
#endif

    /**\defgroup GUI GUI: macros, headers, defines, timers, allocators,...
      \addtogroup GUI
      @{
      */

#include <nilorea/n_allegro5.h>
#include <nilorea/n_common.h>
#include <nilorea/n_str.h>
#include <nilorea/n_log.h>
#include <nilorea/n_list.h>
#include <nilorea/n_hash.h>

typedef struct N_GUI_PROPS
{
    size_t 
        min_width,
        max_width,
        min_height,
        max_height,
        horizontal_align,
        vertical_align,
        font_size,
        font_style,
        margin_top,
        margin_bottom,
        margin_left,
        margin_right;

    ALLEGRO_COLOR background_color;
    
    N_STR 
        font,
        background_image;
} N_GUI_PROPS;

typedef struct N_GUI_WIDGET
{
    
}N_GUI_WIDGET;

typedef struct N_GUI_WIDGET_GROUP
{
    
}N_GUI_WIDGET_GROUP;

typedef struct N_GUI_DIALOG
{
    LIST *groups ;

}N_GUI_DIALOG;

#ifdef __cplusplus
}
#endif

#endif // header guard
