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
 *@file n_network_msg.c
 *@brief Network messages function
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/05/2005
 */
#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_network.h"
#include "nilorea/n_network_msg.h"

/**
 *@brief Swap bytes endiannes for a double
 *@param value The double to swap
 *@return A swapped double
 */
double double_swap(double value) {
    double swaped = 0;
    unsigned char* src = (unsigned char*)&value;
    unsigned char* dst = (unsigned char*)&swaped;

    /* swap bytes */
    dst[0] = src[7];
    dst[1] = src[6];
    dst[2] = src[5];
    dst[3] = src[4];
    dst[4] = src[3];
    dst[5] = src[2];
    dst[6] = src[1];
    dst[7] = src[0];

    return swaped;
} /* double_swap() */

/**
 *@brief If needed swap bytes for a double
 *@param value The value to eventually swap
 *@return The modified value
 */
double htond(double value) {
    double ret = value;
    /* if byte order is little like x86, PC, ... we swap to network notation */
#if (BYTEORDER_ENDIAN == BYTEORDER_LITTLE_ENDIAN)
    ret = double_swap(value);
#endif
    return ret;
} /* htond() */

/**
 *@brief If needed swap bytes for a double
 *@param value The value to eventually swap
 *@return The modified value
 */
double ntohd(double value) {
    double ret = value;
    /* if byte order is little like x86, PC, ... we swap to network notation */
#if (BYTEORDER_ENDIAN == BYTEORDER_LITTLE_ENDIAN)
    ret = double_swap(value);
#endif
    return ret;
} /* ntohd() */

/**
 *@brief Create a NETW_MSG *object
 *@param msg A NETW_MSG *object
 *@return TRUE or FALSE
 */
int create_msg(NETW_MSG** msg) {
    if ((*msg)) {
        n_log(LOG_ERR, "create_msg destination is valid and should be NULL !");
        return FALSE;
    }

    Malloc((*msg), NETW_MSG, 1);
    __n_assert(msg && (*msg), return FALSE);

    (*msg)->tabstr = new_generic_list(MAX_LIST_ITEMS);
    __n_assert((*msg)->tabstr, Free((*msg)); return FALSE);
    (*msg)->tabint = new_generic_list(MAX_LIST_ITEMS);
    __n_assert((*msg)->tabint, list_destroy(&(*msg)->tabstr); Free((*msg)); return FALSE);
    (*msg)->tabflt = new_generic_list(MAX_LIST_ITEMS);
    __n_assert((*msg)->tabflt, list_destroy(&(*msg)->tabstr); list_destroy(&(*msg)->tabint); Free((*msg)); return FALSE);

    return TRUE;
} /* create_msg( ... ) */

/**
 *@brief Delete a NETW_MSG *object
 *@param msg A NETW_MSG *object to delete
 *@return TRUE or FALSE
 */
int delete_msg(NETW_MSG** msg) {
    __n_assert(msg, return FALSE);
    if ((*msg)) {
        if ((*msg)->tabint) {
            list_destroy(&(*msg)->tabint);
        }
        if ((*msg)->tabflt) {
            list_destroy(&(*msg)->tabflt);
        }
        if ((*msg)->tabstr) {
            list_destroy(&(*msg)->tabstr);
        }
        Free((*msg));
    } else {
        n_log(LOG_ERR, "(*msg) already NULL");
        return FALSE;
    }
    return TRUE;
} /* delete_msg */

/**
 *@brief Empty a NETW_MSG *object
 *@param msg A NETW_MSG **object
 *@return TRUE or FALSE
 */
int empty_msg(NETW_MSG** msg) {
    __n_assert(msg && (*msg), return FALSE);

    list_empty((*msg)->tabint);
    list_empty((*msg)->tabflt);
    list_empty((*msg)->tabstr);

    return TRUE;
} /* empty_msg(...) */

/**
 *@brief Add an float to the message
 *@param msg A NETW_MSG *object to complete with the value
 *@param value The value to add to the message
 *@warning The floating value will be rounded by the network serialization !
 *@return TRUE or FALSE
 */
