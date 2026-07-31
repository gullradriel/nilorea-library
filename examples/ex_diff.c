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
 *@example ex_diff.c
 *@brief n_diff regression: line diff, hunk rendering and the edit ceiling.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_diff.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

/* Parse the -V LOG_LEVEL verbosity argument. */
static void process_args(int argc, char** argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hvV:")) != EOF) {
        switch (opt) {
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 8))
                    set_log_level(LOG_NULL);
                else if (!strncmp("LOG_NOTICE", optarg, 10))
                    set_log_level(LOG_NOTICE);
                else if (!strncmp("LOG_INFO", optarg, 8))
                    set_log_level(LOG_INFO);
                else if (!strncmp("LOG_ERR", optarg, 7))
                    set_log_level(LOG_ERR);
                else if (!strncmp("LOG_DEBUG", optarg, 9))
                    set_log_level(LOG_DEBUG);
                else {
                    fprintf(stderr, "Unknown log level %s\n", optarg);
                    exit(1);
                }
                break;
            case 'v':
                fprintf(stderr, "ex_diff\n");
                exit(1);
            default:
                fprintf(stderr, "Usage: %s [-V LOG_LEVEL]\n", argv[0]);
                exit(1);
        }
    }
}

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    }
}

static void expect_size(const char* label, size_t got, size_t want) {
    if (got != want) {
        n_log(LOG_ERR, "%s: got %zu, expected %zu", label, got, want);
        failures++;
    }
}

static void expect_contains(const char* label, const N_STR* out, const char* needle) {
    if (!out || !out->data || !strstr(out->data, needle)) {
        n_log(LOG_ERR, "%s: output does not contain '%s' (got: %s)",
              label, needle, (out && out->data) ? out->data : "NULL");
        failures++;
    }
}

static void expect_missing(const char* label, const N_STR* out, const char* needle) {
    if (!out || !out->data || strstr(out->data, needle)) {
        n_log(LOG_ERR, "%s: output should not contain '%s' (got: %s)",
              label, needle, (out && out->data) ? out->data : "NULL");
        failures++;
    }
}

