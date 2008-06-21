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
#include <pjnath/turn_session.h>
#include <pjnath/errno.h>
#include <pjlib-util/srv_resolver.h>
#include <pj/addr_resolv.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/hash.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/sock.h>

#define PJ_TURN_CHANNEL_MIN	    0x4000
#define PJ_TURN_CHANNEL_MAX	    0xFFFE  /* inclusive */
#define PJ_TURN_PEER_HTABLE_SIZE    8

static const char *state_names[] = 
{
    "Null",
    "Resolving",
    "Resolved",
    "Allocating",
    "Ready",
    "Deallocating",
    "Deallocated",
    "Destroying"
};

enum timer_id_t
{
    TIMER_NONE,
    TIMER_KEEP_ALIVE,
    TIMER_DESTROY
};


struct peer
{
    pj_uint16_t	    ch_id;
    pj_bool_t	    bound;
    pj_sockaddr	    addr;
    pj_time_val	    expiry;
};

struct pj_turn_session
{
    pj_pool_t		*pool;
    const char		*obj_name;
    pj_turn_session_cb	 cb;
    void		*user_data;
    pj_stun_config	 stun_cfg;

    pj_lock_t		*lock;
    int			 busy;

    pj_turn_state_t	 state;
    pj_status_t		 last_status;
    pj_bool_t		 pending_destroy;
    pj_bool_t		 destroy_notified;

    pj_stun_session	*stun;

    unsigned		 lifetime;
    int			 ka_interval;
    pj_time_val		 expiry;

    pj_timer_heap_t	*timer_heap;
    pj_timer_entry	 timer;

    pj_dns_async_query	*dns_async;
    pj_uint16_t		 default_port;

    pj_uint16_t		 af;
    pj_turn_tp_type	 conn_type;
    pj_uint16_t		 srv_addr_cnt;
    pj_sockaddr		*srv_addr_list;
    pj_sockaddr		*srv_addr;

    pj_bool_t		 pending_alloc;
    pj_turn_alloc_param	 alloc_param;

    pj_sockaddr		 mapped_addr;
    pj_sockaddr		 relay_addr;

    pj_hash_table_t	*peer_table;

    pj_uint32_t		 send_ind_tsx_id[3];
    /* tx_pkt must be 16bit aligned */
    pj_uint8_t		 tx_pkt[PJ_TURN_MAX_PKT_LEN];

    pj_uint16_t		 next_ch;
};


/*
 * Prototypes.
 */
static void sess_shutdown(pj_turn_session *sess,
			  pj_status_t status);
static void do_destroy(pj_turn_session *sess);
static void send_refresh(pj_turn_session *sess, int lifetime);
static pj_status_t stun_on_send_msg(pj_stun_session *sess,
				    void *token,
				    const void *pkt,
				    pj_size_t pkt_size,
				    const pj_sockaddr_t *dst_addr,
				    unsigned addr_len);
static void stun_on_request_complete(pj_stun_session *sess,
				     pj_status_t status,
				     void *token,
				     pj_stun_tx_data *tdata,
				     const pj_stun_msg *response,
				     const pj_sockaddr_t *src_addr,
				     unsigned src_addr_len);
static pj_status_t stun_on_rx_indication(pj_stun_session *sess,
					 const pj_uint8_t *pkt,
					 unsigned pkt_len,
					 const pj_stun_msg *msg,
					 void *token,
					 const pj_sockaddr_t *src_addr,
					 unsigned src_addr_len);
static void dns_srv_resolver_cb(void *user_data,
				pj_status_t status,
				const pj_dns_srv_record *rec);
static struct peer *lookup_peer_by_addr(pj_turn_session *sess,
					const pj_sockaddr_t *addr,
					unsigned addr_len,
					pj_bool_t update,
					pj_bool_t bind_channel);
static struct peer *lookup_peer_by_chnum(pj_turn_session *sess,
					 pj_uint16_t chnum);
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e);


/*
 * Create default pj_turn_alloc_param.
 */
PJ_DEF(void) pj_turn_alloc_param_default(pj_turn_alloc_param *prm)
{
    pj_bzero(prm, sizeof(*prm));
}

/*
 * Duplicate pj_turn_alloc_param.
 */
PJ_DEF(void) pj_turn_alloc_param_copy( pj_pool_t *pool, 
				       pj_turn_alloc_param *dst,
				       const pj_turn_alloc_param *src)
{
    PJ_UNUSED_ARG(pool);
    pj_memcpy(dst, src, sizeof(*dst));
}

/*
 * Get TURN state name.
 */
PJ_DEF(const char*) pj_turn_state_name(pj_turn_state_t state)
{
    return state_names[state];
}

/*
 * Create TURN client session.
 */
PJ_DEF(pj_status_t) pj_turn_session_create( const pj_stun_config *cfg,
					    const char *name,
					    int af,
					    pj_turn_tp_type conn_type,
					    const pj_turn_session_cb *cb,
					    unsigned options,
					    void *user_data,
					    pj_turn_session **p_sess)
{
    pj_pool_t *pool;
    pj_turn_session *sess;
    pj_stun_session_cb stun_cb;
    pj_lock_t *null_lock;
    pj_status_t status;

    PJ_ASSERT_RETURN(cfg && cfg->pf && cb && p_sess, PJ_EINVAL);
    PJ_ASSERT_RETURN(cb->on_send_pkt, PJ_EINVAL);

    PJ_UNUSED_ARG(options);

    if (name == NULL)
	name = "turn%p";

    /* Allocate and create TURN session */
    pool = pj_pool_create(cfg->pf, name, 1000, 1000, NULL);
    sess = PJ_POOL_ZALLOC_T(pool, pj_turn_session);
    sess->pool = pool;
    sess->obj_name = pool->obj_name;
    sess->timer_heap = cfg->timer_heap;
    sess->af = (pj_uint16_t)af;
    sess->conn_type = conn_type;
    sess->ka_interval = PJ_TURN_KEEP_ALIVE_SEC;
    sess->user_data = user_data;
    sess->next_ch = PJ_TURN_CHANNEL_MIN;

    /* Copy STUN session */
    pj_memcpy(&sess->stun_cfg, cfg, sizeof(pj_stun_config));

    /* Copy callback */
    pj_memcpy(&sess->cb, cb, sizeof(*cb));

    /* Peer hash table */
    sess->peer_table = pj_hash_create(pool, PJ_TURN_PEER_HTABLE_SIZE);

    /* Session lock */
    status = pj_lock_create_recursive_mutex(pool, sess->obj_name, 
					    &sess->lock);
    if (status != PJ_SUCCESS) {
	do_destroy(sess);
	return status;
    }

    /* Timer */
    pj_timer_entry_init(&sess->timer, TIMER_NONE, sess, &on_timer_event);

    /* Create STUN session */
    pj_bzero(&stun_cb, sizeof(stun_cb));
    stun_cb.on_send_msg = &stun_on_send_msg;
    stun_cb.on_request_complete = &stun_on_request_complete;
    stun_cb.on_rx_indication = &stun_on_rx_indication;
    status = pj_stun_session_create(&sess->stun_cfg, sess->obj_name, &stun_cb,
				    PJ_FALSE, &sess->stun);
    if (status != PJ_SUCCESS) {
	do_destroy(sess);
	return status;
    }

    /* Attach ourself to STUN session */
    pj_stun_session_set_user_data(sess->stun, sess);

    /* Replace mutex in STUN session with a NULL mutex, since access to
     * STUN session is serialized.
     */
    status = pj_lock_create_null_mutex(pool, name, &null_lock);
    if (status != PJ_SUCCESS) {
	do_destroy(sess);
	return status;
    }
    pj_stun_session_set_lock(sess->stun, null_lock, PJ_TRUE);

    /* Done */

    PJ_LOG(4,(sess->obj_name, "TURN client session created"));

    *p_sess = sess;
    return PJ_SUCCESS;
}


