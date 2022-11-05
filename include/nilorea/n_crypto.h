#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdlib.h>
#include <memory.h>
#include "nilorea/n_str.h"

N_STR *n_vigenere_encode( N_STR *string , N_STR *key );
N_STR *n_vigenere_decode( N_STR *string , N_STR *key );
N_STR *n_vigenere_get_question( size_t question_size );
N_STR *n_vigenere_get_answer( N_STR *root_key , N_STR *question );
int n_vigenere_encode_file( N_STR *in , N_STR *out , N_STR *rootkey );
int n_vigenere_decode_file( N_STR *in , N_STR *out , N_STR *rootkey );
int n_vigenere_decode_file_question( N_STR *in , N_STR *out , N_STR *question , N_STR *answer );

#endif //BASE46_H
