/**\file n_neural_networks.c
 *  Neural network declarations
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/07/2015
 */

#include "nilorea/n_neural_networks.h"
#include <math.h>
#include <float.h>
#include "nilorea/n_common.h"

int rand_between_ints( int a, int b )
{
    return rand()%( a - b + 1 ) + b;
}

double rand_between_doubles( double a, double b )
{
    return rand()%( (int)( a - b + 1 ) ) + b;
}

double rand_equal_double( double a, double b )
{
    return ( (double) rand() / DBL_MAX ) * ( b - a ) + a;
}

int rand_equal_int( int a, int b )
{
    return rand()%( b - a + 1 ) + a;
}


/* basic neural network activation function: sigmoid */
double n_neural_sigmoid ( double x )
{
    return 1.0 /( 1 + exp( -x ) );
}

/*! allocate a new layer */
N_NEURAL_LAYER *new_neural_net_layer( int size_x, int size_y )
{
    N_NEURAL_LAYER *layer = NULL ;

    if( size_x < 0 || size_y < 0 )
        return NULL ;

    Malloc( layer, N_NEURAL_LAYER, 1 );
    __n_assert( layer, return NULL );

    layer -> array = calloc( sizeof( N_PERCEPTRON ), size_x );
    for( int itx = 0; itx < size_x ; itx++ )
    {
        Malloc( layer -> array[ itx ],  N_PERCEPTRON , size_y );
        for( int ity = 0 ; ity < size_y ; ity ++ );
    }

}

typedef struct N_NEURAL_LAYER
{
    /*! Width of the layer */
    int size_x,
        /*!	Height of the layer */
        size_y ;
    /*! Dynamically allocated array of perceptron. Some can be NULL. */
    N_PERCEPTRON ***array ;

} N_NEURAL_LAYER ;


/* Create a new neural network */
NEURAL_NETWORK *new_neural_net( char *name, int input_x, int input_y )
{
    __n_assert( name, return NULL );

    NEURAL_NETWORK *neural_network = NULL ;
    Malloc( neural_net, NEURAL_NETWORK, 1 );
    if( !neural_net )
        return NULL ;

    neural_net -> network = new_generic_list( 0 );





    return neural net ;

}

/* add a new layer */
int add_neural_net_layer( NEURAL_NETWORK *neural_net, int x, int y )
{

}

int set_perceptron( NEURAL_NETWORK *neural_net, int layer, int x, int y, int a_func, double bias, double treshold, double output )
{

}


int unset_perceptron( NEURAL_NETWORK *neural_net, int layer, int x, int y )
{

}


int add_perceptron_input( N_PERCEPTRON *n_perceptron, N_PERCEPTRON *input )
{

}

int neural_net_set_input( NEURAL_NETWORK *neural_net, double **input )
{

}

int neural_net_compute( NEURAL_NETWORK *neural_net, double *output )
{
    __n_assert( neural_net, return FALSE );
    __n_assert( neural_net ->  network, return FALSE );
    __n_assert( neural_net ->  network -> start, return FALSE );
    __n_assert( neural_net ->  network -> start  -> ptr, return FALSE );
    __n_assert( neural_net ->  network -> intput, return FALSE );

    /* processing input data to input layer */
    N_NEURAL_LAYER *layer = NULL ;
    layer = __n_assert( neural_net ->  network -> start  -> ptr );
    for( int x = 0 ; x < layer -> x_size ; x ++ )
    {
        for( int y = 0 ; y < layer -> y_size ; y ++ )
        {
            if( layer -> array[ x ][ y ] != NULL )
            {
                layer -> array[ x ][ y ] -> output = neural_net -> input[ x ][ y ] ;
            }
        }
    }

    return TRUE ;
}



void PropagateLayer(NET* Net, LAYER* Lower, LAYER* Upper)
{
    INT  i,j;
    REAL Sum;

    for (i=1; i<=Upper->Units; i++)
    {
        Sum = 0;
        for (j=0; j<=Lower->Units; j++)
        {
            Sum += Upper->Weight[i][j] * Lower->Output[j];
        }
        Upper->Output[i] = 1 / (1 + exp(-Net->Gain * Sum));
    }
}


