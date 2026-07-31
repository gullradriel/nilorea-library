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
 *@example ex_query.c
 *@brief N_QUERY language regression (headless): operators, AND/OR/NOT, errors.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_log.h"
#include "nilorea/n_query.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

/* one sample record exposed to the query engine through a field getter */
typedef struct {
    const char* host;
    const char* method;
    const char* path;
    const char* status;
    const char* mime;
} REC;

static const char* rec_get(const char* field, void* ud) {
    const REC* r = (const REC*)ud;
    if (strcmp(field, "host") == 0)
        return r->host;
    if (strcmp(field, "method") == 0)
        return r->method;
    if (strcmp(field, "path") == 0)
        return r->path;
    if (strcmp(field, "status") == 0)
        return r->status;
    if (strcmp(field, "mime") == 0)
        return r->mime;
    return NULL; /* unknown field */
}

/* compile + evaluate; returns 1/0, or -1 on a compile error */
static int run(const char* expr, const REC* r) {
    char err[160] = "";
    N_QUERY* q = n_query_compile(expr, err, sizeof(err));
    int m;
    if (!q) {
        n_log(LOG_ERR, "compile '%s' failed: %s", expr, err);
        return -1;
    }
    m = n_query_eval(q, rec_get, (void*)r);
    n_query_free(&q);
    return m;
}

static void expect(const char* label, int got, int want) {
    if (got != want) {
        n_log(LOG_ERR, "%s: got %d, expected %d", label, got, want);
        failures++;
    }
}

static void expect_parse_error(const char* expr) {
    char err[160] = "";
    N_QUERY* q = n_query_compile(expr, err, sizeof(err));
    if (q) {
        n_log(LOG_ERR, "expected '%s' to fail compiling", expr);
        n_query_free(&q);
        failures++;
    }
}

int main(void) {
    REC r;
    set_log_level(LOG_ERR);
    r.host = "api.example.com";
    r.method = "GET";
    r.path = "/admin/login";
    r.status = "403";
    r.mime = "text/html";

    /* equality (case-insensitive) and contains */
    expect("eq", run("host = api.example.com", &r), 1);
    expect("eq ci", run("host = API.EXAMPLE.COM", &r), 1);
    expect("eq no", run("host = other.test", &r), 0);
    expect("contains", run("host ~ example", &r), 1);
    expect("contains path", run("path ~ admin", &r), 1);
    expect("not contains", run("path !~ secret", &r), 1);
    expect("ne", run("mime != text/html", &r), 0);

    /* numeric comparison */
    expect("ge", run("status >= 400", &r), 1);
    expect("gt no", run("status > 500", &r), 0);
    expect("range", run("status >= 400 AND status < 500", &r), 1);

    /* regex */
    expect("regex", run("mime =~ ^text/", &r), 1);
    expect("regex no", run("mime =~ ^json", &r), 0);

    /* boolean composition and precedence (AND binds tighter than OR) */
    expect("and", run("method = GET AND status >= 400", &r), 1);
    expect("or", run("method = POST OR status = 403", &r), 1);
    expect("not", run("NOT (status = 200)", &r), 1);
    expect("paren", run("(method = POST OR method = GET) AND status = 403", &r), 1);
    expect("prec", run("method = POST AND status = 200 OR host ~ example", &r), 1);

    /* quoted value, unknown field, empty query */
    expect("quoted", run("path = \"/admin/login\"", &r), 1);
    expect("unknown field", run("bogus = x", &r), 0);
    expect("empty match-all", run("", &r), 1);
    expect("blank match-all", run("   ", &r), 1);

    /* parse errors */
    expect_parse_error("host =");       /* missing value */
    expect_parse_error("host");         /* missing operator */
    expect_parse_error("( host = a");   /* unbalanced paren */
    expect_parse_error("host = a AND"); /* dangling AND */
    expect_parse_error("mime =~ (");    /* invalid regex */

    if (failures) {
        n_log(LOG_ERR, "ex_query: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_query: all checks passed");
    return 0;
}
