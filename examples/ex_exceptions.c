/**\example ex_exceptions.c Nilorea Library exception api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 24/10/2018
 */

#include "nilorea/n_log.h"
#include "nilorea/n_exceptions.h"

int array_exception( int boolean )
{
    if( boolean )
    {
        n_log( LOG_NOTICE, "Throwed signal" );
        Throw( ARRAY_EXCEPTION );
    }
    n_log( LOG_NOTICE, "No signal Throwed !");

    return 1 ;
}

int divzero_exception( int boolean )
{
    if( boolean )
    {
        n_log( LOG_NOTICE, "Throwed signal" );
        Throw( DIVZERO_EXCEPTION );
    }
    n_log( LOG_NOTICE, "No signal Throwed !");

    return 1 ;
}

int overflow_exception( int boolean )
{
    if( boolean )
    {
        n_log( LOG_NOTICE, "Throwed signal" );
        Throw( OVERFLOW_EXCEPTION );
    }
    n_log( LOG_NOTICE, "No signal Throwed !");

    return 1 ;
}

int parsing_exception( int boolean )
{
    if( boolean )
    {
        n_log( LOG_NOTICE, "Throwed signal" );
        Throw( PARSING_EXCEPTION );
    }
    n_log( LOG_NOTICE, "No signal Throwed !");

    return 1 ;
}

int all_exception( int boolean )
{
    if( boolean )
    {
        n_log( LOG_NOTICE, "Throwed signal" );
        Throw( PARSING_EXCEPTION );
    }
    n_log( LOG_NOTICE, "No signal Throwed !");

    return 1 ;
}


int main( void )
{
    set_log_level( LOG_STDERR );
    set_log_level( LOG_DEBUG );

    puts( "ArrayNoCatch" );
    Try
    {
        n_log( LOG_INFO, "Trying signal:ARRAY_EXCEPTION false" );
        array_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:ARRAY_EXCEPTION true" );
        array_exception( 1 );
    }
    EndTry ;
    puts( "ArrayCatch" );
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:ARRAY_EXCEPTION false" );
        array_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:ARRAY_EXCEPTION true" );
        array_exception( 1 );
    }
    Catch( ARRAY_EXCEPTION )
    {
        n_log( LOG_NOTICE, "Caught signal ARRAY_EXCEPTION" );
    }
    EndTry ;
    /***************************************************************/
    puts( "DivZeroNoCatch" );
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:DIVZERO_EXCEPTION false" );
        divzero_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:DIVZERO_EXCEPTION true" );
        divzero_exception( 1 );
    }
    EndTry ;
    puts( "DivZeroCatch" );
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:DIVZERO_EXCEPTION false" );
        divzero_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:DIVZERO_EXCEPTION true" );
        divzero_exception( 1 );
    }
    Catch( DIVZERO_EXCEPTION )
    {
        n_log( LOG_NOTICE, "Caught signal DIVZERO_EXCEPTION" );
    }
    EndTry ;
    /***************************************************************/
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:OVERFLOW_EXCEPTION false" );
        overflow_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:OVERFLOW_EXCEPTION  true" );
        overflow_exception( 1 );
    }
    EndTry ;
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:OVERFLOW_EXCEPTION false" );
        overflow_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:OVERFLOW_EXCEPTION  true" );
        overflow_exception( 1 );
    }
    Catch( OVERFLOW_EXCEPTION )
    {
        n_log( LOG_NOTICE, "Caught signal OVERFLOW_EXCEPTION " );
    }
    EndTry ;
    /***************************************************************/
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:PARSING_EXCEPTION false" );
        parsing_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:PARSING_EXCEPTION true" );
        parsing_exception( 1 );
    }
    EndTry ;
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:PARSING_EXCEPTION false" );
        parsing_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:PARSING_EXCEPTION true" );
        parsing_exception( 1 );
    }
    Catch( PARSING_EXCEPTION )
    {
        n_log( LOG_NOTICE, "Caught signal PARSING_EXCEPTION" );
    }
    EndTry ;
    /***************************************************************/
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:ALL_EXCEPTION false" );
        all_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:ALL_EXCEPTION true" );
        all_exception( 1 );
    }
    EndTry ;
    Try
    {
        n_log( LOG_NOTICE, "Trying signal:ALL_EXCEPTION false" );
        all_exception( 0 );
        n_log( LOG_NOTICE, "Trying signal:ALL_EXCEPTION true" );
        all_exception( 1 );
    }
    Catch( GENERAL_EXCEPTION )
    {
        n_log( LOG_NOTICE, "Caught signal matching GENERAL_EXCEPTION" );
    }
    EndTry ;


    Try
    {
        n_log( LOG_NOTICE, "Trying mixed Try-Catch blocks, we are inside block 1" );
        Try
        {
            n_log( LOG_NOTICE, "We're inside block 2" );
            Throw( ARRAY_EXCEPTION );

        }
        Catch( GENERAL_EXCEPTION )
        {
            n_log( LOG_NOTICE, "Catched inside block 2" );
        }
        EndTry ;
        Throw( OVERFLOW_EXCEPTION );
    }
    Catch( GENERAL_EXCEPTION )
    {
        n_log( LOG_NOTICE, "Catched inside block 1" );
    }
    EndTry ;

    exit( 0 );
}

