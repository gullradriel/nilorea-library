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
 *@file n_html.c
 *@brief Lightweight HTML/XML extraction: links, forms, and sitemap URLs
 *@author Castagnier Mickael
 *@version 1.0
 */

#include "nilorea/n_html.h"

#include "nilorea/n_common.h"
#include "nilorea/n_list.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/*! attribute parsed off an HTML start tag (name lowercased, value raw) */
typedef struct N_HTML_ATTR_ {
    char* name;  /*!< lowercased attribute name */
    char* value; /*!< raw value, "" when the attribute has no '=' */
} N_HTML_ATTR_;

/*! a single tag picked out by the scanner */
typedef struct N_HTML_TAG_ {
    char* name;       /*!< lowercased tag name */
    int closing;      /*!< 1 if the tag started with '/' */
    int self_closing; /*!< 1 if the tag ended with '/>' */
    LIST* attrs;      /*!< attributes (only populated for open tags) */
} N_HTML_TAG_;

/* Free an internal N_HTML_ATTR_ stored in a LIST. */
static void _n_html_attr_free(void* p) {
    if (!p) return;
    N_HTML_ATTR_* a = (N_HTML_ATTR_*)p;
    FreeNoLog(a->name);
    FreeNoLog(a->value);
    Free(a);
}

/* Free an internal N_HTML_TAG_ stored in a LIST. */
static void _n_html_tag_free(void* p) {
    if (!p) return;
    N_HTML_TAG_* t = (N_HTML_TAG_*)p;
    FreeNoLog(t->name);
    if (t->attrs) list_destroy(&t->attrs);
    Free(t);
}

/* Free a public N_FORM_FIELD stored in a LIST. */
static void _n_form_field_free(void* p) {
    if (!p) return;
    N_FORM_FIELD* f = (N_FORM_FIELD*)p;
    FreeNoLog(f->name);
    FreeNoLog(f->type);
    FreeNoLog(f->value);
    Free(f);
}

/* Free a public N_HTML_FORM stored in a LIST. */
static void _n_html_form_free(void* p) {
    if (!p) return;
    N_HTML_FORM* fm = (N_HTML_FORM*)p;
    FreeNoLog(fm->method);
    FreeNoLog(fm->action);
    FreeNoLog(fm->enctype);
    if (fm->fields) list_destroy(&fm->fields);
    Free(fm);
}

