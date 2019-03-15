/**\file lexmenu.c
*
*  common headers and low-level hugly functions & define
*
 *\author Castagnier Mickael
*
*\version 1.0
*
*\date 24/03/05
*
*/



#include "nilorea/lexmenu.h"

LexMenu* lexMenuCreate(BITMAP *bg)
{
    LexMenu *menu = calloc(1, sizeof(LexMenu));
    if (menu)
    {
        menu->bg = bg;
        menu->begin = calloc(1, sizeof(LexMenuItem));
        menu->end   = calloc(1, sizeof(LexMenuItem));

        menu->begin->next = menu->end;
        menu->end->prev = menu->begin;
    }
    return menu;
}

LexMenu* lexMenuDestroy(LexMenu* menu)
{
    if (menu)
    {
        LexMenuItemItor *itor = menu->begin;
        while (itor != menu->end)
        {
            if (itor->data)
            {
                if (itor->data->autoDestroyResources)
                {
                    int i= 0;
                    for (i = 0; i < LEX_MENU_IMG_COUNT; ++i)
                    {
                        /* It's ok to pass NULL to destroy bitmap */
                        destroy_bitmap(itor->data->img[i]);
                    }
                    for (i = 0; i < LEX_MENU_SND_COUNT; ++i)
                    {
                        /* It's ok to pass NULL to destroy bitmap */
                        destroy_sample(itor->data->snd[i]);
                    }
                }
                if (itor->data->autoDestroy)
                {
                    free(itor->data);
                }
            }
            itor = itor->next;
            free(itor->prev);
        }
        free(menu->end);

        if (menu->autoDestroyResources)
        {
            destroy_bitmap(menu->bg);
        }
        free(menu);
        menu = NULL;
    }
    return menu;
}

LexMenuItem *lexMenuCreateItem()
{
    LexMenuItem *item = calloc(1, sizeof(LexMenuItem));
    item->autoDestroy = 1;

    return item;
}

BITMAP *lexMenu__getImg(LexMenuItem *item)
{
    BITMAP *img = NULL;
    int active = item->state == LEX_MENU_ACTIVE ||
                 item->state == LEX_MENU_PRESSED;
    if (item->img[item->state])
    {
        img = item->img[item->state];
    }
    else
    {
        if (active && item->img[LEX_MENU_ACTIVE])
        {
            img = item->img[LEX_MENU_ACTIVE];
        }
        else
        {
            img = item->img[LEX_MENU_NORMAL];
        }
    }
    return img;
}


int lexMenu__getX(LexMenuItem *item)
{
    BITMAP *img;
    int     x;

    img = lexMenu__getImg(item);
    if (!img)
    {
        return item->x;
    }

    switch (item->align)
    {
    case LEX_MENU_ALIGN_LEFT:
        x = item->x;
        break;
    case LEX_MENU_ALIGN_CENTER:
        x = item->x - img->w/2;
        break;
    case LEX_MENU_ALIGN_RIGHT:
        x = item->x - img->w;
        break;
    }
    return x;
}

int lexMenu__getY(LexMenuItem *item)
{
    BITMAP *img = lexMenu__getImg(item);
    int     y;
    if (!img)
    {
        return item->y;
    }
    switch (item->align)
    {
    case LEX_MENU_ALIGN_TOP:
        y = item->y;
        break;
    case LEX_MENU_ALIGN_CENTER:
        y = item->y - img->h/2;
        break;
    case LEX_MENU_ALIGN_BOTTOM:
        y = item->x - img->h;
        break;
    }
    return y;
}


void lexMenu__fireEvent(LexMenuEvent *event)
{

    if (event->source->cbf[event->type])
    {
        /* Execute callback */
        event->source->cbf[event->type](event);
    }
    if (event->type < LEX_MENU_SND_COUNT &&
            event->source->snd[event->type])
    {
        play_sample(event->source->snd[event->type], 255, 128, 1000, 0);
    }
}

void lexMenuAddItem(LexMenu *menu, LexMenuItem *item)
{
    LexMenuItemItor *itor = calloc(1, sizeof(LexMenuItemItor));
    itor->data = item;

    itor->prev = menu->end->prev;
    itor->next = menu->end;

    menu->end->prev  = itor;
    itor->prev->next = itor;
}

