/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@file n_network.c
 *@brief Network Engine
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/05/2005
 */

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include "nilorea/n_network.h"
#include "nilorea/n_network_msg.h"
#include "nilorea/n_reactor.h"
#include "nilorea/n_log.h"
#include "nilorea/n_hash.h"
#include "nilorea/n_base64.h"
#include "nilorea/n_zlib.h"
#include "nilorea/n_lz4.h"
#include "nilorea/n_time.h"

/* Opportunistic payload compression. Threshold and ratio knobs live
 * in nilorea/n_network.h so the reactor send path shares them. */

/* MinGW does not provide strndup */
#ifdef __windows__
static char* _n_strndup(const char* s, size_t n) {
    size_t len = strlen(s);
    if (n < len) len = n;
    char* p = (char*)malloc(len + 1);
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}
#define strndup _n_strndup
#endif

#ifdef HAVE_OPENSSL
#include <openssl/sha.h>
#include <openssl/rand.h>
#endif

/* error capture infrastructure */

/*! thread-local pre-connection error buffer (DNS, socket creation) */
#ifndef _Thread_local
#define _Thread_local __thread
#endif
static _Thread_local char s_connect_errors[8][512];
static _Thread_local int s_connect_err_count = 0;
static _Thread_local int s_connect_err_next = 0;

/*! capture an error into a NETWORK handle's ring buffer */
static void _netw_capture_error(NETWORK* netw, const char* fmt, ...) {
    if (!netw) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(netw->netw_errors[netw->netw_err_next], 512, fmt, ap);
    va_end(ap);
    netw->netw_err_next = (netw->netw_err_next + 1) % 8;
    if (netw->netw_err_count < 8) netw->netw_err_count++;
}

/*! capture a pre-connection error (thread-local) */
static void _netw_capture_connect_error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_connect_errors[s_connect_err_next], 512, fmt, ap);
    va_end(ap);
    s_connect_err_next = (s_connect_err_next + 1) % 8;
    if (s_connect_err_count < 8) s_connect_err_count++;
}

/* public error accessors */

int n_netw_get_error_count(NETWORK* netw) {
    return netw ? netw->netw_err_count : 0;
}

const char* n_netw_get_error(NETWORK* netw, int index) {
    if (!netw || index < 0 || index >= netw->netw_err_count) return NULL;
    int oldest = (netw->netw_err_next - netw->netw_err_count + 8) % 8;
    return netw->netw_errors[(oldest + index) % 8];
}

void n_netw_clear_errors(NETWORK* netw) {
    if (!netw) return;
    netw->netw_err_count = 0;
    netw->netw_err_next = 0;
}

int n_netw_get_connect_error_count(void) {
    return s_connect_err_count;
}

const char* n_netw_get_connect_error(int index) {
    if (index < 0 || index >= s_connect_err_count) return NULL;
    int oldest = (s_connect_err_next - s_connect_err_count + 8) % 8;
    return s_connect_errors[(oldest + index) % 8];
}

void n_netw_clear_connect_errors(void) {
    s_connect_err_count = 0;
    s_connect_err_next = 0;
}

/*! network error code */
N_ENUM_DEFINE(N_ENUM_netw_code_type, __netw_code_type)

/**
 * @brief host to network size_t
 * @param value the size_t value to convert to network order
 * @return network ordered value
 */
size_t htonst(size_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    if (sizeof(size_t) == 4) {
        return (size_t)htonl((uint32_t)value);
    } else if (sizeof(size_t) == 8) {
        return ((size_t)htonl((uint32_t)(value >> 32)) |
                ((size_t)htonl((uint32_t)value) << 32));
    }
#endif
    return value;  // No conversion needed for big-endian
}

/**
 * @brief network to host size_t
 * @param value the size_t value to convert to host order
 * @return host ordered value
 */
size_t ntohst(size_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    if (sizeof(size_t) == 4) {
        return (size_t)ntohl((uint32_t)value);
    } else if (sizeof(size_t) == 8) {
        return ((size_t)ntohl((uint32_t)(value >> 32)) |
                ((size_t)ntohl((uint32_t)value) << 32));
    }
#endif
    return value;  // No conversion needed for big-endian
}

#ifdef __windows__

/**
 * @brief wchar to char, from https://stackoverflow.com/questions/3019977/convert-wchar-t-to-char/55715607#55715607 by Richard Bamford
 * @param pwchar the wchar_t to convert
 * @return an allocated copy of pwchar in form of a char * or NULL
 */
char* wchar_to_char(const wchar_t* pwchar) {
    __n_assert(pwchar, return NULL);

    // get the number of characters in the string.
    int currentCharIndex = 0;
    char currentChar = (char)pwchar[currentCharIndex];
    char* filePathC = NULL;

    while (currentChar != '\0') {
        currentCharIndex++;
        currentChar = (char)pwchar[currentCharIndex];
    }

    const int charCount = currentCharIndex + 1;

    // allocate a new block of memory size char (1 byte) instead of wide char (2 bytes)
    Malloc(filePathC, char, (size_t)charCount);
    __n_assert(filePathC, return NULL);

    for (int i = 0; i < charCount; i++) {
        // convert to char (1 byte)
        char character = (char)pwchar[i];

        *filePathC = character;

        filePathC += sizeof(char);
    }
    filePathC += '\0';

    filePathC -= (sizeof(char) * (size_t)charCount);

    return filePathC;
}

/*! network-aware retry macro: retries on EINTR and WSAEWOULDBLOCK, uses WSAGetLastError */
#define NETW_CALL_RETRY(__retvar, __expression, __max_tries)                                                                                \
    do {                                                                                                                                    \
        int __nb_retries = 0;                                                                                                               \
        do {                                                                                                                                \
            __retvar = (__expression);                                                                                                      \
            __nb_retries++;                                                                                                                 \
        } while (__retvar == -1 && (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEWOULDBLOCK) && __nb_retries < (__max_tries)); \
        if (__retvar == -1 && __nb_retries >= (__max_tries)) __retvar = -2;                                                                 \
    } while (0)

/*! get last socket error code, windows version */
#define neterrno WSAGetLastError()

/*! @brief Get the last socket error code as a heap-allocated string, Windows version.
 *  @param code error code (typically WSAGetLastError())
 *  @return strdup-style string that the caller must Free, or NULL on failure */
static char* netstrerror(int code) {
    wchar_t* ws = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)code,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPWSTR)&ws, 0, NULL);
    char* netstr = wchar_to_char(ws);
    LocalFree(ws);
    return netstr;
}

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

/**
 *@brief Copy src to string dst of size siz. At most siz-1 characters will be copied. Always NUL terminates (unless siz == 0).
 *@param dst destination buffer
 *@param src source string
 *@param siz size of destination buffer
 *@return strlen(src); if the return value >= siz, truncation occurred
 */
size_t strlcpy(char* dst, const char* src, size_t siz) {
    char* d = dst;
    const char* s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0'; /* NUL-terminate dst */
        while (*s++);
    }

    return (s - src - 1); /* count does not include NUL */
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

static char* inet_ntop4(const unsigned char* src, char* dst, socklen_t size);
static char* inet_ntop6(const unsigned char* src, char* dst, socklen_t size);

/**
 *@brief convert a network format address to presentation format
 *@param af address family (AF_INET or AF_INET6)
 *@param src network address structure
 *@param dst destination buffer for the presentation format string
 *@param size size of destination buffer
 *@return pointer to dst, or NULL on error
 */
