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
 *@file n_pretty.c
 *@brief Lexical pretty-printers for JSON, XML/HTML and JavaScript text
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/07/2026
 */

#include "nilorea/n_pretty.h"
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"

#include <ctype.h>
#include <string.h>

#ifdef HAVE_CJSON
#include "cJSON.h"
#endif

/*! growing output buffer fed by write_and_fit_ex */
typedef struct PRETTY_OUT {
    /*! buffer, reallocated on growth */
    char* buf;
    /*! total allocation in bytes */
    NSTRBYTE size;
    /*! meaningful bytes, excluding the terminating '\0' */
    NSTRBYTE written;
} PRETTY_OUT;

/*! growth headroom so consecutive short appends share one realloc */
#define PRETTY_OUT_PADDING 256
/*! spaces per indentation level */
#define PRETTY_INDENT_STEP 2
/*! longest element name kept for void/raw-text lookups */
#define PRETTY_TAG_NAME_MAX 31

/*! append n bytes of s to the output, TRUE on success */
static int _out_mem(PRETTY_OUT* o, const char* s, size_t n) {
    if (n == 0) return TRUE;
    return write_and_fit_ex(&o->buf, &o->size, &o->written, s, (NSTRBYTE)n, PRETTY_OUT_PADDING);
}

/*! append a single character to the output, TRUE on success */
static int _out_ch(PRETTY_OUT* o, char c) {
    return _out_mem(o, &c, 1);
}

/*! append depth * PRETTY_INDENT_STEP spaces, TRUE on success */
static int _out_indent(PRETTY_OUT* o, int depth) {
    for (int it = 0; it < depth; it++) {
        if (!_out_mem(o, "  ", PRETTY_INDENT_STEP)) return FALSE;
    }
    return TRUE;
}

/*! hand the buffer over to a new N_STR (no copy); the PRETTY_OUT is emptied */
static N_STR* _out_finish(PRETTY_OUT* o) {
    N_STR* out = NULL;
    if (!o->buf) return NULL;
    out = char_to_nstr_nocopy(o->buf);
    if (!out) {
        Free(o->buf);
    }
    o->buf = NULL;
    o->size = 0;
    o->written = 0;
    return out;
}

N_STR* n_pretty_json(const char* text) {
    __n_assert(text, return NULL);
#ifdef HAVE_CJSON
    const char* p = text;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '{' && *p != '[') return NULL;
    cJSON* json = cJSON_Parse(text);
    if (!json) return NULL;
    char* printed = cJSON_Print(json);
    cJSON_Delete(json);
    if (!printed) return NULL;
    N_STR* out = char_to_nstr_nocopy(printed);
    if (!out) free(printed);
    return out;
#else
    n_log(LOG_ERR, "n_pretty_json: built without cJSON support (HAVE_CJSON)");
    return NULL;
#endif
}

/*! emit the pending indent of a lenient JSON line, TRUE on success */
static int _jsonl_lead(PRETTY_OUT* o, int depth, int* at_line_start) {
    if (!*at_line_start) return TRUE;
    *at_line_start = 0;
    return _out_indent(o, depth);
}

/*! end the current lenient JSON line unless it is still empty, TRUE on success */
static int _jsonl_newline(PRETTY_OUT* o, int* at_line_start) {
    if (*at_line_start) return TRUE;
    *at_line_start = 1;
    return _out_ch(o, '\n');
}

