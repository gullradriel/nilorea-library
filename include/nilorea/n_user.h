/**\file n_user.h
*
*  USERS handling for everything you want
*
*\author Castagnier Mickael
*
*\version 1.0
*
*\date 20/02/2006
*
*/


#ifndef N_USER_MANAGEMENT

#define N_USER_MANAGEMENT

#ifdef __cplusplus
extern "C"
{
#endif



/*! USER management cell */
typedef struct N_USER_BOX
{

    /*! Associated NETWORK */
    NETWORK *netw;

    /*! User Name */
    char name[ 1024 ];

    /*! State of the current user */
    int state;

    /*! X Position in array */
    double anchor_x,
           /*! Y position in array */
           anchor_y,
           /*! X shifting inside the cell of the array */
           shift_x,
           /*! Y shifting inside the cell of the array */
           shift_y,
           /*! Unique ident */
           id;

} N_USER_BOX;



/*! USER list */
typedef struct N_USER
{

    /*! Pointer to the allocated list of user */
    N_USER_BOX *list;

    /*! Maximum of user inside the list */
    int max,
        /*! Position of the highter user inside the list */
        highter;

    /*! Mutex for thread safe user management */
    pthread_mutex_t user_bolt;

} N_USER;



/*
 * Create a list of user
 */

int create_list_of_user( N_USER *list, int max );



/*
 * Add an user to the list
 */

int add_user( N_USER *list );



/*
 * Delete an user from the list
 */

int rem_user( N_USER *list,int id );



/*
 * Add a message to all user inside the list
 */

int add_msg_to_all( N_USER *list, N_STR *msg );



/*
 * Add a message to a specific user
 */

int add_msg_to_user( N_USER *list, int id, char *msg, int size );



#ifdef __cplusplus
}
#endif

#endif /* #ifndef N_USER_MANAGEMENT */
