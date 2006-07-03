/* $Id$ */
/* 
 * Copyright (C) 2003-2006 Benny Prijono <benny@prijono.org>
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
#include <pjsip/sip_uri.h>
#include <pjsip/sip_msg.h>
#include <pjsip/sip_parser.h>
#include <pjsip/print_util.h>
#include <pjsip/sip_errno.h>
#include <pjlib-util/string.h>
#include <pj/string.h>
#include <pj/pool.h>
#include <pj/assert.h>

/*
 * Generic parameter manipulation.
 */
PJ_DEF(pjsip_param*) pjsip_param_find(  pjsip_param *param_list,
					const pj_str_t *name )
{
    pjsip_param *p = param_list->next;
    while (p != param_list) {
	if (pj_stricmp(&p->name, name)==0)
	    return p;
	p = p->next;
    }
    return NULL;
}

PJ_DEF(const pjsip_param*) pjsip_param_cfind( const pjsip_param *param_list,
					      const pj_str_t *name )
{
    const pjsip_param *p = param_list->next;
    while (p != param_list) {
	if (pj_stricmp_alnum(&p->name, name)==0)
	    return p;
	p = p->next;
    }
    return NULL;
}

PJ_DEF(void) pjsip_param_clone( pj_pool_t *pool, pjsip_param *dst_list,
				const pjsip_param *src_list)
{
    const pjsip_param *p = src_list->next;

    pj_list_init(dst_list);
    while (p != src_list) {
	pjsip_param *new_param = pj_pool_alloc(pool, sizeof(pjsip_param));
	pj_strdup(pool, &new_param->name, &p->name);
	pj_strdup(pool, &new_param->value, &p->value);
	pj_list_insert_before(dst_list, new_param);
	p = p->next;
    }
}


PJ_DEF(void) pjsip_param_shallow_clone( pj_pool_t *pool, 
					pjsip_param *dst_list,
					const pjsip_param *src_list)
{
    const pjsip_param *p = src_list->next;

    pj_list_init(dst_list);
    while (p != src_list) {
	pjsip_param *new_param = pj_pool_alloc(pool, sizeof(pjsip_param));
	new_param->name = p->name;
	new_param->value = p->value;
	pj_list_insert_before(dst_list, new_param);
	p = p->next;
    }
}

PJ_DEF(pj_ssize_t) pjsip_param_print_on( const pjsip_param *param_list,
					 char *buf, pj_size_t size,
					 const pj_cis_t *pname_spec,
					 const pj_cis_t *pvalue_spec,
					 int sep)
{
    const pjsip_param *p;
    char *startbuf;
    char *endbuf;
    int printed;

    p = param_list->next;
    if (p == param_list)
	return 0;

    startbuf = buf;
    endbuf = buf + size;

    do {
	*buf++ = (char)sep;
	copy_advance_escape(buf, p->name, (*pname_spec));
	if (p->value.slen) {
	    *buf++ = '=';
	    copy_advance_escape(buf, p->value, (*pvalue_spec));
	}
	p = p->next;
	if (sep == '?') sep = '&';
    } while (p != param_list);

    return buf-startbuf;
}


/*
 * URI stuffs
 */
#define IS_SIPS(url)	((url)->vptr==&sips_url_vptr)

static const pj_str_t *pjsip_url_get_scheme( const pjsip_sip_uri* );
static const pj_str_t *pjsips_url_get_scheme( const pjsip_sip_uri* );
static const pj_str_t *pjsip_name_addr_get_scheme( const pjsip_name_addr * );
static void *pjsip_get_uri( pjsip_uri *uri );
static void *pjsip_name_addr_get_uri( pjsip_name_addr *name );

static pj_str_t sip_str = { "sip", 3 };
static pj_str_t sips_str = { "sips", 4 };

#ifdef __GNUC__
#  define HAPPY_FLAG	(void*)
#else
#  define HAPPY_FLAG
#endif

static pjsip_name_addr* pjsip_name_addr_clone( pj_pool_t *pool, 
					       const pjsip_name_addr *rhs);
static pj_ssize_t pjsip_name_addr_print(pjsip_uri_context_e context,
					const pjsip_name_addr *name, 
					char *buf, pj_size_t size);
static int pjsip_name_addr_compare(  pjsip_uri_context_e context,
				     const pjsip_name_addr *naddr1,
				     const pjsip_name_addr *naddr2);
static pj_ssize_t pjsip_url_print(  pjsip_uri_context_e context,
				    const pjsip_sip_uri *url, 
				    char *buf, pj_size_t size);
static int pjsip_url_compare( pjsip_uri_context_e context,
			      const pjsip_sip_uri *url1, 
			      const pjsip_sip_uri *url2);
static pjsip_sip_uri* pjsip_url_clone(pj_pool_t *pool, 
				      const pjsip_sip_uri *rhs);

static pjsip_uri_vptr sip_url_vptr = 
{
    HAPPY_FLAG &pjsip_url_get_scheme,
    HAPPY_FLAG &pjsip_get_uri,
    HAPPY_FLAG &pjsip_url_print,
    HAPPY_FLAG &pjsip_url_compare,
    HAPPY_FLAG &pjsip_url_clone
};

static pjsip_uri_vptr sips_url_vptr = 
{
    HAPPY_FLAG &pjsips_url_get_scheme,
    HAPPY_FLAG &pjsip_get_uri,
    HAPPY_FLAG &pjsip_url_print,
    HAPPY_FLAG &pjsip_url_compare,
    HAPPY_FLAG &pjsip_url_clone
};

static pjsip_uri_vptr name_addr_vptr = 
{
    HAPPY_FLAG &pjsip_name_addr_get_scheme,
    HAPPY_FLAG &pjsip_name_addr_get_uri,
    HAPPY_FLAG &pjsip_name_addr_print,
    HAPPY_FLAG &pjsip_name_addr_compare,
    HAPPY_FLAG &pjsip_name_addr_clone
};

static const pj_str_t *pjsip_url_get_scheme(const pjsip_sip_uri *url)
{
    PJ_UNUSED_ARG(url);
    return &sip_str;
}

static const pj_str_t *pjsips_url_get_scheme(const pjsip_sip_uri *url)
{
    PJ_UNUSED_ARG(url);
    return &sips_str;
}

static void *pjsip_get_uri( pjsip_uri *uri )
{
    return uri;
}

static void *pjsip_name_addr_get_uri( pjsip_name_addr *name )
{
    return name->uri;
}

PJ_DEF(void) pjsip_sip_uri_init(pjsip_sip_uri *url, int secure)
{
    pj_bzero(url, sizeof(*url));
    url->ttl_param = -1;
    url->vptr = secure ? &sips_url_vptr : &sip_url_vptr;
    pj_list_init(&url->other_param);
    pj_list_init(&url->header_param);
}

PJ_DEF(pjsip_sip_uri*) pjsip_sip_uri_create( pj_pool_t *pool, int secure )
{
    pjsip_sip_uri *url = pj_pool_alloc(pool, sizeof(pjsip_sip_uri));
    pjsip_sip_uri_init(url, secure);
    return url;
}

static pj_ssize_t pjsip_url_print(  pjsip_uri_context_e context,
				    const pjsip_sip_uri *url, 
				    char *buf, pj_size_t size)
{
    int printed;
    char *startbuf = buf;
    char *endbuf = buf+size;
    const pj_str_t *scheme;

    *buf = '\0';

    /* Print scheme ("sip:" or "sips:") */
    scheme = pjsip_uri_get_scheme(url);
    copy_advance_check(buf, *scheme);
    *buf++ = ':';

    /* Print "user:password@", if any. */
    if (url->user.slen) {
	copy_advance_escape(buf, url->user, pjsip_USER_SPEC);
	if (url->passwd.slen) {
	    *buf++ = ':';
	    copy_advance_escape(buf, url->passwd, pjsip_PASSWD_SPEC);
	}

	*buf++ = '@';
    }

    /* Print host. */
    pj_assert(url->host.slen != 0);
    copy_advance_check(buf, url->host);

    /* Only print port if it is explicitly specified. 
     * Port is not allowed in To and From header.
     */
    /* Unfortunately some UA requires us to send back the port
     * number exactly as it was sent. We don't remember whether an
     * UA has sent us port, so we'll just send the port indiscrimately
     */
    //PJ_TODO(SHOULD_DISALLOW_URI_PORT_IN_FROM_TO_HEADER)
    if (url->port && context != PJSIP_URI_IN_FROMTO_HDR) {
	if (endbuf - buf < 10)
	    return -1;

	*buf++ = ':';
	printed = pj_utoa(url->port, buf);
	buf += printed;
    }

    /* User param is allowed in all contexes */
    copy_advance_pair_check(buf, ";user=", 6, url->user_param);

    /* Method param is only allowed in external/other context. */
    if (context == PJSIP_URI_IN_OTHER) {
	copy_advance_pair_escape(buf, ";method=", 8, url->method_param, 
				 pjsip_PARAM_CHAR_SPEC);
    }

    /* Transport is not allowed in From/To header. */
    if (context != PJSIP_URI_IN_FROMTO_HDR) {
	copy_advance_pair_escape(buf, ";transport=", 11, url->transport_param,
				 pjsip_PARAM_CHAR_SPEC);
    }

    /* TTL param is not allowed in From, To, Route, and Record-Route header. */
    if (url->ttl_param >= 0 && context != PJSIP_URI_IN_FROMTO_HDR &&
	context != PJSIP_URI_IN_ROUTING_HDR) 
    {
	if (endbuf - buf < 15)
	    return -1;
	pj_memcpy(buf, ";ttl=", 5);
	printed = pj_utoa(url->ttl_param, buf+5);
	buf += printed + 5;
    }

    /* maddr param is not allowed in From and To header. */
    if (context != PJSIP_URI_IN_FROMTO_HDR) {
	copy_advance_pair_escape(buf, ";maddr=", 7, url->maddr_param,
				 pjsip_PARAM_CHAR_SPEC);
    }

    /* lr param is not allowed in From, To, and Contact header. */
    if (url->lr_param && context != PJSIP_URI_IN_FROMTO_HDR &&
	context != PJSIP_URI_IN_CONTACT_HDR) 
    {
	pj_str_t lr = { ";lr", 3 };
	if (endbuf - buf < 3)
	    return -1;
	copy_advance_check(buf, lr);
    }

    /* Other param. */
    printed = pjsip_param_print_on(&url->other_param, buf, endbuf-buf, 
				   &pjsip_PARAM_CHAR_SPEC, 
				   &pjsip_PARAM_CHAR_SPEC, ';');
    if (printed < 0)
	return -1;
    buf += printed;

    /* Header param. 
     * Header param is only allowed in these contexts:
     *	- PJSIP_URI_IN_CONTACT_HDR
     *	- PJSIP_URI_IN_OTHER
     */
    if (context == PJSIP_URI_IN_CONTACT_HDR || context == PJSIP_URI_IN_OTHER) {
	printed = pjsip_param_print_on(&url->header_param, buf, endbuf-buf,
				       &pjsip_HDR_CHAR_SPEC, 
				       &pjsip_HDR_CHAR_SPEC, '?');
	if (printed < 0)
	    return -1;
	buf += printed;
    }

    *buf = '\0';
    return buf-startbuf;
}

