/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 *@example ex_base64.c
 *@brief Nilorea Library crypto base64 api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/05/2015
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

    if (argc > 1 && argv[1]) {
        N_STR* input_data = NULL;
        N_STR* output_name = NULL;
        N_STR* output_data = NULL;
        N_STR* decoded_data = NULL;

        nstrprintf(output_name, "%s.encoded", argv[1]);
        input_data = file_to_nstr(argv[1]);

        if (input_data) {
            n_log(LOG_DEBUG, "Encoding file %s size %ld", argv[1], input_data->written);
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

    /* test character classification and conversion helpers */
    n_log(LOG_INFO, "n_isupper('A'): %d", n_isupper('A'));
    n_log(LOG_INFO, "n_isupper('a'): %d", n_isupper('a'));
    n_log(LOG_INFO, "n_islower('z'): %d", n_islower('z'));
    n_log(LOG_INFO, "n_islower('Z'): %d", n_islower('Z'));
    n_log(LOG_INFO, "n_isalpha('B'): %d", n_isalpha('B'));
    n_log(LOG_INFO, "n_isalpha('5'): %d", n_isalpha('5'));
    n_log(LOG_INFO, "n_toupper('c'): %c", n_toupper('c'));
    n_log(LOG_INFO, "n_tolower('D'): %c", n_tolower('D'));

    exit(0);
} /* END_OF_MAIN */
