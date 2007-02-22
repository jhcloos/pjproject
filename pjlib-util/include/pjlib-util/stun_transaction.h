/* $Id$ */
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
 *
 The @ref PJLIB_UTIL_STUN_TRANSACTION is used to manage outgoing STUN request,
 for example to retransmit the request and to notify application about the
 completion of the request.

 The @ref PJLIB_UTIL_STUN_TRANSACTION does not use any networking operations,
 but instead application must supply the transaction with a callback to
 be used by the transaction to send outgoing requests. This way the STUN
 transaction is made more generic and can work with different types of
 networking codes in application.


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
    /**
     * This callback is called when the STUN transaction completed.
     *
     * @param tsx	    The STUN transaction.
     * @param status	    Status of the transaction. Status PJ_SUCCESS
     *			    means that the request has received a successful
     *			    response.
     * @param response	    The STUN response, which value may be NULL if
     *			    \a status is not PJ_SUCCESS.
     */
    void	(*on_complete)(pj_stun_client_tsx *tsx,
			       pj_status_t status, 
			       const pj_stun_msg *response);

    /**
     * This callback is called by the STUN transaction when it wants to send
     * outgoing message.
     *
     * @param tsx	    The STUN transaction instance.
     * @param stun_pkt	    The STUN packet to be sent.
     * @param pkt_size	    Size of the STUN packet.
     *
     * @return		    If return value of the callback is not PJ_SUCCESS,
     *			    the transaction will fail.
     */
    pj_status_t (*on_send_msg)(pj_stun_client_tsx *tsx,
			       const void *stun_pkt,
			       pj_size_t pkt_size);

} pj_stun_tsx_cb;



/**
 * Create an instance of STUN client transaction. The STUN client 
 * transaction is used to transmit outgoing STUN request and to 
 * ensure the reliability of the request by periodically retransmitting
 * the request, if necessary.
 *
 * @param endpt		The STUN endpoint, which will be used to retrieve
 *			various settings for the transaction.
 * @param cb		Callback structure, to be used by the transaction
 *			to send message and to notify the application about
 *			the completion of the transaction.
 * @param p_tsx		Pointer to receive the transaction instance.
 *
 * @return		PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_create(	pj_stun_endpoint *endpt,
						const pj_stun_tsx_cb *cb,
						pj_stun_client_tsx **p_tsx);

/**
 * Destroy a STUN client transaction.
 *
 * @param tsx		The STUN transaction.
 *
 * @return		PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_destroy(pj_stun_client_tsx *tsx);


/**
 * Check if transaction has completed.
 *
 * @param tsx		The STUN transaction.
 *
 * @return		Non-zero if transaction has completed.
 */
PJ_DECL(pj_bool_t) pj_stun_client_tsx_is_complete(pj_stun_client_tsx *tsx);


/**
 * Associate an arbitrary data with the STUN transaction. This data
 * can be then retrieved later from the transaction, by using
 * pj_stun_client_tsx_get_data() function.
 *
 * @param tsx		The STUN client transaction.
 * @param data		Application data to be associated with the
 *			STUN transaction.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_set_data(pj_stun_client_tsx *tsx,
						 void *data);


/**
 * Get the user data that was previously associated with the STUN 
 * transaction.
 *
 * @param tsx		The STUN client transaction.
 *
 * @return		The user data.
 */
PJ_DECL(void*) pj_stun_client_tsx_get_data(pj_stun_client_tsx *tsx);


/**
 * Start the STUN client transaction by sending STUN request using
 * this transaction. If reliable transport such as TCP or TLS is used,
 * the retransmit flag should be set to PJ_FALSE because reliablity
 * will be assured by the transport layer.
 *
 * @param tsx		The STUN client transaction.
 * @param retransmit	Should this message be retransmitted by the
 *			STUN transaction.
 * @param msg		The STUN message.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_send_msg(pj_stun_client_tsx *tsx,
						 pj_bool_t retransmit,
						 const pj_stun_msg *msg);


/**
 * Notify the STUN transaction about the arrival of STUN response.
 * If the STUN response contains a final error (300 and greater), the
 * transaction will be terminated and callback will be called. If the
 * STUN response contains response code 100-299, retransmission
 * will  cease, but application must still call this function again
 * with a final response later to allow the transaction to complete.
 *
 * @param tsx		The STUN client transaction instance.
 * @param packet	The incoming packet.
 * @param pkt_size	Size of the incoming packet.
 * @param parsed_len	Optional pointer to receive the number of bytes
 *			that have been parsed from the incoming packet
 *			for the STUN message. This is useful if the
 *			STUN transaction is running over stream oriented
 *			socket such as TCP or TLS.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_on_rx_pkt(pj_stun_client_tsx *tsx,
						  const void *packet,
						  pj_size_t pkt_size,
						  unsigned *parsed_len);


/**
 * Notify the STUN transaction about the arrival of STUN response.
 * If the STUN response contains a final error (300 and greater), the
 * transaction will be terminated and callback will be called. If the
 * STUN response contains response code 100-299, retransmission
 * will  cease, but application must still call this function again
 * with a final response later to allow the transaction to complete.
 *
 * @param tsx		The STUN client transaction instance.
 * @param packet	The incoming packet.
 * @param pkt_size	Size of the incoming packet.
 * @param parsed_len	Optional pointer to receive the number of bytes
 *			that have been parsed from the incoming packet
 *			for the STUN message. This is useful if the
 *			STUN transaction is running over stream oriented
 *			socket such as TCP or TLS.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_client_tsx_on_rx_msg(pj_stun_client_tsx *tsx,
						  const pj_stun_msg *msg);


/**
 * @}
 */


PJ_END_DECL


#endif	/* __PJ_STUN_TRANSACTION_H__ */