static pj_status_t pjsip_url_compare( pjsip_uri_context_e context,
				      const pjsip_sip_uri *url1, 
				      const pjsip_sip_uri *url2)
{
    const pjsip_param *p1;

    /*
     * Compare two SIP URL's according to Section 19.1.4 of RFC 3261.
     */

    /* SIP and SIPS URI are never equivalent. 
     * Note: just compare the vptr to avoid string comparison. 
     *       Pretty neat huh!!
     */
    if (url1->vptr != url2->vptr)
	return PJSIP_ECMPSCHEME;

    /* Comparison of the userinfo of SIP and SIPS URIs is case-sensitive. 
     * This includes userinfo containing passwords or formatted as 
     * telephone-subscribers.
     */
    if (pj_strcmp(&url1->user, &url2->user) != 0)
	return PJSIP_ECMPUSER;
    if (pj_strcmp(&url1->passwd, &url2->passwd) != 0)
	return PJSIP_ECMPPASSWD;
    
    /* Comparison of all other components of the URI is
     * case-insensitive unless explicitly defined otherwise.
     */

    /* The ordering of parameters and header fields is not significant 
     * in comparing SIP and SIPS URIs.
     */

    /* Characters other than those in the �reserved� set (see RFC 2396 [5])
     * are equivalent to their �encoding.
     */

    /* An IP address that is the result of a DNS lookup of a host name 
     * does not match that host name.
     */
    if (pj_stricmp(&url1->host, &url2->host) != 0)
	return PJSIP_ECMPHOST;

    /* A URI omitting any component with a default value will not match a URI
     * explicitly containing that component with its default value. 
     * For instance, a URI omitting the optional port component will not match
     * a URI explicitly declaring port 5060. 
     * The same is true for the transport-parameter, ttl-parameter, 
     * user-parameter, and method components.
     */

    /* Port is not allowed in To and From header.
     */
    if (context != PJSIP_URI_IN_FROMTO_HDR) {
	if (url1->port != url2->port)
	    return PJSIP_ECMPPORT;
    }
    /* Transport is not allowed in From/To header. */
    if (context != PJSIP_URI_IN_FROMTO_HDR) {
	if (pj_stricmp(&url1->transport_param, &url2->transport_param) != 0)
	    return PJSIP_ECMPTRANSPORTPRM;
    }
    /* TTL param is not allowed in From, To, Route, and Record-Route header. */
    if (context != PJSIP_URI_IN_FROMTO_HDR &&
	context != PJSIP_URI_IN_ROUTING_HDR)
    {
	if (url1->ttl_param != url2->ttl_param)
	    return PJSIP_ECMPTTLPARAM;
    }
    /* User param is allowed in all contexes */
    if (pj_stricmp(&url1->user_param, &url2->user_param) != 0)
	return PJSIP_ECMPUSERPARAM;
    /* Method param is only allowed in external/other context. */
    if (context == PJSIP_URI_IN_OTHER) {
	if (pj_stricmp(&url1->method_param, &url2->method_param) != 0)
	    return PJSIP_ECMPMETHODPARAM;
    }
    /* maddr param is not allowed in From and To header. */
    if (context != PJSIP_URI_IN_FROMTO_HDR) {
	if (pj_stricmp(&url1->maddr_param, &url2->maddr_param) != 0)
	    return PJSIP_ECMPMADDRPARAM;
    }

    /* lr parameter is ignored (?) */
    /* lr param is not allowed in From, To, and Contact header. */


    /* All other uri-parameters appearing in only one URI are ignored when 
     * comparing the URIs.
     */
    p1 = url1->other_param.next;
    while (p1 != &url1->other_param) {
	const pjsip_param *p2;
	p2 = pjsip_param_cfind(&url2->other_param, &p1->name);
	if (p2 ) {
	    if (pj_stricmp(&p1->value, &p2->value) != 0)
		return PJSIP_ECMPOTHERPARAM;
	}

	p1 = p1->next;
    }

    /* URI header components are never ignored. Any present header component
     * MUST be present in both URIs and match for the URIs to match. 
     * The matching rules are defined for each header field in Section 20.
     */
    p1 = url1->header_param.next;
    while (p1 != &url1->header_param) {
	const pjsip_param *p2;
	p2 = pjsip_param_cfind(&url2->header_param, &p1->name);
	if (p2) {
	    /* It seems too much to compare two header params according to
	     * the rule of each header. We'll just compare them string to
	     * string..
	     */
	    if (pj_stricmp(&p1->value, &p2->value) != 0)
		return PJSIP_ECMPHEADERPARAM;
	} else {
	    return PJSIP_ECMPHEADERPARAM;
	}
	p1 = p1->next;
    }

    /* Equal!! Pheuww.. */
    return PJ_SUCCESS;
}


PJ_DEF(void) pjsip_sip_uri_assign(pj_pool_t *pool, pjsip_sip_uri *url, 
				  const pjsip_sip_uri *rhs)
{
    pj_strdup( pool, &url->user, &rhs->user);
    pj_strdup( pool, &url->passwd, &rhs->passwd);
    pj_strdup( pool, &url->host, &rhs->host);
    url->port = rhs->port;
    pj_strdup( pool, &url->user_param, &rhs->user_param);
    pj_strdup( pool, &url->method_param, &rhs->method_param);
    pj_strdup( pool, &url->transport_param, &rhs->transport_param);
    url->ttl_param = rhs->ttl_param;
    pj_strdup( pool, &url->maddr_param, &rhs->maddr_param);
    pjsip_param_clone(pool, &url->other_param, &rhs->other_param);
    pjsip_param_clone(pool, &url->header_param, &rhs->header_param);
    url->lr_param = rhs->lr_param;
}

static pjsip_sip_uri* pjsip_url_clone(pj_pool_t *pool, const pjsip_sip_uri *rhs)
{
    pjsip_sip_uri *url = pj_pool_alloc(pool, sizeof(pjsip_sip_uri));
    if (!url)
	return NULL;

    pjsip_sip_uri_init(url, IS_SIPS(rhs));
    pjsip_sip_uri_assign(pool, url, rhs);
    return url;
}

static const pj_str_t *pjsip_name_addr_get_scheme(const pjsip_name_addr *name)
{
    pj_assert(name->uri != NULL);
    return pjsip_uri_get_scheme(name->uri);
}

PJ_DEF(void) pjsip_name_addr_init(pjsip_name_addr *name)
{
    name->vptr = &name_addr_vptr;
    name->uri = NULL;
    name->display.slen = 0;
}

PJ_DEF(pjsip_name_addr*) pjsip_name_addr_create(pj_pool_t *pool)
{
    pjsip_name_addr *name_addr = pj_pool_alloc(pool, sizeof(pjsip_name_addr));
    pjsip_name_addr_init(name_addr);
    return name_addr;
}

static pj_ssize_t pjsip_name_addr_print(pjsip_uri_context_e context,
					const pjsip_name_addr *name, 
					char *buf, pj_size_t size)
{
    int printed;
    char *startbuf = buf;
    char *endbuf = buf + size;

    pj_assert(name->uri != NULL);

    if (context != PJSIP_URI_IN_REQ_URI) {
	if (name->display.slen) {
	    if (endbuf-buf < 8) return -1;
	    *buf++ = '"';
	    copy_advance(buf, name->display);
	    *buf++ = '"';
	    *buf++ = ' ';
	}
	*buf++ = '<';
    }

    printed = pjsip_uri_print(context,name->uri, buf, size-(buf-startbuf));
    if (printed < 1)
	return -1;
    buf += printed;

    if (context != PJSIP_URI_IN_REQ_URI) {
	*buf++ = '>';
    }

    *buf = '\0';
    return buf-startbuf;
}

PJ_DEF(void) pjsip_name_addr_assign(pj_pool_t *pool, pjsip_name_addr *dst,
				    const pjsip_name_addr *src)
{
    pj_strdup( pool, &dst->display, &src->display);
    dst->uri = pjsip_uri_clone(pool, src->uri);
}

static pjsip_name_addr* pjsip_name_addr_clone( pj_pool_t *pool, 
					       const pjsip_name_addr *rhs)
{
    pjsip_name_addr *addr = pj_pool_alloc(pool, sizeof(pjsip_name_addr));
    if (!addr)
	return NULL;

    pjsip_name_addr_init(addr);
    pjsip_name_addr_assign(pool, addr, rhs);
    return addr;
}

static int pjsip_name_addr_compare(  pjsip_uri_context_e context,
				     const pjsip_name_addr *naddr1,
				     const pjsip_name_addr *naddr2)
{
    int d;

    /* I'm not sure whether display name is included in the comparison. */
    if (pj_strcmp(&naddr1->display, &naddr2->display) != 0) {
	return -1;
    }

    pj_assert( naddr1->uri != NULL );
    pj_assert( naddr2->uri != NULL );

    /* Compare name-addr as URL */
    d = pjsip_uri_cmp( context, naddr1->uri, naddr2->uri);
    if (d)
	return d;

    return 0;
}

