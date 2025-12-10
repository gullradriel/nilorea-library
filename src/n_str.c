/**
 *@file n_str.c
 *@brief String functions, everything you need to use string is here
 *@author Castagnier Mickael
 *@version 1.0
 *@date 01/04/05
 */

#ifndef NO_NSTR

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>

#ifdef __windows__
/**
 *@brief string case insensitive search
 *@param s1 haystack string
 *@param s2 needle string
 *@return NULL or a pointer to the position of s2 in s1
 */
const char* strcasestr(const char* s1, const char* s2) {
    __n_assert(s1, return NULL);
    __n_assert(s2, return NULL);

    size_t n = strlen(s2);
    while (*s1) {
        if (!strnicmp(s1++, s2, n))
            return (s1 - 1);
    }
    return NULL;
} /* strcasestr */
#endif

/**
 *@brief Free a N_STR pointer structure
 *@param ptr A N_STR *object to free
 */
void free_nstr_ptr(void* ptr) {
    N_STR* strptr = (N_STR*)ptr;
    if (ptr && strptr) {
        FreeNoLog(strptr->data);
        FreeNoLog(strptr);
    }
} /* free_nstr_ptr( ... ) */

/**
 *@brief Free a N_STR structure and set the pointer to NULL
 *@param ptr A N_STR *object to free
 *@return TRUE or FALSE
 */
int _free_nstr(N_STR** ptr) {
    __n_assert(ptr && (*ptr), return FALSE);

    FreeNoLog((*ptr)->data);
    FreeNoLog((*ptr));

    return TRUE;
} /* free_nstr( ... ) */

/**
 *@brief Free a N_STR structure and set the pointer to NULL
 *@param ptr A N_STR *object to free
 *@return TRUE or FALSE
 */
int free_nstr_nolog(N_STR** ptr) {
    if ((*ptr)) {
        FreeNoLog((*ptr)->data);
        FreeNoLog((*ptr));
    }

    return TRUE;
} /* free_nstr( ... ) */

/**
 *@brief Free a N_STR pointer structure
 *@param ptr A N_STR *object to free
 */
void free_nstr_ptr_nolog(void* ptr) {
    N_STR* strptr = (N_STR*)ptr;
    if (strptr) {
        FreeNoLog(strptr->data);
        FreeNoLog(strptr);
    }
} /* free_nstr_ptr_nolog( ... ) */

/**
 *@brief trim and zero end the string, WARNING: keep and original pointer to delete the string correctly
 *@param s The string to trim
 *@return the trimmed string or NULL
 */
char* trim_nocopy(char* s) {
    __n_assert(s, return NULL);

    if (strlen(s) == 0)
        return s;

    char* start = s;

    /* skip spaces at start */
    while (*start && isspace(*start))
        start++;

    char* end = s + strlen(s) - 1;
    /* iterate over the rest remebering last non-whitespace */
    while (*end && isspace(*end) && end > s) {
        end--;
    }
    end++;
    /* write the terminating zero after last non-whitespace */
    *end = 0;

    return start;
} /* trim_nocopy */

/**
 *@brief trim and put a \0 at the end, return new char *
 *@param s The string to trim
 *@return the trimmed string or NULL
 */
char* trim(char* s) {
    __n_assert(s, return NULL);

    return strdup(trim_nocopy(s));
} /* trim_nocopy */

/**
 *@brief try to fgets
 *@param buffer The string where to read the file
 *@param size Size of the string
 *@param stream The file to read
 *@return NULL or the captured string
 */
char* nfgets(char* buffer, NSTRBYTE size, FILE* stream) {
    __n_assert(buffer, return NULL);
    __n_assert(stream, return NULL);

    if (size >= INT_MAX) {
        n_log(LOG_ERR, "size of %zu too big, >INT_MAX for buffer %p", size, buffer);
        return NULL;
    }

    if (!fgets(buffer, (int)size, stream)) {
        return NULL;
    }

    return buffer;
} /* nfgets(...) */

/**
 *@brief empty a N_STR string
 *@param nstr the N_STR *str string to empty
 *@return TRUE or FALSE
 */
int empty_nstr(N_STR* nstr) {
    __n_assert(nstr, return FALSE);
    __n_assert(nstr->data, return FALSE);

    nstr->written = 0;
    memset(nstr->data, 0, nstr->length);

    return TRUE;
}

/**
 *@brief create a new N_STR string
 *@param size Size of the new string. 0 for no allocation.
 *@return A new allocated N_STR or NULL
 */
N_STR* new_nstr(NSTRBYTE size) {
    N_STR* str = NULL;

    Malloc(str, N_STR, 1);
    __n_assert(str, return NULL);

    str->written = 0;
    if (size == 0) {
        str->data = NULL;
        str->length = 0;
    } else {
        Malloc(str->data, char, size + 1);
        __n_assert(str->data, Free(str); return NULL);
        str->length = size;
    }
    return str;
} /* new_nstr(...) */

/**
 *@brief Convert a char into a N_STR, extended version
 *@param from A char *string to convert
 *@param nboct  The size to copy, from 1 octet to nboctet (ustrsizez( from ) )
 *@param to A N_STR pointer who will be Malloced
 *@return True on success, FALSE on failure ( to will be set to NULL )
 */
int char_to_nstr_ex(const char* from, NSTRBYTE nboct, N_STR** to) {
    if ((*to)) {
        n_log(LOG_ERR, "destination N_STR **str is not NULL (%p), it contain (%s). You must provide an empty destination.", (*to), ((*to) && (*to)->data) ? (*to)->data : "DATA_IS_NULL");
        n_log(LOG_ERR, "Data to copy: %s", _str(from));
        return FALSE;
    };

    (*to) = new_nstr(nboct + 1);
    __n_assert(to && (*to), return FALSE);
    /* added a sizeof( void * ) to add a consistant and secure padding at the end */
    __n_assert((*to)->data, Free((*to)); return FALSE);

    memcpy((*to)->data, from, nboct);
    (*to)->written = nboct;

    return TRUE;
} /* char_to_nstr(...) */

/**
 *@brief Convert a char into a N_STR, short version
 *@param src A char *string to convert
 *@return A N_STR copy of src or NULL
 */
N_STR* char_to_nstr(const char* src) {
    __n_assert(src, return NULL);
    N_STR* strptr = NULL;
    size_t length = strlen(src);
    char_to_nstr_ex(src, length, &strptr);
    return strptr;
} /* char_to_str(...) */

