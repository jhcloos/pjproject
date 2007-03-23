/* $Id$ */
/* 
 * Copyright (C) 2003-2005 Benny Prijono <benny@prijono.org>
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
#include <pjnath/ice.h>
#include <pjnath/errno.h>
#include <pj/addr_resolv.h>
#include <pj/array.h>
#include <pj/assert.h>
#include <pj/guid.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/rand.h>
#include <pj/string.h>


/* String names for candidate types */
static const char *cand_type_names[] =
{
    "Host",
    "Server Reflexive",
    "Peer Reflexive",
    "Relayed"

};

/* String names for pj_ice_check_state */
static const char *check_state_name[] = 
{
    "Frozen",
    "Waiting",
    "In Progress",
    "Succeeded",
    "Failed"
};

static const char *clist_state_name[] =
{
    "Idle",
    "Running",
    "Completed"
};

#define CHECK_NAME_LEN	    128
#define LOG(expr)	    PJ_LOG(4,expr)
#define GET_LCAND_ID(cand)  (cand - ice->lcand)
#define GET_CHECK_ID(chk)   (chk - ice->clist.checks)


typedef struct stun_data
{
    pj_ice	*ice;
    unsigned	 lcand_id;
    pj_ice_cand	*lcand;
} stun_data;

typedef struct timer_data
{
    pj_ice		*ice;
    pj_ice_checklist	*clist;
} timer_data;



static void destroy_ice(pj_ice *ice,
			pj_status_t reason);
static pj_status_t start_periodic_check(pj_timer_heap_t *th, 
					pj_timer_entry *te);
static pj_status_t on_stun_send_msg(pj_stun_session *sess,
				    const void *pkt,
				    pj_size_t pkt_size,
				    const pj_sockaddr_t *dst_addr,
				    unsigned addr_len);
static pj_status_t on_stun_rx_request(pj_stun_session *sess,
				      const pj_uint8_t *pkt,
				      unsigned pkt_len,
				      const pj_stun_msg *msg,
				      const pj_sockaddr_t *src_addr,
				      unsigned src_addr_len);
static void on_stun_request_complete(pj_stun_session *stun_sess,
				     pj_status_t status,
				     pj_stun_tx_data *tdata,
				     const pj_stun_msg *response);
static pj_status_t on_stun_rx_indication(pj_stun_session *sess,
					 const pj_uint8_t *pkt,
					 unsigned pkt_len,
					 const pj_stun_msg *msg,
					 const pj_sockaddr_t *src_addr,
					 unsigned src_addr_len);

static pj_status_t stun_auth_get_auth(void *user_data,
				      pj_pool_t *pool,
				      pj_str_t *realm,
				      pj_str_t *nonce);
static pj_status_t stun_auth_get_cred(const pj_stun_msg *msg,
				      void *user_data,
				      pj_pool_t *pool,
				      pj_str_t *realm,
				      pj_str_t *username,
				      pj_str_t *nonce,
				      int *data_type,
				      pj_str_t *data);
static pj_status_t stun_auth_get_password(const pj_stun_msg *msg,
					  void *user_data, 
					  const pj_str_t *realm,
					  const pj_str_t *username,
					  pj_pool_t *pool,
					  int *data_type,
					  pj_str_t *data);
static pj_bool_t stun_auth_verify_nonce(const pj_stun_msg *msg,
					void *user_data,
					const pj_str_t *realm,
					const pj_str_t *username,
					const pj_str_t *nonce);


/*
 * Create ICE stream session.
 */
PJ_DEF(pj_status_t) pj_ice_create(pj_stun_config *stun_cfg,
				  const char *name,
				  pj_ice_role role,
				  const pj_ice_cb *cb,
				  const pj_str_t *local_ufrag,
				  const pj_str_t *local_passwd,
				  pj_ice **p_ice)
{
    pj_pool_t *pool;
    pj_ice *ice;
    char tmp[32];
    pj_str_t s;
    unsigned i;
    pj_status_t status;

    PJ_ASSERT_RETURN(stun_cfg && cb && p_ice, PJ_EINVAL);

    if (name == NULL)
	name = "ice%p";

    pool = pj_pool_create(stun_cfg->pf, name, 4000, 4000, NULL);
    ice = PJ_POOL_ZALLOC_T(pool, pj_ice);
    ice->pool = pool;
    ice->role = role;

    pj_ansi_snprintf(ice->obj_name, sizeof(ice->obj_name),
		     name, ice);

    status = pj_mutex_create_recursive(pool, ice->obj_name, 
				       &ice->mutex);
    if (status != PJ_SUCCESS) {
	destroy_ice(ice, status);
	return status;
    }

    pj_memcpy(&ice->cb, cb, sizeof(*cb));
    pj_memcpy(&ice->stun_cfg, stun_cfg, sizeof(*stun_cfg));

    for (i=0; i<PJ_ICE_MAX_COMP; ++i) {
	ice->comp[i].nominated_check_id = -1;
    }

    if (local_ufrag == NULL) {
	pj_ansi_snprintf(tmp, sizeof(tmp), "%x", pj_rand());
	s = pj_str(tmp);
	local_ufrag = &s;
    }
    pj_strdup(ice->pool, &ice->rx_ufrag, local_ufrag);

    if (local_passwd == NULL) {
	pj_ansi_snprintf(tmp, sizeof(tmp), "%x", pj_rand());
	s = pj_str(tmp);
	local_passwd = &s;
    }
    pj_strdup(ice->pool, &ice->rx_pass, local_passwd);


    /* Done */
    *p_ice = ice;

    LOG((ice->obj_name, "ICE stream session created, role is %s agent",
	(ice->role==PJ_ICE_ROLE_CONTROLLING ? "controlling" : "controlled")));

    return PJ_SUCCESS;
}


/*
 * Destroy
 */
static void destroy_ice(pj_ice *ice,
			pj_status_t reason)
{
    unsigned i;

    if (reason == PJ_SUCCESS) {
	LOG((ice->obj_name, "Destroying ICE session"));
    }

    for (i=0; i<ice->comp_cnt; ++i) {
	/* Nothing to do */
    }

    for (i=0; i<ice->lcand_cnt; ++i) {
	if (ice->lcand[i].stun_sess) {
	    pj_stun_session_destroy(ice->lcand[i].stun_sess);
	    ice->lcand[i].stun_sess = NULL;
	}
    }

    if (ice->clist.timer.id) {
	pj_timer_heap_cancel(ice->stun_cfg.timer_heap, &ice->clist.timer);
	ice->clist.timer.id = PJ_FALSE;
    }

    if (ice->mutex) {
	pj_mutex_destroy(ice->mutex);
	ice->mutex = NULL;
    }

    if (ice->pool) {
	pj_pool_t *pool = ice->pool;
	ice->pool = NULL;
	pj_pool_release(pool);
    }
}


PJ_DEF(pj_status_t) pj_ice_destroy(pj_ice *ice)
{
    destroy_ice(ice, PJ_SUCCESS);
    return PJ_SUCCESS;
}


/* Find component by ID */
static pj_ice_comp *find_comp(const pj_ice *ice, unsigned comp_id)
{
    unsigned i;
    for (i=0; i<ice->comp_cnt; ++i) {
	if (ice->comp[i].comp_id == comp_id)
	    return (pj_ice_comp *) &ice->comp[i];
    }

    return NULL;
}


/* Add a new component */
PJ_DEF(pj_status_t) pj_ice_add_comp(pj_ice *ice, unsigned comp_id)
{
    pj_ice_comp *comp;

    PJ_ASSERT_RETURN(ice && comp_id, PJ_EINVAL);
    PJ_ASSERT_RETURN(ice->comp_cnt < PJ_ARRAY_SIZE(ice->comp), PJ_ETOOMANY);
    PJ_ASSERT_RETURN(comp_id==ice->comp_cnt+1, PJ_EICEINCOMPID);
    PJ_ASSERT_RETURN(find_comp(ice, comp_id) == NULL, PJ_EEXISTS);

    pj_mutex_lock(ice->mutex);

    comp = &ice->comp[ice->comp_cnt];
    comp->comp_id = comp_id;
    comp->nominated_check_id = -1;

    /* Done */
    ice->comp_cnt++;
    pj_mutex_unlock(ice->mutex);

    return PJ_SUCCESS;
}

static pj_status_t stun_auth_get_auth(void *user_data,
				      pj_pool_t *pool,
				      pj_str_t *realm,
				      pj_str_t *nonce)
{
    PJ_UNUSED_ARG(user_data);
    PJ_UNUSED_ARG(pool);

    realm->slen = 0;
    nonce->slen = 0;

    return PJ_SUCCESS;
}


/* Get credential to be sent with outgoing message */
static pj_status_t stun_auth_get_cred(const pj_stun_msg *msg,
				      void *user_data,
				      pj_pool_t *pool,
				      pj_str_t *realm,
				      pj_str_t *username,
				      pj_str_t *nonce,
				      int *data_type,
				      pj_str_t *data)
{
    pj_stun_session *sess = (pj_stun_session *)user_data;
    stun_data *sd = (stun_data*) pj_stun_session_get_user_data(sess);
    pj_ice *ice = sd->ice;

    PJ_UNUSED_ARG(pool);
    realm->slen = nonce->slen = 0;

    if (PJ_STUN_IS_RESPONSE(msg->hdr.type) ||
	PJ_STUN_IS_ERROR_RESPONSE(msg->hdr.type))
    {
	/* Outgoing responses need to have the same credential as
	 * incoming requests.
	 */
	*username = ice->rx_uname;
	*data_type = 0;
	*data = ice->rx_pass;
    }
    else {
	*username = ice->tx_uname;
	*data_type = 0;
	*data = ice->tx_pass;
    }

    return PJ_SUCCESS;
}

/* Get password to be used to authenticate incoming message */
static pj_status_t stun_auth_get_password(const pj_stun_msg *msg,
					  void *user_data, 
					  const pj_str_t *realm,
					  const pj_str_t *username,
					  pj_pool_t *pool,
					  int *data_type,
					  pj_str_t *data)
{
    pj_stun_session *sess = (pj_stun_session *)user_data;
    stun_data *sd = (stun_data*) pj_stun_session_get_user_data(sess);
    pj_ice *ice = sd->ice;

    PJ_UNUSED_ARG(realm);
    PJ_UNUSED_ARG(pool);

    if (PJ_STUN_IS_RESPONSE(msg->hdr.type) ||
	PJ_STUN_IS_ERROR_RESPONSE(msg->hdr.type))
    {
	/* Incoming response is authenticated with TX credential */
	/* Verify username */
	if (pj_strcmp(username, &ice->tx_uname) != 0)
	    return -1;
	*data_type = 0;
	*data = ice->tx_pass;

    } else {
	/* Incoming request is authenticated with RX credential */
	/* The agent MUST accept a credential if the username consists
	 * of two values separated by a colon, where the first value is
	 * equal to the username fragment generated by the agent in an offer
	 * or answer for a session in-progress, and the MESSAGE-INTEGRITY 
	 * is the output of a hash of the password and the STUN packet's 
	 * contents. 
	 */
	PJ_TODO(CHECK_USERNAME_FOR_INCOMING_STUN_REQUEST);
	*data_type = 0;
	*data = ice->rx_pass;

    }

    return PJ_SUCCESS;
}


static pj_bool_t stun_auth_verify_nonce(const pj_stun_msg *msg,
					void *user_data,
					const pj_str_t *realm,
					const pj_str_t *username,
					const pj_str_t *nonce)
{
    /* We don't use NONCE */
    PJ_UNUSED_ARG(msg);
    PJ_UNUSED_ARG(user_data);
    PJ_UNUSED_ARG(realm);
    PJ_UNUSED_ARG(username);
    PJ_UNUSED_ARG(nonce);
    return PJ_TRUE;
}


static pj_uint32_t CALC_CAND_PRIO(pj_ice_cand_type type,
				  pj_uint32_t local_pref,
				  pj_uint32_t comp_id)
{
    static pj_uint32_t type_pref[] =
    {
	PJ_ICE_HOST_PREF,
	PJ_ICE_SRFLX_PREF,
	PJ_ICE_PRFLX_PREF,
	PJ_ICE_RELAYED_PREF
    };

    return ((1 << 24) * type_pref[type]) + 
	   ((1 << 8) * local_pref) +
	   (256 - comp_id);
}


/*
 * Add ICE candidate
 */
PJ_DEF(pj_status_t) pj_ice_add_cand(pj_ice *ice,
				    unsigned comp_id,
				    pj_ice_cand_type type,
				    pj_uint16_t local_pref,
				    const pj_str_t *foundation,
				    const pj_sockaddr_t *addr,
				    const pj_sockaddr_t *base_addr,
				    const pj_sockaddr_t *srv_addr,
				    int addr_len,
				    unsigned *p_cand_id)
{
    pj_ice_cand *lcand;
    pj_stun_session_cb sess_cb;
    pj_stun_auth_cred auth_cred;
    stun_data *sd;
    pj_status_t status = PJ_SUCCESS;
    char tmp[128];

    PJ_ASSERT_RETURN(ice && comp_id && local_pref &&
		     foundation && addr && base_addr && addr_len,
		     PJ_EINVAL);

    pj_mutex_lock(ice->mutex);

    if (ice->lcand_cnt >= PJ_ARRAY_SIZE(ice->lcand)) {
	status = PJ_ETOOMANY;
	goto on_error;
    }

    lcand = &ice->lcand[ice->lcand_cnt];
    lcand->comp_id = comp_id;
    lcand->type = type;
    pj_strdup(ice->pool, &lcand->foundation, foundation);
    lcand->prio = CALC_CAND_PRIO(type, local_pref, lcand->comp_id);
    pj_memcpy(&lcand->addr, addr, addr_len);
    pj_memcpy(&lcand->base_addr, base_addr, addr_len);
    if (srv_addr)
	pj_memcpy(&lcand->srv_addr, srv_addr, addr_len);
    else
	pj_bzero(&lcand->srv_addr, sizeof(lcand->srv_addr));

    /* Init STUN callbacks */
    pj_bzero(&sess_cb, sizeof(sess_cb));
    sess_cb.on_request_complete = &on_stun_request_complete;
    sess_cb.on_rx_indication = &on_stun_rx_indication;
    sess_cb.on_rx_request = &on_stun_rx_request;
    sess_cb.on_send_msg = &on_stun_send_msg;

    /* Create STUN session for this candidate */
    status = pj_stun_session_create(&ice->stun_cfg, ice->obj_name, 
			            &sess_cb, PJ_FALSE,
				    &lcand->stun_sess);
    if (status != PJ_SUCCESS) {
	pj_mutex_unlock(ice->mutex);
	return status;
    }

    /* Associate data with this STUN session */
    sd = PJ_POOL_ZALLOC_T(ice->pool, struct stun_data);
    sd->ice = ice;
    sd->lcand_id = GET_LCAND_ID(lcand);
    sd->lcand = lcand;
    pj_stun_session_set_user_data(lcand->stun_sess, sd);

    /* Init STUN authentication credential */
    pj_bzero(&auth_cred, sizeof(auth_cred));
    auth_cred.type = PJ_STUN_AUTH_CRED_DYNAMIC;
    auth_cred.data.dyn_cred.get_auth = &stun_auth_get_auth;
    auth_cred.data.dyn_cred.get_cred = &stun_auth_get_cred;
    auth_cred.data.dyn_cred.get_password = &stun_auth_get_password;
    auth_cred.data.dyn_cred.verify_nonce = &stun_auth_verify_nonce;
    auth_cred.data.dyn_cred.user_data = lcand->stun_sess;
    pj_stun_session_set_credential(lcand->stun_sess, &auth_cred);


    pj_ansi_strcpy(tmp, pj_inet_ntoa(lcand->addr.ipv4.sin_addr));
    LOG((ice->obj_name, 
	 "Candidate %d added: comp_id=%d, type=%s, foundation=%.*s, "
	 "addr=%s:%d, base=%s:%d, prio=0x%x (%u)",
	 ice->lcand_cnt, 
	 lcand->comp_id, 
	 cand_type_names[lcand->type],
	 (int)lcand->foundation.slen,
	 lcand->foundation.ptr,
	 tmp, 
	 (int)pj_ntohs(lcand->addr.ipv4.sin_port),
	 pj_inet_ntoa(lcand->base_addr.ipv4.sin_addr),
	 (int)pj_htons(lcand->base_addr.ipv4.sin_port),
	 lcand->prio, lcand->prio));

    if (p_cand_id)
	*p_cand_id = ice->lcand_cnt;

    ++ice->lcand_cnt;

on_error:
    pj_mutex_unlock(ice->mutex);
    return status;
}


