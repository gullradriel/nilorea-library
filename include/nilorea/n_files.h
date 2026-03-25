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
 *@file n_files.h
 *@brief Files configuration header
 *@author Castagnier Mickael
 *@version 1.0
 *@date 02/11/23
 */

#ifndef __N_FILES_HEADER
#define __N_FILES_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup N_FILES FILES: files utilies
  @addtogroup N_FILES
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

/*! @brief scan a directory and store file information in the result list */
int n_scan_dir(const char* dir, LIST* result, const int recurse);
/*! @brief free a N_FILE_INFO structure */
void n_free_file_info(void* ptr);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif
