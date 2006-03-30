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
#include <pj/log.h>


/*
 * pjsua_call.c
 *
 * Call (INVITE) related stuffs.
 */

#define THIS_FILE   "pjsua_inv.c"


#define REFRESH_CALL_TIMER	0x63
#define HANGUP_CALL_TIMER	0x64

/* Proto */
static void schedule_call_timer( pjsua_call *call, pj_timer_entry *e,
				 int timer_type, int duration );

/*
 * Timer callback when UAS needs to send re-INVITE to see if remote
 * is still there.
 */
static void call_on_timer(pj_timer_heap_t *ht, pj_timer_entry *e)
{
    pjsua_call *call = e->user_data;

    PJ_UNUSED_ARG(ht);

    if (e->id == REFRESH_CALL_TIMER) {

	/* If call is still not connected, hangup. */
	if (call->inv->state != PJSIP_INV_STATE_CONFIRMED) {
	    PJ_LOG(3,(THIS_FILE, "Refresh call timer is called when "
		      "invite is still not confirmed. Call %d will "
		      "disconnect.", call->index));
	    pjsua_call_hangup(call->index);
	} else {
	    PJ_LOG(3,(THIS_FILE, "Refreshing call %d", call->index));
	    schedule_call_timer(call,e,REFRESH_CALL_TIMER,pjsua.uas_refresh);
	    pjsua_call_reinvite(call->index);
	}

    } else if (e->id == HANGUP_CALL_TIMER) {
	PJ_LOG(3,(THIS_FILE, "Call %d duration exceeded, disconnecting call",
			     call->index));
	pjsua_call_hangup(call->index);

    }
}

/*
 * Schedule call timer.
 */
static void schedule_call_timer( pjsua_call *call, pj_timer_entry *e,
				 int timer_type, int duration )
{
    pj_time_val timeout;

    if (duration == 0) {
	/* Cancel timer. */
	if (e->id != 0) {
	    pjsip_endpt_cancel_timer(pjsua.endpt, e);
	    e->id = 0;
	}

    } else {
	/* Schedule timer. */
	timeout.sec = duration;
	timeout.msec = 0;

	e->cb = &call_on_timer;
	e->id = timer_type;
	e->user_data = call;

	pjsip_endpt_schedule_timer( pjsua.endpt, e, &timeout);
    }
}


/* Close and reopen socket. */
static pj_status_t reopen_sock( pj_sock_t *sock, pj_sockaddr_in *addr)
{
    pj_status_t status;

    status = pj_sock_socket(PJ_AF_INET, PJ_SOCK_DGRAM, 0, sock);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create socket", status);
	return status;
    }

    status = pj_sock_bind(*sock, addr, sizeof(pj_sockaddr_in));
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to re-bind RTP/RTCP socket", status);
	return status;
    }

    return PJ_SUCCESS;
}

/*
 * Destroy the call's media
 */
static pj_status_t call_destroy_media(int call_index)
{
    pjsua_call *call = &pjsua.calls[call_index];

    if (call->conf_slot > 0) {
	pjmedia_conf_remove_port(pjsua.mconf, call->conf_slot);
	call->conf_slot = 0;
    }

    if (call->session) {
	pj_sockaddr_in rtp_addr, rtcp_addr;
	int addrlen;

	addrlen = sizeof(rtp_addr);
	pj_sock_getsockname(call->skinfo.rtp_sock, &rtp_addr, &addrlen);

	addrlen = sizeof(rtcp_addr);
	pj_sock_getsockname(call->skinfo.rtcp_sock, &rtcp_addr, &addrlen);

	/* Destroy session (this will also close RTP/RTCP sockets). */
	pjmedia_session_destroy(call->session);

	/* Close and reopen RTP socket.
	 * This is necessary to get the socket unregistered from ioqueue,
	 * when IOCompletionPort is used.
	 */
	reopen_sock(&call->skinfo.rtp_sock, &rtp_addr);

	/* Close and reopen RTCP socket too. */
	reopen_sock(&call->skinfo.rtcp_sock, &rtcp_addr);

	call->session = NULL;

    }

    PJ_LOG(3,(THIS_FILE, "Media session for call %d is destroyed", 
			 call_index));

    return PJ_SUCCESS;
}


/**
 * Make outgoing call.
 */
