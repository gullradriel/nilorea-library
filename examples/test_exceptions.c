#include <stdio.h>
#include <memory.h>



#include "n_exceptions.h"



int array_exception( int boolean )
{
	if( boolean )
	{
		printf( "Throwed signal\n" );
		Throw( ARRAY_EXCEPTION );
	}
	printf( "No signal Throwed !\n");
	
	return 1 ;
}

int divzero_exception( int boolean )
{
	if( boolean )
	{
		printf( "Throwed signal\n" );
		Throw( DIVZERO_EXCEPTION );
	}
	printf( "No signal Throwed !\n");
	
	return 1 ;
}

int overflow_exception( int boolean )
{
	if( boolean )
	{
		printf( "Throwed signal\n" );
		Throw( OVERFLOW_EXCEPTION );
	}
	printf( "No signal Throwed !\n");
	
	return 1 ;
}

int parsing_exception( int boolean )
{
	if( boolean )
	{
		printf( "Throwed signal\n" );
		Throw( PARSING_EXCEPTION );
	}
	printf( "No signal Throwed !\n");
	
	return 1 ;
}

int all_exception( int boolean )
{
	if( boolean )
	{
		printf( "Throwed signal\n" );
		Throw( PARSING_EXCEPTION );
	}
	printf( "No signal Throwed !\n");
	
	return 1 ;
}


int main( int argc, char *argv[] )
{
	allegro_init();
	/***************************************************************/
	puts( "ArrayNoCatch" );
	Try
	{
		printf( "Trying signal:ARRAY_EXCEPTION false\n" );
		array_exception( 0 );
		printf( "Trying signal:ARRAY_EXCEPTION true\n" );
		array_exception( 1 );
	}
	EndTry ;
	puts( "ArrayCatch" );
	Try
	{
		printf( "Trying signal:ARRAY_EXCEPTION false\n" );
		array_exception( 0 );
		printf( "Trying signal:ARRAY_EXCEPTION true\n" );
		array_exception( 1 );
	}
	Catch( ARRAY_EXCEPTION )
	{
		printf( "Caught signal ARRAY_EXCEPTION\n" );
	}
	EndTry ;
	/***************************************************************/
	puts( "DivZeroNoCatch" );
	Try
	{
		printf( "Trying signal:DIVZERO_EXCEPTION false\n" );
		divzero_exception( 0 );
		printf( "Trying signal:DIVZERO_EXCEPTION true\n" );
		divzero_exception( 1 );
	}
	EndTry ;
	puts( "DivZeroCatch" );
	Try
	{
		printf( "Trying signal:DIVZERO_EXCEPTION false\n" );
		divzero_exception( 0 );
		printf( "Trying signal:DIVZERO_EXCEPTION true\n" );
		divzero_exception( 1 );
	}
	Catch( DIVZERO_EXCEPTION )
	{
		printf( "Caught signal DIVZERO_EXCEPTION\n" );
	}
	EndTry ;
	/***************************************************************/
	Try
	{
		printf( "Trying signal:OVERFLOW_EXCEPTION false\n" );
		overflow_exception( 0 );
		printf( "Trying signal:OVERFLOW_EXCEPTION  true\n" );
		overflow_exception( 1 );
	}
	EndTry ;
	Try
	{
		printf( "Trying signal:OVERFLOW_EXCEPTION false\n" );
		overflow_exception( 0 );
		printf( "Trying signal:OVERFLOW_EXCEPTION  true\n" );
		overflow_exception( 1 );
	}
	Catch( OVERFLOW_EXCEPTION )
	{
		printf( "Caught signal OVERFLOW_EXCEPTION \n" );
	}
	EndTry ;
	/***************************************************************/
	Try
	{
		printf( "Trying signal:PARSING_EXCEPTION false\n" );
		parsing_exception( 0 );
		printf( "Trying signal:PARSING_EXCEPTION true\n" );
		parsing_exception( 1 );
	}
	EndTry ;
	Try
	{
		printf( "Trying signal:PARSING_EXCEPTION false\n" );
		parsing_exception( 0 );
		printf( "Trying signal:PARSING_EXCEPTION true\n" );
		parsing_exception( 1 );
	}
	Catch( PARSING_EXCEPTION )
	{
		printf( "Caught signal PARSING_EXCEPTION\n" );
	}
	EndTry ;
	/***************************************************************/
	Try
	{
		printf( "Trying signal:ALL_EXCEPTION false\n" );
		all_exception( 0 );
		printf( "Trying signal:ALL_EXCEPTION true\n" );
		all_exception( 1 );
	}
	EndTry ;
	Try
	{
		printf( "Trying signal:ALL_EXCEPTION false\n" );
		all_exception( 0 );
		printf( "Trying signal:ALL_EXCEPTION true\n" );
		all_exception( 1 );
	}
	Catch( GENERAL_EXCEPTION )
	{
		printf( "Caught signal matching GENERAL_EXCEPTION\n" );
	}
	EndTry ;


	Try
	{
		printf( "Trying mixed Try-Catch blocks, we are inside block 1\n" );
		Try
		{
			printf( "We're inside block 2\n" );
			Throw( ARRAY_EXCEPTION );

		}
		Catch( GENERAL_EXCEPTION )
		{
			printf( "Catched inside block 2\n" );
		}
		EndTry ;
		Throw( OVERFLOW_EXCEPTION );
	}
	Catch( GENERAL_EXCEPTION )
	{
		printf( "Catched inside block 1\n" );

	}
	EndTry ;
	
	return 1 ;
	
}END_OF_MAIN()


