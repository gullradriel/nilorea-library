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
 *@example ex_html.c
 *@brief n_html regression: link, form, and sitemap extraction.
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_common.h"
#include "nilorea/n_html.h"
#include "nilorea/n_list.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

/* Parse the -V LOG_LEVEL verbosity argument. */
void process_args(int argc, char** argv) {
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
                fprintf(stderr, "ex_html\n");
                exit(1);
            case 'h':
            default:
                fprintf(stderr, "usage: %s [-V LOG_LEVEL]\n", argv[0]);
                break;
        }
    }
}

static int find_link(LIST* links, const char* want) {
    if (!links) return 0;
    list_foreach(node, links) {
        N_STR* s = (N_STR*)node->ptr;
        if (s && s->data && strcmp(s->data, want) == 0) return 1;
    }
    return 0;
}

static void expect_link_present(LIST* links, const char* want) {
    if (!find_link(links, want)) {
        n_log(LOG_ERR, "missing expected link: '%s'", want);
        failures++;
    } else {
        n_log(LOG_INFO, "link present: '%s'", want);
    }
}

static void expect_link_absent(LIST* links, const char* probe) {
    if (find_link(links, probe)) {
        n_log(LOG_ERR, "unexpected link present: '%s'", probe);
        failures++;
    }
}

static void test_links(void) {
    const char* html =
        "<!DOCTYPE html>\n"
        "<html><head>\n"
        "  <link rel=\"stylesheet\" HREF=\"/css/main.css\">\n"
        "  <script src='/js/app.js'></script>\n"
        "  <!-- a comment with <a href=\"/in/comment\">trap</a> -->\n"
        "</head><body>\n"
        "  <a href=\"/about\">about</a>\n"
        "  <a HREF=/contact>contact</a>\n"
        "  <a href = \"/help\" target=_blank>help</a>\n"
        "  <a>no href</a>\n"
        "  <form action=\"/login\" method=POST><input name=u></form>\n"
        "  <img src=\"/img/logo.png\"/>\n"
        "  <iframe src=\"//cdn.example.com/frame\"></iframe>\n"
        "  <area shape=rect href=\"/map/area1\"/>\n"
        "  <script>var x = '<a href=\"/in/script\">trap</a>';</script>\n"
        "  <style>a[href=\"/in/style\"] { color: red; }</style>\n"
        "</body></html>";
    size_t len = strlen(html);

    LIST* links = n_html_extract_links(html, len);
    if (!links) {
        n_log(LOG_ERR, "extract_links returned NULL");
        failures++;
        return;
    }

    expect_link_present(links, "/css/main.css");
    expect_link_present(links, "/js/app.js");
    expect_link_present(links, "/about");
    expect_link_present(links, "/contact");
    expect_link_present(links, "/help");
    expect_link_present(links, "/login");
    expect_link_present(links, "/img/logo.png");
    expect_link_present(links, "//cdn.example.com/frame");
    expect_link_present(links, "/map/area1");

    /* contents of comments, <script>, and <style> must not leak as links */
    expect_link_absent(links, "/in/comment");
    expect_link_absent(links, "/in/script");
    expect_link_absent(links, "/in/style");

    n_html_links_free(&links);
    if (links) {
        n_log(LOG_ERR, "n_html_links_free did not NULL the pointer");
        failures++;
    }
}

static void test_forms(void) {
    const char* html =
        "<form action=\"/signup\" method=\"post\">"
        "  <input type='text' name='user' value=\"alice\" required>"
        "  <input type=password name=\"pass\">"
        "  <textarea name=bio></textarea>"
        "  <select name=country><option>fr</option></select>"
        "  <button type=submit name=go value=1>Go</button>"
        "</form>"
        "<form>" /* no method/action: defaults */
        "  <input name=q>"
        "</form>";
    size_t len = strlen(html);

    LIST* forms = n_html_extract_forms(html, len);
    if (!forms || forms->nb_items != 2) {
        n_log(LOG_ERR, "expected 2 forms, got %zu", forms ? forms->nb_items : (size_t)0);
        failures++;
        if (forms) n_html_forms_free(&forms);
        return;
    }

    LIST_NODE* node = forms->start;
    N_HTML_FORM* f0 = (N_HTML_FORM*)node->ptr;
    N_HTML_FORM* f1 = (N_HTML_FORM*)node->next->ptr;

    if (!f0->method || strcmp(f0->method, "POST") != 0) {
        n_log(LOG_ERR, "form0 method: got '%s' expected 'POST'", f0->method ? f0->method : "(null)");
        failures++;
    }
    if (!f0->action || strcmp(f0->action, "/signup") != 0) {
        n_log(LOG_ERR, "form0 action: got '%s'", f0->action ? f0->action : "(null)");
        failures++;
    }
    if (!f0->fields || f0->fields->nb_items != 5) {
        n_log(LOG_ERR, "form0 fields: got %zu expected 5", f0->fields ? f0->fields->nb_items : (size_t)0);
        failures++;
    } else {
        LIST_NODE* fn = f0->fields->start;
        N_FORM_FIELD* user = (N_FORM_FIELD*)fn->ptr;
        if (strcmp(user->name, "user") != 0 || strcmp(user->type, "text") != 0 || strcmp(user->value, "alice") != 0 || user->required != 1) {
            n_log(LOG_ERR, "form0 field0 wrong: name=%s type=%s value=%s required=%d", user->name, user->type, user->value, user->required);
            failures++;
        }
        fn = fn->next;
        N_FORM_FIELD* pass = (N_FORM_FIELD*)fn->ptr;
        if (strcmp(pass->name, "pass") != 0 || strcmp(pass->type, "password") != 0 || pass->required != 0) {
            n_log(LOG_ERR, "form0 field1 wrong: name=%s type=%s required=%d", pass->name, pass->type, pass->required);
            failures++;
        }
        fn = fn->next;
        N_FORM_FIELD* bio = (N_FORM_FIELD*)fn->ptr;
        if (strcmp(bio->name, "bio") != 0 || strcmp(bio->type, "text") != 0) {
            n_log(LOG_ERR, "form0 field2 wrong: name=%s type=%s", bio->name, bio->type);
            failures++;
        }
    }

    if (!f1->method || strcmp(f1->method, "GET") != 0) {
        n_log(LOG_ERR, "form1 method: got '%s' expected 'GET'", f1->method ? f1->method : "(null)");
        failures++;
    }
    if (!f1->action || strcmp(f1->action, "") != 0) {
        n_log(LOG_ERR, "form1 action: got '%s' expected ''", f1->action ? f1->action : "(null)");
        failures++;
    }
    if (!f1->fields || f1->fields->nb_items != 1) {
        n_log(LOG_ERR, "form1 fields: got %zu expected 1", f1->fields ? f1->fields->nb_items : (size_t)0);
        failures++;
    }

    n_html_forms_free(&forms);
    if (forms) {
        n_log(LOG_ERR, "n_html_forms_free did not NULL the pointer");
        failures++;
    }
}

static void test_sitemap(void) {
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">"
        "  <url><loc>https://example.com/a</loc><lastmod>2026-01-01</lastmod></url>"
        "  <url>\n  <LOC>\n    https://example.com/b\n  </LOC>\n</url>"
        "  <url><loc>https://example.com/c</loc></url>"
        "</urlset>";
    size_t len = strlen(xml);

    LIST* urls = n_sitemap_extract_urls(xml, len);
    if (!urls || urls->nb_items != 3) {
        n_log(LOG_ERR, "sitemap: expected 3 urls, got %zu", urls ? urls->nb_items : (size_t)0);
        failures++;
        if (urls) n_html_links_free(&urls);
        return;
    }

    LIST_NODE* node = urls->start;
    const char* want[3] = {"https://example.com/a", "https://example.com/b", "https://example.com/c"};
    for (int i = 0; i < 3; i++) {
        N_STR* s = (N_STR*)node->ptr;
        if (!s || !s->data || strcmp(s->data, want[i]) != 0) {
            n_log(LOG_ERR, "sitemap url %d: got '%s' expected '%s'", i, s && s->data ? s->data : "(null)", want[i]);
            failures++;
        }
        node = node->next;
    }

    n_html_links_free(&urls);
}

static void test_empty_and_null(void) {
    LIST* a = n_html_extract_links(NULL, 0);
    LIST* b = n_html_extract_links("", 0);
    LIST* c = n_html_extract_forms(NULL, 0);
    LIST* d = n_sitemap_extract_urls("", 0);
    if (!a || a->nb_items != 0) failures++;
    if (!b || b->nb_items != 0) failures++;
    if (!c || c->nb_items != 0) failures++;
    if (!d || d->nb_items != 0) failures++;
    n_html_links_free(&a);
    n_html_links_free(&b);
    n_html_forms_free(&c);
    n_html_links_free(&d);
}

static void test_enctype(void) {
    const char* html =
        "<form action=\"/up\" method=\"post\" enctype=\"multipart/form-data\"><input name=f></form>"
        "<form action=\"/x\"><input name=g></form>"; /* no enctype: default */
    LIST* forms = n_html_extract_forms(html, strlen(html));
    if (!forms || forms->nb_items != 2) {
        n_log(LOG_ERR, "enctype test: expected 2 forms");
        failures++;
        if (forms) n_html_forms_free(&forms);
        return;
    }
    {
        N_HTML_FORM* f0 = (N_HTML_FORM*)forms->start->ptr;
        N_HTML_FORM* f1 = (N_HTML_FORM*)forms->start->next->ptr;
        if (!f0->enctype || strcmp(f0->enctype, "multipart/form-data") != 0) {
            n_log(LOG_ERR, "form0 enctype: got '%s'", f0->enctype ? f0->enctype : "(null)");
            failures++;
        }
        if (!f1->enctype || strcmp(f1->enctype, "application/x-www-form-urlencoded") != 0) {
            n_log(LOG_ERR, "form1 default enctype: got '%s'", f1->enctype ? f1->enctype : "(null)");
            failures++;
        }
    }
    n_html_forms_free(&forms);
}

/* Assert that to-text output contains (or, when want_present is 0, omits) a
   substring. */
static void expect_text(const char* label, N_STR* txt, const char* needle, int want_present) {
    int present = (txt && txt->data && strstr(txt->data, needle) != NULL);
    if (present != want_present) {
        n_log(LOG_ERR, "to_text %s: '%s' should be %s in '%s'", label, needle, want_present ? "present" : "absent", (txt && txt->data) ? txt->data : "(null)");
        failures++;
    }
}

static void test_to_text(void) {
    /* tags drop, block elements line-break, inline text is kept */
    {
        const char* html = "<html><body><h1>Title</h1><p>Hello <b>world</b></p></body></html>";
        N_STR* t = n_html_to_text(html, strlen(html));
        expect_text("basic", t, "Title", 1);
        expect_text("basic", t, "Hello world", 1);
        expect_text("basic", t, "<", 0); /* no markup leaks through */
        if (t) free_nstr(&t);
    }
    /* entity decoding: named and numeric (decimal + hex) */
    {
        const char* html = "<p>a &amp; b &lt;c&gt; d&#65;&#x42;e &quot;q&quot; n&nbsp;m</p>";
        N_STR* t = n_html_to_text(html, strlen(html));
        expect_text("entities", t, "a & b <c> dABe \"q\" n m", 1);
        if (t) free_nstr(&t);
    }
    /* script and style content is dropped wholesale */
    {
        const char* html = "<p>x</p><script>var a = 1 < 2;</script><style>.c{color:red}</style><p>y</p>";
        N_STR* t = n_html_to_text(html, strlen(html));
        expect_text("script", t, "x", 1);
        expect_text("script", t, "y", 1);
        expect_text("script", t, "var a", 0);
        expect_text("script", t, "color:red", 0);
        if (t) free_nstr(&t);
    }
    /* whitespace runs collapse to a single space; no leading/trailing space */
    {
        const char* html = "<p>  a   b\n\t c  </p>";
        N_STR* t = n_html_to_text(html, strlen(html));
        if (!t || !t->data || strcmp(t->data, "a b c") != 0) {
            n_log(LOG_ERR, "to_text whitespace: got '%s'", (t && t->data) ? t->data : "(null)");
            failures++;
        }
        if (t) free_nstr(&t);
    }
    /* comments are skipped */
    {
        const char* html = "<p>a<!-- secret comment -->b</p>";
        N_STR* t = n_html_to_text(html, strlen(html));
        expect_text("comment", t, "secret", 0);
        expect_text("comment", t, "ab", 1);
        if (t) free_nstr(&t);
    }
    /* NULL input returns NULL; empty input returns an empty N_STR */
    {
        N_STR* a = n_html_to_text(NULL, 0);
        N_STR* b = n_html_to_text("", 0);
        if (a != NULL) failures++;
        if (!b || !b->data || b->data[0] != '\0') failures++;
        if (b) free_nstr(&b);
    }
}

