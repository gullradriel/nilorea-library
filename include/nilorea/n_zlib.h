/*!\file n_zlib.h
 *
 *  ZLIB compression handler
 *
 *\author Castagnier Mickael
 *
 *\version 1.0
 *
 *\date 27/06/2017
 */

#ifndef __N__Z_LIB_HEADER
#define __N__Z_LIB_HEADER

#ifdef __cplusplus
extern "C"
{
#endif

#include <zlib.h>
#include "n_str.h"

/**\defgroup N_TIME N_TIME: generally used headers, defines, timers, allocators,...
  \addtogroup N_TIME
  @{
  */

int GetMaxCompressedLen( int nLenSrc );
int CompressData(   unsigned char *abSrc, int nLenSrc, unsigned char *abDst, int nLenDst );
int UncompressData( unsigned char *abSrc, int nLenSrc, unsigned char *abDst, int nLenDst );

N_STR *zip_nstr( N_STR *src );
N_STR *unzip_nstr( N_STR *src );

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif
