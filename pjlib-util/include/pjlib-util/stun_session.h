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
#ifndef __PJLIB_UTIL_STUN_SESSION_H__
#define __PJLIB_UTIL_STUN_SESSION_H__

#include <pjlib-util/stun_msg.h>
#include <pjlib-util/stun_endpoint.h>
#include <pjlib-util/stun_transaction.h>
#include <pj/list.h>

PJ_BEGIN_DECL


/* **************************************************************************/
/**
 * @defgroup PJLIB_UTIL_STUN_SESSION STUN Client/Server Session
 * @brief STUN client and server session
 * @ingroup PJLIB_UTIL_STUN
 * @{
 */


/** Forward declaration for pj_stun_tx_data */
typedef struct pj_stun_tx_data pj_stun_tx_data;

/** Forward declaration for pj_stun_session */
typedef struct pj_stun_session pj_stun_session;


/**
 * This is the callback to be registered to pj_stun_session, to send
 * outgoing message and to receive various notifications from the STUN
 * session.
 */
typedef struct pj_stun_session_cb
{
    /**
     * Callback to be called by the STUN session to send outgoing message.
     *
     * @param tdata	    The STUN transmit data containing the original
     *			    STUN message
     * @param pkt	    Packet to be sent.
     * @param pkt_size	    Size of the packet to be sent.
     * @param addr_len	    Length of destination address.
     * @param dst_addr	    The destination address.
     *
     * @return		    The callback should return the status of the
     *			    packet sending.
     */
    pj_status_t (*on_send_msg)(pj_stun_session *sess,
			       const void *pkt,
			       pj_size_t pkt_size,
			       const pj_sockaddr_t *dst_addr,
			       unsigned addr_len);

    /** 
     * Callback to be called on incoming STUN request message. In the 
     * callback processing, application MUST create a response by calling
     * pj_stun_session_create_response() function and send the response
     * with pj_stun_session_send_msg() function, before returning from
     * the callback.
     *
     * @param sess	    The STUN session.
     * @param pkt	    Pointer to the original STUN packet.
     * @param pkt_len	    Length of the STUN packet.
     * @param msg	    The parsed STUN request.
     * @param src_addr	    Source address of the packet.
     * @param src_addr_len  Length of the source address.
     *
     * @return		    The return value of this callback will be
     *			    returned back to pj_stun_session_on_rx_pkt()
     *			    function.
     */
    pj_status_t (*on_rx_request)(pj_stun_session *sess,
				 const pj_uint8_t *pkt,
				 unsigned pkt_len,
				 const pj_stun_msg *msg,
				 const pj_sockaddr_t *src_addr,
				 unsigned src_addr_len);

    /**
     * Callback to be called when response is received or the transaction 
     * has timed out.
     *
     * @param sess	    The STUN session.
     * @param status	    Status of the request. If the value if not
     *			    PJ_SUCCESS, the transaction has timed-out
     *			    or other error has occurred, and the response
     *			    argument may be NULL.
     * @param request	    The original STUN request.
     * @param response	    The response message, on successful transaction.
     */
    void (*on_request_complete)(pj_stun_session *sess,
			        pj_status_t status,
			        pj_stun_tx_data *tdata,
			        const pj_stun_msg *response);


    /**
     * Type of callback to be called on incoming STUN indication.
     */
    pj_status_t (*on_rx_indication)(pj_stun_session *sess,
				    const pj_uint8_t *pkt,
				    unsigned pkt_len,
				    const pj_stun_msg *msg,
				    const pj_sockaddr_t *src_addr,
				    unsigned src_addr_len);

} pj_stun_session_cb;


/**
 * This structure describe the outgoing STUN transmit data to carry the
 * message to be sent.
 */
struct pj_stun_tx_data
{
    PJ_DECL_LIST_MEMBER(struct pj_stun_tx_data);

    pj_pool_t		*pool;		/**< Pool.			    */
    pj_stun_session	*sess;		/**< The STUN session.		    */
    pj_stun_msg		*msg;		/**< The STUN message.		    */
    void		*user_data;	/**< Arbitrary user data.	    */

    pj_stun_client_tsx	*client_tsx;	/**< Client STUN transaction.	    */
    pj_uint8_t		 client_key[12];/**< Client transaction key.	    */

    void		*pkt;		/**< The STUN packet.		    */
    unsigned		 max_len;	/**< Length of packet buffer.	    */
    unsigned		 pkt_size;	/**< The actual length of STUN pkt. */

