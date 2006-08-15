/* $Id$ */
/* 
 * Copyright (C)2003-2006 Benny Prijono <benny@prijono.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pj/sock_select.h>


PJ_DEF(void) PJ_FD_ZERO(pj_fd_set_t *fdsetp)
{
    PJ_TODO(PJ_FD_ZERO);
}


PJ_DEF(void) PJ_FD_SET(pj_sock_t fd, pj_fd_set_t *fdsetp)
{
    PJ_TODO(PJ_FD_SET);
}


PJ_DEF(void) PJ_FD_CLR(pj_sock_t fd, pj_fd_set_t *fdsetp)
{
    PJ_TODO(PJ_FD_CLR);
}


PJ_DEF(pj_bool_t) PJ_FD_ISSET(pj_sock_t fd, const pj_fd_set_t *fdsetp)
{
    PJ_TODO(PJ_FD_ISSET);
    return 0;
}

PJ_DEF(pj_size_t) PJ_FD_COUNT(const pj_fd_set_t *fdsetp)
{
    PJ_TODO(PJ_FD_COUNT);
    return 0;
}

PJ_DEF(int) pj_sock_select( int n, 
			    pj_fd_set_t *readfds, 
			    pj_fd_set_t *writefds,
			    pj_fd_set_t *exceptfds, 
			    const pj_time_val *timeout)
{
    PJ_TODO(pj_sock_select);
    return 0;
}

