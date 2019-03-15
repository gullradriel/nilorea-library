/**\file n_file.c
 *
 * File utilities
 *
 *\author Castagnier Mickael
 *\version 1.0
 *\date 02/11/16
 */

#include "nilorea/n_file.h"

int file_monitor_refresh( HASH_TABLE *filepool )
{
    ht_foreach( node, filepool )
    {
        FILE_MONITOR *fmon = hash_value( node, FILE_MONITOR );
        if( node && node -> ptr && fmon )
        {


        }
    }
}


int file_monitor_set( HASH_TABLE *filepool, char *name, time_t check_interval )
{

}



int file_monitor_get( HASH_TABLE *filepool, char *name )
{

}

