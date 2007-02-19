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
#include <pjsip-simple/presence.h>
#include <pjsip-simple/errno.h>
#include <pjsip/sip_msg.h>
#include <pjsip/sip_transport.h>
#include <pj/guid.h>
#include <pj/log.h>
#include <pj/pool.h>
#include <pj/string.h>


#define THIS_FILE   "presence_body.c"


static const pj_str_t STR_APPLICATION = { "application", 11 };
static const pj_str_t STR_PIDF_XML =	{ "pidf+xml", 8 };
static const pj_str_t STR_XPIDF_XML =	{ "xpidf+xml", 9 };




/*
 * Function to print XML message body.
 */
static int pres_print_body(struct pjsip_msg_body *msg_body, 
			   char *buf, pj_size_t size)
{
    return pj_xml_print(msg_body->data, buf, size, PJ_TRUE);
}


/*
 * Function to clone XML document.
 */
static void* xml_clone_data(pj_pool_t *pool, const void *data, unsigned len)
{
    PJ_UNUSED_ARG(len);
    return pj_xml_clone( pool, data);
}


/*
 * This is a utility function to create PIDF message body from PJSIP
 * presence status (pjsip_pres_status).
 */
PJ_DEF(pj_status_t) pjsip_pres_create_pidf( pj_pool_t *pool,
					    const pjsip_pres_status *status,
					    const pj_str_t *entity,
					    pjsip_msg_body **p_body )
{
    pjpidf_pres *pidf;
    pjsip_msg_body *body;
    unsigned i;

    /* Create <presence>. */
    pidf = pjpidf_create(pool, entity);

    /* Create <tuple> */
    for (i=0; i<status->info_cnt; ++i) {

	pjpidf_tuple *pidf_tuple;
	pjpidf_status *pidf_status;
	pj_str_t id;

	/* Add tuple id. */
	if (status->info[i].id.slen == 0) {
	    pj_create_unique_string(pool, &id);
	} else {
	    id = status->info[i].id;
	}

	pidf_tuple = pjpidf_pres_add_tuple(pool, pidf, &id);

	/* Set <contact> */
	if (status->info[i].contact.slen)
	    pjpidf_tuple_set_contact(pool, pidf_tuple, 
				     &status->info[i].contact);


	/* Set basic status */
	pidf_status = pjpidf_tuple_get_status(pidf_tuple);
	pjpidf_status_set_basic_open(pidf_status, 
				     status->info[i].basic_open);
    }

    body = pj_pool_zalloc(pool, sizeof(pjsip_msg_body));
    body->data = pidf;
    body->content_type.type = STR_APPLICATION;
    body->content_type.subtype = STR_PIDF_XML;
    body->print_body = &pres_print_body;
    body->clone_data = &xml_clone_data;

    *p_body = body;

    return PJ_SUCCESS;    
}


/*
 * This is a utility function to create X-PIDF message body from PJSIP
 * presence status (pjsip_pres_status).
 */
PJ_DEF(pj_status_t) pjsip_pres_create_xpidf( pj_pool_t *pool,
					     const pjsip_pres_status *status,
					     const pj_str_t *entity,
					     pjsip_msg_body **p_body )
{
    /* Note: PJSIP implementation of XPIDF is not complete!
     */
    pjxpidf_pres *xpidf;
    pjsip_msg_body *body;

    PJ_LOG(4,(THIS_FILE, "Warning: XPIDF format is not fully supported "
			 "by PJSIP"));

    /* Create XPIDF document. */
    xpidf = pjxpidf_create(pool, entity);

    /* Set basic status. */
    if (status->info_cnt > 0)
	pjxpidf_set_status( xpidf, status->info[0].basic_open);
    else
	pjxpidf_set_status( xpidf, PJ_FALSE);

    body = pj_pool_zalloc(pool, sizeof(pjsip_msg_body));
    body->data = xpidf;
    body->content_type.type = STR_APPLICATION;
    body->content_type.subtype = STR_XPIDF_XML;
    body->print_body = &pres_print_body;
    body->clone_data = &xml_clone_data;

    *p_body = body;

    return PJ_SUCCESS;
}



/*
 * This is a utility function to parse PIDF body into PJSIP presence status.
 */
PJ_DEF(pj_status_t) pjsip_pres_parse_pidf( pjsip_rx_data *rdata,
					   pj_pool_t *pool,
					   pjsip_pres_status *pres_status)
{
    pjpidf_pres *pidf;
    pjpidf_tuple *pidf_tuple;

    pidf = pjpidf_parse(rdata->tp_info.pool, 
			rdata->msg_info.msg->body->data,
			rdata->msg_info.msg->body->len);
    if (pidf == NULL)
	return PJSIP_SIMPLE_EBADPIDF;

    pres_status->info_cnt = 0;

    pidf_tuple = pjpidf_pres_get_first_tuple(pidf);
    while (pidf_tuple) {
	pjpidf_status *pidf_status;

	pj_strdup(pool, 
		  &pres_status->info[pres_status->info_cnt].id,
		  pjpidf_tuple_get_id(pidf_tuple));

	pj_strdup(pool, 
		  &pres_status->info[pres_status->info_cnt].contact,
		  pjpidf_tuple_get_contact(pidf_tuple));

	pidf_status = pjpidf_tuple_get_status(pidf_tuple);
	if (pidf_status) {
	    pres_status->info[pres_status->info_cnt].basic_open = 
		pjpidf_status_is_basic_open(pidf_status);
	} else {
	    pres_status->info[pres_status->info_cnt].basic_open = PJ_FALSE;
	}

	pidf_tuple = pjpidf_pres_get_next_tuple( pidf, pidf_tuple );
	pres_status->info_cnt++;
    }

    return PJ_SUCCESS;
}


/*
 * This is a utility function to parse X-PIDF body into PJSIP presence status.
 */
PJ_DEF(pj_status_t) pjsip_pres_parse_xpidf(pjsip_rx_data *rdata,
					   pj_pool_t *pool,
					   pjsip_pres_status *pres_status)
{
    pjxpidf_pres *xpidf;

    xpidf = pjxpidf_parse(rdata->tp_info.pool, 
			  rdata->msg_info.msg->body->data,
			  rdata->msg_info.msg->body->len);
    if (xpidf == NULL)
	return PJSIP_SIMPLE_EBADXPIDF;

    pres_status->info_cnt = 1;
    
    pj_strdup(pool,
	      &pres_status->info[0].contact,
	      pjxpidf_get_uri(xpidf));
    pres_status->info[0].basic_open = pjxpidf_get_status(xpidf);
    pres_status->info[0].id.slen = 0;

    return PJ_SUCCESS;
}


