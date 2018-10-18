/**\file ex_n_network.c
 *  Nilorea Library n_network api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */


#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"

#define LIST_LIMIT   10
#define NB_TEST_ELEM 15

void print_list_info( LIST *list )
{
   __n_assert( list , return ); 
   n_log( LOG_NOTICE , "List: %p, %d max_elements , %d elements" , list , list -> nb_max_items , list -> nb_items );
}

int nstrcmp( const void *a , const void *b )
{
   const N_STR *s1 = a;
   const N_STR *s2 = b;

   if( !s1 || !s1 -> data )
      return 1 ;
   if( !s2 || !s2 -> data )
      return -1 ;

   return strcmp( s1 -> data , s2 -> data );
}


int main( void )
{

   LIST *limited_list = new_generic_list( LIST_LIMIT );
   LIST *unlimited_list = new_generic_list( 0 );

   __n_assert( limited_list , return FALSE );
   __n_assert( unlimited_list , return FALSE );

   N_STR *nstr = NULL ;
   
   set_log_level( LOG_INFO );
   

   n_log( LOG_NOTICE , "Limited list: adding %d element in limited element (%d) list, empty the list at the end" , NB_TEST_ELEM , LIST_LIMIT );
   for( int it = 0 ; it < NB_TEST_ELEM ; it ++ )
   {
      nstrprintf( nstr , "Nombre aleatoire : %d" , rand()%1000 );
      if( nstr )
      {
         int func = rand()%4 ;
         switch( func )
         {
            case 0 :
               n_log( LOG_NOTICE , "list_push" );
               if( list_push( limited_list, nstr , &free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
            case 1 :
               n_log( LOG_NOTICE , "list_unshift" );
               if( list_unshift( limited_list, nstr , &free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
            case 2 :
               n_log( LOG_NOTICE , "list_push_sorted" );
               if( list_push_sorted( limited_list, nstr , nstrcmp , &free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
            case 3 :
               n_log( LOG_NOTICE , "list_unshift" );
               if( list_unshift_sorted( limited_list, nstr , nstrcmp , &free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
			default:
			   n_log( LOG_ERR , "should never happen: no func %d !" , func );
			   break ;
         }
         nstr = NULL ;
         print_list_info( limited_list );
      }
   }
   list_empty( limited_list );
   n_log( LOG_NOTICE , "Limited list: adding %d element in limited element (%d) list, empty the list at the end" , NB_TEST_ELEM , LIST_LIMIT );
   for( int it = 0 ; it < NB_TEST_ELEM ; it ++ )
   {
      nstrprintf( nstr , "Nombre aleatoire : %d" , rand()%1000 );
      if( nstr )
      {
         int func = rand()%4 ;
         switch( func )
         {
            case 0 :
               n_log( LOG_NOTICE , "list_push" );
               if( list_push( limited_list, nstr , free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
            case 1 :
               n_log( LOG_NOTICE , "list_unshift" );
               if( list_unshift( limited_list, nstr , free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
            case 2 :
               n_log( LOG_NOTICE , "list_push_sorted" );
               if( list_push_sorted( limited_list, nstr , nstrcmp , free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
            case 3 :
               n_log( LOG_NOTICE , "list_unshift" );
               if( list_unshift_sorted( limited_list, nstr , nstrcmp , free_nstr_ptr ) == FALSE )
                  free_nstr( &nstr );
               break;
			default:
			   n_log( LOG_ERR , "should never happen: no func %d !" , func );
			   break ;
         }
         nstr = NULL ;
         print_list_info( limited_list );
      }
   }
   list_destroy( &limited_list );
   list_destroy( &unlimited_list );

} /* END_OF_MAIN */