void lexMenuSetCallback(LexMenuItem *item, int callbackID, LexMenuCallback func)
{
    if (callbackID >= 0 && callbackID < LEX_MENU_CBF_COUNT)
    {
        item->cbf[callbackID] = func;
    }
}

void lexMenuSetImage(LexMenuItem *item, int state, BITMAP* img)
{
    if (state >= 0 && state < LEX_MENU_IMG_COUNT)
    {
        item->img[state] = img;
    }
}

void lexMenuSetSample(LexMenuItem *item, int event, SAMPLE* sample)
{
    if (event >= 0 && event < LEX_MENU_SND_COUNT)
    {
        item->snd[event] = sample;
    }
}


int lexMenuRun(LexMenu* menu, BITMAP* dst)
{
    int result  = -1;
    int running =  1;

    int x,y,dx,dy;
    int mx = 0;
    int my = 0;
    int mb = 0;
    int lastX = 0;
    int lastY = 0;
    int lastB = 0;
    int mode  = menu->inputMode != 0 ? menu->inputMode : LEX_MENU_MOUSE_AND_KB;

    int moveX = 0;
    int moveY = 0;
    int shouldMove = 0;
    int wasDragged = 0;

    LexMenuItemItor *itor   = NULL;
    LexMenuItem     *item   = NULL;
    LexMenuItem     *active = NULL;
    LexMenuItem     *next   = NULL;
    LexMenuItem     *grab   = NULL;

    LexMenuItem     *lastActive = NULL;

    LexMenuEvent     event;

    BITMAP *img;

    if (dst == NULL)
    {
        dst = screen;
    }

    event.menu = menu;

    if (!(mode & LEX_MENU_MOUSE))
    {
        active = menu->begin->next->data;
        if (active)
        {
            active->state = LEX_MENU_ACTIVE;
            lastActive = active;
        }
        show_mouse(NULL);
    }
    else if (menu->cursor)
    {
        show_mouse(NULL);
    }
    else
    {
        show_mouse(screen);
    }

    if (menu->begin && menu->begin->next)
    {
        lastActive = menu->begin->next->data;
    }


    do
    {
        if (menu->bg)
        {
            blit(menu->bg, dst, 0, 0, 0, 0, menu->bg->w, menu->bg->h);
        }
        else
        {
            clear(dst);
        }

        if (mode & LEX_MENU_MOUSE)
        {
            lastX = mx;
            lastY = my;
            lastB = mb;

            mx = mouse_x;
            my = mouse_y;
            mb = mouse_b;

            dx = mx - lastX;
            dy = my - lastY;

            event.x = mx;
            event.y = my;

            if (active && (mx != lastX || my != lastY))
            {

                if (grab)
                {
                    event.source = grab;
                    event.type   = LEX_MENU_ON_DRAG;
                    event.dx     = dx;
                    event.dy     = dy;
                    lexMenu__fireEvent(&event);

                    wasDragged = 1;
                }

                x = lexMenu__getX(active);
                y = lexMenu__getY(active);

                img = lexMenu__getImg(active);
                if (img)
                {
                    if ( (mx >= x && mx < x + img->w) &&
                            (my >= y && my < y + img->h)
                       )
                    {
                        /* OnMove: will behandled below */
                    }
                    else
                    {
                        event.source  = active;
                        event.type    = LEX_MENU_ON_LEAVE;
                        active->state = LEX_MENU_NORMAL;

                        lexMenu__fireEvent(&event);
                        active = NULL;
                    }
                }
            }
        }

        itor = menu->begin;
        while (itor != menu->end)
        {
            item = itor->data;
            if (item)
            {
                img = lexMenu__getImg(item);
                x   = lexMenu__getX(item);
                y   = lexMenu__getY(item);

                event.source = item;
                if (item->cbf[LEX_MENU_ON_DRAW])
                {
                    event.type = LEX_MENU_ON_DRAW;
                    lexMenu__fireEvent(&event);
                }
                else if (img)
                {
                    draw_sprite(dst, img, x, y);
                }
                event.type = LEX_MENU_ON_DRAW_DONE;
                lexMenu__fireEvent(&event);

                if (img && (mode & LEX_MENU_MOUSE && (mx != lastX || my != lastY  || mb != lastB)))
                {
                    if (mx >= x && mx < x + img->w &&
                            my >= y && my < y + img->h)
                    {

                        if (grab && item != grab)
                        {
                            grab->state = LEX_MENU_NORMAL;
                            event.type = LEX_MENU_ON_LEAVE;
                            active = NULL;
                            event.source = grab;
                            lexMenu__fireEvent(&event);
                        }

                        event.source = item;
                        if (active != item && grab == NULL)
                        {

                            if (active != NULL)
                            {
                                active->state = LEX_MENU_NORMAL;
                                event.type = LEX_MENU_ON_LEAVE;
                                lexMenu__fireEvent(&event);
                            }

                            item->state = LEX_MENU_ACTIVE;
                            active      = item;
                            lastActive  = item;

                            event.type = LEX_MENU_ON_ENTER;
                            lexMenu__fireEvent(&event);
                        }
                        else
                        {
                            if (mb)
                            {
                                if (item->state != LEX_MENU_PRESSED && grab == NULL)
                                {
                                    item->state = LEX_MENU_PRESSED;

                                    grab = item;
                                    wasDragged = 0;
                                    event.type = LEX_MENU_ON_PRESS;
                                    lexMenu__fireEvent(&event);
                                }
                            }
                            else
                            {
                                if (grab && item != grab)
                                {
                                    event.source = grab;
                                    event.type   = LEX_MENU_ON_RELEASE;
                                    lexMenu__fireEvent(&event);

                                    event.source = item;
                                }
                                if (item->state == LEX_MENU_PRESSED)
                                {
                                    item->state = LEX_MENU_NORMAL;
                                    if (wasDragged)
                                    {
                                        event.type  = LEX_MENU_ON_RELEASE;
                                    }
                                    else
                                    {
                                        event.type  = LEX_MENU_ON_FIRE;
                                        running = 0;
                                    }
                                    lexMenu__fireEvent(&event);
                                }
                                if (lastX != mx || lastY != my)
                                {
                                    event.dx = mx - lastX;
                                    event.dy = my - lastY;

                                    event.type = LEX_MENU_ON_MOVE;
                                    lexMenu__fireEvent(&event);
                                }
                                wasDragged = 0;
                                grab       = NULL;
                            }
                        }
                    }
                }
            }
            itor = itor->next;
        }

        if (key[KEY_ESC])
        {
            active  = NULL;
            running = 0;
        }
        shouldMove = 0;
        if ((mode & LEX_MENU_KEYBOARD) && (active || lastActive))
        {
            if (key[KEY_LEFT])
            {
                if (moveX != -1)
                {
                    moveX = -1;
                    shouldMove = 1;
                }
            }
            else  if (key[KEY_RIGHT])
            {
                if (moveX != 1)
                {
                    moveX = 1;
                    shouldMove = 1;
                }
            }
            else
            {
                moveX = 0;
            }
            if (key[KEY_DOWN])
            {
                if (moveY != 1)
                {
                    moveY = 1;
                    shouldMove = 1;
                }
            }
            else if (key[KEY_UP])
            {
                if (moveY != -1)
                {
                    moveY= -1;
                    shouldMove = 1;
                }
            }
            else
            {
                moveY = 0;
            }

            if (key[KEY_ENTER] || key[KEY_SPACE])
            {

                if (active==NULL && lastActive != NULL)
                {
                    active = lastActive;
                }
                running = 0;

                event.type = LEX_MENU_ON_PRESS;
                event.source = active;
                lexMenu__fireEvent(&event);

                event.type = LEX_MENU_ON_FIRE;
                lexMenu__fireEvent(&event);
            }
        }
        else
        {
            moveX = moveY = 0;
        }

        if (shouldMove)
        {
            int dist = 0;
            int min  = 0xffffff;
            int ox   = 0;
            int oy   = 0;

            if (active==NULL && lastActive != NULL)
            {
                active = lastActive;
            }
            ox = lexMenu__getX(active) + lexMenu__getImg(active)->w/2;
            oy = lexMenu__getY(active) + lexMenu__getImg(active)->h/2;

            next = NULL;
            itor = menu->begin;
            while (itor != menu->end)
            {
                item = itor->data;
                if (item && item != active && item->state != LEX_MENU_DISABLED)
                {
                    img = lexMenu__getImg(item);
                    x = lexMenu__getX(item) + img->w/2;
                    y = lexMenu__getY(item) + img->h/2;

                    if (moveX > 0)
                    {
                        if (x > ox)
                        {
                            dist = ABS(ox-x);
                        }
                        else
                        {
                            dist = 0xffffff;
                        }
                    }
                    else if (moveX < 0)
                    {
                        if (x < ox)
                        {
                            dist = ABS(ox-x);
                        }
                        else
                        {
                            dist = 0xffffff;
                        }
                    }
                    else if (moveY > 0)
                    {
                        if (y > oy)
                        {
                            dist = ABS(oy-y);
                        }
                        else
                        {
                            dist = 0xffffff;
                        }
                    }
                    else if (moveY < 0)
                    {
                        if (y < oy)
                        {
                            dist = ABS(oy-y);
                        }
                        else
                        {
                            dist = 0xffffff;
                        }
                    }
                    if (dist < min)
                    {
                        dist = min;
                        next= item;
                    }
                }
                itor = itor->next;
            }
            if (next)
            {
                event.type = LEX_MENU_ON_LEAVE;
                active->state = LEX_MENU_NORMAL;
                event.source = active;
                lexMenu__fireEvent(&event);

                active = next;
                lastActive = active;
                active->state = LEX_MENU_ACTIVE;
                event.type = LEX_MENU_ON_ENTER;
                event.source = active;
                lexMenu__fireEvent(&event);
            }
        }

        if (menu->cursor)
        {
            draw_sprite(dst, menu->cursor, mx- menu->hotX, my- menu->hotY);
        }

        if (menu->showMenu)
        {
            menu->showMenu(dst);
        }
        else
        {
            if (dst != screen)
            {
                scare_mouse();
                blit(dst, screen, 0, 0, 0, 0, dst->w, dst->h);
                unscare_mouse();
            }
        }
    }
    while (running);

    if (active)
    {
        result = active->id;
    }
    else
    {
        result = -1;
    }

    return result;
}

