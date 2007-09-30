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
#include <pjsip-ua/sip_inv.h>
#include <pjsip-ua/sip_100rel.h>
#include <pjsip/sip_module.h>
#include <pjsip/sip_endpoint.h>
#include <pjsip/sip_event.h>
#include <pjsip/sip_transaction.h>
#include <pjmedia/sdp.h>
#include <pjmedia/sdp_neg.h>
#include <pjmedia/errno.h>
#include <pj/string.h>
#include <pj/pool.h>
#include <pj/assert.h>
#include <pj/os.h>
#include <pj/log.h>


#define THIS_FILE	"sip_inv.c"

static const char *inv_state_names[] =
{
    "NULL",
    "CALLING",
    "INCOMING",
    "EARLY",
    "CONNECTING",
    "CONFIRMED",
    "DISCONNCTD",
    "TERMINATED",
};

/*
 * Static prototypes.
 */
static pj_status_t mod_inv_load(pjsip_endpoint *endpt);
static pj_status_t mod_inv_unload(void);
static pj_bool_t   mod_inv_on_rx_request(pjsip_rx_data *rdata);
static pj_bool_t   mod_inv_on_rx_response(pjsip_rx_data *rdata);
static void	   mod_inv_on_tsx_state(pjsip_transaction*, pjsip_event*);

static void inv_on_state_null( pjsip_inv_session *inv, pjsip_event *e);
static void inv_on_state_calling( pjsip_inv_session *inv, pjsip_event *e);
static void inv_on_state_incoming( pjsip_inv_session *inv, pjsip_event *e);
static void inv_on_state_early( pjsip_inv_session *inv, pjsip_event *e);
static void inv_on_state_connecting( pjsip_inv_session *inv, pjsip_event *e);
static void inv_on_state_confirmed( pjsip_inv_session *inv, pjsip_event *e);
static void inv_on_state_disconnected( pjsip_inv_session *inv, pjsip_event *e);

static pj_status_t inv_check_sdp_in_incoming_msg( pjsip_inv_session *inv,
						  pjsip_transaction *tsx,
						  pjsip_rx_data *rdata);
static pj_status_t process_answer( pjsip_inv_session *inv,
				   int st_code,
				   pjsip_tx_data *tdata,
				   const pjmedia_sdp_session *local_sdp);

static void (*inv_state_handler[])( pjsip_inv_session *inv, pjsip_event *e) = 
{
    &inv_on_state_null,
    &inv_on_state_calling,
    &inv_on_state_incoming,
    &inv_on_state_early,
    &inv_on_state_connecting,
    &inv_on_state_confirmed,
    &inv_on_state_disconnected,
};

static struct mod_inv
{
    pjsip_module	 mod;
    pjsip_endpoint	*endpt;
    pjsip_inv_callback	 cb;
} mod_inv = 
{
    {
	NULL, NULL,			    /* prev, next.		*/
	{ "mod-invite", 10 },		    /* Name.			*/
	-1,				    /* Id			*/
	PJSIP_MOD_PRIORITY_DIALOG_USAGE,    /* Priority			*/
	&mod_inv_load,			    /* load()			*/
	NULL,				    /* start()			*/
	NULL,				    /* stop()			*/
	&mod_inv_unload,		    /* unload()			*/
	&mod_inv_on_rx_request,		    /* on_rx_request()		*/
	&mod_inv_on_rx_response,	    /* on_rx_response()		*/
	NULL,				    /* on_tx_request.		*/
	NULL,				    /* on_tx_response()		*/
	&mod_inv_on_tsx_state,		    /* on_tsx_state()		*/
    }
};


/* Invite session data to be attached to transaction. */
struct tsx_inv_data
{
    pjsip_inv_session	*inv;
    pj_bool_t		 sdp_done;
};

/*
 * Module load()
 */
static pj_status_t mod_inv_load(pjsip_endpoint *endpt)
{
    pj_str_t allowed[] = {{"INVITE", 6}, {"ACK",3}, {"BYE",3}, {"CANCEL",6}};
    pj_str_t accepted = { "application/sdp", 15 };

    /* Register supported methods: INVITE, ACK, BYE, CANCEL */
    pjsip_endpt_add_capability(endpt, &mod_inv.mod, PJSIP_H_ALLOW, NULL,
			       PJ_ARRAY_SIZE(allowed), allowed);

    /* Register "application/sdp" in Accept header */
    pjsip_endpt_add_capability(endpt, &mod_inv.mod, PJSIP_H_ACCEPT, NULL,
			       1, &accepted);

    return PJ_SUCCESS;
}

/*
 * Module unload()
 */
static pj_status_t mod_inv_unload(void)
{
    /* Should remove capability here */
    return PJ_SUCCESS;
}

/*
 * Set session state.
 */
void inv_set_state(pjsip_inv_session *inv, pjsip_inv_state state,
		   pjsip_event *e)
{
    pjsip_inv_state prev_state = inv->state;
    pj_status_t status;


    /* If state is confirmed, check that SDP negotiation is done,
     * otherwise disconnect the session.
     */
    if (state == PJSIP_INV_STATE_CONFIRMED) {
	if (pjmedia_sdp_neg_get_state(inv->neg)!=PJMEDIA_SDP_NEG_STATE_DONE) {
	    pjsip_tx_data *bye;

	    PJ_LOG(4,(inv->obj_name, "SDP offer/answer incomplete, ending the "
		      "session"));

	    status = pjsip_inv_end_session(inv, PJSIP_SC_NOT_ACCEPTABLE, 
					   NULL, &bye);
	    if (status == PJ_SUCCESS && bye)
		status = pjsip_inv_send_msg(inv, bye);

	    return;
	}
    }

    /* Set state. */
    inv->state = state;

    /* If state is DISCONNECTED, cause code MUST have been set. */
    pj_assert(inv->state != PJSIP_INV_STATE_DISCONNECTED ||
	      inv->cause != 0);

    /* Call on_state_changed() callback. */
    if (mod_inv.cb.on_state_changed && inv->notify)
	(*mod_inv.cb.on_state_changed)(inv, e);

    /* Only decrement when previous state is not already DISCONNECTED */
    if (inv->state == PJSIP_INV_STATE_DISCONNECTED &&
	prev_state != PJSIP_INV_STATE_DISCONNECTED) 
    {
	pjsip_dlg_dec_session(inv->dlg, &mod_inv.mod);
    }
}


/*
 * Set cause code.
 */
void inv_set_cause(pjsip_inv_session *inv, int cause_code,
		   const pj_str_t *cause_text)
{
    if (cause_code > inv->cause) {
	inv->cause = (pjsip_status_code) cause_code;
	if (cause_text)
	    pj_strdup(inv->pool, &inv->cause_text, cause_text);
	else if (cause_code/100 == 2)
	    inv->cause_text = pj_str("Normal call clearing");
	else
	    inv->cause_text = *pjsip_get_status_text(cause_code);
    }
}



/*
 * Send ACK for 2xx response.
 */
static pj_status_t inv_send_ack(pjsip_inv_session *inv, pjsip_rx_data *rdata)
{
    pjsip_tx_data *tdata;
    pj_status_t status;

    PJ_LOG(5,(inv->obj_name, "Received %s, sending ACK",
	      pjsip_rx_data_get_info(rdata)));

    status = pjsip_dlg_create_request(inv->dlg, pjsip_get_ack_method(), 
				      rdata->msg_info.cseq->cseq, &tdata);
    if (status != PJ_SUCCESS) {
	/* Better luck next time */
	pj_assert(!"Unable to create ACK!");
	return status;
    }

    status = pjsip_dlg_send_request(inv->dlg, tdata, -1, NULL);
    if (status != PJ_SUCCESS) {
	/* Better luck next time */
	pj_assert(!"Unable to send ACK!");
	return status;
    }

    return PJ_SUCCESS;
}

/*
 * Module on_rx_request()
 *
 * This callback is called for these events:
 *  - endpoint receives request which was unhandled by higher priority
 *    modules (e.g. transaction layer, dialog layer).
 *  - dialog distributes incoming request to its usages.
 */
static pj_bool_t mod_inv_on_rx_request(pjsip_rx_data *rdata)
{
    pjsip_method *method;
    pjsip_dialog *dlg;
    pjsip_inv_session *inv;

    /* Only wants to receive request from a dialog. */
    dlg = pjsip_rdata_get_dlg(rdata);
    if (dlg == NULL)
	return PJ_FALSE;

    inv = (pjsip_inv_session*) dlg->mod_data[mod_inv.mod.id];

    /* Report to dialog that we handle INVITE, CANCEL, BYE, ACK. 
     * If we need to send response, it will be sent in the state
     * handlers.
     */
    method = &rdata->msg_info.msg->line.req.method;

    if (method->id == PJSIP_INVITE_METHOD) {
	return PJ_TRUE;
    }

    /* BYE and CANCEL must have existing invite session */
    if (method->id == PJSIP_BYE_METHOD ||
	method->id == PJSIP_CANCEL_METHOD)
    {
	if (inv == NULL)
	    return PJ_FALSE;

	return PJ_TRUE;
    }

    /* On receipt ACK request, when state is CONNECTING,
     * move state to CONFIRMED.
     */
    if (method->id == PJSIP_ACK_METHOD && inv) {

	/* Ignore ACK if pending INVITE transaction has not finished. */
	if (inv->invite_tsx && 
	    inv->invite_tsx->state < PJSIP_TSX_STATE_COMPLETED)
	{
	    return PJ_TRUE;
	}

	/* Terminate INVITE transaction, if it's still present. */
	if (inv->invite_tsx && 
	    inv->invite_tsx->state <= PJSIP_TSX_STATE_COMPLETED)
	{
	    /* Before we terminate INVITE transaction, process the SDP
	     * in the ACK request, if any.
	     */
	    inv_check_sdp_in_incoming_msg(inv, inv->invite_tsx, rdata);

	    /* Now we can terminate the INVITE transaction */
	    pj_assert(inv->invite_tsx->status_code >= 200);
	    pjsip_tsx_terminate(inv->invite_tsx, 
				inv->invite_tsx->status_code);
	    inv->invite_tsx = NULL;
	    if (inv->last_answer) {
		    pjsip_tx_data_dec_ref(inv->last_answer);
		    inv->last_answer = NULL;
	    }
	}

	/* On receipt of ACK, only set state to confirmed when state
	 * is CONNECTING (e.g. we don't want to set the state to confirmed
	 * when we receive ACK retransmission after sending non-2xx!)
	 */
	if (inv->state == PJSIP_INV_STATE_CONNECTING) {
	    pjsip_event event;

	    PJSIP_EVENT_INIT_RX_MSG(event, rdata);
	    inv_set_state(inv, PJSIP_INV_STATE_CONFIRMED, &event);
	}
    }

    return PJ_FALSE;
}

/*
 * Module on_rx_response().
 *
 * This callback is called for these events:
 *  - dialog distributes incoming 2xx response to INVITE (outside
 *    transaction) to its usages.
 *  - endpoint distributes strayed responses.
 */
static pj_bool_t mod_inv_on_rx_response(pjsip_rx_data *rdata)
{
    pjsip_dialog *dlg;
    pjsip_inv_session *inv;
    pjsip_msg *msg = rdata->msg_info.msg;

    dlg = pjsip_rdata_get_dlg(rdata);

    /* Ignore responses outside dialog */
    if (dlg == NULL)
	return PJ_FALSE;

    /* Ignore responses not belonging to invite session */
    inv = pjsip_dlg_get_inv_session(dlg);
    if (inv == NULL)
	return PJ_FALSE;

    /* This MAY be retransmission of 2xx response to INVITE. 
     * If it is, we need to send ACK.
     */
    if (msg->type == PJSIP_RESPONSE_MSG && msg->line.status.code/100==2 &&
	rdata->msg_info.cseq->method.id == PJSIP_INVITE_METHOD &&
	inv->invite_tsx == NULL) 
    {

	inv_send_ack(inv, rdata);
	return PJ_TRUE;

    }

    /* No other processing needs to be done here. */
    return PJ_FALSE;
}

/*
 * Module on_tsx_state()
 *
 * This callback is called by dialog framework for all transactions
 * inside the dialog for all its dialog usages.
 */
static void mod_inv_on_tsx_state(pjsip_transaction *tsx, pjsip_event *e)
{
    pjsip_dialog *dlg;
    pjsip_inv_session *inv;

    dlg = pjsip_tsx_get_dlg(tsx);
    if (dlg == NULL)
	return;

    inv = pjsip_dlg_get_inv_session(dlg);
    if (inv == NULL)
	return;

    /* Call state handler for the invite session. */
    (*inv_state_handler[inv->state])(inv, e);

    /* Call on_tsx_state */
    if (mod_inv.cb.on_tsx_state_changed && inv->notify)
	(*mod_inv.cb.on_tsx_state_changed)(inv, tsx, e);

    /* Clear invite transaction when tsx is confirmed.
     * Previously we set invite_tsx to NULL only when transaction has
     * terminated, but this didn't work when ACK has the same Via branch
     * value as the INVITE (see http://www.pjsip.org/trac/ticket/113)
     */
    if (tsx->state>=PJSIP_TSX_STATE_CONFIRMED && tsx == inv->invite_tsx) {
        inv->invite_tsx = NULL;
	if (inv->last_answer) {
		pjsip_tx_data_dec_ref(inv->last_answer);
		inv->last_answer = NULL;
	}
    }
}


/*
 * Initialize the invite module.
 */
PJ_DEF(pj_status_t) pjsip_inv_usage_init( pjsip_endpoint *endpt,
					  const pjsip_inv_callback *cb)
{
    pj_status_t status;

    /* Check arguments. */
    PJ_ASSERT_RETURN(endpt && cb, PJ_EINVAL);

    /* Some callbacks are mandatory */
    PJ_ASSERT_RETURN(cb->on_state_changed && cb->on_new_session, PJ_EINVAL);

    /* Check if module already registered. */
    PJ_ASSERT_RETURN(mod_inv.mod.id == -1, PJ_EINVALIDOP);

    /* Copy param. */
    pj_memcpy(&mod_inv.cb, cb, sizeof(pjsip_inv_callback));

    mod_inv.endpt = endpt;

    /* Register the module. */
    status = pjsip_endpt_register_module(endpt, &mod_inv.mod);
    if (status != PJ_SUCCESS)
	return status;

    return PJ_SUCCESS;
}

/*
 * Get the instance of invite module.
 */
PJ_DEF(pjsip_module*) pjsip_inv_usage_instance(void)
{
    return &mod_inv.mod;
}



/*
 * Return the invite session for the specified dialog.
 */
PJ_DEF(pjsip_inv_session*) pjsip_dlg_get_inv_session(pjsip_dialog *dlg)
{
    return (pjsip_inv_session*) dlg->mod_data[mod_inv.mod.id];
}


/*
 * Get INVITE state name.
 */
PJ_DEF(const char *) pjsip_inv_state_name(pjsip_inv_state state)
{
    PJ_ASSERT_RETURN(state >= PJSIP_INV_STATE_NULL && 
		     state <= PJSIP_INV_STATE_DISCONNECTED,
		     "??");

    return inv_state_names[state];
}

/*
 * Create UAC invite session.
 */
PJ_DEF(pj_status_t) pjsip_inv_create_uac( pjsip_dialog *dlg,
					  const pjmedia_sdp_session *local_sdp,
					  unsigned options,
					  pjsip_inv_session **p_inv)
{
    pjsip_inv_session *inv;
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(dlg && p_inv, PJ_EINVAL);

    /* Must lock dialog first */
    pjsip_dlg_inc_lock(dlg);

    /* Normalize options */
    if (options & PJSIP_INV_REQUIRE_100REL)
	options |= PJSIP_INV_SUPPORT_100REL;

#if !PJSIP_HAS_100REL
    /* options cannot specify 100rel if 100rel is disabled */
    PJ_ASSERT_RETURN(
	(options & (PJSIP_INV_REQUIRE_100REL | PJSIP_INV_SUPPORT_100REL))==0,
	PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_BAD_EXTENSION));
    
#endif

    if (options & PJSIP_INV_REQUIRE_TIMER)
	options |= PJSIP_INV_SUPPORT_TIMER;

    /* Create the session */
    inv = PJ_POOL_ZALLOC_T(dlg->pool, pjsip_inv_session);
    pj_assert(inv != NULL);

    inv->pool = dlg->pool;
    inv->role = PJSIP_ROLE_UAC;
    inv->state = PJSIP_INV_STATE_NULL;
    inv->dlg = dlg;
    inv->options = options;
    inv->notify = PJ_TRUE;
    inv->cause = (pjsip_status_code) 0;

    /* Object name will use the same dialog pointer. */
    pj_ansi_snprintf(inv->obj_name, PJ_MAX_OBJ_NAME, "inv%p", dlg);

    /* Create negotiator if local_sdp is specified. */
    if (local_sdp) {
	status = pjmedia_sdp_neg_create_w_local_offer(dlg->pool, local_sdp,
						      &inv->neg);
	if (status != PJ_SUCCESS) {
	    pjsip_dlg_dec_lock(dlg);
	    return status;
	}
    }

    /* Register invite as dialog usage. */
    status = pjsip_dlg_add_usage(dlg, &mod_inv.mod, inv);
    if (status != PJ_SUCCESS) {
	pjsip_dlg_dec_lock(dlg);
	return status;
    }

    /* Increment dialog session */
    pjsip_dlg_inc_session(dlg, &mod_inv.mod);

#if PJSIP_HAS_100REL
    /* Create 100rel handler */
    pjsip_100rel_attach(inv);
#endif

    /* Done */
    *p_inv = inv;

    pjsip_dlg_dec_lock(dlg);

    PJ_LOG(5,(inv->obj_name, "UAC invite session created for dialog %s",
	      dlg->obj_name));

    return PJ_SUCCESS;
}

/*
 * Verify incoming INVITE request.
 */
PJ_DEF(pj_status_t) pjsip_inv_verify_request(pjsip_rx_data *rdata,
					     unsigned *options,
					     const pjmedia_sdp_session *l_sdp,
					     pjsip_dialog *dlg,
					     pjsip_endpoint *endpt,
					     pjsip_tx_data **p_tdata)
{
    pjsip_msg *msg;
    pjsip_allow_hdr *allow;
    pjsip_supported_hdr *sup_hdr;
    pjsip_require_hdr *req_hdr;
    int code = 200;
    unsigned rem_option = 0;
    pj_status_t status = PJ_SUCCESS;
    pjsip_hdr res_hdr_list;

    /* Init return arguments. */
    if (p_tdata) *p_tdata = NULL;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(rdata != NULL && options != NULL, PJ_EINVAL);

    /* Normalize options */
    if (*options & PJSIP_INV_REQUIRE_100REL)
	*options |= PJSIP_INV_SUPPORT_100REL;

    if (*options & PJSIP_INV_REQUIRE_TIMER)
	*options |= PJSIP_INV_SUPPORT_TIMER;

    /* Get the message in rdata */
    msg = rdata->msg_info.msg;

    /* Must be INVITE request. */
    PJ_ASSERT_RETURN(msg->type == PJSIP_REQUEST_MSG &&
		     msg->line.req.method.id == PJSIP_INVITE_METHOD,
		     PJ_EINVAL);

    /* If tdata is specified, then either dlg or endpt must be specified */
    PJ_ASSERT_RETURN((!p_tdata) || (endpt || dlg), PJ_EINVAL);

    /* Get the endpoint */
    endpt = endpt ? endpt : dlg->endpt;

    /* Init response header list */
    pj_list_init(&res_hdr_list);

    /* Check the request body, see if it'inv something that we support
     * (i.e. SDP). 
     */
    if (msg->body) {
	pjsip_msg_body *body = msg->body;
	pj_str_t str_application = {"application", 11};
	pj_str_t str_sdp = { "sdp", 3 };
	pjmedia_sdp_session *sdp;

	/* Check content type. */
	if (pj_stricmp(&body->content_type.type, &str_application) != 0 ||
	    pj_stricmp(&body->content_type.subtype, &str_sdp) != 0)
	{
	    /* Not "application/sdp" */
	    code = PJSIP_SC_UNSUPPORTED_MEDIA_TYPE;
	    status = PJSIP_ERRNO_FROM_SIP_STATUS(code);

	    if (p_tdata) {
		/* Add Accept header to response */
		pjsip_accept_hdr *acc;

		acc = pjsip_accept_hdr_create(rdata->tp_info.pool);
		PJ_ASSERT_RETURN(acc, PJ_ENOMEM);
		acc->values[acc->count++] = pj_str("application/sdp");
		pj_list_push_back(&res_hdr_list, acc);
	    }

	    goto on_return;
	}

	/* Parse and validate SDP */
	status = pjmedia_sdp_parse(rdata->tp_info.pool, 
				   (char*)body->data, body->len, &sdp);
	if (status == PJ_SUCCESS)
	    status = pjmedia_sdp_validate(sdp);

	if (status != PJ_SUCCESS) {
	    /* Unparseable or invalid SDP */
	    code = PJSIP_SC_BAD_REQUEST;

	    if (p_tdata) {
		/* Add Warning header. */
		pjsip_warning_hdr *w;

		w = pjsip_warning_hdr_create_from_status(rdata->tp_info.pool,
							 pjsip_endpt_name(endpt),
							 status);
		PJ_ASSERT_RETURN(w, PJ_ENOMEM);

		pj_list_push_back(&res_hdr_list, w);
	    }

	    goto on_return;
	}

	/* Negotiate with local SDP */
	if (l_sdp) {
	    pjmedia_sdp_neg *neg;

	    /* Local SDP must be valid! */
	    PJ_ASSERT_RETURN((status=pjmedia_sdp_validate(l_sdp))==PJ_SUCCESS,
			     status);

	    /* Create SDP negotiator */
	    status = pjmedia_sdp_neg_create_w_remote_offer(
			    rdata->tp_info.pool, l_sdp, sdp, &neg);
	    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

	    /* Negotiate SDP */
	    status = pjmedia_sdp_neg_negotiate(rdata->tp_info.pool, neg, 0);
	    if (status != PJ_SUCCESS) {

		/* Incompatible media */
		code = PJSIP_SC_NOT_ACCEPTABLE_HERE;

		if (p_tdata) {
		    pjsip_accept_hdr *acc;
		    pjsip_warning_hdr *w;

		    /* Add Warning header. */
		    w = pjsip_warning_hdr_create_from_status(
					    rdata->tp_info.pool, 
					    pjsip_endpt_name(endpt), status);
		    PJ_ASSERT_RETURN(w, PJ_ENOMEM);

		    pj_list_push_back(&res_hdr_list, w);

		    /* Add Accept header to response */
		    acc = pjsip_accept_hdr_create(rdata->tp_info.pool);
		    PJ_ASSERT_RETURN(acc, PJ_ENOMEM);
		    acc->values[acc->count++] = pj_str("application/sdp");
		    pj_list_push_back(&res_hdr_list, acc);

		}

		goto on_return;
	    }
	}
    }

    /* Check supported methods, see if peer supports UPDATE.
     * We just assume that peer supports standard INVITE, ACK, CANCEL, and BYE
     * implicitly by sending this INVITE.
     */
    allow = (pjsip_allow_hdr*) pjsip_msg_find_hdr(msg, PJSIP_H_ALLOW, NULL);
    if (allow) {
	unsigned i;
	const pj_str_t STR_UPDATE = { "UPDATE", 6 };

	for (i=0; i<allow->count; ++i) {
	    if (pj_stricmp(&allow->values[i], &STR_UPDATE)==0)
		break;
	}

	if (i != allow->count) {
	    /* UPDATE is present in Allow */
	    rem_option |= PJSIP_INV_SUPPORT_UPDATE;
	}

    }

    /* Check Supported header */
    sup_hdr = (pjsip_supported_hdr*)
	      pjsip_msg_find_hdr(msg, PJSIP_H_SUPPORTED, NULL);
    if (sup_hdr) {
	unsigned i;
	pj_str_t STR_100REL = { "100rel", 6};
	pj_str_t STR_TIMER = { "timer", 5 };

	for (i=0; i<sup_hdr->count; ++i) {
	    if (pj_stricmp(&sup_hdr->values[i], &STR_100REL)==0)
		rem_option |= PJSIP_INV_SUPPORT_100REL;
	    else if (pj_stricmp(&sup_hdr->values[i], &STR_TIMER)==0)
		rem_option |= PJSIP_INV_SUPPORT_TIMER;
	}
    }

    /* Check Require header */
    req_hdr = (pjsip_require_hdr*)
	      pjsip_msg_find_hdr(msg, PJSIP_H_REQUIRE, NULL);
    if (req_hdr) {
	unsigned i;
	const pj_str_t STR_100REL = { "100rel", 6};
	const pj_str_t STR_TIMER = { "timer", 5 };
	const pj_str_t STR_REPLACES = { "replaces", 8 };
	unsigned unsupp_cnt = 0;
	pj_str_t unsupp_tags[PJSIP_GENERIC_ARRAY_MAX_COUNT];
	
	for (i=0; i<req_hdr->count; ++i) {
	    if ((*options & PJSIP_INV_SUPPORT_100REL) && 
		pj_stricmp(&req_hdr->values[i], &STR_100REL)==0)
	    {
		rem_option |= PJSIP_INV_REQUIRE_100REL;

	    } else if ((*options && PJSIP_INV_SUPPORT_TIMER) &&
		       pj_stricmp(&req_hdr->values[i], &STR_TIMER)==0)
	    {
		rem_option |= PJSIP_INV_REQUIRE_TIMER;

	    } else if (pj_stricmp(&req_hdr->values[i], &STR_REPLACES)==0) {
		pj_bool_t supp;
		
		supp = pjsip_endpt_has_capability(endpt, PJSIP_H_SUPPORTED, 
						  NULL, &STR_REPLACES);
		if (!supp)
		    unsupp_tags[unsupp_cnt++] = req_hdr->values[i];

	    } else {
		/* Unknown/unsupported extension tag!  */
		unsupp_tags[unsupp_cnt++] = req_hdr->values[i];
	    }
	}

	/* Check if there are required tags that we don't support */
	if (unsupp_cnt) {

	    code = PJSIP_SC_BAD_EXTENSION;
	    status = PJSIP_ERRNO_FROM_SIP_STATUS(code);

	    if (p_tdata) {
		pjsip_unsupported_hdr *unsupp_hdr;
		const pjsip_hdr *h;

		/* Add Unsupported header. */
		unsupp_hdr = pjsip_unsupported_hdr_create(rdata->tp_info.pool);
		PJ_ASSERT_RETURN(unsupp_hdr != NULL, PJ_ENOMEM);

		unsupp_hdr->count = unsupp_cnt;
		for (i=0; i<unsupp_cnt; ++i)
		    unsupp_hdr->values[i] = unsupp_tags[i];

		pj_list_push_back(&res_hdr_list, unsupp_hdr);

		/* Add Supported header. */
		h = pjsip_endpt_get_capability(endpt, PJSIP_H_SUPPORTED, 
					       NULL);
		pj_assert(h);
		if (h) {
		    sup_hdr = (pjsip_supported_hdr*)
			      pjsip_hdr_clone(rdata->tp_info.pool, h);
		    pj_list_push_back(&res_hdr_list, sup_hdr);
		}
	    }

	    goto on_return;
	}
    }

    /* Check if there are local requirements that are not supported
     * by peer.
     */
    if ( ((*options & PJSIP_INV_REQUIRE_100REL)!=0 && 
	  (rem_option & PJSIP_INV_SUPPORT_100REL)==0) ||
	 ((*options & PJSIP_INV_REQUIRE_TIMER)!=0 &&
	  (rem_option & PJSIP_INV_SUPPORT_TIMER)==0))
    {
	code = PJSIP_SC_EXTENSION_REQUIRED;
	status = PJSIP_ERRNO_FROM_SIP_STATUS(code);

	if (p_tdata) {
	    const pjsip_hdr *h;

	    /* Add Require header. */
	    req_hdr = pjsip_require_hdr_create(rdata->tp_info.pool);
	    PJ_ASSERT_RETURN(req_hdr != NULL, PJ_ENOMEM);

	    if (*options & PJSIP_INV_REQUIRE_100REL)
		req_hdr->values[req_hdr->count++] = pj_str("100rel");

	    if (*options & PJSIP_INV_REQUIRE_TIMER)
		req_hdr->values[req_hdr->count++] = pj_str("timer");

	    pj_list_push_back(&res_hdr_list, req_hdr);

	    /* Add Supported header. */
	    h = pjsip_endpt_get_capability(endpt, PJSIP_H_SUPPORTED, 
					   NULL);
	    pj_assert(h);
	    if (h) {
		sup_hdr = (pjsip_supported_hdr*)
			  pjsip_hdr_clone(rdata->tp_info.pool, h);
		pj_list_push_back(&res_hdr_list, sup_hdr);
	    }

	}

	goto on_return;
    }

    /* If remote Require something that we support, make us Require
     * that feature too.
     */
    if (rem_option & PJSIP_INV_REQUIRE_100REL) {
	    pj_assert(*options & PJSIP_INV_SUPPORT_100REL);
	    *options |= PJSIP_INV_REQUIRE_100REL;
    }
    if (rem_option & PJSIP_INV_REQUIRE_TIMER) {
	    pj_assert(*options & PJSIP_INV_SUPPORT_TIMER);
	    *options |= PJSIP_INV_REQUIRE_TIMER;
    }

on_return:

    /* Create response if necessary */
    if (code != 200 && p_tdata) {
	pjsip_tx_data *tdata;
	const pjsip_hdr *h;

	if (dlg) {
	    status = pjsip_dlg_create_response(dlg, rdata, code, NULL, 
					       &tdata);
	} else {
	    status = pjsip_endpt_create_response(endpt, rdata, code, NULL, 
						 &tdata);
	}

	if (status != PJ_SUCCESS)
	    return status;

	/* Add response headers. */
	h = res_hdr_list.next;
	while (h != &res_hdr_list) {
	    pjsip_hdr *cloned;

	    cloned = (pjsip_hdr*) pjsip_hdr_clone(tdata->pool, h);
	    PJ_ASSERT_RETURN(cloned, PJ_ENOMEM);

	    pjsip_msg_add_hdr(tdata->msg, cloned);

	    h = h->next;
	}

	*p_tdata = tdata;

	/* Can not return PJ_SUCCESS when response message is produced.
	 * Ref: PROTOS test ~#2490
	 */
	if (status == PJ_SUCCESS)
	    status = PJSIP_ERRNO_FROM_SIP_STATUS(code);

    }

    return status;
}

/*
 * Create UAS invite session.
 */
PJ_DEF(pj_status_t) pjsip_inv_create_uas( pjsip_dialog *dlg,
					  pjsip_rx_data *rdata,
					  const pjmedia_sdp_session *local_sdp,
					  unsigned options,
					  pjsip_inv_session **p_inv)
{
    pjsip_inv_session *inv;
    struct tsx_inv_data *tsx_inv_data;
    pjsip_msg *msg;
    pjmedia_sdp_session *rem_sdp = NULL;
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(dlg && rdata && p_inv, PJ_EINVAL);

    /* Dialog MUST have been initialised. */
    PJ_ASSERT_RETURN(pjsip_rdata_get_tsx(rdata) != NULL, PJ_EINVALIDOP);

    msg = rdata->msg_info.msg;

    /* rdata MUST contain INVITE request */
    PJ_ASSERT_RETURN(msg->type == PJSIP_REQUEST_MSG &&
		     msg->line.req.method.id == PJSIP_INVITE_METHOD,
		     PJ_EINVALIDOP);

    /* Lock dialog */
    pjsip_dlg_inc_lock(dlg);

    /* Normalize options */
    if (options & PJSIP_INV_REQUIRE_100REL)
	options |= PJSIP_INV_SUPPORT_100REL;

#if !PJSIP_HAS_100REL
    /* options cannot specify 100rel if 100rel is disabled */
    PJ_ASSERT_RETURN(
	(options & (PJSIP_INV_REQUIRE_100REL | PJSIP_INV_SUPPORT_100REL))==0,
	PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_BAD_EXTENSION));
    
#endif

    if (options & PJSIP_INV_REQUIRE_TIMER)
	options |= PJSIP_INV_SUPPORT_TIMER;

    /* Create the session */
    inv = PJ_POOL_ZALLOC_T(dlg->pool, pjsip_inv_session);
    pj_assert(inv != NULL);

    inv->pool = dlg->pool;
    inv->role = PJSIP_ROLE_UAS;
    inv->state = PJSIP_INV_STATE_NULL;
    inv->dlg = dlg;
    inv->options = options;
    inv->notify = PJ_TRUE;
    inv->cause = (pjsip_status_code) 0;

    /* Object name will use the same dialog pointer. */
    pj_ansi_snprintf(inv->obj_name, PJ_MAX_OBJ_NAME, "inv%p", dlg);

    /* Parse SDP in message body, if present. */
    if (msg->body) {
	pjsip_msg_body *body = msg->body;

	/* Parse and validate SDP */
	status = pjmedia_sdp_parse(inv->pool, (char*)body->data, body->len,
				   &rem_sdp);
	if (status == PJ_SUCCESS)
	    status = pjmedia_sdp_validate(rem_sdp);

	if (status != PJ_SUCCESS) {
	    pjsip_dlg_dec_lock(dlg);
	    return status;
	}
    }

    /* Create negotiator. */
    if (rem_sdp) {
	status = pjmedia_sdp_neg_create_w_remote_offer(inv->pool, local_sdp,
						       rem_sdp, &inv->neg);
						
    } else if (local_sdp) {
	status = pjmedia_sdp_neg_create_w_local_offer(inv->pool, local_sdp,
						      &inv->neg);
    } else {
	status = PJ_SUCCESS;
    }

    if (status != PJ_SUCCESS) {
	pjsip_dlg_dec_lock(dlg);
	return status;
    }

    /* Register invite as dialog usage. */
    status = pjsip_dlg_add_usage(dlg, &mod_inv.mod, inv);
    if (status != PJ_SUCCESS) {
	pjsip_dlg_dec_lock(dlg);
	return status;
    }

    /* Increment session in the dialog. */
    pjsip_dlg_inc_session(dlg, &mod_inv.mod);

    /* Save the invite transaction. */
    inv->invite_tsx = pjsip_rdata_get_tsx(rdata);

    /* Attach our data to the transaction. */
    tsx_inv_data = PJ_POOL_ZALLOC_T(inv->invite_tsx->pool, struct tsx_inv_data);
    tsx_inv_data->inv = inv;
    inv->invite_tsx->mod_data[mod_inv.mod.id] = tsx_inv_data;

#if PJSIP_HAS_100REL
    /* Create 100rel handler */
    if (inv->options & PJSIP_INV_REQUIRE_100REL) {
	    pjsip_100rel_attach(inv);
    }
#endif

    /* Done */
    pjsip_dlg_dec_lock(dlg);
    *p_inv = inv;

    PJ_LOG(5,(inv->obj_name, "UAS invite session created for dialog %s",
	      dlg->obj_name));

    return PJ_SUCCESS;
}

/*
 * Forcefully terminate the session.
 */
PJ_DEF(pj_status_t) pjsip_inv_terminate( pjsip_inv_session *inv,
				         int st_code,
					 pj_bool_t notify)
{
    PJ_ASSERT_RETURN(inv, PJ_EINVAL);

    /* Lock dialog. */
    pjsip_dlg_inc_lock(inv->dlg);

    /* Set callback notify flag. */
    inv->notify = notify;

    /* If there's pending transaction, terminate the transaction. 
     * This may subsequently set the INVITE session state to
     * disconnected.
     */
    if (inv->invite_tsx && 
	inv->invite_tsx->state <= PJSIP_TSX_STATE_COMPLETED)
    {
	pjsip_tsx_terminate(inv->invite_tsx, st_code);

    }

    /* Set cause. */
    inv_set_cause(inv, st_code, NULL);

    /* Forcefully terminate the session if state is not DISCONNECTED */
    if (inv->state != PJSIP_INV_STATE_DISCONNECTED) {
	inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, NULL);
    }

    /* Done.
     * The dec_lock() below will actually destroys the dialog if it
     * has no other session.
     */
    pjsip_dlg_dec_lock(inv->dlg);

    return PJ_SUCCESS;
}


static void *clone_sdp(pj_pool_t *pool, const void *data, unsigned len)
{
    PJ_UNUSED_ARG(len);
    return pjmedia_sdp_session_clone(pool, (const pjmedia_sdp_session*)data);
}

static int print_sdp(pjsip_msg_body *body, char *buf, pj_size_t len)
{
    return pjmedia_sdp_print((const pjmedia_sdp_session*)body->data, buf, len);
}


PJ_DEF(pj_status_t) pjsip_create_sdp_body( pj_pool_t *pool,
					   pjmedia_sdp_session *sdp,
					   pjsip_msg_body **p_body)
{
    const pj_str_t STR_APPLICATION = { "application", 11};
    const pj_str_t STR_SDP = { "sdp", 3 };
    pjsip_msg_body *body;

    body = PJ_POOL_ZALLOC_T(pool, pjsip_msg_body);
    PJ_ASSERT_RETURN(body != NULL, PJ_ENOMEM);

    body->content_type.type = STR_APPLICATION;
    body->content_type.subtype = STR_SDP;
    body->data = sdp;
    body->len = 0;
    body->clone_data = &clone_sdp;
    body->print_body = &print_sdp;

    *p_body = body;

    return PJ_SUCCESS;
}

static pjsip_msg_body *create_sdp_body(pj_pool_t *pool,
				       const pjmedia_sdp_session *c_sdp)
{
    pjsip_msg_body *body;
    pj_status_t status;

    status = pjsip_create_sdp_body(pool, 
				   pjmedia_sdp_session_clone(pool, c_sdp),
				   &body);

    if (status != PJ_SUCCESS)
	return NULL;

    return body;
}

/*
 * Create initial INVITE request.
 */
PJ_DEF(pj_status_t) pjsip_inv_invite( pjsip_inv_session *inv,
				      pjsip_tx_data **p_tdata )
{
    pjsip_tx_data *tdata;
    const pjsip_hdr *hdr;
    pj_bool_t has_sdp;
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(inv && p_tdata, PJ_EINVAL);

    /* State MUST be NULL or CONFIRMED. */
    PJ_ASSERT_RETURN(inv->state == PJSIP_INV_STATE_NULL ||
		     inv->state == PJSIP_INV_STATE_CONFIRMED, 
		     PJ_EINVALIDOP);

    /* Lock dialog. */
    pjsip_dlg_inc_lock(inv->dlg);

    /* Create the INVITE request. */
    status = pjsip_dlg_create_request(inv->dlg, pjsip_get_invite_method(), -1,
				      &tdata);
    if (status != PJ_SUCCESS)
	goto on_return;


    /* If this is the first INVITE, then copy the headers from inv_hdr.
     * These are the headers parsed from the request URI when the
     * dialog was created.
     */
    if (inv->state == PJSIP_INV_STATE_NULL) {
	hdr = inv->dlg->inv_hdr.next;

	while (hdr != &inv->dlg->inv_hdr) {
	    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)
			      pjsip_hdr_shallow_clone(tdata->pool, hdr));
	    hdr = hdr->next;
	}
    }

    /* See if we have SDP to send. */
    if (inv->neg) {
	pjmedia_sdp_neg_state neg_state;

	neg_state = pjmedia_sdp_neg_get_state(inv->neg);

	has_sdp = (neg_state == PJMEDIA_SDP_NEG_STATE_LOCAL_OFFER ||
		   (neg_state == PJMEDIA_SDP_NEG_STATE_WAIT_NEGO &&
		    pjmedia_sdp_neg_has_local_answer(inv->neg)));


    } else {
	has_sdp = PJ_FALSE;
    }

    /* Add SDP, if any. */
    if (has_sdp) {
	const pjmedia_sdp_session *offer;

	status = pjmedia_sdp_neg_get_neg_local(inv->neg, &offer);
	if (status != PJ_SUCCESS)
	    goto on_return;

	tdata->msg->body = create_sdp_body(tdata->pool, offer);
    }

    /* Add Allow header. */
    if (inv->dlg->add_allow) {
	hdr = pjsip_endpt_get_capability(inv->dlg->endpt, PJSIP_H_ALLOW, NULL);
	if (hdr) {
	    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)
			      pjsip_hdr_shallow_clone(tdata->pool, hdr));
	}
    }

    /* Add Supported header */
    hdr = pjsip_endpt_get_capability(inv->dlg->endpt, PJSIP_H_SUPPORTED, NULL);
    if (hdr) {
	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)
			  pjsip_hdr_shallow_clone(tdata->pool, hdr));
    }

    /* Add Require header. */
    if (inv->options & PJSIP_INV_REQUIRE_100REL) {
	    const pj_str_t HREQ = { "Require", 7 };
	    const pj_str_t tag_100rel = { "100rel", 6 };
	    pjsip_generic_string_hdr *hreq;

	    hreq = pjsip_generic_string_hdr_create(tdata->pool, &HREQ, 
						   &tag_100rel);
	    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*) hreq);
    }

    /* Done. */
    *p_tdata = tdata;


on_return:
    pjsip_dlg_dec_lock(inv->dlg);
    return status;
}


/*
 * Negotiate SDP.
 */
static pj_status_t inv_negotiate_sdp( pjsip_inv_session *inv )
{
    pj_status_t status;

    PJ_ASSERT_RETURN(pjmedia_sdp_neg_get_state(inv->neg) ==
		     PJMEDIA_SDP_NEG_STATE_WAIT_NEGO, 
		     PJMEDIA_SDPNEG_EINSTATE);

    status = pjmedia_sdp_neg_negotiate(inv->pool, inv->neg, 0);

    PJ_LOG(5,(inv->obj_name, "SDP negotiation done, status=%d", status));

    if (mod_inv.cb.on_media_update && inv->notify)
	(*mod_inv.cb.on_media_update)(inv, status);

    return status;
}

/*
 * Check in incoming message for SDP offer/answer.
 */
static pj_status_t inv_check_sdp_in_incoming_msg( pjsip_inv_session *inv,
						  pjsip_transaction *tsx,
						  pjsip_rx_data *rdata)
{
    struct tsx_inv_data *tsx_inv_data;
    static const pj_str_t str_application = { "application", 11 };
    static const pj_str_t str_sdp = { "sdp", 3 };
    pj_status_t status;
    pjsip_msg *msg;
    pjmedia_sdp_session *sdp;

    /* Get/attach invite session's transaction data */
    tsx_inv_data = (struct tsx_inv_data*) tsx->mod_data[mod_inv.mod.id];
    if (tsx_inv_data == NULL) {
	tsx_inv_data = PJ_POOL_ZALLOC_T(tsx->pool, struct tsx_inv_data);
	tsx_inv_data->inv = inv;
	tsx->mod_data[mod_inv.mod.id] = tsx_inv_data;
    }

    /* MUST NOT do multiple SDP offer/answer in a single transaction. 
     */

    if (tsx_inv_data->sdp_done) {
	if (rdata->msg_info.msg->body) {
	    PJ_LOG(4,(inv->obj_name, "SDP negotiation done, message "
		      "body is ignored"));
	}
	return PJ_SUCCESS;
    }

    /* Check if SDP is present in the message. */

    msg = rdata->msg_info.msg;
    if (msg->body == NULL) {
	/* Message doesn't have body. */
	return PJ_SUCCESS;
    }

    if (pj_stricmp(&msg->body->content_type.type, &str_application) ||
	pj_stricmp(&msg->body->content_type.subtype, &str_sdp))
    {
	/* Message body is not "application/sdp" */
	return PJMEDIA_SDP_EINSDP;
    }

    /* Parse the SDP body. */

    status = pjmedia_sdp_parse(rdata->tp_info.pool, 
			       (char*)msg->body->data,
			       msg->body->len, &sdp);
    if (status != PJ_SUCCESS) {
	char errmsg[PJ_ERR_MSG_SIZE];
	pj_strerror(status, errmsg, sizeof(errmsg));
	PJ_LOG(4,(THIS_FILE, "Error parsing SDP in %s: %s",
		  pjsip_rx_data_get_info(rdata), errmsg));
	return PJMEDIA_SDP_EINSDP;
    }

    /* The SDP can be an offer or answer, depending on negotiator's state */

    if (inv->neg == NULL ||
	pjmedia_sdp_neg_get_state(inv->neg) == PJMEDIA_SDP_NEG_STATE_DONE) 
    {

	/* This is an offer. */

	PJ_LOG(5,(inv->obj_name, "Got SDP offer in %s", 
		  pjsip_rx_data_get_info(rdata)));

	if (inv->neg == NULL) {
	    status=pjmedia_sdp_neg_create_w_remote_offer(inv->pool, NULL, 
							 sdp, &inv->neg);
	} else {
	    status=pjmedia_sdp_neg_set_remote_offer(inv->pool, inv->neg, sdp);
	}

	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    pj_strerror(status, errmsg, sizeof(errmsg));
	    PJ_LOG(4,(THIS_FILE, "Error processing SDP offer in %s: %s",
		      pjsip_rx_data_get_info(rdata), errmsg));
	    return PJMEDIA_SDP_EINSDP;
	}

	/* Inform application about remote offer. */

	if (mod_inv.cb.on_rx_offer && inv->notify) {

	    (*mod_inv.cb.on_rx_offer)(inv, sdp);

	}

    } else if (pjmedia_sdp_neg_get_state(inv->neg) == 
		PJMEDIA_SDP_NEG_STATE_LOCAL_OFFER) 
    {

	/* This is an answer. 
	 * Process and negotiate remote answer.
	 */

	PJ_LOG(5,(inv->obj_name, "Got SDP answer in %s", 
		  pjsip_rx_data_get_info(rdata)));

	status = pjmedia_sdp_neg_set_remote_answer(inv->pool, inv->neg, sdp);

	if (status != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];
	    pj_strerror(status, errmsg, sizeof(errmsg));
	    PJ_LOG(4,(THIS_FILE, "Error processing SDP answer in %s: %s",
		      pjsip_rx_data_get_info(rdata), errmsg));
	    return PJMEDIA_SDP_EINSDP;
	}

	/* Negotiate SDP */

	inv_negotiate_sdp(inv);

	/* Mark this transaction has having SDP offer/answer done. */

	tsx_inv_data->sdp_done = 1;

    } else {
	
	PJ_LOG(5,(THIS_FILE, "Ignored SDP in %s: negotiator state is %s",
	      pjsip_rx_data_get_info(rdata), 
	      pjmedia_sdp_neg_state_str(pjmedia_sdp_neg_get_state(inv->neg))));
    }

    return PJ_SUCCESS;
}


