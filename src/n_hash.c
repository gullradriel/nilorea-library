/**
 *@file n_hash.c
 *@brief Hash functions and table
 *@author Castagnier Mickael
 *@version 2.0
 *@date 16/03/2015
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_hash.h"
#include "nilorea/n_str.h"

#include <pthread.h>
#include <string.h>
#include <strings.h>

#ifdef __windows__
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

/************ TRIE TREE TABLES ************/

/**
 *@brief node creation, HASH_CLASSIC mode
 *@param table targeted table
 *@param key key of new node
 *@return NULL or a new HASH_NODE *
 */
HASH_NODE* _ht_new_node_trie(HASH_TABLE* table, const char key) {
    __n_assert(table, return NULL);

    HASH_NODE* new_hash_node = NULL;
    Malloc(new_hash_node, HASH_NODE, 1);
    __n_assert(new_hash_node, n_log(LOG_ERR, "Could not allocate new_hash_node"); return NULL);
    new_hash_node->key = NULL;
    new_hash_node->hash_value = 0;
    new_hash_node->data.ptr = NULL;
    new_hash_node->destroy_func = NULL;
    new_hash_node->children = NULL;
    new_hash_node->is_leaf = 0;
    new_hash_node->need_rehash = 0;
    new_hash_node->alphabet_length = table->alphabet_length;

    Malloc(new_hash_node->children, HASH_NODE*, table->alphabet_length);
    __n_assert(new_hash_node->children, n_log(LOG_ERR, "Could not allocate new_hash_node"); Free(new_hash_node); return NULL);

    /* n_log( LOG_DEBUG , "node: %d %c table: alpha: %d / offset %d" , key , key , table -> alphabet_length , table -> alphabet_offset ); */

    for (size_t it = 0; it < table->alphabet_length; it++) {
        new_hash_node->children[it] = NULL;
    }
    new_hash_node->is_leaf = 0;
    new_hash_node->key_id = key;
    return new_hash_node;
} /* _ht_new_node_trie(...) */

/**
 *@brief  Search a key and tell if it's holding a value (leaf)
 *@param table targeted hash table
 *@param key key verify
 *@return 1 or 0
 */
int _ht_is_leaf_node_trie(HASH_TABLE* table, const char* key) {
    __n_assert(table, return -1);

    /* checks if the prefix match of key and root is a leaf node */
    HASH_NODE* node = table->root;
    for (size_t it = 0; key[it]; it++) {
        size_t index = (size_t)key[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index]) {
            node = node->children[index];
        }
    }
    return node->is_leaf;
} /* _ht_is_leaf_node_trie(...) */

/**
 *@brief destroy a HASH_NODE by first calling the HASH_NODE destructor
 *@param node The node to kill
 */
void _ht_node_destroy(void* node) {
    HASH_NODE* node_ptr = (HASH_NODE*)node;
    __n_assert(node_ptr, return);
    if (node_ptr->type == HASH_STRING) {
        Free(node_ptr->data.string);
    }
    if (node_ptr->type == HASH_PTR) {
        if (node_ptr->destroy_func && node_ptr->data.ptr) {
            node_ptr->destroy_func(node_ptr->data.ptr);
        }
        /* No free by default. must be passed as a destroy_func
           else
           {
           Free( node_ptr  -> data . ptr );
           }
           */
    }
    FreeNoLog(node_ptr->key);
    if (node_ptr->alphabet_length > 0) {
        for (size_t it = 0; it < node_ptr->alphabet_length; it++) {
            if (node_ptr->children[it]) {
                _ht_node_destroy(node_ptr->children[it]);
            }
        }
        Free(node_ptr->children);
    }
    Free(node_ptr)
} /* _ht_node_destroy */

/**
 *@brief check and return branching index in key if any
 *@param table targeted hash table
 *@param key associated value's key
 *@return index of the divergence or SIZE_MAX on error
 */
size_t _ht_check_trie_divergence(HASH_TABLE* table, const char* key) {
    __n_assert(table, return SIZE_MAX);
    __n_assert(key, return SIZE_MAX);

    HASH_NODE* node = table->root;

    size_t last_index = 0;
    for (size_t it = 0; key[it] != '\0'; it++) {
        size_t index = (size_t)key[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index]) {
            /* child check */
            for (size_t it2 = 0; it2 < table->alphabet_length; it2++) {
                if (it2 != (unsigned)index && node->children[it2]) {
                    /* child found, update branch index */
                    last_index = it + 1;
                    break;
                }
            }
            /* Go to the next child in the sequence */
            node = node->children[index];
        }
    }
    return last_index;
} /* _ht_check_trie_divergence(...) */

/**
 *@brief  find the longest prefix string that is not the current key
 *@param table targeted hash table
 *@param key key to search prefix for
 *@return TRUE or FALSE
 */
char* _ht_find_longest_prefix_trie(HASH_TABLE* table, const char* key) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    size_t len = strlen(key);

    /* start with full key and backtrack to search for divergences */
    char* longest_prefix = NULL;
    Malloc(longest_prefix, char, len + 1);
    __n_assert(longest_prefix, return NULL);
    memcpy(longest_prefix, key, len);

    /* check branching from the root */
    size_t branch_index = _ht_check_trie_divergence(table, longest_prefix);
    if (branch_index > 0 && branch_index != SIZE_MAX) {
        /* there is branching, update the position to the longest match and update the longest prefix by the branch index length */
        longest_prefix[branch_index - 1] = '\0';
        Realloc(longest_prefix, char, (branch_index + 1));
        if (!longest_prefix) {
            n_log(LOG_ERR, "reallocation error, stopping find longest prefix");
            return NULL;
        }
        __n_assert(longest_prefix, return NULL);
    }
    return longest_prefix;
} /* _ht_find_longest_prefix_trie(...) */

/**
 *@brief Remove a key from a trie table and destroy the node
 *@param table targeted tabled
 *@param key the node key to kill
 *@return TRUE or FALSE
 */
int _ht_remove_trie(HASH_TABLE* table, const char* key) {
    __n_assert(table, return FALSE);
    __n_assert(table->root, return FALSE);
    __n_assert(key, return FALSE);

    /* stop if matching node not a leaf node */
    if (!_ht_is_leaf_node_trie(table, key)) {
        return FALSE;
    }

    HASH_NODE* node = table->root;
    /* find the longest prefix string that is not the current key */
    char* longest_prefix = _ht_find_longest_prefix_trie(table, key);
    if (longest_prefix && longest_prefix[0] == '\0') {
        Free(longest_prefix);
        return FALSE;
    }
    /* keep track of position in the tree */
    size_t it = 0;
    for (it = 0; longest_prefix[it] != '\0'; it++) {
        size_t index = (size_t)longest_prefix[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index] != NULL) {
            /* common prefix, keep moving */
            node = node->children[index];
        } else {
            /* not found */
            Free(longest_prefix);
            return FALSE;
        }
    }
    /* deepest common node between the two strings */
    /* deleting the sequence corresponding to key */
    for (; key[it] != '\0'; it++) {
        size_t index = (size_t)key[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index]) {
            /* delete the remaining sequence */
            HASH_NODE* node_to_kill = node->children[index];
            node->children[index] = NULL;
            _ht_node_destroy(node_to_kill);
        }
    }
    Free(longest_prefix);

    table->nb_keys--;

    return TRUE;
} /* _ht_remove_trie(...) */

/**
 *@brief put an integral value with given key in the targeted hash table [TRIE HASH TABLE)
 *@param table targeted hash table
 *@param key associated value's key
 *@param value integral value to put
 *@return TRUE or FALSE
 */
int _ht_put_int_trie(HASH_TABLE* table, const char* key, HASH_INT_TYPE value) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    HASH_NODE* node = table->root;

    for (size_t it = 0; key[it] != '\0'; it++) {
        size_t index = (size_t)key[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        /* n_log( LOG_DEBUG , "index:%d" , index ); */
        if (node->children[index] == NULL) {
            /* create a node */
            node->children[index] = _ht_new_node_trie(table, key[it]);
            __n_assert(node->children[index], return FALSE);
        } else {
            /* nothing to do since node is existing */
        }
        /* go down a level, to the child referenced by index */
        node = node->children[index];
    }
    /* At the end of the key, mark this node as the leaf node */
    node->is_leaf = 1;
    /* Put the key */
    node->key = strdup(key);
    if (!node->key) {
        n_log(LOG_ERR, "strdup failure for key [%s]", key);
        table->nb_keys++; /* compensate for upcoming remove */
        _ht_remove_trie(table, key);
        return FALSE;
    }
    /* Put the value */
    node->data.ival = value;
    node->type = HASH_INT;

    table->nb_keys++;

    return TRUE;
} /* _ht_put_int_trie(...) */

/**
 *@brief put a double value with given key in the targeted hash table [TRIE HASH TABLE)
 *@param table targeted hash table
 *@param key associated value's key
 *@param value double value to put
 *@return TRUE or FALSE
 */
int _ht_put_double_trie(HASH_TABLE* table, const char* key, double value) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    HASH_NODE* node = table->root;

    for (size_t it = 0; key[it] != '\0'; it++) {
        size_t index = (size_t)key[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index] == NULL) {
            /* create a node */
            node->children[index] = _ht_new_node_trie(table, key[it]);
            __n_assert(node->children[index], return FALSE);
        } else {
            /* nothing to do since node is existing */
        }
        /* go down a level, to the child referenced by index */
        node = node->children[index];
    }
    /* At the end of the key, mark this node as the leaf node */
    node->is_leaf = 1;
    /* Put the key */
    node->key = strdup(key);
    if (!node->key) {
        n_log(LOG_ERR, "strdup failure for key [%s]", key);
        table->nb_keys++; /* compensate for upcoming remove */
        _ht_remove_trie(table, key);
        return FALSE;
    }
    /* Put the value */
    node->data.fval = value;
    node->type = HASH_DOUBLE;

    table->nb_keys++;

    return TRUE;
} /* _ht_put_double_trie(...) */

/**
 *@brief put a duplicate of the string value with given key in the targeted hash table [TRIE HASH TABLE)
 *@param table targeted hash table
 *@param key associated value's key
 *@param string string value to put (copy)
 *@return TRUE or FALSE
 */
