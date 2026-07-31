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
 *@example ex_log.c
 *@brief Nilorea Library log api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/10/2018
 */

#include "nilorea/n_log.h"
#include "nilorea/n_nodup_log.h"

int main(void) {
    puts("LOG_NULL");
    set_log_level(LOG_NULL);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("EMERG");
    set_log_level(LOG_EMERG);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("ALERT");
    set_log_level(LOG_ALERT);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("CRIT");
    set_log_level(LOG_CRIT);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("ERR");
    set_log_level(LOG_ERR);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("WARNING");
    set_log_level(LOG_WARNING);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("NOTICE");
    set_log_level(LOG_NOTICE);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("INFO");
    set_log_level(LOG_INFO);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    puts("DEBUG");
    set_log_level(LOG_DEBUG);
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");
    set_log_file("ex_log.log");
    n_log(LOG_EMERG, "EMERG");
    n_log(LOG_ALERT, "ALERT");
    n_log(LOG_CRIT, "CRIT");
    n_log(LOG_ERR, "ERR");
    n_log(LOG_WARNING, "WARNING");
    n_log(LOG_NOTICE, "NOTICE");
    n_log(LOG_INFO, "INFO");
    n_log(LOG_DEBUG, "DEBUG");

    /* test get_log_level */
    int current_level = get_log_level();
    printf("get_log_level: %d\n", current_level);

    /* test get_log_file */
    FILE* log_file = get_log_file();
    printf("get_log_file: %p\n", (void*)log_file);

    /* test open_sysjrnl / close_sysjrnl */
    open_sysjrnl("ex_log_test");
    n_log(LOG_NOTICE, "Logged after open_sysjrnl");
    close_sysjrnl();

    init_nodup_log(0);

    n_nodup_log(LOG_INFO, "Duplicated test");
    n_nodup_log(LOG_INFO, "Duplicated test");
    n_nodup_log(LOG_INFO, "Duplicated test");
    n_nodup_log_indexed(LOG_INFO, "NODUPINDEX1", "Duplicated test 2");
    n_nodup_log_indexed(LOG_INFO, "NODUPINDEX1", "Duplicated test 2");
    n_nodup_log_indexed(LOG_INFO, "NODUPINDEX2", "Duplicated test 3");
    n_nodup_log_indexed(LOG_INFO, "NODUPINDEX2", "Duplicated test 3");

    dump_nodup_log("log_nodup.log");

    close_nodup_log();

    TS_LOG* SAFELOG = NULL;
    open_safe_logging(&SAFELOG, "ex_log_safe.log", "w");
    write_safe_log(SAFELOG, "%s(%d): %s", __FILE__, __LINE__, __func__);
    write_safe_log(SAFELOG, "%s(%d): %s", __FILE__, __LINE__, __func__);
    write_safe_log(SAFELOG, "%s(%d): %s", __FILE__, __LINE__, __func__);
    write_safe_log(SAFELOG, "%s(%d): %s", __FILE__, __LINE__, __func__);
    write_safe_log(SAFELOG, "%s(%d): %s", __FILE__, __LINE__, __func__);

    close_safe_logging(SAFELOG);

    exit(0);
}
