/**
 *@file n_files.c
 *@brief Files main source file
 *@author Castagnier Mickael
 *@version 1.0
 *@date 02/11/23
 */

#include "nilorea/n_files.h"

/**
 * @brief free a N_FILE_INFO struct
 * @param ptr the pointer to the struct to free
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
 * @brief local comparison function for sorting filenames, case insensitive
 * @param a First N_FILE_INFO container
 * @param b Second N_FILE_INFO container
 * @return 0 if strings are equals, < 0 if S1 < S2 , > 0 if S1 > S2
 */
static int n_comp_file_info(const void* a, const void* b) {
    N_FILE_INFO *f1, *f2;

    f1 = (N_FILE_INFO*)a;
    f2 = (N_FILE_INFO*)b;

    int ret = -1;

    if (!f1) {
        return -1;
    }
    if (!f2) {
        return 1;
    }

    if (f1->filetime < f2->filetime) {
        ret = -1;
    } else if (f1->filetime == f2->filetime) {
        if (f1->filetime_nsec < f2->filetime_nsec) {
            ret = -1;
        } else if (f1->filetime_nsec > f2->filetime_nsec) {
            ret = 1;
        }
    } else if (f1->filetime > f2->filetime) {
        ret = 1;
    }

    return ret;
}

/**
 *
 *@brief Scan given directory and return a LIST of char. Only files are listed.
 *
 * @param dir The directory to scan
 * @param result An empty LIST *ptr. Will be filled by a char* list of result, sorted by time
 * @param recurse TRUE or FALSE to enable / disable recursion
 *
 * @return TRUE or FALSE
 */
int n_scan_dir(const char* dir, LIST* result, const int recurse) {
    DIR* dp = NULL;
    struct dirent* entry = NULL;
    struct stat statbuf;

    __n_assert(dir, return FALSE);
    __n_assert(result, return FALSE);

    dp = opendir(dir);
    int error = errno;
    if (dp == NULL) {
        n_log(LOG_ERR, "cannot open directory: %s, %s", dir, strerror(error));
        return FALSE;
    }
    if (chdir(dir) == -1) {
        error = errno;
        n_log(LOG_ERR, "cannot chdir to directory %s, %s", dir, strerror(error));
        closedir(dp);
        return FALSE;
    }
    error = 0;
    while ((entry = readdir(dp)) != NULL) {
        if (stat(entry->d_name, &statbuf) != -1) {
            error = errno;
            if (S_ISDIR(statbuf.st_mode)) {
                if (strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) {
                    continue;
                }
                /* Recurse */
                if (recurse != FALSE) {
                    if (n_scan_dir(entry->d_name, result, recurse) == FALSE) {
                        n_log(LOG_ERR, entry->d_name, "error while recursively scanning %s", entry->d_name);
                    }
                }
            } else if (S_ISREG(statbuf.st_mode)) {
                N_FILE_INFO* file = NULL;
                Malloc(file, N_FILE_INFO, 1);
                file->name = strdup(entry->d_name);
#if defined(__linux__)
                file->filetime = statbuf.st_mtime;
                file->filetime_nsec = statbuf.st_mtimensec;
#elif defined(__sun)
                file->filetime = statbuf.st_mtim.tv_sec;
                file->filetime_nsec = statbuf.st_mtim.tv_nsec;
#else
                file->filetime = statbuf.st_mtime;
                file->filetime_nsec = 0;
#endif

                list_push_sorted(result, file, n_comp_file_info, n_free_file_info);
            } else {
                n_log(LOG_ERR, "Not a S_ISDIR or S_ISREG file: %s", entry->d_name);
            }
        } else {
            error = errno;
            n_log(LOG_ERR, "unable to stat %s, %s", entry->d_name, strerror(error));
        }
    }
    if (error != 0) {
        n_log(LOG_ERR, "readdir failed: %s", strerror(error));
    }
    if (chdir("..") == -1) {
        error = errno;
        n_log(LOG_ERR, "cannot chdir back to parent from %s, %s", dir, strerror(error));
    }
    closedir(dp);
    return TRUE;
}
