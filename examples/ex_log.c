#include "nilorea/n_log.h"

int main( void )
{
    puts( "LOG_NULL" );
    set_log_level( LOG_NULL );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "EMERG" );
    set_log_level( LOG_EMERG );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "ALERT" );
    set_log_level( LOG_ALERT );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "CRIT" );
    set_log_level( LOG_CRIT );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "ERR" );
    set_log_level( LOG_ERR );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "WARNING" );
    set_log_level( LOG_WARNING );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "NOTICE" );
    set_log_level( LOG_NOTICE );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "INFO" );
    set_log_level( LOG_INFO );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    puts( "DEBUG" );
    set_log_level( LOG_DEBUG );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );
    set_log_file( "ex_log.log" );
    n_log( LOG_EMERG, "EMERG" );
    n_log( LOG_ALERT, "ALERT" );
    n_log( LOG_CRIT, "CRIT" );
    n_log( LOG_ERR, "ERR" );
    n_log( LOG_WARNING, "WARNING" );
    n_log( LOG_NOTICE, "NOTICE" );
    n_log( LOG_INFO, "INFO" );
    n_log( LOG_DEBUG, "DEBUG" );

    exit( 0 );
}