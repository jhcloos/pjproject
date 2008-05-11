/* $Id$ */
/* 
 * Copyright (C)2003-2007 Benny Prijono <benny@prijono.org>
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
#include <pj/activesock.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/pool.h>
#include <pj/sock.h>
#include <pj/string.h>

#define PJ_ACTIVESOCK_MAX_LOOP	    50


enum read_type
{
    TYPE_NONE,
    TYPE_RECV,
    TYPE_RECV_FROM
};

struct read_op
{
    pj_ioqueue_op_key_t	 op_key;
    pj_uint8_t		*pkt;
    unsigned		 max_size;
    pj_size_t		 size;
    pj_sockaddr		 src_addr;
    int			 src_addr_len;
};

struct accept_op
{
    pj_ioqueue_op_key_t	 op_key;
    pj_sock_t		 new_sock;
    pj_sockaddr		 rem_addr;
    int			 rem_addr_len;
};

struct pj_activesock_t
{
    pj_ioqueue_key_t	*key;
    pj_bool_t		 stream_oriented;
    pj_ioqueue_t	*ioqueue;
    void		*user_data;
    unsigned		 async_count;
    unsigned		 max_loop;
    pj_activesock_cb	 cb;

    struct read_op	*read_op;
    pj_uint32_t		 read_flags;
    enum read_type	 read_type;

    struct accept_op	*accept_op;
};


static void ioqueue_on_read_complete(pj_ioqueue_key_t *key, 
				     pj_ioqueue_op_key_t *op_key, 
				     pj_ssize_t bytes_read);
static void ioqueue_on_write_complete(pj_ioqueue_key_t *key, 
				      pj_ioqueue_op_key_t *op_key,
				      pj_ssize_t bytes_sent);
static void ioqueue_on_accept_complete(pj_ioqueue_key_t *key, 
				       pj_ioqueue_op_key_t *op_key,
				       pj_sock_t sock, 
				       pj_status_t status);
static void ioqueue_on_connect_complete(pj_ioqueue_key_t *key, 
					pj_status_t status);


PJ_DEF(void) pj_activesock_cfg_default(pj_activesock_cfg *cfg)
{
    pj_bzero(cfg, sizeof(*cfg));
    cfg->async_cnt = 1;
    cfg->concurrency = -1;
}


PJ_DEF(pj_status_t) pj_activesock_create( pj_pool_t *pool,
					  pj_sock_t sock,
					  int sock_type,
					  const pj_activesock_cfg *opt,
					  pj_ioqueue_t *ioqueue,
					  const pj_activesock_cb *cb,
					  pj_activesock_t **p_asock)
{
    pj_activesock_t *asock;
    pj_ioqueue_callback ioq_cb;
    pj_status_t status;

    PJ_ASSERT_RETURN(pool && ioqueue && cb && p_asock, PJ_EINVAL);
    PJ_ASSERT_RETURN(sock!=0 && sock!=PJ_INVALID_SOCKET, PJ_EINVAL);
    PJ_ASSERT_RETURN(sock_type==pj_SOCK_STREAM() ||
		     sock_type==pj_SOCK_DGRAM(), PJ_EINVAL);
    PJ_ASSERT_RETURN(!opt || opt->async_cnt >= 1, PJ_EINVAL);

    asock = PJ_POOL_ZALLOC_T(pool, pj_activesock_t);
    asock->ioqueue = ioqueue;
    asock->stream_oriented = (sock_type == pj_SOCK_STREAM());
    asock->async_count = (opt? opt->async_cnt : 1);
    asock->max_loop = PJ_ACTIVESOCK_MAX_LOOP;
    pj_memcpy(&asock->cb, cb, sizeof(*cb));

    pj_bzero(&ioq_cb, sizeof(ioq_cb));
    ioq_cb.on_read_complete = &ioqueue_on_read_complete;
    ioq_cb.on_write_complete = &ioqueue_on_write_complete;
    ioq_cb.on_connect_complete = &ioqueue_on_connect_complete;
    ioq_cb.on_accept_complete = &ioqueue_on_accept_complete;

    status = pj_ioqueue_register_sock(pool, ioqueue, sock, asock, 
				      &ioq_cb, &asock->key);
    if (status != PJ_SUCCESS) {
	pj_activesock_close(asock);
	return status;
    }
    
    if (opt && opt->concurrency >= 0) {
	pj_ioqueue_set_concurrency(asock->key, opt->concurrency);
    }

    *p_asock = asock;
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pj_activesock_create_udp( pj_pool_t *pool,
					      const pj_sockaddr *addr,
					      const pj_activesock_cfg *opt,
					      pj_ioqueue_t *ioqueue,
					      const pj_activesock_cb *cb,
					      pj_activesock_t **p_asock,
					      pj_sockaddr *bound_addr)
{
    pj_sock_t sock_fd;
    pj_sockaddr default_addr;
    pj_status_t status;

    if (addr == NULL) {
	pj_sockaddr_init(pj_AF_INET(), &default_addr, NULL, 0);
	addr = &default_addr;
    }

    status = pj_sock_socket(addr->addr.sa_family, pj_SOCK_DGRAM(), 0, 
			    &sock_fd);
    if (status != PJ_SUCCESS) {
	return status;
    }

    status = pj_sock_bind(sock_fd, addr, pj_sockaddr_get_len(addr));
    if (status != PJ_SUCCESS) {
	pj_sock_close(sock_fd);
	return status;
    }

    status = pj_activesock_create(pool, sock_fd, pj_SOCK_DGRAM(), opt,
				  ioqueue, cb, p_asock);
    if (status != PJ_SUCCESS) {
	pj_sock_close(sock_fd);
	return status;
    }

    if (bound_addr) {
	int addr_len = sizeof(*bound_addr);
	status = pj_sock_getsockname(sock_fd, bound_addr, &addr_len);
	if (status != PJ_SUCCESS) {
	    pj_activesock_close(*p_asock);
	    return status;
	}
    }

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pj_activesock_close(pj_activesock_t *asock)
{
    PJ_ASSERT_RETURN(asock, PJ_EINVAL);
    if (asock->key) {
	pj_ioqueue_unregister(asock->key);
	asock->key = NULL;
    }
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pj_activesock_set_user_data( pj_activesock_t *asock,
						 void *user_data)
{
    PJ_ASSERT_RETURN(asock, PJ_EINVAL);
    asock->user_data = user_data;
    return PJ_SUCCESS;
}


PJ_DEF(void*) pj_activesock_get_user_data(pj_activesock_t *asock)
{
    PJ_ASSERT_RETURN(asock, NULL);
    return asock->user_data;
}


PJ_DEF(pj_status_t) pj_activesock_start_read(pj_activesock_t *asock,
					     pj_pool_t *pool,
					     unsigned buff_size,
					     pj_uint32_t flags)
{
    unsigned i;
    pj_status_t status;

    PJ_ASSERT_RETURN(asock && pool && buff_size, PJ_EINVAL);
    PJ_ASSERT_RETURN(asock->read_type == TYPE_NONE, PJ_EINVALIDOP);
    PJ_ASSERT_RETURN(asock->read_op == NULL, PJ_EINVALIDOP);

    asock->read_op = (struct read_op*)
		     pj_pool_calloc(pool, asock->async_count, 
				    sizeof(struct read_op));
    asock->read_type = TYPE_RECV;
    asock->read_flags = flags;

    for (i=0; i<asock->async_count; ++i) {
	struct read_op *r = &asock->read_op[i];
	pj_ssize_t size_to_read;

	r->pkt = pj_pool_alloc(pool, buff_size);
	r->max_size = size_to_read = buff_size;

	status = pj_ioqueue_recv(asock->key, &r->op_key, r->pkt, &size_to_read,
				 PJ_IOQUEUE_ALWAYS_ASYNC | flags);
	PJ_ASSERT_RETURN(status != PJ_SUCCESS, PJ_EBUG);

	if (status != PJ_EPENDING)
	    return status;
    }

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pj_activesock_start_recvfrom(pj_activesock_t *asock,
						 pj_pool_t *pool,
						 unsigned buff_size,
						 pj_uint32_t flags)
{
    unsigned i;
    pj_status_t status;

    PJ_ASSERT_RETURN(asock && pool && buff_size, PJ_EINVAL);
    PJ_ASSERT_RETURN(asock->read_type == TYPE_NONE, PJ_EINVALIDOP);

    asock->read_op = (struct read_op*)
		     pj_pool_calloc(pool, asock->async_count, 
				    sizeof(struct read_op));
    asock->read_type = TYPE_RECV_FROM;
    asock->read_flags = flags;

    for (i=0; i<asock->async_count; ++i) {
	struct read_op *r = &asock->read_op[i];
	pj_ssize_t size_to_read;

	r->pkt = pj_pool_alloc(pool, buff_size);
	r->max_size = size_to_read = buff_size;
	r->src_addr_len = sizeof(r->src_addr);

	status = pj_ioqueue_recvfrom(asock->key, &r->op_key, r->pkt,
				     &size_to_read, 
				     PJ_IOQUEUE_ALWAYS_ASYNC | flags,
				     &r->src_addr, &r->src_addr_len);
	PJ_ASSERT_RETURN(status != PJ_SUCCESS, PJ_EBUG);

	if (status != PJ_EPENDING)
	    return status;
    }

    return PJ_SUCCESS;
}


static void ioqueue_on_read_complete(pj_ioqueue_key_t *key, 
				     pj_ioqueue_op_key_t *op_key, 
				     pj_ssize_t bytes_read)
{
    pj_activesock_t *asock;
    struct read_op *r = (struct read_op*)op_key;
    unsigned loop = 0;
    pj_status_t status;

    asock = (pj_activesock_t*) pj_ioqueue_get_user_data(key);

    do {
	unsigned flags;

	if (bytes_read > 0) {
	    /*
	     * We've got new data.
	     */
	    pj_size_t remainder;
	    pj_bool_t ret;

	    /* Append this new data to existing data. If socket is stream 
	     * oriented, user might have left some data in the buffer. 
	     * Otherwise if socket is datagram there will be nothing in 
	     * existing packet hence the packet will contain only the new
	     * packet.
	     */
	    r->size += bytes_read;

	    /* Set default remainder to zero */
	    remainder = 0;

	    /* And return value to TRUE */
	    ret = PJ_TRUE;

	    /* Notify callback */
	    if (asock->read_type == TYPE_RECV && asock->cb.on_data_read) {
		ret = (*asock->cb.on_data_read)(asock, r->pkt, r->size,
						PJ_SUCCESS, &remainder);
	    } else if (asock->read_type == TYPE_RECV_FROM && 
		       asock->cb.on_data_recvfrom) 
	    {
		ret = (*asock->cb.on_data_recvfrom)(asock, r->pkt, r->size,
						    &r->src_addr, 
						    r->src_addr_len,
						    PJ_SUCCESS);
	    }

	    /* If callback returns false, we have been destroyed! */
	    if (!ret)
		return;

	    /* Only stream oriented socket may leave data in the packet */
	    if (asock->stream_oriented) {
		r->size = remainder;
	    } else {
		r->size = 0;
	    }

	} else if (bytes_read <= 0) {

	    pj_size_t remainder;
	    pj_bool_t ret;

	    if (bytes_read == 0) {
		/* For stream/connection oriented socket, this means the 
		 * connection has been closed. For datagram sockets, it means
		 * we've received datagram with zero length.
		 */
		if (asock->stream_oriented)
		    status = PJ_EEOF;
		else
		    status = PJ_SUCCESS;
	    } else {
		/* This means we've got an error. If this is stream/connection
		 * oriented, it means connection has been closed. For datagram
		 * sockets, it means we've got some error (e.g. EWOULDBLOCK).
		 */
		status = -bytes_read;
	    }

	    /* Set default remainder to zero */
	    remainder = 0;

	    /* And return value to TRUE */
	    ret = PJ_TRUE;

	    /* Notify callback */
	    if (asock->read_type == TYPE_RECV && asock->cb.on_data_read) {
		/* For connection oriented socket, we still need to report 
		 * the remainder data (if any) to the user to let user do 
		 * processing with the remainder data before it closes the
		 * connection.
		 * If there is no remainder data, set the packet to NULL.
		 */
		ret = (*asock->cb.on_data_read)(asock, (r->size? r->pkt:NULL),
						r->size, status, &remainder);

	    } else if (asock->read_type == TYPE_RECV_FROM && 
		       asock->cb.on_data_recvfrom) 
	    {
		/* This would always be datagram oriented hence there's 
		 * nothing in the packet. We can't be sure if there will be
		 * anything useful in the source_addr, so just put NULL
		 * there too.
		 */
		ret = (*asock->cb.on_data_recvfrom)(asock, NULL, 0,
						    NULL, 0, status);
	    }

	    /* If callback returns false, we have been destroyed! */
	    if (!ret)
		return;

	    /* Only stream oriented socket may leave data in the packet */
	    if (asock->stream_oriented) {
		r->size = remainder;
	    } else {
		r->size = 0;
	    }
	}

	/* Read next data. We limit ourselves to processing max_loop immediate
	 * data, so when the loop counter has exceeded this value, force the
	 * read()/recvfrom() to return pending operation to allow the program
	 * to do other jobs.
	 */
	bytes_read = r->max_size - r->size;
	flags = asock->read_flags;
	if (++loop >= asock->max_loop)
	    flags |= PJ_IOQUEUE_ALWAYS_ASYNC;

	if (asock->read_type == TYPE_RECV) {
	    status = pj_ioqueue_recv(key, op_key, r->pkt + r->size, 
				     &bytes_read, flags);
	} else {
	    r->src_addr_len = sizeof(r->src_addr);
	    status = pj_ioqueue_recvfrom(key, op_key, r->pkt + r->size,
				         &bytes_read, flags,
					 &r->src_addr, &r->src_addr_len);
	}

    } while (status != PJ_EPENDING && status != PJ_ECANCELLED);

}


