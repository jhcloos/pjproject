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
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>


#define THIS_FILE		"pjsua_acc.c"


/*
 * Get number of current accounts.
 */
PJ_DEF(unsigned) pjsua_acc_get_count(void)
{
    return pjsua_var.acc_cnt;
}


/*
 * Check if the specified account ID is valid.
 */
PJ_DEF(pj_bool_t) pjsua_acc_is_valid(pjsua_acc_id acc_id)
{
    return acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc) &&
	   pjsua_var.acc[acc_id].valid;
}


/*
 * Set default account
 */
PJ_DEF(pj_status_t) pjsua_acc_set_default(pjsua_acc_id acc_id)
{
    pjsua_var.default_acc = acc_id;
    return PJ_SUCCESS;
}


/*
 * Get default account.
 */
PJ_DEF(pjsua_acc_id) pjsua_acc_get_default(void)
{
    return pjsua_var.default_acc;
}


/*
 * Copy account configuration.
 */
PJ_DEF(void) pjsua_acc_config_dup( pj_pool_t *pool,
				   pjsua_acc_config *dst,
				   const pjsua_acc_config *src)
{
    unsigned i;

    pj_memcpy(dst, src, sizeof(pjsua_acc_config));

    pj_strdup_with_null(pool, &dst->id, &src->id);
    pj_strdup_with_null(pool, &dst->reg_uri, &src->reg_uri);
    pj_strdup_with_null(pool, &dst->force_contact, &src->force_contact);
    pj_strdup_with_null(pool, &dst->pidf_tuple_id, &src->pidf_tuple_id);

    dst->proxy_cnt = src->proxy_cnt;
    for (i=0; i<src->proxy_cnt; ++i)
	pj_strdup_with_null(pool, &dst->proxy[i], &src->proxy[i]);

    dst->reg_timeout = src->reg_timeout;
    dst->cred_count = src->cred_count;

    for (i=0; i<src->cred_count; ++i) {
	pjsip_cred_dup(pool, &dst->cred_info[i], &src->cred_info[i]);
    }

    dst->ka_interval = src->ka_interval;
    pj_strdup(pool, &dst->ka_data, &src->ka_data);
}


/*
 * Initialize a new account (after configuration is set).
 */
static pj_status_t initialize_acc(unsigned acc_id)
{
    pjsua_acc_config *acc_cfg = &pjsua_var.acc[acc_id].cfg;
    pjsua_acc *acc = &pjsua_var.acc[acc_id];
    pjsip_name_addr *name_addr;
    pjsip_sip_uri *sip_uri, *sip_reg_uri;
    pj_status_t status;
    unsigned i;

    /* Need to parse local_uri to get the elements: */

    name_addr = (pjsip_name_addr*)
		    pjsip_parse_uri(pjsua_var.pool, acc_cfg->id.ptr,
				    acc_cfg->id.slen, 
				    PJSIP_PARSE_URI_AS_NAMEADDR);
    if (name_addr == NULL) {
	pjsua_perror(THIS_FILE, "Invalid local URI", 
		     PJSIP_EINVALIDURI);
	return PJSIP_EINVALIDURI;
    }

    /* Local URI MUST be a SIP or SIPS: */

    if (!PJSIP_URI_SCHEME_IS_SIP(name_addr) && 
	!PJSIP_URI_SCHEME_IS_SIPS(name_addr)) 
    {
	pjsua_perror(THIS_FILE, "Invalid local URI", 
		     PJSIP_EINVALIDSCHEME);
	return PJSIP_EINVALIDSCHEME;
    }


    /* Get the SIP URI object: */
    sip_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(name_addr);


    /* Parse registrar URI, if any */
    if (acc_cfg->reg_uri.slen) {
	pjsip_uri *reg_uri;

	reg_uri = pjsip_parse_uri(pjsua_var.pool, acc_cfg->reg_uri.ptr,
				  acc_cfg->reg_uri.slen, 0);
	if (reg_uri == NULL) {
	    pjsua_perror(THIS_FILE, "Invalid registrar URI", 
			 PJSIP_EINVALIDURI);
	    return PJSIP_EINVALIDURI;
	}

	/* Registrar URI MUST be a SIP or SIPS: */
	if (!PJSIP_URI_SCHEME_IS_SIP(reg_uri) && 
	    !PJSIP_URI_SCHEME_IS_SIPS(reg_uri)) 
	{
	    pjsua_perror(THIS_FILE, "Invalid registar URI", 
			 PJSIP_EINVALIDSCHEME);
	    return PJSIP_EINVALIDSCHEME;
	}

	sip_reg_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(reg_uri);

    } else {
	sip_reg_uri = NULL;
    }


    /* Save the user and domain part. These will be used when finding an 
     * account for incoming requests.
     */
    acc->display = name_addr->display;
    acc->user_part = sip_uri->user;
    acc->srv_domain = sip_uri->host;
    acc->srv_port = 0;

    if (sip_reg_uri) {
	acc->srv_port = sip_reg_uri->port;
    }

    /* Create Contact header if not present. */
    //if (acc_cfg->contact.slen == 0) {
    //	acc_cfg->contact = acc_cfg->id;
    //}

    /* Build account route-set from outbound proxies and route set from 
     * account configuration.
     */
    pj_list_init(&acc->route_set);

    for (i=0; i<pjsua_var.ua_cfg.outbound_proxy_cnt; ++i) {
    	pj_str_t hname = { "Route", 5};
	pjsip_route_hdr *r;
	pj_str_t tmp;

	pj_strdup_with_null(pjsua_var.pool, &tmp, 
			    &pjsua_var.ua_cfg.outbound_proxy[i]);
	r = (pjsip_route_hdr*)
	    pjsip_parse_hdr(pjsua_var.pool, &hname, tmp.ptr, tmp.slen, NULL);
	if (r == NULL) {
	    pjsua_perror(THIS_FILE, "Invalid outbound proxy URI",
			 PJSIP_EINVALIDURI);
	    return PJSIP_EINVALIDURI;
	}
	pj_list_push_back(&acc->route_set, r);
    }

    for (i=0; i<acc_cfg->proxy_cnt; ++i) {
    	pj_str_t hname = { "Route", 5};
	pjsip_route_hdr *r;
	pj_str_t tmp;

	pj_strdup_with_null(pjsua_var.pool, &tmp, &acc_cfg->proxy[i]);
	r = (pjsip_route_hdr*)
	    pjsip_parse_hdr(pjsua_var.pool, &hname, tmp.ptr, tmp.slen, NULL);
	if (r == NULL) {
	    pjsua_perror(THIS_FILE, "Invalid URI in account route set",
			 PJ_EINVAL);
	    return PJ_EINVAL;
	}
	pj_list_push_back(&acc->route_set, r);
    }

    
    /* Concatenate credentials from account config and global config */
    acc->cred_cnt = 0;
    for (i=0; i<acc_cfg->cred_count; ++i) {
	acc->cred[acc->cred_cnt++] = acc_cfg->cred_info[i];
    }
    for (i=0; i<pjsua_var.ua_cfg.cred_count && 
	      acc->cred_cnt < PJ_ARRAY_SIZE(acc->cred); ++i)
    {
	acc->cred[acc->cred_cnt++] = pjsua_var.ua_cfg.cred_info[i];
    }

    status = pjsua_pres_init_acc(acc_id);
    if (status != PJ_SUCCESS)
	return status;

    /* Mark account as valid */
    pjsua_var.acc[acc_id].valid = PJ_TRUE;

    /* Insert account ID into account ID array, sorted by priority */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	if ( pjsua_var.acc[pjsua_var.acc_ids[i]].cfg.priority <
	     pjsua_var.acc[acc_id].cfg.priority)
	{
	    break;
	}
    }
    pj_array_insert(pjsua_var.acc_ids, sizeof(pjsua_var.acc_ids[0]),
		    pjsua_var.acc_cnt, i, &acc_id);

    return PJ_SUCCESS;
}