#include "lexmenu.h"
#include <stdio.h>
#include <strings.h>

char* lex__createFileName(const char *filename, const char* postfix)
{
    int len = strlen(filename);
    int postfixLen = strlen(postfix);
    int i =0;
    char* buf = NULL;
    char* cursor = NULL;

    i = len;
    while (filename[i] != '.' && i >= 0)
    {
        --i;
    }
    if (i >=0 && filename[i] == '.')
    {
        buf = calloc(len + postfixLen+1, 1);

        cursor = buf;
        strncpy(cursor, filename, i);
        cursor += i;
        strncpy(cursor, postfix, postfixLen);
        cursor+=postfixLen;
        strncpy(cursor, filename+i, len-i);

        return buf;
    }
    else
    {
        return NULL;
    }
}

BITMAP* lex__loadStateImage(const char* value, const char* postfix)
{
    char *imageName = NULL;
    BITMAP* image = NULL;

    imageName = lex__createFileName(value, postfix);
    if (imageName != NULL)
    {
        image = load_bitmap(imageName, NULL);
        free(imageName);
    }
    return image;
}

SAMPLE* lex__loadStateSample(const char* value, const char* postfix)
{
    char *sampleName = NULL;
    SAMPLE* sample = NULL;

    sampleName = lex__createFileName(value, postfix);
    if (sampleName != NULL)
    {
        sample = load_sample(sampleName);
        free(sampleName);
    }
    return sample;
}