int add_nb_to_msg(NETW_MSG* msg, double value) {
    double* new_val = NULL;

    __n_assert(msg, return FALSE);

    Malloc(new_val, double, 1);
    __n_assert(new_val, return FALSE);

    new_val[0] = value;

    if (list_push(msg->tabflt, new_val, free) == FALSE) {
        Free(new_val);
        return FALSE;
    }
    return TRUE;
} /* add_nb_to_msg( ... ) */

/**
 *@brief Add an int to the int list int the message
 *@param msg A NETW_MSG *object to complete with the value
 *@param value The value to add to the message
 *@return TRUE or FALSE
 */
int add_int_to_msg(NETW_MSG* msg, int value) {
    int* new_val = NULL;

    __n_assert(msg, return FALSE);

    Malloc(new_val, int, 1);
    __n_assert(new_val, return FALSE);

    new_val[0] = value;

    if (list_push(msg->tabint, new_val, free) == FALSE) {
        Free(new_val);
        return FALSE;
    }
    return TRUE;
} /* add_int_to_msg( ... ) */

/**
 *@brief Add a string to the string list in the message
 *@param msg A NETW_MSG *object to complete with the string
 *@param str The string to add to the message
 *@return TRUE or FALSE
 */
int add_nstrptr_to_msg(NETW_MSG* msg, N_STR* str) {
    __n_assert(msg, return FALSE);
    __n_assert(str, return FALSE);
    __n_assert(str->data, return FALSE);
    return list_push(msg->tabstr, str, free_nstr_ptr);
} /* add_nstrptr_to_msg(...) */

/**
 *@brief Add a copy of str to the string list in the message
 *@param msg A NETW_MSG *object to complete with the string
 *@param str The string to add to the message
 *@return TRUE or FALSE
 */
int add_nstrdup_to_msg(NETW_MSG* msg, N_STR* str) {
    __n_assert(msg, return FALSE);
    __n_assert(str, return FALSE);
    __n_assert(str->data, return FALSE);
    N_STR* dup = nstrdup(str);
    __n_assert(dup, return FALSE);
    return list_push(msg->tabstr, dup, free_nstr_ptr);
} /* add_nstrdup_to_msg(...) */

/**
 *@brief Add a copy of char *str to the string list in the message
 *@param msg A NETW_MSG *object to complete with the string
 *@param str The string to add to the message
 *@return TRUE or FALSE
 */
int add_strdup_to_msg(NETW_MSG* msg, const char* str) {
    __n_assert(msg, return FALSE);
    __n_assert(str, return FALSE);

    N_STR* dup = char_to_nstr(str);
    __n_assert(dup, return FALSE);

    return list_push(msg->tabstr, dup, free_nstr_ptr);
} /* add_strdup_to_msg(...) */

/**
 *@brief Get a number from a message number list
 *@param msg A NETW_MSG *object for extracting number
 *@param value A double pointer where the result will be stored
 *@return TRUE or FALSE
 */
int get_nb_from_msg(NETW_MSG* msg, double* value) {
    double* val = NULL;

    __n_assert(msg, return FALSE);

    val = list_shift(msg->tabflt, double);
    if (val) {
        (*value) = val[0];
        Free(val);
        return TRUE;
    }
    return FALSE;
} /* get_nb_from_msg(...) */

/**
 *@brief Get an int from a message int list
 *@param msg A NETW_MSG *object for extracting int
 *@param value An int pointer where the result will be stored
 *@return TRUE or FALSE
 */
int get_int_from_msg(NETW_MSG* msg, int* value) {
    int* val = NULL;

    __n_assert(msg, return FALSE);

    val = list_shift(msg->tabint, int);
    if (val) {
        (*value) = val[0];
        Free(val);
        return TRUE;
    }
    return FALSE;

} /* get_int_from_msg(...) */

/**
 *@brief Get a string from a message string list
 *@param msg The NETW_MSG where you have a str to get
 *@param value A pointer to a NULL N_STR for storage
 *@return TRUE or FALSE
 */
int get_nstr_from_msg(NETW_MSG* msg, N_STR** value) {
    N_STR* val = NULL;

    __n_assert(msg, return FALSE);

    val = list_shift(msg->tabstr, N_STR);
    if (val) {
        if ((*value)) {
            n_log(LOG_ERR, "Previous pointer value %p overriden by pointer %p", (*value), val);
        }
        (*value) = val;
        return TRUE;
    }
    return FALSE;
} /* get_nstr_from_msg(...) */

