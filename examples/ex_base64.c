/**\example ex_base64.c Nilorea Library crypto base64 api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */

#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_str.h"
#include "nilorea/n_base64.h"

int main(int argc, char* argv[]) {
    set_log_level(LOG_DEBUG);

    N_STR* example_text = char_to_nstr("This is an example of base64 encode/decode, with a newline:\nThis is the end of the testing test.");
    example_text->written = 0;
    while (example_text->data[example_text->written] != '\0') {
        example_text->written++;
    }

    n_log(LOG_NOTICE, "Testing base64, encoding text of size (%ld/%ld):\n%s", example_text->written, example_text->length, _nstr(example_text));

    N_STR* encoded_answer = n_base64_encode(example_text);
    n_log(LOG_DEBUG, "encoded=\n%s", _nstr(encoded_answer));

    N_STR* decoded_answer = n_base64_decode(encoded_answer);
    n_log(LOG_DEBUG, "decoded=\n%s", _nstr(decoded_answer));

    free_nstr(&encoded_answer);
    free_nstr(&decoded_answer);

    if (argc > 0 && argv[1]) {
        N_STR* input_data = NULL;
        N_STR* output_name = NULL;
        N_STR* output_data = NULL;
        N_STR* decoded_data = NULL;

        nstrprintf(output_name, "%s.encoded", argv[1]);
        input_data = file_to_nstr(argv[1]);
        n_log(LOG_DEBUG, "Encoding file %s size %ld", argv[1], input_data->written);

        if (input_data) {
            output_data = n_base64_encode(input_data);
            nstr_to_file(output_data, _nstr(output_name));
            n_log(LOG_DEBUG, "Generated encoded %s version of size %ld", _nstr(output_name), output_data->written);

            nstrprintf(output_name, "%s.decoded", argv[1]);
            decoded_data = n_base64_decode(output_data);
            nstr_to_file(decoded_data, _nstr(output_name));

            n_log(LOG_DEBUG, "Generated decoded %s version of size %ld", _nstr(output_name), decoded_data->written);
            free_nstr(&decoded_data);
        }
        free_nstr(&input_data);
        free_nstr(&output_name);
        free_nstr(&output_data);
    }
    free_nstr(&example_text);
    exit(0);
} /* END_OF_MAIN */
