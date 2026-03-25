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

/**@file n_stack.h
 *  Stack header definitions
 *@author Castagnier Mickael
 *@version 2.0
 *@date 01/01/2018
 */

#ifndef __N_STACK_HEADER
#define __N_STACK_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup STACK STACKS: generic type stack
  @addtogroup STACK
  @{
  */

#include "nilorea/n_common.h"

/*! v_type value for a bool */
#define STACK_ITEM_BOOL 1
/*! v_type value for a char */
#define STACK_ITEM_CHAR 2
/*! v_type value for a uint8_t */
#define STACK_ITEM_UINT8 3
/*! v_type value for a int8_t */
#define STACK_ITEM_INT8 4
/*! v_type value for a uint32_t */
#define STACK_ITEM_UINT32 5
/*! v_type value for a int32_t */
#define STACK_ITEM_INT32 6
/*! v_type value for a uint64_t */
#define STACK_ITEM_UINT64 7
/*! v_type value for a int64_t */
#define STACK_ITEM_INT64 8
/*! v_type value for a float */
#define STACK_ITEM_FLOAT 9
/*! v_type value for a double */
#define STACK_ITEM_DOUBLE 10
/*! v_type value for a void *pointer */
#define STACK_ITEM_PTR 11

/*! code for a full stack state */
#define STACK_IS_FULL 0
/*! code for an empty stack state */
#define STACK_IS_EMPTY 1
/*! code for a NULL stack state */
#define STACK_IS_UNDEFINED 2
/*! code for a bad item type */
#define STACK_ITEM_WRONG_TYPE 3
/*! code for a successfully retrieved item */
#define STACK_ITEM_OK 4

/*! structure of a STACK_ITEM data */
union STACK_DATA {
    /*! boolean */
    bool b;
    /*! single character */
    char c;
    /*! unsigned int 8 */
    uint8_t ui8;
    /*! int 8 */
    int8_t i8;
    /*! unsigned int 32 */
    uint32_t ui32;
    /*! int 32 */
    int32_t i32;
#ifdef ENV_64BITS
    /*! unsigned int 64 */
    uint64_t ui64;
    /*! int 64 */
    int64_t i64;
#endif
    /*! float */
    float f;
    /*! double */
    double d;
    /*! pointer */
    void* p;
};

/*! structure of a STACK item */
typedef struct STACK_ITEM {
    /*! is item set ? */
    bool is_set;
    /*! is item empty ? */
    bool is_empty;
    /*! union of different types */
    union STACK_DATA data;
    /*! type of the item */
    uint8_t v_type;
    /*! if v_type is STACK_ITEM_PTR, user defined pointer type */
    uint16_t p_type;
} STACK_ITEM;

/*! STACK structure */
typedef struct STACK {
    /*! STACK_ITEM array */
    STACK_ITEM* stack_array;
    /*! Size of array */
    size_t size;
    /*! position of head */
    size_t head;
    /*! position of tail */
    size_t tail;
    /*! number of item inside stack */
    size_t nb_items;
} STACK;

/* stack_push_p_default is declared below with the other push functions.
 * It wraps stack_push_p with a default p_type of 0 so the _Generic macro
 * can dispatch void* with only 2 arguments.
 * Use stack_push_p() directly when a custom p_type is needed. */

#if defined(__sun) && defined(__SVR4)
/* Solaris: avoid _Generic conflicts due to type compatibility */
#ifdef ENV_64BITS
#define stack_push(__STACK, __VAL)                            \
    _Generic((__VAL),                                         \
        bool: stack_push_b,                                   \
        char: stack_push_c,                                   \
        signed char: stack_push_i8,    /* replaces int8_t */  \
        unsigned char: stack_push_ui8, /* replaces uint8_t */ \
        uint32_t: stack_push_ui32,                            \
        int32_t: stack_push_i32,                              \
        uint64_t: stack_push_ui64,                            \
        int64_t: stack_push_i64,                              \
        float: stack_push_f,                                  \
        double: stack_push_d,                                 \
        void*: stack_push_p_default)(__STACK, __VAL)
#else
#define stack_push(__STACK, __VAL)     \
    _Generic((__VAL),                  \
        bool: stack_push_b,            \
        char: stack_push_c,            \
        signed char: stack_push_i8,    \
        unsigned char: stack_push_ui8, \
        uint32_t: stack_push_ui32,     \
        int32_t: stack_push_i32,       \
        float: stack_push_f,           \
        double: stack_push_d,          \
        void*: stack_push_p_default)(__STACK, __VAL)
#endif
#else
/* Default version for Linux, Windows, etc. */
#ifdef ENV_64BITS
#define stack_push(__STACK, __VAL) \
    _Generic((__VAL),              \
        bool: stack_push_b,        \
        char: stack_push_c,        \
        uint8_t: stack_push_ui8,   \
        int8_t: stack_push_i8,     \
        uint32_t: stack_push_ui32, \
        int32_t: stack_push_i32,   \
        uint64_t: stack_push_ui64, \
        int64_t: stack_push_i64,   \
        float: stack_push_f,       \
        double: stack_push_d,      \
        void*: stack_push_p_default)(__STACK, __VAL)
#else
#define stack_push(__STACK, __VAL) \
    _Generic((__VAL),              \
        bool: stack_push_b,        \
        char: stack_push_c,        \
        uint8_t: stack_push_ui8,   \
        int8_t: stack_push_i8,     \
        uint32_t: stack_push_ui32, \
        int32_t: stack_push_i32,   \
        float: stack_push_f,       \
        double: stack_push_d,      \
        void*: stack_push_p_default)(__STACK, __VAL)
#endif
#endif

/*! allocate a new stack */
STACK* new_stack(size_t nb_items);
/*! delete a stack and free its memory */
bool delete_stack(STACK** stack);
/*! check if the stack is full */
bool stack_is_full(STACK* stack);
/*! check if the stack is empty */
bool stack_is_empty(STACK* stack);
/*! peek at an item at given position without removing it */
STACK_ITEM* stack_peek(STACK* stack, size_t position);

/*! push a bool onto the stack */
bool stack_push_b(STACK* stack, bool b);
/*! push a char onto the stack */
bool stack_push_c(STACK* stack, char c);
/*! push a uint8_t onto the stack */
bool stack_push_ui8(STACK* stack, uint8_t ui8);
/*! push an int8_t onto the stack */
bool stack_push_i8(STACK* stack, int8_t i8);
/*! push a uint32_t onto the stack */
bool stack_push_ui32(STACK* stack, uint32_t ui32);
/*! push an int32_t onto the stack */
bool stack_push_i32(STACK* stack, int32_t i32);
/*! push a float onto the stack */
bool stack_push_f(STACK* stack, float f);
/*! push a double onto the stack */
bool stack_push_d(STACK* stack, double d);
/*! push a pointer onto the stack with a custom type */
bool stack_push_p(STACK* stack, void* p, uint16_t p_type);
/*! push a pointer onto the stack with default type */
bool stack_push_p_default(STACK* stack, void* p);

/*! pop a bool from the stack */
bool stack_pop_b(STACK* stack, uint8_t* status);
/*! pop a char from the stack */
char stack_pop_c(STACK* stack, uint8_t* status);
/*! pop a uint8_t from the stack */
uint8_t stack_pop_ui8(STACK* stack, uint8_t* status);
/*! pop an int8_t from the stack */
int8_t stack_pop_i8(STACK* stack, uint8_t* status);
/*! pop a uint32_t from the stack */
uint32_t stack_pop_ui32(STACK* stack, uint8_t* status);
/*! pop an int32_t from the stack */
int32_t stack_pop_i32(STACK* stack, uint8_t* status);
/*! pop a float from the stack */
float stack_pop_f(STACK* stack, uint8_t* status);
/*! pop a double from the stack */
double stack_pop_d(STACK* stack, uint8_t* status);
/*! pop a pointer from the stack */
void* stack_pop_p(STACK* stack, uint8_t* status);

#ifdef ENV_64BITS
/*! push a uint64_t onto the stack */
bool stack_push_ui64(STACK* stack, uint64_t ui64_t);
/*! push an int64_t onto the stack */
bool stack_push_i64(STACK* stack, int64_t i64);
/*! pop a uint64_t from the stack */
uint64_t stack_pop_ui64(STACK* stack, uint8_t* status);
/*! pop an int64_t from the stack */
int64_t stack_pop_i64(STACK* stack, uint8_t* status);
#endif

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif
