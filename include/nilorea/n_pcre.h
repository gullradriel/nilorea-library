/**
 *@file n_pcre.h
 *@brief PCRE helpers for regex matching
 *@author Castagnier Mickael
 *@version 2.0
 *@date 04/12/2019
 */

#ifndef __NILOREA_PCRE_HELPERS__
#define __NILOREA_PCRE_HELPERS__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __sun
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

/*! PCRE error logging function wrapper to get line and func */
#define npcre_print_error( __LEVEL__ , __rc__ )                              \
    do {                                                                   \
        _npcre_print_error(__LEVEL__, __FILE__, __func__, __LINE__, __rc__); \
    } while (0)

/**@defgroup PCRE PCREs: regex matching helpers
  @addtogroup PCRE
  @{
  */

/*! N_PCRE structure */
typedef struct N_PCRE {
    /*! original regexp string */
    char* regexp_str;
    /*! regexp */
    pcre* regexp;
    /*! optimization if any */
    pcre_extra* extra;

    /*! configured maximum number of matched occurence */
    int ovecount,
        /*! list of indexes */
        *ovector;
    /*! populated match list if nPcreCapMatch is called */
    const char** match_list;
    /*! flag for match_list cleaning */
    int captured;
} N_PCRE;

/* pcre helper, compile and optimize regexp */
N_PCRE* npcre_new(char* str, int max_cap, int flags);
/* pcre helper, match a regexp against a string */
int npcre_delete(N_PCRE** pcre);
/* pcre helper, match a regexp against a string */
int npcre_clean_match(N_PCRE* pcre);
/* pcre helper, match a regexp against a string */
int npcre_match(char* str, N_PCRE* pcre);
/* print error helper */
int _npcre_print_error( int LOG_LEVEL , const char *file , const char *func, int line , int rc );

/**
@}
*/

#ifdef __cplusplus
}
#endif
/* #ifndef N_STR*/
#endif
