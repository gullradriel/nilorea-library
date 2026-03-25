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
 *@example ex_trees.c
 *@brief Nilorea Library tree test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 03/01/2025
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
    /* processing args and set log_level */
    process_args(argc, argv);

    /* n-ary tree: new_tree, tree_create_node, tree_insert_child, tree_delete_node, tree_destroy */
    n_log(LOG_NOTICE, "--- N-ary Tree ---");
    TREE* tree = new_tree();
    __n_assert(tree, return 1);

    NODE_DATA root_data;
    root_data.type = 0;
    root_data.value.ival = 1;
    tree->root = tree_create_node(root_data, NULL);

    NODE_DATA child1_data;
    child1_data.type = 0;
    child1_data.value.ival = 10;
    TREE_NODE* child1 = tree_create_node(child1_data, NULL);
    tree_insert_child(tree->root, child1);

    NODE_DATA child2_data;
    child2_data.type = 0;
    child2_data.value.ival = 20;
    TREE_NODE* child2 = tree_create_node(child2_data, NULL);
    tree_insert_child(tree->root, child2);

    NODE_DATA grandchild_data;
    grandchild_data.type = 0;
    grandchild_data.value.ival = 100;
    TREE_NODE* grandchild = tree_create_node(grandchild_data, NULL);
    tree_insert_child(child1, grandchild);

    n_log(LOG_INFO, "Tree root: %d", tree->root->data.value.ival);
    n_log(LOG_INFO, "Tree nb_nodes: %zu", tree->nb_nodes);

    tree_delete_node(tree, grandchild);
    n_log(LOG_INFO, "After delete grandchild, nb_nodes: %zu", tree->nb_nodes);

    tree_destroy(&tree);
    n_log(LOG_NOTICE, "N-ary tree destroyed");

    /* quadtree: create_quadtree, create_node, insert, search, free_quadtree */
    n_log(LOG_NOTICE, "--- Quad Tree ---");
    QUADTREE* qt = create_quadtree(COORD_INT);

    int data1 = 100;
    int data2 = 200;

    COORD_VALUE x1, y1, x2, y2;
    x1.i = 5;
    y1.i = 5;
    x2.i = 9;
    y2.i = 7;

    insert(qt, &(qt->root), x1, y1, &data1);
    insert(qt, &(qt->root), x2, y2, &data2);

    /* create_node used directly */
    COORD_VALUE x3, y3;
    x3.i = 3;
    y3.i = 3;
    int data3 = 300;
    QUADTREE_NODE* manual_node = create_node(x3, y3, &data3);
    n_log(LOG_INFO, "create_node: data=%d", *(int*)manual_node->data_ptr);
    Free(manual_node);

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

    free_quadtree(qt->root);
    free(qt);

    /* octree: create_octree, create_octree_node, insert_octree, free_octree */
    n_log(LOG_NOTICE, "--- Octree ---");
    OCTREE* ot = create_octree(COORD_INT);
    __n_assert(ot, return 1);

    int oct_data1 = 500;
    int oct_data2 = 600;
    POINT3D p1;
    p1.x.i = 1;
    p1.y.i = 2;
    p1.z.i = 3;
    POINT3D p2;
    p2.x.i = 5;
    p2.y.i = 6;
    p2.z.i = 7;

    insert_octree(ot, p1, &oct_data1);
    insert_octree(ot, p2, &oct_data2);
    n_log(LOG_INFO, "Inserted 2 octree points");

    /* create_octree_node and free_octree_node */
    POINT3D p3;
    p3.x.i = 10;
    p3.y.i = 11;
    p3.z.i = 12;
    OCTREE_NODE* oct_node = create_octree_node(p3, NULL);
    n_log(LOG_INFO, "create_octree_node at (%d,%d,%d)", oct_node->point.x.i, oct_node->point.y.i, oct_node->point.z.i);
    free_octree_node(oct_node);

    free_octree(ot);
    n_log(LOG_NOTICE, "Octree destroyed");

    exit(0);
}