N_STR* n_pretty_json_lenient(const char* text) {
    PRETTY_OUT o;
    const char* p = text;
    int depth = 0;
    int at_line_start = 1;
    int ok = TRUE;

    __n_assert(text, return NULL);
    if (!text[0]) return NULL;
    memset(&o, 0, sizeof(o));

    while (*p && ok) {
        char c = *p;
        if (isspace((unsigned char)c)) {
            p++;
            continue;
        }
        if (c == '"') {
            /* copied verbatim, so a brace or a comma inside a string cannot
               break the line; an unterminated one runs to the end of input */
            size_t n = 1;
            while (p[n]) {
                if (p[n] == '\\' && p[n + 1]) {
                    n += 2;
                    continue;
                }
                if (p[n] == '"') {
                    n++;
                    break;
                }
                n++;
            }
            ok = _jsonl_lead(&o, depth, &at_line_start) && _out_mem(&o, p, n);
            p += n;
            continue;
        }
        if (c == '{' || c == '[') {
            char close = (c == '{') ? '}' : ']';
            const char* q = p + 1;
            while (*q && isspace((unsigned char)*q)) q++;
            ok = _jsonl_lead(&o, depth, &at_line_start) && _out_ch(&o, c);
            if (*q == close) {
                /* an empty container reads better kept on its line */
                ok = ok && _out_ch(&o, close);
                p = q + 1;
                continue;
            }
            depth++;
            ok = ok && _jsonl_newline(&o, &at_line_start);
            p++;
            continue;
        }
        if (c == '}' || c == ']') {
            if (depth > 0) depth--;
            ok = _jsonl_newline(&o, &at_line_start) &&
                 _jsonl_lead(&o, depth, &at_line_start) && _out_ch(&o, c);
            p++;
            continue;
        }
        if (c == ',') {
            ok = _jsonl_lead(&o, depth, &at_line_start) && _out_ch(&o, c) &&
                 _jsonl_newline(&o, &at_line_start);
            p++;
            continue;
        }
        if (c == ':') {
            ok = _jsonl_lead(&o, depth, &at_line_start) && _out_mem(&o, ": ", 2);
            p++;
            continue;
        }
        {
            /* numbers, true/false/null, and whatever else a malformed or
               truncated document left behind: passed through as one token */
            size_t n = 0;
            while (p[n] && !strchr("\"{}[],:", p[n]) && !isspace((unsigned char)p[n])) n++;
            if (n == 0) n = 1;
            ok = _jsonl_lead(&o, depth, &at_line_start) && _out_mem(&o, p, n);
            p += n;
        }
    }
    if (ok) ok = _jsonl_newline(&o, &at_line_start);
    if (!ok) {
        Free(o.buf);
        return NULL;
    }
    return _out_finish(&o);
}

/*! HTML elements that never have content or a closing tag */
static const char* PRETTY_VOID_ELEMENTS[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", NULL};

/*! HTML elements whose content is raw text (no nested markup) */
static const char* PRETTY_RAWTEXT_ELEMENTS[] = {"script", "style", NULL};

/*! whether lowercase element name is in a NULL-terminated list */
static int _name_in_list(const char* name, const char** list) {
    for (size_t it = 0; list[it]; it++) {
        if (strcmp(name, list[it]) == 0) return TRUE;
    }
    return FALSE;
}

/*! extract the lowercased element name from a tag starting at p ("<name" or "</name") */
static void _tag_name(const char* p, char* name, size_t name_size) {
    size_t out = 0;
    p++;
    if (*p == '/') p++;
    while (*p && out + 1 < name_size &&
           (isalnum((unsigned char)*p) || *p == ':' || *p == '-' || *p == '_')) {
        name[out++] = (char)tolower((unsigned char)*p);
        p++;
    }
    name[out] = '\0';
}

/*! find the '>' closing a tag starting at p, honoring quoted attribute
 *  values; returns NULL when the tag never closes */
static const char* _find_tag_end(const char* p) {
    char quote = 0;
    p++;
    while (*p) {
        if (quote) {
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
        p++;
    }
    return NULL;
}

/*! case-insensitive search for needle in haystack */
static const char* _stristr(const char* haystack, const char* needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (const char* p = haystack; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) return p;
    }
    return NULL;
}

/*! emit one indented line holding n raw bytes of s */
static int _emit_line(PRETTY_OUT* o, int depth, const char* s, size_t n) {
    if (!_out_indent(o, depth)) return FALSE;
    if (!_out_mem(o, s, n)) return FALSE;
    return _out_ch(o, '\n');
}

/*! emit s trimmed of leading/trailing whitespace as one indented line;
 *  empty-after-trim runs are skipped */
static int _emit_trimmed(PRETTY_OUT* o, int depth, const char* s, size_t n) {
    size_t a = 0;
    size_t b = n;
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    if (b <= a) return TRUE;
    return _emit_line(o, depth, s + a, b - a);
}

N_STR* n_pretty_xml(const char* text) {
    __n_assert(text, return NULL);
    const char* p = text;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '<') return NULL;

    PRETTY_OUT o = {NULL, 0, 0};
    int depth = 0;
    int ok = TRUE;

    while (*p && ok) {
        if (*p != '<') {
            /* text run up to the next tag */
            const char* e = strchr(p, '<');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            ok = _emit_trimmed(&o, depth, p, n);
            p += n;
            continue;
        }
        /* comment "<!--" (p[0] == '<' already checked above); explicit byte
         * tests instead of strncmp so GCC can prove p + 4 stays in bounds */
        if (p[1] == '!' && p[2] == '-' && p[3] == '-') {
            const char* e = strstr(p + 4, "-->");
            size_t n = e ? (size_t)(e + 3 - p) : strlen(p);
            ok = _emit_line(&o, depth, p, n);
            p += n;
            continue;
        }
        if (strncasecmp(p, "<![CDATA[", 9) == 0) {
            const char* e = strstr(p + 9, "]]>");
            size_t n = e ? (size_t)(e + 3 - p) : strlen(p);
            ok = _emit_line(&o, depth, p, n);
            p += n;
            continue;
        }
        if (p[1] == '?' || p[1] == '!') {
            /* processing instruction or declaration (<?xml ...?>, <!DOCTYPE ...>) */
            const char* e = _find_tag_end(p);
            size_t n = e ? (size_t)(e + 1 - p) : strlen(p);
            ok = _emit_line(&o, depth, p, n);
            p += n;
            continue;
        }
        /* regular tag: open, close, or self-closing */
        {
            const char* e = _find_tag_end(p);
            size_t n = e ? (size_t)(e + 1 - p) : strlen(p);
            int is_close = (p[1] == '/');
            int self_close = (e && n >= 2 && p[n - 2] == '/');
            char name[PRETTY_TAG_NAME_MAX + 1];
            _tag_name(p, name, sizeof(name));
            if (is_close && depth > 0) depth--;
            ok = _emit_line(&o, depth, p, n);
            p += n;
            if (!ok || is_close || self_close || !e) continue;
            if (_name_in_list(name, PRETTY_VOID_ELEMENTS)) continue;
            depth++;
            if (_name_in_list(name, PRETTY_RAWTEXT_ELEMENTS)) {
                /* raw-text content: copy verbatim up to the matching close tag */
                char closer[PRETTY_TAG_NAME_MAX + 4];
                snprintf(closer, sizeof(closer), "</%s", name);
                const char* end = _stristr(p, closer);
                size_t rn = end ? (size_t)(end - p) : strlen(p);
                ok = _emit_trimmed(&o, depth, p, rn);
                p += rn;
            }
        }
    }
    if (!ok || !o.buf) {
        Free(o.buf);
        return NULL;
    }
    return _out_finish(&o);
}

/*! whether a '/' may start a regex literal after last significant char c
 *  (start of input, after an operator, an opener, or a separator) */
static int _js_regex_possible(char c) {
    if (c == 0) return TRUE;
    return strchr("(,=:[!&|?;{}<>+-*%~^", c) != NULL;
}

/*! whether the n bytes at s are a keyword after which an expression (and
 *  therefore a regex literal) can start, e.g. return /re/ */
static int _js_regex_keyword(const char* s, size_t n) {
    static const char* kw[] = {"await", "case", "delete", "do", "else", "in",
                               "instanceof", "new", "of", "return", "throw",
                               "typeof", "void", "yield", NULL};
    for (size_t it = 0; kw[it]; it++) {
        if (strlen(kw[it]) == n && strncmp(s, kw[it], n) == 0) return TRUE;
    }
    return FALSE;
}

/*! JS emitter state threaded through the helpers */
typedef struct JS_STATE {
    /*! output buffer */
    PRETTY_OUT o;
    /*! current indentation depth */
    int indent;
    /*! 1 when nothing has been emitted on the current line yet */
    int at_line_start;
    /*! 1 when collapsed whitespace separates the previous and next token */
    int pending_space;
    /*! last significant code character emitted (0 = none yet) */
    char last_sig;
    /*! 1 when the last token was a keyword that can precede a regex literal */
    int kw_regex;
} JS_STATE;

/*! emit the indent or the collapsed space before a token, as appropriate */
static int _js_lead(JS_STATE* js) {
    if (js->at_line_start) {
        if (!_out_indent(&js->o, js->indent)) return FALSE;
        js->at_line_start = 0;
    } else if (js->pending_space) {
        if (!_out_ch(&js->o, ' ')) return FALSE;
    }
    js->pending_space = 0;
    return TRUE;
}

/*! emit a raw token of n bytes with leading indent/space handling */
static int _js_token(JS_STATE* js, const char* s, size_t n) {
    if (!_js_lead(js)) return FALSE;
    return _out_mem(&js->o, s, n);
}

/*! terminate the current line unless already at a line start */
static int _js_newline(JS_STATE* js) {
    if (js->at_line_start) return TRUE;
    js->at_line_start = 1;
    js->pending_space = 0;
    return _out_ch(&js->o, '\n');
}

/*! length of the string literal starting at s (quote to closing quote,
 *  escapes honored, stops at end of line or input on unterminated ones) */
static size_t _js_string_len(const char* s) {
    char quote = s[0];
    size_t it = 1;
    while (s[it]) {
        if (s[it] == '\\' && s[it + 1]) {
            it += 2;
            continue;
        }
        if (s[it] == quote) return it + 1;
        if (s[it] == '\n') break;
        it++;
    }
    return it;
}

/*! length of the template literal starting at s (backtick to backtick,
 *  escapes honored, ${...} interpolations skipped by brace counting) */
static size_t _js_template_len(const char* s) {
    size_t it = 1;
    while (s[it]) {
        if (s[it] == '\\' && s[it + 1]) {
            it += 2;
            continue;
        }
        if (s[it] == '`') return it + 1;
        if (s[it] == '$' && s[it + 1] == '{') {
            int braces = 1;
            it += 2;
            while (s[it] && braces > 0) {
                if (s[it] == '{') braces++;
                if (s[it] == '}') braces--;
                it++;
            }
            continue;
        }
        it++;
    }
    return it;
}

/*! length of the regex literal starting at s (slash to closing slash,
 *  escapes and character classes honored, trailing flags included) */
static size_t _js_regex_len(const char* s) {
    size_t it = 1;
    int in_class = 0;
    while (s[it]) {
        if (s[it] == '\\' && s[it + 1]) {
            it += 2;
            continue;
        }
        if (s[it] == '\n') return it;
        if (in_class) {
            if (s[it] == ']') in_class = 0;
        } else if (s[it] == '[') {
            in_class = 1;
        } else if (s[it] == '/') {
            it++;
            while (isalpha((unsigned char)s[it])) it++;
            return it;
        }
        it++;
    }
    return it;
}

N_STR* n_pretty_js(const char* text) {
    __n_assert(text, return NULL);
    if (!text[0]) return NULL;

    JS_STATE js = {{NULL, 0, 0}, 0, 1, 0, 0, 0};
    long paren = 0;
    const char* p = text;
    int ok = TRUE;

    while (*p && ok) {
        char c = *p;
        if (isspace((unsigned char)c)) {
            if (!js.at_line_start) js.pending_space = 1;
            p++;
            continue;
        }
        if (c == '/' && p[1] == '/') {
            const char* e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            ok = _js_token(&js, p, n) && _js_newline(&js);
            p += n;
            continue;
        }
        if (c == '/' && p[1] == '*') {
            const char* e = strstr(p + 2, "*/");
            size_t n = e ? (size_t)(e + 2 - p) : strlen(p);
            ok = _js_token(&js, p, n);
            js.pending_space = 1;
            p += n;
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t n = _js_string_len(p);
            ok = _js_token(&js, p, n);
            js.last_sig = c;
            js.kw_regex = 0;
            p += n;
            continue;
        }
        if (c == '`') {
            size_t n = _js_template_len(p);
            ok = _js_token(&js, p, n);
            /* the literal may span lines; whatever follows is mid-line */
            js.last_sig = c;
            js.kw_regex = 0;
            p += n;
            continue;
        }
        if (c == '/' && (js.kw_regex || _js_regex_possible(js.last_sig))) {
            size_t n = _js_regex_len(p);
            ok = _js_token(&js, p, n);
            js.last_sig = '/';
            js.kw_regex = 0;
            p += n;
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_' || c == '$') {
            /* whole identifier as one token so keywords are recognizable */
            size_t n = 1;
            while (p[n] && (isalnum((unsigned char)p[n]) || p[n] == '_' || p[n] == '$')) n++;
            ok = _js_token(&js, p, n);
            js.last_sig = p[n - 1];
            js.kw_regex = _js_regex_keyword(p, n);
            p += n;
            continue;
        }
        if (c == '{' && paren == 0) {
            ok = _js_token(&js, "{", 1) && _js_newline(&js);
            js.indent++;
            js.last_sig = c;
            js.kw_regex = 0;
            p++;
            continue;
        }
        if (c == '}' && paren == 0) {
            ok = _js_newline(&js);
            if (js.indent > 0) js.indent--;
            ok = ok && _js_token(&js, "}", 1);
            js.last_sig = c;
            js.kw_regex = 0;
            p++;
            /* keep a statement terminator on the same line as its brace */
            while (*p && isspace((unsigned char)*p) && *p != '\n') p++;
            if (*p == ';' || *p == ',') {
                ok = ok && _out_ch(&js.o, *p);
                js.last_sig = *p;
                p++;
            }
            ok = ok && _js_newline(&js);
            continue;
        }
        if (c == ';' && paren == 0) {
            ok = _js_token(&js, ";", 1) && _js_newline(&js);
            js.last_sig = c;
            js.kw_regex = 0;
            p++;
            continue;
        }
        if (c == '(' || c == '[') paren++;
        if ((c == ')' || c == ']') && paren > 0) paren--;
        ok = _js_token(&js, &c, 1);
        js.last_sig = c;
        js.kw_regex = 0;
        p++;
    }
    if (!ok || !js.o.buf) {
        Free(js.o.buf);
        return NULL;
    }
    return _out_finish(&js.o);
}