/**
 *@brief Get a string from a message string list
 *@param msg The NETW_MSG where you have a str to get
 *@param value A pointer to a NULL N_STR for storage
 *@return TRUE or FALSE
 */
int get_str_from_msg(NETW_MSG* msg, char** value) {
    N_STR* val = NULL;

    __n_assert(msg, return FALSE);

    val = list_shift(msg->tabstr, N_STR);
    if (val) {
        if ((*value)) {
            n_log(LOG_ERR, "Previous pointer value %p overriden by pointer %p", (*value), val);
        }

        (*value) = val->data;
        Free(val);
        return TRUE;
    }
    return FALSE;
} /* get_str_from_msg(...) */

/**
 *@brief Make a single string of the message
 *@param msg A NETW_MSG structure to convert into a N_STR one
 *@return A pointer to the generated N_STR, do not forget to free it after use !
 */
N_STR* make_str_from_msg(NETW_MSG* msg) {
    size_t str_length = 0;
    LIST_NODE* node = NULL;

    N_STR *strptr = NULL,
          *generated_str = NULL;

    char* charptr = NULL;

    __n_assert(msg, return NULL);

    /* the first thing to do is to computer the resulting string size
       the first 12 octets are for the number of ints , floats, strings in the message
       */
    str_length = 3 * sizeof(int32_t) + (msg->tabint->nb_items * sizeof(int32_t)) + (msg->tabflt->nb_items * sizeof(double));

    /* let's count the eventual string blocks */
    node = msg->tabstr->start;
    while (node) {
        strptr = (N_STR*)node->ptr;
        str_length += sizeof(int32_t) + sizeof(int32_t) + strptr->written;
        node = node->next;
    }

    /* preparing the new string */
    Malloc(generated_str, N_STR, 1);
    __n_assert(generated_str, return NULL);
    Malloc(generated_str->data, char, str_length + 1);
    __n_assert(generated_str->data, Free(generated_str); return NULL);

    generated_str->length = str_length + 1;
    generated_str->written = str_length;

    /* copying header */
    charptr = generated_str->data;

    /* testing limits */
    if (msg->tabint->nb_items >= UINT32_MAX) {
        n_log(LOG_ERR, "too much items (>=UINT32_MAX) in int list of message %p", msg);
        free_nstr(&generated_str);
        return NULL;
    }
    if (msg->tabflt->nb_items >= UINT32_MAX) {
        n_log(LOG_ERR, "too much items (>=UINT32_MAX) in float list of message %p", msg);
        free_nstr(&generated_str);
        return NULL;
    }
    if (msg->tabstr->nb_items >= UINT32_MAX) {
        n_log(LOG_ERR, "too much items (>=UINT32_MAX) in string list of message %p", msg);
        free_nstr(&generated_str);
        return NULL;
    }

    /* byte order */
    uint32_t nb_int_items = htonl((uint32_t)msg->tabint->nb_items);
    uint32_t nb_float_items = htonl((uint32_t)msg->tabflt->nb_items);
    uint32_t nb_str_items = htonl((uint32_t)msg->tabstr->nb_items);

    /* number of ints inside the message */
    memcpy(charptr, &nb_int_items, sizeof(uint32_t));
    charptr += sizeof(uint32_t);
    /* number of doubles inside the message */
    memcpy(charptr, &nb_float_items, sizeof(uint32_t));
    charptr += sizeof(uint32_t);
    /* number of string inside the message */
    memcpy(charptr, &nb_str_items, sizeof(uint32_t));
    charptr += sizeof(uint32_t);

    /* if there are numbers, copy all them ! */
    node = msg->tabint->start;
    while (node) {
        uint32_t* nbptr = (uint32_t*)node->ptr;
        uint32_t val = htonl(nbptr[0]);
        memcpy(charptr, &val, sizeof(uint32_t));
        charptr += sizeof(uint32_t);
        node = node->next;
    }

    /* if there are numbers, copy all them ! */
    node = msg->tabflt->start;
    while (node) {
        double* nbptr = (double*)node->ptr;
        double val = htond(nbptr[0]);
        memcpy(charptr, &val, sizeof(double));
        charptr += sizeof(double);
        node = node->next;
    }

    /* if there are strings, copy all them ! */
    if (msg->tabstr->nb_items > 0) {
        node = msg->tabstr->start;
        while (node) {
            strptr = (N_STR*)node->ptr;
            if (strptr->length >= UINT32_MAX) {
                n_log(LOG_ERR, "string too long (>=UINT32_MAX) in string list of message %p", msg);
                free_nstr(&generated_str);
                return NULL;
            }
            uint32_t var = htonl((uint32_t)strptr->length);
            memcpy(charptr, &var, sizeof(uint32_t));
            charptr += sizeof(uint32_t);

            if (strptr->written >= UINT32_MAX) {
                n_log(LOG_ERR, "string too long (>=UINT32_MAX) in string list of message %p", msg);
                free_nstr(&generated_str);
                return NULL;
            }
            var = htonl((uint32_t)strptr->written);
            memcpy(charptr, &var, sizeof(uint32_t));
            charptr += sizeof(uint32_t);

            memcpy(charptr, strptr->data, strptr->written);
            charptr += strptr->written;
            node = node->next;
        }
    }

    return generated_str;
} /* make_str_from_msg(...) */

