/**\file n_pcre.h
 *  PCRE helpers for regex matching
 *\author Castagnier Mickael
 *\version 2.0
 *\date 04/12/2019
 */

#include "nilorea/n_pcre.h"
#include "nilorea/n_log.h"
#include "strings.h"

#include "string.h"


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

    Malloc( pcre, N_PCRE, 1 );
    __n_assert( pcre, return NULL );

    pcre -> captured = 0 ;

    if( max_cap <= 0 )
    {
        max_cap = 1 ;
    }
    pcre -> match_list = NULL ;

    pcre -> regexp_str = strdup( str );
    __n_assert( str, npcre_delete( &pcre ); return NULL );

    pcre -> ovecount = max_cap ;
    Malloc( pcre -> ovector, int, 3 * max_cap );
    __n_assert( pcre -> ovector, npcre_delete( &pcre ); return NULL );

    const char *error = NULL ;
    int erroroffset = 0 ;

    pcre -> regexp = pcre_compile( str, flags,  &error, &erroroffset, NULL  ) ;
    if( !pcre -> regexp )
    {
        n_log( LOG_ERR, " pcre compilation of %s failed at offset %d : %s", str, erroroffset, error );
        npcre_delete( &pcre );
        return FALSE ;
    }
    /* no flags for study = no JIT compilation */
    pcre -> extra = pcre_study( pcre -> regexp, 0, &error );

    return pcre ;
}/* npcre_new(...) */



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
    }
    FreeNoLog( (*pcre) -> regexp_str );
    FreeNoLog( (*pcre) );
    return TRUE ;
} /* npcre_delete (...) */




/*!\fn int npcre_clean_match( N_PCRE *pcre )
 *\brief clean the match list of the last capture, if any
 *\param pcre The N_PCRE regexp holder
 *\return TRUE or FALSE
 */
int npcre_clean_match( N_PCRE *pcre )
{
    __n_assert( pcre, return FALSE );
    __n_assert( pcre -> match_list, return FALSE );

    pcre -> captured = 0 ;
    pcre_free_substring_list( pcre -> match_list );

    return TRUE ;
} /* npcre_clean_match */


/*!\fn int npcre_match( char *str, N_PCRE *pcre )
 *\brief Return TRUE if str matches regexp, and make captures up to max_cap
 *\param pcre The N_PCRE regexp holder
 *\param str String to test against the regexp
 *\return TRUE or FALSE
 */
int npcre_match( char *str, N_PCRE *pcre )
{
    __n_assert( str, return FALSE );
    __n_assert( pcre, return FALSE );
    __n_assert( pcre -> regexp_str, return FALSE );
    __n_assert( pcre -> regexp, return FALSE );

    int rc, len = 0;
    len = ( int )strlen( str ) ;

    rc = pcre_exec( pcre -> regexp, pcre -> extra, str, len, 0, 0, pcre -> ovector, pcre -> ovecount );
    if ( rc < 0 )
    {
        switch( rc )
        {
        case PCRE_ERROR_NOMATCH      :
            n_log( LOG_DEBUG, "String did not match the pattern");
            break;
        case PCRE_ERROR_NULL         :
            n_log( LOG_DEBUG, "Something was null");
            break;
        case PCRE_ERROR_BADOPTION    :
            n_log( LOG_DEBUG, "A bad option was passed");
            break;
        case PCRE_ERROR_BADMAGIC     :
            n_log( LOG_DEBUG, "Magic number bad (compiled re corrupt?)");
            break;
        case PCRE_ERROR_UNKNOWN_NODE :
            n_log( LOG_DEBUG, "Something kooky in the compiled regexp");
            break;
        case PCRE_ERROR_NOMEMORY     :
            n_log( LOG_DEBUG, "Ran out of memory");
            break;
        default                      :
            n_log( LOG_DEBUG, "Unknown error");
            break;
        }
        return FALSE ;
    }
    else if( rc == 0 )
    {
        rc = pcre -> ovecount ;
    }

    if( rc > 0 )
    {
        npcre_clean_match( pcre );
        pcre -> captured = rc ;
        pcre_get_substring_list( str, pcre -> ovector, rc, &pcre -> match_list );
    }
    return TRUE ;
}/* npcre_match(...) */



