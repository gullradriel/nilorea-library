/**\file n_common.h
 *  common headers and low-level hugly functions & define
 *\author Castagnier Mickael
 *\version 1.0
 *\date 24/03/05
 */

#ifndef __NILOREA_COMMONS__
#define __NILOREA_COMMONS__

#ifdef __cplusplus
extern "C"
{
#endif

/**\defgroup COMMONS COMMONS: generally used headers, defines, timers, allocators,...
  \addtogroup COMMONS
  @{
  */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <malloc.h>
#include <nilorea/n_log.h>


/*! feature test macro */
#define __EXTENSIONS__

/* set __windows__ if true */
#if defined( _WIN32 ) || defined( _WIN64 )
#ifndef __windows__
#define __windows__
#endif
#ifndef WEXITSTATUS
	#define WEXITSTATUS(w)    (((w) >> 8) & 0xff)
#endif
#ifndef WIFEXITED
	#define WIFEXITED(w)      (((w) & 0xff) == 0)
#endif
#else
	#include <alloca.h>
#endif

#if defined(__GNUC__) && __GNUC__ >= 7
 #define FALL_THROUGH __attribute__ ((fallthrough))
#else
 #define FALL_THROUGH /* fall through */ \
((void)0)
#endif /* __GNUC__ >= 7 */

#if defined( __windows__ )
#ifndef __BYTE_ORDER
#define __BYTE_ORDER __BYTE_ORDER__
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN __ORDER_BIG_ENDIAN__
#endif
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
#endif
/* typedefine for unsigned category for basic native types */
/*! shortcut for unsigned int*/
typedef unsigned int uint;
/*! shortcut for unsigned long*/
typedef unsigned long ulong;
/*! shortcut for unsigned short*/
typedef unsigned short ushort;
/*! shortcut for unsigned char*/
typedef unsigned char uchar;
#endif

/*! FORCE_INLINE portable macro */
#ifdef _MSVC_VER
#define FORCE_INLINE    __forceinline
#elif defined( __linux__ )  || defined( __windows__ )
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE __attribute__((always_inline))
#endif

/*! define true */
#ifndef true
#define true (1==1)
#endif

/*! define TRUE */
#ifndef TRUE
#define TRUE true
#endif

/*! define false */
#ifndef false
#define false (1==0)
#endif

/*! define FALSE */
#ifndef FALSE
#define FALSE false
#endif

/*!  returned by N_STRLIST functions to tell the caller that the list is empty */
#ifndef EMPTY
#define EMPTY 2
#endif



/*! String or "NULL" string for logging purposes */
#define _str( __PTR ) ((__PTR)?(__PTR):"NULL")
/*! String or " " string for config purposes */
#define _strw( __PTR ) ((__PTR)?(__PTR):" ")
/*! N_STR or "NULL" string for logging purposes */
#define _nstr( __PTR ) ((__PTR&&__PTR->data)?(__PTR->data):"NULL")
/*! N_STR or NULL string for logging purposes */
#define _nstrp( __PTR ) ((__PTR&&__PTR->data)?(__PTR->data):NULL)

/*! Malloc Handler to get errors and set to 0 */
#define Malloc( __ptr , __struct , __size ) \
    if ( !(  __ptr  = (  __struct  *)calloc(  __size  , sizeof(  __struct  ) ) ) )   \
    {                                                                                 \
        n_log( LOG_ERR , "( %s *)malloc( %s * sizeof( %d ) ) Error at line %d of %s \n", #__ptr , #__struct , __size , __LINE__ , __FILE__); \
    }

/*! Malloca Handler to get errors and set to 0 */
#define Alloca( __ptr , __size ) \
    __ptr = alloca( __size ); \
    if( ! __ptr ) \
    { \
        n_log( LOG_ERR , "%s=alloca( %d ) Error at line %d of %s \n", #__ptr , __size , __LINE__ , __FILE__); \
    } \
    else \
    memset( __ptr , 0 , __size );


/*! Free Handler to get errors */
#define Free( __ptr ) \
    if (  __ptr  )\
    {\
        free(  __ptr  );\
        __ptr  = NULL;\
    }\
    else\
    {\
        n_log( LOG_DEBUG , "Free( %s ) already done or NULL at line %d of %s \n", #__ptr , __LINE__ , __FILE__ );\
    }
/*! Free Handler without log */
#define FreeNoLog( __ptr )\
    if (  __ptr  )\
    {\
        free(  __ptr  );\
        __ptr  = NULL;\
    }


/*! Realloc Handler to get errors */
#define Realloc( __ptr, __struct , __size )  \
    if( !(  __ptr  = (  __struct  *)realloc(  __ptr , __size  * sizeof(  __struct  ) ) ) )\
    { \
        n_log( LOG_ERR , "( %s *)malloc( %s * sizeof( %d ) ) Error at line %d of %s \n", #__ptr , #__struct , __size , __LINE__ , __FILE__);\
        __ptr = NULL;\
    }

/*! Realloc + zero new memory zone Handler to get errors */
#define Reallocz( __ptr, __struct , __old_size , __size )  \
    if ( !(  __ptr  = (  __struct  *)realloc(  __ptr , __size  * sizeof(  __struct  ) ) ) )\
    {\
        n_log( LOG_ERR , "( %s *)malloc( %s * sizeof( %d ) ) Error at line %d of %s \n", #__ptr , #__struct , __size , __LINE__ , __FILE__);\
        __ptr = NULL;\
    }\
    else\
    {\
        if( __size > __old_size )memset( ( __ptr + __old_size ) , 0 , __size - __old_size );\
    }

/*! macro to assert things */
#define __n_assert( __ptr , __ret ) \
    if( !(__ptr) ) \
    { \
        n_log( LOG_DEBUG , "%s is NULL at line %d of %s" , #__ptr ,  __LINE__ , __FILE__ );\
        __ret ; \
    }

/*! next odd helper */
#define next_odd( __val ) ( (__val)%2 == 0 ) ? (__val) : ( __val + 1 )

/*! next odd helper */
#define next_even( __val ) ( (__val)%2 == 0 ) ? (__val + 1) : ( __val )


/*! init error checking in a function */
#define init_error_check() \
    static int ___error__check_flag = FALSE ;

/*! error checker type if( !toto ) */
#define ifnull if( !

/*! error checker type if( 0 != toto ) */
#define ifzero if( 0 ==

/*! error checker type if( toto == FALSE )  */
#define iffalse if( FALSE ==

/*! error checker type if( toto == FALSE )  */
#define iftrue if( TRUE ==

/*! check for errors */
#define checkerror() if( ___error__check_flag == TRUE ) \
    { n_log( LOG_ERR , "checkerror return false at line %d of %s" , __LINE__ , __FILE__ ); \
    goto error ; \
    }

/*! close a ifwhatever block */
#define endif ){ ___error__check_flag = TRUE ; n_log( LOG_ERR , "First err was at line %d of %s" , __LINE__ , __FILE__ );}

/*! pop up errors if any */
#define get_error() \
    (___error__check_flag == TRUE)

#ifdef RWLOCK_DEBUG
#define RWLOCK_LOGLEVEL LOG_DEBUG
#else
#define RWLOCK_LOGLEVEL LOG_NULL
#endif

/*! Macro for initializing a rwlock */
#define init_lock( __rwlock_mutex ) \
    n_log( RWLOCK_LOGLEVEL , "init_lock %s" , #__rwlock_mutex ); \
    pthread_rwlock_init( &(__rwlock_mutex) , NULL )
/*! Macro for acquiring a read lock on a rwlock mutex */
#define read_lock( __rwlock_mutex ) \
    n_log( RWLOCK_LOGLEVEL , "read_lock %s" , #__rwlock_mutex ); \
    pthread_rwlock_rdlock( &(__rwlock_mutex) )
/*! Macro for acquiring a write lock on a rwlock mutex */
#define write_lock( __rwlock_mutex ) \
    n_log( RWLOCK_LOGLEVEL , "write_lock %s" , #__rwlock_mutex ); \
    pthread_rwlock_wrlock( &(__rwlock_mutex) )
/*! Macro for releasing read/write lock a rwlock mutex */
#define unlock( __rwlock_mutex ) \
    n_log( RWLOCK_LOGLEVEL , "unlock %s" , #__rwlock_mutex ); \
    pthread_rwlock_unlock( &(__rwlock_mutex) )
/*! Macro to destroy rwlock mutex */
#define rw_lock_destroy( __rwlock_mutex ) \
    n_log( RWLOCK_LOGLEVEL , "destroy lock %s" , #__rwlock_mutex ); \
    pthread_rwlock_destroy( &(__rwlock_mutex) )

/*! Flag for SET something , passing as a function parameter */
#define SET        1234
/*! Flag for GET something , passing as a function parameter */
#define GET        4321
/*! Default APP_STATUS Value */
#define DEFAULT    1000
/*! Value of the state of an application who is running */
#define RUNNING    1001
/*! Value of the state of an application who want to stop his activity */
#define STOPWANTED 1002
/*! Value of the state of an application who is stopped */
#define STOPPED    1003
/*! Value of the state of an application who is paused */
#define PAUSED     1004

/*! Initialize the random sequence with time */
#define randomize() { srand((unsigned)time(NULL)); rand(); }

#ifndef MIN
/*! define MIN macro */
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
/*! define MIN macro */
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif


void n_abort( char const *format, ... );

#ifndef __windows__
int n_daemonize( void );
#endif

/* get running program name */
char *get_prog_name( void );

/* get running program directory */
char *get_prog_dir( void );

/* test file presence*/
int file_exist( const char *filename );

/* shortcut for popen usage */
int n_popen( char *cmd , int read_buf_size , void **nstr_output , int *ret );

/* non blocking system call */
pid_t system_nb(const char * command, int * infp, int * outfp);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __COMMON_FOR_C_IMPLEMENTATION__ */
