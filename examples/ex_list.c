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
 *@example ex_list.c
 *@brief Nilorea Library list api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/05/2015
 */

#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_str.h"

#define LIST_LIMIT 10
#define NB_TEST_ELEM 15

void print_list_info(LIST* list) {
    __n_assert(list, return);
    n_log(LOG_NOTICE, "List: %p, %d max_elements , %d elements", list, list->nb_max_items, list->nb_items);
}

int nstrcmp(const void* a, const void* b) {
    const N_STR* s1 = a;
    const N_STR* s2 = b;

    if (!s1 || !s1->data)
        return 1;
    if (!s2 || !s2->data)
        return -1;

    return strcmp(s1->data, s2->data);
}

int main(void) {
    set_log_level(LOG_DEBUG);

    LIST* list = new_generic_list(LIST_LIMIT);

    __n_assert(list, return FALSE);

    N_STR* nstr = NULL;

    n_log(LOG_NOTICE, "Testing empty list cleaning");

    list_destroy(&list);

    n_log(LOG_NOTICE, "list list: adding %d element in list element (%d) list, empty the list at the end", NB_TEST_ELEM, LIST_LIMIT);
    list = new_generic_list(LIST_LIMIT);
    for (int it = 0; it < NB_TEST_ELEM; it++) {
        nstrprintf(nstr, "Nombre aleatoire : %d", rand() % 1000);
        if (nstr) {
            int func = rand() % 4;
            switch (func) {
                case 0:
                    n_log(LOG_NOTICE, "list_push");
                    if (list_push(list, nstr, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                case 1:
                    n_log(LOG_NOTICE, "list_unshift");
                    if (list_unshift(list, nstr, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                case 2:
                    n_log(LOG_NOTICE, "list_push_sorted");
                    if (list_push_sorted(list, nstr, nstrcmp, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                case 3:
                    n_log(LOG_NOTICE, "list_unshift");
                    if (list_unshift_sorted(list, nstr, nstrcmp, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                default:
                    n_log(LOG_ERR, "should never happen: no func %d !", func);
                    break;
            }
            nstr = NULL;
            print_list_info(list);
        }
    }
    n_log(LOG_NOTICE, "Emptying the list and setting nb_max_item to unlimit");
    list_empty(list);
    /* setiing no item limit in list */
    list->nb_max_items = UNLIMITED_LIST_ITEMS;
    for (int it = 0; it < NB_TEST_ELEM; it++) {
        nstrprintf(nstr, "Nombre aleatoire : %d", rand() % 1000);
        if (nstr) {
            int func = rand() % 4;
            switch (func) {
                case 0:
                    n_log(LOG_NOTICE, "list_push");
                    if (list_push(list, nstr, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                case 1:
                    n_log(LOG_NOTICE, "list_unshift");
                    if (list_unshift(list, nstr, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                case 2:
                    n_log(LOG_NOTICE, "list_push_sorted");
                    if (list_push_sorted(list, nstr, nstrcmp, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                case 3:
                    n_log(LOG_NOTICE, "list_unshift sorted");
                    if (list_unshift_sorted(list, nstr, nstrcmp, free_nstr_ptr) == FALSE)
                        free_nstr(&nstr);
                    break;
                default:
                    n_log(LOG_ERR, "should never happen: no func %d !", func);
                    break;
            }
            nstr = NULL;
            print_list_info(list);
        }
    }
    list_foreach(node, list) {
        N_STR* nodestr = (N_STR*)node->ptr;
        n_log(LOG_INFO, "Listnode: %p item: %s", node, nodestr->data);
    }

    /* test list_pop and list_shift */
    N_STR* popped = list_pop(list, N_STR);
    if (popped) {
        n_log(LOG_INFO, "list_pop: %s", _nstr(popped));
        free_nstr_ptr(popped);
    }
    N_STR* shifted = list_shift(list, N_STR);
    if (shifted) {
        n_log(LOG_INFO, "list_shift: %s", _nstr(shifted));
        free_nstr_ptr(shifted);
    }

    /* test list_search */
    if (list->nb_items > 0) {
        LIST_NODE* first = list->start;
        LIST_NODE* found = list_search(list, first->ptr);
        if (found) {
            n_log(LOG_INFO, "list_search found node: %p", found);
        }
    }

    /* test remove_list_node */
    if (list->nb_items > 0) {
        LIST_NODE* target = list->start;
        void* removed_ptr = remove_list_node(list, target, void);
        if (removed_ptr) {
            n_log(LOG_INFO, "remove_list_node: removed %p", removed_ptr);
            free_nstr_ptr(removed_ptr);
        }
    }

    /* test new_list_node and list_node_push / list_node_pop */
    nstrprintf(nstr, "Manual node test");
    if (nstr) {
        LIST_NODE* manual_node = new_list_node(nstr, free_nstr_ptr);
        if (manual_node) {
            list_node_push(list, manual_node);
            n_log(LOG_INFO, "list_node_push: pushed manual node");
        }
        LIST_NODE* popped_node = list_node_pop(list);
        if (popped_node) {
            N_STR* popped_nstr = (N_STR*)popped_node->ptr;
            n_log(LOG_INFO, "list_node_pop: %s", _nstr(popped_nstr));
            if (popped_node->destroy_func) {
                popped_node->destroy_func(popped_node->ptr);
            }
            Free(popped_node);
        }
        nstr = NULL;
    }

    /* test list_node_unshift / list_node_shift */
    nstrprintf(nstr, "Unshift node test");
    if (nstr) {
        LIST_NODE* manual_node2 = new_list_node(nstr, free_nstr_ptr);
        if (manual_node2) {
            list_node_unshift(list, manual_node2);
            n_log(LOG_INFO, "list_node_unshift: unshifted manual node");
        }
        LIST_NODE* shifted_node = list_node_shift(list);
        if (shifted_node) {
            N_STR* shifted_nstr = (N_STR*)shifted_node->ptr;
            n_log(LOG_INFO, "list_node_shift: %s", _nstr(shifted_nstr));
            if (shifted_node->destroy_func) {
                shifted_node->destroy_func(shifted_node->ptr);
            }
            Free(shifted_node);
        }
        nstr = NULL;
    }

    list_destroy(&list);

    exit(0);
} /* END_OF_MAIN */
