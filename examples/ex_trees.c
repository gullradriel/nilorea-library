/**\example ex_trees.c Nilorea Library common api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 03/01/2019
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_trees.h"

#ifndef __windows__
#include <sys/wait.h>
#endif

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
    /* processing args and set log_level */
    process_args(argc, argv);

    // Create a quad tree with int coordinates
    QUADTREE* qt = create_quadtree(COORD_INT);

    // Example data pointers (could be any type)
    int data1 = 100;
    int data2 = 200;

    // Insert points into the quad tree
    COORD_VALUE x1, y1, x2, y2;
    x1.i = 5;
    y1.i = 5;
    x2.i = 9;
    y2.i = 7;

    insert(qt, &(qt->root), x1, y1, &data1);
    insert(qt, &(qt->root), x2, y2, &data2);

    // Search for a point in the quad tree
    QUADTREE_NODE* result = search(qt, qt->root, x1, y1);
    if (result && result->data_ptr) {
        printf("Found node at (");
        qt->print(result->x);
        printf(", ");
        qt->print(result->y);
        printf(") with data: %d\n", *(int*)result->data_ptr);
    } else {
        printf("Node not found or has no data.\n");
    }

    // Free the quad tree
    free_quadtree(qt->root);
    free(qt);

    exit(0);
}
