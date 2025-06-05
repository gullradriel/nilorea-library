/**\example ex_pcre.c Nilorea Library pcre api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 04/12/2019
 */

#include "nilorea/n_log.h"
#include "nilorea/n_pcre.h"

int main(int argc, char** argv) {
    set_log_level(LOG_STDERR);
    set_log_level(LOG_DEBUG);

    n_log(LOG_INFO, "Starting !");

    if (argc < 3) {
        n_log(LOG_ERR, "Not enough args\nUsage:\n    %s \"regexp\" \"string\"", argv[0]);
        exit(1);
    }
    N_PCRE* pcre = npcre_new(argv[1], 99, 0);
    if (npcre_match(argv[2], pcre) == TRUE) {
        n_log(LOG_INFO, "MATCHED !");
        if (pcre->captured > 1) {
            n_log(LOG_INFO, "CAPTURED !");
            int it = 0;
            while (pcre->match_list[it]) {
                n_log(LOG_INFO, "Match[%d]:%s", it, pcre->match_list[it]);
                it++;
            }
        }
    } else {
        n_log(LOG_INFO, "NO MATCH !");
    }

    npcre_delete(&pcre);

    exit(0);
}