/**
 *@brief Convert a char into a N_STR, direct use of linked source pointer
 *@param src A char *string to use
 *@return A N_STR using src or NULL
 */
N_STR* char_to_nstr_nocopy(char* src) {
    __n_assert(src, return NULL);

    N_STR* to = NULL;
    to = new_nstr(0);
    __n_assert(to, return NULL);

    to->data = src;
    to->written = strlen(src);
    to->length = to->written + 1;  // include src end of string

    return to;
} /* char_to_str_nocopy(...) */

/**
 *@brief Load a whole file into a N_STR. Be aware of the NSTRBYTE addressing limit (2GB commonly)
 *@param filename The filename to load inside a N_STR
 *@return A valid N_STR or NULL
 */
N_STR* file_to_nstr(char* filename) {
    N_STR* tmpstr = NULL;
    struct stat filestat;
    FILE* in = NULL;
    int error = 0;

    __n_assert(filename, n_log(LOG_ERR, "Unable to make a string from NULL filename"); return NULL);

    int fd = open(filename, O_RDONLY);
    error = errno;
    if (fd == -1) {
        n_log(LOG_ERR, "Unable to open %s for reading. Errno: %s", filename, strerror(error));
        return NULL;
    }

    if (fstat(fd, &filestat) != 0) {
        error = errno;
#ifdef __linux__
        if (error == EOVERFLOW) {
            n_log(LOG_ERR, "%s size is too big ,EOVERFLOW)", filename);
        } else {
#endif
            n_log(LOG_ERR, "Couldn't stat %s. Errno: %s", filename, strerror(error));
#ifdef __linux__
        }
#endif
        close(fd);
        return NULL;
    }

    if ((filestat.st_size + 1) >= (pow(2, 32) - 1)) {
        n_log(LOG_ERR, "file size >= 2GB is not possible yet. %s is %lld oct", filename, filestat.st_size);
        return NULL;
    }

    n_log(LOG_DEBUG, "%s file size is: %lld", filename, (long long unsigned int)filestat.st_size);

    in = fdopen(fd, "rb");
    if (!in) {
        n_log(LOG_ERR, "fdopen failed on %s (fd=%d)", filename, fd);
        close(fd);
        return NULL;
    }

    tmpstr = new_nstr((size_t)filestat.st_size + 1);
    __n_assert(tmpstr, fclose(in); n_log(LOG_ERR, "Unable to get a new nstr of %ld octets", filestat.st_size + 1); return NULL);

    tmpstr->written = (size_t)filestat.st_size;

    if (fread(tmpstr->data, sizeof(char), tmpstr->written, in) == 0) {
        n_log(LOG_ERR, "Couldn't read %s, fread return 0", filename);
        free_nstr(&tmpstr);
        fclose(in);
        return NULL;
    }
    if (ferror(in)) {
        n_log(LOG_ERR, "There were some errors when reading %s", filename);
        free_nstr(&tmpstr);
        fclose(in);
        return NULL;
    }

    fclose(in);

    return tmpstr;
} /*file_to_nstr */

/**
 *@brief Write a N_STR content into a file
 *@param str The N_STR *content to write down
 *@param out An opened FILE *handler
 *@param lock a write lock will be put if lock = 1
 *@return TRUE or FALSE
 */

/* Write a whole N_STR into a file */
int nstr_to_fd(N_STR* str, FILE* out, int lock) {
#ifdef __linux__
    struct flock out_lock;
#endif
    __n_assert(out, return FALSE);
    __n_assert(str, return FALSE);

    int ret = TRUE;

    if (lock == 1) {
#ifdef __linux__
        memset(&out_lock, 0, sizeof(out_lock));
        out_lock.l_type = F_WRLCK;
        /* Place a write lock on the file. */
        fcntl(fileno(out), F_SETLKW, &out_lock);
#else
        lock = 2; /* compiler warning suppressor */
#endif
    }

    size_t written_to_file = 0;
    if ((written_to_file = fwrite(str->data, sizeof(char), str->written, out)) != (size_t)str->written) {
        n_log(LOG_ERR, "Couldn't write file, fwrite %d of %d octets", written_to_file, str->written);
        ret = FALSE;
    }
    if (ferror(out)) {
        n_log(LOG_ERR, "There were some errors when writing to %d", fileno(out));
        ret = FALSE;
    }

    if (lock == 1) {
#ifdef __linux__
        memset(&out_lock, 0, sizeof(out_lock));
        out_lock.l_type = F_WRLCK;
        /* Place a write lock on the file. */
        fcntl(fileno(out), F_SETLKW, &out_lock);
#else
        lock = 2; /* compiler warning suppressor */
#endif
    }

    return ret;
} /* nstr_to_fd( ... ) */

/**
 *@brief Write a N_STR content into a file
 *@param str The N_STR *content to write down
 *@param filename The destination filename
 *@return TRUE or FALSE
 */
int nstr_to_file(N_STR* str, char* filename) {
    __n_assert(str, return FALSE);
    __n_assert(filename, return FALSE);

    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0600);  // Secure permissions: rw-------
    if (fd == -1) {
        n_log(LOG_ERR, "Couldn't open %s for writing (0600). Errno: %s", _str(filename), strerror(errno));
        return FALSE;
    }

    FILE* out = NULL;
    out = fdopen(fd, "wb");
    if (!out) {
        n_log(LOG_ERR, "fdopen failed for %s (fd=%d). Errno: %s", _str(filename), fd, strerror(errno));
        close(fd);
        return FALSE;
    }

    int ret = nstr_to_fd(str, out, 1);

    fclose(out);

    n_log(LOG_DEBUG, "%s file size is: %lld", _str(filename), (long long)str->written);

    return ret;
} /* nstr_to_file(...) */

/**
 * @brief Helper for string[start to end] to integer. Automatically add /0 for
 conversion. Leave values untouched if any error occur. Work on a copy of the
 chunk.
 * @param s String to convert
 * @param start Start position of the chunk
 * @param end End position of the chunk
 * @param i A pointer to an integer variable which will receive the value.
 * @param base Base for converting values
 * @return TRUE or FALSE
 */
int str_to_int_ex(const char* s, NSTRBYTE start, NSTRBYTE end, int* i, const int base) {
    char* tmpstr = NULL;
    char* endstr = NULL;
    long int l = 0;

    __n_assert(s, return FALSE);

    Malloc(tmpstr, char, sizeof(int) + end - start + 8);
    __n_assert(tmpstr, n_log(LOG_ERR, "Unable to Malloc( tmpstr , char ,  sizeof( int ) + %d - %d )", end, start); return FALSE);

    memcpy(tmpstr, s + start, end - start);

    errno = 0;
    l = strtol(tmpstr, &endstr, base);
    if ((errno == ERANGE && l == LONG_MAX) || l > INT_MAX) {
        n_log(LOG_ERR, "OVERFLOW reached when converting %s to int", tmpstr);
        Free(tmpstr);
        return FALSE;
    }
    if ((errno == ERANGE && l == LONG_MIN) || l < INT_MIN) {
        n_log(LOG_ERR, "UNDERFLOW reached when converting %s to int", tmpstr);
        Free(tmpstr);
        return FALSE;
    }
    if (*endstr != '\0' && *endstr != '\n') {
        n_log(LOG_ERR, "Impossible conversion for %s", tmpstr);
        Free(tmpstr);
        return FALSE;
    }
    Free(tmpstr);
    *i = (int)l;
    return TRUE;
} /* str_to_int_ex( ... ) */

/**
 * @brief Helper for string[start to end] to integer. Automatically add /0 for
 conversion. Leave values untouched if any error occur. Work on a copy of the
 chunk.
 * @param s String to convert
 * @param start Start position of the chunk
 * @param end End position of the chunk
 * @param i A pointer to an integer variable which will receieve the value.
 * @param base Base for converting values
 * @param infos If not NULL , contain the errors. remember to free the pointer if returned !!
 * @return TRUE or FALSE
 */
int str_to_int_nolog(const char* s, NSTRBYTE start, NSTRBYTE end, int* i, const int base, N_STR** infos) {
    char* tmpstr = NULL;
    char* endstr = NULL;
    long int l = 0;

    __n_assert(s, return FALSE);

    Malloc(tmpstr, char, sizeof(int) + end - start + 8);
    __n_assert(tmpstr, n_log(LOG_ERR, "Unable to Malloc( tmpstr , char ,  sizeof( int ) + %d - %d )", end, start); return FALSE);

    memcpy(tmpstr, s + start, end - start);

    errno = 0;
    l = strtol(tmpstr, &endstr, base);
    if ((errno == ERANGE && l == LONG_MAX) || l > INT_MAX) {
        nstrprintf((*infos), "OVERFLOW reached when converting %s to int", tmpstr);
        Free(tmpstr);
        return FALSE;
    }
    if ((errno == ERANGE && l == LONG_MIN) || l < INT_MIN) {
        nstrprintf((*infos), "UNDERFLOW reached when converting %s to int", tmpstr);
        Free(tmpstr);
        return FALSE;
    }
    if (*endstr != '\0' && *endstr != '\n') {
        nstrprintf((*infos), "Impossible conversion for %s", tmpstr);
        Free(tmpstr);
        return FALSE;
    }
    Free(tmpstr);
    *i = (int)l;
    return TRUE;
} /* str_to_int_nolog( ... ) */

/**
 * @brief Helper for string to integer
 * @param s String to convert
 * @param i A pointer to an integer variable which will receieve the value.
 * @param base Base for converting values
 * @return TRUE or FALSE
 */
int str_to_int(const char* s, int* i, const int base) {
    int ret = FALSE;
    if (s) {
        ret = str_to_int_ex(s, 0, strlen(s), i, base);
    }
    return ret;
} /* str_to_int(...) */

/**
 * @brief Helper for string[start to end] to long integer. Automatically add /0 for
 conversion. Leave values untouched if any error occur. Work on a copy of the
 chunk.
 * @param s String to convert
 * @param start Start position of the chunk
 * @param end End position of the chunk
 * @param i A pointer to an integer variable which will receieve the value.
 * @param base Base for converting values
 * @return TRUE or FALSE
 */
int str_to_long_ex(const char* s, NSTRBYTE start, NSTRBYTE end, long int* i, const int base) {
    char* tmpstr = NULL;
    char* endstr = NULL;
    long l = 0;

    __n_assert(s, return FALSE);

    Malloc(tmpstr, char, sizeof(int) + end - start + 8);
    __n_assert(tmpstr, n_log(LOG_ERR, "Unable to Malloc( tmpstr , char ,  sizeof( int ) + %d - %d )", end, start); return FALSE);

    memcpy(tmpstr, s + start, end - start);

    errno = 0;
    l = strtol(tmpstr, &endstr, base);
    int error = errno;

    /* test return to number and errno values */
    if (tmpstr == endstr) {
        n_log(LOG_ERR, " number : %lu  invalid  (no digits found, 0 returned)", l);
        Free(tmpstr);
        return FALSE;
    } else if (error == ERANGE && l == LONG_MIN) {
        n_log(LOG_ERR, " number : %lu  invalid  (underflow occurred)", l);
        Free(tmpstr);
        return FALSE;
    } else if (error == ERANGE && l == LONG_MAX) {
        n_log(LOG_ERR, " number : %lu  invalid  (overflow occurred)", l);
        Free(tmpstr);
        return FALSE;
    } else if (error == EINVAL) /* not in all c99 implementations - gcc OK */
    {
        n_log(LOG_ERR, " number : %lu  invalid  (base contains unsupported value)", l);
        Free(tmpstr);
        return FALSE;
    } else if (error != 0 && l == 0) {
        n_log(LOG_ERR, " number : %lu  invalid  (unspecified error occurred)", l);
        Free(tmpstr);
        return FALSE;
    } else if (error == 0 && tmpstr && !*endstr) {
        n_log(LOG_DEBUG, " number : %lu    valid  (and represents all characters read)", l);
    } else if (error == 0 && tmpstr && *endstr != 0) {
        n_log(LOG_DEBUG, " number : %lu    valid  (but additional characters remain", l);
    }
    Free(tmpstr);
    *i = l;
    return TRUE;
} /* str_to_long_ex( ... ) */

/**
 * @brief Helper for string[start to end] to long long integer. Automatically add /0 for conversion. Leave values untouched if any error occur. Work on a copy of the chunk.
 * @param s String to convert
 * @param start Start position of the chunk
 * @param end End position of the chunk
 * @param i A pointer to an integer variable which will receieve the value.
 * @param base Base for converting values
 * @return TRUE or FALSE
 */
