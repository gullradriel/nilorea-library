/**\file n_gfx.c
 *  Graphic mode and helper
 *\author Castagnier Mickael
 *\version 2.0
 *\date 17/03/2015
 */

#include "nilorea/n_gfx.h"

#ifndef NOISOENGINE

/*!\fn int gfx_status( int ACTION , int value )
 *\brief status of opengl & or directx
 *\param ACTION set it to GET or SET
 *\param value if action is set to SET, then status take the value 'value' else nothing is done with 'value'
 *\return the gfx_status, MODE_OGL or MODE_DX
 */
int gfx_status( int ACTION , int value )
{

   static int STATUS = MODE_DX;

   if ( ACTION == SET ) {
      if ( value == MODE_DX )
         STATUS = MODE_DX;

      if ( value == MODE_OGL )
         STATUS = MODE_OGL;
   }
   return STATUS;
} /* gfx_status(...) */



/*!fn int gfx_mode( int card , int W , int H , int VW , int VH , int depth , float r , float g , float b , float alpha )
 *\brief init and make a clear of the grafic screen
 *\param card type of GFX_MODE wanted
 *\param W width size of screen
 *\param H heigh size of screen
 *\param VW virtual width size of screen
 *\param VH virtual heigh size of screen
 *\param depth depth wanted
 *\param r red color of the first cleared screen
 *\param g green color of the first cleared screen
 *\param b blue color of the first cleared screen
 *\param alpha alpha color of the first cleared screen
 *\return TRUE or FALSE
 */
int gfx_mode( int card , int W , int H , int VW , int VH , int depth , float r , float g , float b , float alpha )
{
   set_color_depth( depth );
   /*set_color_conversion( COLORCONV_KEEP_TRANS );*/

   if ( card == GFX_OPENGL || card == GFX_OPENGL_FULLSCREEN || card == GFX_OPENGL_WINDOWED ) 
   {
      install_allegro_gl();
      allegro_gl_clear_settings();
      allegro_gl_set ( AGL_COLOR_DEPTH , depth );
      allegro_gl_set ( AGL_DOUBLEBUFFER , 1 );
      allegro_gl_set ( AGL_RENDERMETHOD , 1 );
      allegro_gl_set ( AGL_REQUIRE, AGL_RENDERMETHOD | AGL_COLOR_DEPTH | AGL_DOUBLEBUFFER  );
      if( set_gfx_mode( card, W, H, VW, VH ) < 0 )
      {
         n_log( LOG_ERR , "Error setting %d mode in %d / %d / %d !\n" , card , W , H , depth );
         exit( EXIT_FAILURE );
      }
      allegro_gl_use_alpha_channel( TRUE );
      allegro_gl_use_mipmapping( TRUE );
      glClearColor( r, g, b, alpha );
      glDisable( GL_DEPTH_TEST );
      glDisable( GL_LIGHTING );
      glMatrixMode( GL_PROJECTION );
      glLoadIdentity();
      glOrtho( 0, W, H, 0, -1, 1 );
      glMatrixMode( GL_MODELVIEW );
      glLoadIdentity();
      glBlendFunc( GL_SRC_ALPHA, GL_ONE );
      /* Skip pixels which alpha channel is lower than 0.5*/
      /*glAlphaFunc( GL_GREATER , 0.5 );*/
      glEnable( GL_TEXTURE_2D );
      /* setting gfx_status to MODE_OGL */
      gfx_status( SET , MODE_OGL );
      allegro_font = allegro_gl_convert_allegro_font_ex( font, AGL_FONT_TYPE_TEXTURED, 16.0, GL_ALPHA8 );
   }
   else
   {   if ( set_gfx_mode( card, W, H, VW, VH ) < 0 ) 
      {
         n_log( LOG_ERR , "Error setting %d mode in %d / %d / %d !\n" , card , W , H , depth );
         exit( EXIT_FAILURE );
      }
      /* setting gfx_status to MODE_DIRECTX */
      gfx_status( SET , MODE_DX );

   }

   if( set_display_switch_mode( SWITCH_BACKGROUND ) != 0 )
   {
      n_log( LOG_ERR , "Warning: can not change switch mode to SWITCH_BACKGROUND" );
      if( set_display_switch_mode( SWITCH_BACKAMNESIA ) !=0)
      {
         n_log( LOG_ERR , "Error: can not change switch mode to BACKAMNESIA" );
         return FALSE;
      }
   }
   n_log( LOG_INFO , "background running allowed" );

   return TRUE;
} /* gfx_mode(...) */




