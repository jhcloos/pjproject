/* $Header: /pjproject-0.3/pjlib/src/pj/ioqueue_linux_kernel.c 1     10/05/05 4:42p Bennylp $ */
/* $Log: /pjproject-0.3/pjlib/src/pj/ioqueue_linux_kernel.c $
 * 
 * 1     10/05/05 4:42p Bennylp
 * Created.
 * 
 */
#include <pj/ioqueue.h>
#include <pj/os.h>
#include <pj/log.h>
#include <pj/list.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/assert.h>
#include <pj/sock.h>

#define THIS_FILE   "ioqueue"

#define PJ_IOQUEUE_IS_READ_OP(op)   \
	((op & PJ_IOQUEUE_OP_READ)  || (op & PJ_IOQUEUE_OP_RECV_FROM))
#define PJ_IOQUEUE_IS_WRITE_OP(op)  \
	((op & PJ_IOQUEUE_OP_WRITE) || (op & PJ_IOQUEUE_OP_SEND_TO))


#if PJ_HAS_TCP
#  define PJ_IOQUEUE_IS_ACCEPT_OP(op)	(op & PJ_IOQUEUE_OP_ACCEPT)
#  define PJ_IOQUEUE_IS_CONNECT_OP(op)	(op & PJ_IOQUEUE_OP_CONNECT)
#else
#  define PJ_IOQUEUE_IS_ACCEPT_OP(op)	0
#  define PJ_IOQUEUE_IS_CONNECT_OP(op)	0
#endif

#if defined(PJ_DEBUG) && PJ_DEBUG != 0
#  define VALIDATE_FD_SET		1
#else
#  define VALIDATE_FD_SET		0
#endif

struct pj_ioqueue_key_t
{
    PJ_DECL_LIST_MEMBER(struct pj_ioqueue_key_t)
    pj_sock_t		    fd;
    pj_ioqueue_operation_e  op;
    void		   *user_data;
    pj_ioqueue_callback	    cb;
};

struct pj_ioqueue_t
{
};

PJ_DEF(pj_ioqueue_t*) pj_ioqueue_create(pj_pool_t *pool, pj_size_t max_fd)
{
    return NULL;
}

PJ_DEF(pj_status_t) pj_ioqueue_destroy(pj_ioqueue_t *ioque)
{
    return 0;
}

PJ_DEF(pj_ioqueue_key_t*) pj_ioqueue_register( pj_pool_t *pool,
					       pj_ioqueue_t *ioque,
					       pj_oshandle_t sock,
					       void *user_data,
					       const pj_ioqueue_callback *cb)
{
    return NULL;
}

PJ_DEF(pj_status_t) pj_ioqueue_unregister( pj_ioqueue_t *ioque,
					   pj_ioqueue_key_t *key)
{
    return -1;
}

PJ_DEF(void*) pj_ioqueue_get_user_data( pj_ioqueue_key_t *key )
{
    return NULL;
}


PJ_DEF(int) pj_ioqueue_poll( pj_ioqueue_t *ioque, const pj_time_val *timeout)
{
    return -1;
}

PJ_DEF(int) pj_ioqueue_read( pj_ioqueue_t *ioque,
			     pj_ioqueue_key_t *key,
			     void *buffer,
			     pj_size_t buflen)
{
    return -1;
}

PJ_DEF(int) pj_ioqueue_recvfrom( pj_ioqueue_t *ioque,
				 pj_ioqueue_key_t *key,
				 void *buffer,
				 pj_size_t buflen,
				 pj_sockaddr_t *addr,
				 int *addrlen)
{
    return -1;
}

PJ_DEF(int) pj_ioqueue_write( pj_ioqueue_t *ioque,
			      pj_ioqueue_key_t *key,
			      const void *data,
			      pj_size_t datalen)
{
    return -1;
}

PJ_DEF(int) pj_ioqueue_sendto( pj_ioqueue_t *ioque,
			       pj_ioqueue_key_t *key,
			       const void *data,
			       pj_size_t datalen,
			       const pj_sockaddr_t *addr,
			       int addrlen)
{
    return -1;
}

#if PJ_HAS_TCP
/*
 * Initiate overlapped accept() operation.
 */
PJ_DEF(int) pj_ioqueue_accept( pj_ioqueue_t *ioqueue,
			       pj_ioqueue_key_t *key,
			       pj_sock_t *new_sock,
			       pj_sockaddr_t *local,
			       pj_sockaddr_t *remote,
			       int *addrlen)
{
    return -1;
}

/*
 * Initiate overlapped connect() operation (well, it's non-blocking actually,
 * since there's no overlapped version of connect()).
 */
PJ_DEF(pj_status_t) pj_ioqueue_connect( pj_ioqueue_t *ioqueue,
					pj_ioqueue_key_t *key,
					const pj_sockaddr_t *addr,
					int addrlen )
{
    return -1;
}
#endif	/* PJ_HAS_TCP */