int _ht_put_string_trie(HASH_TABLE* table, const char* key, char* string) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    HASH_NODE* node = table->root;

    for (size_t it = 0; key[it] != '\0'; it++) {
        size_t index = (size_t)key[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index] == NULL) {
            /* create a node */
            node->children[index] = _ht_new_node_trie(table, key[it]);
            __n_assert(node->children[index], return FALSE);
        } else {
            /* nothing to do since node is existing */
        }
        /* go down a level, to the child referenced by index */
        node = node->children[index];
    }
    /* At the end of the key, mark this node as the leaf node */
    node->is_leaf = 1;
    /* Put the key */
    node->key = strdup(key);
    if (!node->key) {
        n_log(LOG_ERR, "strdup failure for key [%s]", key);
        table->nb_keys++; /* compensate for upcoming remove */
        _ht_remove_trie(table, key);
        return FALSE;
    }
    /* Put the value */
    if (string) {
        node->data.string = strdup(string);
        if (!node->data.string) {
            n_log(LOG_ERR, "strdup failure for value at key [%s]", key);
            Free(node->key);
            table->nb_keys++; /* compensate for upcoming remove */
            _ht_remove_trie(table, key);
            return FALSE;
        }
    } else {
        node->data.string = NULL;
    }

    node->type = HASH_STRING;

    table->nb_keys++;

    return TRUE;
} /* _ht_put_string_trie(...) */

/**
 *@brief put a pointer to the string value with given key in the targeted hash table [TRIE HASH TABLE)
 *@param table targeted hash table
 *@param key associated value's key
 *@param string string value to put (pointer)
 *@return TRUE or FALSE
 */
int _ht_put_string_ptr_trie(HASH_TABLE* table, const char* key, char* string) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    HASH_NODE* node = table->root;

    for (size_t it = 0; key[it] != '\0'; it++) {
        size_t index = (size_t)key[it] - table->alphabet_offset;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %d for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index] == NULL) {
            /* create a node */
            node->children[index] = _ht_new_node_trie(table, key[it]);
            __n_assert(node->children[index], return FALSE);
        } else {
            /* nothing to do since node is existing */
        }
        /* go down a level, to the child referenced by index */
        node = node->children[index];
    }
    /* At the end of the key, mark this node as the leaf node */
    node->is_leaf = 1;
    /* Put the key */
    node->key = strdup(key);
    if (!node->key) {
        n_log(LOG_ERR, "strdup failure for key [%s]", key);
        table->nb_keys++; /* compensate for upcoming remove */
        _ht_remove_trie(table, key);
        return FALSE;
    }
    /* Put the string */
    node->data.string = string;
    node->type = HASH_STRING;

    table->nb_keys++;

    return TRUE;
} /* _ht_put_string_ptr_trie(...) */

/**
 *@brief put a pointer to the string value with given key in the targeted hash table [TRIE HASH TABLE)
 *@param table targeted hash table
 *@param key associated value's key
 *@param ptr pointer value to put
 *@param destructor the destructor func for ptr or NULL
 *@param duplicator the duplicate func for ptr or NULL
 *@return TRUE or FALSE
 */
int _ht_put_ptr_trie(HASH_TABLE* table, const char* key, void* ptr, void (*destructor)(void* ptr),void* (*duplicator)(void* ptr)) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    HASH_NODE* node = table->root;

    for (size_t it = 0; key[it] != '\0'; it++) {
        size_t index = ((size_t)key[it] - table->alphabet_offset) % table->alphabet_length;
        if (index >= table->alphabet_length) {
            n_log(LOG_ERR, "Invalid value %u for charater at position %d of %s, set to 0", index, it, key);
            index = 0;
        }
        if (node->children[index] == NULL) {
            /* create a node */
            node->children[index] = _ht_new_node_trie(table, key[it]);
            __n_assert(node->children[index], return FALSE);
        } else {
            /* nothing to do since node is existing */
        }
        /* go down a level, to the child referenced by index */
        node = node->children[index];
    }
    /* At the end of the key, mark this node as the leaf node */
    node->is_leaf = 1;
    /* Put the key */
    node->key = strdup(key);
    /* Put the value */
    node->data.ptr = ptr;
    node->destroy_func = destructor;
    node->duplicate_func = duplicator;
    node->type = HASH_PTR;

    table->nb_keys++;

    return TRUE;
} /* _ht_put_ptr_trie(...) */

/**
 *@brief retrieve a HASH_NODE at key from table
 *@param table targeted hash table
 *@param key associated value's key
 *@return a HASH_NODE *node or NULL
 */
HASH_NODE* _ht_get_node_trie(HASH_TABLE* table, const char* key) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    HASH_NODE* node = NULL;

    if (key[0] != '\0') {
        node = table->root;
        for (size_t it = 0; key[it] != '\0'; it++) {
            size_t index = (size_t)key[it] - table->alphabet_offset;
            if (index >= table->alphabet_length) {
                n_log(LOG_DEBUG, "Invalid value %d for charater at index %d of %s, set to 0", index, it, key);
                return NULL;
            }
            if (node->children[index] == NULL) {
                /* not found */
                return NULL;
            }
            node = node->children[index];
        }
    } else {
        node = NULL;
    }
    return node;
} /* _ht_get_node_trie(...) */

/**
 *@brief Retrieve an integral value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val a pointer to a destination integer
 *@return TRUE or FALSE.
 */
int _ht_get_int_trie(HASH_TABLE* table, const char* key, HASH_INT_TYPE* val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node_trie(table, key);

    if (!node || !node->is_leaf)
        return FALSE;

    if (node->type != HASH_INT) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.ival;

    return TRUE;
} /* _ht_get_int_trie() */

/**
 *@brief Retrieve an double value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val a pointer to a destination double
 *@return TRUE or FALSE.
 */
int _ht_get_double_trie(HASH_TABLE* table, const char* key, double* val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node_trie(table, key);

    if (!node || !node->is_leaf)
        return FALSE;

    if (node->type != HASH_DOUBLE) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.fval;

    return TRUE;
} /* _ht_get_double_trie() */

/**
 *@brief Retrieve an char *string value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val a pointer to a destination char *string
 *@return TRUE or FALSE.
 */
int _ht_get_string_trie(HASH_TABLE* table, const char* key, char** val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node_trie(table, key);

    if (!node || !node->is_leaf)
        return FALSE;

    if (node->type != HASH_STRING) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.string;

    return TRUE;
} /* _ht_get_string_trie() */

/**
 *@brief Retrieve a pointer value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val a pointer to a destination pointer
 *@return TRUE or FALSE.
 */
int _ht_get_ptr_trie(HASH_TABLE* table, const char* key, void** val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node_trie(table, key);

    if (!node || !node->is_leaf)
        return FALSE;

    if (node->type != HASH_PTR) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_PTR , key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.ptr;

    return TRUE;
} /* _ht_get_ptr_trie() */

/**
 *@brief Empty a TRIE hash table
 *@param table targeted hash table
 *@return TRUE or FALSE.
 */
int _empty_ht_trie(HASH_TABLE* table) {
    __n_assert(table, return FALSE);

    _ht_node_destroy(table->root);

    table->root = _ht_new_node_trie(table, '\0');

    table->nb_keys = 0;
    return TRUE;
} /* _empty_ht_trie */

/**
 *@brief Free and set the table to NULL (TRIE mode)
 *@param table targeted hash table
 *@return TRUE or FALSE.
 */
int _destroy_ht_trie(HASH_TABLE** table) {
    __n_assert(table && (*table), n_log(LOG_ERR, "Can't destroy table: already NULL"); return FALSE);

    _ht_node_destroy((*table)->root);

    Free((*table));

    return TRUE;
} /* _destroy_ht_trie */

/**
 *@brief Recursive function to print trie tree's keys and values
 *@param table targeted hash table
 *@param node current node
 */
void _ht_print_trie_helper(HASH_TABLE* table, HASH_NODE* node) {
    if (!node)
        return;

    if (node->is_leaf) {
        printf("key: %s, val: ", node->key);
        switch (node->type) {
            case HASH_INT:
                printf("int: %ld", (long)node->data.ival);
                break;
            case HASH_DOUBLE:
                printf("double: %f", node->data.fval);
                break;
            case HASH_PTR:
                printf("ptr: %p", node->data.ptr);
                break;
            case HASH_STRING:
                printf("%s", node->data.string);
                break;
            default:
                printf("unknwow type %d", node->type);
                break;
        }
        printf("\n");
    }
    for (size_t it = 0; it < table->alphabet_length; it++) {
        _ht_print_trie_helper(table, node->children[it]);
    }
} /* _ht_print_trie_helper(...) */

/**
 *@brief Generic print func call for trie trees
 *@param table targeted hash table
 */
void _ht_print_trie(HASH_TABLE* table) {
    __n_assert(table, return);
    __n_assert(table->root, return);

    HASH_NODE* node = table->root;

    _ht_print_trie_helper(table, node);

    return;
} /* _ht_print_trie(...) */

/**
 *@brief Recursive function to search tree's keys and apply a matching func to put results in the list
 *@param results targeted and initialized LIST in which the matching nodes will be put
 *@param node targeted starting trie node
 *@param node_is_matching pointer to a matching function to use
 */
void _ht_search_trie_helper(LIST* results, HASH_NODE* node, int (*node_is_matching)(HASH_NODE* node)) {
    if (!node)
        return;

    if (node->is_leaf) {
        if (node_is_matching(node) == TRUE) {
            list_push(results, strdup(node->key), &free);
        }
    }
    for (size_t it = 0; it < node->alphabet_length; it++) {
        _ht_search_trie_helper(results, node->children[it], node_is_matching);
    }
}

/**
 *@brief Search tree's keys and apply a matching func to put results in the list
 *@param table targeted table
 *@param node_is_matching pointer to a matching function to use
 *@return NULL or a LIST *list of HASH_NODE *elements
 */