int str_to_long_long_ex(const char* s, NSTRBYTE start, NSTRBYTE end, long long int* i, const int base) {
    char* tmpstr = NULL;
    char* endstr = NULL;
    long long l = 0;

    __n_assert(s, return FALSE);

    Malloc(tmpstr, char, sizeof(int) + end - start + 8);
    __n_assert(tmpstr, n_log(LOG_ERR, "Unable to Malloc( tmpstr , char ,  sizeof( int ) + %d - %d )", end, start); return FALSE);

    memcpy(tmpstr, s + start, end - start);

    errno = 0;
    l = strtoll(tmpstr, &endstr, base);
    int error = errno;

    /* test return to number and errno values */
    if (tmpstr == endstr) {
        n_log(LOG_ERR, "number: '%s' invalid (no digits found, 0 returned)", s);
        Free(tmpstr);
        return FALSE;
    } else if (error == ERANGE && l == LLONG_MIN) {
        n_log(LOG_ERR, "number: '%s' invalid (underflow occurred)", s);
        Free(tmpstr);
        return FALSE;
    } else if (error == ERANGE && l == LLONG_MAX) {
        n_log(LOG_ERR, "number: 's' invalid (overflow occurred)", s);
        Free(tmpstr);
        return FALSE;
    } else if (error == EINVAL) /* not in all c99 implementations - gcc OK */
    {
        n_log(LOG_ERR, "number: '%s' invalid (base contains unsupported value)", s);
        Free(tmpstr);
        return FALSE;
    } else if (error != 0 && l == 0) {
        n_log(LOG_ERR, "number: '%s' invalid (unspecified error occurred)", s);
        Free(tmpstr);
        return FALSE;
    } else if (error == 0 && tmpstr && !*endstr) {
        n_log(LOG_DEBUG, "number : '%llu' valid (and represents all characters read)", l);
    } else if (error == 0 && tmpstr && *endstr != 0) {
        n_log(LOG_DEBUG, " number : '%llu' valid (remaining characters: '%s')", l, s);
    }
    Free(tmpstr);
    *i = l;
    return TRUE;
} /* str_to_long_long_ex( ... ) */

/**
 * @brief Helper for string to integer
 * @param s String to convert
 * @param i A pointer to an integer variable which will receieve the value.
 * @param base Base for converting values
 * @return TRUE or FALSE
 */
int str_to_long(const char* s, long int* i, const int base) {
    int ret = FALSE;
    if (s) {
        ret = str_to_long_ex(s, 0, strlen(s), i, base);
    }
    return ret;
} /* str_to_long(...) */

/**
 * @brief Helper for string to integer
 * @param s String to convert
 * @param i A pointer to an integer variable which will receieve the value.
 * @param base Base for converting values
 * @return TRUE or FALSE
 */
int str_to_long_long(const char* s, long long int* i, const int base) {
    int ret = FALSE;
    if (s) {
        ret = str_to_long_long_ex(s, 0, strlen(s), i, base);
    }
    return ret;
} /* str_to_long(...) */

/**
 *@brief Duplicate a N_STR
 *@param str A N_STR *object to free
 *@return A valid N_STR or NULL
 */
N_STR* nstrdup(N_STR* str) {
    N_STR* new_str = NULL;

    __n_assert(str, return NULL);
    __n_assert(str->data, return NULL);

    new_str = new_nstr(str->length);
    if (new_str) {
        if (new_str->data) {
            memcpy(new_str->data, str->data, str->written);
            new_str->length = str->length;
            new_str->written = str->written;
        } else {
            Free(new_str);
            n_log(LOG_ERR, "Error duplicating N_STR %p -> data", str);
        }
    } else {
        n_log(LOG_ERR, "Error duplicating N_STR %p", str);
    }
    return new_str;
} /* nstrdup(...) */

/**
 *@brief skip while 'toskip' occurence is found from 'iterator' to the next non 'toskip' position.
 * The new iterator index is automatically stored, returning to it first value if an error append.
 *@param string a char *string to search in
 *@param toskip skipping while char character 'toskip' is found
 *@param iterator an int iteraor position on the string
 *@param inc an int to specify the step of skipping
 *@return TRUE if success FALSE or ABORT if not
 */
int skipw(char* string, char toskip, NSTRBYTE* iterator, int inc) {
    int error_flag = 0;
    // NSTRBYTE previous = 0 ;

    __n_assert(string, return FALSE);

    // previous = *iterator;
    if (toskip == ' ') {
        while (*iterator <= (NSTRBYTE)strlen(string) && isspace(string[*iterator])) {
            if (inc < 0 && *iterator == 0) {
                error_flag = 1;
                break;
            } else {
                if (inc > 0)
                    *iterator = *iterator + (NSTRBYTE)inc;
                else
                    *iterator = *iterator - (NSTRBYTE)inc;
            }
        }
    } else {
        while (*iterator <= (NSTRBYTE)strlen(string) && string[*iterator] == toskip) {
            if (inc < 0 && *iterator == 0) {
                error_flag = 1;
                break;
            } else {
                if (inc > 0)
                    *iterator = *iterator + (NSTRBYTE)inc;
                else
                    *iterator = *iterator - (NSTRBYTE)inc;
            }
        }
    }
    if (error_flag == 1 || *iterator > (NSTRBYTE)strlen(string)) {
        //*iterator = previous ;
        return FALSE;
    }

    return TRUE;
} /*skipw(...)*/

/**
 *@brief skip until 'toskip' occurence is found from 'iterator' to the next 'toskip' value.
 * The new iterator index is automatically stored, returning to it first value if an error append.
 *@param string a char *stri:wqng to search in
 *@param toskip skipping while char character 'toskip' isnt found
 *@param iterator an int iteraor position on the string
 *@param inc an int to specify the step of skipping
 *@return TRUE if success FALSE or ABORT if not
 */
int skipu(char* string, char toskip, NSTRBYTE* iterator, int inc) {
    int error_flag = 0;
    // NSTRBYTE previous = 0 ;

    __n_assert(string, return FALSE);

    // previous = *iterator;
    if (toskip == ' ') {
        while (*iterator <= (NSTRBYTE)strlen(string) && !isspace(string[*iterator])) {
            if (inc < 0 && *iterator == 0) {
                error_flag = 1;
                break;
            } else {
                if (inc > 0)
                    *iterator = *iterator + (NSTRBYTE)inc;
                else
                    *iterator = *iterator - (NSTRBYTE)inc;
            }
        }
    } else {
        while (*iterator <= (NSTRBYTE)strlen(string) && string[*iterator] != toskip) {
            if (inc < 0 && *iterator == 0) {
                error_flag = 1;
                break;
            } else {
                if (inc > 0)
                    *iterator = *iterator + (NSTRBYTE)inc;
                else
                    *iterator = *iterator - (NSTRBYTE)inc;
            }
        }
    }

    if (error_flag == 1 || *iterator > (NSTRBYTE)strlen(string)) {
        //*iterator = previous ;
        return FALSE;
    }

    return TRUE;
} /*Skipu(...)*/

