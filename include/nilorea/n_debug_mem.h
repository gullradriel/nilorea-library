/*!\file n_debug_mem.h
 * Helpers for debugging memory
 *\author Castagnier Mickael
 *\date 2014-09-10
 */

#ifndef N_DEBUG_MEM_HEADER_GUARD
#define N_DEBUG_MEM_HEADER_GUARD

#ifdef __cplusplus
extern "C"
{
#endif

/**\defgroup DEBUGMEMCORE MEMORY DEBUG: leaked memory checker core files
   \addtogroup DEBUGMEMCORE 
  @{
*/

#include <stdlib.h>
#include <pthread.h>

#ifdef DEBUG_MEM
#undef DEBUG_MEM
   #include "n_hash.h"
#define DEBUG_MEM
#else
   #include "n_hash.h"
#endif

   /*! max memory dumped by leaked unit */
#define DEBUG_MEM_DUMP_MAX_LEN 256

   /*! structure of a debug unit */
   typedef struct DEBUG_MEM_ITEM
   {
      /*! address of the tracked pointer */
      void *ptr ;
      /*! size of the pointer */
      size_t size ;
      /*! file of the call generating that pointer */
      char *file ;
      /*! function in the file of the call generating that pointer */
      char *func ;
      /*! line of the call generating the pointer */
      int line ;

   }DEBUG_MEM_ITEM ;

   /*! structure of a table of DEBUG_MEM_ITEM */
   typedef struct DEBUG_MEM_TABLE
   {
      /*! track of the number of allocated */
      int nb_malloc ;
      /*! track of the number of freed */
      int nb_free ;
      /*! table of tracked elements */
      HASH_TABLE *table ;
      /*! lock for concurrent access to the table */
      pthread_mutex_t lock ;

   }DEBUG_MEM_TABLE ;

   /*! external table for tracking by file usage */
   extern DEBUG_MEM_TABLE *debug_mem_table ;

#ifdef DEBUG_MEM
   /* initialize debug tables */
   int init_debug_mem();
   /* new debug item */
   DEBUG_MEM_ITEM *new_db_mem_item( void *ptr , size_t size , int line , char *file , char *func );
   /* delete debug item */
   void delete_db_mem_item( void *ptr );
   /* debug malloc full  */ 
   FORCE_INLINE void *db_mem_alloc( size_t size , int line , char *file , const char *func );
   /* debug malloc */ 
   FORCE_INLINE void *db_malloc( size_t size );
   /* debug free */ 
   FORCE_INLINE void db_mem_free( void *ptr );
   /* debug dump ! */ 
   int db_mem_dump_leaked( char *file );
#endif

/*@}*/

#ifdef __cplusplus
}
#endif

#endif
