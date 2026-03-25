/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@example ex_nstr.c
 *@brief Nilorea Library string  api test
 *@author Castagnier Mickael
 *@version 1.0
 *@date 26/05/2015
 */

#include "nilorea/n_str.h"
#include "nilorea/n_log.h"

int main(void) {
    set_log_level(LOG_DEBUG);

    char* chardest = NULL;
    NSTRBYTE written = 0,
             length = 0;

    write_and_fit(&chardest, &length, &written, "Hello");
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit(&chardest, &length, &written, " ");
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit(&chardest, &length, &written, "world !");
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit(&chardest, &length, &written, "world ! ");
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit(&chardest, &length, &written, "world ! ");
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit(&chardest, &length, &written, "world ! ");
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);

    Free(chardest);
    written = length = 0;

    write_and_fit_ex(&chardest, &length, &written, "Hello", 5, 0);
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit_ex(&chardest, &length, &written, " ", 1, 0);
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit_ex(&chardest, &length, &written, "world !", 7, 0);
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit_ex(&chardest, &length, &written, "Hello", 5, 0);
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit_ex(&chardest, &length, &written, " ", 1, 10);  // alocate 10 more byte if resize needed
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);
    write_and_fit_ex(&chardest, &length, &written, "world !", 7, 0);
    n_log(LOG_INFO, "charstr (%d/%d): %s", written, length, chardest);

    Free(chardest);

    N_STR* nstr = NULL;

    n_log(LOG_INFO, "NULL str:%s", _nstr(nstr));

    nstrprintf(nstr, "Hello, file is %s line %d date %s", __FILE__, __LINE__, __TIME__);

    n_log(LOG_INFO, "str:%s", _nstr(nstr));

    nstrprintf_cat(nstr, " - This will be added at file %s line %d date %s", __FILE__, __LINE__, __TIME__);

    n_log(LOG_INFO, "str:%s", _nstr(nstr));

    free_nstr(&nstr);

    nstr = new_nstr(0);

    n_log(LOG_INFO, "EMPTY str:%s", _nstr(nstr));

    nstrprintf(nstr, "Hello, file is %s line %d date %s", __FILE__, __LINE__, __TIME__);

    n_log(LOG_INFO, "str:%s", _nstr(nstr));

    nstrprintf_cat(nstr, " - This will be added at file %s line %d date %s", __FILE__, __LINE__, __TIME__);

    n_log(LOG_INFO, "str:%s", _nstr(nstr));

    nstrprintf_cat(nstr, " - some more texte");

    N_STR* nstr2 = nstrdup(nstr);

    n_log(LOG_INFO, "str: %s\n str2: %s", _nstr(nstr), _nstr(nstr2));

    N_STR* nstr3 = NULL;

    nstrcat(nstr3, nstr);
    nstrcat(nstr3, nstr2);

    n_log(LOG_INFO, "str:%s", _nstr(nstr3));

    free_nstr(&nstr3);

    nstr3 = new_nstr(10);

    nstrcat(nstr3, nstr);
    nstrcat(nstr3, nstr2);

    n_log(LOG_INFO, "str:%s", _nstr(nstr3));

    free_nstr(&nstr);
    free_nstr(&nstr2);
    free_nstr(&nstr3);

    nstr = new_nstr(128);
    char data[1048576] = "";

    for (int it = 0; it < 1048575; it++) {
        data[it] = (char)(32 + rand() % 63);
    }
    data[1048574] = '\0';

    for (int it = 0; it < 100; it++) {
        write_and_fit(&nstr->data, &nstr->length, &nstr->written, data);
    }

    free_nstr(&nstr);

    /* test char_to_nstr and empty_nstr */
    nstr = char_to_nstr("char_to_nstr test");
    n_log(LOG_INFO, "char_to_nstr: %s", _nstr(nstr));
    empty_nstr(nstr);
    n_log(LOG_INFO, "after empty_nstr: '%s' written=%zu", _nstr(nstr), nstr->written);
    free_nstr(&nstr);

    /* test trim */
    char* trimmed = trim("   spaces around   ");
    if (trimmed) {
        n_log(LOG_INFO, "trim result: '%s'", trimmed);
        Free(trimmed);
    }
    /* test trim_nocopy */
    char trimbuf[] = "  hello  ";
    char* trimptr = trim_nocopy(trimbuf);
    n_log(LOG_INFO, "trim_nocopy result: '%s'", trimptr);

    /* test str_to_int, str_to_long, str_to_long_long */
    int ival = 0;
    if (str_to_int("12345", &ival, 10) == TRUE) {
        n_log(LOG_INFO, "str_to_int: %d", ival);
    }
    long int lval = 0;
    if (str_to_long("9876543210", &lval, 10) == TRUE) {
        n_log(LOG_INFO, "str_to_long: %ld", lval);
    }
    long long int llval = 0;
    if (str_to_long_long("123456789012345", &llval, 10) == TRUE) {
        n_log(LOG_INFO, "str_to_long_long: %lld", llval);
    }

    /* test strup, strlo */
    char upper[32] = "";
    char lower[32] = "";
    strup("hello world", upper);
    n_log(LOG_INFO, "strup: %s", upper);
    strlo("HELLO WORLD", lower);
    n_log(LOG_INFO, "strlo: %s", lower);

    /* test split, split_count, join, free_split_result */
    char** parts = split("one:two:three:four", ":", 0);
    if (parts) {
        int count = split_count(parts);
        n_log(LOG_INFO, "split count: %d", count);
        for (int it = 0; it < count; it++) {
            n_log(LOG_INFO, "  split[%d]: %s", it, parts[it]);
        }
        char* joined = join(parts, " - ");
        if (joined) {
            n_log(LOG_INFO, "join result: %s", joined);
            Free(joined);
        }
        free_split_result(&parts);
    }

    /* test wildmat, wildmatcase */
    n_log(LOG_INFO, "wildmat(\"hello.txt\", \"*.txt\"): %d", wildmat("hello.txt", "*.txt"));
    n_log(LOG_INFO, "wildmat(\"hello.txt\", \"*.csv\"): %d", wildmat("hello.txt", "*.csv"));
    n_log(LOG_INFO, "wildmatcase(\"Hello.TXT\", \"*.txt\"): %d", wildmatcase("Hello.TXT", "*.txt"));

    /* test str_replace */
    char* replaced = str_replace("hello world hello", "hello", "bye");
    if (replaced) {
        n_log(LOG_INFO, "str_replace: %s", replaced);
        Free(replaced);
    }

    /* test str_sanitize */
    char sanitize_buf[] = "hello; rm -rf /";
    str_sanitize(sanitize_buf, ";/", '_');
    n_log(LOG_INFO, "str_sanitize: %s", sanitize_buf);

    /* test file_to_nstr and nstr_to_file */
    N_STR* file_content = char_to_nstr("file content test\nsecond line\n");
    nstr_to_file(file_content, "nilorea_nstr_test.txt");
    free_nstr(&file_content);
    file_content = file_to_nstr("nilorea_nstr_test.txt");
    if (file_content) {
        n_log(LOG_INFO, "file_to_nstr: %s", _nstr(file_content));
        free_nstr(&file_content);
    }

    /* test skipw, skipu */
    char skipbuf[] = "   hello";
    NSTRBYTE it = 0;
    skipw(skipbuf, ' ', &it, 1);
    n_log(LOG_INFO, "skipw past spaces: pos=%zu char='%c'", it, skipbuf[it]);
    it = 0;
    skipu(skipbuf, 'h', &it, 1);
    n_log(LOG_INFO, "skipu to 'h': pos=%zu char='%c'", it, skipbuf[it]);

    /* test resize_nstr */
    nstr = new_nstr(16);
    n_log(LOG_INFO, "before resize: length=%zu", nstr->length);
    resize_nstr(nstr, 256);
    n_log(LOG_INFO, "after resize: length=%zu", nstr->length);
    free_nstr(&nstr);

    /* test scan_dir */
    LIST* dir_list = new_generic_list(0);
    if (scan_dir(".", dir_list, 0) == TRUE) {
        n_log(LOG_INFO, "scan_dir found %zu entries", dir_list->nb_items);
    }
    list_destroy(&dir_list);

    exit(0);
}
