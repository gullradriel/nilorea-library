/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@example ex_stack.c
 *@brief Nilorea Library stack API
 *@author Castagnier Mickael
 *@version 1.0
 *@date 17/06/2024
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_stack.h"

void usage(void) {
    fprintf(stderr,
            "     -v version\n"
            "     -V log level: LOG_INFO, LOG_NOTICE, LOG_ERR, LOG_DEBUG\n"
            "     -h help\n");
}

void process_args(int argc, char** argv) {
    int getoptret = 0,
        log_level = LOG_DEBUG; /* default log level */

    /* Arguments optionnels */
    /* -v version
     * -V log level
     * -h help
     */
    while ((getoptret = getopt(argc, argv, "hvV:")) != EOF) {
        switch (getoptret) {
            case 'v':
                fprintf(stderr, "Date de compilation : %s a %s.\n", __DATE__, __TIME__);
                exit(1);
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
                else {
                    fprintf(stderr, "%s n'est pas un niveau de log valide.\n", optarg);
                    exit(-1);
                }
                break;
            default:
            case '?': {
                if (optopt == 'V') {
                    fprintf(stderr, "\n      Missing log level\n");
                } else if (optopt == 'p') {
                    fprintf(stderr, "\n      Missing port\n");
                } else if (optopt != 's') {
                    fprintf(stderr, "\n      Unknow missing option %c\n", optopt);
                }
                usage();
                exit(1);
            }
            case 'h': {
                usage();
                exit(1);
            }
        }
    }
    set_log_level(log_level);
} /* void process_args( ... ) */

int main(int argc, char** argv) {
    set_log_level(LOG_INFO);

    /* processing args and set log_level */
    process_args(argc, argv);

    STACK* stack = new_stack(16);
    n_log(LOG_INFO, "created stack of 16 elements at %p", stack);

    for (int it = 0; it < 20; it++) {
        int32_t nb = rand() % 10;
        bool btest = rand() % 2;

        stack_push(stack, nb);
        stack_push(stack, btest);
    }

    for (int it = 0; it < 20; it++) {
        STACK_ITEM* item = NULL;
        item = stack_peek(stack, stack->tail);
        if (item) {
            uint8_t status = STACK_IS_UNDEFINED;
            switch (item->v_type) {
                case STACK_ITEM_BOOL: {
                    bool bval = stack_pop_b(stack, &status);
                    n_log(LOG_INFO, "got bool: %d", bval);
                    status = STACK_ITEM_OK;
                } break;
                case STACK_ITEM_INT32: {
                    int32_t val = stack_pop_i32(stack, &status);
                    n_log(LOG_INFO, "got int32_t: %d", val);
                    status = STACK_ITEM_OK;
                } break;
                default:
                    n_log(LOG_ERR, "uknown type %d", item->v_type);
                    break;
            }
            if (status != STACK_ITEM_OK) {
                n_log(LOG_ERR, "error popping value ! status: %d", status);
            }
        }
    }

    delete_stack(&stack);

    /* test additional type variants and stack_is_full, stack_is_empty */
    stack = new_stack(8);

    n_log(LOG_INFO, "stack_is_empty on new stack: %d", stack_is_empty(stack));

    /* push various types */
    char cval = 'A';
    stack_push(stack, cval);
    n_log(LOG_INFO, "pushed char: %c", cval);

    double dval = 3.14159;
    stack_push(stack, dval);
    n_log(LOG_INFO, "pushed double: %f", dval);

    float fval = 2.71f;
    stack_push(stack, fval);
    n_log(LOG_INFO, "pushed float: %f", fval);

    uint8_t u8val = 255;
    stack_push(stack, u8val);
    n_log(LOG_INFO, "pushed uint8: %u", u8val);

    int8_t i8val = -42;
    stack_push(stack, i8val);
    n_log(LOG_INFO, "pushed int8: %d", i8val);

    uint32_t u32val = 123456;
    stack_push(stack, u32val);
    n_log(LOG_INFO, "pushed uint32: %u", u32val);

    int data_value = 999;
    void* pval = &data_value;
    stack_push(stack, pval);
    n_log(LOG_INFO, "pushed pointer: %p", pval);

    int32_t i32val = -789;
    stack_push(stack, i32val);
    n_log(LOG_INFO, "pushed int32: %d", i32val);

    n_log(LOG_INFO, "stack_is_full: %d", stack_is_full(stack));

    /* pop them back in reverse order */
    uint8_t pop_status = STACK_IS_UNDEFINED;
    STACK_ITEM* peek_item = NULL;
    while ((peek_item = stack_peek(stack, stack->tail)) != NULL) {
        pop_status = STACK_IS_UNDEFINED;
        switch (peek_item->v_type) {
            case STACK_ITEM_CHAR: {
                char cv = stack_pop_c(stack, &pop_status);
                n_log(LOG_INFO, "popped char: %c", cv);
            } break;
            case STACK_ITEM_DOUBLE: {
                double dv = stack_pop_d(stack, &pop_status);
                n_log(LOG_INFO, "popped double: %f", dv);
            } break;
            case STACK_ITEM_FLOAT: {
                float fv = stack_pop_f(stack, &pop_status);
                n_log(LOG_INFO, "popped float: %f", fv);
            } break;
            case STACK_ITEM_UINT8: {
                uint8_t uv = stack_pop_ui8(stack, &pop_status);
                n_log(LOG_INFO, "popped uint8: %u", uv);
            } break;
            case STACK_ITEM_INT8: {
                int8_t iv = stack_pop_i8(stack, &pop_status);
                n_log(LOG_INFO, "popped int8: %d", iv);
            } break;
            case STACK_ITEM_UINT32: {
                uint32_t uv = stack_pop_ui32(stack, &pop_status);
                n_log(LOG_INFO, "popped uint32: %u", uv);
            } break;
            case STACK_ITEM_INT32: {
                int32_t iv = stack_pop_i32(stack, &pop_status);
                n_log(LOG_INFO, "popped int32: %d", iv);
            } break;
            case STACK_ITEM_PTR: {
                void* pv = stack_pop_p(stack, &pop_status);
                n_log(LOG_INFO, "popped pointer: %p (value=%d)", pv, pv ? *(int*)pv : 0);
            } break;
            default:
                n_log(LOG_ERR, "unknown type %d", peek_item->v_type);
                stack_pop_i32(stack, &pop_status);
                break;
        }
    }

    n_log(LOG_INFO, "stack_is_empty after pops: %d", stack_is_empty(stack));
    delete_stack(&stack);

    exit(0);
}
