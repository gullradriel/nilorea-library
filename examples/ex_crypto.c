/**\example ex_base64.c Nilorea Library list api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */


#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_str.h"
#include "nilorea/n_base64.h"
#include "nilorea/n_crypto.h"


int main( int argc , char *argv[] )
{
    set_log_level( LOG_DEBUG );

    N_STR *EXAMPLE_TEXT = char_to_nstr( 
            "##############################################################\n"
            "# This is an example of crypto encode/decode, with a newline #\n"
            "# This is the end of the test                                #\n" 
            "##############################################################" );
    EXAMPLE_TEXT -> written= 0 ;
    while( EXAMPLE_TEXT -> data[ EXAMPLE_TEXT -> written ] != '\0' )
    {
        EXAMPLE_TEXT -> written ++ ;
    }

    n_log( LOG_NOTICE, "Testing crypto, encoding text of size (%ld/%ld):\n%s" , EXAMPLE_TEXT -> written , EXAMPLE_TEXT -> length , _nstr( EXAMPLE_TEXT ) );
    /*                                   10        20        30        40        50        60     */
    /*                         "1234567890123456789012345678901234567890123456789012345678901234" */
    N_STR *key = char_to_nstr( "AZERTYQCFCCVLKNLKNKJNHGFCGFGHJVSQCLJNSJNCJNQSJKTYUYUOIZPOPOOISQD" );
    n_log( LOG_DEBUG , "key= %s" , _nstr( key ) );

    N_STR *B64_EXAMPLE_TEXT = n_base64_encode( EXAMPLE_TEXT );
    n_log( LOG_DEBUG , "b64encode: %d/%d" , B64_EXAMPLE_TEXT -> written , B64_EXAMPLE_TEXT -> length );

    N_STR *encoded_data = n_vigenere_encode( B64_EXAMPLE_TEXT , key );
    n_log( LOG_DEBUG , "encoded=\n%s" , _nstr( encoded_data ) );

    N_STR *decoded_data = n_vigenere_decode( encoded_data , key );
    N_STR *unbase64 = n_base64_decode( decoded_data );
    n_log( LOG_DEBUG , "decoded=\n%s" , _nstr( unbase64 ) );
    free_nstr( &unbase64 );

    N_STR *question = n_vigenere_get_question( 32 );
    n_log( LOG_DEBUG , "question: %s" , _nstr( question ) );

    N_STR *encoded_with_question_data = n_vigenere_encode( encoded_data , question );
    n_log( LOG_DEBUG , "encoded_with_question_data=\n%s" , _nstr( encoded_with_question_data ) );

    N_STR *answer = n_vigenere_get_answer( key , question );    
    n_log( LOG_DEBUG , "answer: %s" , _nstr( answer ) );

    N_STR *final_decode = n_vigenere_decode( encoded_with_question_data , answer );
    unbase64 = n_base64_decode( final_decode );
    n_log( LOG_DEBUG , "decoded with answer=\n%s" , _nstr( unbase64 ) );


    if( argc > 0 && argv[ 1 ] )
    {
        N_STR *input_name = char_to_nstr( argv[ 1 ] );
        N_STR *output_name_enc = NULL ;
        N_STR *output_name_dec = NULL ;
        N_STR *output_name_dec_question = NULL ;

        nstrprintf( output_name_enc , "%s.crypt_encoded" , argv[ 1 ] );
        n_vigenere_encode_file( input_name , output_name_enc , key );
        n_log( LOG_DEBUG , "Encoded file saved in %s" , _nstr( output_name_enc ) );

        nstrprintf( output_name_dec , "%s.crypt_decoded" , argv[ 1 ] );
        n_vigenere_decode_file( output_name_enc , output_name_dec , key );
        n_log( LOG_DEBUG , "Decoded file saved in %s" , _nstr( output_name_dec ) );

        nstrprintf( output_name_dec_question , "%s.crypt_decoded_question" , argv[ 1 ] );
        n_vigenere_decode_file_question( output_name_enc , output_name_dec_question , question , answer );
        n_log( LOG_DEBUG , "Decoded with question/answer file saved in %s" , _nstr( output_name_dec_question ) );

        free_nstr( &input_name );
        free_nstr( &output_name_enc );
        free_nstr( &output_name_dec );
        free_nstr( &output_name_dec_question );
    }

    free_nstr( &key );
    free_nstr( &encoded_data );
    free_nstr( &decoded_data );
    free_nstr( &question );
    free_nstr( &encoded_with_question_data );
    free_nstr( &answer );
    free_nstr( &final_decode );
    free_nstr( &EXAMPLE_TEXT );
    free_nstr( &B64_EXAMPLE_TEXT );
    free_nstr( &unbase64 );

    exit( 0 );
} /* END_OF_MAIN */
