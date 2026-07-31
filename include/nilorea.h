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
 *@file nilorea.h
 *@brief Header for a monolith use
 *@author Castagnier Mickael
 *@version 1.0
 *@date 24/03/05
 */

#ifndef __NILOREA_Unified_Library__

#define __NILOREA_Unified_Library__

#ifdef __cplusplus
extern "C" {
#endif

#include <nilorea/n_3d.h>
#include <nilorea/n_trajectory.h>
#include <nilorea/n_common.h>
#include <nilorea/n_log.h>
#include <nilorea/n_list.h>
#include <nilorea/n_config_file.h>
#include <nilorea/n_exceptions.h>
#include <nilorea/n_hash.h>
#include <nilorea/n_network.h>
#include <nilorea/n_network_msg.h>
#include <nilorea/n_reactor.h>
#include <nilorea/n_nodup_log.h>
#include <nilorea/n_pcre.h>
#include <nilorea/n_pretty.h>
#include <nilorea/n_signals.h>
#include <nilorea/n_stack.h>
#include <nilorea/n_str.h>
#include <nilorea/n_thread_pool.h>
#include <nilorea/n_time.h>
#include <nilorea/n_user.h>
#include <nilorea/n_zlib.h>
#include <nilorea/n_games.h>
#include <nilorea/n_dead_reckoning.h>
#include <nilorea/n_astar.h>

#ifdef HAVE_KAFKA
#include <nilorea/n_kafka.h>
#endif

#ifdef HAVE_LIBGIT2
#include <nilorea/n_git.h>
#endif

#ifdef HAVE_ALLEGRO
#include <nilorea/n_anim.h>
#include <nilorea/n_gui.h>
#include <nilorea/n_particles.h>
#include <nilorea/n_iso_engine.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /*#ifndef __NILOREA_Unified_Library__*/