/*
 * Add a new account to pjsua.
 */
PJ_DEF(pj_status_t) pjsua_acc_add( const pjsua_acc_config *cfg,
				   pj_bool_t is_default,
				   pjsua_acc_id *p_acc_id)
{
    unsigned id;
    pj_status_t status;

    PJ_ASSERT_RETURN(pjsua_var.acc_cnt < PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_ETOOMANY);

    /* Must have a transport */
    PJ_ASSERT_RETURN(pjsua_var.tpdata[0].data.ptr != NULL, PJ_EINVALIDOP);

    PJSUA_LOCK();

    /* Find empty account id. */
    for (id=0; id < PJ_ARRAY_SIZE(pjsua_var.acc); ++id) {
	if (pjsua_var.acc[id].valid == PJ_FALSE)
	    break;
    }

    /* Expect to find a slot */
    PJ_ASSERT_ON_FAIL(	id < PJ_ARRAY_SIZE(pjsua_var.acc), 
			{PJSUA_UNLOCK(); return PJ_EBUG;});

    /* Copy config */
    pjsua_acc_config_dup(pjsua_var.pool, &pjsua_var.acc[id].cfg, cfg);
    
    /* Normalize registration timeout */
    if (pjsua_var.acc[id].cfg.reg_uri.slen &&
	pjsua_var.acc[id].cfg.reg_timeout == 0)
    {
	pjsua_var.acc[id].cfg.reg_timeout = PJSUA_REG_INTERVAL;
    }

    status = initialize_acc(id);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error adding account", status);
	PJSUA_UNLOCK();
	return status;
    }

    if (is_default)
	pjsua_var.default_acc = id;

    if (p_acc_id)
	*p_acc_id = id;

    pjsua_var.acc_cnt++;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Account %.*s added with id %d",
	      (int)cfg->id.slen, cfg->id.ptr, id));

    /* If accounts has registration enabled, start registration */
    if (pjsua_var.acc[id].cfg.reg_uri.slen)
	pjsua_acc_set_registration(id, PJ_TRUE);


    return PJ_SUCCESS;
}


/*
 * Add local account
 */
PJ_DEF(pj_status_t) pjsua_acc_add_local( pjsua_transport_id tid,
					 pj_bool_t is_default,
					 pjsua_acc_id *p_acc_id)
{
    pjsua_acc_config cfg;
    pjsua_transport_data *t = &pjsua_var.tpdata[tid];
    char uri[PJSIP_MAX_URL_SIZE];

    /* ID must be valid */
    PJ_ASSERT_RETURN(tid>=0 && tid<(int)PJ_ARRAY_SIZE(pjsua_var.tpdata), 
		     PJ_EINVAL);

    /* Transport must be valid */
    PJ_ASSERT_RETURN(t->data.ptr != NULL, PJ_EINVAL);
    
    pjsua_acc_config_default(&cfg);

    /* Lower the priority of local account */
    --cfg.priority;

    /* Build URI for the account */
    pj_ansi_snprintf(uri, PJSIP_MAX_URL_SIZE,
		     "<sip:%.*s:%d;transport=%s>", 
		     (int)t->local_name.host.slen,
		     t->local_name.host.ptr,
		     t->local_name.port,
		     pjsip_transport_get_type_name(t->type));

    cfg.id = pj_str(uri);
    
    return pjsua_acc_add(&cfg, is_default, p_acc_id);
}


/*
 * Delete account.
 */
PJ_DEF(pj_status_t) pjsua_acc_del(pjsua_acc_id acc_id)
{
    unsigned i;

    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();

    /* Delete registration */
    if (pjsua_var.acc[acc_id].regc != NULL) {
	pjsua_acc_set_registration(acc_id, PJ_FALSE);
	if (pjsua_var.acc[acc_id].regc) {
	    pjsip_regc_destroy(pjsua_var.acc[acc_id].regc);
	}
	pjsua_var.acc[acc_id].regc = NULL;
    }

    /* Delete server presence subscription */
    pjsua_pres_delete_acc(acc_id);

    /* Invalidate */
    pjsua_var.acc[acc_id].valid = PJ_FALSE;

    /* Remove from array */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	if (pjsua_var.acc_ids[i] == acc_id)
	    break;
    }
    if (i != pjsua_var.acc_cnt) {
	pj_array_erase(pjsua_var.acc_ids, sizeof(pjsua_var.acc_ids[0]),
		       pjsua_var.acc_cnt, i);
	--pjsua_var.acc_cnt;
    }

    /* Leave the calls intact, as I don't think calls need to
     * access account once it's created
     */

    /* Update default account */
    if (pjsua_var.default_acc == acc_id)
	pjsua_var.default_acc = 0;

    PJSUA_UNLOCK();

    PJ_LOG(4,(THIS_FILE, "Account id %d deleted", acc_id));

    return PJ_SUCCESS;
}


/*
 * Modify account information.
 */
PJ_DEF(pj_status_t) pjsua_acc_modify( pjsua_acc_id acc_id,
				      const pjsua_acc_config *cfg)
{
    PJ_TODO(pjsua_acc_modify);
    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(cfg);
    return PJ_EINVALIDOP;
}


/*
 * Modify account's presence status to be advertised to remote/presence
 * subscribers.
 */
PJ_DEF(pj_status_t) pjsua_acc_set_online_status( pjsua_acc_id acc_id,
						 pj_bool_t is_online)
{
    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    pjsua_var.acc[acc_id].online_status = is_online;
    pj_bzero(&pjsua_var.acc[acc_id].rpid, sizeof(pjrpid_element));
    pjsua_pres_update_acc(acc_id, PJ_FALSE);
    return PJ_SUCCESS;
}


/* 
 * Set online status with extended information 
 */
PJ_DEF(pj_status_t) pjsua_acc_set_online_status2( pjsua_acc_id acc_id,
						  pj_bool_t is_online,
						  const pjrpid_element *pr)
{
    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    pjsua_var.acc[acc_id].online_status = is_online;
    pjrpid_element_dup(pjsua_var.pool, &pjsua_var.acc[acc_id].rpid, pr);
    pjsua_pres_update_acc(acc_id, PJ_TRUE);
    return PJ_SUCCESS;
}


/* Update NAT address from the REGISTER response */
static pj_bool_t acc_check_nat_addr(pjsua_acc *acc,
				    struct pjsip_regc_cbparam *param)
{
    pjsip_transport *tp;
    const pj_str_t *via_addr;
    int rport;
    pjsip_via_hdr *via;

    tp = param->rdata->tp_info.transport;

    /* Only update if account is configured to auto-update */
    if (acc->cfg.auto_update_nat == PJ_FALSE)
	return PJ_FALSE;

    /* Only update if registration uses UDP transport */
    if (tp->key.type != PJSIP_TRANSPORT_UDP)
	return PJ_FALSE;

    /* Only update if STUN is enabled (for now) */
    if (pjsua_var.ua_cfg.stun_domain.slen == 0 &&
	pjsua_var.ua_cfg.stun_host.slen == 0)
    {
	return PJ_FALSE;
    }

    /* Get the received and rport info */
    via = param->rdata->msg_info.via;
    if (via->rport_param < 1) {
	/* Remote doesn't support rport */
	rport = via->sent_by.port;
    } else
	rport = via->rport_param;

    if (via->recvd_param.slen != 0)
	via_addr = &via->recvd_param;
    else
	via_addr = &via->sent_by.host;

    /* Compare received and rport with transport published address */
    if (tp->local_name.port == rport &&
	pj_stricmp(&tp->local_name.host, via_addr)==0)
    {
	/* Address doesn't change */
	return PJ_FALSE;
    }

    /* At this point we've detected that the address as seen by registrar.
     * has changed.
     */
    PJ_LOG(3,(THIS_FILE, "IP address change detected for account %d "
			 "(%.*s:%d --> %.*s:%d). Updating registration..",
			 acc->index,
			 (int)tp->local_name.host.slen,
			 tp->local_name.host.ptr,
			 tp->local_name.port,
			 (int)via_addr->slen,
			 via_addr->ptr,
			 rport));

    /* Unregister current contact */
    pjsua_acc_set_registration(acc->index, PJ_FALSE);
    if (acc->regc != NULL) {
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;
    }

    /* Update transport address */
    pj_strdup_with_null(tp->pool, &tp->local_name.host, via_addr);
    tp->local_name.port = rport;

    /* Perform new registration */
    pjsua_acc_set_registration(acc->index, PJ_TRUE);

    return PJ_TRUE;
}

/* Check and update Service-Route header */
void update_service_route(pjsua_acc *acc, pjsip_rx_data *rdata)
{
    pjsip_generic_string_hdr *hsr = NULL;
    pjsip_route_hdr *hr, *h;
    const pj_str_t HNAME = { "Service-Route", 13 };
    const pj_str_t HROUTE = { "Route", 5 };
    pjsip_uri *uri[PJSUA_ACC_MAX_PROXIES];
    unsigned i, uri_cnt = 0, rcnt;

    /* Find and parse Service-Route headers */
    for (;;) {
	char saved;
	int parsed_len;

	/* Find Service-Route header */
	hsr = (pjsip_generic_string_hdr*)
	      pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &HNAME, hsr);
	if (!hsr)
	    break;

	/* Parse as Route header since the syntax is similar. This may
	 * return more than one headers.
	 */
	saved = hsr->hvalue.ptr[hsr->hvalue.slen];
	hsr->hvalue.ptr[hsr->hvalue.slen] = '\0';
	hr = (pjsip_route_hdr*)
	     pjsip_parse_hdr(rdata->tp_info.pool, &HROUTE, hsr->hvalue.ptr,
			     hsr->hvalue.slen, &parsed_len);
	hsr->hvalue.ptr[hsr->hvalue.slen] = saved;

	if (hr == NULL) {
	    /* Error */
	    PJ_LOG(1,(THIS_FILE, "Error parsing Service-Route header"));
	    return;
	}

	/* Save each URI in the result */
	h = hr;
	do {
	    if (!PJSIP_URI_SCHEME_IS_SIP(h->name_addr.uri) &&
		!PJSIP_URI_SCHEME_IS_SIPS(h->name_addr.uri))
	    {
		PJ_LOG(1,(THIS_FILE,"Error: non SIP URI in Service-Route: %.*s",
			  (int)hsr->hvalue.slen, hsr->hvalue.ptr));
		return;
	    }

	    uri[uri_cnt++] = h->name_addr.uri;
	    h = h->next;
	} while (h != hr && uri_cnt != PJ_ARRAY_SIZE(uri));

	if (h != hr) {
	    PJ_LOG(1,(THIS_FILE, "Error: too many Service-Route headers"));
	    return;
	}

	/* Prepare to find next Service-Route header */
	hsr = hsr->next;
	if ((void*)hsr == (void*)&rdata->msg_info.msg->hdr)
	    break;
    }

    if (uri_cnt == 0)
	return;

    /* 
     * Update account's route set 
     */
    
    /* First remove all routes which are not the outbound proxies */
    rcnt = pj_list_size(&acc->route_set);
    if (rcnt != pjsua_var.ua_cfg.outbound_proxy_cnt + acc->cfg.proxy_cnt) {
	for (i=pjsua_var.ua_cfg.outbound_proxy_cnt + acc->cfg.proxy_cnt, 
		hr=acc->route_set.prev; 
	     i<rcnt; 
	     ++i)
	 {
	    pjsip_route_hdr *prev = hr->prev;
	    pj_list_erase(hr);
	    hr = prev;
	 }
    }

    /* Then append the Service-Route URIs */
    for (i=0; i<uri_cnt; ++i) {
	hr = pjsip_route_hdr_create(pjsua_var.pool);
	hr->name_addr.uri = (pjsip_uri*)pjsip_uri_clone(pjsua_var.pool, uri[i]);
	pj_list_push_back(&acc->route_set, hr);
    }

    /* Done */

    PJ_LOG(4,(THIS_FILE, "Service-Route updated for acc %d with %d URI(s)",
	      acc->index, uri_cnt));
}


/* Keep alive timer callback */
static void keep_alive_timer_cb(pj_timer_heap_t *th, pj_timer_entry *te)
{
    pjsua_acc *acc;
    pjsip_tpselector tp_sel;
    pj_time_val delay;
    pj_status_t status;

    PJ_UNUSED_ARG(th);

    PJSUA_LOCK();

    te->id = PJ_FALSE;

    acc = (pjsua_acc*) te->user_data;

    /* Select the transport to send the packet */
    pj_bzero(&tp_sel, sizeof(tp_sel));
    tp_sel.type = PJSIP_TPSELECTOR_TRANSPORT;
    tp_sel.u.transport = acc->ka_transport;

    PJ_LOG(5,(THIS_FILE, 
	      "Sending %d bytes keep-alive packet for acc %d to %s:%d",
	      acc->cfg.ka_data.slen, acc->index,
	      pj_inet_ntoa(acc->ka_target.ipv4.sin_addr),
	      pj_ntohs(acc->ka_target.ipv4.sin_port)));

    /* Send raw packet */
    status = pjsip_tpmgr_send_raw(pjsip_endpt_get_tpmgr(pjsua_var.endpt),
				  PJSIP_TRANSPORT_UDP, &tp_sel,
				  NULL, acc->cfg.ka_data.ptr, 
				  acc->cfg.ka_data.slen, 
				  &acc->ka_target, acc->ka_target_len,
				  NULL, NULL);

    if (status != PJ_SUCCESS && status != PJ_EPENDING) {
	pjsua_perror(THIS_FILE, "Error sending keep-alive packet", status);
    }

    /* Reschedule next timer */
    delay.sec = acc->cfg.ka_interval;
    delay.msec = 0;
    status = pjsip_endpt_schedule_timer(pjsua_var.endpt, te, &delay);
    if (status == PJ_SUCCESS) {
	te->id = PJ_TRUE;
    } else {
	pjsua_perror(THIS_FILE, "Error starting keep-alive timer", status);
    }

    PJSUA_UNLOCK();
}


/* Update keep-alive for the account */
static void update_keep_alive(pjsua_acc *acc, pj_bool_t start,
			      struct pjsip_regc_cbparam *param)
{
    /* In all cases, stop keep-alive timer if it's running. */
    if (acc->ka_timer.id) {
	pjsip_endpt_cancel_timer(pjsua_var.endpt, &acc->ka_timer);
	acc->ka_timer.id = PJ_FALSE;

	pjsip_transport_dec_ref(acc->ka_transport);
	acc->ka_transport = NULL;
    }

    if (start) {
	pj_time_val delay;
	pj_status_t status;

	/* Only do keep-alive if:
	 *  - STUN is enabled in global config, and
	 *  - ka_interval is not zero in the account, and
	 *  - transport is UDP.
	 */
	if (pjsua_var.stun_srv.ipv4.sin_family == 0 ||
	    acc->cfg.ka_interval == 0 ||
	    param->rdata->tp_info.transport->key.type != PJSIP_TRANSPORT_UDP)
	{
	    /* Keep alive is not necessary */
	    return;
	}

	/* Save transport and destination address. */
	acc->ka_transport = param->rdata->tp_info.transport;
	pjsip_transport_add_ref(acc->ka_transport);
	pj_memcpy(&acc->ka_target, &param->rdata->pkt_info.src_addr,
		  param->rdata->pkt_info.src_addr_len);
	acc->ka_target_len = param->rdata->pkt_info.src_addr_len;

	/* Setup and start the timer */
	acc->ka_timer.cb = &keep_alive_timer_cb;
	acc->ka_timer.user_data = (void*)acc;

	delay.sec = acc->cfg.ka_interval;
	delay.msec = 0;
	status = pjsip_endpt_schedule_timer(pjsua_var.endpt, &acc->ka_timer, 
					    &delay);
	if (status == PJ_SUCCESS) {
	    acc->ka_timer.id = PJ_TRUE;
	    PJ_LOG(4,(THIS_FILE, "Keep-alive timer started for acc %d, "
				 "destination:%s:%d, interval:%ds",
				 acc->index,
				 param->rdata->pkt_info.src_name,
				 param->rdata->pkt_info.src_port,
				 acc->cfg.ka_interval));
	} else {
	    acc->ka_timer.id = PJ_FALSE;
	    pjsip_transport_dec_ref(acc->ka_transport);
	    acc->ka_transport = NULL;
	    pjsua_perror(THIS_FILE, "Error starting keep-alive timer", status);
	}
    }
}


