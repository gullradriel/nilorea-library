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

/**@file n_user.h
 * USERS handling for tiny game apps
 *@author Castagnier Mickael
 *@version 1.0
 *@date 20/02/2006
 */

#ifndef N_USER_MANAGEMENT

#define N_USER_MANAGEMENT

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_network.h"
#include "nilorea/n_3d.h"

/**@defgroup N_USER NETWORK USERS: server side network oriented user handling
  @addtogroup N_USER
  @{
  */

/*! flag to target all users in the list */
#define USERLIST_ALL 0
/*! flag to target all users in the list except one */
#define USERLIST_ALL_EXCEPT 1
/*! flag to target one user in the list */
#define USERLIST_ONE 2

/*! USER management cell */
typedef struct N_USER {
    /*! User Name */
    char name[1024];

    /*! Associated NETWORK */
    NETWORK* netw;

    /*! State of the current user */
    int state,
        /*! Number of saved positions , default: 10 */
        nb_rec_pos,
        /*! 1 => keep only_last_position in waitlist , 0 => send all the positions, default: 1 */
        only_last_pos,
        /*! Unique world ident */
        id;

    /*! Last nb_rec_pos position messages, for a better dead reckoning / lag simulation */
    VECTOR3D* last_positions;
    /*! actual position */
    VECTOR3D position;

    /*! N_STR *messages waiting to be sent */
    LIST* netw_waitlist;

} N_USER;

/*! USER list */
typedef struct N_USERLIST {
    /*! Pointer to the allocated list of user */
    N_USER* list;

    /*! Maximum of user inside the list */
    int max,
        /*! Position of the highter user inside the list */
        highter;

    /*! Mutex for thread safe user management */
    pthread_rwlock_t user_rwbolt;

} N_USERLIST;

/*! @brief allocate a new N_USERLIST with the given maximum number of users */
N_USERLIST* userlist_new(int max);
/*! @brief set position recording behavior for a user */
int userlist_set_position_behavior(N_USERLIST* ulist, int id, int nb_rec_pos, int only_last_pos);
/*! @brief add a user associated with a NETWORK connection to the list */
int userlist_add_user(N_USERLIST* ulist, NETWORK* netw);
/*! @brief remove a user from the list by id */
int userlist_del_user(N_USERLIST* ulist, int id);
/*! @brief add a message to users selected by mode and id */
int userlist_add_msg_to_ex(N_USERLIST* ulist, N_STR* msg, int mode, int id);
/*! @brief add a message to a specific user by id */
int userlist_add_msg_to(N_USERLIST* ulist, N_STR* msg, int id);
/*! @brief add a message to all users in the list */
int userlist_add_msg_to_all(N_USERLIST* ulist, N_STR* msg);
/*! @brief add a message to all users except the one with the given id */
int userlist_add_msg_to_all_except(N_USERLIST* ulist, N_STR* msg, int id);
/*! @brief destroy a N_USERLIST and free all resources */
int userlist_destroy(N_USERLIST** ulist);
/*! @brief add a waiting message to a specific user's queue */
int userlist_user_add_waiting_msg(N_USERLIST* ulist, int id, N_STR* netmsg);
/*! @brief send all waiting messages for a specific user */
int userlist_user_send_waiting_msgs(N_USERLIST* ulist, int id);
/*! @brief send all waiting messages for all users in the list */
int userlist_send_waiting_msgs(N_USERLIST* ulist);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif  // header guard
