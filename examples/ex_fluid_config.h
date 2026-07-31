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
 *@example ex_fluid_config.h
 *@brief Config file for ex_fluid
 *@author Castagnier Mickael aka Gull Ra Driel
 *@version 1.0
 *@date 29/12/2021
 */

#ifndef _N_FLUID_CONFIG
#define _N_FLUID_CONFIG

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_fluids.h"

/*! @brief Load application state from a config file
 *@param state_filename path to the config file
 *@param WIDTH pointer to store display width
 *@param HEIGHT pointer to store display height
 *@param fullscreen pointer to store fullscreen flag
 *@param bgmusic pointer to store background music path
 *@param drawFPS pointer to store drawing framerate
 *@param logicFPS pointer to store logic framerate
 *@param fluid pointer to a N_FLUID to populate
 *@param threaded pointer to store threaded processing flag
 *@return TRUE on success, FALSE on error
 */
int load_app_state(char* state_filename, int* WIDTH, int* HEIGHT, bool* fullscreen, char** bgmusic, double* drawFPS, double* logicFPS, N_FLUID* fluid, int* threaded);

#ifdef __cplusplus
}
#endif

#endif