/*
 * Process INVITE answer, for both initial and subsequent re-INVITE
 */
static pj_status_t process_answer( pjsip_inv_session *inv,
				   int st_code,
				   pjsip_tx_data *tdata,
				   const pjmedia_sdp_session *local_sdp)
{
    pj_status_t status;
    const pjmedia_sdp_session *sdp = NULL;

    /* If local_sdp is specified, then we MUST NOT have answered the
     * offer before. 
     */
    if (local_sdp && (st_code/100==1 || st_code/100==2)) {

	if (inv->neg == NULL) {
	    status = pjmedia_sdp_neg_create_w_local_offer(inv->pool, local_sdp,
							  &inv->neg);
	} else if (pjmedia_sdp_neg_get_state(inv->neg)==
		   PJMEDIA_SDP_NEG_STATE_REMOTE_OFFER)
	{
	    status = pjmedia_sdp_neg_set_local_answer(inv->pool, inv->neg,
						      local_sdp);
	} else {

	    /* Can not specify local SDP at this state. */
	    pj_assert(0);
	    status = PJMEDIA_SDPNEG_EINSTATE;
	}

	if (status != PJ_SUCCESS)
	    return status;

    }


     /* If SDP negotiator is ready, start negotiation. */
    if (st_code/100==2 || (st_code/10==18 && st_code!=180)) {

	pjmedia_sdp_neg_state neg_state;

	/* Start nego when appropriate. */
	neg_state = inv->neg ? pjmedia_sdp_neg_get_state(inv->neg) :
		    PJMEDIA_SDP_NEG_STATE_NULL;

	if (neg_state == PJMEDIA_SDP_NEG_STATE_LOCAL_OFFER) {

	    status = pjmedia_sdp_neg_get_neg_local(inv->neg, &sdp);

	} else if (neg_state == PJMEDIA_SDP_NEG_STATE_WAIT_NEGO &&
		   pjmedia_sdp_neg_has_local_answer(inv->neg) )
	{
	    struct tsx_inv_data *tsx_inv_data;

	    /* Get invite session's transaction data */
	    tsx_inv_data = (struct tsx_inv_data*) 
		           inv->invite_tsx->mod_data[mod_inv.mod.id];

	    status = inv_negotiate_sdp(inv);
	    if (status != PJ_SUCCESS)
		return status;
	    
	    /* Mark this transaction has having SDP offer/answer done. */
	    tsx_inv_data->sdp_done = 1;

	    status = pjmedia_sdp_neg_get_active_local(inv->neg, &sdp);
	}
    }

    /* Include SDP when it's available for 2xx and 18x (but not 180) response.
     * Subsequent response will include this SDP.
     */
    if (sdp) {
	tdata->msg->body = create_sdp_body(tdata->pool, sdp);
    }


    return PJ_SUCCESS;
}


/*
 * Create first response to INVITE
 */
PJ_DEF(pj_status_t) pjsip_inv_initial_answer(	pjsip_inv_session *inv,
						pjsip_rx_data *rdata,
						int st_code,
						const pj_str_t *st_text,
						const pjmedia_sdp_session *sdp,
						pjsip_tx_data **p_tdata)
{
    pjsip_tx_data *tdata;
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(inv && p_tdata, PJ_EINVAL);

    /* Must have INVITE transaction. */
    PJ_ASSERT_RETURN(inv->invite_tsx, PJ_EBUG);

    pjsip_dlg_inc_lock(inv->dlg);

    /* Create response */
    status = pjsip_dlg_create_response(inv->dlg, rdata, st_code, st_text,
				       &tdata);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Process SDP in answer */
    status = process_answer(inv, st_code, tdata, sdp);
    if (status != PJ_SUCCESS) {
	pjsip_tx_data_dec_ref(tdata);
	goto on_return;
    }

    /* Save this answer */
    inv->last_answer = tdata;
    pjsip_tx_data_add_ref(inv->last_answer);
    PJ_LOG(5,(inv->dlg->obj_name, "Initial answer %s",
	      pjsip_tx_data_get_info(inv->last_answer)));

    *p_tdata = tdata;

on_return:
    pjsip_dlg_dec_lock(inv->dlg);
    return status;
}


/*
 * Answer initial INVITE
 * Re-INVITE will be answered automatically, and will not use this function.
 */ 
PJ_DEF(pj_status_t) pjsip_inv_answer(	pjsip_inv_session *inv,
					int st_code,
					const pj_str_t *st_text,
					const pjmedia_sdp_session *local_sdp,
					pjsip_tx_data **p_tdata )
{
    pjsip_tx_data *last_res;
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(inv && p_tdata, PJ_EINVAL);

    /* Must have INVITE transaction. */
    PJ_ASSERT_RETURN(inv->invite_tsx, PJ_EBUG);

    /* Must have created an answer before */
    PJ_ASSERT_RETURN(inv->last_answer, PJ_EINVALIDOP);

    pjsip_dlg_inc_lock(inv->dlg);

    /* Modify last response. */
    last_res = inv->last_answer;
    status = pjsip_dlg_modify_response(inv->dlg, last_res, st_code, st_text);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* For non-2xx final response, strip message body */
    if (st_code >= 300) {
	last_res->msg->body = NULL;
    }

    /* Process SDP in answer */
    status = process_answer(inv, st_code, last_res, local_sdp);
    if (status != PJ_SUCCESS) {
	pjsip_tx_data_dec_ref(last_res);
	goto on_return;
    }


    *p_tdata = last_res;

on_return:
    pjsip_dlg_dec_lock(inv->dlg);
    return status;
}


/*
 * Set SDP answer.
 */
PJ_DEF(pj_status_t) pjsip_inv_set_sdp_answer( pjsip_inv_session *inv,
					      const pjmedia_sdp_session *sdp )
{
    pj_status_t status;

    PJ_ASSERT_RETURN(inv && sdp, PJ_EINVAL);

    pjsip_dlg_inc_lock(inv->dlg);
    status = pjmedia_sdp_neg_set_local_answer( inv->pool, inv->neg, sdp);
    pjsip_dlg_dec_lock(inv->dlg);

    return status;
}


/*
 * End session.
 */
PJ_DEF(pj_status_t) pjsip_inv_end_session(  pjsip_inv_session *inv,
					    int st_code,
					    const pj_str_t *st_text,
					    pjsip_tx_data **p_tdata )
{
    pjsip_tx_data *tdata;
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(inv && p_tdata, PJ_EINVAL);

    /* Set cause code. */
    inv_set_cause(inv, st_code, st_text);

    /* Create appropriate message. */
    switch (inv->state) {
    case PJSIP_INV_STATE_CALLING:
    case PJSIP_INV_STATE_EARLY:
    case PJSIP_INV_STATE_INCOMING:

	if (inv->role == PJSIP_ROLE_UAC) {

	    /* For UAC when session has not been confirmed, create CANCEL. */

	    /* MUST have the original UAC INVITE transaction. */
	    PJ_ASSERT_RETURN(inv->invite_tsx != NULL, PJ_EBUG);

	    /* But CANCEL should only be called when we have received a
	     * provisional response. If we haven't received any responses,
	     * just destroy the transaction.
	     */
	    if (inv->invite_tsx->status_code < 100) {

		pjsip_tsx_stop_retransmit(inv->invite_tsx);
		inv->cancelling = PJ_TRUE;
		inv->pending_cancel = PJ_TRUE;
		*p_tdata = NULL;
		PJ_LOG(4, (inv->obj_name, "Stopping retransmission, "
			   "delaying CANCEL"));
		return PJ_SUCCESS;
	    }

	    /* The CSeq here assumes that the dialog is started with an
	     * INVITE session. This may not be correct; dialog can be 
	     * started as SUBSCRIBE session.
	     * So fix this!
	     */
	    status = pjsip_endpt_create_cancel(inv->dlg->endpt, 
					       inv->invite_tsx->last_tx,
					       &tdata);

	} else {

	    /* For UAS, send a final response. */
	    tdata = inv->invite_tsx->last_tx;
	    PJ_ASSERT_RETURN(tdata != NULL, PJ_EINVALIDOP);

	    //status = pjsip_dlg_modify_response(inv->dlg, tdata, st_code,
	    //				       st_text);
	    status = pjsip_inv_answer(inv, st_code, st_text, NULL, &tdata);
	}
	break;

    case PJSIP_INV_STATE_CONNECTING:
    case PJSIP_INV_STATE_CONFIRMED:
	/* For established dialog, send BYE */
	status = pjsip_dlg_create_request(inv->dlg, pjsip_get_bye_method(), 
					  -1, &tdata);
	break;

    case PJSIP_INV_STATE_DISCONNECTED:
	/* No need to do anything. */
	return PJSIP_ESESSIONTERMINATED;

    default:
	pj_assert("!Invalid operation!");
	return PJ_EINVALIDOP;
    }

    if (status != PJ_SUCCESS)
	return status;


    /* Done */

    inv->cancelling = PJ_TRUE;
    *p_tdata = tdata;

    return PJ_SUCCESS;
}


/*
 * Create re-INVITE.
 */
PJ_DEF(pj_status_t) pjsip_inv_reinvite( pjsip_inv_session *inv,
					const pj_str_t *new_contact,
					const pjmedia_sdp_session *new_offer,
					pjsip_tx_data **p_tdata )
{
    pj_status_t status;
    pjsip_contact_hdr *contact_hdr = NULL;

    /* Check arguments. */
    PJ_ASSERT_RETURN(inv && p_tdata, PJ_EINVAL);

    /* Must NOT have a pending INVITE transaction */
    if (inv->invite_tsx!=NULL)
	return PJ_EINVALIDOP;


    pjsip_dlg_inc_lock(inv->dlg);

    if (new_contact) {
	pj_str_t tmp;
	const pj_str_t STR_CONTACT = { "Contact", 7 };

	pj_strdup_with_null(inv->dlg->pool, &tmp, new_contact);
	contact_hdr = (pjsip_contact_hdr*)
		      pjsip_parse_hdr(inv->dlg->pool, &STR_CONTACT, 
				      tmp.ptr, tmp.slen, NULL);
	if (!contact_hdr) {
	    status = PJSIP_EINVALIDURI;
	    goto on_return;
	}
    }


    if (new_offer) {
	if (!inv->neg) {
	    status = pjmedia_sdp_neg_create_w_local_offer(inv->pool, new_offer,
							  &inv->neg);
	    if (status != PJ_SUCCESS)
		goto on_return;

	} else switch (pjmedia_sdp_neg_get_state(inv->neg)) {

	    case PJMEDIA_SDP_NEG_STATE_NULL:
		pj_assert(!"Unexpected SDP neg state NULL");
		status = PJ_EBUG;
		goto on_return;

	    case PJMEDIA_SDP_NEG_STATE_LOCAL_OFFER:
		PJ_LOG(4,(inv->obj_name, 
			  "pjsip_inv_reinvite: already have an offer, new "
			  "offer is ignored"));
		break;

	    case PJMEDIA_SDP_NEG_STATE_REMOTE_OFFER:
		status = pjmedia_sdp_neg_set_local_answer(inv->pool, inv->neg,
							  new_offer);
		if (status != PJ_SUCCESS)
		    goto on_return;
		break;

	    case PJMEDIA_SDP_NEG_STATE_WAIT_NEGO:
		PJ_LOG(4,(inv->obj_name, 
			  "pjsip_inv_reinvite: SDP in WAIT_NEGO state, new "
			  "offer is ignored"));
		break;

	    case PJMEDIA_SDP_NEG_STATE_DONE:
		status = pjmedia_sdp_neg_modify_local_offer(inv->pool,inv->neg,
							    new_offer);
		if (status != PJ_SUCCESS)
		    goto on_return;
		break;
	}
    }

    if (contact_hdr)
	inv->dlg->local.contact = contact_hdr;

    status = pjsip_inv_invite(inv, p_tdata);

on_return:
    pjsip_dlg_dec_lock(inv->dlg);
    return status;
}

/*
 * Create UPDATE.
 */
PJ_DEF(pj_status_t) pjsip_inv_update (	pjsip_inv_session *inv,
					const pj_str_t *new_contact,
					const pjmedia_sdp_session *new_offer,
					pjsip_tx_data **p_tdata )
{
    PJ_UNUSED_ARG(inv);
    PJ_UNUSED_ARG(new_contact);
    PJ_UNUSED_ARG(new_offer);
    PJ_UNUSED_ARG(p_tdata);

    PJ_TODO(CREATE_UPDATE_REQUEST);
    return PJ_ENOTSUP;
}

/*
 * Send a request or response message.
 */
PJ_DEF(pj_status_t) pjsip_inv_send_msg( pjsip_inv_session *inv,
					pjsip_tx_data *tdata)
{
    pj_status_t status;

    /* Verify arguments. */
    PJ_ASSERT_RETURN(inv && tdata, PJ_EINVAL);

    PJ_LOG(5,(inv->obj_name, "Sending %s", 
	      pjsip_tx_data_get_info(tdata)));

    if (tdata->msg->type == PJSIP_REQUEST_MSG) {
	struct tsx_inv_data *tsx_inv_data;

	pjsip_dlg_inc_lock(inv->dlg);

	tsx_inv_data = PJ_POOL_ZALLOC_T(inv->pool, struct tsx_inv_data);
	tsx_inv_data->inv = inv;

	pjsip_dlg_dec_lock(inv->dlg);

	status = pjsip_dlg_send_request(inv->dlg, tdata, mod_inv.mod.id, 
					tsx_inv_data);
	if (status != PJ_SUCCESS)
	    return status;

    } else {
	pjsip_cseq_hdr *cseq;

	/* Can only do this to send response to original INVITE
	 * request.
	 */
	PJ_ASSERT_RETURN((cseq=(pjsip_cseq_hdr*)pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL))!=NULL
			  && (cseq->cseq == inv->invite_tsx->cseq),
			 PJ_EINVALIDOP);

#if PJSIP_HAS_100REL
	if (inv->options & PJSIP_INV_REQUIRE_100REL) {
		status = pjsip_100rel_tx_response(inv, tdata);
	} else 
#endif
	{
		status = pjsip_dlg_send_response(inv->dlg, inv->invite_tsx, tdata);
	}

	if (status != PJ_SUCCESS)
	    return status;
    }

    /* Done (?) */
    return PJ_SUCCESS;
}


/*
 * Respond to incoming CANCEL request.
 */
static void inv_respond_incoming_cancel(pjsip_inv_session *inv,
					pjsip_transaction *cancel_tsx,
					pjsip_rx_data *rdata)
{
    pjsip_tx_data *tdata;
    pjsip_transaction *invite_tsx;
    pj_str_t key;
    pj_status_t status;

    /* See if we have matching INVITE server transaction: */

    pjsip_tsx_create_key(rdata->tp_info.pool, &key, PJSIP_ROLE_UAS,
			 pjsip_get_invite_method(), rdata);
    invite_tsx = pjsip_tsx_layer_find_tsx(&key, PJ_TRUE);

    if (invite_tsx == NULL) {

	/* Invite transaction not found! 
	 * Respond CANCEL with 491 (RFC 3261 Section 9.2 page 42)
	 */
	status = pjsip_dlg_create_response( inv->dlg, rdata, 200, NULL, 
					    &tdata);

    } else {
	/* Always answer CANCEL will 200 (OK) regardless of
	 * the state of the INVITE transaction.
	 */
	status = pjsip_dlg_create_response( inv->dlg, rdata, 200, NULL, 
					    &tdata);
    }

    /* See if we have created the response successfully. */
    if (status != PJ_SUCCESS) return;

    /* Send the CANCEL response */
    status = pjsip_dlg_send_response(inv->dlg, cancel_tsx, tdata);
    if (status != PJ_SUCCESS) return;


    /* See if we need to terminate the UAS INVITE transaction
     * with 487 (Request Terminated) response. 
     */
    if (invite_tsx && invite_tsx->status_code < 200) {

	pj_assert(invite_tsx->last_tx != NULL);

	tdata = invite_tsx->last_tx;

	status = pjsip_dlg_modify_response(inv->dlg, tdata, 487, NULL);
	if (status == PJ_SUCCESS)
	    pjsip_dlg_send_response(inv->dlg, invite_tsx, tdata);
    }

    if (invite_tsx)
	pj_mutex_unlock(invite_tsx->mutex);
}


/*
 * Respond to incoming BYE request.
 */
static void inv_respond_incoming_bye( pjsip_inv_session *inv,
				      pjsip_transaction *bye_tsx,
				      pjsip_rx_data *rdata,
				      pjsip_event *e )
{
    pj_status_t status;
    pjsip_tx_data *tdata;

    /* Respond BYE with 200: */

    status = pjsip_dlg_create_response(inv->dlg, rdata, 200, NULL, &tdata);
    if (status != PJ_SUCCESS) return;

    status = pjsip_dlg_send_response(inv->dlg, bye_tsx, tdata);
    if (status != PJ_SUCCESS) return;

    /* Terminate session: */

    if (inv->state != PJSIP_INV_STATE_DISCONNECTED) {
	inv_set_cause(inv, PJSIP_SC_OK, NULL);
	inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
    }
}

/*
 * Respond to BYE request.
 */
static void inv_handle_bye_response( pjsip_inv_session *inv,
				     pjsip_transaction *tsx,
				     pjsip_rx_data *rdata,
				     pjsip_event *e )
{
    pj_status_t status;
    
    if (e->body.tsx_state.type != PJSIP_EVENT_RX_MSG) {
	inv_set_cause(inv, PJSIP_SC_OK, NULL);
	inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	return;
    }

    /* Handle 401/407 challenge. */
    if (tsx->status_code == 401 || tsx->status_code == 407) {

	pjsip_tx_data *tdata;
	
	status = pjsip_auth_clt_reinit_req( &inv->dlg->auth_sess, 
					    rdata,
					    tsx->last_tx,
					    &tdata);
	
	if (status != PJ_SUCCESS) {
	    
	    /* Does not have proper credentials. 
	     * End the session anyway.
	     */
	    inv_set_cause(inv, PJSIP_SC_OK, NULL);
	    inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	    
	} else {
	    /* Re-send BYE. */
	    status = pjsip_inv_send_msg(inv, tdata);
	}

    } else {

	/* End the session. */
	inv_set_cause(inv, PJSIP_SC_OK, NULL);
	inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
    }

}

/*
 * State NULL is before anything is sent/received.
 */
static void inv_on_state_null( pjsip_inv_session *inv, pjsip_event *e)
{
    pjsip_transaction *tsx = e->body.tsx_state.tsx;
    pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);

    PJ_ASSERT_ON_FAIL(tsx && dlg, return);

    if (tsx->method.id == PJSIP_INVITE_METHOD) {

	/* Keep the initial INVITE transaction. */
	if (inv->invite_tsx == NULL)
	    inv->invite_tsx = tsx;

	if (dlg->role == PJSIP_ROLE_UAC) {

	    switch (tsx->state) {
	    case PJSIP_TSX_STATE_CALLING:
		inv_set_state(inv, PJSIP_INV_STATE_CALLING, e);
		break;
	    default:
		inv_on_state_calling(inv, e);
		break;
	    }

	} else {
	    switch (tsx->state) {
	    case PJSIP_TSX_STATE_TRYING:
		inv_set_state(inv, PJSIP_INV_STATE_INCOMING, e);
		break;
	    case PJSIP_TSX_STATE_PROCEEDING:
		inv_set_state(inv, PJSIP_INV_STATE_INCOMING, e);
		if (tsx->status_code > 100)
		    inv_set_state(inv, PJSIP_INV_STATE_EARLY, e);
		break;
	    default:
		inv_on_state_incoming(inv, e);
		break;
	    }
	}

    } else {
	pj_assert(!"Unexpected transaction type");
    }
}

/*
 * State CALLING is after sending initial INVITE request but before
 * any response (with tag) is received.
 */
static void inv_on_state_calling( pjsip_inv_session *inv, pjsip_event *e)
{
    pjsip_transaction *tsx = e->body.tsx_state.tsx;
    pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);
    pj_status_t status;

    PJ_ASSERT_ON_FAIL(tsx && dlg, return);
    
    if (tsx == inv->invite_tsx) {

	switch (tsx->state) {

	case PJSIP_TSX_STATE_CALLING:
	    inv_set_state(inv, PJSIP_INV_STATE_CALLING, e);
	    break;

	case PJSIP_TSX_STATE_PROCEEDING:
	    if (inv->pending_cancel) {
		pjsip_tx_data *cancel;

		inv->pending_cancel = PJ_FALSE;

		status = pjsip_inv_end_session(inv, 487, NULL, &cancel);
		if (status == PJ_SUCCESS && cancel)
		    status = pjsip_inv_send_msg(inv, cancel);
	    }

	    if (dlg->remote.info->tag.slen) {

		inv_set_state(inv, PJSIP_INV_STATE_EARLY, e);

		inv_check_sdp_in_incoming_msg(inv, tsx, 
					      e->body.tsx_state.src.rdata);

	    } else {
		/* Ignore 100 (Trying) response, as it doesn't change
		 * session state. It only ceases retransmissions.
		 */
	    }
	    break;

	case PJSIP_TSX_STATE_COMPLETED:
	    if (tsx->status_code/100 == 2) {
		
		/* This should not happen.
		 * When transaction receives 2xx, it should be terminated
		 */
		pj_assert(0);
		inv_set_state(inv, PJSIP_INV_STATE_CONNECTING, e);
    
		inv_check_sdp_in_incoming_msg(inv, tsx, 
					      e->body.tsx_state.src.rdata);

	    } else if ((tsx->status_code==401 || tsx->status_code==407) &&
			!inv->cancelling) 
	    {

		/* Handle authentication failure:
		 * Resend the request with Authorization header.
		 */
		pjsip_tx_data *tdata;

		status = pjsip_auth_clt_reinit_req(&inv->dlg->auth_sess, 
						   e->body.tsx_state.src.rdata,
						   tsx->last_tx,
						   &tdata);

		if (status != PJ_SUCCESS) {

		    /* Does not have proper credentials. 
		     * End the session.
		     */
		    inv_set_cause(inv, tsx->status_code, &tsx->status_text);
		    inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);

		} else {

		    /* Restart session. */
		    inv->state = PJSIP_INV_STATE_NULL;
		    inv->invite_tsx = NULL;
		    if (inv->last_answer) {
			pjsip_tx_data_dec_ref(inv->last_answer);
			inv->last_answer = NULL;
		    }

		    /* Send the request. */
		    status = pjsip_inv_send_msg(inv, tdata);
		}

	    } else {

		inv_set_cause(inv, tsx->status_code, &tsx->status_text);
		inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);

	    }
	    break;

	case PJSIP_TSX_STATE_TERMINATED:
	    /* INVITE transaction can be terminated either because UAC
	     * transaction received 2xx response or because of transport
	     * error.
	     */
	    if (tsx->status_code/100 == 2) {
		/* This must be receipt of 2xx response */

		/* Set state to CONNECTING */
		inv_set_state(inv, PJSIP_INV_STATE_CONNECTING, e);

		inv_check_sdp_in_incoming_msg(inv, tsx, 
					      e->body.tsx_state.src.rdata);

		/* Send ACK */
		pj_assert(e->body.tsx_state.type == PJSIP_EVENT_RX_MSG);

		inv_send_ack(inv, e->body.tsx_state.src.rdata);
		inv_set_state(inv, PJSIP_INV_STATE_CONFIRMED, e);


	    } else  {
		inv_set_cause(inv, tsx->status_code, &tsx->status_text);
		inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	    }
	    break;

	default:
	    break;
	}

    } else if (inv->role == PJSIP_ROLE_UAC &&
	       tsx->role == PJSIP_ROLE_UAC &&
	       tsx->method.id == PJSIP_CANCEL_METHOD)
    {
	/*
	 * Handle case when outgoing CANCEL is answered with 481 (Call/
	 * Transaction Does Not Exist), 408, or when it's timed out. In these
	 * cases, disconnect session (i.e. dialog usage only).
	 */
	if (tsx->status_code == PJSIP_SC_CALL_TSX_DOES_NOT_EXIST ||
	    tsx->status_code == PJSIP_SC_REQUEST_TIMEOUT ||
	    tsx->status_code == PJSIP_SC_TSX_TIMEOUT ||
	    tsx->status_code == PJSIP_SC_TSX_TRANSPORT_ERROR)
	{
	    inv_set_cause(inv, tsx->status_code, &tsx->status_text);
	    inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	}
    }
}

/*
 * State INCOMING is after we received the request, but before
 * responses with tag are sent.
 */
static void inv_on_state_incoming( pjsip_inv_session *inv, pjsip_event *e)
{
    pjsip_transaction *tsx = e->body.tsx_state.tsx;
    pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);

    PJ_ASSERT_ON_FAIL(tsx && dlg, return);

    if (tsx == inv->invite_tsx) {

	/*
	 * Handle the INVITE state transition.
	 */

	switch (tsx->state) {

	case PJSIP_TSX_STATE_TRYING:
	    inv_set_state(inv, PJSIP_INV_STATE_INCOMING, e);
	    break;

	case PJSIP_TSX_STATE_PROCEEDING:
	    /*
	     * Transaction sent provisional response.
	     */
	    if (tsx->status_code > 100)
		inv_set_state(inv, PJSIP_INV_STATE_EARLY, e);
	    break;

	case PJSIP_TSX_STATE_COMPLETED:
	    /*
	     * Transaction sent final response.
	     */
	    if (tsx->status_code/100 == 2) {
		inv_set_state(inv, PJSIP_INV_STATE_CONNECTING, e);
	    } else {
		inv_set_cause(inv, tsx->status_code, &tsx->status_text);
		inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	    }
	    break;

	case PJSIP_TSX_STATE_TERMINATED:
	    /* 
	     * This happens on transport error (e.g. failed to send
	     * response)
	     */
	    inv_set_cause(inv, tsx->status_code, &tsx->status_text);
	    inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	    break;

	default:
	    pj_assert(!"Unexpected INVITE state");
	    break;
	}

    } else if (tsx->method.id == PJSIP_CANCEL_METHOD &&
	       tsx->role == PJSIP_ROLE_UAS &&
	       tsx->state < PJSIP_TSX_STATE_COMPLETED &&
	       e->body.tsx_state.type == PJSIP_EVENT_RX_MSG )
    {

	/*
	 * Handle incoming CANCEL request.
	 */

	inv_respond_incoming_cancel(inv, tsx, e->body.tsx_state.src.rdata);

    }
}

/*
 * State EARLY is for both UAS and UAC, after response with To tag
 * is sent/received.
 */
static void inv_on_state_early( pjsip_inv_session *inv, pjsip_event *e)
{
    pjsip_transaction *tsx = e->body.tsx_state.tsx;
    pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);

    PJ_ASSERT_ON_FAIL(tsx && dlg, return);

    if (tsx == inv->invite_tsx) {

	/*
	 * Handle the INVITE state progress.
	 */

	switch (tsx->state) {

	case PJSIP_TSX_STATE_PROCEEDING:
	    /* Send/received another provisional response. */
	    inv_set_state(inv, PJSIP_INV_STATE_EARLY, e);

	    if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		inv_check_sdp_in_incoming_msg(inv, tsx, 
					      e->body.tsx_state.src.rdata);
	    }
	    break;

	case PJSIP_TSX_STATE_COMPLETED:
	    if (tsx->status_code/100 == 2) {
		inv_set_state(inv, PJSIP_INV_STATE_CONNECTING, e);
		if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		    inv_check_sdp_in_incoming_msg(inv, tsx, 
						  e->body.tsx_state.src.rdata);
		}

	    } else {
		inv_set_cause(inv, tsx->status_code, &tsx->status_text);
		inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	    }
	    break;

	case PJSIP_TSX_STATE_CONFIRMED:
	    /* For some reason can go here (maybe when ACK for 2xx has
	     * the same branch value as the INVITE transaction) */

	case PJSIP_TSX_STATE_TERMINATED:
	    /* INVITE transaction can be terminated either because UAC
	     * transaction received 2xx response or because of transport
	     * error.
	     */
	    if (tsx->status_code/100 == 2) {

		/* This must be receipt of 2xx response */

		/* Set state to CONNECTING */
		inv_set_state(inv, PJSIP_INV_STATE_CONNECTING, e);

		if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		    inv_check_sdp_in_incoming_msg(inv, tsx, 
						  e->body.tsx_state.src.rdata);
		}

		/* if UAC, send ACK and move state to confirmed. */
		if (tsx->role == PJSIP_ROLE_UAC) {
		    pj_assert(e->body.tsx_state.type == PJSIP_EVENT_RX_MSG);

		    inv_send_ack(inv, e->body.tsx_state.src.rdata);
		    inv_set_state(inv, PJSIP_INV_STATE_CONFIRMED, e);
		}

	    } else  {
		inv_set_cause(inv, tsx->status_code, &tsx->status_text);
		inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	    }
	    break;

	default:
	    pj_assert(!"Unexpected INVITE tsx state");
	}

    } else if (inv->role == PJSIP_ROLE_UAS &&
	       tsx->role == PJSIP_ROLE_UAS &&
	       tsx->method.id == PJSIP_CANCEL_METHOD &&
	       tsx->state < PJSIP_TSX_STATE_COMPLETED &&
	       e->body.tsx_state.type == PJSIP_EVENT_RX_MSG )
    {

	/*
	 * Handle incoming CANCEL request.
	 */

	inv_respond_incoming_cancel(inv, tsx, e->body.tsx_state.src.rdata);

    } else if (inv->role == PJSIP_ROLE_UAC &&
	       tsx->role == PJSIP_ROLE_UAC &&
	       tsx->method.id == PJSIP_CANCEL_METHOD)
    {
	/*
	 * Handle case when outgoing CANCEL is answered with 481 (Call/
	 * Transaction Does Not Exist), 408, or when it's timed out. In these
	 * cases, disconnect session (i.e. dialog usage only).
	 */
	if (tsx->status_code == PJSIP_SC_CALL_TSX_DOES_NOT_EXIST ||
	    tsx->status_code == PJSIP_SC_REQUEST_TIMEOUT ||
	    tsx->status_code == PJSIP_SC_TSX_TIMEOUT ||
	    tsx->status_code == PJSIP_SC_TSX_TRANSPORT_ERROR)
	{
	    inv_set_cause(inv, tsx->status_code, &tsx->status_text);
	    inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	}
    }
}

/*
 * State CONNECTING is after 2xx response to INVITE is sent/received.
 */