/**
 *@brief Upper case a string
 *@param string the string to change to upper case
 *@param dest the string where storing result
 *@warning string must be same size as dest
 *@return TRUE or FALSE
 */
int strup(char* string, char* dest) {
    NSTRBYTE it = 0;

    __n_assert(string, return FALSE);
    __n_assert(dest, return FALSE);

    for (it = 0; it < (NSTRBYTE)strlen(string); it++)
        dest[it] = (char)toupper(string[it]);

    return TRUE;
} /*strup(...)*/

/**
 *@brief Upper case a string
 *@param string the string to change to lower case
 *@param dest the string where storing result
 *@warning string must be same size as dest
 *@return TRUE or FALSE
 */
int strlo(char* string, char* dest) {
    NSTRBYTE it = 0;

    __n_assert(string, return FALSE);
    __n_assert(dest, return FALSE);

    for (it = 0; it < (NSTRBYTE)strlen(string); it++)
        dest[it] = (char)tolower(string[it]);

    return TRUE;
} /*strlo(...)*/

/**
 *@brief Copy from start to dest until from[ iterator ] == split
 *@param from Source string
 *@param to Dest string
 *@param to_size the maximum size to write
 *@param split stopping character
 *@param it Save of iterator
 *@return TRUE or FALSE
 */
int strcpy_u(char* from, char* to, NSTRBYTE to_size, char split, NSTRBYTE* it) {
    NSTRBYTE _it = 0;

    __n_assert(from, return FALSE);
    __n_assert(to, return FALSE);
    __n_assert(it, return FALSE);

    while (_it < to_size && from[(*it)] != '\0' && from[(*it)] != split) {
        to[_it] = from[(*it)];
        (*it) = (*it) + 1;
        _it = _it + 1;
    }

    if (_it == to_size) {
        _it--;
        to[_it] = '\0';
        n_log(LOG_DEBUG, "strcpy_u: not enough space to write %d octet to dest (%d max) , %s: %d", _it, to_size, __FILE__, __LINE__);
        return FALSE;
    }

    to[_it] = '\0';

    if (from[(*it)] != split) {
        n_log(LOG_DEBUG, "strcpy_u: split value not found, written %d octet to dest (%d max) , %s: %d", _it, to_size, __FILE__, __LINE__);
        return FALSE;
    }
    return TRUE;
} /* strcpy_u(...) */

/**
 *@brief split the strings into a an array of char *pointer	, ended by a NULL one.
 *@param str The char *str to split
 *@param delim The delimiter, one or more characters
 *@param empty Empty flag. If 1, then empty delimited areas will be added as NULL entries, else they will be skipped.
 *@return An array of char *, ended by a NULL entry.
 */
char** split(const char* str, const char* delim, int empty) {
    char** tab = NULL;              /* result array */
    char* ptr = NULL;               /* tmp pointer */
    char** tmp = NULL;              /* temporary pointer for realloc */
    size_t sizeStr = 0;             /* string token size */
    size_t sizeTab = 0;             /* array size */
    const char* largestring = NULL; /* pointer to start of string */

    __n_assert(str, return NULL);
    __n_assert(delim, return NULL);

    size_t sizeDelim = strlen(delim);
    largestring = str;

    while ((ptr = strstr(largestring, delim)) != NULL) {
        sizeStr = (size_t)(ptr - largestring);
        if (empty == 1 || sizeStr != 0) {
            sizeTab++;
            tmp = (char**)realloc(tab, sizeof(char*) * sizeTab);
            __n_assert(tmp, goto error);
            tab = tmp;

            Malloc(tab[sizeTab - 1], char, sizeof(char) * (sizeStr + 1));
            __n_assert(tab[sizeTab - 1], goto error);

            memcpy(tab[sizeTab - 1], largestring, sizeStr);
            tab[sizeTab - 1][sizeStr] = '\0';
        }
        ptr = ptr + sizeDelim;
        largestring = ptr;
    }

    /* adding last part if any */
    if (strlen(largestring) != 0) {
        sizeStr = strlen(largestring);
        sizeTab++;

        tmp = (char**)realloc(tab, sizeof(char*) * sizeTab);
        __n_assert(tmp, goto error);
        tab = tmp;

        Malloc(tab[sizeTab - 1], char, sizeof(char) * (sizeStr + 1));
        __n_assert(tab[sizeTab - 1], goto error);

        memcpy(tab[sizeTab - 1], largestring, sizeStr);
        tab[sizeTab - 1][sizeStr] = '\0';
    } else {
        if (empty == 1) {
            sizeTab++;

            tmp = (char**)realloc(tab, sizeof(char*) * sizeTab);
            __n_assert(tmp, goto error);
            tab = tmp;

            Malloc(tab[sizeTab - 1], char, (int)(sizeof(char) * 1));
            __n_assert(tab[sizeTab - 1], goto error);

            tab[sizeTab - 1][0] = '\0';
        }
    }

    /* on ajoute une case a null pour finir le tableau */
    sizeTab++;
    tmp = (char**)realloc(tab, sizeof(char*) * sizeTab);
    __n_assert(tmp, goto error);
    tab = tmp;
    tab[sizeTab - 1] = NULL;

    return tab;

error:
    free_split_result(&tab);
    return NULL;
} /* split( ... ) */

/**
 *@brief Count split elements
 *@param split_result A char **result from a split call
 *@return a joined string using delimiter with The number of elements or -1 if errors
 */
int split_count(char** split_result) {
    __n_assert(split_result, return -1);
    __n_assert(split_result[0], return -1);

    int it = 0;
    while (split_result[it]) {
        it++;
    }
    return it;
} /* split_count(...) */

/**
 *@brief Free a split result allocated array
 *@param tab A pointer to a split result to free
 *@return TRUE
 */
