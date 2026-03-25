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
 *@example ex_configfile.c
 *@brief Nilorea Library config api test
 *@author Mickael Castagnier
 *@version 1.0
 *@date 2018-26-10
 */

#include "nilorea/n_config_file.h"

int main(int argc, char* argv[]) {
    set_log_level(LOG_DEBUG);
    if (argc < 2) {
        n_log(LOG_ERR, "Not enough arguments. Use ex_configfile file");
        exit(1);
    }
    int errors = 0;
    CONFIG_FILE* config = load_config_file(argv[1], &errors);
    if (!config) {
        n_log(LOG_ERR, "Unable to load config file from %s", argv[1]);
        exit(1);
    }
    if (errors != 0) {
        n_log(LOG_ERR, "There were %d errors in %s. Check the logs !", errors, argv[1]);
    }
    /* default section, only one should be allocated. Let's test it ! */
    size_t nb = get_nb_config_file_sections(config, "__DEFAULT__");
    for (size_t it = 0; it < nb; it++) {
        n_log(LOG_INFO, "[DEFAULT]");
        char* value = NULL;
        value = get_config_section_value(config, "__DEFAULT__", it, "check_interval", 0);
        n_log(LOG_INFO, "check_interval:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "__DEFAULT__", it, "refresh_interval", 0);
        n_log(LOG_INFO, "refresh_interval:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "__DEFAULT__", it, "cache_file", 0);
        n_log(LOG_INFO, "cache_file:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "__DEFAULT__", it, "cache_file_swp", 0);
        n_log(LOG_INFO, "cache_file_swp:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "__DEFAULT__", it, "cache_refresh_interval", 0);
        n_log(LOG_INFO, "cache_refresh_interval:%s", (value != NULL) ? value : "NULL");
    }
    /* get a loaded config value */
    nb = get_nb_config_file_sections(config, "SECTION");
    n_log(LOG_INFO, "%zu SECTION sections", nb);
    for (size_t it = 0; it < nb; it++) {
        n_log(LOG_INFO, "[SECTION]");
        char* value = NULL;
        value = get_config_section_value(config, "SECTION", it, "check_interval", 0);
        n_log(LOG_INFO, "check_interval:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION", it, "refresh_interval", 0);
        n_log(LOG_INFO, "refresh_interval:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION", it, "cache_file", 0);
        n_log(LOG_INFO, "cache_file:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION", it, "cache_file_swp", 0);
        n_log(LOG_INFO, "cache_file_swp:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION", it, "cache_refresh_interval", 0);
        n_log(LOG_INFO, "cache_refresh_interval:%s", (value != NULL) ? value : "NULL");
    }
    /* get a loaded config value */
    nb = get_nb_config_file_sections(config, "SECTION_FILE");
    n_log(LOG_INFO, "%zu SECTION_FILE sections", nb);
    for (size_t it = 0; it < nb; it++) {
        n_log(LOG_INFO, "[SECTION_FILE]");
        char* value = NULL;
        value = get_config_section_value(config, "SECTION_FILE", it, "file_id", 0);
        n_log(LOG_INFO, "file_id:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION_FILE", it, "file", 0);
        n_log(LOG_INFO, "file:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION_FILE", it, "check_interval", 0);
        n_log(LOG_INFO, "check_interval:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION_FILE", it, "command", 0);
        n_log(LOG_INFO, "command:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION_FILE", it, "command_timeout", 0);
        n_log(LOG_INFO, "command_timeout:%s", (value != NULL) ? value : "NULL");
    }
    /* get a loaded config value */
    nb = get_nb_config_file_sections(config, "SECTION_FILE_MULTICMD");
    n_log(LOG_INFO, "%zu SECTION_FILE_MULTICMD sections", nb);
    for (size_t it = 0; it < nb; it++) {
        n_log(LOG_INFO, "[SECTION_FILE_MULTICMD]");
        char* value = NULL;
        value = get_config_section_value(config, "SECTION_FILE_MULTICMD", it, "file_id", 0);
        n_log(LOG_INFO, "file_id:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION_FILE_MULTICMD", it, "file", 0);
        n_log(LOG_INFO, "file:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION_FILE_MULTICMD", it, "check_interval", 0);
        n_log(LOG_INFO, "check_interval:%s", (value != NULL) ? value : "NULL");
        value = get_config_section_value(config, "SECTION_FILE_MULTICMD", it, "command_timeout", 0);
        n_log(LOG_INFO, "command_timeout:%s", (value != NULL) ? value : "NULL");
        size_t nb_cmd = get_nb_config_file_sections_entries(config, "SECTION_FILE_MULTICMD", it, "command");
        for (size_t it1 = 0; it1 < nb_cmd; it1++) {
            value = get_config_section_value(config, "SECTION_FILE_MULTICMD", it, "command", it1);
            n_log(LOG_INFO, "command:%s", (value != NULL) ? value : "NULL");
        }
    }
    n_log(LOG_INFO, "\n\nCONFIG_FILE foreach !!");

    char *section_name = NULL, *key = NULL, *val = NULL;
    config_foreach(config, section_name, key, val) {
        n_log(LOG_INFO, "[%s]%s:%s", section_name, key, val);
    }
    config_endfor;

    /* test write_config_file */
    if (write_config_file(config, "nilorea_ex_configfile_out.cfg") == TRUE) {
        n_log(LOG_INFO, "write_config_file: written to nilorea_ex_configfile_out.cfg");
    } else {
        n_log(LOG_ERR, "write_config_file failed");
    }

    destroy_config_file(&config);
    exit(0);
}
