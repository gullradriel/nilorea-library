/**
 *@file n_pcre.c
 *@brief PCRE helpers for regex matching
 *@author Castagnier Mickael
 *@version 2.0
 *@date 04/12/2019
 */

#include "nilorea/n_pcre.h"
#include "nilorea/n_log.h"
#include "strings.h"

#include "string.h"

/**
  From pcre doc, the flag bits are:
  PCRE_ANCHORED           Force pattern anchoring
  PCRE_AUTO_CALLOUT       Compile automatic callouts
  PCRE_BSR_ANYCRLF        \\R matches only CR, LF, or CRLF
  PCRE_BSR_UNICODE        \\R matches all Unicode line endings
  PCRE_CASELESS           Do caseless matching
  PCRE_DOLLAR_ENDONLY     $ not to match newline at end
  PCRE_DOTALL             . matches anything including NL
  PCRE_DUPNAMES           Allow duplicate names for subpatterns
  PCRE_EXTENDED           Ignore white space and # comments
  PCRE_EXTRA              PCRE extra features
  (not much use currently)
  PCRE_FIRSTLINE          Force matching to be before newline
  PCRE_JAVASCRIPT_COMPAT  JavaScript compatibility
  PCRE_MULTILINE          ^ and $ match newlines within data
  PCRE_NEWLINE_ANY        Recognize any Unicode newline sequence
  PCRE_NEWLINE_ANYCRLF    Recognize CR, LF, and CRLF as newline
  sequences
  PCRE_NEWLINE_CR         Set CR as the newline sequence
  PCRE_NEWLINE_CRLF       Set CRLF as the newline sequence
  PCRE_NEWLINE_LF         Set LF as the newline sequence
  PCRE_NO_AUTO_CAPTURE    Disable numbered capturing paren-
  theses (named ones available)
  PCRE_NO_UTF16_CHECK     Do not check the pattern for UTF-16
  validity (only relevant if
  PCRE_UTF16 is set)
  PCRE_NO_UTF32_CHECK     Do not check the pattern for UTF-32
  validity (only relevant if
  PCRE_UTF32 is set)
  PCRE_NO_UTF8_CHECK      Do not check the pattern for UTF-8
  validity (only relevant if
  PCRE_UTF8 is set)
  PCRE_UCP                Use Unicode properties for backslash-d, backslash-w, etc.
  PCRE_UNGREEDY           Invert greediness of quantifiers
  PCRE_UTF16              Run in pcre16_compile() UTF-16 mode
  PCRE_UTF32              Run in pcre32_compile() UTF-32 mode
  PCRE_UTF8               Run in pcre_compile() UTF-8 mode
 *@brief new N_PCRE object with given parameters
 *@param str The string containing the regexp
 *@param max_cap Maximum number of captures. str The string containing the regexp
 *@param flags pcre_compile flags
 *@return a filled N_PCRE struct or NULL
 */
N_PCRE* npcre_new(char* str, int max_cap, int flags) {
    N_PCRE* pcre = NULL;
    __n_assert(str, return NULL);

    Malloc(pcre, N_PCRE, 1);
    __n_assert(pcre, return NULL);

    pcre->captured = 0;

    if (max_cap == 0) {
        max_cap = 1;
    }
    pcre->match_list = NULL;

    pcre->regexp_str = strdup(str);
    __n_assert(str, npcre_delete(&pcre); return NULL);

    pcre->ovecount = max_cap;
    Malloc(pcre->ovector, int, (size_t)(3 * max_cap));
    __n_assert(pcre->ovector, npcre_delete(&pcre); return NULL);

    const char* error = NULL;
    int erroroffset = 0;

    pcre->regexp = pcre_compile(str, flags, &error, &erroroffset, NULL);
    if (!pcre->regexp) {
        n_log(LOG_ERR, " pcre compilation of %s failed at offset %d : %s", str, erroroffset, error);
        npcre_delete(&pcre);
        return FALSE;
    }
    /* no flags for study = no JIT compilation */
    pcre->extra = pcre_study(pcre->regexp, 0, &error);

    return pcre;
} /* npcre_new(...) */

/**
 *
 *@brief Free a N_PCRE pointer
 *@param pcre The N_PCRE regexp holder
 *
 *@return TRUE or FALSE
 */
int npcre_delete(N_PCRE** pcre) {
    __n_assert(pcre && (*pcre), return FALSE);

    if ((*pcre)->ovector) {
        Free((*pcre)->ovector);
    }
    if ((*pcre)->regexp) {
        pcre_free((*pcre)->regexp);
        (*pcre)->regexp = NULL;
    }
#ifdef __linux__
    pcre_free_study((*pcre)->extra);
    (*pcre)->extra = NULL;
#else
    pcre_free((*pcre)->extra);
    (*pcre)->extra = NULL;
#endif
    if ((*pcre)->captured > 0) {
        pcre_free_substring_list((*pcre)->match_list);
    }
    FreeNoLog((*pcre)->regexp_str);
    FreeNoLog((*pcre));
    return TRUE;
} /* npcre_delete (...) */

