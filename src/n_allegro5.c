/**\file n_allegro5.c
 *  allegro5 helpers
 *\author Castagnier Mickael
 *\version 1.0
 *\date 10/05/2020
 */


#include "nilorea/n_common.h"
#include "nilorea/n_allegro5.h"

int get_keyboard( ALLEGRO_USTR *str, ALLEGRO_EVENT event )
{
    __n_assert( str, return FALSE );
    if( event.type == ALLEGRO_EVENT_KEY_CHAR )
    {
        if(event.keyboard.unichar >= 32)
        {
            al_ustr_append_chr(str, event.keyboard.unichar);
        }
        else if(event.keyboard.keycode == ALLEGRO_KEY_BACKSPACE)
        {
            int pos = (int)al_ustr_size(str);
            if(al_ustr_prev(str, &pos))
                al_ustr_truncate(str, pos);
        }
    }
    else
    {
        return FALSE ;
    }
    return TRUE ;
}
