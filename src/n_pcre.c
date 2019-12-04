/**\file n_pcre.h
 *  PCRE helpers for regex matching
 *\author Castagnier Mickael
 *\version 2.0
 *\date 04/12/2019
 */

#include "nilorea/n_pcre.h"
#include "nilorea/n_log.h"


/*!\fn N_PCRE *npcre_new( char *str , int max_cap , int flags )
 *
 *\brief create a compiled regex
 *\param str The string containing the regexp
 *\param max_cap Maximum number of captures. str The string containing the regexp
 *\param flags pcre_compile flags
 *
 *\return a filled N_PCRE struct or NULL
 */
N_PCRE *npcre_new( char *str, int max_cap, int flags )
{
    N_PCRE *pcre = NULL ;
    __n_assert( str, return NULL );
    __n_assert( ( max_cap > 0 ), return NULL );

    Malloc( pcre, N_PCRE, 1 );
    __n_assert( pcre, return NULL );

    pcre -> capture_done = 0 ;

    if( max_cap <= 0 )
    {
        max_cap = 1 ;
        pcre -> match_list = NULL ;
    }
    else
    {
        Malloc( pcre -> match_list,  const char *, max_cap );
        __n_assert( pcre -> match_list , delete_pcre( &pcre ); return NULL );
    }

    pcre -> regexp_str = strdup( str );
    __n_assert( str, delete_pcre( &pcre ); return NULL );

    pcre -> ovecount = max_cap ;
    Malloc( pcre -> ovector, int, 3 * max_cap );
    __n_assert( pcre -> ovector, delete_pcre( &pcre ); return NULL );

    const char *error = NULL ;
    int erroroffset = 0 ;

    pcre -> regexp = pcre_compile( str, flags,  &error, &erroroffset, NULL  ) ;
    if(  pcre -> regexp )
    {
        n_log( LOG_ERR, " pcre compilation of %s failed at offset %d : %s", str, erroroffset, error );
        delete_pcre( &pcre );
        return FALSE ;
    }
    /* no flags for study = no JIT compilation */
    pcre -> extra = pcre_study( pcre -> regexp , 0 , &error );
    if( !pcre -> extra )
    {
        n_log( LOG_DEBUG, " pcre optimisation of %s (study) returned NULL: %s", str, error );
        delete_pcre( &pcre );
        return FALSE ;
    }
    return pcre ;
}/* npcre_new(...) */



/*!\fn int npcre_match( char *str, N_PCRE *pcre )
 *
 *\brief Return TRUE if str matches regexp, and make captures up to max_cap
 *\param pcre The N_PCRE regexp holder
 *
 *\return TRUE or FALSE
 */
int npcre_match( char *str, N_PCRE *pcre )
{
    __n_assert( str, return FALSE );
    __n_assert( pcre -> regexp_str, return FALSE );
    __n_assert( pcre -> regexp, return FALSE );

    int rc, len = 0;
    len = ( int )strlen( str ) ;

    rc = pcre_exec( pcre -> regexp , pcre -> extra , str , len , 0 , 0 , pcre -> ovector , pcre -> ovecount );
    if ( rc < 0 )
    {
        switch( rc )
        {
        case PCRE_ERROR_NOMATCH      :
            n_log( LOG_ERR, "String did not match the pattern");
            break;
        case PCRE_ERROR_NULL         :
            n_log( LOG_ERR, "Something was null");
            break;
        case PCRE_ERROR_BADOPTION    :
            n_log( LOG_ERR, "A bad option was passed");
            break;
        case PCRE_ERROR_BADMAGIC     :
            n_log( LOG_ERR, "Magic number bad (compiled re corrupt?)");
            break;
        case PCRE_ERROR_UNKNOWN_NODE :
            n_log( LOG_ERR, "Something kooky in the compiled regexp");
            break;
        case PCRE_ERROR_NOMEMORY     :
            n_log( LOG_ERR, "Ran out of memory");
            break;
        default                      :
            n_log( LOG_ERR, "Unknown error");
            break;
        }
        return FALSE ;
    }
    else if( rc == 0 )
    {
        rc = pcre -> ovecount ;
    }

    if( rc > 0 && pcre -> match_list != NULL )
    {
        if( pcre -> capture_done == 1 )
        {
            pcre_free_substring_list( pcre -> match_list );
        }
        else
        {
            pcre -> capture_done = 1 ;
        }
        pcre_get_substring_list( str , pcre -> ovector , rc , &pcre -> match_list );

    }
    return TRUE ;
}/* npcre_match(...) */


/*!\fn int npcre_delete( N_PCRE ** pcre )
 *
 *\brief Free a N_PCRE pointer
 *\param pcre The N_PCRE regexp holder
 *
 *\return TRUE or FALSE
 */
int npcre_delete( N_PCRE ** pcre )
{
    __n_assert( (*pcre), return FALSE );

    if( (*pcre) -> ovector )
    {
        Free( (*pcre) -> ovector );
    }
    if( (*pcre) -> regexp )
    {
        pcre_free( (*pcre) -> regexp );
        (*pcre) -> regexp = NULL ;
    }
#ifndef __windows__
    pcre_free_study(  (*pcre) -> extra );
    (*pcre) -> extra = NULL ;
#else
    pcre_free( (*pcre) -> extra );
    (*pcre) -> extra = NULL ;
#endif
    if( (*pcre) -> match_list )
    {
        pcre_free_substring_list( (*pcre) -> match_list );
        (*pcre) -> match_list = NULL ;
    }
    free( (*pcre) );
    return TRUE ;
} /* npcre_delete (...) */
