/**\file n_network.c
 *  Network Engine
 *\author Castagnier Mickael
 *\version 1.0
 *\date 10/05/2005
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include "nilorea/n_network.h"
#include "nilorea/n_network_msg.h"
#include "nilorea/n_log.h"
#include "nilorea/n_hash.h"
#include "nilorea/n_hash.h"

/*! network error code */
N_ENUM_DEFINE( N_ENUM_netw_code_type, __netw_code_type )

#ifdef __windows__

    /*!\fn char* wchar_to_char(const wchar_t* pwchar)
     * \brief wchar to char, from https://stackoverflow.com/questions/3019977/convert-wchar-t-to-char/55715607#55715607 by Richard Bamford
     * \param pwchar the wchar_t to convert
     * \return an allocated copy of pwchar in form of a char * or NULL 
     */
char* wchar_to_char(const wchar_t* pwchar)
{
    // get the number of characters in the string.
    int currentCharIndex = 0;
    char currentChar = pwchar[currentCharIndex];
    char *filePathC = NULL ;

    while (currentChar != '\0')
    {
        currentCharIndex++;
        currentChar = pwchar[currentCharIndex];
    }

    const int charCount = currentCharIndex + 1;

    // allocate a new block of memory size char (1 byte) instead of wide char (2 bytes)
    Malloc( filePathC, char, charCount );
    __n_assert( filePathC, return NULL );

    for (int i = 0; i < charCount; i++)
    {
        // convert to char (1 byte)
        char character = pwchar[i];

        *filePathC = character;

        filePathC += sizeof(char);

    }
    filePathC += '\0';

    filePathC -= (sizeof(char) * charCount);

    return filePathC;
}

/*! get last socket error code, windows version */
#define neterrno WSAGetLastError()

/*! get last socket error code as a string, windows version */
#define netstrerror( code )({ \
        wchar_t *s = NULL; \
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, \
                NULL, code , \
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), \
                (LPWSTR)&s, 0, NULL); \
                char *netstr = wchar_to_char( s ); \
                LocalFree( s ); \
                netstr ; \
                })


#if __GNUC__ <= 6 && __GNUC_MINOR__ <= 3

/*--------------------------------------------------------------------------------------
  By Marco Ladino - mladinox.. jan/2016
  MinGW 3.45 thru 4.5 versions, don't have the socket functions:
  --> inet_ntop(..)
  --> inet_pton(..)
  But with this adapted code using the original functions from FreeBSD,
  one can to use it in the C/C++ Applications, without problem..!
  This implementation, include tests for IPV4 and IPV6 addresses,
  and is full C/C++ compatible..
  --------------------------------------------------------------------------------------*/
/*	OpenBSD: strlcpy.c,v 1.11 2006/05/05 15:27:38 millert Exp 	*/
/*-
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller at courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t strlcpy(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0)
    {
        while (--n != 0)
        {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0)
    {
        if (siz != 0)
            *d = '\0';		/* NUL-terminate dst */
        while (*s++)
            ;
    }

    return(s - src - 1);	/* count does not include NUL */
}


/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static char	*inet_ntop4(const unsigned char *src, char *dst, socklen_t size);
static char	*inet_ntop6(const unsigned char *src, char *dst, socklen_t size);

/* char *
 * inet_ntop(af, src, dst, size)
 *	convert a network format address to presentation format.
 * return:
 *	pointer to presentation format address (`dst'), or NULL (see errno).
 * author:
 *	Paul Vixie, 1996.
 */
char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    switch (af)
    {
        case AF_INET:
            return (inet_ntop4((const unsigned char*)src, dst, size));
        case AF_INET6:
            return (inet_ntop6((const unsigned char*)src, dst, size));
        default:
            return (NULL);
    }
    /* NOTREACHED */
}

/* const char *
 * inet_ntop4(src, dst, size)
 *	format an IPv4 address
 * return:
 *	`dst' (as a const)
 * notes:
 *	(1) uses no statics
 *	(2) takes a u_char* not an in_addr as input
 * author:
 *	Paul Vixie, 1996.
 */
static char *inet_ntop4(const unsigned char *src, char *dst, socklen_t size)
{
    static const char fmt[] = "%u.%u.%u.%u";
    char tmp[sizeof "255.255.255.255"];
    int l;

    l = snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]);
    if (l <= 0 || (socklen_t) l >= size)
    {
        return (NULL);
    }
    strlcpy(dst, tmp, size);
    return (dst);
}

/* const char *
 * inet_ntop6(src, dst, size)
 *	convert IPv6 binary address into presentation (printable) format
 * author:
 *	Paul Vixie, 1996.
 */
static char *inet_ntop6(const unsigned char *src, char *dst, socklen_t size)
{
    /*
     * Note that int32_t and int16_t need only be "at least" large enough
     * to contain a value of the specified size.  On some systems, like
     * Crays, there is no such thing as an integer variable with 16 bits.
     * Keep this in mind if you think this function should have been coded
     * to use pointer overlays.  All the world's not a VAX.
     */
    char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"], *tp;
    struct
    {
        int base, len;
    } best, cur;
#define NS_IN6ADDRSZ	16
#define NS_INT16SZ	2
    u_int words[NS_IN6ADDRSZ / NS_INT16SZ];
    int i;

    /*
     * Preprocess:
     *	Copy the input (bytewise) array into a wordwise array.
     *	Find the longest run of 0x00's in src[] for :: shorthanding.
     */
    memset(words, '\0', sizeof words);
    for (i = 0; i < NS_IN6ADDRSZ; i++)
        words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
    best.base = -1;
    best.len = 0;
    cur.base = -1;
    cur.len = 0;
    for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++)
    {
        if (words[i] == 0)
        {
            if (cur.base == -1)
                cur.base = i, cur.len = 1;
            else
                cur.len++;
        }
        else
        {
            if (cur.base != -1)
            {
                if (best.base == -1 || cur.len > best.len)
                    best = cur;
                cur.base = -1;
            }
        }
    }
    if (cur.base != -1)
    {
        if (best.base == -1 || cur.len > best.len)
            best = cur;
    }
    if (best.base != -1 && best.len < 2)
        best.base = -1;

    /*
     * Format the result.
     */
    tp = tmp;
    for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++)
    {
        /* Are we inside the best run of 0x00's? */
        if (best.base != -1 && i >= best.base &&
                i < (best.base + best.len))
        {
            if (i == best.base)
                *tp++ = ':';
            continue;
        }
        /* Are we following an initial run of 0x00s or any real hex? */
        if (i != 0)
            *tp++ = ':';
        /* Is this address an encapsulated IPv4? */
        if (i == 6 && best.base == 0 && (best.len == 6 ||
                    (best.len == 7 && words[7] != 0x0001) ||
                    (best.len == 5 && words[5] == 0xffff)))
        {
            if (!inet_ntop4(src+12, tp, sizeof tmp - (tp - tmp)))
                return (NULL);
            tp += strlen(tp);
            break;
        }
        tp += sprintf(tp, "%x", words[i]);
    }
    /* Was it a trailing run of 0x00's? */
    if (best.base != -1 && (best.base + best.len) ==
            (NS_IN6ADDRSZ / NS_INT16SZ))
        *tp++ = ':';
    *tp++ = '\0';

    /*
     * Check for overflow, copy, and we're done.
     */
    if ((socklen_t)(tp - tmp) > size)
    {
        return (NULL);
    }
    strcpy(dst, tmp);
    return (dst);
}

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1996,1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * WARNING: Don't even consider trying to compile this on a system where
 * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
 */

static int	inet_pton4(const char *src, u_char *dst);
static int	inet_pton6(const char *src, u_char *dst);

/* int
 * inet_pton(af, src, dst)
 *	convert from presentation format (which usually means ASCII printable)
 *	to network format (which is usually some kind of binary format).
 * return:
 *	1 if the address was valid for the specified address family
 *	0 if the address wasn't valid (`dst' is untouched in this case)
 *	-1 if some other error occurred (`dst' is untouched in this case, too)
 * author:
 *	Paul Vixie, 1996.
 */
int inet_pton(int af, const char *src, void *dst)
{
    switch (af)
    {
        case AF_INET:
            return (inet_pton4(src, (unsigned char *)dst));
        case AF_INET6:
            return (inet_pton6(src, (unsigned char *)dst));
        default:
            return (-1);
    }
    /* NOTREACHED */
}

/* int
 * inet_pton4(src, dst)
 *	like inet_aton() but without all the hexadecimal and shorthand.
 * return:
 *	1 if `src' is a valid dotted quad, else 0.
 * notice:
 *	does not touch `dst' unless it's returning 1.
 * author:
 *	Paul Vixie, 1996.
 */
static int inet_pton4(const char *src, u_char *dst)
{
    static const char digits[] = "0123456789";
    int saw_digit, octets, ch;
#define NS_INADDRSZ	4
    u_char tmp[NS_INADDRSZ], *tp;

    saw_digit = 0;
    octets = 0;
    *(tp = tmp) = 0;
    while ((ch = *src++) != '\0')
    {
        const char *pch;

        if ((pch = strchr(digits, ch)) != NULL)
        {
            u_int uiNew = *tp * 10 + (pch - digits);

            if (saw_digit && *tp == 0)
                return (0);
            if (uiNew > 255)
                return (0);
            *tp = uiNew;
            if (!saw_digit)
            {
                if (++octets > 4)
                    return (0);
                saw_digit = 1;
            }
        }
        else if (ch == '.' && saw_digit)
        {
            if (octets == 4)
                return (0);
            *++tp = 0;
            saw_digit = 0;
        }
        else
            return (0);
    }
    if (octets < 4)
        return (0);
    memcpy(dst, tmp, NS_INADDRSZ);
    return (1);
}

/* int
 * inet_pton6(src, dst)
 *	convert presentation level address to network order binary form.
 * return:
 *	1 if `src' is a valid [RFC1884 2.2] address, else 0.
 * notice:
 *	(1) does not touch `dst' unless it's returning 1.
 *	(2) :: in a full address is silently ignored.
 * credit:
 *	inspired by Mark Andrews.
 * author:
 *	Paul Vixie, 1996.
 */
static int inet_pton6(const char *src, u_char *dst)
{
    static const char xdigits_l[] = "0123456789abcdef",
                 xdigits_u[] = "0123456789ABCDEF";
#define NS_IN6ADDRSZ	16
#define NS_INT16SZ	2
    u_char tmp[NS_IN6ADDRSZ], *tp, *endp, *colonp;
    const char *curtok;
    int ch, seen_xdigits;
    u_int val;

    memset((tp = tmp), '\0', NS_IN6ADDRSZ);
    endp = tp + NS_IN6ADDRSZ;
    colonp = NULL;
    /* Leading :: requires some special handling. */
    if (*src == ':')
        if (*++src != ':')
            return (0);
    curtok = src;
    seen_xdigits = 0;
    val = 0;
    while ((ch = *src++) != '\0')
    {
        const char *xdigits :
            const char *pch ;

        if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
            pch = strchr((xdigits = xdigits_u), ch);
        if (pch != NULL)
        {
            val <<= 4;
            val |= (pch - xdigits);
            if (++seen_xdigits > 4)
                return (0);
            continue;
        }
        if (ch == ':')
        {
            curtok = src;
            if (!seen_xdigits)
            {
                if (colonp)
                    return (0);
                colonp = tp;
                continue;
            }
            else if (*src == '\0')
            {
                return (0);
            }
            if (tp + NS_INT16SZ > endp)
                return (0);
            *tp++ = (u_char) (val >> 8) & 0xff;
            *tp++ = (u_char) val & 0xff;
            seen_xdigits = 0;
            val = 0;
            continue;
        }
        if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
                inet_pton4(curtok, tp) > 0)
        {
            tp += NS_INADDRSZ;
            seen_xdigits = 0;
            break;	/* '\\0' was seen by inet_pton4(). */
        }
        return (0);
    }
    if (seen_xdigits)
    {
        if (tp + NS_INT16SZ > endp)
            return (0);
        *tp++ = (u_char) (val >> 8) & 0xff;
        *tp++ = (u_char) val & 0xff;
    }
    if (colonp != NULL)
    {
        /*
         * Since some memmove()'s erroneously fail to handle
         * overlapping regions, we'll do the shift by hand.
         */
        const int n = tp - colonp;
        int i;

        if (tp == endp)
            return (0);
        for (i = 1; i <= n; i++)
        {
            endp[- i] = colonp[n - i];
            colonp[n - i] = 0;
        }
        tp = endp;
    }
    if (tp != endp)
        return (0);
    memcpy(dst, tmp, NS_IN6ADDRSZ);
    return (1);
}

#endif /* if GCC_VERSION <= 4.5 */

#else /* not __windows__ */

#include <sys/types.h>
#include <sys/wait.h>


/*! get last socket error code, linux version */
#define neterrno errno

/*! BSD style errno string NO WORKING ON REDHAT */
/* #define netstrerror( code )({ \
   size_t errmsglen = 512 ; // strerrorlen_s( code ) + 1  ; \
   char *errmsg = NULL ; \
   Malloc( errmsg , char , errmsglen ); \
   if( errmsg ) \
   { \
   strerror_s( errmsg , errmsglen , code ); \
   } \
   errmsg ; \
   }) */

/*! get last socket error code as a string, linux version */
#define netstrerror( code )({ \
        char *errmsg = NULL ; \
        errno = 0 ; \
        errmsg = strdup( strerror( code ) ); \
        if( errno == ENOMEM ) \
        { \
        errmsg = NULL ; \
        } \
        errmsg ; \
        })

#endif /* if not def windows */

/*!\fn NETWORK *netw_new( int send_list_limit , int recv_list_limit )
 *\brief Return an empty allocated network ready to be netw_closed
 *\param send_list_limit Thread engine number of tosend message limit
 *\param recv_list_limit Thread engine number of received message limit
 *\return NULL or a new empty network
 */
NETWORK *netw_new( int send_list_limit, int recv_list_limit )
{
    NETWORK *netw = NULL ;

    Malloc( netw, NETWORK, 1 );
    __n_assert( netw, return NULL );

    /* netw itself */
    netw -> nb_pending = -1 ;
    netw -> mode = -1 ;
    netw -> user_id = -1 ;
    netw -> nb_running_threads = 0 ;
    netw -> state = NETW_EXITED ;
    netw -> threaded_engine_status = NETW_THR_ENGINE_STOPPED ;

    /* netw -> link */
    netw -> link . sock = -1 ;
    netw -> link . port =
        netw -> link . ip = NULL ;
    memset( &netw -> link . hints, 0, sizeof( struct addrinfo ) );
    memset( &netw -> link . raddr, 0, sizeof( struct sockaddr_storage ) );

    /*initiliaze mutexs*/
    if ( pthread_mutex_init( &netw -> sendbolt, NULL ) != 0 )
    {
        n_log( LOG_ERR, "Error initializing netw -> sendbolt" );
        Free( netw );
        return NULL ;
    }
    /*initiliaze mutexs*/
    if ( pthread_mutex_init( &netw -> recvbolt, NULL ) != 0 )
    {
        n_log( LOG_ERR, "Error initializing netw -> recvbolt" );
        pthread_mutex_destroy( &netw -> sendbolt );
        Free( netw );
        return NULL ;
    }
    /*initiliaze mutexs*/
    if ( pthread_mutex_init( &netw -> eventbolt, NULL ) != 0 )
    {
        n_log( LOG_ERR, "Error initializing netw -> eventbolt" );
        pthread_mutex_destroy( &netw -> sendbolt );
        pthread_mutex_destroy( &netw -> recvbolt );
        Free( netw );
        return NULL ;
    }
    /* initialize send sem bolt */
    if( sem_init(&netw -> send_blocker, 0, 0 ) != 0 )
    {
        n_log( LOG_ERR, "Error initializing netw -> eventbolt" );
        pthread_mutex_destroy( &netw -> eventbolt );
        pthread_mutex_destroy( &netw -> sendbolt );
        pthread_mutex_destroy( &netw -> recvbolt );
        Free( netw );
        return NULL ;
    }
    /*initialize queues */
    netw -> recv_buf = new_generic_list( recv_list_limit );
    if( ! netw -> recv_buf )
    {
        n_log( LOG_ERR, "Error when creating receive list with %d item limit", recv_list_limit );
        netw_close( &netw );
        return NULL ;
    }
    netw -> send_buf = new_generic_list( send_list_limit );
    if( !netw -> send_buf )
    {
        n_log( LOG_ERR, "Error when creating send list with %d item limit", send_list_limit );
        netw_close( &netw );
        return NULL ;
    }
    netw -> pools = new_generic_list( -1 );
    if( !netw -> pools )
    {
        n_log( LOG_ERR, "Error when creating pools list" );
        netw_close( &netw );
        return NULL ;
    }
    netw -> addr_infos_loaded = 0 ;
    netw -> send_queue_consecutive_wait = 0 ;
    netw -> so_reuseaddr = -1 ;
    //netw -> so_reuseport = -1 ;
    netw -> so_keepalive = -1 ;
    netw -> tcpnodelay = -1 ;
    netw -> so_sndbuf = -1 ;
    netw -> so_rcvbuf = -1 ;
    netw -> so_rcvtimeo = -1 ;
    netw -> so_sndtimeo = -1 ;
    netw -> so_linger = -1 ;
    netw -> deplete_timeout = 0 ;
    netw -> crypto_algo = NETW_ENCRYPT_NONE ;

    netw -> send_data = &send_data;
    netw -> recv_data = &recv_data;

#ifdef HAVE_OPENSSL
    netw -> method = NULL ;
    netw -> ctx = NULL ;
    netw -> ssl = NULL ;
    netw -> key = NULL ;
    netw -> certificate = NULL ;
#endif

    netw -> link . is_blocking = -1 ;
    return netw ;

} /* netw_new() */