PJ_DEF(unsigned) pj_ice_get_cand_cnt(pj_ice *ice)
{
    return ice->lcand_cnt;
}


PJ_DEF(pj_status_t) pj_ice_enum_cands(pj_ice *ice,
				      unsigned *p_count,
				      unsigned cand_ids[])
{
    unsigned i, count;

    PJ_ASSERT_RETURN(ice && p_count && *p_count && cand_ids, PJ_EINVAL);

    pj_mutex_lock(ice->mutex);

    count = (*p_count < ice->lcand_cnt) ? *p_count : ice->lcand_cnt;
    for (i=0; i<count; ++i)
	cand_ids[i] = i;

    *p_count = count;
    pj_mutex_unlock(ice->mutex);

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pj_ice_get_default_cand(pj_ice *ice,
					    unsigned comp_id,
					    int *cand_id)
{
    unsigned i;

    PJ_ASSERT_RETURN(ice && comp_id && cand_id, PJ_EINVAL);

    *cand_id = -1;

    pj_mutex_lock(ice->mutex);

    /* First find in valid list if we have nominated pair */
    for (i=0; i<ice->valid_cnt; ++i) {
	pj_ice_cand *lcand;
	
	lcand = ice->clist.checks[ice->valid_list[i]].lcand;
	if (lcand->comp_id==comp_id) {
	    *cand_id = GET_LCAND_ID(lcand);
	    pj_mutex_unlock(ice->mutex);
	    return PJ_SUCCESS;
	}
    }

    /* If there's no nominated pair, find relayed candidate */
    for (i=0; i<ice->lcand_cnt; ++i) {
	pj_ice_cand *lcand = &ice->lcand[i];
	if (lcand->comp_id==comp_id &&
	    lcand->type == PJ_ICE_CAND_TYPE_RELAYED) 
	{
	    *cand_id = GET_LCAND_ID(lcand);
	    pj_mutex_unlock(ice->mutex);
	    return PJ_SUCCESS;
	}
    }

    /* If there's no relayed candidate, find server reflexive candidate */
    for (i=0; i<ice->lcand_cnt; ++i) {
	pj_ice_cand *lcand = &ice->lcand[i];
	if (lcand->comp_id==comp_id &&
	    lcand->type == PJ_ICE_CAND_TYPE_SRFLX) 
	{
	    *cand_id = GET_LCAND_ID(lcand);
	    pj_mutex_unlock(ice->mutex);
	    return PJ_SUCCESS;
	}
    }

    /* Otherwise return host candidate */
    for (i=0; i<ice->lcand_cnt; ++i) {
	pj_ice_cand *lcand = &ice->lcand[i];
	if (lcand->comp_id==comp_id &&
	    lcand->type == PJ_ICE_CAND_TYPE_HOST) 
	{
	    *cand_id = GET_LCAND_ID(lcand);
	    pj_mutex_unlock(ice->mutex);
	    return PJ_SUCCESS;
	}
    }

    /* Still no candidate is found! :( */
    pj_mutex_unlock(ice->mutex);

    pj_assert(!"Should have a candidate by now");
    return PJ_EICENOCAND;
}


PJ_DEF(pj_status_t) pj_ice_get_cand(pj_ice *ice,
				    unsigned cand_id,
				    pj_ice_cand **p_cand)
{
    PJ_ASSERT_RETURN(ice && p_cand, PJ_EINVAL);
    PJ_ASSERT_RETURN(cand_id <= ice->lcand_cnt, PJ_EINVAL);

    *p_cand = &ice->lcand[cand_id];

    return PJ_SUCCESS;
}

#ifndef MIN
#   define MIN(a,b) (a < b ? a : b)
#endif

#ifndef MAX
#   define MAX(a,b) (a > b ? a : b)
#endif

static pj_uint64_t CALC_CHECK_PRIO(const pj_ice *ice, 
				   const pj_ice_cand *lcand,
				   const pj_ice_cand *rcand)
{
    pj_uint32_t O, A;

    if (ice->role == PJ_ICE_ROLE_CONTROLLING) {
	O = lcand->prio; 
	A = rcand->prio;
    } else {
	O = rcand->prio;
	A = lcand->prio;
    }

    return ((pj_uint64_t)1 << 32) * MIN(O, A) +
	   (pj_uint64_t)2 * MAX(O, A) + (O>A ? 1 : 0);
}

static const char *dump_check(char *buffer, unsigned bufsize,
			      const pj_ice *ice,
			      const pj_ice_check *check)
{
    const pj_ice_cand *lcand = check->lcand;
    const pj_ice_cand *rcand = check->rcand;
    char laddr[CHECK_NAME_LEN];
    int len;

    pj_ansi_strcpy(laddr, pj_inet_ntoa(lcand->addr.ipv4.sin_addr));

    if (lcand->addr.addr.sa_family == PJ_AF_INET) {
	len = pj_ansi_snprintf(buffer, bufsize,
			       "%d: %s:%d-->%s:%d",
			       GET_CHECK_ID(check),
			       laddr, (int)pj_ntohs(lcand->addr.ipv4.sin_port),
			       pj_inet_ntoa(rcand->addr.ipv4.sin_addr),
			       (int)pj_ntohs(rcand->addr.ipv4.sin_port));
    } else {
	len = pj_ansi_snprintf(buffer, bufsize, "IPv6->IPv6");
    }


    if (len < 0)
	len = 0;
    else if (len >= (int)bufsize)
	len = bufsize - 1;

    buffer[len] = '\0';
    return buffer;
}

#if PJ_LOG_MAX_LEVEL >= 4
static void dump_checklist(const char *title, const pj_ice *ice, 
			   const pj_ice_checklist *clist)
{
    unsigned i;
    char buffer[CHECK_NAME_LEN];

    LOG((ice->obj_name, "%s", title));
    for (i=0; i<clist->count; ++i) {
	const pj_ice_check *c = &clist->checks[i];
	LOG((ice->obj_name, " %s (%s, state=%s)",
	     dump_check(buffer, sizeof(buffer), ice, c),
	     (c->nominated ? "nominated" : "not nominated"), 
	     check_state_name[c->state]));
    }
}

static void dump_valid_list(const char *title, const pj_ice *ice)
{
    unsigned i;
    char buffer[CHECK_NAME_LEN];

    LOG((ice->obj_name, "%s", title));
    for (i=0; i<ice->valid_cnt; ++i) {
	const pj_ice_check *c = &ice->clist.checks[ice->valid_list[i]];
	LOG((ice->obj_name, " %s (%s, state=%s)",
	     dump_check(buffer, sizeof(buffer), ice, c),
	     (c->nominated ? "nominated" : "not nominated"), 
	     check_state_name[c->state]));
    }
}

#else
#define dump_checklist(ice, clist)
#endif

static void check_set_state(pj_ice *ice, pj_ice_check *check,
			    pj_ice_check_state st, 
			    pj_status_t err_code)
{
    char buf[CHECK_NAME_LEN];

    pj_assert(check->state < PJ_ICE_CHECK_STATE_SUCCEEDED);

    LOG((ice->obj_name, "Check %s: state changed from %s to %s",
	 dump_check(buf, sizeof(buf), ice, check),
	 check_state_name[check->state],
	 check_state_name[st]));
    check->state = st;
    check->err_code = err_code;
}

static void clist_set_state(pj_ice *ice, pj_ice_checklist *clist,
			    pj_ice_checklist_state st)
{
    if (clist->state != st) {
	LOG((ice->obj_name, "Checklist: state changed from %s to %s",
	     clist_state_name[clist->state],
	     clist_state_name[st]));
	clist->state = st;
    }
}

/* Sort checklist based on priority */
static void sort_checklist(pj_ice_checklist *clist)
{
    unsigned i;

    for (i=0; i<clist->count-1; ++i) {
	unsigned j, highest = i;
	for (j=i+1; j<clist->count; ++j) {
	    if (clist->checks[j].prio > clist->checks[highest].prio) {
		highest = j;
	    }
	}

	if (highest != i) {
	    pj_ice_check tmp;

	    pj_memcpy(&tmp, &clist->checks[i], sizeof(pj_ice_check));
	    pj_memcpy(&clist->checks[i], &clist->checks[highest], 
		      sizeof(pj_ice_check));
	    pj_memcpy(&clist->checks[highest], &tmp, sizeof(pj_ice_check));
	}
    }
}

/* Sort valid list based on priority */
static void sort_valid_list(pj_ice *ice)
{
    unsigned i;

    for (i=0; i<ice->valid_cnt-1; ++i) {
	unsigned j, highest = i;
	pj_ice_check *ci = &ice->clist.checks[ice->valid_list[i]];

	for (j=i+1; j<ice->valid_cnt; ++j) {
	    pj_ice_check *cj = &ice->clist.checks[ice->valid_list[j]];

	    if (cj->prio > ci->prio) {
		highest = j;
	    }
	}

	if (highest != i) {
	    unsigned tmp = ice->valid_list[i];
	    ice->valid_list[i] = ice->valid_list[j];
	    ice->valid_list[j] = tmp;
	}
    }
}


enum 
{ 
    SOCKADDR_EQUAL = 0, 
    SOCKADDR_NOT_EQUAL = 1 
};

/* Utility: compare sockaddr.
 * Returns 0 if equal.
 */
static int sockaddr_cmp(const pj_sockaddr *a1, const pj_sockaddr *a2)
{
    if (a1->addr.sa_family != a2->addr.sa_family)
	return SOCKADDR_NOT_EQUAL;

    if (a1->addr.sa_family == PJ_AF_INET) {
	return !(a1->ipv4.sin_addr.s_addr == a2->ipv4.sin_addr.s_addr &&
		 a1->ipv4.sin_port == a2->ipv4.sin_port);
    } else if (a1->addr.sa_family == PJ_AF_INET6) {
	return pj_memcmp(&a1->ipv6, &a2->ipv6, sizeof(a1->ipv6));
    } else {
	pj_assert(!"Invalid address family!");
	return SOCKADDR_NOT_EQUAL;
    }
}


/* Prune checklist, this must have been done after the checklist
 * is sorted.
 */
static void prune_checklist(pj_ice *ice, pj_ice_checklist *clist)
{
    unsigned i;

    /* Since an agent cannot send requests directly from a reflexive
     * candidate, but only from its base, the agent next goes through the
     * sorted list of candidate pairs.  For each pair where the local
     * candidate is server reflexive, the server reflexive candidate MUST be
     * replaced by its base.  Once this has been done, the agent MUST prune
     * the list.  This is done by removing a pair if its local and remote
     * candidates are identical to the local and remote candidates of a pair
     * higher up on the priority list.  The result is a sequence of ordered
     * candidate pairs, called the check list for that media stream.    
     */
    for (i=0; i<clist->count; ++i) {
	pj_ice_cand *licand = clist->checks[i].lcand;
	pj_ice_cand *ricand = clist->checks[i].rcand;
	const pj_sockaddr *liaddr;
	unsigned j;

	if (licand->type == PJ_ICE_CAND_TYPE_SRFLX)
	    liaddr = &licand->base_addr;
	else
	    liaddr = &licand->addr;

	for (j=i+1; j<clist->count;) {
	    pj_ice_cand *ljcand = clist->checks[j].lcand;
	    pj_ice_cand *rjcand = clist->checks[j].rcand;
	    const pj_sockaddr *ljaddr;

	    if (ljcand->type == PJ_ICE_CAND_TYPE_SRFLX)
		ljaddr = &ljcand->base_addr;
	    else
		ljaddr = &ljcand->addr;

	    if (sockaddr_cmp(liaddr, ljaddr) == SOCKADDR_EQUAL &&
		sockaddr_cmp(&ricand->addr, &rjcand->addr) == SOCKADDR_EQUAL)
	    {
		/* Found duplicate, remove it */
		char buf[CHECK_NAME_LEN];

		LOG((ice->obj_name, "Check %s pruned",
		    dump_check(buf, sizeof(buf), ice, &clist->checks[j])));

		pj_array_erase(clist->checks, sizeof(clist->checks[0]),
			       clist->count, j);
		--clist->count;

	    } else {
		++j;
	    }
	}
    }
}

/* This function is called when ICE processing completes */
static void on_ice_complete(pj_ice *ice, pj_status_t status)
{
    if (!ice->is_complete) {
	char errmsg[PJ_ERR_MSG_SIZE];

	ice->is_complete = PJ_TRUE;
	ice->ice_status = status;
    
	/* Log message */
	LOG((ice->obj_name, "ICE process complete, status=%s", 
	     pj_strerror(status, errmsg, sizeof(errmsg)).ptr));

	dump_checklist("Dumping checklist", ice, &ice->clist);
	dump_valid_list("Dumping valid list", ice);

	/* Call callback */
	(*ice->cb.on_ice_complete)(ice, status);
    }
}


/* This function is called when one check completes */
static pj_bool_t on_check_complete(pj_ice *ice,
				   pj_ice_check *check)
{
    unsigned i;

    pj_assert(check->state >= PJ_ICE_CHECK_STATE_SUCCEEDED);

    /* If there is at least one nominated pair in the valid list:
     * - The agent MUST remove all Waiting and Frozen pairs in the check
     *   list for the same component as the nominated pairs for that
     *   media stream
     * - If an In-Progress pair in the check list is for the same
     *   component as a nominated pair, the agent SHOULD cease
     *   retransmissions for its check if its pair priority is lower
     *   than the lowest priority nominated pair for that component
     */
    if (check->err_code==PJ_SUCCESS && check->nominated) {
	pj_ice_comp *comp;

	LOG((ice->obj_name, "Check %d is successful and nominated",
	     GET_CHECK_ID(check)));

	for (i=0; i<ice->clist.count; ++i) {
	    pj_ice_check *c = &ice->clist.checks[i];
	    if (c->lcand->comp_id == check->lcand->comp_id &&
		(c->state==PJ_ICE_CHECK_STATE_FROZEN ||
		 c->state==PJ_ICE_CHECK_STATE_WAITING))
	    {
		LOG((ice->obj_name, 
		     "Check %d to be failed because state is %s",
		     i, check_state_name[c->state]));
		check_set_state(ice, c, PJ_ICE_CHECK_STATE_FAILED,
				PJ_ECANCELLED);
	    }
	}

	/* Update the nominated check for the component */
	comp = find_comp(ice, check->lcand->comp_id);
	if (comp->nominated_check_id < 0) {
	    comp->nominated_check_id = GET_CHECK_ID(check);
	} else {
	    pj_ice_check *nom_check;
	    nom_check = &ice->clist.checks[comp->nominated_check_id];
	    if (nom_check->prio < check->prio)
		comp->nominated_check_id = GET_CHECK_ID(check);
	}
    }

    /* Once there is at least one nominated pair in the valid list for
     * every component of at least one media stream:
     * - The agent MUST change the state of processing for its check
     *   list for that media stream to Completed.
     * - The agent MUST continue to respond to any checks it may still
     *   receive for that media stream, and MUST perform triggered
     *   checks if required by the processing of Section 7.2.
     * - The agent MAY begin transmitting media for this media stream as
     *   described in Section 11.1
     */
    /* TODO */

    /* Once there is at least one nominated pair in the valid list for
     * each component of each media stream:
     * - The agent sets the state of ICE processing overall to
     *   Completed.
     * - If an agent is controlling, it examines the highest priority
     *   nominated candidate pair for each component of each media
     *   stream.  If any of those candidate pairs differ from the
     *   default candidate pairs in the most recent offer/answer
     *   exchange, the controlling agent MUST generate an updated offer
     *   as described in Section 9.  If the controlling agent is using
     *   an aggressive nomination algorithm, this may result in several
     *   updated offers as the pairs selected for media change.  An
     *   agent MAY delay sending the offer for a brief interval (one
     *   second is RECOMMENDED) in order to allow the selected pairs to
     *   stabilize.
     */
    /* TODO */


#if 0
    /* For now, just see if we have a valid pair in component 1 and
     * just terminate ICE.
     */
    for (i=0; i<ice->valid_cnt; ++i) {
	pj_ice_check *c = &ice->clist.checks[ice->valid_list[i]];
	if (c->lcand->comp_id == 1)
	    break;
    }

    if (i != ice->valid_cnt) {
	/* ICE succeeded */
	on_ice_complete(ice, PJ_SUCCESS);
	return PJ_TRUE;
    }
#else
    /* See if all components have nominated pair. If they do, then mark
     * ICE processing as success, otherwise wait.
     */
    for (i=0; i<ice->comp_cnt; ++i) {
	if (ice->comp[i].nominated_check_id == -1)
	    break;
    }
    if (i == ice->comp_cnt) {
	/* All components have nominated pair */
	on_ice_complete(ice, PJ_SUCCESS);
	return PJ_TRUE;
    }
#endif

    /* 
     * See if all checks in the checklist have completed. If we do,
     * then mark ICE processing as failed.
     */
    for (i=0; i<ice->clist.count; ++i) {
	pj_ice_check *c = &ice->clist.checks[i];
	if (c->state < PJ_ICE_CHECK_STATE_SUCCEEDED) {
	    break;
	}
    }

    if (i == ice->clist.count) {
	/* All checks have completed */
	on_ice_complete(ice, -1);
	return PJ_TRUE;
    }

    /* We still have checks to perform */
    return PJ_FALSE;
}



PJ_DEF(pj_status_t) pj_ice_create_check_list(pj_ice *ice,
					     const pj_str_t *rem_ufrag,
					     const pj_str_t *rem_passwd,
					     unsigned rcand_cnt,
					     const pj_ice_cand rcand[])
{
    pj_ice_checklist *clist;
    char buf[128];
    pj_str_t username;
    timer_data *td;
    unsigned i, j;

    PJ_ASSERT_RETURN(ice && rem_ufrag && rem_passwd && rcand_cnt && rcand,
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(rcand_cnt + ice->rcand_cnt <= PJ_ICE_MAX_CAND, 
		     PJ_ETOOMANY);

    pj_mutex_lock(ice->mutex);

    /* Save credentials */
    username.ptr = buf;

    pj_strcpy(&username, rem_ufrag);
    pj_strcat2(&username, ":");
    pj_strcat(&username, &ice->rx_ufrag);

    pj_strdup(ice->pool, &ice->tx_uname, &username);
    pj_strdup(ice->pool, &ice->tx_ufrag, rem_ufrag);
    pj_strdup(ice->pool, &ice->tx_pass, rem_passwd);

    pj_strcpy(&username, &ice->rx_ufrag);
    pj_strcat2(&username, ":");
    pj_strcat(&username, rem_ufrag);

    pj_strdup(ice->pool, &ice->rx_uname, &username);


    /* Save remote candidates */
    ice->rcand_cnt = 0;
    for (i=0; i<rcand_cnt; ++i) {
	pj_ice_cand *cn = &ice->rcand[ice->rcand_cnt++];
	pj_memcpy(cn, &rcand[i], sizeof(pj_ice_cand));
	pj_strdup(ice->pool, &cn->foundation, &rcand[i].foundation);
    }

    /* Generate checklist */
    clist = &ice->clist;
    for (i=0; i<ice->lcand_cnt; ++i) {
	for (j=0; j<ice->rcand_cnt; ++j) {

	    pj_ice_cand *lcand = &ice->lcand[i];
	    pj_ice_cand *rcand = &ice->rcand[j];
	    pj_ice_check *chk = &clist->checks[clist->count];

	    if (clist->count > PJ_ICE_MAX_CHECKS) {
		pj_mutex_unlock(ice->mutex);
		return PJ_ETOOMANY;
	    } else {
		clist->count++;
	    }

	    /* A local candidate is paired with a remote candidate if
	     * and only if the two candidates have the same component ID 
	     * and have the same IP address version. 
	     */
	    if (lcand->comp_id != rcand->comp_id ||
		lcand->addr.addr.sa_family != rcand->addr.addr.sa_family)
	    {
		continue;
	    }


	    chk->lcand = lcand;
	    chk->rcand = rcand;
	    chk->state = PJ_ICE_CHECK_STATE_FROZEN;

	    chk->prio = CALC_CHECK_PRIO(ice, lcand, rcand);
	}
    }

    /* Sort checklist based on priority */
    sort_checklist(clist);

    /* Prune the checklist */
    prune_checklist(ice, clist);

    /* Init timer entry in the checklist. Initially the timer ID is FALSE
     * because timer is not running.
     */
    clist->timer.id = PJ_FALSE;
    td = PJ_POOL_ZALLOC_T(ice->pool, timer_data);
    td->ice = ice;
    td->clist = clist;
    clist->timer.user_data = (void*)td;
    clist->timer.cb = &start_periodic_check;


    /* Log checklist */
    dump_checklist("Checklist created:", ice, clist);

    pj_mutex_unlock(ice->mutex);

    return PJ_SUCCESS;
}


struct req_data
{
    pj_ice		*ice;
    pj_ice_checklist	*clist;
    unsigned		 ckid;
};

/* Perform check on the specified candidate pair */
static pj_status_t perform_check(pj_ice *ice, pj_ice_checklist *clist,
				 unsigned check_id)
{
    pj_stun_tx_data *tdata;
    pj_ice_comp *comp;
    struct req_data *rd;
    pj_ice_check *check;
    const pj_ice_cand *lcand;
    const pj_ice_cand *rcand;
    pj_uint32_t prio;
    char buffer[128];
    pj_status_t status;

    check = &clist->checks[check_id];
    lcand = check->lcand;
    rcand = check->rcand;
    comp = find_comp(ice, lcand->comp_id);

    LOG((ice->obj_name, 
	 "Sending connectivity check for check %s", 
	 dump_check(buffer, sizeof(buffer), ice, check)));

    /* Create request */
    status = pj_stun_session_create_req(lcand->stun_sess, 
					PJ_STUN_BINDING_REQUEST, &tdata);
    if (status != PJ_SUCCESS)
	return status;

    /* Attach data to be retrieved later when STUN request transaction
     * completes and on_stun_request_complete() callback is called.
     */
    rd = PJ_POOL_ZALLOC_T(tdata->pool, struct req_data);
    rd->ice = ice;
    rd->clist = clist;
    rd->ckid = check_id;
    tdata->user_data = (void*) rd;

    /* Add PRIORITY */
    prio = CALC_CAND_PRIO(PJ_ICE_CAND_TYPE_PRFLX, 65535, 
			  lcand->comp_id);
    pj_stun_msg_add_uint_attr(tdata->pool, tdata->msg, PJ_STUN_ATTR_PRIORITY,
			      prio);

    /* Add USE-CANDIDATE and set this check to nominated */
    if (ice->role == PJ_ICE_ROLE_CONTROLLING) {
	pj_stun_msg_add_empty_attr(tdata->pool, tdata->msg, 
				   PJ_STUN_ATTR_USE_CANDIDATE);
	check->nominated = PJ_TRUE;
    }

    /* Note that USERNAME and MESSAGE-INTEGRITY will be added by the 
     * STUN session.
     */

    /* Initiate STUN transaction to send the request */
    status = pj_stun_session_send_msg(lcand->stun_sess, PJ_FALSE, 
				      &rcand->addr, 
				      sizeof(pj_sockaddr_in), tdata);
    if (status != PJ_SUCCESS)
	return status;

    check_set_state(ice, check, PJ_ICE_CHECK_STATE_IN_PROGRESS, PJ_SUCCESS);
    return PJ_SUCCESS;
}


/* Start periodic check for the specified checklist.
 * This callback is called by timer on every Ta (20msec by default)
 */
static pj_status_t start_periodic_check(pj_timer_heap_t *th, 
					pj_timer_entry *te)
{
    timer_data *td;
    pj_ice *ice;
    pj_ice_checklist *clist;
    unsigned i, start_count=0;
    pj_status_t status;

    td = (struct timer_data*) te->user_data;
    ice = td->ice;
    clist = td->clist;

    pj_mutex_lock(ice->mutex);

    /* Set timer ID to FALSE first */
    te->id = PJ_FALSE;

    /* Set checklist state to Running */
    clist_set_state(ice, clist, PJ_ICE_CHECKLIST_ST_RUNNING);

    LOG((ice->obj_name, "Starting checklist periodic check"));

    /* Send STUN Binding request for check with highest priority on
     * Waiting state.
     */
    for (i=0; i<clist->count; ++i) {
	pj_ice_check *check = &clist->checks[i];

	if (check->state == PJ_ICE_CHECK_STATE_WAITING) {
	    status = perform_check(ice, clist, i);
	    if (status != PJ_SUCCESS) {
		pj_mutex_unlock(ice->mutex);
		return status;
	    }

	    ++start_count;
	    break;
	}
    }

    /* If we don't have anything in Waiting state, perform check to
     * highest priority pair that is in Frozen state.
     */
    if (start_count==0) {
	for (i=0; i<clist->count; ++i) {
	    pj_ice_check *check = &clist->checks[i];

	    if (check->state == PJ_ICE_CHECK_STATE_FROZEN) {
		status = perform_check(ice, clist, i);
		if (status != PJ_SUCCESS) {
		    pj_mutex_unlock(ice->mutex);
		    return status;
		}

		++start_count;
		break;
	    }
	}
    }

    /* Cannot start check because there's no suitable candidate pair.
     * Set checklist state to Completed.
     */
    if (start_count==0) {
	clist_set_state(ice, clist, PJ_ICE_CHECKLIST_ST_COMPLETED);

    } else {
	/* Schedule for next timer */
	pj_time_val timeout = {0, PJ_ICE_TA_VAL};

	te->id = PJ_TRUE;
	pj_time_val_normalize(&timeout);
	pj_timer_heap_schedule(th, te, &timeout);
    }

    pj_mutex_unlock(ice->mutex);
    return PJ_SUCCESS;
}


/* Start ICE check */
PJ_DEF(pj_status_t) pj_ice_start_check(pj_ice *ice)
{
    pj_ice_checklist *clist;
    const pj_ice_cand *cand0;
    unsigned i;

    PJ_ASSERT_RETURN(ice, PJ_EINVAL);

    LOG((ice->obj_name, "Starting ICE check.."));

    clist = &ice->clist;

    if (clist->count == 0)
	return PJ_EICENOCHECKLIST;

    /* Pickup the first pair and set the state to Waiting */
    clist->checks[0].state = PJ_ICE_CHECK_STATE_WAITING;
    cand0 = clist->checks[0].lcand;

    /* Find all of the other pairs in that check list with the same
     * component ID, but different foundations, and sets all of their
     * states to Waiting as well.
     */
    for (i=1; i<clist->count; ++i) {
	const pj_ice_cand *cand1;

	cand1 = clist->checks[i].lcand;

	if (cand0->comp_id == cand1->comp_id &&
	    pj_strcmp(&cand0->foundation, &cand1->foundation)!=0)
	{
	    clist->checks[i].state = PJ_ICE_CHECK_STATE_WAITING;
	}
    }

    /* Start periodic check */
    return start_periodic_check(ice->stun_cfg.timer_heap, &clist->timer);
}


//////////////////////////////////////////////////////////////////////////////

static pj_status_t on_stun_send_msg(pj_stun_session *sess,
				    const void *pkt,
				    pj_size_t pkt_size,
				    const pj_sockaddr_t *dst_addr,
				    unsigned addr_len)
{
    stun_data *sd = (stun_data*) pj_stun_session_get_user_data(sess);
    return (*sd->ice->cb.on_tx_pkt)(sd->ice, sd->lcand->comp_id, sd->lcand_id,
				    pkt, pkt_size, 
				    dst_addr, addr_len);
}


/* This callback is called when outgoing STUN request completed */
static void on_stun_request_complete(pj_stun_session *stun_sess,
				     pj_status_t status,
				     pj_stun_tx_data *tdata,
				     const pj_stun_msg *response)
{
    struct req_data *rd = (struct req_data*) tdata->user_data;
    pj_ice *ice;
    pj_ice_check *check;
    const pj_ice_cand *lcand;
    const pj_ice_cand *rcand;
    pj_ice_checklist *clist;
    pj_stun_xor_mapped_addr_attr *xaddr;
    char buffer[CHECK_NAME_LEN];
    unsigned i;

    PJ_UNUSED_ARG(stun_sess);

    ice = rd->ice;
    check = &rd->clist->checks[rd->ckid];
    clist = rd->clist;

    pj_mutex_lock(ice->mutex);

    lcand = check->lcand;
    rcand = check->rcand;

    LOG((ice->obj_name, 
	 "Check %s%s: connectivity check %s",
	 dump_check(buffer, sizeof(buffer), ice, check),
	 (check->nominated ? " (nominated)" : " (not nominated)"),
	 (status==PJ_SUCCESS ? "SUCCESS" : "FAILED")));

    if (status != PJ_SUCCESS) {
	check_set_state(ice, check, PJ_ICE_CHECK_STATE_FAILED, status);
	on_check_complete(ice, check);
	pj_mutex_unlock(ice->mutex);
	return;
    }

    /* The agent MUST check that the source IP address and port of the
     * response equals the destination IP address and port that the Binding
     * Request was sent to, and that the destination IP address and port of
     * the response match the source IP address and port that the Binding
     * Request was sent from.
     */
    PJ_TODO(ICE_CHECK_RESPONSE_SOURCE_ADDRESS);

    /* Get the STUN MAPPED-ADDRESS attribute. */
    xaddr = (pj_stun_xor_mapped_addr_attr*)
	    pj_stun_msg_find_attr(response, PJ_STUN_ATTR_XOR_MAPPED_ADDR,0);
    if (!xaddr) {
	check_set_state(ice, check, PJ_ICE_CHECK_STATE_FAILED, 
			PJNATH_ESTUNNOXORMAP);
	on_check_complete(ice, check);
	pj_mutex_unlock(ice->mutex);
	return;
    }

    /* If the transport address returned in XOR-MAPPED-ADDRESS does not match
     * any of the local candidates that the agent knows about, the mapped 
     * address represents a new candidate - a peer reflexive candidate.
     */
    for (i=0; i<ice->lcand_cnt; ++i) {
	if (sockaddr_cmp(&xaddr->sockaddr, &ice->lcand[i].addr) == 0) {
	    /* Match */
	    break;
	}
    }

    if (i == ice->lcand_cnt) {
	unsigned cand_id;
	char buf[32];
	pj_str_t foundation;

	pj_ansi_snprintf(buf, sizeof(buf), "P%x", 
			 lcand->base_addr.ipv4.sin_addr.s_addr);
	foundation = pj_str(buf);

	/* Add new peer reflexive candidate */
	status = pj_ice_add_cand(ice, lcand->comp_id, 
				 PJ_ICE_CAND_TYPE_PRFLX,
				 65535, &foundation,
				 &xaddr->sockaddr, &lcand->base_addr, NULL,
				 sizeof(pj_sockaddr_in), &cand_id);
	if (status != PJ_SUCCESS) {
	    check_set_state(ice, check, PJ_ICE_CHECK_STATE_FAILED, status);
	    on_check_complete(ice, check);
	    pj_mutex_unlock(ice->mutex);
	    return;
	}

	/* Update local candidate */
	lcand = &ice->lcand[cand_id];
    }

    /* Sets the state of the pair that generated the check to succeeded. */
    check_set_state(ice, check, PJ_ICE_CHECK_STATE_SUCCEEDED, PJ_SUCCESS);

    /* This is a valid pair, so add this to the valid list */
    ice->valid_list[ice->valid_cnt++] = rd->ckid;

    /* Sort valid_list */
    sort_valid_list(ice);

    /* Inform about check completion.
     * This may terminate ICE processing.
     */
    if (on_check_complete(ice, check)) {
	/* ICE complete! */
	pj_mutex_unlock(ice->mutex);
	return;
    }

    /* If the pair had a component ID of 1, the agent MUST change the
     * states for all other Frozen pairs for the same media stream and
     * same foundation, but different component IDs, to Waiting.
     */
    if (lcand->comp_id == 1) {
	unsigned i;
	pj_bool_t unfrozen = PJ_FALSE;

	for (i=0; i<clist->count; ++i)  {
	    pj_ice_check *c = &clist->checks[i];

	    if (c->state == PJ_ICE_CHECK_STATE_FROZEN &&
		c->lcand->comp_id != lcand->comp_id &&
		pj_strcmp(&c->lcand->foundation, &lcand->foundation)==0)
	    {
		/* Unfreeze and start check */
		check_set_state(ice, c, PJ_ICE_CHECK_STATE_WAITING, 
				PJ_SUCCESS);
		unfrozen = PJ_TRUE;
	    }
	}

	if (unfrozen && clist->timer.id == PJ_FALSE)
	    start_periodic_check(ice->stun_cfg.timer_heap, &clist->timer);
    } 

    /* If the pair had a component ID equal to the number of components
     * for the media stream (where this is the actual number of
     * components being used, in cases where the number of components
     * signaled in the SDP differs from offerer to answerer), the agent
     * MUST change the state for all other Frozen pairs for the first
     * component of different media streams (and thus in different check
     * lists) but the same foundation, to Waiting.
     */
    else if (0) {
	PJ_TODO(UNFREEZE_OTHER_COMPONENT_ID);
    }
    /* If the pair has any other component ID, no other pairs can be
     * unfrozen.
     */
    else {
	PJ_TODO(UNFREEZE_OTHER_COMPONENT_ID1);
    }

    pj_mutex_unlock(ice->mutex);
}


static pj_status_t on_stun_rx_request(pj_stun_session *sess,
				      const pj_uint8_t *pkt,
				      unsigned pkt_len,
				      const pj_stun_msg *msg,
				      const pj_sockaddr_t *src_addr,
				      unsigned src_addr_len)
{
    stun_data *sd;
    pj_ice *ice;
    pj_stun_priority_attr *ap;
    pj_stun_use_candidate_attr *uc;
    pj_ice_comp *comp;
    pj_ice_cand *lcand;
    pj_ice_cand *rcand;
    unsigned i;
    pj_stun_tx_data *tdata;
    pj_bool_t is_relayed;
    pj_status_t status;

    PJ_UNUSED_ARG(pkt);
    PJ_UNUSED_ARG(pkt_len);

    /* Reject any requests except Binding request */
    if (msg->hdr.type != PJ_STUN_BINDING_REQUEST) {
	pj_str_t err_msg = pj_str("Expecting Binding Request only");
	status = pj_stun_session_create_response(sess, msg, 
						 PJ_STUN_SC_BAD_REQUEST,
						 &err_msg, &tdata);
	if (status != PJ_SUCCESS) {
	    return status;
	}

	status = pj_stun_session_send_msg(sess, PJ_TRUE, 
					  src_addr, src_addr_len, tdata);

	return status;
    }


    sd = (stun_data*) pj_stun_session_get_user_data(sess);
    ice = sd->ice;
    lcand = sd->lcand;
    comp = find_comp(ice, lcand->comp_id);

    pj_mutex_lock(ice->mutex);

    /* Get PRIORITY attribute */
    ap = (pj_stun_priority_attr*)
	 pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_PRIORITY, 0);
    if (ap == 0) {
	LOG((ice->obj_name, "Received Binding request with no PRIORITY"));
	pj_mutex_unlock(ice->mutex);
	return PJ_SUCCESS;
    }

    /* Get USE-CANDIDATE attribute */
    uc = (pj_stun_use_candidate_attr*)
	 pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_USE_CANDIDATE, 0);

    /* For simplicity, ignore incoming requests when we don't have remote
     * candidates yet. The peer agent should retransmit the STUN request
     * and we'll receive it again later.
     */
    if (ice->rcand_cnt == 0) {
	pj_mutex_unlock(ice->mutex);
	return PJ_SUCCESS;
    }

    /* 
     * First send response to this request 
     */
    status = pj_stun_session_create_response(sess, msg, 0, NULL, &tdata);
    if (status != PJ_SUCCESS) {
	pj_mutex_unlock(ice->mutex);
	return status;
    }

    status = pj_stun_msg_add_sockaddr_attr(tdata->pool, tdata->msg, 
					   PJ_STUN_ATTR_XOR_MAPPED_ADDR,
					   PJ_TRUE, src_addr, src_addr_len);

    status = pj_stun_session_send_msg(sess, PJ_TRUE, 
				      src_addr, src_addr_len, tdata);


    /* Find remote candidate based on the source transport address of 
     * the request.
     */
    for (i=0; i<ice->rcand_cnt; ++i) {
	if (sockaddr_cmp((const pj_sockaddr*)src_addr, &ice->rcand[i].addr)==0)
	    break;
    }

    /* If the source transport address of the request does not match any
     * existing remote candidates, it represents a new peer reflexive remote
     * candidate.
     */
    if (i == ice->rcand_cnt) {
	rcand = &ice->rcand[ice->rcand_cnt++];
	rcand->comp_id = comp->comp_id;
	rcand->type = PJ_ICE_CAND_TYPE_PRFLX;
	rcand->prio = ap->value;
	pj_memcpy(&rcand->addr, src_addr, src_addr_len);

	/* Foundation is random, unique from other foundation */
	rcand->foundation.ptr = pj_pool_alloc(ice->pool, 32);
	rcand->foundation.slen = pj_ansi_snprintf(rcand->foundation.ptr, 32,
						  "f%p", 
						  rcand->foundation.ptr);

	LOG((ice->obj_name, 
	     "Added new remote candidate from the request: %s:%d",
	     pj_inet_ntoa(rcand->addr.ipv4.sin_addr),
	     (int)pj_ntohs(rcand->addr.ipv4.sin_port)));

    } else {
	/* Remote candidate found */
	rcand = &ice->rcand[i];
    }

    /* 
     * Create candidate pair for this request. 
     */
    /* First check if the source address is the source address of the 
     * STUN relay, to determine if local candidate is relayed candidate.
     */
    PJ_TODO(DETERMINE_IF_REQUEST_COMES_FROM_RELAYED_CANDIDATE);
    is_relayed = PJ_FALSE;

    /* Now that we have local and remote candidate, check if we already
     * have this pair in our checklist.
     */
    for (i=0; i<ice->clist.count; ++i) {
	pj_ice_check *c = &ice->clist.checks[i];
	if (c->lcand == lcand && c->rcand == rcand)
	    break;
    }

    /* If the pair is already on the check list:
     * - If the state of that pair is Waiting or Frozen, its state is
     *   changed to In-Progress and a check for that pair is performed
     *   immediately.  This is called a triggered check.
     *
     * - If the state of that pair is In-Progress, the agent SHOULD
     *   generate an immediate retransmit of the Binding Request for the
     *   check in progress.  This is to facilitate rapid completion of
     *   ICE when both agents are behind NAT.
     * 
     * - If the state of that pair is Failed or Succeeded, no triggered
     *   check is sent.
     */
    if (i != ice->clist.count) {
	pj_ice_check *c = &ice->clist.checks[i];

	/* If USE-CANDIDATE is present, set nominated flag 
	 * Note: DO NOT overwrite nominated flag if one is already set.
	 */
	c->nominated = ((uc != NULL) || c->nominated);

	if (c->state == PJ_ICE_CHECK_STATE_FROZEN ||
	    c->state == PJ_ICE_CHECK_STATE_WAITING)
	{
	    LOG((ice->obj_name, "Performing triggered check for check %d",i));
	    perform_check(ice, &ice->clist, i);

	} else if (c->state == PJ_ICE_CHECK_STATE_IN_PROGRESS) {
	    /* Should retransmit here, but how??
	     * TODO
	     */
	    LOG((ice->obj_name, "Triggered check for check %d not performed "
				"because it's in progress", i));
	} else if (c->state == PJ_ICE_CHECK_STATE_SUCCEEDED) {
	    /* Check complete for this component.
	     * Note this may end ICE process.
	     */
	    pj_bool_t complete;

	    LOG((ice->obj_name, "Triggered check for check %d not performed "
				"because it's completed", i));

	    complete = on_check_complete(ice, c);
	    if (complete) {
		pj_mutex_unlock(ice->mutex);
		return PJ_SUCCESS;
	    }
	}

    }
    /* If the pair is not already on the check list:
     * - The pair is inserted into the check list based on its priority.
     * - Its state is set to In-Progress
     * - A triggered check for that pair is performed immediately.
     */
    /* Note: only do this if we don't have too many checks in checklist */
    else if (ice->clist.count < PJ_ICE_MAX_CHECKS) {

	pj_ice_check *c = &ice->clist.checks[ice->clist.count];

	c->lcand = lcand;
	c->rcand = rcand;
	c->prio = CALC_CHECK_PRIO(ice, lcand, rcand);
	c->state = PJ_ICE_CHECK_STATE_WAITING;
	c->nominated = (uc != NULL);
	c->err_code = PJ_SUCCESS;

	LOG((ice->obj_name, "New triggered check added: %d", 
	     ice->clist.count));
	perform_check(ice, &ice->clist, ice->clist.count++);

    } else {
	LOG((ice->obj_name, "Error: unable to perform triggered check: "
	     "TOO MANY CHECKS IN CHECKLIST!"));
    }

    pj_mutex_unlock(ice->mutex);
    return status;
}


