/* $Id$ */
/* 
 * Copyright (C) 2003-2007 Benny Prijono <benny@prijono.org>
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
#ifndef __PJNATH_STUN_SOCK_H__
#define __PJNATH_STUN_SOCK_H__

/**
 * @file stun_sock.h
 * @brief STUN aware socket transport
 */
#include <pjnath/stun_config.h>
#include <pjlib-util/resolver.h>
#include <pj/ioqueue.h>
#include <pj/sock.h>


PJ_BEGIN_DECL


/**
 * @defgroup PJNATH_STUN_SOCK STUN aware socket transport
 * @brief STUN aware socket transport
 * @ingroup PJNATH_STUN
 * @{
 * The STUN transport provides asynchronous UDP like socket transport
 * with the additional capability to query the publicly mapped transport
 * address (using STUN resolution), to refresh the NAT binding, and to
 * demultiplex internal STUN messages from application data (the 
 * application data may be a STUN message as well).
 */

/**
 * Opaque type to represent a STUN transport.
 */
typedef struct pj_stun_sock pj_stun_sock;

/**
 * Types of operation being reported in \a on_status() callback of
 * pj_stun_sock_cb. Application may retrieve the string representation
 * of these constants with pj_stun_sock_op_name().
 */
typedef enum pj_stun_sock_op
{
    /**
     * Asynchronous DNS resolution.
     */
    PJ_STUN_SOCK_DNS_OP		= 1,

    /**
     * Initial STUN Binding request.
     */
    PJ_STUN_SOCK_BINDING_OP,

    /**
     * Subsequent STUN Binding request for keeping the binding
     * alive.
     */
    PJ_STUN_SOCK_KEEP_ALIVE_OP,

} pj_stun_sock_op;


/**
 * This structure contains callbacks that will be called by the STUN
 * transport to notify application about various events.
 */
typedef struct pj_stun_sock_cb
{
    /**
     * Notification when incoming packet has been received.
     *
     * @param stun_sock	The STUN transport.
     * @param data	The packet.
     * @param data_len	Length of the packet.
     * @param src_addr	The source address of the packet.
     * @param addr_len	The length of the source address.
     *
     * @return		Application should normally return PJ_TRUE to let
     *			the STUN transport continue its operation. However
     *			it must return PJ_FALSE if it has destroyed the
     *			STUN transport in this callback.
     */
    pj_bool_t (*on_rx_data)(pj_stun_sock *stun_sock,
			    void *pkt,
			    unsigned pkt_len,
			    const pj_sockaddr_t *src_addr,
			    unsigned addr_len);

    /**
     * Notifification when asynchronous send operation has completed.
     *
     * @param stun_sock	The STUN transport.
     * @param send_key	The send operation key that was given in
     *			#pj_stun_sock_sendto().
     * @param sent	If value is positive non-zero it indicates the
     *			number of data sent. When the value is negative,
     *			it contains the error code which can be retrieved
     *			by negating the value (i.e. status=-sent).
     *
     * @return		Application should normally return PJ_TRUE to let
     *			the STUN transport continue its operation. However
     *			it must return PJ_FALSE if it has destroyed the
     *			STUN transport in this callback.
     */
    pj_bool_t (*on_data_sent)(pj_stun_sock *stun_sock,
			      pj_ioqueue_op_key_t *send_key,
			      pj_ssize_t sent);

    /**
     * Notification when the status of the STUN transport has changed. This
     * callback may be called for the following conditions:
     *	- the first time the publicly mapped address has been resolved from
     *	  the STUN server, this callback will be called with \a op argument
     *    set to PJ_STUN_SOCK_BINDING_OP \a status  argument set to 
     *    PJ_SUCCESS.
     *  - anytime when the transport has detected that the publicly mapped
     *    address has changed, this callback will be called with \a op
     *    argument set to PJ_STUN_SOCK_KEEP_ALIVE_OP and \a status
     *    argument set to PJ_SUCCESS. On this case and the case above,
     *    application will get the resolved public address in the
     *    #pj_stun_sock_info structure.
     *  - for any terminal error (such as STUN time-out, DNS resolution
     *    failure, or keep-alive failure), this callback will be called 
     *	  with the \a status argument set to non-PJ_SUCCESS.
     *
     * @param stun_sock	The STUN transport.
     * @param op	The operation that triggers the callback.
     * @param status	The status.
     *
     * @return		Must return PJ_FALSE if it has destroyed the
     *			STUN transport in this callback. Application should
     *			normally destroy the socket and return PJ_FALSE
     *			upon encountering terminal error, otherwise it
     *			should return PJ_TRUE to let the STUN socket operation
     *			continues.
     */
    pj_bool_t	(*on_status)(pj_stun_sock *stun_sock, 
			     pj_stun_sock_op op,
			     pj_status_t status);

} pj_stun_sock_cb;


/**
 * This structure contains information about the STUN transport. Application
 * may query this information by calling #pj_stun_sock_get_info().
 */
typedef struct pj_stun_sock_info
{
    /**
     * The bound address of the socket.
     */
    pj_sockaddr	    bound_addr;

    /**
     * IP address of the STUN server.
     */
    pj_sockaddr	    srv_addr;

    /**
     * The publicly mapped address. It may contain zero address when the
     * mapped address has not been resolved. Application may query whether
     * this field contains valid address with pj_sockaddr_has_addr().
     */
    pj_sockaddr	    mapped_addr;

    /**
     * Number of interface address aliases. The interface address aliases
     * are list of all interface addresses in this host.
     */
    unsigned	    alias_cnt;

    /**
     * Array of interface address aliases.
     */
    pj_sockaddr	    aliases[PJ_ICE_ST_MAX_CAND];

} pj_stun_sock_info;


/**
 * This describe the settings to be given to the STUN transport during its
 * creation. Application should initialize this structure by calling
 * #pj_stun_sock_cfg_default().
 */
typedef struct pj_stun_sock_cfg
{
    /**
     * Packet buffer size. Default value is PJ_STUN_SOCK_PKT_LEN.
     */
    unsigned max_pkt_size;

    /**
     * Specify the number of simultaneous asynchronous read operations to
     * be invoked to the ioqueue. Having more than one read operations will
     * increase performance on multiprocessor systems since the application
     * will be able to process more than one incoming packets simultaneously.
     * Default value is 1.
     */
    unsigned async_cnt;

    /**
     * Specify the interface where the socket should be bound to. If the
     * address is zero, socket will be bound to INADDR_ANY. If the address
     * is non-zero, socket will be bound to this address only, and the
     * transport will have only one address alias (the \a alias_cnt field
     * in #pj_stun_sock_info structure.
     */
    pj_sockaddr bound_addr;

    /**
     * Specify the STUN keep-alive duration, in seconds. The STUN transport
     * does keep-alive by sending STUN Binding request to the STUN server. 
     * If this value is zero, the PJ_STUN_KEEP_ALIVE_SEC value will be used.
     * If the value is negative, it will disable STUN keep-alive.
     */
    int ka_interval;

} pj_stun_sock_cfg;



/**
 * Retrieve the name representing the specified operation.
 */
PJ_DECL(const char*) pj_stun_sock_op_name(pj_stun_sock_op op);


/**
 * Initialize the STUN transport setting with its default values.
 *
 * @param cfg	The STUN transport config.
 */
PJ_DECL(void) pj_stun_sock_cfg_default(pj_stun_sock_cfg *cfg);


/**
 * Create the STUN transport using the specified configuration. Once
 * the STUN transport has been create, application should call
 * #pj_stun_sock_start() to start the transport.
 *
 * @param stun_cfg	The STUN configuration which contains among other
 *			things the ioqueue and timer heap instance for
 *			the operation of this transport.
 * @param af		Address family of socket. Currently pj_AF_INET()
 *			and pj_AF_INET6() are supported. 
 * @param name		Optional name to be given to this transport to
 *			assist debugging.
 * @param cb		Callback to receive events/data from the transport.
 * @param cfg		Optional transport settings.
 * @param user_data	Arbitrary application data to be associated with
 *			this transport.
 * @param p_sock	Pointer to receive the created transport instance.
 *
 * @restun		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_stun_sock_create(pj_stun_config *stun_cfg,
					 const char *name,
					 int af,
					 const pj_stun_sock_cb *cb,
					 const pj_stun_sock_cfg *cfg,
					 void *user_data,
					 pj_stun_sock **p_sock);


/**
 * Start the STUN transport. This will start the DNS SRV resolution for
 * the STUN server (if desired), and once the server is resolved, STUN
 * Binding request will be sent to resolve the publicly mapped address.
 * Once the initial STUN Binding response is received, the keep-alive
 * timer will be started.
 *
 * @param stun_sock	The STUN transport instance.
 * @param domain	The domain, hostname, or IP address of the TURN
 *			server. When this parameter contains domain name,
 *			the \a resolver parameter must be set to activate
 *			DNS SRV resolution.
 * @param default_port	The default STUN port number to use when DNS SRV
 *			resolution is not used. If DNS SRV resolution is
 *			used, the server port number will be set from the
 *			DNS SRV records. The recommended value for this
 *			parameter is PJ_STUN_PORT.
 * @param resolver	If this parameter is not NULL, then the \a domain
 *			parameter will be first resolved with DNS SRV and
 *			then fallback to using DNS A/AAAA resolution when
 *			DNS SRV resolution fails. If this parameter is
 *			NULL, the \a domain parameter will be resolved as
 *			hostname.
 *
 * @return		PJ_SUCCESS if the operation has been successfully
 *			queued, or the appropriate error code on failure.
 *			When this function returns PJ_SUCCESS, the final
 *			result of the allocation process will be notified
 *			to application in \a on_state() callback.
 */
PJ_DECL(pj_status_t) pj_stun_sock_start(pj_stun_sock *stun_sock,
				        const pj_str_t *domain,
				        pj_uint16_t default_port,
				        pj_dns_resolver *resolver);

/**
 * Destroy the STUN transport.
 *
 * @param sock		The STUN transport socket.
 *
 * @restun		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_stun_sock_destroy(pj_stun_sock *sock);


/**
 * Associate a user data with this STUN transport. The user data may then
 * be retrieved later with #pj_stun_sock_get_user_data().
 *
 * @param stun_sock	The STUN transport instance.
 * @param user_data	Arbitrary data.
 *
 * @restun		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_stun_sock_set_user_data(pj_stun_sock *stun_sock,
					        void *user_data);

/**
 * Retrieve the previously assigned user data associated with this STUN
 * transport.
 *
 * @param stun_sock	The STUN transport instance.
 *
 * @restun		The user/application data.
 */
PJ_DECL(void*) pj_stun_sock_get_user_data(pj_stun_sock *stun_sock);


/**
 * Get the STUN transport info. The transport info contains, among other
 * things, the allocated relay address.
 *
 * @param stun_sock	The STUN transport instance.
 * @param info		Pointer to be filled with STUN transport info.
 *
 * @restun		PJ_SUCCESS if the operation has been successful,
 *			or the appropriate error code on failure.
 */
PJ_DECL(pj_status_t) pj_stun_sock_get_info(pj_stun_sock *stun_sock,
					   pj_stun_sock_info *info);


/**
 * Send a data to the specified address. This function may complete
 * asynchronously and in this case \a on_data_sent() will be called.
 *
 * @param stun_sock	The STUN transport instance.
 * @param op_key	Optional send key for sending the packet down to
 *			the ioqueue. This value will be given back to
 *			\a on_data_sent() callback
 * @param pkt		The data/packet to be sent to peer.
 * @param pkt_len	Length of the data.
 * @param flag		pj_ioqueue_sendto() flag.
 * @param dst_addr	The remote address.
 * @param addr_len	Length of the address.
 *
 * @return		PJ_SUCCESS if data has been sent immediately, or
 *			PJ_EPENDING if data cannot be sent immediately. In
 *			this case the \a on_data_sent() callback will be
 *			called when data is actually sent. Any other return
 *			value indicates error condition.
 */ 
PJ_DECL(pj_status_t) pj_stun_sock_sendto(pj_stun_sock *stun_sock,
					 pj_ioqueue_op_key_t *send_key,
					 const void *pkt,
					 unsigned pkt_len,
					 unsigned flag,
					 const pj_sockaddr_t *dst_addr,
					 unsigned addr_len);

/**
 * @}
 */


PJ_END_DECL


#endif	/* __PJNATH_STUN_SOCK_H__ */

