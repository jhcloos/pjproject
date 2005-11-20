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
#include "test.h"
#include <pjsip_core.h>
#include <pjlib.h>

static pjsip_msg *create_msg0(pj_pool_t *pool);

struct test_msg
{
    char	 msg[1024];
    pjsip_msg *(*creator)(pj_pool_t *pool);
    pj_size_t	 len;
} test_array[] = 
{
    {
	/* 'Normal' message with all headers. */
	"INVITE sip:user@foo SIP/2.0\n"
	"From: Hi I'm Joe <sip:joe.user@bar.otherdomain.com>;tag=1234578901234567890\r"
	"To: Fellow User <sip:user@foo.bar.domain.com>\r\n"
	"Call-ID: 12345678901234567890@bar\r\n"
	"Content-Length: 0\r\n"
	"CSeq: 123456 INVITE\n"
	"Contact: <sip:joe@bar> ; q=0.5;expires=3600,sip:user@host;q=0.500\r"
	"  ,sip:user2@host2\n"
	"Content-Type: text/html ; charset=ISO-8859-4\r"
	"Route: <sip:bigbox3.site3.atlanta.com;lr>,\r\n"
	"  <sip:server10.biloxi.com;lr>\r"
	"Record-Route: <sip:server10.biloxi.com>,\r\n"
	"  <sip:bigbox3.site3.atlanta.com;lr>\n"
	"Via: SIP/2.0/SCTP bigbox3.site3.atlanta.com;branch=z9hG4bK77ef4c2312983.1\n"
	"Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8\n"
	" ;received=192.0.2.1\r\n"
	"Via: SIP/2.0/UDP 10.2.1.1, SIP/2.0/TCP 192.168.1.1\n"
	"Organization: \r"
	"Max-Forwards: 70\n"
	"X-Header: \r\n"
	"\r",
	&create_msg0
    }
};

static pj_uint32_t parse_len, parse_time, print_time;

static pj_status_t test_entry( pj_pool_t *pool, struct test_msg *entry )
{
    pjsip_msg *parsed_msg, *ref_msg;
    pj_status_t status = PJ_SUCCESS;
    int len;
    pj_str_t str1, str2;
    pjsip_hdr *hdr1, *hdr2;
    pj_timestamp t1, t2;
    char *msgbuf;

    enum { BUFLEN = 512 };
    
    /* Parse message. */
    parse_len += entry->len;
    pj_get_timestamp(&t1);
    parsed_msg = pjsip_parse_msg(pool, entry->msg, entry->len, NULL);
    if (parsed_msg == NULL) {
	status = -10;
	goto on_return;
    }
    pj_get_timestamp(&t2);
    parse_time += t2.u32.lo - t1.u32.lo;

    /* Create reference message. */
    ref_msg = entry->creator(pool);

    /* Create buffer for comparison. */
    str1.ptr = pj_pool_alloc(pool, BUFLEN);
    str2.ptr = pj_pool_alloc(pool, BUFLEN);

    /* Compare message type. */
    if (parsed_msg->type != ref_msg->type) {
	status = -20;
	goto on_return;
    }

    /* Compare request or status line. */
    if (parsed_msg->type == PJSIP_REQUEST_MSG) {
	pjsip_method *m1 = &parsed_msg->line.req.method;
	pjsip_method *m2 = &ref_msg->line.req.method;

	if (m1->id != m2->id || pj_strcmp(&m1->name, &m2->name)) {
	    status = -30;
	    goto on_return;
	}
    } else {

    }

    /* Compare headers. */
    hdr1 = parsed_msg->hdr.next;
    hdr2 = ref_msg->hdr.next;

    while (hdr1 != &parsed_msg->hdr && hdr2 != &ref_msg->hdr) {
	len = hdr1->vptr->print_on(hdr1, str1.ptr, BUFLEN);
	if (len < 1) {
	    status = -40;
	    goto on_return;
	}
	str1.slen = len;

	len = hdr2->vptr->print_on(hdr2, str2.ptr, BUFLEN);
	if (len < 1) {
	    status = -50;
	    goto on_return;
	}
	str2.slen = len;

	if (pj_strcmp(&str1, &str2) != 0) {
	    status = -60;
	    goto on_return;
	}

	hdr1 = hdr1->next;
	hdr2 = hdr2->next;
    }

    if (hdr1 != &parsed_msg->hdr || hdr2 != &ref_msg->hdr) {
	status = -70;
	goto on_return;
    }

    /* Print message. */
    msgbuf = pj_pool_alloc(pool, PJSIP_MAX_PKT_LEN);
    if (msgbuf == NULL) {
	status = -80;
	goto on_return;
    }
    pj_get_timestamp(&t1);
    len = pjsip_msg_print(parsed_msg, msgbuf, PJSIP_MAX_PKT_LEN);
    if (len < 1) {
	status = -90;
	goto on_return;
    }
    pj_get_timestamp(&t2);
    print_time += t2.u32.lo - t1.u32.lo;
    status = PJ_SUCCESS;

on_return:
    return status;
}


pj_status_t msg_test(void)
{
    pj_status_t status;
    pj_pool_t *pool;

    pool = pjsip_endpt_create_pool(endpt, NULL, 4000, 4000);

    status = test_entry( pool, &test_array[0] );

    pjsip_endpt_destroy(endpt);
    return status;
}

/*****************************************************************************/

static pjsip_msg *create_msg0(pj_pool_t *pool)
{

    pjsip_msg *msg;
    pjsip_name_addr *name_addr;
    pjsip_url *url;
    pjsip_fromto_hdr *fromto;
    pjsip_cid_hdr *cid;
    pjsip_clen_hdr *clen;
    pjsip_cseq_hdr *cseq;
    pjsip_contact_hdr *contact;
    pjsip_ctype_hdr *ctype;
    pjsip_routing_hdr *routing;
    pjsip_via_hdr *via;
    pjsip_generic_string_hdr *generic;
    pj_str_t str;

    msg = pjsip_msg_create(pool, PJSIP_REQUEST_MSG);

    /* "INVITE sip:user@foo SIP/2.0\n" */
    pjsip_method_set(&msg->line.req.method, PJSIP_INVITE_METHOD);
    url = pjsip_url_create(pool, 0);
    msg->line.req.uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->user, "user");
    pj_strdup2(pool, &url->host, "foo");

    /* "From: Hi I'm Joe <sip:joe.user@bar.otherdomain.com>;tag=1234578901234567890\r" */
    fromto = pjsip_from_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)fromto);
    pj_strdup2(pool, &fromto->tag, "1234578901234567890");
    name_addr = pjsip_name_addr_create(pool);
    fromto->uri = (pjsip_uri*)name_addr;
    pj_strdup2(pool, &name_addr->display, "Hi I'm Joe");
    url = pjsip_url_create(pool, 0);
    name_addr->uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->user, "joe.user");
    pj_strdup2(pool, &url->host, "bar.otherdomain.com");

    /* "To: Fellow User <sip:user@foo.bar.domain.com>\r\n" */
    fromto = pjsip_to_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)fromto);
    name_addr = pjsip_name_addr_create(pool);
    fromto->uri = (pjsip_uri*)name_addr;
    pj_strdup2(pool, &name_addr->display, "Fellow User");
    url = pjsip_url_create(pool, 0);
    name_addr->uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->user, "user");
    pj_strdup2(pool, &url->host, "foo.bar.domain.com");

    /* "Call-ID: 12345678901234567890@bar\r\n" */
    cid = pjsip_cid_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)cid);
    pj_strdup2(pool, &cid->id, "12345678901234567890@bar");

    /* "Content-Length: 0\r\n" */
    clen = pjsip_clen_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)clen);
    clen->len = 0;

    /* "CSeq: 123456 INVITE\n" */
    cseq = pjsip_cseq_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)cseq);
    cseq->cseq = 123456;
    pjsip_method_set(&cseq->method, PJSIP_INVITE_METHOD);

    /* "Contact: <sip:joe@bar>;q=0.5;expires=3600*/
    contact = pjsip_contact_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)contact);
    contact->q1000 = 500;
    contact->expires = 3600;
    name_addr = pjsip_name_addr_create(pool);
    contact->uri = (pjsip_uri*)name_addr;
    url = pjsip_url_create(pool, 0);
    name_addr->uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->user, "joe");
    pj_strdup2(pool, &url->host, "bar");

    /*, sip:user@host;q=0.500\r" */
    contact = pjsip_contact_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)contact);
    contact->q1000 = 500;
    url = pjsip_url_create(pool, 0);
    contact->uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->user, "user");
    pj_strdup2(pool, &url->host, "host");

    /* "  ,sip:user2@host2\n" */
    contact = pjsip_contact_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)contact);
    url = pjsip_url_create(pool, 0);
    contact->uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->user, "user2");
    pj_strdup2(pool, &url->host, "host2");

    /* "Content-Type: text/html; charset=ISO-8859-4\r" */
    ctype = pjsip_ctype_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)ctype);
    pj_strdup2(pool, &ctype->media.type, "text");
    pj_strdup2(pool, &ctype->media.subtype, "html");
    pj_strdup2(pool, &ctype->media.param, ";charset=ISO-8859-4");

    /* "Route: <sip:bigbox3.site3.atlanta.com;lr>,\r\n" */
    routing = pjsip_route_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)routing);
    url = pjsip_url_create(pool, 0);
    routing->name_addr.uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->host, "bigbox3.site3.atlanta.com");
    url->lr_param = 1;

    /* "  <sip:server10.biloxi.com;lr>\r" */
    routing = pjsip_route_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)routing);
    url = pjsip_url_create(pool, 0);
    routing->name_addr.uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->host, "server10.biloxi.com");
    url->lr_param = 1;

    /* "Record-Route: <sip:server10.biloxi.com>,\r\n" */
    routing = pjsip_rr_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)routing);
    url = pjsip_url_create(pool, 0);
    routing->name_addr.uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->host, "server10.biloxi.com");
    url->lr_param = 0;

    /* "  <sip:bigbox3.site3.atlanta.com;lr>\n" */
    routing = pjsip_rr_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)routing);
    url = pjsip_url_create(pool, 0);
    routing->name_addr.uri = (pjsip_uri*)url;
    pj_strdup2(pool, &url->host, "bigbox3.site3.atlanta.com");
    url->lr_param = 1;

    /* "Via: SIP/2.0/SCTP bigbox3.site3.atlanta.com;branch=z9hG4bK77ef4c2312983.1\n" */
    via = pjsip_via_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)via);
    pj_strdup2(pool, &via->transport, "SCTP");
    pj_strdup2(pool, &via->sent_by.host, "bigbox3.site3.atlanta.com");
    pj_strdup2(pool, &via->branch_param, "z9hG4bK77ef4c2312983.1");

    /* "Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8\n"
	" ;received=192.0.2.1\r\n" */
    via = pjsip_via_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)via);
    pj_strdup2(pool, &via->transport, "UDP");
    pj_strdup2(pool, &via->sent_by.host, "pc33.atlanta.com");
    pj_strdup2(pool, &via->branch_param, "z9hG4bKnashds8");
    pj_strdup2(pool, &via->recvd_param, "192.0.2.1");


    /* "Via: SIP/2.0/UDP 10.2.1.1, */ 
    via = pjsip_via_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)via);
    pj_strdup2(pool, &via->transport, "UDP");
    pj_strdup2(pool, &via->sent_by.host, "10.2.1.1");
    
    
    /*SIP/2.0/TCP 192.168.1.1\n" */
    via = pjsip_via_hdr_create(pool);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)via);
    pj_strdup2(pool, &via->transport, "TCP");
    pj_strdup2(pool, &via->sent_by.host, "192.168.1.1");

    /* "Organization: \r" */
    str.ptr = "Organization";
    str.slen = 12;
    generic = pjsip_generic_string_hdr_create(pool, &str);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)generic);
    generic->hvalue.ptr = NULL;
    generic->hvalue.slen = 0;

    /* "Max-Forwards: 70\n" */
    str.ptr = "Max-Forwards";
    str.slen = 12;
    generic = pjsip_generic_string_hdr_create(pool, &str);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)generic);
    str.ptr = "70";
    str.slen = 2;
    generic->hvalue = str;

    /* "X-Header: \r\n" */
    str.ptr = "X-Header";
    str.slen = 8;
    generic = pjsip_generic_string_hdr_create(pool, &str);
    pjsip_msg_add_hdr(msg, (pjsip_hdr*)generic);
    str.ptr = NULL;
    str.slen = 0;
    generic->hvalue = str;

    return msg;
}
