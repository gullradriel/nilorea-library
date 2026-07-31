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
 *@example ex_json.c
 *@brief n_json wrapper: parse, typed getters, arrays, and file parsing.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nilorea/n_json.h"
#include "nilorea/n_log.h"

static int log_level = LOG_ERR;
static int failures = 0;

static int process_args(int argc, char** argv) {
    int opt;
    while ((opt = getopt(argc, argv, "V:")) != -1) {
        switch (opt) {
            case 'V':
                if (!strcmp("LOG_NULL", optarg))
                    log_level = LOG_NULL;
                else if (!strcmp("LOG_NOTICE", optarg))
                    log_level = LOG_NOTICE;
                else if (!strcmp("LOG_INFO", optarg))
                    log_level = LOG_INFO;
                else if (!strcmp("LOG_ERR", optarg))
                    log_level = LOG_ERR;
                else if (!strcmp("LOG_DEBUG", optarg))
                    log_level = LOG_DEBUG;
                else
                    return -1;
                break;
            default:
                return -1;
        }
    }
    return 0;
}

static void expect_true(const char* label, int cond) {
    if (!cond) {
        n_log(LOG_ERR, "%s: condition false", label);
        failures++;
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    if (process_args(argc, argv) != 0) {
        fprintf(stderr, "Usage: %s [-V LOG_LEVEL]\n", argv[0]);
        return 2;
    }
    set_log_level(log_level);

    /* invalid JSON yields NULL */
    expect_true("invalid parse is NULL", n_json_parse("{ not json") == NULL);

    {
        const char* text =
            "{ \"name\": \"scan\", \"depth\": 3, \"active\": true,"
            "  \"meta\": { \"host\": \"example.test\" },"
            "  \"tags\": [ \"a\", \"b\", \"c\" ] }";
        N_JSON* root = n_json_parse(text);
        N_JSON* meta;
        N_JSON* tags;
        char* dup;
        expect_true("root parsed", root != NULL);
        if (!root)
            return 1;

        /* typed getters with defaults */
        expect_true("string", strcmp(n_json_string(root, "name", "?"), "scan") == 0);
        expect_true("string default", strcmp(n_json_string(root, "missing", "def"), "def") == 0);
        expect_true("int", n_json_int(root, "depth", -1) == 3);
        expect_true("int default", n_json_int(root, "missing", 7) == 7);
        expect_true("bool", n_json_bool(root, "active", 0) == 1);
        expect_true("bool default", n_json_bool(root, "missing", 1) == 1);

        /* string_dup returns an owned copy, NULL when absent */
        dup = n_json_string_dup(root, "name");
        expect_true("string_dup", dup && strcmp(dup, "scan") == 0);
        free(dup);
        expect_true("string_dup absent is NULL", n_json_string_dup(root, "missing") == NULL);

        /* nested object */
        meta = n_json_get(root, "meta");
        expect_true("nested object", n_json_is_object(meta));
        expect_true("nested string", strcmp(n_json_string(meta, "host", "?"), "example.test") == 0);

        /* array */
        tags = n_json_get(root, "tags");
        expect_true("array", n_json_is_array(tags));
        expect_true("array size", n_json_array_size(tags) == 3);
        expect_true("array elem", strcmp(n_json_as_string(n_json_array_at(tags, 1), "?"), "b") == 0);
        expect_true("array out of range", n_json_array_at(tags, 9) == NULL);

        n_json_free(&root);
        expect_true("free nulls", root == NULL);
    }

    /* parse a file */
    {
        const char* path = "ex_json_tmp.json";
        FILE* fp = fopen(path, "w");
        N_JSON* root;
        if (fp) {
            fputs("{ \"k\": \"v\", \"n\": 42 }", fp);
            fclose(fp);
        }
        expect_true("temp file written", fp != NULL);
        root = n_json_parse_file(path);
        expect_true("file parsed", root != NULL);
        if (root) {
            expect_true("file string", strcmp(n_json_string(root, "k", "?"), "v") == 0);
            expect_true("file int", n_json_int(root, "n", 0) == 42);
            n_json_free(&root);
        }
        unlink(path);
        expect_true("missing file is NULL", n_json_parse_file("ex_json_no_such.json") == NULL);
    }

    if (failures) {
        n_log(LOG_ERR, "ex_json: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_json: all checks passed");
    return 0;
}
