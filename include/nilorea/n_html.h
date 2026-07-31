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
 *@file n_html.h
 *@brief Lightweight HTML/XML extraction: links, forms, and sitemap URLs
 *@author Castagnier Mickael
 *@version 1.0
 *@date 2026
 *
 * A small, dependency-free tag scanner (not a full HTML parser) that pulls the
 * link, form, and sitemap surface out of a document for a web crawler. It is
 * tolerant of malformed markup and never executes anything.
 */

#ifndef __NILOREA_HTML_HEADER
#define __NILOREA_HTML_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_list.h"
#include "nilorea/n_str.h"

#include <stddef.h>

/*! a single form field (input, select, textarea, button) */
typedef struct N_FORM_FIELD {
    char* name;   /*!< field name attribute, or "" */
    char* type;   /*!< field type (lowercased), default "text" */
    char* value;  /*!< default value attribute, or "" */
    int required; /*!< 1 when the required attribute is present */
} N_FORM_FIELD;

/*! a parsed HTML form with its fields */
typedef struct N_HTML_FORM {
    char* method;  /*!< upper-case method, default "GET" */
    char* action;  /*!< action attribute (may be "" for self) */
    char* enctype; /*!< lower-case enctype, default "application/x-www-form-urlencoded" */
    LIST* fields;  /*!< list of N_FORM_FIELD* */
} N_HTML_FORM;

/*! @brief extract link URLs (a/href, form/action, img/src, script/src, link/href, iframe/src) as a LIST of N_STR*; free with n_html_links_free */
LIST* n_html_extract_links(const char* html, size_t len);
/*! @brief extract forms as a LIST of N_HTML_FORM*; free with n_html_forms_free */
LIST* n_html_extract_forms(const char* html, size_t len);
/*! @brief extract \<loc\> URLs from a sitemap.xml as a LIST of N_STR*; free with n_html_links_free */
LIST* n_sitemap_extract_urls(const char* xml, size_t len);
/*! @brief extract URL/path tokens from JavaScript source (quoted string literals that look like an http(s) URL, a protocol-relative "//host" URL, or a "/" / "./" / "../" path; a "${...}" template is truncated to its static prefix), deduped, as a LIST of N_STR*; free with n_html_links_free. Recovers fetch()/XHR/route endpoints a static link scan misses. */
LIST* n_html_extract_js_urls(const char* js, size_t len);
/*! @brief extract the inline \<script\> bodies (those without a src attribute) from an HTML document as a LIST of N_STR*; free with n_html_links_free. Feed each to n_html_extract_js_urls to mine inline endpoints. */
LIST* n_html_extract_scripts(const char* html, size_t len);
/*! @brief render HTML to readable plain text: drop tags, skip script/style, decode common entities, collapse whitespace, and line-break block elements; returns a new N_STR (free with free_nstr) or NULL */
N_STR* n_html_to_text(const char* html, size_t len);
/*! @brief free a list returned by n_html_extract_links or n_sitemap_extract_urls */
void n_html_links_free(LIST** links);
/*! @brief free a list returned by n_html_extract_forms */
void n_html_forms_free(LIST** forms);

#ifdef __cplusplus
}
#endif

#endif /* __NILOREA_HTML_HEADER */
