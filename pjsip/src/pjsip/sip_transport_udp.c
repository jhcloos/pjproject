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
#include <pjsip/sip_transport_udp.h>
#include <pjsip/sip_endpoint.h>
#include <pjsip/sip_errno.h>
#include <pj/addr_resolv.h>
#include <pj/assert.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/sock.h>
#include <pj/compat/socket.h>
#include <pj/string.h>


#define THIS_FILE   "sip_transport_udp.c"

/**
 * These are the target values for socket send and receive buffer sizes,
 * respectively. They will be applied to UDP socket with setsockopt().
 * When transport failed to set these size, it will decrease it until
 * sufficiently large number has been successfully set.
 *
 * The buffer size is important, especially in WinXP/2000 machines.
 * Basicly the lower the size, the more packets will be lost (dropped?)
 * when we're sending (receiving?) packets in large volumes.
 * 
 * The figure here is taken based on my experiment on WinXP/2000 machine,
 * and with this value, the rate of dropped packet is about 8% when
 * sending 1800 requests simultaneously (percentage taken as average
 * after 50K requests or so).
 *
 * More experiments are needed probably.
 */
#ifndef PJSIP_UDP_SO_SNDBUF_SIZE
#   define PJSIP_UDP_SO_SNDBUF_SIZE	(24*1024*1024)
#endif

#ifndef PJSIP_UDP_SO_RCVBUF_SIZE
#   define PJSIP_UDP_SO_RCVBUF_SIZE	(24*1024*1024)
#endif


/* Struct udp_transport "inherits" struct pjsip_transport */
struct udp_transport
{
    pjsip_transport	base;
    pj_sock_t		sock;
    pj_ioqueue_key_t   *key;
    int			rdata_cnt;
    pjsip_rx_data     **rdata;
    int			is_closing;
    pj_bool_t		is_paused;
};


/*
 * Initialize transport's receive buffer from the specified pool.
 */
static void init_rdata(struct udp_transport *tp, unsigned rdata_index,
		       pj_pool_t *pool, pjsip_rx_data **p_rdata)
{
    pjsip_rx_data *rdata;

    /* Reset pool. */
    //note: already done by caller
    //pj_pool_reset(pool);

    rdata = PJ_POOL_ZALLOC_T(pool, pjsip_rx_data);

    /* Init tp_info part. */
    rdata->tp_info.pool = pool;
    rdata->tp_info.transport = &tp->base;
    rdata->tp_info.tp_data = (void*)(long)rdata_index;
    rdata->tp_info.op_key.rdata = rdata;
    pj_ioqueue_op_key_init(&rdata->tp_info.op_key.op_key, 
			   sizeof(pj_ioqueue_op_key_t));

    tp->rdata[rdata_index] = rdata;

    if (p_rdata)
	*p_rdata = rdata;
}


/*
 * udp_on_read_complete()
 *
 * This is callback notification from ioqueue that a pending recvfrom()
 * operation has completed.
 */
static void udp_on_read_complete( pj_ioqueue_key_t *key, 
				  pj_ioqueue_op_key_t *op_key, 
				  pj_ssize_t bytes_read)
{
    enum { MAX_IMMEDIATE_PACKET = 10 };
    pjsip_rx_data_op_key *rdata_op_key = (pjsip_rx_data_op_key*) op_key;
    pjsip_rx_data *rdata = rdata_op_key->rdata;
    struct udp_transport *tp = (struct udp_transport*)rdata->tp_info.transport;
    int i;
    pj_status_t status;

    /* Don't do anything if transport is closing. */
    if (tp->is_closing) {
	tp->is_closing++;
	return;
    }

    /* Don't do anything if transport is being paused. */
    if (tp->is_paused)
	return;

    /*
     * The idea of the loop is to process immediate data received by
     * pj_ioqueue_recvfrom(), as long as i < MAX_IMMEDIATE_PACKET. When
     * i is >= MAX_IMMEDIATE_PACKET, we force the recvfrom() operation to
     * complete asynchronously, to allow other sockets to get their data.
     */
    for (i=0;; ++i) {
    	enum { MIN_SIZE = 32 };
	pj_uint32_t flags;

	/* Report the packet to transport manager. Only do so if packet size
	 * is relatively big enough for a SIP packet.
	 */
	if (bytes_read > MIN_SIZE) {
	    pj_size_t size_eaten;
	    const pj_sockaddr_in *src_addr = 
		(pj_sockaddr_in*)&rdata->pkt_info.src_addr;

	    /* Init pkt_info part. */
	    rdata->pkt_info.len = bytes_read;
	    rdata->pkt_info.zero = 0;
	    pj_gettimeofday(&rdata->pkt_info.timestamp);
	    pj_ansi_strcpy(rdata->pkt_info.src_name,
			   pj_inet_ntoa(src_addr->sin_addr));
	    rdata->pkt_info.src_port = pj_ntohs(src_addr->sin_port);

	    size_eaten = 
		pjsip_tpmgr_receive_packet(rdata->tp_info.transport->tpmgr, 
					   rdata);

	    if (size_eaten < 0) {
		pj_assert(!"It shouldn't happen!");
		size_eaten = rdata->pkt_info.len;
	    }

	    /* Since this is UDP, the whole buffer is the message. */
	    rdata->pkt_info.len = 0;

	} else if (bytes_read <= MIN_SIZE) {

	    /* TODO: */

	} else if (-bytes_read != PJ_STATUS_FROM_OS(OSERR_EWOULDBLOCK) &&
		   -bytes_read != PJ_STATUS_FROM_OS(OSERR_EINPROGRESS) && 
		   -bytes_read != PJ_STATUS_FROM_OS(OSERR_ECONNRESET)) 
	{

	    /* Report error to endpoint. */
	    PJSIP_ENDPT_LOG_ERROR((rdata->tp_info.transport->endpt,
				   rdata->tp_info.transport->obj_name,
				   -bytes_read, 
				   "Warning: pj_ioqueue_recvfrom()"
				   " callback error"));
	}

	if (i >= MAX_IMMEDIATE_PACKET) {
	    /* Force ioqueue_recvfrom() to return PJ_EPENDING */
	    flags = PJ_IOQUEUE_ALWAYS_ASYNC;
	} else {
	    flags = 0;
	}

	/* Reset pool. 
	 * Need to copy rdata fields to temp variable because they will
	 * be invalid after pj_pool_reset().
	 */
	{
	    pj_pool_t *rdata_pool = rdata->tp_info.pool;
	    struct udp_transport *rdata_tp ;
	    unsigned rdata_index;

	    rdata_tp = (struct udp_transport*)rdata->tp_info.transport;
	    rdata_index = (unsigned)(unsigned long)rdata->tp_info.tp_data;

	    pj_pool_reset(rdata_pool);
	    init_rdata(rdata_tp, rdata_index, rdata_pool, &rdata);

	    /* Change some vars to point to new location after
	     * pool reset.
	     */
	    op_key = &rdata->tp_info.op_key.op_key;
	}

	/* Only read next packet if transport is not being paused. This
	 * check handles the case where transport is paused while endpoint
	 * is still processing a SIP message.
	 */
	if (tp->is_paused)
	    return;

	/* Read next packet. */
	bytes_read = sizeof(rdata->pkt_info.packet);
	rdata->pkt_info.src_addr_len = sizeof(rdata->pkt_info.src_addr);
	status = pj_ioqueue_recvfrom(key, op_key, 
				     rdata->pkt_info.packet,
				     &bytes_read, flags,
				     &rdata->pkt_info.src_addr, 
				     &rdata->pkt_info.src_addr_len);

	if (status == PJ_SUCCESS) {
	    /* Continue loop. */
	    pj_assert(i < MAX_IMMEDIATE_PACKET);

	} else if (status == PJ_EPENDING) {
	    break;

	} else {

	    if (i < MAX_IMMEDIATE_PACKET) {

		/* Report error to endpoint if this is not EWOULDBLOCK error.*/
		if (status != PJ_STATUS_FROM_OS(OSERR_EWOULDBLOCK) &&
		    status != PJ_STATUS_FROM_OS(OSERR_EINPROGRESS) && 
		    status != PJ_STATUS_FROM_OS(OSERR_ECONNRESET)) 
		{

		    PJSIP_ENDPT_LOG_ERROR((rdata->tp_info.transport->endpt,
					   rdata->tp_info.transport->obj_name,
					   status, 
					   "Warning: pj_ioqueue_recvfrom"));
		}

		/* Continue loop. */
		bytes_read = 0;
	    } else {
		/* This is fatal error.
		 * Ioqueue operation will stop for this transport!
		 */
		PJSIP_ENDPT_LOG_ERROR((rdata->tp_info.transport->endpt,
				       rdata->tp_info.transport->obj_name,
				       status, 
				       "FATAL: pj_ioqueue_recvfrom() error, "
				       "UDP transport stopping! Error"));
		break;
	    }
	}
    }
}

/*
 * udp_on_write_complete()
 *
 * This is callback notification from ioqueue that a pending sendto()
 * operation has completed.
 */
static void udp_on_write_complete( pj_ioqueue_key_t *key, 
				   pj_ioqueue_op_key_t *op_key,
				   pj_ssize_t bytes_sent)
{
    struct udp_transport *tp = (struct udp_transport*) 
    			       pj_ioqueue_get_user_data(key);
    pjsip_tx_data_op_key *tdata_op_key = (pjsip_tx_data_op_key*)op_key;

    tdata_op_key->tdata = NULL;

    if (tdata_op_key->callback) {
	tdata_op_key->callback(&tp->base, tdata_op_key->token, bytes_sent);
    }
}

/*
 * udp_send_msg()
 *
 * This function is called by transport manager (by transport->send_msg())
 * to send outgoing message.
 */
static pj_status_t udp_send_msg( pjsip_transport *transport,
				 pjsip_tx_data *tdata,
				 const pj_sockaddr_t *rem_addr,
				 int addr_len,
				 void *token,
				 pjsip_transport_callback callback)
{
    struct udp_transport *tp = (struct udp_transport*)transport;
    pj_ssize_t size;
    pj_status_t status;

    PJ_ASSERT_RETURN(transport && tdata, PJ_EINVAL);
    PJ_ASSERT_RETURN(tdata->op_key.tdata == NULL, PJSIP_EPENDINGTX);
    
    /* Return error if transport is paused */
    if (tp->is_paused)
	return PJSIP_ETPNOTAVAIL;

    /* Init op key. */
    tdata->op_key.tdata = tdata;
    tdata->op_key.token = token;
    tdata->op_key.callback = callback;

    /* Send to ioqueue! */
    size = tdata->buf.cur - tdata->buf.start;
    status = pj_ioqueue_sendto(tp->key, (pj_ioqueue_op_key_t*)&tdata->op_key,
			       tdata->buf.start, &size, 0,
			       rem_addr, addr_len);

    if (status != PJ_EPENDING)
	tdata->op_key.tdata = NULL;

    return status;
}

/*
 * udp_destroy()
 *
 * This function is called by transport manager (by transport->destroy()).
 */
static pj_status_t udp_destroy( pjsip_transport *transport )
{
    struct udp_transport *tp = (struct udp_transport*)transport;
    int i;

    /* Mark this transport as closing. */
    tp->is_closing = 1;

    /* Cancel all pending operations. */
    /* blp: NO NO NO...
     *      No need to post queued completion as we poll the ioqueue until
     *      we've got events anyway. Posting completion will only cause
     *      callback to be called twice with IOCP: one for the post completion
     *      and another one for closing the socket.
     *
    for (i=0; i<tp->rdata_cnt; ++i) {
	pj_ioqueue_post_completion(tp->key, 
				   &tp->rdata[i]->tp_info.op_key.op_key, -1);
    }
    */

    /* Unregister from ioqueue. */
    if (tp->key) {
	pj_ioqueue_unregister(tp->key);
	tp->key = NULL;
    } else {
	/* Close socket. */
	if (tp->sock && tp->sock != PJ_INVALID_SOCKET) {
	    pj_sock_close(tp->sock);
	    tp->sock = PJ_INVALID_SOCKET;
	}
    }

    /* Must poll ioqueue because IOCP calls the callback when socket
     * is closed. We poll the ioqueue until all pending callbacks 
     * have been called.
     */
    for (i=0; i<50 && tp->is_closing < 1+tp->rdata_cnt; ++i) {
	int cnt;
	pj_time_val timeout = {0, 1};

	cnt = pj_ioqueue_poll(pjsip_endpt_get_ioqueue(transport->endpt), 
			      &timeout);
	if (cnt == 0)
	    break;
    }

    /* Destroy rdata */
    for (i=0; i<tp->rdata_cnt; ++i) {
	pj_pool_release(tp->rdata[i]->tp_info.pool);
    }

    /* Destroy reference counter. */
    if (tp->base.ref_cnt)
	pj_atomic_destroy(tp->base.ref_cnt);

    /* Destroy lock */
    if (tp->base.lock)
	pj_lock_destroy(tp->base.lock);

    /* Destroy pool. */
    pjsip_endpt_release_pool(tp->base.endpt, tp->base.pool);

    return PJ_SUCCESS;
}


/*
 * udp_shutdown()
 *
 * Start graceful UDP shutdown.
 */
static pj_status_t udp_shutdown(pjsip_transport *transport)
{
    return pjsip_transport_dec_ref(transport);
}


/* Create socket */
static pj_status_t create_socket(const pj_sockaddr_in *local_a,
				 pj_sock_t *p_sock)
{
    pj_sock_t sock;
    pj_sockaddr_in tmp_addr;
    pj_status_t status;

    status = pj_sock_socket(PJ_AF_INET, PJ_SOCK_DGRAM, 0, &sock);
    if (status != PJ_SUCCESS)
	return status;

    if (local_a == NULL) {
	pj_sockaddr_in_init(&tmp_addr, NULL, 0);
	local_a = &tmp_addr;
    }

    status = pj_sock_bind(sock, local_a, sizeof(*local_a));
    if (status != PJ_SUCCESS) {
	pj_sock_close(sock);
	return status;
    }

    *p_sock = sock;
    return PJ_SUCCESS;
}


/* Generate transport's published address */
static pj_status_t get_published_name(pj_sock_t sock,
				      char hostbuf[],
				      pjsip_host_port *bound_name)
{
    pj_sockaddr_in tmp_addr;
    int addr_len;
    pj_status_t status;

    addr_len = sizeof(tmp_addr);
    status = pj_sock_getsockname(sock, &tmp_addr, &addr_len);
    if (status != PJ_SUCCESS)
	return status;

    bound_name->host.ptr = hostbuf;
    bound_name->port = pj_ntohs(tmp_addr.sin_port);

    /* If bound address specifies "0.0.0.0", get the IP address
     * of local hostname.
     */
    if (tmp_addr.sin_addr.s_addr == PJ_INADDR_ANY) {
	pj_in_addr hostip;

	status = pj_gethostip(&hostip);
	if (status != PJ_SUCCESS)
	    return status;

	pj_strcpy2(&bound_name->host, pj_inet_ntoa(hostip));
    } else {
	/* Otherwise use bound address. */
	pj_strcpy2(&bound_name->host, pj_inet_ntoa(tmp_addr.sin_addr));
    }

    return PJ_SUCCESS;
}

/* Set the published address of the transport */
static void udp_set_pub_name(struct udp_transport *tp,
			     const pjsip_host_port *a_name)
{
    enum { INFO_LEN = 80 };

    pj_assert(a_name->host.slen != 0);
    pj_strdup_with_null(tp->base.pool, &tp->base.local_name.host, 
			&a_name->host);
    tp->base.local_name.port = a_name->port;

    /* Update transport info. */
    if (tp->base.info == NULL) {
	tp->base.info = (char*) pj_pool_alloc(tp->base.pool, INFO_LEN);
    }
    pj_ansi_snprintf( 
	tp->base.info, INFO_LEN, "udp %s:%d [published as %s:%d]",
	pj_inet_ntoa(((pj_sockaddr_in*)&tp->base.local_addr)->sin_addr),
	pj_ntohs(((pj_sockaddr_in*)&tp->base.local_addr)->sin_port),
	tp->base.local_name.host.ptr,
	tp->base.local_name.port);
}

/* Set the socket handle of the transport */
static void udp_set_socket(struct udp_transport *tp,
			   pj_sock_t sock,
			   const pjsip_host_port *a_name)
{
    long sobuf_size;
    pj_status_t status;

    /* Adjust socket rcvbuf size */
    sobuf_size = PJSIP_UDP_SO_RCVBUF_SIZE;
    status = pj_sock_setsockopt(sock, PJ_SOL_SOCKET, PJ_SO_RCVBUF,
				&sobuf_size, sizeof(sobuf_size));
    if (status != PJ_SUCCESS) {
	char errmsg[PJ_ERR_MSG_SIZE];
	pj_strerror(status, errmsg, sizeof(errmsg));
	PJ_LOG(4,(THIS_FILE, "Error setting SO_RCVBUF: %s [%d]", errmsg,
		  status));
    }

    /* Adjust socket sndbuf size */
    sobuf_size = PJSIP_UDP_SO_SNDBUF_SIZE;
    status = pj_sock_setsockopt(sock, PJ_SOL_SOCKET, PJ_SO_SNDBUF,
				&sobuf_size, sizeof(sobuf_size));
    if (status != PJ_SUCCESS) {
	char errmsg[PJ_ERR_MSG_SIZE];
	pj_strerror(status, errmsg, sizeof(errmsg));
	PJ_LOG(4,(THIS_FILE, "Error setting SO_SNDBUF: %s [%d]", errmsg,
		  status));
    }

    /* Set the socket. */
    tp->sock = sock;

    /* Init address name (published address) */
    udp_set_pub_name(tp, a_name);
}

/* Register socket to ioqueue */
static pj_status_t register_to_ioqueue(struct udp_transport *tp)
{
    pj_ioqueue_t *ioqueue;
    pj_ioqueue_callback ioqueue_cb;

    /* Ignore if already registered */
    if (tp->key != NULL)
    	return PJ_SUCCESS;
    
    /* Register to ioqueue. */
    ioqueue = pjsip_endpt_get_ioqueue(tp->base.endpt);
    pj_memset(&ioqueue_cb, 0, sizeof(ioqueue_cb));
    ioqueue_cb.on_read_complete = &udp_on_read_complete;
    ioqueue_cb.on_write_complete = &udp_on_write_complete;

    return pj_ioqueue_register_sock(tp->base.pool, ioqueue, tp->sock, tp,
				    &ioqueue_cb, &tp->key);
}

/* Start ioqueue asynchronous reading to all rdata */
static pj_status_t start_async_read(struct udp_transport *tp)
{
    pj_ioqueue_t *ioqueue;
    int i;
    pj_status_t status;

    ioqueue = pjsip_endpt_get_ioqueue(tp->base.endpt);

    /* Start reading the ioqueue. */
    for (i=0; i<tp->rdata_cnt; ++i) {
	pj_ssize_t size;

	size = sizeof(tp->rdata[i]->pkt_info.packet);
	tp->rdata[i]->pkt_info.src_addr_len = sizeof(tp->rdata[i]->pkt_info.src_addr);
	status = pj_ioqueue_recvfrom(tp->key, 
				     &tp->rdata[i]->tp_info.op_key.op_key,
				     tp->rdata[i]->pkt_info.packet,
				     &size, PJ_IOQUEUE_ALWAYS_ASYNC,
				     &tp->rdata[i]->pkt_info.src_addr,
				     &tp->rdata[i]->pkt_info.src_addr_len);
	if (status == PJ_SUCCESS) {
	    pj_assert(!"Shouldn't happen because PJ_IOQUEUE_ALWAYS_ASYNC!");
	    udp_on_read_complete(tp->key, &tp->rdata[i]->tp_info.op_key.op_key,
				 size);
	} else if (status != PJ_EPENDING) {
	    /* Error! */
	    return status;
	}
    }

    return PJ_SUCCESS;
}


/*
 * pjsip_udp_transport_attach()
 *
 * Attach UDP socket and start transport.
 */
PJ_DEF(pj_status_t) pjsip_udp_transport_attach( pjsip_endpoint *endpt,
						pj_sock_t sock,
						const pjsip_host_port *a_name,
						unsigned async_cnt,
						pjsip_transport **p_transport)
{
    pj_pool_t *pool;
    struct udp_transport *tp;
    unsigned i;
    pj_status_t status;

    PJ_ASSERT_RETURN(endpt && sock!=PJ_INVALID_SOCKET && a_name && async_cnt>0,
		     PJ_EINVAL);

    /* Create pool. */
    pool = pjsip_endpt_create_pool(endpt, "udp%p", PJSIP_POOL_LEN_TRANSPORT, 
				   PJSIP_POOL_INC_TRANSPORT);
    if (!pool)
	return PJ_ENOMEM;

    /* Create the UDP transport object. */
    tp = PJ_POOL_ZALLOC_T(pool, struct udp_transport);

    /* Save pool. */
    tp->base.pool = pool;

    /* Object name. */
    pj_ansi_snprintf(tp->base.obj_name, sizeof(tp->base.obj_name), 
		     "udp%p", tp);

    /* Init reference counter. */
    status = pj_atomic_create(pool, 0, &tp->base.ref_cnt);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Init lock. */
    status = pj_lock_create_recursive_mutex(pool, "udp%p", &tp->base.lock);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Set type. */
    tp->base.key.type = PJSIP_TRANSPORT_UDP;

    /* Remote address is left zero (except the family) */
    tp->base.key.rem_addr.addr.sa_family = PJ_AF_INET;

    /* Type name. */
    tp->base.type_name = "UDP";

    /* Transport flag */
    tp->base.flag = pjsip_transport_get_flag_from_type(PJSIP_TRANSPORT_UDP);


    /* Length of addressess. */
    tp->base.addr_len = sizeof(pj_sockaddr_in);

    /* Init local address. */
    status = pj_sock_getsockname(sock, &tp->base.local_addr, 
				 &tp->base.addr_len);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Init remote name. */
    tp->base.remote_name.host = pj_str("0.0.0.0");
    tp->base.remote_name.port = 0;

    /* Set endpoint. */
    tp->base.endpt = endpt;

    /* Transport manager and timer will be initialized by tpmgr */

    /* Attach socket and assign name. */
    udp_set_socket(tp, sock, a_name);

    /* Register to ioqueue */
    status = register_to_ioqueue(tp);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Set functions. */
    tp->base.send_msg = &udp_send_msg;
    tp->base.do_shutdown = &udp_shutdown;
    tp->base.destroy = &udp_destroy;

    /* This is a permanent transport, so we initialize the ref count
     * to one so that transport manager don't destroy this transport
     * when there's no user!
     */
    pj_atomic_inc(tp->base.ref_cnt);

    /* Register to transport manager. */
    tp->base.tpmgr = pjsip_endpt_get_tpmgr(endpt);
    status = pjsip_transport_register( tp->base.tpmgr, (pjsip_transport*)tp);
    if (status != PJ_SUCCESS)
	goto on_error;


    /* Create rdata and put it in the array. */
    tp->rdata_cnt = 0;
    tp->rdata = (pjsip_rx_data**)
    		pj_pool_calloc(tp->base.pool, async_cnt, 
			       sizeof(pjsip_rx_data*));
    for (i=0; i<async_cnt; ++i) {
	pj_pool_t *rdata_pool = pjsip_endpt_create_pool(endpt, "rtd%p", 
							PJSIP_POOL_RDATA_LEN,
							PJSIP_POOL_RDATA_INC);
	if (!rdata_pool) {
	    pj_atomic_set(tp->base.ref_cnt, 0);
	    pjsip_transport_destroy(&tp->base);
	    return PJ_ENOMEM;
	}

	init_rdata(tp, i, rdata_pool, NULL);
	tp->rdata_cnt++;
    }

    /* Start reading the ioqueue. */
    status = start_async_read(tp);
    if (status != PJ_SUCCESS) {
	pjsip_transport_destroy(&tp->base);
	return status;
    }

    /* Done. */
    if (p_transport)
	*p_transport = &tp->base;

    PJ_LOG(4,(tp->base.obj_name, 
	      "SIP UDP transport started, published address is %.*s:%d",
	      (int)tp->base.local_name.host.slen,
	      tp->base.local_name.host.ptr,
	      tp->base.local_name.port));

    return PJ_SUCCESS;

on_error:
    udp_destroy((pjsip_transport*)tp);
    return status;
}

/*
 * pjsip_udp_transport_start()
 *
 * Create a UDP socket in the specified address and start a transport.
 */
PJ_DEF(pj_status_t) pjsip_udp_transport_start( pjsip_endpoint *endpt,
					       const pj_sockaddr_in *local_a,
					       const pjsip_host_port *a_name,
					       unsigned async_cnt,
					       pjsip_transport **p_transport)
{
    pj_sock_t sock;
    pj_status_t status;
    char addr_buf[16];
    pjsip_host_port bound_name;

    PJ_ASSERT_RETURN(endpt && async_cnt, PJ_EINVAL);

    status = create_socket(local_a, &sock);
    if (status != PJ_SUCCESS)
	return status;

    if (a_name == NULL) {
	/* Address name is not specified. 
	 * Build a name based on bound address.
	 */
	status = get_published_name(sock, addr_buf, &bound_name);
	if (status != PJ_SUCCESS) {
	    pj_sock_close(sock);
	    return status;
	}

	a_name = &bound_name;
    }

    return pjsip_udp_transport_attach( endpt, sock, a_name, async_cnt, 
				       p_transport );
}


/*
 * Retrieve the internal socket handle used by the UDP transport.
 */
PJ_DEF(pj_sock_t) pjsip_udp_transport_get_socket(pjsip_transport *transport)
{
    struct udp_transport *tp;

    PJ_ASSERT_RETURN(transport != NULL, PJ_INVALID_SOCKET);

    tp = (struct udp_transport*) transport;

    return tp->sock;
}


/*
 * Temporarily pause or shutdown the transport. 
 */
PJ_DEF(pj_status_t) pjsip_udp_transport_pause(pjsip_transport *transport,
					      unsigned option)
{
    struct udp_transport *tp;
    unsigned i;

    PJ_ASSERT_RETURN(transport != NULL, PJ_EINVAL);

    /* Flag must be specified */
    PJ_ASSERT_RETURN((option & 0x03) != 0, PJ_EINVAL);

    tp = (struct udp_transport*) transport;

    /* Transport must not have been paused */
    PJ_ASSERT_RETURN(tp->is_paused==0, PJ_EINVALIDOP);

    /* Set transport to paused first, so that when the read callback is 
     * called by pj_ioqueue_post_completion() it will not try to
     * re-register the rdata.
     */
    tp->is_paused = PJ_TRUE;

    /* Cancel the ioqueue operation. */
    for (i=0; i<(unsigned)tp->rdata_cnt; ++i) {
	pj_ioqueue_post_completion(tp->key, 
				   &tp->rdata[i]->tp_info.op_key.op_key, -1);
    }

    /* Destroy the socket? */
    if (option & PJSIP_UDP_TRANSPORT_DESTROY_SOCKET) {
	if (tp->key) {
	    /* This implicitly closes the socket */
	    pj_ioqueue_unregister(tp->key);
	    tp->key = NULL;
	} else {
	    /* Close socket. */
	    if (tp->sock && tp->sock != PJ_INVALID_SOCKET) {
		pj_sock_close(tp->sock);
		tp->sock = PJ_INVALID_SOCKET;
	    }
	}
	tp->sock = PJ_INVALID_SOCKET;
	
    }

    PJ_LOG(4,(tp->base.obj_name, "SIP UDP transport paused"));

    return PJ_SUCCESS;
}


/*
 * Restart transport.
 *
 * If option is KEEP_SOCKET, just re-activate ioqueue operation.
 *
 * If option is DESTROY_SOCKET:
 *  - if socket is specified, replace.
 *  - if socket is not specified, create and replace.
 */
PJ_DEF(pj_status_t) pjsip_udp_transport_restart(pjsip_transport *transport,
					        unsigned option,
						pj_sock_t sock,
						const pj_sockaddr_in *local,
						const pjsip_host_port *a_name)
{
    struct udp_transport *tp;
    pj_status_t status;

    PJ_ASSERT_RETURN(transport != NULL, PJ_EINVAL);
    /* Flag must be specified */
    PJ_ASSERT_RETURN((option & 0x03) != 0, PJ_EINVAL);

    tp = (struct udp_transport*) transport;

    if (option & PJSIP_UDP_TRANSPORT_DESTROY_SOCKET) {
	char addr_buf[16];
	pjsip_host_port bound_name;

	/* Request to recreate transport */

	/* Destroy existing socket, if any. */
	if (tp->key) {
	    /* This implicitly closes the socket */
	    pj_ioqueue_unregister(tp->key);
	    tp->key = NULL;
	} else {
	    /* Close socket. */
	    if (tp->sock && tp->sock != PJ_INVALID_SOCKET) {
		pj_sock_close(tp->sock);
		tp->sock = PJ_INVALID_SOCKET;
	    }
	}
	tp->sock = PJ_INVALID_SOCKET;

	/* Create the socket if it's not specified */
	if (sock == PJ_INVALID_SOCKET) {
	    status = create_socket(local, &sock);
	    if (status != PJ_SUCCESS)
		return status;
	}

	/* If transport published name is not specified, calculate it
	 * from the bound address.
	 */
	if (a_name == NULL) {
	    status = get_published_name(sock, addr_buf, &bound_name);
	    if (status != PJ_SUCCESS) {
		pj_sock_close(sock);
		return status;
	    }

	    a_name = &bound_name;
	}

	/* Assign the socket and published address to transport. */
	udp_set_socket(tp, sock, a_name);

    } else {

	/* For KEEP_SOCKET, transport must have been paused before */
	PJ_ASSERT_RETURN(tp->is_paused, PJ_EINVALIDOP);

	/* If address name is specified, update it */
	if (a_name != NULL)
	    udp_set_pub_name(tp, a_name);
    }

    /* Re-register new or existing socket to ioqueue. */
    status = register_to_ioqueue(tp);
    if (status != PJ_SUCCESS) {
	return status;
    }

    /* Restart async read operation. */
    status = start_async_read(tp);
    if (status != PJ_SUCCESS)
	return status;

    /* Everything has been set up */
    tp->is_paused = PJ_FALSE;

    PJ_LOG(4,(tp->base.obj_name, 
	      "SIP UDP transport restarted, published address is %.*s:%d",
	      (int)tp->base.local_name.host.slen,
	      tp->base.local_name.host.ptr,
	      tp->base.local_name.port));

    return PJ_SUCCESS;
}

