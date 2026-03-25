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
size_t get_nb_config_file_sections(CONFIG_FILE* cfg_file, char* section_name);
/*! get the number of entries for section_name/entry */
size_t get_nb_config_file_sections_entries(CONFIG_FILE* cfg_file, char* section_name, size_t section_position, char* entry);
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
