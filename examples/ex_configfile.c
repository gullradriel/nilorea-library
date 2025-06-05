/**\example ex_configfile.c Nilorea Library config api test
 *\author Mickael Castagnier
 *\version 1.0
 *\date 2018-26-10
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
    int nb = get_nb_config_file_sections(config, "__DEFAULT__");
    for (int it = 0; it < nb; it++) {
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
    n_log(LOG_INFO, "%d SECTION sections", nb);
    for (int it = 0; it < nb; it++) {
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
    n_log(LOG_INFO, "%d SECTION_FILE sections", nb);
    for (int it = 0; it < nb; it++) {
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
    n_log(LOG_INFO, "%d SECTION_FILE_MULTICMD sections", nb);
    for (int it = 0; it < nb; it++) {
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
        int nb_cmd = get_nb_config_file_sections_entries(config, "SECTION_FILE_MULTICMD", it, "command");
        for (int it1 = 0; it1 < nb_cmd; it1++) {
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

    destroy_config_file(&config);
    exit(0);
}
