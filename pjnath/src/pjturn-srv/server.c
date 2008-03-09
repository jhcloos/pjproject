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
#include "turn.h"
#include "auth.h"

#define MAX_CLIENTS		32
#define MAX_PEERS_PER_CLIENT	8
//#define MAX_HANDLES		(MAX_CLIENTS*MAX_PEERS_PER_CLIENT+MAX_LISTENERS)
#define MAX_HANDLES		PJ_IOQUEUE_MAX_HANDLES
#define MAX_TIMER		(MAX_HANDLES * 2)
#define MIN_PORT		49152
#define MAX_PORT		65535
#define MAX_LISTENERS		16
#define MAX_THREADS		2
#define MAX_NET_EVENTS		10

/* Prototypes */
static int server_thread_proc(void *arg);
static pj_status_t on_tx_stun_msg( pj_stun_session *sess,
				   const void *pkt,
				   pj_size_t pkt_size,
				   const pj_sockaddr_t *dst_addr,
				   unsigned addr_len);
static pj_status_t on_rx_stun_request(pj_stun_session *sess,
				      const pj_uint8_t *pkt,
				      unsigned pkt_len,
				      const pj_stun_msg *msg,
				      const pj_sockaddr_t *src_addr,
				      unsigned src_addr_len);

struct saved_cred
{
    pj_str_t realm;
    pj_str_t username;
    pj_str_t nonce;
    int	     data_type;
    pj_str_t data;
};


/*
 * Get transport type name, normally for logging purpose only.
 */
PJ_DEF(const char*) pj_turn_tp_type_name(int tp_type)
{
    /* Must be 3 characters long! */
    if (tp_type == PJ_TURN_TP_UDP) {
	return "UDP";
    } else if (tp_type == PJ_TURN_TP_TCP) {
	return "TCP";
    } else {
	pj_assert(!"Unsupported transport");
	return "???";
    }
}

/*
 * Create server.
 */
PJ_DEF(pj_status_t) pj_turn_srv_create(pj_pool_factory *pf, 
				       pj_turn_srv **p_srv)
{
    pj_pool_t *pool;
    pj_turn_srv *srv;
    unsigned i;
    pj_status_t status;

    PJ_ASSERT_RETURN(pf && p_srv, PJ_EINVAL);

    /* Create server and init core settings */
    pool = pj_pool_create(pf, "srv%p", 1000, 1000, NULL);
    srv = PJ_POOL_ZALLOC_T(pool, pj_turn_srv);
    srv->obj_name = pool->obj_name;
    srv->core.pf = pf;
    srv->core.pool = pool;
    srv->core.tls_key = srv->core.tls_data = -1;
    
    /* Create ioqueue */
    status = pj_ioqueue_create(pool, MAX_HANDLES, &srv->core.ioqueue);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Server mutex */
    status = pj_lock_create_recursive_mutex(pool, srv->obj_name, 
					    &srv->core.lock);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Allocate TLS */
    status = pj_thread_local_alloc(&srv->core.tls_key);
    if (status != PJ_SUCCESS)
	goto on_error;

    status = pj_thread_local_alloc(&srv->core.tls_data);
    if (status != PJ_SUCCESS)
	goto on_error;
    
    /* Create timer heap */
    status = pj_timer_heap_create(pool, MAX_TIMER, &srv->core.timer_heap);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Configure lock for the timer heap */
    pj_timer_heap_set_lock(srv->core.timer_heap, srv->core.lock, PJ_FALSE);

    /* Array of listeners */
    srv->core.listener = (pj_turn_listener**)
			 pj_pool_calloc(pool, MAX_LISTENERS, 
					sizeof(srv->core.listener[0]));

    /* Array of STUN sessions, one for each listener */
    srv->core.stun_sess = (pj_stun_session**)
			  pj_pool_calloc(pool, MAX_LISTENERS,
					 (sizeof(srv->core.stun_sess[0])));

    /* Create hash tables */
    srv->tables.alloc = pj_hash_create(pool, MAX_CLIENTS);
    srv->tables.res = pj_hash_create(pool, MAX_CLIENTS);

    /* Init ports settings */
    srv->ports.min_udp = srv->ports.next_udp = MIN_PORT;
    srv->ports.max_tcp = MAX_PORT;
    srv->ports.min_tcp = srv->ports.next_tcp = MIN_PORT;
    srv->ports.max_tcp = MAX_PORT;

    /* Init STUN config */
    pj_stun_config_init(&srv->core.stun_cfg, pf, 0, srv->core.ioqueue,
		        srv->core.timer_heap);

    /* Init STUN credential */
    srv->core.cred.type = PJ_STUN_AUTH_CRED_DYNAMIC;
    srv->core.cred.data.dyn_cred.user_data = srv;
    srv->core.cred.data.dyn_cred.get_auth = &pj_turn_get_auth;
    srv->core.cred.data.dyn_cred.get_cred = &pj_turn_srv_get_cred;
    srv->core.cred.data.dyn_cred.get_password = &pj_turn_get_password;
    srv->core.cred.data.dyn_cred.verify_nonce = &pj_turn_verify_nonce;

    /* Array of worker threads */
    srv->core.thread_cnt = MAX_THREADS;
    srv->core.thread = (pj_thread_t**)
		       pj_pool_calloc(pool, srv->core.thread_cnt, 
				      sizeof(pj_thread_t*));

    /* Start the worker threads */
    for (i=0; i<srv->core.thread_cnt; ++i) {
	status = pj_thread_create(pool, srv->obj_name, &server_thread_proc, 
				  srv, 0, 0, &srv->core.thread[i]);
	if (status != PJ_SUCCESS)
	    goto on_error;
    }

    /* We're done. Application should add listeners now */
    PJ_LOG(4,(srv->obj_name, "TURN server v%s is running", 
	      pj_get_version()));

    *p_srv = srv;
    return PJ_SUCCESS;

on_error:
    pj_turn_srv_destroy(srv);
    return status;
}


