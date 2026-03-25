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
 *@file n_network_msg.h
 *@brief Network messages , serialization tools
 *@author Castagnier Mickael
 *@version 1.0
 *@date 10/05/2005
 */
#ifndef N_NETWORK_MESSAGES
#define N_NETWORK_MESSAGES

#ifdef __cplusplus
extern "C" {
#endif

/**@defgroup NETWORK_MSG NETWORK MESSAGES: tools to serialize integers, double, N_STR from and back to a single N_STR *message
   @addtogroup NETWORK_MSG
  @{
*/

#include <sys/param.h>

#include "n_str.h"
#include "n_list.h"
#include "n_3d.h"

/*! Network Message is identification request: (int)type , (int)id , (N_STR *)name , (N_STR *)password */
#define NETMSG_IDENT_REQUEST 0
/*! Network Message is identification reply OK : (int)type , (int)id , (N_STR *)name , (N_STR *)password */
#define NETMSG_IDENT_REPLY_OK 1
/*! Network Message is identification reply NON OK: (int)type , (int)id , (N_STR *)name , (N_STR *)password */
#define NETMSG_IDENT_REPLY_NOK 2
/*! Network Message is string: (int)type , (int)id , (N_STR *)name , (N_STR *)chan , (N_STR *)text , (int)color */
#define NETMSG_STRING 3
/*! Network Message position: (int)type , (int)id , (int)X , (int)Y , (int)x_shift , (int)y_shift ,(int)vx ,(int)vy , (int)speed , (int)acceleration , (int)time_stamp */
#define NETMSG_POSITION 4
/*! Network Message is ping request: (int)type , (int)id_from , (int)id_to */
#define NETMSG_PING_REQUEST 5
/*! Network Message is ping reply: (int)type , (int)id_from , (int)id_to */
#define NETMSG_PING_REPLY 6
/*! Network Message is box retrieve: int x , int y , int z */
#define NETMSG_GET_BOX 7
/*! Network Message is box retrieving reply: (int x , int y , int z , N_STR *data ) */
#define NETMSG_BOX 8
/*! Network asking for exit */
#define NETMSG_QUIT 9

/*! Maximum allowed length for a single string inside a network message (default: 1 GB).
 *  Override before including this header to change the limit. */
#ifndef NETW_MSG_MAX_STRING_LENGTH
#define NETW_MSG_MAX_STRING_LENGTH (1024UL * 1024UL * 1024UL)
#endif

/*!network message, array of char and int*/
typedef struct NETW_MSG {
    /*! array of int */
    LIST* tabint;
    /*! array of N_STR */
    LIST* tabstr;
    /*! array of casted double value */
    LIST* tabflt;
} NETW_MSG;

/*! @brief swap bytes of a double value */
double double_swap(double value);
/*! @brief convert host double to network byte order */
double htond(double value);
/*! @brief convert network double to host byte order */
double ntohd(double value);

/*! @brief create a NETW_MSG object */
int create_msg(NETW_MSG** msg);
/*! @brief delete a NETW_MSG object */
int delete_msg(NETW_MSG** msg);
/*! @brief empty a NETW_MSG object */
int empty_msg(NETW_MSG** msg);
/*! @brief add a float/double to the message */
int add_nb_to_msg(NETW_MSG* msg, double value);
/*! @brief add an int to the message */
int add_int_to_msg(NETW_MSG* msg, int value);
/*! @brief add a N_STR pointer to the message without copying */
int add_nstrptr_to_msg(NETW_MSG* msg, N_STR* str);
/*! @brief add a duplicate of a N_STR to the message */
int add_nstrdup_to_msg(NETW_MSG* msg, N_STR* str);
/*! @brief add a duplicate of a char string to the message */
int add_strdup_to_msg(NETW_MSG* msg, const char* str);
/*! @brief get an int from the message */
int get_int_from_msg(NETW_MSG* msg, int* value);
/*! @brief get a float/double value from the message */
int get_nb_from_msg(NETW_MSG* msg, double* value);
/*! @brief get a N_STR string from the message */
int get_nstr_from_msg(NETW_MSG* msg, N_STR** str);
/*! @brief get a char string from the message */
int get_str_from_msg(NETW_MSG* msg, char** str);
/*! @brief serialize a NETW_MSG into a single N_STR */
N_STR* make_str_from_msg(NETW_MSG* msg);
/*! @brief deserialize a N_STR into a NETW_MSG */
NETW_MSG* make_msg_from_str(N_STR* str);

/*! @brief create a ping network message */
N_STR* netmsg_make_ping(int type, int id_from, int id_to, int time);
/*! @brief create an identification network message */
N_STR* netmsg_make_ident(int type, int id, N_STR* name, N_STR* passwd);
/*! @brief create a position network message */
N_STR* netmsg_make_position_msg(int id, double X, double Y, double vx, double vy, double acc_x, double acc_y, int time_stamp);
/*! @brief create a string network message */
N_STR* netmsg_make_string_msg(int id_from, int id_to, N_STR* name, N_STR* chan, N_STR* txt, int color);
/*! @brief create a quit network message */
N_STR* netmsg_make_quit_msg(void);

/*! @brief get the type of a network message. Returns the type value (>=0) on success, -1 on error */
int netw_msg_get_type(N_STR* msg);

/*! @brief get identification data from a network message */
int netw_get_ident(N_STR* msg, int* type, int* ident, N_STR** name, N_STR** passwd);
/*! @brief get position data from a network message */
int netw_get_position(N_STR* msg, int* id, double* X, double* Y, double* vx, double* vy, double* acc_x, double* acc_y, int* time_stamp);
/*! @brief get string data from a network message */
int netw_get_string(N_STR* msg, int* id, N_STR** name, N_STR** chan, N_STR** txt, int* color);
/*! @brief get ping data from a network message */
int netw_get_ping(N_STR* msg, int* type, int* from, int* to, int* time);
/*! @brief get quit data from a network message */
int netw_get_quit(N_STR* msg);

/**
@}
*/

#ifdef __cplusplus
}
#endif

#endif /*#ifndef N_NETWORK*/
