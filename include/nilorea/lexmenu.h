/**\file lexmenu.h
*  common headers and low-level hugly functions & define
*\version 1.0
*\date 24/03/05
*
*/

#ifndef LEX_MENU_HEADER
#define LEX_MENU_HEADER


#include <malloc.h>
#include <allegro.h>

#ifdef __cplusplus
extern "C" {
#endif

/**\defgroup GUI LEXMENU: quick GUI functions (by spellcaster)
   \addtogroup GUI
  @{
*/

#define LEX_MENU_IMG_COUNT 4
#define LEX_MENU_SND_COUNT 5
#define LEX_MENU_CBF_COUNT 9


/* Alignment constants. These can be used to
   specify how an item should be aligned to its
   coordinates.
*/
#define LEX_MENU_ALIGN_LEFT   0
#define LEX_MENU_ALIGN_TOP    0
#define LEX_MENU_ALIGN_CENTER 1
#define LEX_MENU_ALIGN_RIGHT  2
#define LEX_MENU_ALIGN_BOTTOM 2

/* A menu item can have on of four states:
   LEX_MENU_NORMAL  : The default state for an item
   LEX_MENU_ACTIVE  : The mouse cursor hovers over the item
                      / the item has the focus
   LEX_MENU_PRESSED : The mouse button was pressed
   LEX_MENU_DISABLED: The item is disabled

   You can set a bitmap for each of these states,
   giving you a simple way to simulate "rollover" effects.

   Use lexMenuSetImage() to set the image for a certain state.
*/
#define LEX_MENU_NORMAL   0
#define LEX_MENU_ACTIVE   1
#define LEX_MENU_PRESSED  2
#define LEX_MENU_DISABLED 3

/* There're a couple of "events" thrown by the items.
   You register to an event on a per item base.

   Use the lexMenuSetCallback() function to set the function
   you want to be called if one of the events occurs.

   You can also set a sound which should be played if one of
   the events is fired. You cannot set sound for the MOVE and
   DRAG event, though.
   For all other events, use lexMenuSetSample() to specify a
   sound effect.

   If ON_DRAW is set, the items will not draw itself, but call
   the callback function instead. Use this for user-drawn menu
   items.

   ON_DRAW_DONE is fired as soon as an item was drawn. You can
   use that callback to decorate an already drawn item
*/
#define LEX_MENU_ON_ENTER     0
#define LEX_MENU_ON_LEAVE     1
#define LEX_MENU_ON_PRESS     2
#define LEX_MENU_ON_RELEASE   3
#define LEX_MENU_ON_FIRE      4
#define LEX_MENU_ON_MOVE      5
#define LEX_MENU_ON_DRAG      6
#define LEX_MENU_ON_DRAW      7
#define LEX_MENU_ON_DRAW_DONE 8

/* You can specify how the menu should react on events.
   Per default it will react on mouse and keyboard events.
   If you set menu->inputMode to LEX_MENU_MOUSE only mouse
   input will be accepted.
   Set it to LEX_MENU_KEYBOARD if you want the menu to react
   on keyboard only.
*/
#define LEX_MENU_MOUSE    1
#define LEX_MENU_KEYBOARD 2
#define LEX_MENU_MOUSE_AND_KB 3

/* A LexMenuItem represents a single entry of the menu.
   You can set the bitmaps it has using lexMenuSetImage().
   You can set the sounds that should be played if certain events are
   triggered by calling lexMenuSetSample().

   Each item has an ID you can set (if you want). That id is returned
   lexMenuRun()

   If you want to get notified if a certain event occurs, use
   lexMenuSetCallback() to register your event callback function.

   You can create a LexMenuItem by calling lexMenuCreateItem()
   and add it to a menu via lexMenuAddItem().

   Once the item is added to the menu, you can forget it.
   It will be destroyed automatically by lexMenuDestroy if it has
   been allocated via lexMenuCreateItem, or if the autoDestroy member
   has been set manually.
*/
typedef struct LexMenuItemStruct     LexMenuItem;
typedef struct LexMenuItemItorStruct LexMenuItemItor;

/* The LexMenu represents the actual menu. It contains a list of items,
   an optional background image (bg), an optional software mouse cursor
   image (cursor) with hotspot information (hotX, hotY).
   You can set on what kinds of inputs the menus hould react by setting
   the inputMode variable. The default is to react on mouse and keyboard.

   You can also specify a function pointer to your update function if you
   want. If you don't set the showMenu function pointer, the bitmap
   passed to lexMenuRun() will be blitted to the screen - if the bitmap passed
   to lexMenuRun() is not the screen already, that is.
*/
typedef struct LexMenuStruct         LexMenu;

/* The event which will be passed to an event callback.
   type, menu and source are always valid.
   x,y,dx,dy are only valid for mouse input.
   dx,dy is only valid for DRAG and MOVE events.
*/
typedef struct LexMenuEventStruct    LexMenuEvent;

/* The event callback functipn */
typedef void (*LexMenuCallback)(LexMenuEvent *);

struct LexMenuItemItorStruct
{
    LexMenuItem*     data;
    LexMenuItemItor* next;
    LexMenuItemItor* prev;
};

struct LexMenuEventStruct
{
    int          type;
    LexMenu     *menu;
    LexMenuItem *source;
    int          x, y;
    int          dx, dy;
};


struct LexMenuItemStruct
{
    BITMAP          *img[LEX_MENU_IMG_COUNT];
    SAMPLE          *snd[LEX_MENU_SND_COUNT];
    LexMenuCallback  cbf[LEX_MENU_CBF_COUNT];
    int              id;
    int              state;
    int              x,y;
    int              align, valign;
    int              autoDestroy;
    int              autoDestroyResources;
};


struct LexMenuStruct
{
    LexMenuItemItor *begin, *end;
    BITMAP *bg;

    BITMAP *cursor;
    int  hotX, hotY;
    int  autoDestroyResources;

    int  inputMode;
    void (*showMenu)(BITMAP* img);
};

/* creates a new Menu and sets the BG image */
LexMenu* lexMenuCreate(BITMAP *bg);
/* Frees the memory occupied by the menu.
 * If autoDestroyResources is set (say by creating the menu via lexLoadMenuFromFile
 * or by manually setting it) all BITMAPs and SAMPLEs belonging to the menu
 * will be destroyed as well.
 * Passing NULL for menu will do nothing.
 */
LexMenu* lexMenuDestroy(LexMenu* menu);

/* Creates a menu item.
   LexMenuItems created by this function, which are added
   to a menu, will be destroyed automatically by lexMenuDestroy()
 */
LexMenuItem *lexMenuCreateItem();

/* Adds item to menu */
void lexMenuAddItem(LexMenu *menu, LexMenuItem *item);
/* Executes the menu. Returns the id of the activated item, or -1 if ESC was pressed */
int lexMenuRun(LexMenu* menu, BITMAP* dst);
/* Allows you to set a BITMAP for a certain state. The item will never free the image.
   You need to ensure that the bitmap is destroyed if no longer needed
*/
void lexMenuSetImage(LexMenuItem *item, int state, BITMAP* img);
/* Allows you to set a SAMPLE for a certain event. The item will never free the sample .
   You need to ensure that the sample is destroyed if no longer needed
*/
void lexMenuSetSample(LexMenuItem *item, int event, SAMPLE* sample);
/* Allows you to specify a callback to be called if an event occurs.*/
void lexMenuSetCallback(LexMenuItem *item, int callbackID, LexMenuCallback func);

/* Loads a menu from a config file named "filename". "screenname" is the section name for the menu.
   see "menu.ini" for a detailed example ini file.
*/
LexMenu* lexLoadMenuFromFile(const char* filename, const char* screenname);

/* Return a pointer to the bitmap of the item or NULL */

BITMAP *lexMenuGetBitmap( LexMenu* menu, int item, int whichone );

/*@}*/

#ifdef __cplusplus
}
#endif
#endif