/*
 * This callback is called by pjsip_regc when outgoing register
 * request has completed.
 */
static void regc_cb(struct pjsip_regc_cbparam *param)
{

    pjsua_acc *acc = (pjsua_acc*) param->token;

    if (param->regc != acc->regc)
	return;

    PJSUA_LOCK();

    /*
     * Print registration status.
     */
    if (param->status!=PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "SIP registration error", 
		     param->status);
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;
	
	/* Stop keep-alive timer if any. */
	update_keep_alive(acc, PJ_FALSE, NULL);

    } else if (param->code < 0 || param->code >= 300) {
	PJ_LOG(2, (THIS_FILE, "SIP registration failed, status=%d (%.*s)", 
		   param->code, 
		   (int)param->reason.slen, param->reason.ptr));
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;

	/* Stop keep-alive timer if any. */
	update_keep_alive(acc, PJ_FALSE, NULL);

    } else if (PJSIP_IS_STATUS_IN_CLASS(param->code, 200)) {

	if (param->expiration < 1) {
	    pjsip_regc_destroy(acc->regc);
	    acc->regc = NULL;

	    /* Stop keep-alive timer if any. */
	    update_keep_alive(acc, PJ_FALSE, NULL);

	    PJ_LOG(3,(THIS_FILE, "%s: unregistration success",
		      pjsua_var.acc[acc->index].cfg.id.ptr));
	} else {
	    /* Check NAT bound address */
	    if (acc_check_nat_addr(acc, param)) {
		/* Update address, don't notify application yet */
		PJSUA_UNLOCK();
		return;
	    }

	    /* Check and update Service-Route header */
	    update_service_route(acc, param->rdata);

	    PJ_LOG(3, (THIS_FILE, 
		       "%s: registration success, status=%d (%.*s), "
		       "will re-register in %d seconds", 
		       pjsua_var.acc[acc->index].cfg.id.ptr,
		       param->code,
		       (int)param->reason.slen, param->reason.ptr,
		       param->expiration));

	    /* Start keep-alive timer if necessary. */
	    update_keep_alive(acc, PJ_TRUE, param);

	    /* Send initial PUBLISH if it is enabled */
	    if (acc->cfg.publish_enabled && acc->publish_sess==NULL)
		pjsua_pres_init_publish_acc(acc->index);
	}

    } else {
	PJ_LOG(4, (THIS_FILE, "SIP registration updated status=%d", param->code));
    }

    acc->reg_last_err = param->status;
    acc->reg_last_code = param->code;

    if (pjsua_var.ua_cfg.cb.on_reg_state)
	(*pjsua_var.ua_cfg.cb.on_reg_state)(acc->index);

    PJSUA_UNLOCK();
}


/*
 * Initialize client registration.
 */
static pj_status_t pjsua_regc_init(int acc_id)
{
    pjsua_acc *acc;
    pj_str_t contact;
    char contact_buf[1024];
    pj_pool_t *pool;
    pj_status_t status;

    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    if (acc->cfg.reg_uri.slen == 0) {
	PJ_LOG(3,(THIS_FILE, "Registrar URI is not specified"));
	return PJ_SUCCESS;
    }

    /* Destroy existing session, if any */
    if (acc->regc) {
	pjsip_regc_destroy(acc->regc);
	acc->regc = NULL;
    }

    /* initialize SIP registration if registrar is configured */

    status = pjsip_regc_create( pjsua_var.endpt, 
				acc, &regc_cb, &acc->regc);

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create client registration", 
		     status);
	return status;
    }

    pool = pj_pool_create_on_buf(NULL, contact_buf, sizeof(contact_buf));
    status = pjsua_acc_create_uac_contact( pool, &contact,
					   acc_id, &acc->cfg.reg_uri);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to generate suitable Contact header"
				" for registration", 
		     status);
	return status;
    }

    status = pjsip_regc_init( acc->regc,
			      &acc->cfg.reg_uri, 
			      &acc->cfg.id, 
			      &acc->cfg.id,
			      1, &contact, 
			      acc->cfg.reg_timeout);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Client registration initialization error", 
		     status);
	return status;
    }

    /* If account is locked to specific transport, then set transport to
     * the client registration.
     */
    if (pjsua_var.acc[acc_id].cfg.transport_id != PJSUA_INVALID_ID) {
	pjsip_tpselector tp_sel;

	pjsua_init_tpselector(pjsua_var.acc[acc_id].cfg.transport_id, &tp_sel);
	pjsip_regc_set_transport(acc->regc, &tp_sel);
    }


    /* Set credentials
     */
    if (acc->cred_cnt) {
	pjsip_regc_set_credentials( acc->regc, acc->cred_cnt, acc->cred);
    }

    /* Set authentication preference */
    pjsip_regc_set_prefs(acc->regc, &acc->cfg.auth_pref);

    /* Set route-set
     */
    if (!pj_list_empty(&acc->route_set)) {
	pjsip_regc_set_route_set( acc->regc, &acc->route_set );
    }

    /* Add other request headers. */
    if (pjsua_var.ua_cfg.user_agent.slen) {
	pjsip_hdr hdr_list;
	const pj_str_t STR_USER_AGENT = { "User-Agent", 10 };
	pjsip_generic_string_hdr *h;

	pool = pj_pool_create_on_buf(NULL, contact_buf, sizeof(contact_buf));
	pj_list_init(&hdr_list);

	h = pjsip_generic_string_hdr_create(pool, &STR_USER_AGENT, 
					    &pjsua_var.ua_cfg.user_agent);
	pj_list_push_back(&hdr_list, (pjsip_hdr*)h);

	pjsip_regc_add_headers(acc->regc, &hdr_list);
    }

    return PJ_SUCCESS;
}


/*
 * Update registration or perform unregistration. 
 */
PJ_DEF(pj_status_t) pjsua_acc_set_registration( pjsua_acc_id acc_id, 
						pj_bool_t renew)
{
    pj_status_t status = 0;
    pjsip_tx_data *tdata = 0;

    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc),
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();

    if (renew) {
	if (pjsua_var.acc[acc_id].regc == NULL) {
	    status = pjsua_regc_init(acc_id);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Unable to create registration", 
			     status);
		goto on_return;
	    }
	}
	if (!pjsua_var.acc[acc_id].regc) {
	    status = PJ_EINVALIDOP;
	    goto on_return;
	}

	status = pjsip_regc_register(pjsua_var.acc[acc_id].regc, 1, 
				     &tdata);

	if (0 && status == PJ_SUCCESS && pjsua_var.acc[acc_id].cred_cnt) {
	    pjsua_acc *acc = &pjsua_var.acc[acc_id];
	    pjsip_authorization_hdr *h;
	    char *uri;
	    int d;

	    uri = (char*) pj_pool_alloc(tdata->pool, acc->cfg.reg_uri.slen+10);
	    d = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, tdata->msg->line.req.uri,
				uri, acc->cfg.reg_uri.slen+10);
	    pj_assert(d > 0);

	    h = pjsip_authorization_hdr_create(tdata->pool);
	    h->scheme = pj_str("Digest");
	    h->credential.digest.username = acc->cred[0].username;
	    h->credential.digest.realm = acc->srv_domain;
	    h->credential.digest.uri = pj_str(uri);
	    h->credential.digest.algorithm = pj_str("md5");

	    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)h);
	}

    } else {
	if (pjsua_var.acc[acc_id].regc == NULL) {
	    PJ_LOG(3,(THIS_FILE, "Currently not registered"));
	    status = PJ_EINVALIDOP;
	    goto on_return;
	}
	status = pjsip_regc_unregister(pjsua_var.acc[acc_id].regc, &tdata);
    }

    if (status == PJ_SUCCESS) {
	//pjsua_process_msg_data(tdata, NULL);
	status = pjsip_regc_send( pjsua_var.acc[acc_id].regc, tdata );
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create/send REGISTER", 
		     status);
    } else {
	PJ_LOG(3,(THIS_FILE, "%s sent",
	         (renew? "Registration" : "Unregistration")));
    }

on_return:
    PJSUA_UNLOCK();
    return status;
}


/*
 * Get account information.
 */
PJ_DEF(pj_status_t) pjsua_acc_get_info( pjsua_acc_id acc_id,
					pjsua_acc_info *info)
{
    pjsua_acc *acc = &pjsua_var.acc[acc_id];
    pjsua_acc_config *acc_cfg = &pjsua_var.acc[acc_id].cfg;

    PJ_ASSERT_RETURN(info != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    
    pj_bzero(info, sizeof(pjsua_acc_info));

    PJ_ASSERT_RETURN(acc_id>=0 && acc_id<(int)PJ_ARRAY_SIZE(pjsua_var.acc), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.acc[acc_id].valid, PJ_EINVALIDOP);

    PJSUA_LOCK();
    
    if (pjsua_var.acc[acc_id].valid == PJ_FALSE) {
	PJSUA_UNLOCK();
	return PJ_EINVALIDOP;
    }

    info->id = acc_id;
    info->is_default = (pjsua_var.default_acc == acc_id);
    info->acc_uri = acc_cfg->id;
    info->has_registration = (acc->cfg.reg_uri.slen > 0);
    info->online_status = acc->online_status;
    pj_memcpy(&info->rpid, &acc->rpid, sizeof(pjrpid_element));
    if (info->rpid.note.slen)
	info->online_status_text = info->rpid.note;
    else if (info->online_status)
	info->online_status_text = pj_str("Online");
    else
	info->online_status_text = pj_str("Offline");

    if (acc->reg_last_err) {
	info->status = (pjsip_status_code) acc->reg_last_err;
	pj_strerror(acc->reg_last_err, info->buf_, sizeof(info->buf_));
	info->status_text = pj_str(info->buf_);
    } else if (acc->reg_last_code) {
	if (info->has_registration) {
	    info->status = (pjsip_status_code) acc->reg_last_code;
	    info->status_text = *pjsip_get_status_text(acc->reg_last_code);
	} else {
	    info->status = (pjsip_status_code) 0;
	    info->status_text = pj_str("not registered");
	}
    } else if (acc->cfg.reg_uri.slen) {
	info->status = PJSIP_SC_TRYING;
	info->status_text = pj_str("In Progress");
    } else {
	info->status = (pjsip_status_code) 0;
	info->status_text = pj_str("does not register");
    }
    
    if (acc->regc) {
	pjsip_regc_info regc_info;
	pjsip_regc_get_info(acc->regc, &regc_info);
	info->expires = regc_info.next_reg;
    } else {
	info->expires = -1;
    }

    PJSUA_UNLOCK();

    return PJ_SUCCESS;

}


/*
 * Enum accounts all account ids.
 */
PJ_DEF(pj_status_t) pjsua_enum_accs(pjsua_acc_id ids[],
				    unsigned *count )
{
    unsigned i, c;

    PJ_ASSERT_RETURN(ids && *count, PJ_EINVAL);

    PJSUA_LOCK();

    for (i=0, c=0; c<*count && i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	if (!pjsua_var.acc[i].valid)
	    continue;
	ids[c] = i;
	++c;
    }

    *count = c;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*
 * Enum accounts info.
 */
PJ_DEF(pj_status_t) pjsua_acc_enum_info( pjsua_acc_info info[],
					 unsigned *count )
{
    unsigned i, c;

    PJ_ASSERT_RETURN(info && *count, PJ_EINVAL);

    PJSUA_LOCK();

    for (i=0, c=0; c<*count && i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	if (!pjsua_var.acc[i].valid)
	    continue;

	pjsua_acc_get_info(i, &info[c]);
	++c;
    }

    *count = c;

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*
 * This is an internal function to find the most appropriate account to
 * used to reach to the specified URL.
 */
PJ_DEF(pjsua_acc_id) pjsua_acc_find_for_outgoing(const pj_str_t *url)
{
    pj_str_t tmp;
    pjsip_uri *uri;
    pjsip_sip_uri *sip_uri;
    unsigned i;

    PJSUA_LOCK();

    PJ_TODO(dont_use_pjsua_pool);

    pj_strdup_with_null(pjsua_var.pool, &tmp, url);

    uri = pjsip_parse_uri(pjsua_var.pool, tmp.ptr, tmp.slen, 0);
    if (!uri) {
	PJSUA_UNLOCK();
	return pjsua_var.default_acc;
    }

    if (!PJSIP_URI_SCHEME_IS_SIP(uri) && 
	!PJSIP_URI_SCHEME_IS_SIPS(uri)) 
    {
	/* Return the first account with proxy */
	for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.acc); ++i) {
	    if (!pjsua_var.acc[i].valid)
		continue;
	    if (!pj_list_empty(&pjsua_var.acc[i].route_set))
		break;
	}

	if (i != PJ_ARRAY_SIZE(pjsua_var.acc)) {
	    /* Found rather matching account */
	    PJSUA_UNLOCK();
	    return 0;
	}

	/* Not found, use default account */
	PJSUA_UNLOCK();
	return pjsua_var.default_acc;
    }

    sip_uri = (pjsip_sip_uri*) pjsip_uri_get_uri(uri);

    /* Find matching domain AND port */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	if (pj_stricmp(&pjsua_var.acc[acc_id].srv_domain, &sip_uri->host)==0 &&
	    pjsua_var.acc[acc_id].srv_port == sip_uri->port)
	{
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* If no match, try to match the domain part only */
    for (i=0; i<pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	if (pj_stricmp(&pjsua_var.acc[acc_id].srv_domain, &sip_uri->host)==0)
	{
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }


    /* Still no match, just use default account */
    PJSUA_UNLOCK();
    return pjsua_var.default_acc;
}


/*
 * This is an internal function to find the most appropriate account to be
 * used to handle incoming calls.
 */
PJ_DEF(pjsua_acc_id) pjsua_acc_find_for_incoming(pjsip_rx_data *rdata)
{
    pjsip_uri *uri;
    pjsip_sip_uri *sip_uri;
    unsigned i;

    /* Check that there's at least one account configured */
    PJ_ASSERT_RETURN(pjsua_var.acc_cnt!=0, pjsua_var.default_acc);

    uri = rdata->msg_info.to->uri;

    /* Just return default account if To URI is not SIP: */
    if (!PJSIP_URI_SCHEME_IS_SIP(uri) && 
	!PJSIP_URI_SCHEME_IS_SIPS(uri)) 
    {
	return pjsua_var.default_acc;
    }


    PJSUA_LOCK();

    sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);

    /* Find account which has matching username and domain. */
    for (i=0; i < pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	pjsua_acc *acc = &pjsua_var.acc[acc_id];

	if (acc->valid && pj_stricmp(&acc->user_part, &sip_uri->user)==0 &&
	    pj_stricmp(&acc->srv_domain, &sip_uri->host)==0) 
	{
	    /* Match ! */
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* No matching account, try match domain part only. */
    for (i=0; i < pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	pjsua_acc *acc = &pjsua_var.acc[acc_id];

	if (acc->valid && pj_stricmp(&acc->srv_domain, &sip_uri->host)==0) {
	    /* Match ! */
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* No matching account, try match user part (and transport type) only. */
    for (i=0; i < pjsua_var.acc_cnt; ++i) {
	unsigned acc_id = pjsua_var.acc_ids[i];
	pjsua_acc *acc = &pjsua_var.acc[acc_id];

	if (acc->valid && pj_stricmp(&acc->user_part, &sip_uri->user)==0) {

	    if (acc->cfg.transport_id != PJSUA_INVALID_ID) {
		pjsip_transport_type_e type;
		type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
		if (type == PJSIP_TRANSPORT_UNSPECIFIED)
		    type = PJSIP_TRANSPORT_UDP;

		if (pjsua_var.tpdata[acc->cfg.transport_id].type != type)
		    continue;
	    }

	    /* Match ! */
	    PJSUA_UNLOCK();
	    return acc_id;
	}
    }

    /* Still no match, use default account */
    PJSUA_UNLOCK();
    return pjsua_var.default_acc;
}


/*
 * Create arbitrary requests for this account. 
 */
PJ_DEF(pj_status_t) pjsua_acc_create_request(pjsua_acc_id acc_id,
					     const pjsip_method *method,
					     const pj_str_t *target,
					     pjsip_tx_data **p_tdata)
{
    pjsip_tx_data *tdata;
    pjsua_acc *acc;
    pjsip_route_hdr *r;
    pj_status_t status;

    PJ_ASSERT_RETURN(method && target && p_tdata, PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);

    acc = &pjsua_var.acc[acc_id];

    status = pjsip_endpt_create_request(pjsua_var.endpt, method, target, 
					&acc->cfg.id, target,
					NULL, NULL, -1, NULL, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create request", status);
	return status;
    }

    /* Copy routeset */
    r = acc->route_set.next;
    while (r != &acc->route_set) {
	pjsip_msg_add_hdr(tdata->msg, 
			  (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, r));
	r = r->next;
    }
    
    /* Done */
    *p_tdata = tdata;
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjsua_acc_create_uac_contact( pj_pool_t *pool,
						  pj_str_t *contact,
						  pjsua_acc_id acc_id,
						  const pj_str_t *suri)
{
    pjsua_acc *acc;
    pjsip_sip_uri *sip_uri;
    pj_status_t status;
    pjsip_transport_type_e tp_type = PJSIP_TRANSPORT_UNSPECIFIED;
    pj_str_t local_addr;
    pjsip_tpselector tp_sel;
    unsigned flag;
    int secure;
    int local_port;
    
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    /* If force_contact is configured, then use use it */
    if (acc->cfg.force_contact.slen) {
	*contact = acc->cfg.force_contact;
	return PJ_SUCCESS;
    }

    /* If route-set is configured for the account, then URI is the 
     * first entry of the route-set.
     */
    if (!pj_list_empty(&acc->route_set)) {
	sip_uri = (pjsip_sip_uri*) 
		  pjsip_uri_get_uri(acc->route_set.next->name_addr.uri);
    } else {
	pj_str_t tmp;
	pjsip_uri *uri;

	pj_strdup_with_null(pool, &tmp, suri);

	uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
	if (uri == NULL)
	    return PJSIP_EINVALIDURI;

	/* For non-SIP scheme, route set should be configured */
	if (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))
	    return PJSIP_EINVALIDREQURI;

	sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);
    }

    /* Get transport type of the URI */
    if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri))
	tp_type = PJSIP_TRANSPORT_TLS;
    else if (sip_uri->transport_param.slen == 0) {
	tp_type = PJSIP_TRANSPORT_UDP;
    } else
	tp_type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
    
    if (tp_type == PJSIP_TRANSPORT_UNSPECIFIED)
	return PJSIP_EUNSUPTRANSPORT;

    flag = pjsip_transport_get_flag_from_type(tp_type);
    secure = (flag & PJSIP_TRANSPORT_SECURE) != 0;

    /* Init transport selector. */
    pjsua_init_tpselector(pjsua_var.acc[acc_id].cfg.transport_id, &tp_sel);

    /* Get local address suitable to send request from */
    status = pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(pjsua_var.endpt),
					 pool, tp_type, &tp_sel, 
					 &local_addr, &local_port);
    if (status != PJ_SUCCESS)
	return status;

    /* Create the contact header */
    contact->ptr = (char*)pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
    contact->slen = pj_ansi_snprintf(contact->ptr, PJSIP_MAX_URL_SIZE,
				     "%.*s%s<%s:%.*s%s%.*s:%d;transport=%s>",
				     (int)acc->display.slen,
				     acc->display.ptr,
				     (acc->display.slen?" " : ""),
				     (secure ? PJSUA_SECURE_SCHEME : "sip"),
				     (int)acc->user_part.slen,
				     acc->user_part.ptr,
				     (acc->user_part.slen?"@":""),
				     (int)local_addr.slen,
				     local_addr.ptr,
				     local_port,
				     pjsip_transport_get_type_name(tp_type));

    return PJ_SUCCESS;
}



PJ_DEF(pj_status_t) pjsua_acc_create_uas_contact( pj_pool_t *pool,
						  pj_str_t *contact,
						  pjsua_acc_id acc_id,
						  pjsip_rx_data *rdata )
{
    /* 
     *  Section 12.1.1, paragraph about using SIPS URI in Contact.
     *  If the request that initiated the dialog contained a SIPS URI 
     *  in the Request-URI or in the top Record-Route header field value, 
     *  if there was any, or the Contact header field if there was no 
     *  Record-Route header field, the Contact header field in the response
     *  MUST be a SIPS URI.
     */
    pjsua_acc *acc;
    pjsip_sip_uri *sip_uri;
    pj_status_t status;
    pjsip_transport_type_e tp_type = PJSIP_TRANSPORT_UNSPECIFIED;
    pj_str_t local_addr;
    pjsip_tpselector tp_sel;
    unsigned flag;
    int secure;
    int local_port;
    
    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    /* If force_contact is configured, then use use it */
    if (acc->cfg.force_contact.slen) {
	*contact = acc->cfg.force_contact;
	return PJ_SUCCESS;
    }

    /* If Record-Route is present, then URI is the top Record-Route. */
    if (rdata->msg_info.record_route) {
	sip_uri = (pjsip_sip_uri*) 
		pjsip_uri_get_uri(rdata->msg_info.record_route->name_addr.uri);
    } else {
	pjsip_contact_hdr *h_contact;
	pjsip_uri *uri = NULL;

	/* Otherwise URI is Contact URI */
	h_contact = (pjsip_contact_hdr*)
		    pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT,
				       NULL);
	if (h_contact)
	    uri = (pjsip_uri*) pjsip_uri_get_uri(h_contact->uri);
	

	/* Or if Contact URI is not present, take the remote URI from
	 * the From URI.
	 */
	if (uri == NULL)
	    uri = (pjsip_uri*) pjsip_uri_get_uri(rdata->msg_info.from->uri);


	/* Can only do sip/sips scheme at present. */
	if (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))
	    return PJSIP_EINVALIDREQURI;

	sip_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(uri);
    }

    /* Get transport type of the URI */
    if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri))
	tp_type = PJSIP_TRANSPORT_TLS;
    else if (sip_uri->transport_param.slen == 0) {
	tp_type = PJSIP_TRANSPORT_UDP;
    } else
	tp_type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
    
    if (tp_type == PJSIP_TRANSPORT_UNSPECIFIED)
	return PJSIP_EUNSUPTRANSPORT;

    flag = pjsip_transport_get_flag_from_type(tp_type);
    secure = (flag & PJSIP_TRANSPORT_SECURE) != 0;

    /* Init transport selector. */
    pjsua_init_tpselector(pjsua_var.acc[acc_id].cfg.transport_id, &tp_sel);

    /* Get local address suitable to send request from */
    status = pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(pjsua_var.endpt),
					 pool, tp_type, &tp_sel,
					 &local_addr, &local_port);
    if (status != PJ_SUCCESS)
	return status;

    /* Create the contact header */
    contact->ptr = (char*) pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
    contact->slen = pj_ansi_snprintf(contact->ptr, PJSIP_MAX_URL_SIZE,
				     "%.*s%s<%s:%.*s%s%.*s:%d;transport=%s>",
				     (int)acc->display.slen,
				     acc->display.ptr,
				     (acc->display.slen?" " : ""),
				     (secure ? PJSUA_SECURE_SCHEME : "sip"),
				     (int)acc->user_part.slen,
				     acc->user_part.ptr,
				     (acc->user_part.slen?"@":""),
				     (int)local_addr.slen,
				     local_addr.ptr,
				     local_port,
				     pjsip_transport_get_type_name(tp_type));

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjsua_acc_set_transport( pjsua_acc_id acc_id,
					     pjsua_transport_id tp_id)
{
    pjsua_acc *acc;

    PJ_ASSERT_RETURN(pjsua_acc_is_valid(acc_id), PJ_EINVAL);
    acc = &pjsua_var.acc[acc_id];

    PJ_ASSERT_RETURN(tp_id >= 0 && tp_id < (int)PJ_ARRAY_SIZE(pjsua_var.tpdata),
		     PJ_EINVAL);
    
    acc->cfg.transport_id = tp_id;

    return PJ_SUCCESS;
}

