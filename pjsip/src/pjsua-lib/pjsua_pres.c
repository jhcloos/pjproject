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
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>


#define THIS_FILE   "pjsua_pres.c"


/*
 * Get total number of buddies.
 */
PJ_DEF(unsigned) pjsua_get_buddy_count(void)
{
    return pjsua_var.buddy_cnt;
}


/*
 * Check if buddy ID is valid.
 */
PJ_DEF(pj_bool_t) pjsua_buddy_is_valid(pjsua_buddy_id buddy_id)
{
    return buddy_id>=0 && buddy_id<PJ_ARRAY_SIZE(pjsua_var.buddy) &&
	   pjsua_var.buddy[buddy_id].uri.slen != 0;
}


/*
 * Enum buddy IDs.
 */
PJ_DEF(pj_status_t) pjsua_enum_buddies( pjsua_buddy_id ids[],
					unsigned *count)
{
    unsigned i, c;

    PJ_ASSERT_RETURN(ids && count, PJ_EINVAL);

    PJSUA_LOCK();

    for (i=0, c=0; c<*count && i<PJ_ARRAY_SIZE(pjsua_var.buddy); ++i) {
	if (!pjsua_var.buddy[i].uri.slen)
	    continue;
	ids[c] = i;
	++c;
    }

    *count = c;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;

}


/*
 * Get detailed buddy info.
 */
PJ_DEF(pj_status_t) pjsua_buddy_get_info( pjsua_buddy_id buddy_id,
					  pjsua_buddy_info *info)
{
    int total=0;
    pjsua_buddy *buddy;

    PJ_ASSERT_RETURN(buddy_id>=0 && 
		       buddy_id<(int)PJ_ARRAY_SIZE(pjsua_var.buddy), 
		     PJ_EINVAL);

    PJSUA_LOCK();

    pj_memset(info, 0, sizeof(pjsua_buddy_info));

    buddy = &pjsua_var.buddy[buddy_id];
    info->id = buddy->index;
    if (pjsua_var.buddy[buddy_id].uri.slen == 0) {
	PJSUA_UNLOCK();
	return PJ_SUCCESS;
    }

    /* uri */
    info->uri.ptr = info->buf_ + total;
    pj_strncpy(&info->uri, &buddy->uri, sizeof(info->buf_)-total);
    total += info->uri.slen;

    /* contact */
    info->contact.ptr = info->buf_ + total;
    pj_strncpy(&info->contact, &buddy->contact, sizeof(info->buf_)-total);
    total += info->contact.slen;
	
    /* status and status text */    
    if (buddy->sub == NULL || buddy->status.info_cnt==0) {
	info->status = PJSUA_BUDDY_STATUS_UNKNOWN;
	info->status_text = pj_str("?");
    } else if (pjsua_var.buddy[buddy_id].status.info[0].basic_open) {
	info->status = PJSUA_BUDDY_STATUS_ONLINE;
	info->status_text = pj_str("Online");
    } else {
	info->status = PJSUA_BUDDY_STATUS_OFFLINE;
	info->status_text = pj_str("Offline");
    }

    /* monitor pres */
    info->monitor_pres = buddy->monitor;

    PJSUA_UNLOCK();
    return PJ_SUCCESS;
}


/*
 * Reset buddy descriptor.
 */
static void reset_buddy(pjsua_buddy_id id)
{
    pj_memset(&pjsua_var.buddy[id], 0, sizeof(pjsua_var.buddy[id]));
    pjsua_var.buddy[id].index = id;
}


/*
 * Add new buddy.
 */
PJ_DEF(pj_status_t) pjsua_buddy_add( const pjsua_buddy_config *cfg,
				     pjsua_buddy_id *p_buddy_id)
{
    pjsip_name_addr *url;
    pjsip_sip_uri *sip_uri;
    int index;
    pj_str_t tmp;

    PJ_ASSERT_RETURN(pjsua_var.buddy_cnt <= 
			PJ_ARRAY_SIZE(pjsua_var.buddy),
		     PJ_ETOOMANY);

    PJSUA_LOCK();

    /* Find empty slot */
    for (index=0; index<PJ_ARRAY_SIZE(pjsua_var.buddy); ++index) {
	if (pjsua_var.buddy[index].uri.slen == 0)
	    break;
    }

    /* Expect to find an empty slot */
    if (index == PJ_ARRAY_SIZE(pjsua_var.buddy)) {
	PJSUA_UNLOCK();
	/* This shouldn't happen */
	pj_assert(!"index < PJ_ARRAY_SIZE(pjsua_var.buddy)");
	return PJ_ETOOMANY;
    }


    /* Get name and display name for buddy */
    pj_strdup_with_null(pjsua_var.pool, &tmp, &cfg->uri);
    url = (pjsip_name_addr*)pjsip_parse_uri(pjsua_var.pool, tmp.ptr, tmp.slen,
					    PJSIP_PARSE_URI_AS_NAMEADDR);

    if (url == NULL) {
	pjsua_perror(THIS_FILE, "Unable to add buddy", PJSIP_EINVALIDURI);
	PJSUA_UNLOCK();
	return PJSIP_EINVALIDURI;
    }

    /* Reset buddy, to make sure everything is cleared with default
     * values
     */
    reset_buddy(index);

    /* Save URI */
    pjsua_var.buddy[index].uri = tmp;

    sip_uri = (pjsip_sip_uri*) url->uri;
    pjsua_var.buddy[index].name = sip_uri->user;
    pjsua_var.buddy[index].display = url->display;
    pjsua_var.buddy[index].host = sip_uri->host;
    pjsua_var.buddy[index].port = sip_uri->port;
    pjsua_var.buddy[index].monitor = cfg->subscribe;
    if (pjsua_var.buddy[index].port == 0)
	pjsua_var.buddy[index].port = 5060;

    if (p_buddy_id)
	*p_buddy_id = index;

    pjsua_var.buddy_cnt++;

    pjsua_buddy_subscribe_pres(index, cfg->subscribe);

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*
 * Delete buddy.
 */
PJ_DEF(pj_status_t) pjsua_buddy_del(pjsua_buddy_id buddy_id)
{
    PJ_ASSERT_RETURN(buddy_id>=0 && 
			buddy_id<(int)PJ_ARRAY_SIZE(pjsua_var.buddy),
		     PJ_EINVAL);

    PJSUA_LOCK();

    if (pjsua_var.buddy[buddy_id].uri.slen == 0) {
	PJSUA_UNLOCK();
	return PJ_SUCCESS;
    }

    /* Unsubscribe presence */
    pjsua_buddy_subscribe_pres(buddy_id, PJ_FALSE);

    /* Remove buddy */
    pjsua_var.buddy[buddy_id].uri.slen = 0;
    pjsua_var.buddy_cnt--;

    /* Reset buddy struct */
    reset_buddy(buddy_id);

    PJSUA_UNLOCK();
    return PJ_SUCCESS;
}


/*
 * Enable/disable buddy's presence monitoring.
 */
PJ_DEF(pj_status_t) pjsua_buddy_subscribe_pres( pjsua_buddy_id buddy_id,
						pj_bool_t subscribe)
{
    pjsua_buddy *buddy;

    PJ_ASSERT_RETURN(buddy_id>=0 && 
			buddy_id<(int)PJ_ARRAY_SIZE(pjsua_var.buddy),
		     PJ_EINVAL);

    PJSUA_LOCK();

    buddy = &pjsua_var.buddy[buddy_id];
    buddy->monitor = subscribe;
    pjsua_pres_refresh();

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*
 * Dump presence subscriptions to log file.
 */
PJ_DEF(void) pjsua_pres_dump(pj_bool_t verbose)
{
    unsigned acc_id;
    unsigned i;

    
    PJSUA_LOCK();

    /*
     * When no detail is required, just dump number of server and client
     * subscriptions.
     */
    if (verbose == PJ_FALSE) {
	
	int count = 0;

	for (acc_id=0; acc_id<PJ_ARRAY_SIZE(pjsua_var.acc); ++acc_id) {

	    if (!pjsua_var.acc[acc_id].valid)
		continue;

	    if (!pj_list_empty(&pjsua_var.acc[acc_id].pres_srv_list)) {
		struct pjsua_srv_pres *uapres;

		uapres = pjsua_var.acc[acc_id].pres_srv_list.next;
		while (uapres != &pjsua_var.acc[acc_id].pres_srv_list) {
		    ++count;
		    uapres = uapres->next;
		}
	    }
	}

	PJ_LOG(3,(THIS_FILE, "Number of server/UAS subscriptions: %d", 
		  count));

	count = 0;

	for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.buddy); ++i) {
	    if (pjsua_var.buddy[i].uri.slen == 0)
		continue;
	    if (pjsua_var.buddy[i].sub) {
		++count;
	    }
	}

	PJ_LOG(3,(THIS_FILE, "Number of client/UAC subscriptions: %d", 
		  count));
	PJSUA_UNLOCK();
	return;
    }
    

    /*
     * Dumping all server (UAS) subscriptions
     */
    PJ_LOG(3,(THIS_FILE, "Dumping pjsua server subscriptions:"));

    for (acc_id=0; acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc); ++acc_id) {

	if (!pjsua_var.acc[acc_id].valid)
	    continue;

	PJ_LOG(3,(THIS_FILE, "  %.*s",
		  (int)pjsua_var.acc[acc_id].cfg.id.slen,
		  pjsua_var.acc[acc_id].cfg.id.ptr));

	if (pj_list_empty(&pjsua_var.acc[acc_id].pres_srv_list)) {

	    PJ_LOG(3,(THIS_FILE, "  - none - "));

	} else {
	    struct pjsua_srv_pres *uapres;

	    uapres = pjsua_var.acc[acc_id].pres_srv_list.next;
	    while (uapres != &pjsua_var.acc[acc_id].pres_srv_list) {
	    
		PJ_LOG(3,(THIS_FILE, "    %10s %s",
			  pjsip_evsub_get_state_name(uapres->sub),
			  uapres->remote));

		uapres = uapres->next;
	    }
	}
    }

    /*
     * Dumping all client (UAC) subscriptions
     */
    PJ_LOG(3,(THIS_FILE, "Dumping pjsua client subscriptions:"));

    if (pjsua_var.buddy_cnt == 0) {

	PJ_LOG(3,(THIS_FILE, "  - no buddy list - "));

    } else {
	for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.buddy); ++i) {

	    if (pjsua_var.buddy[i].uri.slen == 0)
		continue;

	    if (pjsua_var.buddy[i].sub) {
		PJ_LOG(3,(THIS_FILE, "  %10s %.*s",
			  pjsip_evsub_get_state_name(pjsua_var.buddy[i].sub),
			  (int)pjsua_var.buddy[i].uri.slen,
			  pjsua_var.buddy[i].uri.ptr));
	    } else {
		PJ_LOG(3,(THIS_FILE, "  %10s %.*s",
			  "(null)",
			  (int)pjsua_var.buddy[i].uri.slen,
			  pjsua_var.buddy[i].uri.ptr));
	    }
	}
    }

    PJSUA_UNLOCK();
}


/***************************************************************************
 * Server subscription.
 */

/* Proto */
static pj_bool_t pres_on_rx_request(pjsip_rx_data *rdata);

/* The module instance. */
static pjsip_module mod_pjsua_pres = 
{
    NULL, NULL,				/* prev, next.		*/
    { "mod-pjsua-pres", 14 },		/* Name.		*/
    -1,					/* Id			*/
    PJSIP_MOD_PRIORITY_APPLICATION,	/* Priority	        */
    NULL,				/* load()		*/
    NULL,				/* start()		*/
    NULL,				/* stop()		*/
    NULL,				/* unload()		*/
    &pres_on_rx_request,		/* on_rx_request()	*/
    NULL,				/* on_rx_response()	*/
    NULL,				/* on_tx_request.	*/
    NULL,				/* on_tx_response()	*/
    NULL,				/* on_tsx_state()	*/

};


/* Callback called when *server* subscription state has changed. */
static void pres_evsub_on_srv_state( pjsip_evsub *sub, pjsip_event *event)
{
    pjsua_srv_pres *uapres;

    PJ_UNUSED_ARG(event);

    PJSUA_LOCK();

    uapres = pjsip_evsub_get_mod_data(sub, pjsua_var.mod.id);
    if (uapres) {
	PJ_LOG(3,(THIS_FILE, "Server subscription to %s is %s",
		  uapres->remote, pjsip_evsub_get_state_name(sub)));

	if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
	    pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, NULL);
	    pj_list_erase(uapres);
	}
    }

    PJSUA_UNLOCK();
}

/* This is called when request is received. 
 * We need to check for incoming SUBSCRIBE request.
 */
static pj_bool_t pres_on_rx_request(pjsip_rx_data *rdata)
{
    int acc_id;
    pjsua_acc_config *acc_config;
    pjsip_method *req_method = &rdata->msg_info.msg->line.req.method;
    pjsua_srv_pres *uapres;
    pjsip_evsub *sub;
    pjsip_evsub_user pres_cb;
    pjsip_tx_data *tdata;
    pjsip_pres_status pres_status;
    pjsip_dialog *dlg;
    pj_status_t status;

    if (pjsip_method_cmp(req_method, &pjsip_subscribe_method) != 0)
	return PJ_FALSE;

    /* Incoming SUBSCRIBE: */

    PJSUA_LOCK();

    /* Find which account for the incoming request. */
    acc_id = pjsua_acc_find_for_incoming(rdata);
    acc_config = &pjsua_var.acc[acc_id].cfg;

    /* Create UAS dialog: */
    status = pjsip_dlg_create_uas(pjsip_ua_instance(), rdata, 
				  &acc_config->contact,
				  &dlg);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Unable to create UAS dialog for subscription", 
		     status);
	PJSUA_UNLOCK();
	return PJ_TRUE;
    }

    /* Init callback: */
    pj_memset(&pres_cb, 0, sizeof(pres_cb));
    pres_cb.on_evsub_state = &pres_evsub_on_srv_state;

    /* Create server presence subscription: */
    status = pjsip_pres_create_uas( dlg, &pres_cb, rdata, &sub);
    if (status != PJ_SUCCESS) {
	pjsip_dlg_terminate(dlg);
	pjsua_perror(THIS_FILE, "Unable to create server subscription", 
		     status);
	PJSUA_UNLOCK();
	return PJ_TRUE;
    }

    /* Attach our data to the subscription: */
    uapres = pj_pool_alloc(dlg->pool, sizeof(pjsua_srv_pres));
    uapres->sub = sub;
    uapres->remote = pj_pool_alloc(dlg->pool, PJSIP_MAX_URL_SIZE);
    status = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, dlg->remote.info->uri,
			     uapres->remote, PJSIP_MAX_URL_SIZE);
    if (status < 1)
	pj_ansi_strcpy(uapres->remote, "<-- url is too long-->");
    else
	uapres->remote[status] = '\0';

    pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, uapres);

    /* Add server subscription to the list: */
    pj_list_push_back(&pjsua_var.acc[acc_id].pres_srv_list, uapres);


    /* Create and send 200 (OK) to the SUBSCRIBE request: */
    status = pjsip_pres_accept(sub, rdata, 200, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to accept presence subscription", 
		     status);
	pj_list_erase(uapres);
	pjsip_pres_terminate(sub, PJ_FALSE);
	PJSUA_UNLOCK();
	return PJ_FALSE;
    }


    /* Set our online status: */
    pj_memset(&pres_status, 0, sizeof(pres_status));
    pres_status.info_cnt = 1;
    pres_status.info[0].basic_open = pjsua_var.acc[acc_id].online_status;
    //Both pjsua_var.local_uri and pjsua_var.contact_uri are enclosed in "<" and ">"
    //causing XML parsing to fail.
    //pres_status.info[0].contact = pjsua_var.local_uri;

    pjsip_pres_set_status(sub, &pres_status);

    /* Create and send the first NOTIFY to active subscription: */
    status = pjsip_pres_notify( sub, PJSIP_EVSUB_STATE_ACTIVE, NULL,
			        NULL, &tdata);
    if (status == PJ_SUCCESS)
	status = pjsip_pres_send_request( sub, tdata);

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create/send NOTIFY", 
		     status);
	pj_list_erase(uapres);
	pjsip_pres_terminate(sub, PJ_FALSE);
	PJSUA_UNLOCK();
	return PJ_FALSE;
    }


    /* Done: */

    PJSUA_UNLOCK();

    return PJ_TRUE;
}


/* Terminate server subscription for the account */
void pjsua_pres_delete_acc(int acc_id)
{
    pjsua_srv_pres *uapres;

    uapres = pjsua_var.acc[acc_id].pres_srv_list.next;

    while (uapres != &pjsua_var.acc[acc_id].pres_srv_list) {
	
	pjsip_pres_status pres_status;
	pj_str_t reason = { "noresource", 10 };
	pjsip_tx_data *tdata;

	pjsip_pres_get_status(uapres->sub, &pres_status);
	
	pres_status.info[0].basic_open = pjsua_var.acc[acc_id].online_status;
	pjsip_pres_set_status(uapres->sub, &pres_status);

	if (pjsip_pres_notify(uapres->sub, 
			      PJSIP_EVSUB_STATE_TERMINATED, NULL,
			      &reason, &tdata)==PJ_SUCCESS)
	{
	    pjsip_pres_send_request(uapres->sub, tdata);
	}

	uapres = uapres->next;
    }
}


/* Refresh subscription (e.g. when our online status has changed) */
static void refresh_server_subscription(int acc_id)
{
    pjsua_srv_pres *uapres;

    uapres = pjsua_var.acc[acc_id].pres_srv_list.next;

    while (uapres != &pjsua_var.acc[acc_id].pres_srv_list) {
	
	pjsip_pres_status pres_status;
	pjsip_tx_data *tdata;

	pjsip_pres_get_status(uapres->sub, &pres_status);
	if (pres_status.info[0].basic_open != pjsua_var.acc[acc_id].online_status) {
	    pres_status.info[0].basic_open = pjsua_var.acc[acc_id].online_status;
	    pjsip_pres_set_status(uapres->sub, &pres_status);

	    if (pjsip_pres_current_notify(uapres->sub, &tdata)==PJ_SUCCESS)
		pjsip_pres_send_request(uapres->sub, tdata);
	}

	uapres = uapres->next;
    }
}



/***************************************************************************
 * Client subscription.
 */

/* Callback called when *client* subscription state has changed. */
static void pjsua_evsub_on_state( pjsip_evsub *sub, pjsip_event *event)
{
    pjsua_buddy *buddy;

    PJ_UNUSED_ARG(event);

    PJSUA_LOCK();

    buddy = pjsip_evsub_get_mod_data(sub, pjsua_var.mod.id);
    if (buddy) {
	PJ_LOG(3,(THIS_FILE, 
		  "Presence subscription to %.*s is %s",
		  (int)pjsua_var.buddy[buddy->index].uri.slen,
		  pjsua_var.buddy[buddy->index].uri.ptr, 
		  pjsip_evsub_get_state_name(sub)));

	if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
	    buddy->sub = NULL;
	    buddy->status.info_cnt = 0;
	    pjsip_evsub_set_mod_data(sub, pjsua_var.mod.id, NULL);
	}

	/* Call callback */
	if (pjsua_var.ua_cfg.cb.on_buddy_state)
	    (*pjsua_var.ua_cfg.cb.on_buddy_state)(buddy->index);
    }

    PJSUA_UNLOCK();
}


/* Callback when transaction state has changed. */
static void pjsua_evsub_on_tsx_state(pjsip_evsub *sub, 
				     pjsip_transaction *tsx,
				     pjsip_event *event)
{
    pjsua_buddy *buddy;
    pjsip_contact_hdr *contact_hdr;

    PJSUA_LOCK();

    buddy = pjsip_evsub_get_mod_data(sub, pjsua_var.mod.id);
    if (!buddy) {
	PJSUA_UNLOCK();
	return;
    }

    /* We only use this to update buddy's Contact, when it's not
     * set.
     */
    if (buddy->contact.slen != 0) {
	/* Contact already set */
	PJSUA_UNLOCK();
	return;
    }
    
    /* Only care about 2xx response to outgoing SUBSCRIBE */
    if (tsx->status_code/100 != 2 ||
	tsx->role != PJSIP_UAC_ROLE ||
	event->type != PJSIP_EVENT_RX_MSG || 
	pjsip_method_cmp(&tsx->method, &pjsip_subscribe_method)!=0)
    {
	PJSUA_UNLOCK();
	return;
    }

    /* Find contact header. */
    contact_hdr = pjsip_msg_find_hdr(event->body.rx_msg.rdata->msg_info.msg,
				     PJSIP_H_CONTACT, NULL);
    if (!contact_hdr) {
	PJSUA_UNLOCK();
	return;
    }

    buddy->contact.ptr = pj_pool_alloc(pjsua_var.pool, PJSIP_MAX_URL_SIZE);
    buddy->contact.slen = pjsip_uri_print( PJSIP_URI_IN_CONTACT_HDR,
					   contact_hdr->uri,
					   buddy->contact.ptr, 
					   PJSIP_MAX_URL_SIZE);
    if (buddy->contact.slen < 0)
	buddy->contact.slen = 0;

    PJSUA_UNLOCK();
}


/* Callback called when we receive NOTIFY */
static void pjsua_evsub_on_rx_notify(pjsip_evsub *sub, 
				     pjsip_rx_data *rdata,
				     int *p_st_code,
				     pj_str_t **p_st_text,
				     pjsip_hdr *res_hdr,
				     pjsip_msg_body **p_body)
{
    pjsua_buddy *buddy;

    PJSUA_LOCK();

    buddy = pjsip_evsub_get_mod_data(sub, pjsua_var.mod.id);
    if (buddy) {
	/* Update our info. */
	pjsip_pres_get_status(sub, &buddy->status);
    }

    /* The default is to send 200 response to NOTIFY.
     * Just leave it there..
     */
    PJ_UNUSED_ARG(rdata);
    PJ_UNUSED_ARG(p_st_code);
    PJ_UNUSED_ARG(p_st_text);
    PJ_UNUSED_ARG(res_hdr);
    PJ_UNUSED_ARG(p_body);

    PJSUA_UNLOCK();
}


/* Event subscription callback. */
static pjsip_evsub_user pres_callback = 
{
    &pjsua_evsub_on_state,  
    &pjsua_evsub_on_tsx_state,

    NULL,   /* on_rx_refresh: don't care about SUBSCRIBE refresh, unless 
	     * we want to authenticate 
	     */

    &pjsua_evsub_on_rx_notify,

    NULL,   /* on_client_refresh: Use default behaviour, which is to 
	     * refresh client subscription. */

    NULL,   /* on_server_timeout: Use default behaviour, which is to send 
	     * NOTIFY to terminate. 
	     */
};


/* It does what it says.. */
static void subscribe_buddy_presence(unsigned index)
{
    pjsua_buddy *buddy;
    int acc_id;
    pjsua_acc *acc;
    pjsip_dialog *dlg;
    pjsip_tx_data *tdata;
    pj_status_t status;

    buddy = &pjsua_var.buddy[index];
    acc_id = pjsua_acc_find_for_outgoing(&buddy->uri);

    acc = &pjsua_var.acc[acc_id];

    /* Create UAC dialog */
    status = pjsip_dlg_create_uac( pjsip_ua_instance(), 
				   &acc->cfg.id,
				   &acc->cfg.contact,
				   &buddy->uri,
				   NULL, &dlg);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create dialog", 
		     status);
	return;
    }

    /* Set route-set */
    if (!pj_list_empty(&acc->route_set)) {
	pjsip_dlg_set_route_set(dlg, &acc->route_set);
    }

    /* Set credentials */
    if (acc->cred_cnt) {
	pjsip_auth_clt_set_credentials( &dlg->auth_sess, 
					acc->cred_cnt, acc->cred);
    }

    status = pjsip_pres_create_uac( dlg, &pres_callback, 
				    &buddy->sub);
    if (status != PJ_SUCCESS) {
	pjsua_var.buddy[index].sub = NULL;
	pjsua_perror(THIS_FILE, "Unable to create presence client", 
		     status);
	pjsip_dlg_terminate(dlg);
	return;
    }

    pjsip_evsub_set_mod_data(buddy->sub, pjsua_var.mod.id, buddy);

    status = pjsip_pres_initiate(buddy->sub, -1, &tdata);
    if (status != PJ_SUCCESS) {
	pjsip_pres_terminate(buddy->sub, PJ_FALSE);
	buddy->sub = NULL;
	pjsua_perror(THIS_FILE, "Unable to create initial SUBSCRIBE", 
		     status);
	return;
    }

    status = pjsip_pres_send_request(buddy->sub, tdata);
    if (status != PJ_SUCCESS) {
	pjsip_pres_terminate(buddy->sub, PJ_FALSE);
	buddy->sub = NULL;
	pjsua_perror(THIS_FILE, "Unable to send initial SUBSCRIBE", 
		     status);
	return;
    }
}


/* It does what it says... */
static void unsubscribe_buddy_presence(unsigned index)
{
    pjsua_buddy *buddy;
    pjsip_tx_data *tdata;
    pj_status_t status;

    buddy = &pjsua_var.buddy[index];

    if (buddy->sub == NULL)
	return;

    if (pjsip_evsub_get_state(buddy->sub) == PJSIP_EVSUB_STATE_TERMINATED) {
	pjsua_var.buddy[index].sub = NULL;
	return;
    }

    status = pjsip_pres_initiate( buddy->sub, 0, &tdata);
    if (status == PJ_SUCCESS)
	status = pjsip_pres_send_request( buddy->sub, tdata );

    if (status != PJ_SUCCESS) {
	pjsip_pres_terminate(buddy->sub, PJ_FALSE);
	buddy->sub = NULL;
	pjsua_perror(THIS_FILE, "Unable to unsubscribe presence", 
		     status);
    }
}


/* It does what it says.. */
static void refresh_client_subscriptions(void)
{
    unsigned i;

    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.buddy); ++i) {

	if (!pjsua_var.buddy[i].uri.slen)
	    continue;

	if (pjsua_var.buddy[i].monitor && !pjsua_var.buddy[i].sub) {
	    subscribe_buddy_presence(i);

	} else if (!pjsua_var.buddy[i].monitor && pjsua_var.buddy[i].sub) {
	    unsubscribe_buddy_presence(i);

	}
    }
}


/*
 * Init presence
 */
pj_status_t pjsua_pres_init()
{
    unsigned i;
    pj_status_t status;

    status = pjsip_endpt_register_module( pjsua_var.endpt, &mod_pjsua_pres);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to register pjsua presence module", 
		     status);
    }

    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.buddy); ++i) {
	reset_buddy(i);
    }

    return status;
}


/*
 * Start presence subsystem.
 */
pj_status_t pjsua_pres_start(void)
{
    /* Nothing to do (is it?) */
    return PJ_SUCCESS;
}


/*
 * Refresh presence subscriptions
 */
void pjsua_pres_refresh()
{
    unsigned i;

    refresh_client_subscriptions();

    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	if (pjsua_var.acc[i].valid)
	    refresh_server_subscription(i);
    }
}


/*
 * Shutdown presence.
 */
void pjsua_pres_shutdown(void)
{
    unsigned i;

    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	if (!pjsua_var.acc[i].valid)
	    continue;
	pjsua_pres_delete_acc(i);
    }

    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.buddy); ++i) {
	pjsua_var.buddy[i].monitor = 0;
    }

    pjsua_pres_refresh();
}