/*!\fn char *get_in_addr(struct sockaddr *sa)
 *\brief get sockaddr, IPv4 or IPv6
 *\param sa addrinfo to get
 *\return socket address
 */
char *get_in_addr(struct sockaddr *sa)
{
    return sa->sa_family == AF_INET
        ? (char *)&(((struct sockaddr_in *)sa)->sin_addr)
        : (char *)&(((struct sockaddr_in6 *)sa)->sin6_addr);
}


/*!\fn int netw_init_wsa( int mode , int v1 , int v2 )
 *\brief Do not directly use, internal api. Initialize winsock dll loading on windows if needed.
 *\param mode 1 for opening 0 for close 2 for status
 *\param v1 First digit of version requested
 *\param v2 Second digit of version requested
 *\return TRUE on success FALSE on error
 */
int netw_init_wsa( int mode, int v1, int v2 )
{
    int compiler_warning_suppressor = 0 ;
#if !defined( __linux__ ) && !defined( __sun ) && !defined( _AIX )
    static WSADATA WSAdata; /*WSA world*/
    static int WSA_IS_INITIALIZED = 0; /*status checking*/

    switch ( mode )
    {
        default:
            /*returning WSA status*/
        case 2 :
            return WSA_IS_INITIALIZED;
            break;
            /*loading WSA dll*/
        case 1 :
            if ( WSA_IS_INITIALIZED == 1 )
                return TRUE; /*already loaded*/
            if ( ( WSAStartup( MAKEWORD( v1, v2 ), &WSAdata ) ) != 0 )
            {
                WSA_IS_INITIALIZED = 0;
                return FALSE;
            }
            else
            {
                WSA_IS_INITIALIZED = 1 ;
                return TRUE;
            }
            break;
            /*unloading (closing) WSA */
        case 0 :
            if ( WSA_IS_INITIALIZED == 0 )
                return TRUE; /*already CLEANED or not loaded */
            if ( WSACleanup() == 0 )
            {
                WSA_IS_INITIALIZED = 0;
                return TRUE;
            }
            break;
    } /*switch(...)*/
#endif /* ifndef __linux__ __sun _AIX */
    compiler_warning_suppressor = mode + v1 + v2 ;
    compiler_warning_suppressor = TRUE ;
    return compiler_warning_suppressor ;
} /*netw_init_wsa(...)*/



/*!\fn int netw_set_blocking( NETWORK *netw , unsigned long int is_blocking )
 *\brief Modify blocking socket mode
 *\param netw The network to configure
 *\param is_blocking 0 NON BLOCk , 1 BLOCK
 *\return TRUE or FALSE
 */
int netw_set_blocking( NETWORK *netw, unsigned long int is_blocking )
{
    __n_assert( netw, return FALSE );

    int error = 0 ;
    char *errmsg = NULL ;

#if defined(__linux__) || defined(__sun)
    int flags = 0 ;
    flags = fcntl( netw -> link . sock, F_GETFL, 0 );
    if( netw -> link . is_blocking != 0 && !is_blocking)
    {
        if ( (flags &O_NONBLOCK) && !is_blocking )
        {
            n_log( LOG_DEBUG, "socket %d was already in non-blocking mode", netw -> link . sock );
            /* in case we missed it, let's update the link mode */
            netw -> link. is_blocking = 0 ;
            return TRUE;
        }
    }
    else if( netw -> link . is_blocking != 1 && is_blocking )
    {
        if (!(flags &O_NONBLOCK) && is_blocking )
        {
            n_log( LOG_DEBUG, "socket %d was already in blocking mode", netw -> link . sock );
            /* in case we missed it, let's update the link mode */
            netw -> link. is_blocking = 1 ;
            return TRUE;
        }
    }
    if( fcntl(netw -> link . sock, F_SETFL, is_blocking ? flags ^ O_NONBLOCK : flags | O_NONBLOCK) == -1 )
    {
        error = neterrno ;
        errmsg = netstrerror( error );
        n_log( LOG_ERR, "couldn't set blocking mode %d on %d: %s", is_blocking, netw -> link . sock, _str( errmsg ) );
        FreeNoLog( errmsg );
        return FALSE ;
    }
#else
    unsigned long int blocking = 1 - is_blocking ;
    int res = ioctlsocket( netw -> link . sock, FIONBIO, &blocking );
    error = neterrno ;
    if( res != NO_ERROR )
    {
        errmsg = netstrerror( error );
        n_log( LOG_ERR, "ioctlsocket failed with error: %ld , neterrno: %s", res, _str( errmsg ) );
        FreeNoLog( errmsg );
        netw_close( &netw );
        return FALSE ;
    }
#endif
    netw -> link. is_blocking = is_blocking ;
    return TRUE ;
} /* netw_set_blocking */



/*!\fn int netw_setsockopt( NETWORK *netw  , int optname , int value )
 *\brief Modify common socket options on the given netw
 *\param netw The socket to configure
 *\param optname NETWORK_DEPLETE_TIMEOUT,NETWORK_CONSECUTIVE_SEND_TIMEOUT ,SO_REUSEADDR,SO_REUSEPORT,SO_KEEPALIVE,TCP_NODELAY,SO_SNDBUF,SO_RCVBUF,SO_LINGER,SO_RCVTIMEO,SO_SNDTIMEO. Please refer to man setsockopt for details
 *\param value The value of the socket parameter
 *\return TRUE or FALSE
 */
int netw_setsockopt( NETWORK *netw, int optname, int value )
{
    __n_assert( netw, return FALSE );

    int error = 0 ;
    char *errmsg = NULL ;

    switch( optname )
    {
        case TCP_NODELAY :
            if( value >= 0 )
            {
                /* disable naggle algorithm */
                if ( setsockopt( netw -> link . sock, IPPROTO_TCP, TCP_NODELAY, ( const char * ) &value, sizeof( value ) ) == -1 )
                {
                    error = neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(TCP_NODELAY) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                    FreeNoLog( errmsg );
                    return FALSE ;
                }
            }
            netw -> tcpnodelay = value ;
            break ;
        case SO_SNDBUF :
            /* socket sending buffer size */
            if( value >= 0 )
            {
                if ( setsockopt ( netw -> link . sock, SOL_SOCKET, SO_SNDBUF, ( const char * ) &value, sizeof( value ) ) == -1 )
                {
                    error = neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(SO_SNDBUF) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                    return FALSE ;
                }
            }
            netw -> so_sndbuf = value ;
            break ;
        case SO_RCVBUF :
            /* socket receiving buffer */
            if( value >= 0 )
            {
                if ( setsockopt ( netw -> link . sock, SOL_SOCKET, SO_RCVBUF, ( const char * ) &value, sizeof( value ) ) == -1 )
                {
                    error = neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(SO_RCVBUF) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                    FreeNoLog( errmsg );
                    return FALSE ;
                }
            }
            netw -> so_rcvbuf = value ;
            break ;
        case SO_REUSEADDR :
            /* lose the pesky "Address already in use" error message*/
            if ( setsockopt( netw -> link . sock, SOL_SOCKET, SO_REUSEADDR, (char *)&value, sizeof( value  ) ) == -1 )
            {
                error=neterrno ;
                errmsg = netstrerror( error );
                n_log( LOG_ERR, "Error from setsockopt(SO_REUSEADDR) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                FreeNoLog( errmsg );
                return FALSE ;
            }
            netw -> so_reuseaddr = value ;
            break ;
            /*case SO_REUSEPORT :
            // lose the pesky "port already in use" error message
            if ( setsockopt( netw -> link . sock, SOL_SOCKET, SO_REUSEPORT, (char *)&value, sizeof( value  ) ) == -1 )
            {
            error=neterrno ;
            errmsg = netstrerror( error );
            n_log( LOG_ERR, "Error from setsockopt(SO_REUSEPORT) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
            FreeNoLog( errmsg );
            return FALSE ;
            }
            netw -> so_reuseport = value ;
            break ;*/
        case SO_LINGER :
            {
                struct linger ling;
                if( value < 0 )
                {
                    ling.l_onoff=0;
                    ling.l_linger=0;
                }
                else if( value == 0 )
                {
                    ling.l_onoff=1;
                    ling.l_linger=0;
                }
                else
                {
                    ling.l_onoff=1;
                    ling.l_linger=value;
                }
#ifndef __windows__
                if( setsockopt(netw -> link . sock, SOL_SOCKET, SO_LINGER, &ling, sizeof( ling ) ) == -1 )
                {
                    error=neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(SO_LINGER) on socket %d. neterrno: %s", netw -> link . sock,_str( errmsg ) );
                    FreeNoLog( errmsg );
                    return FALSE ;
                }
#else
                if( setsockopt(netw -> link . sock, SOL_SOCKET, SO_LINGER, (const char *)&ling, sizeof( ling ) ) == -1 )
                {
                    error=neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(SO_LINGER) on socket %d. neterrno: %s", netw -> link . sock,_str( errmsg ) );
                    FreeNoLog( errmsg );
                    return FALSE ;
                }
#endif // __windows__
                netw -> so_linger = value ;
            }
            break ;
        case SO_RCVTIMEO :
            if( value >= 0 )
            {
#ifndef __windows__
                {
                    struct timeval tv;
                    tv.tv_sec = value ;
                    tv.tv_usec = 0;
                    if( setsockopt(netw -> link . sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) == -1 )
                    {
                        error=neterrno ;
                        errmsg = netstrerror( error );
                        n_log( LOG_ERR, "Error from setsockopt(SO_RCVTIMEO) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                        FreeNoLog( errmsg );
                        return FALSE ;
                    }
                }
#else
                if( setsockopt(netw -> link . sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&value, sizeof value ) == -1 )
                {
                    error=neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(SO_RCVTIMEO) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                    FreeNoLog( errmsg );
                    return FALSE ;
                }
#endif
            }
            netw -> so_rcvtimeo = value ;
            break ;
        case SO_SNDTIMEO :
            if( value >= 0 )
            {
#ifndef __windows__
                {
                    struct timeval tv;
                    tv.tv_sec = value ;
                    tv.tv_usec = 0;

                    if( setsockopt(netw -> link . sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv) == -1 )
                    {
                        error=neterrno ;
                        errmsg = netstrerror( error );
                        n_log( LOG_ERR, "Error from setsockopt(SO_SNDTIMEO) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                        FreeNoLog( errmsg );
                        return FALSE ;
                    }
                }
#else
                if( setsockopt(netw -> link . sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&value, sizeof value ) == -1 )
                {
                    error=neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(SO_SNDTIMEO) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                    FreeNoLog( errmsg );
                    return FALSE ;
                }
#endif
            }
            netw -> so_sndtimeo = value ;
            break ;
        case NETWORK_DEPLETE_TIMEOUT:
            netw -> deplete_timeout = value ;
            break;
        case NETWORK_CONSECUTIVE_SEND_TIMEOUT:
            netw -> send_queue_consecutive_wait = value ;
            break ;
#ifdef __linux__
        case TCP_USER_TIMEOUT:
            {
                unsigned int timeout = 1000 * value ;
                if( setsockopt(netw -> link . sock, SOL_SOCKET, TCP_USER_TIMEOUT, (const char*)&timeout, sizeof timeout) == -1 )
                {
                    error=neterrno ;
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "Error from setsockopt(TCP_USER_TIMEOUT) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                    FreeNoLog( errmsg );
                    return FALSE ;
                }
            }
            break ;
#endif
        case SO_KEEPALIVE:
            if( setsockopt(netw -> link . sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&value, sizeof value) == -1 )
            {
                error=neterrno ;
                errmsg = netstrerror( error );
                n_log( LOG_ERR, "Error from setsockopt(SO_KEEPALIVE) on socket %d. neterrno: %s", netw -> link . sock, _str( errmsg ) );
                FreeNoLog( errmsg );
                return FALSE ;
            }
            netw -> so_keepalive = value ;
            break ;

        default:
            n_log( LOG_ERR, "%d is not a supported setsockopt", optname );
            return FALSE ;
    }
    return TRUE ;
} /* netw_set_sock_opt */



#ifdef HAVE_OPENSSL

char *netw_get_openssl_error_string() {
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return NULL;
    }

    ERR_print_errors(bio);  // Write errors to the BIO

    char *buf;
    long len = BIO_get_mem_data(bio, &buf);  // Get data from the BIO

    // Allocate memory for the error string and copy it
    char *error_str = malloc(len + 1);
    if (error_str) {
        memcpy(error_str, buf, len);
        error_str[len] = '\0';  // Null-terminate the string
    }

    BIO_free(bio);  // Free the BIO

    return error_str;
}

void netw_ssl_print_errors( SOCKET socket )
{
    unsigned long error = 0 ;
    while( ( error = ERR_get_error() ) )
    {
        n_log( LOG_ERR, "socket %d: %s", socket , ERR_reason_error_string( error ) );
    }
}

/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 1998 - 2019, Daniel Stenberg, <daniel at haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/
/* <DESC>
 * Show the required mutex callback setups for GnuTLS and OpenSSL when using
 * libcurl multi-threaded.
 * </DESC>
 */
/* A multi-threaded example that uses pthreads and fetches 4 remote files at
 * once over HTTPS. The lock callbacks and stuff assume OpenSSL <1.1 or GnuTLS
 * (libgcrypt) so far.
 *
 * OpenSSL docs for this:
 *   https://www.openssl.org/docs/man1.0.2/man3/CRYPTO_num_locks.html
 * gcrypt docs for this:
 *   https://gnupg.org/documentation/manuals/gcrypt/Multi_002dThreading.html
 */

/* we have this global to let the callback get easy access to it */
static pthread_mutex_t *netw_ssl_lockarray;

__attribute__((unused)) static void netw_ssl_lock_callback(int mode, int type, char *file, int line)
{
    (void)file;
    (void)line;
    if(mode & CRYPTO_LOCK)
    {
        pthread_mutex_lock(&(netw_ssl_lockarray[type]));
    }
    else
    {
        pthread_mutex_unlock(&(netw_ssl_lockarray[type]));
    }
}

__attribute__((unused)) static unsigned long thread_id(void)
{
    unsigned long ret;

    ret = (unsigned long)pthread_self();
    return ret;
}

static void netw_init_locks(void)
{
    int i;

    netw_ssl_lockarray = (pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() *
            sizeof(pthread_mutex_t));
    for(i = 0; i<CRYPTO_num_locks(); i++)
    {
        pthread_mutex_init(&(netw_ssl_lockarray[i]), NULL);
    }

    CRYPTO_set_id_callback((unsigned long (*)())thread_id);
    CRYPTO_set_locking_callback((void (*)())netw_ssl_lock_callback);
}

static void netw_kill_locks(void)
{
    int i;

    CRYPTO_set_locking_callback(NULL);
    for(i = 0; i<CRYPTO_num_locks(); i++)
        pthread_mutex_destroy(&(netw_ssl_lockarray[i]));

    OPENSSL_free(netw_ssl_lockarray);
}



/*!\fn int netw_init_openssl( void )
 *\brief Do not directly use, internal api. Initialize openssl
 *\return TRUE
 */
int netw_init_openssl( void )
{
    static int OPENSSL_IS_INITIALIZED = 0; /*status checking*/

    if ( OPENSSL_IS_INITIALIZED == 1 )
        return TRUE; /*already loaded*/

    SSL_library_init();
    SSL_load_error_strings();
#if OPENSSL_VERSION_NUMBER < 0x10100000L	
    ERR_load_BIO_strings();
#endif
    OpenSSL_add_all_algorithms();
    netw_init_locks();

    OPENSSL_IS_INITIALIZED = 1 ;

    return TRUE ;
} /*netw_init_openssl(...)*/



/*!\fn int netw_unload_openssl( void )
 *\brief Do not directly use, internal api. Initialize openssl
 *\return TRUE
 */
int netw_unload_openssl( void )
{
    static int OPENSSL_IS_INITIALIZED = 0; /*status checking*/

    if ( OPENSSL_IS_INITIALIZED == 0 )
        return TRUE; /*already unloaded*/

    netw_kill_locks();
    EVP_cleanup();

    OPENSSL_IS_INITIALIZED = 0 ;

    return TRUE ;
} /*netw_unload_openssl(...)*/



/*!\fn int netw_set_crypto( NETWORK *netw, char *key, char *certificate )
 * \brief activate SSL encryption on selected network, using key and certificate
 * \param key path to the key file
 * \param certificate path to the certificate file
 * \return TRUE of FALSE
 */
int netw_set_crypto( NETWORK *netw, char *key, char *certificate )
{
    __n_assert( netw, return FALSE );

    if( key && strlen( key ) > 0 )
    {
        netw -> key = strdup( key );
    }
    if( certificate && strlen( certificate ) > 0 )
    {
        netw -> certificate = strdup( certificate );
    }
    if( key && certificate )
    {
        netw_init_openssl();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L  // OpenSSL 1.1.0 or later
        netw -> method = TLS_method();         // create new server-method instance 
#else
        netw -> method = TLSv1_2_method();         // create new server-method instance 
#endif
        netw -> ctx    = SSL_CTX_new(netw -> method);      // create new context from method 
        // SSL_CTX_set_verify(netw -> ctx, SSL_VERIFY_PEER, NULL); // Enable certificate verification

        if ( netw -> ctx == NULL )
        {
            netw_ssl_print_errors( netw -> link . sock );
            return FALSE ;
        }

        // Load default system certs
        if (SSL_CTX_load_verify_locations(netw -> ctx, NULL, "/etc/ssl/certs/") != 1) {
            netw_ssl_print_errors( netw -> link . sock );
            return FALSE ;
        }

        if (SSL_CTX_use_certificate_file( netw -> ctx, certificate, SSL_FILETYPE_PEM) <= 0) {
            netw_ssl_print_errors( netw -> link . sock );
            return FALSE ;
        }
        if (SSL_CTX_use_PrivateKey_file( netw -> ctx, key, SSL_FILETYPE_PEM) <= 0) {
            netw_ssl_print_errors( netw -> link . sock );
            return FALSE ;
        }

        netw -> send_data = &send_ssl_data ;
        netw -> recv_data = &recv_ssl_data ;

        netw -> crypto_algo = NETW_ENCRYPT_OPENSSL ;
    }


    return TRUE ;
} /* netw_set_crypto */

#endif // HAVE_OPENSSL WIP


/*!\fn int netw_connect_ex(NETWORK **netw , char *host , char *port, int send_list_limit , int recv_list_limit , int ip_version, char *ssl_key_file, char *ssl_cert_file )
 *\brief Use this to connect a NETWORK to any listening one
 *\param netw a NETWORK *object
 *\param host Host or IP to connect to
 *\param port Port to use to connect
 *\param send_list_limit Internal sending list maximum number of item. 0 or negative for unrestricted
 *\param recv_list_limit Internal receiving list maximum number of item. 0 or negative for unrestricted
 *\param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *\param ssl_key_file NULL or the path to the SSL key file to use. If set, a ssl_cert_file must also be provided
 *\param ssl_cert_file NULL or the path to the SSL certificate file to use. If set, a ssl_key_file must also be provided
 *\return TRUE or FALSE
 */
int netw_connect_ex( NETWORK **netw, char *host, char *port, int send_list_limit, int recv_list_limit, int ip_version, char *ssl_key_file, char *ssl_cert_file )
{
    // kill compilation warning when there is no openssl
    (void)ssl_key_file ;
    (void)ssl_cert_file ;
    int error = 0, net_status = 0;
    char *errmsg = NULL ;

    /*do not work over an already used netw*/
    if( (*netw) )
    {
        n_log( LOG_ERR, "Unable to allocate (*netw), already existing. You must use empty NETWORK *structs." );
        return FALSE ;
    }

    /*creating array*/
    (*netw) = netw_new( send_list_limit, recv_list_limit );
    __n_assert( netw&&(*netw), return FALSE );

    /*checking WSA when under windows*/
    if ( netw_init_wsa( 1, 2, 2 ) == FALSE )
    {
        n_log( LOG_ERR, "Unable to load WSA dll's" );
        Free( (*netw) );
        return FALSE ;
    }

    /* choose ip version */
    if( ip_version == NETWORK_IPV4 )
    {
        (*netw) -> link . hints . ai_family   = AF_INET ;     /* Allow IPv4 */
    }
    else if( ip_version == NETWORK_IPV6 )
    {
        (*netw) -> link . hints . ai_family   = AF_INET6 ;     /* Allow IPv6 */
    }
    else
    {
        /* NETWORK_ALL or unknown value */
        (*netw) -> link . hints . ai_family   = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    }

    (*netw) -> link . hints . ai_socktype = SOCK_STREAM;
    (*netw) -> link . hints . ai_protocol = IPPROTO_TCP;
    (*netw) -> link . hints . ai_flags    = AI_PASSIVE;
    (*netw) -> link . hints . ai_canonname = NULL;
    (*netw) -> link . hints . ai_next = NULL;

    /* Note: on some system, i.e Solaris, it WILL show leak in getaddrinfo.
     * Testing it inside a 1,100 loop showed not effect on the amount of leaked
     * memory */
    error = getaddrinfo( host, port, &(*netw) -> link . hints, &(*netw) -> link . rhost );
    if( error != 0 )
    {
        n_log( LOG_ERR, "Error when resolving %s:%s getaddrinfo: %s", host, port, gai_strerror( error ) );
        netw_close( &(*netw) );
        return FALSE ;
    }
    (*netw) -> addr_infos_loaded = 1 ;
    Malloc( (*netw) -> link . ip, char, 64 );
    __n_assert( (*netw) -> link . ip, netw_close( &(*netw ) ); return FALSE );

    /* getaddrinfo() returns a list of address structures. Try each address until we successfully connect. If socket or connect fails, we close the socket and try the next address. */
    struct addrinfo *rp = NULL ;
    for( rp = (*netw) -> link . rhost ; rp != NULL ; rp = rp -> ai_next )
    {
        int sock = socket( rp -> ai_family, rp -> ai_socktype, rp->ai_protocol );
        if( sock == -1 )
        {
            error = neterrno ;
            errmsg = netstrerror( error );
            n_log( LOG_ERR, "Error while trying to make a socket: %s", _str( errmsg ) );
            FreeNoLog( errmsg );
            continue ;
        }

        (*netw) -> link . sock = sock ;

        net_status = connect( sock, rp -> ai_addr, rp -> ai_addrlen );
        if( net_status == -1 )
        {
            error = neterrno ;
            errmsg = netstrerror( error );
            n_log( LOG_INFO, "connecting to %s:%s : %s", host, port, _str( errmsg ) );
            FreeNoLog( errmsg );
            closesocket( sock );
            (*netw) -> link . sock = -1 ;
            continue ;
        }
        else
        {
            /*storing connected port and ip adress*/
            if(!inet_ntop( rp->ai_family, get_in_addr( rp -> ai_addr ), (*netw) -> link . ip, 64 ) )
            {
                error = neterrno ;
                errmsg = netstrerror( error );
                n_log( LOG_ERR, "inet_ntop: %p , %s", rp, _str( errmsg ) );
                FreeNoLog( errmsg );
            }
            break;                  /* Success */
        }
    }
    if( rp == NULL )
    {
        /* No address succeeded */
        n_log( LOG_ERR, "Couldn't connect to %s:%s : no adress succeeded", host, port );
        netw_close( &(*netw) );
        return FALSE ;
    }

    (*netw)->link . port = strdup( port );
    __n_assert( (*netw) -> link . port, netw_close( &(*netw ) ); return FALSE );


    if( ssl_key_file && ssl_cert_file )
    {
#ifdef HAVE_OPENSSL
        if( netw_set_crypto( (*netw) , ssl_key_file , ssl_cert_file ) == FALSE )
        {
            /* could not initialize SSL */
            n_log( LOG_ERR, "couldn't initialize SSL !" );
            netw_close( &(*netw) );
            return FALSE ;
        }

        (*netw) -> ssl = SSL_new( (*netw) -> ctx );
        SSL_set_fd( (*netw) -> ssl, (*netw) -> link . sock);

        // Perform SSL Handshake
        if (SSL_connect((*netw) -> ssl) <= 0) {
            /* could not connect with SSL */
            n_log( LOG_ERR, "SSL Handshake error !" );
            netw_close( &(*netw) );
            return FALSE ;
        }
        n_log( LOG_DEBUG, "SSL-Connected to %s:%s", (*netw) -> link . ip, (*netw) -> link . port );
#else
        n_log( LOG_ERR, "%s:%s trying to configure SSL but application was compiled without SSL support !", (*netw) -> link . ip, (*netw) -> link . port );
#endif
    }
    else
    {
        n_log( LOG_DEBUG, "Connected to %s:%s", (*netw) -> link . ip, (*netw) -> link . port );
    }

    netw_set( (*netw), NETW_CLIENT|NETW_RUN|NETW_THR_ENGINE_STOPPED );

    return TRUE ;
} /* netw_connect_ex(...)*/



/*!\fn int netw_connect( NETWORK **netw , char *host , char *port , int ip_version )
 *\brief Use this to connect a NETWORK to any listening one, unrestricted send/recv lists
 *\param netw a NETWORK *object
 *\param host Host or IP to connect to
 *\param port Port to use to connect
 *\param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *\return TRUE or FALSE
 */
int netw_connect( NETWORK **netw, char *host, char *port, int ip_version )
{
    n_log( LOG_INFO, "Trying to connect to %s : %s", _str( host ), _str( port ) );
    return netw_connect_ex( &(*netw), host, port, -1, -1, ip_version, NULL, NULL );
} /* netw_connect() */



#ifdef HAVE_OPENSSL
/*!\fn int netw_ssl_connect( NETWORK **netw , char *host , char *port , int ip_version, char *ssl_key_file , char *ssl_cert_file )
 *\brief Use this to connect a NETWORK to any listening one, unrestricted send/recv lists
 *\param netw a NETWORK *object
 *\param host Host or IP to connect to
 *\param port Port to use to connect
 *\param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *\param ssl_key_file NULL or the path to the SSL key file to use. If set, a ssl_cert_file must also be provided
 *\param ssl_cert_file NULL or the path to the SSL certificate file to use. If set, a ssl_key_file must also be provided
 *\return TRUE or FALSE
 */
int netw_ssl_connect( NETWORK **netw, char *host, char *port, int ip_version, char *ssl_key_file , char *ssl_cert_file )
{
    n_log( LOG_INFO, "Trying to connect to %s : %s", _str( host ), _str( port ) );
    return netw_connect_ex( &(*netw), host, port, -1, -1, ip_version, ssl_key_file, ssl_cert_file );
} /* netw_connect() */
#endif



/*!\fn int netw_get_state( NETWORK *netw , int *state , int *thr_engine_status )
 *\brief Get the state of a network
 *\param netw The NETWORK *connection to query
 *\param state pointer to network status storage , NETW_RUN , NETW_EXIT_ASKED , NETW_EXITED
 *\param thr_engine_status pointer to network thread engine status storage ,NETW_THR_ENGINE_STARTED , NETW_THR_ENGINE_STOPPED
 *\return TRUE or FALSE
 */
int netw_get_state( NETWORK *netw, int *state, int *thr_engine_status )
{
    if( netw )
    {
        pthread_mutex_lock( &netw -> sendbolt );
        if( state )
            (*state) = netw -> state ;
        if( thr_engine_status )
            (*thr_engine_status) = netw -> threaded_engine_status ;
        pthread_mutex_unlock( &netw -> sendbolt );
        return TRUE ;
    }
    else
    {
        n_log( LOG_ERR, "Can't get status of a NULL network" );
    }
    return FALSE ;
} /*netw_get_state() */



/*!\fn int netw_set( NETWORK *netw , int flag )
 *\brief Restart or reset the specified network ability
 *\param netw The NETWORK *connection to modify
 *\param flag NETW_EMPTY_SENDBUF, NETW_EMPTY_RECVBUF, NETW_RUN , NETW_EXIT_ASKED , NETW_EXITED
 *\return TRUE or FALSE
 */
int netw_set( NETWORK *netw, int flag )
{
    if( flag & NETW_EMPTY_SENDBUF )
    {
        pthread_mutex_lock( &netw -> sendbolt );
        if( netw -> send_buf )
            list_empty( netw -> send_buf );
        pthread_mutex_unlock( &netw -> sendbolt );
    };
    if( flag & NETW_EMPTY_RECVBUF )
    {
        pthread_mutex_lock( &netw -> recvbolt );
        if( netw -> recv_buf )
            list_empty( netw -> recv_buf );
        pthread_mutex_unlock( &netw -> recvbolt );
    }
    if( flag & NETW_DESTROY_SENDBUF )
    {
        pthread_mutex_lock( &netw -> sendbolt );
        if( netw -> send_buf )
            list_destroy( &netw -> send_buf );
        pthread_mutex_unlock( &netw -> sendbolt );
    };
    if( flag & NETW_DESTROY_RECVBUF )
    {
        pthread_mutex_lock( &netw -> recvbolt );
        if( netw -> recv_buf )
            list_destroy( &netw -> recv_buf );
        pthread_mutex_unlock( &netw -> recvbolt );
    }
    pthread_mutex_lock( &netw -> eventbolt );
    if( flag & NETW_CLIENT )
    {
        netw -> mode = NETW_CLIENT ;
    }
    if( flag & NETW_SERVER )
    {
        netw -> mode = NETW_SERVER ;
    }
    if( flag & NETW_RUN )
    {
        netw -> state = NETW_RUN ;
    }
    if( flag & NETW_EXITED )
    {
        netw -> state = NETW_EXITED ;
    }
    if( flag & NETW_ERROR )
    {
        netw -> state = NETW_ERROR ;
    }
    if( flag & NETW_EXIT_ASKED )
    {
        netw -> state = NETW_EXIT_ASKED ;
    }
    if( flag & NETW_THR_ENGINE_STARTED )
    {
        netw -> threaded_engine_status = NETW_THR_ENGINE_STARTED ;
    }
    if( flag & NETW_THR_ENGINE_STOPPED )
    {
        netw -> threaded_engine_status = NETW_THR_ENGINE_STOPPED ;
    }
    pthread_mutex_unlock( &netw -> eventbolt );

    sem_post( &netw -> send_blocker );

    return TRUE;
} /* netw_set(...) */



/*!\fn netw_close( NETWORK **netw )
 *\brief Closing a specified Network, destroy queues, free the structure
 *\param netw A NETWORK *network to close
 *\return TRUE on success , FALSE on failure
 */
int netw_close( NETWORK **netw )
{
    int state = 0, thr_engine_status = 0 ;
    __n_assert( netw&&(*netw), return FALSE );

    list_foreach( node, (*netw) -> pools )
    {
        NETWORK_POOL *pool = (NETWORK_POOL *)node -> ptr ;
        netw_pool_remove( pool, (*netw) );
    }

    list_destroy( &(*netw) -> pools );

    netw_get_state( (*netw), &state, &thr_engine_status );
    if( thr_engine_status == NETW_THR_ENGINE_STARTED )
    {
        netw_stop_thr_engine( (*netw ) );
    }

    /*closing connection*/
    /* Now with end of send fix */
    if( (*netw) -> link . sock != INVALID_SOCKET )
    {
#ifdef HAVE_OPENSSL
        if( (*netw) -> crypto_algo == NETW_ENCRYPT_OPENSSL )
        {
            if( (*netw) -> ssl )
            {
                SSL_shutdown( (*netw) -> ssl );
                SSL_free( (*netw) -> ssl );
            }
            else
            {
                n_log( LOG_ERR , "SSL handle of socket %d was already NULL" , (*netw) -> link . sock );
            }
        }
#endif
        closesocket( (*netw) -> link . sock );
#ifdef HAVE_OPENSSL
        if( (*netw) -> crypto_algo == NETW_ENCRYPT_OPENSSL )
        {
            if( (*netw) -> ctx )
            {	
                SSL_CTX_free( (*netw) -> ctx );
            }
            else
            {
                n_log( LOG_ERR , "SSL context of socket %d was already NULL" , (*netw) -> link . sock );
            }
        }
        n_log( LOG_DEBUG, "socket %d closed", (*netw) -> link . sock );
        FreeNoLog( (*netw) -> key );
        FreeNoLog( (*netw) -> certificate );
#endif
    }

    FreeNoLog( (*netw) -> link . ip );
    FreeNoLog( (*netw) -> link . port );

    if( (*netw) -> link . rhost )
    {
        freeaddrinfo( (*netw) -> link . rhost );
    }

    /*list freeing*/
    netw_set( (*netw), NETW_DESTROY_SENDBUF|NETW_DESTROY_RECVBUF );

    pthread_mutex_destroy( &(*netw) -> recvbolt );
    pthread_mutex_destroy( &(*netw) -> sendbolt );
    pthread_mutex_destroy( &(*netw) -> eventbolt );
    sem_destroy( &(*netw) -> send_blocker );

    Free( (*netw) );

    return TRUE;
} /* netw_close(...)*/



/*!\fn int deplete_send_buffer( int fd , size_t timeout )
 *\brief wait until the socket is empty or timeout, checking each 100 msec. Not as reliable as expected.
 *\param fd socket descriptor
 *\param timeout timeout value in msec , zero => disabled
 *\return 0 or the amount of remaining datas in bytes
 */
int deplete_send_buffer( int fd, size_t timeout )
{
#if defined( __linux__ )
    int outstanding = 0 ;
    if( timeout == 0 )
    {
        return 0 ;
    }
    for( size_t it = 0 ; it < timeout ; it += 100 )
    {
        outstanding = 0 ;
        ioctl(fd, SIOCOUTQ, &outstanding);
        if(!outstanding)
        {
            break;
        }
        usleep( 100000 );
    }
    return outstanding ;
#else
    (void)fd;
    (void)timeout;
    return 0 ;
#endif
}


/*!\fn netw_wait_close( NETWORK **netw )
 *\brief Wait for peer closing a specified Network, destroy queues, free the structure. Default 30 seconds timeout
 *\warning Do not use on the accept socket itself (the server socket) as it will display false errors
 *\param netw A NETWORK *network to close
 *\return TRUE on success , FALSE on failure
 */
int netw_wait_close( NETWORK **netw )
{
    // default 30 secs timeout
    return netw_wait_close_timed( netw, 30 );
} /* netw_wait_close(...)*/



/*!\fn netw_wait_close_timed( NETWORK **netw , size_t timeout )
 *\brief Wait for peer closing a specified Network, destroy queues, free the structure
 *\warning Do not use on the accept socket itself (the server socket) as it will display false errors
 *\param netw A NETWORK *network to close
 *\param timeout timeout in seconds before force close engine , 0 to instantly close (not recommanded)
 *\return TRUE on success , FALSE on failure
 */
int netw_wait_close_timed( NETWORK **netw, size_t timeout )
{
    int state = 0, thr_engine_status = 0 ;
    __n_assert( netw&&(*netw), return FALSE );

    int error = 0 ;
    size_t countdown = timeout * 1000000 ;
    char *errmsg = NULL ;
    int nb_running = 0 ;

    netw_get_state( (*netw), &state, &thr_engine_status );
    if( thr_engine_status == NETW_THR_ENGINE_STARTED )
    {
        do
        {
            pthread_mutex_lock( &(*netw) -> eventbolt );
            nb_running = (*netw) -> nb_running_threads ;
            pthread_mutex_unlock( &(*netw) -> eventbolt );
            usleep( 100000 );
            countdown -= 100000 ;
        }
        while( nb_running > 0 && countdown > 0 );
        netw_get_state( (*netw), &state, &thr_engine_status );
        if( countdown == 0 && nb_running > 0 )
        {
            n_log( LOG_ERR, "netw %d: %d threads are still running after %d seconds, netw is in state %s (%d)", (*netw) -> link . sock, nb_running, timeout, N_ENUM_ENTRY(__netw_code_type,toString)(state), state );
        }
    }

    /* wait for close fix */
    if( (*netw) -> link . sock != INVALID_SOCKET )
    {
        /* inform peer that we have finished */
        shutdown( (*netw) -> link . sock, SHUT_WR );
        int remaining = deplete_send_buffer( (*netw) -> link . sock, (*netw) -> deplete_timeout );
        if( remaining > 0 )
        {
            n_log( LOG_ERR, "socket %d (%s:%s) %d octets still in send buffer before closing after a wait of %d seconds", (*netw) -> link . sock, (*netw) -> link . ip, (*netw) -> link . port, remaining, (int)(*netw) -> deplete_timeout );
        }
        /* wait for fin ack */
        char buffer[ 4096 ] = "" ;
        for( size_t it = 0 ; it < (timeout*1000000) ; it += 100000 )
        {
            int res = 0 ;
            res = recv( (*netw) -> link . sock, buffer, 4096, NETFLAGS );
            if( !res )
                break;
            if( res < 0 )
            {
                error = neterrno ;
                if( error != ENOTCONN && error != 10057 && error != EINTR && error != ECONNRESET ) // no an error if already closed
                {
                    errmsg = netstrerror( error );
                    n_log( LOG_ERR, "read returned error %d when closing socket %d (%s:%s): %s", error, (*netw) -> link . sock, _str( (*netw) -> link . ip ), (*netw) -> link . port,  _str( errmsg ) );
                    FreeNoLog( errmsg );
                }
                break ;
            }
            usleep( 100000 );
        }
    }
    return netw_close( &(*netw) );

} /* netw_wait_close_timed(...)*/



/*!\fn int netw_make_listening( NETWORK **netw , char *addr , char *port , int nbpending  , int ip_version )
 *\brief Make a NETWORK be a Listening network
 *\param netw A NETWORK **network to make listening
 *\param addr Adress to bind, NULL for automatic address filling
 *\param port For choosing a PORT to listen to
 *\param nbpending Number of pending connection when listening
 *\param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *\return TRUE on success, FALSE on error
 */
int netw_make_listening( NETWORK **netw, char *addr, char *port, int nbpending, int ip_version )
{
    __n_assert( port, return FALSE );

    int error = 0 ;
    char *errmsg = NULL ;

    if( *netw )
    {
        n_log( LOG_ERR, "Cannot use an allocated network. Please pass a NULL network to modify" );
        return FALSE;
    }

    /*checking WSA when under windows*/
    if ( netw_init_wsa( 1, 2, 2 ) == FALSE )
    {
        n_log( LOG_ERR, "Unable to load WSA dll's" );
        return FALSE ;
    }

    (*netw) = netw_new( -1, -1 );
    (*netw) -> link . port = strdup( port );
    /*creating array*/
    if( ip_version == NETWORK_IPV4 )
    {
        (*netw) -> link . hints . ai_family   = AF_INET ;     /* Allow IPv4 */
    }
    else if( ip_version == NETWORK_IPV6 )
    {
        (*netw) -> link . hints . ai_family   = AF_INET6 ;     /* Allow IPv6 */
    }
    else
    {
        /* NETWORK_ALL or unknown value */
        (*netw) -> link . hints . ai_family   = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    }
    if( !addr )
    {
        (*netw) -> link . hints . ai_flags    = AI_PASSIVE;    /* For wildcard IP address */
    }
    (*netw) -> link . hints . ai_socktype = SOCK_STREAM;
    (*netw) -> link . hints . ai_protocol = IPPROTO_TCP;
    (*netw) -> link . hints . ai_canonname = NULL;
    (*netw) -> link . hints . ai_next = NULL;

    error = getaddrinfo( addr, port, &(*netw) -> link . hints, &(*netw) -> link . rhost );
    if( error != 0)
    {
        n_log( LOG_ERR, "Error when resolving %s:%s getaddrinfo: %s", _str( addr ),  port, gai_strerror( error ) );
        netw_close( &(*netw) );
        return FALSE ;
    }
    (*netw) -> addr_infos_loaded = 1 ;

    /* getaddrinfo() returns a list of address structures.
       Try each address until we successfully connect(2).
       If socket(2) (or connect(2)) fails, we (close the socket
       and) try the next address. */
    struct addrinfo *rp = NULL ;
    for( rp = (*netw) -> link . rhost ; rp != NULL ; rp = rp -> ai_next )
    {
        (*netw) -> link . sock = socket( rp -> ai_family, rp -> ai_socktype, rp->ai_protocol );
        if( (*netw) -> link . sock == INVALID_SOCKET )
        {
            error = neterrno ;
            errmsg = netstrerror( error );
            n_log( LOG_ERR, "Error while trying to make a socket: %s", _str( errmsg ) );
            FreeNoLog( errmsg );
            continue ;
        }
        netw_setsockopt( (*netw), SO_REUSEADDR, 1 );
        //netw_setsockopt( (*netw), SO_REUSEPORT, 1 );
        if( bind( (*netw) -> link . sock, rp -> ai_addr, rp -> ai_addrlen ) == 0 )
        {
            char *ip = NULL ;
            Malloc( ip, char, 64 );
            if( !ip )
            {
                n_log( LOG_ERR, "Error allocating 64 bytes for ip" );
                netw_close( &(*netw) );
                return FALSE ;
            }
            if( !inet_ntop( rp -> ai_family, get_in_addr( rp -> ai_addr ), ip, 64 ) )
            {
                error = neterrno ;
                errmsg = netstrerror( error );
                n_log( LOG_ERR, "inet_ntop: %p , %s", (*netw) -> link . raddr, _str( errmsg ) );
                FreeNoLog( errmsg );
            }
            /*n_log( LOG_DEBUG, "Socket %d successfully binded to %s %s",  (*netw) -> link . sock, _str( ip ) , _str( port ) );*/
            (*netw) -> link . ip = ip ;
            break;                  /* Success */
        }
        error = neterrno ;
        errmsg = netstrerror( error );
        n_log( LOG_ERR, "Error from bind() on port %s neterrno: %s", port, errmsg );
        FreeNoLog( errmsg );
        closesocket( (*netw) -> link .sock );
    }
    if( rp == NULL )
    {
        /* No address succeeded */
        n_log( LOG_ERR, "Couldn't get a socket for listening on port %s", port );
        netw_close( &(*netw) );
        return FALSE ;
    }

    /* nb_pending connections*/
    ( *netw ) -> nb_pending = nbpending ;
    listen( ( *netw ) -> link . sock, ( *netw ) -> nb_pending );

    netw_set( (*netw), NETW_SERVER|NETW_RUN|NETW_THR_ENGINE_STOPPED );

    return TRUE;
} /* netw_make_listening(...)*/



/*!\fn NETWORK *netw_accept_from_ex( NETWORK *from , int send_list_limit , int recv_list_limit , int blocking , int *retval )
 *\brief make a normal 'accept' . Network 'from' must be allocated with netw_make_listening.
 *\param from the network from where we accept
 *\param send_list_limit Internal sending list maximum number of item. 0 or negative for unrestricted
 *\param recv_list_limit Internal receiving list maximum number of item. 0 or negative for unrestricted
 *\param blocking set to -1 to make it non blocking, to 0 for blocking, else it's the select timeout value in msecs.
 *\param retval EAGAIN ou EWOULDBLOCK or neterrno (use netstrerr( retval) to obtain a string describing the code )
 *\return NULL on failure, if not a pointer to the connected network
 */
NETWORK *netw_accept_from_ex( NETWORK *from, int send_list_limit, int recv_list_limit, int blocking, int *retval )
{
    int tmp = 0, error = 0;
    char *errmsg = NULL ;

#if defined( __linux__ ) || defined( __sun ) || defined( _AIX )
    socklen_t sin_size = 0 ;
#else
    int sin_size = 0 ;
#endif

    NETWORK *netw = NULL ;

    /*checking WSA when under windows*/
    if( netw_init_wsa( 1, 2, 2 ) == FALSE )
    {
        n_log( LOG_ERR, "Unable to load WSA dll's" );
        return NULL ;
    }

    __n_assert( from, return NULL );

    netw = netw_new( send_list_limit, recv_list_limit );

    sin_size = sizeof( netw -> link . raddr );

    if( blocking > 0 )
    {
        int secs = blocking/1000 ;
        int usecs = (blocking%1000)*1000;
        struct timeval select_timeout = { secs, usecs };

        fd_set accept_set ;
        FD_SET( from -> link . sock, &accept_set );
        FD_ZERO( &accept_set );

        int ret = select( from -> link . sock + 1, &accept_set, NULL, NULL, &select_timeout );
        if( ret == -1 )
        {
            error = neterrno ;
            errmsg = netstrerror( error );
            if( retval != NULL )
                (*retval) = error ;
            n_log( LOG_DEBUG, "error on select with timeout %ds (%d.%ds), neterrno: %s", blocking, secs, usecs, _str( errmsg ) );
            FreeNoLog( errmsg );
            netw_close( &netw );
            return NULL;
        }
        else if( ret == 0 )
        {
            /* that one produce waaaay too much logs under a lot of cases */
            /* n_log( LOG_DEBUG , "No connection waiting on %d" , from -> link . sock ); */
            netw_close( &netw );
            return NULL;
        }
        if( FD_ISSET(  from -> link . sock, &accept_set ) )
        {
            //n_log( LOG_DEBUG, "select accept call on %d", from -> link . sock );
            tmp = accept( from -> link . sock, (struct sockaddr * )&netw -> link . raddr, &sin_size );
            if ( tmp < 0 )
            {
                error = neterrno ;
                errmsg = netstrerror( error );
                if( retval != NULL )
                    (*retval) = error ;
                n_log( LOG_DEBUG, "error accepting on %d, %s", netw -> link . sock, _str( errmsg ) );
                FreeNoLog( errmsg );
                netw_close( &netw );
                return NULL;
            }
        }
        else
        {
            netw_close( &netw );
            return NULL;
        }
        netw -> link . is_blocking = -1 ;
    }
    else if( blocking == -1 )
    {
        if( from -> link . is_blocking != 0 )
        {
            netw_set_blocking( from, 0 );
        }
        tmp = accept( from -> link . sock, (struct sockaddr *)&netw -> link . raddr, &sin_size );
        error = neterrno ;
        if( retval != NULL )
            (*retval) = error ;
        if ( tmp < 0 )
        {
            netw_close( &netw );
            return NULL;
        }
        netw -> link . is_blocking = -1 ;
    }
    else
    {
        if( from -> link . is_blocking  == 0 )
        {
            netw_set_blocking( from, 1 );
            n_log( LOG_DEBUG, "(default) blocking accept call on socket %d", from -> link . sock );
        }
        tmp = accept( from -> link . sock, (struct sockaddr *)&netw -> link . raddr, &sin_size );
        if ( tmp < 0 )
        {
            error = neterrno ;
            errmsg = netstrerror( error );
            if( retval != NULL )
                (*retval) = error ;
            n_log( LOG_DEBUG, "error accepting on socket %d, %s", netw -> link . sock, _str( errmsg ) );
            FreeNoLog( errmsg );
            netw_close( &netw );
            return NULL;
        }
        netw -> link . is_blocking = -1 ;
    }
    netw -> link . sock = tmp ;
    netw -> link . port = strdup( from -> link . port );
    Malloc( netw -> link . ip, char, 64 );
    if( !netw -> link . ip )
    {
        n_log( LOG_ERR, "Error allocating 64 bytes for ip" );
        netw_close( &netw );
        return FALSE ;
    }
    if( !inet_ntop( netw -> link . raddr . ss_family, get_in_addr( ((struct sockaddr *)&netw -> link . raddr) ), netw -> link . ip, 64 ) )
    {
        error = neterrno ;
        errmsg = netstrerror( error );
        n_log( LOG_ERR, "inet_ntop: %p , %s", netw -> link . raddr, _str( errmsg ) );
        FreeNoLog( errmsg );
        netw_close( &netw );
        return FALSE ;
    }

    netw_setsockopt( netw, SO_REUSEADDR, 1 );
    //netw_setsockopt( netw, SO_REUSEPORT, 1 );
    netw_setsockopt( netw, SO_KEEPALIVE, 1 );
    netw_set( netw, NETW_SERVER|NETW_RUN|NETW_THR_ENGINE_STOPPED );
    netw_set_blocking( netw , 1 );

    n_log( LOG_DEBUG, "Connection accepted from %s:%s socket %d", netw-> link . ip, netw -> link . port , netw -> link . sock );

#ifdef HAVE_OPENSSL
    if( from -> crypto_algo == NETW_ENCRYPT_OPENSSL )
    {		
        netw -> ssl = SSL_new( from -> ctx );
        SSL_set_fd( netw -> ssl, netw -> link . sock );

        netw -> send_data = &send_ssl_data ;
        netw -> recv_data = &recv_ssl_data ;

        if (SSL_accept(netw -> ssl) <= 0) {
            netw_ssl_print_errors( netw -> link . sock );
            netw_close( &netw );
            return FALSE ;
        } else {
            n_log( LOG_DEBUG, " socket %d: SSL connection established" , netw -> link . sock );
        }
    }
#endif

    return netw;
} /* netw_accept_from_ex(...) */



/*!\fn NETWORK *netw_accept_from( NETWORK *from )
 *\brief make a normal blocking 'accept' . Network 'from' must be allocated with netw_make_listening.
 *\param from The network from which to obtaion the connection
 *\return NULL
 */
NETWORK *netw_accept_from( NETWORK *from )
{
    return netw_accept_from_ex( from, 0, 0, 0, NULL );
} /* network_accept_from( ... ) */



/*!\fn NETWORK *netw_accept_nonblock_from( NETWORK *from , int blocking )
 *\brief make a normal blocking 'accept' . Network 'from' must be allocated with netw_make_listening.
 *\param from The network from which to obtaion the connection
 *\param blocking set to -1 to make it non blocking, to 0 for blocking, else it's the select timeout value in mseconds.
 *\return NULL
 */
NETWORK *netw_accept_nonblock_from( NETWORK *from, int blocking )
{
    return netw_accept_from_ex( from, 0, 0, blocking, NULL );
} /* network_accept_from( ... ) */


/*!\fn int netw_add_msg( NETWORK *netw , N_STR *msg )
 *\brief Add a message to send in aimed NETWORK
 *\param netw NETWORK where add the message
 *\param msg  the message to add
 *\return TRUE if success FALSE on error
 */
int netw_add_msg( NETWORK *netw, N_STR *msg )
{
    __n_assert( netw, return FALSE );
    __n_assert( msg, return FALSE );
    __n_assert( msg -> data, return FALSE );

    if( msg -> length <= 0 )
    {
        n_log( LOG_ERR, "Empty messages are not supported. msg(%p)->lenght=%d", msg, msg -> length );
        return FALSE;
    }

    pthread_mutex_lock( &netw -> sendbolt );

    if( list_push( netw -> send_buf, msg, free_nstr_ptr ) == FALSE )
    {
        pthread_mutex_unlock( &netw -> sendbolt );
        return FALSE;
    }

    pthread_mutex_unlock( &netw -> sendbolt );

    sem_post( &netw -> send_blocker );

    return TRUE;
} /* netw_add_msg(...) */



/*!\fn int netw_add_msg_ex( NETWORK *netw , char *str , unsigned int length )
 *\brief Add a message to send in aimed NETWORK
 *\param netw NETWORK where add the message
 *\param str  the message to add
 *\param length  the size of the message to add
 *\return TRUE if success FALSE on error
 */
int netw_add_msg_ex( NETWORK *netw, char *str, unsigned int length )
{
    __n_assert( netw, return FALSE );
    __n_assert( str, return FALSE );
    if( length == 0 )
    {
        return FALSE ;
    }

    N_STR *nstr = NULL ;
    Malloc( nstr, N_STR, 1 );
    __n_assert( nstr, return FALSE );

    if( length == 0 )
    {
        n_log( LOG_ERR, "Empty messages are not supported. msg(%p)->lenght=%d", str, length );
        return FALSE;
    }

    nstr -> data = str ;
    nstr -> written = nstr -> length = length ;

    pthread_mutex_lock( &netw -> sendbolt );
    if( list_push( netw -> send_buf, nstr, free_nstr_ptr ) == FALSE )
    {
        pthread_mutex_unlock( &netw -> sendbolt );
        return FALSE;
    }
    pthread_mutex_unlock( &netw -> sendbolt );

    sem_post( &netw -> send_blocker );

    return TRUE;
} /* netw_add_msg_ex(...) */




/*!\fn N_STR *netw_get_msg( NETWORK *netw )
 *\brief Get a message from aimed NETWORK
 *\param netw NETWORK where get the msg
 *\return NULL or a filled N_STR
 */
N_STR *netw_get_msg( NETWORK *netw )
{
    N_STR * ptr = NULL ;

    __n_assert( netw, return NULL );

    pthread_mutex_lock( &netw -> recvbolt );

    ptr = list_shift( netw -> recv_buf, N_STR );

    pthread_mutex_unlock( &netw -> recvbolt );

    return ptr ;
} /* netw_get_msg(...)*/



/*!\fn N_STR *netw_wait_msg( NETWORK *netw , size_t refresh , size_t timeout )
 *\brief Wait a message from aimed NETWORK. Recheck each usec until a valid
 *\param netw The link on which we wait a message
 *\param refresh The time in usec between each check until there is a message
 *\param timeout in usecs , maximum amount of time to wait before return. 0 to disable.
 *\return NULL or a filled N_STR
 */
N_STR *netw_wait_msg( NETWORK *netw, size_t refresh, size_t timeout )
{
    N_STR *nstrptr = NULL ;
    size_t timed = 0 ;
    size_t secs = 0 ;
    size_t usecs = 0 ;

    __n_assert( netw, return NULL );

    usecs = refresh ;
    if( refresh > 999999 )
    {
        secs = refresh / 1000000 ;
        usecs = refresh % 1000000 ;
    }

    if( timeout > 0 )
        timed = timeout ;

    int state = NETW_RUN, thr_state = 0 ;
    do
    {
        nstrptr = netw_get_msg( netw );
        if( nstrptr )
            return nstrptr ;

        if( timeout > 0 )
        {
            if( timed >= refresh )
                timed -= refresh ;
            if( timed == 0 || timed < refresh )
            {
                n_log( LOG_ERR, "netw %d, status: %s (%d), timeouted after waiting %zu msecs", netw -> link . sock, N_ENUM_ENTRY(__netw_code_type,toString)(state), state, timeout );
                return NULL ;
            }
        }
        if(secs > 0)
            sleep( secs );
        if( usecs > 0 )
            u_sleep( usecs );

        netw_get_state( netw, &state, &thr_state );
    }
    while( state != NETW_EXITED && state != NETW_ERROR );

    n_log( LOG_ERR, "got no answer and netw %d is no more running, state: %s (%d)" , netw -> link . sock, N_ENUM_ENTRY(__netw_code_type,toString)(state), state);

    return NULL ;
} /* netw_wait_msg(...) */



/*!\fn netw_start_thr_engine( NETWORK *netw )
 *\brief Start the NETWORK netw Threaded Engine. Create a sending & receiving thread.
 *\param netw The aimed NETWORK connection to start receiving data
 *\return TRUE on success FALSE on failure
 */
int netw_start_thr_engine( NETWORK *netw )
{
    __n_assert( netw, return FALSE );

    pthread_mutex_lock( &netw -> eventbolt );
    if( netw -> threaded_engine_status == NETW_THR_ENGINE_STARTED )
    {
        n_log( LOG_ERR, "THR Engine already started for network %p (%s)", netw, _str( netw -> link . ip ) );
        pthread_mutex_unlock( &netw -> eventbolt );
        return FALSE ;
    }

    if( pthread_create(  &netw -> recv_thr,   NULL,  netw_recv_func,  (void *) netw ) != 0 )
    {
        n_log( LOG_ERR, "Unable to create recv_thread for network %p (%s)", netw, _str( netw -> link . ip ) );
        pthread_mutex_unlock( &netw -> eventbolt );
        return FALSE ;
    }
    netw -> nb_running_threads ++ ;
    if( pthread_create(  &netw -> send_thr,   NULL,  netw_send_func,  (void *) netw ) != 0 )
    {
        n_log( LOG_ERR, "Unable to create send_thread for network %p (%s)", netw, _str( netw -> link . ip ) );
        pthread_mutex_unlock( &netw -> eventbolt );
        return FALSE ;
    }
    netw -> nb_running_threads ++ ;

    netw -> threaded_engine_status = NETW_THR_ENGINE_STARTED ;

    pthread_mutex_unlock( &netw -> eventbolt );

    return TRUE ;
}  /* netw_create_recv_thread(....) */



/*!\fn void *netw_send_func( void *NET )
 *\brief Thread send function
 *\param NET the NETWORK connection to use, casted into (void*)
 *\return NULL
 */
void *netw_send_func( void *NET )
{
    int DONE = 0,
        state = 0,
        net_status = 0 ;

    int32_t nboctet = 0 ;

    char nboct[ 5 ] = "" ;

    N_STR *ptr = NULL ;

    NETWORK *netw = (NETWORK *) NET ;

    __n_assert( netw, return NULL );

    do
    {
        /* do not consume cpu for nothing, reduce delay */
        sem_wait( &netw -> send_blocker );
        int message_sent = 0 ;
        while( message_sent == 0 && !DONE )
        {
            netw_get_state( netw, &state, NULL );
            if( state&NETW_ERROR )
            {
                DONE = 666 ;
            }
            else if( state&NETW_EXITED )
            {
                DONE = 100 ;
            }
            else if( state&NETW_EXIT_ASKED )
            {
                DONE = 100 ;
                /* sending state */
                nboctet = htonl( NETW_EXIT_ASKED );
                memcpy( nboct, &nboctet, sizeof( int32_t ) );
                n_log( LOG_DEBUG, "%d Sending Quit !", netw -> link . sock );
                net_status = netw->send_data( netw, nboct, sizeof( int32_t ) );
                if( net_status < 0 )
                    DONE = 4 ;
                n_log( LOG_DEBUG, "%d Quit sent!", netw -> link . sock );
            }
            else
            {
                pthread_mutex_lock( &netw -> sendbolt );
                ptr = list_shift( netw -> send_buf, N_STR ) ;
                pthread_mutex_unlock( &netw -> sendbolt );
                if( ptr && ptr -> length > 0 && ptr -> data )
                {
                    n_log( LOG_DEBUG, "Sending ptr size %d written %d...", ptr -> length, ptr -> written );

                    /* sending state */
                    nboctet = htonl( state );
                    memcpy( nboct, &nboctet, sizeof( int32_t ) );
                    /* sending state */
                    if ( !DONE )
                    {
                        net_status = netw->send_data( netw, nboct, sizeof( int32_t ) );
                        if( net_status < 0 )
                            DONE = 1 ;
                    }
                    if( !DONE )
                    {
                        /* sending number of octet */
                        nboctet = htonl( ptr -> written );
                        memcpy( nboct, &nboctet, sizeof( int32_t ) );
                        /* sending the number of octet to receive on next message */
                        net_status = netw->send_data( netw, nboct, sizeof( int32_t ) );
                        if( net_status < 0 )
                            DONE = 2 ;
                    }
                    /* sending the data itself */
                    if ( !DONE )
                    {
                        net_status = netw->send_data( netw, ptr -> data, ptr -> written );
                        if( net_status < 0 )
                            DONE = 3 ;
                    }
                    free_nstr( &ptr );
                    u_sleep(  netw -> send_queue_consecutive_wait );
                    message_sent = 1 ;
                }
            }
        }
    } while( !DONE );

    if( DONE == 1 )
        n_log( LOG_ERR,  "Error when sending state %d on socket %d (%s), network: %s", netw -> state, netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 2 )
        n_log( LOG_ERR,  "Error when sending number of octet to socket %d (%s), network: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 3 )
        n_log( LOG_DEBUG,  "Error when sending data on socket %d (%s), network: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 4 )
        n_log( LOG_ERR,  "Error when sending state QUIT on socket %d (%s), network: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 5 )
        n_log( LOG_ERR,  "Error when sending state QUIT number of octet (0) on socket %d (%s), network: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );

    if( DONE == 100 )
    {
        n_log( LOG_DEBUG, "Socket %d: Sending thread exiting correctly", netw -> link . sock );
        netw_set( netw, NETW_EXIT_ASKED );
    }
    else
    {
        n_log( LOG_ERR, "Socket %d (%s): Sending thread exiting with error %d !", netw -> link . sock, _str( netw -> link . ip ),DONE );
        netw_set( netw, NETW_ERROR );
    }

    pthread_mutex_lock( &netw -> eventbolt );
    netw -> nb_running_threads -- ;
    pthread_mutex_unlock( &netw -> eventbolt );

    pthread_exit( 0 );

    /* suppress compiler warning */
#if !defined( __linux__ )
    return NULL ;
#endif
} /* netw_send_func(...) */



/*!\fn void *netw_recv_func( void *NET )
 *\brief To Thread Receiving function
 *\param NET the NETWORK connection to use
 *\return NULL ;
 */
void *netw_recv_func( void *NET )
{
    int DONE = 0,
        state = 0,
        tmpstate = 0,
        net_status = 0 ;

    int32_t nboctet = 0 ;

    char nboct[ 5 ]= "" ;

    N_STR *recvdmsg = NULL ;

    NETWORK *netw = (NETWORK *) NET ;

    __n_assert( netw, return NULL );

    do
    {
        netw_get_state( netw, &state, NULL );
        if( state&NETW_EXIT_ASKED || state&NETW_EXITED )
        {
            DONE = 100;
        }
        if( state&NETW_ERROR )
        {
            DONE = 666 ;
        }
        if( !DONE )
        {
            /* receiving state */
            net_status = netw->recv_data( netw, nboct, sizeof( int32_t ) );
            if( net_status < 0 )
            {
                DONE = 1 ;
            }
            else
            {
                memcpy( &nboctet, nboct, sizeof( int32_t ) );
                tmpstate = ntohl( nboctet );
                nboctet = tmpstate ;
                if( tmpstate==NETW_EXIT_ASKED )
                {
                    n_log( LOG_DEBUG,  "%d receiving order to QUIT, nboctet %d NETW_EXIT_ASKED %d !", netw -> link . sock, nboctet, NETW_EXIT_ASKED );
                    DONE = 100 ;
                }
                else
                {
                    /* receiving nboctet */
                    net_status = netw->recv_data( netw, nboct, sizeof( int32_t ) );
                    if( net_status < 0 )
                    {
                        DONE = 2 ;
                    }
                    else
                    {
                        memcpy( &nboctet, nboct, sizeof( int32_t ) );
                        tmpstate = ntohl( nboctet );
                        nboctet = tmpstate ;

                        Malloc( recvdmsg, N_STR, 1 );
                        if( !recvdmsg )
                        {
                            DONE = 3;
                        }
                        else
                        {
                            n_log( LOG_DEBUG,  "%d octets to receive...", nboctet );
                            Malloc( recvdmsg -> data, char, nboctet + 1 );
                            if( !recvdmsg -> data )
                            {
                                DONE = 4 ;
                            }
                            else
                            {
                                recvdmsg -> length  = nboctet + 1 ;
                                recvdmsg -> written = nboctet ;

                                /* receiving the data itself */
                                net_status = netw->recv_data( netw, recvdmsg -> data, nboctet );
                                if( net_status < 0 )
                                {
                                    DONE = 5 ;
                                }
                                else
                                {
                                    pthread_mutex_lock( &netw -> recvbolt );
                                    if( list_push( netw -> recv_buf, recvdmsg, free_nstr_ptr ) == FALSE )
                                        DONE = 6 ;
                                    pthread_mutex_unlock( &netw -> recvbolt );
                                    n_log( LOG_DEBUG,  "%d octets received !", nboctet );
                                } /* recv data */
                            } /* recv data allocation */
                        } /* recv struct allocation */
                    } /* recv nb octet*/
                } /* exit asked */
            } /* recv state */
        } /* if( !done) */
    } while( !DONE );

    if( DONE == 1 )
        n_log( LOG_ERR,  "Error when receiving state from socket %d (%s), net_status: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 2 )
        n_log( LOG_ERR,  "Error when receiving nboctet from socket %d (%s), net_status: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 3 )
        n_log( LOG_ERR,  "Error when receiving data from socket %d (%s), net_status: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 4 )
        n_log( LOG_ERR,  "Error allocating received message struct from socket %d (%s), net_status: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 5 )
        n_log( LOG_ERR,  "Error allocating received messages data array from socket %d (%s), net_status: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );
    else if( DONE == 6 )
        n_log( LOG_ERR,  "Error adding receved message from socket %d (%s), net_status: %s", netw -> link . sock, _str( netw -> link . ip ), (net_status==NETW_SOCKET_DISCONNECTED)?"disconnected":"socket error" );


    if( DONE == 100 )
    {
        n_log( LOG_DEBUG, "Socket %d (%s): Receive thread exiting correctly", netw -> link . sock, _str( netw -> link . ip ) );
        netw_set( netw, NETW_EXIT_ASKED );
    }
    else
    {
        n_log( LOG_ERR, "Socket %d (%s): Receive thread exiting with code %d !", netw -> link . sock, _str( netw -> link . ip ), DONE );
        netw_set( netw, NETW_ERROR );
    }

    pthread_mutex_lock( &netw -> eventbolt );
    netw -> nb_running_threads -- ;
    pthread_mutex_unlock( &netw -> eventbolt );

    pthread_exit( 0 );

    /* suppress compiler warning */
#if !defined( __linux__ )
    return NULL ;
#endif
} /* netw_recv_func(...) */



/*!\fn netw_stop_thr_engine( NETWORK *netw )
 *\brief Stop a NETWORK connection sending and receing thread
 *\param netw The aimed NETWORK conection to stop
 *\return TRUE on success FALSE on failure
 */
int netw_stop_thr_engine( NETWORK *netw )
{
    __n_assert( netw,  return FALSE );
    int state = 0, thr_engine_status = 0 ;

    n_log( LOG_DEBUG,  "Network %d stop threads event", netw -> link . sock );

    netw_get_state( netw, &state, &thr_engine_status );

    if( thr_engine_status != NETW_THR_ENGINE_STARTED )
    {
        n_log( LOG_ERR, "Thread engine status already stopped for network %p", netw );
        return FALSE ;
    }

    netw_set( netw, NETW_EXIT_ASKED );

    n_log( LOG_DEBUG,  "Network %d waits for send threads to stop", netw -> link . sock );
    for( int it = 0 ; it < 10 ; it ++ )
    {
        sem_post( &netw -> send_blocker );
        usleep( 1000 );
    }
    pthread_join( netw -> send_thr, NULL );
    n_log( LOG_DEBUG,  "Network %d waits for recv threads to stop", netw -> link . sock );
    pthread_join( netw -> recv_thr, NULL );

    netw_set( netw, NETW_EXITED|NETW_THR_ENGINE_STOPPED );

    n_log( LOG_DEBUG,  "Network %d threads Stopped !", netw -> link . sock );

    return TRUE;
} /* Stop_Network( ... ) */



/*!\fn int send_data( void *netw, char *buf, NSTRBYTE n )
 *\brief send data onto the socket
 *\param netw connected NETWORK
 *\param buf pointer to buffer
 *\param n number of characters we want to send
 *\return NETW_SOCKET_ERROR on error, NETW_SOCKET_DISCONNECTED on disconnection, 'n' value on success
 */
int send_data( void *netw, char *buf, NSTRBYTE n )
{
    __n_assert( netw, return NETW_SOCKET_ERROR );
    __n_assert( buf, return NETW_SOCKET_ERROR );

    SOCKET s = ((NETWORK *)netw) -> link . sock ;
    NSTRBYTE bcount = 0 ; /* counts bytes sent */
    int error = 0 ;
    char *errmsg = NULL ;

    if( n == 0 )
    {
        n_log( LOG_ERR, "Send of 0 is unsupported." );
        return NETW_SOCKET_ERROR ;
    }

    char *tmp_buf = buf ;
    while( bcount < n )                 /* loop until full buffer */
    {
        int br = 0 ;     /* bytes sent this pass */
        int retry_count = CALL_RETRY( br, send( s, tmp_buf, n - bcount, NETFLAGS ), NETW_MAX_RETRIES, 10 );
        if( retry_count == NETW_MAX_RETRIES )
        {
            n_log(LOG_ERR, "Maximum retries (%d) reached for socket %d", NETW_MAX_RETRIES , s);
            return NETW_SOCKET_ERROR ;
        }
        //br = send( s, tmp_buf, n - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                /* increment byte counter */
            tmp_buf += br;               /* move buffer ptr for next read */
            n_log( LOG_DEBUG , "socket %d sent %d/%d , retry %d/%d" , s , bcount , n , retry_count , NETW_MAX_RETRIES );
        }
        else if( error == ECONNRESET || error == ENOTCONN )
        {
            n_log( LOG_DEBUG, "socket %d disconnected !", s );
            return NETW_SOCKET_DISCONNECTED ;
        }
        else
        {
            /* signal an error to the caller */
            errmsg = netstrerror( error );
            n_log( LOG_ERR,  "Socket %d send Error: %d , %s", s, br, _str( errmsg ) );
            FreeNoLog( errmsg );
            return NETW_SOCKET_ERROR ;
        }
    }
    return (int)bcount;
} /*send_data(...)*/



/*!\fn int recv_data( void *netw, char *buf, NSTRBYTE n )
 * \brief recv data from the socket
 * \param netw connected NETWORK
 * \param buf pointer to buffer
 * \param n number of characters we want
 * \return NETW_SOCKET_ERROR on error, NETW_RECV_DISCONNECTED on disconnection, 'n' value on success
 */
int recv_data( void *netw, char *buf, NSTRBYTE n )
{
    __n_assert( netw, return NETW_SOCKET_ERROR );
    __n_assert( buf, return NETW_SOCKET_ERROR );

    SOCKET s = ((NETWORK *)netw) -> link . sock ;
    NSTRBYTE bcount = 0 ; /* counts bytes read */
    int error = 0 ;
    char *errmsg = NULL ;

    if( n == 0 )
    {
        n_log( LOG_ERR, "Recv of 0 is unsupported." );
        return NETW_SOCKET_ERROR ;
    }

    char *tmp_buf = buf ;
    while( bcount < n )
    {
        int br = 0 ;     /* bytes read this pass */
        /* loop until full buffer */
        int retry_count= CALL_RETRY( br, recv( s, tmp_buf, n - bcount, NETFLAGS ), NETW_MAX_RETRIES , 10 );
        if( retry_count == NETW_MAX_RETRIES )
        {
            n_log(LOG_ERR, "Maximum retries (%d) reached for socket %d", NETW_MAX_RETRIES , s);
            return NETW_SOCKET_ERROR ;
        }
        //br = recv( s, tmp_buf, n - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                    /* increment byte counter */
            tmp_buf += br;                   /* move buffer ptr for next read */
            n_log( LOG_DEBUG , "socket %d received %d/%d , retry %d/%d" , s , bcount , n , retry_count , NETW_MAX_RETRIES );
        }
        else if( br == 0 )
        {
            n_log( LOG_DEBUG, "socket %d disconnected !", s );
            return NETW_SOCKET_DISCONNECTED ;
        }
        else
        {
            /* signal an error to the caller */
            errmsg = netstrerror( error );
            n_log( LOG_ERR,  "socket %d recv returned %d, error: %s", s, br, _str( errmsg ) );
            FreeNoLog( errmsg );
            return NETW_SOCKET_ERROR ;
        }
    }
    return (int)bcount;
} /*recv_data(...)*/


#ifdef HAVE_OPENSSL
/*!\fn int send_ssl_data( void *netw, char *buf, NSTRBYTE n )
 *\brief send data onto the socket
 *\param netw connected NETWORK in SSL mode
 *\param buf pointer to buffer
 *\param n number of characters we want to send
 *\return -1 on error, NETW_SOCKET_DISCONNECTED on disconnection, n on success
 */
int send_ssl_data( void *netw, char *buf, NSTRBYTE n )
{
    __n_assert( netw, return -1 );
    __n_assert( buf, return -1 );

    SSL *ssl = ((NETWORK *)netw) -> ssl ;
    __n_assert( ssl , return -1 );

    SOCKET s = ((NETWORK *)netw) -> link . sock ;

    int bcount = 0 ; // counts bytes sent 
    int error = 0 ;
    char *errmsg = NULL ;

    if( n == 0 )
    {
        n_log( LOG_ERR, "Send of 0 is unsupported." );
        return -1 ;
    }

    while( (NSTRBYTE)bcount < n )                 // loop until full buffer 
    {
        int br = 0 ;     // bytes sent this pass 
        br = SSL_write( ssl, buf, n - bcount );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                // increment byte counter 
            buf += br;                   // move buffer ptr for next read 
        }
        else
        {
            int ssl_error = SSL_get_error(ssl, br);
            if( ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE )
            {
                usleep( 0 );
                continue;
            }
            errmsg = netstrerror( error );
            switch (ssl_error) {
                case SSL_ERROR_SYSCALL:
                    // Handle syscall failure
                    if( br == 0 )
                    {
                        n_log( LOG_ERR,  "socket %d SSL_read syscall error: connection closed by peer", s );
                    }
                    break ;
                case SSL_ERROR_SSL:
                    // Handle SSL protocol failure
                    n_log( LOG_ERR,  "socket %d SSL_read returned %d, error: %s", s, br, ERR_reason_error_string(ERR_get_error()) );
                    break ;
                default:
                    // Other errors
                    n_log( LOG_ERR,  "socket %d SSL_read returned %d, errno: %s", s, br, _str( errmsg ) );
                    break ;
            }
            FreeNoLog( errmsg );
            return -1 ;
        }
    } 
    return bcount;
} /*send_ssl_data(...)*/



/*!\fn int recv_ssl_data( void *netw, char *buf, NSTRBYTE n )
 * \brief recv data from the socket
 * \param netw connected NETWORK
 * \param buf pointer to buffer
 * \param n number of characters we want
 * \return -1 on error, NETW_SOCKET_DISCONNECTED on disconnection n on success
 */
int recv_ssl_data( void *netw, char *buf, NSTRBYTE n )
{
    __n_assert( netw , return -1 );
    __n_assert( buf, return -1 );

    SSL *ssl = ((NETWORK *)netw) -> ssl ;
    __n_assert( ssl , return -1 );

    SOCKET s = ((NETWORK *)netw) -> link . sock ;
    int bcount = 0 ; // counts bytes read 
    int error = 0 ;
    char *errmsg = NULL ;

    if( n == 0 )
    {
        n_log( LOG_ERR, "Recv of 0 is unsupported." );
        return -1 ;
    }

    while( (NSTRBYTE)bcount < n )
    {
        int br = 0 ;     // bytes read this pass 
        // loop until full buffer 
        br = SSL_read( ssl, buf, n - bcount );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                    // increment byte counter 
            buf += br;                       // move buffer ptr for next read 
        }
        else
        {
            int ssl_error = SSL_get_error(ssl, br);
            if( ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE )
            {
                usleep( 0 );
                continue;
            }
            errmsg = netstrerror( error );
            switch (ssl_error) {
                case SSL_ERROR_SYSCALL:
                    // Handle syscall failure
                    if( br == 0 )
                    {
                        n_log( LOG_ERR,  "socket %d SSL_read syscall error: connection closed by peer", s );
                    }
                    break ;
                case SSL_ERROR_SSL:
                    // Handle SSL protocol failure
                    n_log( LOG_ERR,  "socket %d SSL_read returned %d, error: %s", s, br, ERR_reason_error_string(ERR_get_error()) );
                    break ;
                default:
                    // Other errors
                    n_log( LOG_ERR,  "socket %d SSL_read returned %d, errno: %s", s, br, _str( errmsg ) );
                    break ;
            }
            FreeNoLog( errmsg );
            return -1 ;
        }
    }
    return bcount; 
} /*recv_ssl_data(...)*/
#endif



/*!\fn int send_php( SOCKET s, int _code , char *buf, int n )
 *\brief send data onto the socket
 *\param s connected socket
 *\param buf pointer to buffer
 *\param n number of characters we want to send
 *\param _code Code for php decoding rule
 *\return -1 on error, n on success
 */

int send_php( SOCKET s, int _code, char *buf, int n )
{
    int bcount = 0 ;   /* counts bytes read */
    int br = 0 ;       /* bytes read this pass */

    int error = 0 ;
    char *errmsg = NULL ;

    char *ptr = NULL ; /* temp char ptr ;-) */
    char tmpstr[ HEAD_SIZE +1 ] = "";
    char head[ HEAD_SIZE +1 ] = "" ;
    char code[ HEAD_CODE +1 ] = "" ;

    snprintf( tmpstr, HEAD_SIZE + 1, "%%0%dd", HEAD_SIZE );
    snprintf( head, HEAD_SIZE + 1, tmpstr, n );
    snprintf( tmpstr, HEAD_SIZE + 1, "%%0%dd", HEAD_CODE );
    snprintf( code, HEAD_CODE + 1, tmpstr, _code );

    /* sending head */
    bcount = br = 0 ;
    ptr = head ;
    while ( bcount < HEAD_SIZE )                 /* loop until full buffer */
    {
        br = send( s, head, HEAD_SIZE - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                /* increment byte counter */
            ptr += br;                   /* move buffer ptr for next read */
        }
        else
        {
            /* signal an error to the caller */
            errmsg = netstrerror( error );
            n_log( LOG_ERR,  "Socket %d sending Error %d when sending head size, neterrno: %s", s, br, _str( errmsg ) );
            FreeNoLog( errmsg );
            return FALSE;
        }
    }

    /* sending code */
    bcount = br = 0 ;
    ptr = code ;
    while ( bcount < HEAD_CODE )                 /* loop until full buffer */
    {
        br = send( s, code, HEAD_CODE - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                /* increment byte counter */
            ptr += br;                   /* move buffer ptr for next read */
        }
        else
        {
            errmsg = netstrerror( error );
            n_log( LOG_ERR,  "Socket %d sending Error %d when sending head code, neterrno: %s", s, br, _str( errmsg ) );
            FreeNoLog( errmsg );
            return FALSE;
        }
    }

    /* sending buf */
    bcount = 0;
    br = 0;
    while ( bcount < n )                 /* loop until full buffer */
    {
        br = send( s, buf, n - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                /* increment byte counter */
            buf += br;                   /* move buffer ptr for next read */
        }
        else
        {
            /* signal an error to the caller */
            errmsg = netstrerror( error );
            n_log( LOG_ERR, "Socket %d sending Error %d when sending message of size %d, neterrno: %S", s, br, n, _str( errmsg ) );
            FreeNoLog( errmsg );
            return FALSE;
        }
    }

    return bcount;

} /*send_php(...)*/



/*!\fn int recv_php( SOCKET s, int *_code , char **buf )
 *
 *\brief recv data from the socket
 *
 *\param s connected socket
 *\param _code pointer to store the code
 *\param buf pointer to buffer
 *
 *\return -1 on error, size on success
 */
int recv_php( SOCKET s, int *_code, char **buf )
{
    int bcount = 0 ; /* counts bytes read */
    int br = 0 ;     /* bytes read this pass */
    long int tmpnb = 0, size = 0 ;    /* size of message to receive */
    char *ptr = NULL ;

    int error = 0 ;
    char *errmsg = NULL ;

    char head[ HEAD_SIZE + 1 ] = "" ;
    char code[ HEAD_CODE + 1]  = "" ;

    /* Receiving total message size */
    bcount = br = 0 ;
    ptr = head;
    while ( bcount < HEAD_SIZE )
    {
        /* loop until full buffer */
        br = recv( s, ptr, HEAD_SIZE - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                    /* increment byte counter */
            ptr += br;                       /* move buffer ptr for next read */
        }
        else
        {
            if( br == 0 && bcount == ( HEAD_SIZE - bcount ) )
                break ;
            /* signal an error to the caller */
            errmsg = netstrerror( error );
            n_log( LOG_ERR, "Socket %d receive %d Error %s", s, br, _str( errmsg ) );
            FreeNoLog( errmsg );
            return FALSE;
        }
    }

    tmpnb = strtol( head, NULL, 10 );
    if( tmpnb == LONG_MIN || tmpnb == LONG_MAX )
    {
        n_log( LOG_ERR, "Size received ( %ld ) can not be determined on socket %d", tmpnb, s );
        return FALSE ;
    }

    size = tmpnb ;

    /* receiving request code */
    bcount = br = 0 ;
    ptr = code;
    while ( bcount < HEAD_CODE )
    {
        /* loop until full buffer */
        br = recv( s, ptr, HEAD_CODE - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                    /* increment byte counter */
            ptr += br;                       /* move buffer ptr for next read */
        }
        else
        {
            if( br == 0 && bcount == ( HEAD_CODE - bcount ) )
                break ;
            /* signal an error to the caller */
            errmsg = netstrerror( error );
            n_log( LOG_ERR, "Socket %d receive %d Error , neterrno: %s", s, br, _str( errmsg ) );
            FreeNoLog( errmsg );
            return FALSE;
        }
    }

    tmpnb = strtol( code, NULL, 10 );
    if( tmpnb == LONG_MIN || tmpnb == LONG_MAX )
    {
        n_log( LOG_ERR, "Code received ( %ld ) can not be determined on socket %d\n", tmpnb, s );
        return FALSE ;
    }

    (*_code) = tmpnb ;

    /* receving message */
    if( (*buf ) )
    {
        Free( (*buf ) );
    }
    Malloc( (*buf), char, (size+1) );
    if( !(*buf ) )
    {
        n_log( LOG_ERR, "Could not allocate PHP receive buf" );
        return FALSE ;
    }

    bcount = 0;
    br = 0;
    ptr = (*buf);
    while ( bcount < size )
    {
        /* loop until full buffer */
        br = recv( s, ptr, size - bcount, NETFLAGS );
        error = neterrno ;
        if( br > 0 )
        {
            bcount += br;                    /* increment byte counter */
            ptr += br;                       /* move buffer ptr for next read */
        }
        else
        {
            if( br == 0 && bcount == ( size - bcount ) )
                break ;
            /* signal an error to the caller */
            errmsg = netstrerror( error );
            n_log( LOG_ERR, "Socket %d receive %d Error neterrno: %s", s, br, _str( errmsg ) );
            FreeNoLog( errmsg );
            return FALSE;
        }
    }
    return size ;
} /*recv_php(...)*/



/*!\fn int netw_get_queue_status( NETWORK *netw, int *nb_to_send, int *nb_to_read )
 *
 *\brief retrieve network send queue status
 *\param netw NETWORK object
 *\param nb_to_send Number of messages still in send buffer (not yet submitted to kernel)
 *\param nb_to_read Number of message already read by the kernel, waiting in the local message list
 *\return TRUE or FALSE
 */
int netw_get_queue_status( NETWORK *netw, int *nb_to_send, int *nb_to_read )
{
    __n_assert( netw, return FALSE );

    pthread_mutex_lock( &netw -> sendbolt );
    (*nb_to_send) = netw -> send_buf -> nb_items ;
    pthread_mutex_unlock( &netw -> sendbolt );

    pthread_mutex_lock( &netw -> recvbolt );
    (*nb_to_read) = netw -> recv_buf -> nb_items ;
    pthread_mutex_unlock( &netw -> recvbolt );

    return TRUE ;
} /* get queue states */



/*!\fn NETWORK_POOL *netw_new_pool( int nb_min_element )
 *\brief return a new network pool of nb_min_element
 *\param nb_min_element size of internal hash table for network pool
 *\return a new NETWORK_POOL or NULL
 */
NETWORK_POOL *netw_new_pool( int nb_min_element )
{
    NETWORK_POOL *netw_pool = NULL ;

    Malloc( netw_pool, NETWORK_POOL, 1 );
    __n_assert( netw_pool, return NULL );

    netw_pool -> pool = new_ht( nb_min_element );
    __n_assert( netw_pool -> pool, Free( netw_pool ) ; return NULL );

    init_lock( netw_pool -> rwlock );

    return netw_pool ;
} /* netw_new_pool() */



/*!\fn int netw_destroy_pool( NETWORK_POOL **netw_pool )
 *\brief free a NETWORK_POOL *pool
 *\param netw_pool the address of a NETWORK_POOL *pointer to free
 *\return TRUE or FALSE
 */
int netw_destroy_pool(  NETWORK_POOL **netw_pool )
{
    __n_assert( netw_pool&&(*netw_pool), return FALSE );

    write_lock( (*netw_pool) -> rwlock );
    if( (*netw_pool) -> pool )
        destroy_ht( &(*netw_pool) -> pool );
    unlock( (*netw_pool) -> rwlock );

    rw_lock_destroy( (*netw_pool) -> rwlock );

    Free( (*netw_pool) );

    return TRUE ;
} /* netw_destroy_pool() */



/*!\fn void netw_pool_netw_close( void *netw_ptr )
 *\brief close a network from a network pool
 *\param netw_ptr NETWORK *network pointer
 */
void netw_pool_netw_close( void *netw_ptr )
{
    NETWORK *netw = (NETWORK *)netw_ptr ;
    __n_assert( netw, return );
    n_log( LOG_DEBUG, "Network pool %p: network id %d still active !!", netw, netw -> link . sock );
    return ;
} /* netw_pool_netw_close() */



/*!\fn int netw_pool_add( NETWORK_POOL *netw_pool, NETWORK *netw )
 *\brief add a NETWORK *netw to a NETWORK_POOL *pool
 *\param netw_pool targeted network pool
 *\param netw network to add
 *\return TRUE or FALSE
 */
int netw_pool_add( NETWORK_POOL *netw_pool, NETWORK *netw )
{
    __n_assert( netw_pool, return FALSE );
    __n_assert( netw, return FALSE );

    n_log( LOG_DEBUG, "Trying to add %lld to %p", (unsigned long long)netw -> link . sock, netw_pool -> pool );

    /* write lock the pool */
    write_lock( netw_pool -> rwlock );
    /* test if not already added */
    N_STR *key = NULL ;
    nstrprintf( key, "%lld", (unsigned long long)netw -> link . sock );
    HASH_NODE *node = NULL ;
    if( ht_get_ptr( netw_pool -> pool, _nstr( key ), (void *)&node ) == TRUE )
    {
        n_log( LOG_ERR, "Network id %d already added !", netw -> link . sock );
        free_nstr( &key );
        unlock( netw_pool -> rwlock );
        return FALSE ;
    }
    int retval = FALSE ;
    /* add it */
    if( ( retval = ht_put_ptr( netw_pool -> pool, _nstr( key ), netw, &netw_pool_netw_close ) ) == TRUE )
    {
        if( ( retval = list_push( netw -> pools, netw_pool, NULL ) ) == TRUE )
        {
            n_log( LOG_DEBUG, "added netw %d to pool %p", netw -> link . sock, netw_pool );
        }
        else
        {
            n_log( LOG_ERR, "could not add netw %d to pool %p", netw -> link . sock, netw_pool );
        }
    }
    else
    {
        n_log( LOG_ERR, "could not add netw %d to pool %p", netw -> link . sock, netw_pool );
    }
    free_nstr( &key );

    /* unlock the pool */
    unlock( netw_pool -> rwlock );

    return retval ;
} /* netw_pool_add() */



/*!\fn int netw_pool_remove( NETWORK_POOL *netw_pool, NETWORK *netw )
 *\brief remove a NETWORK *netw to a NETWORK_POOL *pool
 *\param netw_pool targeted network pool
 *\param netw network to remove
 *\return TRUE or FALSE
 */
int netw_pool_remove( NETWORK_POOL *netw_pool, NETWORK *netw )
{
    __n_assert( netw_pool, return FALSE );
    __n_assert( netw, return FALSE );

    /* write lock the pool */
    write_lock( netw_pool -> rwlock );
    /* test if present */
    N_STR *key = NULL ;
    nstrprintf( key, "%lld", (unsigned long long int)netw -> link . sock );
    if( ht_remove( netw_pool -> pool, _nstr( key ) ) == TRUE )
    {
        LIST_NODE *node = list_search( netw -> pools, netw );
        if( node )
        {
            if( !remove_list_node( netw -> pools, node, NETWORK_POOL ) )
            {
                n_log( LOG_ERR, "Network id %d could not be removed !", netw -> link . sock );
            }
        }
        unlock( netw_pool -> rwlock );
        n_log( LOG_DEBUG, "Network id %d removed !", netw -> link . sock );

        return TRUE ;
    }
    free_nstr( &key );
    n_log( LOG_ERR, "Network id %d already removed !", netw -> link . sock );
    /* unlock the pool */
    unlock( netw_pool -> rwlock );
    return FALSE ;
} /* netw_pool_remove */



/*!\fn int netw_pool_broadcast( NETWORK_POOL *netw_pool, NETWORK *from, N_STR *net_msg )
 *\brief add net_msg to all network in netork pool
 *\param netw_pool targeted network pool
 *\param from source network
 *\param net_msg mesage to broadcast
 *\return TRUE or FALSE
 */
int netw_pool_broadcast( NETWORK_POOL *netw_pool, NETWORK *from, N_STR *net_msg )
{
    __n_assert( netw_pool, return FALSE );
    __n_assert( net_msg, return FALSE );

    /* write lock the pool */
    read_lock( netw_pool -> rwlock );
    ht_foreach( node, netw_pool -> pool )
    {
        NETWORK *netw = hash_val( node, NETWORK );
        if( from )
        {
            if( netw -> link . sock != from -> link . sock )
                netw_add_msg( netw, nstrdup( net_msg ) );
        }
        else
        {
            netw_add_msg( netw, nstrdup( net_msg ) );
        }
    }
    unlock( netw_pool -> rwlock );
    return TRUE ;
} /* netw_pool_broadcast */



/*!\fn int netw_pool_nbclients( NETWORK_POOL *netw_pool )
 *\brief return the number of networks in netw_pool
 *\param netw_pool targeted network pool
 *\return -1 or the number of networks in netw_pool
 */
int netw_pool_nbclients( NETWORK_POOL *netw_pool )
{
    __n_assert( netw_pool, return -1 );

    int nb = -1 ;
    read_lock( netw_pool -> rwlock );
    nb = netw_pool -> pool -> nb_keys ;
    unlock( netw_pool -> rwlock );

    return nb ;
} /* netw_pool_nbclients() */



/*!\fn int netw_set_user_id( NETWORK *netw, int id )
 *\brief associate an id and a network
 *\param netw targeted network
 *\param id id we want to associated with
 *\return TRUE or FALSE
 */
int netw_set_user_id( NETWORK *netw, int id )
{
    __n_assert( netw, return FALSE );
    netw -> user_id = id ;
    return TRUE ;
} /* netw_set_user_id() */



/*!\fn netw_send_ping( NETWORK *netw , int id_from , int id_to , int time , int type )
 *\brief Add a ping reply to the network
 *\param netw The aimed NETWORK where we want to add something to send
 *\param id_from Identifiant of the sender
 *\param id_to Identifiant of the destination, -1 if the serveur itslef is targetted
 *\param time The time it was when the ping was sended
 *\param type NETW_PING_REQUEST or NETW_PING_REPLY
 *\return TRUE or FALSE
 */
int netw_send_ping( NETWORK *netw, int type, int id_from, int id_to, int time )
{
    N_STR *tmpstr = NULL ;
    __n_assert( netw, return FALSE );

    tmpstr = netmsg_make_ping( type, id_from, id_to, time );
    __n_assert( tmpstr, return FALSE );

    return netw_add_msg( netw, tmpstr );
} /* netw_send_ping( ... ) */



/*!\fn int netw_send_ident( NETWORK *netw , int type , int id , N_STR *name , N_STR *passwd  )
 *\brief Add a formatted NETWMSG_IDENT message to the specified network
 *\param netw The aimed NETWORK where we want to add something to send
 *\param type type of identification ( NETW_IDENT_REQUEST , NETW_IDENT_NEW )
 *\param id The ID of the sending client
 *\param name Username
 *\param passwd Password
 *\return TRUE or FALSE
 */
int netw_send_ident( NETWORK *netw, int type, int id, N_STR *name, N_STR *passwd  )
{
    N_STR *tmpstr = NULL ;

    __n_assert( netw, return FALSE );

    tmpstr = netmsg_make_ident( type, id, name, passwd  );
    __n_assert( tmpstr, return FALSE );

    return netw_add_msg( netw, tmpstr );
} /* netw_send_ident( ... ) */



/*!\fn netw_send_position( NETWORK *netw , int id , double X , double Y , double vx , double vy , double acc_x , double acc_y , int time_stamp )
 *\brief Add a formatted NETWMSG_IDENT message to the specified network
 *\param netw The aimed NETWORK where we want to add something to send
 *\param id The ID of the sending client
 *\param X X position inside a big grid
 *\param Y Y position inside a big grid
 *\param vx X speed
 *\param vy Y speed
 *\param acc_x Y acceleration
 *\param acc_y X acceleration
 *\param time_stamp Current Time when sending (for some delta we would want to compute )
 *\return TRUE or FALSE
 */
int netw_send_position( NETWORK *netw, int id, double X, double Y, double vx, double vy, double acc_x, double acc_y, int time_stamp )
{
    N_STR *tmpstr = NULL ;

    __n_assert( netw, return FALSE );

    tmpstr = netmsg_make_position_msg(  id, X, Y, vx, vy, acc_x, acc_y, time_stamp );

    __n_assert( tmpstr, return FALSE );

    return netw_add_msg( netw, tmpstr );
} /* netw_send_position( ... ) */



/*!\fn int netw_send_string_to( NETWORK *netw, int id_to, N_STR *name, N_STR *chan, N_STR *txt, int color )
 *\brief Add a string to the network, aiming a specific user
 *\param netw The aimed NETWORK where we want to add something to send
 *\param id_to The ID of the targetted client
 *\param name Sender Name
 *\param chan channel to use
 *\param txt Sender text
 *\param color Sender text color
 *\return TRUE or FALSE
 */
int netw_send_string_to( NETWORK *netw, int id_to, N_STR *name, N_STR *chan, N_STR *txt, int color )
{
    N_STR *tmpstr = NULL ;

    __n_assert( netw, return FALSE );

    tmpstr = netmsg_make_string_msg( netw -> user_id, id_to, name, chan, txt, color );
    __n_assert( tmpstr, return FALSE );

    return netw_add_msg( netw, tmpstr );
} /* netw_send_string_to( ... ) */



/*!\fn int netw_send_string_to_all( NETWORK *netw, N_STR *name, N_STR *chan, N_STR *txt, int color )
 *\brief Add a string to the network, aiming all server-side users
 *\param netw The aimed NETWORK where we want to add something to send
 *\param name Name of user
 *\param chan Target Channel, if any. Pass "ALL" to target the default channel
 *\param txt The text to send
 *\param color The color of the text
 *\return TRUE or FALSE;
 */
int netw_send_string_to_all( NETWORK *netw, N_STR *name, N_STR *chan, N_STR *txt, int color )
{
    N_STR *tmpstr = NULL ;

    __n_assert( netw, return FALSE );

    tmpstr = netmsg_make_string_msg( netw -> user_id,  -1, name, chan, txt, color );
    __n_assert( tmpstr, return FALSE );

    return netw_add_msg( netw, tmpstr );
} /* netw_send_string_to_all( ... ) */



/*!\fn int netw_send_quit( NETWORK *netw )
 *\brief Add a formatted NETMSG_QUIT message to the specified network
 *\param netw The aimed NETWORK
 *\return TRUE or FALSE
 */
int netw_send_quit( NETWORK *netw )
{
    __n_assert( netw, return FALSE );

    N_STR *tmpstr = NULL ;

    tmpstr = netmsg_make_quit_msg();
    __n_assert( tmpstr, return FALSE );

    return netw_add_msg( netw, tmpstr );
} /* netw_send_quit( ... ) */


/*!\fn size_t netw_calculate_urlencoded_size(const char* str, size_t len)
 * \brief function to calculate the required size for the URL-encoded string
 * \param str clear string from which we want to know the urlencoded size
 * \param len length of the input string
 * \return the urlencoded size if the string was urlencoded
 */
size_t netw_calculate_urlencoded_size(const char* str, size_t len) {
    size_t encoded_size = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded_size += 1;  // Safe character, no encoding needed
        } else {
            encoded_size += 3;  // Unsafe character, will be encoded as %XX
        }
    }

    return encoded_size;
}

/*!\fn char* netw_urlencode(const char* str, size_t len) 
 * \brief function to perform URL encoding
 * \param str input string
 * \param len input string length
 * \return urlencoded char *string or NULL
 */
char* netw_urlencode(const char* str, size_t len) {
    static const char *hex = "0123456789ABCDEF";
    size_t encoded_size = netw_calculate_urlencoded_size(str, len);
    char* encoded = (char*)malloc(encoded_size + 1);  // Allocate memory for the encoded string (+1 for the null terminator)

    if (!encoded) {
        return NULL; // Return NULL if memory allocation fails
    }

    char* pbuf = encoded;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *pbuf++ = c;  // Copy safe characters directly
        } else {
            *pbuf++ = '%';  // Encode unsafe characters as %XX
            *pbuf++ = hex[c >> 4];
            *pbuf++ = hex[c & 0xF];
        }
    }

    *pbuf = '\0';  // Null-terminate the encoded string
    return encoded;
}


/*!\fn char* netw_extract_http_request_type(const char *request) 
 * \brief function to extract the request method from an http request
 * \param request the raw http request
 * \return a char *copy of the request type, or NULL
 */
char* netw_extract_http_request_type(const char *request) {
    // Find the first space in the request string
    const char *space = strchr(request, ' ');

    if (space == NULL) {
        // No space found, invalid request format
        return NULL;
    }

    // Calculate the length of the request type
    size_t method_length = space - request;

    // Allocate memory for the method string (+1 for the null terminator)
    char *method = (char*) malloc(method_length + 1);

    if (method == NULL) {
        // Memory allocation failed
        return NULL;
    }

    // Copy the request method to the allocated memory
    strncpy(method, request, method_length);
    method[method_length] = '\0'; // Null-terminate the string

    return method;
}



/*!\fn NETWORK_HTTP_INFO netw_extract_http_info(char *request)
 * \brief extract a lot of informations, mostly as pointers, and populate a NETWORK_HTTP_INFO structure
 * \param request the raw http request string
 * \return a NETWORK_HTTP_INFO structure, filled with zeros in case of errors
 */
NETWORK_HTTP_INFO netw_extract_http_info(char *request)
{
    NETWORK_HTTP_INFO info;
    info.content_length = 0;
    info.body = NULL;
    memset(info.content_type, 0, sizeof(info.content_type));

    // Find Request-Type 
    info.type = netw_extract_http_request_type( request );

    // Find Content-Type header (Optional)
    const char *content_type_header = strstr(request, "Content-Type:");
    if (content_type_header) {
        const char *start = content_type_header + strlen("Content-Type: ");
        const char *end = strstr(start, "\r\n");
        if (end) {
            size_t length = end - start;
            strncpy(info.content_type, start, length);
            info.content_type[length] = '\0';
        }
    } else {
        // If no Content-Type header found, set default
        strncpy(info.content_type, "text/plain", sizeof(info.content_type) - 1);
    }

    // Find Content-Length header (Optional)
    const char *content_length_header = strstr(request, "Content-Length:");
    if (content_length_header) {
        const char *start = content_length_header + strlen("Content-Length: ");
        info.content_length = atoi(start);  // Convert to integer
    }

    // Find the start of the body (after \r\n\r\n)
    const char *body_start = strstr(request, "\r\n\r\n");
    if (body_start) {
        body_start += 4;  // Skip the \r\n\r\n

        // If there is a Content-Length, and it's greater than 0, copy the body
        if (info.content_length > 0) {
            info.body = malloc(info.content_length + 1);  // Allocate memory for body
            if (info.body) {
                strncpy(info.body, body_start, info.content_length);
                info.body[info.content_length] = '\0';  // Null-terminate the body
            }
        }
    }
    return info;
}



/*!\fn int netw_info_destroy( NETWORK_HTTP_INFO http_request )
 * \brief destroy a NETWORK_HTTP_INFO loaded informations
 * \param http_request the parsed NETWORK_HTTP_INFO request
 * \return TRUE ;
 */
int netw_info_destroy( NETWORK_HTTP_INFO http_request )
{
    FreeNoLog( http_request . body );
    FreeNoLog( http_request . type );
    return TRUE ;
}


/*!\fn void netw_get_url_from_http_request(const char *request, char *url, size_t size)
 * \brief Helper function to extract the URL from the HTTP request line
 * \param request raw http request to decode
 * \param url pointer to an allocated char *holder
 * \param size the size of the url holder
 */
void netw_get_url_from_http_request(const char *request, char *url, size_t size) {

    __n_assert( request && strlen( request ) > 0 , return );

    // Example request line: "GET /path/to/resource HTTP/1.1"
    const char *start = strstr(request, " ") + 1;  // Skip the HTTP method (e.g., GET)
    const char *end = strstr(start, " ");  // End of the URL path
    size_t len = end - start;

    if (len < size) {
        strncpy(url, start, len);
        url[len] = '\0';  // Null-terminate the URL
    } else {
        strncpy(url, "/", size - 1);  // Default to root if URL is too large
        url[size - 1] = '\0';
    }
}

/*!\fn char *netw_urldecode(const char *str)
 *\brief Function to decode URL-encoded data
 *\param str input string to decode
 *\return url decoded string
 */
char *netw_urldecode(const char *str) {
    char *decoded = malloc(strlen(str) + 1);
    char *p = decoded;
    while (*str) {
        if (*str == '%') {
            int value;
            sscanf(str + 1, "%2x", &value);
            *p++ = (char)value;
            str += 3;
        } else if (*str == '+') {
            *p++ = ' ';
            str++;
        } else {
            *p++ = *str++;
        }
    }
    *p = '\0';
    return decoded;
}

/*!\fn HASH_TABLE *netw_parse_post_data(const char *post_data) 
 *\brief Function to parse POST data
 *\param post_data the post data to parse
 *\return An allocated HASH_TABLE with post data as keys/values, or NULL
 */
HASH_TABLE *netw_parse_post_data(const char *post_data) {
    __n_assert( post_data , return NULL );

    // Create a copy of the post_data string because strtok modifies the string
    char *data = strdup(post_data);
    char *pair = data;
    char *ampersand_pos;

    HASH_TABLE *post_data_table = new_ht( 32 );

    while (pair != NULL) {
        // Find the next key-value pair separated by '&'
        ampersand_pos = strchr(pair, '&');

        // If found, replace it with '\0' to isolate the current pair
        if (ampersand_pos != NULL) {
            *ampersand_pos = '\0';
        }

        // Now split each pair by '=' to get the key and value
        char *equal_pos = strchr(pair, '=');
        if (equal_pos != NULL) {
            *equal_pos = '\0';  // Terminate the key string
            char *key = pair;
            char *value = equal_pos + 1;

            // Decode the value since POST data is URL-encoded
            char *decoded_value = netw_urldecode(value);
            ht_put_string( post_data_table , key , decoded_value );
            // printf("Key: %s, Value: %s\n", key, decoded_value);
            free(decoded_value);
        }
        // Move to the next key-value pair (if any)
        pair = (ampersand_pos != NULL) ? (ampersand_pos + 1) : NULL;
    }
    // Free the duplicated string
    free(data);
    return post_data_table ;
}

/*!\fn const char* netw_guess_http_content_type(const char* url) 
 * \brief function to guess the content type based on URL extension
 * \param url the url from which we want to guess the content type
 * \return a pointer to the detected content type. It will be 'unknown' if no detection worked
 */
const char* netw_guess_http_content_type(const char* url) {
    // Create a copy of the URL to work on (to avoid modifying the original string)
    char url_copy[1024];
    strncpy(url_copy, url, sizeof(url_copy) - 1);
    url_copy[sizeof(url_copy) - 1] = '\0';

    // Find if there is a '?' (indicating GET parameters) and terminate the string there
    char* query_start = strchr(url_copy, '?');
    if (query_start) {
        *query_start = '\0';  // Terminate the string before the query parameters
    }

    // Find the last occurrence of a dot in the URL (file extension)
    const char* ext = strrchr(url_copy, '.');

    // If no extension is found, return "unknown"
    if (!ext) {
        return "unknown";
    }

    // Compare the extension to known content types
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return "text/html";
    } else if (strcmp(ext, ".txt") == 0) {
        return "text/plain";
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(ext, ".png") == 0) {
        return "image/png";
    } else if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(ext, ".css") == 0) {
        return "text/css";
    } else if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    } else if (strcmp(ext, ".json") == 0) {
        return "application/json";
    } else if (strcmp(ext, ".xml") == 0) {
        return "application/xml";
    } else if (strcmp(ext, ".pdf") == 0) {
        return "application/pdf";
    } else if (strcmp(ext, ".zip") == 0) {
        return "application/zip";
    } else if (strcmp(ext, ".mp4") == 0) {
        return "video/mp4";
    } else if (strcmp(ext, ".mp3") == 0) {
        return "audio/mpeg";
    } else if (strcmp(ext, ".wav") == 0) {
        return "audio/wav";
    } else if (strcmp(ext, ".ogg") == 0) {
        return "audio/ogg";
    }

    // Return unknown for other types
    return "unknown";
}


/*!\fn const char *netw_get_http_status_message(int status_code) 
 * \brief helper function to convert status code to a human-readable message
 * \param status_code the code to convert into a human-readable message
 * \return a pointer to the human readable message. 'Unknown' is returned if the status code is not supported
 */
const char *netw_get_http_status_message(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
                  // Add more status codes as needed
        default: return "Unknown";
    }
}

/*!\fn void netw_get_http_date(char *buffer, size_t buffer_size) 
 * \brief helper function to generate the current date in HTTP format
 * \param buffer the target buffer, must be big enough to hold the date
 * \param buffer_size the size of the target buffer
 */
void netw_get_http_date(char *buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now); // Convert to UTC
    strftime(buffer, buffer_size, "%a, %d %b %Y %H:%M:%S GMT", gmt); // Format the date
}


/*!\fn int netw_build_http_response(N_STR **http_response , int status_code, const char *server_name, const char *content_type, char *additional_headers, N_STR *body)
 * \brief function to dynamically generate an HTTP response
 * \param http_response pointer to a N_STR *response. Will be set to the response, or NULL
 * \param status_code response http status code
 * \param server_name response 'Server' in headers
 * \param content_type response 'Content-Type' in headers
 * \param additional_headers additional response headers, can be "" if no additional headers, else 'backslash r backslash n' separated key: values
 * \param body response 'Server' in headers
 * \return TRUE if the http_response was built, FALSE if not
 */
