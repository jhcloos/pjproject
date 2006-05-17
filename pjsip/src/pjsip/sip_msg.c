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
#include <pjsip/sip_msg.h>
#include <pjsip/sip_parser.h>
#include <pjsip/print_util.h>
#include <pjsip/sip_errno.h>
#include <pj/string.h>
#include <pj/pool.h>
#include <pj/assert.h>

const pjsip_method 
    pjsip_invite_method	    = { PJSIP_INVITE_METHOD,	{ "INVITE",6 }	},
    pjsip_cancel_method	    = { PJSIP_CANCEL_METHOD,	{ "CANCEL",6 }	},
    pjsip_ack_method	    = { PJSIP_ACK_METHOD,	{ "ACK",3}	},
    pjsip_bye_method	    = { PJSIP_BYE_METHOD,	{ "BYE",3}	},
    pjsip_register_method   = { PJSIP_REGISTER_METHOD,	{ "REGISTER",8}	},
    pjsip_options_method    = { PJSIP_OPTIONS_METHOD,	{ "OPTIONS",7}	};

static const pj_str_t *method_names[] = 
{
    &pjsip_invite_method.name,
    &pjsip_cancel_method.name,
    &pjsip_ack_method.name,
    &pjsip_bye_method.name,
    &pjsip_register_method.name,
    &pjsip_options_method.name
};

const pj_str_t pjsip_hdr_names[] = 
{
    { "Accept",		     6 },   // PJSIP_H_ACCEPT,
    { "Accept-Encoding",    15 },   // PJSIP_H_ACCEPT_ENCODING,
    { "Accept-Language",    15 },   // PJSIP_H_ACCEPT_LANGUAGE,
    { "Alert-Info",	    10 },   // PJSIP_H_ALERT_INFO,
    { "Allow",		     5 },   // PJSIP_H_ALLOW,
    { "Authentication-Info",19 },   // PJSIP_H_AUTHENTICATION_INFO,
    { "Authorization",	    13 },   // PJSIP_H_AUTHORIZATION,
    { "Call-ID",	     7 },   // PJSIP_H_CALL_ID,
    { "Call-Info",	     9 },   // PJSIP_H_CALL_INFO,
    { "Contact",	     7 },   // PJSIP_H_CONTACT,
    { "Content-Disposition",19 },   // PJSIP_H_CONTENT_DISPOSITION,
    { "Content-Encoding",   16 },   // PJSIP_H_CONTENT_ENCODING,
    { "Content-Language",   16 },   // PJSIP_H_CONTENT_LANGUAGE,
    { "Content-Length",	    14 },   // PJSIP_H_CONTENT_LENGTH,
    { "Content-Type",	    12 },   // PJSIP_H_CONTENT_TYPE,
    { "CSeq",		     4 },   // PJSIP_H_CSEQ,
    { "Date",		     4 },   // PJSIP_H_DATE,
    { "Error-Info",	    10 },   // PJSIP_H_ERROR_INFO,
    { "Expires",	     7 },   // PJSIP_H_EXPIRES,
    { "From",		     4 },   // PJSIP_H_FROM,
    { "In-Reply-To",	    11 },   // PJSIP_H_IN_REPLY_TO,
    { "Max-Forwards",	    12 },   // PJSIP_H_MAX_FORWARDS,
    { "MIME-Version",	    12 },   // PJSIP_H_MIME_VERSION,
    { "Min-Expires",	    11 },   // PJSIP_H_MIN_EXPIRES,
    { "Organization",	    12 },   // PJSIP_H_ORGANIZATION,
    { "Priority",	     8 },   // PJSIP_H_PRIORITY,
    { "Proxy-Authenticate", 18 },   // PJSIP_H_PROXY_AUTHENTICATE,
    { "Proxy-Authorization",19 },   // PJSIP_H_PROXY_AUTHORIZATION,
    { "Proxy-Require",	    13 },   // PJSIP_H_PROXY_REQUIRE,
    { "Record-Route",	    12 },   // PJSIP_H_RECORD_ROUTE,
    { "Reply-To",	     8 },   // PJSIP_H_REPLY_TO,
    { "Require",	     7 },   // PJSIP_H_REQUIRE,
    { "Retry-After",	    11 },   // PJSIP_H_RETRY_AFTER,
    { "Route",		     5 },   // PJSIP_H_ROUTE,
    { "Server",		     6 },   // PJSIP_H_SERVER,
    { "Subject",	     7 },   // PJSIP_H_SUBJECT,
    { "Supported",	     9 },   // PJSIP_H_SUPPORTED,
    { "Timestamp",	     9 },   // PJSIP_H_TIMESTAMP,
    { "To",		     2 },   // PJSIP_H_TO,
    { "Unsupported",	    11 },   // PJSIP_H_UNSUPPORTED,
    { "User-Agent",	    10 },   // PJSIP_H_USER_AGENT,
    { "Via",		     3 },   // PJSIP_H_VIA,
    { "Warning",	     7 },   // PJSIP_H_WARNING,
    { "WWW-Authenticate",   16 },   // PJSIP_H_WWW_AUTHENTICATE,

    { "_Unknown-Header",    15 },   // PJSIP_H_OTHER,
};

static pj_str_t status_phrase[710];
static int print_media_type(char *buf, const pjsip_media_type *media);

static int init_status_phrase()
{
    int i;
    pj_str_t default_reason_phrase = { "Default status message", 22};

    for (i=0; i<PJ_ARRAY_SIZE(status_phrase); ++i)
	status_phrase[i] = default_reason_phrase;

    pj_strset2( &status_phrase[100], "Trying");
    pj_strset2( &status_phrase[180], "Ringing");
    pj_strset2( &status_phrase[181], "Call Is Being Forwarded");
    pj_strset2( &status_phrase[182], "Queued");
    pj_strset2( &status_phrase[183], "Session Progress");

    pj_strset2( &status_phrase[200], "OK");
    pj_strset2( &status_phrase[202], "Accepted");

    pj_strset2( &status_phrase[300], "Multiple Choices");
    pj_strset2( &status_phrase[301], "Moved Permanently");
    pj_strset2( &status_phrase[302], "Moved Temporarily");
    pj_strset2( &status_phrase[305], "Use Proxy");
    pj_strset2( &status_phrase[380], "Alternative Service");

    pj_strset2( &status_phrase[400], "Bad Request");
    pj_strset2( &status_phrase[401], "Unauthorized");
    pj_strset2( &status_phrase[402], "Payment Required");
    pj_strset2( &status_phrase[403], "Forbidden");
    pj_strset2( &status_phrase[404], "Not Found");
    pj_strset2( &status_phrase[405], "Method Not Allowed");
    pj_strset2( &status_phrase[406], "Not Acceptable");
    pj_strset2( &status_phrase[407], "Proxy Authentication Required");
    pj_strset2( &status_phrase[408], "Request Timeout");
    pj_strset2( &status_phrase[410], "Gone");
    pj_strset2( &status_phrase[413], "Request Entity Too Large");
    pj_strset2( &status_phrase[414], "Request URI Too Long");
    pj_strset2( &status_phrase[415], "Unsupported Media Type");
    pj_strset2( &status_phrase[416], "Unsupported URI Scheme");
    pj_strset2( &status_phrase[420], "Bad Extension");
    pj_strset2( &status_phrase[421], "Extension Required");
    pj_strset2( &status_phrase[422], "Session Timer Too Small");
    pj_strset2( &status_phrase[423], "Interval Too Brief");
    pj_strset2( &status_phrase[480], "Temporarily Unavailable");
    pj_strset2( &status_phrase[481], "Call/Transaction Does Not Exist");
    pj_strset2( &status_phrase[482], "Loop Detected");
    pj_strset2( &status_phrase[483], "Too Many Hops");
    pj_strset2( &status_phrase[484], "Address Incompleted");
    pj_strset2( &status_phrase[485], "Ambiguous");
    pj_strset2( &status_phrase[486], "Busy Here");
    pj_strset2( &status_phrase[487], "Request Terminated");
    pj_strset2( &status_phrase[488], "Not Acceptable Here");
    pj_strset2( &status_phrase[489], "Bad Event");
    pj_strset2( &status_phrase[490], "Request Updated");
    pj_strset2( &status_phrase[491], "Request Pending");
    pj_strset2( &status_phrase[493], "Undecipherable");

    pj_strset2( &status_phrase[500], "Internal Server Error");
    pj_strset2( &status_phrase[501], "Not Implemented");
    pj_strset2( &status_phrase[502], "Bad Gateway");
    pj_strset2( &status_phrase[503], "Service Unavailable");
    pj_strset2( &status_phrase[504], "Server Timeout");
    pj_strset2( &status_phrase[505], "Version Not Supported");
    pj_strset2( &status_phrase[513], "Message Too Large");
    pj_strset2( &status_phrase[580], "Precondition Failure");

    pj_strset2( &status_phrase[600], "Busy Everywhere");
    pj_strset2( &status_phrase[603], "Decline");
    pj_strset2( &status_phrase[604], "Does Not Exist Anywhere");
    pj_strset2( &status_phrase[606], "Not Acceptable");

    pj_strset2( &status_phrase[701], "No response from destination server");
    pj_strset2( &status_phrase[702], "Unable to resolve destination server");
    pj_strset2( &status_phrase[703], "Error sending message to destination server");

    return 1;
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Method.
 */

PJ_DEF(void) pjsip_method_init( pjsip_method *m, 
			        pj_pool_t *pool, 
			        const pj_str_t *str)
{
    pj_str_t dup;
    pjsip_method_init_np(m, pj_strdup(pool, &dup, str));
}

PJ_DEF(void) pjsip_method_set( pjsip_method *m, pjsip_method_e me )
{
    pj_assert(me < PJSIP_OTHER_METHOD);
    m->id = me;
    m->name = *method_names[me];
}

PJ_DEF(void) pjsip_method_init_np(pjsip_method *m,
				  pj_str_t *str)
{
    int i;
    for (i=0; i<PJ_ARRAY_SIZE(method_names); ++i) {
	if (pj_stricmp(str, method_names[i])==0) {
	    m->id = (pjsip_method_e)i;
	    m->name = *method_names[i];
	    return;
	}
    }
    m->id = PJSIP_OTHER_METHOD;
    m->name = *str;
}

PJ_DEF(void) pjsip_method_copy( pj_pool_t *pool,
				pjsip_method *method,
				const pjsip_method *rhs )
{
    method->id = rhs->id;
    if (rhs->id != PJSIP_OTHER_METHOD) {
	method->name = rhs->name;
    } else {
	pj_strdup(pool, &method->name, &rhs->name);
    }
}


PJ_DEF(int) pjsip_method_cmp( const pjsip_method *m1, const pjsip_method *m2)
{
    if (m1->id == m2->id) {
	if (m1->id != PJSIP_OTHER_METHOD)
	    return 0;
	return pj_stricmp(&m1->name, &m2->name);
    }
    
    return ( m1->id < m2->id ) ? -1 : 1;
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Message.
 */

PJ_DEF(pjsip_msg*) pjsip_msg_create( pj_pool_t *pool, pjsip_msg_type_e type)
{
    pjsip_msg *msg = pj_pool_alloc(pool, sizeof(pjsip_msg));
    pj_list_init(&msg->hdr);
    msg->type = type;
    msg->body = NULL;
    return msg;
}

PJ_DEF(void*)  pjsip_msg_find_hdr( const pjsip_msg *msg, 
				   pjsip_hdr_e hdr_type, const void *start)
{
    const pjsip_hdr *hdr=start, *end=&msg->hdr;

    if (hdr == NULL) {
	hdr = msg->hdr.next;
    }
    for (; hdr!=end; hdr = hdr->next) {
	if (hdr->type == hdr_type)
	    return (void*)hdr;
    }
    return NULL;
}

PJ_DEF(void*)  pjsip_msg_find_hdr_by_name( const pjsip_msg *msg, 
					   const pj_str_t *name, 
					   const void *start)
{
    const pjsip_hdr *hdr=start, *end=&msg->hdr;

    if (hdr == NULL) {
	hdr = msg->hdr.next;
    }
    for (; hdr!=end; hdr = hdr->next) {
	if (hdr->type < PJSIP_H_OTHER) {
	    if (pj_stricmp(&pjsip_hdr_names[hdr->type], name) == 0)
		return (void*)hdr;
	} else {
	    if (pj_stricmp(&hdr->name, name) == 0)
		return (void*)hdr;
	}
    }
    return NULL;
}

PJ_DEF(void*) pjsip_msg_find_remove_hdr( pjsip_msg *msg, 
				         pjsip_hdr_e hdr_type, void *start)
{
    pjsip_hdr *hdr = pjsip_msg_find_hdr(msg, hdr_type, start);
    if (hdr) {
	pj_list_erase(hdr);
    }
    return hdr;
}

PJ_DEF(pj_ssize_t) pjsip_msg_print( const pjsip_msg *msg, 
				    char *buf, pj_size_t size)
{
    char *p=buf, *end=buf+size;
    int len;
    pjsip_hdr *hdr;
    pj_str_t clen_hdr =  { "Content-Length: ", 16};

    /* Get a wild guess on how many bytes are typically needed.
     * We'll check this later in detail, but this serves as a quick check.
     */
    if (size < 256)
	return -1;

    /* Print request line or status line depending on message type */
    if (msg->type == PJSIP_REQUEST_MSG) {
	pjsip_uri *uri;

	/* Add method. */
	len = msg->line.req.method.name.slen;
	pj_memcpy(p, msg->line.req.method.name.ptr, len);
	p += len;
	*p++ = ' ';

	/* Add URI */
	uri = pjsip_uri_get_uri(msg->line.req.uri);
	len = pjsip_uri_print( PJSIP_URI_IN_REQ_URI, uri, p, end-p);
	if (len < 1)
	    return -1;
	p += len;

	/* Add ' SIP/2.0' */
	if (end-p < 16)
	    return -1;
	pj_memcpy(p, " SIP/2.0\r\n", 10);
	p += 10;

    } else {

	/* Add 'SIP/2.0 ' */
	pj_memcpy(p, "SIP/2.0 ", 8);
	p += 8;

	/* Add status code. */
	len = pj_utoa(msg->line.status.code, p);
	p += len;
	*p++ = ' ';

	/* Add reason text. */
	len = msg->line.status.reason.slen;
	pj_memcpy(p, msg->line.status.reason.ptr, len );
	p += len;

	/* Add newline. */
	*p++ = '\r';
	*p++ = '\n';
    }

    /* Print each of the headers. */
    for (hdr=msg->hdr.next; hdr!=&msg->hdr; hdr=hdr->next) {
	len = (*hdr->vptr->print_on)(hdr, p, end-p);
	if (len < 1)
	    return -1;
	p += len;

	if (p+3 >= end)
	    return -1;

	*p++ = '\r';
	*p++ = '\n';
    }

    /* Process message body. */
    if (msg->body) {
	enum { CLEN_SPACE = 12 };
	char *clen_pos = NULL;

	/* Automaticly adds Content-Type and Content-Length headers, only
	 * if content_type is set in the message body.
	 */
	if (msg->body->content_type.type.slen) {
	    pj_str_t ctype_hdr = { "Content-Type: ", 14};
	    const pjsip_media_type *media = &msg->body->content_type;

	    /* Add Content-Type header. */
	    if ( (end-p) < 24 + media->type.slen + media->subtype.slen + 
			   media->param.slen) 
	    {
		return -1;
	    }
	    pj_memcpy(p, ctype_hdr.ptr, ctype_hdr.slen);
	    p += ctype_hdr.slen;
	    p += print_media_type(p, media);
	    *p++ = '\r';
	    *p++ = '\n';

	    /* Add Content-Length header. */
	    if ((end-p) < clen_hdr.slen + 12 + 2) {
		return -1;
	    }
	    pj_memcpy(p, clen_hdr.ptr, clen_hdr.slen);
	    p += clen_hdr.slen;
	    
	    /* Print blanks after "Content-Type:", this is where we'll put
	     * the content length value after we know the length of the
	     * body.
	     */
	    pj_memset(p, ' ', CLEN_SPACE);
	    clen_pos = p;
	    p += CLEN_SPACE;
	    *p++ = '\r';
	    *p++ = '\n';
	}
	
	/* Add blank newline. */
	*p++ = '\r';
	*p++ = '\n';

	/* Print the message body itself. */
	len = (*msg->body->print_body)(msg->body, p, end-p);
	if (len < 0) {
	    return -1;
	}
	p += len;

	/* Now that we have the length of the body, print this to the
	 * Content-Length header.
	 */
	if (clen_pos) {
	    char tmp[CLEN_SPACE];
	    len = pj_utoa(len, tmp);
	    pj_memcpy(clen_pos+CLEN_SPACE-len, tmp, len);
	}

    } else {
	/* There's no message body.
	 * Add Content-Length with zero value.
	 */
	if ((end-p) < clen_hdr.slen+8) {
	    return -1;
	}
	pj_memcpy(p, clen_hdr.ptr, clen_hdr.slen);
	p += clen_hdr.slen;
	*p++ = '0';
	*p++ = '\r';
	*p++ = '\n';
	*p++ = '\r';
	*p++ = '\n';
    }

    *p = '\0';
    return p-buf;
}

///////////////////////////////////////////////////////////////////////////////
PJ_DEF(void*) pjsip_hdr_clone( pj_pool_t *pool, const void *hdr_ptr )
{
    const pjsip_hdr *hdr = hdr_ptr;
    return (*hdr->vptr->clone)(pool, hdr_ptr);
}


PJ_DEF(void*) pjsip_hdr_shallow_clone( pj_pool_t *pool, const void *hdr_ptr )
{
    const pjsip_hdr *hdr = hdr_ptr;
    return (*hdr->vptr->shallow_clone)(pool, hdr_ptr);
}

PJ_DEF(int) pjsip_hdr_print_on( void *hdr_ptr, char *buf, pj_size_t len)
{
    pjsip_hdr *hdr = hdr_ptr;
    return (*hdr->vptr->print_on)(hdr_ptr, buf, len);
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Status/Reason Phrase
 */

PJ_DEF(const pj_str_t*) pjsip_get_status_text(int code)
{
    static int is_initialized;
    if (is_initialized == 0) {
	is_initialized = 1;
	init_status_phrase();
    }

    return (code>=100 && code<(sizeof(status_phrase)/sizeof(status_phrase[0]))) ? 
	&status_phrase[code] : &status_phrase[0];
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Generic pjsip_hdr_names/hvalue header.
 */

static int pjsip_generic_string_hdr_print( pjsip_generic_string_hdr *hdr, 
				    char *buf, pj_size_t size);
static pjsip_generic_string_hdr* pjsip_generic_string_hdr_clone( pj_pool_t *pool, 
						   const pjsip_generic_string_hdr *hdr);
static pjsip_generic_string_hdr* pjsip_generic_string_hdr_shallow_clone( pj_pool_t *pool,
							   const pjsip_generic_string_hdr *hdr );

static pjsip_hdr_vptr generic_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_generic_string_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_generic_string_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_generic_string_hdr_print,
};

PJ_DEF(pjsip_generic_string_hdr*) 
pjsip_generic_string_hdr_init( pj_pool_t *pool,
			       void *mem,
			       const pj_str_t *hnames,
			       const pj_str_t *hvalue)
{
    pjsip_generic_string_hdr *hdr = mem;

    init_hdr(hdr, PJSIP_H_OTHER, &generic_hdr_vptr);
    if (hnames) {
	pj_strdup(pool, &hdr->name, hnames);
	hdr->sname = hdr->name;
    }
    if (hvalue) {
	pj_strdup(pool, &hdr->hvalue, hvalue);
    } else {
	hdr->hvalue.ptr = NULL;
	hdr->hvalue.slen = 0;
    }

    return hdr;
}

PJ_DEF(pjsip_generic_string_hdr*) 
pjsip_generic_string_hdr_create( pj_pool_t *pool,
				 const pj_str_t *hnames,
				 const pj_str_t *hvalue)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_generic_string_hdr));
    return pjsip_generic_string_hdr_init(pool, mem, hnames, hvalue);
}

static int pjsip_generic_string_hdr_print( pjsip_generic_string_hdr *hdr,
					   char *buf, pj_size_t size)
{
    char *p = buf;
    
    if ((pj_ssize_t)size < hdr->name.slen + hdr->hvalue.slen + 5)
	return -1;

    pj_memcpy(p, hdr->name.ptr, hdr->name.slen);
    p += hdr->name.slen;
    *p++ = ':';
    *p++ = ' ';
    pj_memcpy(p, hdr->hvalue.ptr, hdr->hvalue.slen);
    p += hdr->hvalue.slen;
    *p = '\0';

    return p - buf;
}

static pjsip_generic_string_hdr* pjsip_generic_string_hdr_clone( pj_pool_t *pool, 
					           const pjsip_generic_string_hdr *rhs)
{
    pjsip_generic_string_hdr *hdr;
    
    hdr = pjsip_generic_string_hdr_create(pool, &rhs->name, &rhs->hvalue);

    hdr->type = rhs->type;
    hdr->sname = hdr->name;
    return hdr;
}

static pjsip_generic_string_hdr* pjsip_generic_string_hdr_shallow_clone( pj_pool_t *pool,
							   const pjsip_generic_string_hdr *rhs )
{
    pjsip_generic_string_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    return hdr;
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Generic pjsip_hdr_names/integer value header.
 */

static int pjsip_generic_int_hdr_print( pjsip_generic_int_hdr *hdr, 
					char *buf, pj_size_t size);
static pjsip_generic_int_hdr* pjsip_generic_int_hdr_clone( pj_pool_t *pool, 
						   const pjsip_generic_int_hdr *hdr);
static pjsip_generic_int_hdr* pjsip_generic_int_hdr_shallow_clone( pj_pool_t *pool,
							   const pjsip_generic_int_hdr *hdr );

static pjsip_hdr_vptr generic_int_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_generic_int_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_generic_int_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_generic_int_hdr_print,
};

PJ_DEF(pjsip_generic_int_hdr*) pjsip_generic_int_hdr_init(  pj_pool_t *pool,
							    void *mem,
							    const pj_str_t *hnames,
							    int value)
{
    pjsip_generic_int_hdr *hdr = mem;

    init_hdr(hdr, PJSIP_H_OTHER, &generic_int_hdr_vptr);
    if (hnames) {
	pj_strdup(pool, &hdr->name, hnames);
	hdr->sname = hdr->name;
    }
    hdr->ivalue = value;
    return hdr;
}

PJ_DEF(pjsip_generic_int_hdr*) pjsip_generic_int_hdr_create( pj_pool_t *pool,
						     const pj_str_t *hnames,
						     int value)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_generic_int_hdr));
    return pjsip_generic_int_hdr_init(pool, mem, hnames, value);
}

static int pjsip_generic_int_hdr_print( pjsip_generic_int_hdr *hdr, 
					char *buf, pj_size_t size)
{
    char *p = buf;

    if ((pj_ssize_t)size < hdr->name.slen + 15)
	return -1;

    pj_memcpy(p, hdr->name.ptr, hdr->name.slen);
    p += hdr->name.slen;
    *p++ = ':';
    *p++ = ' ';

    p += pj_utoa(hdr->ivalue, p);

    return p - buf;
}

static pjsip_generic_int_hdr* pjsip_generic_int_hdr_clone( pj_pool_t *pool, 
					           const pjsip_generic_int_hdr *rhs)
{
    pjsip_generic_int_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    return hdr;
}

static pjsip_generic_int_hdr* pjsip_generic_int_hdr_shallow_clone( pj_pool_t *pool,
							   const pjsip_generic_int_hdr *rhs )
{
    pjsip_generic_int_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    return hdr;
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Generic array header.
 */
static int pjsip_generic_array_hdr_print( pjsip_generic_array_hdr *hdr, char *buf, pj_size_t size);
static pjsip_generic_array_hdr* pjsip_generic_array_hdr_clone( pj_pool_t *pool, 
						 const pjsip_generic_array_hdr *hdr);
static pjsip_generic_array_hdr* pjsip_generic_array_hdr_shallow_clone( pj_pool_t *pool, 
						 const pjsip_generic_array_hdr *hdr);

static pjsip_hdr_vptr generic_array_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_generic_array_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_generic_array_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_generic_array_hdr_print,
};


PJ_DEF(pjsip_generic_array_hdr*) pjsip_generic_array_hdr_init( pj_pool_t *pool,
							       void *mem,
							       const pj_str_t *hnames)
{
    pjsip_generic_array_hdr *hdr = mem;

    init_hdr(hdr, PJSIP_H_OTHER, &generic_array_hdr_vptr);
    if (hnames) {
	pj_strdup(pool, &hdr->name, hnames);
	hdr->sname = hdr->name;
    }
    hdr->count = 0;
    return hdr;
}

PJ_DEF(pjsip_generic_array_hdr*) pjsip_generic_array_hdr_create( pj_pool_t *pool,
							     const pj_str_t *hnames)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_generic_array_hdr));
    return pjsip_generic_array_hdr_init(pool, mem, hnames);

}

static int pjsip_generic_array_hdr_print( pjsip_generic_array_hdr *hdr, 
					  char *buf, pj_size_t size)
{
    char *p = buf, *endbuf = buf+size;

    copy_advance(p, hdr->name);
    *p++ = ':';
    *p++ = ' ';

    if (hdr->count > 0) {
	unsigned i;
	int printed;
	copy_advance(p, hdr->values[0]);
	for (i=1; i<hdr->count; ++i) {
	    copy_advance_pair(p, ", ", 2, hdr->values[i]);
	}
    }

    return p - buf;
}

static pjsip_generic_array_hdr* pjsip_generic_array_hdr_clone( pj_pool_t *pool, 
						 const pjsip_generic_array_hdr *rhs)
{
    unsigned i;
    pjsip_generic_array_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));

    pj_memcpy(hdr, rhs, sizeof(*hdr));
    for (i=0; i<rhs->count; ++i) {
	pj_strdup(pool, &hdr->values[i], &rhs->values[i]);
    }

    return hdr;
}


static pjsip_generic_array_hdr* pjsip_generic_array_hdr_shallow_clone( pj_pool_t *pool, 
						 const pjsip_generic_array_hdr *rhs)
{
    pjsip_generic_array_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    return hdr;
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Accept header.
 */
PJ_DEF(pjsip_accept_hdr*) pjsip_accept_hdr_init( pj_pool_t *pool,
						 void *mem )
{
    pjsip_accept_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_ACCEPT, &generic_array_hdr_vptr);
    hdr->count = 0;
    return hdr;
}

PJ_DEF(pjsip_accept_hdr*) pjsip_accept_hdr_create(pj_pool_t *pool)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_accept_hdr));
    return pjsip_accept_hdr_init(pool, mem);
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Allow header.
 */

PJ_DEF(pjsip_allow_hdr*) pjsip_allow_hdr_init( pj_pool_t *pool,
					       void *mem )
{
    pjsip_allow_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_ALLOW, &generic_array_hdr_vptr);
    hdr->count = 0;
    return hdr;
}

PJ_DEF(pjsip_allow_hdr*) pjsip_allow_hdr_create(pj_pool_t *pool)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_allow_hdr));
    return pjsip_allow_hdr_init(pool, mem);
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Call-ID header.
 */

PJ_DEF(pjsip_cid_hdr*) pjsip_cid_hdr_init( pj_pool_t *pool,
					   void *mem )
{
    pjsip_cid_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_CALL_ID, &generic_hdr_vptr);
    return hdr;

}

PJ_DEF(pjsip_cid_hdr*) pjsip_cid_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_cid_hdr));
    return pjsip_cid_hdr_init(pool, mem);
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Content-Length header.
 */
static int pjsip_clen_hdr_print( pjsip_clen_hdr *hdr, char *buf, pj_size_t size);
static pjsip_clen_hdr* pjsip_clen_hdr_clone( pj_pool_t *pool, const pjsip_clen_hdr *hdr);
#define pjsip_clen_hdr_shallow_clone pjsip_clen_hdr_clone

static pjsip_hdr_vptr clen_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_clen_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_clen_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_clen_hdr_print,
};

PJ_DEF(pjsip_clen_hdr*) pjsip_clen_hdr_init( pj_pool_t *pool,
					     void *mem )
{
    pjsip_clen_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_CONTENT_LENGTH, &clen_hdr_vptr);
    hdr->len = 0;
    return hdr;
}

PJ_DEF(pjsip_clen_hdr*) pjsip_clen_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_clen_hdr));
    return pjsip_clen_hdr_init(pool, mem);
}

static int pjsip_clen_hdr_print( pjsip_clen_hdr *hdr, 
				 char *buf, pj_size_t size)
{
    char *p = buf;
    int len;

    if ((pj_ssize_t)size < hdr->name.slen + 14)
	return -1;

    pj_memcpy(p, hdr->name.ptr, hdr->name.slen);
    p += hdr->name.slen;
    *p++ = ':';
    *p++ = ' ';

    len = pj_utoa(hdr->len, p);
    p += len;
    *p = '\0';

    return p-buf;
}

static pjsip_clen_hdr* pjsip_clen_hdr_clone( pj_pool_t *pool, const pjsip_clen_hdr *rhs)
{
    pjsip_clen_hdr *hdr = pjsip_clen_hdr_create(pool);
    hdr->len = rhs->len;
    return hdr;
}


///////////////////////////////////////////////////////////////////////////////
/*
 * CSeq header.
 */
static int pjsip_cseq_hdr_print( pjsip_cseq_hdr *hdr, char *buf, pj_size_t size);
static pjsip_cseq_hdr* pjsip_cseq_hdr_clone( pj_pool_t *pool, const pjsip_cseq_hdr *hdr);
static pjsip_cseq_hdr* pjsip_cseq_hdr_shallow_clone( pj_pool_t *pool, const pjsip_cseq_hdr *hdr );

static pjsip_hdr_vptr cseq_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_cseq_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_cseq_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_cseq_hdr_print,
};

PJ_DEF(pjsip_cseq_hdr*) pjsip_cseq_hdr_init( pj_pool_t *pool,
					     void *mem )
{
    pjsip_cseq_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_CSEQ, &cseq_hdr_vptr);
    hdr->cseq = 0;
    hdr->method.id = PJSIP_OTHER_METHOD;
    hdr->method.name.ptr = NULL;
    hdr->method.name.slen = 0;
    return hdr;
}

PJ_DEF(pjsip_cseq_hdr*) pjsip_cseq_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_cseq_hdr));
    return pjsip_cseq_hdr_init(pool, mem);
}

static int pjsip_cseq_hdr_print( pjsip_cseq_hdr *hdr, char *buf, pj_size_t size)
{
    char *p = buf;
    int len;

    if ((pj_ssize_t)size < hdr->name.slen + hdr->method.name.slen + 15)
	return -1;

    pj_memcpy(p, hdr->name.ptr, hdr->name.slen);
    p += hdr->name.slen;
    *p++ = ':';
    *p++ = ' ';

    len = pj_utoa(hdr->cseq, p);
    p += len;
    *p++ = ' ';

    pj_memcpy(p, hdr->method.name.ptr, hdr->method.name.slen);
    p += hdr->method.name.slen;

    *p = '\0';

    return p-buf;
}

static pjsip_cseq_hdr* pjsip_cseq_hdr_clone( pj_pool_t *pool, 
					     const pjsip_cseq_hdr *rhs)
{
    pjsip_cseq_hdr *hdr = pjsip_cseq_hdr_create(pool);
    hdr->cseq = rhs->cseq;
    pjsip_method_copy(pool, &hdr->method, &rhs->method);
    return hdr;
}

static pjsip_cseq_hdr* pjsip_cseq_hdr_shallow_clone( pj_pool_t *pool,
						     const pjsip_cseq_hdr *rhs )
{
    pjsip_cseq_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    return hdr;
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Contact header.
 */
static int pjsip_contact_hdr_print( pjsip_contact_hdr *hdr, char *buf, pj_size_t size);
static pjsip_contact_hdr* pjsip_contact_hdr_clone( pj_pool_t *pool, const pjsip_contact_hdr *hdr);
static pjsip_contact_hdr* pjsip_contact_hdr_shallow_clone( pj_pool_t *pool, const pjsip_contact_hdr *);

static pjsip_hdr_vptr contact_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_contact_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_contact_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_contact_hdr_print,
};

PJ_DEF(pjsip_contact_hdr*) pjsip_contact_hdr_init( pj_pool_t *pool,
						   void *mem )
{
    pjsip_contact_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    pj_memset(mem, 0, sizeof(pjsip_contact_hdr));
    init_hdr(hdr, PJSIP_H_CONTACT, &contact_hdr_vptr);
    hdr->expires = -1;
    pj_list_init(&hdr->other_param);
    return hdr;
}

PJ_DEF(pjsip_contact_hdr*) pjsip_contact_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_contact_hdr));
    return pjsip_contact_hdr_init(pool, mem);
}

static int pjsip_contact_hdr_print( pjsip_contact_hdr *hdr, char *buf, 
				    pj_size_t size)
{
    if (hdr->star) {
	char *p = buf;
	if ((pj_ssize_t)size < hdr->name.slen + 6)
	    return -1;
	pj_memcpy(p, hdr->name.ptr, hdr->name.slen);
	p += hdr->name.slen;
	*p++ = ':';
	*p++ = ' ';
	*p++ = '*';
	return p - buf;

    } else {
	int printed;
	char *startbuf = buf;
	char *endbuf = buf + size;

	copy_advance(buf, hdr->name);
	*buf++ = ':';
	*buf++ = ' ';

	printed = pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, hdr->uri, 
				  buf, endbuf-buf);
	if (printed < 1)
	    return -1;

	buf += printed;

	if (hdr->q1000) {
	    if (buf+19 >= endbuf)
		return -1;

	    /*
	    printed = sprintf(buf, ";q=%u.%03u",
				   hdr->q1000/1000, hdr->q1000 % 1000);
	     */
	    pj_memcpy(buf, ";q=", 3);
	    printed = pj_utoa(hdr->q1000/1000, buf+3);
	    buf += printed + 3;
	    *buf++ = '.';
	    printed = pj_utoa(hdr->q1000 % 1000, buf);
	    buf += printed;
	}

	if (hdr->expires >= 0) {
	    if (buf+23 >= endbuf)
		return -1;

	    pj_memcpy(buf, ";expires=", 9);
	    printed = pj_utoa(hdr->expires, buf+9);
	    buf += printed + 9;
	}

	printed = pjsip_param_print_on(&hdr->other_param, buf, endbuf-buf,
				       &pjsip_PARAM_CHAR_SPEC,
				       &pjsip_PARAM_CHAR_SPEC, ';');
	if (printed < 0)
	    return printed;
	buf += printed;

	return buf-startbuf;
    }
}

static pjsip_contact_hdr* pjsip_contact_hdr_clone(pj_pool_t *pool, 
					          const pjsip_contact_hdr *rhs)
{
    pjsip_contact_hdr *hdr = pjsip_contact_hdr_create(pool);

    hdr->star = rhs->star;
    if (hdr->star)
	return hdr;

    hdr->uri = pjsip_uri_clone(pool, rhs->uri);
    hdr->q1000 = rhs->q1000;
    hdr->expires = rhs->expires;
    pjsip_param_clone(pool, &hdr->other_param, &rhs->other_param);
    return hdr;
}

static pjsip_contact_hdr* 
pjsip_contact_hdr_shallow_clone( pj_pool_t *pool,
				 const pjsip_contact_hdr *rhs)
{
    pjsip_contact_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    pjsip_param_shallow_clone(pool, &hdr->other_param, &rhs->other_param);
    return hdr;
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Content-Type header..
 */
static int pjsip_ctype_hdr_print( pjsip_ctype_hdr *hdr, char *buf, 
				  pj_size_t size);
static pjsip_ctype_hdr* pjsip_ctype_hdr_clone(pj_pool_t *pool, 
					      const pjsip_ctype_hdr *hdr);
#define pjsip_ctype_hdr_shallow_clone pjsip_ctype_hdr_clone

static pjsip_hdr_vptr ctype_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_ctype_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_ctype_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_ctype_hdr_print,
};

PJ_DEF(pjsip_ctype_hdr*) pjsip_ctype_hdr_init( pj_pool_t *pool,
					       void *mem )
{
    pjsip_ctype_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    pj_memset(mem, 0, sizeof(pjsip_ctype_hdr));
    init_hdr(hdr, PJSIP_H_CONTENT_TYPE, &ctype_hdr_vptr);
    return hdr;

}

PJ_DEF(pjsip_ctype_hdr*) pjsip_ctype_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_ctype_hdr));
    return pjsip_ctype_hdr_init(pool, mem);
}

static int print_media_type(char *buf, const pjsip_media_type *media)
{
    char *p = buf;

    pj_memcpy(p, media->type.ptr, media->type.slen);
    p += media->type.slen;
    *p++ = '/';
    pj_memcpy(p, media->subtype.ptr, media->subtype.slen);
    p += media->subtype.slen;

    if (media->param.slen) {
	pj_memcpy(p, media->param.ptr, media->param.slen);
	p += media->param.slen;
    }

    return p-buf;
}

static int pjsip_ctype_hdr_print( pjsip_ctype_hdr *hdr, 
				  char *buf, pj_size_t size)
{
    char *p = buf;
    int len;

    if ((pj_ssize_t)size < hdr->name.slen + 
			   hdr->media.type.slen + hdr->media.subtype.slen + 
			   hdr->media.param.slen + 8)
    {
	return -1;
    }

    pj_memcpy(p, hdr->name.ptr, hdr->name.slen);
    p += hdr->name.slen;
    *p++ = ':';
    *p++ = ' ';

    len = print_media_type(p, &hdr->media);
    p += len;

    *p = '\0';
    return p-buf;
}

static pjsip_ctype_hdr* pjsip_ctype_hdr_clone( pj_pool_t *pool, 
					       const pjsip_ctype_hdr *rhs)
{
    pjsip_ctype_hdr *hdr = pjsip_ctype_hdr_create(pool);
    pj_strdup(pool, &hdr->media.type, &rhs->media.type);
    pj_strdup(pool, &hdr->media.param, &rhs->media.param);
    return hdr;
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Expires header.
 */
PJ_DEF(pjsip_expires_hdr*) pjsip_expires_hdr_init( pj_pool_t *pool,
						   void *mem,
						   int value)
{
    pjsip_expires_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_EXPIRES, &generic_int_hdr_vptr);
    hdr->ivalue = value;
    return hdr;

}

PJ_DEF(pjsip_expires_hdr*) pjsip_expires_hdr_create( pj_pool_t *pool,
						     int value )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_expires_hdr));
    return pjsip_expires_hdr_init(pool, mem, value);
}

///////////////////////////////////////////////////////////////////////////////
/*
 * To or From header.
 */
static int pjsip_fromto_hdr_print( pjsip_fromto_hdr *hdr, 
				   char *buf, pj_size_t size);
static pjsip_fromto_hdr* pjsip_fromto_hdr_clone( pj_pool_t *pool, 
					         const pjsip_fromto_hdr *hdr);
static pjsip_fromto_hdr* pjsip_fromto_hdr_shallow_clone( pj_pool_t *pool,
							 const pjsip_fromto_hdr *hdr);


static pjsip_hdr_vptr fromto_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_fromto_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_fromto_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_fromto_hdr_print,
};

PJ_DEF(pjsip_from_hdr*) pjsip_from_hdr_init( pj_pool_t *pool,
					     void *mem )
{
    pjsip_from_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    pj_memset(mem, 0, sizeof(pjsip_from_hdr));
    init_hdr(hdr, PJSIP_H_FROM, &fromto_hdr_vptr);
    pj_list_init(&hdr->other_param);
    return hdr;
}

PJ_DEF(pjsip_from_hdr*) pjsip_from_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_from_hdr));
    return pjsip_from_hdr_init(pool, mem);
}

PJ_DEF(pjsip_to_hdr*) pjsip_to_hdr_init( pj_pool_t *pool,
					 void *mem )
{
    pjsip_to_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    pj_memset(mem, 0, sizeof(pjsip_to_hdr));
    init_hdr(hdr, PJSIP_H_TO, &fromto_hdr_vptr);
    pj_list_init(&hdr->other_param);
    return hdr;

}

PJ_DEF(pjsip_to_hdr*) pjsip_to_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_to_hdr));
    return pjsip_to_hdr_init(pool, mem);
}

PJ_DEF(pjsip_from_hdr*) pjsip_fromto_hdr_set_from( pjsip_fromto_hdr *hdr )
{
    hdr->type = PJSIP_H_FROM;
    hdr->name = hdr->sname = pjsip_hdr_names[PJSIP_H_FROM];
    return hdr;
}

PJ_DEF(pjsip_to_hdr*) pjsip_fromto_hdr_set_to( pjsip_fromto_hdr *hdr )
{
    hdr->type = PJSIP_H_TO;
    hdr->name = hdr->sname = pjsip_hdr_names[PJSIP_H_TO];
    return hdr;
}

static int pjsip_fromto_hdr_print( pjsip_fromto_hdr *hdr, 
				   char *buf, pj_size_t size)
{
    int printed;
    char *startbuf = buf;
    char *endbuf = buf + size;

    copy_advance(buf, hdr->name);
    *buf++ = ':';
    *buf++ = ' ';

    printed = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, hdr->uri, 
			      buf, endbuf-buf);
    if (printed < 1)
	return -1;

    buf += printed;

    copy_advance_pair(buf, ";tag=", 5, hdr->tag);

    printed = pjsip_param_print_on(&hdr->other_param, buf, endbuf-buf, 
				   &pjsip_PARAM_CHAR_SPEC,
				   &pjsip_PARAM_CHAR_SPEC, ';');
    if (printed < 0)
	return -1;
    buf += printed;

    return buf-startbuf;
}

static pjsip_fromto_hdr* pjsip_fromto_hdr_clone( pj_pool_t *pool, 
					         const pjsip_fromto_hdr *rhs)
{
    pjsip_fromto_hdr *hdr = pjsip_from_hdr_create(pool);

    hdr->type = rhs->type;
    hdr->name = rhs->name;
    hdr->sname = rhs->sname;
    hdr->uri = pjsip_uri_clone(pool, rhs->uri);
    pj_strdup( pool, &hdr->tag, &rhs->tag);
    pjsip_param_clone( pool, &hdr->other_param, &rhs->other_param);

    return hdr;
}

static pjsip_fromto_hdr* 
pjsip_fromto_hdr_shallow_clone( pj_pool_t *pool,
				const pjsip_fromto_hdr *rhs)
{
    pjsip_fromto_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    pjsip_param_shallow_clone( pool, &hdr->other_param, &rhs->other_param);
    return hdr;
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Max-Forwards header.
 */
PJ_DEF(pjsip_max_fwd_hdr*) pjsip_max_fwd_hdr_init( pj_pool_t *pool,
						   void *mem,
						   int value)
{
    pjsip_max_fwd_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_MAX_FORWARDS, &generic_int_hdr_vptr);
    hdr->ivalue = value;
    return hdr;

}

PJ_DEF(pjsip_max_fwd_hdr*) pjsip_max_fwd_hdr_create(pj_pool_t *pool,
						    int value)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_max_fwd_hdr));
    return pjsip_max_fwd_hdr_init(pool, mem, value);
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Min-Expires header.
 */
PJ_DEF(pjsip_min_expires_hdr*) pjsip_min_expires_hdr_init( pj_pool_t *pool,
							   void *mem,
							   int value )
{
    pjsip_min_expires_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_MIN_EXPIRES, &generic_int_hdr_vptr);
    hdr->ivalue = value;
    return hdr;
}

PJ_DEF(pjsip_min_expires_hdr*) pjsip_min_expires_hdr_create(pj_pool_t *pool,
							    int value )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_min_expires_hdr));
    return pjsip_min_expires_hdr_init(pool, mem, value );
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Record-Route and Route header.
 */
static int pjsip_routing_hdr_print( pjsip_routing_hdr *r, char *buf, pj_size_t size );
static pjsip_routing_hdr* pjsip_routing_hdr_clone( pj_pool_t *pool, const pjsip_routing_hdr *r );
static pjsip_routing_hdr* pjsip_routing_hdr_shallow_clone( pj_pool_t *pool, const pjsip_routing_hdr *r );

static pjsip_hdr_vptr routing_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_routing_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_routing_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_routing_hdr_print,
};

PJ_DEF(pjsip_rr_hdr*) pjsip_rr_hdr_init( pj_pool_t *pool,
					 void *mem )
{
    pjsip_rr_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_RECORD_ROUTE, &routing_hdr_vptr);
    pjsip_name_addr_init(&hdr->name_addr);
    pj_list_init(&hdr->other_param);
    return hdr;

}

PJ_DEF(pjsip_rr_hdr*) pjsip_rr_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_rr_hdr));
    return pjsip_rr_hdr_init(pool, mem);
}

PJ_DEF(pjsip_route_hdr*) pjsip_route_hdr_init( pj_pool_t *pool,
					       void *mem )
{
    pjsip_route_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_ROUTE, &routing_hdr_vptr);
    pjsip_name_addr_init(&hdr->name_addr);
    pj_list_init(&hdr->other_param);
    return hdr;
}

PJ_DEF(pjsip_route_hdr*) pjsip_route_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_route_hdr));
    return pjsip_route_hdr_init(pool, mem);
}

PJ_DEF(pjsip_rr_hdr*) pjsip_routing_hdr_set_rr( pjsip_routing_hdr *hdr )
{
    hdr->type = PJSIP_H_RECORD_ROUTE;
    hdr->name = hdr->sname = pjsip_hdr_names[PJSIP_H_RECORD_ROUTE];
    return hdr;
}

PJ_DEF(pjsip_route_hdr*) pjsip_routing_hdr_set_route( pjsip_routing_hdr *hdr )
{
    hdr->type = PJSIP_H_ROUTE;
    hdr->name = hdr->sname = pjsip_hdr_names[PJSIP_H_ROUTE];
    return hdr;
}

static int pjsip_routing_hdr_print( pjsip_routing_hdr *hdr,
				    char *buf, pj_size_t size )
{
    int printed;
    char *startbuf = buf;
    char *endbuf = buf + size;

    copy_advance(buf, hdr->name);
    *buf++ = ':';
    *buf++ = ' ';

    printed = pjsip_uri_print(PJSIP_URI_IN_ROUTING_HDR, &hdr->name_addr, buf, 
			      endbuf-buf);
    if (printed < 1)
	return -1;
    buf += printed;

    printed = pjsip_param_print_on(&hdr->other_param, buf, endbuf-buf, 
				   &pjsip_PARAM_CHAR_SPEC, 
				   &pjsip_PARAM_CHAR_SPEC, ';');
    if (printed < 0)
	return -1;
    buf += printed;

    return buf-startbuf;
}

static pjsip_routing_hdr* pjsip_routing_hdr_clone( pj_pool_t *pool,
						   const pjsip_routing_hdr *rhs )
{
    pjsip_routing_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));

    init_hdr(hdr, rhs->type, rhs->vptr);
    pjsip_name_addr_init(&hdr->name_addr);
    pjsip_name_addr_assign(pool, &hdr->name_addr, &rhs->name_addr);
    pjsip_param_clone( pool, &hdr->other_param, &rhs->other_param);
    return hdr;
}

static pjsip_routing_hdr* pjsip_routing_hdr_shallow_clone( pj_pool_t *pool,
							   const pjsip_routing_hdr *rhs )
{
    pjsip_routing_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    pjsip_param_shallow_clone( pool, &hdr->other_param, &rhs->other_param);
    return hdr;
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Require header.
 */
PJ_DEF(pjsip_require_hdr*) pjsip_require_hdr_init( pj_pool_t *pool,
						   void *mem )
{
    pjsip_require_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_REQUIRE, &generic_array_hdr_vptr);
    hdr->count = 0;
    return hdr;
}

PJ_DEF(pjsip_require_hdr*) pjsip_require_hdr_create(pj_pool_t *pool)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_require_hdr));
    return pjsip_require_hdr_init(pool, mem);
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Retry-After header.
 */
PJ_DEF(pjsip_retry_after_hdr*) pjsip_retry_after_hdr_init( pj_pool_t *pool,
							   void *mem,
							   int value )
{
    pjsip_retry_after_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_RETRY_AFTER, &generic_int_hdr_vptr);
    hdr->ivalue = value;
    return hdr;
}

PJ_DEF(pjsip_retry_after_hdr*) pjsip_retry_after_hdr_create(pj_pool_t *pool,
							    int value )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_retry_after_hdr));
    return pjsip_retry_after_hdr_init(pool, mem, value );
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Supported header.
 */
PJ_DEF(pjsip_supported_hdr*) pjsip_supported_hdr_init( pj_pool_t *pool,
						       void *mem )
{
    pjsip_supported_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);
    init_hdr(hdr, PJSIP_H_SUPPORTED, &generic_array_hdr_vptr);
    hdr->count = 0;
    return hdr;
}

PJ_DEF(pjsip_supported_hdr*) pjsip_supported_hdr_create(pj_pool_t *pool)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_supported_hdr));
    return pjsip_supported_hdr_init(pool, mem);
}


///////////////////////////////////////////////////////////////////////////////
/*
 * Unsupported header.
 */
PJ_DEF(pjsip_unsupported_hdr*) pjsip_unsupported_hdr_init( pj_pool_t *pool,
							   void *mem )
{
    pjsip_unsupported_hdr *hdr = mem;
    
    PJ_UNUSED_ARG(pool);

    init_hdr(hdr, PJSIP_H_UNSUPPORTED, &generic_array_hdr_vptr);
    hdr->count = 0;
    return hdr;
}

PJ_DEF(pjsip_unsupported_hdr*) pjsip_unsupported_hdr_create(pj_pool_t *pool)
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_unsupported_hdr));
    return pjsip_unsupported_hdr_init(pool, mem);
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Via header.
 */
static int pjsip_via_hdr_print( pjsip_via_hdr *hdr, char *buf, pj_size_t size);
static pjsip_via_hdr* pjsip_via_hdr_clone( pj_pool_t *pool, const pjsip_via_hdr *hdr);
static pjsip_via_hdr* pjsip_via_hdr_shallow_clone( pj_pool_t *pool, const pjsip_via_hdr *hdr );

static pjsip_hdr_vptr via_hdr_vptr = 
{
    (pjsip_hdr_clone_fptr) &pjsip_via_hdr_clone,
    (pjsip_hdr_clone_fptr) &pjsip_via_hdr_shallow_clone,
    (pjsip_hdr_print_fptr) &pjsip_via_hdr_print,
};

PJ_DEF(pjsip_via_hdr*) pjsip_via_hdr_init( pj_pool_t *pool,
					   void *mem )
{
    pjsip_via_hdr *hdr = mem;

    PJ_UNUSED_ARG(pool);

    pj_memset(mem, 0, sizeof(pjsip_via_hdr));
    init_hdr(hdr, PJSIP_H_VIA, &via_hdr_vptr);
    hdr->ttl_param = -1;
    hdr->rport_param = -1;
    pj_list_init(&hdr->other_param);
    return hdr;

}

PJ_DEF(pjsip_via_hdr*) pjsip_via_hdr_create( pj_pool_t *pool )
{
    void *mem = pj_pool_alloc(pool, sizeof(pjsip_via_hdr));
    return pjsip_via_hdr_init(pool, mem);
}

static int pjsip_via_hdr_print( pjsip_via_hdr *hdr, 
				char *buf, pj_size_t size)
{
    int printed;
    char *startbuf = buf;
    char *endbuf = buf + size;
    pj_str_t sip_ver = { "SIP/2.0/", 8 };

    if ((pj_ssize_t)size < hdr->name.slen + sip_ver.slen + 
			   hdr->transport.slen + hdr->sent_by.host.slen + 12)
    {
	return -1;
    }

    /* pjsip_hdr_names */
    copy_advance(buf, hdr->name);
    *buf++ = ':';
    *buf++ = ' ';

    /* SIP/2.0/transport host:port */
    pj_memcpy(buf, sip_ver.ptr, sip_ver.slen);
    buf += sip_ver.slen;
    pj_memcpy(buf, hdr->transport.ptr, hdr->transport.slen);
    buf += hdr->transport.slen;
    *buf++ = ' ';
    pj_memcpy(buf, hdr->sent_by.host.ptr, hdr->sent_by.host.slen);
    buf += hdr->sent_by.host.slen;
    if (hdr->sent_by.port != 0) {
	*buf++ = ':';
	printed = pj_utoa(hdr->sent_by.port, buf);
	buf += printed;
    }

    if (hdr->ttl_param >= 0) {
	size = endbuf-buf;
	if (size < 14)
	    return -1;
	pj_memcpy(buf, ";ttl=", 5);
	printed = pj_utoa(hdr->ttl_param, buf+5);
	buf += printed + 5;
    }

    if (hdr->rport_param >= 0) {
	size = endbuf-buf;
	if (size < 14)
	    return -1;
	pj_memcpy(buf, ";rport", 6);
	buf += 6;
	if (hdr->rport_param > 0) {
	    *buf++ = '=';
	    buf += pj_utoa(hdr->rport_param, buf);
	}
    }


    copy_advance_pair(buf, ";maddr=", 7, hdr->maddr_param);
    copy_advance_pair(buf, ";received=", 10, hdr->recvd_param);
    copy_advance_pair(buf, ";branch=", 8, hdr->branch_param);
    
    printed = pjsip_param_print_on(&hdr->other_param, buf, endbuf-buf, 
				   &pjsip_PARAM_CHAR_SPEC,
				   &pjsip_PARAM_CHAR_SPEC, ';');
    if (printed < 0)
	return -1;
    buf += printed;
    
    return buf-startbuf;
}

static pjsip_via_hdr* pjsip_via_hdr_clone( pj_pool_t *pool, 
					   const pjsip_via_hdr *rhs)
{
    pjsip_via_hdr *hdr = pjsip_via_hdr_create(pool);
    pj_strdup(pool, &hdr->transport, &rhs->transport);
    pj_strdup(pool, &hdr->sent_by.host, &rhs->sent_by.host);
    hdr->sent_by.port = rhs->sent_by.port;
    hdr->ttl_param = rhs->ttl_param;
    hdr->rport_param = rhs->rport_param;
    pj_strdup(pool, &hdr->maddr_param, &rhs->maddr_param);
    pj_strdup(pool, &hdr->recvd_param, &rhs->recvd_param);
    pj_strdup(pool, &hdr->branch_param, &rhs->branch_param);
    pjsip_param_clone(pool, &hdr->other_param, &rhs->other_param);
    return hdr;
}

static pjsip_via_hdr* pjsip_via_hdr_shallow_clone( pj_pool_t *pool,
						   const pjsip_via_hdr *rhs )
{
    pjsip_via_hdr *hdr = pj_pool_alloc(pool, sizeof(*hdr));
    pj_memcpy(hdr, rhs, sizeof(*hdr));
    pjsip_param_shallow_clone(pool, &hdr->other_param, &rhs->other_param);
    return hdr;
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Warning header.
 */
PJ_DEF(pjsip_warning_hdr*) pjsip_warning_hdr_create(  pj_pool_t *pool,
						      int code,
						      const pj_str_t *host,
						      const pj_str_t *text)
{
    const pj_str_t str_warning = { "Warning", 7 };
    pj_str_t hvalue;

    hvalue.ptr = pj_pool_alloc(pool, 10 +		/* code */
				     host->slen + 2 +	/* host */
				     text->slen + 2);	/* text */
    hvalue.slen = pj_ansi_sprintf(hvalue.ptr, "%u %.*s \"%.*s\"",
				  code, (int)host->slen, host->ptr,
				  (int)text->slen, text->ptr);

    return pjsip_generic_string_hdr_create(pool, &str_warning, &hvalue);
}

PJ_DEF(pjsip_warning_hdr*) 
pjsip_warning_hdr_create_from_status( pj_pool_t *pool,
				      const pj_str_t *host,
				      pj_status_t status)
{
    char errbuf[PJ_ERR_MSG_SIZE];
    pj_str_t text;
    
    text = pj_strerror(status, errbuf, sizeof(errbuf));
    return pjsip_warning_hdr_create(pool, 399, host, &text);
}

///////////////////////////////////////////////////////////////////////////////
/*
 * Message body manipulations.
 */
PJ_DEF(int) pjsip_print_text_body(pjsip_msg_body *msg_body, char *buf, pj_size_t size)
{
    if (size < msg_body->len)
	return -1;
    pj_memcpy(buf, msg_body->data, msg_body->len);
    return msg_body->len;
}

PJ_DEF(void*) pjsip_clone_text_data( pj_pool_t *pool, const void *data,
				     unsigned len)
{
    char *newdata = "";

    if (len) {
	newdata = pj_pool_alloc(pool, len);
	pj_memcpy(newdata, data, len);
    }
    return newdata;
}

PJ_DEF(pj_status_t) pjsip_msg_body_copy( pj_pool_t *pool,
					 pjsip_msg_body *dst_body,
					 const pjsip_msg_body *src_body )
{
    /* First check if clone_data field is initialized. */
    PJ_ASSERT_RETURN( src_body->clone_data!=NULL, PJ_EINVAL );

    /* Duplicate content-type */
    pj_strdup(pool, &dst_body->content_type.type, 
		    &src_body->content_type.type);
    pj_strdup(pool, &dst_body->content_type.subtype, 
		    &src_body->content_type.subtype);
    pj_strdup(pool, &dst_body->content_type.param,
		    &src_body->content_type.param);

    /* Duplicate data. */
    dst_body->data = (*src_body->clone_data)(pool, src_body->data, 
					     src_body->len );

    /* Length. */
    dst_body->len = src_body->len;

    /* Function pointers. */
    dst_body->print_body = src_body->print_body;
    dst_body->clone_data = src_body->clone_data;

    return PJ_SUCCESS;
}


PJ_DEF(pjsip_msg_body*) pjsip_msg_body_clone( pj_pool_t *pool,
					      const pjsip_msg_body *body )
{
    pjsip_msg_body *new_body;
    pj_status_t status;

    new_body = pj_pool_alloc(pool, sizeof(pjsip_msg_body));
    PJ_ASSERT_RETURN(new_body, NULL);

    status = pjsip_msg_body_copy(pool, new_body, body);

    return (status==PJ_SUCCESS) ? new_body : NULL;
}


PJ_DEF(pjsip_msg_body*) pjsip_msg_body_create( pj_pool_t *pool,
					       const pj_str_t *type,
					       const pj_str_t *subtype,
					       const pj_str_t *text )
{
    pjsip_msg_body *body;

    PJ_ASSERT_RETURN(pool && type && subtype && text, NULL);

    body = pj_pool_zalloc(pool, sizeof(pjsip_msg_body));
    PJ_ASSERT_RETURN(body != NULL, NULL);

    pj_strdup(pool, &body->content_type.type, type);
    pj_strdup(pool, &body->content_type.subtype, subtype);
    body->content_type.param.slen = 0;

    body->data = pj_pool_alloc(pool, text->slen);
    pj_memcpy(body->data, text->ptr, text->slen);
    body->len = text->slen;

    body->clone_data = &pjsip_clone_text_data;
    body->print_body = &pjsip_print_text_body;

    return body;
}

