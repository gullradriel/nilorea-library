/**\file exclient.c
*
*  Nilorea Main File
*
*\author Castagnier Mickaël
*
*\version 1.0
*
*\date 16/09/2005
*
*/

#define ALLEGRO_USE_CONSOLE


#define _CLIENT_ "1.0 b"

/*  NILOREA ENGINE HEADER */
#include <nilorea.h>     

int 		heartbeat = 0 ,
        BLITTIME   = 16666 ,          /* Time between screen refresh */
        UPDATETIME = 250000 ,         /* Max Time between two update ( defaut 250000 microseconds ) */
        LOGICTIME  = 33333 ,         /* Time between two logic refresh ( defaut 10000 microseconds ) */
		PINGTIME   = 1000000 ,        /* Ping interval, 500 000 usec */  
		ping_time = 0 ,				 /* ping timer */
        elapsed_time = 0 ,           /* main loop timer for adjusting rest/draw/logic */
        logic_time = 0 ,             /* logic timer value */
        blit_time = 0 ,              /* blit timer value */
        update_time = 0 ,            /* update timer value */
        auto_nice_check = 0 ,	     /* if in a loop there was no update, this value will be 0. If not, then no rest. */
		PORT = 0 ,   /* client connection port       */
		LOGGING = 0 ,/* State of LOGGING, 1-ON 2-OFF */
		state = 0 ,
		send_ping = 0 ,
		cur = 0,
		max = 1024,
		it=0,it1=0,it2=0,
		idle =0,
		nboct=0,
		scroll=0,
		sendlimit=0,
		med_t=0,
		med_f=0,
		color=0;

		int d_it = 0, ident = -1 ;

char 	*ip = NULL,         /* ip to connect */
		*tmpstr = NULL ,
		name[ 512 ] ,        	/*client name */
		buf[ 512 ] ;           /* typing buffer */

N_STR	*in_buf = NULL ,
		*tmpbuf = NULL ,
		*namestr = NULL , 
		*passwd = NULL ,
		*netw_exchange = NULL ;		

BITMAP *scr_buf = NULL;

FILE *chatparamf = NULL;

NETWORK *n_client = NULL;

LIST *chatbuf = NULL;
N_STR *chat_line = NULL;
N_TIME timer;

LIST_NODE *node = NULL ;

