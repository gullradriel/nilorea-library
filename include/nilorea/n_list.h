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
 *@file n_list.h
 *@brief List structures and definitions
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/04/13
 */

#ifndef N_GENERIC_LIST
#define N_GENERIC_LIST

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup LIST LISTS: generic type list
  @addtogroup LIST
  @{
  */

#include <stdio.h>
#include <stdlib.h>

/*! Structure of a generic list node */
typedef struct LIST_NODE {
    /*! void pointer to store */
    void* ptr;

    /*! pointer to destructor function if any, else NULL */
    void (*destroy_func)(void* ptr);

    /*! pointer to the next node */
    struct LIST_NODE* next;
    /*! pointer to the previous node */
    struct LIST_NODE* prev;

} LIST_NODE;

/*! Structure of a generic LIST container */
typedef struct LIST {
    /*! number of item currently in the list */
    size_t nb_items;
    /*! Maximum number of items in the list. 0 means unlimited */
    size_t nb_max_items;

    /*! pointer to the start of the list */
    LIST_NODE* start;
    /*! pointer to the end of the list */
    LIST_NODE* end;

} LIST;

/*! flag to pass to new_generic_list for an unlimited number of item in the list. Can be dangerous ! */
#define UNLIMITED_LIST_ITEMS 0
/*! flag to pass to new_generic_list for the maximum possible number of item in a list */
#define MAX_LIST_ITEMS SIZE_MAX

/*! Macro helper for linking two nodes, with NULL safety */
#define link_node(__NODE_1, __NODE_2)                                                                                              \
    do {                                                                                                                           \
        if (!(__NODE_1) || !(__NODE_2)) {                                                                                          \
            n_log(LOG_ERR, "link_node: NULL argument (%s=%p, %s=%p)", #__NODE_1, (void*)(__NODE_1), #__NODE_2, (void*)(__NODE_2)); \
        } else {                                                                                                                   \
            (__NODE_2)->prev = (__NODE_1);                                                                                         \
            (__NODE_1)->next = (__NODE_2);                                                                                         \
        }                                                                                                                          \
    } while (0)

/*! ForEach macro helper, safe for node removal during iteration */
#define list_foreach(__ITEM_, __LIST_)                                                                                                                      \
    for (LIST_NODE* __ITEM_ = (__LIST_) ? (__LIST_)->start : NULL, *__next_##__ITEM_ = __ITEM_ ? __ITEM_->next : NULL; __ITEM_; __ITEM_ = __next_##__ITEM_, \
                    __next_##__ITEM_ = __ITEM_ ? __ITEM_->next : NULL)

/*! Pop macro helper for void pointer casting */
#define list_pop(__LIST_, __TYPE_) (__TYPE_*)list_pop_f(__LIST_)
/*! Shift macro helper for void pointer casting */
#define list_shift(__LIST_, __TYPE_) (__TYPE_*)list_shift_f(__LIST_, __FILE__, __LINE__)
/*! Remove macro helper for void pointer casting */
#define remove_list_node(__LIST_, __NODE_, __TYPE_) (__TYPE_*)remove_list_node_f(__LIST_, __NODE_)

/*! initialize a list */
LIST* new_generic_list(size_t max_items);
/*! create a new node */
LIST_NODE* new_list_node(void* ptr, void (*destructor)(void* ptr));
/*! remove a node */
void* remove_list_node_f(LIST* list, LIST_NODE* node);
/*! push a node at the end of the list */
int list_node_push(LIST* list, LIST_NODE* node);
/*! pop a node from the end of the list */
LIST_NODE* list_node_pop(LIST* list);
/*! shift a node from the beginning of the list */
LIST_NODE* list_node_shift(LIST* list);
/*! unshift a node at the beginning of the list */
int list_node_unshift(LIST* list, LIST_NODE* node);

/*! add a pointer at the end of the list */
int list_push(LIST* list, void* ptr, void (*destructor)(void* ptr));
/*! add a pointer sorted via comparator from the end of the list */
int list_push_sorted(LIST* list, void* ptr, int (*comparator)(const void* a, const void* b), void (*destructor)(void* ptr));
/*! put a pointer at the beginning of list */
int list_unshift(LIST* list, void* ptr, void (*destructor)(void* ptr));
/*! put a pointer sorted via comparator from the start to the end */
int list_unshift_sorted(LIST* list, void* ptr, int (*comparator)(const void* a, const void* b), void (*destructor)(void* ptr));

/*! get last ptr from list */
void* list_pop_f(LIST* list);
/*! get first ptr from list */
void* list_shift_f(LIST* list, char* file, size_t line);

/*! search ptr in list */
LIST_NODE* list_search(LIST* list, void* ptr);
/*! search for data in list */
LIST_NODE* list_search_with_f(LIST* list, int (*checkfunk)(void* ptr));

/*! empty the list */
int list_empty(LIST* list);
/*! empty the list with a custom free function */
int list_empty_with_f(LIST* list, void (*free_fnct)(void* ptr));

/*! free the list */
int list_destroy(LIST** list);

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif
