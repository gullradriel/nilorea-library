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
 *@example ex_pretty.c
 *@brief n_pretty regression: JSON, XML/HTML and JavaScript reformatting.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_pretty.h"
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
                fprintf(stderr, "ex_pretty\n");
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

static void expect_contains(const char* label, const N_STR* out, const char* needle) {
    if (!out || !out->data || !strstr(out->data, needle)) {
        n_log(LOG_ERR, "%s: output does not contain '%s' (got: %s)",
              label, needle, (out && out->data) ? out->data : "NULL");
        failures++;
    }
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    /* JSON */
    {
        N_STR* out = n_pretty_json("{\"a\":1,\"b\":[true,null],\"c\":\"x\"}");
        expect_true("json valid returns output", out != NULL);
        expect_contains("json output is multi-line", out, "\n");
        expect_contains("json key survives", out, "\"a\":");
        free_nstr(&out);

        out = n_pretty_json("this is not json");
        expect_true("json garbage returns NULL", out == NULL);
        free_nstr(&out);

        out = n_pretty_json("{\"a\":");
        expect_true("json truncated returns NULL", out == NULL);
        free_nstr(&out);
    }

    /* JSON, lenient: what the parsing printer refuses still has to become
       readable, since a body cut at a size limit is the common case */
    {
        N_STR* out = n_pretty_json_lenient("{\"a\":1,\"b\":[true,null],\"c\":\"x\"}");
        expect_true("lenient valid returns output", out != NULL);
        expect_contains("lenient indents members", out, "\n  \"a\": 1,\n");
        expect_contains("lenient indents nested values", out, "\n    true,\n");
        expect_contains("lenient closes at depth", out, "\n}\n");
        free_nstr(&out);

        out = n_pretty_json_lenient("{\"a\":1,\"b\":{\"c\":\"trunca");
        expect_true("lenient truncated returns output", out != NULL);
        expect_contains("lenient keeps the cut string", out, "\"trunca");
        expect_contains("lenient still broke the line", out, "\n");
        free_nstr(&out);

        /* punctuation inside a string must not break the line */
        out = n_pretty_json_lenient("{\"a\":\"x,y{z}\"}");
        expect_true("lenient string returns output", out != NULL);
        expect_contains("lenient copies a string verbatim", out, "\"x,y{z}\"");
        free_nstr(&out);

        /* an empty container stays on its line rather than opening a level */
        out = n_pretty_json_lenient("{\"a\":[],\"b\":{ }}");
        expect_true("lenient empty container returns output", out != NULL);
        expect_contains("lenient keeps [] together", out, "\"a\": [],");
        expect_contains("lenient keeps {} together", out, "\"b\": {}");
        free_nstr(&out);
    }

    /* XML / HTML */
    {
        N_STR* out = n_pretty_xml("<root><a x=\"1\"><b>hi</b></a></root>");
        expect_true("xml valid returns output", out != NULL);
        expect_contains("xml child is indented", out, "\n  <a x=\"1\">\n");
        expect_contains("xml grandchild is indented", out, "\n    <b>\n");
        expect_contains("xml text is indented", out, "\n      hi\n");
        expect_contains("xml close returns to depth", out, "\n</root>\n");
        free_nstr(&out);

        /* void element must not indent what follows; comments pass through */
        out = n_pretty_xml("<div><br><!-- note --><span>x</span></div>");
        expect_true("html returns output", out != NULL);
        expect_contains("html br stays at depth", out, "\n  <br>\n");
        expect_contains("html comment kept", out, "\n  <!-- note -->\n");
        expect_contains("html span stays at depth", out, "\n  <span>\n");
        free_nstr(&out);

        /* script content is raw text: the inner '<' is not a tag */
        out = n_pretty_xml("<html><script>if(a<b){c();}</script></html>");
        expect_true("html script returns output", out != NULL);
        expect_contains("script content verbatim", out, "if(a<b){c();}");
        expect_contains("script close tag kept", out, "</script>");
        free_nstr(&out);

        /* attribute values may contain '>' */
        out = n_pretty_xml("<a title=\"1 > 0\">t</a>");
        expect_true("xml quoted gt returns output", out != NULL);
        expect_contains("xml quoted gt kept in tag", out, "<a title=\"1 > 0\">");
        free_nstr(&out);

        out = n_pretty_xml("plain text, no markup");
        expect_true("xml non-markup returns NULL", out == NULL);
        free_nstr(&out);
    }

    /* JavaScript */
    {
        N_STR* out = n_pretty_js("function f(a){if(a){return 1;}else{return 2;}}var x=f(1);");
        expect_true("js valid returns output", out != NULL);
        expect_contains("js opens a block", out, "{\n");
        expect_contains("js indents body", out, "\n  if(a){\n");
        expect_contains("js indents nested body", out, "\n    return 1;\n");
        expect_contains("js statement gets its own line", out, "\nvar x=f(1);\n");
        free_nstr(&out);

        /* structural characters inside strings/regex must not break lines */
        out = n_pretty_js("var s=\"x;y{z}\";var re=/a\\/b;{/g;var t='a;b';");
        expect_true("js literals return output", out != NULL);
        expect_contains("js double-quoted string intact", out, "\"x;y{z}\"");
        expect_contains("js regex intact", out, "/a\\/b;{/g");
        expect_contains("js single-quoted string intact", out, "'a;b'");
        free_nstr(&out);

        /* template literal spans lines and holds braces */
        out = n_pretty_js("const q=`a{b}\n${x({})}`;done();");
        expect_true("js template returns output", out != NULL);
        expect_contains("js template intact", out, "`a{b}\n${x({})}`");
        expect_contains("js statement after template", out, "done();");
        free_nstr(&out);

        /* a regex right after an expression keyword is a regex, not division */
        out = n_pretty_js("function t(a){return /x;{/.test(a);}");
        expect_true("js keyword regex returns output", out != NULL);
        expect_contains("js regex after return intact", out, "return /x;{/.test(a);");
        free_nstr(&out);

        /* a '/' after an identifier or a closing paren is division */
        out = n_pretty_js("var x=a/b;var y=f(1)/2;done();");
        expect_true("js division returns output", out != NULL);
        expect_contains("js division after identifier", out, "var x=a/b;\n");
        expect_contains("js division after paren", out, "var y=f(1)/2;\n");
        free_nstr(&out);

        /* for(;;) semicolons at paren depth > 0 do not break the line */
        out = n_pretty_js("for(i=0;i<3;i++){go(i);}");
        expect_true("js for returns output", out != NULL);
        expect_contains("js for header on one line", out, "for(i=0;i<3;i++){");
        free_nstr(&out);

        /* '} else {' style input: terminator glued to the closing brace */
        out = n_pretty_js("a();};next();");
        expect_true("js brace-semicolon returns output", out != NULL);
        expect_contains("js terminator glued to brace", out, "\n};\n");
        free_nstr(&out);
    }

    /* NULL / empty inputs */
    expect_true("json NULL returns NULL", n_pretty_json(NULL) == NULL);
    expect_true("lenient NULL returns NULL", n_pretty_json_lenient(NULL) == NULL);
    expect_true("lenient empty returns NULL", n_pretty_json_lenient("") == NULL);
    expect_true("xml NULL returns NULL", n_pretty_xml(NULL) == NULL);
    expect_true("js NULL returns NULL", n_pretty_js(NULL) == NULL);
    expect_true("xml empty returns NULL", n_pretty_xml("") == NULL);
    expect_true("js empty returns NULL", n_pretty_js("") == NULL);

    if (failures) {
        n_log(LOG_ERR, "ex_pretty: %d failure(s)", failures);
        return 1;
    }
    printf("ex_pretty: all tests passed\n");
    return 0;
}
