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
#include <nilorea/n_nodup_log.h>
#include <nilorea/n_pcre.h>
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