/* Destroy */
static void do_destroy(pj_turn_session *sess)
{
    /* Lock session */
    if (sess->lock) {
	pj_lock_acquire(sess->lock);
    }

    /* Cancel pending timer, if any */
    if (sess->timer.id != TIMER_NONE) {
	pj_timer_heap_cancel(sess->timer_heap, &sess->timer);
	sess->timer.id = TIMER_NONE;
    }

    /* Destroy STUN session */
    if (sess->stun) {
	pj_stun_session_destroy(sess->stun);
	sess->stun = NULL;
    }

    /* Destroy lock */
    if (sess->lock) {
	pj_lock_release(sess->lock);
	pj_lock_destroy(sess->lock);
	sess->lock = NULL;
    }

    /* Destroy pool */
    if (sess->pool) {
	pj_pool_t *pool = sess->pool;

	PJ_LOG(4,(sess->obj_name, "TURN client session destroyed"));

	sess->pool = NULL;
	pj_pool_release(pool);
    }
}


/* Set session state */
static void set_state(pj_turn_session *sess, enum pj_turn_state_t state)
{
    pj_turn_state_t old_state = sess->state;

    if (state==sess->state)
	return;

    PJ_LOG(4,(sess->obj_name, "State changed %s --> %s",
	      state_names[old_state], state_names[state]));
    sess->state = state;

    if (sess->cb.on_state) {
	(*sess->cb.on_state)(sess, old_state, state);
    }
}

/*
 * Notify application and shutdown the TURN session.
 */
static void sess_shutdown(pj_turn_session *sess,
			  pj_status_t status)
{
    pj_bool_t can_destroy = PJ_TRUE;

    PJ_LOG(4,(sess->obj_name, "Request to shutdown in state %s, cause:%d",
	      state_names[sess->state], status));

    switch (sess->state) {
    case PJ_TURN_STATE_NULL:
	break;
    case PJ_TURN_STATE_RESOLVING:
	if (sess->dns_async != NULL) {
	    pj_dns_resolver_cancel_query(sess->dns_async, PJ_FALSE);
	    sess->dns_async = NULL;
	}
	break;
    case PJ_TURN_STATE_RESOLVED:
	break;
    case PJ_TURN_STATE_ALLOCATING:
	/* We need to wait until allocation complete */
	sess->pending_destroy = PJ_TRUE;
	can_destroy = PJ_FALSE;
	break;
    case PJ_TURN_STATE_READY:
	/* Send REFRESH with LIFETIME=0 */
	can_destroy = PJ_FALSE;
	send_refresh(sess, 0);
	break;
    case PJ_TURN_STATE_DEALLOCATING:
	can_destroy = PJ_FALSE;
	/* This may recursively call this function again with
	 * state==PJ_TURN_STATE_DEALLOCATED.
	 */
	send_refresh(sess, 0);
	break;
    case PJ_TURN_STATE_DEALLOCATED:
    case PJ_TURN_STATE_DESTROYING:
	break;
    }

    if (can_destroy) {
	/* Schedule destroy */
	pj_time_val delay = {0, 0};

	set_state(sess, PJ_TURN_STATE_DESTROYING);

	if (sess->timer.id != TIMER_NONE) {
	    pj_timer_heap_cancel(sess->timer_heap, &sess->timer);
	    sess->timer.id = TIMER_NONE;
	}

	sess->timer.id = TIMER_DESTROY;
	pj_timer_heap_schedule(sess->timer_heap, &sess->timer, &delay);
    }
}


/*
 * Public API to destroy TURN client session.
 */
PJ_DEF(pj_status_t) pj_turn_session_shutdown(pj_turn_session *sess)
{
    PJ_ASSERT_RETURN(sess, PJ_EINVAL);

    pj_lock_acquire(sess->lock);

    sess_shutdown(sess, PJ_SUCCESS);

    pj_lock_release(sess->lock);

    return PJ_SUCCESS;
}


/**
 * Forcefully destroy the TURN session.
 */
PJ_DEF(pj_status_t) pj_turn_session_destroy( pj_turn_session *sess)
{
    PJ_ASSERT_RETURN(sess, PJ_EINVAL);

    set_state(sess, PJ_TURN_STATE_DEALLOCATED);
    sess_shutdown(sess, PJ_SUCCESS);
    return PJ_SUCCESS;
}


/*
 * Get TURN session info.
 */
PJ_DEF(pj_status_t) pj_turn_session_get_info( pj_turn_session *sess,
					      pj_turn_session_info *info)
{
    pj_time_val now;

    PJ_ASSERT_RETURN(sess && info, PJ_EINVAL);

    pj_gettimeofday(&now);

    info->state = sess->state;
    info->conn_type = sess->conn_type;
    info->lifetime = sess->expiry.sec - now.sec;
    info->last_status = sess->last_status;

    if (sess->srv_addr)
	pj_memcpy(&info->server, sess->srv_addr, sizeof(info->server));
    else
	pj_bzero(&info->server, sizeof(info->server));

    pj_memcpy(&info->mapped_addr, &sess->mapped_addr, 
	      sizeof(sess->mapped_addr));
    pj_memcpy(&info->relay_addr, &sess->relay_addr, 
	      sizeof(sess->relay_addr));

    return PJ_SUCCESS;
}


/*
 * Re-assign user data.
 */
PJ_DEF(pj_status_t) pj_turn_session_set_user_data( pj_turn_session *sess,
						   void *user_data)
{
    sess->user_data = user_data;
    return PJ_SUCCESS;
}


/**
 * Retrieve user data.
 */
