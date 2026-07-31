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
 *@file n_config_file.h
 *@brief Config file reading and writing
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/03/05
 */

#ifndef __N_CONFIG_FILE__
#define __N_CONFIG_FILE__

#ifdef __cplusplus
extern "C" {
#endif

#include "n_hash.h"
#include "n_str.h"
#include "n_log.h"

/**@defgroup CONFIGFILE CONFIGURATIONS: classic config file loading/saving
  @addtogroup CONFIGFILE
  @{
  */

/*! maximum length of a single config line */
#define MAX_CONFIG_LINE_LEN 1024
/*! size of the hash table of config sections entries */
#define CONFIG_SECTION_HASH_TABLE_LEN 16

/*! Structure of a config section */
typedef struct CONFIG_FILE_SECTION {
    /*! name of the config section */
    char* section_name;
    /*! hash table of key-value entries */
    HASH_TABLE* entries;
} CONFIG_FILE_SECTION;

/*! Structure of a config file */
typedef struct CONFIG_FILE {
    /*! path and name of the config file */
    char* filename;
    /*! list of CONFIG_FILE_SECTION */
    LIST* sections;
} CONFIG_FILE;

/*! load a config from a file */
CONFIG_FILE* load_config_file(char* filename, int* errors);
/*! write file from config */
int write_config_file(CONFIG_FILE* cfg_file, char* filename);
/*! get a loaded config value */
char* get_config_section_value(CONFIG_FILE* cfg_file, char* section_name, size_t section_position, char* entry, size_t entry_position);
/*! get the number of section for section name */
size_t get_nb_config_file_sections(CONFIG_FILE* cfg_file, const char* section_name);
/*! get the number of entries for section_name/entry */
size_t get_nb_config_file_sections_entries(CONFIG_FILE* cfg_file, const char* section_name, size_t section_position, char* entry);
/*! destroy a loaded config */
int destroy_config_file(CONFIG_FILE** cfg_file);

/*! Foreach elements of CONFIG_FILE macro, i.e config_foreach( config , section , key , val ); config_endfor; */
#define config_foreach(__config, __section_name, __key, __val)                    \
    if (!__config || !__config->sections) {                                       \
        n_log(LOG_ERR, "No config file for %s", #__config);                       \
    } else {                                                                      \
        list_foreach(listnode, __config->sections) {                              \
            CONFIG_FILE_SECTION* __section = (CONFIG_FILE_SECTION*)listnode->ptr; \
            __section_name = __section->section_name;                             \
            ht_foreach(entry, __section->entries) {                               \
                HASH_NODE* htnode = (HASH_NODE*)entry->ptr;                       \
                if (htnode && htnode->data.ptr) {                                 \
                    __key = htnode->key;                                          \
                    __val = htnode->data.ptr;

/*! Foreach elements of CONFIG_FILE macro END. Will cause errors if ommitted */
#define config_endfor \
    }                 \
    }                 \
    }                 \
    }

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /* #ifndef __N_CONFIG_FILE__ */