/* Duplicate n bytes from s into a fresh, NUL-terminated, lowercased buffer. */
static char* _n_html_strndup_lower(const char* s, size_t n) {
    char* out = NULL;
    Malloc(out, char, n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        out[i] = (char)((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
    }
    out[n] = '\0';
    return out;
}

/* Duplicate n bytes from s into a fresh, NUL-terminated buffer (preserves case). */
static char* _n_html_strndup(const char* s, size_t n) {
    char* out = NULL;
    Malloc(out, char, n + 1);
    if (!out) return NULL;
    if (n > 0) memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

/* Duplicate s into a fresh upper-cased buffer (for the HTTP method). */
static char* _n_html_strdup_upper(const char* s) {
    if (!s) return _n_html_strndup("", 0);
    size_t n = strlen(s);
    char* out = NULL;
    Malloc(out, char, n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        out[i] = (char)((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c);
    }
    out[n] = '\0';
    return out;
}

/* Case-insensitive ASCII string equality. */
static int _n_html_ci_eq(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Predicate for a tag/attribute name character (very liberal). */
static int _n_html_is_name_char(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':' || c == '.';
}

/* Find an attribute on a parsed tag. Returns the (possibly empty) value or NULL when absent. */
static const char* _n_html_attr_lookup(const N_HTML_TAG_* tag, const char* name) {
    if (!tag || !tag->attrs) return NULL;
    list_foreach(node, tag->attrs) {
        const N_HTML_ATTR_* a = (const N_HTML_ATTR_*)node->ptr;
        if (a && a->name && _n_html_ci_eq(a->name, name)) {
            return a->value ? a->value : "";
        }
    }
    return NULL;
}

/* Parse one tag from *pos (which points just past '<') into out and advance *pos past '>'.
 * Tolerant of malformed input: when no tag name is recognized, advance past the next '>'. */
static void _n_html_parse_tag(const char* html, size_t len, size_t* pos, LIST* out) {
    size_t i = *pos;
    if (i >= len) return;

    int closing = 0;
    if (html[i] == '/') {
        closing = 1;
        i++;
    }

    size_t name_start = i;
    while (i < len && _n_html_is_name_char((unsigned char)html[i])) i++;
    if (i == name_start) {
        while (i < len && html[i] != '>') i++;
        if (i < len) i++;
        *pos = i;
        return;
    }
    char* name = _n_html_strndup_lower(html + name_start, i - name_start);
    if (!name) {
        while (i < len && html[i] != '>') i++;
        if (i < len) i++;
        *pos = i;
        return;
    }

    N_HTML_TAG_* tag = NULL;
    Malloc(tag, N_HTML_TAG_, 1);
    if (!tag) {
        Free(name);
        while (i < len && html[i] != '>') i++;
        if (i < len) i++;
        *pos = i;
        return;
    }
    tag->name = name;
    tag->closing = closing;
    tag->self_closing = 0;
    tag->attrs = new_generic_list(MAX_LIST_ITEMS);

    while (i < len && html[i] != '>') {
        while (i < len && isspace((unsigned char)html[i])) i++;
        if (i >= len) break;
        if (html[i] == '>') break;
        if (html[i] == '/') {
            tag->self_closing = 1;
            i++;
            continue;
        }

        size_t an_start = i;
        while (i < len && html[i] != '=' && html[i] != '>' && html[i] != '/' && !isspace((unsigned char)html[i])) i++;
        if (i == an_start) {
            i++;
            continue;
        }
        char* an = _n_html_strndup_lower(html + an_start, i - an_start);
        if (!an) break;

        size_t j = i;
        while (j < len && isspace((unsigned char)html[j])) j++;

        char* av = NULL;
        if (j < len && html[j] == '=') {
            j++;
            while (j < len && isspace((unsigned char)html[j])) j++;
            if (j < len && (html[j] == '"' || html[j] == '\'')) {
                char q = html[j];
                j++;
                size_t v_start = j;
                while (j < len && html[j] != q) j++;
                av = _n_html_strndup(html + v_start, j - v_start);
                if (j < len) j++;
            } else {
                size_t v_start = j;
                while (j < len && html[j] != '>' && !isspace((unsigned char)html[j])) j++;
                av = _n_html_strndup(html + v_start, j - v_start);
            }
            i = j;
        } else {
            av = _n_html_strndup("", 0);
        }
        if (!av) {
            FreeNoLog(an);
            break;
        }
        N_HTML_ATTR_* a = NULL;
        Malloc(a, N_HTML_ATTR_, 1);
        if (!a) {
            FreeNoLog(an);
            FreeNoLog(av);
            break;
        }
        a->name = an;
        a->value = av;
        list_push(tag->attrs, a, _n_html_attr_free);
    }
    if (i < len && html[i] == '>') i++;

    list_push(out, tag, _n_html_tag_free);
    *pos = i;
}

/* Skip the raw text content of an open <script> or <style> up to its matching close tag.
 * Returns the byte index of the '<' of the close tag, or len when none is found. */
static size_t _n_html_skip_rawtext(const char* html, size_t len, size_t start, const char* close_tag) {
    size_t clen = strlen(close_tag);
    size_t j = start;
    while (j + clen <= len) {
        if (html[j] == '<') {
            int eq = 1;
            for (size_t k = 0; k < clen; k++) {
                unsigned char a = (unsigned char)html[j + k];
                unsigned char b = (unsigned char)close_tag[k];
                if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
                if (a != b) {
                    eq = 0;
                    break;
                }
            }
            if (eq) return j;
        }
        j++;
    }
    return len;
}

/* Scan all tags in [html, html+len). Returns a LIST of N_HTML_TAG_* (destructor wired). */
static LIST* _n_html_scan_tags(const char* html, size_t len) {
    LIST* tags = new_generic_list(MAX_LIST_ITEMS);
    if (!tags) return NULL;

    size_t i = 0;
    while (i < len) {
        if (html[i] != '<') {
            i++;
            continue;
        }
        i++;
        if (i >= len) break;

        if (i + 2 < len && html[i] == '!' && html[i + 1] == '-' && html[i + 2] == '-') {
            i += 3;
            while (i + 2 < len && !(html[i] == '-' && html[i + 1] == '-' && html[i + 2] == '>')) i++;
            if (i + 2 < len)
                i += 3;
            else
                i = len;
            continue;
        }
        if (html[i] == '!') {
            if (i + 7 < len && memcmp(html + i, "![CDATA[", 8) == 0) {
                i += 8;
                while (i + 2 < len && !(html[i] == ']' && html[i + 1] == ']' && html[i + 2] == '>')) i++;
                if (i + 2 < len)
                    i += 3;
                else
                    i = len;
                continue;
            }
            while (i < len && html[i] != '>') i++;
            if (i < len) i++;
            continue;
        }
        if (html[i] == '?') {
            i++;
            while (i + 1 < len && !(html[i] == '?' && html[i + 1] == '>')) i++;
            if (i + 1 < len)
                i += 2;
            else
                i = len;
            continue;
        }

        _n_html_parse_tag(html, len, &i, tags);

        const LIST_NODE* tail = tags->end;
        if (tail && tail->ptr) {
            const N_HTML_TAG_* t = (const N_HTML_TAG_*)tail->ptr;
            if (!t->closing && !t->self_closing && t->name) {
                if (strcmp(t->name, "script") == 0) {
                    i = _n_html_skip_rawtext(html, len, i, "</script");
                } else if (strcmp(t->name, "style") == 0) {
                    i = _n_html_skip_rawtext(html, len, i, "</style");
                }
            }
        }
    }
    return tags;
}

/* Push a URL extracted off a tag attribute onto out as an N_STR*. */
static void _n_html_push_link(LIST* out, const char* url) {
    if (!url || !*url) return;
    N_STR* s = char_to_nstr(url);
    if (s) list_push(out, s, free_nstr_ptr);
}

LIST* n_html_extract_links(const char* html, size_t len) {
    LIST* out = new_generic_list(MAX_LIST_ITEMS);
    if (!out) return NULL;
    if (!html || len == 0) return out;

    LIST* tags = _n_html_scan_tags(html, len);
    if (!tags) return out;

    list_foreach(node, tags) {
        const N_HTML_TAG_* t = (const N_HTML_TAG_*)node->ptr;
        if (!t || !t->name || t->closing) continue;
        const char* attr = NULL;
        if (strcmp(t->name, "a") == 0)
            attr = _n_html_attr_lookup(t, "href");
        else if (strcmp(t->name, "area") == 0)
            attr = _n_html_attr_lookup(t, "href");
        else if (strcmp(t->name, "form") == 0)
            attr = _n_html_attr_lookup(t, "action");
        else if (strcmp(t->name, "img") == 0)
            attr = _n_html_attr_lookup(t, "src");
        else if (strcmp(t->name, "script") == 0)
            attr = _n_html_attr_lookup(t, "src");
        else if (strcmp(t->name, "link") == 0)
            attr = _n_html_attr_lookup(t, "href");
        else if (strcmp(t->name, "iframe") == 0)
            attr = _n_html_attr_lookup(t, "src");
        if (attr) _n_html_push_link(out, attr);
    }
    list_destroy(&tags);
    return out;
}

LIST* n_html_extract_forms(const char* html, size_t len) {
    LIST* out = new_generic_list(MAX_LIST_ITEMS);
    if (!out) return NULL;
    if (!html || len == 0) return out;

    LIST* tags = _n_html_scan_tags(html, len);
    if (!tags) return out;

    N_HTML_FORM* cur = NULL;
    list_foreach(node, tags) {
        const N_HTML_TAG_* t = (const N_HTML_TAG_*)node->ptr;
        if (!t || !t->name) continue;

        if (strcmp(t->name, "form") == 0) {
            if (t->closing) {
                if (cur) {
                    list_push(out, cur, _n_html_form_free);
                    cur = NULL;
                }
                continue;
            }
            if (cur) {
                /* unclosed previous form: flush and start fresh */
                list_push(out, cur, _n_html_form_free);
                cur = NULL;
            }
            Malloc(cur, N_HTML_FORM, 1);
            if (!cur) continue;
            const char* m = _n_html_attr_lookup(t, "method");
            const char* a = _n_html_attr_lookup(t, "action");
            const char* e = _n_html_attr_lookup(t, "enctype");
            cur->method = _n_html_strdup_upper(m ? m : "GET");
            cur->action = _n_html_strndup(a ? a : "", a ? strlen(a) : 0);
            cur->enctype = _n_html_strndup_lower(e ? e : "application/x-www-form-urlencoded", e ? strlen(e) : 33);
            cur->fields = new_generic_list(MAX_LIST_ITEMS);
            continue;
        }

        if (!cur) continue;
        if (t->closing) continue;

        int is_field = (strcmp(t->name, "input") == 0 || strcmp(t->name, "select") == 0 || strcmp(t->name, "textarea") == 0 || strcmp(t->name, "button") == 0);
        if (!is_field) continue;

        N_FORM_FIELD* f = NULL;
        Malloc(f, N_FORM_FIELD, 1);
        if (!f) continue;
        const char* n_attr = _n_html_attr_lookup(t, "name");
        const char* ty_attr = _n_html_attr_lookup(t, "type");
        const char* v_attr = _n_html_attr_lookup(t, "value");
        const char* req_attr = _n_html_attr_lookup(t, "required");
        if (!ty_attr || !*ty_attr) ty_attr = "text";
        f->name = _n_html_strndup(n_attr ? n_attr : "", n_attr ? strlen(n_attr) : 0);
        f->type = _n_html_strndup_lower(ty_attr, strlen(ty_attr));
        f->value = _n_html_strndup(v_attr ? v_attr : "", v_attr ? strlen(v_attr) : 0);
        f->required = (req_attr != NULL) ? 1 : 0;
        list_push(cur->fields, f, _n_form_field_free);
    }

    if (cur) list_push(out, cur, _n_html_form_free);
    list_destroy(&tags);
    return out;
}

LIST* n_sitemap_extract_urls(const char* xml, size_t len) {
    LIST* out = new_generic_list(MAX_LIST_ITEMS);
    if (!out) return NULL;
    if (!xml || len == 0) return out;

    size_t i = 0;
    while (i < len) {
        /* find next '<loc' open tag (case-insensitive) */
        const char* content = NULL;
        while (i + 5 <= len) {
            if (xml[i] == '<') {
                unsigned char c1 = (unsigned char)xml[i + 1];
                unsigned char c2 = (unsigned char)xml[i + 2];
                unsigned char c3 = (unsigned char)xml[i + 3];
                unsigned char c4 = (unsigned char)xml[i + 4];
                if ((c1 == 'l' || c1 == 'L') && (c2 == 'o' || c2 == 'O') && (c3 == 'c' || c3 == 'C') && (c4 == '>' || c4 == ' ' || c4 == '\t' || c4 == '\r' || c4 == '\n' || c4 == '/')) {
                    i += 4;
                    while (i < len && xml[i] != '>') i++;
                    if (i >= len) break;
                    i++;
                    content = xml + i;
                    break;
                }
            }
            i++;
        }
        if (!content) break;

        const char* close = NULL;
        while (i + 6 <= len) {
            if (xml[i] == '<' && xml[i + 1] == '/') {
                unsigned char c1 = (unsigned char)xml[i + 2];
                unsigned char c2 = (unsigned char)xml[i + 3];
                unsigned char c3 = (unsigned char)xml[i + 4];
                if ((c1 == 'l' || c1 == 'L') && (c2 == 'o' || c2 == 'O') && (c3 == 'c' || c3 == 'C') && (xml[i + 5] == '>' || xml[i + 5] == ' ' || xml[i + 5] == '\t' || xml[i + 5] == '\r' || xml[i + 5] == '\n')) {
                    close = xml + i;
                    break;
                }
            }
            i++;
        }
        if (!close) break;

        const char* s = content;
        const char* e = close;
        while (s < e && isspace((unsigned char)*s)) s++;
        while (e > s && isspace((unsigned char)*(e - 1))) e--;
        if (e > s) {
            size_t n = (size_t)(e - s);
            char* raw = _n_html_strndup(s, n);
            if (raw) {
                N_STR* nstr = char_to_nstr(raw);
                Free(raw);
                if (nstr) list_push(out, nstr, free_nstr_ptr);
            }
        }
        /* advance past the close-tag '>' */
        while (i < len && xml[i] != '>') i++;
        if (i < len) i++;
    }
    return out;
}

/* Lowercase one ASCII letter. */
static char _n_html_lc(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

/* Append a Unicode codepoint as UTF-8 (or ASCII) into buf, bounded by cap. */
static void _n_html_put_cp(char* buf, size_t cap, size_t* o, unsigned long cp) {
    if (cp == 0 || cp > 0x10FFFFUL) return;
    if (cp < 0x80UL) {
        if (*o + 1 <= cap) buf[(*o)++] = (char)cp;
    } else if (cp < 0x800UL) {
        if (*o + 2 <= cap) {
            buf[(*o)++] = (char)(0xC0UL | (cp >> 6));
            buf[(*o)++] = (char)(0x80UL | (cp & 0x3FUL));
        }
    } else if (cp < 0x10000UL) {
        if (*o + 3 <= cap) {
            buf[(*o)++] = (char)(0xE0UL | (cp >> 12));
            buf[(*o)++] = (char)(0x80UL | ((cp >> 6) & 0x3FUL));
            buf[(*o)++] = (char)(0x80UL | (cp & 0x3FUL));
        }
    } else {
        if (*o + 4 <= cap) {
            buf[(*o)++] = (char)(0xF0UL | (cp >> 18));
            buf[(*o)++] = (char)(0x80UL | ((cp >> 12) & 0x3FUL));
            buf[(*o)++] = (char)(0x80UL | ((cp >> 6) & 0x3FUL));
            buf[(*o)++] = (char)(0x80UL | (cp & 0x3FUL));
        }
    }
}

/* Append a NUL-terminated replacement string into buf, bounded by cap. */
static void _n_html_put_str(char* buf, size_t cap, size_t* o, const char* s) {
    while (*s && *o < cap) buf[(*o)++] = *s++;
}

/* Named HTML entities mapped to an ASCII-friendly replacement. The five XML
   entities decode exactly; the smart-punctuation set is folded to plain ASCII so
   the rendered text stays readable in a fixed font. */
static const struct {
    const char* name;
    const char* rep;
} _n_html_entities[] = {
    {"amp", "&"},
    {"lt", "<"},
    {"gt", ">"},
    {"quot", "\""},
    {"apos", "'"},
    {"nbsp", " "},
    {"copy", "(c)"},
    {"reg", "(R)"},
    {"trade", "(tm)"},
    {"mdash", "-"},
    {"ndash", "-"},
    {"minus", "-"},
    {"shy", ""},
    {"hellip", "..."},
    {"lsquo", "'"},
    {"rsquo", "'"},
    {"sbquo", "'"},
    {"ldquo", "\""},
    {"rdquo", "\""},
    {"bdquo", "\""},
    {"middot", "."},
    {"bull", "*"},
    {"deg", " deg "},
    {"laquo", "<<"},
    {"raquo", ">>"},
    {"times", "x"},
    {"divide", "/"},
    {"frac12", "1/2"},
    {"frac14", "1/4"},
    {"frac34", "3/4"},
    {"plusmn", "+/-"}};

/* Classify a tag name for line breaking: 0 none (inline), 1 a cell separator
   (a single space), 2 a line break, 3 a paragraph break (a blank line). */
static int _n_html_block_kind(const char* name) {
    static const char* para[] = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "table", "ul", "ol", "blockquote", "pre", "section", "article", "header", "footer", "nav", "aside", "main", "form", "fieldset", "dl", "figure", "figcaption", "address", "title", "body", NULL};
    static const char* line[] = {"br", "li", "tr", "dt", "dd", "option", "caption", "label", "legend", "thead", "tbody", "tfoot", NULL};
    int i;
    for (i = 0; para[i]; i++)
        if (strcmp(name, para[i]) == 0) return 3;
    for (i = 0; line[i]; i++)
        if (strcmp(name, line[i]) == 0) return 2;
    if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) return 1;
    return 0;
}

/* Emit any pending newlines (capped at 2, i.e. one blank line) or a pending
   space before the next visible character. */
static void _n_html_flush(char* buf, size_t cap, size_t* o, int* pending_nl, int* pending_sp, int* line_content) {
    if (*pending_nl > 0) {
        if (*o > 0) { /* suppress leading newlines at the document start */
            int n = (*pending_nl > 2) ? 2 : *pending_nl;
            int t;
            for (t = 0; t < n; t++)
                if (*o < cap) buf[(*o)++] = '\n';
        }
        *pending_nl = 0;
        *line_content = 0;
    } else if (*pending_sp && *line_content) {
        if (*o < cap) buf[(*o)++] = ' ';
    }
    *pending_sp = 0;
}

N_STR* n_html_to_text(const char* html, size_t len) {
    char* buf = NULL;
    size_t cap, o = 0, i = 0;
    int pending_nl = 0, pending_sp = 0, line_content = 0;
    N_STR* out = NULL;

    if (!html) return NULL;
    cap = len + 16;
    Malloc(buf, char, cap + 1);
    if (!buf) {
        n_log(LOG_ERR, "n_html_to_text: out of memory for %zu bytes", cap + 1);
        return NULL;
    }

    while (i < len) {
        char c = html[i];
        if (c == '<') {
            /* HTML comment: skip to the closing --> */
            if (i + 3 < len && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
                i += 4;
                while (i + 2 < len && !(html[i] == '-' && html[i + 1] == '-' && html[i + 2] == '>'))
                    i++;
                i = (i + 3 <= len) ? i + 3 : len;
                continue;
            }
            /* doctype or other declaration: skip to '>' */
            if (i + 1 < len && html[i + 1] == '!') {
                while (i < len && html[i] != '>') i++;
                if (i < len) i++;
                continue;
            }
            /* parse the tag name */
            {
                size_t j = i + 1;
                int closing = 0;
                char name[32];
                size_t nlen = 0;
                if (j < len && html[j] == '/') {
                    closing = 1;
                    j++;
                }
                while (j < len && nlen < sizeof(name) - 1) {
                    char tc = html[j];
                    if ((tc >= 'a' && tc <= 'z') || (tc >= 'A' && tc <= 'Z') || (tc >= '0' && tc <= '9')) {
                        name[nlen++] = _n_html_lc(tc);
                        j++;
                    } else {
                        break;
                    }
                }
                name[nlen] = '\0';
                /* script and style: drop the element content wholesale */
                int is_script = !closing && strcmp(name, "script") == 0;
                int is_style = !closing && strcmp(name, "style") == 0;
                if (is_script || is_style) {
                    const char* endtag = is_script ? "</script" : "</style";
                    size_t tlen = strlen(endtag);
                    while (j < len && html[j] != '>') j++;
                    if (j < len) j++;
                    while (j < len) {
                        if (j + tlen <= len) {
                            size_t k = 0;
                            while (k < tlen && _n_html_lc(html[j + k]) == endtag[k]) k++;
                            if (k == tlen) {
                                j += tlen;
                                while (j < len && html[j] != '>') j++;
                                if (j < len) j++;
                                break;
                            }
                        }
                        j++;
                    }
                    if (pending_nl < 2) pending_nl = 2;
                    i = j;
                    continue;
                }
                /* line/paragraph structure */
                {
                    int bk = _n_html_block_kind(name);
                    if (bk == 3) {
                        if (pending_nl < 2) pending_nl = 2;
                        pending_sp = 0;
                    } else if (bk == 2) {
                        if (pending_nl < 1) pending_nl = 1;
                        pending_sp = 0;
                    } else if (bk == 1) {
                        if (line_content) pending_sp = 1;
                    }
                }
                /* skip to the end of the tag */
                while (j < len && html[j] != '>') j++;
                if (j < len) j++;
                i = j;
                continue;
            }
        } else if (c == '&') {
            size_t j = i + 1;
            int handled = 0;
            if (j < len && html[j] == '#') {
                /* numeric character reference (decimal or hex) */
                unsigned long cp = 0;
                size_t start;
                int hex = 0, digits = 0;
                j++;
                if (j < len && (html[j] == 'x' || html[j] == 'X')) {
                    hex = 1;
                    j++;
                }
                start = j;
                while (j < len && html[j] != ';' && (j - start) < 8) {
                    char d = html[j];
                    if (hex) {
                        if (d >= '0' && d <= '9')
                            cp = cp * 16UL + (unsigned long)(d - '0');
                        else if (d >= 'a' && d <= 'f')
                            cp = cp * 16UL + (unsigned long)(d - 'a' + 10);
                        else if (d >= 'A' && d <= 'F')
                            cp = cp * 16UL + (unsigned long)(d - 'A' + 10);
                        else
                            break;
                    } else {
                        if (d >= '0' && d <= '9')
                            cp = cp * 10UL + (unsigned long)(d - '0');
                        else
                            break;
                    }
                    digits++;
                    j++;
                }
                if (digits > 0 && j < len && html[j] == ';') {
                    _n_html_flush(buf, cap, &o, &pending_nl, &pending_sp, &line_content);
                    _n_html_put_cp(buf, cap, &o, cp);
                    line_content = 1;
                    i = j + 1;
                    handled = 1;
                }
            } else {
                /* named character reference */
                char ename[16];
                size_t en = 0;
                size_t k = j;
                while (k < len && en < sizeof(ename) - 1) {
                    char d = html[k];
                    if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9')) {
                        ename[en++] = d;
                        k++;
                    } else {
                        break;
                    }
                }
                ename[en] = '\0';
                if (en > 0 && k < len && html[k] == ';') {
                    size_t e;
                    for (e = 0; e < sizeof(_n_html_entities) / sizeof(_n_html_entities[0]); e++) {
                        if (strcmp(ename, _n_html_entities[e].name) == 0) {
                            _n_html_flush(buf, cap, &o, &pending_nl, &pending_sp, &line_content);
                            _n_html_put_str(buf, cap, &o, _n_html_entities[e].rep);
                            line_content = 1;
                            i = k + 1;
                            handled = 1;
                            break;
                        }
                    }
                }
            }
            if (!handled) {
                _n_html_flush(buf, cap, &o, &pending_nl, &pending_sp, &line_content);
                if (o < cap) buf[o++] = '&';
                line_content = 1;
                i++;
            }
            continue;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            if (line_content) pending_sp = 1; /* collapse a whitespace run */
            i++;
            continue;
        } else {
            _n_html_flush(buf, cap, &o, &pending_nl, &pending_sp, &line_content);
            if (o < cap) buf[o++] = c;
            line_content = 1;
            i++;
            continue;
        }
    }

    buf[o] = '\0';
    char_to_nstr_ex(buf, o, &out);
    Free(buf);
    return out;
}

/* Portable substring search over a bounded buffer (no GNU memmem dependency).
   Returns a pointer to the first occurrence of needle in hay, or NULL. */
static const char* _n_html_find(const char* hay, size_t haylen, const char* needle, size_t nlen) {
    size_t i;
    if (!hay || !needle || nlen == 0 || nlen > haylen)
        return NULL;
    for (i = 0; i + nlen <= haylen; i++)
        if (memcmp(hay + i, needle, nlen) == 0)
            return hay + i;
    return NULL;
}

/* Portable case-insensitive substring search; needle must be given lowercase. */
static const char* _n_html_find_ci(const char* hay, size_t haylen, const char* needle, size_t nlen) {
    size_t i, j;
    if (!hay || !needle || nlen == 0 || nlen > haylen)
        return NULL;
    for (i = 0; i + nlen <= haylen; i++) {
        for (j = 0; j < nlen; j++)
            if (tolower((unsigned char)hay[i + j]) != needle[j])
                break;
        if (j == nlen)
            return hay + i;
    }
    return NULL;
}

/* Case-insensitive prefix test: does s (length n) begin with the lowercase pre? */
static int _n_html_starts_ci(const char* s, size_t n, const char* pre, size_t plen) {
    size_t i;
    if (n < plen)
        return 0;
    for (i = 0; i < plen; i++)
        if (tolower((unsigned char)s[i]) != pre[i])
            return 0;
    return 1;
}

/* True when a captured string literal looks like a URL or a site-relative path
   worth treating as an endpoint: an absolute http(s) URL, a protocol-relative
   "//host" URL, or a "/" / "./" / "../" path. Rejects tokens carrying characters
   that a URL does not (whitespace, quotes, angle/curly brackets, backslash, and
   regex metacharacters), which filters most non-URL string literals and inline
   regular expressions out. */
static int _n_html_js_url_like(const char* s, size_t n) {
    size_t i;
    int ok_prefix = 0;
    if (!s || n < 2)
        return 0;
    if (_n_html_starts_ci(s, n, "http://", 7) ||
        _n_html_starts_ci(s, n, "https://", 8) ||
        (s[0] == '/' && s[1] == '/') ||
        (s[0] == '/') ||
        (s[0] == '.' && s[1] == '/') ||
        (n >= 3 && s[0] == '.' && s[1] == '.' && s[2] == '/'))
        ok_prefix = 1;
    if (!ok_prefix)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c <= ' ' || c == '"' || c == '\'' || c == '`' || c == '<' || c == '>' ||
            c == '{' || c == '}' || c == '|' || c == '^' || c == '\\' || c == 127)
            return 0;
    }
    return 1;
}

/* Append a URL/path token (bounded by len) to out, deduped, when it looks like a
   URL. A "${" template placeholder truncates the token at its static prefix. */
static void _n_html_push_js_url(LIST* out, const char* tok, size_t len) {
    size_t use = len;
    size_t i;
    char* buf;
    const char* tmpl;
    if (!tok || len == 0)
        return;
    /* keep only the static prefix before a template placeholder */
    tmpl = _n_html_find(tok, len, "${", 2);
    if (tmpl)
        use = (size_t)(tmpl - tok);
    if (!_n_html_js_url_like(tok, use))
        return;
    list_foreach(node, out) { /* dedup */
        const N_STR* e = (const N_STR*)node->ptr;
        if (e && e->data && strlen(e->data) == use && strncmp(e->data, tok, use) == 0)
            return;
    }
    buf = malloc(use + 1);
    if (!buf)
        return;
    for (i = 0; i < use; i++)
        buf[i] = tok[i];
    buf[use] = '\0';
    _n_html_push_link(out, buf);
    free(buf);
}

LIST* n_html_extract_js_urls(const char* js, size_t len) {
    LIST* out = new_generic_list(MAX_LIST_ITEMS);
    size_t i = 0;
    if (!out) return NULL;
    if (!js || len == 0) return out;
    for (i = 0; i < len; i++) {
        char q = js[i];
        size_t start, j;
        if (q != '"' && q != '\'' && q != '`')
            continue;
        start = i + 1;
        for (j = start; j < len; j++) {
            if (js[j] == '\\') { /* skip an escaped char */
                j++;
                continue;
            }
            if (js[j] == q)
                break;
        }
        if (j <= len && j > start)
            _n_html_push_js_url(out, js + start, j - start);
        i = (j < len) ? j : len; /* resume after the closing quote */
    }
    return out;
}

LIST* n_html_extract_scripts(const char* html, size_t len) {
    LIST* out = new_generic_list(MAX_LIST_ITEMS);
    size_t i = 0;
    if (!out) return NULL;
    if (!html || len == 0) return out;
    while (i < len) {
        const char* open = _n_html_find_ci(html + i, len - i, "<script", 7);
        size_t tag_start, body_start, k;
        const char* close;
        N_STR* body;
        if (!open)
            break;
        tag_start = (size_t)(open - html);
        /* find the end of the opening tag */
        body_start = tag_start + 7;
        while (body_start < len && html[body_start] != '>')
            body_start++;
        if (body_start >= len)
            break;
        /* skip external scripts (a src attribute); their body is fetched separately */
        if (_n_html_find_ci(html + tag_start, body_start - tag_start, "src", 3) != NULL) {
            i = body_start + 1;
            continue;
        }
        body_start++; /* past '>' */
        close = _n_html_find_ci(html + body_start, len - body_start, "</script", 8);
        k = close ? (size_t)(close - html) : len;
        if (k > body_start) {
            body = NULL;
            char_to_nstr_ex(html + body_start, k - body_start, &body);
            if (body)
                list_push(out, body, free_nstr_ptr);
        }
        i = close ? k + 8 : len;
    }
    return out;
}

void n_html_links_free(LIST** links) {
    if (!links || !*links) return;
    list_destroy(links);
}

void n_html_forms_free(LIST** forms) {
    if (!forms || !*forms) return;
    list_destroy(forms);
}
