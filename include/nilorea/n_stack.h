/**@file n_stack.c
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
    /*! unsigned int 8 */
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
    /*! is item empty ? */
    bool is_set;
    /*! is item set ? */
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

#if defined(__sun) && defined(__SVR4)
/* Solaris: avoid _Generic conflicts due to type compatibility */
#ifdef ENV_64BITS
#define stack_push(__STACK, __VAL)                            \
    _Generic((__VAL),                                         \
        bool: stack_push_b,                                   \
        signed char: stack_push_i8,    /* replaces int8_t */  \
        unsigned char: stack_push_ui8, /* replaces uint8_t */ \
        uint32_t: stack_push_ui32,                            \
        int32_t: stack_push_i32,                              \
        uint64_t: stack_push_ui64,                            \
        int64_t: stack_push_i64,                              \
        float: stack_push_f,                                  \
        double: stack_push_d,                                 \
        void*: stack_push_p)(__STACK, __VAL)
#else
#define stack_push(__STACK, __VAL)     \
    _Generic((__VAL),                  \
        bool: stack_push_b,            \
        signed char: stack_push_i8,    \
        unsigned char: stack_push_ui8, \
        uint32_t: stack_push_ui32,     \
        int32_t: stack_push_i32,       \
        float: stack_push_f,           \
        double: stack_push_d,          \
        void*: stack_push_p)(__STACK, __VAL)
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
        void*: stack_push_p)(__STACK, __VAL)
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
        void*: stack_push_p)(__STACK, __VAL)
#endif
#endif

STACK* new_stack(size_t nb_items);
bool delete_stack(STACK** stack);
bool stack_is_full(STACK* stack);
bool stack_is_empty(STACK* stack);
STACK_ITEM* stack_peek(STACK* stack, size_t position);

bool stack_push_b(STACK* stack, bool b);
bool stack_push_c(STACK* stack, char c);
bool stack_push_ui8(STACK* stack, uint8_t ui8);
bool stack_push_i8(STACK* stack, int8_t i8);
bool stack_push_ui32(STACK* stack, uint32_t ui32);
bool stack_push_i32(STACK* stack, int32_t i32);
bool stack_push_f(STACK* stack, float f);
bool stack_push_d(STACK* stack, double d);
bool stack_push_p(STACK* stack, void* p, uint16_t p_type);

bool stack_pop_b(STACK* stack, uint8_t* status);
char stack_pop_c(STACK* stack, uint8_t* status);
uint8_t stack_pop_ui8(STACK* stack, uint8_t* status);
int8_t stack_pop_i8(STACK* stack, uint8_t* status);
uint32_t stack_pop_ui32(STACK* stack, uint8_t* status);
int32_t stack_pop_i32(STACK* stack, uint8_t* status);
float stack_pop_f(STACK* stack, uint8_t* status);
double stack_pop_d(STACK* stack, uint8_t* status);
void* stack_pop_p(STACK* stack, uint8_t* status);

#ifdef ENV_64BITS
bool stack_push_ui64(STACK* stack, uint64_t ui64_t);
bool stack_push_i64(STACK* stack, int64_t i64);
uint64_t stack_pop_ui64(STACK* stack, uint8_t* status);
int64_t stack_pop_i64(STACK* stack, uint8_t* status);
#endif

/**
  @}
  */

#ifdef __cplusplus
}
#endif

#endif
