/**\file n_gui.h
*
*  GUI header for Nilorea/Allegro5
*
*\author Castagnier Mickaël aka Gull Ra Driel
*
*\version 1.0
*
*\date 03/01/2019
*
*/



#ifndef __N_GUI_HEADER
#define __N_GUI_HEADER

#ifdef __cplusplus
extern "C" {
#endif


/**\defgroup N_GUI N_GUI : N_GUI system
   \addtogroup N_GUI
  @{
*/

#include <allegro5/allegro.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>


#include "n_common.h"
#include "n_str.h"
#include "n_list.h"
#include "n_hash.h"
#include "n_resources.h"


/* NGUI GlOBAL CONSTANTS */
/*! NB states */
#define NGUI_NBSTATES 4

/*! dialog item type */
#define NGUI_TYPE_DIALOG 0
/*! button item type */
#define NGUI_TYPE_BUTTON 1
/*! menu item type */
#define NGUI_TYPE_MENU 2

/*! dialog hash table size */
#define NGUI_DIALOG_ITEM_HASH_SIZE 128
/*! item hash table size */
#define NGUI_DIALOG_LIST_HASH_SIZE 32

/* NGUI ALIGNEMENT CONSTANTS */
/*! align element to left */
#define NGUI_ALIGN_LEFT   0
/*! align element to top */
#define NGUI_ALIGN_TOP    1
/*! align element to center */
#define NGUI_ALIGN_CENTER 2
/*! align element to right */
#define NGUI_ALIGN_RIGHT  3
/*! align element to bottom */
#define NGUI_ALIGN_BOTTOM 4

/* NGUI STATE CONSTANTS */
/*! normal state */
#define NGUI_NORMAL   0
/*! on hover state */
#define NGUI_ACTIVE   1
/*! on click or key press */
#define NGUI_PRESSED  2
/*! disabled item */
#define NGUI_DISABLED 3

/* NGUI ACTIONS FLAGS */
/*! when entering a dialog */
#define NGUI_ON_ENTER     0
/*! when leaving a dialog */
#define NGUI_ON_LEAVE     1
/*! when pressing action button */
#define NGUI_ON_PRESS     2
/*! when releasing action button */
#define NGUI_ON_RELEASE
/*! when actioning an item */
#define NGUI_ON_FIRE      4
/*! on mouse move */
#define NGUI_ON_MOVE      5
/*! on mouse drag */
#define NGUI_ON_DRAG      6
/*! when drawing */
#define NGUI_ON_DRAW      7
/*! when draw done */
#define NGUI_ON_DRAW_DONE 8

/*! Dialogs using mouse */
#define NGUI_MOUSE    1
/*! Dialogs using keyboard */
#define NGUI_KEYBOARD 2
/*! Dialogs using mouse and keyboard */
#define NGUI_MOUSE_AND_KB 3

/*! when entering a dialog or an item */
typedef struct NGUI_DIALOG
{
    /*! z sorted list of items */
    LIST *item_list ;
    /*! z sorted list of dialogs */
    LIST *dialog_list ;
    /*! x position */
    int x,
        /*! y position */
        y,
        /*! z position */
        z,
        /*! width */
        w,
        /*! height */
        h,
        /*! 0 or 1 to draw decorations */
        decorations,
        /*! 0 or 1 to draw rounded corners */
        soft_angles,
        /*! if decorations, size of angle in percent of item size (max 50% of MIN(x,h) ) */
        soft_angles_size,
        /*! 0 or 1 to show or hide dialog */
        hidden ;
    /*! background color */
    ALLEGRO_COLOR bg[ NGUI_NBSTATES] ;
    /*! various shared datas like bitmaps, ect */
    RESOURCES *resources ;
    /*! Dialog name */
    char *name ;
} NGUI_DIALOG ;


/*! when entering a dialog or an item */
typedef struct NGUI_ITEM
{
    /*! Pointer to loaded bitmap if any */
    ALLEGRO_BITMAP *bmp[ NGUI_NBSTATES ];
    /*! Pointer to allegro sound if any */
    ALLEGRO_SAMPLE *sound[ NGUI_NBSTATES ] ;
    /*! Pointer to callbacks if any */
    void (*callback[NGUI_NBSTATES])(void *data);
    char   /*! Text if any */
    *text,
    /*! item id */
    *id ;

    /*! Pointer to font if any */
    ALLEGRO_FONT *font ;
    int /*! font size */
    font_size,
    /*! x position */
    x,
    /*! y position */
    y,
    /*! width */
    w,
    /*! height */
    h,
    /*! x center */
    cx,
    /*! y center */
    cy,
    /*! z position inside dialog */
    z,
    /*! alignment */
    alignment,
    /*! alignment */
    text_alignment,
    /*! state of the item */
    status ;
    /*! background color */
    ALLEGRO_COLOR lbg[ NGUI_NBSTATES ] ;
    /*! z sorted list of items */
    LIST *item_list ;
    /*! pointer to parent dialog */
    NGUI_DIALOG *dialog ;
} NGUI_ITEM ;


/* new item */
NGUI_ITEM *ngui_new_item( NGUI_DIALOG *dialog, char *id );
/* delete item */
void ngui_delete_item( void *item );
/* select item */
NGUI_ITEM *ngui_get_item( NGUI_DIALOG *dialog, char *id );
/* set function callback on item state */
int ngui_set_item_callback( NGUI_ITEM *item, int state, void (*callback)(void *data) );
/* set sound on item state */
int ngui_set_item_sound( NGUI_ITEM *item, int state, ALLEGRO_SAMPLE *sample );
/* set bitmap on item state */
int ngui_set_item_bitmap( NGUI_ITEM *item, int state,ALLEGRO_BITMAP *bitmap );
/* set item position */
int ngui_set_item_positition( NGUI_ITEM *item, int x, int y );
/* set item size */
int ngui_set_item_size( NGUI_ITEM *item, int w, int h );
/* set item alignment flag */
int ngui_set_item_alignment( NGUI_ITEM *item, int alignment );
/* set item text alignment flag */
int ngui_set_item_text_alignment( NGUI_ITEM *item, int text_alignment );

/* create new dialog */
NGUI_DIALOG *ngui_new_dialog( char *name, int x, int y, int z, int w, int h );
/* delete dialog */
void ngui_delete_dialog( void *dialog );
/* load dialog from file */
NGUI_DIALOG *ngui_load_from_file( char *file );
/*sort items in dialogs */
int ngui_sort_dialog( NGUI_DIALOG *dialog );
/* process dialog */
int ngui_manage( NGUI_DIALOG *dialog );
/* draw dialog */
int ngui_draw( NGUI_DIALOG *dialog );
/*destroy dialog */
int ngui_destroy( NGUI_DIALOG **dialog );

/**
@}
*/

#ifdef __cplusplus
}
#endif
#endif /* __N_GUI_HEADER */

