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

/**@file n_str.h
 *  N_STR and string function declaration
 *@author Castagnier Mickael
 *@version 2.0
 *@date 05/02/14
 */

#ifndef N_STRFUNC
#define N_STRFUNC

/*! list of evil characters */
#define BAD_METACHARS "/-+&;`'\\\"|*?~<>^()[]{}$\n\r\t "

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup N_STR STRINGS: replacement to char *strings with dynamic resizing and boundary checking
  @addtogroup N_STR
  @{
  */

#include "n_common.h"
#include "n_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>

/*! N_STR base unit */
typedef size_t NSTRBYTE;

/*! A box including a string and his lenght */
typedef struct N_STR {
    /*! the string */
    char* data;
    /*! length of string (in case we wanna keep information after the 0 end of string value) */
    size_t length;
    /*! size of the written data inside the string */
    size_t written;
} N_STR;

/*! Abort code to sped up pattern matching. Special thanks to Lars Mathiesen <thorinn@diku.dk> for the ABORT code.*/
#define WILDMAT_ABORT -2
/*! What character marks an inverted character class? */
#define WILDMAT_NEGATE_CLASS '^'
/*! Do tar(1) matching rules, which ignore a trailing slash? */
#undef WILDMAT_MATCH_TAR_PATTERN

/*! local strdup */
#define local_strdup(__src_)                                                                                         \
    ({                                                                                                               \
        char* __local_strdup_str = NULL;                                                                             \
        size_t __local_strdup_len = strlen((__src_));                                                                \
        Malloc(__local_strdup_str, char, __local_strdup_len + 1);                                                    \
        if (!__local_strdup_str) {                                                                                   \
            n_log(LOG_ERR, "Couldn't allocate %d byte for duplicating \"%s\"", (int)strlen((__src_)) + 1, (__src_)); \
        } else {                                                                                                     \
            for (size_t __local_strdup_it = 0; __local_strdup_it <= __local_strdup_len; __local_strdup_it++) {       \
                __local_strdup_str[__local_strdup_it] = (__src_)[__local_strdup_it];                                 \
            }                                                                                                        \
        }                                                                                                            \
        __local_strdup_str;                                                                                          \
    })

/*! Macro to quickly allocate and sprintf to a char */
#define strprintf(__n_var, ...)                                           \
    ({                                                                    \
        char* __strprintf_ptr = NULL;                                     \
        if (!(__n_var)) {                                                 \
            int __needed = snprintf(NULL, 0, __VA_ARGS__);                \
            if (__needed > 0) {                                           \
                Malloc(__n_var, char, (size_t)__needed + 1);              \
                if (__n_var) {                                            \
                    snprintf(__n_var, (size_t)__needed + 1, __VA_ARGS__); \
                    __strprintf_ptr = __n_var;                            \
                } else {                                                  \
                    n_log(LOG_ERR, "couldn't allocate %s with %d bytes",  \
                          #__n_var, __needed + 1);                        \
                }                                                         \
            }                                                             \
        } else {                                                          \
            n_log(LOG_ERR, "%s is already allocated.", #__n_var);         \
        }                                                                 \
        __strprintf_ptr;                                                  \
    })

/*! Macro to quickly allocate and sprintf to N_STR */
#define nstrprintf(__nstr_var, __format, ...) \
    nstrprintf_ex(&(__nstr_var), (__format), ##__VA_ARGS__)

/*! Macro to quickly allocate and sprintf and cat to a N_STR */
#define nstrprintf_cat(__nstr_var, __format, ...) \
    nstrprintf_cat_ex(&(__nstr_var), (__format), ##__VA_ARGS__)

/*! Macro to quickly concatenate two N_STR */
#define nstrcat(__nstr_dst, __nstr_src)                                                          \
    ({                                                                                           \
        N_STR* __nstrcat_ret = NULL;                                                             \
        if (__nstr_src && __nstr_src->data) {                                                    \
            __nstrcat_ret = nstrcat_ex(&(__nstr_dst), __nstr_src->data, __nstr_src->written, 1); \
        }                                                                                        \
        __nstrcat_ret;                                                                           \
    })

/*! Macro to quickly concatenate a '\0' ended byte stream to a N_STR */
#define nstrcat_bytes(__nstr_dst, __void_src) \
    nstrcat_bytes_ex(&(__nstr_dst), __void_src, strlen(__void_src))

/*! Remove carriage return (backslash r) if there is one in the last position of the string */
#define n_remove_ending_cr(__nstr_var)                                                                                        \
    do {                                                                                                                      \
        if (__nstr_var && __nstr_var->data && __nstr_var->written > 0 && __nstr_var->data[__nstr_var->written - 1] == '\r') { \
            __nstr_var->data[__nstr_var->written - 1] = '\0';                                                                 \
            __nstr_var->written--;                                                                                            \
        }                                                                                                                     \
    } while (0)

/*! Find and replace all occurences of carriage return (backslash r) in the string */
#define n_replace_cr(__nstr_var, __replacement)                                    \
    do {                                                                           \
        if (__nstr_var && __nstr_var->data && __nstr_var->written > 0) {           \
            char* __replaced = str_replace(__nstr_var->data, "\r", __replacement); \
            if (__replaced) {                                                      \
                Free(__nstr_var->data);                                            \
                __nstr_var->data = __replaced;                                     \
                __nstr_var->written = strlen(__nstr_var->data);                    \
                __nstr_var->length = __nstr_var->written + 1;                      \
            }                                                                      \
        }                                                                          \
    } while (0)

#ifdef __windows__
const char* strcasestr(const char* s1, const char* s2);
#endif

/*! @brief trim and put a \0 at the end, return new begin pointer */
char* trim_nocopy(char* s);
/*! @brief trim and put a \0 at the end, return new char * */
char* trim(char* s);
/*! @brief N_STR wrapper around fgets */
char* nfgets(char* buffer, NSTRBYTE size, FILE* stream);
/*! @brief create a new string */
N_STR* new_nstr(NSTRBYTE size);
/*! @brief reinitialize a nstr */
int empty_nstr(N_STR* nstr);
/*! @brief make a copy of a N_STR */
N_STR* nstrdup(N_STR* msg);
/*! @brief convert a char into a N_STR */
int char_to_nstr_ex(const char* from, NSTRBYTE nboct, N_STR** to);
/*! @brief convert a char into a N_STR, shorter version */
N_STR* char_to_nstr(const char* src);
/*! @brief convert a char into a N_STR without copying */
int char_to_nstr_nocopy_ex(char* from, NSTRBYTE nboct, N_STR** to);
/*! @brief convert a char into a N_STR without copying, shorter version */
N_STR* char_to_nstr_nocopy(char* src);
/*! @brief concatenate data inside a N_STR */
N_STR* nstrcat_ex(N_STR** dest, void* src, NSTRBYTE size, int resize_flag);
/*! @brief wrapper to nstrcat_ex to concatenate void *data */
N_STR* nstrcat_bytes_ex(N_STR** dest, void* src, NSTRBYTE size);
/*! @brief resize a N_STR */
int resize_nstr(N_STR* nstr, size_t size);
/*! @brief printf on a N_STR */
N_STR* nstrprintf_ex(N_STR** nstr_var, const char* format, ...);
/*! @brief concatenate printf on a N_STR */
N_STR* nstrprintf_cat_ex(N_STR** nstr_var, const char* format, ...);
/*! @brief load a whole file into a N_STR */
N_STR* file_to_nstr(char* filename);
/*! @brief write a whole N_STR into an open file descriptor */
int nstr_to_fd(N_STR* str, FILE* out, int lock);
/*! @brief write a whole N_STR into a file */
int nstr_to_file(N_STR* n_str, char* filename);

/*! free a N_STR structure and set the pointer to NULL */
#define free_nstr(__ptr)                                    \
    {                                                       \
        if ((*__ptr)) {                                     \
            _free_nstr(__ptr);                              \
        } else {                                            \
            n_log(LOG_DEBUG, "%s is already NULL", #__ptr); \
        }                                                   \
    }

/*! @brief free a N_STR and set the pointer to NULL */
int _free_nstr(N_STR** ptr);
/*! @brief free a N_STR without setting the pointer to NULL */
void free_nstr_ptr(void* ptr);
/*! @brief free a N_STR and set to NULL without logging */
int free_nstr_nolog(N_STR** ptr);
/*! @brief free a N_STR without logging or setting to NULL */
void free_nstr_ptr_nolog(void* ptr);
/*! @brief string to long integer, with error checking */
int str_to_long_ex(const char* s, NSTRBYTE start, NSTRBYTE end, long int* i, const int base);
/*! @brief string to long integer, shorter version */
int str_to_long(const char* s, long int* i, const int base);
/*! @brief string to long long integer, with error checking */
int str_to_long_long_ex(const char* s, NSTRBYTE start, NSTRBYTE end, long long int* i, const int base);
/*! @brief string to long long integer, shorter version */
int str_to_long_long(const char* s, long long int* i, const int base);
/*! @brief string to integer, with error checking */
int str_to_int_ex(const char* s, NSTRBYTE start, NSTRBYTE end, int* i, const int base);
/*! @brief string to integer, with error checking and no logging */
int str_to_int_nolog(const char* s, NSTRBYTE start, NSTRBYTE end, int* i, const int base, N_STR** infos);
/*! @brief string to integer, shorter version */
int str_to_int(const char* s, int* i, const int base);
/*! @brief skip characters in string while string[iterator] == toskip */
int skipw(char* string, char toskip, NSTRBYTE* iterator, int inc);
/*! @brief skip characters in string until string[iterator] == toskip */
int skipu(char* string, char toskip, NSTRBYTE* iterator, int inc);
/*! @brief upper case a string */
int strup(char* string, char* dest);
/*! @brief lower case a string */
int strlo(char* string, char* dest);
/*! @brief copy from string to dest until from[iterator] == split */
int strcpy_u(char* from, char* to, NSTRBYTE to_size, char split, NSTRBYTE* it);
/*! @brief return an array of char pointers to the split sections */
char** split(const char* str, const char* delim, int empty);
/*! @brief count split elements */
int split_count(char** split_result);
/*! @brief free a char **tab and set it to NULL */
int free_split_result(char*** tab);
/*! @brief join a split result into a string */
char* join(char** splitresult, char* delim);
/*! @brief write and fit bytes into a dynamically sized buffer */
int write_and_fit_ex(char** dest, NSTRBYTE* size, NSTRBYTE* written, const char* src, NSTRBYTE src_size, NSTRBYTE additional_padding);
/*! @brief write and fit into the char array */
int write_and_fit(char** dest, NSTRBYTE* size, NSTRBYTE* written, const char* src);
/*! @brief get a list of files in a directory */
int scan_dir(const char* dir, LIST* result, const int recurse);
/*! @brief get a list of files in a directory, extended N_STR version */
int scan_dir_ex(const char* dir, const char* pattern, LIST* result, const int recurse, const int mode);
/*! @brief pattern matching */
int wildmat(register const char* text, register const char* p);
/*! @brief pattern matching, case insensitive */
int wildmatcase(register const char* text, register const char* p);
/*! @brief return a new string with all occurrences of substr replaced */
char* str_replace(const char* string, const char* substr, const char* replacement);
/*! @brief sanitize a string using a character mask */
int str_sanitize_ex(char* string, const NSTRBYTE string_len, const char* mask, const NSTRBYTE masklen, const char replacement);
/*! @brief in-place substitution of a set of chars by a single one */
int str_sanitize(char* string, const char* mask, const char replacement);

/**
  @}
  */

#ifdef __cplusplus
}
#endif
/* #ifndef N_STR*/
#endif