int netw_build_http_response(N_STR **http_response , int status_code, const char *server_name, const char *content_type, char *additional_headers, N_STR *body)
{
    __n_assert( server_name , return FALSE );
    __n_assert( content_type , return FALSE );
    __n_assert( additional_headers , return FALSE );

    const char *status_message = netw_get_http_status_message(status_code);
    const char *connection_type = "close";

    // Buffer for the date header
    char date_buffer[128] = "" ;
    netw_get_http_date(date_buffer, sizeof(date_buffer));

    // Format for the HTTP response without body
    const char *http_format_no_body = 
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Server: %s\r\n"
        "Content-Length: 0\r\n"
        "%s"
        "Connection: %s\r\n\r\n";

    // Format for the HTTP response with body
    const char *http_format_with_body = 
        "HTTP/1.1 %d %s\r\n"
        "Date: %s\r\n"
        "Server: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "%s"
        "Connection: %s\r\n\r\n";

    if( (*http_response) )
    {
        (*http_response) -> written = 0 ;
    }

    if (!body || body -> written == 0) {
        // Handle the case where there is no body
        nstrprintf( (*http_response), http_format_no_body , status_code , status_message , date_buffer , server_name , additional_headers, connection_type );
        n_log( LOG_DEBUG , "empty response" );
    } else {
        // Build the response with body
        nstrprintf( (*http_response), http_format_with_body , status_code, status_message, date_buffer , server_name, content_type, body -> written, additional_headers, connection_type);
        nstrcat( (*http_response) , body );
        n_log( LOG_DEBUG , "body response" );
    }
    return TRUE ;
}