PJ_DEF(void*) pj_turn_session_get_user_data(pj_turn_session *sess)
{
    return sess->user_data;
}


/*
 * Configure message logging. By default all flags are enabled.
 *
 * @param sess		The TURN client session.
 * @param flags		Bitmask combination of #pj_stun_sess_msg_log_flag
 */
PJ_DEF(void) pj_turn_session_set_log( pj_turn_session *sess,
				      unsigned flags)
{
    pj_stun_session_set_log(sess->stun, flags);
}


/**
 * Set the server or domain name of the server.
 */
PJ_DEF(pj_status_t) pj_turn_session_set_server( pj_turn_session *sess,
					        const pj_str_t *domain,
						int default_port,
						pj_dns_resolver *resolver)
{
    pj_sockaddr tmp_addr;
    pj_bool_t is_ip_addr;
    pj_status_t status;

    PJ_ASSERT_RETURN(sess && domain, PJ_EINVAL);
    PJ_ASSERT_RETURN(sess->state == PJ_TURN_STATE_NULL, PJ_EINVALIDOP);

    pj_lock_acquire(sess->lock);

    /* See if "domain" contains just IP address */
    tmp_addr.addr.sa_family = sess->af;
    status = pj_inet_pton(sess->af, domain, 
			  pj_sockaddr_get_addr(&tmp_addr));
    is_ip_addr = (status == PJ_SUCCESS);

    if (!is_ip_addr && resolver) {
	/* Resolve with DNS SRV resolution, and fallback to DNS A resolution
	 * if default_port is specified.
	 */
	unsigned opt = 0;
	pj_str_t res_name;

	switch (sess->conn_type) {
	case PJ_TURN_TP_UDP:
	    res_name = pj_str("_turn._udp.");
	    break;
	case PJ_TURN_TP_TCP:
	    res_name = pj_str("_turn._tcp.");
	    break;
	case PJ_TURN_TP_TLS:
	    res_name = pj_str("_turns._tcp.");
	    break;
	default:
	    status = PJNATH_ETURNINTP;
	    goto on_return;
	}

	/* Fallback to DNS A only if default port is specified */
	if (default_port>0 && default_port<65536) {
	    opt = PJ_DNS_SRV_FALLBACK_A;
	    sess->default_port = (pj_uint16_t)default_port;
	}

	PJ_LOG(5,(sess->obj_name, "Resolving %.*s%.*s with DNS SRV",
		  (int)res_name.slen, res_name.ptr,
		  (int)domain->slen, domain->ptr));
	set_state(sess, PJ_TURN_STATE_RESOLVING);

	/* User may have destroyed us in the callback */
	if (sess->state != PJ_TURN_STATE_RESOLVING) {
	    status = PJ_ECANCELLED;
	    goto on_return;
	}

	status = pj_dns_srv_resolve(domain, &res_name, default_port, 
				    sess->pool, resolver, opt, sess, 
				    &dns_srv_resolver_cb, &sess->dns_async);
	if (status != PJ_SUCCESS) {
	    set_state(sess, PJ_TURN_STATE_NULL);
	    goto on_return;
	}

    } else {
	/* Resolver is not specified, resolve with standard gethostbyname().
	 * The default_port MUST be specified in this case.
	 */
	pj_addrinfo *ai;
	unsigned i, cnt;

	/* Default port must be specified */
	PJ_ASSERT_RETURN(default_port>0 && default_port<65536, PJ_EINVAL);
	sess->default_port = (pj_uint16_t)default_port;

	cnt = PJ_TURN_MAX_DNS_SRV_CNT;
	ai = (pj_addrinfo*)
	     pj_pool_calloc(sess->pool, cnt, sizeof(pj_addrinfo));

	PJ_LOG(5,(sess->obj_name, "Resolving %.*s with DNS A",
		  (int)domain->slen, domain->ptr));
	set_state(sess, PJ_TURN_STATE_RESOLVING);

	/* User may have destroyed us in the callback */
	if (sess->state != PJ_TURN_STATE_RESOLVING) {
	    status = PJ_ECANCELLED;
	    goto on_return;
	}

	status = pj_getaddrinfo(sess->af, domain, &cnt, ai);
	if (status != PJ_SUCCESS)
	    goto on_return;

	sess->srv_addr_cnt = (pj_uint16_t)cnt;
	sess->srv_addr_list = (pj_sockaddr*)
		              pj_pool_calloc(sess->pool, cnt, 
					     sizeof(pj_sockaddr));
	for (i=0; i<cnt; ++i) {
	    pj_sockaddr *addr = &sess->srv_addr_list[i];
	    pj_memcpy(addr, &ai[i].ai_addr, sizeof(pj_sockaddr));
	    addr->addr.sa_family = sess->af;
	    addr->ipv4.sin_port = pj_htons(sess->default_port);
	}

	sess->srv_addr = &sess->srv_addr_list[0];
	set_state(sess, PJ_TURN_STATE_RESOLVED);
    }

on_return:
    pj_lock_release(sess->lock);
    return status;
}


/**
 * Set credential to be used by the session.
 */
PJ_DEF(pj_status_t) pj_turn_session_set_credential(pj_turn_session *sess,
					     const pj_stun_auth_cred *cred)
{
    PJ_ASSERT_RETURN(sess && cred, PJ_EINVAL);
    PJ_ASSERT_RETURN(sess->stun, PJ_EINVALIDOP);

    pj_lock_acquire(sess->lock);

    pj_stun_session_set_credential(sess->stun, PJ_STUN_AUTH_LONG_TERM, cred);

    pj_lock_release(sess->lock);

    return PJ_SUCCESS;
}


/**
 * Create TURN allocation.
 */