LIST* _ht_search_trie(HASH_TABLE* table, int (*node_is_matching)(HASH_NODE* node)) {
    __n_assert(table, return NULL);

    LIST* results = new_generic_list(MAX_LIST_ITEMS);
    __n_assert(results, return NULL);

    _ht_search_trie_helper(results, table->root, node_is_matching);

    if (results->nb_items < 1)
        list_destroy(&results);

    return results;
} /* _ht_search_trie(...) */

/**
 *@brief recursive, helper for ht_get_completion_list, get the list of leaf starting from node
 *@param node starting node
 *@param results initialized LIST * for the matching NODES
 *@return TRUE or FALSE
 */
int _ht_depth_first_search(HASH_NODE* node, LIST* results) {
    __n_assert(results, return FALSE);

    if (!node)
        return FALSE;

    for (size_t it = 0; it < node->alphabet_length; it++) {
        _ht_depth_first_search(node->children[it], results);
    }
    if (node->is_leaf) {
        if (results->nb_items < results->nb_max_items) {
            return list_push(results, strdup(node->key), &free);
        }
        return TRUE;
    }
    return TRUE;
} /* _ht_depth_first_search(...) */

/************ CLASSIC HASH TABLE ************/
/*! 64 bit rotate left */
#define ROTL64(x, r) (((x) << (r)) | ((x) >> (64 - (r))))
/*! 32 bit rotate left */
#define ROTL32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))
/*! max unsigned long long */
#define BIG_CONSTANT(x) (x##LLU)

/* NO-OP for little-endian platforms */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BYTESWAP32(x) (x)
#define BYTESWAP64(x) (x)
#endif
/* if __BYTE_ORDER__ is not predefined (like FreeBSD), use arch */
#elif defined(__i386) || defined(__x86_64) || defined(__alpha) || defined(__vax)

#define BYTESWAP32(x) (x)
#define BYTESWAP64(x) (x)
/* use __builtin_bswap32 if available */
#elif defined(__GNUC__) || defined(__clang__)
#ifdef __has_builtin
#if __has_builtin(__builtin_bswap32)
#define BYTESWAP32(x) __builtin_bswap32(x)
#endif  // __has_builtin(__builtin_bswap32)
#if __has_builtin(__builtin_bswap64)
#define BYTESWAP64(x) __builtin_bswap64(x)
#endif  // __has_builtin(__builtin_bswap64)
#endif  // __has_builtin
#endif  // defined(__GNUC__) || defined(__clang__)
/* last resort (big-endian w/o __builtin_bswap) */
#ifndef BYTESWAP32
/*! 32 bits bytes swap */
#define BYTESWAP32(x) ((((x) & 0xFF) << 24) | (((x) >> 24) & 0xFF) | (((x) & 0x0000FF00) << 8) | (((x) & 0x00FF0000) >> 8))
#endif
#ifndef BYTESWAP64
/*! 32 bits bytes swap */
#define BYTESWAP64(x)                                  \
    (((uint64_t)(x) << 56) |                           \
     (((uint64_t)(x) << 40) & 0X00FF000000000000ULL) | \
     (((uint64_t)(x) << 24) & 0X0000FF0000000000ULL) | \
     (((uint64_t)(x) << 8) & 0X000000FF00000000ULL) |  \
     (((uint64_t)(x) >> 8) & 0X00000000FF000000ULL) |  \
     (((uint64_t)(x) >> 24) & 0X0000000000FF0000ULL) | \
     (((uint64_t)(x) >> 40) & 0X000000000000FF00ULL) | \
     ((uint64_t)(x) >> 56))
#endif

/**
 *@brief Block read - modified from murmur's author, ajusted byte endianess
 *@param p value
 *@param i position
 *@return value at position inside block
 */
FORCE_INLINE uint32_t getblock32(const uint32_t* p, const size_t i) {
    uint32_t result;
    memcpy(&result, (const uint8_t*)p + i * 4, sizeof(result));
    return BYTESWAP32(result);
}

/**
 *@brief Block read - modified from murmur's author, ajusted byte endianess
 *@param p value
 *@param i position
 *@return value at position inside block
 */
FORCE_INLINE uint64_t getblock64(const uint64_t* p, const size_t i) {
    uint64_t result;
    memcpy(&result, (const uint8_t*)p + i * 8, sizeof(result));
    return BYTESWAP64(result);
}

/**
 *@brief Finalization mix - force all bits of a hash block to avalanche (from murmur's author)
 *@param h value
 *@return mixed value
 */
FORCE_INLINE uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
} /* fmix32(...) */

/**
 *@brief Finalization mix - force all bits of a hash block to avalanche (from murmur's author)
 *@param k value
 *@return mixed value
 */
FORCE_INLINE uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= BIG_CONSTANT(0xff51afd7ed558ccd);
    k ^= k >> 33;
    k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
    k ^= k >> 33;

    return k;
}

/**
  MurmurHash3 was written by Austin Appleby, and is placed in the public
  domain. The author hereby disclaims copyright to this source code.
  Note - The x86 and x64 versions do _not_ produce the same results, as the
  algorithms are optimized for their respective platforms. You can still
  compile and run any of them on any platform, but your performance with the
  non-native version will be less than optimal.
 *@param key char *string as the key
 *@param len size of the key
 *@param seed seed value for murmur hash
 *@param out generated hash
 */
// Safe forward-indexing version
void MurmurHash3_x86_32(const void* key, const size_t len, const uint32_t seed, void* out) {
    const uint8_t* data = (const uint8_t*)key;
    const size_t nblocks = len / 4;

    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    const uint32_t* blocks = (const uint32_t*)data;

    for (size_t i = 0; i < nblocks; i++) {
        uint32_t k1 = getblock32(blocks, i);
        k1 *= c1;
        k1 = ROTL32(k1, 15);
        k1 *= c2;

        h1 ^= k1;
        h1 = ROTL32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    const uint8_t* tail = data + nblocks * 4;
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3:
            k1 ^= (uint32_t)tail[2] << 16; /* fall through */
            FALL_THROUGH;
        case 2:
            k1 ^= (uint32_t)tail[1] << 8; /* fall through */
            FALL_THROUGH;
        case 1:
            k1 ^= (uint32_t)tail[0];
            k1 *= c1;
            k1 = ROTL32(k1, 15);
            k1 *= c2;
            h1 ^= k1;
            break;
        default:
            break;
    }

    h1 ^= (uint32_t)len;
    h1 = fmix32(h1);
    *(uint32_t*)out = h1;
} /* MurmurHash3_x86_32 */

/**
  MurmurHash3 was written by Austin Appleby, and is placed in the public
  domain. The author hereby disclaims copyright to this source code.
  Note - The x86 and x64 versions do _not_ produce the same results, as the
  algorithms are optimized for their respective platforms. You can still
  compile and run any of them on any platform, but your performance with the
  non-native version will be less than optimal.
 *@param key char *string as the key
 *@param len size of the key
 *@param seed seed value for murmur hash
 *@param out generated hash
 */
void MurmurHash3_x86_128(const void* key, const size_t len, const uint32_t seed, void* out) {
    const uint8_t* data = (const uint8_t*)key;
    const size_t nblocks = len / 16;

    uint32_t h1 = seed;
    uint32_t h2 = seed;
    uint32_t h3 = seed;
    uint32_t h4 = seed;

    const uint32_t c1 = 0x239b961b;
    const uint32_t c2 = 0xab0e9789;
    const uint32_t c3 = 0x38b34ae5;
    const uint32_t c4 = 0xa1e38b93;

    const uint32_t* blocks = (const uint32_t*)data;

    for (size_t i = 0; i < nblocks; i++) {
        uint32_t k1 = getblock32(blocks, i * 4 + 0);
        uint32_t k2 = getblock32(blocks, i * 4 + 1);
        uint32_t k3 = getblock32(blocks, i * 4 + 2);
        uint32_t k4 = getblock32(blocks, i * 4 + 3);

        k1 *= c1;
        k1 = ROTL32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        h1 = ROTL32(h1, 19);
        h1 += h2;
        h1 = h1 * 5 + 0x561ccd1b;

        k2 *= c2;
        k2 = ROTL32(k2, 16);
        k2 *= c3;
        h2 ^= k2;
        h2 = ROTL32(h2, 17);
        h2 += h3;
        h2 = h2 * 5 + 0x0bcaa747;

        k3 *= c3;
        k3 = ROTL32(k3, 17);
        k3 *= c4;
        h3 ^= k3;
        h3 = ROTL32(h3, 15);
        h3 += h4;
        h3 = h3 * 5 + 0x96cd1c35;

        k4 *= c4;
        k4 = ROTL32(k4, 18);
        k4 *= c1;
        h4 ^= k4;
        h4 = ROTL32(h4, 13);
        h4 += h1;
        h4 = h4 * 5 + 0x32ac3b17;
    }

    // tail
    const uint8_t* tail = data + nblocks * 16;

    uint32_t k1 = 0, k2 = 0, k3 = 0, k4 = 0;

    switch (len & 15) {
        case 15:
            k4 ^= (uint32_t)tail[14] << 16; /* fall through */
            FALL_THROUGH;
        case 14:
            k4 ^= (uint32_t)tail[13] << 8; /* fall through */
            FALL_THROUGH;
        case 13:
            k4 ^= (uint32_t)tail[12];
            k4 *= c4;
            k4 = ROTL32(k4, 18);
            k4 *= c1;
            h4 ^= k4;
            /* fall through */
            FALL_THROUGH;
        case 12:
            k3 ^= (uint32_t)tail[11] << 24; /* fall through */
            FALL_THROUGH;
        case 11:
            k3 ^= (uint32_t)tail[10] << 16; /* fall through */
            FALL_THROUGH;
        case 10:
            k3 ^= (uint32_t)tail[9] << 8; /* fall through */
            FALL_THROUGH;
        case 9:
            k3 ^= (uint32_t)tail[8];
            k3 *= c3;
            k3 = ROTL32(k3, 17);
            k3 *= c4;
            h3 ^= k3;
            /* fall through */
            FALL_THROUGH;
        case 8:
            k2 ^= (uint32_t)tail[7] << 24; /* fall through */
            FALL_THROUGH;
        case 7:
            k2 ^= (uint32_t)tail[6] << 16; /* fall through */
            FALL_THROUGH;
        case 6:
            k2 ^= (uint32_t)tail[5] << 8; /* fall through */
            FALL_THROUGH;
        case 5:
            k2 ^= (uint32_t)tail[4];
            k2 *= c2;
            k2 = ROTL32(k2, 16);
            k2 *= c3;
            h2 ^= k2;
            /* fall through */
            FALL_THROUGH;
        case 4:
            k1 ^= (uint32_t)tail[3] << 24; /* fall through */
            FALL_THROUGH;
        case 3:
            k1 ^= (uint32_t)tail[2] << 16; /* fall through */
            FALL_THROUGH;
        case 2:
            k1 ^= (uint32_t)tail[1] << 8; /* fall through */
            FALL_THROUGH;
        case 1:
            k1 ^= (uint32_t)tail[0];
            k1 *= c1;
            k1 = ROTL32(k1, 15);
            k1 *= c2;
            h1 ^= k1;
            break;
        default:
            break;
    }

    // finalization
    h1 ^= (uint32_t)len;
    h2 ^= (uint32_t)len;
    h3 ^= (uint32_t)len;
    h4 ^= (uint32_t)len;

    h1 += h2 + h3 + h4;
    h2 += h1;
    h3 += h1;
    h4 += h1;

    h1 = fmix32(h1);
    h2 = fmix32(h2);
    h3 = fmix32(h3);
    h4 = fmix32(h4);

    h1 += h2 + h3 + h4;
    h2 += h1;
    h3 += h1;
    h4 += h1;

    ((uint32_t*)out)[0] = h1;
    ((uint32_t*)out)[1] = h2;
    ((uint32_t*)out)[2] = h3;
    ((uint32_t*)out)[3] = h4;
} /* MurmurHash3_x86_128(...) */

/**
  MurmurHash3 was written by Austin Appleby, and is placed in the public
  domain. The author hereby disclaims copyright to this source code.
  Note - The x86 and x64 versions do _not_ produce the same results, as the
  algorithms are optimized for their respective platforms. You can still
  compile and run any of them on any platform, but your performance with the
  non-native version will be less than optimal.
 *@param key char *string as the key
 *@param len size of the key
 *@param seed seed value for murmur hash
 *@param out generated hash
 */
void MurmurHash3_x64_128(const void* key, const size_t len, const uint64_t seed, void* out) {
    const uint8_t* data = (const uint8_t*)key;
    const size_t nblocks = len / 16;

    uint64_t h1 = seed;
    uint64_t h2 = seed;

    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;

    // Body
    const uint64_t* blocks = (const uint64_t*)data;
    for (size_t i = 0; i < nblocks; i++) {
        uint64_t k1 = getblock64(blocks, i * 2 + 0);
        uint64_t k2 = getblock64(blocks, i * 2 + 1);

        k1 *= c1;
        k1 = ROTL64(k1, 31);
        k1 *= c2;
        h1 ^= k1;

        h1 = ROTL64(h1, 27);
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;

        k2 *= c2;
        k2 = ROTL64(k2, 33);
        k2 *= c1;
        h2 ^= k2;

        h2 = ROTL64(h2, 31);
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }

    // Tail
    const uint8_t* tail = data + nblocks * 16;

    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch (len & 15) {
        case 15:
            k2 ^= ((uint64_t)tail[14]) << 48; /* fall through */
            FALL_THROUGH;
        case 14:
            k2 ^= ((uint64_t)tail[13]) << 40; /* fall through */
            FALL_THROUGH;
        case 13:
            k2 ^= ((uint64_t)tail[12]) << 32; /* fall through */
            FALL_THROUGH;
        case 12:
            k2 ^= ((uint64_t)tail[11]) << 24; /* fall through */
            FALL_THROUGH;
        case 11:
            k2 ^= ((uint64_t)tail[10]) << 16; /* fall through */
            FALL_THROUGH;
        case 10:
            k2 ^= ((uint64_t)tail[9]) << 8; /* fall through */
            FALL_THROUGH;
        case 9:
            k2 ^= ((uint64_t)tail[8]);
            k2 *= c2;
            k2 = ROTL64(k2, 33);
            k2 *= c1;
            h2 ^= k2;
            /* fall through */
            FALL_THROUGH;
        case 8:
            k1 ^= ((uint64_t)tail[7]) << 56; /* fall through */
            FALL_THROUGH;
        case 7:
            k1 ^= ((uint64_t)tail[6]) << 48; /* fall through */
            FALL_THROUGH;
        case 6:
            k1 ^= ((uint64_t)tail[5]) << 40; /* fall through */
            FALL_THROUGH;
        case 5:
            k1 ^= ((uint64_t)tail[4]) << 32; /* fall through */
            FALL_THROUGH;
        case 4:
            k1 ^= ((uint64_t)tail[3]) << 24; /* fall through */
            FALL_THROUGH;
        case 3:
            k1 ^= ((uint64_t)tail[2]) << 16; /* fall through */
            FALL_THROUGH;
        case 2:
            k1 ^= ((uint64_t)tail[1]) << 8; /* fall through */
            FALL_THROUGH;
        case 1:
            k1 ^= ((uint64_t)tail[0]);
            k1 *= c1;
            k1 = ROTL64(k1, 31);
            k1 *= c2;
            h1 ^= k1;
            break;
        default:
            break;
    }

    // Finalization
    h1 ^= len;
    h2 ^= len;

    h1 += h2;
    h2 += h1;

    h1 = fmix64(h1);
    h2 = fmix64(h2);

    h1 += h2;
    h2 += h1;

    ((uint64_t*)out)[0] = h1;
    ((uint64_t*)out)[1] = h2;
} /* MurmurHash3_x64_128()*/

/**
 *@brief get the type of a node , text version
 *@param node node to check
 *@return NULL or the associated node type
 */
char* ht_node_type(HASH_NODE* node) {
    static char* HASH_TYPE_STR[5] = {"HASH_INT", "HASH_DOUBLE", "HASH_STRING", "HASH_PTR", "HASH_UNKNOWN"};

    __n_assert(node, return NULL);

    if (node->type >= 0 && node->type < HASH_UNKNOWN)
        return HASH_TYPE_STR[node->type];

    return NULL;
} /* ht_node_type(...) */

/**
 *@brief return the associated key's node inside the hash_table
 *@param table targeted table
 *@param key Associated value's key
 *@return The found node, or NULL
 */
HASH_NODE* _ht_get_node(HASH_TABLE* table, const char* key) {
    HASH_VALUE hash_value[2] = {0, 0};
    size_t index = 0;

    HASH_NODE* node_ptr = NULL;

    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    if (key[0] == '\0')
        return NULL;

    MurmurHash(key, strlen(key), table->seed, &hash_value);
    index = (hash_value[0]) % (table->size);

    if (!table->hash_table[index]->start)
        return NULL;

    list_foreach(list_node, table->hash_table[index]) {
        node_ptr = (HASH_NODE*)list_node->ptr;
        if (!strcmp(key, node_ptr->key)) {
            return node_ptr;
        }
    }
    return NULL;
} /* _ht_get_node() */

/**
 *@brief node creation, HASH_CLASSIC mode
 *@param table targeted table
 *@param key key of new node
 *@return NULL or a new HASH_NODE *
 */
HASH_NODE* _ht_new_node(HASH_TABLE* table, const char* key) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    HASH_NODE* new_hash_node = NULL;

    HASH_VALUE hash_value[2] = {0, 0};

    if (strlen(key) == 0)
        return NULL;

    MurmurHash(key, strlen(key), table->seed, &hash_value);

    Malloc(new_hash_node, HASH_NODE, 1);
    __n_assert(new_hash_node, n_log(LOG_ERR, "Could not allocate new_hash_node"); return NULL);
    new_hash_node->key = strdup(key);
    new_hash_node->key_id = '\0';
    __n_assert(new_hash_node->key, n_log(LOG_ERR, "Could not allocate new_hash_node->key"); Free(new_hash_node); return NULL);
    new_hash_node->hash_value = hash_value[0];
    new_hash_node->data.ptr = NULL;
    new_hash_node->destroy_func = NULL;
    new_hash_node->children = NULL;
    new_hash_node->is_leaf = 0;
    new_hash_node->need_rehash = 0;
    new_hash_node->alphabet_length = 0;

    return new_hash_node;
} /* _ht_new_node */

/**
 *@brief node creation, HASH_CLASSIC mode
 *@param table targeted table
 *@param key key of new node
 *@param value int value of key
 *@return NULL or a new HASH_NODE *
 */
HASH_NODE* _ht_new_int_node(HASH_TABLE* table, const char* key, HASH_INT_TYPE value) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    HASH_NODE* new_hash_node = NULL;
    new_hash_node = _ht_new_node(table, key);
    if (new_hash_node) {
        new_hash_node->data.ival = value;
        new_hash_node->type = HASH_INT;
    } else {
        n_log(LOG_ERR, "Could not get a new node in table %p with key %s", table, key);
    }
    return new_hash_node;
} /* _ht_new_int_node */

/**
 *@brief node creation, HASH_CLASSIC mode
 *@param table targeted table
 *@param key key of new node
 *@param value double value of key
 *@return NULL or a new HASH_NODE *
 */
HASH_NODE* _ht_new_double_node(HASH_TABLE* table, const char* key, double value) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    HASH_NODE* new_hash_node = NULL;
    new_hash_node = _ht_new_node(table, key);
    if (new_hash_node) {
        new_hash_node->data.fval = value;
        new_hash_node->type = HASH_DOUBLE;
    } else {
        n_log(LOG_ERR, "Could not get a new node in table %p with key %s", table, key);
    }
    return new_hash_node;
} /* _ht_new_double_node */

/**
 *@brief node creation, HASH_CLASSIC mode, strdup of value
 *@param table targeted table
 *@param key key of new node
 *@param value char *value of key
 *@return NULL or a new HASH_NODE *
 */
