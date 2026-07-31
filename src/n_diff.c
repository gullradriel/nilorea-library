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
 *@file n_diff.c
 *@brief Line oriented text comparison (Myers shortest edit script)
 *@author Castagnier Mickael
 *@version 1.0
 *@date 31/07/2026
 */

#include "nilorea/n_diff.h"
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/*! growth headroom so consecutive short appends share one realloc */
#define DIFF_OUT_PADDING 256
/*! widest "@@ -a,b +c,d @@" header we can produce */
#define DIFF_HEADER_BUF 128

/*! a text split into lines, all pointing into one owned buffer */
typedef struct DIFF_TEXT {
    /*! copy of the input with every '\n' turned into '\0' */
    char* buf;
    /*! start of each line inside buf */
    char** line;
    /*! number of lines */
    size_t count;
} DIFF_TEXT;

/*! release a DIFF_TEXT, leaving it empty */
static void _text_free(DIFF_TEXT* t) {
    FreeNoLog(t->line);
    FreeNoLog(t->buf);
    t->count = 0;
}

/*! split text into lines. A trailing newline closes the last line instead of
 *  opening an empty one. TRUE on success, FALSE on allocation failure. */
static int _text_load(DIFF_TEXT* t, const char* text) {
    size_t len = 0, nb = 1, idx = 0;
    char* start = NULL;

    t->buf = NULL;
    t->line = NULL;
    t->count = 0;

    if (!text || !text[0]) return TRUE;

    len = strlen(text);
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') nb++;
    }
    if (text[len - 1] == '\n') nb--;
    if (nb == 0) return TRUE;

    Malloc(t->buf, char, len + 1);
    if (!t->buf) return FALSE;
    memcpy(t->buf, text, len + 1);

    Malloc(t->line, char*, nb);
    if (!t->line) {
        FreeNoLog(t->buf);
        return FALSE;
    }

    start = t->buf;
    for (size_t i = 0; i <= len && idx < nb; i++) {
        if (t->buf[i] == '\n' || t->buf[i] == '\0') {
            t->buf[i] = '\0';
            t->line[idx] = start;
            idx++;
            start = t->buf + i + 1;
        }
    }
    t->count = idx;
    return TRUE;
}

/*! destructor for the N_DIFF_LINE entries of N_DIFF_RESULT.lines */
static void _line_free(void* ptr) {
    N_DIFF_LINE* line = (N_DIFF_LINE*)ptr;
    if (!line) return;
    FreeNoLog(line->text);
    FreeNoLog(line);
}

/*! prepend one line to the script, which is built back to front. TRUE on
 *  success, FALSE on allocation failure. */
static int _emit(N_DIFF_RESULT* result, int op, const char* text, size_t line_a, size_t line_b) {
    N_DIFF_LINE* line = NULL;

    Malloc(line, N_DIFF_LINE, 1);
    if (!line) return FALSE;

    line->op = op;
    line->line_a = line_a;
    line->line_b = line_b;
    line->text = strdup(text ? text : "");
    if (!line->text) {
        FreeNoLog(line);
        return FALSE;
    }
    if (list_unshift(result->lines, line, _line_free) != TRUE) {
        _line_free(line);
        return FALSE;
    }

    if (op == N_DIFF_COMMON) {
        result->nb_common++;
    } else if (op == N_DIFF_DELETE) {
        result->nb_deleted++;
    } else {
        result->nb_inserted++;
    }
    return TRUE;
}

/*! Myers greedy forward pass with a stored trace, then a backtrack emitting
 *  the script in reverse. base_a / base_b are added to the emitted line
 *  numbers so a trimmed middle still reports positions in the whole text.
 *  Returns 1 when a script was emitted, 0 when the texts are further apart
 *  than max_edits (nothing emitted), -1 on allocation failure. */