PJ_DEF(pj_status_t) pj_activesock_send( pj_activesock_t *asock,
					pj_ioqueue_op_key_t *send_key,
					const void *data,
					pj_ssize_t *size,
					unsigned flags)
{
    PJ_ASSERT_RETURN(asock && send_key && data && size, PJ_EINVAL);

    return pj_ioqueue_send(asock->key, send_key, data, size, flags);
}


PJ_DEF(pj_status_t) pj_activesock_sendto( pj_activesock_t *asock,
					  pj_ioqueue_op_key_t *send_key,
					  const void *data,
					  pj_ssize_t *size,
					  unsigned flags,
					  const pj_sockaddr_t *addr,
					  int addr_len)
{
    PJ_ASSERT_RETURN(asock && send_key && data && size && addr && addr_len, 
		     PJ_EINVAL);

    return pj_ioqueue_sendto(asock->key, send_key, data, size, flags,
			     addr, addr_len);
}


static void ioqueue_on_write_complete(pj_ioqueue_key_t *key, 
				      pj_ioqueue_op_key_t *op_key,
				      pj_ssize_t bytes_sent)
{
    pj_activesock_t *asock;

    asock = (pj_activesock_t*) pj_ioqueue_get_user_data(key);

    if (asock->cb.on_data_sent) {
	pj_bool_t ret;

	ret = (*asock->cb.on_data_sent)(asock, op_key, bytes_sent);

	/* If callback returns false, we have been destroyed! */
	if (!ret)
	    return;
    }
}