/* 
 * Handle timer and network events 
 */
static void srv_handle_events(pj_turn_srv *srv, const pj_time_val *max_timeout)
{
    /* timeout is 'out' var. This just to make compiler happy. */
    pj_time_val timeout = { 0, 0};
    unsigned net_event_count = 0;
    int c;

    /* Poll the timer. The timer heap has its own mutex for better 
     * granularity, so we don't need to lock the server. 
     */
    timeout.sec = timeout.msec = 0;
    c = pj_timer_heap_poll( srv->core.timer_heap, &timeout );

    /* timer_heap_poll should never ever returns negative value, or otherwise
     * ioqueue_poll() will block forever!
     */
    pj_assert(timeout.sec >= 0 && timeout.msec >= 0);
    if (timeout.msec >= 1000) timeout.msec = 999;

    /* If caller specifies maximum time to wait, then compare the value with
     * the timeout to wait from timer, and use the minimum value.
     */
    if (max_timeout && PJ_TIME_VAL_GT(timeout, *max_timeout)) {
	timeout = *max_timeout;
    }

    /* Poll ioqueue. 
     * Repeat polling the ioqueue while we have immediate events, because
     * timer heap may process more than one events, so if we only process
     * one network events at a time (such as when IOCP backend is used),
     * the ioqueue may have trouble keeping up with the request rate.
     *
     * For example, for each send() request, one network event will be
     *   reported by ioqueue for the send() completion. If we don't poll
     *   the ioqueue often enough, the send() completion will not be
     *   reported in timely manner.
     */
    do {
	c = pj_ioqueue_poll( srv->core.ioqueue, &timeout);
	if (c < 0) {
	    pj_thread_sleep(PJ_TIME_VAL_MSEC(timeout));
	    return;
	} else if (c == 0) {
	    break;
	} else {
	    net_event_count += c;
	    timeout.sec = timeout.msec = 0;
	}
    } while (c > 0 && net_event_count < MAX_NET_EVENTS);

}

/*
 * Server worker thread proc.
 */
static int server_thread_proc(void *arg)
{
    pj_turn_srv *srv = (pj_turn_srv*)arg;

    while (!srv->core.quit) {
	pj_time_val timeout_max = {0, 500};
	srv_handle_events(srv, &timeout_max);
    }

    return 0;
}

/*
 * Destroy the server.
 */
PJ_DEF(pj_status_t) pj_turn_srv_destroy(pj_turn_srv *srv)
{
    pj_hash_iterator_t itbuf, *it;
    unsigned i;

    /* Stop all worker threads */
    srv->core.quit = PJ_TRUE;
    for (i=0; i<srv->core.thread_cnt; ++i) {
	if (srv->core.thread[i]) {
	    pj_thread_join(srv->core.thread[i]);
	    pj_thread_destroy(srv->core.thread[i]);
	    srv->core.thread[i] = NULL;
	}
    }

    /* Destroy all listeners and STUN sessions associated with them. */
    for (i=0; i<srv->core.lis_cnt; ++i) {
	if (srv->core.listener[i]) {
	    pj_turn_listener_destroy(srv->core.listener[i]);
	    srv->core.listener[i] = NULL;
	}
	if (srv->core.stun_sess[i]) {
	    pj_stun_session_destroy(srv->core.stun_sess[i]);
	    srv->core.stun_sess[i] = NULL;
	}
    }

    /* Destroy all allocations */
    if (srv->tables.alloc) {
	it = pj_hash_first(srv->tables.alloc, &itbuf);
	while (it != NULL) {
	    pj_turn_allocation *alloc = (pj_turn_allocation*)
					pj_hash_this(srv->tables.alloc, it);
	    pj_turn_allocation_destroy(alloc);
	    it = pj_hash_next(srv->tables.alloc, it);
	}
    }
    

    /* Destroy hash tables (well, sort of) */
    if (srv->tables.alloc) {
	srv->tables.alloc = NULL;
	srv->tables.res = NULL;
    }
    
    /* Destroy timer heap */
    if (srv->core.timer_heap) {
	pj_timer_heap_destroy(srv->core.timer_heap);
	srv->core.timer_heap = NULL;
    }

    /* Destroy ioqueue */
    if (srv->core.ioqueue) {
	pj_ioqueue_destroy(srv->core.ioqueue);
	srv->core.ioqueue = NULL;
    }

    /* Destroy thread local IDs */
    if (srv->core.tls_key != -1) {
	pj_thread_local_free(srv->core.tls_key);
	srv->core.tls_key = -1;
    }
    if (srv->core.tls_data != -1) {
	pj_thread_local_free(srv->core.tls_data);
	srv->core.tls_data = -1;
    }

    /* Destroy server lock */
    if (srv->core.lock) {
	pj_lock_destroy(srv->core.lock);
	srv->core.lock = NULL;
    }

    /* Release pool */
    if (srv->core.pool) {
	pj_pool_t *pool = srv->core.pool;
	srv->core.pool = NULL;
	pj_pool_release(pool);
    }

    /* Done */
    return PJ_SUCCESS;
}


/*
 * Add listener.
 */
PJ_DEF(pj_status_t) pj_turn_srv_add_listener(pj_turn_srv *srv,
					     pj_turn_listener *lis)
{
    pj_stun_session_cb sess_cb;
    unsigned index;
    pj_stun_session *sess;
    pj_status_t status;

    PJ_ASSERT_RETURN(srv && lis, PJ_EINVAL);
    PJ_ASSERT_RETURN(srv->core.lis_cnt < MAX_LISTENERS, PJ_ETOOMANY);

    /* Add to array */
    index = srv->core.lis_cnt;
    srv->core.listener[index] = lis;
    lis->server = srv;

    /* Create STUN session to handle new allocation */
    pj_bzero(&sess_cb, sizeof(sess_cb));
    sess_cb.on_rx_request = &on_rx_stun_request;
    sess_cb.on_send_msg = &on_tx_stun_msg;

    status = pj_stun_session_create(&srv->core.stun_cfg, lis->obj_name, 
				    &sess_cb, PJ_FALSE, &sess);
    if (status != PJ_SUCCESS) {
	srv->core.listener[index] = NULL;
	return status;
    }

    pj_stun_session_set_user_data(sess, lis);
    pj_stun_session_set_credential(sess, &srv->core.cred);

    srv->core.stun_sess[index] = sess;
    lis->id = index;
    srv->core.lis_cnt++;

    PJ_LOG(4,(srv->obj_name, "Listener %s/%s added at index %d",
	      lis->obj_name, lis->info, lis->id));

    return PJ_SUCCESS;
}