HASH_NODE* _ht_new_string_node(HASH_TABLE* table, const char* key, char* value) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    HASH_NODE* new_hash_node = NULL;
    new_hash_node = _ht_new_node(table, key);
    if (new_hash_node) {
        if (value)
            new_hash_node->data.string = strdup(value);
        else
            new_hash_node->data.string = NULL;
        new_hash_node->type = HASH_STRING;
    } else {
        n_log(LOG_ERR, "Could not get a new node in table %p with key %s", table, key);
    }
    return new_hash_node;
}

/**
 *@brief node creation, HASH_CLASSIC mode, pointer to string value
 *@param table targeted table
 *@param key key of new node
 *@param value char *value of key
 *@return NULL or a new HASH_NODE *
 */
HASH_NODE* _ht_new_string_ptr_node(HASH_TABLE* table, const char* key, char* value) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    HASH_NODE* new_hash_node = NULL;
    new_hash_node = _ht_new_node(table, key);
    if (new_hash_node) {
        new_hash_node->data.string = value;
        new_hash_node->type = HASH_STRING;
    } else {
        n_log(LOG_ERR, "Could not get a new node in table %p with key %s", table, key);
    }
    return new_hash_node;
}

/**
 *@brief node creation, HASH_CLASSIC mode, pointer to string value
 *@param table targeted table
 *@param key key of new node
 *@param value pointer data of key
 *@param destructor function pointer to a destructor of value type
 *@return NULL or a new HASH_NODE *
 */
HASH_NODE* _ht_new_ptr_node(HASH_TABLE* table, const char* key, void* value, void (*destructor)(void* ptr),void* (*duplicator)(void* ptr)) {
    __n_assert(table, return NULL);
    __n_assert(key, return NULL);

    HASH_NODE* new_hash_node = NULL;
    new_hash_node = _ht_new_node(table, key);
    if (new_hash_node) {
        new_hash_node->data.ptr = value;
        new_hash_node->destroy_func = destructor;
        new_hash_node->duplicate_func = duplicator;
        new_hash_node->type = HASH_PTR;
    } else {
        n_log(LOG_ERR, "Could not get a new node in table %p with key %s", table, key);
    }
    return new_hash_node;
}

/**
 *@brief put an integral value with given key in the targeted hash table [CLASSIC HASH TABLE)
 *@param table targeted table
 *@param key associated value's key
 *@param value integral value to put
 *@return TRUE or FALSE
 */
int _ht_put_int(HASH_TABLE* table, const char* key, HASH_INT_TYPE value) {
    HASH_NODE* node_ptr = NULL;

    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    if ((node_ptr = _ht_get_node(table, key))) {
        if (node_ptr->type == HASH_INT) {
            node_ptr->data.ival = value;
            return TRUE;
        }
        n_log(LOG_ERR, "Can't add key[\"%s\"] with type HASH_INT, key already exist with type %s", node_ptr->key, ht_node_type(node_ptr));
        return FALSE; /* key registered with another data type */
    }

    int retcode = FALSE;
    node_ptr = _ht_new_int_node(table, key, value);
    if (node_ptr) {
        size_t index = (node_ptr->hash_value) % (table->size);
        retcode = list_push(table->hash_table[index], node_ptr, &_ht_node_destroy);
        if (retcode == TRUE) {
            table->nb_keys++;
        }
    }
    return retcode;
} /*_ht_put_int() */

/**
 *@brief put a double value with given key in the targeted hash table
 *@param table targeted hash table
 *@param key associated value's key
 *@param value double value to put
 *@return TRUE or FALSE
 */
int _ht_put_double(HASH_TABLE* table, const char* key, double value) {
    HASH_NODE* node_ptr = NULL;

    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    if ((node_ptr = _ht_get_node(table, key))) {
        if (node_ptr->type == HASH_DOUBLE) {
            node_ptr->data.fval = value;
            return TRUE;
        }
        n_log(LOG_ERR, "Can't add key[\"%s\"] with type HASH_DOUBLE, key already exist with type %s", node_ptr->key, ht_node_type(node_ptr));
        return FALSE; /* key registered with another data type */
    }

    int retcode = FALSE;
    node_ptr = _ht_new_double_node(table, key, value);
    if (node_ptr) {
        HASH_VALUE index = (node_ptr->hash_value) % (table->size);
        retcode = list_push(table->hash_table[index], node_ptr, &_ht_node_destroy);
        if (retcode == TRUE) {
            table->nb_keys++;
        }
    }
    return retcode;
} /*_ht_put_double()*/

/**
 *@brief put a pointer value with given key in the targeted hash table
 *@param table targeted hash table
 *@param key Associated value's key
 *@param ptr pointer value to put
 *@param destructor Pointer to the ptr type destructor function. Leave to NULL if there isn't
 *@return TRUE or FALSE
 */
int _ht_put_ptr(HASH_TABLE* table, const char* key, void* ptr, void (*destructor)(void* ptr), void* (*duplicator)(void* ptr)) {
    HASH_NODE* node_ptr = NULL;

    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    if ((node_ptr = _ht_get_node(table, key))) {
        /* let's check the key isn't already assigned with another data type */
        if (node_ptr->type == HASH_PTR) {
            if (node_ptr->destroy_func) {
                node_ptr->destroy_func(node_ptr->data.ptr);
                node_ptr->data.ptr = ptr;
                node_ptr->destroy_func = destructor;
                node_ptr->duplicate_func = duplicator;
            }
            return TRUE;
        }
        n_log(LOG_ERR, "Can't add key[\"%s\"] with type HASH_PTR , key already exist with type %s", node_ptr->key, ht_node_type(node_ptr));
        return FALSE; /* key registered with another data type */
    }

    int retcode = FALSE;
    node_ptr = _ht_new_ptr_node(table, key, ptr, destructor, duplicator);
    if (node_ptr) {
        HASH_VALUE index = (node_ptr->hash_value) % (table->size);
        retcode = list_push(table->hash_table[index], node_ptr, &_ht_node_destroy);
        if (retcode == TRUE) {
            table->nb_keys++;
        }
    }
    return retcode;
} /* _ht_put_ptr() */

/**
 *@brief put a null terminated char *string with given key in the targeted hash table (copy of string)
 *@param table targeted hash table
 *@param key Associated value's key
 *@param string string value to put (will be strdup'ed)
 *@return TRUE or FALSE
 */
int _ht_put_string(HASH_TABLE* table, const char* key, char* string) {
    HASH_NODE* node_ptr = NULL;

    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    if ((node_ptr = _ht_get_node(table, key))) {
        /* let's check the key isn't already assigned with another data type */
        if (node_ptr->type == HASH_STRING) {
            Free(node_ptr->data.string);
            node_ptr->data.string = strdup(string);
            return TRUE;
        }
        n_log(LOG_ERR, "Can't add key[\"%s\"] with type HASH_PTR , key already exist with type %s", node_ptr->key, ht_node_type(node_ptr));
        return FALSE; /* key registered with another data type */
    }

    int retcode = FALSE;
    node_ptr = _ht_new_string_node(table, key, string);
    if (node_ptr) {
        HASH_VALUE index = (node_ptr->hash_value) % (table->size);
        retcode = list_push(table->hash_table[index], node_ptr, &_ht_node_destroy);
        if (retcode == TRUE) {
            table->nb_keys++;
        }
    }
    return retcode;
} /*_ht_put_string */

/**
 *@brief put a null terminated char *string with given key in the targeted hash table (pointer)
 *@param table targeted hash table
 *@param key Associated value's key
 *@param string The string to put
 *@return TRUE or FALSE
 */
int _ht_put_string_ptr(HASH_TABLE* table, const char* key, char* string) {
    HASH_NODE* node_ptr = NULL;

    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    if ((node_ptr = _ht_get_node(table, key))) {
        /* let's check the key isn't already assigned with another data type */
        if (node_ptr->type == HASH_STRING) {
            Free(node_ptr->data.string);
            node_ptr->data.string = string;
            return TRUE;
        }
        n_log(LOG_ERR, "Can't add key[\"%s\"] with type HASH_PTR , key already exist with type %s", node_ptr->key, ht_node_type(node_ptr));
        return FALSE; /* key registered with another data type */
    }

    int retcode = FALSE;
    node_ptr = _ht_new_string_node(table, key, string);
    if (node_ptr) {
        HASH_VALUE index = (node_ptr->hash_value) % (table->size);
        retcode = list_push(table->hash_table[index], node_ptr, &_ht_node_destroy);
        if (retcode == TRUE) {
            table->nb_keys++;
        }
    }
    return retcode;
} /*_ht_put_string_ptr */

/**
 *@brief Retrieve an integral value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val A pointer to a destination integer
 *@return TRUE or FALSE.
 */
int _ht_get_int(HASH_TABLE* table, const char* key, HASH_INT_TYPE* val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node(table, key);

    if (!node)
        return FALSE;

    if (node->type != HASH_INT) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_INT, key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.ival;

    return TRUE;
} /* _ht_get_int() */

/**
 *@brief Retrieve a double value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val A pointer to a destination double
 *@return TRUE or FALSE.
 */
int _ht_get_double(HASH_TABLE* table, const char* key, double* val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);

    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node(table, key);

    if (!node)
        return FALSE;

    if (node->type != HASH_DOUBLE) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_DOUBLE, key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.fval;

    return TRUE;
} /* _ht_get_double()*/

/**
 *@brief Retrieve a pointer value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val A pointer to an empty pointer store
 *@return TRUE or FALSE.
 */
int _ht_get_ptr(HASH_TABLE* table, const char* key, void** val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node(table, key);
    if (!node)
        return FALSE;

    if (node->type != HASH_PTR) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_PTR, key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.ptr;

    return TRUE;
} /* _ht_get_ptr() */

/**
 *@brief Retrieve a char *string value in the hash table, at the given key. Leave val untouched if key is not found.
 *@param table targeted hash table
 *@param key associated value's key
 *@param val A pointer to an empty destination char *string
 *@return TRUE or FALSE.
 */
int _ht_get_string(HASH_TABLE* table, const char* key, char** val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    HASH_NODE* node = _ht_get_node(table, key);
    if (!node)
        return FALSE;

    if (node->type != HASH_STRING) {
        n_log(LOG_ERR, "Can't get key[\"%s\"] of type HASH_STRING, key is type %s", key, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.string;

    return TRUE;
} /* _ht_get_string() */

/**
 *@brief Remove a key from a hash table
 *@param table targeted hash table
 *@param key Key to remove
 *@return TRUE or FALSE.
 */
int _ht_remove(HASH_TABLE* table, const char* key) {
    HASH_VALUE hash_value[2] = {0, 0};
    size_t index = 0;

    HASH_NODE* node_ptr = NULL;
    LIST_NODE* node_to_kill = NULL;

    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    if (strlen(key) == 0)
        return FALSE;

    MurmurHash(key, strlen(key), table->seed, &hash_value);
    index = (hash_value[0]) % (table->size);

    if (!table->hash_table[index]->start) {
        n_log(LOG_ERR, "Can't remove key[\"%s\"], table is empty", key);
        return FALSE;
    }

    list_foreach(list_node, table->hash_table[index]) {
        node_ptr = (HASH_NODE*)list_node->ptr;
        /* if we found the same */
        if (!strcmp(key, node_ptr->key)) {
            node_to_kill = list_node;
            break;
        }
    }
    if (node_to_kill) {
        node_ptr = remove_list_node(table->hash_table[index], node_to_kill, HASH_NODE);
        _ht_node_destroy(node_ptr);

        table->nb_keys--;

        return TRUE;
    }
    n_log(LOG_ERR, "Can't delete key[\"%s\"]: inexisting key", key);
    return FALSE;
} /* ht_remove() */

/**
 *@brief Empty a hash table (CLASSIC mode)
 *@param table targeted hash table
 *@return TRUE or FALSE.
 */
int _empty_ht(HASH_TABLE* table) {
    HASH_NODE* hash_node = NULL;

    __n_assert(table, return FALSE);

    HASH_VALUE index = 0;
    for (index = 0; index < table->size; index++) {
        while (table->hash_table[index] && table->hash_table[index]->start) {
            hash_node = remove_list_node(table->hash_table[index], table->hash_table[index]->start, HASH_NODE);
            _ht_node_destroy(hash_node);
        }
    }
    table->nb_keys = 0;
    return TRUE;
} /* empty_ht */

/**
 *@brief Free and set the table to NULL
 *@param table targeted hash table
 *@return TRUE or FALSE.
 */
int _destroy_ht(HASH_TABLE** table) {
    __n_assert(table && (*table), n_log(LOG_ERR, "Can't destroy table: already NULL"); return FALSE);

    if ((*table)->hash_table) {
        // empty_ht( (*table) );

        HASH_VALUE it = 0;
        for (it = 0; it < (*table)->size; it++) {
            if ((*table)->hash_table[it])
                list_destroy(&(*table)->hash_table[it]);
        }
        Free((*table)->hash_table);
    }
    Free((*table));
    return TRUE;
} /* _destroy_ht */

/**
 *@brief Generic print func call for classic hash tables
 *@param table targeted hash table
 */
void _ht_print(HASH_TABLE* table) {
    __n_assert(table, return);
    __n_assert(table->hash_table, return);
    ht_foreach(node, table) {
        printf("key:%s node:%s\n", ((HASH_NODE*)node->ptr)->key, ((HASH_NODE*)node->ptr)->key);
    }

    return;
} /* _ht_print(...) */

/**
 *@brief Search hash table's keys and apply a matching func to put results in the list
 *@param table targeted table
 *@param node_is_matching pointer to a matching function to use
 *@return NULL or a LIST *list of HASH_NODE *elements
 */
LIST* _ht_search(HASH_TABLE* table, int (*node_is_matching)(HASH_NODE* node)) {
    __n_assert(table, return NULL);

    LIST* results = new_generic_list(MAX_LIST_ITEMS);
    __n_assert(results, return NULL);

    ht_foreach(node, table) {
        HASH_NODE* hnode = (HASH_NODE*)node->ptr;
        if (node_is_matching(hnode) == TRUE) {
            list_push(results, strdup(hnode->key), &free);
        }
    }

    if (results->nb_items < 1)
        list_destroy(&results);

    return results;
} /* _ht_search(...) */

/************ HASH_TABLES FUNCTION POINTERS AND COMMON TABLE TYPE FUNCS ************/

/**
 *@brief create a TRIE hash table with the alphabet_size, each key value beeing decreased by alphabet_offset
 *@param alphabet_length of the alphabet
 *@param alphabet_offset offset of each character in a key (i.e: to have 'a->z' with 'a' starting at zero, offset must be 32.
 *@return NULL or the new allocated hash table
 */
HASH_TABLE* new_ht_trie(size_t alphabet_length, size_t alphabet_offset) {
    HASH_TABLE* table = NULL;

    Malloc(table, HASH_TABLE, 1);
    __n_assert(table, n_log(LOG_ERR, "Error allocating HASH_TABLE *table"); return NULL);

    table->size = 0;
    table->seed = 0;
    table->nb_keys = 0;
    errno = 0;
    table->hash_table = NULL;

    table->ht_put_int = _ht_put_int_trie;
    table->ht_put_double = _ht_put_double_trie;
    table->ht_put_ptr = _ht_put_ptr_trie;
    table->ht_put_string = _ht_put_string_trie;
    table->ht_put_string_ptr = _ht_put_string_ptr_trie;
    table->ht_get_int = _ht_get_int_trie;
    table->ht_get_double = _ht_get_double_trie;
    table->ht_get_string = _ht_get_string_trie;
    table->ht_get_ptr = _ht_get_ptr_trie;
    table->ht_remove = _ht_remove_trie;
    table->ht_search = _ht_search_trie;
    table->empty_ht = _empty_ht_trie;
    table->destroy_ht = _destroy_ht_trie;
    table->ht_print = _ht_print_trie;

    table->alphabet_length = alphabet_length;
    table->alphabet_offset = alphabet_offset;

    table->root = _ht_new_node_trie(table, '\0');
    if (!table->root) {
        n_log(LOG_ERR, "Couldn't allocate new_ht_trie with alphabet_length of %zu and alphabet offset of %d", alphabet_length, alphabet_offset);
        Free(table);
        return NULL;
    }
    table->mode = HASH_TRIE;

    return table;
} /* new_ht_trie */

/**
 *@brief Create a hash table with the given size
 *@param size Size of the root hash node table
 *@return NULL or the new allocated hash table
 */
HASH_TABLE* new_ht(size_t size) {
    HASH_TABLE* table = NULL;

    if (size < 1) {
        n_log(LOG_ERR, "Invalide size %d for new_ht()", size);
        return NULL;
    }
    Malloc(table, HASH_TABLE, 1);
    __n_assert(table, n_log(LOG_ERR, "Error allocating HASH_TABLE *table"); return NULL);

    table->size = size;
    table->seed = (uint32_t)rand() % 100000;
    table->nb_keys = 0;
    errno = 0;
    Malloc(table->hash_table, LIST*, size);
    // table -> hash_table = (LIST **)calloc( size, sizeof( LIST *) );
    __n_assert(table->hash_table, n_log(LOG_ERR, "Can't allocate table -> hash_table with size %d !", size); Free(table); return NULL);

    size_t it = 0;
    for (it = 0; it < size; it++) {
        table->hash_table[it] = new_generic_list(MAX_LIST_ITEMS);
        // if no valid table then unroll previsouly created slots
        if (!table->hash_table[it]) {
            n_log(LOG_ERR, "Can't allocate table -> hash_table[ %d ] !", it);
            size_t it_delete = 0;
            for (it_delete = 0; it_delete < it; it_delete++) {
                list_destroy(&table->hash_table[it_delete]);
            }
            Free(table->hash_table);
            Free(table);
            return NULL;
        }
    }
    table->mode = HASH_CLASSIC;

    table->ht_put_int = _ht_put_int;
    table->ht_put_double = _ht_put_double;
    table->ht_put_ptr = _ht_put_ptr;
    table->ht_put_string = _ht_put_string;
    table->ht_put_string_ptr = _ht_put_string_ptr;
    table->ht_get_int = _ht_get_int;
    table->ht_get_double = _ht_get_double;
    table->ht_get_string = _ht_get_string;
    table->ht_get_ptr = _ht_get_ptr;
    table->ht_get_node = _ht_get_node;
    table->ht_remove = _ht_remove;
    table->ht_search = _ht_search;
    table->empty_ht = _empty_ht;
    table->destroy_ht = _destroy_ht;
    table->ht_print = _ht_print;

    return table;
} /* new_ht(...) */

/**
 *@brief get node at 'key' from 'table'
 *@param table targeted table
 *@param key key to retrieve
 *@return NULL or the matching HASH_NODE *node
 */
HASH_NODE* ht_get_node(HASH_TABLE* table, const char* key) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_get_node(table, key);
} /*ht_get_node(...) */

/**
 *@brief get double at 'key' from 'table'
 *@param table targeted table
 *@param key key to retrieve
 *@param val pointer to double storage
 *@return TRUE if found, else FALSE. 'val' value is preserved if no key is matching.
 */
int ht_get_double(HASH_TABLE* table, const char* key, double* val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_get_double(table, key, val);
} /* ht_get_double(...) */

/**
 *@brief get node at 'key' from 'table'
 *@param table targeted table
 *@param key key to retrieve
 *@param val pointer to int storage
 *@return TRUE if found, else FALSE. 'val' value is preserved if no key is matching.
 */
int ht_get_int(HASH_TABLE* table, const char* key, HASH_INT_TYPE* val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_get_int(table, key, val);
} /* ht_get_int(...) */

/**
 *@brief get pointer at 'key' from 'table'
 *@param table targeted table
 *@param key key to retrieve
 *@param val pointer to pointer storage
 *@return TRUE if found, else FALSE. 'val' value is preserved if no key is matching.
 */
int ht_get_ptr(HASH_TABLE* table, const char* key, void** val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_get_ptr(table, key, &(*val));
} /* ht_get_ptr(...) */

/**
 *@brief get string at 'key' from 'table'
 *@param table targeted table
 *@param key key to retrieve
 *@param val pointer to string storage
 *@return TRUE if found, else FALSE. 'val' value is preserved if no key is matching.
 */
int ht_get_string(HASH_TABLE* table, const char* key, char** val) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_get_string(table, key, val);
} /* ht_get_string(...) */