int main( void )
{
	set_log_level( LOG_DEBUG );

	n_log( LOG_INFO , "\nNILOREA Client\n\nVersion: CLIENT %s ", _CLIENT_ );

	n_log( LOG_INFO , "Loading settings from file client.cfg . . .\n\n" );

	load_client_config( "client.cfg" , &ip , &PORT , &sendlimit , &LOGGING );

	n_log( LOG_INFO , "\nNetwork Config Loaded !\n" );

	if ( save_client_config( "client.cfg" , ip , PORT , sendlimit , LOGGING ) == TRUE )
		n_log( LOG_INFO , "\nNetwork Config Saved !\n" );
	else
		n_log( LOG_INFO , "\nNetwork Config Creating-saving FAILED ! FATAL ERROR! EXITING NOW !\n" );

	n_log( LOG_INFO , "Loading chat parameters...\n");

	chatparamf = fopen( "chatparam.txt" , "rt+" );

	if( !chatparamf )
	{
		n_log( LOG_INFO ,  "Error opening file chatparam.txt !\n" );
		return FALSE;
	}

	fill_str(  buf , 0 , 1024 );
	fill_str( name , 0 , 512 );

	fgets(  buf , 512 , chatparamf );

	ustrcpy( name , buf );
	uremove( name , -1 );

	fscanf( chatparamf , "%d %d %d" , &it , &it1 , &it2 );

	fclose( chatparamf );

	color = makecol( it , it1 , it2 );

	n_log( LOG_INFO , "Chat params loaded, User: %s , color=%d,%d,%d\n" , name , it , it1 , it2 );

	n_log( LOG_INFO , "Creating client: Ip %s\nPort %d\nLogging %d\nSendlimit: %d o/s\n", ip , PORT , LOGGING , sendlimit );

	/*
	 * Initialisation
	 */


	allegro_init();
	install_timer();
	install_keyboard();
	set_uformat(U_ASCII);

	/* gfx_mode( GFX_DIRECTX_WIN , 640,480,0,0,32,0,0,0,255);*/
	set_color_depth( 16 );
	gfx_mode( GFX_DIRECTX_WIN , 640,480,0,0,32,0,0,0,255);


	if (set_display_switch_mode(SWITCH_BACKGROUND) != 0) 
	{
		n_log( LOG_INFO , "Error changing switch to SWITCH_BACKGROUND ");
		if( set_display_switch_mode(SWITCH_BACKAMNESIA) !=0)
		{
			n_log( LOG_INFO , "Error changing switch mode to SWITCH_BACKGROUND");
			return FALSE;
		}
	}

	scr_buf = create_bitmap( SCREEN_W , SCREEN_H );
	if( !scr_buf )
	{
		n_log( LOG_ERR , "Error creating bitmap of size %dx%d" , SCREEN_W,SCREEN_H );
		allegro_exit();
		return FALSE;
	}
	
	Init_All_Network( 2,2);

	tmpstr = Connect_Network ( &n_client , ip , PORT );
	if( tmpstr )
	{
		n_log( LOG_INFO , "error while connecting %s port %d\n" , ip , PORT );
		allegro_exit();
		return FALSE;
	}

	n_log( LOG_INFO , "Connected !!!\n");
	
	n_log( LOG_NOTICE ,"Sending ident request with name:\"%s\"" , name );
	
	char_to_str( name , strlen( name ) , &namestr );
	char_to_str( "blankpasswd" , strlen( "blankpasswd" ) , &passwd );
	if( netw_send_ident( n_client , NETMSG_IDENT_REQUEST , 0 ,  namestr , passwd ) == FALSE )
    {
        n_log( LOG_ERR , "Sending ident request failed" );
        goto exit_client;
    }
    n_log( LOG_INFO ,"Ident request sended");

    netw_exchange = NULL ;

    while( !netw_exchange && !key[KEY_ESC])
    {
        netw_exchange = Get_Msg( n_client );

        if( netw_exchange )
        {
            break;
		}
        rest( 10 );
    }

    /* quitting if no right answer */
	if( !netw_exchange || ( netw_exchange && netw_exchange -> data == NULL ) )
		goto exit_client;
    
	netw_get_ident( netw_exchange , &d_it , &ident , &namestr , &passwd );
	n_log( LOG_INFO ,"Id Message received, pointer:%p size %d id %g",netw_exchange -> data, netw_exchange -> length , ident );
	
    if( ident == -1 )
    {
        n_log( LOG_ERR ,"No ident received");
        goto exit_client;
    }

    /* We got our id ! */
    n_log( LOG_NOTICE , "Id is:%g" , ident );

    free_str( &netw_exchange );

	chatbuf = new_generic_list( 1000 );

	n_log( LOG_INFO , "Chat Buffer Initialised\n");

	fill_str( buf , 0 , 1024 );

	n_log( LOG_INFO ,"Entering loop...\n");


	start_HiTimer( &timer );

	n_log( LOG_INFO ,"netw -> state = %d\n" , n_client -> state );
	n_client -> state = NETW_RUN ;
	n_log( LOG_INFO ,"n_client -> state = NETW_RUN ;\nnetw -> state = %d\n" , n_client -> state );

	if( Create_Send_Thread( n_client ) == TRUE )
		n_log( LOG_INFO ,"Create_Send_Thread OK , nnetw -> state = %d\n" , n_client -> state );
	else
		n_log( LOG_INFO ,"Create_Send_Thread NON OK , nnetw -> state = %d\n" , n_client -> state );
	
	
	if( Create_Recv_Thread( n_client ) == TRUE )
		n_log( LOG_INFO ,"Create_recv_Thread OK , nnetw -> state = %d\n" , n_client -> state );
	else
		n_log( LOG_INFO ,"Create_recv_Thread NON OK , nnetw -> state = %d\n" , n_client -> state );


	cur = ustrlen( name );
	ustrcpy( buf , name );

	do
	{
		elapsed_time = get_usec( &timer );

        logic_time += elapsed_time ;
        update_time += elapsed_time ;
        blit_time += elapsed_time ;
		
        auto_nice_check = 0 ;
		
		if( send_ping )
		{
			ping_time += elapsed_time ;
			
			if( ping_time > PINGTIME )
			{
				
				while( ping_time >= PINGTIME )
					ping_time -= PINGTIME;
	
				it=timeGetTime();
				netw_send_ping( n_client , NETMSG_PING_REQUEST , ident , -1 , it );
				n_log( LOG_DEBUG ,"Sended Pinq Request, type:%d from %d to %d at %d",NETMSG_PING_REQUEST,(int)ident ,-1, it );
				
			}
		}
		
        if( logic_time >= LOGICTIME )
        {
            auto_nice_check ++ ;

            while( logic_time >= LOGICTIME )
                logic_time -= LOGICTIME;
				
			if( key[KEY_PGUP] )
			{
				rest( 10 );
				it = chatbuf -> nb_items - 45;
				if( it < 0 ) it = 0;
				if( scroll < it )scroll++;
			}
			if( key[KEY_PGDN] )
			{
				rest( 10 );
				if( scroll > 0 )
					scroll--;
			}

			get_keyboard( buf , &cur , ustrsize( name ),max );

			if( key[KEY_ENTER] && cur > ustrsize( name ) )
			{
				N_STR msg ;
				msg.data = buf ;
				msg.length = strlen( buf ) ;
				Add_Msg( n_client, &msg );
				fill_str( buf , 0 , max );
				ustrcpy( buf , name  );
				cur = ustrsize( name );
			}

			in_buf = Get_Msg( n_client );

			if( in_buf )
			{
				if( list_push( chatbuf , in_buf ) == FALSE )
				{
					tmpbuf = list_shift( chatbuf , N_STR );
					free_str( &tmpbuf );
					if( list_push( chatbuf , in_buf ) == FALSE )
					{
						n_log( LOG_INFO , "Error adding %s to chat\n" , in_buf -> data );
						return FALSE;
					}
				}
				free_str( &in_buf );
			}
		}
		
		if( idle > BLITTIME )
		{
			if( key[ KEY_F11 ] && BLITTIME > 0 )
				BLITTIME -= 10;
			if( key[ KEY_F12 ] && BLITTIME < 100000 )
				BLITTIME += 10;
			
			if( gfx_status( GET , 0 ) == MODE_OGL )
				allegro_gl_set_allegro_mode();

			clear(scr_buf);
			textprintf_ex( scr_buf , font , 0 , SCREEN_H-10 , makecol(255,255,255), 1 , "%s",buf);
			rectfill( scr_buf , text_length(font, buf),SCREEN_H-10 , text_length(font, buf)+6,SCREEN_H,makecol(200,100,255));

			node = chatbuf -> end ;
			for( it = 0 ; it < scroll ; it ++)
			{
				if( node -> prev )
					node = node -> prev ;
			}

			for( it = 0; it < 45 ; it ++ )
			{
				if( node )
				{
					chat_line = (N_STR *)node -> ptr ;
					textprintf_ex( scr_buf , font , 5 , 460 - it*10 , makecol( 128+it2,128+it2,128+it2) ,1, "%s", chat_line -> data);
					if( node -> prev )
						node = node -> prev ;			
				}

				textprintf_ex( scr_buf , font , 5 , 5 , makecol( 255,255,255) ,1, "fps: %d, usec/loop: %d, %s: %d, %d item, boucle: %d",((med_f>0)?1000000/med_f:9999),med_t,name,color,n_client -> send_buf -> nb_items,BLITTIME);
				blit(scr_buf,screen,0,0,0,0,SCREEN_W,SCREEN_H);

				if( gfx_status( GET , 0 ) == MODE_OGL )
				{
					allegro_gl_unset_allegro_mode();
					allegro_gl_flip();
				}
				idle -= BLITTIME ;
				if( idle > BLITTIME )
					idle = 0 ;
				else
					rest(1);
			}
			rest( 1 );
		}
		
		if( update_time >= UPDATETIME )
        {

          	auto_nice_check ++ ;

            while( update_time >= UPDATETIME )
                update_time -= UPDATETIME ;

            /*
             * Network output processing
             */

        }
		
		if( auto_nice_check == 0 )
            rest( 1 );
        else
            rest( 0 );
			
	}while( !key[KEY_ESC] );

	exit_client:
	Stop_Network( &n_client );
	Close_Network( &n_client );

	/*
	 * Exiting 
	 */

	allegro_exit();

	return TRUE;

}END_OF_MAIN()