LexMenuItem* lexLoadItemFromSection(const char* itemSectionName, int id)
{
    LexMenuItem* item = NULL;
    BITMAP* image = NULL;
    const char* value;

    /* The minimum requirement is that the item has a position */
    /* So, we first check if the x and y entries are present */
    if (get_config_string(itemSectionName,"x", NULL) == NULL
            || get_config_string(itemSectionName,"y", NULL) == NULL )
    {
        return NULL;
    }

    item = lexMenuCreateItem();
    if (item != NULL)
    {
        item->autoDestroyResources = TRUE;
        item->id = id;

        /* Position */
        item->x = get_config_int(itemSectionName,"x", 0);
        item->y = get_config_int(itemSectionName,"y", 0);

        /* Alignment */
        value = get_config_string(itemSectionName,"h_align", "center");
        if (stricmp("center", value) == 0)
        {
            item->align = LEX_MENU_ALIGN_CENTER;
        }
        else if (stricmp("left", value) == 0)
        {
            item->align = LEX_MENU_ALIGN_LEFT;
        }
        else if (stricmp("right", value) == 0)
        {
            item->align = LEX_MENU_ALIGN_RIGHT;
        }

        value = get_config_string(itemSectionName,"h_align", "center");
        if (stricmp("center", value) == 0)
        {
            item->valign = LEX_MENU_ALIGN_CENTER;
        }
        else if (stricmp("top", value) == 0)
        {
            item->valign = LEX_MENU_ALIGN_TOP;
        }
        else if (stricmp("bottom", value) == 0)
        {
            item->valign = LEX_MENU_ALIGN_BOTTOM;
        }

        /* Images */
        value = get_config_string(itemSectionName, "image", NULL);

        if (value != NULL)
        {
            image = load_bitmap(value, NULL);
            if (image != NULL)
            {
                lexMenuSetImage(item, LEX_MENU_NORMAL, image);
                lexMenuSetImage(item, LEX_MENU_ACTIVE,   lex__loadStateImage(value, "_hot"));
                lexMenuSetImage(item, LEX_MENU_PRESSED,  lex__loadStateImage(value, "_down"));
                lexMenuSetImage(item, LEX_MENU_DISABLED, lex__loadStateImage(value, "_gray"));
            }
        }
        value = get_config_string(itemSectionName, "sound", NULL);
        if (value != NULL)
        {
            lexMenuSetSample(item, LEX_MENU_ON_FIRE,  load_sample(value));
            lexMenuSetSample(item, LEX_MENU_ON_ENTER, lex__loadStateSample(value, "_hot"));
            lexMenuSetSample(item, LEX_MENU_ON_LEAVE, lex__loadStateSample(value, "_cold"));
        }

    }
    return item;
}