int free_split_result(char*** tab) {
    if (!(*tab))
        return FALSE;

    int it = 0;
    while ((*tab)[it]) {
        Free((*tab)[it]);
        it++;
    }
    Free((*tab));

    return TRUE;
} /* free_split_result(...)*/

/**
 * @brief join the array into a string
 * @param splitresult the split result to join
 * @param delim The delimiter, one or more characters
 * @return the joined split result or NULL
 */
char* join(char** splitresult, char* delim) {
    size_t delim_length = 0;
    if (delim)
        delim_length = strlen(delim);
    size_t total_length = 0;
    int it = 0;
    while (splitresult[it]) {
        // n_log( LOG_DEBUG , "split entry %d: %s" , it , splitresult[ it ] );
        total_length += strlen(splitresult[it]);
        // if there is a delimitor and 'it' isn't the last entry
        if (delim && splitresult[it + 1])
            total_length += delim_length;
        it++;
    }
    char* result = NULL;
    Malloc(result, char, total_length + 1);
    size_t position = 0;
    it = 0;
    while (splitresult[it]) {
        size_t copy_size = strlen(splitresult[it]);
        memcpy(&result[position], splitresult[it], copy_size);
        position += copy_size;
        // if there is a delimitor and 'it' isn't the last entry
        if (delim && splitresult[it + 1]) {
            memcpy(&result[position], delim, delim_length);
            position += delim_length;
        }
        it++;
    }
    return result;
} /* join */

/**
 *@brief Append data into N_STR using internal N_STR size and cursor position.
 *@param dest The N_STR *destination (accumulator)
 *@param src The data to append
 *@param size The number of octet of data we want to append in dest
 *@param resize_flag Set it to a positive non zero value to allow resizing, or to zero to forbid resizing
 *@return TRUE or FALSE
 */
N_STR* nstrcat_ex(N_STR** dest, void* src, NSTRBYTE size, int resize_flag) {
    char* ptr = NULL;
    __n_assert(src, return NULL);

    if (resize_flag == 0) {
        if ((*dest)) {
            if (((*dest)->written + size + 1) > (*dest)->length) {
                n_log(LOG_ERR, "%p to %p: not enough space. Resize forbidden. %lld needed, %lld available", src, (*dest), (*dest)->written + size + 1, (*dest)->length);
                return NULL;
            }
        } else {
            n_log(LOG_ERR, "%p to %p: not enough space. Resize forbidden. %lld needed, destination is NULL", src, (*dest), size + 1);
            return NULL;
        }
    }

    if (!(*dest)) {
        (*dest) = new_nstr(size + 1);
    }

    if ((*dest)->length < (*dest)->written + size + 1) {
        (*dest)->length = (*dest)->written + size + 1;
        Reallocz((*dest)->data, char, (*dest)->written, (*dest)->length);
        __n_assert((*dest)->data, Free((*dest)); return FALSE);
    }

    ptr = (*dest)->data + (*dest)->written;
    memcpy(ptr, src, size);
    (*dest)->written += size;

    (*dest)->data[(*dest)->written] = '\0';

    return (*dest);
} /* nstrcat_ex( ... ) */

/**
 *@brief Append data into N_STR using internal N_STR size and cursor position. Wrapper to nstrcat_ex, with resize_flag on and a block size one byte higher than size.
 *@param dest The N_STR **destination (accumulator)
 *@param data The data to append
 *@param size The number of octet of data we want to append in dest
 *@return TRUE or FALSE
 */
N_STR* nstrcat_bytes_ex(N_STR** dest, void* data, NSTRBYTE size) {
    __n_assert(dest, return NULL);

    if (size <= 0) {
        n_log(LOG_ERR, "Could not copy 0 or less (%ld) octet!", size);
        return NULL;
    }

    return nstrcat_ex(dest, data, size, 1);
} /* nstrcat_bytes_ex( ... )*/

/**
 *@brief concatenate a copy of src of size src_size to dest, starting at dest[ written ], updating written and size variable, allocation of new blocks of (needed size + additional_padding ) if resize is needed. If dest is NULL it will be allocated.
 *@param dest The dest string
 *@param size The current size, will be updated if written + strlen( dest) > size
 *@param written the number of octet added
 *@param src The source string to add
 *@param src_size The source string size
 *@param additional_padding In case the destination is reallocated, number of additional bytes that will be added (provisionning)
 *@return TRUE on success or FALSE on a realloc error
 */
int write_and_fit_ex(char** dest, NSTRBYTE* size, NSTRBYTE* written, const char* src, NSTRBYTE src_size, NSTRBYTE additional_padding) {
    char* ptr = NULL;
    NSTRBYTE needed_size = (*written) + src_size + 1;

    // realloc if needed , also if destination is not allocated
    if ((needed_size >= (*size)) || !(*dest)) {
        if (!(*dest)) {
            (*written) = 0;
            (*size) = 0;
        }
        Reallocz((*dest), char, (*size), needed_size + additional_padding);
        (*size) = needed_size;
        if (!(*dest)) {
            n_log(LOG_ERR, "reallocation error !!!!");
            return FALSE;
        }
    }
    ptr = (*dest) + (*written);
    memcpy(ptr, src, src_size);
    (*written) += src_size;
    (*dest)[(*written)] = '\0';

    return TRUE;
} /* write_and_fit_ex( ...) */

/**
 *@brief concatenate a copy of src of size strlen( src ) to dest, starting at dest[ written ], updating written and size variable, allocation of new blocks of (needed size + 512) if resize is needed. If dest is NULL it will be allocated.
 *@param dest The dest string
 *@param size The current size, will be updated if written + strlen( dest) > size
 *@param written the number of octet added
 *@param src The source string to add
 *@return TRUE on success or FALSE on a realloc error
 */
int write_and_fit(char** dest, NSTRBYTE* size, NSTRBYTE* written, const char* src) {
    return write_and_fit_ex(dest, size, written, src, strlen(src), 8);
} /* write_and_fit( ...) */

/**
 *@brief Scan a list of directory and return a list of char *file
 *@param dir The directory to scan
 *@param result A pointer to a valid LIST for the results
 *@param recurse Recursive search if TRUE, directory only if FALSE
 *@return TRUE or FALSE
 */
int scan_dir(const char* dir, LIST* result, const int recurse) {
    return scan_dir_ex(dir, "*", result, recurse, 0);
}

