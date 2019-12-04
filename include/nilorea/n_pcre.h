/**\file n_pcre.h
 *  N_STR and string function declaration
 *\author Castagnier Mickael
 *\version 2.0
 *\date 05/02/14
 */


#ifndef __NILOREA_PCRE_HELPERS__
#define __NILOREA_PCRE_HELPERS__

#ifdef __cplusplus
extern "C"
{
#endif

#include <pcre.h>

/**\defgroup PCRE: regex matching helpers
  \addtogroup PCRE
  @{
  */


#define pcre_foreach( __PCRE_ , __ITEM_ ) \
   if( !__PCRE_ || !__PCRE_ -> match_list ) \
   { \
      n_log( LOG_ERR , "Error in pcre_foreach, %s is NULL" , #__PCRE_ ); \
   } \
   else \
   for( int __ITEM_it = 0 , char *__ITEM_ = __PCRE_ -> match_list[ __ITEM_it ] ; __ITEM_ != NULL , __ITEM_it < __PCRE_ -> max_cap ; __ITEM_it++ ) \
   {


/*! N_PCRE structure */
typedef struct N_PCRE
{
    /*! original regexp string */
    char *regexp_str ;
    /*! regexp */
    pcre *regexp;
    /*! optimization if any */
    pcre_extra *extra;

    /*! configured maximum number of matched occurence */
    int ovecount,
        /*! list of indexes */
        *ovector ;
    /*! populated match list if nPcreCapMatch is called */
    const char** match_list;
    /*! flag for match_list cleaning */
    int capture_done ;
} N_PCRE ;



/* pcre helper, compile and optimize regexp */
N_PCRE *npcre_new( char *str, int max_cap, int flags );
/* pcre helper, match a regexp against a string */
int npcre_match( char *str, N_PCRE *pcre );
/* pcre helper, match a regexp against a string */
int delete_pcre( N_PCRE ** pcre );


/*@}*/
#ifdef __cplusplus
}
#endif
/* #ifndef N_STR*/
#endif
