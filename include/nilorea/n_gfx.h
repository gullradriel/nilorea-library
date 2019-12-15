/**\file n_gfx.h
 *  Graphic mode and helpers functions
 *\author Castagnier Mickael
 *\version 1.0
 *\date 28/07/05
 *
 */


#ifndef __N_GRAFIC__
#define __N_GRAFIC__

#ifdef __cplusplus
extern "C"
{
#endif

/**\defgroup GFX GFX: graphic mode utilities
   \addtogroup GFX
  @{
*/

#include "n_log.h"

#ifndef NOALLEGRO
//#include <alleggl.h>

/*! OpenGL mode flag for grafic_status */
#define MODE_OGL 1
/*! DirectX mode flag for grafic_status */
#define MODE_DX  2

/* status of opengl & or directx */
int gfx_status( int ACTION, int value );
/* init gfx mode */
int gfx_mode( int card, int W, int H, int VW, int VH, int depth, float r, float g, float b, float alpha );

/* Force if already not a 32bpp bmp to be the same with an alpha channel */
int Force32BitBmpToAlpha( BITMAP *bmp );
/* Change all the bmp pixel of the same color src to color dst */
void force_color_to_be( BITMAP *bmp, int oldcolor, int newcolor );
/* ogl helper drawer */
//void ogl_blit( GLuint mytexture, int x, int y, int r, int g, int b, int alpha, int w, int h, int mode );

/*@}*/

#endif /* NOALLEGRO */

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __N_GRAFIC__  */
