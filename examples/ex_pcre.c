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
 *@example ex_pcre.c
 *@brief Nilorea Library pcre api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 04/12/2019
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
    N_PCRE* pcre = npcre_new(argv[1], 0);
    if (npcre_match_capture(argv[2], pcre) == TRUE) {
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

    /* test npcre_clean_match to clear previous match data */
    npcre_clean_match(pcre);
    n_log(LOG_INFO, "npcre_clean_match done, captured: %d", pcre->captured);

    /* reuse the same pattern with a second match */
    if (npcre_match_capture("another test string", pcre) == TRUE) {
        n_log(LOG_INFO, "Second match succeeded");
    } else {
        n_log(LOG_INFO, "Second match: no match");
    }

    npcre_delete(&pcre);

    exit(0);
}