/**
 *@brief put a double value with given key in the targeted hash table
 *@param table targeted table
 *@param key key to retrieve
 *@param value double value to put
 *@return TRUE or FALSE
 */
int ht_put_double(HASH_TABLE* table, const char* key, double value) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_put_double(table, key, value);
} /* ht_put_double(...) */

/**
 *@brief put an integral value with given key in the targeted hash table
 *@param table targeted hash table
 *@param key associated value's key
 *@param value integral value to put
 *@return TRUE or FALSE
 */
int ht_put_int(HASH_TABLE* table, const char* key, HASH_INT_TYPE value) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_put_int(table, key, value);
} /* ht_put_int(...) */

/**
 *@brief put a pointer to the string value with given key in the targeted hash table
 *@param table targeted hash table
 *@param key associated value's key
 *@param ptr pointer value to put
 *@param destructor the destructor func for ptr or NULL
 *@return TRUE or FALSE
 */
int ht_put_ptr(HASH_TABLE* table, const char* key, void* ptr, void (*destructor)(void* ptr),void* (*duplicator)(void* ptr)) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_put_ptr(table, key, ptr, destructor, duplicator);
} /* ht_put_ptr(...) */

/**
 *@brief put a string value (copy/dup) with given key in the targeted hash table
 *@param table targeted hash table
 *@param key associated value's key
 *@param string string value to put (copy)
 *@return TRUE or FALSE
 */
int ht_put_string(HASH_TABLE* table, const char* key, char* string) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_put_string(table, key, string);
} /* ht_put_string(...) */

/**
 *@brief put a string value (pointer) with given key in the targeted hash table
 *@param table targeted hash table
 *@param key associated value's key
 *@param string string value to put (pointer)
 *@return TRUE or FALSE
 */
int ht_put_string_ptr(HASH_TABLE* table, const char* key, char* string) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_put_string_ptr(table, key, string);
} /* ht_put_string_ptr(...) */

/**
 *@brief remove and delete node at key in table
 *@param table targeted hash table
 *@param key key of node to destroy
 *@return TRUE or FALSE
 */
int ht_remove(HASH_TABLE* table, const char* key) {
    __n_assert(table, return FALSE);
    __n_assert(key, return FALSE);
    return table->ht_remove(table, key);
} /* ht_remove(...) */

/**
 *@brief print contents of table
 *@param table targeted hash table
 */
void ht_print(HASH_TABLE* table) {
    __n_assert(table, return);
    table->ht_print(table);
    return;
} /* ht_print(...) */

/**
 *@brief seach table for matching nodes
 *@param table targeted hash table
 *@param node_is_matching matching function
 *@return NULL or a LIST *list of HASH_NODE *nodes
 */
LIST* ht_search(HASH_TABLE* table, int (*node_is_matching)(HASH_NODE* node)) {
    __n_assert(table, return FALSE);
    return table->ht_search(table, node_is_matching);
} /* ht_search(...) */

/**
 *@brief empty a table
 *@param table targeted hash table
 *@return TRUE or FALSE
 */
int empty_ht(HASH_TABLE* table) {
    __n_assert(table, return FALSE);
    return table->empty_ht(table);
} /* empty_ht(...) */

/**
 *@brief empty a table and destroy it
 *@param table targeted hash table
 *@return TRUE or FALSE
 */
int destroy_ht(HASH_TABLE** table) {
    __n_assert((*table), return FALSE);
    return (*table)->destroy_ht(table);
} /* destroy_ht(...) */

/**
 *@brief return the associated key's node inside the hash_table (HASH_CLASSIC only)
 *@param table Targeted hash table
 *@param hash_value Associated hash_value
 *@return The found node, or NULL
 */
HASH_NODE* ht_get_node_ex(HASH_TABLE* table, HASH_VALUE hash_value) {
    __n_assert(table, return NULL);
    __n_assert(table->mode == HASH_CLASSIC, return NULL);

    size_t index = 0;
    HASH_NODE* node_ptr = NULL;

    index = (hash_value) % (table->size);
    if (!table->hash_table[index]->start) {
        return NULL;
    }

    list_foreach(list_node, table->hash_table[index]) {
        node_ptr = (HASH_NODE*)list_node->ptr;
        if (hash_value == node_ptr->hash_value) {
            return node_ptr;
        }
    }
    return NULL;
} /* ht_get_node_ex() */

/**
 *@brief Retrieve a pointer value in the hash table, at the given key. Leave val untouched if key is not found. (HASH_CLASSIC only)
 *@param table Targeted hash table
 *@param hash_value key pre computed numeric hash value
 *@param val A pointer to an empty pointer store
 *@return TRUE or FALSE.
 */
int ht_get_ptr_ex(HASH_TABLE* table, HASH_VALUE hash_value, void** val) {
    __n_assert(table, return FALSE);
    __n_assert(table->mode == HASH_CLASSIC, return FALSE);

    HASH_NODE* node = ht_get_node_ex(table, hash_value);
    if (!node)
        return FALSE;

    if (node->type != HASH_PTR) {
        n_log(LOG_ERR, "Can't get key[\"%lld\"] of type HASH_PTR, key is type %s", hash_value, ht_node_type(node));
        return FALSE;
    }

    (*val) = node->data.ptr;

    return TRUE;
} /* ht_get_ptr_ex() */

/**
 *@brief put a pointer value with given key in the targeted hash table (HASH_CLASSIC only)
 *@param table Targeted hash table
 *@param hash_value numerical hash key
 *@param val pointer value to put
 *@param destructor Pointer to the ptr type destructor function. Leave to NULL if there isn't
 *@return TRUE or FALSE
 */
int ht_put_ptr_ex(HASH_TABLE* table, HASH_VALUE hash_value, void* val, void (*destructor)(void* ptr),void* (*duplicator)(void* ptr)) {
    __n_assert(table, return FALSE);
    __n_assert(table->mode == HASH_CLASSIC, return FALSE);

    size_t index = 0;
    HASH_NODE* new_hash_node = NULL;
    HASH_NODE* node_ptr = NULL;

    index = (hash_value) % (table->size);

    /* we have some nodes here. Let's check if the key already exists */
    list_foreach(list_node, table->hash_table[index]) {
        node_ptr = (HASH_NODE*)list_node->ptr;
        /* if we found the same key we just replace the value and return */
        if (hash_value == node_ptr->hash_value) {
            /* let's check the key isn't already assigned with another data type */
            if (node_ptr->type == HASH_PTR) {
                if (node_ptr->destroy_func) {
                    node_ptr->destroy_func(node_ptr->data.ptr);
                }
                else
                {
                    n_log(LOG_ERR, "Can't free previous key[\"%s\"] with type HASH_PTR , no hash node destroy func", node_ptr->key);
                }
                node_ptr->destroy_func = destructor;
                node_ptr->duplicate_func = duplicator;
                node_ptr->data.ptr = val;
                return TRUE;
            }
            n_log(LOG_ERR, "Can't add key[\"%s\"] with type HASH_PTR , key already exist with type %s", node_ptr->key, ht_node_type(node_ptr));
            return FALSE; /* key registered with another data type */
        }
    }

    Malloc(new_hash_node, HASH_NODE, 1);
    __n_assert(new_hash_node, n_log(LOG_ERR, "Could not allocate new_hash_node"); return FALSE);

    new_hash_node->key = NULL;
    new_hash_node->hash_value = hash_value;
    new_hash_node->data.ptr = val;
    new_hash_node->type = HASH_PTR;
    new_hash_node->destroy_func = destructor;
    new_hash_node->duplicate_func = duplicator;

    table->nb_keys++;

    return list_push(table->hash_table[index], new_hash_node, &_ht_node_destroy);
} /* ht_put_ptr_ex() */

/**
 *@brief Remove a key from a hash table (HASH_CLASSIC only)
 *@param table Targeted hash table
 *@param hash_value key pre computed numeric hash value
 *@return TRUE or FALSE.
 */
int ht_remove_ex(HASH_TABLE* table, HASH_VALUE hash_value) {
    __n_assert(table, return FALSE);
    __n_assert(table->mode == HASH_CLASSIC, return FALSE);

    size_t index = 0;
    HASH_NODE* node_ptr = NULL;
    LIST_NODE* node_to_kill = NULL;

    index = (hash_value) % (table->size);
    if (!table->hash_table[index]->start) {
        n_log(LOG_ERR, "Can't remove key[\"%d\"], table is empty", hash_value);
        return FALSE;
    }

    list_foreach(list_node, table->hash_table[index]) {
        node_ptr = (HASH_NODE*)list_node->ptr;
        /* if we found the same */
        if (hash_value == node_ptr->hash_value) {
            node_to_kill = list_node;
            break;
        }
    }
    if (node_to_kill) {
        node_ptr = remove_list_node(table->hash_table[index], node_to_kill, HASH_NODE);
        _ht_node_destroy(node_ptr);

        table->nb_keys--;

        return TRUE;
    }
    n_log(LOG_ERR, "Can't delete key[\"%d\"]: inexisting key", hash_value);
    return FALSE;
} /* ht_remove_ex() */

/**
 *@brief get next matching keys in table tree
 *@param table targeted hash table
 *@param keybud starting characters of the keys we want
 *@param max_results maximum number of matching keys in list. From UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@return NULL or a LIST *list of matching HASH_NODE *nodes
 */
