/**\file n_file.h
 *
 * File utilities
 *
 *\author Castagnier Mickael
 *\version 1.0
 *\date 02/11/16
 */

#ifndef N_FILE_UTILS
#define N_FILE_UTILS

#ifdef __cplusplus
extern "C"
{
#endif

/**\defgroup FILE FILE: generic type FILE
   \addtogroup FILE
  @{
*/

#include "stdio.h"
#include "stdlib.h"
#include "n_log.h"
#include "n_common.h"
#include "n_list.h"
#include "n_hash.h"

/*! File state monitoring element */
typedef struct FILE_MONITOR
{
    struct stat last_filestat ;
    struct stat filestat ;
    time_t filetime, filetime_nsec,
           last_filetime, last_filetime_nsec,
           last_check ;
    time_t check_interval ;

} FILE_MONITOR ;


int file_monitor_refresh( HASH_TABLE *filepool );
int file_monitor_set( HASH_TABLE *filepool, char *name, time_t check_interval );
int file_monitor_get( HASH_TABLE *filepool, char *name );
int file_monitor_del( HASH_TABLE *filepool, char *name );

/*@}*/

#ifdef __cplusplus
}
#endif

#endif
