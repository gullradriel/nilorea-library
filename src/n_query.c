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
 *@file n_query.c
 *@brief Implementation of the small boolean field-query language.
 */

#include "nilorea/n_query.h"
#include "nilorea/n_log.h"
#include "nilorea/n_pcre.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* comparison operators */
enum { OP_EQ,
       OP_NE,
       OP_CONT,
       OP_NCONT,
       OP_REGEX,
       OP_GT,
       OP_LT,
       OP_GE,
       OP_LE };

/* AST node kinds */
enum { N_AND,
       N_OR,
       N_NOT,
       N_CMP };

/* one AST node */
typedef struct QNODE {
    int kind;
    struct QNODE* a; /* AND/OR left, NOT child */
    struct QNODE* b; /* AND/OR right */
    char* field;     /* CMP: field name */
    int op;          /* CMP: operator */
    char* value;     /* CMP: comparison value */
    N_PCRE* re;      /* CMP: compiled regex for OP_REGEX, else NULL */
} QNODE;

struct N_QUERY {
    QNODE* root; /* NULL = match all */
};

/* token kinds */
enum { TK_END,
       TK_WORD,
       TK_AND,
       TK_OR,
       TK_NOT,
       TK_LP,
       TK_RP,
       TK_OP };

/* parser/lexer state */
typedef struct {
    const char* p; /* cursor into the source */
    int tok;       /* current token kind */
    char* tval;    /* current token text (heap), for TK_WORD/keywords */
    int top;       /* current operator (when tok == TK_OP) */
    char err[160]; /* error message on failure */
} PARSER;

/* heap copy of the first n bytes of s as a NUL-terminated string */
static char* dupn(const char* s, size_t n) {
    char* out = malloc(n + 1);
    if (!out)
        return NULL;
    if (n)
        memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static int is_op_char(char c) {
    return c == '=' || c == '!' || c == '~' || c == '>' || c == '<';
}

/* case-insensitive substring test */
static int ci_contains(const char* hay, const char* needle) {
    size_t nl = strlen(needle);
    if (nl == 0)
        return 1;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl)
            return 1;
    }
    return 0;
}

/* advance to the next token */
static void lex_next(PARSER* P) {
    const char* s = P->p;
    if (P->tval) {
        free(P->tval);
        P->tval = NULL;
    }
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    if (*s == '\0') {
        P->tok = TK_END;
        P->p = s;
        return;
    }
    if (*s == '(') {
        P->tok = TK_LP;
        P->p = s + 1;
        return;
    }
    if (*s == ')') {
        P->tok = TK_RP;
        P->p = s + 1;
        return;
    }
    /* operators, longest match first */
    if (s[0] == '>' && s[1] == '=') {
        P->tok = TK_OP;
        P->top = OP_GE;
        P->p = s + 2;
        return;
    }
    if (s[0] == '<' && s[1] == '=') {
        P->tok = TK_OP;
        P->top = OP_LE;
        P->p = s + 2;
        return;
    }
    if (s[0] == '=' && s[1] == '~') {
        P->tok = TK_OP;
        P->top = OP_REGEX;
        P->p = s + 2;
        return;
    }
    if (s[0] == '!' && s[1] == '=') {
        P->tok = TK_OP;
        P->top = OP_NE;
        P->p = s + 2;
        return;
    }
    if (s[0] == '!' && s[1] == '~') {
        P->tok = TK_OP;
        P->top = OP_NCONT;
        P->p = s + 2;
        return;
    }
    if (s[0] == '=') {
        P->tok = TK_OP;
        P->top = OP_EQ;
        P->p = s + 1;
        return;
    }
    if (s[0] == '~') {
        P->tok = TK_OP;
        P->top = OP_CONT;
        P->p = s + 1;
        return;
    }
    if (s[0] == '>') {
        P->tok = TK_OP;
        P->top = OP_GT;
        P->p = s + 1;
        return;
    }
    if (s[0] == '<') {
        P->tok = TK_OP;
        P->top = OP_LT;
        P->p = s + 1;
        return;
    }
    if (*s == '"') {
        const char* start = s + 1;
        const char* e = start;
        while (*e && *e != '"')
            e++;
        P->tval = dupn(start, (size_t)(e - start));
        P->tok = TK_WORD;
        P->p = (*e == '"') ? e + 1 : e;
        return;
    }
    /* bareword: a run of non-space, non-paren, non-operator, non-quote bytes */
    {
        const char* start = s;
        while (*s && !isspace((unsigned char)*s) && *s != '(' && *s != ')' && *s != '"' && !is_op_char(*s))
            s++;
        P->tval = dupn(start, (size_t)(s - start));
        P->p = s;
        if (P->tval && strcasecmp(P->tval, "and") == 0)
            P->tok = TK_AND;
        else if (P->tval && strcasecmp(P->tval, "or") == 0)
            P->tok = TK_OR;
        else if (P->tval && strcasecmp(P->tval, "not") == 0)
            P->tok = TK_NOT;
        else
            P->tok = TK_WORD;
    }
}

