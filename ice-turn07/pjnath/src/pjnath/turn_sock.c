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
#include <pjnath/turn_sock.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/pool.h>
#include <pj/ioqueue.h>

enum
{
    TIMER_NONE,
    TIMER_DESTROY
};

#define INIT	0x1FFFFFFF

struct pj_turn_sock
{
    pj_pool_t		*pool;
    const char		*obj_name;
    pj_turn_session	*sess;
    pj_turn_sock_cb	 cb;
    void		*user_data;

    pj_lock_t		*lock;

    pj_turn_alloc_param	 alloc_param;
    pj_stun_config	 cfg;

    pj_bool_t		 destroy_request;
    pj_timer_entry	 timer;

    int			 af;
    pj_turn_tp_type	 conn_type;
    pj_sock_t		 sock;
    pj_ioqueue_key_t	*key;
    pj_ioqueue_op_key_t	 read_key;
    pj_ioqueue_op_key_t	 send_key;
    pj_uint8_t		 pkt[PJ_TURN_MAX_PKT_LEN];
};


/*
 * Callback prototypes.
 */
static pj_status_t turn_on_send_pkt(pj_turn_session *sess,
				    const pj_uint8_t *pkt,
				    unsigned pkt_len,
				    const pj_sockaddr_t *dst_addr,
				    unsigned dst_addr_len);
static void turn_on_channel_bound(pj_turn_session *sess,
				  const pj_sockaddr_t *peer_addr,
				  unsigned addr_len,
				  unsigned ch_num);
static void turn_on_rx_data(pj_turn_session *sess,
			    void *pkt,
			    unsigned pkt_len,
			    const pj_sockaddr_t *peer_addr,
			    unsigned addr_len);
static void turn_on_state(pj_turn_session *sess, 
			  pj_turn_state_t old_state,
			  pj_turn_state_t new_state);
static void on_read_complete(pj_ioqueue_key_t *key, 
                             pj_ioqueue_op_key_t *op_key, 
                             pj_ssize_t bytes_read);
static void on_connect_complete(pj_ioqueue_key_t *key, 
                                pj_status_t status);


static void destroy(pj_turn_sock *turn_sock);
static void timer_cb(pj_timer_heap_t *th, pj_timer_entry *e);


/*
 * Create.
 */
PJ_DEF(pj_status_t) pj_turn_sock_create(pj_stun_config *cfg,
					int af,
					pj_turn_tp_type conn_type,
					const pj_turn_sock_cb *cb,
					unsigned options,
					void *user_data,
					pj_turn_sock **p_turn_sock)
{
    pj_turn_sock *turn_sock;
    pj_turn_session_cb sess_cb;
    pj_pool_t *pool;
    const char *name_tmpl;
    pj_status_t status;

    PJ_ASSERT_RETURN(cfg && p_turn_sock, PJ_EINVAL);
    PJ_ASSERT_RETURN(af==pj_AF_INET() || af==pj_AF_INET6(), PJ_EINVAL);
    PJ_ASSERT_RETURN(options==0, PJ_EINVAL);

    switch (conn_type) {
    case PJ_TURN_TP_UDP:
	name_tmpl = "udprel%p";
	break;
    case PJ_TURN_TP_TCP:
	name_tmpl = "tcprel%p";
	break;
    default:
	PJ_ASSERT_RETURN(!"Invalid TURN conn_type", PJ_EINVAL);
	name_tmpl = "tcprel%p";
	break;
    }

    /* Create and init basic data structure */
    pool = pj_pool_create(cfg->pf, name_tmpl, 1000, 1000, NULL);
    turn_sock = PJ_POOL_ZALLOC_T(pool, pj_turn_sock);
    turn_sock->pool = pool;
    turn_sock->obj_name = pool->obj_name;
    turn_sock->user_data = user_data;
    turn_sock->af = af;
    turn_sock->conn_type = conn_type;

    /* Copy STUN config (this contains ioqueue, timer heap, etc.) */
    pj_memcpy(&turn_sock->cfg, cfg, sizeof(*cfg));

    /* Set callback */
    if (cb) {
	pj_memcpy(&turn_sock->cb, cb, sizeof(*cb));
    }

    /* Create lock */
    status = pj_lock_create_recursive_mutex(pool, turn_sock->obj_name, 
					    &turn_sock->lock);
    if (status != PJ_SUCCESS) {
	destroy(turn_sock);
	return status;
    }

    /* Init timer */
    pj_timer_entry_init(&turn_sock->timer, TIMER_NONE, turn_sock, &timer_cb);

    /* Init TURN session */
    pj_bzero(&sess_cb, sizeof(sess_cb));
    sess_cb.on_send_pkt = &turn_on_send_pkt;
    sess_cb.on_channel_bound = &turn_on_channel_bound;
    sess_cb.on_rx_data = &turn_on_rx_data;
    sess_cb.on_state = &turn_on_state;
    status = pj_turn_session_create(cfg, pool->obj_name, af, conn_type,
				    &sess_cb, turn_sock, 0, &turn_sock->sess);
    if (status != PJ_SUCCESS) {
	destroy(turn_sock);
	return status;
    }

    /* Note: socket and ioqueue will be created later once the TURN server
     * has been resolved.
     */

    *p_turn_sock = turn_sock;
    return PJ_SUCCESS;
}