/**
 *@brief clean the match list of the last capture, if any
 *@param pcre The N_PCRE regexp holder
 *@return TRUE or FALSE
 */
int npcre_clean_match(N_PCRE* pcre) {
    __n_assert(pcre, return FALSE);
    __n_assert(pcre->match_list, return FALSE);

    if (pcre->captured > 0) {
        pcre->captured = 0;
        pcre_free_substring_list(pcre->match_list);
    }

    return TRUE;
} /* npcre_clean_match */

/**
 *@brief print error associated to pcre error code
 *@param LOG_LEVEL The log level to use when printing errors. Note: non error will always be printed in LOG_DEBUG
 *@param file name of the file which called the function
 *@param func name of the calling function
 *@param line line in the file
 *@param rc the code to test
 *@return TRUE if everything was OK and no errors, FALSE if an error was decoded, or if it couldn't get the exact error and printed default
 */
int _npcre_print_error( int LOG_LEVEL , const char *file , const char *func, int line , int rc )
{
    // not an error message
    if( rc > 0 )
    {
        _n_log(LOG_DEBUG, file, func, line, "no pcre error, match: %d elements in ovector");  
        return TRUE ;
    }

    switch (rc)
    {
        case 0:
            _n_log(LOG_LEVEL, file, func, line, "match OK, but not enough space in ovector to store all elements");  
            break;

        case PCRE_ERROR_NOMATCH:
            _n_log(LOG_LEVEL, file, func, line, "String did not match the pattern"); 
            break;
    
        case PCRE_ERROR_NULL:
            _n_log(LOG_LEVEL, file, func, line, "A NULL argument was passed"); 
            break;
    
        case PCRE_ERROR_BADOPTION:
            _n_log(LOG_LEVEL, file, func, line, "Invalid option flag passed to pcre_exec()"); 
            break;
    
        case PCRE_ERROR_BADMAGIC:
            _n_log(LOG_LEVEL, file, func, line, "Bad magic number (compiled regex corrupt?)"); 
            break;
    
        case PCRE_ERROR_UNKNOWN_OPCODE:
            _n_log(LOG_LEVEL, file, func, line, "Unknown opcode in compiled pattern (internal error)"); 
            break;
    
        case PCRE_ERROR_NOMEMORY:
            _n_log(LOG_LEVEL, file, func, line, "Out of memory"); 
            break;
    
        case PCRE_ERROR_NOSUBSTRING:
            _n_log(LOG_LEVEL, file, func, line, "Invalid substring number (pcre_get_substring())"); 
            break;
    
        case PCRE_ERROR_MATCHLIMIT:
            _n_log(LOG_LEVEL, file, func, line, "Match recursion limit reached (PCRE_EXTRA_MATCH_LIMIT)"); 
            break;
    
        case PCRE_ERROR_CALLOUT:
            _n_log(LOG_LEVEL, file, func, line, "Callout function returned non-zero"); 
            break;
    
        case PCRE_ERROR_BADUTF8:
            _n_log(LOG_LEVEL, file, func, line, "Invalid UTF-8 string in subject"); 
            break;
    
        case PCRE_ERROR_BADUTF8_OFFSET:
            _n_log(LOG_LEVEL, file, func, line, "Bad UTF-8 offset"); 
            break;
    
        case PCRE_ERROR_PARTIAL:
            _n_log(LOG_LEVEL, file, func, line, "Partial match (PCRE_PARTIAL used)"); 
            break;
    
        case PCRE_ERROR_BADPARTIAL:
            _n_log(LOG_LEVEL, file, func, line, "Partial match not allowed here"); 
            break;
    
        case PCRE_ERROR_INTERNAL:
            _n_log(LOG_LEVEL, file, func, line, "Internal PCRE error"); 
            break;
    
        case PCRE_ERROR_BADCOUNT:
            _n_log(LOG_LEVEL, file, func, line, "Bad ovector size or count"); 
            break;
    
        case PCRE_ERROR_DFA_UITEM:
            _n_log(LOG_LEVEL, file, func, line, "Item not supported in DFA mode"); 
            break;
    
        case PCRE_ERROR_DFA_UCOND:
            _n_log(LOG_LEVEL, file, func, line, "Unsupported condition in DFA mode"); 
            break;
    
        case PCRE_ERROR_DFA_UMLIMIT:
            _n_log(LOG_LEVEL, file, func, line, "DFA workspace limit reached"); 
            break;
    
        case PCRE_ERROR_DFA_WSSIZE:
            _n_log(LOG_LEVEL, file, func, line, "Insufficient DFA workspace"); 
            break;
    
        case PCRE_ERROR_DFA_RECURSE:
            _n_log(LOG_LEVEL, file, func, line, "Too much recursion in DFA matching"); 
            break;
    
        case PCRE_ERROR_RECURSIONLIMIT:
            _n_log(LOG_LEVEL, file, func, line, "Recursion depth limit reached"); 
            break;
    
        case PCRE_ERROR_NULLWSLIMIT:
            _n_log(LOG_LEVEL, file, func, line, "Null workspace limit reached (DFA)"); 
            break;
    
        case PCRE_ERROR_BADNEWLINE:
            _n_log(LOG_LEVEL, file, func, line, "Invalid newline setting"); 
            break;
    
        case PCRE_ERROR_BADOFFSET:
            _n_log(LOG_LEVEL, file, func, line, "Bad start offset"); 
            break;
    
        case PCRE_ERROR_SHORTUTF8:
            _n_log(LOG_LEVEL, file, func, line, "Truncated UTF-8 sequence"); 
            break;
    
        case PCRE_ERROR_RECURSELOOP:
            _n_log(LOG_LEVEL, file, func, line, "Infinite recursion loop detected"); 
            break;
    
        case PCRE_ERROR_JIT_STACKLIMIT:
            _n_log(LOG_LEVEL, file, func, line, "JIT stack limit exceeded"); 
            break;
    
        case PCRE_ERROR_BADMODE:
            _n_log(LOG_LEVEL, file, func, line, "Mixed JIT/DFA usage not allowed"); 
            break;
    
        case PCRE_ERROR_BADENDIANNESS:
            _n_log(LOG_LEVEL, file, func, line, "Compiled pattern endianness mismatch"); 
            break;
    
        case PCRE_ERROR_DFA_BADRESTART:
            _n_log(LOG_LEVEL, file, func, line, "Bad restart data for DFA");
            break;
    
        case PCRE_ERROR_JIT_BADOPTION:
            _n_log(LOG_LEVEL, file, func, line, "Invalid JIT option"); 
            break;
    
        case PCRE_ERROR_BADLENGTH:
            _n_log(LOG_LEVEL, file, func, line, "Bad subject length parameter");
            break;
    
        #ifdef PCRE_ERROR_UNSET
        case PCRE_ERROR_UNSET:
            _n_log(LOG_LEVEL, file, func, line, "Unset substring requested"); 
            break;
        #endif

        default:
            _n_log(LOG_LEVEL, file, func, line, "Unknown PCRE error code: %d", rc); 
            break;
    }
    return FALSE ;
}


/**
 *@brief Return TRUE if str matches regexp, and make captures up to max_cap
 *@param pcre The N_PCRE regexp holder
 *@param str String to test against the regexp
 *@return TRUE or FALSE
 */
int npcre_match(char* str, N_PCRE* pcre) {
    __n_assert(str, return FALSE);
    __n_assert(pcre, return FALSE);
    __n_assert(pcre->regexp_str, return FALSE);
    __n_assert(pcre->regexp, return FALSE);

    int rc, len = 0;
    len = (int)strlen(str);

    rc = pcre_exec(pcre->regexp, pcre->extra, str, len, 0, 0, pcre->ovector, pcre->ovecount);
    if (rc < 0) {
        switch (rc) {
            case PCRE_ERROR_NOMATCH: /* n_log( LOG_DEBUG , "String did not match the pattern");*/
                break;
            case PCRE_ERROR_NULL:
                n_log(LOG_DEBUG, "Something was null");
                break;
            case PCRE_ERROR_BADOPTION:
                n_log(LOG_DEBUG, "A bad option was passed");
                break;
            case PCRE_ERROR_BADMAGIC:
                n_log(LOG_DEBUG, "Magic number bad (compiled re corrupt?)");
                break;
            case PCRE_ERROR_UNKNOWN_NODE:
                n_log(LOG_DEBUG, "Something kooky in the compiled regexp");
                break;
            case PCRE_ERROR_NOMEMORY:
                n_log(LOG_DEBUG, "Ran out of memory");
                break;
            default:
                n_log(LOG_DEBUG, "Unknown error");
                break;
        }
        return FALSE;
    } else if (rc == 0) {
        rc = pcre->ovecount;
    }

    if (rc > 0) {
        npcre_clean_match(pcre);
        pcre->captured = rc;
        pcre_get_substring_list(str, pcre->ovector, rc, &pcre->match_list);
    }
    return TRUE;
} /* npcre_match(...) */