static pj_status_t on_stun_rx_indication(pj_stun_session *sess,
					 const pj_uint8_t *pkt,
					 unsigned pkt_len,
					 const pj_stun_msg *msg,
					 const pj_sockaddr_t *src_addr,
					 unsigned src_addr_len)
{
    PJ_UNUSED_ARG(sess);
    PJ_UNUSED_ARG(pkt);
    PJ_UNUSED_ARG(pkt_len);
    PJ_UNUSED_ARG(msg);
    PJ_UNUSED_ARG(src_addr);
    PJ_UNUSED_ARG(src_addr_len);

    PJ_TODO(SUPPORT_RX_BIND_REQUEST_AS_INDICATION);

    return PJ_ENOTSUP;
}


PJ_DEF(pj_status_t) pj_ice_send_data( pj_ice *ice,
				      unsigned comp_id,
				      const void *data,
				      pj_size_t data_len)
{
    pj_status_t status = PJ_SUCCESS;
    pj_ice_comp *comp;
    unsigned cand_id;
    pj_ice_check *check;

    PJ_ASSERT_RETURN(ice, PJ_EINVAL);
    
    pj_mutex_lock(ice->mutex);

    comp = find_comp(ice, comp_id);
    if (comp == NULL) {
	status = PJ_EICEINCOMPID;
	goto on_return;
    }

    if (comp->nominated_check_id == -1) {
	status = PJ_EICEINPROGRESS;
	goto on_return;
    }

    check = &ice->clist.checks[comp->nominated_check_id];
    cand_id = GET_LCAND_ID(check->lcand);

    status = (*ice->cb.on_tx_pkt)(ice, comp_id, cand_id, data, data_len, 
				  &check->rcand->addr, 
				  sizeof(pj_sockaddr_in));

on_return:
    pj_mutex_unlock(ice->mutex);
    return status;
}


