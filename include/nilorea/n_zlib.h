/*!@file n_zlib.h
 * ZLIB compression handler
 *@author Castagnier Mickael
 *@version 1.0
 *@date 27/06/2017
 */

#ifndef __N__Z_LIB_HEADER
#define __N__Z_LIB_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <zlib.h>
#include "n_str.h"

/**@defgroup ZLIB ZLIB: shortcuts to easy compress/decompress data
  @addtogroup ZLIB
  @{
  */

size_t GetMaxCompressedLen(size_t nLenSrc);
size_t CompressData(unsigned char* abSrc, size_t nLenSrc, unsigned char* abDst, size_t nLenDst);
size_t UncompressData(unsigned char* abSrc, size_t nLenSrc, unsigned char* abDst, size_t nLenDst);

N_STR* zip_nstr(N_STR* src);
N_STR* unzip_nstr(N_STR* src);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif  // header guard
