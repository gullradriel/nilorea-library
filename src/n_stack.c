/**\file n_stack.c
 *  Stack functions definitions
 *\author Castagnier Mickael
 *\version 2.0
 *\date 01/01/2018
 */

#include "nilorea/n_stack.h"

STACK *new_stack( size_t size )
{
    STACK *stack = NULL ;

    Malloc( stack , STACK , 1 );
    __n_assert( stack , return NULL );
    Malloc( stack -> stack_array , STACK_ITEM , size );
    __n_assert( stack -> stack_array , Free( stack); return NULL; );

    stack -> size = size ;
    stack -> head = stack -> tail = 0 ;
    stack -> nb_items = 0 ;
    return stack ;
}

bool delete_stack( STACK **stack )
{
    __n_assert( (*stack) , return FALSE );
    Free( (*stack) -> stack_array );
    Free( (*stack) );
    return TRUE ;
}

bool stack_is_full( STACK *stack )
{
    return ( stack -> nb_items == stack -> size );
}

bool stack_is_empty( STACK *stack )
{
    return ( stack -> nb_items == 0 );
}

STACK_ITEM *stack_peek( STACK *stack , size_t position )
{
    STACK_ITEM *item = NULL ;
    __n_assert( stack , return NULL );

    if( stack_is_empty( stack ) )
    {
        return FALSE ;
    }

    if( stack -> tail < stack -> head )
    {
        if( position >= stack -> tail && position <= stack -> head && position < stack -> size && stack -> stack_array[ position ] . is_set )
        {
            item = &stack -> stack_array[ position ];
        }
    }
    else if( stack -> tail > stack -> head )
    {
        if( position >= stack -> head && position <= stack -> tail && position < stack -> size && stack -> stack_array[ position ] . is_set )
        {
            item = &stack -> stack_array[ position ];
        }
    }
    return item ;
}

size_t __stack_push( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return 0 );
   
    (*status) = STACK_IS_FULL ;
    if( stack_is_full( stack ) )
        return 0 ;

    size_t next_pos = ( stack -> head + 1 ) % stack -> size ;

    // if next_pos == tail, the stack is full
    if( next_pos == stack -> tail )
    {
        // status already set to (*status) = STACK_IS_FULL ;
        return 0 ;
    }

    // set data, move, set item status, inc counter
    (*status) = STACK_ITEM_OK ;
    stack -> stack_array[ stack -> head ] . is_set = 1 ;
    stack -> stack_array[ stack -> head ] . is_empty = 0 ;
    // inc
    stack -> nb_items ++ ;
    return next_pos ;
}

size_t __stack_pop( STACK *stack , uint8_t *status )
{
    __n_assert( stack , return FALSE );

    size_t next_pos = 0;
    size_t prev_pos = 0;

    // if the head == tail, the stack is empty
    if( stack -> head == stack -> tail )
    {
        (*status) = STACK_IS_EMPTY ;
        return 0;
    }

    // item is here and read    
    (*status) = STACK_ITEM_OK ;

    // next is where tail will point to after this read
    next_pos = (stack -> tail + 1) % stack -> size;

    // set item status, dec counter, move, return value
    stack -> stack_array[ stack -> tail ] . is_set = 0 ;
    stack -> stack_array[ stack -> tail ] . is_empty = 1 ;
    prev_pos = stack -> tail ;
    // tail to next offset.
    stack -> tail = next_pos ;              
    // dec
    stack -> nb_items -- ;
    return prev_pos ;
}

bool stack_push_b( STACK *stack , bool b )
{
    uint8_t status = STACK_ITEM_OK ;
    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;
    stack -> stack_array[ stack -> head ] . data . b = b; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_BOOL ;
    stack -> head = next_pos ;

    return TRUE ;
}


bool stack_pop_b( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_BOOL )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . b ;  
}

bool stack_push_c( STACK *stack , char c )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . c = c; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_CHAR ;
    stack -> head = next_pos ;

    return TRUE ;
}


char stack_pop_c( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_CHAR )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . c ;  
}

bool stack_push_ui8( STACK *stack , uint8_t ui8 )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . ui8 = ui8; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_UINT8 ;
    stack -> head = next_pos ;

    return TRUE ;
}


uint8_t stack_pop_ui8( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_UINT8 )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . ui8 ;  
}

bool stack_push_i8( STACK *stack , int8_t i8 )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . i8 = i8; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_INT8 ;
    stack -> head = next_pos ;

    return TRUE ;
}


int8_t stack_pop_i8( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_INT8 )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . i8 ;  
}

bool stack_push_ui32( STACK *stack , uint32_t ui32 )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . ui32 = ui32; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_UINT32 ;
    stack -> head = next_pos ;

    return TRUE ;
}


uint32_t stack_pop_ui32( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_UINT32 )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . ui32 ;  
}

bool stack_push_i32( STACK *stack , int32_t i32 )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . i32 = i32; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_INT32 ;
    stack -> head = next_pos ;

    return TRUE ;
}


int32_t stack_pop_i32( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_INT32 )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . i32 ;  
}

bool stack_push_f( STACK *stack , float f )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . f = f ; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_FLOAT ;
    stack -> head = next_pos ;

    return TRUE ;
}


float stack_pop_f( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_FLOAT )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . f ;  
}

bool stack_push_d( STACK *stack , double d )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . d = d ; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_DOUBLE ;
    stack -> head = next_pos ;

    return TRUE ;
}


double stack_pop_d( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_DOUBLE )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . d ;  
}

bool stack_push_p( STACK *stack , void *p , uint16_t p_type )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . p = p ; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_PTR ;
    stack -> stack_array[ stack -> head ] . p_type = p_type ;
    stack -> head = next_pos ;

    return TRUE ;
}


void *stack_pop_p( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_PTR )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . p ;  
}

#ifdef ENV_64BITS        

bool stack_push_ui64( STACK *stack , uint64_t ui64 )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . ui64 = ui64; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_UINT64 ;
    stack -> head = next_pos ;

    return TRUE ;
}


uint64_t stack_pop_ui64( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_UINT64 )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . ui64 ;  
}

bool stack_push_i64( STACK *stack , int64_t i64 )
{
    uint8_t status = STACK_ITEM_OK ;

    size_t next_pos = __stack_push( stack , &status );
    if( status != STACK_ITEM_OK )
        return FALSE ;

    stack -> stack_array[ stack -> head ] . data . i64 = i64; 
    stack -> stack_array[ stack -> head ] . v_type = STACK_ITEM_INT64 ;
    stack -> head = next_pos ;

    return TRUE ;
}


int64_t stack_pop_i64( STACK *stack , uint8_t *status )
{
    (*status) = STACK_IS_UNDEFINED ;
    __n_assert( stack , return FALSE );

    if( stack -> stack_array[ stack -> tail ] . v_type != STACK_ITEM_INT64 )
    {
        (*status) = STACK_ITEM_WRONG_TYPE ;
        return 0 ;
    }

    uint8_t pop_status = STACK_IS_UNDEFINED ;
    size_t prev_pos = __stack_pop( stack , &pop_status );
    if( pop_status != STACK_ITEM_OK )
    {
        (*status) = pop_status ;
        return 0 ;
    }
    return stack -> stack_array[ prev_pos ] . data . i64 ;  
}
#endif

