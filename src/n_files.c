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
 *@file n_files.c
 *@brief Cross-platform directory scanning (Linux/Solaris/Windows), optionnal recursion
 *@author Castagnier Mickael
 *@version 1.0
 *@date 02/11/23
 */

#include "nilorea/n_files.h"

/**
 * Cross-platform directory scanning (Linux / Solaris / Windows via MinGW) WITHOUT chdir().
 *
 * This implementation uses ONLY POSIX-style APIs:
 *   - opendir / readdir / closedir
 *   - stat
 *
 * MinGW provides these APIs on Windows, so no WinAPI (FindFirstFile...) is required.
 *
 * Sorting:
 *   - Files are inserted sorted by modification time (mtime), oldest first.
 *   - Comparator uses seconds then nanoseconds (if available).
 *
 * Notes:
 *   - This version stores FULL PATH in N_FILE_INFO->name so recursion is unambiguous.
 *     If you prefer basename only, replace strdup(fullpath) with strdup(entry->d_name).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/**
 *  @brief helper to free N_FILE_INFO structs
 *
 *  @param ptr a pointer to a N_FILE_INFO structure
 */
void n_free_file_info(void* ptr) {
    __n_assert(ptr, return);
    N_FILE_INFO* file = (N_FILE_INFO*)ptr;
    if (file->name)
        free(file->name);
    free(file);
    return;
}

/**
 * @brief Build "dir + '/' + name" (or without extra slash if dir already ends with '/' or '\\').
 *
 * Uses '/' as a separator for portability. Windows (MinGW) accepts forward slashes.
 *
 * @param dir  Base directory path (non-NULL)
 * @param name Entry name (non-NULL)
 * @return Newly allocated full path (free() by caller), or NULL on error.
 */
char* n_path_join(const char* dir, const char* name) {
    if (!dir || !name) return NULL;

    size_t dl = strlen(dir);
    size_t nl = strlen(name);

    int need_sep = 1;
    if (dl == 0) {
        need_sep = 0;
    } else {
        char last = dir[dl - 1];
        if (last == '/' || last == '\\') need_sep = 0;
    }

    /* We use '/' for output paths. */
    size_t out_len = dl + (need_sep ? 1 : 0) + nl + 1;
    char* out = (char*)malloc(out_len);
    if (!out) return NULL;

    if (need_sep)
        snprintf(out, out_len, "%s/%s", dir, name);
    else
        snprintf(out, out_len, "%s%s", dir, name);

    return out;
}

/**
 * @brief Comparison function for sorting N_FILE_INFO by time (oldest first).
 *
 * Orders by:
 *   1) filetime      (seconds) ascending
 *   2) filetime_nsec (nanoseconds) ascending
 *
 * Return values:
 *   - < 0 if a is older than b
 *   - = 0 if equal timestamps
 *   - > 0 if a is newer than b
 *
 * @param a Pointer to first N_FILE_INFO
 * @param b Pointer to second N_FILE_INFO
 * @return Ordering result.
 */
int n_comp_file_info(const void* a, const void* b) {
    const N_FILE_INFO* f1 = (const N_FILE_INFO*)a;
    const N_FILE_INFO* f2 = (const N_FILE_INFO*)b;

    if (!f1) return -1;
    if (!f2) return 1;

    if (f1->filetime < f2->filetime) return -1;
    if (f1->filetime > f2->filetime) return 1;

    if (f1->filetime_nsec < f2->filetime_nsec) return -1;
    if (f1->filetime_nsec > f2->filetime_nsec) return 1;

    return 0;
}

/**
 * @brief Fill filetime and filetime_nsec from a struct stat (mtime).
 *
 * On Linux and Solaris, uses st_mtim (seconds + nanoseconds).
 * On other POSIX environments, falls back to st_mtime (seconds only).
 *
 * @param file N_FILE_INFO to fill (non-NULL)
 * @param st   struct stat source (non-NULL)
 */
void n_set_time_from_stat(N_FILE_INFO* file, const struct stat* st) {
    if (!file || !st) return;

#if defined(__linux__) || defined(__sun)
    file->filetime = (int64_t)st->st_mtim.tv_sec;
    file->filetime_nsec = (int32_t)st->st_mtim.tv_nsec;
#else
    file->filetime = (int64_t)st->st_mtime;
    file->filetime_nsec = 0;
#endif
}

/**
 * @brief Scan a directory and append only regular files into a sorted list (oldest first).
 *
 * This function:
 *   - Opens @p dir with opendir()
 *   - Iterates entries via readdir()
 *   - Builds full path for each entry (no chdir())
 *   - Uses stat() to detect file type and modification time
 *   - If recursion enabled and entry is a directory, scans it recursively
 *   - If entry is a regular file, pushes an allocated N_FILE_INFO into @p result,
 *     keeping @p result sorted by n_comp_file_info (oldest -> newest).
 *
 * Error handling:
 *   - If opendir() fails: returns FALSE.
 *   - If stat() fails for an entry: logs and continues.
 *   - If readdir() fails: logs and returns FALSE.
 *
 * @param dir     Directory to scan (non-NULL)
 * @param result  LIST to fill (non-NULL)
 * @param recurse TRUE/FALSE recursion flag
 * @return TRUE on success, FALSE on fatal error.
 */
int n_scan_dir(const char* dir, LIST* result, const int recurse) {
    DIR* dp = NULL;
    struct dirent* entry = NULL;

    __n_assert(dir, return FALSE);
    __n_assert(result, return FALSE);

    int error = 0;
    dp = opendir(dir);
    error = errno;
    if (!dp) {
        n_log(LOG_ERR, "cannot open directory: %s, %s", dir, strerror(error));
        return FALSE;
    }

    error = 0;
    while ((entry = readdir(dp)) != NULL) {
        const char* name = entry->d_name;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            errno = 0;
            continue;
        }

        char* fullpath = n_path_join(dir, name);
        if (!fullpath) {
            n_log(LOG_ERR, "malloc failed building path for %s/%s", dir, name);
            closedir(dp);
            return FALSE;
        }

        struct stat st;
        if (stat(fullpath, &st) == -1) {
            error = errno;
            n_log(LOG_ERR, "unable to stat %s, %s", fullpath, strerror(error));
            free(fullpath);
            error = 0;
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (recurse != FALSE) {
                if (n_scan_dir(fullpath, result, recurse) == FALSE) {
                    n_log(LOG_ERR, "error while recursively scanning %s", fullpath);
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            N_FILE_INFO* file = NULL;
            Malloc(file, N_FILE_INFO, 1);

            if (file) {
                /* Store full path to avoid ambiguity in recursion results */
                file->name = strdup(fullpath);
                n_set_time_from_stat(file, &st);

                list_push_sorted(result, file, n_comp_file_info, n_free_file_info);
            }
        } else {
            /* Ignore non-regular / non-directory entries (symlinks, sockets, etc.) */
            /* If you want to include symlinks, use lstat() and handle S_ISLNK. */
        }

        free(fullpath);
        errno = error = 0;
    }

    if (errno != 0) {
        n_log(LOG_ERR, "readdir failed for %s: %s", dir, strerror(errno));
        closedir(dp);
        return FALSE;
    }

    closedir(dp);
    return TRUE;
}