static QNODE* node_new(int kind) {
    QNODE* n = calloc(1, sizeof(QNODE));
    if (n)
        n->kind = kind;
    return n;
}

static void node_free(QNODE* n) {
    if (!n)
        return;
    node_free(n->a);
    node_free(n->b);
    free(n->field);
    free(n->value);
    if (n->re)
        npcre_delete(&n->re);
    free(n);
}

static QNODE* parse_or(PARSER* P); /* forward */

static QNODE* parse_primary(PARSER* P) {
    if (P->tok == TK_LP) {
        QNODE* inner;
        lex_next(P);
        inner = parse_or(P);
        if (!inner)
            return NULL;
        if (P->tok != TK_RP) {
            snprintf(P->err, sizeof(P->err), "expected ')'");
            node_free(inner);
            return NULL;
        }
        lex_next(P);
        return inner;
    }
    if (P->tok != TK_WORD) {
        snprintf(P->err, sizeof(P->err), "expected a field name");
        return NULL;
    }
    {
        char* field = P->tval ? dupn(P->tval, strlen(P->tval)) : NULL;
        int op;
        char* val;
        QNODE* n;
        lex_next(P);
        if (P->tok != TK_OP) {
            snprintf(P->err, sizeof(P->err), "expected an operator after field '%s'", field ? field : "");
            free(field);
            return NULL;
        }
        op = P->top;
        lex_next(P);
        if (P->tok != TK_WORD && P->tok != TK_AND && P->tok != TK_OR && P->tok != TK_NOT) {
            snprintf(P->err, sizeof(P->err), "expected a value");
            free(field);
            return NULL;
        }
        val = P->tval ? dupn(P->tval, strlen(P->tval)) : dupn("", 0);
        lex_next(P);
        n = node_new(N_CMP);
        if (!n || !field || !val) {
            free(field);
            free(val);
            node_free(n);
            return NULL;
        }
        n->field = field;
        n->op = op;
        n->value = val;
        if (op == OP_REGEX) {
            n->re = npcre_new(val, 0);
            if (!n->re) {
                snprintf(P->err, sizeof(P->err), "invalid regex '%s'", val);
                node_free(n);
                return NULL;
            }
        }
        return n;
    }
}

static QNODE* parse_not(PARSER* P) {
    if (P->tok == TK_NOT) {
        QNODE* child;
        QNODE* n;
        lex_next(P);
        child = parse_not(P);
        if (!child)
            return NULL;
        n = node_new(N_NOT);
        if (!n) {
            node_free(child);
            return NULL;
        }
        n->a = child;
        return n;
    }
    return parse_primary(P);
}

static QNODE* parse_and(PARSER* P) {
    QNODE* left = parse_not(P);
    if (!left)
        return NULL;
    while (P->tok == TK_AND) {
        QNODE* right;
        QNODE* n;
        lex_next(P);
        right = parse_not(P);
        if (!right) {
            node_free(left);
            return NULL;
        }
        n = node_new(N_AND);
        if (!n) {
            node_free(left);
            node_free(right);
            return NULL;
        }
        n->a = left;
        n->b = right;
        left = n;
    }
    return left;
}

static QNODE* parse_or(PARSER* P) {
    QNODE* left = parse_and(P);
    if (!left)
        return NULL;
    while (P->tok == TK_OR) {
        QNODE* right;
        QNODE* n;
        lex_next(P);
        right = parse_and(P);
        if (!right) {
            node_free(left);
            return NULL;
        }
        n = node_new(N_OR);
        if (!n) {
            node_free(left);
            node_free(right);
            return NULL;
        }
        n->a = left;
        n->b = right;
        left = n;
    }
    return left;
}

N_QUERY* n_query_compile(const char* expr, char* errbuf, size_t errlen) {
    PARSER P;
    N_QUERY* q;
    QNODE* root;
    memset(&P, 0, sizeof(P));
    P.p = expr ? expr : "";
    lex_next(&P);
    if (P.tok == TK_END) {
        /* empty expression: a match-all query */
        if (P.tval)
            free(P.tval);
        q = calloc(1, sizeof(N_QUERY));
        return q;
    }
    root = parse_or(&P);
    if (!root) {
        if (errbuf && errlen)
            snprintf(errbuf, errlen, "%s", P.err[0] ? P.err : "parse error");
        if (P.tval)
            free(P.tval);
        return NULL;
    }
    if (P.tok != TK_END) {
        if (errbuf && errlen)
            snprintf(errbuf, errlen, "unexpected trailing input");
        node_free(root);
        if (P.tval)
            free(P.tval);
        return NULL;
    }
    if (P.tval)
        free(P.tval);
    q = calloc(1, sizeof(N_QUERY));
    if (!q) {
        node_free(root);
        return NULL;
    }
    q->root = root;
    return q;
}

static int eval_node(const QNODE* n, N_QUERY_GET get, void* ud) {
    if (!n)
        return 1;
    switch (n->kind) {
        case N_AND:
            return eval_node(n->a, get, ud) && eval_node(n->b, get, ud);
        case N_OR:
            return eval_node(n->a, get, ud) || eval_node(n->b, get, ud);
        case N_NOT:
            return !eval_node(n->a, get, ud);
        case N_CMP: {
            const char* v = get ? get(n->field, ud) : NULL;
            if (!v)
                v = "";
            switch (n->op) {
                case OP_EQ:
                    return strcasecmp(v, n->value) == 0;
                case OP_NE:
                    return strcasecmp(v, n->value) != 0;
                case OP_CONT:
                    return ci_contains(v, n->value);
                case OP_NCONT:
                    return !ci_contains(v, n->value);
                case OP_REGEX:
                    return (n->re && npcre_match((char*)v, n->re) == TRUE) ? 1 : 0;
                case OP_GT:
                case OP_LT:
                case OP_GE:
                case OP_LE: {
                    char* e1 = NULL;
                    char* e2 = NULL;
                    double a = strtod(v, &e1);
                    double b = strtod(n->value, &e2);
                    if (e1 == v || e2 == n->value)
                        return 0; /* a non-numeric side never matches */
                    if (n->op == OP_GT)
                        return a > b;
                    if (n->op == OP_LT)
                        return a < b;
                    if (n->op == OP_GE)
                        return a >= b;
                    return a <= b;
                }
                default:
                    return 0;
            }
        }
        default:
            return 0;
    }
}

int n_query_eval(const N_QUERY* query, N_QUERY_GET get, void* user_data) {
    if (!query)
        return 1;
    return eval_node(query->root, get, user_data) ? 1 : 0;
}

void n_query_free(N_QUERY** query) {
    if (!query || !*query)
        return;
    node_free((*query)->root);
    free(*query);
    *query = NULL;
}
