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
#include <pjsip/sip_dialog.h>
#include <pjsip/sip_ua_layer.h>
#include <pjsip/sip_errno.h>
#include <pjsip/sip_endpoint.h>
#include <pjsip/sip_parser.h>
#include <pjsip/sip_module.h>
#include <pjsip/sip_util.h>
#include <pjsip/sip_transaction.h>
#include <pj/assert.h>
#include <pj/os.h>
#include <pj/string.h>
#include <pj/pool.h>
#include <pj/guid.h>
#include <pj/rand.h>
#include <pj/array.h>
#include <pj/except.h>
#include <pj/hash.h>
#include <pj/log.h>

#define THIS_FILE	"sip_dialog.c"

long pjsip_dlg_lock_tls_id;

PJ_DEF(pj_bool_t) pjsip_method_creates_dialog(const pjsip_method *m)
{
    const pjsip_method subscribe = { PJSIP_OTHER_METHOD, {"SUBSCRIBE", 9}};
    const pjsip_method refer = { PJSIP_OTHER_METHOD, {"REFER", 5}};
    const pjsip_method notify = { PJSIP_OTHER_METHOD, {"NOTIFY", 6}};

    return m->id == PJSIP_INVITE_METHOD ||
	   (pjsip_method_cmp(m, &subscribe)==0) ||
	   (pjsip_method_cmp(m, &refer)==0) ||
	   (pjsip_method_cmp(m, &notify)==0);
}

static pj_status_t create_dialog( pjsip_user_agent *ua,
				  pjsip_dialog **p_dlg)
{
    pjsip_endpoint *endpt;
    pj_pool_t *pool;
    pjsip_dialog *dlg;
    pj_status_t status;

    endpt = pjsip_ua_get_endpt(ua);
    if (!endpt)
	return PJ_EINVALIDOP;

    pool = pjsip_endpt_create_pool(endpt, "dlg%p", 
				   PJSIP_POOL_LEN_DIALOG, 
				   PJSIP_POOL_INC_DIALOG);
    if (!pool)
	return PJ_ENOMEM;

    dlg = pj_pool_zalloc(pool, sizeof(pjsip_dialog));
    PJ_ASSERT_RETURN(dlg != NULL, PJ_ENOMEM);

    dlg->pool = pool;
    pj_ansi_sprintf(dlg->obj_name, "dlg%p", dlg);
    dlg->ua = ua;
    dlg->endpt = endpt;
    dlg->state = PJSIP_DIALOG_STATE_NULL;

    pj_list_init(&dlg->inv_hdr);

    status = pj_mutex_create_recursive(pool, "dlg%p", &dlg->mutex);
    if (status != PJ_SUCCESS)
	goto on_error;


    *p_dlg = dlg;
    return PJ_SUCCESS;

on_error:
    if (dlg->mutex)
	pj_mutex_destroy(dlg->mutex);
    pjsip_endpt_release_pool(endpt, pool);
    return status;
}

static void destroy_dialog( pjsip_dialog *dlg )
{
    if (dlg->mutex)
	pj_mutex_destroy(dlg->mutex);
    pjsip_endpt_release_pool(dlg->endpt, dlg->pool);
}


/*
 * Create an UAC dialog.
 */
PJ_DEF(pj_status_t) pjsip_dlg_create_uac( pjsip_user_agent *ua,
					  const pj_str_t *local_uri,
					  const pj_str_t *local_contact,
					  const pj_str_t *remote_uri,
					  const pj_str_t *target,
					  pjsip_dialog **p_dlg)
{
    pj_status_t status;
    pj_str_t tmp;
    pjsip_dialog *dlg;

    /* Check arguments. */
    PJ_ASSERT_RETURN(ua && local_uri && remote_uri && p_dlg, PJ_EINVAL);

    /* Create dialog instance. */
    status = create_dialog(ua, &dlg);
    if (status != PJ_SUCCESS)
	return status;

    /* Parse target. */
    pj_strdup_with_null(dlg->pool, &tmp, target ? target : remote_uri);
    dlg->target = pjsip_parse_uri(dlg->pool, tmp.ptr, tmp.slen, 0);
    if (!dlg->target) {
	status = PJSIP_EINVALIDURI;
	goto on_error;
    }

    /* Put any header param in the target URI into INVITE header list. */
    if (PJSIP_URI_SCHEME_IS_SIP(dlg->target) ||
	PJSIP_URI_SCHEME_IS_SIPS(dlg->target))
    {
	pjsip_param *param;
	pjsip_sip_uri *uri = (pjsip_sip_uri*)pjsip_uri_get_uri(dlg->target);

	param = uri->header_param.next;
	while (param != &uri->header_param) {
	    pjsip_hdr *hdr;

	    hdr = pjsip_parse_hdr(dlg->pool, &param->name, param->value.ptr,
				  param->value.slen, NULL);
	    if (hdr == NULL) {
		status = PJSIP_EINVALIDURI;
		goto on_error;
	    }
	    pj_list_push_back(&dlg->inv_hdr, hdr);

	    param = param->next;
	}

	/* Now must remove any header params from URL, since that would
	 * create another header in pjsip_endpt_create_request().
	 */
	pj_list_init(&uri->header_param);
    }

    /* Init local info. */
    dlg->local.info = pjsip_from_hdr_create(dlg->pool);
    pj_strdup_with_null(dlg->pool, &dlg->local.info_str, local_uri);
    dlg->local.info->uri = pjsip_parse_uri(dlg->pool, 
					   dlg->local.info_str.ptr, 
					   dlg->local.info_str.slen, 0);
    if (!dlg->local.info->uri) {
	status = PJSIP_EINVALIDURI;
	goto on_error;
    }

    /* Generate local tag. */
    pj_create_unique_string(dlg->pool, &dlg->local.info->tag);

    /* Calculate hash value of local tag. */
    dlg->local.tag_hval = pj_hash_calc(0, dlg->local.info->tag.ptr,
				       dlg->local.info->tag.slen);

    /* Randomize local CSeq. */
    dlg->local.first_cseq = pj_rand() % 0x7FFFFFFFL;
    dlg->local.cseq = dlg->local.first_cseq;

    /* Init local contact. */
    dlg->local.contact = pjsip_contact_hdr_create(dlg->pool);
    pj_strdup_with_null(dlg->pool, &tmp, 
			local_contact ? local_contact : local_uri);
    dlg->local.contact->uri = pjsip_parse_uri(dlg->pool, tmp.ptr, tmp.slen,
					      PJSIP_PARSE_URI_AS_NAMEADDR);
    if (!dlg->local.contact->uri) {
	status = PJSIP_EINVALIDURI;
	goto on_error;
    }

    /* Init remote info. */
    dlg->remote.info = pjsip_to_hdr_create(dlg->pool);
    pj_strdup_with_null(dlg->pool, &dlg->remote.info_str, remote_uri);
    dlg->remote.info->uri = pjsip_parse_uri(dlg->pool, 
					    dlg->remote.info_str.ptr, 
					    dlg->remote.info_str.slen, 0);
    if (!dlg->remote.info->uri) {
	status = PJSIP_EINVALIDURI;
	goto on_error;
    }

    /* Initialize remote's CSeq to -1. */
    dlg->remote.cseq = dlg->remote.first_cseq = -1;

    /* Initial role is UAC. */
    dlg->role = PJSIP_ROLE_UAC;

    /* Secure? */
    dlg->secure = PJSIP_URI_SCHEME_IS_SIPS(dlg->target);

    /* Generate Call-ID header. */
    dlg->call_id = pjsip_cid_hdr_create(dlg->pool);
    pj_create_unique_string(dlg->pool, &dlg->call_id->id);

    /* Initial route set is empty. */
    pj_list_init(&dlg->route_set);

    /* Init client authentication session. */
    status = pjsip_auth_clt_init(&dlg->auth_sess, dlg->endpt, 
				 dlg->pool, 0);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Register this dialog to user agent. */
    status = pjsip_ua_register_dlg( ua, dlg );
    if (status != PJ_SUCCESS)
	goto on_error;


    /* Done! */
    *p_dlg = dlg;


    PJ_LOG(5,(dlg->obj_name, "UAC dialog created"));

    return PJ_SUCCESS;

on_error:
    destroy_dialog(dlg);
    return status;
}


