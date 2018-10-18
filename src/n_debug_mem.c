/*!\file n_debug_mem.c
 * Helpers for debugging memory
 *\author Castagnier Mickael
 *\date 2014-10-10
 */

#include "nilorea/n_debug_mem.h"

/*! global table of allocation */
static DEBUG_MEM_TABLE __debug_mem_table ;


/*!\fn int init_debug_mem()
 *\brief initialize debug tables
 *\return TRUE or FALSE 
 */
int init_debug_mem()
{
   __debug_mem_table . nb_malloc = 0 ;
   __debug_mem_table . nb_free = 0 ;

   /* A huge table because there could be a ton of memory allocation */
   __debug_mem_table . table = new_ht( 1048576 );
   if( ! __debug_mem_table . table )
   {
      n_log( LOG_ERR , "Unable to allocate a 1048576 item hash table" );
      return FALSE ;
   }

   if( pthread_mutex_init( &__debug_mem_table . lock , NULL ) != 0 )
   {
      n_log( LOG_ERR , "Unable to initialize lock" );
      return FALSE ;
   }

   return TRUE ;
} /* init_debuf_mem() */



/*!\fn DEBUG_MEM_ITEM *new_db_mem_item( void *ptr , size_t size , int line , char *file , char *func )
 *\brief allocate a new item in the debug table
 *\param ptr Pointer address to save
 *\param size the size of the allocated data
 *\param line line of the allocation
 *\param file file of the allocation
 *\param func function where the allocation was made
 *\return A new item or NULL
 */
DEBUG_MEM_ITEM *new_db_mem_item( void *ptr , size_t size , int line , char *file , char *func )
{
   DEBUG_MEM_ITEM *item = NULL ;

   item = (DEBUG_MEM_ITEM *)calloc( 1 , sizeof( DEBUG_MEM_ITEM ) );
   if( !item )
   {
      n_log( LOG_ERR , "Unable to allocate a DEBUG_MEM_ITEM" );
      return NULL ;
   }
   item -> ptr = ptr ;
   item -> size = size ;
   item -> line = line ;
   item -> file = file ;
   item -> func = func ;

   return item ;
} /* new_db_item() */



/*!\fn void delete_db_mem_item( void *item )
 *\brief delete a debug meme item
 *\param item the item to destroy
 */
void delete_db_mem_item( void *ptr )
{
   DEBUG_MEM_ITEM *item = (DEBUG_MEM_ITEM *)ptr ;  
   if( item )
   {
      FreeNoLog( item -> file );
      FreeNoLog( item -> func );
      FreeNoLog( item );
   }
} /* delete_db_mem_item */



/*!\fn void *db_mem_alloc( size_t size , int line , char *file , const char *func )
 *\brief allocate a new pointer with size and also put it in the debug table
 *\param size the size of the allocated data
 *\param line line of the allocation
 *\param file file of the allocation
 *\param func function where the allocation was made
 *\return A new item or NULL
 */
void *db_mem_alloc( size_t size , int line , char *file , const char *func )
{
   DEBUG_MEM_ITEM *item = NULL ;
   char key[ 512 ] = "";
   void *ptr = NULL ;

   __debug_mem_table . nb_malloc ++ ;

   ptr = calloc( 1 , size );
   if( ptr )
   {
      item = new_db_mem_item( ptr , size , line , strdup( file ) , strdup( func ) );
      sprintf( key , "%p" , ptr );
      ht_put_ptr( __debug_mem_table . table , key , item , delete_db_mem_item );
      n_log( LOG_DEBUG , "Added %p size %zu file %s func %s line %d" , item -> ptr , item -> size , item -> file , item -> func , item -> line );
   }
   else
   {
      n_log( LOG_ERR , "Unable to allocate %d oct at file %s func %s line %d " , size , file , func , line );
   }
   return ptr ;
}



/*!\fn void *db_malloc( size_t size )
 *\brief return a new allocated element 
 *\param size the size of the element
 *\return Allocated delement or NULL
 */
void *db_malloc( size_t size )
{
   return db_mem_alloc( size , __LINE__, __FILE__, __func__ );
} /* db_malloc */



/*!\fn void *db_mem_free( void *ptr )
 *\brief Free a pointer and also remove it from debug table 
 *\param ptr Pointer to free
 */
void db_mem_free( void *ptr )
{
   void *hashptr = NULL ; 
   char key[ 512 ] = "";

   sprintf( key , "%p" , ptr );
   ht_get_ptr( __debug_mem_table . table , key , &hashptr );
   if( hashptr )
   {
      DEBUG_MEM_ITEM *item = hashptr ;
      n_log( LOG_DEBUG , "Removed %p size %zu file %s func %s line %d" , item -> ptr , item -> size , item -> file , item -> func , item -> line );
      delete_db_mem_item( item );
      item = NULL ;
      ht_remove( __debug_mem_table . table , key );
      __debug_mem_table . nb_free ++ ;
   }
   Free( ptr );
} /* db_mem_free() */



/*!\fn int db_mem_dump_leaked( char *file )
 *\brief Dump the current debug memory table
 *\param file The name of the logfile
 *\return TRUE or FALSE
 */
int db_mem_dump_leaked( char *file )
{
   FILE *out = NULL ;
   LIST_NODE *node = NULL ;
   HASH_NODE *hash_entry = NULL ;
   DEBUG_MEM_ITEM *item = NULL ;
   size_t len = 0 ;

   out = fopen( file , "w" );
   if( !out )
   {
      n_log( LOG_ERR ,"Unable to open %s" , file );
      return FALSE ;
   }

   fprintf( out , "%d malloc / %d free\n" , __debug_mem_table . nb_malloc , __debug_mem_table . nb_free );

   for( int it = 0 ; it < __debug_mem_table . table -> size ; it ++ )
   {
      node =  __debug_mem_table . table -> hash_table[ it ] -> start ;
      while( node )
      {
         hash_entry= (HASH_NODE *)node -> ptr ;
         item = hash_entry -> data . ptr ;

         if( item )
         {
            fprintf( out , "%p size %zu file %s func %s line %d dump:" , item -> ptr , item -> size , item -> file , item -> func , item -> line );
            if( item -> size > DEBUG_MEM_DUMP_MAX_LEN )
               len = DEBUG_MEM_DUMP_MAX_LEN ;
            else
               len = item -> size ;

            fwrite( item -> ptr , len , 1 , out );
            fprintf( out , "\n" );
            delete_db_mem_item( item );
            item = NULL ;
         }
         else
         {
            n_log( LOG_ERR  ,"Unable to load item from node -> ptr" );
         }
         node = node -> next ;
      }
   }

   fclose( out );
   n_log( LOG_DEBUG , "Memory dump finished" );
   return TRUE ;
} /*db_mem_dump_leaked() */
