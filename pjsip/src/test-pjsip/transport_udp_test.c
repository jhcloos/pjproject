/* $Id$ */
/* 
 * Copyright (C) 2003-2006 Benny Prijono <benny@prijono.org>
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

#include "test.h"
#include <pjsip_core.h>
#include <pjlib.h>


/*
 * UDP transport test.
 */
int transport_udp_test(void)
{
    enum { SEND_RECV_LOOP = 2 };
    pjsip_transport *udp_tp, *tp;
    pj_sockaddr_in addr, rem_addr;
    pj_str_t s;
    pj_status_t status;
    int i;

    pj_sockaddr_in_init(&addr, NULL, TEST_UDP_PORT);

    /* Start UDP transport. */
    status = pjsip_udp_transport_start( endpt, &addr, NULL, 1, &udp_tp);
    if (status != PJ_SUCCESS) {
	app_perror("   Error: unable to start UDP transport", status);
	return -10;
    }

    /* UDP transport must have initial reference counter set to 1. */
    if (pj_atomic_get(udp_tp->ref_cnt) != 1)
	return -20;

    /* Test basic transport attributes */
    status = generic_transport_test(udp_tp);
    if (status != PJ_SUCCESS)
	return status;

    /* Test that transport manager is returning the correct
     * transport.
     */
    pj_sockaddr_in_init(&rem_addr, pj_cstr(&s, "1.1.1.1"), 80);
    status = pjsip_endpt_acquire_transport(endpt, PJSIP_TRANSPORT_UDP, 
					   &rem_addr, sizeof(rem_addr),
					   &tp);
    if (status != PJ_SUCCESS)
	return -50;
    if (tp != udp_tp)
	return -60;

    /* pjsip_endpt_acquire_transport() adds reference, so we need
     * to decrement it.
     */
    pjsip_transport_dec_ref(tp);

    /* Check again that reference counter is 1. */
    if (pj_atomic_get(udp_tp->ref_cnt) != 1)
	return -70;

    /* Basic transport's send/receive loopback test. */
    pj_sockaddr_in_init(&rem_addr, pj_cstr(&s, "127.0.0.1"), TEST_UDP_PORT);
    for (i=0; i<SEND_RECV_LOOP; ++i) {
	status = transport_send_recv_test(PJSIP_TRANSPORT_UDP, tp, &rem_addr);
	if (status != 0)
	    return status;
    }

    /* Multi-threaded round-trip test. */
    status = transport_rt_test(PJSIP_TRANSPORT_UDP, tp, &rem_addr);
    if (status != 0)
	return status;

    /* Check again that reference counter is 1. */
    if (pj_atomic_get(udp_tp->ref_cnt) != 1)
	return -80;

    /* Destroy this transport. */
    pjsip_transport_dec_ref(udp_tp);

    /* Done */
    return 0;
}
