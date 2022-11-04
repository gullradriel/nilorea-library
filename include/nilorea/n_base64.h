#ifndef BASE46_H
#define BASE46_H

#include <stdlib.h>
#include <memory.h>
#include "nilorea/n_str.h"

N_STR *n_base64_encode( N_STR *string );
N_STR *n_base64_decode( N_STR *bufcoded );

#endif //BASE46_H