/**
 *@brief Make a single message of the string
 *@param str A N_STR structure to convert into a NETW_MSG one
 *@return A pointer to the generated NETW_MSG, do not forget to free it after use !
 */
NETW_MSG* make_msg_from_str(N_STR* str) {
    NETW_MSG* generated_msg = NULL;
    N_STR* tmpstr = NULL;

    char* charptr = NULL;

    uint32_t it,
        nb_int = 0,
        nb_flt = 0,
        nb_str = 0;
    int32_t tmpnb = 0;

    double tmpflt = 0;

    __n_assert(str, return NULL);

    charptr = str->data;

    /* need at least 3 uint32_t for the header */
    if (str->written < 3 * sizeof(uint32_t)) {
        n_log(LOG_ERR, "message too short: %zu bytes, need at least %zu", str->written, 3 * sizeof(uint32_t));
        return NULL;
    }

    create_msg(&generated_msg);

    __n_assert(generated_msg, return NULL);

    memcpy(&nb_int, charptr, sizeof(uint32_t));
    charptr += sizeof(uint32_t);
    nb_int = ntohl(nb_int);

    memcpy(&nb_flt, charptr, sizeof(uint32_t));
    charptr += sizeof(uint32_t);
    nb_flt = ntohl(nb_flt);

    memcpy(&nb_str, charptr, sizeof(uint32_t));
    charptr += sizeof(uint32_t);
    nb_str = ntohl(nb_str);

    /* validate that the message is large enough for the claimed int and float counts.
     * Use division-based checks to avoid size_t overflow on 32-bit platforms. */
    size_t remaining = str->written - 3 * sizeof(uint32_t);

    if (nb_int > remaining / sizeof(uint32_t)) {
        n_log(LOG_ERR, "message claims %u ints but only %zu bytes remain", nb_int, remaining);
        delete_msg(&generated_msg);
        return NULL;
    }
    remaining -= (size_t)nb_int * sizeof(uint32_t);

    if (nb_flt > remaining / sizeof(double)) {
        n_log(LOG_ERR, "message claims %u floats but only %zu bytes remain", nb_flt, remaining);
        delete_msg(&generated_msg);
        return NULL;
    }
    remaining -= (size_t)nb_flt * sizeof(double);

    if (nb_int > 0) {
        for (it = 0; it < nb_int; it++) {
            memcpy(&tmpnb, charptr, sizeof(uint32_t));
            tmpnb = (int32_t)ntohl((uint32_t)tmpnb);
            charptr += sizeof(uint32_t);
            if (add_int_to_msg(generated_msg, tmpnb) == FALSE) {
                n_log(LOG_ERR, "failed to add int %u/%u to message", it, nb_int);
                delete_msg(&generated_msg);
                return NULL;
            }
        }
    }

    if (nb_flt > 0) {
        for (it = 0; it < nb_flt; it++) {
            memcpy(&tmpflt, charptr, sizeof(double));
            tmpflt = ntohd(tmpflt);
            charptr += sizeof(double);
            if (add_nb_to_msg(generated_msg, tmpflt) == FALSE) {
                n_log(LOG_ERR, "failed to add float %u/%u to message", it, nb_flt);
                delete_msg(&generated_msg);
                return NULL;
            }
        }
    }

    if (nb_str > 0) {
        /* sanity check: each string needs at least 2 uint32_t for headers */
        if ((size_t)nb_str > remaining / (2 * sizeof(uint32_t))) {
            n_log(LOG_ERR, "message claims %u strings but only %zu bytes remain (need at least %zu for headers)",
                  nb_str, remaining, (size_t)nb_str * 2 * sizeof(uint32_t));
            delete_msg(&generated_msg);
            return NULL;
        }

        for (it = 0; it < nb_str; it++) {
            /* check space for length + written headers */
            if (remaining < 2 * sizeof(uint32_t)) {
                n_log(LOG_ERR, "string %u/%u: not enough data for string headers (%zu remaining)", it, nb_str, remaining);
                delete_msg(&generated_msg);
                return NULL;
            }

            Malloc(tmpstr, N_STR, 1);
            __n_assert(tmpstr, { delete_msg(&generated_msg); return NULL; });

            uint32_t var = 0;
            memcpy(&var, charptr, sizeof(uint32_t));
            charptr += sizeof(uint32_t);
            tmpstr->length = ntohl(var);

            memcpy(&var, charptr, sizeof(uint32_t));
            charptr += sizeof(uint32_t);
            tmpstr->written = ntohl(var);
            remaining -= 2 * sizeof(uint32_t);

            /* validate written < length (N_STR requires room for trailing '\0') */
            if (tmpstr->length == 0 || tmpstr->written >= tmpstr->length) {
                n_log(LOG_ERR, "string %u/%u: written (%zu) >= length (%zu), violates N_STR invariant", it, nb_str, tmpstr->written, tmpstr->length);
                Free(tmpstr);
                delete_msg(&generated_msg);
                return NULL;
            }

            /* validate remaining buffer has enough for written bytes */
            if (tmpstr->written > remaining) {
                n_log(LOG_ERR, "string %u/%u: written size %zu exceeds remaining %zu bytes", it, nb_str, tmpstr->written, remaining);
                Free(tmpstr);
                delete_msg(&generated_msg);
                return NULL;
            }

            /* sanity cap on allocation size to prevent DoS via huge allocation */
            if (tmpstr->length > NETW_MSG_MAX_STRING_LENGTH) {
                n_log(LOG_ERR, "string %u/%u: length %zu exceeds NETW_MSG_MAX_STRING_LENGTH (%zu)", it, nb_str, tmpstr->length, (size_t)NETW_MSG_MAX_STRING_LENGTH);
                Free(tmpstr);
                delete_msg(&generated_msg);
                return NULL;
            }

            /* allocate based on written+1 (not untrusted length) to prevent memory-DoS
             * where attacker sets length near cap but written tiny */
            Malloc(tmpstr->data, char, tmpstr->written + 1);
            __n_assert(tmpstr->data, { Free(tmpstr); delete_msg(&generated_msg); return NULL; });
            tmpstr->length = tmpstr->written + 1;

            memcpy(tmpstr->data, charptr, tmpstr->written);
            tmpstr->data[tmpstr->written] = '\0';
            charptr += tmpstr->written;
            remaining -= tmpstr->written;

            if (add_nstrptr_to_msg(generated_msg, tmpstr) == FALSE) {
                Free(tmpstr->data);
                Free(tmpstr);
                delete_msg(&generated_msg);
                return NULL;
            }
        }
    }
    return generated_msg;
} /* make_msg_from_str(...) */