/*
 * Send packet with this listener.
 */
PJ_DEF(pj_status_t) pj_turn_listener_sendto(pj_turn_listener *listener,
					    const void *packet,
					    pj_size_t size,
					    unsigned flag,
					    const pj_sockaddr_t *addr,
					    int addr_len)
{
    pj_assert(listener->id != PJ_TURN_INVALID_LIS_ID);
    return listener->sendto(listener, packet, size, flag, addr, addr_len);
}


/*
 * Destroy listener.
 */
PJ_DEF(pj_status_t) pj_turn_listener_destroy(pj_turn_listener *listener)
{
    pj_turn_srv *srv = listener->server;
    unsigned i;

    /* Remove from our listener list */
    pj_lock_acquire(srv->core.lock);
    for (i=0; i<srv->core.lis_cnt; ++i) {
	if (srv->core.listener[i] == listener) {
	    srv->core.listener[i] = NULL;
	    srv->core.lis_cnt--;
	    listener->id = PJ_TURN_INVALID_LIS_ID;
	    if (srv->core.stun_sess[i]) {
		pj_stun_session_destroy(srv->core.stun_sess[i]);
		srv->core.stun_sess[i] = NULL;
	    }
	    break;
	}
    }
    pj_lock_release(srv->core.lock);

    /* Destroy */
    return listener->destroy(listener);
}


/*
 * Register an allocation to the hash tables.
 */
PJ_DEF(pj_status_t) pj_turn_srv_register_allocation(pj_turn_srv *srv,
						    pj_turn_allocation *alloc)
{
    /* Add to hash tables */
    pj_lock_acquire(srv->core.lock);
    pj_hash_set(alloc->pool, srv->tables.alloc,
		&alloc->hkey, sizeof(alloc->hkey), 0, alloc);
    pj_hash_set(alloc->pool, srv->tables.res,
		&alloc->relay.hkey, sizeof(alloc->relay.hkey), 0,
		&alloc->relay);
    pj_lock_release(srv->core.lock);

    return PJ_SUCCESS;
}


/*
 * Unregister an allocation from the hash tables.
 */
PJ_DEF(pj_status_t) pj_turn_srv_unregister_allocation(pj_turn_srv *srv,
						     pj_turn_allocation *alloc)
{
    /* Unregister from hash tables */
    pj_lock_acquire(srv->core.lock);
    pj_hash_set(alloc->pool, srv->tables.alloc,
		&alloc->hkey, sizeof(alloc->hkey), 0, NULL);
    pj_hash_set(alloc->pool, srv->tables.res,
		&alloc->relay.hkey, sizeof(alloc->relay.hkey), 0, NULL);
    pj_lock_release(srv->core.lock);

    return PJ_SUCCESS;
}


/* Callback from our own STUN session whenever it needs to send 
 * outgoing STUN packet.
 */
static pj_status_t on_tx_stun_msg( pj_stun_session *sess,
				   const void *pkt,
				   pj_size_t pkt_size,
				   const pj_sockaddr_t *dst_addr,
				   unsigned addr_len)
{
    pj_turn_listener *listener;
    
    listener = (pj_turn_listener*) pj_stun_session_get_user_data(sess);

    PJ_ASSERT_RETURN(listener!=NULL, PJ_EINVALIDOP);

    return pj_turn_listener_sendto(listener, pkt, pkt_size, 0, 
				   dst_addr, addr_len);
}


