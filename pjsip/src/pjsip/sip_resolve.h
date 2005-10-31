/* $Header: /pjproject/pjsip/src/pjsip/sip_resolve.h 5     6/17/05 11:16p Bennylp $ */
#ifndef __PJSIP_SIP_RESOLVE_H__
#define __PJSIP_SIP_RESOLVE_H__

/**
 * @file sip_resolve.h
 * @brief 
 * This module contains the mechanism to resolve server address as specified by
 * RFC 3263 - Locating SIP Servers
 */

#include <pjsip/sip_types.h>
#include <pj/sock.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJSIP_RESOLVE SIP Server Resolver
 * @ingroup PJSIP
 * @{
 */

/** 
 * Maximum number of addresses returned by the resolver. 
 */
#define PJSIP_MAX_RESOLVED_ADDRESSES	8

typedef struct pjsip_server_addresses pjsip_server_addresses;

/**
 * The server addresses returned by the resolver.
 */
struct pjsip_server_addresses
{
    /** Number of address records. */
    unsigned	count;

    /** Address records. */
    struct
    {
	/** Preferable transport to be used to contact this address. */
	pjsip_transport_type_e	type;

	/** The server's address. */
	pj_sockaddr_in		addr;

    } entry[PJSIP_MAX_RESOLVED_ADDRESSES];

};

/**
 * The type of callback function to be called when resolver finishes the job.
 *
 * @param status    The status of the operation, which is zero on success.
 * @param token	    The token that was associated with the job when application
 *		    call the resolve function.
 * @param addr	    The addresses resolved by the operation.
 */
typedef void pjsip_resolver_callback(pj_status_t status,
				     void *token,
				     const struct pjsip_server_addresses *addr);

/**
 * Create resolver engine.
 *
 * @param pool	The Pool.
 * @return	The resolver engine.
 */
PJ_DECL(pjsip_resolver_t*) pjsip_resolver_create(pj_pool_t *pool);

/**
 * Destroy resolver engine.
 *
 * @param resolver The resolver.
 */
PJ_DECL(void) pjsip_resolver_destroy(pjsip_resolver_t *resolver);

/**
 * Asynchronously resolve a SIP target host or domain according to rule 
 * specified in RFC 3263 (Locating SIP Servers). When the resolving operation
 * has completed, the callback will be called.
 *
 * Note: at the moment we don't have implementation of RFC 3263 yet!
 *
 * @param resolver	The resolver engine.
 * @param pool		The pool to allocate resolver job.
 * @param target	The target specification to be resolved.
 * @param token		A user defined token to be passed back to callback function.
 * @param cb		The callback function.
 */
PJ_DECL(void) pjsip_resolve( pjsip_resolver_t *resolver,
			     pj_pool_t *pool,
			     pjsip_host_port *target,
			     void *token,
			     pjsip_resolver_callback *cb);

/**
 * @}
 */

PJ_END_DECL

#endif	/* __PJSIP_SIP_RESOLVE_H__ */