PJ_DEF(pj_status_t) pj_turn_session_alloc(pj_turn_session *sess,
					  const pj_turn_alloc_param *param)
{
    pj_stun_tx_data *tdata;
    pj_bool_t retransmit;
    pj_status_t status;

    PJ_ASSERT_RETURN(sess, PJ_EINVAL);
    PJ_ASSERT_RETURN(sess->state>PJ_TURN_STATE_NULL && 
		     sess->state<=PJ_TURN_STATE_RESOLVED, 
		     PJ_EINVALIDOP);

    pj_lock_acquire(sess->lock);

    if (sess->state < PJ_TURN_STATE_RESOLVED) {
	if (param && param != &sess->alloc_param) 
	    pj_turn_alloc_param_copy(sess->pool, &sess->alloc_param, param);
	sess->pending_alloc = PJ_TRUE;

	PJ_LOG(4,(sess->obj_name, "Pending ALLOCATE in state %s",
		  state_names[sess->state]));

	pj_lock_release(sess->lock);
	return PJ_SUCCESS;

    }

    /* Ready to allocate */
    pj_assert(sess->state == PJ_TURN_STATE_RESOLVED);
    
    /* Create a bare request */
    status = pj_stun_session_create_req(sess->stun, PJ_STUN_ALLOCATE_REQUEST,
					PJ_STUN_MAGIC, NULL, &tdata);
    if (status != PJ_SUCCESS) {
	pj_lock_release(sess->lock);
	return status;
    }

    /* MUST include REQUESTED-TRANSPORT attribute */
    pj_stun_msg_add_uint_attr(tdata->pool, tdata->msg,
			      PJ_STUN_ATTR_REQ_TRANSPORT, 
			      PJ_STUN_SET_RT_PROTO(PJ_TURN_TP_UDP));

    /* Include BANDWIDTH if requested */
    if (sess->alloc_param.bandwidth > 0) {
	pj_stun_msg_add_uint_attr(tdata->pool, tdata->msg,
				  PJ_STUN_ATTR_BANDWIDTH,
				  sess->alloc_param.bandwidth);
    }

    /* Include LIFETIME if requested */
    if (sess->alloc_param.lifetime > 0) {
	pj_stun_msg_add_uint_attr(tdata->pool, tdata->msg,
				  PJ_STUN_ATTR_LIFETIME,
				  sess->alloc_param.lifetime);
    }

    /* Server address must be set */
    pj_assert(sess->srv_addr != NULL);

    /* Send request */
    set_state(sess, PJ_TURN_STATE_ALLOCATING);
    retransmit = (sess->conn_type == PJ_TURN_TP_UDP);
    status = pj_stun_session_send_msg(sess->stun, NULL, PJ_FALSE, 
				      retransmit, sess->srv_addr,
				      pj_sockaddr_get_len(sess->srv_addr), 
				      tdata);
    if (status != PJ_SUCCESS) {
	/* Set state back to RESOLVED. We don't want to destroy session now,
	 * let the application do it if it wants to.
	 */
	set_state(sess, PJ_TURN_STATE_RESOLVED);
    }

    pj_lock_release(sess->lock);
    return status;
}


/*
 * Send REFRESH
 */
static void send_refresh(pj_turn_session *sess, int lifetime)
{
    pj_stun_tx_data *tdata;
    pj_status_t status;

    PJ_ASSERT_ON_FAIL(sess->state==PJ_TURN_STATE_READY, return);

    /* Create a bare REFRESH request */
    status = pj_stun_session_create_req(sess->stun, PJ_STUN_REFRESH_REQUEST,
					PJ_STUN_MAGIC, NULL, &tdata);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Add LIFETIME */
    if (lifetime >= 0) {
	pj_stun_msg_add_uint_attr(tdata->pool, tdata->msg,
				  PJ_STUN_ATTR_LIFETIME, lifetime);
    }

    /* Send request */
    if (lifetime == 0) {
	set_state(sess, PJ_TURN_STATE_DEALLOCATING);
    }

    status = pj_stun_session_send_msg(sess->stun, NULL, PJ_FALSE, 
				      (sess->conn_type==PJ_TURN_TP_UDP),
				      sess->srv_addr,
				      pj_sockaddr_get_len(sess->srv_addr), 
				      tdata);
    if (status != PJ_SUCCESS)
	goto on_error;

    return;

on_error:
    if (lifetime == 0) {
	set_state(sess, PJ_TURN_STATE_DEALLOCATED);
	sess_shutdown(sess, status);
    }
}


/**
 * Relay data to the specified peer through the session.
 */
PJ_DEF(pj_status_t) pj_turn_session_sendto( pj_turn_session *sess,
					    const pj_uint8_t *pkt,
					    unsigned pkt_len,
					    const pj_sockaddr_t *addr,
					    unsigned addr_len)
{
    struct peer *peer;
    pj_status_t status;

    PJ_ASSERT_RETURN(sess && pkt && pkt_len && addr && addr_len, 
		     PJ_EINVAL);

    /* Return error if we're not ready */
    if (sess->state != PJ_TURN_STATE_READY) {
	return PJ_EIGNORED;
    }

    /* Lock session now */
    pj_lock_acquire(sess->lock);

    /* Lookup peer to see whether we've assigned a channel number
     * to this peer.
     */
    peer = lookup_peer_by_addr(sess, addr, addr_len, PJ_TRUE, PJ_FALSE);
    pj_assert(peer != NULL);

    if (peer->ch_id != PJ_TURN_INVALID_CHANNEL && peer->bound) {
	/* Peer is assigned Channel number, we can use ChannelData */
	pj_turn_channel_data *cd = (pj_turn_channel_data*)sess->tx_pkt;
	
	pj_assert(sizeof(*cd)==4);

	if (pkt_len > sizeof(sess->tx_pkt)-sizeof(*cd)) {
	    status = PJ_ETOOBIG;
	    goto on_return;
	}

	cd->ch_number = pj_htons((pj_uint16_t)peer->ch_id);
	cd->length = pj_htons((pj_uint16_t)pkt_len);
	pj_memcpy(cd+1, pkt, pkt_len);

	pj_assert(sess->srv_addr != NULL);

	status = sess->cb.on_send_pkt(sess, sess->tx_pkt, pkt_len+sizeof(*cd),
				      sess->srv_addr,
				      pj_sockaddr_get_len(sess->srv_addr));

    } else {
	/* Peer has not been assigned Channel number, must use Send
	 * Indication.
	 */
	pj_stun_sockaddr_attr peer_attr;
	pj_stun_binary_attr data_attr;
	pj_stun_msg send_ind;
	pj_size_t send_ind_len;

	/* Increment counter */
	++sess->send_ind_tsx_id[2];

	/* Create blank SEND-INDICATION */
	status = pj_stun_msg_init(&send_ind, PJ_STUN_SEND_INDICATION,
				  PJ_STUN_MAGIC, 
				  (const pj_uint8_t*)sess->send_ind_tsx_id);
	if (status != PJ_SUCCESS)
	    goto on_return;

	/* Add PEER-ADDRESS */
	pj_stun_sockaddr_attr_init(&peer_attr, PJ_STUN_ATTR_PEER_ADDR,
				   PJ_TRUE, addr, addr_len);
	pj_stun_msg_add_attr(&send_ind, (pj_stun_attr_hdr*)&peer_attr);

	/* Add DATA attribute */
	pj_stun_binary_attr_init(&data_attr, NULL, PJ_STUN_ATTR_DATA, NULL, 0);
	data_attr.data = (void*)pkt;
	data_attr.length = pkt_len;
	pj_stun_msg_add_attr(&send_ind, (pj_stun_attr_hdr*)&data_attr);

	/* Encode the message */
	status = pj_stun_msg_encode(&send_ind, sess->tx_pkt, 
				    sizeof(sess->tx_pkt), 0,
				    NULL, &send_ind_len);
	if (status != PJ_SUCCESS)
	    goto on_return;

	/* Send the Send Indication */
	status = sess->cb.on_send_pkt(sess, sess->tx_pkt, send_ind_len,
				      sess->srv_addr,
				      pj_sockaddr_get_len(sess->srv_addr));
    }

on_return:
    pj_lock_release(sess->lock);
    return status;
}