    unsigned		 addr_len;	/**< Length of destination address. */
    const pj_sockaddr_t	*dst_addr;	/**< Destination address.	    */
};


/**
 * Options that can be specified when creating or sending outgoing STUN
 * messages. These options may be specified as bitmask.
 */
enum pj_stun_session_option
{
    /**
     * Add short term credential to the message. This option may not be used
     * together with PJ_STUN_USE_LONG_TERM_CRED option.
     */
    PJ_STUN_USE_SHORT_TERM_CRED	= 1,

    /**
     * Add long term credential to the message. This option may not be used
     * together with PJ_STUN_USE_SHORT_TERM_CRED option.
     */
    PJ_STUN_USE_LONG_TERM_CRED	= 2,

    /**
     * Add STUN fingerprint to the message.
     */
    PJ_STUN_USE_FINGERPRINT	= 4
};


/**
 * Create a STUN session.
 *
 * @param endpt	    The STUN endpoint, to be used to register timers etc.
 * @param name	    Optional name to be associated with this instance. The
 *		    name will be used for example for logging purpose.
 * @param cb	    Session callback.
 * @param p_sess    Pointer to receive STUN session instance.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_create(pj_stun_endpoint *endpt,
					    const char *name,
					    const pj_stun_session_cb *cb,
					    pj_stun_session **p_sess);

/**
 * Destroy the STUN session.
 *
 * @param sess	    The STUN session instance.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_destroy(pj_stun_session *sess);

/**
 * Associated an arbitrary data with this STUN session. The user data may
 * be retrieved later with pj_stun_session_get_user_data() function.
 *
 * @param sess	    The STUN session instance.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_set_user_data(pj_stun_session *sess,
						   void *user_data);

/**
 * Retrieve the user data previously associated to this STUN session with
 * pj_stun_session_set_user_data().
 *
 * @param sess	    The STUN session instance.
 *
 * @return	    The user data associated with this STUN session.
 */
PJ_DECL(void*) pj_stun_session_get_user_data(pj_stun_session *sess);

/**
 * Save a long term credential to be used by this STUN session when sending
 * outgoing messages. After long term credential is configured, application
 * may specify PJ_STUN_USE_LONG_TERM_CRED option when sending outgoing STUN
 * message to send the long term credential in the message.
 *
 * @param sess	    The STUN session instance.
 * @param realm	    Realm of the long term credential.
 * @param user	    The user name.
 * @param passwd    The pain-text password.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_session_set_long_term_credential(pj_stun_session *sess,
					 const pj_str_t *realm,
					 const pj_str_t *user,
					 const pj_str_t *passwd);


/**
 * Save a short term credential to be used by this STUN session when sending
 * outgoing messages. After short term credential is configured, application
 * may specify PJ_STUN_USE_SHORT_TERM_CRED option when sending outgoing STUN
 * message to send the short term credential in the message.
 *
 * @param sess	    The STUN session instance.
 * @param user	    The user name.
 * @param passwd    The pain-text password.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_session_set_short_term_credential(pj_stun_session *sess,
					  const pj_str_t *user,
					  const pj_str_t *passwd);

/**
 * Create a STUN Bind request message. After the message has been 
 * successfully created, application can send the message by calling 
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param p_tdata   Pointer to receive STUN transmit data instance containing
 *		    the request.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_create_bind_req(pj_stun_session *sess,
						     pj_stun_tx_data **p_tdata);

/**
 * Create a STUN Allocate request message. After the message has been
 * successfully created, application can send the message by calling  
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param p_tdata   Pointer to receive STUN transmit data instance containing
 *		    the request.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_create_allocate_req(pj_stun_session *sess,
							 pj_stun_tx_data **p_tdata);

/**
 * Create a STUN Set Active Destination request message. After the message 
 * has been successfully created, application can send the message by calling
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param p_tdata   Pointer to receive STUN transmit data instance containing
 *		    the request.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) 
pj_stun_session_create_set_active_destination_req(pj_stun_session *sess,
						  pj_stun_tx_data **p_tdata);

/**
 * Create a STUN Connect request message. After the message has been 
 * successfully created, application can send the message by calling 
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param p_tdata   Pointer to receive STUN transmit data instance containing
 *		    the request.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_create_connect_req(pj_stun_session *sess,
							pj_stun_tx_data **p_tdata);

/**
 * Create a STUN Connection Status Indication message. After the message 
 * has been successfully created, application can send the message by calling
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param p_tdata   Pointer to receive STUN transmit data instance containing
 *		    the message.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t)
pj_stun_session_create_connection_status_ind(pj_stun_session *sess,
					     pj_stun_tx_data **p_tdata);

/**
 * Create a STUN Send Indication message. After the message has been 
 * successfully created, application can send the message by calling 
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param p_tdata   Pointer to receive STUN transmit data instance containing
 *		    the message.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_create_send_ind(pj_stun_session *sess,
						     pj_stun_tx_data **p_tdata);

/**
 * Create a STUN Data Indication message. After the message has been 
 * successfully created, application can send the message by calling 
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param p_tdata   Pointer to receive STUN transmit data instance containing
 *		    the message.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_create_data_ind(pj_stun_session *sess,
						     pj_stun_tx_data **p_tdata);

/**
 * Create a STUN response message. After the message has been 
 * successfully created, application can send the message by calling 
 * pj_stun_session_send_msg().
 *
 * @param sess	    The STUN session instance.
 * @param req	    The STUN request where the response is to be created.
 * @param err_code  Error code to be set in the response, if error response
 *		    is to be created, according to pj_stun_status enumeration.
 *		    This argument MUST be zero if successful response is
 *		    to be created.
 * @param err_msg   Optional pointer for the error message string, when
 *		    creating error response. If the value is NULL and the
 *		    \a err_code is non-zero, then default error message will
 *		    be used.
 * @param p_tdata   Pointer to receive the response message created.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_create_response(pj_stun_session *sess,
						     const pj_stun_msg *req,
						     unsigned err_code,
						     const pj_str_t *err_msg,
						     pj_stun_tx_data **p_tdata);


/**
 * Send STUN message to the specified destination. This function will encode
 * the pj_stun_msg instance to a packet buffer, and add credential or
 * fingerprint if necessary. If the message is a request, the session will
 * also create and manage a STUN client transaction to be used to manage the
 * retransmission of the request. After the message has been encoded and
 * transaction is setup, the \a on_send_msg() callback of pj_stun_session_cb
 * (which is registered when the STUN session is created) will be called
 * to actually send the message to the wire.
 *
 * @param sess	    The STUN session instance.
 * @param options   Optional flags, from pj_stun_session_option.
 * @param dst_addr  The destination socket address.
 * @param addr_len  Length of destination address.
 * @param tdata	    The STUN transmit data containing the STUN message to
 *		    be sent.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_send_msg(pj_stun_session *sess,
					      unsigned options,
					      const pj_sockaddr_t *dst_addr,
					      unsigned addr_len,
					      pj_stun_tx_data *tdata);

/**
 * Application must call this function to notify the STUN session about
 * the arrival of STUN packet. The STUN packet MUST have been checked
 * first with #pj_stun_msg_check() to verify that this is indeed a valid
 * STUN packet.
 *
 * The STUN session will decode the packet into pj_stun_msg, and process
 * the message accordingly. If the message is a response, it will search
 * through the outstanding STUN client transactions for a matching
 * transaction ID and hand over the response to the transaction.
 *
 * On successful message processing, application will be notified about
 * the message via one of the pj_stun_session_cb callback.
 *
 * @param sess	     The STUN session instance.
 * @param packet     The packet containing STUN message.
 * @param pkt_size   Size of the packet.
 * @param options    Options, from pj_stun_options.
 * @param parsed_len Optional pointer to receive the size of the parsed
 *		     STUN message (useful if packet is received via a
 *		     stream oriented protocol).
 *
 * @return	     PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(pj_status_t) pj_stun_session_on_rx_pkt(pj_stun_session *sess,
					       const void *packet,
					       pj_size_t pkt_size,
					       unsigned options,
					       unsigned *parsed_len,
					       const pj_sockaddr_t *src_addr,
					       unsigned src_addr_len);

/**
 * Destroy the transmit data. Call this function only when tdata has been
 * created but application doesn't want to send the message (perhaps
 * because of other error).
 *
 * @param sess	    The STUN session.
 * @param tdata	    The transmit data.
 *
 * @return	    PJ_SUCCESS on success, or the appropriate error code.
 */
PJ_DECL(void) pj_stun_msg_destroy_tdata(pj_stun_session *sess,
					pj_stun_tx_data *tdata);


/**
 * @}
 */


PJ_END_DECL

#endif	/* __PJLIB_UTIL_STUN_SESSION_H__ */

