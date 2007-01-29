/* $Id */
/* 
 * Copyright (C) 2003-2005 Benny Prijono <benny@prijono.org>
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
#ifndef __PJ_STUN_TRANSACTION_H__
#define __PJ_STUN_TRANSACTION_H__

/**
 * @file stun_transaction.h
 * @brief STUN transaction
 */

#include <pjlib-util/stun_msg.h>
#include <pjlib-util/stun_endpoint.h>


PJ_BEGIN_DECL


/* **************************************************************************/
/**
 * @defgroup PJLIB_UTIL_STUN_TRANSACTION STUN Client Transaction
 * @brief STUN client transaction
 * @ingroup PJLIB_UTIL_STUN
 * @{
 */

/**
 * Opaque declaration of STUN client transaction.
 */
typedef struct pj_stun_client_tsx pj_stun_client_tsx;

/**
 * STUN client transaction callback.
 */
typedef struct pj_stun_tsx_cb
{
    void	(*on_complete)(pj_stun_client_tsx *tsx,
			       pj_status_t status, 
			       pj_stun_msg *response);
    pj_status_t (*on_send_msg)(pj_stun_client_tsx *tsx,
			       const void *stun_pkt,
			       pj_size_t pkt_size);

} pj_stun_tsx_cb;



/**
 * Create a STUN client transaction.
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_create(	pj_stun_endpoint *endpt,
						const pj_stun_tsx_cb *cb,
						pj_stun_client_tsx **p_tsx);

/**
 * .
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_destroy(pj_stun_client_tsx *tsx);


/**
 * .
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_set_data(pj_stun_client_tsx *tsx,
						 void *data);


/**
 * .
 */
PJ_DECL(void*) pj_stun_client_tsx_get_data(pj_stun_client_tsx *tsx);


/**
 * .
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_send_msg(pj_stun_client_tsx *tsx,
						 const pj_stun_msg *msg);


/**
 * .
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_on_rx_msg(pj_stun_client_tsx *tsx,
						  const void *packet,
						  pj_size_t pkt_size,
						  unsigned *parsed_len);



/**
 * @}
 */


PJ_END_DECL


#endif	/* __PJ_STUN_TRANSACTION_H__ */