/* Respond to STUN request */
static pj_status_t stun_respond(pj_turn_srv *srv,
				pj_stun_session *sess, 
				const pj_stun_msg *req,
				unsigned code, 
				const char *errmsg,
				pj_bool_t cache, 
				const pj_sockaddr_t *dst_addr, 
				unsigned addr_len)
{
    pj_status_t status;
    pj_str_t reason;
    pj_stun_tx_data *tdata;

    /* Create response */
    status = pj_stun_session_create_res(sess, req, code, 
					(errmsg?pj_cstr(&reason,errmsg):NULL),
					&tdata);
    if (status != PJ_SUCCESS)
	return status;

    /* Store the credential for future lookup. */
    if (pj_stun_auth_valid_for_msg(tdata->msg)) {
	pj_turn_srv_put_cred(srv, req, tdata);
    }

    /* Send the response */
    return pj_stun_session_send_msg(sess, cache, dst_addr,  addr_len, tdata);
}


/*
 * Store the credential to put placed for the specified message for
 * future retrieval.
 */
PJ_DEF(pj_status_t) pj_turn_srv_put_cred(pj_turn_srv *srv,
				         const pj_stun_msg *req,
					 pj_stun_tx_data *response)
{
    pj_stun_username_attr *user;
    pj_stun_realm_attr *realm;
    pj_stun_nonce_attr *nonce;
    struct saved_cred *saved_cred;
    pj_status_t status;

    realm = (pj_stun_realm_attr*)
	    pj_stun_msg_find_attr(req, PJ_STUN_ATTR_REALM, 0);
    PJ_ASSERT_RETURN(realm != NULL, PJ_EBUG);

    user = (pj_stun_username_attr*)
	   pj_stun_msg_find_attr(req, PJ_STUN_ATTR_USERNAME, 0);
    PJ_ASSERT_RETURN(user != NULL, PJ_EBUG);

    nonce = (pj_stun_nonce_attr*)
	    pj_stun_msg_find_attr(req, PJ_STUN_ATTR_NONCE, 0);
    PJ_ASSERT_RETURN(nonce != NULL, PJ_EBUG);

    saved_cred = PJ_POOL_ALLOC_T(response->pool, struct saved_cred);

    /* Lookup the password */
    status = pj_turn_get_password(response->msg, NULL, &realm->value, 
				  &user->value, response->pool, 
				  &saved_cred->data_type, 
				  &saved_cred->data);
    if (status != PJ_SUCCESS)
	return status;

    /* Store credential */
    pj_strdup(response->pool, &saved_cred->username, &user->value);
    pj_strdup(response->pool, &saved_cred->realm, &realm->value);
    pj_strdup(response->pool, &saved_cred->nonce, &nonce->value);

    pj_thread_local_set(srv->core.tls_key, response->msg);
    pj_thread_local_set(srv->core.tls_data, saved_cred);

    return PJ_SUCCESS;
}


/**
 * Retrieve previously stored credential for the specified message.
 */
PJ_DEF(pj_status_t) pj_turn_srv_get_cred(const pj_stun_msg *msg,
					 void *user_data,
					 pj_pool_t *pool,
					 pj_str_t *realm,
					 pj_str_t *username,
					 pj_str_t *nonce,
					 int *data_type,
					 pj_str_t *data)
{
    pj_turn_srv *srv;
    const pj_stun_msg *saved_msg;
    struct saved_cred *saved_cred;

    PJ_UNUSED_ARG(pool);

    srv = (pj_turn_srv*)user_data;

    /* Lookup stored message and make sure it's for the same message */
    saved_msg = (const pj_stun_msg*)
	        pj_thread_local_get(srv->core.tls_key);
    PJ_ASSERT_RETURN(saved_msg==msg, PJ_ENOTFOUND);

    /* Lookup saved credential */
    saved_cred = (struct saved_cred*) 
		 pj_thread_local_get(srv->core.tls_data);
    PJ_ASSERT_RETURN(saved_cred != NULL, PJ_ENOTFOUND);


    *realm = saved_cred->realm;
    *username = saved_cred->username;
    *nonce = saved_cred->nonce;
    *data_type = saved_cred->data_type;
    *data = saved_cred->data;


    /* Don't clear saved_cred as this may be called more than once */

    return PJ_SUCCESS;
}


/* Callback from our own STUN session when incoming request arrives.
 * This function is triggered by pj_stun_session_on_rx_pkt() call in
  * pj_turn_srv_on_rx_pkt() function below.
 */