PJ_DEF(pj_status_t) pj_ice_on_rx_pkt( pj_ice *ice,
				      unsigned comp_id,
				      unsigned cand_id,
				      void *pkt,
				      pj_size_t pkt_size,
				      const pj_sockaddr_t *src_addr,
				      int src_addr_len)
{
    pj_status_t status = PJ_SUCCESS;
    pj_ice_comp *comp;
    pj_ice_cand *lcand;
    pj_status_t stun_status;

    PJ_ASSERT_RETURN(ice, PJ_EINVAL);

    pj_mutex_lock(ice->mutex);

    comp = find_comp(ice, comp_id);
    if (comp == NULL) {
	status = PJ_EICEINCOMPID;
	goto on_return;
    }

    lcand = &ice->lcand[cand_id];

    stun_status = pj_stun_msg_check(pkt, pkt_size, PJ_STUN_IS_DATAGRAM);
    if (stun_status == PJ_SUCCESS) {
	status = pj_stun_session_on_rx_pkt(lcand->stun_sess, pkt, pkt_size,
					   PJ_STUN_IS_DATAGRAM,
					   NULL, src_addr, src_addr_len);
    } else {
	(*ice->cb.on_rx_data)(ice, comp_id, cand_id, pkt, pkt_size, 
			      src_addr, src_addr_len);
    }
    

on_return:
    pj_mutex_unlock(ice->mutex);
    return status;
}