/**
 *@brief Scan a list of directory and return a list of char *file
 *@param dir The directory to scan
 *@param pattern Pattern that files must follow to figure in the list
 *@param result A pointer to a valid LIST for the results
 *@param recurse Recursive search if TRUE, directory only if FALSE
 *@param mode 0 for a list of char* , 1 for a list of N_STR *
 *@return TRUE or FALSE
 */
int scan_dir_ex(const char* dir, const char* pattern, LIST* result, const int recurse, const int mode) {
    DIR* dp = NULL;
    struct dirent* entry = NULL;
    struct stat statbuf;

    if (!result)
        return FALSE;

    if ((dp = opendir(dir)) == NULL) {
        n_log(LOG_ERR, "cannot open directory: %s", dir);
        return FALSE;
    }

    N_STR* newname = NULL;
    while ((entry = readdir(dp)) != NULL) {
        nstrprintf(newname, "%s/%s", dir, entry->d_name);

        if (stat(newname->data, &statbuf) >= 0) {
            if (S_ISDIR(statbuf.st_mode) != 0) {
                if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) {
                    free_nstr(&newname);
                    continue;
                }

                /* Recurse */
                if (recurse != FALSE) {
                    if (scan_dir_ex(newname->data, pattern, result, recurse, mode) != TRUE) {
                        n_log(LOG_ERR, "scan_dir_ex( %s , %s , %p , %d , %d ) returned FALSE !", newname->data, pattern, result, recurse, mode);
                    }
                }
                free_nstr(&newname);
                continue;
            } else if (S_ISREG(statbuf.st_mode) != 0) {
                if (wildmatcase(newname->data, pattern) == TRUE) {
                    if (mode == 0) {
                        char* file = strdup(newname->data);
                        if (file) {
                            list_push(result, file, &free);
                        } else {
                            n_log(LOG_ERR, "Error adding %s/%s to list", dir, entry->d_name);
                        }
                    } else if (mode == 1) {
                        list_push(result, newname, &free_nstr_ptr);
                        newname = NULL;
                    }
                }
            }
        }
        if (newname)
            free_nstr(&newname);
    }
    closedir(dp);
    return TRUE;
} /*scan_dir(...) */

/**
 *@brief Written by Rich Salz rsalz at osf.org, refurbished by me. Wildcard pattern matching .
 *@param text The source text to search
 *@param p The text to search, with wildcards
 *@return return TRUE, FALSE, or WILDMAT_ABORT.
 */
int wildmat(register const char* text, register const char* p) {
    register int last = 0;
    register int matched = 0;
    register int reverse = 0;

    for (; *p; text++, p++) {
        if (*text == '\0' && *p != '*')
            return WILDMAT_ABORT;
        switch (*p) {
            case '\\':
                /* Literal match with following character. */
                p++;
            /* FALLTHROUGH */
            default:
                if (*text != *p)
                    return FALSE;
                continue;
            case '?':
                /* Match anything. */
                continue;
            case '*':
                while (*++p == '*')
                    /* Consecutive stars act just like one. */
                    continue;
                if (*p == '\0')
                    /* Trailing star matches everything. */
                    return TRUE;
                while (*text)
                    if ((matched = wildmat(text++, p)) != FALSE)
                        return matched;
                return WILDMAT_ABORT;
            case '[':
                reverse = p[1] == WILDMAT_NEGATE_CLASS ? TRUE : FALSE;
                if (reverse)
                    /* Inverted character class. */
                    p++;
                matched = FALSE;
                if (p[1] == ']' || p[1] == '-')
                    if (*++p == *text)
                        matched = TRUE;
                for (last = *p; *++p && *p != ']'; last = *p)
                    /* This next line requires a good C compiler. */
                    if (*p == '-' && p[1] != ']'
                            ? *text <= *++p && *text >= last
                            : *text == *p)
                        matched = TRUE;
                if (matched == reverse)
                    return FALSE;
                continue;
        }
    }
#ifdef WILDMAT_MATCH_TAR_PATTERN
    if (*text == '/')
        return TRUE;
#endif /* MATCH_TAR_ATTERN */
    return *text == '\0';
} /* wildmatch(...) */

/**
 *@brief Written by Rich Salz rsalz at osf.org, refurbished by me. Wildcard pattern matching case insensitive.
 *@param text The source text to search
 *@param p The text to search, with wildcards
 *@return return TRUE, FALSE, or WILDMAT_ABORT.
 */
int wildmatcase(register const char* text, register const char* p) {
    register int last;
    register int matched;
    register int reverse;

    for (; *p; text++, p++) {
        if (*text == '\0' && *p != '*')
            return WILDMAT_ABORT;
        switch (*p) {
            case '\\':
                /* Literal match with following character. */
                p++;
            /* FALLTHROUGH */
            default:
                if (toupper(*text) != toupper(*p))
                    return FALSE;
                continue;
            case '?':
                /* Match anything. */
                continue;
            case '*':
                while (*++p == '*')
                    /* Consecutive stars act just like one. */
                    continue;
                if (*p == '\0')
                    /* Trailing star matches everything. */
                    return TRUE;
                while (*text)
                    if ((matched = wildmatcase(text++, p)) != FALSE)
                        return matched;
                return WILDMAT_ABORT;
            case '[':
                reverse = p[1] == WILDMAT_NEGATE_CLASS ? TRUE : FALSE;
                if (reverse)
                    /* Inverted character class. */
                    p++;
                matched = FALSE;
                if (p[1] == ']' || p[1] == '-')
                    if (toupper(*++p) == toupper(*text))
                        matched = TRUE;
                for (last = toupper(*p); *++p && *p != ']'; last = toupper(*p))
                    if (*p == '-' && p[1] != ']'
                            ? toupper(*text) <= toupper(*++p) && toupper(*text) >= last
                            : toupper(*text) == toupper(*p))
                        matched = TRUE;
                if (matched == reverse)
                    return FALSE;
                continue;
        }
    }
#ifdef WILDMAT_MATCH_TAR_PATTERN
    if (*text == '/')
        return TRUE;
#endif /* MATCH_TAR_ATTERN */
    return *text == '\0';
} /* wildmatcase(...) */

/**
 *@brief Replace "substr" by "replacement" inside string
 taken from http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring
 By Chantra
 *@param string Original string to modify
 *@param substr String to search
 *@param replacement Substitution string
 *@return A copy of the sustituted string or NULL
 */
