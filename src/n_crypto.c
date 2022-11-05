#include "nilorea/n_common.h"
#include "nilorea/n_base64.h"
#include "nilorea/n_crypto.h"
#include <string.h>
#include <ctype.h>

int Mod(int a, int b)
{
	return (a % b + b) % b;
}

N_STR *n_vigenere_cypher( N_STR *input , N_STR *key , bool encipher )
{
    for( uint32_t i = 0 ; i < key -> written ; ++i )
    {
        if( !isalpha( key -> data[ i ] ) )
        {
            n_log( LOG_ERR , "key contain bad char '%c'" ,  key -> data[ i ] );
			return NULL ; // Error
        }
    }
    N_STR *dest = nstrdup( input );
    
    uint32_t nonAlphaCharCount = 0;
 
    for( uint32_t i = 0 ; i < input -> written ; i++ ) 
    {
        if( isalpha( input -> data[ i ] ) )
		{
			bool cIsUpper = isupper( input -> data[ i ] );
			char offset = cIsUpper ? 'A' : 'a';
			int keyIndex = ( i - nonAlphaCharCount ) % key -> written ;
			int k = ( cIsUpper ? toupper( key -> data[ keyIndex % key -> written ] ) : tolower( key -> data[ keyIndex % key -> written ] ) ) - offset ;
			k = encipher ? k : -k;
			char ch = (char)( ( Mod( ( ( input -> data[ i ] + k ) - offset ) , 26 ) ) + offset );
			dest -> data[ i ] = ch;
		}
		else
		{
			dest -> data[ i ] = input -> data[ i ];
			++nonAlphaCharCount;
		}
    }
    return dest;
}

N_STR *n_vigenere_encode( N_STR *string , N_STR *key )
{
    return n_vigenere_cypher( string , key , 1 );
}

N_STR *n_vigenere_decode( N_STR *string , N_STR *key )
{
    return n_vigenere_cypher( string , key , 0 );
}

N_STR *n_vigenere_get_question( size_t question_size )
{
    //system("hdparm -i /dev/hda | grep -i serial");
    N_STR *output = NULL ;
    int ret = -1 ;
    n_popen( "wmic path win32_physicalmedia get SerialNumber" , 1024 , (void **)&output , &ret );
    char *replaced = str_replace( output -> data , "\r" , "" );
    char *tmp_str = str_replace( replaced , "\n" , "" );
    Free( replaced );
    replaced = tmp_str ;
    char **output_items = split( replaced , " " , 0 );
    free_nstr( &output );
    Free( replaced );
    output = char_to_nstr( output_items[ 1 ] );
    N_STR *tmpoutput = n_base64_encode( output );
    free_nstr( &output );
    output = new_nstr( question_size + 1 );
    for( size_t it = 0 ; it < question_size ; it ++ )
    {
        output -> data[ it ] = tmpoutput -> data[ it % tmpoutput -> written ] ;

        while( output -> data[ it ] < 'A' ) output -> data[ it ]  += 26 ;
        while( output -> data[ it ] > 'Z' ) output -> data[ it ] -= 26 ;
    }
    output -> written = question_size ;
    free_nstr( &tmpoutput );
    return output ;
}

N_STR *n_vigenere_get_answer( N_STR *root_key , N_STR *question )
{
    N_STR *answer = new_nstr( root_key -> length );
    for( uint32_t it = 0 ; it < root_key -> written ; it ++ )
    {
		int32_t val = ( root_key -> data[ it % root_key -> written ] - 'A' ) + ( question -> data[ it % question -> written ] - 'A' );
        val = val % 26 ;
        val = val + 'A' ;
        answer -> data[ it ] = val ;
    }
    answer -> written = root_key -> written ;
    return answer ;
}

/* must be a multiple of two in length */
int n_vigenere_encode_file( N_STR *in , N_STR *out , N_STR *rootkey )
{
    __n_assert( in , return FALSE );
    __n_assert( out , return FALSE );
    __n_assert( rootkey , return FALSE );

    N_STR *file = file_to_nstr( _nstr( in ) );
    if( !file )
    {
        n_log( LOG_ERR , "error loading %s" , _nstr( in ) );
        return FALSE ;
    }

    N_STR *base64_data = n_base64_encode( file );
    free_nstr( &file );

    N_STR *encoded_data = n_vigenere_encode( base64_data , rootkey );
    free_nstr( &base64_data );

    nstr_to_file( encoded_data , _nstr( out ) );
    free_nstr( &encoded_data );

    return TRUE ;
}

int n_vigenere_decode_file( N_STR *in , N_STR *out , N_STR *rootkey )
{
    __n_assert( in , return FALSE );
    __n_assert( out , return FALSE );
    __n_assert( rootkey , return FALSE );

    N_STR *file = file_to_nstr( _nstr( in ) );
    if( !file )
    {
        n_log( LOG_ERR , "error loading %s" , _nstr( in ) );
        return FALSE ;
    }

    N_STR *decoded_data = n_vigenere_decode( file , rootkey );
    free_nstr( &file );

    N_STR *base64_data = n_base64_decode( decoded_data );
    free_nstr( &decoded_data );

    int ret = nstr_to_file( base64_data , _nstr( out ) );
    free_nstr( &base64_data );

    return ret ;
}

int n_vigenere_decode_file_question( N_STR *in , N_STR *out , N_STR *question , N_STR *answer )
{
    __n_assert( in , return FALSE );
    __n_assert( out , return FALSE );
    __n_assert( question , return FALSE );
    __n_assert( answer , return FALSE );

    N_STR *file = file_to_nstr( _nstr( in ) );
    if( !file )
    {
        n_log( LOG_ERR , "error loading %s" , _nstr( in ) );
        return FALSE ;
    }

    N_STR *encoded_with_answer = n_vigenere_encode( file , question );
    free_nstr( &file );

    N_STR *final_decode = n_vigenere_decode( encoded_with_answer , answer );
    free_nstr( &encoded_with_answer );

    N_STR *base64_data = n_base64_decode( final_decode );
    free_nstr( &final_decode );

    int ret = nstr_to_file( base64_data , _nstr( out ) );
    free_nstr( &base64_data );

    return ret ;
}