#ifndef NO_NETMSG_API

/**
 *@brief Make a ping message to send to a network
 *@param type NETW_PING_REQUEST or NETW_PING_REPLY
 *@param id_from Identifiant of the sender
 *@param id_to Identifiant of the destination, -1 if the serveur itslef is targetted
 *@param time The time it was when the ping was sended
 *@return A N_STR *message or NULL
 */
N_STR* netmsg_make_ping(int type, int id_from, int id_to, int time) {
    NETW_MSG* msg = NULL;

    N_STR* tmpstr = NULL;

    create_msg(&msg);

    __n_assert(msg, return NULL);

    if (add_int_to_msg(msg, type) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, id_from) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, id_to) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, time) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }

    tmpstr = make_str_from_msg(msg);
    delete_msg(&msg);

    return tmpstr;
} /* netmsg_make_ping() */

/**
 *@brief Add a formatted NETWMSG_IDENT message to the specified network
 *@param type type of identification ( NETW_IDENT_REQUEST , NETW_IDENT_NEW )
 *@param id The ID of the sending client
 *@param name Username
 *@param passwd Password
 *@return N_STR *netmsg or NULL
 */
N_STR* netmsg_make_ident(int type, int id, N_STR* name, N_STR* passwd) {
    NETW_MSG* msg = NULL;
    N_STR* tmpstr = NULL;

    create_msg(&msg);
    __n_assert(msg, return NULL);

    if (add_int_to_msg(msg, type) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, id) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }

    if (add_nstrdup_to_msg(msg, name) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nstrdup_to_msg(msg, passwd) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }

    tmpstr = make_str_from_msg(msg);
    delete_msg(&msg);

    return tmpstr;
} /* netmsg_make_ident() */