/*
 * Destroy.
 */
static void destroy(pj_turn_sock *turn_sock)
{
    if (turn_sock->lock) {
	pj_lock_acquire(turn_sock->lock);
    }

    if (turn_sock->sess) {
	pj_turn_session_set_user_data(turn_sock->sess, NULL);
	pj_turn_session_shutdown(turn_sock->sess);
	turn_sock->sess = NULL;
    }

    if (turn_sock->key) {
	pj_ioqueue_unregister(turn_sock->key);
	turn_sock->key = NULL;
	turn_sock->sock = 0;
    } else if (turn_sock->sock) {
	pj_sock_close(turn_sock->sock);
	turn_sock->sock = 0;
    }

    if (turn_sock->lock) {
	pj_lock_release(turn_sock->lock);
	pj_lock_destroy(turn_sock->lock);
	turn_sock->lock = NULL;
    }

    if (turn_sock->pool) {
	pj_pool_t *pool = turn_sock->pool;
	turn_sock->pool = NULL;
	pj_pool_release(pool);
    }
}


PJ_DEF(void) pj_turn_sock_destroy(pj_turn_sock *turn_sock)
{
    pj_lock_acquire(turn_sock->lock);
    turn_sock->destroy_request = PJ_TRUE;

    if (turn_sock->sess) {
	pj_turn_session_shutdown(turn_sock->sess);
	/* This will ultimately call our state callback, and when
	 * session state is DESTROYING we will schedule a timer to
	 * destroy ourselves.
	 */
	pj_lock_release(turn_sock->lock);
    } else {
	pj_lock_release(turn_sock->lock);
	destroy(turn_sock);
    }

}


/* Timer callback */
static void timer_cb(pj_timer_heap_t *th, pj_timer_entry *e)
{
    pj_turn_sock *turn_sock = (pj_turn_sock*)e->user_data;
    int eid = e->id;

    PJ_UNUSED_ARG(th);

    e->id = TIMER_NONE;

    switch (eid) {
    case TIMER_DESTROY:
	PJ_LOG(5,(turn_sock->obj_name, "Destroying TURN"));
	destroy(turn_sock);
	break;
    default:
	pj_assert(!"Invalid timer id");
	break;
    }
}


/* Display error */
static void show_err(pj_turn_sock *turn_sock, const char *title,
		     pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    if (status != PJ_SUCCESS) {
	pj_strerror(status, errmsg, sizeof(errmsg));
	PJ_LOG(4,(turn_sock->obj_name, "%s: %s", title, errmsg));
    } else {
	PJ_LOG(4,(turn_sock->obj_name, "%s", title, errmsg));
    }
}

/* On error, terminate session */
static void sess_fail(pj_turn_sock *turn_sock, const char *title,
		      pj_status_t status)
{
    show_err(turn_sock, title, status);
    pj_turn_session_destroy(turn_sock->sess);
}

/*
 * Set user data.
 */
PJ_DEF(pj_status_t) pj_turn_sock_set_user_data( pj_turn_sock *turn_sock,
					       void *user_data)
{
    turn_sock->user_data = user_data;
    return PJ_SUCCESS;
}

/*
 * Get user data.
 */
PJ_DEF(void*) pj_turn_sock_get_user_data(pj_turn_sock *turn_sock)
{
    return turn_sock->user_data;
}

/**
 * Get info.
 */
PJ_DEF(pj_status_t) pj_turn_sock_get_info(pj_turn_sock *turn_sock,
					 pj_turn_session_info *info)
{
    PJ_ASSERT_RETURN(turn_sock && info, PJ_EINVAL);

    if (turn_sock->sess) {
	return pj_turn_session_get_info(turn_sock->sess, info);
    } else {
	pj_bzero(info, sizeof(*info));
	info->state = PJ_TURN_STATE_NULL;
	return PJ_SUCCESS;
    }
}

/**
 * Lock the TURN socket. Application may need to call this function to
 * synchronize access to other objects to avoid deadlock.
 */
PJ_DEF(pj_status_t) pj_turn_sock_lock(pj_turn_sock *turn_sock)
{
    return pj_lock_acquire(turn_sock->lock);
}

/**
 * Unlock the TURN socket.
 */
PJ_DEF(pj_status_t) pj_turn_sock_unlock(pj_turn_sock *turn_sock)
{
    return pj_lock_release(turn_sock->lock);
}


/*
 * Initialize.
 */
PJ_DEF(pj_status_t) pj_turn_sock_init(pj_turn_sock *turn_sock,
				      const pj_str_t *domain,
				      int default_port,
				      pj_dns_resolver *resolver,
				      const pj_stun_auth_cred *cred,
				      const pj_turn_alloc_param *param)
{
    pj_status_t status;

    PJ_ASSERT_RETURN(turn_sock && domain, PJ_EINVAL);
    PJ_ASSERT_RETURN(turn_sock->sess, PJ_EINVALIDOP);

    /* Copy alloc param. We will call session_alloc() only after the 
     * server address has been resolved.
     */
    if (param) {
	pj_turn_alloc_param_copy(turn_sock->pool, &turn_sock->alloc_param, param);
    } else {
	pj_turn_alloc_param_default(&turn_sock->alloc_param);
    }

    /* Set credental */
    if (cred) {
	status = pj_turn_session_set_credential(turn_sock->sess, cred);
	if (status != PJ_SUCCESS) {
	    sess_fail(turn_sock, "Error setting credential", status);
	    return status;
	}
    }

    /* Resolve server */
    status = pj_turn_session_set_server(turn_sock->sess, domain, default_port,
					resolver);
    if (status != PJ_SUCCESS) {
	sess_fail(turn_sock, "Error setting TURN server", status);
	return status;
    }

    /* Done for now. The next work will be done when session state moved
     * to RESOLVED state.
     */

    return PJ_SUCCESS;
}

/*
 * Send packet.
 */ 
PJ_DEF(pj_status_t) pj_turn_sock_sendto( pj_turn_sock *turn_sock,
					const pj_uint8_t *pkt,
					unsigned pkt_len,
					const pj_sockaddr_t *addr,
					unsigned addr_len)
{
    PJ_ASSERT_RETURN(turn_sock && addr && addr_len, PJ_EINVAL);

    if (turn_sock->sess == NULL)
	return PJ_EINVALIDOP;

    return pj_turn_session_sendto(turn_sock->sess, pkt, pkt_len, 
				  addr, addr_len);
}

/*
 * Bind a peer address to a channel number.
 */
PJ_DEF(pj_status_t) pj_turn_sock_bind_channel( pj_turn_sock *turn_sock,
					      const pj_sockaddr_t *peer,
					      unsigned addr_len)
{
    PJ_ASSERT_RETURN(turn_sock && peer && addr_len, PJ_EINVAL);
    PJ_ASSERT_RETURN(turn_sock->sess != NULL, PJ_EINVALIDOP);

    return pj_turn_session_bind_channel(turn_sock->sess, peer, addr_len);
}


/*
 * Notification when outgoing TCP socket has been connected.
 */
static void on_connect_complete(pj_ioqueue_key_t *key, 
                                pj_status_t status)
{
    pj_turn_sock *turn_sock;

    turn_sock = (pj_turn_sock*) pj_ioqueue_get_user_data(key);

    if (status != PJ_SUCCESS) {
	sess_fail(turn_sock, "TCP connect() error", status);
	return;
    }

    if (turn_sock->conn_type != PJ_TURN_TP_UDP) {
	PJ_LOG(5,(turn_sock->obj_name, "TCP connected"));
    }

    /* Kick start pending read operation */
    pj_ioqueue_op_key_init(&turn_sock->read_key, sizeof(turn_sock->read_key));
    on_read_complete(turn_sock->key, &turn_sock->read_key, INIT);

    /* Init send_key */
    pj_ioqueue_op_key_init(&turn_sock->send_key, sizeof(turn_sock->send_key));

    /* Send Allocate request */
    status = pj_turn_session_alloc(turn_sock->sess, &turn_sock->alloc_param);
    if (status != PJ_SUCCESS) {
	sess_fail(turn_sock, "Error sending ALLOCATE", status);
	return;
    }
}

/*
 * Notification from ioqueue when incoming UDP packet is received.
 */
static void on_read_complete(pj_ioqueue_key_t *key, 
                             pj_ioqueue_op_key_t *op_key, 
                             pj_ssize_t bytes_read)
{
    enum { MAX_RETRY = 10 };
    pj_turn_sock *turn_sock;
    int retry = 0;
    pj_status_t status;

    turn_sock = (pj_turn_sock*) pj_ioqueue_get_user_data(key);
    pj_lock_acquire(turn_sock->lock);

    do {
	if (bytes_read == INIT) {
	    /* Special instruction to initialize pending read() */
	} else if (bytes_read > 0 && turn_sock->sess) {
	    /* Report incoming packet to TURN session */
	    pj_turn_session_on_rx_pkt(turn_sock->sess, turn_sock->pkt, 
				      bytes_read, 
				      turn_sock->conn_type == PJ_TURN_TP_UDP);
	} else if (bytes_read <= 0 && turn_sock->conn_type != PJ_TURN_TP_UDP) {
	    sess_fail(turn_sock, "TCP connection closed", -bytes_read);
	    goto on_return;
	}

	/* Read next packet */
	bytes_read = sizeof(turn_sock->pkt);
	status = pj_ioqueue_recv(turn_sock->key, op_key,
				 turn_sock->pkt, &bytes_read, 0);

	if (status != PJ_EPENDING && status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];

	    pj_strerror(status, errmsg, sizeof(errmsg));
	    sess_fail(turn_sock, "Socket recv() error", status);
	    goto on_return;
	}

    } while (status != PJ_EPENDING && status != PJ_ECANCELLED &&
	     ++retry < MAX_RETRY);

on_return:
    pj_lock_release(turn_sock->lock);
}


/*
 * Callback from TURN session to send outgoing packet.
 */
static pj_status_t turn_on_send_pkt(pj_turn_session *sess,
				    const pj_uint8_t *pkt,
				    unsigned pkt_len,
				    const pj_sockaddr_t *dst_addr,
				    unsigned dst_addr_len)
{
    pj_turn_sock *turn_sock = (pj_turn_sock*) 
			   pj_turn_session_get_user_data(sess);
    pj_ssize_t len = pkt_len;
    pj_status_t status;

    if (turn_sock == NULL) {
	/* We've been destroyed */
	pj_assert(!"We should shutdown gracefully");
	return PJ_EINVALIDOP;
    }

    PJ_UNUSED_ARG(dst_addr);
    PJ_UNUSED_ARG(dst_addr_len);

    status = pj_ioqueue_send(turn_sock->key, &turn_sock->send_key, 
			     pkt, &len, 0);
    if (status != PJ_SUCCESS && status != PJ_EPENDING) {
	show_err(turn_sock, "socket send()", status);
    }

    return status;
}


/*
 * Callback from TURN session when a channel is successfully bound.
 */
static void turn_on_channel_bound(pj_turn_session *sess,
				  const pj_sockaddr_t *peer_addr,
				  unsigned addr_len,
				  unsigned ch_num)
{
    PJ_UNUSED_ARG(sess);
    PJ_UNUSED_ARG(peer_addr);
    PJ_UNUSED_ARG(addr_len);
    PJ_UNUSED_ARG(ch_num);
}


/*
 * Callback from TURN session upon incoming data.
 */