PJ_DEF(pj_status_t) pj_activesock_start_accept(pj_activesock_t *asock,
					       pj_pool_t *pool)
{
    unsigned i;

    PJ_ASSERT_RETURN(asock, PJ_EINVAL);
    PJ_ASSERT_RETURN(asock->accept_op==NULL, PJ_EINVALIDOP);

    asock->accept_op = (struct accept_op*)
		       pj_pool_calloc(pool, asock->async_count,
				      sizeof(struct accept_op));
    for (i=0; i<asock->async_count; ++i) {
	struct accept_op *a = &asock->accept_op[i];
	pj_status_t status;

	do {
	    a->new_sock = PJ_INVALID_SOCKET;
	    a->rem_addr_len = sizeof(a->rem_addr);

	    status = pj_ioqueue_accept(asock->key, &a->op_key, &a->new_sock,
				       NULL, &a->rem_addr, &a->rem_addr_len);
	    if (status == PJ_SUCCESS) {
		/* We've got immediate connection. Not sure if it's a good
		 * idea to call the callback now (probably application will
		 * not be prepared to process it), so lets just silently
		 * close the socket.
		 */
		pj_sock_close(a->new_sock);
	    }
	} while (status == PJ_SUCCESS);

	if (status != PJ_EPENDING) {
	    return status;
	}
    }

    return PJ_SUCCESS;
}


static void ioqueue_on_accept_complete(pj_ioqueue_key_t *key, 
				       pj_ioqueue_op_key_t *op_key,
				       pj_sock_t new_sock, 
				       pj_status_t status)
{
    pj_activesock_t *asock = (pj_activesock_t*) pj_ioqueue_get_user_data(key);
    struct accept_op *accept_op = (struct accept_op*) op_key;

    do {
	if (status==PJ_SUCCESS && asock->cb.on_accept_complete) {
	    pj_bool_t ret;

	    /* Notify callback */
	    ret = (*asock->cb.on_accept_complete)(asock, new_sock,
						  &accept_op->rem_addr,
						  accept_op->rem_addr_len);

	    /* If callback returns false, we have been destroyed! */
	    if (!ret)
		return;

	} else if (status==PJ_SUCCESS) {
	    /* Application doesn't handle the new socket, we need to 
	     * close it to avoid resource leak.
	     */
	    pj_sock_close(accept_op->new_sock);
	}

	/* Prepare next accept() */
	accept_op->new_sock = PJ_INVALID_SOCKET;
	accept_op->rem_addr_len = sizeof(accept_op->rem_addr);

	status = pj_ioqueue_accept(asock->key, op_key, &accept_op->new_sock,
				   NULL, &accept_op->rem_addr, 
				   &accept_op->rem_addr_len);

    } while (status != PJ_EPENDING && status != PJ_ECANCELLED);
}


PJ_DEF(pj_status_t) pj_activesock_start_connect( pj_activesock_t *asock,
						 pj_pool_t *pool,
						 const pj_sockaddr_t *remaddr,
						 int addr_len)
{
    PJ_UNUSED_ARG(pool);
    return pj_ioqueue_connect(asock->key, remaddr, addr_len);
}


static void ioqueue_on_connect_complete(pj_ioqueue_key_t *key, 
					pj_status_t status)
{
    pj_activesock_t *asock = (pj_activesock_t*) pj_ioqueue_get_user_data(key);

    if (asock->cb.on_connect_complete) {
	pj_bool_t ret;

	ret = (*asock->cb.on_connect_complete)(asock, status);

	if (!ret) {
	    /* We've been destroyed */
	    return;
	}
    }
}

