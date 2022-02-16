/**\example ex_gui_dictionary.c Nilorea Library gui particle api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 10/02/2020
 */

#define WIDTH 1280
#define HEIGHT 800

#include "nilorea/n_common.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"
#include "nilorea/n_pcre.h"
#include "nilorea/n_str.h"
#include "nilorea/n_allegro5.h"
#include <allegro5/allegro_ttf.h>

/* dictionnaries are from https://www.bragitoff.com/2016/03/english-dictionary-in-csv-format/ */

ALLEGRO_DISPLAY *display  = NULL ;

int		DONE = 0,               /* Flag to check if we are always running */
        getoptret = 0,		    /* launch parameter check */
        log_level = LOG_INFO ;	/* default LOG_LEVEL */

ALLEGRO_BITMAP *scr_buf       = NULL ;

ALLEGRO_TIMER *fps_timer = NULL ;
ALLEGRO_TIMER *logic_timer = NULL ;


int main( int argc, char *argv[] )
{
    set_log_level( log_level );

    n_log( LOG_NOTICE, "%s is starting ...", argv[ 0 ] );


    /* allegro 5 + addons loading */
    if (!al_init())
    {
        n_abort("Could not init Allegro.\n");
    }
    if (!al_init_image_addon())
    {
        n_abort("Unable to initialize image addon\n");
    }
    if (!al_init_primitives_addon() )
    {
        n_abort("Unable to initialize primitives addon\n");
    }
    if( !al_init_font_addon() )
    {
        n_abort("Unable to initialize font addon\n");
    }
    if( !al_init_ttf_addon() )
    {
        n_abort("Unable to initialize ttf_font addon\n");
    }
    if( !al_install_keyboard() )
    {
        n_abort("Unable to initialize keyboard handler\n");
    }
    if( !al_install_mouse())
    {
        n_abort("Unable to initialize mouse handler\n");
    }
    ALLEGRO_EVENT_QUEUE *event_queue = NULL;

    event_queue = al_create_event_queue();
    if(!event_queue)
    {
        fprintf(stderr, "failed to create event_queue!\n");
        al_destroy_display(display);
        return -1;
    }

    char ver_str[ 128 ] = "" ;
    while( ( getoptret = getopt( argc, argv, "hvV:L:" ) ) != EOF )
    {
        switch( getoptret )
        {
            case 'h':
                n_log( LOG_NOTICE, "\n    %s -h help -v version -V LOG_LEVEL (NOLOG,INFO,NOTICE,ERR,DEBUG) -L OPT_LOG_FILE\n", argv[ 0 ] );
                exit( TRUE );
            case 'v':
                sprintf( ver_str, "%s %s", __DATE__, __TIME__ );
                exit( TRUE );
                break ;
            case 'V':
                if( !strncmp( "INFO", optarg, 6 ) )
                {
                    log_level = LOG_INFO ;
                }
                else
                {
                    if(  !strncmp( "NOTICE", optarg, 7 ) )
                    {
                        log_level = LOG_NOTICE ;
                    }
                    else
                    {
                        if(  !strncmp( "ERR", optarg, 5 ) )
                        {
                            log_level = LOG_ERR ;
                        }
                        else
                        {
                            if(  !strncmp( "DEBUG", optarg, 5 ) )
                            {
                                log_level = LOG_DEBUG ;
                            }
                            else
                            {
                                n_log( LOG_ERR, "%s is not a valid log level\n", optarg );
                                exit( FALSE );
                            }
                        }
                    }
                }
                n_log( LOG_NOTICE, "LOG LEVEL UP TO: %d\n", log_level );
                set_log_level( log_level );
                break;
            case 'L' :
                n_log( LOG_NOTICE, "LOG FILE: %s\n", optarg );
                set_log_file( optarg );
                break ;
            case '?' :
                {
                    switch( optopt )
                    {
                        case 'V' :
                            n_log( LOG_ERR, "\nPlease specify a log level after -V. \nAvailable values: NOLOG,VERBOSE,NOTICE,ERROR,DEBUG\n\n" );
                            break;
                        case 'L' :
                            n_log( LOG_ERR, "\nPlease specify a log file after -L\n" );
                        default:
                            break;
                    }
                }
                __attribute__ ((fallthrough));
            default:
                n_log( LOG_ERR, "\n    %s -h help -v version -V DEBUGLEVEL (NOLOG,VERBOSE,NOTICE,ERROR,DEBUG) -L logfile\n", argv[ 0 ] );
                exit( FALSE );
        }
    }

    double fps = 60.0 ;
    double fps_delta_time = 1.0 / fps ;
    double logic_delta_time = 1.0 / (5.0 * fps);

    fps_timer = al_create_timer( fps_delta_time );
    logic_timer = al_create_timer( logic_delta_time );

    al_set_new_display_flags( ALLEGRO_OPENGL|ALLEGRO_WINDOWED );
    display = al_create_display( WIDTH, HEIGHT );
    if( !display )
    {
        n_abort("Unable to create display\n");
    }
    al_set_window_title( display, argv[ 0 ] );

    al_set_new_bitmap_flags( ALLEGRO_VIDEO_BITMAP );

    DONE = 0 ;

    enum APP_KEYS
    {
        KEY_ESC
    };
    int key[ 1 ] = {false};

    al_register_event_source(event_queue, al_get_display_event_source(display));
    al_start_timer( fps_timer );
    al_start_timer( logic_timer );
    al_register_event_source(event_queue, al_get_timer_event_source(fps_timer));
    al_register_event_source(event_queue, al_get_timer_event_source(logic_timer));

    al_register_event_source(event_queue, al_get_keyboard_event_source());
    al_register_event_source(event_queue, al_get_mouse_event_source());

    ALLEGRO_FONT *font = NULL ;
    font = al_load_font( "2Dumb.ttf", 24 , 0 );
    if (! font )
    {
        n_log( LOG_ERR, "Unable to load 2Dumb.ttf" );
        exit( 1 );
    }

    ALLEGRO_BITMAP *scrbuf = al_create_bitmap( WIDTH, HEIGHT );

    al_hide_mouse_cursor(display);

    int mx = 0, my = 0, mouse_b1 = 0, mouse_b2 = 0 ;
    int do_draw = 0, do_logic = 0 ;

    /*! dictionary definition for DICTIONARY_ENTRY */
    typedef struct DICTIONARY_DEFINITION
    {
        /*! type of definition (verb, noun,...) */
        char *type ;
        /*! content of definition */
        char *definition ;
    } DICTIONARY_DEFINITION;

    /*! dictionary entry */
    typedef struct DICTIONARY_ENTRY
    {
        /*! key of the entry */
        char *key ;
        /*! list of DICTIONARY_DEFINITION for that entry */
        LIST *definitions ;
    } DICTIONARY_ENTRY ;

    void free_entry_def( void *entry_def )
    {
        DICTIONARY_DEFINITION *def = (DICTIONARY_DEFINITION *)entry_def;
        FreeNoLog( def -> type );
        FreeNoLog( def -> definition );
        FreeNoLog( def );
    }
    void free_entry( void *entry_ptr )
    {
        DICTIONARY_ENTRY *entry = (DICTIONARY_ENTRY *)entry_ptr;
        list_destroy( &entry -> definitions );
        FreeNoLog( entry -> key );
        FreeNoLog( entry );
    }

    /* Load dictionaries */
    HASH_TABLE *dictionary = new_ht_trie( 256 , 32 );
    FILE *dict_file = fopen( "dictionary.csv" , "r" );
    char read_buf[ 16384 ] = "" ;
    DICTIONARY_DEFINITION current_definition ;
    char *entry_key = NULL ;
    char *type = NULL ;
    char *definition = NULL ;
    N_PCRE *dico_regexp = npcre_new ( "\"(.*)\",\"(.*)\",\"(.*)\"" , 99 , 0 );
    N_PCRE *nagios_regexp = npcre_new ( "(.*?);.*?;.*?;(.*?);(.*)" , 99 , 0 );

    char lastletter = ' ' ;
    int match_type = 0 ; /* 0 dico 1 nagios */
    while( fgets( read_buf , 16384 , dict_file ) )
    {
        match_type = -1 ;
        if( npcre_match( read_buf , dico_regexp ) )
            match_type = 0 ;
        if( npcre_match( read_buf , nagios_regexp ) )
            match_type = 1 ;

        if( match_type != -1 )
        {
            if( match_type == 0 )
            {
                entry_key  = strdup( _str( dico_regexp -> match_list[ 1 ] ) );
                type       = strdup( _str( dico_regexp -> match_list[ 2 ] ) );
                definition = strdup( _str( dico_regexp -> match_list[ 3 ] ) );
            }
            else
            {
                N_STR *tmpstr = NULL ;
                nstrprintf( tmpstr , "%s-%s" , nagios_regexp -> match_list[ 1 ] , nagios_regexp -> match_list[ 2 ] );
                entry_key  = _nstr( tmpstr );
                Free( tmpstr );
                type       = strdup( _str( nagios_regexp -> match_list[ 2 ] ) );
                definition = strdup( _str( nagios_regexp -> match_list[ 3 ] ) );
            }

            n_log( LOG_DEBUG , "matched %s , %s , %s" , entry_key , type , definition );

            DICTIONARY_ENTRY *entry = NULL ;
            DICTIONARY_DEFINITION *entry_def = NULL ;

            if( ht_get_ptr( dictionary , entry_key , (void **)&entry ) == TRUE )
            {
                Malloc( entry_def , DICTIONARY_DEFINITION , 1 );
                entry_def -> type = strdup( type );
                entry_def -> definition = strdup( definition );
                list_push( entry -> definitions , entry_def , &free_entry_def );
            }
            else
            {
                Malloc( entry , DICTIONARY_ENTRY , 1 );

                entry -> definitions = new_generic_list( 0 );
                entry -> key = strdup( entry_key );

                Malloc( entry_def , DICTIONARY_DEFINITION , 1 );

                entry_def -> type = strdup( type );
                entry_def -> definition = strdup( definition );

                list_push( entry -> definitions , entry_def , &free_entry_def );

                ht_put_ptr( dictionary  , entry_key , entry , &free_entry );
            }
            FreeNoLog( entry_key );
            FreeNoLog( type );
            FreeNoLog( definition );
            npcre_clean_match( nagios_regexp ); 
            npcre_clean_match( dico_regexp ); 
        }
    }
    fclose( dict_file );

    ALLEGRO_USTR *keyboard_buffer = al_ustr_new( "" );
    int cur_key_pos = 0 ;
    LIST *completion = NULL ;
    int key_pressed = 0 ;
    n_log( LOG_NOTICE , "Starting main loop" );

    al_clear_keyboard_state( NULL );
    al_flush_event_queue( event_queue );
    do
    {
        do
        {
            ALLEGRO_EVENT ev ;

            al_wait_for_event(event_queue, &ev);

            if(ev.type == ALLEGRO_EVENT_KEY_DOWN)
            {
                switch(ev.keyboard.keycode)
                {
                    case ALLEGRO_KEY_ESCAPE:
                        key[KEY_ESC] = 1 ;
                        break;
                    default:
                        break;
                }
            }
            else if(ev.type == ALLEGRO_EVENT_KEY_UP)
            {
                switch(ev.keyboard.keycode)
                {
                    case ALLEGRO_KEY_ESCAPE:
                        key[KEY_ESC] = 0 ;
                        break;
                    default:
                        break;
                }
            }
            else if( ev.type == ALLEGRO_EVENT_TIMER )
            {
                if( al_get_timer_event_source( fps_timer ) == ev.any.source )
                {
                    do_draw = 1 ;
                }
                else if( al_get_timer_event_source( logic_timer ) == ev.any.source )
                {
                    do_logic = 1;
                }
            }
            else if( ev.type == ALLEGRO_EVENT_MOUSE_AXES )
            {
                mx = ev.mouse.x;
                my = ev.mouse.y;
            }
            else if( ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN )
            {
                if( ev.mouse.button == 1 )
                    mouse_b1 = 1 ;
                if( ev.mouse.button == 2 )
                    mouse_b2 = 1 ;
            }
            else if( ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_UP )
            {
                if( ev.mouse.button == 1 )
                    mouse_b1 = 0 ;
                if( ev.mouse.button == 2 )
                    mouse_b2 = 0 ;
            }
            else if( ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_IN || ev.type == ALLEGRO_EVENT_DISPLAY_SWITCH_OUT )
            {
                al_clear_keyboard_state( display );
                al_flush_event_queue( event_queue );
            }
            else
            {
                /* Processing inputs */
                key_pressed = 0 ;
                if( get_keyboard( keyboard_buffer , ev ) == TRUE )
                    key_pressed = 1 ;
            }
        }while( !al_is_event_queue_empty( event_queue) );

        if( do_logic == 1 )
        {
            if( key_pressed == 1 )
            {
                key_pressed = 0 ;
                int max_results = 20 ;
                if( completion )
                {
                    list_destroy( &completion );
                }
                completion = ht_get_completion_list( dictionary , al_cstr( keyboard_buffer ) , max_results );
            }

            int mouse_button = -1 ;
            if( mouse_b1==1 )
                mouse_button = 1 ;
            if( mouse_b2==1 )
                mouse_button = 2 ;

            do_logic = 0 ;
        }

        if( do_draw == 1 )
        {
            static int last_time = 0 ;
            static int cursor_blinking = 1 ;

            al_acknowledge_resize( display );
            int w = al_get_display_width(  display );
            int h = al_get_display_height( display );

            al_set_target_bitmap( scrbuf );
            al_clear_to_color( al_map_rgba( 0, 0, 0, 255 ) );

            last_time -= 1000000/30.0 ;
            static int length = 0 ;
            if(  last_time < 0 )
            {
                last_time = 250000 ;
            }
            else
            {
                length = al_get_text_width( font , al_cstr( keyboard_buffer ) );
                al_draw_filled_rectangle( WIDTH / 2 + ( length + 6 ) / 2      , 50 ,
                        WIDTH / 2 + ( length + 6 ) / 2 + 15 , 70 ,
                        al_map_rgb( 0 , 128 , 0 ) );
            }
            al_draw_text( font , al_map_rgb( 0 , 255 , 0 ) , WIDTH / 2 , 50 , ALLEGRO_ALIGN_CENTRE , al_cstr( keyboard_buffer ) );

            DICTIONARY_ENTRY *entry = NULL ;
            if( ht_get_ptr( dictionary , al_cstr( keyboard_buffer ) , (void **)&entry ) == TRUE )
            {
                int vertical_it = 0 ;
                list_foreach( node , entry -> definitions ) 
                {
                    DICTIONARY_DEFINITION *definition = (DICTIONARY_DEFINITION *)node -> ptr ;
                    al_draw_textf( font , al_map_rgb( 0 , 255 , 0 ) , WIDTH / 2 , 150 + vertical_it , ALLEGRO_ALIGN_CENTRE , "%s : %s" , definition -> type , definition -> definition );
                    vertical_it += 35 ;
                }
            }
            else
            {
                if( completion )
                {
                    int vertical_it = 0 ;
                    list_foreach( node , completion ) 
                    {
                        char *key = (char *)node -> ptr ;
                        al_draw_text( font , al_map_rgb( 0 , 255 , 0 ) , WIDTH / 2 , 100 + vertical_it , ALLEGRO_ALIGN_CENTRE , key );
                        vertical_it += 35 ;
                    }
                }
            }

            al_set_target_bitmap( al_get_backbuffer( display ) );
            al_draw_bitmap( scrbuf, 0, 0, 0 );

            /* mouse pointer */
            al_draw_line( mx - 5, my, mx + 5, my, al_map_rgb( 255, 0, 0 ), 1 );
            al_draw_line( mx, my + 5, mx, my - 5, al_map_rgb( 255, 0, 0 ), 1 );

            al_flip_display();
            do_draw = 0 ;
        }

    }
    while( !key[KEY_ESC] && !DONE );

    destroy_ht( &dictionary );

    return 0;
}