/*
 * Create UAS dialog.
 */
PJ_DEF(pj_status_t) pjsip_dlg_create_uas(   pjsip_user_agent *ua,
					    pjsip_rx_data *rdata,
					    const pj_str_t *contact,
					    pjsip_dialog **p_dlg)
{
    pj_status_t status;
    pjsip_hdr *contact_hdr;
    pjsip_rr_hdr *rr;
    pjsip_transaction *tsx = NULL;
    pj_str_t tmp;
    enum { TMP_LEN=128};
    pj_ssize_t len;
    pjsip_dialog *dlg;

    /* Check arguments. */
    PJ_ASSERT_RETURN(ua && rdata && p_dlg, PJ_EINVAL);

    /* rdata must have request message. */
    PJ_ASSERT_RETURN(rdata->msg_info.msg->type == PJSIP_REQUEST_MSG,
		     PJSIP_ENOTREQUESTMSG);

    /* Request must not have To tag. 
     * This should have been checked in the user agent (or application?).
     */
    PJ_ASSERT_RETURN(rdata->msg_info.to->tag.slen == 0, PJ_EINVALIDOP);
		     
    /* The request must be a dialog establishing request. */
    PJ_ASSERT_RETURN(
	pjsip_method_creates_dialog(&rdata->msg_info.msg->line.req.method),
	PJ_EINVALIDOP);

    /* Create dialog instance. */
    status = create_dialog(ua, &dlg);
    if (status != PJ_SUCCESS)
	return status;

    /* Temprary string for getting the string representation of
     * both local and remote URI.
     */
    tmp.ptr = pj_pool_alloc(rdata->tp_info.pool, TMP_LEN);

    /* Init local info from the To header. */
    dlg->local.info = pjsip_hdr_clone(dlg->pool, rdata->msg_info.to);
    pjsip_fromto_hdr_set_from(dlg->local.info);

    /* Generate local tag. */
    pj_create_unique_string(dlg->pool, &dlg->local.info->tag);


    /* Print the local info. */
    len = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR,
			  dlg->local.info->uri, tmp.ptr, TMP_LEN);
    if (len < 1) {
	pj_ansi_strcpy(tmp.ptr, "<-error: uri too long->");
	tmp.slen = pj_ansi_strlen(tmp.ptr);
    } else
	tmp.slen = len;

    /* Save the local info. */
    pj_strdup(dlg->pool, &dlg->local.info_str, &tmp);

    /* Calculate hash value of local tag. */
    dlg->local.tag_hval = pj_hash_calc(0, dlg->local.info->tag.ptr,
				       dlg->local.info->tag.slen);

    /* Print the local info. */
    len = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR,
			  dlg->local.info->uri, tmp.ptr, TMP_LEN);
    if (len < 1) {
	pj_ansi_strcpy(tmp.ptr, "<-error: uri too long->");
	tmp.slen = pj_ansi_strlen(tmp.ptr);
    } else
	tmp.slen = len;

    /* Save the local info. */
    pj_strdup(dlg->pool, &dlg->remote.info_str, &tmp);


    /* Randomize local cseq */
    dlg->local.first_cseq = pj_rand() % 0x7FFFFFFFL;
    dlg->local.cseq = dlg->local.first_cseq;

    /* Init local contact. */
    /* TODO:
     *  Section 12.1.1, paragraph about using SIPS URI in Contact.
     *  If the request that initiated the dialog contained a SIPS URI 
     *  in the Request-URI or in the top Record-Route header field value, 
     *  if there was any, or the Contact header field if there was no 
     *  Record-Route header field, the Contact header field in the response
     *  MUST be a SIPS URI.
     */
    if (contact) {
	pj_str_t tmp;

	dlg->local.contact = pjsip_contact_hdr_create(dlg->pool);
	pj_strdup_with_null(dlg->pool, &tmp, contact);
	dlg->local.contact->uri = pjsip_parse_uri(dlg->pool, tmp.ptr, tmp.slen,
						  PJSIP_PARSE_URI_AS_NAMEADDR);
	if (!dlg->local.contact->uri) {
	    status = PJSIP_EINVALIDURI;
	    goto on_error;
	}

    } else {
	dlg->local.contact = pjsip_contact_hdr_create(dlg->pool);
	dlg->local.contact->uri = dlg->local.info->uri;
    }

    /* Init remote info from the From header. */
    dlg->remote.info = pjsip_hdr_clone(dlg->pool, rdata->msg_info.from);
    pjsip_fromto_hdr_set_to(dlg->remote.info);

    /* Init remote's contact from Contact header. */
    contact_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, 
				     NULL);
    if (!contact_hdr) {
	status = PJSIP_ERRNO_FROM_SIP_STATUS(PJSIP_SC_BAD_REQUEST);
	goto on_error;
    }
    dlg->remote.contact = pjsip_hdr_clone(dlg->pool, contact_hdr);

    /* Init remote's CSeq from CSeq header */
    dlg->remote.cseq = dlg->remote.first_cseq = rdata->msg_info.cseq->cseq;

    /* Set initial target to remote's Contact. */
    dlg->target = dlg->remote.contact->uri;

    /* Initial role is UAS */
    dlg->role = PJSIP_ROLE_UAS;

    /* Secure? 
     *  RFC 3261 Section 12.1.1:
     *  If the request arrived over TLS, and the Request-URI contained a 
     *  SIPS URI, the 'secure' flag is set to TRUE.
     */
    dlg->secure = PJSIP_TRANSPORT_IS_SECURE(rdata->tp_info.transport) &&
		  PJSIP_URI_SCHEME_IS_SIPS(rdata->msg_info.msg->line.req.uri);

    /* Call-ID */
    dlg->call_id = pjsip_hdr_clone(dlg->pool, rdata->msg_info.cid);

    /* Route set. 
     *  RFC 3261 Section 12.1.1:
     *  The route set MUST be set to the list of URIs in the Record-Route 
     *  header field from the request, taken in order and preserving all URI 
     *  parameters. If no Record-Route header field is present in the request,
     * the route set MUST be set to the empty set.
     */
    pj_list_init(&dlg->route_set);
    rr = rdata->msg_info.record_route;
    while (rr != NULL) {
	pjsip_route_hdr *route;

	/* Clone the Record-Route, change the type to Route header. */
	route = pjsip_hdr_clone(dlg->pool, rr);
	pjsip_routing_hdr_set_route(route);

	/* Add to route set. */
	pj_list_push_back(&dlg->route_set, route);

	/* Find next Record-Route header. */
	rr = rr->next;
	if (rr == (pjsip_rr_hdr*)&rdata->msg_info.msg->hdr)
	    break;
	rr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_RECORD_ROUTE, rr);
    }

    /* Init client authentication session. */
    status = pjsip_auth_clt_init(&dlg->auth_sess, dlg->endpt,
				 dlg->pool, 0);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Create UAS transaction for this request. */
    status = pjsip_tsx_create_uas(dlg->ua, rdata, &tsx);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Associate this dialog to the transaction. */
    tsx->mod_data[dlg->ua->id] = dlg;

    /* Increment tsx counter */
    ++dlg->tsx_count;

    /* Calculate hash value of remote tag. */
    dlg->remote.tag_hval = pj_hash_calc(0, dlg->remote.info->tag.ptr, 
					dlg->remote.info->tag.slen);

    /* Register this dialog to user agent. */
    status = pjsip_ua_register_dlg( ua, dlg );
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Put this dialog in rdata's mod_data */
    rdata->endpt_info.mod_data[ua->id] = dlg;

    PJ_TODO(DIALOG_APP_TIMER);

    /* Feed the first request to the transaction. */
    pjsip_tsx_recv_msg(tsx, rdata);

    /* Done. */
    *p_dlg = dlg;
    PJ_LOG(5,(dlg->obj_name, "UAS dialog created"));
    return PJ_SUCCESS;

