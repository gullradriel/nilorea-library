/**\example ex_monolith.c Nilorea Library monolith api test
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/05/2015
 */

#include "nilorea.h"

int main(void) {
    set_log_level(LOG_DEBUG);

    N_STR* nstr = NULL;

    printf("str:%s\n", _nstr(nstr));

    nstrprintf(nstr, "Hello, file is %s line %d date %s", __FILE__, __LINE__, __TIME__);

    printf("str:%s\n", _nstr(nstr));

    nstrprintf_cat(nstr, " - This will be added at file %s line %d date %s", __FILE__, __LINE__, __TIME__);

    printf("str:%s\n", _nstr(nstr));

    free_nstr(&nstr);

    nstr = new_nstr(1024);

    printf("str:%s\n", _nstr(nstr));

    nstrprintf(nstr, "Hello, file is %s line %d date %s", __FILE__, __LINE__, __TIME__);

    printf("str:%s\n", _nstr(nstr));

    nstrprintf_cat(nstr, " - This will be added at file %s line %d date %s", __FILE__, __LINE__, __TIME__);

    printf("str:%s\n", _nstr(nstr));

    nstrprintf_cat(nstr, " - some more texte");

    N_STR* nstr2 = nstrdup(nstr);

    printf("str: %s\n str2: %s\n", _nstr(nstr), _nstr(nstr2));

    N_STR* nstr3 = NULL;

    nstrcat(nstr3, nstr);
    nstrcat(nstr3, nstr2);

    printf("str:%s\n", _nstr(nstr3));

    nstr3 = new_nstr(10);

    nstrcat(nstr3, nstr);
    nstrcat(nstr3, nstr2);

    printf("str:%s\n", _nstr(nstr3));

    free_nstr(&nstr);
    free_nstr(&nstr2);
    free_nstr(&nstr3);

    exit(0);
}
