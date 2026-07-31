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
 *@example ex_pcre.c
 *@brief Nilorea Library pcre api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 04/12/2019
 */

#include "nilorea/n_log.h"
#include "nilorea/n_pcre.h"

#include <string.h>

/* Headless self-test of npcre_replace ($N capture refs, global substitution). */
static int test_replace(const char* pattern, const char* subject, const char* repl, const char* want) {
    N_PCRE* re = npcre_new((char*)pattern, 0);
    N_STR* out = re ? npcre_replace(re, subject, strlen(subject), repl) : NULL;
    int ok = (out && out->data && strcmp(out->data, want) == 0);
    if (!ok)
        n_log(LOG_ERR, "npcre_replace(/%s/, '%s', '%s') -> '%s', expected '%s'",
              pattern, subject, repl, (out && out->data) ? out->data : "(null)", want);
    if (out)
        free_nstr(&out);
    if (re)
        npcre_delete(&re);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    int failures = 0;
    set_log_level(LOG_STDERR);
    set_log_level(LOG_DEBUG);

    n_log(LOG_INFO, "Starting !");

    /* always-on regression for npcre_replace (no CLI args required) */
    failures += test_replace("(\\w+)@(\\w+)", "a@b and c@d", "$2.$1", "b.a and d.c");
    failures += test_replace("foo", "no match here", "bar", "no match here");
    failures += test_replace("[0-9]+", "id=42&n=7", "#", "id=#&n=#");
    failures += test_replace("a", "", "X", "");
    if (failures) {
        n_log(LOG_ERR, "ex_pcre: %d npcre_replace failure(s)", failures);
        exit(1);
    }
    n_log(LOG_NOTICE, "ex_pcre: npcre_replace self-tests passed");

    if (argc < 3) {
        n_log(LOG_INFO, "ex_pcre: pass \"regexp\" \"string\" to also run the match demo");
        exit(0);
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
