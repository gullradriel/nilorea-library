/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
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