static int _myers(N_DIFF_RESULT* result, char** a, int n, char** b, int m, size_t base_a, size_t base_b, int max_edits) {
    int* v = NULL;
    int** trace = NULL;
    int maxd = n + m;
    int voff = 0, found = -1, ret = -1;

    if (maxd > max_edits) maxd = max_edits;
    voff = maxd + 1;

    Malloc(v, int, (size_t)(2 * maxd + 3));
    if (!v) return -1;
    Malloc(trace, int*, (size_t)(maxd + 1));
    if (!trace) {
        FreeNoLog(v);
        return -1;
    }

    for (int d = 0; d <= maxd; d++) {
        int* snap = NULL;

        /* the backtrack reads the state as it was before this step */
        Malloc(snap, int, (size_t)(2 * d + 3));
        if (!snap) goto cleanup;
        for (int k = -d - 1; k <= d + 1; k++) {
            snap[k + d + 1] = v[k + voff];
        }
        trace[d] = snap;

        for (int k = -d; k <= d; k += 2) {
            int x = 0, y = 0;
            if (k == -d || (k != d && v[k - 1 + voff] < v[k + 1 + voff])) {
                /* move down: b contributes a line */
                x = v[k + 1 + voff];
            } else {
                /* move right: a loses a line */
                x = v[k - 1 + voff] + 1;
            }
            y = x - k;
            while (x < n && y < m && strcmp(a[x], b[y]) == 0) {
                x++;
                y++;
            }
            v[k + voff] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
        if (found >= 0) break;
    }

    if (found < 0) {
        ret = 0;
        goto cleanup;
    }

    {
        int x = n, y = m;
        for (int d = found; d >= 0; d--) {
            const int* snap = trace[d];
            int k = x - y;
            int prev_k = 0, prev_x = 0, prev_y = 0;

            if (d > 0) {
                if (k == -d || (k != d && snap[k - 1 + d + 1] < snap[k + 1 + d + 1])) {
                    prev_k = k + 1;
                } else {
                    prev_k = k - 1;
                }
                prev_x = snap[prev_k + d + 1];
                prev_y = prev_x - prev_k;
                if (prev_x < 0 || prev_x > n || prev_y < 0 || prev_y > m) {
                    n_log(LOG_ERR, "n_diff: inconsistent trace at d %d, giving up", d);
                    goto cleanup;
                }
            }
            /* d 0 is the leading snake: it runs diagonally from the origin,
               so walking back to (0,0) is all that is left */

            while (x > prev_x && y > prev_y) {
                x--;
                y--;
                if (!_emit(result, N_DIFF_COMMON, a[x], base_a + (size_t)x, base_b + (size_t)y)) goto cleanup;
            }
            if (d > 0) {
                if (x == prev_x) {
                    y--;
                    if (!_emit(result, N_DIFF_INSERT, b[y], N_DIFF_NO_LINE, base_b + (size_t)y)) goto cleanup;
                } else {
                    x--;
                    if (!_emit(result, N_DIFF_DELETE, a[x], base_a + (size_t)x, N_DIFF_NO_LINE)) goto cleanup;
                }
            }
        }
    }
    ret = 1;

cleanup:
    if (trace) {
        for (int d = 0; d <= maxd; d++) {
            FreeNoLog(trace[d]);
        }
        FreeNoLog(trace);
    }
    FreeNoLog(v);
    return ret;
}

N_DIFF_RESULT* n_diff_lines(const char* text_a, const char* text_b, size_t max_edits) {
    DIFF_TEXT ta, tb;
    N_DIFF_RESULT* result = NULL;
    size_t pre = 0, suf = 0, mid_n = 0, mid_m = 0;
    int rc = 0;

    if (max_edits == 0) max_edits = N_DIFF_MAX_EDITS_DEFAULT;
    if (max_edits > N_DIFF_MAX_EDITS_CEILING) max_edits = N_DIFF_MAX_EDITS_CEILING;

    if (!_text_load(&ta, text_a)) return NULL;
    if (!_text_load(&tb, text_b)) {
        _text_free(&ta);
        return NULL;
    }

    Malloc(result, N_DIFF_RESULT, 1);
    if (!result) goto fail;
    result->lines = new_generic_list(MAX_LIST_ITEMS);
    if (!result->lines) goto fail;

    /* identical head and tail cost nothing to match and shrink the region the
       bounded search has to cover, which is what keeps the common case (two
       runs of the same request) far below the edit ceiling */
    while (pre < ta.count && pre < tb.count && strcmp(ta.line[pre], tb.line[pre]) == 0) {
        pre++;
    }
    // cppcheck-suppress knownConditionTrueFalse ; cppcheck only follows the head loop exit where it ran to exhaustion, where nothing is left to match
    while (suf < ta.count - pre && suf < tb.count - pre &&
           strcmp(ta.line[ta.count - 1 - suf], tb.line[tb.count - 1 - suf]) == 0) {
        suf++;
    }
    mid_n = ta.count - pre - suf;
    mid_m = tb.count - pre - suf;

    /* the script is built back to front: tail, then the middle, then head */
    for (size_t i = 0; i < suf; i++) {
        size_t ia = ta.count - 1 - i;
        size_t ib = tb.count - 1 - i;
        if (!_emit(result, N_DIFF_COMMON, ta.line[ia], ia, ib)) goto fail;
    }

    if (mid_n == 0 && mid_m == 0) {
        rc = 1;
    } else if (mid_n > INT_MAX / 2 || mid_m > INT_MAX / 2) {
        /* more lines than the trace arithmetic can index */
        rc = 0;
    } else {
        rc = _myers(result, ta.line + pre, (int)mid_n, tb.line + pre, (int)mid_m, pre, pre, (int)max_edits);
    }
    if (rc < 0) goto fail;

    if (rc == 0) {
        /* too far apart to search: report the whole differing region as one
           delete run followed by one insert run, which is coarse but never
           misleading, and say so through truncated */
        result->truncated = 1;
        for (size_t i = mid_m; i > 0; i--) {
            size_t ib = pre + i - 1;
            if (!_emit(result, N_DIFF_INSERT, tb.line[ib], N_DIFF_NO_LINE, ib)) goto fail;
        }
        for (size_t i = mid_n; i > 0; i--) {
            size_t ia = pre + i - 1;
            if (!_emit(result, N_DIFF_DELETE, ta.line[ia], ia, N_DIFF_NO_LINE)) goto fail;
        }
    }

    for (size_t i = pre; i > 0; i--) {
        if (!_emit(result, N_DIFF_COMMON, ta.line[i - 1], i - 1, i - 1)) goto fail;
    }

    _text_free(&ta);
    _text_free(&tb);
    return result;

fail:
    n_diff_result_free(&result);
    _text_free(&ta);
    _text_free(&tb);
    return NULL;
}

void n_diff_result_free(N_DIFF_RESULT** result) {
    if (!result || !*result) return;
    if ((*result)->lines) {
        list_destroy(&(*result)->lines);
    }
    FreeNoLog(*result);
}

/*! growing output buffer fed by write_and_fit_ex */
typedef struct DIFF_OUT {
    /*! buffer, reallocated on growth */
    char* buf;
    /*! total allocation in bytes */
    NSTRBYTE size;
    /*! meaningful bytes, excluding the terminating '\0' */
    NSTRBYTE written;
} DIFF_OUT;

/*! append n bytes of s to the output, TRUE on success */
static int _out_mem(DIFF_OUT* o, const char* s, size_t n) {
    if (n == 0) return TRUE;
    return write_and_fit_ex(&o->buf, &o->size, &o->written, s, (NSTRBYTE)n, DIFF_OUT_PADDING);
}

/*! append a NULL terminated string to the output, TRUE on success */
static int _out_str(DIFF_OUT* o, const char* s) {
    return _out_mem(o, s, strlen(s));
}

/*! hand the buffer over to a new N_STR (no copy); the DIFF_OUT is emptied */
static N_STR* _out_finish(DIFF_OUT* o) {
    N_STR* out = NULL;
    if (!o->buf) return char_to_nstr("");
    out = char_to_nstr_nocopy(o->buf);
    if (!out) {
        FreeNoLog(o->buf);
    }
    o->buf = NULL;
    o->size = 0;
    o->written = 0;
    return out;
}

/*! unified diff marker for an operation */
static char _op_char(int op) {
    if (op == N_DIFF_DELETE) return '-';
    if (op == N_DIFF_INSERT) return '+';
    return ' ';
}

/*! append one script line as "<marker>text\n", TRUE on success */
static int _out_line(DIFF_OUT* o, const N_DIFF_LINE* line) {
    char marker = _op_char(line->op);
    if (!_out_mem(o, &marker, 1)) return FALSE;
    if (!_out_str(o, line->text ? line->text : "")) return FALSE;
    return _out_mem(o, "\n", 1);
}

N_STR* n_diff_to_unified(const N_DIFF_RESULT* result, size_t context) {
    DIFF_OUT out;
    N_DIFF_LINE** line = NULL;
    size_t* a_before = NULL;
    size_t* b_before = NULL;
    size_t count = 0, idx = 0, seen_a = 0, seen_b = 0;

    __n_assert(result, return NULL);
    __n_assert(result->lines, return NULL);

    memset(&out, 0, sizeof(out));

    if (context == N_DIFF_CONTEXT_FULL) {
        list_foreach(node, result->lines) {
            const N_DIFF_LINE* cur = (const N_DIFF_LINE*)node->ptr;
            if (!cur) continue;
            if (!_out_line(&out, cur)) goto fail;
        }
        return _out_finish(&out);
    }

    count = result->lines->nb_items;
    if (count == 0) return _out_finish(&out);

    /* indexed access, plus how many lines of each side precede every
       position, which is what the hunk headers are counted from */
    Malloc(line, N_DIFF_LINE*, count);
    if (!line) goto fail;
    Malloc(a_before, size_t, count);
    if (!a_before) goto fail;
    Malloc(b_before, size_t, count);
    if (!b_before) goto fail;

    list_foreach(node, result->lines) {
        N_DIFF_LINE* cur = (N_DIFF_LINE*)node->ptr;
        /* skipping empties rather than storing them keeps every entry of line[]
           non-NULL, which is what the hunk scan below relies on */
        if (!cur) continue;
        line[idx] = cur;
        a_before[idx] = seen_a;
        b_before[idx] = seen_b;
        if (cur->op != N_DIFF_INSERT) seen_a++;
        if (cur->op != N_DIFF_DELETE) seen_b++;
        idx++;
    }
    count = idx;
    if (count == 0) {
        FreeNoLog(b_before);
        FreeNoLog(a_before);
        FreeNoLog(line);
        return _out_finish(&out);
    }

    idx = 0;
    while (idx < count) {
        size_t start = 0, end = 0, last = 0, scan = 0;
        size_t len_a = 0, len_b = 0, start_a = 0, start_b = 0;
        char header[DIFF_HEADER_BUF];

        if (line[idx]->op == N_DIFF_COMMON) {
            idx++;
            continue;
        }

        start = (idx > context) ? (idx - context) : 0;
        last = idx;
        for (scan = idx; scan < count; scan++) {
            if (line[scan]->op != N_DIFF_COMMON) {
                last = scan;
            } else if (scan - last > 2 * context) {
                /* far enough from the last change to close the hunk; a
                   nearer one would have been merged into it */
                break;
            }
        }
        end = last + context;
        if (end >= count) end = count - 1;

        for (scan = start; scan <= end; scan++) {
            if (line[scan]->op != N_DIFF_INSERT) len_a++;
            if (line[scan]->op != N_DIFF_DELETE) len_b++;
        }
        start_a = len_a ? a_before[start] + 1 : a_before[start];
        start_b = len_b ? b_before[start] + 1 : b_before[start];

        snprintf(header, sizeof(header), "@@ -%zu,%zu +%zu,%zu @@\n", start_a, len_a, start_b, len_b);
        if (!_out_str(&out, header)) goto fail;
        for (scan = start; scan <= end; scan++) {
            if (!_out_line(&out, line[scan])) goto fail;
        }
        idx = end + 1;
    }

    FreeNoLog(b_before);
    FreeNoLog(a_before);
    FreeNoLog(line);
    return _out_finish(&out);

fail:
    FreeNoLog(b_before);
    FreeNoLog(a_before);
    FreeNoLog(line);
    FreeNoLog(out.buf);
    return NULL;
}
