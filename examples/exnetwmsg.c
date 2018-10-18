#include <nilorea.h>

NETW_MSG *test = NULL ;
N_STR *str = NULL;

double float_val = 0.0 ;
int it = 0 , int_val = 0 ;

size_t fsize = 0 ;

FILE *out = NULL;

int main( void )
{
	set_log_level( LOG_DEBUG );

	allegro_init();

	create_msg( &test );

	printf("\nAdding floating nb:");
	for( it = 0 ; it < 5 ; it ++ )
	{
		float_val = (100.0-rand()%200)/3.0 ;
        if( add_nb_to_msg( test , float_val ) == FALSE )
        {
            printf( "Error adding %g\n" , float_val );
        }
        else
        {
            printf(" %g" , float_val);
        }
    }
	
	printf("\nAdding integer nb:");
	for( it = 0 ; it < 5 ; it ++ )
	{
		int_val = (100-rand()%200)/3 ;
        if( add_int_to_msg( test , int_val ) == FALSE )
        {
            printf( "Error adding %d\n" , int_val );
        }
        else
        {
            printf(" %d" , int_val);
        }
    }

    printf("\nAdding str:");

    for( it = 0 ; it < 10 ; it ++ )
    {

        int_val = 1+rand()%10;

        str = new_str( int_val );
		
        fill_str( str -> data , 'A' , int_val-1 ) ;

        str->data[ int_val - 1 ] = 0;
		str->written = int_val - 1 ;

        add_strptr_to_msg( test , str );

        printf("adding:%3d:%s\n", str -> length , str -> data );


    }

    printf("Making str from message\n");

    str = make_str_from_msg( test );
	printf("writing to test.log %d char\n",str -> length );
    out = fopen( "test.log","wb" );
	fwrite( str -> data , sizeof( char ) ,str -> length , out );
	fclose( out);

	puts(" ");

	printf( "Killing the message now we have a string\n" );

    delete_msg( &test );

    printf("making a new msg from the string we have\n" );

    test = make_msg_from_str( str );

    printf("printing what we get from our msg\n");

	while( get_nb_from_msg( test , &float_val ) != FALSE )
		printf( "double:%g\n" , float_val );

	while( get_int_from_msg( test , &int_val ) != FALSE )
		printf( "int:%d\n" , int_val );

	while( ( str = get_str_from_msg( test ) ) != NULL )
		printf( "getting %d:%s\n" ,  str -> length , str -> data  );


	printf("delete_msg\n");

	delete_msg( &test );
	
	Malloc( str , N_STR , 1 );
	out = fopen( "test.log" , "r+" );
	fseek( out , 0 , SEEK_END );
	fsize = ftell( out );
	fseek( out , 0 , SEEK_SET);
	
	Malloc( str -> data , char , fsize );
	fread( str -> data , 1 , fsize , out );
	
	fclose(out);
	
	test = make_msg_from_str( str );

    printf("printing what we get from our file msg\n");

	while( get_nb_from_msg( test , &float_val ) )
		printf( "double:%g\n" , float_val );
		
	while( get_int_from_msg( test , &int_val ) )
		printf( "int:%d\n" , int_val );

	while( ( str = get_str_from_msg( test ) ) != NULL )
		printf( "getting %d:%s\n" ,  str -> length , str -> data  );

	/*
	 *	N_STR test
	 */
	printf( "creating new str\n" );
	
	N_STR *nstr = new_str( 10 );
	
	for( int it = 0 ; it < 10 ; it ++ )
	{
		int result = nstrcat( nstr , "aaa" , 3 );
		int result = nstrcat( nstr , nstr2->data , nstr2->length );
		
		printf( "Result: %s string: %d/%d ,  %s\n" , (result==TRUE)?"TRUE":"FALSE" , nstr -> written , nstr -> length , _str( totonstr -> data ) );
	}
	
	free_str( &nstr );
		
	nstr = file_to_nstr( "TESTFILE.TXT" );
	if( !nstr )
	{
		printf( "Error loading TESTFILE.TXT" );
		return FALSE ;
	}
	
	printf( "File:%d/%d\n%s\n" , nstr->written , nstr -> length , nstr -> data );
	
	if( str_to_file( nstr , "TESTFILE.OUT" ) != TRUE )
	{
		printf( "Error writing to TESTFILE.OUT\n" );
	}
	else
	{
		printf( "SUCCESS !\n");
	}
			
    return 0;

}END_OF_MAIN()

