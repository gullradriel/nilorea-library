/*!\file n_debug_mem_h.h
 * Helpers for debugging memory
 *\author Castagnier Mickael
 *\date 2014-09-10
 */

#ifndef N_DEBUG_MEM_H_HEADER_GUARD
#define N_DEBUG_MEM_H_HEADER_GUARD

#ifdef __cplusplus
extern C
{
#endif

#include "n_debug_mem.h"

#ifdef DEBUG_MEM
    /*! macro substitution for debug malloc */
#define malloc( size ) db_mem_alloc( (size) , __LINE__, __FILE__, __func__ )
    /*! macro substitution for debug free */
#define free( ptr ) db_mem_free( (ptr) )
#endif

#ifdef __cplusplus
}
#endif

#endif
