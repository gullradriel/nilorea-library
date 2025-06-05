/**\file n_files.h
 *  files configuration header
 *\author Castagnier Mickael
 *\version 1.0
 *\date 02/11/23
 */

#ifndef __N_FILES_HEADER
#define __N_FILES_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**\defgroup N_FILES FILES: files utilies
  \addtogroup N_FILES
  @{
  */

#include "nilorea/n_str.h"
#include "nilorea/n_list.h"
#include "nilorea/n_log.h"
#include "dirent.h"

/*! common file information */
typedef struct N_FILE_INFO {
    /*! file name */
    char* name;
    /*! file creation time */
    time_t filetime, filetime_nsec;
} N_FILE_INFO;

int n_scan_dir(const char* dir, LIST* result, const int recurse);
void n_free_file_info(void* ptr);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif
