/**\example ex_stack.c Nilorea Library stack API
 *\author Castagnier Mickael
 *\version 1.0
 *\date 17/06/2024
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
                if (!strncmp("LOG_NULL", optarg, 5)) {
                    log_level = LOG_NULL;
                } else {
                    if (!strncmp("LOG_NOTICE", optarg, 6)) {
                        log_level = LOG_NOTICE;
                    } else {
                        if (!strncmp("LOG_INFO", optarg, 7)) {
                            log_level = LOG_INFO;
                        } else {
                            if (!strncmp("LOG_ERR", optarg, 5)) {
                                log_level = LOG_ERR;
                            } else {
                                if (!strncmp("LOG_DEBUG", optarg, 5)) {
                                    log_level = LOG_DEBUG;
                                } else {
                                    fprintf(stderr, "%s n'est pas un niveau de log valide.\n", optarg);
                                    exit(-1);
                                }
                            }
                        }
                    }
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
        bool btest = rand() % 1;

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

    exit(0);
}