/**
 * Bind a peer address to a channel number.
 */
PJ_DEF(pj_status_t) pj_turn_session_bind_channel(pj_turn_session *sess,
						 const pj_sockaddr_t *peer_adr,
						 unsigned addr_len)
{
    struct peer *peer;
    pj_stun_tx_data *tdata;
    pj_uint16_t ch_num;
    pj_status_t status;

    PJ_ASSERT_RETURN(sess && peer_adr && addr_len, PJ_EINVAL);
    PJ_ASSERT_RETURN(sess->state == PJ_TURN_STATE_READY, PJ_EINVALIDOP);

    pj_lock_acquire(sess->lock);

    /* Create blank ChannelBind request */
    status = pj_stun_session_create_req(sess->stun, 
					PJ_STUN_CHANNEL_BIND_REQUEST,
					PJ_STUN_MAGIC, NULL, &tdata);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Lookup peer */
    peer = lookup_peer_by_addr(sess, peer_adr, addr_len, PJ_TRUE, PJ_FALSE);
    pj_assert(peer);

    if (peer->ch_id != PJ_TURN_INVALID_CHANNEL) {
	/* Channel is already bound. This is a refresh request. */
	ch_num = peer->ch_id;
    } else {
	PJ_ASSERT_ON_FAIL(sess->next_ch <= PJ_TURN_CHANNEL_MAX, 
			    {status=PJ_ETOOMANY; goto on_return;});
	peer->ch_id = ch_num = sess->next_ch++;
    }

    /* Add CHANNEL-NUMBER attribute */
    pj_stun_msg_add_uint_attr(tdata->pool, tdata->msg,
			      PJ_STUN_ATTR_CHANNEL_NUMBER,
			      PJ_STUN_SET_CH_NB(ch_num));

    /* Add PEER-ADDRESS attribute */
    pj_stun_msg_add_sockaddr_attr(tdata->pool, tdata->msg,
				  PJ_STUN_ATTR_PEER_ADDR, PJ_TRUE,
				  peer_adr, addr_len);

    /* Send the request, associate peer data structure with tdata 
     * for future reference when we receive the ChannelBind response.
     */
    status = pj_stun_session_send_msg(sess->stun, peer, PJ_FALSE, 
				      (sess->conn_type==PJ_TURN_TP_UDP),
				      sess->srv_addr,
				      pj_sockaddr_get_len(sess->srv_addr),
				      tdata);

on_return:
    pj_lock_release(sess->lock);
    return status;
}


/**
 * Notify TURN client session upon receiving a packet from server.
 * The packet maybe a STUN packet or ChannelData packet.
 */
PJ_DEF(pj_status_t) pj_turn_session_on_rx_pkt(pj_turn_session *sess,
					      void *pkt,
					      unsigned pkt_len)
{
    pj_bool_t is_stun;
    pj_status_t status;
    pj_bool_t is_datagram;

    /* Packet could be ChannelData or STUN message (response or
     * indication).
     */

    /* Start locking the session */
    pj_lock_acquire(sess->lock);

    is_datagram = (sess->conn_type==PJ_TURN_TP_UDP);

    /* Quickly check if this is STUN message */
    is_stun = ((((pj_uint8_t*)pkt)[0] & 0xC0) == 0);

    if (is_stun) {
	/* This looks like STUN, give it to the STUN session */
	unsigned options;

	options = PJ_STUN_CHECK_PACKET | PJ_STUN_NO_FINGERPRINT_CHECK;
	if (is_datagram)
	    options |= PJ_STUN_IS_DATAGRAM;
	status=pj_stun_session_on_rx_pkt(sess->stun, pkt, pkt_len,
					 options, NULL, NULL,
					 sess->srv_addr,
					 pj_sockaddr_get_len(sess->srv_addr));

    } else if (sess->cb.on_rx_data) {

	/* This must be ChannelData. Only makes sense when on_rx_data() is
	 * implemented by application.
	 */
	pj_turn_channel_data cd;
	struct peer *peer;

	PJ_ASSERT_RETURN(pkt_len >= 4, PJ_ETOOSMALL);

	/* Lookup peer */
	pj_memcpy(&cd, pkt, sizeof(pj_turn_channel_data));
	cd.ch_number = pj_ntohs(cd.ch_number);
	cd.length = pj_ntohs(cd.length);
	peer = lookup_peer_by_chnum(sess, cd.ch_number);
	if (!peer || !peer->bound) {
	    status = PJ_ENOTFOUND;
	    goto on_return;
	}

	/* Check that size is correct, for UDP */
	if (pkt_len < cd.length+sizeof(cd)) {
	    status = PJ_ETOOSMALL;
	    goto on_return;
	}

	/* Notify application */
	(*sess->cb.on_rx_data)(sess, ((pj_uint8_t*)pkt)+sizeof(cd), 
			       cd.length, &peer->addr,
			       pj_sockaddr_get_len(&peer->addr));

	status = PJ_SUCCESS;

    } else {
	/* This is ChannelData and application doesn't implement
	 * on_rx_data() callback. Just ignore the packet.
	 */
	status = PJ_SUCCESS;
    }

on_return:
    pj_lock_release(sess->lock);
    return status;
}


/*
 * This is a callback from STUN session to send outgoing packet.
 */
static pj_status_t stun_on_send_msg(pj_stun_session *stun,
				    void *token,
				    const void *pkt,
				    pj_size_t pkt_size,
				    const pj_sockaddr_t *dst_addr,
				    unsigned addr_len)
{
    pj_turn_session *sess;

    PJ_UNUSED_ARG(token);

    sess = (pj_turn_session*) pj_stun_session_get_user_data(stun);
    return (*sess->cb.on_send_pkt)(sess, (const pj_uint8_t*)pkt, pkt_size, 
				   dst_addr, addr_len);
}


/*
 * Handle failed ALLOCATE or REFRESH request. This may switch to alternate
 * server if we have one.
 */