char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
    switch (af) {
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
static char* inet_ntop4(const unsigned char* src, char* dst, socklen_t size) {
    static const char fmt[] = "%u.%u.%u.%u";
    char tmp[sizeof "255.255.255.255"];
    int l;

    l = snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]);
    if (l <= 0 || (socklen_t)l >= size) {
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
static char* inet_ntop6(const unsigned char* src, char* dst, socklen_t size) {
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
#define NS_IN6ADDRSZ 16
#define NS_INT16SZ 2
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
    for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
        if (words[i] == 0) {
            if (cur.base == -1)
                cur.base = i, cur.len = 1;
            else
                cur.len++;
        } else {
            if (cur.base != -1) {
                if (best.base == -1 || cur.len > best.len)
                    best = cur;
                cur.base = -1;
            }
        }
    }
    if (cur.base != -1) {
        if (best.base == -1 || cur.len > best.len)
            best = cur;
    }
    if (best.base != -1 && best.len < 2)
        best.base = -1;

    /*
     * Format the result.
     */
    tp = tmp;
    for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
        /* Are we inside the best run of 0x00's? */
        if (best.base != -1 && i >= best.base &&
            i < (best.base + best.len)) {
            if (i == best.base)
                *tp++ = ':';
            continue;
        }
        /* Are we following an initial run of 0x00s or any real hex? */
        if (i != 0)
            *tp++ = ':';
        /* Is this address an encapsulated IPv4? */
        if (i == 6 && best.base == 0 && (best.len == 6 || (best.len == 7 && words[7] != 0x0001) || (best.len == 5 && words[5] == 0xffff))) {
            if (!inet_ntop4(src + 12, tp, sizeof tmp - (tp - tmp)))
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
    if ((socklen_t)(tp - tmp) > size) {
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

static int inet_pton4(const char* src, u_char* dst);
static int inet_pton6(const char* src, u_char* dst);

/**
 *@brief convert from presentation format to network format
 *@param af address family (AF_INET or AF_INET6)
 *@param src presentation format address string
 *@param dst destination for the network address structure
 *@return 1 on success, 0 if address invalid, -1 on error
 */
int inet_pton(int af, const char* src, void* dst) {
    switch (af) {
        case AF_INET:
            return (inet_pton4(src, (unsigned char*)dst));
        case AF_INET6:
            return (inet_pton6(src, (unsigned char*)dst));
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
static int inet_pton4(const char* src, u_char* dst) {
    static const char digits[] = "0123456789";
    int saw_digit, octets, ch;
#define NS_INADDRSZ 4
    /* Zero-initialise: the octets are filled through the `tp` alias
     * below, which cppcheck's data flow can't follow, so it cannot
     * prove tmp is fully written before the trailing memcpy. The
     * init is harmless and removes the false uninitvar diagnostic. */
    u_char tmp[NS_INADDRSZ] = {0}, *tp;

    saw_digit = 0;
    octets = 0;
    tp = tmp;
    while ((ch = *src++) != '\0') {
        const char* pch;

        if ((pch = strchr(digits, ch)) != NULL) {
            u_int uiNew = *tp * 10 + (pch - digits);

            if (saw_digit && *tp == 0)
                return (0);
            if (uiNew > 255)
                return (0);
            *tp = uiNew;
            if (!saw_digit) {
                if (++octets > 4)
                    return (0);
                saw_digit = 1;
            }
        } else if (ch == '.' && saw_digit) {
            if (octets == 4)
                return (0);
            *++tp = 0;
            saw_digit = 0;
        } else
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
static int inet_pton6(const char* src, u_char* dst) {
    static const char xdigits_l[] = "0123456789abcdef",
                      xdigits_u[] = "0123456789ABCDEF";
#define NS_IN6ADDRSZ 16
#define NS_INT16SZ 2
    u_char tmp[NS_IN6ADDRSZ], *tp, *endp, *colonp;
    const char* curtok;
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
    while ((ch = *src++) != '\0') {
        const char* xdigits : const char* pch;

        if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
            pch = strchr((xdigits = xdigits_u), ch);
        if (pch != NULL) {
            val <<= 4;
            val |= (pch - xdigits);
            if (++seen_xdigits > 4)
                return (0);
            continue;
        }
        if (ch == ':') {
            curtok = src;
            if (!seen_xdigits) {
                if (colonp)
                    return (0);
                colonp = tp;
                continue;
            } else if (*src == '\0') {
                return (0);
            }
            if (tp + NS_INT16SZ > endp)
                return (0);
            *tp++ = (u_char)(val >> 8) & 0xff;
            *tp++ = (u_char)val & 0xff;
            seen_xdigits = 0;
            val = 0;
            continue;
        }
        if (ch == '.' && ((tp + NS_INADDRSZ) <= endp) &&
            inet_pton4(curtok, tp) > 0) {
            tp += NS_INADDRSZ;
            seen_xdigits = 0;
            break; /* '\\0' was seen by inet_pton4(). */
        }
        return (0);
    }
    if (seen_xdigits) {
        if (tp + NS_INT16SZ > endp)
            return (0);
        *tp++ = (u_char)(val >> 8) & 0xff;
        *tp++ = (u_char)val & 0xff;
    }
    if (colonp != NULL) {
        /*
         * Since some memmove()'s erroneously fail to handle
         * overlapping regions, we'll do the shift by hand.
         */
        const int n = tp - colonp;
        int i;

        if (tp == endp)
            return (0);
        for (i = 1; i <= n; i++) {
            endp[-i] = colonp[n - i];
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

/*! network-aware retry macro: retries on EINTR and EAGAIN/EWOULDBLOCK */
#define NETW_CALL_RETRY(__retvar, __expression, __max_tries)                                                                     \
    do {                                                                                                                         \
        int __nb_retries = 0;                                                                                                    \
        do {                                                                                                                     \
            __retvar = (__expression);                                                                                           \
            __nb_retries++;                                                                                                      \
        } while (__retvar == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) && __nb_retries < (__max_tries)); \
        if (__retvar == -1 && __nb_retries >= (__max_tries)) __retvar = -2;                                                      \
    } while (0)

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

/*! @brief Get the last socket error code as a heap-allocated string, POSIX version.
 *  @param code error code (typically errno)
 *  @return strdup-style string that the caller must Free, or NULL on allocation failure */
static char* netstrerror(int code) {
    /* strdup returns NULL and sets errno=ENOMEM on allocation failure;
     * propagate that NULL to the caller, who treats it as "no message". */
    return strdup(strerror(code));
}

#endif /* if not def windows */

/**
 *@brief Return an empty allocated network ready to be netw_closed
 *@param send_list_limit Thread engine number of tosend message limit. From UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@param recv_list_limit Thread engine number of received message limit. From UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@return NULL or a new empty network
 */
NETWORK* netw_new(size_t send_list_limit, size_t recv_list_limit) {
    NETWORK* netw = NULL;

    Malloc(netw, NETWORK, 1);
    __n_assert(netw, return NULL);

    /* netw itself */
    netw->nb_pending = -1;
    netw->mode = -1;
    netw->user_id = -1;
    netw->nb_running_threads = 0;
    netw_atomic_write_state(netw, NETW_EXITED);
    netw->threaded_engine_status = NETW_THR_ENGINE_STOPPED;
    /* Default to zlib for back-compat with the first compression
     * pass that shipped before LZ4 support landed. Callers can
     * switch to NONE or LZ4 via netw_set_compression_mode(). */
    netw->compress_mode = NETW_COMPRESS_ZLIB;

    /* netw -> link */
    netw->link.sock = INVALID_SOCKET;
    netw->link.port =
        netw->link.ip = NULL;
    memset(&netw->link.hints, 0, sizeof(struct addrinfo));
    memset(&netw->link.raddr, 0, sizeof(struct sockaddr_storage));

    /*initiliaze mutexs*/
    if (pthread_mutex_init(&netw->sendbolt, NULL) != 0) {
        n_log(LOG_ERR, "Error initializing netw -> sendbolt");
        Free(netw);
        return NULL;
    }
    /*initiliaze mutexs*/
    if (pthread_mutex_init(&netw->recvbolt, NULL) != 0) {
        n_log(LOG_ERR, "Error initializing netw -> recvbolt");
        pthread_mutex_destroy(&netw->sendbolt);
        Free(netw);
        return NULL;
    }
    /*initiliaze mutexs*/
    if (pthread_mutex_init(&netw->eventbolt, NULL) != 0) {
        n_log(LOG_ERR, "Error initializing netw -> eventbolt");
        pthread_mutex_destroy(&netw->sendbolt);
        pthread_mutex_destroy(&netw->recvbolt);
        Free(netw);
        return NULL;
    }
    /* initialize send sem bolt */
    if (sem_init(&netw->send_blocker, 0, 0) != 0) {
        n_log(LOG_ERR, "Error initializing netw -> eventbolt");
        pthread_mutex_destroy(&netw->eventbolt);
        pthread_mutex_destroy(&netw->sendbolt);
        pthread_mutex_destroy(&netw->recvbolt);
        Free(netw);
        return NULL;
    }
    /*initialize queues */
    netw->recv_buf = new_generic_list(recv_list_limit);
    if (!netw->recv_buf) {
        n_log(LOG_ERR, "Error when creating receive list with %d item limit", recv_list_limit);
        netw_close(&netw);
        return NULL;
    }
    netw->send_buf = new_generic_list(send_list_limit);
    if (!netw->send_buf) {
        n_log(LOG_ERR, "Error when creating send list with %d item limit", send_list_limit);
        netw_close(&netw);
        return NULL;
    }
    netw->pools = new_generic_list(MAX_LIST_ITEMS);
    if (!netw->pools) {
        n_log(LOG_ERR, "Error when creating pools list");
        netw_close(&netw);
        return NULL;
    }
    netw->addr_infos_loaded = 0;
    netw->send_queue_consecutive_wait = 0;
    netw->so_reuseaddr = -1;
    // netw -> so_reuseport = -1 ;
    netw->so_keepalive = -1;
    netw->tcpnodelay = -1;
    netw->so_sndbuf = -1;
    netw->so_rcvbuf = -1;
    netw->so_rcvtimeo = -1;
    netw->so_sndtimeo = -1;
    netw->so_linger = -1;
    netw->deplete_socket_timeout = -1;
    netw->deplete_queues_timeout = -1;
    netw->wait_close_timeout = -1;
    netw->transport_type = NETWORK_TCP;
    netw->crypto_algo = NETW_ENCRYPT_NONE;

    netw->send_data = &send_data;
    netw->recv_data = &recv_data;
    netw->send_data_once = &send_data_once;
    netw->recv_data_once = &recv_data_once;

#ifdef HAVE_OPENSSL
    netw->method = NULL;
    netw->ctx = NULL;
    netw->ssl = NULL;
    netw->key = NULL;
    netw->certificate = NULL;
#endif

    netw->link.is_blocking = 1;
    return netw;

} /* netw_new() */

/**
 *@brief get sockaddr, IPv4 or IPv6
 *@param sa addrinfo to get
 *@return socket address
 */
char* get_in_addr(struct sockaddr* sa) {
    return sa->sa_family == AF_INET
               ? (char*)&(((struct sockaddr_in*)sa)->sin_addr)
               : (char*)&(((struct sockaddr_in6*)sa)->sin6_addr);
}

/**
 *@brief Do not directly use, internal api. Initialize winsock dll loading on windows if needed.
 *@param mode 1 for opening 0 for close 2 for status
 *@param v1 First digit of version requested
 *@param v2 Second digit of version requested
 *@return TRUE on success FALSE on error
 */
int netw_init_wsa(int mode, int v1, int v2) {
    int compiler_warning_suppressor = 0;
#if !defined(__linux__) && !defined(__sun) && !defined(_AIX)
    static WSADATA WSAdata;            /*WSA world*/
    static int WSA_IS_INITIALIZED = 0; /*status checking*/

    switch (mode) {
        default:
            /*returning WSA status*/
        case 2:
            return WSA_IS_INITIALIZED;
            break;
            /*loading WSA dll*/
        case 1:
            if (WSA_IS_INITIALIZED == 1)
                return TRUE; /*already loaded*/
            if ((WSAStartup(MAKEWORD(v1, v2), &WSAdata)) != 0) {
                WSA_IS_INITIALIZED = 0;
                return FALSE;
            } else {
                WSA_IS_INITIALIZED = 1;
                return TRUE;
            }
            break;
            /*unloading (closing) WSA */
        case 0:
            if (WSA_IS_INITIALIZED == 0)
                return TRUE; /*already CLEANED or not loaded */
            if (WSACleanup() == 0) {
                WSA_IS_INITIALIZED = 0;
                return TRUE;
            }
            break;
    } /*switch(...)*/
#endif /* ifndef __linux__ __sun _AIX */
    compiler_warning_suppressor = mode + v1 + v2;
    (void)compiler_warning_suppressor;
    compiler_warning_suppressor = TRUE;
    return compiler_warning_suppressor;
} /*netw_init_wsa(...)*/

/**
 *@brief Pick send-side payload compression algorithm.
 *@param netw The network to configure (must be non-NULL).
 *@param mode One of NETW_COMPRESS_NONE / NETW_COMPRESS_ZLIB /
 *            NETW_COMPRESS_LZ4.  Invalid values are rejected.
 *@return TRUE on success, FALSE on a NULL netw or unknown mode.
 *
 * Affects outbound packets only.  The recv path honours whichever
 * NETW_COMPRESSED_* flag the sender set, so two peers running
 * different modes still interoperate.
 */
int netw_set_compression_mode(NETWORK* netw, int mode) {
    __n_assert(netw, return FALSE);
    if (mode != NETW_COMPRESS_NONE &&
        mode != NETW_COMPRESS_ZLIB &&
        mode != NETW_COMPRESS_LZ4) {
        n_log(LOG_ERR, "netw_set_compression_mode: unknown mode %d", mode);
        return FALSE;
    }
    netw->compress_mode = mode;
    return TRUE;
} /* netw_set_compression_mode */

/**
 *@brief Modify blocking socket mode
 *@param netw The network to configure
 *@param is_blocking 0 NON BLOCk , 1 BLOCK
 *@return TRUE or FALSE
 */
int netw_set_blocking(NETWORK* netw, unsigned long int is_blocking) {
    __n_assert(netw, return FALSE);

    int error = 0;
    (void)error;
    char* errmsg = NULL;

#if defined(__linux__) || defined(__sun)
    int flags = 0;
    flags = fcntl(netw->link.sock, F_GETFL, 0);
    if (netw->link.is_blocking != 0 && !is_blocking) {
        if (flags & O_NONBLOCK) {
            n_log(LOG_DEBUG, "socket %d was already in non-blocking mode", netw->link.sock);
            /* in case we missed it, let's update the link mode */
            netw->link.is_blocking = 0;
            return TRUE;
        }
    } else if (netw->link.is_blocking != 1 && is_blocking) {
        if (!(flags & O_NONBLOCK)) {
            n_log(LOG_DEBUG, "socket %d was already in blocking mode", netw->link.sock);
            /* in case we missed it, let's update the link mode */
            netw->link.is_blocking = 1;
            return TRUE;
        }
    }
    if (fcntl(netw->link.sock, F_SETFL, is_blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) == -1) {
        error = neterrno;
        errmsg = netstrerror(error);
        _netw_capture_error(netw, "couldn't set blocking mode %d on %d: %s", is_blocking, netw->link.sock, _str(errmsg));
        n_log(LOG_ERR, "couldn't set blocking mode %d on %d: %s", is_blocking, netw->link.sock, _str(errmsg));
        FreeNoLog(errmsg);
        return FALSE;
    }
#else
    unsigned long int blocking = 1 - is_blocking;
    int res = ioctlsocket(netw->link.sock, (long)FIONBIO, &blocking);
    error = neterrno;
    if (res != NO_ERROR) {
        errmsg = netstrerror(error);
        _netw_capture_error(netw, "ioctlsocket failed with error: %ld , neterrno: %s", res, _str(errmsg));
        n_log(LOG_ERR, "ioctlsocket failed with error: %ld , neterrno: %s", res, _str(errmsg));
        FreeNoLog(errmsg);
        netw_close(&netw);
        return FALSE;
    }
#endif
    netw->link.is_blocking = is_blocking;
    return TRUE;
} /* netw_set_blocking */

/**
 *@brief Modify common socket options on the given netw
 *@param netw The socket to configure
 *@param optname NETWORK_DEPLETE_SOCKET_TIMEOUT,NETWORK_DEPLETE_QUEUES_TIMEOUT,NETWORK_WAIT_CLOSE_TIMEOUT,NETWORK_CONSECUTIVE_SEND_WAIT ,SO_REUSEADDR,SO_REUSEPORT,SO_KEEPALIVE,TCP_NODELAY,SO_SNDBUF,SO_RCVBUF,SO_LINGER,SO_RCVTIMEO,SO_SNDTIMEO,TCP_USER_TIMEOUT. Please refer to man setsockopt for details
 *@param value The value of the socket parameter
 *@warning Beware of TCP_USER_TIMEOUT, it's causing hangs on some systems. It should not be used lightly and tested. It looks like it's messing up with TCP_KEEPALIVE
 *@return TRUE or FALSE
 */
int netw_setsockopt(NETWORK* netw, int optname, int value) {
    __n_assert(netw, return FALSE);

    int error = 0;
    char* errmsg = NULL;

    switch (optname) {
        case TCP_NODELAY:
            if (value >= 0) {
                /* disable naggle algorithm */
                if (setsockopt(netw->link.sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value)) == -1) {
                    error = neterrno;
                    errmsg = netstrerror(error);
                    _netw_capture_error(netw, "Error from setsockopt(TCP_NODELAY) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    n_log(LOG_ERR, "Error from setsockopt(TCP_NODELAY) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    FreeNoLog(errmsg);
                    return FALSE;
                }
            }
            netw->tcpnodelay = value;
            break;
        case SO_SNDBUF:
            /* socket sending buffer size */
            if (value >= 0) {
                if (setsockopt(netw->link.sock, SOL_SOCKET, SO_SNDBUF, (const char*)&value, sizeof(value)) == -1) {
                    error = neterrno;
                    errmsg = netstrerror(error);
                    _netw_capture_error(netw, "Error from setsockopt(SO_SNDBUF) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    n_log(LOG_ERR, "Error from setsockopt(SO_SNDBUF) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    FreeNoLog(errmsg);
                    return FALSE;
                }
            }
            netw->so_sndbuf = value;
            break;
        case SO_RCVBUF:
            /* socket receiving buffer */
            if (value >= 0) {
                if (setsockopt(netw->link.sock, SOL_SOCKET, SO_RCVBUF, (const char*)&value, sizeof(value)) == -1) {
                    error = neterrno;
                    errmsg = netstrerror(error);
                    _netw_capture_error(netw, "Error from setsockopt(SO_RCVBUF) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    n_log(LOG_ERR, "Error from setsockopt(SO_RCVBUF) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    FreeNoLog(errmsg);
                    return FALSE;
                }
            }
            netw->so_rcvbuf = value;
            break;
        case SO_REUSEADDR:
            /* lose the pesky "Address already in use" error message*/
            if (setsockopt(netw->link.sock, SOL_SOCKET, SO_REUSEADDR, (char*)&value, sizeof(value)) == -1) {
                error = neterrno;
                errmsg = netstrerror(error);
                _netw_capture_error(netw, "Error from setsockopt(SO_REUSEADDR) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                n_log(LOG_ERR, "Error from setsockopt(SO_REUSEADDR) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                FreeNoLog(errmsg);
                return FALSE;
            }
            netw->so_reuseaddr = value;
            break;
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
        case SO_LINGER: {
            struct linger ling;
            if (value < 0) {
                ling.l_onoff = 0;
                ling.l_linger = 0;
            } else if (value == 0) {
                ling.l_onoff = 1;
                ling.l_linger = 0;
            } else {
                ling.l_onoff = 1;
#ifdef __windows__
                ling.l_linger = (u_short)value;
#else
                ling.l_linger = value;
#endif
            }
#ifndef __windows__
            if (setsockopt(netw->link.sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling)) == -1) {
                error = neterrno;
                errmsg = netstrerror(error);
                _netw_capture_error(netw, "Error from setsockopt(SO_LINGER) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                n_log(LOG_ERR, "Error from setsockopt(SO_LINGER) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                FreeNoLog(errmsg);
                return FALSE;
            }
#else
            if (setsockopt(netw->link.sock, SOL_SOCKET, SO_LINGER, (const char*)&ling, sizeof(ling)) == -1) {
                error = neterrno;
                errmsg = netstrerror(error);
                _netw_capture_error(netw, "Error from setsockopt(SO_LINGER) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                n_log(LOG_ERR, "Error from setsockopt(SO_LINGER) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                FreeNoLog(errmsg);
                return FALSE;
            }
#endif  // __windows__
            netw->so_linger = value;
        } break;
        case SO_RCVTIMEO:
            if (value >= 0) {
#ifndef __windows__
                {
                    struct timeval tv;
                    tv.tv_sec = value;
                    tv.tv_usec = 0;
                    if (setsockopt(netw->link.sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) == -1) {
                        error = neterrno;
                        errmsg = netstrerror(error);
                        _netw_capture_error(netw, "Error from setsockopt(SO_RCVTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                        n_log(LOG_ERR, "Error from setsockopt(SO_RCVTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                        FreeNoLog(errmsg);
                        return FALSE;
                    }
                }
#else
                if (setsockopt(netw->link.sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&value, sizeof value) == -1) {
                    error = neterrno;
                    errmsg = netstrerror(error);
                    _netw_capture_error(netw, "Error from setsockopt(SO_RCVTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    n_log(LOG_ERR, "Error from setsockopt(SO_RCVTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    FreeNoLog(errmsg);
                    return FALSE;
                }
#endif
            }
            netw->so_rcvtimeo = value;
            break;
        case SO_SNDTIMEO:
            if (value >= 0) {
#ifndef __windows__
                {
                    struct timeval tv;
                    tv.tv_sec = value;
                    tv.tv_usec = 0;

                    if (setsockopt(netw->link.sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv) == -1) {
                        error = neterrno;
                        errmsg = netstrerror(error);
                        _netw_capture_error(netw, "Error from setsockopt(SO_SNDTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                        n_log(LOG_ERR, "Error from setsockopt(SO_SNDTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                        FreeNoLog(errmsg);
                        return FALSE;
                    }
                }
#else
                if (setsockopt(netw->link.sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&value, sizeof value) == -1) {
                    error = neterrno;
                    errmsg = netstrerror(error);
                    _netw_capture_error(netw, "Error from setsockopt(SO_SNDTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    n_log(LOG_ERR, "Error from setsockopt(SO_SNDTIMEO) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    FreeNoLog(errmsg);
                    return FALSE;
                }
#endif
            }
            netw->so_sndtimeo = value;
            break;
        case NETWORK_DEPLETE_SOCKET_TIMEOUT:
            netw->deplete_socket_timeout = value;
            break;
        case NETWORK_DEPLETE_QUEUES_TIMEOUT:
            netw->deplete_queues_timeout = value;
            break;
        case NETWORK_WAIT_CLOSE_TIMEOUT:
            netw->wait_close_timeout = value;
            break;
        case NETWORK_CONSECUTIVE_SEND_WAIT:
            netw->send_queue_consecutive_wait = value;
            break;
#ifdef __linux__
        case TCP_USER_TIMEOUT:
            if (value >= 0) {
                if (setsockopt(netw->link.sock, IPPROTO_TCP, TCP_USER_TIMEOUT, (const char*)&value, sizeof value) == -1) {
                    error = neterrno;
                    errmsg = netstrerror(error);
                    _netw_capture_error(netw, "Error from setsockopt(TCP_USER_TIMEOUT) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    n_log(LOG_ERR, "Error from setsockopt(TCP_USER_TIMEOUT) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                    FreeNoLog(errmsg);
                    return FALSE;
                }
            }
            break;
        case TCP_QUICKACK:
            if (setsockopt(netw->link.sock, IPPROTO_TCP, TCP_QUICKACK, &value, sizeof(value)) < 0) {
                error = neterrno;
                errmsg = netstrerror(error);
                _netw_capture_error(netw, "Error setting setsockopt(TCP_QUICKACK) to %d on sock %d. neterrno: %s", value, netw->link.sock, _str(errmsg));
                n_log(LOG_ERR, "Error setting setsockopt(TCP_QUICKACK) to %d on sock %d. neterrno: %s", value, netw->link.sock, _str(errmsg));
                FreeNoLog(errmsg);
                return FALSE;
            }
            break;
#endif
        case SO_KEEPALIVE:
            if (setsockopt(netw->link.sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&value, sizeof value) == -1) {
                error = neterrno;
                errmsg = netstrerror(error);
                _netw_capture_error(netw, "Error from setsockopt(SO_KEEPALIVE) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                n_log(LOG_ERR, "Error from setsockopt(SO_KEEPALIVE) on socket %d. neterrno: %s", netw->link.sock, _str(errmsg));
                FreeNoLog(errmsg);
                return FALSE;
            }
            netw->so_keepalive = value;
            break;

        default:
            _netw_capture_error(netw, "%d is not a supported setsockopt", optname);
            n_log(LOG_ERR, "%d is not a supported setsockopt", optname);
            return FALSE;
    }
    return TRUE;
} /* netw_set_sock_opt */

#ifdef HAVE_OPENSSL

/**
 *@brief get the OpenSSL error string
 *@return allocated string containing the error messages, or NULL
 */
char* netw_get_openssl_error_string() {
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return NULL;
    }

    ERR_print_errors(bio);  // Write errors to the BIO

    char* buf;
    size_t len = (size_t)BIO_get_mem_data(bio, &buf);  // Get data from the BIO. Can return 0 if empty of failled

    // Allocate memory for the error string and copy it
    char* error_str = malloc(len + 1);
    if (error_str) {
        memcpy(error_str, buf, len);
        error_str[len] = '\0';  // Null-terminate the string
    }

    BIO_free(bio);  // Free the BIO

    return error_str;
}

/**
 *@brief print OpenSSL errors for a given socket
 *@param socket the socket to report errors for
 */
void netw_ssl_print_errors(SOCKET socket) {
    unsigned long error = 0;
    while ((error = ERR_get_error())) {
        n_log(LOG_ERR, "socket %d: %s", socket, ERR_reason_error_string(error));
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
static pthread_mutex_t* netw_ssl_lockarray;

__attribute__((unused)) static void netw_ssl_lock_callback(int mode, int type, char* file, int line) {
    (void)file;
    (void)line;
    if (mode & CRYPTO_LOCK) {
        pthread_mutex_lock(&(netw_ssl_lockarray[type]));
    } else {
        pthread_mutex_unlock(&(netw_ssl_lockarray[type]));
    }
}

__attribute__((unused)) static unsigned long thread_id(void) {
    unsigned long ret;

    ret = (unsigned long)pthread_self();
    return ret;
}

static void netw_init_locks(void) {
    int i;

    size_t lock_count = (size_t)CRYPTO_num_locks();
    netw_ssl_lockarray = (pthread_mutex_t*)OPENSSL_malloc((size_t)(lock_count * sizeof(pthread_mutex_t)));

    for (i = 0; i < CRYPTO_num_locks(); i++) {
        pthread_mutex_init(&(netw_ssl_lockarray[i]), NULL);
    }

    CRYPTO_set_id_callback((unsigned long (*)())thread_id);
    CRYPTO_set_locking_callback((void (*)())netw_ssl_lock_callback);
}

static void netw_kill_locks(void) {
    int i;

    CRYPTO_set_locking_callback(NULL);
    for (i = 0; i < CRYPTO_num_locks(); i++)
        pthread_mutex_destroy(&(netw_ssl_lockarray[i]));

    OPENSSL_free(netw_ssl_lockarray);
}

static int OPENSSL_IS_INITIALIZED = 0;

/**
 *@brief Do not directly use, internal api. Initialize openssl
 *@return TRUE
 */
int netw_init_openssl(void) {
    if (OPENSSL_IS_INITIALIZED == 1)
        return TRUE; /*already loaded*/

    SSL_library_init();
    SSL_load_error_strings();
    // Before OpenSSL 1.1.0 (< 0x10100000L): ERR_load_BIO_strings(); was required to load error messages for BIO functions
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ERR_load_BIO_strings();
#endif
    OpenSSL_add_all_algorithms();
    netw_init_locks();

#ifndef __windows__
    /* The raw send paths pass MSG_NOSIGNAL (NETFLAGS),
     * but OpenSSL's internal write(2) does not: a TLS peer that
     * disconnects mid-SSL_write delivers SIGPIPE and kills the whole
     * process (server, client, or test alike). Ignoring it here turns
     * that into the EPIPE errno the SSL error paths already handle.
     * Standard practice for OpenSSL applications; scoped to TLS users
     * since this only runs from netw_init_openssl. */
    signal(SIGPIPE, SIG_IGN);
#endif

    OPENSSL_IS_INITIALIZED = 1;

    return TRUE;
} /*netw_init_openssl(...)*/

/**
 *@brief Do not directly use, internal api. Initialize openssl
 *@return TRUE
 */
int netw_unload_openssl(void) {
    if (OPENSSL_IS_INITIALIZED == 0)
        return TRUE; /*already unloaded*/

    netw_kill_locks();
    EVP_cleanup();

    OPENSSL_IS_INITIALIZED = 0;

    return TRUE;
} /*netw_unload_openssl(...)*/

/**
 * @brief activate SSL encryption on selected network, using key and certificate
 * @param netw the NETWORK instance to configure
 * @param key path to the key file
 * @param certificate path to the certificate file
 * @return TRUE of FALSE
 */
int netw_set_crypto(NETWORK* netw, char* key, char* certificate) {
    __n_assert(netw, return FALSE);

    char* tmp_key = NULL;
    char* tmp_cert = NULL;
    if (key && strlen(key) > 0) {
        tmp_key = strdup(key);
        if (!tmp_key) {
            n_log(LOG_ERR, "strdup failed for key in netw_set_crypto");
            return FALSE;
        }
    }
    if (certificate && strlen(certificate) > 0) {
        tmp_cert = strdup(certificate);
        if (!tmp_cert) {
            n_log(LOG_ERR, "strdup failed for certificate in netw_set_crypto");
            FreeNoLog(tmp_key);
            return FALSE;
        }
    }
    if (tmp_key) {
        FreeNoLog(netw->key);
        netw->key = tmp_key;
    }
    if (tmp_cert) {
        FreeNoLog(netw->certificate);
        netw->certificate = tmp_cert;
    }
    if (key && certificate) {
        netw_init_openssl();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L  // OpenSSL 1.1.0 or later
        netw->method = TLS_method();       // create new server-method instance
#else
        netw->method = TLSv1_2_method();  // create new server-method instance
#endif
        netw->ctx = SSL_CTX_new(netw->method);  // create new context from method
                                                // SSL_CTX_set_verify(netw -> ctx, SSL_VERIFY_PEER, NULL); // Enable certificate verification

        if (netw->ctx == NULL) {
            netw_ssl_print_errors(netw->link.sock);
            return FALSE;
        }
        /* PARTIAL_WRITE: a short SSL_write can be resumed from the
         * caller's buffer+offset (required by the reactor's
         * single-attempt send path); MOVING_WRITE_BUFFER: tolerate the
         * resume pointer not being the original one. No-op for the
         * blocking thread engine. */
        SSL_CTX_set_mode(netw->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

        // Load default system certs
        if (SSL_CTX_load_verify_locations(netw->ctx, NULL, "/etc/ssl/certs/") != 1) {
            netw_ssl_print_errors(netw->link.sock);
            return FALSE;
        }

        if (SSL_CTX_use_certificate_file(netw->ctx, certificate, SSL_FILETYPE_PEM) <= 0) {
            netw_ssl_print_errors(netw->link.sock);
            return FALSE;
        }
        if (SSL_CTX_use_PrivateKey_file(netw->ctx, key, SSL_FILETYPE_PEM) <= 0) {
            netw_ssl_print_errors(netw->link.sock);
            return FALSE;
        }

        netw->send_data = &send_ssl_data;
        netw->recv_data = &recv_ssl_data;
        netw->send_data_once = &send_ssl_data_once;
        netw->recv_data_once = &recv_ssl_data_once;

        netw->crypto_algo = NETW_ENCRYPT_OPENSSL;
    }

    return TRUE;
} /* netw_set_crypto */

/**
 * @brief activate SSL encryption using PEM-formatted key and certificate strings loaded from memory
 * @param netw the NETWORK to configure
 * @param key_pem PEM-formatted private key string
 * @param cert_pem PEM-formatted certificate string
 * @return TRUE or FALSE
 */
int netw_set_crypto_pem(NETWORK* netw, const char* key_pem, const char* cert_pem) {
    __n_assert(netw, return FALSE);
    __n_assert(key_pem, return FALSE);
    __n_assert(cert_pem, return FALSE);

    netw_init_openssl();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    netw->method = TLS_method();
#else
    netw->method = TLSv1_2_method();
#endif
    netw->ctx = SSL_CTX_new(netw->method);
    if (netw->ctx == NULL) {
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }
    /* PARTIAL_WRITE: a short SSL_write can be resumed from the
     * caller's buffer+offset (required by the reactor's
     * single-attempt send path); MOVING_WRITE_BUFFER: tolerate the
     * resume pointer not being the original one. No-op for the
     * blocking thread engine. */
    SSL_CTX_set_mode(netw->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    /* Load certificate from PEM string */
    BIO* cert_bio = BIO_new_mem_buf(cert_pem, -1);
    if (!cert_bio) {
        n_log(LOG_ERR, "Failed to create BIO for certificate PEM");
        return FALSE;
    }
    X509* cert = PEM_read_bio_X509(cert_bio, NULL, NULL, NULL);
    BIO_free(cert_bio);
    if (!cert) {
        n_log(LOG_ERR, "Failed to parse certificate PEM");
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }
    if (SSL_CTX_use_certificate(netw->ctx, cert) <= 0) {
        X509_free(cert);
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }
    X509_free(cert);

    /* Load private key from PEM string */
    BIO* key_bio = BIO_new_mem_buf(key_pem, -1);
    if (!key_bio) {
        n_log(LOG_ERR, "Failed to create BIO for key PEM");
        return FALSE;
    }
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, NULL, NULL, NULL);
    BIO_free(key_bio);
    if (!pkey) {
        n_log(LOG_ERR, "Failed to parse private key PEM");
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }
    if (SSL_CTX_use_PrivateKey(netw->ctx, pkey) <= 0) {
        EVP_PKEY_free(pkey);
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }
    EVP_PKEY_free(pkey);

    /* Verify that key matches certificate */
    if (!SSL_CTX_check_private_key(netw->ctx)) {
        n_log(LOG_ERR, "Private key does not match the certificate");
        return FALSE;
    }

    netw->send_data = &send_ssl_data;
    netw->recv_data = &recv_ssl_data;
    netw->send_data_once = &send_ssl_data_once;
    netw->recv_data_once = &recv_ssl_data_once;
    netw->crypto_algo = NETW_ENCRYPT_OPENSSL;

    return TRUE;
} /* netw_set_crypto_pem */

/**
 * @brief activate SSL encryption using key/certificate files and a CA file for chain verification
 * @param netw the NETWORK to configure
 * @param key path to the key file
 * @param certificate path to the certificate file
 * @param ca_file path to the CA certificate file for chain verification
 * @return TRUE or FALSE
 */
int netw_set_crypto_chain(NETWORK* netw, char* key, char* certificate, char* ca_file) {
    __n_assert(netw, return FALSE);
    __n_assert(key, return FALSE);
    __n_assert(certificate, return FALSE);
    __n_assert(ca_file, return FALSE);

    if (netw_set_crypto(netw, key, certificate) == FALSE) {
        return FALSE;
    }

    /* Load the full certificate chain */
    if (SSL_CTX_use_certificate_chain_file(netw->ctx, certificate) <= 0) {
        n_log(LOG_ERR, "Failed to load certificate chain from %s", certificate);
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }

    /* Load CA file for verification */
    if (SSL_CTX_load_verify_locations(netw->ctx, ca_file, NULL) != 1) {
        n_log(LOG_ERR, "Failed to load CA file %s", ca_file);
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }

    return TRUE;
} /* netw_set_crypto_chain */

/**
 * @brief activate SSL encryption using PEM strings for key, certificate, and CA
 * @param netw the NETWORK to configure
 * @param key_pem PEM-formatted private key string
 * @param cert_pem PEM-formatted certificate string
 * @param ca_pem PEM-formatted CA certificate string for verification
 * @return TRUE or FALSE
 */
int netw_set_crypto_chain_pem(NETWORK* netw, const char* key_pem, const char* cert_pem, const char* ca_pem) {
    __n_assert(netw, return FALSE);
    __n_assert(key_pem, return FALSE);
    __n_assert(cert_pem, return FALSE);
    __n_assert(ca_pem, return FALSE);

    if (netw_set_crypto_pem(netw, key_pem, cert_pem) == FALSE) {
        return FALSE;
    }

    /* Load CA certificate from PEM string into the trust store */
    BIO* ca_bio = BIO_new_mem_buf(ca_pem, -1);
    if (!ca_bio) {
        n_log(LOG_ERR, "Failed to create BIO for CA PEM");
        return FALSE;
    }

    X509_STORE* store = SSL_CTX_get_cert_store(netw->ctx);
    if (!store) {
        BIO_free(ca_bio);
        n_log(LOG_ERR, "Failed to get certificate store from SSL context");
        return FALSE;
    }

    X509* ca_cert = NULL;
    int ca_loaded = 0;
    while ((ca_cert = PEM_read_bio_X509(ca_bio, NULL, NULL, NULL)) != NULL) {
        if (X509_STORE_add_cert(store, ca_cert) != 1) {
            n_log(LOG_ERR, "Failed to add CA certificate to store");
            X509_free(ca_cert);
            BIO_free(ca_bio);
            return FALSE;
        }
        X509_free(ca_cert);
        ca_loaded++;
    }
    BIO_free(ca_bio);

    if (ca_loaded == 0) {
        n_log(LOG_ERR, "No CA certificates were loaded from PEM string");
        return FALSE;
    }

    /* Also load any extra chain certificates from the cert PEM */
    BIO* chain_bio = BIO_new_mem_buf(cert_pem, -1);
    if (chain_bio) {
        /* Skip the first certificate (already loaded as the leaf) */
        X509* skip_cert = PEM_read_bio_X509(chain_bio, NULL, NULL, NULL);
        if (skip_cert) {
            X509_free(skip_cert);
        }
        /* Load remaining certificates as chain intermediates */
        X509* chain_cert = NULL;
        while ((chain_cert = PEM_read_bio_X509(chain_bio, NULL, NULL, NULL)) != NULL) {
            if (SSL_CTX_add_extra_chain_cert(netw->ctx, chain_cert) != 1) {
                n_log(LOG_ERR, "Failed to add chain certificate");
                X509_free(chain_cert);
            }
            /* Note: SSL_CTX_add_extra_chain_cert takes ownership, so no X509_free on success */
        }
        BIO_free(chain_bio);
    }

    return TRUE;
} /* netw_set_crypto_chain_pem */

/**
 * @brief set custom CA verify location for SSL context
 * @param netw the NETWORK to configure
 * @param ca_file path to a CA certificate file, or NULL
 * @param ca_path path to a directory of CA certificates, or NULL
 * @return TRUE or FALSE
 */
int netw_ssl_set_ca(NETWORK* netw, const char* ca_file, const char* ca_path) {
    __n_assert(netw, return FALSE);
    __n_assert(netw->ctx, n_log(LOG_ERR, "SSL context not initialized, call netw_set_crypto first"); return FALSE);

    if (!ca_file && !ca_path) {
        n_log(LOG_ERR, "At least one of ca_file or ca_path must be specified");
        return FALSE;
    }

    if (SSL_CTX_load_verify_locations(netw->ctx, ca_file, ca_path) != 1) {
        n_log(LOG_ERR, "Failed to load CA from file=%s path=%s", _str(ca_file), _str(ca_path));
        _netw_capture_error(netw, "Failed to load CA from file=%s path=%s", _str(ca_file), _str(ca_path));
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }

    return TRUE;
} /* netw_ssl_set_ca */

/**
 * @brief enable or disable SSL peer certificate verification
 * @param netw the NETWORK to configure
 * @param enable 1 to enable verification, 0 to disable
 * @return TRUE or FALSE
 */
int netw_ssl_set_verify(NETWORK* netw, int enable) {
    __n_assert(netw, return FALSE);
    __n_assert(netw->ctx, n_log(LOG_ERR, "SSL context not initialized, call netw_set_crypto first"); return FALSE);

    if (enable) {
        SSL_CTX_set_verify(netw->ctx, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_CTX_set_verify(netw->ctx, SSL_VERIFY_NONE, NULL);
    }

    return TRUE;
} /* netw_ssl_set_verify */

/**
 * @brief load a client certificate and private key for mTLS
 * @param netw the NETWORK to configure
 * @param cert_file path to the client certificate file (PEM)
 * @param key_file path to the client private key file (PEM), or NULL to use cert_file
 * @return TRUE or FALSE
 */
int netw_ssl_set_client_cert(NETWORK* netw, const char* cert_file, const char* key_file) {
    __n_assert(netw, return FALSE);
    __n_assert(netw->ctx, n_log(LOG_ERR, "SSL context not initialized"); return FALSE);
    __n_assert(cert_file, return FALSE);

    if (SSL_CTX_use_certificate_file(netw->ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
        n_log(LOG_ERR, "Failed to load client certificate from %s", cert_file);
        _netw_capture_error(netw, "Failed to load client certificate from %s", cert_file);
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }

    const char* kf = key_file ? key_file : cert_file;
    if (SSL_CTX_use_PrivateKey_file(netw->ctx, kf, SSL_FILETYPE_PEM) != 1) {
        n_log(LOG_ERR, "Failed to load client private key from %s", kf);
        _netw_capture_error(netw, "Failed to load client private key from %s", kf);
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }

    if (SSL_CTX_check_private_key(netw->ctx) != 1) {
        n_log(LOG_ERR, "Client certificate and private key do not match");
        _netw_capture_error(netw, "Client certificate and private key do not match");
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }

    return TRUE;
} /* netw_ssl_set_client_cert */

#endif /* HAVE_OPENSSL */

/**
 *@brief Connect a socket, optionally bounding the attempt by a timeout.
 *
 * With connect_timeout_ms <= 0 this performs the historical blocking
 * connect() (the OS decides when to give up). With a positive timeout the
 * socket is switched to non-blocking, connect() is issued, and select()
 * waits up to the timeout for completion; the socket is always restored to
 * blocking mode before returning so the rest of the stack sees a normal
 * blocking socket.
 *@param netw NETWORK owning the socket (its link.sock must equal sock)
 *@param sock the socket to connect
 *@param rp addrinfo describing the target address
 *@param connect_timeout_ms connect timeout in milliseconds, 0 = none
 *@return 0 on success, -1 on error or timeout
 */
static int _netw_timed_connect(NETWORK* netw, SOCKET sock, struct addrinfo* rp, int connect_timeout_ms) {
    if (connect_timeout_ms <= 0) {
        return connect(sock, rp->ai_addr, (socklen_t)rp->ai_addrlen);
    }

    if (netw_set_blocking(netw, 0) == FALSE) {
        /* could not switch modes: fall back to a blocking connect */
        return connect(sock, rp->ai_addr, (socklen_t)rp->ai_addrlen);
    }

    int rc = connect(sock, rp->ai_addr, (socklen_t)rp->ai_addrlen);
    if (rc == 0) {
        netw_set_blocking(netw, 1);
        return 0;
    }

    int in_progress = 0;
#ifdef __windows__
    if (neterrno == WSAEWOULDBLOCK || neterrno == WSAEINPROGRESS) in_progress = 1;
#else
    if (neterrno == EINPROGRESS) in_progress = 1;
#endif
    if (!in_progress) {
        netw_set_blocking(netw, 1);
        return -1;
    }

    /* select() blocks the thread in the kernel (no busy-wait). If a signal
       interrupts it (EINTR) we retry with the remaining time so the total
       wait still honours connect_timeout_ms. select() only rewrites the
       timeval argument on Linux, so the remaining time is tracked
       explicitly with N_TIME for portability (Windows/MinGW included). */
    N_TIME timer;
    start_HiTimer(&timer);
    time_t remaining_us = (time_t)connect_timeout_ms * 1000;
    int connected = 0;
    for (;;) {
        /* Watch both the write set and the exception set: a completed
           connect signals writefds, but on Winsock a *failed* connect
           signals exceptfds (not writefds). Watching only writefds there
           would miss the failure and wait out the whole timeout. The
           SO_ERROR check after the loop is the final arbiter on both. */
        fd_set wset, eset;
        FD_ZERO(&wset);
        FD_SET(sock, &wset);
        FD_ZERO(&eset);
        FD_SET(sock, &eset);
        struct timeval tv;
        tv.tv_sec = (long)(remaining_us / 1000000);
        tv.tv_usec = (long)(remaining_us % 1000000);

        int sel = select((int)sock + 1, NULL, &wset, &eset, &tv);
        if (sel > 0) {
            connected = 1; /* socket signalled; SO_ERROR check decides below */
            break;
        }
        if (sel == 0) {
            break; /* timed out */
        }
        /* sel < 0 */
#ifdef __windows__
        int interrupted = (neterrno == WSAEINTR);
#else
        int interrupted = (neterrno == EINTR);
#endif
        if (!interrupted) {
            break; /* genuine select() error */
        }
        /* interrupted by a signal: subtract the time already spent and
           retry only if some of the budget is left. */
        remaining_us -= get_usec(&timer);
        if (remaining_us <= 0) {
            break; /* budget exhausted while interrupted */
        }
    }

    if (!connected) {
        netw_set_blocking(netw, 1);
        return -1;
    }

    int so_error = 0;
    socklen_t slen = sizeof(so_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &slen) == -1 || so_error != 0) {
        netw_set_blocking(netw, 1);
        return -1;
    }

    netw_set_blocking(netw, 1);
    return 0;
} /* _netw_timed_connect */

/**
 *@brief Use this to connect a NETWORK to any listening one
 *@param netw a NETWORK *object
 *@param host Host or IP to connect to
 *@param port Port to use to connect
 *@param send_list_limit Internal sending list maximum number of item. From UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@param recv_list_limit Internal receiving list maximum number of item. From UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *@param ssl_key_file NULL or the path to the SSL key file to use. If set, a ssl_cert_file must also be provided
 *@param ssl_cert_file NULL or the path to the SSL certificate file to use. If set, a ssl_key_file must also be provided
 *@param connect_timeout_ms connect timeout in milliseconds, 0 = OS default
 *@return TRUE or FALSE
 */
int netw_connect_ex_to(NETWORK** netw, char* host, char* port, size_t send_list_limit, size_t recv_list_limit, int ip_version, char* ssl_key_file, char* ssl_cert_file, int connect_timeout_ms) {
    // kill compilation warning when there is no openssl
    (void)ssl_key_file;
    (void)ssl_cert_file;
    int error = 0, net_status = 0;
    char* errmsg = NULL;

    /*do not work over an already used netw*/
    if ((*netw)) {
        n_log(LOG_ERR, "Unable to allocate (*netw), already existing. You must use empty NETWORK *structs.");
        return FALSE;
    }

    /*creating array*/
    (*netw) = netw_new(send_list_limit, recv_list_limit);
    __n_assert(netw && (*netw), return FALSE);

    /*checking WSA when under windows*/
    if (netw_init_wsa(1, 2, 2) == FALSE) {
        n_log(LOG_ERR, "Unable to load WSA dll's");
        netw_close(netw);
        return FALSE;
    }

    /* choose ip version */
    if (ip_version == NETWORK_IPV4) {
        (*netw)->link.hints.ai_family = AF_INET; /* Allow IPv4 */
    } else if (ip_version == NETWORK_IPV6) {
        (*netw)->link.hints.ai_family = AF_INET6; /* Allow IPv6 */
    } else {
        /* NETWORK_ALL or unknown value */
        (*netw)->link.hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
    }

    (*netw)->link.hints.ai_socktype = SOCK_STREAM;
    (*netw)->link.hints.ai_protocol = IPPROTO_TCP;
    (*netw)->link.hints.ai_flags = AI_PASSIVE;
    (*netw)->link.hints.ai_canonname = NULL;
    (*netw)->link.hints.ai_next = NULL;

    /* Time the resolve and connect phases so callers can present a
       connect-timing breakdown. get_usec returns the delta since the
       previous call, so the first reads the DNS phase and the second
       reads the TCP-connect phase. */
    N_TIME connect_timer;
    start_HiTimer(&connect_timer);

    /* Note: on some system, i.e Solaris, it WILL show leak in getaddrinfo.
     * Testing it inside a 1,100 loop showed not effect on the amount of leaked
     * memory */
    error = getaddrinfo(host, port, &(*netw)->link.hints, &(*netw)->link.rhost);
    if (error != 0) {
        _netw_capture_error(*netw, "Error when resolving %s:%s getaddrinfo: %s", host, port, gai_strerror(error));
        n_log(LOG_ERR, "Error when resolving %s:%s getaddrinfo: %s", host, port, gai_strerror(error));
        _netw_capture_connect_error("DNS resolution failed for %s:%s: %s", host, port, gai_strerror(error));
        netw_close(netw);
        return FALSE;
    }
    (*netw)->addr_infos_loaded = 1;
    (*netw)->connect_dns_usec = (long long)get_usec(&connect_timer);
    Malloc((*netw)->link.ip, char, 64);
    __n_assert((*netw)->link.ip, netw_close(netw); return FALSE);

    /* getaddrinfo() returns a list of address structures. Try each address until we successfully connect. If socket or connect fails, we close the socket and try the next address. */
    struct addrinfo* rp = NULL;
    for (rp = (*netw)->link.rhost; rp != NULL; rp = rp->ai_next) {
        SOCKET sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == INVALID_SOCKET) {
            error = neterrno;
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Error while trying to make a socket: %s", _str(errmsg));
            FreeNoLog(errmsg);
            continue;
        }

        (*netw)->link.sock = sock;

        net_status = _netw_timed_connect(*netw, sock, rp, connect_timeout_ms);
        if (net_status == -1) {
            error = neterrno;
            errmsg = netstrerror(error);
            n_log(LOG_INFO, "connecting to %s:%s : %s", host, port, _str(errmsg));
            FreeNoLog(errmsg);
            closesocket(sock);
            (*netw)->link.sock = INVALID_SOCKET;
            continue;
        } else {
            /*storing connected port and ip address*/
            if (!inet_ntop(rp->ai_family, get_in_addr(rp->ai_addr), (*netw)->link.ip, 64)) {
                error = neterrno;
                errmsg = netstrerror(error);
                n_log(LOG_ERR, "inet_ntop: %p , %s", rp, _str(errmsg));
                FreeNoLog(errmsg);
            }
            break; /* Success */
        }
    }
    if (rp == NULL) {
        /* No address succeeded */
        _netw_capture_error(*netw, "Couldn't connect to %s:%s : no address succeeded", host, port);
        n_log(LOG_ERR, "Couldn't connect to %s:%s : no address succeeded", host, port);
        _netw_capture_connect_error("TCP connect failed for %s:%s: no address succeeded", host, port);
        netw_close(netw);
        return FALSE;
    }
    (*netw)->connect_tcp_usec = (long long)get_usec(&connect_timer);

    (*netw)->link.port = strdup(port);
    __n_assert((*netw)->link.port, netw_close(netw); return FALSE);

    if (ssl_key_file && ssl_cert_file) {
#ifdef HAVE_OPENSSL
        if (netw_set_crypto((*netw), ssl_key_file, ssl_cert_file) == FALSE) {
            /* could not initialize SSL */
            n_log(LOG_ERR, "couldn't initialize SSL !");
            _netw_capture_error(*netw, "SSL initialization failed");
            netw_close(netw);
            return FALSE;
        }

        (*netw)->ssl = SSL_new((*netw)->ctx);
        SSL_set_fd((*netw)->ssl, (int)(*netw)->link.sock);

        // Perform SSL Handshake
        if (SSL_connect((*netw)->ssl) <= 0) {
            /* could not connect with SSL */
            n_log(LOG_ERR, "SSL Handshake error !");
            _netw_capture_error(*netw, "SSL handshake failed for %s:%s", (*netw)->link.ip, (*netw)->link.port);
            netw_close(netw);
            return FALSE;
        }
        n_log(LOG_DEBUG, "SSL-Connected to %s:%s", (*netw)->link.ip, (*netw)->link.port);
#else
        _netw_capture_error(*netw, "%s:%s trying to configure SSL but application was compiled without SSL support !", (*netw)->link.ip, (*netw)->link.port);
        n_log(LOG_ERR, "%s:%s trying to configure SSL but application was compiled without SSL support !", (*netw)->link.ip, (*netw)->link.port);
#endif
    } else {
        n_log(LOG_DEBUG, "Connected to %s:%s", (*netw)->link.ip, (*netw)->link.port);
    }

    netw_set((*netw), NETW_CLIENT | NETW_RUN | NETW_THR_ENGINE_STOPPED);

    return TRUE;
} /* netw_connect_ex_to(...)*/

/**
 *@brief Use this to connect a NETWORK to any listening one
 *@param netw a NETWORK *object
 *@param host Host or IP to connect to
 *@param port Port to use to connect
 *@param send_list_limit Internal sending list maximum number of item. From UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@param recv_list_limit Internal receiving list maximum number of item. From UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *@param ssl_key_file NULL or the path to the SSL key file to use. If set, a ssl_cert_file must also be provided
 *@param ssl_cert_file NULL or the path to the SSL certificate file to use. If set, a ssl_key_file must also be provided
 *@return TRUE or FALSE
 */
int netw_connect_ex(NETWORK** netw, char* host, char* port, size_t send_list_limit, size_t recv_list_limit, int ip_version, char* ssl_key_file, char* ssl_cert_file) {
    return netw_connect_ex_to(netw, host, port, send_list_limit, recv_list_limit, ip_version, ssl_key_file, ssl_cert_file, 0);
} /* netw_connect_ex(...)*/

/**
 *@brief Use this to connect a NETWORK to any listening one, unrestricted send/recv lists
 *@param netw a NETWORK *object
 *@param host Host or IP to connect to
 *@param port Port to use to connect
 *@param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *@return TRUE or FALSE
 */
int netw_connect(NETWORK** netw, char* host, char* port, int ip_version) {
    n_log(LOG_INFO, "Trying to connect to %s : %s", _str(host), _str(port));
    return netw_connect_ex(netw, host, port, MAX_LIST_ITEMS, MAX_LIST_ITEMS, ip_version, NULL, NULL);
} /* netw_connect() */

/**
 *@brief Connect a NETWORK with a bounded connection-establishment time
 *@param netw a NETWORK *object
 *@param host Host or IP to connect to
 *@param port Port to use to connect
 *@param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *@param connect_timeout_ms connect timeout in milliseconds, 0 = OS default
 *@return TRUE or FALSE
 */
int netw_connect_to(NETWORK** netw, char* host, char* port, int ip_version, int connect_timeout_ms) {
    n_log(LOG_INFO, "Trying to connect to %s : %s (connect timeout %d ms)", _str(host), _str(port), connect_timeout_ms);
    return netw_connect_ex_to(netw, host, port, MAX_LIST_ITEMS, MAX_LIST_ITEMS, ip_version, NULL, NULL, connect_timeout_ms);
} /* netw_connect_to() */

#ifdef HAVE_OPENSSL
/**
 *@brief Use this to connect a NETWORK to any listening one, unrestricted send/recv lists
 *@param netw a NETWORK *object
 *@param host Host or IP to connect to
 *@param port Port to use to connect
 *@param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *@param ssl_key_file NULL or the path to the SSL key file to use. If set, a ssl_cert_file must also be provided
 *@param ssl_cert_file NULL or the path to the SSL certificate file to use. If set, a ssl_key_file must also be provided
 *@return TRUE or FALSE
 */
int netw_ssl_connect(NETWORK** netw, char* host, char* port, int ip_version, char* ssl_key_file, char* ssl_cert_file) {
    n_log(LOG_INFO, "Trying to connect to %s : %s", _str(host), _str(port));
    return netw_connect_ex(netw, host, port, MAX_LIST_ITEMS, MAX_LIST_ITEMS, ip_version, ssl_key_file, ssl_cert_file);
} /* netw_connect() */

/**
 * @brief Connect as an SSL client without providing a client certificate.
 *
 * Performs TCP connect, creates an SSL_CTX with TLS_client_method and
 * loads default system verify paths. Does NOT perform the SSL handshake.
 * Call netw_ssl_set_ca / netw_ssl_set_verify to configure verification,
 * then netw_ssl_do_handshake to complete the connection.
 *
 * @param netw output: new NETWORK (must be NULL on entry)
 * @param host hostname to connect to
 * @param port port string
 * @param ip_version NETWORK_IPALL, NETWORK_IPV4, or NETWORK_IPV6
 * @param connect_timeout_ms connect timeout in milliseconds, 0 = OS default
 * @return TRUE or FALSE
 */
int netw_ssl_connect_client_to(NETWORK** netw, char* host, char* port, int ip_version, int connect_timeout_ms) {
    __n_assert(netw, return FALSE);
    /* TCP connect first (no SSL params) */
    if (netw_connect_ex_to(netw, host, port, MAX_LIST_ITEMS, MAX_LIST_ITEMS, ip_version, NULL, NULL, connect_timeout_ms) == FALSE) {
        return FALSE;
    }
    /* Create SSL context for client */
    netw_init_openssl();
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    (*netw)->method = TLS_client_method();
#else
    (*netw)->method = TLSv1_2_client_method();
#endif
    (*netw)->ctx = SSL_CTX_new((*netw)->method);
    if (!(*netw)->ctx) {
        netw_ssl_print_errors((*netw)->link.sock);
        netw_close(netw);
        return FALSE;
    }
    /* PARTIAL_WRITE: a short SSL_write can be resumed from the
     * caller's buffer+offset (required by the reactor's
     * single-attempt send path); MOVING_WRITE_BUFFER: tolerate the
     * resume pointer not being the original one. No-op for the
     * blocking thread engine. */
    SSL_CTX_set_mode((*netw)->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    /* Load default system CA paths */
    SSL_CTX_set_default_verify_paths((*netw)->ctx);
    /* Set send/recv to SSL variants */
    (*netw)->send_data = &send_ssl_data;
    (*netw)->recv_data = &recv_ssl_data;
    (*netw)->send_data_once = &send_ssl_data_once;
    (*netw)->recv_data_once = &recv_ssl_data_once;
    (*netw)->crypto_algo = NETW_ENCRYPT_OPENSSL;
    return TRUE;
} /* netw_ssl_connect_client_to */

/**
 * @brief Connect as an SSL client without providing a client certificate.
 *
 * Equivalent to netw_ssl_connect_client_to with no connect timeout (the
 * OS decides when the TCP connect gives up). See netw_ssl_connect_client_to.
 *
 * @param netw output: new NETWORK (must be NULL on entry)
 * @param host hostname to connect to
 * @param port port string
 * @param ip_version NETWORK_IPALL, NETWORK_IPV4, or NETWORK_IPV6
 * @return TRUE or FALSE
 */
int netw_ssl_connect_client(NETWORK** netw, char* host, char* port, int ip_version) {
    return netw_ssl_connect_client_to(netw, host, port, ip_version, 0);
} /* netw_ssl_connect_client */

/**
 * @brief Complete the SSL handshake on an already-connected NETWORK.
 *
 * The NETWORK must have a valid SSL_CTX (from netw_ssl_connect_client
 * or netw_set_crypto). Optionally sets the SNI hostname for virtual
 * hosting support.
 *
 * @param netw connected NETWORK with ctx set
 * @param sni_hostname hostname for SNI extension, or NULL to skip
 * @return TRUE or FALSE
 */
int netw_ssl_do_handshake(NETWORK* netw, const char* sni_hostname) {
    __n_assert(netw, return FALSE);
    __n_assert(netw->ctx, n_log(LOG_ERR, "netw_ssl_do_handshake: no SSL context"); return FALSE);

    netw->ssl = SSL_new(netw->ctx);
    if (!netw->ssl) {
        _netw_capture_error(netw, "SSL_new failed");
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }
    SSL_set_fd(netw->ssl, (int)netw->link.sock);
    if (sni_hostname) {
        SSL_set_tlsext_host_name(netw->ssl, sni_hostname);
    }
    if (SSL_connect(netw->ssl) <= 0) {
        n_log(LOG_ERR, "SSL handshake failed");
        unsigned long err = ERR_peek_error();
        _netw_capture_error(netw, "SSL handshake failed for %s:%s: %s",
                            _str(netw->link.ip), _str(netw->link.port),
                            err ? ERR_reason_error_string(err) : "unknown error");
        netw_ssl_print_errors(netw->link.sock);
        return FALSE;
    }
    n_log(LOG_DEBUG, "SSL handshake completed with %s:%s", _str(netw->link.ip), _str(netw->link.port));
    return TRUE;
} /* netw_ssl_do_handshake */

#endif

/**
 *@brief Get the state of a network
 *@param netw The NETWORK *connection to query
 *@param state pointer to network status storage , NETW_RUN , NETW_EXIT_ASKED , NETW_EXITED
 *@param thr_engine_status pointer to network thread engine status storage ,NETW_THR_ENGINE_STARTED , NETW_THR_ENGINE_STOPPED
 *@return TRUE or FALSE
 */
int netw_get_state(NETWORK* netw, uint32_t* state, int* thr_engine_status) {
    if (netw) {
        /* use eventbolt: netw_set() writes state/threaded_engine_status under eventbolt */
        pthread_mutex_lock(&netw->eventbolt);
        if (state)
            (*state) = netw_atomic_read_state(netw);
        if (thr_engine_status)
            (*thr_engine_status) = netw->threaded_engine_status;
        pthread_mutex_unlock(&netw->eventbolt);
        return TRUE;
    } else {
        n_log(LOG_ERR, "Can't get status of a NULL network");
    }
    return FALSE;
} /*netw_get_state() */

/**
 *@brief Restart or reset the specified network ability
 *@param netw The NETWORK *connection to modify
 *@param flag NETW_EMPTY_SENDBUF, NETW_EMPTY_RECVBUF, NETW_RUN , NETW_EXIT_ASKED , NETW_EXITED
 *@return TRUE or FALSE
 */
int netw_set(NETWORK* netw, int flag) {
    __n_assert(netw, return FALSE);
    if (flag & NETW_EMPTY_SENDBUF) {
        pthread_mutex_lock(&netw->sendbolt);
        if (netw->send_buf)
            list_empty(netw->send_buf);
        pthread_mutex_unlock(&netw->sendbolt);
    };
    if (flag & NETW_EMPTY_RECVBUF) {
        pthread_mutex_lock(&netw->recvbolt);
        if (netw->recv_buf)
            list_empty(netw->recv_buf);
        pthread_mutex_unlock(&netw->recvbolt);
    }
    if (flag & NETW_DESTROY_SENDBUF) {
        pthread_mutex_lock(&netw->sendbolt);
        if (netw->send_buf)
            list_destroy(&netw->send_buf);
        pthread_mutex_unlock(&netw->sendbolt);
    };
    if (flag & NETW_DESTROY_RECVBUF) {
        pthread_mutex_lock(&netw->recvbolt);
        if (netw->recv_buf)
            list_destroy(&netw->recv_buf);
        pthread_mutex_unlock(&netw->recvbolt);
    }
    pthread_mutex_lock(&netw->eventbolt);
    if (flag & NETW_CLIENT) {
        netw->mode = NETW_CLIENT;
    }
    if (flag & NETW_SERVER) {
        netw->mode = NETW_SERVER;
    }
    if (flag & NETW_RUN) {
        netw_atomic_write_state(netw, NETW_RUN);
    }
    if (flag & NETW_EXITED) {
        netw_atomic_write_state(netw, NETW_EXITED);
    }
    if (flag & NETW_ERROR) {
        netw_atomic_write_state(netw, NETW_ERROR);
    }
    if (flag & NETW_EXIT_ASKED) {
        netw_atomic_write_state(netw, NETW_EXIT_ASKED);
    }
    if (flag & NETW_THR_ENGINE_STARTED) {
        netw->threaded_engine_status = NETW_THR_ENGINE_STARTED;
    }
    if (flag & NETW_THR_ENGINE_STOPPED) {
        netw->threaded_engine_status = NETW_THR_ENGINE_STOPPED;
    }
    pthread_mutex_unlock(&netw->eventbolt);

    /* Only wake the send thread for state changes it actually cares
     * about. The inner loop in netw_send_func branches on NETW_ERROR,
     * NETW_EXITED, and NETW_EXIT_ASKED; every other flag (mode /
     * thread-engine-status / buffer maintenance) is invisible to it,
     * and posting unconditionally creates a spurious wakeup that,
     * paired with an empty send_buf, produces the classic
     * "offset-by-one semaphore" bug where every subsequent real
     * list_push leaves one extra post in the semaphore for the next
     * sem_wait to consume on a drained list. Avoiding the spurious
     * post here is the root cause fix and removes the need for a
     * defensive branch in the common path. */
    if (flag & (NETW_ERROR | NETW_EXITED | NETW_EXIT_ASKED)) {
        sem_post(&netw->send_blocker);
    }

    return TRUE;
} /* netw_set(...) */

/**
 *@brief wait until the socket is empty or timeout, checking each 100 msec. Not as reliable as expected.
 *@param fd socket descriptor
 *@param timeout timeout value in msec , value <= 0 : disabled
 *@return 0 if timeout or unsupported, -1 on error or the amount of remaining datas in bytes
 */
int deplete_send_buffer(int fd, int timeout) {
#if defined(__linux__)
    int outstanding = 0;
    if (timeout <= 0) {
        return 0;
    }
    for (int it = 0; it < timeout; it += 100) {
        outstanding = 0;
        if (ioctl(fd, SIOCOUTQ, &outstanding) == -1) {
            int error = errno;
            n_log(LOG_ERR, "ioctl SIOCOUTQ returned -1: %s for socket %d", strerror(error), fd);
            return -1;
        }
        if (!outstanding) {
            break;
        }
        usleep(100000);
    }
    return outstanding;
#else
    (void)fd;
    (void)timeout;
    return 0;
#endif
}

/**
 *@brief Closing a specified Network, destroy queues, free the structure
 *@param netw A NETWORK *network to close
 *@return TRUE on success , FALSE on failure
 */
int netw_close(NETWORK** netw) {
    __n_assert(netw && (*netw), return FALSE);
    uint32_t state = 0;
    int thr_engine_status = 0;
    int nb_running = 0;

    /* Reactor close handshake. Synchronously wait for the reactor to
     * finish with this connection (drains pending sends best-effort,
     * shutdown(SHUT_WR) so the peer sees EOF, removes the fd from the
     * epoll set, publishes the close ack) before any of the existing
     * close logic runs. After this returns the reactor will never touch
     * the NETWORK again, so the rest of netw_close (and the final Free)
     * proceeds exactly like the thread-mode path.
     *
     * Keyed on reactor_registered, NOT the live reactor_mode: the reactor
     * clears reactor_mode partway through unregister (peer EOF / hard
     * error) and keeps touching the NETWORK for a few more lines before
     * acking. Gating on reactor_mode would let a clear that lands just
     * before this read send us straight to Free() while the reactor is
     * still mid-unregister (use-after-free). reactor_registered is set
     * once at register time and never cleared, so this is race-free.
     *
     * Compile-time gated on N_REACTOR_AVAILABLE so non-Linux/Android
     * builds (Windows MinGW, Solaris) don't pull in n_reactor.o just
     * to satisfy this symbol, the reactor is unavailable there anyway
     * and reactor_registered can never be set. */
#if N_REACTOR_AVAILABLE
    if (__atomic_load_n(&(*netw)->reactor_registered, __ATOMIC_ACQUIRE)) {
        n_reactor_close_netw_sync(*netw);
    }
#endif

    if ((*netw)->deplete_queues_timeout > 0) {
        /* use iteration count to avoid integer overflow with large timeouts */
        int max_iterations = (*netw)->deplete_queues_timeout * 10; /* each iteration ~100ms */
        netw_get_state((*netw), &state, &thr_engine_status);
        if (thr_engine_status == NETW_THR_ENGINE_STARTED) {
            int it = 0;
            do {
                pthread_mutex_lock(&(*netw)->eventbolt);
                nb_running = (*netw)->nb_running_threads;
                pthread_mutex_unlock(&(*netw)->eventbolt);
                usleep(100000);
                it++;
            } while (nb_running > 0 && it < max_iterations);
            netw_get_state((*netw), &state, &thr_engine_status);
            if (it >= max_iterations && nb_running > 0) {
                n_log(LOG_ERR, "netw %d: %d threads are still running after %d seconds, netw is in state %s (%" PRIu32 ")", (*netw)->link.sock, nb_running, (*netw)->deplete_queues_timeout, N_ENUM_ENTRY(__netw_code_type, toString)(state), state);
            }
        }
    }

    if ((*netw)->link.sock != INVALID_SOCKET) {
        int remaining = deplete_send_buffer((int)(*netw)->link.sock, (*netw)->deplete_socket_timeout);
        // cppcheck-suppress knownConditionTrueFalse ; remaining can be > 0 on Linux (SIOCOUTQ)
        if (remaining > 0) {
            n_log(LOG_ERR, "socket %d (%s:%s) %d octets still in send buffer before closing after a wait of %d msecs", (*netw)->link.sock, (*netw)->link.ip, (*netw)->link.port, remaining, (*netw)->deplete_socket_timeout);
        }
    }

    list_foreach(node, (*netw)->pools) {
        NETWORK_POOL* pool = (NETWORK_POOL*)node->ptr;
        netw_pool_remove(pool, (*netw));
    }
    list_destroy(&(*netw)->pools);

    netw_get_state((*netw), &state, &thr_engine_status);
    if (thr_engine_status == NETW_THR_ENGINE_STARTED) {
        netw_stop_thr_engine((*netw));
    }

    /* recompute nb_running after joining threads so the SSL_shutdown
     * decision reflects the actual post-join state */
    pthread_mutex_lock(&(*netw)->eventbolt);
    nb_running = (*netw)->nb_running_threads;
    pthread_mutex_unlock(&(*netw)->eventbolt);

    if ((*netw)->link.sock != INVALID_SOCKET) {
#ifdef HAVE_OPENSSL
        if ((*netw)->crypto_algo == NETW_ENCRYPT_OPENSSL) {
            if ((*netw)->ssl) {
                /* only attempt SSL_shutdown if threads have fully stopped,
                 * otherwise SSL operations may race with send/recv threads */
                if (nb_running == 0) {
                    int shutdown_res = SSL_shutdown((*netw)->ssl);
                    if (shutdown_res == 0) {
                        /* try again to complete bidirectional shutdown;
                         * peer may have already closed (common for HTTP),
                         * so SYSCALL/SSL errors are non-fatal here */
                        shutdown_res = SSL_shutdown((*netw)->ssl);
                    }
                    if (shutdown_res < 0) {
                        int err = SSL_get_error((*netw)->ssl, shutdown_res);
                        if (err != SSL_ERROR_SYSCALL && err != SSL_ERROR_SSL) {
                            n_log(LOG_ERR, "SSL_shutdown() failed: %d", err);
                        } else {
                            n_log(LOG_DEBUG, "SSL_shutdown() peer already closed: %d", err);
                        }
                    }
                } else {
                    n_log(LOG_WARNING, "netw %d: forcing bidirectional SSL_shutdown with %d threads still running", (*netw)->link.sock, nb_running);
                    int shutdown_res = SSL_shutdown((*netw)->ssl);
                    if (shutdown_res == 0) {
                        /* send close_notify and wait for peer's close_notify */
                        shutdown_res = SSL_shutdown((*netw)->ssl);
                    }
                    if (shutdown_res < 0) {
                        int err = SSL_get_error((*netw)->ssl, shutdown_res);
                        n_log(LOG_ERR, "netw %d: SSL_shutdown() failed with %d threads still running: %d", (*netw)->link.sock, nb_running, err);
                    }
                }
                SSL_free((*netw)->ssl);
            } else {
                if ((*netw)->mode != NETW_SERVER) {
                    n_log(LOG_ERR, "SSL handle of socket %d was already NULL", (*netw)->link.sock);
                } else {
                    n_log(LOG_DEBUG, "listening socket %d has no SSL handle (expected)", (*netw)->link.sock);
                }
            }
        }
#endif

        /* inform peer that we have finished */
        shutdown((*netw)->link.sock, SHUT_WR);

        if ((*netw)->wait_close_timeout > 0) {
            /* wait for fin ack using select() to avoid busy-waiting.
             * Use iteration count to avoid integer overflow. */
            char buffer[4096] = "";
            int max_iters = (*netw)->wait_close_timeout * 10; /* each iteration ~100ms */
            for (int it = 0; it < max_iters; it++) {
                fd_set rfds;
                struct timeval tv;
                FD_ZERO(&rfds);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
                FD_SET((*netw)->link.sock, &rfds);
#pragma GCC diagnostic pop
                tv.tv_sec = 0;
                tv.tv_usec = 100000; /* 100ms */
                int sel = select((int)(*netw)->link.sock + 1, &rfds, NULL, NULL, &tv);
                if (sel > 0) {
                    ssize_t res = recv((*netw)->link.sock, buffer, 4096, NETFLAGS);
                    if (!res)
                        break;
                    if (res < 0) {
                        int error = neterrno;
                        if (error != ENOTCONN && error != EINTR && error != ECONNRESET
#ifdef __windows__
                            && error != WSAENOTCONN && error != WSAECONNRESET && error != WSAESHUTDOWN && error != WSAEWOULDBLOCK && error != WSAEINTR
#endif
                        ) {
                            char* errmsg = netstrerror(error);
                            n_log(LOG_ERR, "read returned error %d when closing socket %d (%s:%s): %s", error, (*netw)->link.sock, _str((*netw)->link.ip), (*netw)->link.port, _str(errmsg));
                            FreeNoLog(errmsg);
                        } else {
                            n_log(LOG_DEBUG, "wait close: connection gracefully closed on socket %d (%s:%s)", (*netw)->link.sock, _str((*netw)->link.ip), (*netw)->link.port);
                        }
                        break;
                    }
                } else if (sel < 0) {
                    /* select error, bail out */
                    int error = neterrno;
                    if (error != EINTR
#ifdef __windows__
                        && error != WSAEINTR
#endif
                    ) {
                        char* errmsg = netstrerror(error);
                        n_log(LOG_ERR, "select() error on socket %d during close: %s", (*netw)->link.sock, _str(errmsg));
                        FreeNoLog(errmsg);
                        break;
                    }
                }
                /* sel == 0: timeout, continue waiting */
            }
        }

        /* effectively close socket */
        closesocket((*netw)->link.sock);

#ifdef HAVE_OPENSSL
        /* clean openssl state. Accepted server-side SSL connections borrow
         * ctx from the listener (their ctx is NULL by design), so a NULL
         * ctx here is not an error, only the owner frees it. */
        if ((*netw)->crypto_algo == NETW_ENCRYPT_OPENSSL && (*netw)->ctx) {
            SSL_CTX_free((*netw)->ctx);
        }
        n_log(LOG_DEBUG, "socket %d closed", (*netw)->link.sock);
        FreeNoLog((*netw)->key);
        FreeNoLog((*netw)->certificate);
#endif
    }

    FreeNoLog((*netw)->link.ip);
    FreeNoLog((*netw)->link.port);

    if ((*netw)->link.rhost) {
        freeaddrinfo((*netw)->link.rhost);
    }

    /*list freeing*/
    netw_set((*netw), NETW_DESTROY_SENDBUF | NETW_DESTROY_RECVBUF);

    pthread_mutex_destroy(&(*netw)->recvbolt);
    pthread_mutex_destroy(&(*netw)->sendbolt);
    pthread_mutex_destroy(&(*netw)->eventbolt);
    sem_destroy(&(*netw)->send_blocker);

    Free((*netw));

    return TRUE;
} /* netw_close(...)*/

/**
 *@brief Make a NETWORK be a Listening network
 *@param netw A NETWORK **network to make listening
 *@param addr Address to bind, NULL for automatic address filling
 *@param port For choosing a PORT to listen to
 *@param nbpending Number of pending connection when listening
 *@param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *@return TRUE on success, FALSE on error
 */
int netw_make_listening(NETWORK** netw, char* addr, char* port, int nbpending, int ip_version) {
    __n_assert(port, return FALSE);

    int error = 0;
    char* errmsg = NULL;

    if (*netw) {
        n_log(LOG_ERR, "Cannot use an allocated network. Please pass a NULL network to modify");
        return FALSE;
    }

    /*checking WSA when under windows*/
    if (netw_init_wsa(1, 2, 2) == FALSE) {
        n_log(LOG_ERR, "Unable to load WSA dll's");
        return FALSE;
    }

    (*netw) = netw_new(MAX_LIST_ITEMS, MAX_LIST_ITEMS);
    __n_assert((*netw), return FALSE);
    (*netw)->link.port = strdup(port);
    /*creating array*/
    if (ip_version == NETWORK_IPV4) {
        (*netw)->link.hints.ai_family = AF_INET; /* Allow IPv4 */
    } else if (ip_version == NETWORK_IPV6) {
        (*netw)->link.hints.ai_family = AF_INET6; /* Allow IPv6 */
    } else {
        /* NETWORK_ALL or unknown value */
        (*netw)->link.hints.ai_family = AF_UNSPEC; /* Allow IPv4 or IPv6 */
    }
    if (!addr) {
        (*netw)->link.hints.ai_flags = AI_PASSIVE; /* For wildcard IP address */
    }
    (*netw)->link.hints.ai_socktype = SOCK_STREAM;
    (*netw)->link.hints.ai_protocol = IPPROTO_TCP;
    (*netw)->link.hints.ai_canonname = NULL;
    (*netw)->link.hints.ai_next = NULL;

    error = getaddrinfo(addr, port, &(*netw)->link.hints, &(*netw)->link.rhost);
    if (error != 0) {
        _netw_capture_error(*netw, "Error when resolving %s:%s getaddrinfo: %s", _str(addr), port, gai_strerror(error));
        n_log(LOG_ERR, "Error when resolving %s:%s getaddrinfo: %s", _str(addr), port, gai_strerror(error));
        netw_close(netw);
        return FALSE;
    }
    (*netw)->addr_infos_loaded = 1;

    /* getaddrinfo() returns a list of address structures.
       Try each address until we successfully connect(2).
       If socket(2) (or connect(2)) fails, we (close the socket
       and) try the next address. */
    struct addrinfo* rp = NULL;
    for (rp = (*netw)->link.rhost; rp != NULL; rp = rp->ai_next) {
        (*netw)->link.sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if ((*netw)->link.sock == INVALID_SOCKET) {
            error = neterrno;
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Error while trying to make a socket: %s", _str(errmsg));
            FreeNoLog(errmsg);
            continue;
        }
        netw_setsockopt((*netw), SO_REUSEADDR, 1);
        // netw_setsockopt( (*netw), SO_REUSEPORT, 1 );
        if (bind((*netw)->link.sock, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            char* ip = NULL;
            Malloc(ip, char, 64);
            if (!ip) {
                n_log(LOG_ERR, "Error allocating 64 bytes for ip");
                netw_close(netw);
                return FALSE;
            }
            if (!inet_ntop(rp->ai_family, get_in_addr(rp->ai_addr), ip, 64)) {
                error = neterrno;
                errmsg = netstrerror(error);
                n_log(LOG_ERR, "inet_ntop: %p , %s", (*netw)->link.raddr, _str(errmsg));
                FreeNoLog(errmsg);
            }
            /*n_log( LOG_DEBUG, "Socket %d successfully binded to %s %s",  (*netw) -> link . sock, _str( ip ) , _str( port ) );*/
            (*netw)->link.ip = ip;
            break; /* Success */
        }
        error = neterrno;
        errmsg = netstrerror(error);
        _netw_capture_error(*netw, "Error from bind() on port %s neterrno: %s", port, errmsg);
        n_log(LOG_ERR, "Error from bind() on port %s neterrno: %s", port, errmsg);
        FreeNoLog(errmsg);
        closesocket((*netw)->link.sock);
    }
    if (rp == NULL) {
        /* No address succeeded */
        _netw_capture_error(*netw, "Couldn't get a socket for listening on port %s", port);
        n_log(LOG_ERR, "Couldn't get a socket for listening on port %s", port);
        netw_close(netw);
        return FALSE;
    }

    /* nb_pending connections*/
    (*netw)->nb_pending = nbpending;
    if (listen((*netw)->link.sock, (*netw)->nb_pending) != 0) {
        error = neterrno;
        errmsg = netstrerror(error);
        _netw_capture_error(*netw, "listen() failed on port %s: %s", port, _str(errmsg));
        n_log(LOG_ERR, "listen() failed on port %s: %s", port, _str(errmsg));
        FreeNoLog(errmsg);
        netw_close(netw);
        return FALSE;
    }

    netw_set((*netw), NETW_SERVER | NETW_RUN | NETW_THR_ENGINE_STOPPED);

    return TRUE;
} /* netw_make_listening(...)*/

/**
 *@brief Create a UDP bound socket for receiving datagrams
 *@param netw A NETWORK **network to bind
 *@param addr Address to bind, NULL for wildcard
 *@param port Port to bind to
 *@param ip_version NETWORK_IPALL for both ipv4 and ipv6 , NETWORK_IPV4 or NETWORK_IPV6
 *@return TRUE on success, FALSE on error
 */
int netw_bind_udp(NETWORK** netw, char* addr, char* port, int ip_version) {
    __n_assert(port, return FALSE);

    int error = 0;
    char* errmsg = NULL;

    if (*netw) {
        n_log(LOG_ERR, "Cannot use an allocated network. Please pass a NULL network to modify");
        return FALSE;
    }

    /*checking WSA when under windows*/
    if (netw_init_wsa(1, 2, 2) == FALSE) {
        n_log(LOG_ERR, "Unable to load WSA dll's");
        return FALSE;
    }

    (*netw) = netw_new(MAX_LIST_ITEMS, MAX_LIST_ITEMS);
    __n_assert((*netw), return FALSE);
    (*netw)->link.port = strdup(port);
    (*netw)->transport_type = NETWORK_UDP;

    /* choose ip version */
    if (ip_version == NETWORK_IPV4) {
        (*netw)->link.hints.ai_family = AF_INET;
    } else if (ip_version == NETWORK_IPV6) {
        (*netw)->link.hints.ai_family = AF_INET6;
    } else {
        (*netw)->link.hints.ai_family = AF_UNSPEC;
    }
    if (!addr) {
        (*netw)->link.hints.ai_flags = AI_PASSIVE;
    }
    (*netw)->link.hints.ai_socktype = SOCK_DGRAM;
    (*netw)->link.hints.ai_protocol = IPPROTO_UDP;
    (*netw)->link.hints.ai_canonname = NULL;
    (*netw)->link.hints.ai_next = NULL;

    error = getaddrinfo(addr, port, &(*netw)->link.hints, &(*netw)->link.rhost);
    if (error != 0) {
        _netw_capture_error(*netw, "Error when resolving %s:%s getaddrinfo: %s", _str(addr), port, gai_strerror(error));
        n_log(LOG_ERR, "Error when resolving %s:%s getaddrinfo: %s", _str(addr), port, gai_strerror(error));
        netw_close(netw);
        return FALSE;
    }
    (*netw)->addr_infos_loaded = 1;

    struct addrinfo* rp = NULL;
    for (rp = (*netw)->link.rhost; rp != NULL; rp = rp->ai_next) {
        (*netw)->link.sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if ((*netw)->link.sock == INVALID_SOCKET) {
            error = neterrno;
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Error while trying to make a UDP socket: %s", _str(errmsg));
            FreeNoLog(errmsg);
            continue;
        }
        netw_setsockopt((*netw), SO_REUSEADDR, 1);
        if (bind((*netw)->link.sock, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            char* ip = NULL;
            Malloc(ip, char, 64);
            if (!ip) {
                n_log(LOG_ERR, "Error allocating 64 bytes for ip");
                netw_close(netw);
                return FALSE;
            }
            if (!inet_ntop(rp->ai_family, get_in_addr(rp->ai_addr), ip, 64)) {
                error = neterrno;
                errmsg = netstrerror(error);
                n_log(LOG_ERR, "inet_ntop: %p , %s", (*netw)->link.raddr, _str(errmsg));
                FreeNoLog(errmsg);
            }
            (*netw)->link.ip = ip;
            break; /* Success */
        }
        error = neterrno;
        errmsg = netstrerror(error);
        _netw_capture_error(*netw, "Error from bind() on UDP port %s neterrno: %s", port, errmsg);
        n_log(LOG_ERR, "Error from bind() on UDP port %s neterrno: %s", port, errmsg);
        FreeNoLog(errmsg);
        closesocket((*netw)->link.sock);
    }
    if (rp == NULL) {
        _netw_capture_error(*netw, "Couldn't get a socket for UDP binding on port %s", port);
        n_log(LOG_ERR, "Couldn't get a socket for UDP binding on port %s", port);
        netw_close(netw);
        return FALSE;
    }

    /* set send/recv to UDP functions */
    (*netw)->send_data = &send_udp_data;
    (*netw)->recv_data = &recv_udp_data;

    netw_set((*netw), NETW_SERVER | NETW_RUN | NETW_THR_ENGINE_STOPPED);

    n_log(LOG_DEBUG, "UDP socket bound to %s:%s", _str((*netw)->link.ip), port);

    return TRUE;
} /* netw_bind_udp(...)*/

/**
 *@brief Connect a UDP socket to a remote host
 *@param netw A NETWORK **network to connect
 *@param host Host or IP to connect to
 *@param port Port to connect to
 *@param ip_version NETWORK_IPALL, NETWORK_IPV4 or NETWORK_IPV6
 *@return TRUE on success, FALSE on error
 */
int netw_connect_udp(NETWORK** netw, char* host, char* port, int ip_version) {
    int error = 0;
    char* errmsg = NULL;

    if ((*netw)) {
        n_log(LOG_ERR, "Unable to allocate (*netw), already existing. You must use empty NETWORK *structs.");
        return FALSE;
    }

    (*netw) = netw_new(MAX_LIST_ITEMS, MAX_LIST_ITEMS);
    __n_assert(netw && (*netw), return FALSE);
    (*netw)->transport_type = NETWORK_UDP;

    if (netw_init_wsa(1, 2, 2) == FALSE) {
        n_log(LOG_ERR, "Unable to load WSA dll's");
        netw_close(netw);
        return FALSE;
    }

    /* choose ip version */
    if (ip_version == NETWORK_IPV4) {
        (*netw)->link.hints.ai_family = AF_INET;
    } else if (ip_version == NETWORK_IPV6) {
        (*netw)->link.hints.ai_family = AF_INET6;
    } else {
        (*netw)->link.hints.ai_family = AF_UNSPEC;
    }

    (*netw)->link.hints.ai_socktype = SOCK_DGRAM;
    (*netw)->link.hints.ai_protocol = IPPROTO_UDP;
    (*netw)->link.hints.ai_flags = AI_PASSIVE;
    (*netw)->link.hints.ai_canonname = NULL;
    (*netw)->link.hints.ai_next = NULL;

    error = getaddrinfo(host, port, &(*netw)->link.hints, &(*netw)->link.rhost);
    if (error != 0) {
        _netw_capture_error(*netw, "Error when resolving %s:%s getaddrinfo: %s", host, port, gai_strerror(error));
        n_log(LOG_ERR, "Error when resolving %s:%s getaddrinfo: %s", host, port, gai_strerror(error));
        netw_close(netw);
        return FALSE;
    }
    (*netw)->addr_infos_loaded = 1;
    Malloc((*netw)->link.ip, char, 64);
    __n_assert((*netw)->link.ip, netw_close(netw); return FALSE);

    struct addrinfo* rp = NULL;
    for (rp = (*netw)->link.rhost; rp != NULL; rp = rp->ai_next) {
        SOCKET sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == INVALID_SOCKET) {
            error = neterrno;
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Error while trying to make a UDP socket: %s", _str(errmsg));
            FreeNoLog(errmsg);
            continue;
        }

        (*netw)->link.sock = sock;

        /* connect() on UDP sets the default destination for send/recv */
        if (connect(sock, rp->ai_addr, (socklen_t)rp->ai_addrlen) == -1) {
            error = neterrno;
            errmsg = netstrerror(error);
            n_log(LOG_INFO, "UDP connecting to %s:%s : %s", host, port, _str(errmsg));
            FreeNoLog(errmsg);
            closesocket(sock);
            (*netw)->link.sock = INVALID_SOCKET;
            continue;
        } else {
            if (!inet_ntop(rp->ai_family, get_in_addr(rp->ai_addr), (*netw)->link.ip, 64)) {
                error = neterrno;
                errmsg = netstrerror(error);
                n_log(LOG_ERR, "inet_ntop: %p , %s", rp, _str(errmsg));
                FreeNoLog(errmsg);
            }
            break; /* Success */
        }
    }
    if (rp == NULL) {
        _netw_capture_error(*netw, "Couldn't connect UDP to %s:%s : no address succeeded", host, port);
        n_log(LOG_ERR, "Couldn't connect UDP to %s:%s : no address succeeded", host, port);
        netw_close(netw);
        return FALSE;
    }

    (*netw)->link.port = strdup(port);
    __n_assert((*netw)->link.port, netw_close(netw); return FALSE);

    /* set send/recv to UDP functions */
    (*netw)->send_data = &send_udp_data;
    (*netw)->recv_data = &recv_udp_data;

    netw_set((*netw), NETW_CLIENT | NETW_RUN | NETW_THR_ENGINE_STOPPED);

    n_log(LOG_DEBUG, "UDP-Connected to %s:%s", (*netw)->link.ip, (*netw)->link.port);

    return TRUE;
} /* netw_connect_udp(...)*/

/**
 *@brief send data via UDP on a connected socket
 *@param netw connected NETWORK
 *@param buf pointer to buffer
 *@param n number of bytes to send
 *@return NETW_SOCKET_ERROR on error, n on success
 */
ssize_t send_udp_data(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SOCKET s = ((NETWORK*)netw)->link.sock;
    int error = 0;
    char* errmsg = NULL;

    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Send of 0 is unsupported.");
        n_log(LOG_ERR, "Send of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }

    ssize_t bs = send(s, buf, NETW_BUFLEN_CAST(n), NETFLAGS);
    error = neterrno;
    if (bs < 0) {
        if (error == ECONNRESET || error == ENOTCONN
#ifdef __windows__
            || error == WSAECONNRESET || error == WSAENOTCONN
#endif
        ) {
            n_log(LOG_DEBUG, "UDP socket %d disconnected !", s);
            return NETW_SOCKET_DISCONNECTED;
        }
        errmsg = netstrerror(error);
        _netw_capture_error((NETWORK*)netw, "UDP Socket %d send Error: %zd , %s", s, bs, _str(errmsg));
        n_log(LOG_ERR, "UDP Socket %d send Error: %zd , %s", s, bs, _str(errmsg));
        FreeNoLog(errmsg);
        return NETW_SOCKET_ERROR;
    }
    return bs;
} /*send_udp_data(...)*/

/**
 *@brief recv data via UDP from a connected socket
 *@param netw connected NETWORK
 *@param buf pointer to buffer
 *@param n max number of bytes to receive
 *@return NETW_SOCKET_ERROR on error, number of bytes received on success
 */
ssize_t recv_udp_data(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SOCKET s = ((NETWORK*)netw)->link.sock;

    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Recv of 0 is unsupported.");
        n_log(LOG_ERR, "Recv of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }

    ssize_t br = recv(s, buf, NETW_BUFLEN_CAST(n), NETFLAGS);
    int error = neterrno;
    if (br < 0) {
        char* errmsg = netstrerror(error);
        _netw_capture_error((NETWORK*)netw, "UDP socket %d recv returned %zd, error: %s", s, br, _str(errmsg));
        n_log(LOG_ERR, "UDP socket %d recv returned %zd, error: %s", s, br, _str(errmsg));
        FreeNoLog(errmsg);
        return NETW_SOCKET_ERROR;
    }
    if (br == 0) {
        n_log(LOG_DEBUG, "UDP socket %d: zero-length recv", s);
    }
    return br;
} /*recv_udp_data(...)*/

/**
 *@brief send data via UDP to a specific destination address
 *@param netw NETWORK to use for sending
 *@param buf pointer to buffer
 *@param n number of bytes to send
 *@param dest_addr destination sockaddr
 *@param dest_len length of dest_addr
 *@return NETW_SOCKET_ERROR on error, bytes sent on success
 */
ssize_t netw_udp_sendto(NETWORK* netw, char* buf, uint32_t n, struct sockaddr* dest_addr, socklen_t dest_len) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);
    __n_assert(dest_addr, return NETW_SOCKET_ERROR);

    if (n == 0) {
        _netw_capture_error(netw, "Sendto of 0 is unsupported.");
        n_log(LOG_ERR, "Sendto of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }

    ssize_t bs = sendto(netw->link.sock, buf, NETW_BUFLEN_CAST(n), NETFLAGS, dest_addr, dest_len);
    if (bs < 0) {
        int error = neterrno;
        char* errmsg = netstrerror(error);
        _netw_capture_error(netw, "UDP socket %d sendto Error: %zd , %s", netw->link.sock, bs, _str(errmsg));
        n_log(LOG_ERR, "UDP socket %d sendto Error: %zd , %s", netw->link.sock, bs, _str(errmsg));
        FreeNoLog(errmsg);
        return NETW_SOCKET_ERROR;
    }
    return bs;
} /*netw_udp_sendto(...)*/

/**
 *@brief recv data via UDP and capture the source address
 *@param netw NETWORK to use for receiving
 *@param buf pointer to buffer
 *@param n max number of bytes to receive
 *@param src_addr pointer to store source address (can be NULL)
 *@param src_len pointer to length of src_addr (can be NULL)
 *@return NETW_SOCKET_ERROR on error, bytes received on success
 */
ssize_t netw_udp_recvfrom(NETWORK* netw, char* buf, uint32_t n, struct sockaddr* src_addr, socklen_t* src_len) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    if (n == 0) {
        _netw_capture_error(netw, "Recvfrom of 0 is unsupported.");
        n_log(LOG_ERR, "Recvfrom of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }

    ssize_t br = recvfrom(netw->link.sock, buf, NETW_BUFLEN_CAST(n), NETFLAGS, src_addr, src_len);
    if (br < 0) {
        int error = neterrno;
        char* errmsg = netstrerror(error);
        _netw_capture_error(netw, "UDP socket %d recvfrom returned %zd, error: %s", netw->link.sock, br, _str(errmsg));
        n_log(LOG_ERR, "UDP socket %d recvfrom returned %zd, error: %s", netw->link.sock, br, _str(errmsg));
        FreeNoLog(errmsg);
        return NETW_SOCKET_ERROR;
    }
    return br;
} /*netw_udp_recvfrom(...)*/

/**
 *@brief make a normal 'accept' . Network 'from' must be allocated with netw_make_listening.
 *@param from the network from where we accept
 *@param send_list_limit Internal sending list maximum number of item. 0 for unrestricted
 *@param recv_list_limit Internal receiving list maximum number of item. 0 for unrestricted
 *@param blocking set to -1 to make it non blocking, to 0 for blocking, else it's the select timeout value in msecs.
 *@param retval EAGAIN ou EWOULDBLOCK or neterrno (use netstrerr( retval) to obtain a string describing the code )
 *@return NULL on failure, if not a pointer to the connected network
 */
NETWORK* netw_accept_from_ex(NETWORK* from, size_t send_list_limit, size_t recv_list_limit, int blocking, int* retval) {
    SOCKET tmp = INVALID_SOCKET;
    int error;
    char* errmsg = NULL;

#if defined(__linux__) || defined(__sun) || defined(_AIX)
    socklen_t sin_size = 0;
#else
    int sin_size = 0;
#endif

    NETWORK* netw = NULL;

    /*checking WSA when under windows*/
    if (netw_init_wsa(1, 2, 2) == FALSE) {
        n_log(LOG_ERR, "Unable to load WSA dll's");
        return NULL;
    }

    __n_assert(from, return NULL);

    netw = netw_new(send_list_limit, recv_list_limit);

    sin_size = sizeof(netw->link.raddr);

    if (blocking > 0) {
        int secs = blocking / 1000;
        int usecs = (blocking % 1000) * 1000;
        struct timeval select_timeout = {secs, usecs};

        fd_set accept_set;
        FD_ZERO(&accept_set);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
        FD_SET(from->link.sock, &accept_set);
#pragma GCC diagnostic pop

        int ret = select((int)(from->link.sock + 1), &accept_set, NULL, NULL, &select_timeout);
        if (ret == -1) {
            error = neterrno;
            errmsg = netstrerror(error);
            if (retval != NULL)
                (*retval) = error;
            n_log(LOG_DEBUG, "error on select with timeout %ds (%d.%ds), neterrno: %s", blocking, secs, usecs, _str(errmsg));
            FreeNoLog(errmsg);
            netw_close(&netw);
            return NULL;
        } else if (ret == 0) {
            /* that one produce waaaay too much logs under a lot of cases */
            n_log(LOG_DEBUG, "No connection waiting on %d", from->link.sock);
            netw_close(&netw);
            return NULL;
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
        if (FD_ISSET(from->link.sock, &accept_set)) {
#pragma GCC diagnostic pop
            // n_log( LOG_DEBUG, "select accept call on %d", from -> link . sock );
            tmp = accept(from->link.sock, (struct sockaddr*)&netw->link.raddr, &sin_size);
            if (tmp == INVALID_SOCKET) {
                error = neterrno;
                errmsg = netstrerror(error);
                if (retval != NULL)
                    (*retval) = error;
                n_log(LOG_DEBUG, "error accepting on %d, %s", netw->link.sock, _str(errmsg));
                FreeNoLog(errmsg);
                netw_close(&netw);
                return NULL;
            }
        } else {
            _netw_capture_error(from, "FD_ISSET returned false on %d", from->link.sock);
            n_log(LOG_ERR, "FD_ISSET returned false on %d", from->link.sock);
            netw_close(&netw);
            return NULL;
        }
        netw->link.is_blocking = 0;
    } else if (blocking == -1) {
        if (from->link.is_blocking != 0) {
            netw_set_blocking(from, 0);
        }
        tmp = accept(from->link.sock, (struct sockaddr*)&netw->link.raddr, &sin_size);
        error = neterrno;
        if (retval != NULL)
            (*retval) = error;
        if (tmp == INVALID_SOCKET) {
            if (error != EINTR && error != EAGAIN
#ifdef __windows__
                && error != WSAEWOULDBLOCK
#endif
            ) {
                char* errmsg_nb = netstrerror(error);
                // cppcheck-suppress unknownMacro ; SOCKET_SIZE_FORMAT is a platform-specific printf format macro
                n_log(LOG_ERR, "accept returned an invalid socket (" SOCKET_SIZE_FORMAT "), neterrno: %s", tmp, _str(errmsg_nb));
                FreeNoLog(errmsg_nb);
            }
            netw_close(&netw);
            return NULL;
        }
        netw->link.is_blocking = 0;
    } else {
        if (from->link.is_blocking == 0) {
            netw_set_blocking(from, 1);
            n_log(LOG_DEBUG, "(default) blocking accept call on socket %d", from->link.sock);
        }
        tmp = accept(from->link.sock, (struct sockaddr*)&netw->link.raddr, &sin_size);
        if (tmp == INVALID_SOCKET) {
            error = neterrno;
            errmsg = netstrerror(error);
            if (retval != NULL)
                (*retval) = error;
            n_log(LOG_DEBUG, "error accepting on socket %d, %s", netw->link.sock, _str(errmsg));
            FreeNoLog(errmsg);
            netw_close(&netw);
            return NULL;
        }
        netw->link.is_blocking = 1;
    }
    netw->link.sock = tmp;

    /* On Windows and Solaris, the accepted socket inherits the non-blocking
     * mode from the listening socket.  The send/recv engine expects a
     * blocking socket, so force it to blocking here.  On Linux this is a
     * harmless no-op since accept() always returns a blocking socket. */
    netw_set_blocking(netw, 1);
    netw->link.is_blocking = 1;

    netw->link.port = strdup(from->link.port);
    Malloc(netw->link.ip, char, 64);
    if (!netw->link.ip) {
        n_log(LOG_ERR, "Error allocating 64 bytes for ip");
        netw_close(&netw);
        return NULL;
    }
    if (!inet_ntop(netw->link.raddr.ss_family, get_in_addr(((struct sockaddr*)&netw->link.raddr)), netw->link.ip, 64)) {
        error = neterrno;
        errmsg = netstrerror(error);
        n_log(LOG_ERR, "inet_ntop: %p , %s", netw->link.raddr, _str(errmsg));
        FreeNoLog(errmsg);
        netw_close(&netw);
        return NULL;
    }

    netw_setsockopt(netw, SO_REUSEADDR, 1);
    // netw_setsockopt( netw, SO_REUSEPORT, 1 );
    netw_setsockopt(netw, SO_KEEPALIVE, 1);
    netw_set(netw, NETW_SERVER | NETW_RUN | NETW_THR_ENGINE_STOPPED);
    // netw_set_blocking(netw, 1);

    n_log(LOG_DEBUG, "Connection accepted from %s:%s socket %d", netw->link.ip, netw->link.port, netw->link.sock);

#ifdef HAVE_OPENSSL
    if (from->crypto_algo == NETW_ENCRYPT_OPENSSL) {
        netw->ssl = SSL_new(from->ctx);
        SSL_set_fd(netw->ssl, (int)netw->link.sock);

        netw->send_data = &send_ssl_data;
        netw->recv_data = &recv_ssl_data;
        netw->send_data_once = &send_ssl_data_once;
        netw->recv_data_once = &recv_ssl_data_once;
        /* mark accepted netw as SSL so netw_close() runs SSL_free on its ssl;
         * ctx stays NULL, it is borrowed from the listener and freed there */
        netw->crypto_algo = NETW_ENCRYPT_OPENSSL;

        if (SSL_accept(netw->ssl) <= 0) {
            error = errno;
            _netw_capture_error(netw, "SSL error on %d", netw->link.sock);
            n_log(LOG_ERR, "SSL error on %d", netw->link.sock);
            if (retval != NULL)
                (*retval) = error;
            netw_ssl_print_errors(netw->link.sock);
            netw_close(&netw);
            return NULL;
        } else {
            n_log(LOG_DEBUG, " socket %d: SSL connection established", netw->link.sock);
        }
    }
#endif

    return netw;
} /* netw_accept_from_ex(...) */

/**
 *@brief make a normal blocking 'accept' . Network 'from' must be allocated with netw_make_listening.
 *@param from The network from which to obtaion the connection
 *@return NULL
 */
NETWORK* netw_accept_from(NETWORK* from) {
    return netw_accept_from_ex(from, MAX_LIST_ITEMS, MAX_LIST_ITEMS, 0, NULL);
} /* network_accept_from( ... ) */

/**
 *@brief make a normal blocking 'accept' . Network 'from' must be allocated with netw_make_listening.
 *@param from The network from which to obtaion the connection
 *@param blocking set to -1 to make it non blocking, to 0 for blocking, else it's the select timeout value in mseconds.
 *@return NULL
 */
NETWORK* netw_accept_nonblock_from(NETWORK* from, int blocking) {
    return netw_accept_from_ex(from, MAX_LIST_ITEMS, MAX_LIST_ITEMS, blocking, NULL);
} /* network_accept_from( ... ) */

/**
 *@brief Add a message to send in aimed NETWORK
 *@param netw NETWORK where add the message
 *@param msg  the message to add
 *@return TRUE if success FALSE on error
 */
/* Process-wide byte counters. See netw_bytes_stats_get in
 * n_network_msg.c. Advisory telemetry; no atomic guarantee. */
long long g_netw_bytes_sent = 0;
long long g_netw_bytes_recv = 0;

int netw_add_msg(NETWORK* netw, N_STR* msg) {
    __n_assert(netw, return FALSE);
    __n_assert(msg, return FALSE);
    __n_assert(msg->data, return FALSE);

    if (msg->length == 0) {
        _netw_capture_error(netw, "Empty messages are not supported. msg(%p)->lenght=%d", msg, msg->length);
        n_log(LOG_ERR, "Empty messages are not supported. msg(%p)->lenght=%d", msg, msg->length);
        return FALSE;
    }

    /* Capture msg->written for the byte counter before the push:
     * list_push transfers ownership to send_buf with free_nstr_ptr
     * as the destructor, so once we release sendbolt the consumer
     * (send thread or reactor) can shift, send, and free `msg`,
     * making any post-unlock `msg->written` read a use-after-free.
     * The race is mostly latent in thread mode (send thread blocks
     * on the syscall before freeing) but the reactor consumes within
     * microseconds and TSan flags it consistently. */
    long long bytes_for_counter = (long long)msg->written;

    pthread_mutex_lock(&netw->sendbolt);

    if (list_push(netw->send_buf, msg, free_nstr_ptr) == FALSE) {
        pthread_mutex_unlock(&netw->sendbolt);
        return FALSE;
    }

    pthread_mutex_unlock(&netw->sendbolt);

    /* Reactor mode: wake the reactor's epoll_wait via its
     * wake-eventfd so it picks up the new send_buf entry. The
     * thread engine's sem_post path stays as the default; the wake
     * call is a single non-blocking eventfd write when reactor_mode
     * is set. Forward declared in n_reactor.h to avoid a circular
     * include with this header, the function pointer dispatch
     * happens through netw->reactor_handle.
     *
     * Compile-time gated on N_REACTOR_AVAILABLE: on Windows / Solaris
     * the reactor is unavailable, reactor_mode is always 0, and we
     * never want this TU to reference n_reactor_notify_send (which
     * would force linking n_reactor.o for a code path that can never
     * fire). The unconditional sem_post fallback matches the
     * non-reactor branch of the gated form. */
#if N_REACTOR_AVAILABLE
    if (netw_atomic_read_reactor_mode(netw) && netw_atomic_read_reactor_handle(netw)) {
        extern void n_reactor_notify_send(NETWORK*);
        n_reactor_notify_send(netw);
    } else {
        sem_post(&netw->send_blocker);
    }
#else
    sem_post(&netw->send_blocker);
#endif

    g_netw_bytes_sent += bytes_for_counter;
    return TRUE;
} /* netw_add_msg(...) */

/**
 *@brief Add a message to send in aimed NETWORK
 *@param netw NETWORK where add the message
 *@param str  the message to add
 *@param length  the size of the message to add
 *@return TRUE if success FALSE on error
 */
int netw_add_msg_ex(NETWORK* netw, char* str, unsigned int length) {
    __n_assert(netw, return FALSE);
    __n_assert(str, return FALSE);
    if (length == 0) {
        return FALSE;
    }

    N_STR* nstr = NULL;
    Malloc(nstr, N_STR, 1);
    __n_assert(nstr, return FALSE);

    nstr->data = str;
    nstr->written = nstr->length = length;

    pthread_mutex_lock(&netw->sendbolt);
    if (list_push(netw->send_buf, nstr, free_nstr_ptr) == FALSE) {
        pthread_mutex_unlock(&netw->sendbolt);
        return FALSE;
    }
    pthread_mutex_unlock(&netw->sendbolt);

    sem_post(&netw->send_blocker);

    return TRUE;
} /* netw_add_msg_ex(...) */

/**
 *@brief Get a message from aimed NETWORK
 *@param netw NETWORK where get the msg
 *@return NULL or a filled N_STR
 */
N_STR* netw_get_msg(NETWORK* netw) {
    N_STR* ptr = NULL;

    __n_assert(netw, return NULL);

    pthread_mutex_lock(&netw->recvbolt);

    ptr = list_shift(netw->recv_buf, N_STR);

    pthread_mutex_unlock(&netw->recvbolt);

    if (ptr) g_netw_bytes_recv += (long long)ptr->written;
    return ptr;
} /* netw_get_msg(...)*/

/**
 *@brief Wait a message from aimed NETWORK. Recheck each usec until a valid
 *@param netw The link on which we wait a message
 *@param refresh The time in usec between each check until there is a message
 *@param timeout in usecs , maximum amount of time to wait before return. 0 to disable.
 *@return NULL or a filled N_STR
 */
N_STR* netw_wait_msg(NETWORK* netw, unsigned int refresh, size_t timeout) {
    size_t timed = 0;
    unsigned int secs = 0;
    unsigned int usecs = 0;

    __n_assert(netw, return NULL);

    usecs = refresh;
    if (refresh > 999999) {
        secs = refresh / 1000000;
        usecs = refresh % 1000000;
    }

    if (timeout > 0)
        timed = timeout;

    n_log(LOG_DEBUG, "wait from socket %d, refresh: %zu usec (%zu secs, %zu usecs), timeout %zu usec", netw->link.sock, refresh, secs, usecs, timeout);
    uint32_t state = NETW_RUN;
    int thr_state = 0;
    do {
        N_STR* nstrptr = netw_get_msg(netw);
        if (nstrptr)
            return nstrptr;

        if (timeout > 0) {
            if (timed >= refresh)
                timed -= refresh;
            if (timed == 0 || timed < refresh) {
                _netw_capture_error(netw, "netw %d, status: %s (%" PRIu32 "), timeouted after waiting %zu msecs", netw->link.sock, N_ENUM_ENTRY(__netw_code_type, toString)(state), state, timeout);
                n_log(LOG_ERR, "netw %d, status: %s (%" PRIu32 "), timeouted after waiting %zu msecs", netw->link.sock, N_ENUM_ENTRY(__netw_code_type, toString)(state), state, timeout);
                return NULL;
            }
        }
        if (secs > 0)
            sleep(secs);
        if (usecs > 0)
            u_sleep(usecs);

        netw_get_state(netw, &state, &thr_state);
    } while (state != NETW_EXITED && state != NETW_ERROR);

    _netw_capture_error(netw, "got no answer and netw %d is no more running, state: %s (%" PRIu32 ")", netw->link.sock, N_ENUM_ENTRY(__netw_code_type, toString)(state), state);
    n_log(LOG_ERR, "got no answer and netw %d is no more running, state: %s (%" PRIu32 ")", netw->link.sock, N_ENUM_ENTRY(__netw_code_type, toString)(state), state);

    return NULL;
} /* netw_wait_msg(...) */

/**
 *@brief Start the NETWORK netw Threaded Engine. Create a sending & receiving thread.
 *@param netw The aimed NETWORK connection to start receiving data
 *@return TRUE on success FALSE on failure
 */
int netw_start_thr_engine(NETWORK* netw) {
    __n_assert(netw, return FALSE);

    pthread_mutex_lock(&netw->eventbolt);
    if (netw->threaded_engine_status == NETW_THR_ENGINE_STARTED) {
        _netw_capture_error(netw, "THR Engine already started for network %p (%s)", netw, _str(netw->link.ip));
        n_log(LOG_ERR, "THR Engine already started for network %p (%s)", netw, _str(netw->link.ip));
        pthread_mutex_unlock(&netw->eventbolt);
        return FALSE;
    }

    if (pthread_create(&netw->recv_thr, NULL, netw_recv_func, (void*)netw) != 0) {
        _netw_capture_error(netw, "Unable to create recv_thread for network %p (%s)", netw, _str(netw->link.ip));
        n_log(LOG_ERR, "Unable to create recv_thread for network %p (%s)", netw, _str(netw->link.ip));
        pthread_mutex_unlock(&netw->eventbolt);
        return FALSE;
    }
    netw->nb_running_threads++;
    if (pthread_create(&netw->send_thr, NULL, netw_send_func, (void*)netw) != 0) {
        _netw_capture_error(netw, "Unable to create send_thread for network %p (%s)", netw, _str(netw->link.ip));
        n_log(LOG_ERR, "Unable to create send_thread for network %p (%s)", netw, _str(netw->link.ip));
        pthread_cancel(netw->recv_thr);
        pthread_join(netw->recv_thr, NULL);
        netw->nb_running_threads--;
        pthread_mutex_unlock(&netw->eventbolt);
        return FALSE;
    }
    netw->nb_running_threads++;

    netw->threaded_engine_status = NETW_THR_ENGINE_STARTED;

    pthread_mutex_unlock(&netw->eventbolt);

    return TRUE;
} /* netw_create_recv_thread(....) */

/**
 *@brief Thread send function
 *@param NET the NETWORK connection to use, casted into (void*)
 *@return NULL
 */
void* netw_send_func(void* NET) {
    int DONE = 0;

    ssize_t net_status = 0;

    uint32_t state;
    uint32_t nboctet;

    char nboct[5] = "";

    N_STR* ptr = NULL;

    NETWORK* netw = (NETWORK*)NET;
    __n_assert(netw, return NULL);

    do {
        /* do not consume cpu for nothing, reduce delay */
        sem_wait(&netw->send_blocker);
        int message_sent = 0;
        while (message_sent == 0 && !DONE) {
            state = netw_atomic_read_state(netw);
            if (state & NETW_ERROR) {
                DONE = NETW_THR_EXIT_ERROR;
            } else if (state & NETW_EXITED) {
                DONE = NETW_THR_EXIT_OK;
            } else if (state & NETW_EXIT_ASKED) {
                DONE = NETW_THR_EXIT_OK;
                /* sending state */
                nboctet = htonl(NETW_EXIT_ASKED);
                memcpy(nboct, &nboctet, sizeof(uint32_t));
                n_log(LOG_DEBUG, "%d Sending Quit !", netw->link.sock);
                net_status = netw->send_data(netw, nboct, sizeof(int32_t));
                /* Peer disconnect during QUIT send is the expected
                 * end of the handshake; only a real socket error
                 * deserves the DONE=4 error path. */
                if (net_status < 0 && net_status != NETW_SOCKET_DISCONNECTED)
                    DONE = 4;
                n_log(LOG_DEBUG, "%d Quit sent!", netw->link.sock);
            } else {
                pthread_mutex_lock(&netw->sendbolt);
                ptr = list_shift(netw->send_buf, N_STR);
                pthread_mutex_unlock(&netw->sendbolt);
                if (ptr && ptr->length > 0 && ptr->data) {
                    /* Headroom for the 8-byte state+length header so
                     * frame_len below cannot overflow the uint32 the
                     * send_data API takes. */
                    if (ptr->written <= UINT_MAX - 2 * sizeof(uint32_t)) {
                        n_log(LOG_DEBUG, "Sending ptr size %zu written %zu...", ptr->length, ptr->written);

                        /* Opportunistic compression. Gated on threshold
                         * + ratio so small packets and incompressible
                         * binary snapshots don't pay the codec cost.
                         * Algorithm picked per-connection via
                         * netw_set_compression_mode(); NONE skips the
                         * entire attempt. */
                        uint32_t pkt_state = state;
                        if (netw->compress_mode != NETW_COMPRESS_NONE &&
                            ptr->written >= NETW_COMPRESS_THRESHOLD) {
                            N_STR* zipped = NULL;
                            uint32_t flag_bit = 0;
                            if (netw->compress_mode == NETW_COMPRESS_LZ4) {
                                zipped = zip4_nstr(ptr);
                                flag_bit = NETW_COMPRESSED_LZ4;
                            } else {
                                zipped = zip_nstr(ptr);
                                flag_bit = NETW_COMPRESSED_ZLIB;
                            }
                            if (zipped && zipped->written > 0 &&
                                zipped->written * 100u <=
                                    ptr->written * (100u - NETW_COMPRESS_MIN_RATIO)) {
                                /* Good enough shrink, swap. */
                                free_nstr(&ptr);
                                ptr = zipped;
                                pkt_state |= flag_bit;
                            } else if (zipped) {
                                free_nstr(&zipped);
                            }
                        }

                        /* Single-write framing. The frame
                         * used to go out as three separate send_data
                         * calls (4B state, 4B length, payload): three
                         * syscalls, and three TLS records on crypto
                         * sockets, per message, with the two 4-byte
                         * writes interacting badly with Nagle +
                         * delayed-ACK on small-message streams (game
                         * inputs, snapshots). Build the contiguous
                         * frame and hand the kernel one write.
                         * Peer disconnect (NETW_SOCKET_DISCONNECTED)
                         * during the send is a normal end-of-life: the
                         * connection is over, nothing to log. Only a
                         * real socket error trips the DONE=1 error
                         * path so the tail logger emits LOG_ERR. */
                        size_t frame_len = 2 * sizeof(uint32_t) + ptr->written;
                        char* frame = NULL;
                        Malloc(frame, char, frame_len);
                        if (frame) {
                            nboctet = htonl(pkt_state);
                            memcpy(frame, &nboctet, sizeof(uint32_t));
                            nboctet = htonl((uint32_t)ptr->written);
                            memcpy(frame + sizeof(uint32_t), &nboctet, sizeof(uint32_t));
                            memcpy(frame + 2 * sizeof(uint32_t), ptr->data, ptr->written);
                            net_status = netw->send_data(netw, frame, (uint32_t)frame_len);
                            if (net_status < 0)
                                DONE = (net_status == NETW_SOCKET_DISCONNECTED) ? NETW_THR_EXIT_OK : 1;
                            Free(frame);
                        } else {
                            /* OOM building the frame: drop the message
                             * (stream alignment is preserved, nothing
                             * was written) and keep the connection up,
                             * mirroring the oversize-discard branch. */
                            _netw_capture_error(netw, "could not allocate %zu byte send frame, packet dropped", frame_len);
                            n_log(LOG_ERR, "could not allocate %zu byte send frame, packet dropped", frame_len);
                        }
                        if (netw->send_queue_consecutive_wait >= 0) {
                            u_sleep((unsigned int)netw->send_queue_consecutive_wait);
                        }
                        message_sent = 1;
                        free_nstr(&ptr);
                    } else {
                        _netw_capture_error(netw, "discarded packet of size %zu which is greater than %" PRIu32, ptr->written, UINT_MAX);
                        n_log(LOG_ERR, "discarded packet of size %zu which is greater than %" PRIu32, ptr->written, UINT_MAX);
                        message_sent = 1;
                        free_nstr(&ptr);
                    }
                } else {
                    /* Empty send_buf or malformed head entry: the
                     * producer-consumer contract means sem_post was
                     * called without a usable list_push, or a state
                     * transition posted without a real message. Break
                     * out of the inner loop and block on sem_wait
                     * rather than spinning, spinning grabs sendbolt
                     * at MHz rates and creates main-thread contention
                     * in netw_add_msg. */
                    if (ptr) free_nstr(&ptr);
                    message_sent = 1;
                }
            }
        }
    } while (!DONE);

    if (DONE == 1) {
        _netw_capture_error(netw, "Error when sending state %" PRIu32 " on socket %d (%s), network: %s", netw_atomic_read_state(netw), netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error when sending state %" PRIu32 " on socket %d (%s), network: %s", netw_atomic_read_state(netw), netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 2) {
        _netw_capture_error(netw, "Error when sending number of octet to socket %d (%s), network: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error when sending number of octet to socket %d (%s), network: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 3)
        n_log(LOG_DEBUG, "Error when sending data on socket %d (%s), network: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    else if (DONE == 4) {
        _netw_capture_error(netw, "Error when sending state QUIT on socket %d (%s), network: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error when sending state QUIT on socket %d (%s), network: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 5) {
        _netw_capture_error(netw, "Error when sending state QUIT number of octet (0) on socket %d (%s), network: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error when sending state QUIT number of octet (0) on socket %d (%s), network: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    }

    if (DONE == NETW_THR_EXIT_OK) {
        n_log(LOG_DEBUG, "Socket %d: Sending thread exiting correctly", netw->link.sock);
        netw_set(netw, NETW_EXIT_ASKED);
    } else {
        _netw_capture_error(netw, "Socket %d (%s): Sending thread exiting with error %d !", netw->link.sock, _str(netw->link.ip), DONE);
        n_log(LOG_ERR, "Socket %d (%s): Sending thread exiting with error %d !", netw->link.sock, _str(netw->link.ip), DONE);
        netw_set(netw, NETW_ERROR);
    }

    pthread_mutex_lock(&netw->eventbolt);
    netw->nb_running_threads--;
    pthread_mutex_unlock(&netw->eventbolt);

    pthread_exit(0);

    /* suppress compiler warning */
#if !defined(__linux__)
    return NULL;
#endif
} /* netw_send_func(...) */

/**
 *@brief To Thread Receiving function
 *@param NET the NETWORK connection to use
 *@return NULL ;
 */
void* netw_recv_func(void* NET) {
    int DONE = 0;
    ssize_t net_status = 0;

    uint32_t nboctet;
    uint32_t tmpstate;
    uint32_t state;

    char nboct[5] = "";

    N_STR* recvdmsg = NULL;

    NETWORK* netw = (NETWORK*)NET;

    __n_assert(netw, return NULL);

    do {
        state = netw_atomic_read_state(netw);
        if (state & NETW_EXIT_ASKED || state & NETW_EXITED) {
            DONE = NETW_THR_EXIT_OK;
        }
        if (state & NETW_ERROR) {
            DONE = NETW_THR_EXIT_ERROR;
        }
        if (!DONE) {
            n_log(LOG_DEBUG, "socket %d : waiting to receive status", netw->link.sock);
            /* receiving state */
            net_status = netw->recv_data(netw, nboct, sizeof(uint32_t));
            if (net_status < 0) {
                /* Peer disconnect (NETW_SOCKET_DISCONNECTED) is the
                 * normal end of a connection, no log noise, exit
                 * via the clean NETW_THR_EXIT_OK path so the
                 * application observes NETW_EXIT_ASKED, not
                 * NETW_ERROR. Only NETW_SOCKET_ERROR routes through
                 * the DONE=N error tail logger. */
                DONE = (net_status == NETW_SOCKET_DISCONNECTED) ? NETW_THR_EXIT_OK : 1;
            } else {
                memcpy(&nboctet, nboct, sizeof(uint32_t));
                tmpstate = ntohl(nboctet);
                nboctet = tmpstate;
                /* Freeze the per-packet state word before the next
                 * recv_data call reuses tmpstate for the payload
                 * length, the NETW_COMPRESSED_* flag check below
                 * reads pkt_state, not tmpstate. */
                uint32_t pkt_state = tmpstate;
                if (tmpstate == NETW_EXIT_ASKED) {
                    n_log(LOG_DEBUG, "socket %d : receiving order to QUIT !", netw->link.sock);
                    DONE = NETW_THR_EXIT_OK;
                } else {
                    n_log(LOG_DEBUG, "socket %d : waiting to receive next message nboctets", netw->link.sock);
                    /* receiving nboctet */
                    net_status = netw->recv_data(netw, nboct, sizeof(uint32_t));
                    if (net_status < 0) {
                        DONE = (net_status == NETW_SOCKET_DISCONNECTED) ? NETW_THR_EXIT_OK : 2;
                    } else {
                        memcpy(&nboctet, nboct, sizeof(uint32_t));
                        tmpstate = ntohl(nboctet);
                        nboctet = tmpstate;

                        Malloc(recvdmsg, N_STR, 1);
                        if (!recvdmsg) {
                            DONE = 3;
                        } else {
                            n_log(LOG_DEBUG, "socket %d : %" PRIu32 " octets to receive...", netw->link.sock, nboctet);
                            Malloc(recvdmsg->data, char, nboctet + 1);
                            if (!recvdmsg->data) {
                                free_nstr(&recvdmsg);
                                DONE = 4;
                            } else {
                                recvdmsg->length = nboctet + 1;
                                recvdmsg->written = nboctet;

                                /* receiving the data itself */
                                net_status = netw->recv_data(netw, recvdmsg->data, nboctet);
                                if (net_status < 0) {
                                    free_nstr(&recvdmsg);
                                    DONE = (net_status == NETW_SOCKET_DISCONNECTED) ? NETW_THR_EXIT_OK : 5;
                                } else {
                                    /* Inverse of the compression step on
                                     * the send side. Each NETW_COMPRESSED_*
                                     * bit identifies the codec the sender
                                     * chose; we decode regardless of our
                                     * own compress_mode so two peers
                                     * running different algorithms still
                                     * interop. */
                                    int want_zlib = (pkt_state & NETW_COMPRESSED_ZLIB) != 0;
                                    int want_lz4 = (pkt_state & NETW_COMPRESSED_LZ4) != 0;
                                    if (want_zlib || want_lz4) {
                                        recvdmsg->data[nboctet] = '\0';
                                        N_STR* plain = want_lz4
                                                           ? unzip4_nstr(recvdmsg)
                                                           : unzip_nstr(recvdmsg);
                                        if (plain) {
                                            free_nstr(&recvdmsg);
                                            recvdmsg = plain;
                                        } else {
                                            n_log(LOG_ERR,
                                                  "socket %d : failed to decompress payload (%" PRIu32 " bytes, codec=%s); dropping",
                                                  netw->link.sock, nboctet,
                                                  want_lz4 ? "lz4" : "zlib");
                                            free_nstr(&recvdmsg);
                                            DONE = 7;
                                        }
                                    }
                                    if (!DONE) {
                                        pthread_mutex_lock(&netw->recvbolt);
                                        if (list_push(netw->recv_buf, recvdmsg, free_nstr_ptr) == FALSE)
                                            DONE = 6;
                                        pthread_mutex_unlock(&netw->recvbolt);
                                        n_log(LOG_DEBUG, "socket %d : %" PRIu32 " octets received !", netw->link.sock, nboctet);
                                    }
                                } /* recv data */
                            } /* recv data allocation */
                        } /* recv struct allocation */
                    } /* recv nb octet*/
                } /* exit asked */
            } /* recv state */
        } /* if( !done) */
    } while (!DONE);

    if (DONE == 1) {
        _netw_capture_error(netw, "Error when receiving state from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error when receiving state from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 2) {
        _netw_capture_error(netw, "Error when receiving nboctet from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error when receiving nboctet from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 3) {
        _netw_capture_error(netw, "Error when receiving data from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error when receiving data from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 4) {
        _netw_capture_error(netw, "Error allocating received message struct from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error allocating received message struct from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 5) {
        _netw_capture_error(netw, "Error allocating received messages data array from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error allocating received messages data array from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    } else if (DONE == 6) {
        _netw_capture_error(netw, "Error adding receved message from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
        n_log(LOG_ERR, "Error adding receved message from socket %d (%s), net_status: %s", netw->link.sock, _str(netw->link.ip), (net_status == NETW_SOCKET_DISCONNECTED) ? "disconnected" : "socket error");
    }

    if (DONE == NETW_THR_EXIT_OK) {
        n_log(LOG_DEBUG, "Socket %d (%s): Receive thread exiting correctly", netw->link.sock, _str(netw->link.ip));
        netw_set(netw, NETW_EXIT_ASKED);
    } else {
        _netw_capture_error(netw, "Socket %d (%s): Receive thread exiting with code %d !", netw->link.sock, _str(netw->link.ip), DONE);
        n_log(LOG_ERR, "Socket %d (%s): Receive thread exiting with code %d !", netw->link.sock, _str(netw->link.ip), DONE);
        netw_set(netw, NETW_ERROR);
    }

    pthread_mutex_lock(&netw->eventbolt);
    netw->nb_running_threads--;
    pthread_mutex_unlock(&netw->eventbolt);

    pthread_exit(0);

    /* suppress compiler warning */
#if !defined(__linux__)
    return NULL;
#endif
} /* netw_recv_func(...) */

/**
 *@brief Stop a NETWORK connection sending and receing thread
 *@param netw The aimed NETWORK conection to stop
 *@return TRUE on success FALSE on failure
 */
int netw_stop_thr_engine(NETWORK* netw) {
    __n_assert(netw, return FALSE);
    uint32_t state = 0;
    int thr_engine_status = 0;

    n_log(LOG_DEBUG, "Network %d stop threads event", netw->link.sock);

    netw_get_state(netw, &state, &thr_engine_status);

    if (thr_engine_status != NETW_THR_ENGINE_STARTED) {
        _netw_capture_error(netw, "Thread engine status already stopped for network %p", netw);
        n_log(LOG_ERR, "Thread engine status already stopped for network %p", netw);
        return FALSE;
    }

    netw_set(netw, NETW_EXIT_ASKED);

    n_log(LOG_DEBUG, "Network %d waits for send threads to stop", netw->link.sock);
    for (int it = 0; it < 10; it++) {
        sem_post(&netw->send_blocker);
        usleep(1000);
    }
    pthread_join(netw->send_thr, NULL);
    n_log(LOG_DEBUG, "Network %d waits for recv threads to stop", netw->link.sock);
    pthread_join(netw->recv_thr, NULL);

    netw_set(netw, NETW_EXITED | NETW_THR_ENGINE_STOPPED);

    n_log(LOG_DEBUG, "Network %d threads Stopped !", netw->link.sock);

    return TRUE;
} /* Stop_Network( ... ) */

/**
 *@brief send data onto the socket
 *@param netw connected NETWORK
 *@param buf pointer to buffer
 *@param n number of characters we want to send
 *@return NETW_SOCKET_ERROR on error, NETW_SOCKET_DISCONNECTED on disconnection, 'n' value on success
 */
ssize_t send_data(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SOCKET s = ((NETWORK*)netw)->link.sock;
    ssize_t bcount = 0; /* counts bytes sent */
    int error;
    char* errmsg = NULL;

    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Send of 0 is unsupported.");
        n_log(LOG_ERR, "Send of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }

    char* tmp_buf = buf;
    while (bcount < n) /* loop until all sent */
    {
        ssize_t bs = 0; /* bytes sent this pass */

        NETW_CALL_RETRY(bs, send(s, tmp_buf, NETW_BUFLEN_CAST(n - bcount), NETFLAGS), NETW_MAX_RETRIES);
        error = neterrno;

        if (bs > 0) {
            bcount += bs;  /* increment byte counter */
            tmp_buf += bs; /* move buffer ptr for next send */
        } else if (error == ECONNRESET || error == ENOTCONN || error == EPIPE
#ifdef __windows__
                   || error == WSAECONNRESET || error == WSAENOTCONN || error == WSAESHUTDOWN
#endif
        ) {
            n_log(LOG_DEBUG, "socket %d disconnected !", s);
            return NETW_SOCKET_DISCONNECTED;
        } else if (bs == -1) {
            /* real network error */
            errmsg = netstrerror(error);
            _netw_capture_error((NETWORK*)netw, "Socket %d send error: %d, %s", s, bs, _str(errmsg));
            n_log(LOG_ERR, "Socket %d send error: %d, %s", s, bs, _str(errmsg));
            FreeNoLog(errmsg);
            return NETW_SOCKET_ERROR;
        } else if (bs == -2) {
            /* EINTR/WOULDBLOCK retries exhausted */
            _netw_capture_error((NETWORK*)netw, "Socket %d : retry storm on send (%d retries)", s, NETW_MAX_RETRIES);
            n_log(LOG_ERR, "Socket %d : retry storm on send (%d retries)", s, NETW_MAX_RETRIES);
            return NETW_SOCKET_ERROR;
        } else if (bs == 0) {
            /* should never happen on send() */
            n_log(LOG_DEBUG, "socket %d : send returned 0", s);
            return NETW_SOCKET_DISCONNECTED;
        }
    }
    return bcount;
} /*send_data(...)*/

/**
 * @brief recv data from the socket
 * @param netw connected NETWORK
 * @param buf pointer to buffer
 * @param n number of characters we want
 * @return NETW_SOCKET_ERROR on error, NETW_RECV_DISCONNECTED on disconnection, 'n' value on success
 */
ssize_t recv_data(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SOCKET s = ((NETWORK*)netw)->link.sock;
    ssize_t bcount = 0; /* counts bytes read */
    int error;
    char* errmsg = NULL;

#if defined(NETWORK_DISABLE_ZERO_LENGTH_RECV) && (NETWORK_DISABLE_ZERO_LENGTH_RECV == TRUE)
    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Recv of 0 is unsupported.");
        n_log(LOG_ERR, "Recv of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }
#endif

    char* tmp_buf = buf;
    while (bcount < n) /* loop until all received */
    {
        ssize_t br = 0; /* bytes received this pass */

        NETW_CALL_RETRY(br, recv(s, tmp_buf, NETW_BUFLEN_CAST(n - bcount), NETFLAGS), NETW_MAX_RETRIES);
        error = neterrno;

        if (br > 0) {
            bcount += br;  /* increment byte counter */
            tmp_buf += br; /* move buffer ptr for next recv */
        } else if (br == 0) {
            /* clean shutdown from peer */
            n_log(LOG_DEBUG, "socket %d : peer closed connection", s);
            return NETW_SOCKET_DISCONNECTED;
        } else if (error == ECONNRESET || error == ENOTCONN
#ifdef __windows__
                   || error == WSAECONNRESET || error == WSAENOTCONN || error == WSAESHUTDOWN
#endif
        ) {
            /* connection reset or not connected */
            n_log(LOG_DEBUG, "socket %d disconnected !", s);
            return NETW_SOCKET_DISCONNECTED;
        } else if (br == -1) {
            /* real network error */
            errmsg = netstrerror(error);
            _netw_capture_error((NETWORK*)netw, "Socket %d recv error: %d, %s", s, br, _str(errmsg));
            n_log(LOG_ERR, "Socket %d recv error: %d, %s", s, br, _str(errmsg));
            FreeNoLog(errmsg);
            return NETW_SOCKET_ERROR;
        } else if (br == -2) {
            /* EINTR/WOULDBLOCK retries exhausted */
            _netw_capture_error((NETWORK*)netw, "Socket %d : retry storm on recv (%d retries)", s, NETW_MAX_RETRIES);
            n_log(LOG_ERR, "Socket %d : retry storm on recv (%d retries)", s, NETW_MAX_RETRIES);
            return NETW_SOCKET_ERROR;
        }
    }
    return bcount;
} /*recv_data(...)*/

/**
 *@brief single-attempt send for non-blocking sockets (reactor use).
 *       One transport attempt, no loop-until-complete, no retry storm:
 *       partial writes are the caller's state machine to handle.
 *@param netw the NETWORK to use
 *@param buf pointer to buffer
 *@param n number of bytes to try to send (> 0)
 *@return > 0 bytes written, NETW_IO_WANT_WRITE when the socket is not
 *        writable, NETW_SOCKET_DISCONNECTED, or NETW_SOCKET_ERROR
 */
ssize_t send_data_once(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SOCKET s = ((NETWORK*)netw)->link.sock;
    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Send of 0 is unsupported.");
        n_log(LOG_ERR, "Send of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }
    for (;;) {
        ssize_t bs = send(s, buf, NETW_BUFLEN_CAST(n), NETFLAGS);
        int error = neterrno;
        if (bs > 0) return bs;
        if (bs == 0) {
            /* should never happen on send() */
            n_log(LOG_DEBUG, "socket %d : send returned 0", s);
            return NETW_SOCKET_DISCONNECTED;
        }
        if (error == EINTR
#ifdef __windows__
            || error == WSAEINTR
#endif
        )
            continue;
        if (error == EAGAIN || error == EWOULDBLOCK
#ifdef __windows__
            || error == WSAEWOULDBLOCK
#endif
        )
            return NETW_IO_WANT_WRITE;
        if (error == ECONNRESET || error == ENOTCONN || error == EPIPE
#ifdef __windows__
            || error == WSAECONNRESET || error == WSAENOTCONN || error == WSAESHUTDOWN
#endif
        ) {
            n_log(LOG_DEBUG, "socket %d disconnected !", s);
            return NETW_SOCKET_DISCONNECTED;
        }
        char* errmsg = netstrerror(error);
        _netw_capture_error((NETWORK*)netw, "Socket %d send_once error: %s", s, _str(errmsg));
        n_log(LOG_ERR, "Socket %d send_once error: %s", s, _str(errmsg));
        FreeNoLog(errmsg);
        return NETW_SOCKET_ERROR;
    }
} /*send_data_once(...)*/

/**
 *@brief single-attempt recv for non-blocking sockets (reactor use).
 *@param netw the NETWORK to use
 *@param buf pointer to buffer
 *@param n max number of bytes to read (> 0)
 *@return > 0 bytes read, NETW_IO_WANT_READ when no data is available,
 *        NETW_SOCKET_DISCONNECTED on orderly EOF / reset, or
 *        NETW_SOCKET_ERROR
 */
ssize_t recv_data_once(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SOCKET s = ((NETWORK*)netw)->link.sock;
    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Recv of 0 is unsupported.");
        n_log(LOG_ERR, "Recv of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }
    for (;;) {
        ssize_t br = recv(s, buf, NETW_BUFLEN_CAST(n), NETFLAGS);
        int error = neterrno;
        if (br > 0) return br;
        if (br == 0) {
            /* orderly shutdown by peer */
            n_log(LOG_DEBUG, "socket %d disconnected !", s);
            return NETW_SOCKET_DISCONNECTED;
        }
        if (error == EINTR
#ifdef __windows__
            || error == WSAEINTR
#endif
        )
            continue;
        if (error == EAGAIN || error == EWOULDBLOCK
#ifdef __windows__
            || error == WSAEWOULDBLOCK
#endif
        )
            return NETW_IO_WANT_READ;
        if (error == ECONNRESET || error == ENOTCONN
#ifdef __windows__
            || error == WSAECONNRESET || error == WSAENOTCONN
#endif
        ) {
            n_log(LOG_DEBUG, "socket %d disconnected !", s);
            return NETW_SOCKET_DISCONNECTED;
        }
        char* errmsg = netstrerror(error);
        _netw_capture_error((NETWORK*)netw, "Socket %d recv_once error: %s", s, _str(errmsg));
        n_log(LOG_ERR, "Socket %d recv_once error: %s", s, _str(errmsg));
        FreeNoLog(errmsg);
        return NETW_SOCKET_ERROR;
    }
} /*recv_data_once(...)*/

#ifdef HAVE_OPENSSL
/**
 *@brief send data onto the socket
 *@param netw connected NETWORK in SSL mode
 *@param buf pointer to buffer
 *@param n number of characters we want to send
 *@return -1 on error, NETW_SOCKET_DISCONNECTED on disconnection, n on success
 */
ssize_t send_ssl_data(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return -1);
    __n_assert(buf, return -1);

    SSL* ssl = ((NETWORK*)netw)->ssl;
    __n_assert(ssl, return -1);

    SOCKET s = ((NETWORK*)netw)->link.sock;

    ssize_t bcount = 0;  // counts bytes sent
    int error;
    char* errmsg = NULL;

    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Send of 0 is unsupported.");
        n_log(LOG_ERR, "Send of 0 is unsupported.");
        return -1;
    }

    while (bcount < n)  // loop until full buffer
    {
        size_t bs = 0;  // bytes sent this pass
#if OPENSSL_VERSION_NUMBER < 0x1010107fL
        int status = SSL_write(ssl, buf, (int)(n - bcount));  // OpenSSL < 1.1.1
        bs = (status > 0) ? (size_t)status : 0;
#else
        int status = SSL_write_ex(ssl, buf, (size_t)(n - bcount), &bs);  // OpenSSL >= 1.1.1
#endif
        error = neterrno;
        if (status > 0) {
            bcount += (ssize_t)bs;  // increment byte counter
            buf += bs;              // move buffer ptr for next read
        } else {
            int ssl_error = SSL_get_error(ssl, status);
            if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                /* Rare on the blocking sockets the thread engine uses
                 * (renegotiation / partial record). usleep(0) was a
                 * pure busy-yield, burn a bounded 1 ms instead so a
                 * stuck WANT state can't peg a core. */
                u_sleep(1000);
                continue;
            }
            /* Peer-closed connection is a normal end-of-life, not a
             * protocol error. SSL_ERROR_ZERO_RETURN is a clean TLS
             * close_notify; SSL_ERROR_SYSCALL with errno EPIPE /
             * ECONNRESET / ENOTCONN (or errno 0, OpenSSL's "unexpected
             * EOF" signal) means the socket went away under us. Mirror
             * the plaintext send_data() path: log at DEBUG and return
             * NETW_SOCKET_DISCONNECTED so the send thread's QUIT / normal
             * branches treat it as a graceful exit instead of an error.
             * Without this, a TLS client quitting after the server has
             * already dropped the link logs three spurious LOG_ERR lines
             * (SSL_write syscall error / state QUIT / thread exit err 4). */
            if (ssl_error == SSL_ERROR_ZERO_RETURN ||
                (ssl_error == SSL_ERROR_SYSCALL &&
                 (error == 0 || error == EPIPE || error == ECONNRESET || error == ENOTCONN
#ifdef __windows__
                  || error == WSAECONNRESET || error == WSAENOTCONN || error == WSAESHUTDOWN
#endif
                  ))) {
                n_log(LOG_DEBUG, "socket %d disconnected during SSL_write !", s);
                return NETW_SOCKET_DISCONNECTED;
            }
            errmsg = netstrerror(error);
            switch (ssl_error) {
                case SSL_ERROR_SYSCALL:
                    // Real, unexpected syscall failure (errno set to something other than a peer close)
                    _netw_capture_error((NETWORK*)netw, "socket %d SSL_write syscall error: %s", s, _str(errmsg));
                    n_log(LOG_ERR, "socket %d SSL_write syscall error: %s", s, _str(errmsg));
                    break;
                case SSL_ERROR_SSL:
                    // Handle SSL protocol failure
                    _netw_capture_error((NETWORK*)netw, "socket %d SSL_write returned %d, error: %s", s, bs, ERR_reason_error_string(ERR_get_error()));
                    n_log(LOG_ERR, "socket %d SSL_write returned %d, error: %s", s, bs, ERR_reason_error_string(ERR_get_error()));
                    break;
                default:
                    // Other errors
                    _netw_capture_error((NETWORK*)netw, "socket %d SSL_write returned %d, errno: %s", s, bs, _str(errmsg));
                    n_log(LOG_ERR, "socket %d SSL_write returned %d, errno: %s", s, bs, _str(errmsg));
                    break;
            }
            FreeNoLog(errmsg);
            return NETW_SOCKET_ERROR;
        }
    }
    return bcount;
} /*send_ssl_data(...)*/

/**
 * @brief recv data from the socket
 * @param netw connected NETWORK
 * @param buf pointer to buffer
 * @param n number of characters we want
 * @return -1 on error, NETW_SOCKET_DISCONNECTED on disconnection n on success
 */
ssize_t recv_ssl_data(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return -1);
    __n_assert(buf, return -1);

    SSL* ssl = ((NETWORK*)netw)->ssl;
    __n_assert(ssl, return -1);

    SOCKET s = ((NETWORK*)netw)->link.sock;
    ssize_t bcount = 0;  // counts bytes read
    int error;
    char* errmsg = NULL;

    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Recv of 0 is unsupported.");
        n_log(LOG_ERR, "Recv of 0 is unsupported.");
        return -1;
    }

    while (bcount < n) {
        size_t br = 0;  // bytes read this pass
                        // loop until full buffer
#if OPENSSL_VERSION_NUMBER < 0x10101000L
        int status = SSL_read(ssl, buf, (int)(n - bcount));  // OpenSSL < 1.1.1
        br = (status > 0) ? (size_t)status : 0;
#else
        int status = SSL_read_ex(ssl, buf, (size_t)(n - bcount), &br);  // OpenSSL >= 1.1.1
#endif
        error = neterrno;
        if (status > 0) {
            bcount += (ssize_t)br;  // increment byte counter
            buf += br;              // move buffer ptr for next read
        } else {
            int ssl_error = SSL_get_error(ssl, status);
            if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
                /* Same bounded back-off as send_ssl_data, usleep(0)
                 * was a busy-yield that could peg a core on a stuck
                 * WANT state. */
                u_sleep(1000);
                continue;
            }
            /* Connection teardown is the normal end of a session, not an
             * error. Mirror the plaintext recv_data() and the
             * non-blocking recv_ssl_data_once() paths: return
             * NETW_SOCKET_DISCONNECTED so netw_recv_func() exits via the
             * clean NETW_THR_EXIT_OK path with no LOG_ERR noise -- and so
             * the recv thread does not flip the NETWORK into NETW_ERROR,
             * which is what was tripping the send thread into the
             * "exiting with error 666" (NETW_THR_EXIT_ERROR) tail on a
             * normal client exit. */
            if (ssl_error == SSL_ERROR_ZERO_RETURN) {
                /* peer sent a clean close_notify */
                n_log(LOG_DEBUG, "socket %d : TLS session closed by peer (close_notify)", s);
                return NETW_SOCKET_DISCONNECTED;
            }
            if (ssl_error == SSL_ERROR_SYSCALL && (error == 0 || error == ECONNRESET || error == ENOTCONN
#ifdef __windows__
                                                   || error == WSAECONNRESET || error == WSAENOTCONN || error == WSAESHUTDOWN
#endif
                                                   )) {
                /* peer vanished without close_notify (TCP FIN/RST) */
                n_log(LOG_DEBUG, "socket %d : peer closed connection (TLS, no close_notify)", s);
                return NETW_SOCKET_DISCONNECTED;
            }
            /* Pop the queued reason exactly ONCE and reuse it below;
             * calling ERR_get_error() twice (capture + log) cleared the
             * queue between calls, so the second read always printed
             * "(null)" instead of the real reason. */
            unsigned long ssl_reason = ERR_get_error();
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
            if (ssl_error == SSL_ERROR_SSL && ERR_GET_REASON(ssl_reason) == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
                /* OpenSSL >= 3.0 reports an unclean TCP close as an
                 * SSL_ERROR_SSL/UNEXPECTED_EOF rather than the
                 * SSL_ERROR_SYSCALL/errno-0 of 1.1.1; still a disconnect. */
                n_log(LOG_DEBUG, "socket %d : peer closed connection (TLS unexpected EOF)", s);
                return NETW_SOCKET_DISCONNECTED;
            }
#endif
            errmsg = netstrerror(error);
            switch (ssl_error) {
                case SSL_ERROR_SYSCALL:
                    // Genuine syscall-level failure (not a peer close)
                    _netw_capture_error((NETWORK*)netw, "socket %d SSL_read syscall error: %s", s, _str(errmsg));
                    n_log(LOG_ERR, "socket %d SSL_read syscall error: %s", s, _str(errmsg));
                    break;
                case SSL_ERROR_SSL:
                    // Genuine TLS protocol failure
                    _netw_capture_error((NETWORK*)netw, "socket %d SSL_read protocol error: %s", s, _str((char*)ERR_reason_error_string(ssl_reason)));
                    n_log(LOG_ERR, "socket %d SSL_read protocol error: %s", s, _str((char*)ERR_reason_error_string(ssl_reason)));
                    break;
                default:
                    // Other errors
                    _netw_capture_error((NETWORK*)netw, "socket %d SSL_read returned %zu, errno: %s", s, br, _str(errmsg));
                    n_log(LOG_ERR, "socket %d SSL_read returned %zu, errno: %s", s, br, _str(errmsg));
                    break;
            }
            FreeNoLog(errmsg);
            return NETW_SOCKET_ERROR;
        }
    }
    return bcount;
} /*recv_ssl_data(...)*/

/**
 *@brief single-attempt TLS send for non-blocking sockets (reactor use).
 *       One SSL_write attempt; WANT_READ/WANT_WRITE are surfaced to the
 *       caller's event loop instead of being spun on. Requires
 *       SSL_MODE_ENABLE_PARTIAL_WRITE (set at SSL_CTX creation) so a
 *       short write can be resumed from the caller's buffer + offset.
 *       Note: a TLS send can return NETW_IO_WANT_READ mid-renegotiation,
 *       the caller must retry after the next READABLE event.
 *@param netw the NETWORK to use (crypto must be active)
 *@param buf pointer to buffer
 *@param n number of bytes to try to send (> 0)
 *@return > 0 bytes written, NETW_IO_WANT_READ / NETW_IO_WANT_WRITE,
 *        NETW_SOCKET_DISCONNECTED, or NETW_SOCKET_ERROR
 */
ssize_t send_ssl_data_once(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SSL* ssl = ((NETWORK*)netw)->ssl;
    __n_assert(ssl, return NETW_SOCKET_ERROR);
    SOCKET s = ((NETWORK*)netw)->link.sock;

    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Send of 0 is unsupported.");
        n_log(LOG_ERR, "Send of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }

    size_t bs = 0;
#if OPENSSL_VERSION_NUMBER < 0x1010107fL
    int status = SSL_write(ssl, buf, (int)n);
    bs = (status > 0) ? (size_t)status : 0;
#else
    int status = SSL_write_ex(ssl, buf, (size_t)n, &bs);
#endif
    int error = neterrno;
    if (status > 0) return (ssize_t)bs;

    int ssl_error = SSL_get_error(ssl, status);
    switch (ssl_error) {
        case SSL_ERROR_WANT_READ:
            return NETW_IO_WANT_READ;
        case SSL_ERROR_WANT_WRITE:
            return NETW_IO_WANT_WRITE;
        case SSL_ERROR_ZERO_RETURN:
            n_log(LOG_DEBUG, "socket %d TLS session closed by peer", s);
            return NETW_SOCKET_DISCONNECTED;
        case SSL_ERROR_SYSCALL:
            if (error == 0 || error == ECONNRESET || error == EPIPE || error == ENOTCONN
#ifdef __windows__
                || error == WSAECONNRESET || error == WSAENOTCONN
#endif
            ) {
                n_log(LOG_DEBUG, "socket %d SSL_write syscall: connection closed by peer", s);
                return NETW_SOCKET_DISCONNECTED;
            }
            if (error == EINTR || error == EAGAIN || error == EWOULDBLOCK
#ifdef __windows__
                || error == WSAEINTR || error == WSAEWOULDBLOCK
#endif
            )
                return NETW_IO_WANT_WRITE;
            {
                char* errmsg = netstrerror(error);
                _netw_capture_error((NETWORK*)netw, "socket %d SSL_write_once syscall error: %s", s, _str(errmsg));
                n_log(LOG_ERR, "socket %d SSL_write_once syscall error: %s", s, _str(errmsg));
                FreeNoLog(errmsg);
            }
            return NETW_SOCKET_ERROR;
        default:
            _netw_capture_error((NETWORK*)netw, "socket %d SSL_write_once error: %s", s, ERR_reason_error_string(ERR_get_error()));
            n_log(LOG_ERR, "socket %d SSL_write_once error: %s", s, ERR_reason_error_string(ERR_get_error()));
            return NETW_SOCKET_ERROR;
    }
} /*send_ssl_data_once(...)*/

/**
 *@brief single-attempt TLS recv for non-blocking sockets (reactor use).
 *       The caller must loop until NETW_IO_WANT_READ, SSL_read drains
 *       its internal plaintext buffer before reporting WANT_READ, so
 *       loop-until-WANT_READ also covers the SSL_pending() case that
 *       otherwise stalls TLS over edge-triggered epoll.
 *@param netw the NETWORK to use (crypto must be active)
 *@param buf pointer to buffer
 *@param n max number of bytes to read (> 0)
 *@return > 0 bytes read, NETW_IO_WANT_READ / NETW_IO_WANT_WRITE,
 *        NETW_SOCKET_DISCONNECTED, or NETW_SOCKET_ERROR
 */
ssize_t recv_ssl_data_once(void* netw, char* buf, uint32_t n) {
    __n_assert(netw, return NETW_SOCKET_ERROR);
    __n_assert(buf, return NETW_SOCKET_ERROR);

    SSL* ssl = ((NETWORK*)netw)->ssl;
    __n_assert(ssl, return NETW_SOCKET_ERROR);
    SOCKET s = ((NETWORK*)netw)->link.sock;

    if (n == 0) {
        _netw_capture_error((NETWORK*)netw, "Recv of 0 is unsupported.");
        n_log(LOG_ERR, "Recv of 0 is unsupported.");
        return NETW_SOCKET_ERROR;
    }

    size_t br = 0;
#if OPENSSL_VERSION_NUMBER < 0x10101000L
    int status = SSL_read(ssl, buf, (int)n);
    br = (status > 0) ? (size_t)status : 0;
#else
    int status = SSL_read_ex(ssl, buf, (size_t)n, &br);
#endif
    int error = neterrno;
    if (status > 0) return (ssize_t)br;

    int ssl_error = SSL_get_error(ssl, status);
    switch (ssl_error) {
        case SSL_ERROR_WANT_READ:
            return NETW_IO_WANT_READ;
        case SSL_ERROR_WANT_WRITE:
            return NETW_IO_WANT_WRITE;
        case SSL_ERROR_ZERO_RETURN:
            n_log(LOG_DEBUG, "socket %d TLS session closed by peer", s);
            return NETW_SOCKET_DISCONNECTED;
        case SSL_ERROR_SYSCALL:
            if (error == 0 || error == ECONNRESET || error == ENOTCONN
#ifdef __windows__
                || error == WSAECONNRESET || error == WSAENOTCONN
#endif
            ) {
                n_log(LOG_DEBUG, "socket %d SSL_read syscall: connection closed by peer", s);
                return NETW_SOCKET_DISCONNECTED;
            }
            if (error == EINTR || error == EAGAIN || error == EWOULDBLOCK
#ifdef __windows__
                || error == WSAEINTR || error == WSAEWOULDBLOCK
#endif
            )
                return NETW_IO_WANT_READ;
            {
                char* errmsg = netstrerror(error);
                _netw_capture_error((NETWORK*)netw, "socket %d SSL_read_once syscall error: %s", s, _str(errmsg));
                n_log(LOG_ERR, "socket %d SSL_read_once syscall error: %s", s, _str(errmsg));
                FreeNoLog(errmsg);
            }
            return NETW_SOCKET_ERROR;
        default: {
            /* Pop the reason once (a second ERR_get_error() would clear
             * the queue and print "(null)"). OpenSSL >= 3.0 reports an
             * unclean TCP close here as SSL_ERROR_SSL/UNEXPECTED_EOF --
             * a disconnect, not a protocol error. */
            unsigned long ssl_reason = ERR_get_error();
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
            if (ssl_error == SSL_ERROR_SSL && ERR_GET_REASON(ssl_reason) == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
                n_log(LOG_DEBUG, "socket %d : peer closed connection (TLS unexpected EOF)", s);
                return NETW_SOCKET_DISCONNECTED;
            }
#endif
            _netw_capture_error((NETWORK*)netw, "socket %d SSL_read_once error: %s", s, _str((char*)ERR_reason_error_string(ssl_reason)));
            n_log(LOG_ERR, "socket %d SSL_read_once error: %s", s, _str((char*)ERR_reason_error_string(ssl_reason)));
            return NETW_SOCKET_ERROR;
        }
    }
} /*recv_ssl_data_once(...)*/
#endif

/**
 *@brief send data onto the socket
 *@param s connected socket
 *@param buf pointer to buffer
 *@param n number of characters we want to send
 *@param _code Code for php decoding rule
 *@return -1 on error, n on success
 */
ssize_t send_php(SOCKET s, int _code, char* buf, int n) {
    ssize_t bcount = 0; /* counts bytes read */
    ssize_t bs;         /* bytes read this pass */

    int error;
    char* errmsg = NULL;

    char* ptr = NULL; /* temp char ptr ;-) */
    char head[HEAD_SIZE + 1] = "";
    char code[HEAD_CODE + 1] = "";

    // Format and assign to head
    snprintf(head, HEAD_SIZE + 1, "%0*d", HEAD_SIZE, n);
    // Format and assign to code
    snprintf(code, HEAD_CODE + 1, "%0*d", HEAD_CODE, _code);

    /* sending head */
    bcount = 0;
    ptr = head;
    while (bcount < HEAD_SIZE) /* loop until full buffer */
    {
        bs = send(s, ptr, NETW_BUFLEN_CAST(HEAD_SIZE - bcount), NETFLAGS);
        error = neterrno;
        if (bs > 0) {
            bcount += bs; /* increment byte counter */
            ptr += bs;    /* move buffer ptr for next read */
        } else {
            /* signal an error to the caller */
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Socket %d sending Error %d when sending head size, neterrno: %s", s, bs, _str(errmsg));
            FreeNoLog(errmsg);
            return -1;
        }
    }

    /* sending code */
    bcount = 0;
    ptr = code;
    while (bcount < HEAD_CODE) /* loop until full buffer */
    {
        bs = send(s, ptr, NETW_BUFLEN_CAST(HEAD_CODE - bcount), NETFLAGS);
        error = neterrno;
        if (bs > 0) {
            bcount += bs; /* increment byte counter */
            ptr += bs;    /* move buffer ptr for next read */
        } else {
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Socket %d sending Error %d when sending head code, neterrno: %s", s, bs, _str(errmsg));
            FreeNoLog(errmsg);
            return -1;
        }
    }

    /* sending buf */
    bcount = 0;
    while (bcount < n) /* loop until full buffer */
    {
        bs = send(s, buf, NETW_BUFLEN_CAST(n - bcount), NETFLAGS);
        error = neterrno;
        if (bs > 0) {
            bcount += bs; /* increment byte counter */
            buf += bs;    /* move buffer ptr for next read */
        } else {
            /* signal an error to the caller */
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Socket %d sending Error %d when sending message of size %d, neterrno: %s", s, bs, n, _str(errmsg));
            FreeNoLog(errmsg);
            return -1;
        }
    }

    return bcount;

} /*send_php(...)*/

/**
 *@brief recv data from the socket
 *@param s connected socket
 *@param _code pointer to store the code
 *@param buf pointer to buffer
 *@return -1 on error, size on success
 */
ssize_t recv_php(SOCKET s, int* _code, char** buf) {
    ssize_t bcount = 0;           /* counts bytes read */
    ssize_t br;                   /* bytes read this pass */
    long int tmpnb = 0, size = 0; /* size of message to receive */
    char* ptr = NULL;

    int error;
    char* errmsg = NULL;

    char head[HEAD_SIZE + 1] = "";
    char code[HEAD_CODE + 1] = "";

    /* Receiving total message size */
    bcount = 0;
    ptr = head;
    while (bcount < HEAD_SIZE) {
        /* loop until full buffer */
        br = recv(s, ptr, NETW_BUFLEN_CAST(HEAD_SIZE - bcount), NETFLAGS);
        error = neterrno;
        if (br > 0) {
            bcount += br; /* increment byte counter */
            ptr += br;    /* move buffer ptr for next read */
        } else {
            /* signal an error to the caller */
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Socket %d receive %d Error %s", s, br, _str(errmsg));
            FreeNoLog(errmsg);
            return FALSE;
        }
    }

    tmpnb = strtol(head, NULL, 10);
    if (tmpnb == LONG_MIN || tmpnb == LONG_MAX) {
        n_log(LOG_ERR, "Size received ( %ld ) can not be determined on socket %d", tmpnb, s);
        return FALSE;
    }

    size = tmpnb;

    /* receiving request code */
    bcount = 0;
    ptr = code;
    while (bcount < HEAD_CODE) {
        /* loop until full buffer */
        br = recv(s, ptr, NETW_BUFLEN_CAST(HEAD_CODE - bcount), NETFLAGS);
        error = neterrno;
        if (br > 0) {
            bcount += br; /* increment byte counter */
            ptr += br;    /* move buffer ptr for next read */
        } else {
            /* signal an error to the caller */
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Socket %d receive %d Error , neterrno: %s", s, br, _str(errmsg));
            FreeNoLog(errmsg);
            return FALSE;
        }
    }

    tmpnb = strtol(code, NULL, 10);
    if (tmpnb <= INT_MIN || tmpnb >= INT_MAX) {
        n_log(LOG_ERR, "Code received ( %ld ) too big or too little to be  valid code on socket %d", tmpnb, s);
        return FALSE;
    }

    (*_code) = (int)tmpnb;

    /* receving message */
    if ((*buf)) {
        // cppcheck-suppress identicalInnerCondition ; Free() macro rechecks pointer internally
        Free((*buf));
    }
    Malloc((*buf), char, (size_t)(size + 1));
    if (!(*buf)) {
        n_log(LOG_ERR, "Could not allocate PHP receive buf");
        return FALSE;
    }

    bcount = 0;
    ptr = (*buf);
    while (bcount < size) {
        /* loop until full buffer */
        br = recv(s, ptr, NETW_BUFLEN_CAST(size - bcount), NETFLAGS);
        error = neterrno;
        if (br > 0) {
            bcount += br; /* increment byte counter */
            ptr += br;    /* move buffer ptr for next read */
        } else {
            /* signal an error to the caller */
            errmsg = netstrerror(error);
            n_log(LOG_ERR, "Socket %d receive %d Error neterrno: %s", s, br, _str(errmsg));
            FreeNoLog(errmsg);
            return FALSE;
        }
    }
    return size;
} /*recv_php(...)*/

/**
 *
 *@brief retrieve network send queue status
 *@param netw NETWORK object
 *@param nb_to_send Number of messages still in send buffer (not yet submitted to kernel)
 *@param nb_to_read Number of message already read by the kernel, waiting in the local message list
 *@return TRUE or FALSE
 */
int netw_get_queue_status(NETWORK* netw, size_t* nb_to_send, size_t* nb_to_read) {
    __n_assert(netw, return FALSE);

    pthread_mutex_lock(&netw->sendbolt);
    (*nb_to_send) = netw->send_buf->nb_items;
    pthread_mutex_unlock(&netw->sendbolt);

    pthread_mutex_lock(&netw->recvbolt);
    (*nb_to_read) = netw->recv_buf->nb_items;
    pthread_mutex_unlock(&netw->recvbolt);

    return TRUE;
} /* get queue states */

/**
 *@brief return a new network pool of nb_min_element
 *@param nb_min_element size of internal hash table for network pool
 *@return a new NETWORK_POOL or NULL
 */
NETWORK_POOL* netw_new_pool(size_t nb_min_element) {
    NETWORK_POOL* netw_pool = NULL;

    Malloc(netw_pool, NETWORK_POOL, 1);
    __n_assert(netw_pool, return NULL);

    netw_pool->pool = new_ht(nb_min_element);
    __n_assert(netw_pool->pool, Free(netw_pool); return NULL);

    init_lock(netw_pool->rwlock);

    return netw_pool;
} /* netw_new_pool() */

/**
 *@brief free a NETWORK_POOL *pool
 *@param netw_pool the address of a NETWORK_POOL *pointer to free
 *@return TRUE or FALSE
 */
int netw_destroy_pool(NETWORK_POOL** netw_pool) {
    __n_assert(netw_pool && (*netw_pool), return FALSE);

    write_lock((*netw_pool)->rwlock);
    if ((*netw_pool)->pool)
        destroy_ht(&(*netw_pool)->pool);
    unlock((*netw_pool)->rwlock);

    rw_lock_destroy((*netw_pool)->rwlock);

    Free((*netw_pool));

    return TRUE;
} /* netw_destroy_pool() */

/**
 *@brief close a network from a network pool
 *@param netw_ptr NETWORK *network pointer
 */
void netw_pool_netw_close(void* netw_ptr) {
    NETWORK* netw = (NETWORK*)netw_ptr;
    __n_assert(netw, return);
    n_log(LOG_DEBUG, "Network pool %p: network id %d still active !!", netw, netw->link.sock);
    return;
} /* netw_pool_netw_close() */

/**
 *@brief add a NETWORK *netw to a NETWORK_POOL *pool
 *@param netw_pool targeted network pool
 *@param netw network to add
 *@return TRUE or FALSE
 */
int netw_pool_add(NETWORK_POOL* netw_pool, NETWORK* netw) {
    __n_assert(netw_pool, return FALSE);
    __n_assert(netw, return FALSE);

    n_log(LOG_DEBUG, "Trying to add %lld to %p", (unsigned long long)netw->link.sock, netw_pool->pool);

    /* write lock the pool */
    write_lock(netw_pool->rwlock);
    /* test if not already added */
    N_STR* key = NULL;
    nstrprintf(key, "%lld", (unsigned long long)netw->link.sock);
    HASH_NODE* node = NULL;
    if (ht_get_ptr(netw_pool->pool, _nstr(key), (void*)&node) == TRUE) {
        _netw_capture_error(netw, "Network id %d already added !", netw->link.sock);
        n_log(LOG_ERR, "Network id %d already added !", netw->link.sock);
        free_nstr(&key);
        unlock(netw_pool->rwlock);
        return FALSE;
    }
    int retval;
    /* add it */
    if ((retval = ht_put_ptr(netw_pool->pool, _nstr(key), netw, &netw_pool_netw_close, NULL)) == TRUE) {
        if ((retval = list_push(netw->pools, netw_pool, NULL)) == TRUE) {
            n_log(LOG_DEBUG, "added netw %d to pool %p", netw->link.sock, netw_pool);
        } else {
            _netw_capture_error(netw, "could not add netw %d to pool %p", netw->link.sock, netw_pool);
            n_log(LOG_ERR, "could not add netw %d to pool %p", netw->link.sock, netw_pool);
        }
    } else {
        _netw_capture_error(netw, "could not add netw %d to pool %p", netw->link.sock, netw_pool);
        n_log(LOG_ERR, "could not add netw %d to pool %p", netw->link.sock, netw_pool);
    }
    free_nstr(&key);

    /* unlock the pool */
    unlock(netw_pool->rwlock);

    return retval;
} /* netw_pool_add() */

/**
 *@brief remove a NETWORK *netw to a NETWORK_POOL *pool
 *@param netw_pool targeted network pool
 *@param netw network to remove
 *@return TRUE or FALSE
 */
int netw_pool_remove(NETWORK_POOL* netw_pool, NETWORK* netw) {
    __n_assert(netw_pool, return FALSE);
    __n_assert(netw, return FALSE);

    /* write lock the pool */
    write_lock(netw_pool->rwlock);
    /* test if present */
    N_STR* key = NULL;
    nstrprintf(key, "%lld", (unsigned long long int)netw->link.sock);
    if (ht_remove(netw_pool->pool, _nstr(key)) == TRUE) {
        LIST_NODE* node = list_search(netw->pools, netw_pool);
        if (node) {
            if (!remove_list_node(netw->pools, node, NETWORK_POOL)) {
                _netw_capture_error(netw, "Network id %d could not be removed !", netw->link.sock);
                n_log(LOG_ERR, "Network id %d could not be removed !", netw->link.sock);
            }
        }
        unlock(netw_pool->rwlock);
        n_log(LOG_DEBUG, "Network id %d removed !", netw->link.sock);
        free_nstr(&key);
        return TRUE;
    }
    free_nstr(&key);
    _netw_capture_error(netw, "Network id %d already removed !", netw->link.sock);
    n_log(LOG_ERR, "Network id %d already removed !", netw->link.sock);
    /* unlock the pool */
    unlock(netw_pool->rwlock);
    return FALSE;
} /* netw_pool_remove */

/**
 *@brief add net_msg to all network in netork pool
 *@param netw_pool targeted network pool
 *@param from source network
 *@param net_msg mesage to broadcast
 *@return TRUE or FALSE
 */
int netw_pool_broadcast(NETWORK_POOL* netw_pool, const NETWORK* from, N_STR* net_msg) {
    __n_assert(netw_pool, return FALSE);
    __n_assert(net_msg, return FALSE);

    /* write lock the pool */
    read_lock(netw_pool->rwlock);
    ht_foreach(node, netw_pool->pool) {
        NETWORK* netw = hash_val(node, NETWORK);
        if (from) {
            if (netw->link.sock != from->link.sock)
                netw_add_msg(netw, nstrdup(net_msg));
        } else {
            netw_add_msg(netw, nstrdup(net_msg));
        }
    }
    unlock(netw_pool->rwlock);
    return TRUE;
} /* netw_pool_broadcast */

/**
 *@brief return the number of networks in netw_pool
 *@param netw_pool targeted network pool
 *@return 0 or the number of networks in netw_pool
 */
size_t netw_pool_nbclients(NETWORK_POOL* netw_pool) {
    __n_assert(netw_pool, return 0);

    size_t nb = 0;
    read_lock(netw_pool->rwlock);
    nb = netw_pool->pool->nb_keys;
    unlock(netw_pool->rwlock);

    return nb;
} /* netw_pool_nbclients() */

/**
 *@brief associate an id and a network
 *@param netw targeted network
 *@param id id we want to associated with
 *@return TRUE or FALSE
 */
int netw_set_user_id(NETWORK* netw, int id) {
    __n_assert(netw, return FALSE);
    netw->user_id = id;
    return TRUE;
} /* netw_set_user_id() */

/**
 *@brief Add a ping reply to the network
 *@param netw The aimed NETWORK where we want to add something to send
 *@param id_from Identifiant of the sender
 *@param id_to Identifiant of the destination, -1 if the serveur itslef is targetted
 *@param time The time it was when the ping was sended
 *@param type NETW_PING_REQUEST or NETW_PING_REPLY
 *@return TRUE or FALSE
 */
int netw_send_ping(NETWORK* netw, int type, int id_from, int id_to, int time) {
    N_STR* tmpstr = NULL;
    __n_assert(netw, return FALSE);

    tmpstr = netmsg_make_ping(type, id_from, id_to, time);
    __n_assert(tmpstr, return FALSE);

    return netw_add_msg(netw, tmpstr);
} /* netw_send_ping( ... ) */

/**
 *@brief Add a formatted NETWMSG_IDENT message to the specified network
 *@param netw The aimed NETWORK where we want to add something to send
 *@param type type of identification ( NETW_IDENT_REQUEST , NETW_IDENT_NEW )
 *@param id The ID of the sending client
 *@param name Username
 *@param passwd Password
 *@return TRUE or FALSE
 */
int netw_send_ident(NETWORK* netw, int type, int id, N_STR* name, N_STR* passwd) {
    N_STR* tmpstr = NULL;

    __n_assert(netw, return FALSE);

    tmpstr = netmsg_make_ident(type, id, name, passwd);
    __n_assert(tmpstr, return FALSE);

    return netw_add_msg(netw, tmpstr);
} /* netw_send_ident( ... ) */

/**
 *@brief Add a formatted NETWMSG_IDENT message to the specified network
 *@param netw The aimed NETWORK where we want to add something to send
 *@param id The ID of the sending client
 *@param X X position inside a big grid
 *@param Y Y position inside a big grid
 *@param vx X speed
 *@param vy Y speed
 *@param acc_x Y acceleration
 *@param acc_y X acceleration
 *@param time_stamp Current Time when sending (for some delta we would want to compute )
 *@return TRUE or FALSE
 */
int netw_send_position(NETWORK* netw, int id, double X, double Y, double vx, double vy, double acc_x, double acc_y, int time_stamp) {
    N_STR* tmpstr = NULL;

    __n_assert(netw, return FALSE);

    tmpstr = netmsg_make_position_msg(id, X, Y, vx, vy, acc_x, acc_y, time_stamp);

    __n_assert(tmpstr, return FALSE);

    return netw_add_msg(netw, tmpstr);
} /* netw_send_position( ... ) */

/**
 *@brief Add a string to the network, aiming a specific user
 *@param netw The aimed NETWORK where we want to add something to send
 *@param id_to The ID of the targetted client
 *@param name Sender Name
 *@param chan channel to use
 *@param txt Sender text
 *@param color Sender text color
 *@return TRUE or FALSE
 */
int netw_send_string_to(NETWORK* netw, int id_to, N_STR* name, N_STR* chan, N_STR* txt, int color) {
    N_STR* tmpstr = NULL;

    __n_assert(netw, return FALSE);

    tmpstr = netmsg_make_string_msg(netw->user_id, id_to, name, chan, txt, color);
    __n_assert(tmpstr, return FALSE);

    return netw_add_msg(netw, tmpstr);
} /* netw_send_string_to( ... ) */

/**
 *@brief Add a string to the network, aiming all server-side users
 *@param netw The aimed NETWORK where we want to add something to send
 *@param name Name of user
 *@param chan Target Channel, if any. Pass "ALL" to target the default channel
 *@param txt The text to send
 *@param color The color of the text
 *@return TRUE or FALSE;
 */
int netw_send_string_to_all(NETWORK* netw, N_STR* name, N_STR* chan, N_STR* txt, int color) {
    N_STR* tmpstr = NULL;

    __n_assert(netw, return FALSE);

    tmpstr = netmsg_make_string_msg(netw->user_id, -1, name, chan, txt, color);
    __n_assert(tmpstr, return FALSE);

    return netw_add_msg(netw, tmpstr);
} /* netw_send_string_to_all( ... ) */

/**
 *@brief Add a formatted NETMSG_QUIT message to the specified network
 *@param netw The aimed NETWORK
 *@return TRUE or FALSE
 */
int netw_send_quit(NETWORK* netw) {
    __n_assert(netw, return FALSE);

    N_STR* tmpstr = NULL;

    tmpstr = netmsg_make_quit_msg();
    __n_assert(tmpstr, return FALSE);

    return netw_add_msg(netw, tmpstr);
} /* netw_send_quit( ... ) */

/**
 * @brief function to calculate the required size for the URL-encoded string
 * @param str clear string from which we want to know the urlencoded size
 * @param len length of the input string
 * @return the urlencoded size if the string was urlencoded
 */
size_t netw_calculate_urlencoded_size(const char* str, size_t len) {
    __n_assert(str, return 0);

    size_t encoded_size = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded_size += 1;  // Safe character, no encoding needed
        } else {
            encoded_size += 3;  // Unsafe character, will be encoded as %XX
        }
    }

    return encoded_size;
}

/**
 * @brief function to perform URL encoding
 * @param str input string
 * @param len input string length
 * @return urlencoded char *string or NULL
 */
char* netw_urlencode(const char* str, size_t len) {
    __n_assert(str, return NULL);

    static const char* hex = "0123456789ABCDEF";
    size_t encoded_size = netw_calculate_urlencoded_size(str, len);
    char* encoded = (char*)malloc(encoded_size + 1);  // Allocate memory for the encoded string (+1 for the null terminator)

    if (!encoded) {
        return NULL;  // Return NULL if memory allocation fails
    }

    char* pbuf = encoded;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *pbuf++ = (char)c;  // Copy safe characters directly
        } else {
            *pbuf++ = '%';  // Encode unsafe characters as %XX
            *pbuf++ = hex[c >> 4];
            *pbuf++ = hex[c & 0xF];
        }
    }

    *pbuf = '\0';  // Null-terminate the encoded string
    return encoded;
}

/**
 * @brief function to extract the request method from an http request
 * @param request the raw http request
 * @return a char *copy of the request type, or NULL
 */
char* netw_extract_http_request_type(const char* request) {
    __n_assert(request, return NULL);
    // Find the first space in the request string
    const char* space = strchr(request, ' ');

    if (space == NULL) {
        // No space found, invalid request format
        return NULL;
    }

    // Calculate the length of the request type
    size_t method_length = (size_t)(space - request);

    // Allocate memory for the method string (+1 for the null terminator)
    char* method = (char*)malloc(method_length + 1);

    if (method == NULL) {
        // Memory allocation failed
        return NULL;
    }

    // Copy the request method to the allocated memory
    strncpy(method, request, method_length);
    method[method_length] = '\0';  // Null-terminate the string

    return method;
}

/**
 * @brief extract a lot of informations, mostly as pointers, and populate a NETWORK_HTTP_INFO structure
 * @param request the raw http request string
 * @return a NETWORK_HTTP_INFO structure, filled with zeros in case of errors
 */
NETWORK_HTTP_INFO netw_extract_http_info(char* request) {
    NETWORK_HTTP_INFO info = {.content_length = 0, .body = NULL, .type = NULL, .content_type = {0}};

    __n_assert(request, return info);

    // Hold the request-type allocation in a local until just before return.
    // The clang static analyzer's unix.Malloc checker mis-tracks ownership
    // when a malloc'd pointer is parked in a returned-by-value struct member
    // early in the function; keeping it local until the final assignment
    // makes the escape via the returned struct unambiguous to the analyzer.
    char* request_type = netw_extract_http_request_type(request);

    // Find Content-Type header (Optional)
    const char* content_type_header = strstr(request, "Content-Type:");
    if (content_type_header) {
        const char* start = content_type_header + strlen("Content-Type: ");
        const char* end = strstr(start, "\r\n");
        if (end) {
            size_t length = (size_t)(end - start);
            if (length > 255) length = 255;
            strncpy(info.content_type, start, length);
            info.content_type[length] = '\0';
        }
    } else {
        // If no Content-Type header found, set default
        strncpy(info.content_type, "text/plain", sizeof(info.content_type) - 1);
    }

    // Find Content-Length header (Optional)
    const char* content_length_header = strstr(request, "Content-Length:");
    if (content_length_header) {
        const char* start = content_length_header + strlen("Content-Length: ");
        errno = 0;
        unsigned long tmp_cl = strtoul(start, NULL, 10); /* parse once */
        int error = errno;
        // Handle out-of-range error (e.g., set a safe default or return an error)
        // Only when ULONG_MAX is larger than SIZE_MAX, to prevent a constant false comparison warning
#if ULONG_MAX > SIZE_MAX
        if (error == ERANGE || tmp_cl > SIZE_MAX) {
#else
        if (error == ERANGE) {
#endif
            n_log(LOG_ERR, "could not get content_length for request %p, returned %s", request, strerror(error));
            info.content_length = SIZE_MAX; /* clamp to the maximum supported value */
        } else {
            info.content_length = (size_t)tmp_cl;
        }
    }

    // Find the start of the body (after \r\n\r\n)
    const char* body_start = strstr(request, "\r\n\r\n");
    if (body_start) {
        body_start += 4;  // Skip the \r\n\r\n

        // If there is a Content-Length, and it's greater than 0, copy the body
        if (info.content_length > 0 && info.content_length < SIZE_MAX) {
            info.body = malloc(info.content_length + 1);  // Allocate memory for body
            if (info.body) {
                strncpy(info.body, body_start, info.content_length);
                info.body[info.content_length] = '\0';  // Null-terminate the body
            }
        }
    }

    info.type = request_type;
    return info;
}

/**
 * @brief destroy a NETWORK_HTTP_INFO loaded informations
 * @param http_request the parsed NETWORK_HTTP_INFO request
 * @return TRUE ;
 */
int netw_info_destroy(NETWORK_HTTP_INFO http_request) {
    FreeNoLog(http_request.body);
    FreeNoLog(http_request.type);
    return TRUE;
}

/**
 * @brief Helper function to extract the URL from the HTTP request line
 * @param request raw http request to decode
 * @param url pointer to an allocated char *holder
 * @param size the size of the url holder
 * @return TRUE or FALSE
 */
int netw_get_url_from_http_request(const char* request, char* url, size_t size) {
    __n_assert(request && strlen(request) > 0, return FALSE);
    __n_assert(url && size > 1, return FALSE);

    /* Default output */
    strncpy(url, "/", size - 1);
    url[size - 1] = '\0';

    /* Example request line: "GET /path/to/resource HTTP/1.1" */
    const char* first_space = strchr(request, ' ');
    if (!first_space) {
        /* Malformed request, return default '/' */
        return FALSE;
    }

    const char* second_space = strchr(first_space + 1, ' ');
    if (!second_space) {
        /* Malformed request, return default '/' */
        return FALSE;
    }

    size_t len = (size_t)(second_space - first_space - 1);
    if (len >= size) {
        len = size - 1;
    }

    strncpy(url, first_space + 1, len);
    url[len] = '\0'; /* Null-terminate the URL */

    return TRUE;
}

/**
 *@brief Function to decode URL-encoded data
 *@param str input string to decode
 *@return url decoded string
 */
char* netw_urldecode(const char* str) {
    __n_assert(str, return NULL);
    char* decoded = malloc(strlen(str) + 1);
    __n_assert(decoded, return NULL);

    char* p = decoded;
    while (*str) {
        if (*str == '%') {
            if (isxdigit((unsigned char)str[1]) && isxdigit((unsigned char)str[2])) {
                unsigned int value;
                if (sscanf(str + 1, "%2x", &value) >= 1) {
                    *p++ = (char)value;
                    str += 3;
                } else {
                    n_log(LOG_ERR, "sscanf could not parse char *str (%p) for a %%2x", str);
                    Free(decoded);
                    return NULL;
                }
            } else {
                *p++ = *str++;
            }
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

/**
 *@brief Function to parse POST data
 *@param post_data the post data to parse
 *@return An allocated HASH_TABLE with post data as keys/values, or NULL
 */
HASH_TABLE* netw_parse_post_data(const char* post_data) {
    __n_assert(post_data, return NULL);

    // Create a copy of the post_data string because strtok modifies the string
    char* data = strdup(post_data);
    __n_assert(data, return NULL);

    char* pair = data;

    HASH_TABLE* post_data_table = new_ht(32);

    while (pair != NULL) {
        // Find the next key-value pair separated by '&'
        char* ampersand_pos = strchr(pair, '&');

        // If found, replace it with '\0' to isolate the current pair
        if (ampersand_pos != NULL) {
            *ampersand_pos = '\0';
        }

        // Now split each pair by '=' to get the key and value
        char* equal_pos = strchr(pair, '=');
        if (equal_pos != NULL) {
            *equal_pos = '\0';  // Terminate the key string
            const char* key = pair;
            const char* value = equal_pos + 1;

            // Decode the value since POST data is URL-encoded
            char* decoded_value = netw_urldecode(value);
            ht_put_string(post_data_table, key, decoded_value);
            // printf("Key: %s, Value: %s\n", key, decoded_value);
            free(decoded_value);
        }
        // Move to the next key-value pair (if any)
        pair = (ampersand_pos != NULL) ? (ampersand_pos + 1) : NULL;
    }
    // Free the duplicated string
    free(data);
    return post_data_table;
}

/**
 * @brief function to guess the content type based on URL extension
 * @param url the url from which we want to guess the content type
 * @return a pointer to the detected content type. It will be 'unknown' if no detection worked
 */
const char* netw_guess_http_content_type(const char* url) {
    __n_assert(url, return NULL);

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

/**
 * @brief helper function to convert status code to a human-readable message
 * @param status_code the code to convert into a human-readable message
 * @return a pointer to the human readable message. 'Unknown' is returned if the status code is not supported
 */
const char* netw_get_http_status_message(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 204:
            return "No Content";
        case 304:
            return "Not Modified";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
            // Add more status codes as needed
        default:
            return "Unknown";
    }
}

/**
 * @brief helper function to generate the current date in HTTP format
 * @param buffer the target buffer, must be big enough to hold the date
 * @param buffer_size the size of the target buffer
 * @return TRUE or FALSE
 */
int netw_get_http_date(char* buffer, size_t buffer_size) {
    __n_assert(buffer, return FALSE);
    const time_t now = time(NULL);
    struct tm gmt;
#ifdef _WIN32
    if (gmtime_s(&gmt, &now) != 0) {
        n_log(LOG_ERR, "gmtime_s failed");
        return FALSE;
    }
#else
    if (!gmtime_r(&now, &gmt)) {
        n_log(LOG_ERR, "gmtime_r returned NULL");
        return FALSE;
    }
#endif
    if (strftime(buffer, buffer_size, "%a, %d %b %Y %H:%M:%S GMT", &gmt) == 0) {
        n_log(LOG_ERR, "strftime failed: buffer too small");
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief function to dynamically generate an HTTP response
 * @param http_response pointer to a N_STR *response. Will be set to the response, or NULL
 * @param status_code response http status code
 * @param server_name response 'Server' in headers
 * @param content_type response 'Content-Type' in headers
 * @param additional_headers additional response headers, can be "" if no additional headers, else 'backslash r backslash n' separated key: values
 * @param body response 'Server' in headers
 * @return TRUE if the http_response was built, FALSE if not
 */
int netw_build_http_response(N_STR** http_response, int status_code, const char* server_name, const char* content_type, char* additional_headers, N_STR* body) {
    __n_assert(server_name, return FALSE);
    __n_assert(content_type, return FALSE);
    __n_assert(additional_headers, return FALSE);

    const char* status_message = netw_get_http_status_message(status_code);
    const char* connection_type = "close";

    // Buffer for the date header
    char date_buffer[128] = "";
    netw_get_http_date(date_buffer, sizeof(date_buffer));

    if ((*http_response)) {
        (*http_response)->written = 0;
    }

    if (!body || body->written == 0) {
        // Handle the case where there is no body
        nstrprintf((*http_response),
                   "HTTP/1.1 %d %s\r\n"
                   "Date: %s\r\n"
                   "Server: %s\r\n"
                   "Content-Length: 0\r\n"
                   "%s"
                   "Connection: %s\r\n\r\n",
                   status_code, status_message, date_buffer, server_name, additional_headers, connection_type);
        n_log(LOG_DEBUG, "empty response");
    } else {
        // Build the response with body
        nstrprintf((*http_response),
                   "HTTP/1.1 %d %s\r\n"
                   "Date: %s\r\n"
                   "Server: %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "%s"
                   "Connection: %s\r\n\r\n",
                   status_code, status_message, date_buffer, server_name, content_type, body->written, additional_headers, connection_type);
        nstrcat((*http_response), body);
        n_log(LOG_DEBUG, "body response");
    }
    return TRUE;
}

#ifdef HAVE_OPENSSL

/*! @brief write bytes to a WebSocket connection (SSL or plain)
 *  @param conn WebSocket connection
 *  @param buf data to write
 *  @param len number of bytes
 *  @return number of bytes written, or -1 on error */
static ssize_t _ws_write(N_WS_CONN* conn, const void* buf, size_t len) {
    __n_assert(conn, return -1);
    __n_assert(conn->netw, return -1);
    if (conn->netw->crypto_algo == NETW_ENCRYPT_OPENSSL && conn->netw->ssl) {
        int ret = SSL_write(conn->netw->ssl, buf, (int)len);
        return (ret > 0) ? (ssize_t)ret : -1;
    }
    ssize_t ret = send(conn->netw->link.sock, buf, NETW_BUFLEN_CAST(len), NETFLAGS);
    return ret;
}

/*! @brief read exactly len bytes from a WebSocket connection
 *  @param conn WebSocket connection
 *  @param buf output buffer
 *  @param len number of bytes to read
 *  @return number of bytes read, or -1 on error */
static ssize_t _ws_read(N_WS_CONN* conn, void* buf, size_t len) {
    __n_assert(conn, return -1);
    __n_assert(conn->netw, return -1);
    size_t total = 0;
    char* p = (char*)buf;
    while (total < len) {
        ssize_t ret = 0;
        if (conn->netw->crypto_algo == NETW_ENCRYPT_OPENSSL && conn->netw->ssl) {
            ret = SSL_read(conn->netw->ssl, p + total, (int)(len - total));
        } else {
            ret = recv(conn->netw->link.sock, p + total, NETW_BUFLEN_CAST(len - total), 0);
        }
        if (ret <= 0) {
            return -1;
        }
        total += (size_t)ret;
    }
    return (ssize_t)total;
}

/**
 * @brief Connect to a WebSocket server (ws:// or wss://).
 *
 * Performs the HTTP/1.1 upgrade handshake per RFC 6455.
 *
 * @param host hostname to connect to
 * @param port port string (e.g. "443")
 * @param path resource path (e.g. "/")
 * @param use_ssl 1 for wss://, 0 for ws://
 * @return new N_WS_CONN, or NULL on failure
 */
N_WS_CONN* n_ws_connect(const char* host, const char* port, const char* path, int use_ssl) {
    __n_assert(host, return NULL);
    __n_assert(port, return NULL);
    __n_assert(path, return NULL);

    N_WS_CONN* conn = NULL;
    Malloc(conn, N_WS_CONN, 1);
    __n_assert(conn, return NULL);

    conn->netw = NULL;
    conn->connected = 0;
    conn->host = strdup(host);
    conn->path = strdup(path);
    if (!conn->host || !conn->path) {
        n_log(LOG_ERR, "n_ws_connect: strdup failed");
        goto ws_connect_fail;
    }

    /* establish TCP + optional SSL */
    if (use_ssl) {
        if (netw_ssl_connect_client(&conn->netw, (char*)host, (char*)port, NETWORK_IPALL) == FALSE) {
            n_log(LOG_ERR, "n_ws_connect: TCP+SSL context setup failed for %s:%s", host, port);
            goto ws_connect_fail;
        }
        if (netw_ssl_do_handshake(conn->netw, host) == FALSE) {
            _netw_capture_error(conn->netw, "n_ws_connect: SSL handshake failed for %s:%s", host, port);
            n_log(LOG_ERR, "n_ws_connect: SSL handshake failed for %s:%s", host, port);
            goto ws_connect_fail;
        }
    } else {
        if (netw_connect(&conn->netw, (char*)host, (char*)port, NETWORK_IPALL) == FALSE) {
            n_log(LOG_ERR, "n_ws_connect: TCP connect failed for %s:%s", host, port);
            goto ws_connect_fail;
        }
    }

    /* generate Sec-WebSocket-Key: base64 of 16 random bytes */
    unsigned char rand_bytes[16];
    if (RAND_bytes(rand_bytes, 16) != 1) {
        _netw_capture_error(conn->netw, "n_ws_connect: RAND_bytes failed");
        n_log(LOG_ERR, "n_ws_connect: RAND_bytes failed");
        goto ws_connect_fail;
    }

    N_STR* rand_nstr = new_nstr(16);
    __n_assert(rand_nstr, goto ws_connect_fail);
    memcpy(rand_nstr->data, rand_bytes, 16);
    rand_nstr->written = 16;

    N_STR* ws_key_nstr = n_base64_encode(rand_nstr);
    free_nstr(&rand_nstr);
    __n_assert(ws_key_nstr, goto ws_connect_fail);
    /* strip any trailing whitespace from base64 output */
    while (ws_key_nstr->written > 0 &&
           (ws_key_nstr->data[ws_key_nstr->written - 1] == '\n' ||
            ws_key_nstr->data[ws_key_nstr->written - 1] == '\r' ||
            ws_key_nstr->data[ws_key_nstr->written - 1] == ' ')) {
        ws_key_nstr->data[--ws_key_nstr->written] = '\0';
    }

    /* build HTTP upgrade request */
    char upgrade_req[2048];
    int req_len = snprintf(upgrade_req, sizeof(upgrade_req),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "\r\n",
                           path, host, ws_key_nstr->data);

    if (req_len < 0 || (size_t)req_len >= sizeof(upgrade_req)) {
        _netw_capture_error(conn->netw, "n_ws_connect: upgrade request too large");
        n_log(LOG_ERR, "n_ws_connect: upgrade request too large");
        free_nstr(&ws_key_nstr);
        goto ws_connect_fail;
    }

    /* send upgrade request */
    if (_ws_write(conn, upgrade_req, (size_t)req_len) < 0) {
        _netw_capture_error(conn->netw, "n_ws_connect: failed to send upgrade request");
        n_log(LOG_ERR, "n_ws_connect: failed to send upgrade request");
        free_nstr(&ws_key_nstr);
        goto ws_connect_fail;
    }

    /* read response (up to 4096 bytes, look for \r\n\r\n) */
    char resp_buf[4096];
    memset(resp_buf, 0, sizeof(resp_buf));
    size_t resp_len = 0;
    while (resp_len < sizeof(resp_buf) - 1) {
        ssize_t r = 0;
        if (conn->netw->crypto_algo == NETW_ENCRYPT_OPENSSL && conn->netw->ssl) {
            r = SSL_read(conn->netw->ssl, resp_buf + resp_len, 1);
        } else {
            r = recv(conn->netw->link.sock, resp_buf + resp_len, 1, 0);
        }
        if (r <= 0) {
            _netw_capture_error(conn->netw, "n_ws_connect: failed reading handshake response");
            n_log(LOG_ERR, "n_ws_connect: failed reading handshake response");
            free_nstr(&ws_key_nstr);
            goto ws_connect_fail;
        }
        resp_len += (size_t)r;
        if (resp_len >= 4 &&
            resp_buf[resp_len - 4] == '\r' && resp_buf[resp_len - 3] == '\n' &&
            resp_buf[resp_len - 2] == '\r' && resp_buf[resp_len - 1] == '\n') {
            break;
        }
    }
    resp_buf[resp_len] = '\0';

    /* verify 101 Switching Protocols */
    if (strstr(resp_buf, "101") == NULL) {
        _netw_capture_error(conn->netw, "n_ws_connect: server did not return 101: %.128s", resp_buf);
        n_log(LOG_ERR, "n_ws_connect: server did not return 101: %.128s", resp_buf);
        free_nstr(&ws_key_nstr);
        goto ws_connect_fail;
    }

    /* verify Sec-WebSocket-Accept */
    static const char ws_magic[] = "258EAFA5-E914-47DA-95CA-5AB5FE44F513";
    char concat_key[256];
    snprintf(concat_key, sizeof(concat_key), "%s%s", ws_key_nstr->data, ws_magic);
    n_log(LOG_DEBUG, "n_ws_connect: key sent: [%s] len:%zu", ws_key_nstr->data, ws_key_nstr->written);
    n_log(LOG_DEBUG, "n_ws_connect: concat_key: [%s]", concat_key);
    free_nstr(&ws_key_nstr);

    unsigned char sha1_hash[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char*)concat_key, strlen(concat_key), sha1_hash);

    N_STR* sha1_nstr = new_nstr(SHA_DIGEST_LENGTH);
    __n_assert(sha1_nstr, goto ws_connect_fail);
    memcpy(sha1_nstr->data, sha1_hash, SHA_DIGEST_LENGTH);
    sha1_nstr->written = SHA_DIGEST_LENGTH;

    N_STR* expected_accept = n_base64_encode(sha1_nstr);
    free_nstr(&sha1_nstr);
    __n_assert(expected_accept, goto ws_connect_fail);
    /* strip trailing whitespace from base64 output */
    while (expected_accept->written > 0 &&
           (expected_accept->data[expected_accept->written - 1] == '\n' ||
            expected_accept->data[expected_accept->written - 1] == '\r' ||
            expected_accept->data[expected_accept->written - 1] == ' ')) {
        expected_accept->data[--expected_accept->written] = '\0';
    }

    /* find Sec-WebSocket-Accept in response (case-insensitive search) */
    const char* accept_hdr = NULL;
    char* search_pos = resp_buf;
    while (*search_pos) {
        if (strncasecmp(search_pos, "Sec-WebSocket-Accept:", 21) == 0) {
            accept_hdr = search_pos + 21;
            break;
        }
        search_pos++;
    }
    if (!accept_hdr) {
        _netw_capture_error(conn->netw, "n_ws_connect: no Sec-WebSocket-Accept header in response");
        n_log(LOG_ERR, "n_ws_connect: no Sec-WebSocket-Accept header in response");
        free_nstr(&expected_accept);
        goto ws_connect_fail;
    }
    /* skip whitespace */
    while (*accept_hdr == ' ') accept_hdr++;
    /* compare up to expected length; trim trailing \r\n from accept_hdr */
    char accept_val[128];
    {
        int ai = 0;
        while (accept_hdr[ai] && accept_hdr[ai] != '\r' && accept_hdr[ai] != '\n' && ai < (int)sizeof(accept_val) - 1) {
            accept_val[ai] = accept_hdr[ai];
            ai++;
        }
        accept_val[ai] = '\0';
    }
    if (strcmp(accept_val, expected_accept->data) != 0) {
        /* Many proxies (Fly.io, Cloudflare, etc.) re-key the handshake,
         * so the accept may not match. Log as debug, not error. */
        n_log(LOG_DEBUG, "n_ws_connect: Sec-WebSocket-Accept mismatch (proxy?): got [%s] expected [%s]",
              accept_val, expected_accept->data);
    }
    free_nstr(&expected_accept);

    conn->connected = 1;
    n_log(LOG_INFO, "n_ws_connect: WebSocket handshake completed with %s:%s%s", host, port, path);
    return conn;

ws_connect_fail:
    n_ws_conn_free(&conn);
    return NULL;
}

/**
 * @brief Send a WebSocket frame (client always masks).
 *
 * Builds a masked WebSocket frame per RFC 6455 and sends it.
 *
 * @param conn WebSocket connection
 * @param payload data to send
 * @param len payload length
 * @param opcode frame opcode (N_WS_OP_TEXT, etc.)
 * @return 0 on success, -1 on error
 */
int n_ws_send(N_WS_CONN* conn, const char* payload, size_t len, int opcode) {
    __n_assert(conn, return -1);
    __n_assert(conn->netw, return -1);

    /* max frame overhead: 2 (header) + 8 (ext len) + 4 (mask) = 14 */
    size_t frame_max = 14 + len;
    unsigned char* frame = NULL;
    Malloc(frame, unsigned char, frame_max);
    __n_assert(frame, return -1);

    size_t pos = 0;

    /* byte 0: FIN + opcode */
    frame[pos++] = (unsigned char)(0x80 | (opcode & 0x0F));

    /* byte 1+: mask bit set + payload length */
    if (len <= 125) {
        frame[pos++] = (unsigned char)(0x80 | len);
    } else if (len <= 65535) {
        frame[pos++] = (unsigned char)(0x80 | 126);
        frame[pos++] = (unsigned char)((len >> 8) & 0xFF);
        frame[pos++] = (unsigned char)(len & 0xFF);
    } else {
        frame[pos++] = (unsigned char)(0x80 | 127);
        for (int i = 7; i >= 0; i--) {
            frame[pos++] = (unsigned char)((len >> (8 * i)) & 0xFF);
        }
    }

    /* masking key: 4 random bytes */
    unsigned char mask_key[4];
    if (RAND_bytes(mask_key, 4) != 1) {
        _netw_capture_error(conn->netw, "n_ws_send: RAND_bytes failed for mask key");
        n_log(LOG_ERR, "n_ws_send: RAND_bytes failed for mask key");
        FreeNoLog(frame);
        return -1;
    }
    memcpy(frame + pos, mask_key, 4);
    pos += 4;

    /* masked payload */
    if (payload && len > 0) {
        for (size_t i = 0; i < len; i++) {
            frame[pos + i] = (unsigned char)((unsigned char)payload[i] ^ mask_key[i % 4]);
        }
        pos += len;
    }

    ssize_t written = _ws_write(conn, frame, pos);
    FreeNoLog(frame);
    if (written < 0 || (size_t)written != pos) {
        _netw_capture_error(conn->netw, "n_ws_send: write failed (wrote %zd of %zu)", written, pos);
        n_log(LOG_ERR, "n_ws_send: write failed (wrote %zd of %zu)", written, pos);
        return -1;
    }
    return 0;
}

/**
 * @brief Receive one WebSocket frame.
 *
 * Reads the frame header, extended length, optional mask, and payload.
 *
 * @param conn WebSocket connection
 * @param msg_out output: populated message. Caller must free payload with free_nstr.
 * @return 0 on success, -1 on error or connection closed
 */
int n_ws_recv(N_WS_CONN* conn, N_WS_MESSAGE* msg_out) {
    __n_assert(conn, return -1);
    __n_assert(conn->netw, return -1);
    __n_assert(msg_out, return -1);

    memset(msg_out, 0, sizeof(*msg_out));

    /* read 2-byte header */
    unsigned char hdr[2];
    if (_ws_read(conn, hdr, 2) < 0) {
        return -1;
    }

    /* int fin = (hdr[0] >> 7) & 1; */
    int opcode = hdr[0] & 0x0F;
    int mask_bit = (hdr[1] >> 7) & 1;
    uint64_t payload_len = hdr[1] & 0x7F;

    /* extended payload length */
    if (payload_len == 126) {
        unsigned char ext[2];
        if (_ws_read(conn, ext, 2) < 0) return -1;
        payload_len = ((uint64_t)ext[0] << 8) | (uint64_t)ext[1];
    } else if (payload_len == 127) {
        unsigned char ext[8];
        if (_ws_read(conn, ext, 8) < 0) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | (uint64_t)ext[i];
        }
    }

    /* read masking key if present */
    unsigned char mask_key[4] = {0};
    if (mask_bit) {
        if (_ws_read(conn, mask_key, 4) < 0) return -1;
    }

    /* read payload */
    N_STR* payload = new_nstr((size_t)payload_len + 1);
    __n_assert(payload, return -1);

    if (payload_len > 0) {
        if (_ws_read(conn, payload->data, (size_t)payload_len) < 0) {
            free_nstr(&payload);
            return -1;
        }
        /* unmask if needed */
        if (mask_bit) {
            for (uint64_t i = 0; i < payload_len; i++) {
                payload->data[i] = (char)((unsigned char)payload->data[i] ^ mask_key[i % 4]);
            }
        }
    }
    payload->written = (size_t)payload_len;
    payload->data[payload_len] = '\0';

    msg_out->opcode = opcode;
    msg_out->payload = payload;
    msg_out->masked = mask_bit;

    return 0;
}

/**
 * @brief Send close frame and close the connection.
 *
 * Sends a close frame (opcode 0x08), attempts to read a close response,
 * then closes the underlying NETWORK.
 *
 * @param conn WebSocket connection
 */
void n_ws_close(N_WS_CONN* conn) {
    __n_assert(conn, return);

    if (conn->connected && conn->netw) {
        /* send close frame with empty payload */
        n_ws_send(conn, NULL, 0, N_WS_OP_CLOSE);

        /* try to read close response with a short timeout */
        /* set non-blocking or just try one read */
        N_WS_MESSAGE msg;
        memset(&msg, 0, sizeof(msg));
        /* best effort: read one frame, ignore errors */
        if (n_ws_recv(conn, &msg) == 0) {
            free_nstr(&msg.payload);
        }
        conn->connected = 0;
    }

    if (conn->netw) {
        netw_close(&conn->netw);
    }
}

/**
 * @brief Free a WebSocket connection structure.
 *
 * Closes the connection if still open, then frees all members.
 *
 * @param conn pointer to pointer, set to NULL
 */
void n_ws_conn_free(N_WS_CONN** conn) {
    __n_assert(conn, return);
    __n_assert(*conn, return);

    N_WS_CONN* c = *conn;

    if (c->connected) {
        n_ws_close(c);
    }

    if (c->netw) {
        netw_close(&c->netw);
    }

    FreeNoLog(c->host);
    FreeNoLog(c->path);
    FreeNoLog(c);
    *conn = NULL;
}

/**
 * @brief Free the contents of an SSE event (does not free the struct itself).
 * @param event the event to clean
 */
void n_sse_event_clean(N_SSE_EVENT* event) {
    if (!event) return;
    if (event->event) free_nstr(&event->event);
    if (event->data) free_nstr(&event->data);
    if (event->id) free_nstr(&event->id);
    event->retry = 0;
}

/**
 * @brief Signal the SSE connection to stop reading.
 * @param conn SSE connection
 */
void n_sse_stop(N_SSE_CONN* conn) {
    __n_assert(conn, return);
    __atomic_store_n(&conn->stop_flag, 1, __ATOMIC_RELEASE);
}

/**
 * @brief Free an SSE connection structure.
 * @param conn pointer to pointer, set to NULL
 */
void n_sse_conn_free(N_SSE_CONN** conn) {
    __n_assert(conn, return);
    __n_assert(*conn, return);

    N_SSE_CONN* c = *conn;
    if (c->netw) {
        netw_close(&c->netw);
    }
    FreeNoLog(c);
    *conn = NULL;
}

/*! @brief read one byte from an SSE connection (SSL or plain).
 *  Uses a short poll timeout so the caller can check stop_flag.
 *  @param netw NETWORK connection
 *  @param ch output byte
 *  @param stop_flag pointer to atomic stop flag (checked on timeout)
 *  @return 1 on success, 0 on timeout (retry), -1 on error or EOF */
static ssize_t _sse_read_byte(NETWORK* netw, char* ch, volatile int* stop_flag) {
    __n_assert(netw, return -1);

    /* poll with 500ms timeout so we can check stop_flag periodically */
    for (;;) {
        if (stop_flag && __atomic_load_n(stop_flag, __ATOMIC_ACQUIRE)) {
            return -1;
        }
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; /* 500ms */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(netw->link.sock, &fds);

        /* For SSL: check if there's already buffered data */
        int ready = 0;
        if (netw->crypto_algo == NETW_ENCRYPT_OPENSSL && netw->ssl) {
            if (SSL_pending(netw->ssl) > 0) {
                ready = 1;
            }
        }
        if (!ready) {
            int sel = select((int)netw->link.sock + 1, &fds, NULL, NULL, &tv);
            if (sel < 0) return -1; /* error */
            if (sel == 0) continue; /* timeout, loop back to check stop_flag */
        }

        ssize_t ret = 0;
        if (netw->crypto_algo == NETW_ENCRYPT_OPENSSL && netw->ssl) {
            ret = SSL_read(netw->ssl, ch, 1);
        } else {
            ret = recv(netw->link.sock, ch, 1, 0);
        }
        return (ret == 1) ? 1 : -1;
    }
}

/*! @brief write bytes to an SSE connection (SSL or plain)
 *  @param netw NETWORK connection
 *  @param buf data to write
 *  @param len number of bytes
 *  @return number of bytes written, or -1 on error */
static ssize_t _sse_write(NETWORK* netw, const void* buf, size_t len) {
    __n_assert(netw, return -1);
    if (netw->crypto_algo == NETW_ENCRYPT_OPENSSL && netw->ssl) {
        int ret = SSL_write(netw->ssl, buf, (int)len);
        return (ret > 0) ? (ssize_t)ret : -1;
    }
    ssize_t ret = send(netw->link.sock, buf, NETW_BUFLEN_CAST(len), NETFLAGS);
    return ret;
}

/**
 * @brief Connect to an SSE endpoint and start reading events.
 *
 * Blocks until stopped or connection closes. Calls on_event for each
 * complete SSE event received.
 *
 * @param host hostname
 * @param port port string
 * @param path resource path
 * @param use_ssl 1 for https, 0 for http
 * @param on_event callback for each received event
 * @param user_data passed to callback
 * @return new N_SSE_CONN, or NULL on failure
 */
N_SSE_CONN* n_sse_connect(const char* host, const char* port, const char* path, int use_ssl, const char* user_agent, void (*on_event)(N_SSE_EVENT*, N_SSE_CONN*, void*), void* user_data) {
    __n_assert(host, return NULL);
    __n_assert(port, return NULL);
    __n_assert(path, return NULL);
    __n_assert(on_event, return NULL);

    N_SSE_CONN* conn = NULL;
    Malloc(conn, N_SSE_CONN, 1);
    __n_assert(conn, return NULL);

    conn->netw = NULL;
    conn->stop_flag = 0;
    conn->on_event = on_event;
    conn->user_data = user_data;

    /* establish TCP + optional SSL */
    if (use_ssl) {
        if (netw_ssl_connect_client(&conn->netw, (char*)host, (char*)port, NETWORK_IPALL) == FALSE) {
            n_log(LOG_ERR, "n_sse_connect: TCP+SSL context setup failed for %s:%s", host, port);
            goto sse_connect_fail;
        }
        if (netw_ssl_do_handshake(conn->netw, host) == FALSE) {
            _netw_capture_error(conn->netw, "n_sse_connect: SSL handshake failed for %s:%s", host, port);
            n_log(LOG_ERR, "n_sse_connect: SSL handshake failed for %s:%s", host, port);
            goto sse_connect_fail;
        }
    } else {
        if (netw_connect(&conn->netw, (char*)host, (char*)port, NETWORK_IPALL) == FALSE) {
            n_log(LOG_ERR, "n_sse_connect: TCP connect failed for %s:%s", host, port);
            goto sse_connect_fail;
        }
    }

    /* send HTTP GET with SSE headers */
    char request[4096];
    int req_len;
    if (user_agent && user_agent[0]) {
        req_len = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Accept: text/event-stream\r\n"
                           "Cache-Control: no-cache\r\n"
                           "Connection: keep-alive\r\n"
                           "User-Agent: %s\r\n"
                           "\r\n",
                           path, host, user_agent);
    } else {
        req_len = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Accept: text/event-stream\r\n"
                           "Cache-Control: no-cache\r\n"
                           "Connection: keep-alive\r\n"
                           "\r\n",
                           path, host);
    }

    if (req_len < 0 || (size_t)req_len >= sizeof(request)) {
        _netw_capture_error(conn->netw, "n_sse_connect: request too large");
        n_log(LOG_ERR, "n_sse_connect: request too large");
        goto sse_connect_fail;
    }

    if (_sse_write(conn->netw, request, (size_t)req_len) < 0) {
        _netw_capture_error(conn->netw, "n_sse_connect: failed to send HTTP request");
        n_log(LOG_ERR, "n_sse_connect: failed to send HTTP request");
        goto sse_connect_fail;
    }

    /* read HTTP response headers (byte by byte until \r\n\r\n) */
    char resp_buf[8192];
    size_t resp_len = 0;
    while (resp_len < sizeof(resp_buf) - 1) {
        char ch = 0;
        if (_sse_read_byte(conn->netw, &ch, NULL) < 0) {
            _netw_capture_error(conn->netw, "n_sse_connect: failed reading HTTP response");
            n_log(LOG_ERR, "n_sse_connect: failed reading HTTP response");
            goto sse_connect_fail;
        }
        resp_buf[resp_len++] = ch;
        if (resp_len >= 4 &&
            resp_buf[resp_len - 4] == '\r' && resp_buf[resp_len - 3] == '\n' &&
            resp_buf[resp_len - 2] == '\r' && resp_buf[resp_len - 1] == '\n') {
            break;
        }
    }
    resp_buf[resp_len] = '\0';

    /* verify HTTP 200 status */
    if (strstr(resp_buf, "200") == NULL) {
        _netw_capture_error(conn->netw, "n_sse_connect: server did not return 200: %.128s", resp_buf);
        n_log(LOG_ERR, "n_sse_connect: server did not return 200: %.128s", resp_buf);
        goto sse_connect_fail;
    }

    n_log(LOG_INFO, "n_sse_connect: SSE connected to %s:%s%s", host, port, path);

    /* enter SSE read loop */
    {
        N_SSE_EVENT current;
        memset(&current, 0, sizeof(current));

        /* line buffer for SSE parsing */
        char line[8192];
        size_t line_len = 0;

        while (!__atomic_load_n(&conn->stop_flag, __ATOMIC_ACQUIRE)) {
            char ch = 0;
            if (_sse_read_byte(conn->netw, &ch, &conn->stop_flag) < 0) {
                n_log(LOG_DEBUG, "n_sse_connect: connection closed or read error");
                break;
            }

            if (ch == '\n') {
                /* terminate line (strip trailing \r) */
                if (line_len > 0 && line[line_len - 1] == '\r') {
                    line_len--;
                }
                line[line_len] = '\0';

                if (line_len == 0) {
                    /* empty line = dispatch event if we have data */
                    if (current.data) {
                        conn->on_event(&current, conn, conn->user_data);
                        n_sse_event_clean(&current);
                        memset(&current, 0, sizeof(current));
                    }
                } else if (line[0] == ':') {
                    /* comment line, ignore */
                    n_log(LOG_DEBUG, "n_sse_connect: comment: %s", line + 1);
                } else {
                    /* parse field:value */
                    char* colon = strchr(line, ':');
                    const char* field = line;
                    const char* value = "";
                    if (colon) {
                        *colon = '\0';
                        value = colon + 1;
                        /* skip single leading space after colon */
                        if (*value == ' ') value++;
                    }

                    if (strcmp(field, "data") == 0) {
                        if (current.data) {
                            /* append newline + value to existing data */
                            nstrprintf_cat(current.data, "\n%s", value);
                        } else {
                            current.data = char_to_nstr((char*)value);
                        }
                    } else if (strcmp(field, "event") == 0) {
                        if (current.event) free_nstr(&current.event);
                        current.event = char_to_nstr((char*)value);
                    } else if (strcmp(field, "id") == 0) {
                        if (current.id) free_nstr(&current.id);
                        current.id = char_to_nstr((char*)value);
                    } else if (strcmp(field, "retry") == 0) {
                        current.retry = atoi(value);
                    }
                }

                line_len = 0;
            } else {
                if (line_len < sizeof(line) - 1) {
                    line[line_len++] = ch;
                }
            }
        }

        /* clean up any partial event */
        n_sse_event_clean(&current);
    }

    return conn;

sse_connect_fail:
    n_sse_conn_free(&conn);
    return NULL;
}

#endif /* HAVE_OPENSSL */

/**
 * @brief Parse a raw HTTP request buffer into an N_HTTP_REQUEST.
 * @param buf raw request data (null-terminated)
 * @param req output request structure (must be zeroed by caller)
 */
static void _n_mock_parse_request(const char* buf, N_HTTP_REQUEST* req) {
    if (!buf || !req) return;

    /* parse request line: METHOD PATH?QUERY HTTP/1.x */
    const char* line_end = strstr(buf, "\r\n");
    if (!line_end) line_end = strchr(buf, '\n');
    if (!line_end) return;

    size_t line_len = (size_t)(line_end - buf);
    char line[4096];
    if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
    memcpy(line, buf, line_len);
    line[line_len] = '\0';

    /* method */
    char* sp1 = strchr(line, ' ');
    if (!sp1) return;
    size_t mlen = (size_t)(sp1 - line);
    if (mlen >= sizeof(req->method)) mlen = sizeof(req->method) - 1;
    memcpy(req->method, line, mlen);
    req->method[mlen] = '\0';

    /* path and query */
    sp1++;
    char* sp2 = strchr(sp1, ' ');
    if (sp2) *sp2 = '\0';
    char* qmark = strchr(sp1, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(req->query, qmark + 1, sizeof(req->query) - 1);
        req->query[sizeof(req->query) - 1] = '\0';
    }
    strncpy(req->path, sp1, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';

    /* headers: skip past request line */
    const char* hdr_start = line_end;
    if (*hdr_start == '\r') hdr_start++;
    if (*hdr_start == '\n') hdr_start++;

    req->headers = new_generic_list(0);

    const char* body_start = NULL;
    while (hdr_start && *hdr_start) {
        const char* next = strstr(hdr_start, "\r\n");
        if (!next) next = strchr(hdr_start, '\n');
        if (!next) break;

        size_t hlen = (size_t)(next - hdr_start);
        if (hlen == 0) {
            /* blank line = end of headers */
            body_start = next;
            if (*body_start == '\r') body_start++;
            if (*body_start == '\n') body_start++;
            break;
        }

        char* header_line = strndup(hdr_start, hlen);
        if (header_line) {
            list_push(req->headers, header_line, free);
        }

        hdr_start = next;
        if (*hdr_start == '\r') hdr_start++;
        if (*hdr_start == '\n') hdr_start++;
    }

    /* body */
    if (body_start && *body_start) {
        req->body = char_to_nstr(body_start);
    }
}

/**
 * @brief Free the contents of an N_HTTP_REQUEST (does not free the struct itself).
 * @param req request to clean
 */
static void _n_mock_request_clean(N_HTTP_REQUEST* req) {
    if (!req) return;
    if (req->headers) list_destroy(&req->headers);
    if (req->body) free_nstr(&req->body);
}

/**
 * @brief Start a mock HTTP server: set up listener and return immediately.
 * @param port port number to listen on
 * @param on_request callback for each received HTTP request
 * @param user_data arbitrary pointer passed to callback
 * @return new N_MOCK_SERVER handle, or NULL on failure
 */
N_MOCK_SERVER* n_mock_server_start(int port,
                                   void (*on_request)(N_HTTP_REQUEST*, N_HTTP_RESPONSE*, void*),
                                   void* user_data) {
    __n_assert(on_request, return NULL);

    N_MOCK_SERVER* server = NULL;
    Malloc(server, N_MOCK_SERVER, 1);
    __n_assert(server, return NULL);
    memset(server, 0, sizeof(*server));

    server->on_request = on_request;
    server->user_data = user_data;
    server->port = port;
    server->stop_flag = 0;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    NETWORK* listener = NULL;
    if (netw_make_listening(&listener, NULL, port_str, 5, NETWORK_IPALL) != TRUE) {
        n_log(LOG_ERR, "n_mock_server_start: failed to listen on port %d", port);
        FreeNoLog(server);
        return NULL;
    }
    server->listener = listener;

    n_log(LOG_INFO, "mock server listening on port %d", port);
    return server;
}

/**
 * @brief Run the mock server accept loop. Blocks until stop_flag is set.
 * @param server mock server handle
 */
void n_mock_server_run(N_MOCK_SERVER* server) {
    __n_assert(server, return);
    __n_assert(server->listener, return);

    while (!__atomic_load_n(&server->stop_flag, __ATOMIC_ACQUIRE)) {
        /* Use select with timeout to avoid blocking forever */
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server->listener->link.sock, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; /* 200ms */
        int sel = select((int)(server->listener->link.sock + 1), &readfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        /* Accept connection using netw_accept_from_ex with 1000ms timeout */
        int ret = 0;
        NETWORK* client = netw_accept_from_ex(server->listener, 0, 0, 1000, &ret);
        if (!client) continue;

        /* Read HTTP request */
        char buf[8192];
        ssize_t n = recv(client->link.sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            netw_close(&client);
            continue;
        }
        buf[n] = '\0';

        /* Parse request */
        N_HTTP_REQUEST req;
        memset(&req, 0, sizeof(req));
        _n_mock_parse_request(buf, &req);

        /* Prepare default response */
        N_HTTP_RESPONSE resp;
        memset(&resp, 0, sizeof(resp));
        resp.status_code = 404;
        strncpy(resp.content_type, "text/plain", sizeof(resp.content_type) - 1);

        /* Call handler */
        server->on_request(&req, &resp, server->user_data);

        /* Build HTTP response */
        const char* status_msg = netw_get_http_status_message(resp.status_code);
        if (!status_msg) status_msg = "Unknown";

        size_t body_len = 0;
        const char* body_data = "";
        if (resp.body && resp.body->data) {
            body_data = resp.body->data;
            body_len = resp.body->written;
        }

        char header_buf[1024];
        int hlen = snprintf(header_buf, sizeof(header_buf),
                            "HTTP/1.1 %d %s\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %zu\r\n"
                            "Connection: close\r\n"
                            "\r\n",
                            resp.status_code, status_msg,
                            resp.content_type,
                            body_len);

        if (hlen > 0) {
            send(client->link.sock, header_buf, NETW_BUFLEN_CAST(hlen), NETFLAGS);
        }
        if (body_len > 0) {
            send(client->link.sock, body_data, NETW_BUFLEN_CAST(body_len), NETFLAGS);
        }

        /* Cleanup */
        if (resp.body) free_nstr(&resp.body);
        _n_mock_request_clean(&req);
        netw_close(&client);
    }

    n_log(LOG_INFO, "mock server stopped");
}

/**
 * @brief Signal the mock server to stop accepting connections.
 * @param server mock server handle
 */
void n_mock_server_stop(N_MOCK_SERVER* server) {
    __n_assert(server, return);
    __atomic_store_n(&server->stop_flag, 1, __ATOMIC_RELEASE);
}

/**
 * @brief Free a mock server and close the listening socket.
 * @param server pointer to pointer, set to NULL
 */
void n_mock_server_free(N_MOCK_SERVER** server) {
    __n_assert(server && *server, return);
    if ((*server)->listener) {
        netw_close(&(*server)->listener);
    }
    FreeNoLog(*server);
    *server = NULL;
}

static void _n_parse_query_params(N_URL* u, const char* qs) {
    if (!u || !qs || !qs[0]) return;
    char* buf = strdup(qs);
    if (!buf) return;
    char* saveptr = NULL;
    const char* tok = strtok_r(buf, "&", &saveptr);
    while (tok && u->nb_params < N_URL_MAX_PARAMS) {
        char* eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            u->params[u->nb_params].key = strdup(tok);
            u->params[u->nb_params].value = strdup(eq + 1);
        } else {
            u->params[u->nb_params].key = strdup(tok);
            u->params[u->nb_params].value = strdup("");
        }
        u->nb_params++;
        tok = strtok_r(NULL, "&", &saveptr);
    }
    free(buf);
}

N_URL* n_url_parse(const char* url) {
    if (!url) return NULL;
    N_URL* u = NULL;
    Malloc(u, N_URL, 1);
    if (!u) return NULL;
    memset(u, 0, sizeof(*u));
    const char* p = url;
    const char* scheme_end = strstr(p, "://");
    if (scheme_end) {
        u->scheme = strndup(p, (size_t)(scheme_end - p));
        p = scheme_end + 3;
    } else {
        u->scheme = strdup("http");
    }
    const char* host_end = p;
    while (*host_end && *host_end != '/' && *host_end != '?' && *host_end != ':') host_end++;
    u->host = strndup(p, (size_t)(host_end - p));
    p = host_end;
    if (*p == ':') {
        p++;
        u->port = atoi(p);
        while (*p && *p != '/' && *p != '?') p++;
    }
    if (*p == '/') {
        const char* pe = p;
        while (*pe && *pe != '?') pe++;
        u->path = strndup(p, (size_t)(pe - p));
        p = pe;
    } else {
        u->path = strdup("/");
    }
    if (*p == '?') {
        p++;
        u->query = strdup(p);
        _n_parse_query_params(u, p);
    }
    return u;
}

N_STR* n_url_build(const N_URL* u) {
    if (!u) return NULL;
    N_STR* result = new_nstr(512);
    if (!result) return NULL;
    nstrprintf(result, "%s://%s", u->scheme ? u->scheme : "http", u->host ? u->host : "localhost");
    if (u->port > 0) {
        int dp = (u->scheme && strcmp(u->scheme, "https") == 0) ? 443 : 80;
        if (u->port != dp) nstrprintf_cat(result, ":%d", u->port);
    }
    nstrprintf_cat(result, "%s", u->path ? u->path : "/");
    if (u->query && u->query[0]) nstrprintf_cat(result, "?%s", u->query);
    return result;
}

N_STR* n_url_encode(const char* str) {
    return n_str_url_encode(str);
}

N_STR* n_url_decode(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    N_STR* result = new_nstr(len + 1);
    if (!result) return NULL;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len && isxdigit((unsigned char)str[i + 1]) && isxdigit((unsigned char)str[i + 2])) {
            const char hex[3] = {str[i + 1], str[i + 2], '\0'};
            unsigned int val = 0;
            sscanf(hex, "%x", &val);
            nstrprintf_cat(result, "%c", (char)val);
            i += 2;
        } else if (str[i] == '+') {
            nstrprintf_cat(result, " ");
        } else {
            nstrprintf_cat(result, "%c", str[i]);
        }
    }
    return result;
}

void n_url_free(N_URL** u) {
    if (!u || !*u) return;
    N_URL* p = *u;
    FreeNoLog(p->scheme);
    FreeNoLog(p->host);
    FreeNoLog(p->path);
    FreeNoLog(p->query);
    for (int i = 0; i < p->nb_params; i++) {
        FreeNoLog(p->params[i].key);
        FreeNoLog(p->params[i].value);
    }
    FreeNoLog(p);
    *u = NULL;
}

/**
 * @brief Helper: connect a plain TCP socket to host:port.
 * @param host hostname
 * @param port port number
 * @return socket fd on success, INVALID_SOCKET on failure.
 */
static SOCKET _proxy_tcp_connect(const char* host, int port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    int gai_rc = getaddrinfo(host, port_str, &hints, &res);
    if (gai_rc != 0 || !res) {
        n_log(LOG_ERR, "n_proxy: DNS lookup failed for %s:%d: %s",
              host, port, gai_strerror(gai_rc));
        return INVALID_SOCKET;
    }

    SOCKET fd = INVALID_SOCKET;
    struct addrinfo* rp = NULL;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        if (connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) break;
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);

    if (fd == INVALID_SOCKET) {
        n_log(LOG_ERR, "n_proxy: TCP connect to %s:%d failed", host, port);
    }
    return fd;
}

N_PROXY_CFG* n_proxy_cfg_parse(const char* url) {
    if (!url) return NULL;

    /* Expect scheme://... */
    const char* sep = strstr(url, "://");
    if (!sep) {
        n_log(LOG_ERR, "n_proxy_cfg_parse: no scheme in '%s'", url);
        return NULL;
    }

    size_t scheme_len = (size_t)(sep - url);
    if (scheme_len == 0 || scheme_len > 16) return NULL;

    char scheme[17];
    memcpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';

    /* Only http, https, and socks5 */
    if (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0 && strcmp(scheme, "socks5") != 0) {
        n_log(LOG_ERR, "n_proxy_cfg_parse: unsupported scheme '%s'", scheme);
        return NULL;
    }

    const char* after = sep + 3; /* after :// */
    if (!*after) return NULL;

    /* Check for user:pass@host:port */
    const char* at = strchr(after, '@');
    const char* host_start = NULL;
    char* username = NULL;
    char* password = NULL;

    if (at) {
        /* Parse user:pass */
        const char* colon = strchr(after, ':');
        if (colon && colon < at) {
            username = strndup(after, (size_t)(colon - after));
            password = strndup(colon + 1, (size_t)(at - colon - 1));
        } else {
            username = strndup(after, (size_t)(at - after));
        }
        host_start = at + 1;
    } else {
        host_start = after;
    }

    /* Parse host:port */
    /* Handle IPv6 [host]:port */
    char* hostname = NULL;
    int port = 0;

    if (host_start[0] == '[') {
        const char* bracket = strchr(host_start, ']');
        if (!bracket) goto fail;
        hostname = strndup(host_start + 1, (size_t)(bracket - host_start - 1));
        if (bracket[1] == ':') {
            port = atoi(bracket + 2);
        }
    } else {
        const char* colon = strchr(host_start, ':');
        if (colon) {
            hostname = strndup(host_start, (size_t)(colon - host_start));
            port = atoi(colon + 1);
        } else {
            hostname = strdup(host_start);
        }
    }

    if (!hostname || !hostname[0]) goto fail;
    if (port <= 0 || port > 65535) {
        /* Default ports */
        if (strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0)
            port = 3128;
        else if (strcmp(scheme, "socks5") == 0)
            port = 1080;
    }

    N_PROXY_CFG* cfg = NULL;
    Malloc(cfg, N_PROXY_CFG, 1);
    if (!cfg) goto fail;
    memset(cfg, 0, sizeof(*cfg));
    cfg->scheme = strdup(scheme);
    cfg->host = hostname;
    cfg->port = port;
    cfg->username = username;
    cfg->password = password;
    return cfg;

fail:
    FreeNoLog(hostname);
    FreeNoLog(username);
    FreeNoLog(password);
    return NULL;
}

void n_proxy_cfg_free(N_PROXY_CFG** cfg) {
    if (!cfg || !*cfg) return;
    N_PROXY_CFG* p = *cfg;
    FreeNoLog(p->scheme);
    FreeNoLog(p->host);
    FreeNoLog(p->username);
    FreeNoLog(p->password);
    FreeNoLog(p);
    *cfg = NULL;
}

int n_proxy_connect_tunnel(const N_PROXY_CFG* proxy,
                           const char* target_host,
                           int target_port) {
    if (!proxy || !target_host) return -1;

    SOCKET fd = _proxy_tcp_connect(proxy->host, proxy->port);
    if (fd == INVALID_SOCKET) return -1;

    /* Build CONNECT request */
    char connect_req[2048];
    int len = 0;

    len = snprintf(connect_req, sizeof(connect_req),
                   "CONNECT %s:%d HTTP/1.1\r\n"
                   "Host: %s:%d\r\n",
                   target_host, target_port,
                   target_host, target_port);

    /* Proxy-Authorization if credentials present */
    if (proxy->username && proxy->username[0]) {
        char cred[512];
        snprintf(cred, sizeof(cred), "%s:%s",
                 proxy->username, proxy->password ? proxy->password : "");
        N_STR* cred_nstr = char_to_nstr(cred);
        if (cred_nstr) {
            N_STR* b64 = n_base64_encode(cred_nstr);
            if (b64 && b64->data) {
                len += snprintf(connect_req + len,
                                sizeof(connect_req) - (size_t)len,
                                "Proxy-Authorization: Basic %s\r\n",
                                b64->data);
            }
            if (b64) free_nstr(&b64);
            free_nstr(&cred_nstr);
        }
    }

    len += snprintf(connect_req + len, sizeof(connect_req) - (size_t)len,
                    "\r\n");

    /* Send CONNECT */
    ssize_t sent = send(fd, connect_req, NETW_BUFLEN_CAST(len), 0);
    if (sent != (ssize_t)len) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel: send CONNECT failed");
        closesocket(fd);
        return -1;
    }

    /* Read response */
    char resp_buf[4096];
    ssize_t nr = recv(fd, resp_buf, NETW_BUFLEN_CAST(sizeof(resp_buf) - 1), 0);
    if (nr <= 0) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel: no response from proxy");
        closesocket(fd);
        return -1;
    }
    resp_buf[nr] = '\0';

    /* Check for "HTTP/1.x 200" */
    if (strstr(resp_buf, " 200 ") == NULL) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel: proxy rejected CONNECT: %.*s",
              (int)(strchr(resp_buf, '\r') ? strchr(resp_buf, '\r') - resp_buf : nr),
              resp_buf);
        closesocket(fd);
        return -1;
    }

    n_log(LOG_DEBUG, "n_proxy_connect_tunnel: tunnel established to %s:%d via %s:%d",
          target_host, target_port, proxy->host, proxy->port);
    return (int)fd;
}

#ifdef HAVE_OPENSSL
int n_proxy_connect_tunnel_ssl(const N_PROXY_CFG* proxy,
                               const char* target_host,
                               int target_port) {
    if (!proxy || !target_host) return -1;

    SOCKET fd = _proxy_tcp_connect(proxy->host, proxy->port);
    if (fd == INVALID_SOCKET) return -1;

    /* TLS handshake to the proxy itself */
    netw_init_openssl();

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    const SSL_METHOD* method = TLS_client_method();
#else
    const SSL_METHOD* method = TLSv1_2_client_method();
#endif
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel_ssl: SSL_CTX_new failed");
        closesocket(fd);
        return -1;
    }
    SSL_CTX_set_default_verify_paths(ctx);

    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel_ssl: SSL_new failed");
        SSL_CTX_free(ctx);
        closesocket(fd);
        return -1;
    }
    SSL_set_fd(ssl, (int)fd);
    SSL_set_tlsext_host_name(ssl, proxy->host);

    if (SSL_connect(ssl) <= 0) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel_ssl: TLS handshake to proxy %s:%d failed",
              proxy->host, proxy->port);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        closesocket(fd);
        return -1;
    }

    n_log(LOG_DEBUG, "n_proxy_connect_tunnel_ssl: TLS established to proxy %s:%d",
          proxy->host, proxy->port);

    /* Build CONNECT request */
    char connect_req[2048];
    int len = 0;

    len = snprintf(connect_req, sizeof(connect_req),
                   "CONNECT %s:%d HTTP/1.1\r\n"
                   "Host: %s:%d\r\n",
                   target_host, target_port,
                   target_host, target_port);

    /* Proxy-Authorization if credentials present */
    if (proxy->username && proxy->username[0]) {
        char cred[512];
        snprintf(cred, sizeof(cred), "%s:%s",
                 proxy->username, proxy->password ? proxy->password : "");
        N_STR* cred_nstr = char_to_nstr(cred);
        if (cred_nstr) {
            N_STR* b64 = n_base64_encode(cred_nstr);
            if (b64 && b64->data) {
                len += snprintf(connect_req + len,
                                sizeof(connect_req) - (size_t)len,
                                "Proxy-Authorization: Basic %s\r\n",
                                b64->data);
            }
            if (b64) free_nstr(&b64);
            free_nstr(&cred_nstr);
        }
    }

    len += snprintf(connect_req + len, sizeof(connect_req) - (size_t)len,
                    "\r\n");

    /* Send CONNECT over TLS */
    if (SSL_write(ssl, connect_req, len) != len) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel_ssl: send CONNECT failed");
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        closesocket(fd);
        return -1;
    }

    /* Read response over TLS */
    char resp_buf[4096];
    int nr = SSL_read(ssl, resp_buf, (int)(sizeof(resp_buf) - 1));
    if (nr <= 0) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel_ssl: no response from proxy");
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        closesocket(fd);
        return -1;
    }
    resp_buf[nr] = '\0';

    /* Check for "HTTP/1.x 200" */
    if (strstr(resp_buf, " 200 ") == NULL) {
        n_log(LOG_ERR, "n_proxy_connect_tunnel_ssl: proxy rejected CONNECT: %.*s",
              (int)(strchr(resp_buf, '\r') ? strchr(resp_buf, '\r') - resp_buf : nr),
              resp_buf);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        closesocket(fd);
        return -1;
    }

    /* Tunnel established, tear down the proxy TLS session but keep the fd.
     * SSL_shutdown sends close_notify; the proxy knows the CONNECT handshake
     * is done and the raw tunnel begins. We call SSL_set_quiet_shutdown to
     * avoid waiting for the peer's close_notify (the proxy won't send one
     * mid-tunnel). */
    SSL_set_quiet_shutdown(ssl, 1);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);

    n_log(LOG_DEBUG, "n_proxy_connect_tunnel_ssl: tunnel established to %s:%d via https://%s:%d",
          target_host, target_port, proxy->host, proxy->port);
    return (int)fd;
}
#endif /* HAVE_OPENSSL */

int n_proxy_connect_socks5(const N_PROXY_CFG* proxy,
                           const char* target_host,
                           int target_port) {
    if (!proxy || !target_host) return -1;

    SOCKET fd = _proxy_tcp_connect(proxy->host, proxy->port);
    if (fd == INVALID_SOCKET) return -1;

    int use_auth = (proxy->username && proxy->username[0]) ? 1 : 0;

    /* SOCKS5 greeting */
    char greeting[4];
    if (use_auth) {
        greeting[0] = 0x05; /* version */
        greeting[1] = 0x02; /* 2 methods */
        greeting[2] = 0x00; /* no auth */
        greeting[3] = 0x02; /* user/pass */
        if (send(fd, greeting, 4, 0) != 4) goto fail;
    } else {
        greeting[0] = 0x05;
        greeting[1] = 0x01;
        greeting[2] = 0x00;
        if (send(fd, greeting, 3, 0) != 3) goto fail;
    }

    /* Read server method selection */
    char method_resp[2];
    if (recv(fd, method_resp, 2, 0) != 2) goto fail;
    if (method_resp[0] != 0x05) goto fail;

    if (method_resp[1] == 0x02 && use_auth) {
        /* User/pass auth sub-negotiation (RFC 1929) */
        size_t ulen = strlen(proxy->username);
        size_t plen = proxy->password ? strlen(proxy->password) : 0;
        if (ulen > 255 || plen > 255) goto fail;

        char auth_req[515]; /* 1+1+255+1+255 */
        size_t pos = 0;
        auth_req[pos++] = 0x01; /* version */
        auth_req[pos++] = (char)ulen;
        memcpy(auth_req + pos, proxy->username, ulen);
        pos += ulen;
        auth_req[pos++] = (char)plen;
        if (plen > 0) {
            memcpy(auth_req + pos, proxy->password, plen);
            pos += plen;
        }
        if (send(fd, auth_req, NETW_BUFLEN_CAST(pos), 0) != (ssize_t)pos) goto fail;

        char auth_resp[2];
        if (recv(fd, auth_resp, 2, 0) != 2) goto fail;
        if (auth_resp[1] != 0x00) {
            n_log(LOG_ERR, "n_proxy_connect_socks5: auth rejected");
            goto fail;
        }
    } else if (method_resp[1] != 0x00) {
        n_log(LOG_ERR, "n_proxy_connect_socks5: no acceptable method");
        goto fail;
    }

    /* Send connect request (domain name addressing) */
    {
        size_t hlen = strlen(target_host);
        if (hlen > 255) goto fail;

        char conn_req[263]; /* 4 + 1 + 255 + 2 */
        size_t pos = 0;
        conn_req[pos++] = 0x05; /* version */
        conn_req[pos++] = 0x01; /* connect */
        conn_req[pos++] = 0x00; /* reserved */
        conn_req[pos++] = 0x03; /* domain name */
        conn_req[pos++] = (char)hlen;
        memcpy(conn_req + pos, target_host, hlen);
        pos += hlen;
        conn_req[pos++] = (char)((target_port >> 8) & 0xFF);
        conn_req[pos++] = (char)(target_port & 0xFF);

        if (send(fd, conn_req, NETW_BUFLEN_CAST(pos), 0) != (ssize_t)pos) goto fail;
    }

    /* Read connect response */
    {
        char conn_resp[10];
        /* Minimum response: 4 bytes header + address (varies) */
        ssize_t nr = recv(fd, conn_resp, NETW_BUFLEN_CAST(sizeof(conn_resp)), 0);
        if (nr < 4) goto fail;
        if (conn_resp[0] != 0x05 || conn_resp[1] != 0x00) {
            n_log(LOG_ERR, "n_proxy_connect_socks5: connect failed, reply=%02x",
                  conn_resp[1]);
            goto fail;
        }

        /* If address type is domain (0x03), we may need to read more */
        if (conn_resp[3] == 0x03 && nr < 5) {
            /* Read remaining bytes */
            char extra[256];
            recv(fd, extra, NETW_BUFLEN_CAST(sizeof(extra)), 0);
        } else if (conn_resp[3] == 0x04 && nr < 10) {
            /* IPv6: 16 + 2 bytes remaining */
            char extra[18];
            size_t need = 22 - (size_t)nr; /* total IPv6 response = 4+16+2=22 */
            if (need <= sizeof(extra)) {
                recv(fd, extra, NETW_BUFLEN_CAST(need), 0);
            }
        }
    }

    n_log(LOG_DEBUG, "n_proxy_connect_socks5: tunnel established to %s:%d via %s:%d",
          target_host, target_port, proxy->host, proxy->port);
    return (int)fd;

fail:
    n_log(LOG_ERR, "n_proxy_connect_socks5: handshake failed");
    closesocket(fd);
    return -1;
}
