// PCRE2 8-bit build probe used by the Makefile dependency detection.
// If this compiles, we consider PCRE2 available.
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

int main(void){ return 0; }