static void on_session_fail( pj_turn_session *sess, 
			     enum pj_stun_method_e method,
			     pj_status_t status,
			     const pj_str_t *reason)
{
    sess->last_status = status;

    do {
	pj_str_t reason1;
	char err_msg[PJ_ERR_MSG_SIZE];

	if (reason == NULL) {
	    pj_strerror(status, err_msg, sizeof(err_msg));
	    reason1 = pj_str(err_msg);
	    reason = &reason1;
	}

	PJ_LOG(4,(sess->obj_name, "%s error: %.*s",
		  pj_stun_get_method_name(method),
		  (int)reason->slen, reason->ptr));

	/* If this is ALLOCATE response and we don't have more server 
	 * addresses to try, notify application and destroy the TURN
	 * session.
	 */
	if (method==PJ_STUN_ALLOCATE_METHOD &&
	    sess->srv_addr == &sess->srv_addr_list[sess->srv_addr_cnt-1]) 
	{

	    set_state(sess, PJ_TURN_STATE_DEALLOCATED);
	    sess_shutdown(sess, status);
	    return;
	}

	/* Otherwise if this is REFRESH response, notify application
	 * that session has been TERMINATED.
	 */
	if (method==PJ_STUN_REFRESH_METHOD) {
	    set_state(sess, PJ_TURN_STATE_DEALLOCATED);
	    sess_shutdown(sess, status);
	    return;
	}

	/* Try next server */
	++sess->srv_addr;
	reason = NULL;

	PJ_LOG(4,(sess->obj_name, "Trying next server"));

	status = pj_turn_session_alloc(sess, NULL);

    } while (status != PJ_SUCCESS);
}


/*
 * Handle successful response to ALLOCATE or REFRESH request.
 */
static void on_allocate_success(pj_turn_session *sess, 
				enum pj_stun_method_e method,
				const pj_stun_msg *msg)
{
    const pj_stun_lifetime_attr *lf_attr;
    const pj_stun_relay_addr_attr *raddr_attr;
    const pj_stun_sockaddr_attr *mapped_attr;
    pj_str_t s;
    pj_time_val timeout;

    /* Must have LIFETIME attribute */
    lf_attr = (const pj_stun_lifetime_attr*)
	      pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_LIFETIME, 0);
    if (lf_attr == NULL) {
	on_session_fail(sess, method, PJNATH_EINSTUNMSG,
			pj_cstr(&s, "Error: Missing LIFETIME attribute"));
	return;
    }

    /* If LIFETIME is zero, this is a deallocation */
    if (lf_attr->value == 0) {
	set_state(sess, PJ_TURN_STATE_DEALLOCATED);
	sess_shutdown(sess, PJ_SUCCESS);
	return;
    }

    /* Update lifetime and keep-alive interval */
    sess->lifetime = lf_attr->value;
    pj_gettimeofday(&sess->expiry);

    if (sess->lifetime < PJ_TURN_KEEP_ALIVE_SEC) {
	if (sess->lifetime <= 2) {
	    on_session_fail(sess, method, PJ_ETOOSMALL,
			     pj_cstr(&s, "Error: LIFETIME too small"));
	    return;
	}
	sess->ka_interval = sess->lifetime - 2;
	sess->expiry.sec += (sess->ka_interval-1);
    } else {
	int timeout;

	sess->ka_interval = PJ_TURN_KEEP_ALIVE_SEC;

	timeout = sess->lifetime - PJ_TURN_REFRESH_SEC_BEFORE;
	if (timeout < sess->ka_interval)
	    timeout = sess->ka_interval - 1;

	sess->expiry.sec += timeout;
    }

    /* Check that relayed transport address contains correct
     * address family.
     */
    raddr_attr = (const pj_stun_relay_addr_attr*)
		 pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_RELAY_ADDR, 0);
    if (raddr_attr == NULL && method==PJ_STUN_ALLOCATE_METHOD) {
	on_session_fail(sess, method, PJNATH_EINSTUNMSG,
		        pj_cstr(&s, "Error: Received ALLOCATE without "
				    "RELAY-ADDRESS attribute"));
	return;
    }
    if (raddr_attr && raddr_attr->sockaddr.addr.sa_family != sess->af) {
	on_session_fail(sess, method, PJNATH_EINSTUNMSG,
			pj_cstr(&s, "Error: RELAY-ADDRESS with non IPv4"
				    " address family is not supported "
				    "for now"));
	return;
    }
    if (raddr_attr && !pj_sockaddr_has_addr(&raddr_attr->sockaddr)) {
	on_session_fail(sess, method, PJNATH_EINSTUNMSG,
			pj_cstr(&s, "Error: Invalid IP address in "
				    "RELAY-ADDRESS attribute"));
	return;
    }
    
    /* Save relayed address */
    if (raddr_attr) {
	/* If we already have relay address, check if the relay address 
	 * in the response matches our relay address.
	 */
	if (pj_sockaddr_has_addr(&sess->relay_addr)) {
	    if (pj_sockaddr_cmp(&sess->relay_addr, &raddr_attr->sockaddr)) {
		on_session_fail(sess, method, PJNATH_EINSTUNMSG,
				pj_cstr(&s, "Error: different RELAY-ADDRESS is"
					    "returned by server"));
		return;
	    }
	} else {
	    /* Otherwise save the relayed address */
	    pj_memcpy(&sess->relay_addr, &raddr_attr->sockaddr, 
		      sizeof(pj_sockaddr));
	}
    }

    /* Get mapped address */
    mapped_attr = (const pj_stun_sockaddr_attr*)
		  pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_XOR_MAPPED_ADDR, 0);
    if (mapped_attr) {
	pj_memcpy(&sess->mapped_addr, &mapped_attr->sockaddr,
		  sizeof(mapped_attr->sockaddr));
    }

    /* Success */

    /* Cancel existing keep-alive timer, if any */
    pj_assert(sess->timer.id != TIMER_DESTROY);

    if (sess->timer.id != TIMER_NONE) {
	pj_timer_heap_cancel(sess->timer_heap, &sess->timer);
	sess->timer.id = TIMER_NONE;
    }

    /* Start keep-alive timer once allocation succeeds */
    timeout.sec = sess->ka_interval;
    timeout.msec = 0;

    sess->timer.id = TIMER_KEEP_ALIVE;
    pj_timer_heap_schedule(sess->timer_heap, &sess->timer, &timeout);

    set_state(sess, PJ_TURN_STATE_READY);
}

/*
 * Notification from STUN session on request completion.
 */