/* Render a script with full context and check it against an expected text. */
static void expect_unified(const char* label, const char* a, const char* b, const char* want) {
    N_DIFF_RESULT* res = n_diff_lines(a, b, 0);
    N_STR* out = NULL;

    if (!res) {
        n_log(LOG_ERR, "%s: n_diff_lines returned NULL", label);
        failures++;
        return;
    }
    out = n_diff_to_unified(res, N_DIFF_CONTEXT_FULL);
    if (!out || !out->data || strcmp(out->data, want) != 0) {
        n_log(LOG_ERR, "%s: got [%s], expected [%s]", label,
              (out && out->data) ? out->data : "NULL", want);
        failures++;
    }
    free_nstr(&out);
    n_diff_result_free(&res);
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    /* identical texts produce common lines only */
    {
        N_DIFF_RESULT* res = n_diff_lines("a\nb\nc", "a\nb\nc", 0);
        expect_true("identical returns a result", res != NULL);
        expect_size("identical common count", res->nb_common, 3);
        expect_size("identical delete count", res->nb_deleted, 0);
        expect_size("identical insert count", res->nb_inserted, 0);
        expect_true("identical not truncated", res->truncated == 0);
        n_diff_result_free(&res);
    }

    /* a trailing newline closes the last line, it does not open an empty one */
    {
        N_DIFF_RESULT* res = n_diff_lines("a\nb\n", "a\nb", 0);
        expect_size("trailing newline common count", res->nb_common, 2);
        expect_size("trailing newline no change", res->nb_deleted + res->nb_inserted, 0);
        n_diff_result_free(&res);
    }

    /* the point of an LCS: one inserted line must not shift everything after
       it into a run of changes */
    {
        N_DIFF_RESULT* res = n_diff_lines("a\nb\nc\nd", "a\nx\nb\nc\nd", 0);
        expect_size("insert keeps 4 common", res->nb_common, 4);
        expect_size("insert adds 1", res->nb_inserted, 1);
        expect_size("insert deletes nothing", res->nb_deleted, 0);
        n_diff_result_free(&res);
    }
    expect_unified("inserted line rendering", "a\nb\nc\nd", "a\nx\nb\nc\nd",
                   " a\n+x\n b\n c\n d\n");

    /* deletion, replacement and edits at both ends */
    expect_unified("deleted line", "a\nb\nc", "a\nc", " a\n-b\n c\n");
    expect_unified("replaced line", "a\nb\nc", "a\nB\nc", " a\n-b\n+B\n c\n");
    expect_unified("edit at head", "a\nb", "x\nb", "-a\n+x\n b\n");
    expect_unified("edit at tail", "a\nb", "a\nx", " a\n-b\n+x\n");

    /* empty sides */
    expect_unified("empty to content", "", "a\nb", "+a\n+b\n");
    expect_unified("content to empty", "a\nb", "", "-a\n-b\n");
    {
        N_DIFF_RESULT* res = n_diff_lines(NULL, NULL, 0);
        expect_true("NULL inputs return a result", res != NULL);
        expect_size("NULL inputs give no lines", res->lines->nb_items, 0);
        n_diff_result_free(&res);

        res = n_diff_lines("", "", 0);
        expect_size("empty inputs give no lines", res->lines->nb_items, 0);
        n_diff_result_free(&res);
    }

    /* carriage returns are content, not noise: LF -> CRLF is a real change */
    {
        N_DIFF_RESULT* res = n_diff_lines("a\nb", "a\r\nb", 0);
        expect_size("CRLF differs from LF", res->nb_deleted, 1);
        n_diff_result_free(&res);
    }

    /* line numbers are positions in the whole text, past the trimmed head */
    {
        N_DIFF_RESULT* res = n_diff_lines("a\nb\nc\nd", "a\nb\nX\nd", 0);
        int checked = 0;
        list_foreach(node, res->lines) {
            N_DIFF_LINE* line = (N_DIFF_LINE*)node->ptr;
            if (line->op == N_DIFF_DELETE) {
                expect_size("delete line_a", line->line_a, 2);
                expect_true("delete has no line_b", line->line_b == N_DIFF_NO_LINE);
                checked++;
            } else if (line->op == N_DIFF_INSERT) {
                expect_size("insert line_b", line->line_b, 2);
                expect_true("insert has no line_a", line->line_a == N_DIFF_NO_LINE);
                checked++;
            }
        }
        expect_size("both changed lines inspected", (size_t)checked, 2);
        n_diff_result_free(&res);
    }

    /* hunk rendering drops common lines further than context from a change */
    {
        N_DIFF_RESULT* res = n_diff_lines("1\n2\n3\n4\n5\n6\n7\n8\n9",
                                          "1\n2\n3\n4\nX\n6\n7\n8\n9", 0);
        N_STR* out = n_diff_to_unified(res, 1);
        expect_contains("hunk header present", out, "@@ -4,3 +4,3 @@");
        expect_contains("hunk keeps one line before", out, " 4\n");
        expect_contains("hunk shows the change", out, "-5\n+X\n");
        expect_contains("hunk keeps one line after", out, " 6\n");
        expect_missing("hunk drops distant head", out, " 1\n");
        expect_missing("hunk drops distant tail", out, " 9\n");
        free_nstr(&out);

        /* full context keeps everything and writes no header */
        out = n_diff_to_unified(res, N_DIFF_CONTEXT_FULL);
        expect_contains("full context keeps head", out, " 1\n");
        expect_contains("full context keeps tail", out, " 9\n");
        expect_missing("full context has no header", out, "@@");
        free_nstr(&out);
        n_diff_result_free(&res);
    }

    /* two changes separated by no more than 2*context common lines merge
       into a single hunk, like diff(1) does */
    {
        N_DIFF_RESULT* res = n_diff_lines("1\n2\n3\n4", "X\n2\n3\nY", 0);
        N_STR* out = n_diff_to_unified(res, 1);
        int hunks = 0;
        for (const char* p = out->data; (p = strstr(p, "@@ -")) != NULL; p++) {
            hunks++;
        }
        expect_size("nearby changes merge", (size_t)hunks, 1);
        free_nstr(&out);
        n_diff_result_free(&res);
    }

    /* distant changes stay in separate hunks */
    {
        N_DIFF_RESULT* res = n_diff_lines("1\n2\n3\n4\n5\n6\n7\n8\n9",
                                          "X\n2\n3\n4\n5\n6\n7\n8\nY", 0);
        N_STR* out = n_diff_to_unified(res, 1);
        int hunks = 0;
        for (const char* p = out->data; (p = strstr(p, "@@ -")) != NULL; p++) {
            hunks++;
        }
        expect_size("distant changes split", (size_t)hunks, 2);
        free_nstr(&out);
        n_diff_result_free(&res);
    }

    /* past the edit ceiling the whole differing region is reported coarsely,
       and the result says so instead of pretending to be exact */
    {
        N_STR* a = new_nstr(1024);
        N_STR* b = new_nstr(1024);
        N_DIFF_RESULT* res = NULL;

        for (int i = 0; i < 40; i++) {
            nstrprintf_cat(a, "line a %d\n", i);
            nstrprintf_cat(b, "line b %d\n", i);
        }
        res = n_diff_lines(_nstr(a), _nstr(b), 4);
        expect_true("over ceiling returns a result", res != NULL);
        expect_true("over ceiling is flagged", res->truncated == 1);
        expect_size("over ceiling deletes every a line", res->nb_deleted, 40);
        expect_size("over ceiling inserts every b line", res->nb_inserted, 40);
        expect_size("over ceiling keeps nothing common", res->nb_common, 0);
        n_diff_result_free(&res);

        /* the same pair within a generous ceiling is exact */
        res = n_diff_lines(_nstr(a), _nstr(b), 0);
        expect_true("under ceiling is not flagged", res->truncated == 0);
        n_diff_result_free(&res);

        free_nstr(&a);
        free_nstr(&b);
    }

    /* a small change inside two large identical bodies stays well under the
       ceiling, because the matching head and tail are trimmed first */
    {
        N_STR* a = new_nstr(65536);
        N_STR* b = new_nstr(65536);
        N_DIFF_RESULT* res = NULL;

        for (int i = 0; i < 5000; i++) {
            nstrprintf_cat(a, "shared line %d\n", i);
            nstrprintf_cat(b, "shared line %d\n", i == 2500 ? -1 : i);
        }
        res = n_diff_lines(_nstr(a), _nstr(b), 0);
        expect_true("large bodies not truncated", res->truncated == 0);
        expect_size("large bodies one delete", res->nb_deleted, 1);
        expect_size("large bodies one insert", res->nb_inserted, 1);
        expect_size("large bodies keep the rest", res->nb_common, 4999);
        n_diff_result_free(&res);

        free_nstr(&a);
        free_nstr(&b);
    }

    /* rendering an empty script is an empty string, not a failure */
    {
        N_DIFF_RESULT* res = n_diff_lines("", "", 0);
        N_STR* out = n_diff_to_unified(res, 3);
        expect_true("empty script renders", out != NULL);
        expect_size("empty script is empty", out->written, 0);
        free_nstr(&out);
        n_diff_result_free(&res);
    }

    if (failures) {
        n_log(LOG_ERR, "ex_diff: %d failure(s)", failures);
        return 1;
    }
    printf("ex_diff: all tests passed\n");
    return 0;
}