char* str_replace(const char* string, const char* substr, const char* replacement) {
    char* tok = NULL;
    char* newstr = NULL;
    char* oldstr = NULL;
    char* head = NULL;

    /* if either substr or replacement is NULL, duplicate string a let caller handle it */
    if (substr == NULL || replacement == NULL)
        return strdup(string);
    newstr = strdup(string);
    head = newstr;
    while ((tok = strstr(head, substr))) {
        oldstr = newstr;
        Malloc(newstr, char, strlen(oldstr) - strlen(substr) + strlen(replacement) + 8);
        /*failed to alloc mem, free old string and return NULL */
        if (newstr == NULL) {
            free(oldstr);
            return NULL;
        }
        memcpy(newstr, oldstr, (size_t)(tok - oldstr));
        memcpy(newstr + (tok - oldstr), replacement, strlen(replacement));
        memcpy(newstr + (tok - oldstr) + strlen(replacement), tok + strlen(substr), strlen(oldstr) - strlen(substr) - (size_t)(tok - oldstr));
        memset(newstr + strlen(oldstr) - strlen(substr) + strlen(replacement), 0, 1);
        /* move back head right after the last replacement */
        head = newstr + (tok - oldstr) + strlen(replacement);
        free(oldstr);
    }
    return newstr;
}

/**
 * @brief clean a string by replacing evil characteres
 * @param string The string to change
 * @param string_len Size of the data to treat
 * @param mask The caracters to kill
 * @param masklen size of mask
 * @param replacement replacement for mask
 * @return TRUE or FALSE
 */
int str_sanitize_ex(char* string, const NSTRBYTE string_len, const char* mask, const NSTRBYTE masklen, const char replacement) {
    __n_assert(string, return FALSE);
    __n_assert(mask, return FALSE);

    NSTRBYTE it = 0;
    for (it = 0; it < string_len; it++) {
        NSTRBYTE mask_it = 0;
        while (mask_it < masklen && mask[mask_it] != '\0') {
            if (string[it] == mask[mask_it])
                string[it] = replacement;
            mask_it++;
        }
    }
    return TRUE;
}

/**
 * @brief clean a string by replacing evil characteres
 * @param string The string to change
 * @param mask The caracters to kill
 * @param replacement replacement for mask
 * @return TRUE or FALSE
 */
int str_sanitize(char* string, const char* mask, const char replacement) {
    return str_sanitize_ex(string, strlen(string), mask, strlen(mask), replacement);
}

/**
 * @brief reallocate a nstr internal buffer. Warning: N_STR content will be reset if size = 0 !
 * @param nstr targeted N_STR *string
 * @param size new internal buffer size
 * @return TRUE or FALSE
 */
int resize_nstr(N_STR* nstr, size_t size) {
    __n_assert(nstr, return FALSE);
    if (size == 0) {
        // Free the current buffer and reset fields
        Free(nstr->data);
        nstr->written = 0;
        nstr->length = 0;
        return TRUE;
    }

    if (!nstr->data) {
        Malloc(nstr->data, char, size);
    } else {
        Reallocz(nstr->data, char, nstr->length, size);
    }
    __n_assert(nstr->data, return FALSE);

    nstr->length = size;

    return TRUE;
}

/**
 * @brief Function to allocate and format a string into an N_STR object.
 * @param nstr_var Pointer to a pointer to the N_STR object to allocate/resize.
 * @param format The format string (like printf).
 * @param ... Arguments to be formatted.
 * @return The updated N_STR object.
 */
N_STR* nstrprintf_ex(N_STR** nstr_var, const char* format, ...) {
    __n_assert(format, return NULL);

    va_list args;
    va_list args_copy;
    int needed_size = 0;

    // Calculate the required size for the formatted string
    va_start(args, format);
    va_copy(args_copy, args);  // Copy args for reuse
    needed_size = vsnprintf(NULL, 0, format, args);
    va_end(args);

    if (needed_size < 0) {
        n_log(LOG_ERR, "there was an error while computing the new size according to format \"%s\" for N_STR %p", format, (*nstr_var));
        return NULL;
    }

    size_t needed = (size_t)(needed_size + 1);  // Include null-terminator

    // Allocate or resize the N_STR object as needed
    if (!(*nstr_var)) {
        (*nstr_var) = new_nstr(needed);
    } else if (needed > (*nstr_var)->length) {
        if (resize_nstr((*nstr_var), needed) == FALSE) {
            n_log(LOG_ERR, "could not resize N_STR %p to size %zu", (*nstr_var), needed);
            return NULL;
        }
    }

    // Write the formatted string into the buffer
    vsnprintf((*nstr_var)->data, needed, format, args_copy);
    va_end(args_copy);

    (*nstr_var)->written = (size_t)needed_size;
    return (*nstr_var);
}

/**
 * @brief Function to allocate, format, and concatenate a string into an N_STR object.
 * @param nstr_var Pointer to a pointer to the N_STR object to allocate/resize and concatenate.
 * @param format The format string (like printf).
 * @param ... Variadic arguments for formatting.
 * @return The updated N_STR object.
 */
N_STR* nstrprintf_cat_ex(N_STR** nstr_var, const char* format, ...) {
    __n_assert(format, return NULL);

    va_list args;
    va_start(args, format);

    // duplicate va_list
    va_list args_copy;
    va_copy(args_copy, args);

    // compute needed size
    int needed_size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (needed_size < 0) {
        n_log(LOG_ERR, "vsnprintf size estimation failed for format \"%s\"", format);
        va_end(args);
        return NULL;
    }

    size_t needed = (size_t)(needed_size + 1);  // +1 for '\0'

    // initialize if needed
    if (!(*nstr_var)) {
        (*nstr_var) = new_nstr(needed);
        if (!(*nstr_var)) {
            va_end(args);
            return NULL;
        }
    }

    // resize if needed
    size_t total_needed = (*nstr_var)->written + needed;
    if (total_needed > (*nstr_var)->length) {
        resize_nstr((*nstr_var), total_needed);
    }

    // append formatted string
    char* write_ptr = (*nstr_var)->data + (*nstr_var)->written;
    int written_now = vsnprintf(write_ptr, needed, format, args);
    va_end(args);

    if (written_now < 0) {
        n_log(LOG_ERR, "vsnprintf failed while writing at offset %zu in N_STR %p", (*nstr_var)->written, (*nstr_var));
        return NULL;
    }

    (*nstr_var)->written += (size_t)written_now;
    (*nstr_var)->data[(*nstr_var)->written] = '\0';  // ensure final \0

    return *nstr_var;
}

#endif /* #ifndef NOSTR */
