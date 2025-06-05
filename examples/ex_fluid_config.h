/**\example ex_fluid_config.h
 *
 *  config file for ex_fluid
 *
 *\author Castagnier Mickael aka Gull Ra Driel
 *
 *\version 1.0
 *
 *\date 29/12/2021
 *
 */

#ifndef _N_FLUID_CONFIG
#define _N_FLUID_CONFIG

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_fluids.h"

int load_app_state(char* state_filename, size_t* WIDTH, size_t* HEIGHT, bool* fullscreen, char** bgmusic, double* drawFPS, double* logicFPS, N_FLUID* fluid, int* threaded);

#ifdef __cplusplus
}
#endif

#endif
