/**\example ex_log.c Nilorea Library log api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 24/10/2018
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
    Free(SAFELOG);

    exit(0);
}