LIST* ht_get_completion_list(HASH_TABLE* table, const char* keybud, size_t max_results) {
    __n_assert(table, return NULL);
    __n_assert(keybud, return NULL);

    LIST* results = new_generic_list(max_results);
    if (table->mode == HASH_TRIE) {
        HASH_NODE* node = _ht_get_node_trie(table, keybud);
        if (node) {
            if (list_push(results, strdup(keybud), &free) == TRUE) {
                _ht_depth_first_search(node, results);
            }
        } else {
            node = table->root;
            for (size_t it = 0; it < table->alphabet_length; it++) {
                if (node->children[it]) {
                    char new_keybud[3] = "";
                    new_keybud[0] = (char)(it + table->alphabet_offset);
                    list_push(results, strdup(new_keybud), &free);
                }
            }
        }
    } else if (table->mode == HASH_CLASSIC) {
        ht_foreach(node, table) {
            HASH_NODE* hnode = (HASH_NODE*)node->ptr;
            if (strncasecmp(keybud, hnode->key, strlen(keybud)) == 0) {
                char* key = strdup(hnode->key);
                if (list_push(results, key, &free) == FALSE) {
                    n_log( LOG_ERR ,"not enough space in list or memory error, key %s not pushed !" , key );
                    Free(key);
                }
            }
        }
    } else {
        n_log(LOG_ERR, "unsupported mode %d", table->mode);
        return NULL;
    }
    if (results && results->nb_items < 1)
        list_destroy(&results);
    return results;
} /* ht_get_completion_list(...) */

/**
 *@brief test if number is a prime number or not
 *@param nb number to test
 *@return TRUE or FALSE
 */
int is_prime(size_t nb) {
    /* quick test for first primes */
    if (nb <= 1) return FALSE;
    if (nb <= 3) return TRUE;

    /* skip middle five numbers in below loop */
    if ((nb % 2 == 0) || (nb % 3 == 0))
        return FALSE;

    /* looping */
    for (size_t it = 5; it * it <= nb; it = it + 6) {
        if ((nb % it == 0) || (nb % (it + 2) == 0))
            return FALSE;
    }
    return TRUE;
} /*  is_prime() */

/**
 *@brief compute next prime number after nb
 *@param nb number to test
 *@return next prime number after nb or FALSE
 */
size_t next_prime(size_t nb) {
    if (nb <= 1)
        return 2;

    size_t next_prime = nb;
    do {
        next_prime++;
    } while (is_prime(next_prime) == FALSE);

    return next_prime;
} /* next_prime() */

/**
 *@brief get table collision percentage (HASH_CLASSIC mode only)
 *@param table targeted table
 *@return table collision percentage or FALSE
 */
int ht_get_table_collision_percentage(HASH_TABLE* table) {
    __n_assert(table, return FALSE);
    if( table->mode != HASH_CLASSIC )
    {
        n_log( LOG_ERR , "unsupported table->mode (%d instead or %d)" , table->mode , HASH_CLASSIC );
        return FALSE;
    }
    if (table->size == 0) return FALSE;

    size_t nb_collisionned_lists = 0;

    for (size_t hash_it = 0; hash_it < table->size; hash_it++) {
        if (table->hash_table[hash_it] && table->hash_table[hash_it]->nb_items > 1) {
            nb_collisionned_lists++;
        }
    }
    size_t collision_percentage = (100 * nb_collisionned_lists) / table->size;
    return (int)collision_percentage;
} /* ht_get_table_collision_percentage() */

/**
 *@brief get optimal array size based on nb=(number_of_key*1.3) && if( !isprime(nb) )nb=nextprime(nb) (HASH_CLASSIC mode only)
 *@param table targeted table
 *@return 0 or optimal table size
 */
size_t ht_get_optimal_size(HASH_TABLE* table) {
    __n_assert(table, return 0);
    if( table->mode != HASH_CLASSIC )
    {
        n_log( LOG_ERR , "unsupported table->mode (%d instead or %d)" , table->mode , HASH_CLASSIC );
        return FALSE;
    }

    size_t optimum_size = (size_t)((double)table->nb_keys * 1.3);
    if (is_prime(optimum_size) != TRUE)
        optimum_size = next_prime(optimum_size);
    return optimum_size;
} /* ht_get_optimal_size() */

/**
 *@brief rehash table according to size (HASH_CLASSIC mode only)
 *@param table targeted table
 *@param size new hash table size
 *@return TRUE or FALSE
 */
int ht_resize(HASH_TABLE** table, size_t size) {
    __n_assert((*table), return FALSE);
    if( (*table)->mode != HASH_CLASSIC )
    {
        n_log( LOG_ERR , "unsupported table->mode (%d instead or %d)" , (*table)->mode , HASH_CLASSIC );
        return FALSE;
    }
    if (size < 1) {
        n_log(LOG_ERR, "invalid size %d for hash table %p", size, (*table));
        return FALSE;
    }
    HT_FOREACH(node, (*table), { node->need_rehash = 1; });

    if (size > (*table)->size) {
        Realloc((*table)->hash_table, LIST*, size);
        if (!(*table)->hash_table) {
            n_log(LOG_ERR, "Realloc did not augment the size the table !");
        } else {
            for (size_t it = (*table)->size; it < size; it++) {
                (*table)->hash_table[it] = new_generic_list(MAX_LIST_ITEMS);
                // if no valid table then unroll previsouly created slots
                if (!(*table)->hash_table[it]) {
                    n_log(LOG_ERR, "Can't allocate table -> hash_table[ %d ] !", it);
                    for (size_t it_delete = 0; it_delete < it; it_delete++) {
                        list_destroy(&(*table)->hash_table[it_delete]);
                    }
                    Free((*table)->hash_table);
                    Free((*table));
                    return FALSE;
                }
            }
            for (size_t it = 0; it < size; it++) {
                if (((*table)->hash_table[it])) {
                    while ((*table)->hash_table[it]->start) {
                        HASH_NODE* hash_node = (HASH_NODE*)(*table)->hash_table[it]->start->ptr;
                        if (hash_node->need_rehash == 0)
                            break;
                        hash_node->need_rehash = 0;
                        LIST_NODE* node = list_node_shift((*table)->hash_table[it]);
                        node->next = node->prev = NULL;
                        size_t index = (hash_node->hash_value) % (size);
                        list_node_push((*table)->hash_table[index], node);
                    }
                }
            }
        }
    } else {
        for (size_t it = 0; it < (*table)->size; it++) {
            if (((*table)->hash_table[it])) {
                while ((*table)->hash_table[it]->start) {
                    HASH_NODE* hash_node = (HASH_NODE*)(*table)->hash_table[it]->start->ptr;
                    if (hash_node->need_rehash == 0)
                        break;
                    hash_node->need_rehash = 0;
                    LIST_NODE* node = list_node_shift((*table)->hash_table[it]);
                    node->next = node->prev = NULL;
                    size_t index = (hash_node->hash_value) % (size);
                    list_node_push((*table)->hash_table[index], node);
                }
            }
        }
        for (size_t it = size; it < (*table)->size; it++) {
            list_destroy(&(*table)->hash_table[it]);
        }
        Realloc((*table)->hash_table, LIST*, size);
        if (!(*table)->hash_table) {
            n_log(LOG_ERR, "Realloc did not reduce the resize the table !");
        }
    }
    (*table)->size = size;

    return TRUE;
} /* ht_resize() */

/**
 *@brief try an automatic optimization of the table (HASH_CLASSIC mode only)
 *@param table targeted table
 *@return TRUE or FALSE and set table to NULL
 */
int ht_optimize(HASH_TABLE** table) {
    __n_assert((*table), return FALSE);
    if( (*table)->mode != HASH_CLASSIC )
    {
        n_log( LOG_ERR , "unsupported table->mode (%d instead or %d)" , (*table)->mode , HASH_CLASSIC );
        return FALSE;
    }


    size_t optimal_size = ht_get_optimal_size((*table));
    if (optimal_size == FALSE) {
        return FALSE;
    }

    int collision_percentage = ht_get_table_collision_percentage((*table));
    if (collision_percentage == FALSE)
        return FALSE;

    int resize_result = ht_resize(&(*table), optimal_size);
    if (resize_result == FALSE) {
        return FALSE;
    }

    collision_percentage = ht_get_table_collision_percentage((*table));
    if (collision_percentage == FALSE) {
        return FALSE;
    }

    return TRUE;
} /* ht_optimize() */


/**
 *@brief duplicate a hash table (all pointers should have a duplicator func set)
 *@param table the HASH_TABLE *table to duplicate
 *@return NULL or and allocated duplicated HASH_TABLE
 */
HASH_TABLE *ht_duplicate(HASH_TABLE *table)
{
    __n_assert( table , return NULL );
    HASH_TABLE *duplicated_table = NULL ;
    if( table -> mode == HASH_CLASSIC )
    {
        duplicated_table = new_ht( table -> size );
        if( !duplicated_table )
        {
            n_log( LOG_ERR , "couldn't allocate duplicated table of %zu elements" , table -> size );
            return NULL ;
        }
    }
    else if( table -> mode == HASH_TRIE )
    {
        /* duplicated_table = new_ht_trie( table -> alphabet_length , table -> alphabet_offset );
        if( !duplicated_table )
        {
            n_log( LOG_ERR , "couldn't allocate duplicated table of %zu alphabet size and %zu alphabet offset" , table -> alphabet_length , table -> alphabet_offset );
            return NULL ;
        }*/
        n_log( LOG_ERR , "unsupported mode %d for table %p" , table );
        return NULL ;
    }
    else 
    {
        n_log( LOG_ERR , "unsupported mode %d for table %p" , table );
        return NULL ;
    }

    if( table -> mode == HASH_CLASSIC )
    {
        ht_foreach(node, table) {
            HASH_NODE *hash_node = (HASH_NODE *)node -> ptr ;
            switch( hash_node -> type ) {
            case HASH_INT:
                ht_put_int( duplicated_table , hash_node -> key , hash_node -> data .ival );
                break;
            case HASH_DOUBLE:
                ht_put_double( duplicated_table , hash_node -> key , hash_node -> data .fval );
                break;
            case HASH_PTR:
                ht_put_ptr( duplicated_table , hash_node -> key , hash_node -> data .ptr , hash_node -> destroy_func , hash_node -> duplicate_func );
                break;
            case HASH_STRING:
                ht_put_string( duplicated_table , hash_node -> key , hash_node -> data .string );
                break;
            default:
                printf("unknwow type %d", hash_node->type);
                break;
            }
        }
    }
    else if( table -> mode == HASH_TRIE )
    {
    
    }
    
    return duplicated_table ;
}
