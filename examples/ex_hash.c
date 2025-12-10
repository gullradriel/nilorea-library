/**
 *@example ex_hash.c
 *@brief Nilorea Library hash table api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/05/2015
 */

#include "nilorea/n_str.h"
#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"

#define NB_HASH_ELEMENTS 24

/*! string and int holder */
typedef struct DATA {
    /*! string holder */
    N_STR* rnd_str;
    /*! int value */
    int value;
} DATA;

/**
 *@brief destroy a DATA struct
 *@param ptr A pointer to a DATA struct
 */
void destroy_data(void* ptr) {
    DATA* data = (DATA*)ptr;
    free_nstr(&data->rnd_str);
    Free(data);
}

int main(void) {
    set_log_level(LOG_DEBUG);

    HASH_TABLE* htable = new_ht(NB_HASH_ELEMENTS / 3);
    LIST* keys_list = new_generic_list(NB_HASH_ELEMENTS + 1);
    DATA* data = NULL;
    char* str = NULL;

    n_log(LOG_INFO, "Filling HashTable with %d random elements", NB_HASH_ELEMENTS);
    for (int it = 0; it < NB_HASH_ELEMENTS; it++) {
        N_STR* nkey = NULL;
        int randomizator = rand() % 4;
        nstrprintf(nkey, "key%d_%d", it, randomizator);
        switch (randomizator) {
            default:
            case 0:
                ht_put_int(htable, _nstr(nkey), 666);
                n_log(LOG_INFO, "Put int 666 with key %s", _nstr(nkey));
                break;
            case 1:
                ht_put_double(htable, _nstr(nkey), 3.14);
                n_log(LOG_INFO, "Put double 3.14 with key %s", _nstr(nkey));
                break;
            case 2:
                Malloc(data, DATA, 1);
                data->rnd_str = NULL;
                nstrprintf(data->rnd_str, "%s%d", _nstr(nkey), rand() % 10);
                data->value = 7;
                ht_put_ptr(htable, _nstr(nkey), data, &destroy_data, NULL);
                n_log(LOG_INFO, "Put ptr rnd_str %s value %d with key %s", _nstr(data->rnd_str), data->value, _nstr(nkey));
                break;
            case 3:
                Malloc(str, char, 64);
                snprintf(str, 64, "%s%d", _nstr(nkey), rand() % 10);
                ht_put_string(htable, _nstr(nkey), str);
                n_log(LOG_INFO, "Put string %s key %s", str, _nstr(nkey));
                Free(str);
                break;
        }
        /* asving key for ulterior use */
        list_push(keys_list, nkey, free_nstr_ptr);
    }

    n_log(LOG_INFO, "Reading hash table with ht_foreach");
    HT_FOREACH(node, htable,
               {
                   n_log(LOG_INFO, "HT_FOREACH hash: %u, key:%s", node->hash_value, _str(node->key));
               });
    HT_FOREACH_R(node, htable, hash_iterator,
                 {
                     n_log(LOG_INFO, "HT_FOREACH_R hash: %u, key:%s", node->hash_value, _str(node->key));
                 });

    unsigned long int optimal_size = ht_get_optimal_size(htable);
    n_log(LOG_INFO, "########");
    n_log(LOG_INFO, "collisions: %d %%", ht_get_table_collision_percentage(htable));
    n_log(LOG_INFO, "table size: %ld , table optimal size: %ld", htable->size, optimal_size);
    n_log(LOG_INFO, "resizing to %ld returned %d", optimal_size, ht_resize(&htable, optimal_size));
    n_log(LOG_INFO, "collisions after resize: %d %%", ht_get_table_collision_percentage(htable));
    n_log(LOG_INFO, "########");

    if (ht_optimize(&htable) == FALSE) {
        n_log(LOG_ERR, "Error when optimizing table %p", htable);
    }
    n_log(LOG_INFO, "collisions after ht_optimize: %d %%", ht_get_table_collision_percentage(htable));
    n_log(LOG_INFO, "########");

    LIST* results = ht_get_completion_list(htable, "key", NB_HASH_ELEMENTS);
    if (results) {
        list_foreach(node, results) {
            n_log(LOG_INFO, "completion result: %s", (char*)node->ptr);
        }
        list_destroy(&results);
    }

    int matching_nodes(HASH_NODE * node) {
        if (strncasecmp("key", node->key, 3) == 0)
            return TRUE;
        return FALSE;
    }
    results = ht_search(htable, &matching_nodes);
    list_foreach(node, results) {
        n_log(LOG_INFO, "htsearch: key: %s", (char*)node->ptr);
    }
    list_destroy(&results);

    list_destroy(&keys_list);
    destroy_ht(&htable);

    /* testing empty destroy */
    htable = new_ht(1024);
    empty_ht(htable);
    destroy_ht(&htable);

    htable = new_ht_trie(256, 32);

    ht_put_int(htable, "TestInt", 1);
    ht_put_double(htable, "TestDouble", 2.0);
    ht_put_string(htable, "TestString", "MyString");
    HASH_INT_TYPE ival = -1;
    double fval = -1.0;
    ht_get_int(htable, "TestInt", &ival);
    ht_get_double(htable, "TestDouble", &fval);
    char* string = NULL;
    ht_get_string(htable, "TestString", &string);
    n_log(LOG_INFO, "Trie:%ld %f %s", (long)ival, fval, string);

    results = ht_get_completion_list(htable, "Test", 10);
    if (results) {
        list_foreach(node, results) {
            n_log(LOG_INFO, "completion result: %s", (char*)node->ptr);
        }
        list_destroy(&results);
    }

    destroy_ht(&htable);

    exit(0);

} /* END_OF_MAIN */
