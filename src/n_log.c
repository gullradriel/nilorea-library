/*\file n_log.c
 * generic logging system
 *\author Castagnier Mickael
 *\date 2013-03-12
 */

/* for vasprintf */
#ifdef LINUX
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <pthread.h>
#include "nilorea/n_log.h"


/*! internal struct to handle log types */
typedef struct _code {
	/*! string of log type */
	char *c_name;
	/*! numeric value of log type */
	int	c_val;
} CODE;

/*! array of log levels */
static CODE prioritynames[] = {
	{	"EMERG",	 LOG_EMERG },		
	{	"ALERT",	 LOG_ALERT },		
	{	"CRITICAL",LOG_CRIT },		
	{	"ERROR",	 LOG_ERR },		
	{	"WARNING", LOG_WARNING},
	{	"NOTICE",LOG_NOTICE },
	{	"INFO",	LOG_INFO },
	{	"DEBUG",	LOG_DEBUG },
	{	NULL,		-1 }
};


/*! static global maximum wanted log level value */
static int LOG_LEVEL = LOG_NULL ;

/*! static global logging type ( STDERR, FILE, SYSJRNL ) */
static int LOG_TYPE = LOG_STDERR ;

/*! static FILE handling if logging to file is enabled */
static FILE *log_file = NULL ;



/*!\fn open_sysjrnl( char *identity )
*\brief Open connection to syslog
*\param identity Tag for syslog or NULL to use argv[0]
*/
void open_sysjrnl( char *identity )
{
	/* LOG_CONS: log to console if no syslog available 
	 * LOG_PID: add pid of calling process to log
	 * 23: Local use 7
	 */
	openlog ( identity , LOG_CONS|LOG_PID|LOG_NDELAY , 23 );
} /* open_sysjrnl */



/*!\fn void set_log_level( int log_level )
 *\brief Set the global log level value ( static int LOG_LEVEL )
 *\param log_level Log level value. Supported: JRNLONLY,NOLOG,LOG_NOTICE/INFO/ERR/DEBUG
 */
void set_log_level( const int log_level )
{
	if( log_level == LOG_FILE )
	{
		LOG_TYPE = LOG_FILE ;
		return ;
	}
	if( log_level == LOG_STDERR )
	{
		LOG_TYPE = LOG_STDERR ;
		return ;
	}
	if( log_level == LOG_SYSJRNL )
	{
		LOG_TYPE = LOG_SYSJRNL ;
		return ;
	}
	LOG_LEVEL = log_level ;
} /* set_log_level() */ 



/*!\fn int get_log_level( )
 *\brief Get the global log level value
 *\return static int LOG_LEVEL
 */
int get_log_level( void )
{
	return LOG_LEVEL ;
} /* get_log_level() */



/*!\fn int set_log_file( char *file )
 *\brief Set the logging to a file instead of stderr
 *\param file The filename where to log
 *\return TRUE or FALSE
 */
int set_log_file( char *file )
{	
	__n_assert( file , return FALSE );

	if( !log_file )
		log_file = fopen( file , "w" );
	else
	{
		fclose( log_file );
		log_file = fopen( file , "w" );
	}

	set_log_level( LOG_FILE );

	__n_assert( log_file , return FALSE );

	return TRUE ;
} /* set_log_file */



/*!\fn FILE *get_log_file()
 *\brief return the current log_file
 *\return a valid FILE handle or NULL 
 */
FILE *get_log_file( void )
{
	return log_file ;
} /*get_log_level() */



/*!\fn void _n_log( int level , const char *file , const char *func , int line , const char *format , ... ) 
 *\brief Logging function. log( level , const char *format , ... ) is a macro around _log
 *\param level Logging level 
 *\param file File containing the emmited log
 *\param func Function emmiting the log
 *\param line Line of the log
 *\param format Format and string of the log, printf style
 */
void _n_log( int level , const char *file , const char *func , int line , const char *format , ... ) 
{
	va_list args ;

	FILE *out = NULL ;

	if( level == LOG_NULL )
		return ;

	if( !log_file )
		out = stderr ;
	else
		out = log_file ;

	int log_level = get_log_level();

	if( level <= log_level )
	{
#ifndef NOSYSJRNL
		char *syslogbuffer = NULL ;
#endif
		switch( LOG_TYPE )
		{
#ifndef NOSYSJRNL
			case LOG_SYSJRNL :
				va_start (args, format);
				vasprintf( &syslogbuffer , format , args ); 
				va_end( args );
				syslog( level , "%s->%s:%d %s" , file , func , line , syslogbuffer ); 
				Free( syslogbuffer );
				break;
#endif
			default:
				fprintf( out , "%s:%ld %s->%s:%d " , prioritynames[ level ] . c_name , time( NULL ) , file , func , line  ); 
				va_start (args, format);
				vfprintf( out, format , args ); 
				va_end( args );
				fprintf( out, "\n" ); 
				break ;
		}
	}
}  /* _n_log( ... ) */



/*!\fn open_safe_logging( TS_LOG **log , char *pathname , char *opt )
 *\brief Open a thread-safe logging file
 *\param log A TS_LOG handler
 *\param pathname The file path (if any) and name
 *\param opt Options for opening (please never forget to use "w")
 *\return TRUE on success , FALSE on error , -1000 if already open
 */
int open_safe_logging( TS_LOG **log , char *pathname , char *opt ) 
{

	if( (*log) ){

		if( (*log) -> file )
			return -1000; /* already open */

		return FALSE;
	}

	if( !pathname ) return FALSE; /* no path/filename */

	Malloc( (*log) , TS_LOG , 1 );

	if( !(*log) )
		return FALSE;

	pthread_mutex_init( &(*log) -> LOG_MUTEX , NULL );

	(*log) -> file = fopen( pathname , opt );

	if( !(*log) -> file )
		return FALSE;

	return TRUE;

}/* open_safe_logging(...) */



/*!\fn write_safe_log( TS_LOG *log , char *pat , ... )
 *\brief write to a thread-safe logging file
 *\param log A TS_LOG handler
 *\param pat Pattern for writting (i.e "%d %d %s")
 *\return TRUE on success, FALSE on error
 */
int write_safe_log( TS_LOG *log , char *pat , ... ) 
{
	/* argument list */
	va_list arg;
	char str[2048] = "" ;

	if( !log )
		return FALSE;

	va_start( arg , pat );

	vsnprintf( str , sizeof(str), pat, arg );

	va_end( arg );

	pthread_mutex_lock( &log -> LOG_MUTEX );

	fprintf( log -> file , "%s" , str );

	pthread_mutex_unlock( &log -> LOG_MUTEX );

	return TRUE;

} /* write_safe_log( ... ) */



/*!\fn close_safe_logging( TS_LOG *log )
 *\brief close a thread-safe logging file
 *\param log A TS_LOG handler
 *\return TRUE on success, FALSE on error
 */
int close_safe_logging( TS_LOG *log )
{
	if( !log )
		return FALSE;

	pthread_mutex_destroy( &log -> LOG_MUTEX );

	fclose( log -> file );

	return TRUE;

}/* close_safe_logging( ...) */