static size_t list_count(LIST* l) {
    size_t n = 0;
    if (!l) return 0;
    list_foreach(node, l) {
        (void)node;
        n++;
    }
    return n;
}

static void test_js_urls(void) {
    const char* js =
        "const API = '/api/v1';\n"
        "fetch(\"/api/v1/users?id=1\").then(r => r.json());\n"
        "axios.get('https://api.example.com/orders');\n"
        "const u = `/api/v1/item/${id}/detail`;\n"
        "xhr.open('GET', '//cdn.example.com/data.json');\n"
        "const rel = './partials/menu.html';\n"
        "const mime = 'text/html';\n"       /* no leading slash: not a URL */
        "const re = '/^\\\\d+$/';\n"        /* regex in a string: backslash rejects it */
        "const cls = 'btn btn-primary';\n"; /* spaces: rejected */
    LIST* urls = n_html_extract_js_urls(js, strlen(js));
    if (!urls) {
        n_log(LOG_ERR, "extract_js_urls returned NULL");
        failures++;
        return;
    }
    expect_link_present(urls, "/api/v1");
    expect_link_present(urls, "/api/v1/users?id=1");
    expect_link_present(urls, "https://api.example.com/orders");
    expect_link_present(urls, "/api/v1/item/"); /* ${id} template truncated */
    expect_link_present(urls, "//cdn.example.com/data.json");
    expect_link_present(urls, "./partials/menu.html");
    expect_link_absent(urls, "text/html");
    expect_link_absent(urls, "btn btn-primary");
    n_html_links_free(&urls);

    /* NULL / empty inputs are safe */
    {
        LIST* a = n_html_extract_js_urls(NULL, 0);
        LIST* b = n_html_extract_js_urls("", 0);
        if (!a || list_count(a) != 0) failures++;
        if (!b || list_count(b) != 0) failures++;
        n_html_links_free(&a);
        n_html_links_free(&b);
    }
}

static void test_scripts(void) {
    const char* html =
        "<html><head>\n"
        "<script src='/js/app.js'></script>\n" /* external: skipped */
        "<script>const a = '/inline/one';</script>\n"
        "<SCRIPT type='text/javascript'>fetch('/inline/two');</SCRIPT>\n"
        "</head><body>text</body></html>";
    LIST* scripts = n_html_extract_scripts(html, strlen(html));
    LIST* mined;
    N_STR* joined;
    if (!scripts) {
        n_log(LOG_ERR, "extract_scripts returned NULL");
        failures++;
        return;
    }
    if (list_count(scripts) != 2) { /* two inline bodies, the external one skipped */
        n_log(LOG_ERR, "expected 2 inline scripts, got %zu", list_count(scripts));
        failures++;
    }
    /* the inline URLs are mineable from the extracted bodies */
    joined = new_nstr(256);
    list_foreach(node, scripts) {
        N_STR* s = (N_STR*)node->ptr;
        if (s && s->data) nstrprintf_cat(joined, "%s\n", s->data);
    }
    mined = n_html_extract_js_urls(joined->data, joined->written);
    expect_link_present(mined, "/inline/one");
    expect_link_present(mined, "/inline/two");
    expect_link_absent(mined, "/js/app.js"); /* external body was not inlined here */
    n_html_links_free(&mined);
    free_nstr(&joined);
    n_html_links_free(&scripts);
}

int main(int argc, char** argv) {
    set_log_level(LOG_ERR);
    process_args(argc, argv);

    test_links();
    test_forms();
    test_sitemap();
    test_enctype();
    test_empty_and_null();
    test_to_text();
    test_js_urls();
    test_scripts();

    if (failures) {
        n_log(LOG_ERR, "ex_html: %d failure(s)", failures);
        return 1;
    }
    n_log(LOG_NOTICE, "ex_html: all checks passed");
    return 0;
}