static void turn_on_rx_data(pj_turn_session *sess,
			    void *pkt,
			    unsigned pkt_len,
			    const pj_sockaddr_t *peer_addr,
			    unsigned addr_len)
{
    pj_turn_sock *turn_sock = (pj_turn_sock*) 
			   pj_turn_session_get_user_data(sess);
    if (turn_sock == NULL) {
	/* We've been destroyed */
	return;
    }

    if (turn_sock->cb.on_rx_data) {
	(*turn_sock->cb.on_rx_data)(turn_sock, pkt, pkt_len, 
				  peer_addr, addr_len);
    }
}


/*
 * Callback from TURN session when state has changed
 */
static void turn_on_state(pj_turn_session *sess, 
			  pj_turn_state_t old_state,
			  pj_turn_state_t new_state)
{
    pj_turn_sock *turn_sock = (pj_turn_sock*) 
			   pj_turn_session_get_user_data(sess);
    pj_status_t status;

    if (turn_sock == NULL) {
	/* We've been destroyed */
	return;
    }

    if (new_state == PJ_TURN_STATE_RESOLVED) {
	/*
	 * Once server has been resolved, initiate outgoing TCP
	 * connection to the server.
	 */
	pj_turn_session_info info;
	char addrtxt[PJ_INET6_ADDRSTRLEN+8];
	int sock_type;
	pj_ioqueue_callback ioq_cb;

	/* Close existing connection, if any. This happens when
	 * we're switching to alternate TURN server when either TCP
	 * connection or ALLOCATE request failed.
	 */
	if (turn_sock->key) {
	    pj_ioqueue_unregister(turn_sock->key);
	    turn_sock->key = NULL;
	    turn_sock->sock = 0;
	} else if (turn_sock->sock) {
	    pj_sock_close(turn_sock->sock);
	    turn_sock->sock = 0;
	}

	/* Get server address from session info */
	pj_turn_session_get_info(sess, &info);

	if (turn_sock->conn_type == PJ_TURN_TP_UDP)
	    sock_type = pj_SOCK_DGRAM();
	else
	    sock_type = pj_SOCK_STREAM();

	/* Init socket */
	status = pj_sock_socket(turn_sock->af, sock_type, 0, 
				&turn_sock->sock);
	if (status != PJ_SUCCESS) {
	    pj_turn_sock_destroy(turn_sock);
	    return;
	}

	/* Register to ioqeuue */
	pj_bzero(&ioq_cb, sizeof(ioq_cb));
	ioq_cb.on_read_complete = &on_read_complete;
	ioq_cb.on_connect_complete = &on_connect_complete;
	status = pj_ioqueue_register_sock(turn_sock->pool, turn_sock->cfg.ioqueue, 
					  turn_sock->sock, turn_sock, 
					  &ioq_cb, &turn_sock->key);
	if (status != PJ_SUCCESS) {
	    pj_turn_sock_destroy(turn_sock);
	    return;
	}

	PJ_LOG(5,(turn_sock->pool->obj_name,
		  "Connecting to %s", 
		  pj_sockaddr_print(&info.server, addrtxt, 
				    sizeof(addrtxt), 3)));

	/* Initiate non-blocking connect */
	status = pj_ioqueue_connect(turn_sock->key, &info.server,
				    pj_sockaddr_get_len(&info.server));
	if (status == PJ_SUCCESS) {
	    on_connect_complete(turn_sock->key, PJ_SUCCESS);
	} else if (status != PJ_EPENDING) {
	    pj_turn_sock_destroy(turn_sock);
	    return;
	}

	/* Done for now. Subsequent work will be done in 
	 * on_connect_complete() callback.
	 */
    }

    if (turn_sock->cb.on_state) {
	(*turn_sock->cb.on_state)(turn_sock, old_state, new_state);
    }

    if (new_state >= PJ_TURN_STATE_DESTROYING && turn_sock->sess) {
	pj_time_val delay = {0, 0};

	turn_sock->sess = NULL;
	pj_turn_session_set_user_data(sess, NULL);

	if (turn_sock->timer.id) {
	    pj_timer_heap_cancel(turn_sock->cfg.timer_heap, &turn_sock->timer);
	    turn_sock->timer.id = 0;
	}

	turn_sock->timer.id = TIMER_DESTROY;
	pj_timer_heap_schedule(turn_sock->cfg.timer_heap, &turn_sock->timer, 
			       &delay);
    }
}