pj_status_t pjsua_make_call(int acc_index,
			    const char *cstr_dest_uri,
			    int *p_call_index)
{
    pj_str_t dest_uri;
    pjsip_dialog *dlg = NULL;
    pjmedia_sdp_session *offer;
    pjsip_inv_session *inv = NULL;
    int call_index = -1;
    pjsip_tx_data *tdata;
    pj_status_t status;

    /* Convert cstr_dest_uri to dest_uri */
    
    dest_uri = pj_str((char*)cstr_dest_uri);

    /* Find free call slot. */
    for (call_index=0; call_index<pjsua.max_calls; ++call_index) {
	if (pjsua.calls[call_index].inv == NULL)
	    break;
    }

    if (call_index == pjsua.max_calls) {
	PJ_LOG(3,(THIS_FILE, "Error: too many calls!"));
	return PJ_ETOOMANY;
    }

    /* Create outgoing dialog: */

    status = pjsip_dlg_create_uac( pjsip_ua_instance(), 
				   &pjsua.acc[acc_index].local_uri,
				   &pjsua.acc[acc_index].contact_uri, 
				   &dest_uri, &dest_uri,
				   &dlg);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Dialog creation failed", status);
	return status;
    }

    /* Get media capability from media endpoint: */

    status = pjmedia_endpt_create_sdp( pjsua.med_endpt, dlg->pool, 1, 
				       &pjsua.calls[call_index].skinfo, 
				       &offer);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "pjmedia unable to create SDP", status);
	goto on_error;
    }

    /* Create the INVITE session: */

    status = pjsip_inv_create_uac( dlg, offer, 0, &inv);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Invite session creation failed", status);
	goto on_error;
    }


    /* Create and associate our data in the session. */

    pjsua.calls[call_index].inv = inv;

    dlg->mod_data[pjsua.mod.id] = &pjsua.calls[call_index];
    inv->mod_data[pjsua.mod.id] = &pjsua.calls[call_index];


    /* Set dialog Route-Set: */

    if (!pj_list_empty(&pjsua.acc[acc_index].route_set))
	pjsip_dlg_set_route_set(dlg, &pjsua.acc[acc_index].route_set);


    /* Set credentials: */

    pjsip_auth_clt_set_credentials( &dlg->auth_sess, pjsua.cred_count, 
				    pjsua.cred_info);


    /* Create initial INVITE: */

    status = pjsip_inv_invite(inv, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create initial INVITE request", 
		     status);
	goto on_error;
    }


    /* Send initial INVITE: */

    status = pjsip_inv_send_msg(inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send initial INVITE request", 
		     status);
	goto on_error;
    }


    /* Done. */

    ++pjsua.call_cnt;

    if (p_call_index)
	*p_call_index = call_index;

    return PJ_SUCCESS;


on_error:
    if (inv != NULL) {
	pjsip_inv_terminate(inv, PJSIP_SC_OK, PJ_FALSE);
    } else {
	pjsip_dlg_terminate(dlg);
    }

    if (call_index != -1) {
	pjsua.calls[call_index].inv = NULL;
    }
    return status;
}


/**
 * Handle incoming INVITE request.
 */
pj_bool_t pjsua_call_on_incoming(pjsip_rx_data *rdata)
{
    pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
    pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);
    pjsip_msg *msg = rdata->msg_info.msg;
    pjsip_tx_data *response = NULL;
    unsigned options = 0;
    pjsip_inv_session *inv = NULL;
    int acc_index;
    int call_index = -1;
    pjmedia_sdp_session *answer;
    pj_status_t status;

    /* Don't want to handle anything but INVITE */
    if (msg->line.req.method.id != PJSIP_INVITE_METHOD)
	return PJ_FALSE;

    /* Don't want to handle anything that's already associated with
     * existing dialog or transaction.
     */
    if (dlg || tsx)
	return PJ_FALSE;


    /* Verify that we can handle the request. */
    status = pjsip_inv_verify_request(rdata, &options, NULL, NULL,
				      pjsua.endpt, &response);
    if (status != PJ_SUCCESS) {

	/*
	 * No we can't handle the incoming INVITE request.
	 */

	if (response) {
	    pjsip_response_addr res_addr;

	    pjsip_get_response_addr(response->pool, rdata, &res_addr);
	    pjsip_endpt_send_response(pjsua.endpt, &res_addr, response, 
				      NULL, NULL);

	} else {

	    /* Respond with 500 (Internal Server Error) */
	    pjsip_endpt_respond_stateless(pjsua.endpt, rdata, 500, NULL,
					  NULL, NULL);
	}

	return PJ_TRUE;
    } 


    /*
     * Yes we can handle the incoming INVITE request.
     */

    /* Find free call slot. */
    for (call_index=0; call_index < pjsua.max_calls; ++call_index) {
	if (pjsua.calls[call_index].inv == NULL)
	    break;
    }

    if (call_index == PJSUA_MAX_CALLS) {
	pjsip_endpt_respond_stateless(pjsua.endpt, rdata, 
				      PJSIP_SC_BUSY_HERE, NULL,
				      NULL, NULL);
	return PJ_TRUE;
    }


    /* Get media capability from media endpoint: */

    status = pjmedia_endpt_create_sdp( pjsua.med_endpt, rdata->tp_info.pool, 1,
				       &pjsua.calls[call_index].skinfo, 
				       &answer );
    if (status != PJ_SUCCESS) {
	pjsip_endpt_respond_stateless(pjsua.endpt, rdata, 500, NULL,
				      NULL, NULL);

	return PJ_TRUE;
    }

    /* TODO: 
     *
     * Get which account is most likely to be associated with this incoming
     * call. We need the account to find which contact URI to put for
     * the call.
     */
    acc_index = 0;

    /* Create dialog: */

    status = pjsip_dlg_create_uas( pjsip_ua_instance(), rdata,
				   &pjsua.acc[acc_index].contact_uri, 
				   &dlg);
    if (status != PJ_SUCCESS) {
	pjsip_endpt_respond_stateless(pjsua.endpt, rdata, 500, NULL,
				      NULL, NULL);

	return PJ_TRUE;
    }


    /* Create invite session: */

    status = pjsip_inv_create_uas( dlg, rdata, answer, 0, &inv);
    if (status != PJ_SUCCESS) {

	pjsip_dlg_respond(dlg, rdata, 500, NULL, NULL, NULL);
	pjsip_dlg_terminate(dlg);
	return PJ_TRUE;
    }


    /* Create and attach pjsua data to the dialog: */

    pjsua.calls[call_index].inv = inv;

    dlg->mod_data[pjsua.mod.id] = &pjsua.calls[call_index];
    inv->mod_data[pjsua.mod.id] = &pjsua.calls[call_index];


    /* Must answer with some response to initial INVITE.
     * If auto-answer flag is set, send 200 straight away, otherwise send 100.
     */
    
    status = pjsip_inv_initial_answer(inv, rdata, 
				      (pjsua.auto_answer ? pjsua.auto_answer 
					: 100), 
				      NULL, NULL, &response);
    if (status != PJ_SUCCESS) {
	
	int st_code;

	pjsua_perror(THIS_FILE, "Unable to send answer to incoming INVITE", 
		     status);

	/* If failed to send 2xx response, there's a good chance that it is
	 * because SDP negotiation has failed.
	 */
	if (pjsua.auto_answer/100 == 2)
	    st_code = PJSIP_SC_UNSUPPORTED_MEDIA_TYPE;
	else
	    st_code = 500;

	pjsip_dlg_respond(dlg, rdata, st_code, NULL, NULL, NULL);
	pjsip_inv_terminate(inv, st_code, PJ_FALSE);
	return PJ_TRUE;

    } else {
	status = pjsip_inv_send_msg(inv, response);
	if (status != PJ_SUCCESS)
	    pjsua_perror(THIS_FILE, "Unable to send 100 response", status);
    }

    if (pjsua.auto_answer < 200) {
	PJ_LOG(3,(THIS_FILE,
		  "\nIncoming call!!\n"
		  "From: %.*s\n"
		  "To:   %.*s\n"
		  "(press 'a' to answer, 'h' to decline)",
		  (int)dlg->remote.info_str.slen,
		  dlg->remote.info_str.ptr,
		  (int)dlg->local.info_str.slen,
		  dlg->local.info_str.ptr));
    } else {
	PJ_LOG(3,(THIS_FILE,
		  "Call From:%.*s To:%.*s was answered with %d (%s)",
		  (int)dlg->remote.info_str.slen,
		  dlg->remote.info_str.ptr,
		  (int)dlg->local.info_str.slen,
		  dlg->local.info_str.ptr,
		  pjsua.auto_answer,
		  pjsip_get_status_text(pjsua.auto_answer)->ptr ));
    }

    ++pjsua.call_cnt;

    /* Schedule timer to refresh. */
    if (pjsua.uas_refresh > 0) {
	schedule_call_timer( &pjsua.calls[call_index], 
			     &pjsua.calls[call_index].refresh_tm,
			     REFRESH_CALL_TIMER,
			     pjsua.uas_refresh);
    }

    /* Schedule timer to hangup call. */
    if (pjsua.uas_duration > 0) {
	schedule_call_timer( &pjsua.calls[call_index], 
			     &pjsua.calls[call_index].hangup_tm,
			     HANGUP_CALL_TIMER,
			     pjsua.uas_duration);
    }

    /* This INVITE request has been handled. */
    return PJ_TRUE;
}


/*
 * This callback receives notification from invite session when the
 * session state has changed.
 */
static void pjsua_call_on_state_changed(pjsip_inv_session *inv, 
					pjsip_event *e)
{
    pjsua_call *call = inv->dlg->mod_data[pjsua.mod.id];

    /* If this is an outgoing INVITE that was created because of
     * REFER/transfer, send NOTIFY to transferer.
     */
    if (call && call->xfer_sub && e->type==PJSIP_EVENT_TSX_STATE)  {
	int st_code = -1;
	pjsip_evsub_state ev_state = PJSIP_EVSUB_STATE_ACTIVE;
	

	switch (call->inv->state) {
	case PJSIP_INV_STATE_NULL:
	case PJSIP_INV_STATE_CALLING:
	    /* Do nothing */
	    break;

	case PJSIP_INV_STATE_EARLY:
	case PJSIP_INV_STATE_CONNECTING:
	    st_code = e->body.tsx_state.tsx->status_code;
	    ev_state = PJSIP_EVSUB_STATE_ACTIVE;
	    break;

	case PJSIP_INV_STATE_CONFIRMED:
	    /* When state is confirmed, send the final 200/OK and terminate
	     * subscription.
	     */
	    st_code = e->body.tsx_state.tsx->status_code;
	    ev_state = PJSIP_EVSUB_STATE_TERMINATED;
	    break;

	case PJSIP_INV_STATE_DISCONNECTED:
	    st_code = e->body.tsx_state.tsx->status_code;
	    ev_state = PJSIP_EVSUB_STATE_TERMINATED;
	    break;
	}

	if (st_code != -1) {
	    pjsip_tx_data *tdata;
	    pj_status_t status;

	    status = pjsip_xfer_notify( call->xfer_sub,
					ev_state, st_code,
					NULL, &tdata);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Unable to create NOTIFY", status);
	    } else {
		status = pjsip_xfer_send_request(call->xfer_sub, tdata);
		if (status != PJ_SUCCESS) {
		    pjsua_perror(THIS_FILE, "Unable to send NOTIFY", status);
		}
	    }
	}
    }


    pjsua_ui_on_call_state(call->index, e);

    /* call->inv may be NULL now */

    /* Destroy media session when invite session is disconnected. */
    if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {

	pj_assert(call != NULL);

	if (call)
	    call_destroy_media(call->index);

	/* Remove timers. */
	schedule_call_timer(call, &call->refresh_tm, REFRESH_CALL_TIMER, 0);
	schedule_call_timer(call, &call->hangup_tm, HANGUP_CALL_TIMER, 0);

	/* Free call */
	call->inv = NULL;
	--pjsua.call_cnt;
    }
}


/*
 * Callback called by event framework when the xfer subscription state
 * has changed.
 */
static void xfer_on_evsub_state( pjsip_evsub *sub, pjsip_event *event)
{
    
    PJ_UNUSED_ARG(event);

    /*
     * We're only interested when subscription is terminated, to 
     * clear the xfer_sub member of the inv_data.
     */
    if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
	pjsua_call *call;

	call = pjsip_evsub_get_mod_data(sub, pjsua.mod.id);
	if (!call)
	    return;

	pjsip_evsub_set_mod_data(sub, pjsua.mod.id, NULL);
	call->xfer_sub = NULL;

	PJ_LOG(3,(THIS_FILE, "Xfer subscription terminated"));
    }
}


/*
 * Follow transfer (REFER) request.
 */
static void on_call_transfered( pjsip_inv_session *inv,
			        pjsip_rx_data *rdata )
{
    pj_status_t status;
    pjsip_tx_data *tdata;
    pjsua_call *existing_call;
    int new_call;
    const pj_str_t str_refer_to = { "Refer-To", 8};
    pjsip_generic_string_hdr *refer_to;
    char *uri;
    struct pjsip_evsub_user xfer_cb;
    pjsip_evsub *sub;

    existing_call = inv->dlg->mod_data[pjsua.mod.id];

    /* Find the Refer-To header */
    refer_to = (pjsip_generic_string_hdr*)
	pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_refer_to, NULL);

    if (refer_to == NULL) {
	/* Invalid Request.
	 * No Refer-To header!
	 */
	PJ_LOG(4,(THIS_FILE, "Received REFER without Refer-To header!"));
	pjsip_dlg_respond( inv->dlg, rdata, 400, NULL, NULL, NULL);
	return;
    }

    PJ_LOG(3,(THIS_FILE, "Call to %.*s is being transfered to %.*s",
	      (int)inv->dlg->remote.info_str.slen,
	      inv->dlg->remote.info_str.ptr,
	      (int)refer_to->hvalue.slen, 
	      refer_to->hvalue.ptr));

    /* Init callback */
    pj_memset(&xfer_cb, 0, sizeof(xfer_cb));
    xfer_cb.on_evsub_state = &xfer_on_evsub_state;

    /* Create transferee event subscription */
    status = pjsip_xfer_create_uas( inv->dlg, &xfer_cb, rdata, &sub);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create xfer uas", status);
	pjsip_dlg_respond( inv->dlg, rdata, 500, NULL, NULL, NULL);
	return;
    }

    /* Accept the REFER request, send 200 (OK). */
    pjsip_xfer_accept(sub, rdata, 200, NULL);

    /* Create initial NOTIFY request */
    status = pjsip_xfer_notify( sub, PJSIP_EVSUB_STATE_ACTIVE,
				100, NULL, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create NOTIFY to REFER", status);
	return;
    }

    /* Send initial NOTIFY request */
    status = pjsip_xfer_send_request( sub, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send NOTIFY to REFER", status);
	return;
    }

    /* We're cheating here.
     * We need to get a null terminated string from a pj_str_t.
     * So grab the pointer from the hvalue and NULL terminate it, knowing
     * that the NULL position will be occupied by a newline. 
     */
    uri = refer_to->hvalue.ptr;
    uri[refer_to->hvalue.slen] = '\0';

    /* Now make the outgoing call. */
    status = pjsua_make_call(existing_call->acc_index, uri, &new_call);
    if (status != PJ_SUCCESS) {

	/* Notify xferer about the error */
	status = pjsip_xfer_notify(sub, PJSIP_EVSUB_STATE_TERMINATED,
				   500, NULL, &tdata);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create NOTIFY to REFER", 
			  status);
	    return;
	}
	status = pjsip_xfer_send_request(sub, tdata);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to send NOTIFY to REFER", 
			  status);
	    return;
	}
	return;
    }

    /* Put the server subscription in inv_data.
     * Subsequent state changed in pjsua_inv_on_state_changed() will be
     * reported back to the server subscription.
     */
    pjsua.calls[new_call].xfer_sub = sub;

    /* Put the invite_data in the subscription. */
    pjsip_evsub_set_mod_data(sub, pjsua.mod.id, &pjsua.calls[new_call]);
}


/*
 * This callback is called when transaction state has changed in INVITE
 * session. We use this to trap:
 *  - incoming REFER request.
 *  - incoming MESSAGE request.
 */
static void pjsua_call_on_tsx_state_changed(pjsip_inv_session *inv,
					    pjsip_transaction *tsx,
					    pjsip_event *e)
{
    pjsua_call *call = inv->dlg->mod_data[pjsua.mod.id];

    if (tsx->role==PJSIP_ROLE_UAS &&
	tsx->state==PJSIP_TSX_STATE_TRYING &&
	pjsip_method_cmp(&tsx->method, &pjsip_refer_method)==0)
    {
	/*
	 * Incoming REFER request.
	 */
	on_call_transfered(call->inv, e->body.tsx_state.src.rdata);

    }
    else if (tsx->role==PJSIP_ROLE_UAS &&
	     tsx->state==PJSIP_TSX_STATE_TRYING &&
	     pjsip_method_cmp(&tsx->method, &pjsip_message_method)==0)
    {
	/*
	 * Incoming MESSAGE request!
	 */
	pjsip_rx_data *rdata;
	pjsip_msg *msg;
	pjsip_accept_hdr *accept_hdr;
	pj_status_t status;

	rdata = e->body.tsx_state.src.rdata;
	msg = rdata->msg_info.msg;

	/* Request MUST have message body, with Content-Type equal to
	 * "text/plain".
	 */
	if (pjsua_im_accept_pager(rdata, &accept_hdr) == PJ_FALSE) {

	    pjsip_hdr hdr_list;

	    pj_list_init(&hdr_list);
	    pj_list_push_back(&hdr_list, accept_hdr);

	    pjsip_dlg_respond( inv->dlg, rdata, PJSIP_SC_NOT_ACCEPTABLE_HERE, 
			       NULL, &hdr_list, NULL );
	    return;
	}

	/* Respond with 200 first, so that remote doesn't retransmit in case
	 * the UI takes too long to process the message. 
	 */
	status = pjsip_dlg_respond( inv->dlg, rdata, 200, NULL, NULL, NULL);

	/* Process MESSAGE request */
	pjsua_im_process_pager(call->index, &inv->dlg->remote.info_str,
			       &inv->dlg->local.info_str, rdata);
    }

}


/*
 * This callback is called by invite session framework when UAC session
 * has forked.
 */
static void pjsua_call_on_forked( pjsip_inv_session *inv, 
				  pjsip_event *e)
{
    PJ_UNUSED_ARG(inv);
    PJ_UNUSED_ARG(e);

    PJ_TODO(HANDLE_FORKED_DIALOG);
}


/*
 * Create inactive SDP for call hold.
 */
static pj_status_t create_inactive_sdp(pjsua_call *call,
				       pjmedia_sdp_session **p_answer)
{
    pj_status_t status;
    pjmedia_sdp_conn *conn;
    pjmedia_sdp_attr *attr;
    pjmedia_sdp_session *sdp;

    /* Create new offer */
    status = pjmedia_endpt_create_sdp(pjsua.med_endpt, pjsua.pool, 1,
				      &call->skinfo, &sdp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create local SDP", status);
	return status;
    }

    /* Get SDP media connection line */
    conn = sdp->media[0]->conn;
    if (!conn)
	conn = sdp->conn;

    /* Modify address */
    conn->addr = pj_str("0.0.0.0");

    /* Remove existing directions attributes */
    pjmedia_sdp_media_remove_all_attr(sdp->media[0], "sendrecv");
    pjmedia_sdp_media_remove_all_attr(sdp->media[0], "sendonly");
    pjmedia_sdp_media_remove_all_attr(sdp->media[0], "recvonly");
    pjmedia_sdp_media_remove_all_attr(sdp->media[0], "inactive");

    /* Add inactive attribute */
    attr = pjmedia_sdp_attr_create(pjsua.pool, "inactive", NULL);
    pjmedia_sdp_media_add_attr(sdp->media[0], attr);

    *p_answer = sdp;

    return status;
}

/*
 * Called when session received new offer.
 */
static void pjsua_call_on_rx_offer(pjsip_inv_session *inv,
				   const pjmedia_sdp_session *offer)
{
    pjsua_call *call;
    pjmedia_sdp_conn *conn;
    pjmedia_sdp_session *answer;
    pj_bool_t is_remote_active;
    pj_status_t status;

    call = inv->dlg->mod_data[pjsua.mod.id];

    /*
     * See if remote is offering active media (i.e. not on-hold)
     */
    is_remote_active = PJ_TRUE;

    conn = offer->media[0]->conn;
    if (!conn)
	conn = offer->conn;

    if (pj_strcmp2(&conn->addr, "0.0.0.0")==0 ||
	pj_strcmp2(&conn->addr, "0")==0)
    {
	is_remote_active = PJ_FALSE;

    } 
    else if (pjmedia_sdp_media_find_attr2(offer->media[0], "inactive", NULL))
    {
	is_remote_active = PJ_FALSE;
    }

    PJ_LOG(4,(THIS_FILE, "Received SDP offer, remote media is %s",
	      (is_remote_active ? "active" : "inactive")));

    /* Supply candidate answer */
    if (is_remote_active) {
	status = pjmedia_endpt_create_sdp( pjsua.med_endpt, call->inv->pool, 1,
					   &call->skinfo, &answer);
    } else {
	status = create_inactive_sdp( call, &answer );
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create local SDP", status);
	return;
    }

    status = pjsip_inv_set_sdp_answer(call->inv, answer);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to set answer", status);
	return;
    }

}

/* Disconnect call */
static void call_disconnect(pjsip_inv_session *inv,
			    int st_code)
{
    pjsip_tx_data *tdata;
    pj_status_t status;

    status = pjsip_inv_end_session(inv, st_code, NULL, &tdata);
    if (status == PJ_SUCCESS)
	status = pjsip_inv_send_msg(inv, tdata);

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to disconnect call", status);
    }
}

/*
 * Callback to be called when SDP offer/answer negotiation has just completed
 * in the session. This function will start/update media if negotiation
 * has succeeded.
 */
static void pjsua_call_on_media_update(pjsip_inv_session *inv,
				       pj_status_t status)
{
    pjsua_call *call;
    const pjmedia_sdp_session *local_sdp;
    const pjmedia_sdp_session *remote_sdp;
    pjmedia_port *media_port;
    pj_str_t port_name;
    char tmp[PJSIP_MAX_URL_SIZE];

    call = inv->dlg->mod_data[pjsua.mod.id];

    if (status != PJ_SUCCESS) {

	pjsua_perror(THIS_FILE, "SDP negotiation has failed", status);

	/* Disconnect call if we're not in the middle of initializing an
	 * UAS dialog and if this is not a re-INVITE 
	 */
	if (inv->state != PJSIP_INV_STATE_NULL &&
	    inv->state != PJSIP_INV_STATE_CONFIRMED) 
	{
	    //call_disconnect(inv, PJSIP_SC_UNSUPPORTED_MEDIA_TYPE);
	}
	return;

    }

    /* Destroy existing media session, if any. */

    if (call)
	call_destroy_media(call->index);

    /* Get local and remote SDP */

    status = pjmedia_sdp_neg_get_active_local(call->inv->neg, &local_sdp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Unable to retrieve currently active local SDP", 
		     status);
	//call_disconnect(inv, PJSIP_SC_UNSUPPORTED_MEDIA_TYPE);
	return;
    }


    status = pjmedia_sdp_neg_get_active_remote(call->inv->neg, &remote_sdp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Unable to retrieve currently active remote SDP", 
		     status);
	//call_disconnect(inv, PJSIP_SC_UNSUPPORTED_MEDIA_TYPE);
	return;
    }

    /* Create new media session. 
     * The media session is active immediately.
     */
    if (pjsua.null_audio)
	return;
    
    status = pjmedia_session_create( pjsua.med_endpt, 1, 
				     &call->skinfo,
				     local_sdp, remote_sdp, 
				     call,
				     &call->session );
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create media session", 
		     status);
	//call_disconnect(inv, PJSIP_SC_UNSUPPORTED_MEDIA_TYPE);
	return;
    }


    /* Get the port interface of the first stream in the session.
     * We need the port interface to add to the conference bridge.
     */
    pjmedia_session_get_port(call->session, 0, &media_port);


    /*
     * Add the call to conference bridge.
     */
    port_name.ptr = tmp;
    port_name.slen = pjsip_uri_print(PJSIP_URI_IN_REQ_URI,
				     call->inv->dlg->remote.info->uri,
				     tmp, sizeof(tmp));
    if (port_name.slen < 1) {
	port_name = pj_str("call");
    }
    status = pjmedia_conf_add_port( pjsua.mconf, call->inv->pool,
				    media_port, 
				    &port_name,
				    &call->conf_slot);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create conference slot", 
		     status);
	call_destroy_media(call->index);
	//call_disconnect(inv, PJSIP_SC_INTERNAL_SERVER_ERROR);
	return;
    }

    /* If auto-play is configured, connect the call to the file player 
     * port 
     */
    if (pjsua.auto_play && pjsua.wav_file && 
	call->inv->role == PJSIP_ROLE_UAS) 
    {

	pjmedia_conf_connect_port( pjsua.mconf, pjsua.wav_slot, 
				   call->conf_slot, 0);

    } else if (pjsua.auto_loop && call->inv->role == PJSIP_ROLE_UAS) {

	pjmedia_conf_connect_port( pjsua.mconf, call->conf_slot, 
				   call->conf_slot, 0);

    } else if (pjsua.auto_conf) {

	int i;

	pjmedia_conf_connect_port( pjsua.mconf, 0, call->conf_slot, 0);
	pjmedia_conf_connect_port( pjsua.mconf, call->conf_slot, 0, 0);

	for (i=0; i < pjsua.max_calls; ++i) {

	    if (!pjsua.calls[i].session)
		continue;

	    pjmedia_conf_connect_port( pjsua.mconf, call->conf_slot, 
				       pjsua.calls[i].conf_slot, 0);
	    pjmedia_conf_connect_port( pjsua.mconf, pjsua.calls[i].conf_slot,
				       call->conf_slot, 0);
	}

    } else {

	/* Connect new call to the sound device port (port zero) in the
	 * main conference bridge.
	 */
	pjmedia_conf_connect_port( pjsua.mconf, 0, call->conf_slot, 0);
	pjmedia_conf_connect_port( pjsua.mconf, call->conf_slot, 0, 0);
    }


    /* Done. */
    {
	struct pjmedia_session_info sess_info;
	char info[80];
	int info_len = 0;
	unsigned i;

	pjmedia_session_get_info(call->session, &sess_info);
	for (i=0; i<sess_info.stream_cnt; ++i) {
	    int len;
	    const char *dir;
	    pjmedia_stream_info *strm_info = &sess_info.stream_info[i];

	    switch (strm_info->dir) {
	    case PJMEDIA_DIR_NONE:
		dir = "inactive";
		break;
	    case PJMEDIA_DIR_ENCODING:
		dir = "sendonly";
		break;
	    case PJMEDIA_DIR_DECODING:
		dir = "recvonly";
		break;
	    case PJMEDIA_DIR_ENCODING_DECODING:
		dir = "sendrecv";
		break;
	    default:
		dir = "unknown";
		break;
	    }
	    len = pj_ansi_sprintf( info+info_len,
				   ", stream #%d: %.*s (%s)", i,
				   (int)strm_info->fmt.encoding_name.slen,
				   strm_info->fmt.encoding_name.ptr,
				   dir);
	    if (len > 0)
		info_len += len;
	}
	PJ_LOG(3,(THIS_FILE,"Media started%s", info));
    }
}


/*
 * Hangup call.
 */
void pjsua_call_hangup(int call_index)
{
    pjsua_call *call;
    int code;
    pj_status_t status;
    pjsip_tx_data *tdata;


    call = &pjsua.calls[call_index];

    if (!call->inv) {
	PJ_LOG(3,(THIS_FILE,"Call has been disconnected"));
	return;
    }

    if (call->inv->state == PJSIP_INV_STATE_CONFIRMED)
	code = PJSIP_SC_OK;
    else if (call->inv->role == PJSIP_ROLE_UAS)
	code = PJSIP_SC_DECLINE;
    else
	code = PJSIP_SC_REQUEST_TERMINATED;

    status = pjsip_inv_end_session(call->inv, code, NULL, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Failed to create end session message", 
		     status);
	return;
    }

    /* pjsip_inv_end_session may return PJ_SUCCESS with NULL 
     * as p_tdata when INVITE transaction has not been answered
     * with any provisional responses.
     */
    if (tdata == NULL)
	return;

    status = pjsip_inv_send_msg(call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Failed to send end session message", 
		     status);
	return;
    }
}


/*
 * Put call on-Hold.
 */
void pjsua_call_set_hold(int call_index)
{
    pjmedia_sdp_session *sdp;
    pjsua_call *call;
    pjsip_tx_data *tdata;
    pj_status_t status;

    call = &pjsua.calls[call_index];
    
    if (!call->inv) {
	PJ_LOG(3,(THIS_FILE,"Call has been disconnected"));
	return;
    }

    if (call->inv->state != PJSIP_INV_STATE_CONFIRMED) {
	PJ_LOG(3,(THIS_FILE, "Can not hold call that is not confirmed"));
	return;
    }

    status = create_inactive_sdp(call, &sdp);
    if (status != PJ_SUCCESS)
	return;

    /* Send re-INVITE with new offer */
    status = pjsip_inv_reinvite( call->inv, NULL, sdp, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create re-INVITE", status);
	return;
    }

    status = pjsip_inv_send_msg( call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send re-INVITE", status);
	return;
    }
}


/*
 * re-INVITE.
 */
void pjsua_call_reinvite(int call_index)
{
    pjmedia_sdp_session *sdp;
    pjsip_tx_data *tdata;
    pjsua_call *call;
    pj_status_t status;

    call = &pjsua.calls[call_index];

    if (!call->inv) {
	PJ_LOG(3,(THIS_FILE,"Call has been disconnected"));
	return;
    }


    if (call->inv->state != PJSIP_INV_STATE_CONFIRMED) {
	PJ_LOG(3,(THIS_FILE, "Can not re-INVITE call that is not confirmed"));
	return;
    }

    /* Create SDP */
    status = pjmedia_endpt_create_sdp( pjsua.med_endpt, call->inv->pool, 1,
				       &call->skinfo, &sdp);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to get SDP from media endpoint", 
		     status);
	return;
    }

    /* Send re-INVITE with new offer */
    status = pjsip_inv_reinvite( call->inv, NULL, sdp, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create re-INVITE", status);
	return;
    }

    status = pjsip_inv_send_msg( call->inv, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send re-INVITE", status);
	return;
    }
}


/*
 * Transfer call.
 */
void pjsua_call_xfer(int call_index, const char *dest)
{
    pjsip_evsub *sub;
    pjsip_tx_data *tdata;
    pjsua_call *call;
    pj_str_t tmp;
    pj_status_t status;

    
    call = &pjsua.calls[call_index];

    if (!call->inv) {
	PJ_LOG(3,(THIS_FILE,"Call has been disconnected"));
	return;
    }
   
    /* Create xfer client subscription.
     * We're not interested in knowing the transfer result, so we
     * put NULL as the callback.
     */
    status = pjsip_xfer_create_uac(call->inv->dlg, NULL, &sub);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create xfer", status);
	return;
    }

    /*
     * Create REFER request.
     */
    status = pjsip_xfer_initiate(sub, pj_cstr(&tmp, dest), &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create REFER request", status);
	return;
    }

    /* Send. */
    status = pjsip_xfer_send_request(sub, tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send REFER request", status);
	return;
    }

    /* For simplicity (that's what this program is intended to be!), 
     * leave the original invite session as it is. More advanced application
     * may want to hold the INVITE, or terminate the invite, or whatever.
     */
}


/**
 * Send instant messaging inside INVITE session.
 */
void pjsua_call_send_im(int call_index, const char *str)
{
    pjsua_call *call;
    const pj_str_t mime_text = pj_str("text");
    const pj_str_t mime_plain = pj_str("plain");
    pj_str_t text;
    pjsip_tx_data *tdata;
    pj_status_t status;

    call = &pjsua.calls[call_index];

    if (!call->inv) {
	PJ_LOG(3,(THIS_FILE,"Call has been disconnected"));
	return;
    }

    /* Lock dialog. */
    pjsip_dlg_inc_lock(call->inv->dlg);
    
    /* Create request message. */
    status = pjsip_dlg_create_request( call->inv->dlg, &pjsip_message_method,
				       -1, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create MESSAGE request", status);
	goto on_return;
    }

    /* Add accept header. */
    pjsip_msg_add_hdr( tdata->msg, 
		       (pjsip_hdr*)pjsua_im_create_accept(tdata->pool));

    /* Create "text/plain" message body. */
    tdata->msg->body = pjsip_msg_body_create( tdata->pool, &mime_text,
					      &mime_plain, 
					      pj_cstr(&text, str));
    if (tdata->msg->body == NULL) {
	pjsua_perror(THIS_FILE, "Unable to create msg body", PJ_ENOMEM);
	pjsip_tx_data_dec_ref(tdata);
	goto on_return;
    }

    /* Send the request. */
    status = pjsip_dlg_send_request( call->inv->dlg, tdata, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send MESSAGE request", status);
	goto on_return;
    }

on_return:
    pjsip_dlg_dec_lock(call->inv->dlg);
}


/**
 * Send IM typing indication inside INVITE session.
 */
void pjsua_call_typing(int call_index, pj_bool_t is_typing)
{
    pjsua_call *call;
    pjsip_tx_data *tdata;
    pj_status_t status;

    call = &pjsua.calls[call_index];

    if (!call->inv) {
	PJ_LOG(3,(THIS_FILE,"Call has been disconnected"));
	return;
    }

    /* Lock dialog. */
    pjsip_dlg_inc_lock(call->inv->dlg);
    
    /* Create request message. */
    status = pjsip_dlg_create_request( call->inv->dlg, &pjsip_message_method,
				       -1, &tdata);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create MESSAGE request", status);
	goto on_return;
    }

    /* Create "application/im-iscomposing+xml" msg body. */
    tdata->msg->body = pjsip_iscomposing_create_body(tdata->pool, is_typing,
						     NULL, NULL, -1);

    /* Send the request. */
    status = pjsip_dlg_send_request( call->inv->dlg, tdata, NULL);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to send MESSAGE request", status);
	goto on_return;
    }

on_return:
    pjsip_dlg_dec_lock(call->inv->dlg);}


/*
 * Terminate all calls.
 */
void pjsua_call_hangup_all(void)
{
    int i;

    for (i=0; i<pjsua.max_calls; ++i) {
	pjsip_tx_data *tdata;
	int st_code;
	pjsua_call *call;

	if (pjsua.calls[i].inv == NULL)
	    continue;

	call = &pjsua.calls[i];

	if (call->inv->state == PJSIP_INV_STATE_CONFIRMED) {
	    st_code = 200;
	} else {
	    st_code = PJSIP_SC_GONE;
	}

	if (pjsip_inv_end_session(call->inv, st_code, NULL, &tdata)==0) {
	    if (tdata)
		pjsip_inv_send_msg(call->inv, tdata);
	}
    }
}


pj_status_t pjsua_call_init(void)
{
    /* Initialize invite session callback. */
    pjsip_inv_callback inv_cb;
    pj_status_t status;

    pj_memset(&inv_cb, 0, sizeof(inv_cb));
    inv_cb.on_state_changed = &pjsua_call_on_state_changed;
    inv_cb.on_new_session = &pjsua_call_on_forked;
    inv_cb.on_media_update = &pjsua_call_on_media_update;
    inv_cb.on_rx_offer = &pjsua_call_on_rx_offer;
    inv_cb.on_tsx_state_changed = &pjsua_call_on_tsx_state_changed;


    /* Initialize invite session module: */
    status = pjsip_inv_usage_init(pjsua.endpt, &inv_cb);
    
    return status;
}