/**
 *@brief make a network NETMSG_POSITION message with given parameters
 *@param id The ID of the sending client
 *@param X x position
 *@param Y y position
 *@param vx x speed
 *@param vy y speed
 *@param acc_x x acceleration
 *@param acc_y y acceleration
 *@param time_stamp time when the position were copier
 *@return N_STR *netmsg or NULL
 */
N_STR* netmsg_make_position_msg(int id, double X, double Y, double vx, double vy, double acc_x, double acc_y, int time_stamp) {
    NETW_MSG* msg = NULL;
    N_STR* tmpstr = NULL;

    create_msg(&msg);

    __n_assert(msg, return NULL);

    if (add_int_to_msg(msg, NETMSG_POSITION) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, id) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nb_to_msg(msg, X) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nb_to_msg(msg, Y) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nb_to_msg(msg, vx) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nb_to_msg(msg, vy) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nb_to_msg(msg, acc_x) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nb_to_msg(msg, acc_y) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, time_stamp) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }

    tmpstr = make_str_from_msg(msg);
    delete_msg(&msg);
    return tmpstr;
} /* netmsg_make_position_msg() */

/**
 *@brief make a network NETMSG_STRING message with given parameters
 *@param id_from The ID of the sending client
 *@param id_to id of the destination
 *@param name name of the user
 *@param chan targeted channel
 *@param txt text to send
 *@param color color of the text
 *@return N_STR *netmsg or NULL
 */
N_STR* netmsg_make_string_msg(int id_from, int id_to, N_STR* name, N_STR* chan, N_STR* txt, int color) {
    NETW_MSG* msg = NULL;

    N_STR* tmpstr = NULL;

    create_msg(&msg);

    __n_assert(msg, return NULL);

    if (add_int_to_msg(msg, NETMSG_STRING) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, id_from) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, id_to) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_int_to_msg(msg, color) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }

    if (add_nstrdup_to_msg(msg, name) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nstrdup_to_msg(msg, chan) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    if (add_nstrdup_to_msg(msg, txt) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }

    tmpstr = make_str_from_msg(msg);
    delete_msg(&msg);
    __n_assert(tmpstr, return NULL);

    return tmpstr;

} /* netmsg_make_string_msg */

/**
 *@brief make a generic network NETMSG_QUIT message
 *@return N_STR *netmsg or NULL
 */
N_STR* netmsg_make_quit_msg(void) {
    NETW_MSG* msg = NULL;
    N_STR* tmpstr = NULL;

    create_msg(&msg);
    __n_assert(msg, return NULL);

    if (add_int_to_msg(msg, NETMSG_QUIT) == FALSE) {
        delete_msg(&msg);
        return NULL;
    }
    tmpstr = make_str_from_msg(msg);
    delete_msg(&msg);
    __n_assert(tmpstr, return NULL);

    return tmpstr;
} /* netmsg_make_quit_msg() */

/**
 *@param msg A char *msg_object you want to have type
 *@brief Get the type of message without killing the first number. Use with netw_get_XXX
 *@return The value corresponding to the type or -1 on error
 */