static void stun_on_request_complete(pj_stun_session *stun,
				     pj_status_t status,
				     void *token,
				     pj_stun_tx_data *tdata,
				     const pj_stun_msg *response,
				     const pj_sockaddr_t *src_addr,
				     unsigned src_addr_len)
{
    pj_turn_session *sess;
    enum pj_stun_method_e method = (enum pj_stun_method_e) 
			      	   PJ_STUN_GET_METHOD(tdata->msg->hdr.type);

    PJ_UNUSED_ARG(src_addr);
    PJ_UNUSED_ARG(src_addr_len);

    sess = (pj_turn_session*)pj_stun_session_get_user_data(stun);

    if (method == PJ_STUN_ALLOCATE_METHOD) {

	/* Destroy if we have pending destroy request */
	if (sess->pending_destroy) {
	    if (status == PJ_SUCCESS)
		sess->state = PJ_TURN_STATE_READY;
	    else
		sess->state = PJ_TURN_STATE_DEALLOCATED;
	    sess_shutdown(sess, PJ_SUCCESS);
	    return;
	}

	/* Handle ALLOCATE response */
	if (status==PJ_SUCCESS && 
	    PJ_STUN_IS_SUCCESS_RESPONSE(response->hdr.type)) 
	{

	    /* Successful Allocate response */
	    on_allocate_success(sess, method, response);

	} else {
	    /* Failed Allocate request */
	    const pj_str_t *err_msg = NULL;

	    if (status == PJ_SUCCESS) {
		const pj_stun_errcode_attr *err_attr;
		err_attr = (const pj_stun_errcode_attr*)
			   pj_stun_msg_find_attr(response,
						 PJ_STUN_ATTR_ERROR_CODE, 0);
		if (err_attr) {
		    status = PJ_STATUS_FROM_STUN_CODE(err_attr->err_code);
		    err_msg = &err_attr->reason;
		} else {
		    status = PJNATH_EINSTUNMSG;
		}
	    }

	    on_session_fail(sess, method, status, err_msg);
	}

    } else if (method == PJ_STUN_REFRESH_METHOD) {
	/* Handle Refresh response */
	if (status==PJ_SUCCESS && 
	    PJ_STUN_IS_SUCCESS_RESPONSE(response->hdr.type)) 
	{
	    /* Success, schedule next refresh. */
	    on_allocate_success(sess, method, response);

	} else {
	    /* Failed Refresh request */
	    const pj_str_t *err_msg = NULL;

	    if (status == PJ_SUCCESS) {
		const pj_stun_errcode_attr *err_attr;
		err_attr = (const pj_stun_errcode_attr*)
			   pj_stun_msg_find_attr(response,
						 PJ_STUN_ATTR_ERROR_CODE, 0);
		if (err_attr) {
		    status = PJ_STATUS_FROM_STUN_CODE(err_attr->err_code);
		    err_msg = &err_attr->reason;
		} else {
		    status = PJNATH_EINSTUNMSG;
		}
	    }

	    /* Notify and destroy */
	    on_session_fail(sess, method, status, err_msg);
	}

    } else if (method == PJ_STUN_CHANNEL_BIND_METHOD) {
	/* Handle ChannelBind response */
	if (status==PJ_SUCCESS && 
	    PJ_STUN_IS_SUCCESS_RESPONSE(response->hdr.type)) 
	{
	    /* Successful ChannelBind response */
	    struct peer *peer = (struct peer*)token;

	    pj_assert(peer->ch_id != PJ_TURN_INVALID_CHANNEL);
	    peer->bound = PJ_TRUE;

	    /* Update hash table */
	    lookup_peer_by_addr(sess, &peer->addr,
				pj_sockaddr_get_len(&peer->addr),
				PJ_TRUE, PJ_TRUE);

	} else {
	    /* Failed ChannelBind response */
	    pj_str_t err_msg = {"", 0};

	    if (status == PJ_SUCCESS) {
		const pj_stun_errcode_attr *err_attr;
		err_attr = (const pj_stun_errcode_attr*)
			   pj_stun_msg_find_attr(response,
						 PJ_STUN_ATTR_ERROR_CODE, 0);
		if (err_attr) {
		    status = PJ_STATUS_FROM_STUN_CODE(err_attr->err_code);
		    err_msg = err_attr->reason;
		} else {
		    status = PJNATH_EINSTUNMSG;
		}
	    }

	    PJ_LOG(4,(sess->obj_name, "ChannelBind failed: %.*s",
		      (int)err_msg.slen, err_msg.ptr));
	}

    } else {
	PJ_LOG(4,(sess->obj_name, "Unexpected STUN %s response",
		  pj_stun_get_method_name(response->hdr.type)));
    }
}


/*
 * Notification from STUN session on incoming STUN Indication
 * message.
 */
static pj_status_t stun_on_rx_indication(pj_stun_session *stun,
					 const pj_uint8_t *pkt,
					 unsigned pkt_len,
					 const pj_stun_msg *msg,
					 void *token,
					 const pj_sockaddr_t *src_addr,
					 unsigned src_addr_len)
{
    pj_turn_session *sess;
    pj_stun_peer_addr_attr *peer_attr;
    pj_stun_data_attr *data_attr;

    PJ_UNUSED_ARG(token);
    PJ_UNUSED_ARG(pkt);
    PJ_UNUSED_ARG(pkt_len);
    PJ_UNUSED_ARG(src_addr);
    PJ_UNUSED_ARG(src_addr_len);

    sess = (pj_turn_session*)pj_stun_session_get_user_data(stun);

    /* Expecting Data Indication only */
    if (msg->hdr.type != PJ_STUN_DATA_INDICATION) {
	PJ_LOG(4,(sess->obj_name, "Unexpected STUN %s indication",
		  pj_stun_get_method_name(msg->hdr.type)));
	return PJ_EINVALIDOP;
    }

    /* Get PEER-ADDRESS attribute */
    peer_attr = (pj_stun_peer_addr_attr*)
		pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_PEER_ADDR, 0);

    /* Get DATA attribute */
    data_attr = (pj_stun_data_attr*)
		pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_DATA, 0);

    /* Must have both PEER-ADDRESS and DATA attributes */
    if (!peer_attr || !data_attr) {
	PJ_LOG(4,(sess->obj_name, 
		  "Received Data indication with missing attributes"));
	return PJ_EINVALIDOP;
    }

    /* Notify application */
    if (sess->cb.on_rx_data) {
	(*sess->cb.on_rx_data)(sess, data_attr->data, data_attr->length, 
			       &peer_attr->sockaddr,
			       pj_sockaddr_get_len(&peer_attr->sockaddr));
    }

    return PJ_SUCCESS;
}


/*
 * Notification on completion of DNS SRV resolution.
 */
static void dns_srv_resolver_cb(void *user_data,
				pj_status_t status,
				const pj_dns_srv_record *rec)
{
    pj_turn_session *sess = (pj_turn_session*) user_data;
    unsigned i, cnt, tot_cnt;

    /* Clear async resolver */
    sess->dns_async = NULL;

    /* Check failure */
    if (status != PJ_SUCCESS) {
	sess_shutdown(sess, status);
	return;
    }

    /* Calculate total number of server entries in the response */
    tot_cnt = 0;
    for (i=0; i<rec->count; ++i) {
	tot_cnt += rec->entry[i].server.addr_count;
    }

    if (tot_cnt > PJ_TURN_MAX_DNS_SRV_CNT)
	tot_cnt = PJ_TURN_MAX_DNS_SRV_CNT;

    /* Allocate server entries */
    sess->srv_addr_list = (pj_sockaddr*)
		           pj_pool_calloc(sess->pool, tot_cnt, 
					  sizeof(pj_sockaddr));

    /* Copy results to server entries */
    for (i=0, cnt=0; i<rec->count && cnt<PJ_TURN_MAX_DNS_SRV_CNT; ++i) {
	unsigned j;

	for (j=0; j<rec->entry[i].server.addr_count && 
		  cnt<PJ_TURN_MAX_DNS_SRV_CNT; ++j) 
	{
	    pj_sockaddr_in *addr = &sess->srv_addr_list[cnt].ipv4;

	    addr->sin_family = sess->af;
	    addr->sin_port = pj_htons(rec->entry[i].port);
	    addr->sin_addr.s_addr = rec->entry[i].server.addr[j].s_addr;

	    ++cnt;
	}
    }
    sess->srv_addr_cnt = (pj_uint16_t)cnt;

    /* Set current server */
    sess->srv_addr = &sess->srv_addr_list[0];

    /* Set state to PJ_TURN_STATE_RESOLVED */
    set_state(sess, PJ_TURN_STATE_RESOLVED);

    /* Run pending allocation */
    if (sess->pending_alloc) {
	pj_turn_session_alloc(sess, NULL);
    }
}


/*
 * Lookup peer descriptor from its address.
 */
static struct peer *lookup_peer_by_addr(pj_turn_session *sess,
					const pj_sockaddr_t *addr,
					unsigned addr_len,
					pj_bool_t update,
					pj_bool_t bind_channel)
{
    unsigned hval = 0;
    struct peer *peer;

    peer = (struct peer*) pj_hash_get(sess->peer_table, addr, addr_len, &hval);
    if (peer == NULL && update) {
	peer = PJ_POOL_ZALLOC_T(sess->pool, struct peer);
	peer->ch_id = PJ_TURN_INVALID_CHANNEL;
	pj_memcpy(&peer->addr, addr, addr_len);

	/* Register by peer address */
	pj_hash_set(sess->pool, sess->peer_table, &peer->addr, addr_len,
		    hval, peer);
    }

    if (peer && update) {
	pj_gettimeofday(&peer->expiry);
	if (peer->bound) {
	    peer->expiry.sec += PJ_TURN_CHANNEL_TIMEOUT - 10;
	} else {
	    peer->expiry.sec += PJ_TURN_PERM_TIMEOUT - 10;
	}

	if (bind_channel) {
	    pj_uint32_t hval = 0;
	    /* Register by channel number */
	    pj_assert(peer->ch_id != PJ_TURN_INVALID_CHANNEL && peer->bound);

	    if (pj_hash_get(sess->peer_table, &peer->ch_id, 
			    sizeof(peer->ch_id), &hval)==0) {
		pj_hash_set(sess->pool, sess->peer_table, &peer->ch_id,
			    sizeof(peer->ch_id), hval, peer);
	    }
	}
    }

    return peer;
}


/*
 * Lookup peer descriptor from its channel number.
 */
static struct peer *lookup_peer_by_chnum(pj_turn_session *sess,
					 pj_uint16_t chnum)
{
    return (struct peer*) pj_hash_get(sess->peer_table, &chnum, 
				      sizeof(chnum), NULL);
}


/*
 * Timer event.
 */
static void on_timer_event(pj_timer_heap_t *th, pj_timer_entry *e)
{
    pj_turn_session *sess = (pj_turn_session*)e->user_data;
    enum timer_id_t eid;

    PJ_UNUSED_ARG(th);

    pj_lock_acquire(sess->lock);

    eid = (enum timer_id_t) e->id;
    e->id = TIMER_NONE;
    
    if (eid == TIMER_KEEP_ALIVE) {
	pj_time_val now;
	pj_hash_iterator_t itbuf, *it;
	pj_bool_t resched = PJ_TRUE;
	pj_bool_t pkt_sent = PJ_FALSE;

	pj_gettimeofday(&now);

	/* Refresh allocation if it's time to do so */
	if (PJ_TIME_VAL_LTE(sess->expiry, now)) {
	    int lifetime = sess->alloc_param.lifetime;

	    if (lifetime == 0)
		lifetime = -1;

	    send_refresh(sess, lifetime);
	    resched = PJ_FALSE;
	    pkt_sent = PJ_TRUE;
	}

	/* Scan hash table to refresh bound channels */
	it = pj_hash_first(sess->peer_table, &itbuf);
	while (it) {
	    struct peer *peer = (struct peer*) 
				pj_hash_this(sess->peer_table, it);
	    if (peer->bound && PJ_TIME_VAL_LTE(peer->expiry, now)) {

		/* Send ChannelBind to refresh channel binding and 
		 * permission.
		 */
		pj_turn_session_bind_channel(sess, &peer->addr,
					     pj_sockaddr_get_len(&peer->addr));
		pkt_sent = PJ_TRUE;
	    }

	    it = pj_hash_next(sess->peer_table, it);
	}

	/* If no packet is sent, send a blank Send indication to
	 * refresh local NAT.
	 */
	if (!pkt_sent && sess->alloc_param.ka_interval > 0) {
	    pj_stun_tx_data *tdata;
	    pj_status_t rc;

	    /* Create blank SEND-INDICATION */
	    rc = pj_stun_session_create_ind(sess->stun, 
					    PJ_STUN_SEND_INDICATION, &tdata);
	    if (rc == PJ_SUCCESS) {
		/* Add DATA attribute with zero length */
		pj_stun_msg_add_binary_attr(tdata->pool, tdata->msg,
					    PJ_STUN_ATTR_DATA, NULL, 0);

		/* Send the indication */
		pj_stun_session_send_msg(sess->stun, NULL, PJ_FALSE, 
					 PJ_FALSE, sess->srv_addr,
					 pj_sockaddr_get_len(sess->srv_addr),
					 tdata);
	    }
	}

	/* Reshcedule timer */
	if (resched) {
	    pj_time_val delay;

	    delay.sec = sess->ka_interval;
	    delay.msec = 0;

	    sess->timer.id = TIMER_KEEP_ALIVE;
	    pj_timer_heap_schedule(sess->timer_heap, &sess->timer, &delay);
	}

	pj_lock_release(sess->lock);

    } else if (eid == TIMER_DESTROY) {
	/* Time to destroy */
	pj_lock_release(sess->lock);
	do_destroy(sess);
    } else {
	pj_assert(!"Unknown timer event");
	pj_lock_release(sess->lock);
    }
}