/*!\fn Force32BitBmpToAlpha( BITMAP *bmp )
 *\brief Force if already not a 32bpp bmp to be the same with an alpha channel
 *\param bmp the bitmap to check
 *\return TRUE or FALSE
 */
int Force32BitBmpToAlpha( BITMAP *bmp )
{
   int x, y;
   int r, g, b, alpha;
   int col, mask;

   __n_assert( bmp , return FALSE );

   acquire_bitmap( bmp );
   mask = bitmap_mask_color( bmp );

   /* First check if an alpha channel already exists */
   for ( y = 0; y < bmp->h; ++y ) {
      for ( x = 0; x < bmp->w; ++x ) {
         col = getpixel( bmp, x, y );
         alpha = geta( col );

         if ( alpha != 0 && col != mask ) {
            release_bitmap( bmp );
            return FALSE;
         }
      }
   }

   for ( y = 0; y < bmp->h; ++y ) {
      for ( x = 0; x < bmp->w; ++x ) {
         col = getpixel( bmp, x, y );
         r = getr( col );
         g = getg( col );
         b = getb( col );
         putpixel( bmp, x, y, makeacol( r, g, b, 255 ) );
      }
   }
   release_bitmap( bmp );
   return TRUE ;
} /* Force32BitBmpToAlpha */ 



/*!\fn force_color_to_be( BITMAP *bmp , int oldcolor , int newcolor )
 *\brief Change all the bmp pixel of the same color src to color dst
 *\param bmp the bitmap where the effect take place
 *\param oldcolor the color to change
 *\param newcolor the new color of oldcolor ;)
 */
void force_color_to_be( BITMAP *bmp , int oldcolor , int newcolor )
{
   int x , y ;
   if ( bmp ) {
      for ( x = 0 ; x < bmp -> w ; x ++ )
         for ( y = 0 ; y < bmp -> h ; y ++ )
            if ( getpixel( bmp , x , y ) == oldcolor )
               putpixel( bmp, x, y, newcolor );
   }
} /* force color_to_be(...) */



/*!\fn ogl_blit( GLuint mytexture , int x , int y , int r , int g , int b , int alpha , int w , int h , int mode )
 *\brief Draw a given texture to the screen at position x,y and of size w,h
 *\param mytexture the opengl texture index of texture to use
 *\param x x position
 *\param y y position
 *\param r Red Color
 *\param g Green Color
 *\param b Blue Color
 *\param alpha Alpha Color
 *\param w Width of the blit
 *\param h Height of the blit
 *\param mode 0 normal , 1 trans
 *\return NOTHING it's void
 */
void ogl_blit( GLuint mytexture , int x , int y , int r , int g , int b , int alpha , int w , int h , int mode )
{
   if( mode == 1 )
   {
      glBindTexture( GL_TEXTURE_2D, mytexture );

      glBegin( GL_QUADS );
      /*Draw our four points, clockwise.*/
      glColor4f( r, g, b, alpha );
      glTexCoord2f( 0, 1 );
      glVertex3f( x, y, 0 );
      glTexCoord2f( 1, 1 );
      glVertex3f( x + w, y, 0 );
      glTexCoord2f( 1, 0 );
      glVertex3f( x + w, y + h, 0 );
      glTexCoord2f( 0, 0 );
      glVertex3f( x, y + h , 0 );
      glEnd();

      /*binding the default texture*/
      glBindTexture( GL_TEXTURE_2D, 0 );
   } 
   else
   {
      /* binding the texture */
      glBindTexture( GL_TEXTURE_2D, mytexture );

      glEnable( GL_BLEND );

      glBegin( GL_QUADS );
      /*If we set the color to white here, then the textured quad won't be*/
      /*tinted red or half-see-through or something when we draw it based on*/
      /*the last call to glColor*().*/
      glColor3f( r, g, b );

      /*Draw our four points, clockwise.*/
      glTexCoord2f( 0, 1 );
      glVertex3f( x, y, 0 );
      glTexCoord2f( 1, 1 );
      glVertex3f( x + w, y, 0 );
      glTexCoord2f( 1, 0 );
      glVertex3f( x + w, y + h, 0 );
      glTexCoord2f( 0, 0 );
      glVertex3f( x, y + h , 0 );
      glEnd();

      glDisable( GL_BLEND );


      /*binding the default texture*/
      glBindTexture( GL_TEXTURE_2D, 0 );
   } 
} /* ogl_blit(...) */

#endif /* #ifndef NOISOENGINE */