static void inv_on_state_connecting( pjsip_inv_session *inv, pjsip_event *e)
{
    pjsip_transaction *tsx = e->body.tsx_state.tsx;
    pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);

    PJ_ASSERT_ON_FAIL(tsx && dlg, return);

    if (tsx == inv->invite_tsx) {

	/*
	 * Handle INVITE state progression.
	 */
	switch (tsx->state) {

	case PJSIP_TSX_STATE_CONFIRMED:
	    /* It can only go here if incoming ACK request has the same Via
	     * branch parameter as the INVITE transaction.
	     */
	    if (tsx->status_code/100 == 2) {
		if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
		    inv_check_sdp_in_incoming_msg(inv, tsx,
						  e->body.tsx_state.src.rdata);
		}

		inv_set_state(inv, PJSIP_INV_STATE_CONFIRMED, e);
	    }
	    break;

	case PJSIP_TSX_STATE_TERMINATED:
	    /* INVITE transaction can be terminated either because UAC
	     * transaction received 2xx response or because of transport
	     * error.
	     */
	    if (tsx->status_code/100 != 2) {
		inv_set_cause(inv, tsx->status_code, &tsx->status_text);
		inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);
	    }
	    break;

	case PJSIP_TSX_STATE_DESTROYED:
	    /* Do nothing. */
	    break;

	default:
	    pj_assert(!"Unexpected state");
	}

    } else if (tsx->role == PJSIP_ROLE_UAS &&
	       tsx->method.id == PJSIP_BYE_METHOD &&
	       tsx->status_code < 200 &&
	       e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) 
    {

	/*
	 * Handle incoming BYE.
	 */

	inv_respond_incoming_bye( inv, tsx, e->body.tsx_state.src.rdata, e );

    } else if (tsx->method.id == PJSIP_BYE_METHOD &&
	       tsx->role == PJSIP_ROLE_UAC &&
	       (tsx->state == PJSIP_TSX_STATE_COMPLETED ||
	        tsx->state == PJSIP_TSX_STATE_TERMINATED))
    {

	/*
	 * Outgoing BYE
	 */
	inv_handle_bye_response( inv, tsx, e->body.tsx_state.src.rdata, e);

    }
    else if (tsx->method.id == PJSIP_CANCEL_METHOD &&
	     tsx->role == PJSIP_ROLE_UAS &&
	     tsx->status_code < 200 &&
	     e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) 
    {

	/*
	 * Handle strandled incoming CANCEL.
	 */
	pjsip_rx_data *rdata = e->body.tsx_state.src.rdata;
	pjsip_tx_data *tdata;
	pj_status_t status;

	status = pjsip_dlg_create_response(dlg, rdata, 200, NULL, &tdata);
	if (status != PJ_SUCCESS) return;

	status = pjsip_dlg_send_response(dlg, tsx, tdata);
	if (status != PJ_SUCCESS) return;

    }
}

/*
 * State CONFIRMED is after ACK is sent/received.
 */
static void inv_on_state_confirmed( pjsip_inv_session *inv, pjsip_event *e)
{
    pjsip_transaction *tsx = e->body.tsx_state.tsx;
    pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);

    PJ_ASSERT_ON_FAIL(tsx && dlg, return);


    if (tsx->method.id == PJSIP_BYE_METHOD &&
	tsx->role == PJSIP_ROLE_UAC &&
	(tsx->state == PJSIP_TSX_STATE_COMPLETED ||
	 tsx->state == PJSIP_TSX_STATE_TERMINATED))
    {

	/*
	 * Outgoing BYE
	 */

	inv_handle_bye_response( inv, tsx, e->body.tsx_state.src.rdata, e);

    }
    else if (tsx->method.id == PJSIP_BYE_METHOD &&
	     tsx->role == PJSIP_ROLE_UAS &&
	     tsx->status_code < 200 &&
	     e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) 
    {

	/*
	 * Handle incoming BYE.
	 */

	inv_respond_incoming_bye( inv, tsx, e->body.tsx_state.src.rdata, e );

    }
    else if (tsx->method.id == PJSIP_CANCEL_METHOD &&
	     tsx->role == PJSIP_ROLE_UAS &&
	     tsx->status_code < 200 &&
	     e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) 
    {

	/*
	 * Handle strandled incoming CANCEL.
	 */
	pjsip_rx_data *rdata = e->body.tsx_state.src.rdata;
	pjsip_tx_data *tdata;
	pj_status_t status;

	status = pjsip_dlg_create_response(dlg, rdata, 200, NULL, &tdata);
	if (status != PJ_SUCCESS) return;

	status = pjsip_dlg_send_response(dlg, tsx, tdata);
	if (status != PJ_SUCCESS) return;

    }
    else if (tsx->method.id == PJSIP_INVITE_METHOD &&
	     tsx->role == PJSIP_ROLE_UAS)
    {

	/*
	 * Handle incoming re-INVITE
	 */
	if (tsx->state == PJSIP_TSX_STATE_TRYING) {
	    
	    pjsip_rx_data *rdata = e->body.tsx_state.src.rdata;
	    pjsip_tx_data *tdata;
	    pj_status_t status;

	    /* Check if we have INVITE pending. */
	    if (inv->invite_tsx && inv->invite_tsx!=tsx) {
		pj_str_t reason;

		reason = pj_str("Another INVITE transaction in progress");

		/* Can not receive re-INVITE while another one is pending. */
		status = pjsip_dlg_create_response( inv->dlg, rdata, 500, 
						    &reason, &tdata);
		if (status != PJ_SUCCESS)
		    return;

		status = pjsip_dlg_send_response( inv->dlg, tsx, tdata);
		

		return;
	    }

	    /* Save the invite transaction. */
	    inv->invite_tsx = tsx;

	    /* Process SDP in incoming message. */
	    status = inv_check_sdp_in_incoming_msg(inv, tsx, rdata);

	    if (status != PJ_SUCCESS) {

		/* Not Acceptable */
		const pjsip_hdr *accept;

		status = pjsip_dlg_create_response(inv->dlg, rdata, 
						   488, NULL, &tdata);
		if (status != PJ_SUCCESS)
		    return;


		accept = pjsip_endpt_get_capability(dlg->endpt, PJSIP_H_ACCEPT,
						    NULL);
		if (accept) {
		    pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)
				      pjsip_hdr_clone(tdata->pool, accept));
		}

		status = pjsip_dlg_send_response(dlg, tsx, tdata);

		return;
	    }

	    /* Create 2xx ANSWER */
	    status = pjsip_dlg_create_response(dlg, rdata, 200, NULL, &tdata);
	    if (status != PJ_SUCCESS)
		return;

	    /* If the INVITE request has SDP body, send answer.
	     * Otherwise generate offer from local active SDP.
	     */
	    if (rdata->msg_info.msg->body != NULL) {
		status = process_answer(inv, 200, tdata, NULL);
	    } else {
		/* INVITE does not have SDP. 
		 * If on_create_offer() callback is implemented, ask app.
		 * to generate an offer, otherwise just send active local
		 * SDP to signal that nothing gets modified.
		 */
		pjmedia_sdp_session *sdp = NULL;

		if (mod_inv.cb.on_create_offer)  {
		    (*mod_inv.cb.on_create_offer)(inv, &sdp);
		    if (sdp) {
			status = pjmedia_sdp_neg_modify_local_offer(dlg->pool,
								    inv->neg,
								    sdp);
		    }
		} 
		
		if (sdp == NULL) {
		    const pjmedia_sdp_session *active_sdp = NULL;
		    status = pjmedia_sdp_neg_send_local_offer(dlg->pool, 
							      inv->neg, 
							      &active_sdp);
		    if (status == PJ_SUCCESS)
			sdp = (pjmedia_sdp_session*) active_sdp;
		}

		if (sdp) {
		    tdata->msg->body = create_sdp_body(tdata->pool, sdp);
		}
	    }

	    if (status != PJ_SUCCESS) {
		/*
		 * SDP negotiation has failed.
		 */
		pj_status_t rc;
		pj_str_t reason;

		/* Delete the 2xx answer */
		pjsip_tx_data_dec_ref(tdata);
		
		/* Create 500 response */
		reason = pj_str("SDP negotiation failed");
		rc = pjsip_dlg_create_response(dlg, rdata, 500, &reason, 
					       &tdata);
		if (rc == PJ_SUCCESS) {
		    pjsip_warning_hdr *w;
		    const pj_str_t *endpt_name;

		    endpt_name = pjsip_endpt_name(dlg->endpt);
		    w = pjsip_warning_hdr_create_from_status(tdata->pool, 
							     endpt_name,
							     status);
		    if (w)
			pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)w);

		    pjsip_inv_send_msg(inv, tdata);
		}
		return;
	    }

	    /* Send 2xx regardless of the status of negotiation */
	    status = pjsip_inv_send_msg(inv, tdata);

	} else if (tsx->state == PJSIP_TSX_STATE_CONFIRMED) {
	    /* This is the case where ACK has the same branch as
	     * the INVITE request.
	     */
	    if (tsx->status_code/100 == 2 &&
		e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) 
	    {
		inv_check_sdp_in_incoming_msg(inv, tsx,
					      e->body.tsx_state.src.rdata);
	    }

	}

    }
    else if (tsx->method.id == PJSIP_INVITE_METHOD &&
	     tsx->role == PJSIP_ROLE_UAC)
    {
	/*
	 * Handle outgoing re-INVITE
	 */
	if (tsx->state == PJSIP_TSX_STATE_TERMINATED &&
	    tsx->status_code/100 == 2) 
	{

	    /* Re-INVITE was accepted. */

	    /* Process SDP */
	    inv_check_sdp_in_incoming_msg(inv, tsx, 
					  e->body.tsx_state.src.rdata);

	    /* Send ACK */
	    inv_send_ack(inv, e->body.tsx_state.src.rdata);

	} else if (tsx->state == PJSIP_TSX_STATE_COMPLETED &&
		   (tsx->status_code==401 || tsx->status_code==407))
	{
	    pjsip_tx_data *tdata;
	    pj_status_t status;

	    /* Handle authentication challenge. */
	    status = pjsip_auth_clt_reinit_req( &dlg->auth_sess,
						e->body.tsx_state.src.rdata,
						tsx->last_tx,
						&tdata);
	    if (status != PJ_SUCCESS)
		return;

	    /* Send re-INVITE */
	    status = pjsip_inv_send_msg( inv, tdata);

	} else if (tsx->status_code==PJSIP_SC_CALL_TSX_DOES_NOT_EXIST ||
		   tsx->status_code==PJSIP_SC_REQUEST_TIMEOUT ||
		   tsx->status_code >= 700)
	{
	    /*
	     * Handle responses that terminates dialog.
	     */
	    inv_set_cause(inv, tsx->status_code, &tsx->status_text);
	    inv_set_state(inv, PJSIP_INV_STATE_DISCONNECTED, e);

	} else if (tsx->status_code >= 300 && tsx->status_code < 700) {

	    pjmedia_sdp_neg_state neg_state;

	    /* Outgoing INVITE transaction has failed, cancel SDP nego */
	    neg_state = pjmedia_sdp_neg_get_state(inv->neg);
	    if (neg_state == PJMEDIA_SDP_NEG_STATE_LOCAL_OFFER) {
		pjmedia_sdp_neg_cancel_offer(inv->neg);
	    }
	}
    }
}

/*
 * After session has been terminated, but before dialog is destroyed
 * (because dialog has other usages, or because dialog is waiting for
 * the last transaction to terminate).
 */
static void inv_on_state_disconnected( pjsip_inv_session *inv, pjsip_event *e)
{
    pjsip_transaction *tsx = e->body.tsx_state.tsx;
    pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);

    PJ_ASSERT_ON_FAIL(tsx && dlg, return);

    if (tsx->role == PJSIP_ROLE_UAS &&
	tsx->status_code < 200 &&
	e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) 
    {
	pjsip_rx_data *rdata = e->body.tsx_state.src.rdata;

	/*
	 * Respond BYE with 200/OK
	 */
	if (tsx->method.id == PJSIP_BYE_METHOD) {
	    inv_respond_incoming_bye( inv, tsx, rdata, e );
	} else if (tsx->method.id == PJSIP_CANCEL_METHOD) {
	    /*
	     * Respond CANCEL with 200/OK too.
	     */
	    pjsip_tx_data *tdata;
	    pj_status_t status;

	    status = pjsip_dlg_create_response(dlg, rdata, 200, NULL, &tdata);
	    if (status != PJ_SUCCESS) return;

	    status = pjsip_dlg_send_response(dlg, tsx, tdata);
	    if (status != PJ_SUCCESS) return;

	}
    }
}