int netw_msg_get_type(N_STR* msg) {
    uint32_t nb_int = 0;
    int32_t tmpnb = 0;
    char* charptr = NULL;

    __n_assert(msg, return -1);
    __n_assert(msg->data, return -1);

    /* need at least 3*uint32 header + 1*uint32 for the type int */
    if (msg->written < 4 * sizeof(uint32_t)) return -1;

    charptr = msg->data;

    /* read nb_int from the header to verify the int section is present */
    memcpy(&nb_int, charptr, sizeof(uint32_t));
    nb_int = ntohl(nb_int);

    if (nb_int < 1) {
        n_log(LOG_ERR, "netw_msg_get_type: message has no int fields (nb_int=%u), cannot read type", nb_int);
        return -1;
    }

    /* bypass nb_int, nb_flt, nb_str header fields to reach the first int (type) */
    charptr += 3 * sizeof(uint32_t);
    memcpy(&tmpnb, charptr, sizeof(uint32_t));
    tmpnb = (int32_t)ntohl((uint32_t)tmpnb);

    return tmpnb;
} /* netw_msg_get_type(...) */

/**
 *@brief Retrieves identification from netwmsg
 *@param msg The source string from which we are going to extract the data
 *@param type NETMSG_IDENT_NEW , NETMSG_IDENT_REPLY_OK , NETMSG_IDENT_REPLY_NOK , NETMSG_IDENT_REQUEST
 *@param ident ID for the user
 *@param name Name of the user
 *@param passwd Password of the user
 *@return TRUE or FALSE
 */
int netw_get_ident(N_STR* msg, int* type, int* ident, N_STR** name, N_STR** passwd) {
    NETW_MSG* netmsg = NULL;

    __n_assert(msg, return FALSE);

    netmsg = make_msg_from_str(msg);

    __n_assert(netmsg, return FALSE);

    if (get_int_from_msg(netmsg, type) != TRUE ||
        get_int_from_msg(netmsg, ident) != TRUE) {
        n_log(LOG_ERR, "failed to extract type/ident from ident message");
        delete_msg(&netmsg);
        return FALSE;
    }

    if ((*type) != NETMSG_IDENT_REQUEST && (*type) != NETMSG_IDENT_REPLY_OK && (*type) != NETMSG_IDENT_REPLY_NOK) {
        n_log(LOG_ERR, "unknow (*type) %d", (*type));
        delete_msg(&netmsg);
        return FALSE;
    }

    if (get_nstr_from_msg(netmsg, name) != TRUE ||
        get_nstr_from_msg(netmsg, passwd) != TRUE) {
        n_log(LOG_ERR, "failed to extract name/passwd from ident message");
        free_nstr(name);
        free_nstr(passwd);
        delete_msg(&netmsg);
        return FALSE;
    }

    delete_msg(&netmsg);

    return TRUE;
} /* netw_get_ident( ... ) */

/**
 *@brief Retrieves position from netwmsg
 *@param msg The source string from which we are going to extract the data
 *@param id
 *@param X X position inside a big grid
 *@param Y Y position inside a big grid
 *@param vx X speed
 *@param vy Y speed
 *@param acc_x X acceleration
 *@param acc_y Y acceleration
 *@param time_stamp Current Time when it was sended (for some delta we would want to compute )
 *@return TRUE or FALSE
 */
int netw_get_position(N_STR* msg, int* id, double* X, double* Y, double* vx, double* vy, double* acc_x, double* acc_y, int* time_stamp) {
    NETW_MSG* netmsg = NULL;

    int type = 0;

    __n_assert(msg, return FALSE);

    netmsg = make_msg_from_str(msg);

    __n_assert(netmsg, return FALSE);

    if (get_int_from_msg(netmsg, &type) != TRUE) {
        n_log(LOG_ERR, "failed to extract type from position message");
        delete_msg(&netmsg);
        return FALSE;
    }

    if (type != NETMSG_POSITION) {
        delete_msg(&netmsg);
        return FALSE;
    }

    if (get_int_from_msg(netmsg, id) != TRUE ||
        get_nb_from_msg(netmsg, X) != TRUE ||
        get_nb_from_msg(netmsg, Y) != TRUE ||
        get_nb_from_msg(netmsg, vx) != TRUE ||
        get_nb_from_msg(netmsg, vy) != TRUE ||
        get_nb_from_msg(netmsg, acc_x) != TRUE ||
        get_nb_from_msg(netmsg, acc_y) != TRUE ||
        get_int_from_msg(netmsg, time_stamp) != TRUE) {
        n_log(LOG_ERR, "failed to extract fields from position message");
        delete_msg(&netmsg);
        return FALSE;
    }

    delete_msg(&netmsg);

    return TRUE;
} /* netw_get_position( ... ) */

