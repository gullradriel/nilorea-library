/**\example ex_crypto.c Nilorea Library crypto vigenere api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */

#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_str.h"
#include "nilorea/n_base64.h"
#include "nilorea/n_crypto.h"

int main(int argc, char* argv[]) {
    void usage(void) {
        fprintf(stderr,
                "%s usage:\n"
                "     -i input_file [optional] use a file as datas instead of hard coded 3 lines text\n"
                "     -v version\n"
                "     -V log level: LOG_INFO, LOG_NOTICE, LOG_ERR, LOG_DEBUG\n"
                "     -h help\n",
                argv[0]);
    }

    int getoptret = 0, ret = 0,
        log_level = LOG_DEBUG; /* default log level */

    N_STR* input_name = NULL;

    /* Arguments optionnels */
    /* -v version
     * -V log level
     * -h help
     */
    while ((getoptret = getopt(argc, argv, "i:hvV:")) != EOF) {
        switch (getoptret) {
            case 'i':
                input_name = char_to_nstr(optarg);
                break;
            case 'v':
                fprintf(stderr, "Date de compilation : %s a %s.\n", __DATE__, __TIME__);
                exit(1);
            case 'V':
                if (!strncmp("LOG_NULL", optarg, 5)) {
                    log_level = LOG_NULL;
                } else {
                    if (!strncmp("LOG_NOTICE", optarg, 6)) {
                        log_level = LOG_NOTICE;
                    } else {
                        if (!strncmp("LOG_INFO", optarg, 7)) {
                            log_level = LOG_INFO;
                        } else {
                            if (!strncmp("LOG_ERR", optarg, 5)) {
                                log_level = LOG_ERR;
                            } else {
                                if (!strncmp("LOG_DEBUG", optarg, 5)) {
                                    log_level = LOG_DEBUG;
                                } else {
                                    fprintf(stderr, "%s n'est pas un niveau de log valide.\n", optarg);
                                    exit(-1);
                                }
                            }
                        }
                    }
                }
                break;
            default:
            case '?': {
                if (optopt == 'V') {
                    fprintf(stderr, "                                  =>      Missing log level\n");
                } else if (optopt == 'p') {
                    fprintf(stderr, "                                  =>      Missing port\n");
                } else if (optopt == 'i') {
                    fprintf(stderr, "                                  =>      Missing filename\n");
                } else if (optopt != 's') {
                    fprintf(stderr, "                                  =>      Unknow missing option %c\n", optopt);
                }
                usage();
                exit(1);
            }
            case 'h': {
                usage();
                exit(1);
            }
        }
    }
    set_log_level(log_level);

    N_STR* EXAMPLE_TEXT = char_to_nstr(
        "##############################################################\n"
        "# This is an example of crypto encode/decode, with a newline #\n"
        "# This is the end of the test                                #\n"
        "##############################################################");

    n_log(LOG_NOTICE, "Testing crypto, encoding text of size (%ld/%ld):\n%s", EXAMPLE_TEXT->written, EXAMPLE_TEXT->length, _nstr(EXAMPLE_TEXT));
    /*                                   10        20        30        40        50        60     */
    /*                         "1234567890123456789012345678901234567890123456789012345678901234" */
    // N_STR *key = n_vigenere_get_rootkey( 128 );
    N_STR* key = char_to_nstr("RKCHSLZWFNASULJFVPRVUUUAEUVEMSSGEVNWIMVPZVIWITBGIUBEQVEURBYEWTMCQZZYNWQMRAIBTMHDIKIZHOVZQUFONRQDSRDFNTTGVEKOSTSAEABLOXMGTTWIMPNE");
    n_log(LOG_DEBUG, "key= %s", _nstr(key));

    N_STR* B64_EXAMPLE_TEXT = n_base64_encode(EXAMPLE_TEXT);
    N_STR* encoded_data = n_vigenere_encode(B64_EXAMPLE_TEXT, key);
    n_log(LOG_DEBUG, "encoded data=\n%s", _nstr(encoded_data));

    N_STR* decoded_data = n_vigenere_decode(encoded_data, key);
    N_STR* unbase64 = n_base64_decode(decoded_data);
    n_log(LOG_DEBUG, "decoded data=\n%s", _nstr(unbase64));
    free_nstr(&unbase64);

    N_STR* encoded_question = n_vigenere_get_question(128);
    N_STR* decoded_question = n_vigenere_quick_decode(encoded_question);
    n_log(LOG_DEBUG, "encoded_question: %s", _nstr(encoded_question));
    n_log(LOG_DEBUG, "decoded_question: %s", _nstr(decoded_question));
    free_nstr(&decoded_question);

    N_STR* answer = n_vigenere_get_answer(key, encoded_question);
    n_log(LOG_DEBUG, "answer: %s", _nstr(answer));

    N_STR* decode_qa = n_vigenere_decode_qa(encoded_data, encoded_question, answer);
    n_log(LOG_DEBUG, "encode root decode qa:\n%s", _nstr(decode_qa));
    free_nstr(&decode_qa);

    N_STR* encode_qa = n_vigenere_encode_qa(EXAMPLE_TEXT, encoded_question, answer);
    n_log(LOG_DEBUG, "encode qa:\n%s", _nstr(encode_qa));

    decode_qa = n_vigenere_decode_qa(encode_qa, encoded_question, answer);
    n_log(LOG_DEBUG, "encode qa decode qa:\n%s", _nstr(decode_qa));
    free_nstr(&decode_qa);

    N_STR* decode_qa_root = n_vigenere_decode(encode_qa, key);
    unbase64 = n_base64_decode(decode_qa_root);
    n_log(LOG_DEBUG, "encode qa - decode root:\n%s", _nstr(unbase64));

    free_nstr(&decode_qa_root);
    free_nstr(&encode_qa);

    if (input_name) {
        N_STR* output_name_enc = NULL;
        N_STR* output_name_dec = NULL;
        N_STR* output_name_dec_question = NULL;

        nstrprintf(output_name_enc, "%s.crypt_encoded", _nstr(input_name));
        if (n_vigenere_encode_file(input_name, output_name_enc, key)) {
            n_log(LOG_DEBUG, "Encoded file saved in %s", _nstr(output_name_enc));

            nstrprintf(output_name_dec, "%s.crypt_decoded", _nstr(input_name));
            if (n_vigenere_decode_file(output_name_enc, output_name_dec, key)) {
                n_log(LOG_DEBUG, "Decoded file saved in %s", _nstr(output_name_dec));

                nstrprintf(output_name_dec_question, "%s.crypt_decoded_question", _nstr(input_name));
                if (n_vigenere_decode_file_qa(output_name_enc, output_name_dec_question, encoded_question, answer)) {
                    n_log(LOG_DEBUG, "Decoded with question/answer file saved in %s", _nstr(output_name_dec_question));
                } else {
                    n_log(LOG_ERR, "unable to decode with question answer");
                    ret = 1;
                }
            } else {
                n_log(LOG_ERR, "unable to decode with root key");
                ret = 1;
            }
        } else {
            n_log(LOG_ERR, "unable to encode with root key");
            ret = 1;
        }

        free_nstr(&input_name);
        free_nstr(&output_name_enc);
        free_nstr(&output_name_dec);
        free_nstr(&output_name_dec_question);
    }

    free_nstr(&key);
    free_nstr(&encoded_data);
    free_nstr(&decoded_data);
    free_nstr(&encoded_question);
    free_nstr(&answer);
    free_nstr(&EXAMPLE_TEXT);
    free_nstr(&B64_EXAMPLE_TEXT);
    free_nstr(&unbase64);

    exit(ret);
} /* END_OF_MAIN */