on_error:
    if (tsx) {
	pjsip_tsx_terminate(tsx, 500);
	--dlg->tsx_count;
    }

    destroy_dialog(dlg);
    return status;
}


/*
 * Create forked dialog from a response.
 */
PJ_DEF(pj_status_t) pjsip_dlg_fork( const pjsip_dialog *first_dlg,
				    const pjsip_rx_data *rdata,
				    pjsip_dialog **new_dlg )
{
    pjsip_dialog *dlg;
    const pjsip_route_hdr *r;
    pj_status_t status;

    /* Check arguments. */
    PJ_ASSERT_RETURN(first_dlg && rdata && new_dlg, PJ_EINVAL);
    
    /* rdata must be response message. */
    PJ_ASSERT_RETURN(rdata->msg_info.msg->type == PJSIP_RESPONSE_MSG,
		     PJSIP_ENOTRESPONSEMSG);

    /* Status code MUST be 1xx (but not 100), or 2xx */
    status = rdata->msg_info.msg->line.status.code;
    PJ_ASSERT_RETURN( (status/100==1 && status!=100) ||
		      (status/100==2), PJ_EBUG);

    /* To tag must present in the response. */
    PJ_ASSERT_RETURN(rdata->msg_info.to->tag.slen != 0, PJSIP_EMISSINGTAG);

    /* Create the dialog. */
    status = create_dialog((pjsip_user_agent*)first_dlg->ua, &dlg);
    if (status != PJ_SUCCESS)
	return status;

    /* Clone remote target. */
    dlg->target = pjsip_uri_clone(dlg->pool, first_dlg->target);

    /* Clone local info. */
    dlg->local.info = pjsip_hdr_clone(dlg->pool, first_dlg->local.info);

    /* Clone local tag. */
    pj_strdup(dlg->pool, &dlg->local.info->tag, &first_dlg->local.info->tag);
    dlg->local.tag_hval = first_dlg->local.tag_hval;

    /* Clone local CSeq. */
    dlg->local.first_cseq = first_dlg->local.first_cseq;
    dlg->local.cseq = first_dlg->local.cseq;

    /* Clone local Contact. */
    dlg->local.contact = pjsip_hdr_clone(dlg->pool, first_dlg->local.contact);

    /* Clone remote info. */
    dlg->remote.info = pjsip_hdr_clone(dlg->pool, first_dlg->remote.info);

    /* Set remote tag from the response. */
    pj_strdup(dlg->pool, &dlg->remote.info->tag, &rdata->msg_info.to->tag);

    /* Initialize remote's CSeq to -1. */
    dlg->remote.cseq = dlg->remote.first_cseq = -1;

    /* Initial role is UAC. */
    dlg->role = PJSIP_ROLE_UAC;

    /* Dialog state depends on the response. */
    status = rdata->msg_info.msg->line.status.code/100;
    if (status == 1 || status == 2)
	dlg->state = PJSIP_DIALOG_STATE_ESTABLISHED;
    else {
	pj_assert(!"Invalid status code");
	dlg->state = PJSIP_DIALOG_STATE_NULL;
    }

    /* Secure? */
    dlg->secure = PJSIP_URI_SCHEME_IS_SIPS(dlg->target);

    /* Clone Call-ID header. */
    dlg->call_id = pjsip_hdr_clone(dlg->pool, first_dlg->call_id);

    /* Duplicate Route-Set. */
    pj_list_init(&dlg->route_set);
    r = first_dlg->route_set.next;
    while (r != &first_dlg->route_set) {
	pjsip_route_hdr *h;

	h = pjsip_hdr_clone(dlg->pool, r);
	pj_list_push_back(&dlg->route_set, h);

	r = r->next;
    }

    /* Clone client authentication session. */
    status = pjsip_auth_clt_clone(dlg->pool, &dlg->auth_sess, 
				  &first_dlg->auth_sess);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Register this dialog to user agent. */
    status = pjsip_ua_register_dlg(dlg->ua, dlg );
    if (status != PJ_SUCCESS)
	goto on_error;


    /* Done! */
    *new_dlg = dlg;

    PJ_LOG(5,(dlg->obj_name, "Forked dialog created"));
    return PJ_SUCCESS;

on_error:
    destroy_dialog(dlg);
    return status;
}


/*
 * Destroy dialog.
 */
static pj_status_t unregister_and_destroy_dialog( pjsip_dialog *dlg )
{
    pj_status_t status;

    /* Lock must have been held. */

    /* Check dialog state. */
    /* Number of sessions must be zero. */
    PJ_ASSERT_RETURN(dlg->sess_count==0, PJ_EINVALIDOP);

    /* MUST not have pending transactions. */
    PJ_ASSERT_RETURN(dlg->tsx_count==0, PJ_EINVALIDOP);

    /* Unregister from user agent. */
    status = pjsip_ua_unregister_dlg(dlg->ua, dlg);
    if (status != PJ_SUCCESS) {
	pj_assert(!"Unexpected failed unregistration!");
	return status;
    }

    /* Log */
    PJ_LOG(5,(dlg->obj_name, "Dialog destroyed"));

    /* Destroy this dialog. */
    pj_mutex_destroy(dlg->mutex);
    pjsip_endpt_release_pool(dlg->endpt, dlg->pool);

    return PJ_SUCCESS;
}


/*
 * Forcefully terminate dialog.
 */
PJ_DEF(pj_status_t) pjsip_dlg_terminate( pjsip_dialog *dlg )
{
    /* Number of sessions must be zero. */
    PJ_ASSERT_RETURN(dlg->sess_count==0, PJ_EINVALIDOP);

    /* MUST not have pending transactions. */
    PJ_ASSERT_RETURN(dlg->tsx_count==0, PJ_EINVALIDOP);

    return unregister_and_destroy_dialog(dlg);
}


/*
 * Set route_set
 */
PJ_DEF(pj_status_t) pjsip_dlg_set_route_set( pjsip_dialog *dlg,
					     const pjsip_route_hdr *route_set )
{
    pjsip_route_hdr *r;

    PJ_ASSERT_RETURN(dlg, PJ_EINVAL);

    pj_mutex_lock(dlg->mutex);

    /* Clear route set. */
    pj_list_init(&dlg->route_set);

    if (!route_set) {
	pj_mutex_unlock(dlg->mutex);
	return PJ_SUCCESS;
    }

    r = route_set->next;
    while (r != route_set) {
	pjsip_route_hdr *new_r;

	new_r = pjsip_hdr_clone(dlg->pool, r);
	pj_list_push_back(&dlg->route_set, new_r);

	r = r->next;
    }

    pj_mutex_unlock(dlg->mutex);

    return PJ_SUCCESS;
}


/*
 * Increment session counter.
 */
PJ_DEF(pj_status_t) pjsip_dlg_inc_session( pjsip_dialog *dlg,
					   pjsip_module *mod )
{
    PJ_ASSERT_RETURN(dlg && mod, PJ_EINVAL);

    pj_mutex_lock(dlg->mutex);
    ++dlg->sess_count;
    pj_mutex_unlock(dlg->mutex);

    PJ_LOG(5,(dlg->obj_name, "Session count inc to %d by %.*s",
	      dlg->sess_count, (int)mod->name.slen, mod->name.ptr));

    return PJ_SUCCESS;
}

/*
 * Lock dialog and increment session counter temporarily
 * to prevent it from being deleted.
 */
PJ_DEF(void) pjsip_dlg_inc_lock(pjsip_dialog *dlg)
{
    pj_mutex_lock(dlg->mutex);
    dlg->sess_count++;
}


/*
 * Unlock dialog and decrement session counter.
 * It may delete the dialog!
 */
PJ_DEF(void) pjsip_dlg_dec_lock(pjsip_dialog *dlg)
{
    pj_assert(dlg->sess_count > 0);
    --dlg->sess_count;

    if (dlg->sess_count==0 && dlg->tsx_count==0)
	unregister_and_destroy_dialog(dlg);
    else {
	pj_mutex_unlock(dlg->mutex);
    }
}



/*
 * Decrement session counter.
 */
PJ_DEF(pj_status_t) pjsip_dlg_dec_session( pjsip_dialog *dlg,
					   pjsip_module *mod)
{
    PJ_ASSERT_RETURN(dlg, PJ_EINVAL);

    PJ_LOG(5,(dlg->obj_name, "Session count dec to %d by %.*s",
	      dlg->sess_count-1, (int)mod->name.slen, mod->name.ptr));

    pj_mutex_lock(dlg->mutex);
    pjsip_dlg_dec_lock(dlg);

    return PJ_SUCCESS;
}

/*
 * Add usage.
 */
PJ_DEF(pj_status_t) pjsip_dlg_add_usage( pjsip_dialog *dlg,
					 pjsip_module *mod,
					 void *mod_data )
{
    unsigned index;

    PJ_ASSERT_RETURN(dlg && mod, PJ_EINVAL);
    PJ_ASSERT_RETURN(mod->id >= 0 && mod->id < PJSIP_MAX_MODULE,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(dlg->usage_cnt < PJSIP_MAX_MODULE, PJ_EBUG);

    PJ_LOG(5,(dlg->obj_name, 
	      "Module %.*s added as dialog usage, data=%p",
	      (int)mod->name.slen, mod->name.ptr, mod_data));

    pj_mutex_lock(dlg->mutex);

    /* Usages are sorted on priority, lowest number first.
     * Find position to put the new module, also makes sure that
     * this module has not been registered before.
     */
    for (index=0; index<dlg->usage_cnt; ++index) {
	if (dlg->usage[index] == mod) {
	    pj_assert(!"This module is already registered");
	    pj_mutex_unlock(dlg->mutex);
	    return PJSIP_ETYPEEXISTS;
	}

	if (dlg->usage[index]->priority > mod->priority)
	    break;
    }

    /* index holds position to put the module.
     * Insert module at this index.
     */
    pj_array_insert(dlg->usage, sizeof(dlg->usage[0]), dlg->usage_cnt,
		    index, &mod);
    
    /* Set module data. */
    dlg->mod_data[mod->id] = mod_data;

    /* Increment count. */
    ++dlg->usage_cnt;

    pj_mutex_unlock(dlg->mutex);

    return PJ_SUCCESS;
}


/*
 * Attach module specific data to the dialog. Application can also set 
 * the value directly by accessing dlg->mod_data[module_id].
 */
PJ_DEF(pj_status_t) pjsip_dlg_set_mod_data( pjsip_dialog *dlg,
					    int mod_id,
					    void *data )
{
    PJ_ASSERT_RETURN(dlg, PJ_EINVAL);
    PJ_ASSERT_RETURN(mod_id >= 0 && mod_id < PJSIP_MAX_MODULE,
		     PJ_EINVAL);
    dlg->mod_data[mod_id] = data;
    return PJ_SUCCESS;
}

/**
 * Get module specific data previously attached to the dialog. Application
 * can also get value directly by accessing dlg->mod_data[module_id].
 */
PJ_DEF(void*) pjsip_dlg_get_mod_data( pjsip_dialog *dlg,
				      int mod_id)
{
    PJ_ASSERT_RETURN(dlg, NULL);
    PJ_ASSERT_RETURN(mod_id >= 0 && mod_id < PJSIP_MAX_MODULE,
		     NULL);
    return dlg->mod_data[mod_id];
}


/*
 * Create a new request within dialog (i.e. after the dialog session has been
 * established). The construction of such requests follows the rule in 
 * RFC3261 section 12.2.1.
 */
static pj_status_t dlg_create_request_throw( pjsip_dialog *dlg,
					     const pjsip_method *method,
					     int cseq,
					     pjsip_tx_data **p_tdata )
{
    pjsip_tx_data *tdata;
    pjsip_contact_hdr *contact;
    pjsip_route_hdr *route, *end_list;
    pj_status_t status;

    /* Contact Header field.
     * Contact can only be present in requests that establish dialog (in the 
     * core SIP spec, only INVITE).
     */
    if (pjsip_method_creates_dialog(method))
	contact = dlg->local.contact;
    else
	contact = NULL;

    /*
     * Create the request by cloning from the headers in the
     * dialog.
     */
    status = pjsip_endpt_create_request_from_hdr(dlg->endpt,
						 method,
						 dlg->target,
						 dlg->local.info,
						 dlg->remote.info,
						 contact,
						 dlg->call_id,
						 cseq,
						 NULL,
						 &tdata);
    if (status != PJ_SUCCESS)
	return status;

    /* Just copy dialog route-set to Route header. 
     * The transaction will do the processing as specified in Section 12.2.1
     * of RFC 3261 in function tsx_process_route() in sip_transaction.c.
     */
    route = dlg->route_set.next;
    end_list = &dlg->route_set;
    for (; route != end_list; route = route->next ) {
	pjsip_route_hdr *r;
	r = pjsip_hdr_shallow_clone( tdata->pool, route );
	pjsip_routing_hdr_set_route(r);
	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)r);
    }

    /* Copy authorization headers, if request is not ACK or CANCEL. */
    if (method->id != PJSIP_ACK_METHOD && method->id != PJSIP_CANCEL_METHOD) {
	status = pjsip_auth_clt_init_req( &dlg->auth_sess, tdata );
	if (status != PJ_SUCCESS)
	    return status;
    }

    /* Done. */
    *p_tdata = tdata;

    return PJ_SUCCESS;
}



/*
 * Create outgoing request.
 */
PJ_DEF(pj_status_t) pjsip_dlg_create_request( pjsip_dialog *dlg,
					      const pjsip_method *method,
					      int cseq,
					      pjsip_tx_data **p_tdata)
{
    pj_status_t status;
    pjsip_tx_data *tdata = NULL;
    PJ_USE_EXCEPTION;

    PJ_ASSERT_RETURN(dlg && method && p_tdata, PJ_EINVAL);

    /* Lock dialog. */
    pj_mutex_lock(dlg->mutex);

    /* Use outgoing CSeq and increment it by one. */
    if (cseq <= 0)
	cseq = dlg->local.cseq + 1;

    /* Keep compiler happy */
    status = PJ_EBUG;

    /* Create the request. */
    PJ_TRY {
	status = dlg_create_request_throw(dlg, method, cseq, &tdata);
    }
    PJ_CATCH_ANY {
	status = PJ_ENOMEM;
    }
    PJ_END;

    /* Failed! Delete transmit data. */
    if (status != PJ_SUCCESS && tdata) {
	pjsip_tx_data_dec_ref( tdata );
	tdata = NULL;
    }

    /* Unlock dialog. */
    pj_mutex_unlock(dlg->mutex);

    *p_tdata = tdata;

    return status;
}


/*
 * Send request statefully, and update dialog'c CSeq.
 */
PJ_DEF(pj_status_t) pjsip_dlg_send_request( pjsip_dialog *dlg,
					    pjsip_tx_data *tdata,
					    pjsip_transaction **p_tsx )
{
    pjsip_transaction *tsx;
    pjsip_msg *msg = tdata->msg;
    pj_status_t status;

    /* Check arguments. */
    PJ_ASSERT_RETURN(dlg && tdata && tdata->msg, PJ_EINVAL);
    PJ_ASSERT_RETURN(tdata->msg->type == PJSIP_REQUEST_MSG,
		     PJSIP_ENOTREQUESTMSG);

    PJ_LOG(5,(dlg->obj_name, "Sending %s",
	      pjsip_tx_data_get_info(tdata)));

    /* Lock and increment session */
    pjsip_dlg_inc_lock(dlg);

    /* Update dialog's CSeq and message's CSeq if request is not
     * ACK nor CANCEL.
     */
    if (msg->line.req.method.id != PJSIP_CANCEL_METHOD &&
	msg->line.req.method.id != PJSIP_ACK_METHOD) 
    {
	pjsip_cseq_hdr *ch;
	
	ch = PJSIP_MSG_CSEQ_HDR(msg);
	PJ_ASSERT_RETURN(ch!=NULL, PJ_EBUG);

	ch->cseq = dlg->local.cseq++;

	/* Force the whole message to be re-printed. */
	pjsip_tx_data_invalidate_msg( tdata );
    }

    /* Create a new transaction if method is not ACK.
     * The transaction user is the user agent module.
     */
    if (msg->line.req.method.id != PJSIP_ACK_METHOD) {
	status = pjsip_tsx_create_uac(dlg->ua, tdata, &tsx);
	if (status != PJ_SUCCESS)
	    goto on_error;

	/* Attach this dialog to the transaction, so that user agent
	 * will dispatch events to this dialog.
	 */
	tsx->mod_data[dlg->ua->id] = dlg;

	/* Increment transaction counter. */
	++dlg->tsx_count;

	/* Send the message. */
	status = pjsip_tsx_send_msg(tsx, tdata);
	if (status != PJ_SUCCESS) {
	    pjsip_tsx_terminate(tsx, tsx->status_code);
	    goto on_error;
	}

	if (p_tsx)
	    *p_tsx = tsx;

    } else {
	status = pjsip_endpt_send_request_stateless(dlg->endpt, tdata, 
						    NULL, NULL);
	if (status != PJ_SUCCESS)
	    goto on_error;

	if (p_tsx)
	    *p_tsx = NULL;
    }

    /* Unlock dialog, may destroy dialog. */
    pjsip_dlg_dec_lock(dlg);

    return PJ_SUCCESS;

on_error:
    /* Unlock dialog, may destroy dialog. */
    pjsip_dlg_dec_lock(dlg);
   
    /* Whatever happen delete the message. */
    pjsip_tx_data_dec_ref( tdata );

    if (p_tsx)
	*p_tsx = NULL;
    return status;
}

/* Add standard headers for certain types of response */
static void dlg_beautify_response(pjsip_dialog *dlg,
				  int st_code,
				  pjsip_tx_data *tdata)
{
    pjsip_cseq_hdr *cseq;
    int st_class;
    const pjsip_hdr *c_hdr;
    pjsip_hdr *hdr;

    cseq = PJSIP_MSG_CSEQ_HDR(tdata->msg);
    pj_assert(cseq != NULL);

    st_class = st_code / 100;

    /* Contact, Allow, Supported header. */
    if (pjsip_method_creates_dialog(&cseq->method)) {
	/* Add Contact header for 1xx, 2xx, 3xx and 485 response. */
	if (st_class==2 || st_class==3 || (st_class==1 && st_code != 100) ||
	    st_code==485) 
	{
	    /* Add contact header only if one is not present. */
	    if (pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CONTACT, NULL) == 0) {
		hdr = pjsip_hdr_clone(tdata->pool, dlg->local.contact);
		pjsip_msg_add_hdr(tdata->msg, hdr);
	    }
	}

	/* Add Allow header in 2xx and 405 response. */
	if ((st_class==2 || st_code==405) &&
	    pjsip_msg_find_hdr(tdata->msg, PJSIP_H_ALLOW, NULL)==NULL) 
	{
	    c_hdr = pjsip_endpt_get_capability(dlg->endpt,
					       PJSIP_H_ALLOW, NULL);
	    if (c_hdr) {
		hdr = pjsip_hdr_clone(tdata->pool, c_hdr);
		pjsip_msg_add_hdr(tdata->msg, hdr);
	    }
	}

	/* Add Supported header in 2xx response. */
	if (st_class==2 && 
	    pjsip_msg_find_hdr(tdata->msg, PJSIP_H_SUPPORTED, NULL)==NULL) 
	{
	    c_hdr = pjsip_endpt_get_capability(dlg->endpt,
					       PJSIP_H_SUPPORTED, NULL);
	    if (c_hdr) {
		hdr = pjsip_hdr_clone(tdata->pool, c_hdr);
		pjsip_msg_add_hdr(tdata->msg, hdr);
	    }
	}

    }

    /* Add To tag in all responses except 100 */
    if (st_code != 100) {
	pjsip_to_hdr *to;

	to = PJSIP_MSG_TO_HDR(tdata->msg);
	pj_assert(to != NULL);

	to->tag = dlg->local.info->tag;

	if (dlg->state == PJSIP_DIALOG_STATE_NULL)
	    dlg->state = PJSIP_DIALOG_STATE_ESTABLISHED;
    }
}


/*
 * Create response.
 */
PJ_DEF(pj_status_t) pjsip_dlg_create_response(	pjsip_dialog *dlg,
						pjsip_rx_data *rdata,
						int st_code,
						const pj_str_t *st_text,
						pjsip_tx_data **p_tdata)
{
    pj_status_t status;
    pjsip_tx_data *tdata;

    /* Create generic response.
     * This will initialize response's Via, To, From, Call-ID, CSeq
     * and Record-Route headers from the request.
     */
    status = pjsip_endpt_create_response(dlg->endpt,
					 rdata, st_code, st_text, &tdata);
    if (status != PJ_SUCCESS)
	return status;

    /* Lock the dialog. */
    pj_mutex_lock(dlg->mutex);

    dlg_beautify_response(dlg, st_code, tdata);

    /* Unlock the dialog. */
    pj_mutex_unlock(dlg->mutex);

    /* Done. */
    *p_tdata = tdata;
    return PJ_SUCCESS;
}

/*
 * Modify response.
 */
PJ_DEF(pj_status_t) pjsip_dlg_modify_response(	pjsip_dialog *dlg,
						pjsip_tx_data *tdata,
						int st_code,
						const pj_str_t *st_text)
{
    
    PJ_ASSERT_RETURN(dlg && tdata && tdata->msg, PJ_EINVAL);
    PJ_ASSERT_RETURN(tdata->msg->type == PJSIP_RESPONSE_MSG,
		     PJSIP_ENOTRESPONSEMSG);
    PJ_ASSERT_RETURN(st_code >= 100 && st_code <= 699, PJ_EINVAL);

    /* Lock and increment session */
    pjsip_dlg_inc_lock(dlg);

    /* Replace status code and reason */
    tdata->msg->line.status.code = st_code;
    if (st_text) {
	pj_strdup(tdata->pool, &tdata->msg->line.status.reason, st_text);
    } else {
	tdata->msg->line.status.reason = *pjsip_get_status_text(st_code);
    }

    dlg_beautify_response(dlg, st_code, tdata);


    /* Must add reference counter, since tsx_send_msg() will decrement it */
    pjsip_tx_data_add_ref(tdata);

    /* Force to re-print message. */
    pjsip_tx_data_invalidate_msg(tdata);

    /* Unlock dialog and dec session, may destroy dialog. */
    pjsip_dlg_dec_lock(dlg);

    return PJ_SUCCESS;
}

/*
 * Send response statefully.
 */
PJ_DEF(pj_status_t) pjsip_dlg_send_response( pjsip_dialog *dlg,
					     pjsip_transaction *tsx,
					     pjsip_tx_data *tdata)
{
    pj_status_t status;

    /* Sanity check. */
    PJ_ASSERT_RETURN(dlg && tsx && tdata && tdata->msg, PJ_EINVAL);
    PJ_ASSERT_RETURN(tdata->msg->type == PJSIP_RESPONSE_MSG,
		     PJSIP_ENOTRESPONSEMSG);

    /* The transaction must belong to this dialog.  */
    PJ_ASSERT_RETURN(tsx->mod_data[dlg->ua->id] == dlg, PJ_EINVALIDOP);

    PJ_LOG(5,(dlg->obj_name, "Sending %s",
	      pjsip_tx_data_get_info(tdata)));

    /* Check that transaction method and cseq match the response. 
     * This operation is sloooww (search CSeq header twice), that's why
     * we only do it in debug mode.
     */
#if defined(PJ_DEBUG) && PJ_DEBUG!=0
    PJ_ASSERT_RETURN( PJSIP_MSG_CSEQ_HDR(tdata->msg)->cseq == tsx->cseq &&
		      pjsip_method_cmp(&PJSIP_MSG_CSEQ_HDR(tdata->msg)->method, 
				       &tsx->method)==0,
		      PJ_EINVALIDOP);
#endif

    /* Must acquire dialog first, to prevent deadlock */
    pjsip_dlg_inc_lock(dlg);

    status = pjsip_tsx_send_msg(tsx, tdata);

    pjsip_dlg_dec_lock(dlg);

    return status;
}


/*
 * Combo function to create and send response statefully.
 */
PJ_DEF(pj_status_t) pjsip_dlg_respond(  pjsip_dialog *dlg,
					pjsip_rx_data *rdata,
					int st_code,
					const pj_str_t *st_text,
					const pjsip_hdr *hdr_list,
					const pjsip_msg_body *body )
{
    pj_status_t status;
    pjsip_tx_data *tdata;

    /* Sanity check. */
    PJ_ASSERT_RETURN(dlg && rdata && rdata->msg_info.msg, PJ_EINVAL);
    PJ_ASSERT_RETURN(rdata->msg_info.msg->type == PJSIP_REQUEST_MSG,
		     PJSIP_ENOTREQUESTMSG);

    /* The transaction must belong to this dialog.  */
    PJ_ASSERT_RETURN(pjsip_rdata_get_tsx(rdata) &&
		     pjsip_rdata_get_tsx(rdata)->mod_data[dlg->ua->id] == dlg,
		     PJ_EINVALIDOP);

    /* Create the response. */
    status = pjsip_dlg_create_response(dlg, rdata, st_code, st_text, &tdata);
    if (status != PJ_SUCCESS)
	return status;

    /* Add additional header, if any */
    if (hdr_list) {
	const pjsip_hdr *hdr;

	hdr = hdr_list->next;
	while (hdr != hdr_list) {
	    pjsip_msg_add_hdr(tdata->msg,
			      pjsip_hdr_clone(tdata->pool, hdr));
	    hdr = hdr->next;
	}
    }

    /* Add the message body, if any. */
    if (body) {
	tdata->msg->body = pjsip_msg_body_clone( tdata->pool, body);
    }

    /* Send the response. */
    return pjsip_dlg_send_response(dlg, pjsip_rdata_get_tsx(rdata), tdata);
}


/* This function is called by user agent upon receiving incoming response
 * message.
 */
void pjsip_dlg_on_rx_request( pjsip_dialog *dlg, pjsip_rx_data *rdata )
{
    pj_status_t status;
    pjsip_transaction *tsx = NULL;
    unsigned i;

    PJ_LOG(5,(dlg->obj_name, "Received %s",
	      pjsip_rx_data_get_info(rdata)));

    /* Lock dialog and increment session. */
    pjsip_dlg_inc_lock(dlg);

    /* Check CSeq */
    if (rdata->msg_info.cseq->cseq <= dlg->remote.cseq &&
	rdata->msg_info.msg->line.req.method.id != PJSIP_ACK_METHOD &&
	rdata->msg_info.msg->line.req.method.id != PJSIP_CANCEL_METHOD) 
    {
	/* Invalid CSeq.
	 * Respond statelessly with 500 (Internal Server Error)
	 */
	pj_str_t warn_text;

	/* Unlock dialog and dec session, may destroy dialog. */
	pjsip_dlg_dec_lock(dlg);

	pj_assert(pjsip_rdata_get_tsx(rdata) == NULL);
	warn_text = pj_str("Invalid CSeq");
	pjsip_endpt_respond_stateless(dlg->endpt,
				      rdata, 500, &warn_text, NULL, NULL);
	return;
    }

    /* Update CSeq. */
    dlg->remote.cseq = rdata->msg_info.cseq->cseq;

    /* Update To tag if necessary.
     * This only happens if UAS sends a new request before answering
     * our request (e.g. UAS sends NOTIFY before answering our
     * SUBSCRIBE request).
     */
    if (dlg->remote.info->tag.slen == 0) {
	pj_strdup(dlg->pool, &dlg->remote.info->tag,
		  &rdata->msg_info.from->tag);
    }

    /* Create UAS transaction for this request. */
    if (pjsip_rdata_get_tsx(rdata) == NULL && 
	rdata->msg_info.msg->line.req.method.id != PJSIP_ACK_METHOD) 
    {
	status = pjsip_tsx_create_uas(dlg->ua, rdata, &tsx);
	PJ_ASSERT_ON_FAIL(status==PJ_SUCCESS,{goto on_return;});

	/* Put this dialog in the transaction data. */
	tsx->mod_data[dlg->ua->id] = dlg;

	/* Add transaction count. */
	++dlg->tsx_count;
    }

    /* Report the request to dialog usages. */
    for (i=0; i<dlg->usage_cnt; ++i) {
	pj_bool_t processed;

	if (!dlg->usage[i]->on_rx_request)
	    continue;

	processed = (*dlg->usage[i]->on_rx_request)(rdata);

	if (processed)
	    break;
    }

    /* Feed the first request to the transaction. */
    if (tsx)
	pjsip_tsx_recv_msg(tsx, rdata);

on_return:
    /* Unlock dialog and dec session, may destroy dialog. */
    pjsip_dlg_dec_lock(dlg);
}

/* This function is called by user agent upon receiving incoming response
 * message.
 */
void pjsip_dlg_on_rx_response( pjsip_dialog *dlg, pjsip_rx_data *rdata )
{
    unsigned i;
    int res_code;

    PJ_LOG(5,(dlg->obj_name, "Received %s",
	      pjsip_rx_data_get_info(rdata)));

    /* Lock the dialog and inc session. */
    pjsip_dlg_inc_lock(dlg);

    /* Check that rdata already has dialog in mod_data. */
    pj_assert(pjsip_rdata_get_dlg(rdata) == dlg);

    /* Keep the response's status code */
    res_code = rdata->msg_info.msg->line.status.code;

    /* When we receive response that establishes dialog, update To tag, 
     * route set and dialog target.
     */
    if (dlg->state == PJSIP_DIALOG_STATE_NULL && 
	pjsip_method_creates_dialog(&rdata->msg_info.cseq->method) &&
	(res_code > 100 && res_code < 300) &&
	rdata->msg_info.to->tag.slen)
    {
	pjsip_hdr *hdr, *end_hdr;
	pjsip_contact_hdr *contact;

	/* Update To tag. */
	pj_strdup(dlg->pool, &dlg->remote.info->tag, &rdata->msg_info.to->tag);
	/* No need to update remote's tag_hval since its never used. */


	/* RFC 3271 Section 12.1.2:
	 * The route set MUST be set to the list of URIs in the Record-Route
	 * header field from the response, taken in reverse order and 
	 * preserving all URI parameters. If no Record-Route header field 
	 * is present in the response, the route set MUST be set to the 
	 * empty set. This route set, even if empty, overrides any pre-existing
	 * route set for future requests in this dialog.
	 */
	pj_list_init(&dlg->route_set);

	end_hdr = &rdata->msg_info.msg->hdr;
	for (hdr=rdata->msg_info.msg->hdr.prev; hdr!=end_hdr; hdr=hdr->prev) {
	    if (hdr->type == PJSIP_H_RECORD_ROUTE) {
		pjsip_route_hdr *r;
		r = pjsip_hdr_clone(dlg->pool, hdr);
		pjsip_routing_hdr_set_route(r);
		pj_list_push_back(&dlg->route_set, r);
	    }
	}

	/* The remote target MUST be set to the URI from the Contact header 
	 * field of the response.
	 */
	contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, 
				     NULL);
	if (contact) {
	    dlg->remote.contact = pjsip_hdr_clone(dlg->pool, contact);
	    dlg->target = dlg->remote.contact->uri;
	}

	dlg->state = PJSIP_DIALOG_STATE_ESTABLISHED;
    }

    /* Update remote target (again) when receiving 2xx response messages
     * that's defined as target refresh. 
     */
    if (pjsip_method_creates_dialog(&rdata->msg_info.cseq->method) &&
	res_code/100 == 2)
    {
	pjsip_contact_hdr *contact;

	contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, 
				     NULL);
	if (contact) {
	    dlg->remote.contact = pjsip_hdr_clone(dlg->pool, contact);
	    dlg->target = dlg->remote.contact->uri;
	}
    }


    /* Pass to dialog usages. */
    for (i=0; i<dlg->usage_cnt; ++i) {
	pj_bool_t processed;

	if (!dlg->usage[i]->on_rx_response)
	    continue;

	processed = (*dlg->usage[i]->on_rx_response)(rdata);

	if (processed)
	    break;
    }

    if (i==dlg->usage_cnt) {
	PJ_LOG(4,(dlg->obj_name, "%s is unhandled by dialog usages",
		  pjsip_rx_data_get_info(rdata)));
    }

    /* Unlock dialog and dec session, may destroy dialog. */
    pjsip_dlg_dec_lock(dlg);
}

/* This function is called by user agent upon receiving transaction
 * state notification.
 */
void pjsip_dlg_on_tsx_state( pjsip_dialog *dlg,
			     pjsip_transaction *tsx,
			     pjsip_event *e )
{
    unsigned i;

    PJ_LOG(5,(dlg->obj_name, "Transaction %s state changed to %s",
	      tsx->obj_name, pjsip_tsx_state_str(tsx->state)));

    /* Lock the dialog and increment session. */
    pjsip_dlg_inc_lock(dlg);

    /* Pass to dialog usages. */
    for (i=0; i<dlg->usage_cnt; ++i) {

	if (!dlg->usage[i]->on_tsx_state)
	    continue;

	(*dlg->usage[i]->on_tsx_state)(tsx, e);
    }


    if (tsx->state == PJSIP_TSX_STATE_TERMINATED) {
	--dlg->tsx_count;
	tsx->mod_data[dlg->ua->id] = NULL;
    }

    /* Unlock dialog and dec session, may destroy dialog. */
    pjsip_dlg_dec_lock(dlg);
}

