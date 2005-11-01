/* $Header: /pjproject-0.3/pjlib/include/pj/sock_select.h 3     10/14/05 12:26a Bennylp $ */
/* $Log: /pjproject-0.3/pjlib/include/pj/sock_select.h $
 * 
 * 3     10/14/05 12:26a Bennylp
 * Finished error code framework, some fixes in ioqueue, etc. Pretty
 * major.
 * 
 * 2     9/21/05 1:39p Bennylp
 * Periodic checkin for backup.
 * 
 * 1     9/15/05 8:40p Bennylp
 * Created.
 */
#ifndef __PJ_SELECT_H__
#define __PJ_SELECT_H__

/**
 * @file sock_select.h
 * @brief Socket select().
 */

#include <pj/types.h>

PJ_BEGIN_DECL 

/**
 * @defgroup PJ_SOCK_SELECT Socket select() API.
 * @ingroup PJ_IO
 * @{
 * This module provides portable abstraction for \a select() like API.
 * The abstraction is needed so that it can utilize various event
 * dispatching mechanisms that are available across platforms.
 *
 * The API is very similar to normal \a select() usage. 
 *
 * \section pj_sock_select_examples_sec Examples
 *
 * For some examples on how to use the select API, please see:
 *
 *  - \ref page_pjlib_select_test
 */

/**
 * Portable structure declarations for pj_fd_set.
 * The implementation of pj_sock_select() does not use this structure 
 * per-se, but instead it will use the native fd_set structure. However,
 * we must make sure that the size of pj_fd_set_t can accomodate the
 * native fd_set structure.
 */
typedef struct pj_fd_set_t
{
    pj_sock_t	data[FD_SETSIZE + 4];   /**< Opaque buffer for fd_set */
} pj_fd_set_t;


/**
 * Initialize the descriptor set pointed to by fdsetp to the null set.
 *
 * @param fdsetp    The descriptor set.
 */
PJ_DECL(void) PJ_FD_ZERO(pj_fd_set_t *fdsetp);


/**
 * Add the file descriptor fd to the set pointed to by fdsetp. 
 * If the file descriptor fd is already in this set, there shall be no effect
 * on the set, nor will an error be returned.
 *
 * @param fd	    The socket descriptor.
 * @param fdsetp    The descriptor set.
 */
PJ_DECL(void) PJ_FD_SET(pj_sock_t fd, pj_fd_set_t *fdsetp);

/**
 * Remove the file descriptor fd from the set pointed to by fdsetp. 
 * If fd is not a member of this set, there shall be no effect on the set, 
 * nor will an error be returned.
 *
 * @param fd	    The socket descriptor.
 * @param fdsetp    The descriptor set.
 */
PJ_DECL(void) PJ_FD_CLR(pj_sock_t fd, pj_fd_set_t *fdsetp);


/**
 * Evaluate to non-zero if the file descriptor fd is a member of the set 
 * pointed to by fdsetp, and shall evaluate to zero otherwise.
 *
 * @param fd	    The socket descriptor.
 * @param fdsetp    The descriptor set.
 *
 * @return	    Nonzero if fd is member of the descriptor set.
 */
PJ_DECL(pj_bool_t) PJ_FD_ISSET(pj_sock_t fd, const pj_fd_set_t *fdsetp);


/**
 * Get the number of descriptors in the set.
 *
 * @param fdsetp    The descriptor set.
 *
 * @return          Number of descriptors in the set.
 */
PJ_DECL(pj_size_t) PJ_FD_COUNT(const pj_fd_set_t *fdsetp);


/**
 * This function wait for a number of file  descriptors to change status.
 * The behaviour is the same as select() function call which appear in
 * standard BSD socket libraries.
 *
 * @param n	    On Unices, this specifies the highest-numbered
 *		    descriptor in any of the three set, plus 1. On Windows,
 *		    the value is ignored.
 * @param readfds   Optional pointer to a set of sockets to be checked for 
 *		    readability.
 * @param writefds  Optional pointer to a set of sockets to be checked for 
 *		    writability.
 * @param exceptfds Optional pointer to a set of sockets to be checked for 
 *		    errors.
 * @param timeout   Maximum time for select to wait, or null for blocking 
 *		    operations.
 *
 * @return	    The total number of socket handles that are ready, or
 *		    zero if the time limit expired, or -1 if an error occurred.
 */
PJ_DECL(int) pj_sock_select( int n, 
			     pj_fd_set_t *readfds, 
			     pj_fd_set_t *writefds,
			     pj_fd_set_t *exceptfds, 
			     const pj_time_val *timeout);


/**
 * @}
 */


PJ_END_DECL

#endif	/* __PJ_SELECT_H__ */