LexMenu* lexLoadMenuFromFile(const char* filename, const char* screenname)
{
    int curItem = 0;
    int countItems = 0;
    int ok = FALSE;
    int len = 0, max=0;
    char buf[9];
    const char* value=NULL;
    char *itemSectionName = NULL;
    LexMenu* menu = NULL;
    LexMenuItem* item =NULL;
    BITMAP *bg = NULL;

    push_config_state();
    set_config_file(filename);

    /* check how many items we have */
    ok = TRUE;
    while (ok && countItems < 9999)   /* Check is here to ensure that buf won't overflow */
    {
        sprintf(buf, "item%d", countItems);
        value = get_config_string(screenname, buf, NULL);
        if (value == NULL)
        {
            ok = FALSE;
        }
        else
        {
            len = strlen(value);
            max = MAX(max, len);
            ++countItems;
        }
    }

    /* Yeah, we have some work todo */
    if (countItems > 0)
    {
        value = get_config_string(screenname, "bg", NULL);
        if (value)
        {
            bg = load_bitmap(value, NULL);
        }
        menu = lexMenuCreate(bg);
        if (menu != NULL)
        {
            menu->autoDestroyResources= TRUE;

            /* Need to create the section header name dynamically.
             * So we need the space for screenname + '.' + itenname + string terminator.
             * We know the length of the longest item name, so we'll just create
             * one stringbuffer and reuse it.
             */
            itemSectionName = calloc(strlen(screenname) + max +2,1);
            for (curItem = 0; curItem < countItems; ++curItem)
            {
                sprintf(buf, "item%d", curItem);
                sprintf(itemSectionName, "%s.%s", screenname, get_config_string(screenname, buf, "#"));

                item = lexLoadItemFromSection(itemSectionName, curItem);
                if (item != NULL)
                {
                    lexMenuAddItem(menu, item);
                }
            }
            free(itemSectionName);
        }
    }


    pop_config_state();
    return menu;
}


BITMAP *lexMenuGetBitmap( LexMenu* menu, int item, int whichone )
{
    LexMenuItemItor *item_ptr = NULL;

    if( !menu )
        return NULL;
    if( !menu -> begin )
        return NULL ;

    item_ptr = menu -> begin ;

    if( whichone < 0 || whichone > 3 )
        return NULL ;

    while( item_ptr )
    {
        if( item_ptr -> data )
        {
            if( item_ptr -> data -> id == item )
                return item_ptr -> data -> img[ whichone ];
        }

        item_ptr = item_ptr -> next ;

    }

    return NULL;
}
