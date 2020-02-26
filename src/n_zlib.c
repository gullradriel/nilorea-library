/**\file n_zlib.c
 *  ZLIB compression handler
 *\author Castagnier Mickael
 *\version 1.0
 *\date 27/06/2017
 */


#include "nilorea/n_zlib.h"
#include "nilorea/n_log.h"
#include "limits.h"

#ifndef __windows__
#include <arpa/inet.h>
#else
#include <stdint.h>
#include <winsock2.h>
#endif


/*!\fn int GetMaxCompressedLen( int nLenSrc )
*\brief Return the maximum compressed size
*\param nLenSrc
*\return The size in bytes
*/
int GetMaxCompressedLen( int nLenSrc )
{
    int n16kBlocks = (nLenSrc+16383) / 16384; // round up any fraction of a block
    return ( nLenSrc + 6 + (n16kBlocks*5) );
} /* GetMaxCompressedLen */



/*!\fn int CompressData( unsigned char *abSrc, int nLenSrc, unsigned char *abDst, int nLenDst )
*\brief Compress a string to another
*\param abSrc source string
*\param nLenSrc size of source string
*\param abDst destination string
*\param nLenDst destination length
*\return -1 or lenght of output
*/
int CompressData( unsigned char *abSrc, int nLenSrc, unsigned char *abDst, int nLenDst )
{
    z_stream zInfo = {0};
    zInfo.total_in=  zInfo.avail_in = nLenSrc;
    zInfo.total_out= zInfo.avail_out= nLenDst;
    zInfo.next_in= (unsigned char *)abSrc;
    zInfo.next_out= abDst;

    int nErr, nRet= -1;
    nErr= deflateInit( &zInfo, Z_DEFAULT_COMPRESSION ); // zlib function
    if ( nErr == Z_OK )
    {
        nErr= deflate( &zInfo, Z_FINISH );              // zlib function
        if ( nErr == Z_STREAM_END )
        {
            nRet= zInfo.total_out;
        }
    }
    deflateEnd( &zInfo );    // zlib function
    return( nRet );
} /* CompressData */



/*!\fn int UncompressData( unsigned char *abSrc, int nLenSrc, unsigned char *abDst, int nLenDst )
*\brief Uncompress a string to another
*\param abSrc source string
*\param nLenSrc size of source string
*\param abDst destination string
*\param nLenDst destination length
*\return -1 or lenght of output
*/
int UncompressData( unsigned char *abSrc, int nLenSrc, unsigned char *abDst, int nLenDst )
{
    z_stream zInfo = {0};
    zInfo.total_in=  zInfo.avail_in=  nLenSrc;
    zInfo.total_out= zInfo.avail_out= nLenDst;
    zInfo.next_in= (unsigned char *)abSrc;
    zInfo.next_out= abDst;

    int nErr, nRet= -1;
    nErr= inflateInit( &zInfo );               // zlib function
    if ( nErr == Z_OK )
    {
        nErr= inflate( &zInfo, Z_FINISH );     // zlib function
        nRet= zInfo.total_out;
        if ( nErr != Z_STREAM_END && nErr != Z_OK )
        {
            n_log( LOG_ERR, "ZLIB:inflate: %s (%d)", zError( nErr ) );
        }
    }
    else
    {
        n_log( LOG_ERR, "ZLIB:inflateInit: %s (%d)", zError( nErr ) );
    }
    inflateEnd( &zInfo );   // zlib function
    return( nRet ); // -1 or len of output
} /* UncompressData */



/*!\fn N_STR *zip_nstr( N_STR *src )
*\param src The source string
*\return The compressed string or NULL
*/
N_STR *zip_nstr( N_STR *src )
{
    int zip_max_size = GetMaxCompressedLen( src -> length );
    /* storage for original string size + zipped string  + padding */
    N_STR *zipped = new_nstr( 4 + zip_max_size + 64 );
    /* copying size */
    int32_t src_length = htonl( src -> length );
    memcpy( zipped -> data, &src_length, sizeof( int32_t ) );

    char *dataptr =  zipped -> data + 4 ;
    zipped -> written = 8 + CompressData( (unsigned char *)src -> data, src -> written, (unsigned char *)dataptr, zip_max_size );
    n_log( LOG_DEBUG, "Size: zip :%d original:%d", zipped -> written, src -> length );

    return zipped ;
} /* zip_nstr */



/*!\fn N_STR *unzip_nstr( N_STR *src )
*\param src The source string
*\return The uncompressed string or NULL
*/
N_STR *unzip_nstr( N_STR *src )
{
    int32_t original_size = 0 ;
    memcpy( &original_size, src -> data, sizeof( int32_t ) );
    original_size = ntohl( original_size );
    /* storage for original string size + zipped string  + padding */
    N_STR *unzipped = new_nstr( original_size + 64 );
    /* copying size */
    unzipped -> written = UncompressData( ((unsigned char *)src -> data) + 4, src -> written, (unsigned char *)unzipped -> data, original_size + 64 );
    n_log( LOG_DEBUG, "Size: zip: %d => unzip :%d original:%d", src -> written, unzipped -> written, original_size );

    return unzipped ;
} /* unzip_nstr */
