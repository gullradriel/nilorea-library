/**\file n_common.c
 *  common function
 *\author Castagnier Mickael
 *\version 1.0
 *\date 24/03/05
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"


#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

#ifdef __windows__
#include <windows.h>
#else
#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef __linux__
#include <string.h>
#endif
#endif

/*! int file_exist( const char *filename )
 *\brief test if file exist and if it's readable
 *\param filename Path/name of the file 
 *\return TRUE or FALSE 
 */
int file_exist( const char *filename )
{
	FILE *file = NULL ;
	if( ( file = fopen( filename , "r" ) ) != NULL )
	{
		fclose(file);
		return 1 ;
	}
	return 0;
} /* file_exist */



/*!\fn char *get_prog_dir( void )
 *\brief get current program running directory
 *\return A copy of the current program running directory inside a string
 */
char *get_prog_dir( void )
{
	char strbuf[ PATH_MAX ] = "" ;

	int error = 0 ;
#ifdef __windows__
	int bytes = GetModuleFileName( NULL , strbuf , PATH_MAX );
	error = errno ;
	if(bytes != 0)
	{
		return strdup( dirname(strbuf ) );
	}
#else
	char procbuf[ PATH_MAX ] = "" ;
#ifdef __linux__
	sprintf( procbuf , "/proc/%d/exe", (int)getpid() );
#elif defined __sun
	sprintf( procbuf , "/proc/%d/path/a.out", (int)getpid() );
#endif
	int bytes = MIN( readlink( procbuf , strbuf , PATH_MAX ) , PATH_MAX - 1 );
	error = errno ;
	if( bytes >= 0 )
	{
		strbuf[ bytes ] = '\0';
		return strdup( dirname( strbuf ) );
	}
#endif
	fprintf( stderr , "%s" , strerror( error ) );
	return NULL ;
} /* get_prog_dir */


/*!\fn char *get_prog_name( void )
 *\brief get current program name
 *\return A copy of the current program name inside a string
 */
char *get_prog_name( void )
{
	char strbuf[ PATH_MAX ] = "" ;
	int error = 0 ;
#ifdef __windows__
	int bytes = GetModuleFileName( NULL , strbuf , PATH_MAX );
	error = errno ;
	if(bytes != 0)
	{
		return strdup( basename(strbuf ) );
	}
#else
	char procbuf[ PATH_MAX ] = "" ;
#ifdef __linux__
	sprintf( procbuf , "/proc/%d/exe", (int)getpid() );
#elif defined __sun
	sprintf( procbuf , "/proc/%d/path/a.out", (int)getpid() );
#endif
	int bytes = MIN( readlink( procbuf , strbuf , PATH_MAX ) , PATH_MAX - 1 );
	error = errno ;
	if( bytes >= 0 )
	{
		strbuf[ bytes ] = '\0';
		return strdup( basename( strbuf ) );
	}
#endif
	fprintf( stderr , "%s" , strerror( error ) );
	return NULL ;
} /* get_prog_dir */


#ifndef __windows__
/*!\fn n_daemonize( void )
 *\brief Daemonize program
 *\return TRUE or FALSE
 */
int n_daemonize( void )
{
	/* Fork off the parent process */
	pid_t pid = fork();
	if( pid < 0 )
	{
		n_log( LOG_ERR , "Error: unable to fork child: %s" , strerror( errno ) );
		exit( 1 );
	}
	if( pid > 0 )
	{
		n_log( LOG_NOTICE , "Child started successfuly !" );
		exit( 0 );
	}
	/* Close all open file descriptors */
	int x = 0 ;
	for(x = sysconf( _SC_OPEN_MAX ) ; x >= 0 ; x-- )
	{
		close(x);
	}
	int fd = open("/dev/null",O_RDWR, 0);
	if (fd != -1)
	{
		dup2 (fd, STDIN_FILENO);
		dup2 (fd, STDOUT_FILENO);
		dup2 (fd, STDERR_FILENO);
		if (fd > 2)
		{
			close (fd);
		}
	}
	else
	{
		n_log( LOG_ERR , "Failed to open /dev/null" );
	}

	/* On success: The child process becomes session leader */
	if( setsid() < 0 )
	{
		n_log( LOG_ERR , "Error: unable to set session leader with setsid(): %s" , strerror( errno ) );
		exit( 1 );
	}
	else
	{
		n_log( LOG_NOTICE , "Session leader set !" );
	}

	return TRUE ;
} /* n_daemonize(...) */
#endif