static pj_status_t on_rx_stun_request(pj_stun_session *sess,
				      const pj_uint8_t *pkt,
				      unsigned pkt_len,
				      const pj_stun_msg *msg,
				      const pj_sockaddr_t *src_addr,
				      unsigned src_addr_len)
{
    pj_turn_listener *listener;
    pj_turn_srv *srv;
    pj_turn_allocation *alloc;
    pj_status_t status;

    PJ_UNUSED_ARG(pkt);
    PJ_UNUSED_ARG(pkt_len);

    listener = (pj_turn_listener*) pj_stun_session_get_user_data(sess);
    srv = listener->server;

    /* Respond any requests other than ALLOCATE with 437 response */
    if (msg->hdr.type != PJ_STUN_ALLOCATE_REQUEST) {
	stun_respond(srv, sess, msg, PJ_STUN_SC_ALLOCATION_MISMATCH,
		     NULL, PJ_FALSE, src_addr, src_addr_len);
	return PJ_SUCCESS;
    }

    /* Create new allocation. The relay resource will be allocated
     * in this function.
     */
    status = pj_turn_allocation_create(listener, src_addr, src_addr_len,
				       msg, sess, &alloc);
    if (status != PJ_SUCCESS) {
	/* STUN response has been sent, no need to reply here */
	return PJ_SUCCESS;
    }

    /* Done. */
    return PJ_SUCCESS;
}


/*
 * This callback is called by UDP listener on incoming packet. This is
 * the first entry for incoming packet (from client) to the server. From
 * here, the packet may be handed over to an allocation if an allocation
 * is found for the client address, or handed over to owned STUN session
 * if an allocation is not found.
 */
PJ_DEF(void) pj_turn_srv_on_rx_pkt(pj_turn_srv *srv, 
				   pj_turn_pkt *pkt)
{
    pj_turn_allocation *alloc;

    /* Get TURN allocation from the source address */
    pj_lock_acquire(srv->core.lock);
    alloc = pj_hash_get(srv->tables.alloc, &pkt->src, sizeof(pkt->src), NULL);
    pj_lock_release(srv->core.lock);

    /* If allocation is found, just hand over the packet to the
     * allocation.
     */
    if (alloc) {
	pj_turn_allocation_on_rx_client_pkt(alloc, pkt);
    } else {
	/* Otherwise this is a new client */
	unsigned options, lis_id;
	pj_status_t status;

	/* Check that this is a STUN message */
	options = PJ_STUN_CHECK_PACKET;
	if (pkt->listener->tp_type == PJ_TURN_TP_UDP)
	    options |= PJ_STUN_IS_DATAGRAM;

	status = pj_stun_msg_check(pkt->pkt, pkt->len, options);
	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    char ip[PJ_INET6_ADDRSTRLEN+10];

	    pj_strerror(status, errmsg, sizeof(errmsg));
	    PJ_LOG(5,(srv->obj_name, 
		      "Non STUN packet from %s is dropped: %s",
		      pj_sockaddr_print(&pkt->src.clt_addr, ip, sizeof(ip), 3),
		      errmsg));
	    return;
	}

	lis_id = pkt->listener->id;

	/* Hand over processing to STUN session. This will trigger
	 * on_rx_stun_request() callback to be called if the STUN
	 * message is a request.
	 */
	options &= ~PJ_STUN_CHECK_PACKET;
	status = pj_stun_session_on_rx_pkt(srv->core.stun_sess[lis_id], 
					   pkt->pkt, pkt->len, options, NULL,
					   &pkt->src.clt_addr, 
					   pkt->src_addr_len);
	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    char ip[PJ_INET6_ADDRSTRLEN+10];

	    pj_strerror(status, errmsg, sizeof(errmsg));
	    PJ_LOG(5,(srv->obj_name, 
		      "Error processing STUN packet from %s: %s",
		      pj_sockaddr_print(&pkt->src.clt_addr, ip, sizeof(ip), 3),
		      errmsg));
	    return;
	}
    }
}