/**
 *@brief Retrieves string from netwmsg
 *@param msg The source string from which we are going to extract the data
 *@param id The ID of the sending client
 *@param name Name of user
 *@param chan Target Channel, if any. Pass "ALL" to target the default channel
 *@param txt The text to send
 *@param color The color of the text
 *
 *@return TRUE or FALSE
 */
int netw_get_string(N_STR* msg, int* id, N_STR** name, N_STR** chan, N_STR** txt, int* color) {
    NETW_MSG* netmsg = NULL;

    int type = 0;

    __n_assert(msg, return FALSE);

    netmsg = make_msg_from_str(msg);

    __n_assert(netmsg, return FALSE);

    if (get_int_from_msg(netmsg, &type) != TRUE) {
        n_log(LOG_ERR, "failed to extract type from string message");
        delete_msg(&netmsg);
        return FALSE;
    }

    if (type != NETMSG_STRING) {
        delete_msg(&netmsg);
        return FALSE;
    }

    int id_to = 0;
    if (get_int_from_msg(netmsg, id) != TRUE ||
        get_int_from_msg(netmsg, &id_to) != TRUE ||
        get_int_from_msg(netmsg, color) != TRUE) {
        n_log(LOG_ERR, "failed to extract int fields from string message");
        delete_msg(&netmsg);
        return FALSE;
    }

    if (get_nstr_from_msg(netmsg, name) != TRUE ||
        get_nstr_from_msg(netmsg, chan) != TRUE ||
        get_nstr_from_msg(netmsg, txt) != TRUE) {
        n_log(LOG_ERR, "failed to extract string fields from string message");
        free_nstr(name);
        free_nstr(chan);
        free_nstr(txt);
        delete_msg(&netmsg);
        return FALSE;
    }

    delete_msg(&netmsg);

    return TRUE;

} /* netw_get_string( ... ) */

/**
 *@brief Retrieves a ping travel elapsed time
 *@param msg The source string from which we are going to extract the data
 *@param type NETW_PING_REQUEST or NETW_PING_REPLY
 *@param from Identifiant of the sender
 *@param to Targetted Identifiant, -1 for server ping
 *@param time The time it was when the ping was sended
 *@return TRUE or FALSE
 */
int netw_get_ping(N_STR* msg, int* type, int* from, int* to, int* time) {
    NETW_MSG* netmsg;

    __n_assert(msg, return FALSE);

    netmsg = make_msg_from_str(msg);

    __n_assert(netmsg, return FALSE);

    if (get_int_from_msg(netmsg, type) != TRUE) {
        n_log(LOG_ERR, "failed to extract type from ping message");
        delete_msg(&netmsg);
        return FALSE;
    }

    if ((*type) != NETMSG_PING_REQUEST && (*type) != NETMSG_PING_REPLY) {
        delete_msg(&netmsg);
        return FALSE;
    }

    if (get_int_from_msg(netmsg, from) != TRUE ||
        get_int_from_msg(netmsg, to) != TRUE ||
        get_int_from_msg(netmsg, time) != TRUE) {
        n_log(LOG_ERR, "failed to extract fields from ping message");
        delete_msg(&netmsg);
        return FALSE;
    }

    delete_msg(&netmsg);

    return TRUE;
} /* netw_get_ping( ... ) */

/**
 *@brief get a formatted NETWMSG_QUIT message from the specified network
 *@param msg The source string from which we are going to extract the data
 *@return TRUE or FALSE
 */
int netw_get_quit(N_STR* msg) {
    __n_assert(msg, return FALSE);

    NETW_MSG* netmsg = NULL;
    netmsg = make_msg_from_str(msg);
    __n_assert(netmsg, return FALSE);

    int type = -1;
    get_int_from_msg(netmsg, &type);
    if (type != NETMSG_QUIT) {
        delete_msg(&netmsg);
        return FALSE;
    }
    delete_msg(&netmsg);
    return TRUE;
} /* netw_get_quit( ... ) */

#endif  // NO_NETMSG_API
