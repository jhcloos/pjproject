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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA 
 */

#include <pjmedia/transport_srtp.h>
#include <pjmedia/endpoint.h>
#include <pjlib-util/base64.h>
#include <pj/assert.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <srtp.h>

#define THIS_FILE   "transport_srtp.c"

/* Maximum size of packet */
#define MAX_BUFFER_LEN	1500
#define MAX_KEY_LEN	32
#define SRTP_ERROR(e)	-1

static const pj_str_t ID_RTP_AVP = { "RTP/AVP", 7 };
static const pj_str_t ID_RTP_SAVP = { "RTP/SAVP", 8 };

typedef struct crypto_suite
{
    char		*name;
    cipher_type_id_t	 cipher_type;
    unsigned		 cipher_key_len;
    auth_type_id_t	 auth_type;
    unsigned		 auth_key_len;
    unsigned		 srtp_auth_tag_len;
    unsigned		 srtcp_auth_tag_len;
    sec_serv_t		 service;
} crypto_suite;

/* Crypto suites as defined on RFC 4568 */
static crypto_suite crypto_suites[] = {
    /* plain RTP/RTCP (no cipher & no auth) */
    {"NULL", NULL_CIPHER, 0, NULL_AUTH, 0, 0, 0, sec_serv_none},

    /* cipher AES_CM, auth HMAC_SHA1, auth tag len = 10 octets */
    {"AES_CM_128_HMAC_SHA1_80", AES_128_ICM, 30, HMAC_SHA1, 20, 10, 10, 
	sec_serv_conf_and_auth},

    /* cipher AES_CM, auth HMAC_SHA1, auth tag len = 4 octets */
    {"AES_CM_128_HMAC_SHA1_32", AES_128_ICM, 30, HMAC_SHA1, 20, 4, 10,
	sec_serv_conf_and_auth},

    /* 
     * F8_128_HMAC_SHA1_8 not supported by libsrtp?
     * {"F8_128_HMAC_SHA1_8", NULL_CIPHER, 0, NULL_AUTH, 0, 0, 0, sec_serv_none}
     */
};

typedef struct transport_srtp
{
    pjmedia_transport	 base;	    /**< Base transport interface. */
    pj_pool_t		*pool;
    pj_lock_t		*mutex;
    char		 tx_buffer[MAX_BUFFER_LEN];
    char		 rx_buffer[MAX_BUFFER_LEN];
    unsigned		 options;   /**< Transport options.	   */

    pjmedia_srtp_setting setting;
    /* SRTP policy */
    pj_bool_t		 session_inited;
    pj_bool_t		 offerer_side;
    char		 tx_key[MAX_KEY_LEN];
    char		 rx_key[MAX_KEY_LEN];
    pjmedia_srtp_stream_crypto tx_policy;
    pjmedia_srtp_stream_crypto rx_policy;

    /* libSRTP contexts */
    srtp_t		 srtp_tx_ctx;
    srtp_t		 srtp_rx_ctx;

    /* Stream information */
    void		*user_data;
    void		(*rtp_cb)( void *user_data,
				   const void *pkt,
				   pj_ssize_t size);
    void		(*rtcp_cb)(void *user_data,
				   const void *pkt,
				   pj_ssize_t size);
        
    /* Transport information */
    pjmedia_transport	*real_tp; /**< Underlying transport.       */

} transport_srtp;


/*
 * This callback is called by transport when incoming rtp is received
 */
static void srtp_rtp_cb( void *user_data, const void *pkt, pj_ssize_t size);

/*
 * This callback is called by transport when incoming rtcp is received
 */
static void srtp_rtcp_cb( void *user_data, const void *pkt, pj_ssize_t size);


/*
 * These are media transport operations.
 */
static pj_status_t transport_get_info (pjmedia_transport *tp,
				       pjmedia_sock_info *info);
static pj_status_t transport_attach   (pjmedia_transport *tp,
				       void *user_data,
				       const pj_sockaddr_t *rem_addr,
				       const pj_sockaddr_t *rem_rtcp,
				       unsigned addr_len,
				       void (*rtp_cb)(void*,
						      const void*,
						      pj_ssize_t),
				       void (*rtcp_cb)(void*,
						       const void*,
						       pj_ssize_t));
static void	   transport_detach   (pjmedia_transport *tp,
				       void *strm);
static pj_status_t transport_send_rtp( pjmedia_transport *tp,
				       const void *pkt,
				       pj_size_t size);
static pj_status_t transport_send_rtcp(pjmedia_transport *tp,
				       const void *pkt,
				       pj_size_t size);
static pj_status_t transport_media_create(pjmedia_transport *tp,
				       pj_pool_t *pool,
				       pjmedia_sdp_session *sdp_local,
				       const pjmedia_sdp_session *sdp_remote);
static pj_status_t transport_media_start (pjmedia_transport *tp,
				       pj_pool_t *pool,
				       pjmedia_sdp_session *sdp_local,
				       const pjmedia_sdp_session *sdp_remote,
				       unsigned media_index);
static pj_status_t transport_media_stop(pjmedia_transport *tp);
static pj_status_t transport_simulate_lost(pjmedia_transport *tp,
				       pjmedia_dir dir,
				       unsigned pct_lost);
static pj_status_t transport_destroy  (pjmedia_transport *tp);



static pjmedia_transport_op transport_srtp_op = 
{
    &transport_get_info,
    &transport_attach,
    &transport_detach,
    &transport_send_rtp,
    &transport_send_rtcp,
    &transport_media_create,
    &transport_media_start,
    &transport_media_stop,
    &transport_simulate_lost,
    &transport_destroy
};

char * octet_string_hex_string(const void *s, int length);

static pj_status_t pjmedia_srtp_init_lib(void)
{
    static pj_bool_t initialized = PJ_FALSE;

    if (initialized == PJ_FALSE) {
	err_status_t err;
	err = srtp_init();
	if (err != err_status_ok) { 
	    PJ_LOG(4, (THIS_FILE, "Failed to init libsrtp."));
	    return SRTP_ERROR(err);
	}

	initialized = PJ_TRUE;

	PJ_TODO(REGISTER_LIBSRTP_ERROR_CODES_AND_MESSAGES);
    }
    
    return PJ_SUCCESS;
}


/*
 * Create an SRTP media transport.
 */
PJ_DEF(pj_status_t) pjmedia_transport_srtp_create( pjmedia_endpt *endpt, 
					 pjmedia_transport *tp,
					 unsigned options,
					 pjmedia_transport **p_srtp)
{
    pj_pool_t *pool;
    transport_srtp *srtp;
    pj_status_t status;

    PJ_ASSERT_RETURN(endpt && p_srtp, PJ_EINVAL);

    /* Init libsrtp. */
    status = pjmedia_srtp_init_lib();
    if (status != PJ_SUCCESS)
	return status;

    pool = pjmedia_endpt_create_pool(endpt, "srtp%p", 1000, 1000);
    srtp = PJ_POOL_ZALLOC_T(pool, transport_srtp);

    srtp->pool = pool;
    srtp->session_inited = PJ_FALSE;
    srtp->offerer_side = PJ_TRUE;
    srtp->options = options;

    status = pj_lock_create_null_mutex(pool, pool->obj_name, &srtp->mutex);
    if (status != PJ_SUCCESS) {
	pj_pool_release(pool);
	return status;
    }

    /* Initialize base pjmedia_transport */
    pj_memcpy(srtp->base.name, pool->obj_name, PJ_MAX_OBJ_NAME);
    srtp->base.type = tp->type;
    srtp->base.op = &transport_srtp_op;

    /* Set underlying transport */
    srtp->real_tp = tp;

    /* Done */
    *p_srtp = &srtp->base;

    return PJ_SUCCESS;
}


/*
 * Initialize and start SRTP session with the given parameters.
 */
PJ_DEF(pj_status_t) pjmedia_transport_srtp_start(
			   pjmedia_transport *srtp, 
			   const pjmedia_srtp_stream_crypto *policy_tx,
			   const pjmedia_srtp_stream_crypto *policy_rx)
{
    transport_srtp  *p_srtp = (transport_srtp*) srtp;
    srtp_policy_t    policy_tx_;
    srtp_policy_t    policy_rx_;
    err_status_t     err;
    int		     i;
    int		     cs_tx_idx = -1;
    int		     cs_rx_idx = -1;
    int		     crypto_suites_cnt;

    if (p_srtp->session_inited) {
	PJ_LOG(4, (THIS_FILE, "SRTP could not be re-init'd before deinit'd"));
	return PJ_EINVALIDOP;
    }

    crypto_suites_cnt = sizeof(crypto_suites)/sizeof(crypto_suites[0]);

    /* Check whether the crypto-suite requested is supported */
    for (i=0; i<crypto_suites_cnt; ++i) {
	if ((cs_rx_idx == -1) && 
	    !pj_stricmp2(&policy_rx->crypto_suite, crypto_suites[i].name))
	    cs_rx_idx = i;
	
	if ((cs_tx_idx == -1) && 
	    !pj_stricmp2(&policy_tx->crypto_suite, crypto_suites[i].name))
	    cs_tx_idx = i;
    }

    if ((cs_tx_idx == -1) || (cs_rx_idx == -1)) {
	PJ_LOG(4, (THIS_FILE, "Crypto-suite specified is not supported."));
	return PJ_ENOTSUP;
    }

    /* Init transmit direction */
    pj_bzero(&policy_tx_, sizeof(srtp_policy_t));
    pj_memmove(p_srtp->tx_key, policy_tx->key.ptr, policy_tx->key.slen);
    policy_tx_.key		  = (uint8_t*)p_srtp->tx_key;
    policy_tx_.ssrc.type	  = ssrc_any_outbound;
    policy_tx_.ssrc.value	  = 0;
    policy_tx_.rtp.sec_serv	  = crypto_suites[cs_tx_idx].service;
    policy_tx_.rtp.cipher_type	  = crypto_suites[cs_tx_idx].cipher_type;
    policy_tx_.rtp.cipher_key_len = crypto_suites[cs_tx_idx].cipher_key_len;
    policy_tx_.rtp.auth_type	  = crypto_suites[cs_tx_idx].auth_type;
    policy_tx_.rtp.auth_key_len	  = crypto_suites[cs_tx_idx].auth_key_len;
    policy_tx_.rtp.auth_tag_len	  = crypto_suites[cs_tx_idx].srtp_auth_tag_len;
    policy_tx_.rtcp		  = policy_tx_.rtp;
    policy_tx_.rtcp.auth_tag_len  = crypto_suites[cs_tx_idx].srtcp_auth_tag_len;
    policy_tx_.next		  = NULL;
    err = srtp_create(&p_srtp->srtp_tx_ctx, &policy_tx_);
    if (err != err_status_ok) {
	return SRTP_ERROR(err);
    }

    pj_strset(&p_srtp->tx_policy.key,  p_srtp->tx_key, policy_tx->key.slen);
    p_srtp->tx_policy.crypto_suite = pj_str(crypto_suites[cs_tx_idx].name);


    /* Init receive direction */
    pj_bzero(&policy_rx_, sizeof(srtp_policy_t));
    pj_memmove(p_srtp->rx_key, policy_rx->key.ptr, policy_rx->key.slen);
    policy_rx_.key		  = (uint8_t*)p_srtp->rx_key;
    policy_rx_.ssrc.type	  = ssrc_any_inbound;
    policy_rx_.ssrc.value	  = 0;
    policy_rx_.rtp.sec_serv	  = crypto_suites[cs_rx_idx].service;
    policy_rx_.rtp.cipher_type	  = crypto_suites[cs_rx_idx].cipher_type;
    policy_rx_.rtp.cipher_key_len = crypto_suites[cs_rx_idx].cipher_key_len;
    policy_rx_.rtp.auth_type	  = crypto_suites[cs_rx_idx].auth_type;
    policy_rx_.rtp.auth_key_len	  = crypto_suites[cs_rx_idx].auth_key_len;
    policy_rx_.rtp.auth_tag_len	  = crypto_suites[cs_rx_idx].srtp_auth_tag_len;
    policy_rx_.rtcp		  = policy_rx_.rtp;
    policy_rx_.rtcp.auth_tag_len  = crypto_suites[cs_rx_idx].srtcp_auth_tag_len;
    policy_rx_.next		  = NULL;
    err = srtp_create(&p_srtp->srtp_rx_ctx, &policy_rx_);
    if (err != err_status_ok) {
	srtp_dealloc(p_srtp->srtp_tx_ctx);
	return SRTP_ERROR(err);
    }

    pj_strset(&p_srtp->rx_policy.key,  p_srtp->rx_key, policy_rx->key.slen);
    p_srtp->rx_policy.crypto_suite = pj_str(crypto_suites[i].name);

    /* Declare SRTP session initialized */
    p_srtp->session_inited = PJ_TRUE;

    PJ_LOG(5, (THIS_FILE, "TX %s key=%s", crypto_suites[cs_tx_idx].name,
	   octet_string_hex_string(policy_tx->key.ptr, policy_tx->key.slen)));
    PJ_LOG(5, (THIS_FILE, "RX %s key=%s", crypto_suites[cs_rx_idx].name,
	   octet_string_hex_string(policy_rx->key.ptr, policy_rx->key.slen)));

    return PJ_SUCCESS;
}

/*
 * Stop SRTP session.
 */
PJ_DEF(pj_status_t) pjmedia_transport_srtp_stop(pjmedia_transport *srtp)
{
    transport_srtp *p_srtp = (transport_srtp*) srtp;
    err_status_t err;

    if (!p_srtp->session_inited)
	return PJ_SUCCESS;

    err = srtp_dealloc(p_srtp->srtp_rx_ctx);
    if (err != err_status_ok) {
	PJ_LOG(4, (THIS_FILE, "Failed to dealloc RX SRTP context"));
    }
    err = srtp_dealloc(p_srtp->srtp_tx_ctx);
    if (err != err_status_ok) {
	PJ_LOG(4, (THIS_FILE, "Failed to dealloc TX SRTP context"));
    }

    p_srtp->session_inited = PJ_FALSE;

    return PJ_SUCCESS;
}

PJ_DEF(pjmedia_transport *) pjmedia_transport_srtp_get_member(
						pjmedia_transport *tp)
{
    transport_srtp *srtp = (transport_srtp*) tp;

    PJ_ASSERT_RETURN(tp, NULL);

    return srtp->real_tp;
}


static pj_status_t transport_get_info(pjmedia_transport *tp,
				      pjmedia_sock_info *info)
{
    transport_srtp *srtp = (transport_srtp*) tp;

    /* put SRTP info as well? */
    return pjmedia_transport_get_info(srtp->real_tp, info);
}

static pj_status_t transport_attach(pjmedia_transport *tp,
				    void *user_data,
				    const pj_sockaddr_t *rem_addr,
				    const pj_sockaddr_t *rem_rtcp,
				    unsigned addr_len,
				    void (*rtp_cb) (void*, const void*,
						    pj_ssize_t),
				    void (*rtcp_cb)(void*, const void*,
						    pj_ssize_t))
{
    transport_srtp *srtp = (transport_srtp*) tp;
    pj_status_t status;

    /* Attach itself to transport */
    status = pjmedia_transport_attach(srtp->real_tp, srtp, rem_addr, rem_rtcp,
				      addr_len, &srtp_rtp_cb, &srtp_rtcp_cb);
    if (status != PJ_SUCCESS)
	return status;

    /* Save the callbacks */
    srtp->rtp_cb = rtp_cb;
    srtp->rtcp_cb = rtcp_cb;
    srtp->user_data = user_data;

    return status;
}

static void transport_detach(pjmedia_transport *tp, void *strm)
{
    transport_srtp *srtp = (transport_srtp*) tp;

    PJ_ASSERT_ON_FAIL(tp && srtp->real_tp, return);

    PJ_UNUSED_ARG(strm);
    pjmedia_transport_detach(srtp->real_tp, srtp);

    /* Clear up application infos from transport */
    srtp->rtp_cb = NULL;
    srtp->rtcp_cb = NULL;
    srtp->user_data = NULL;
}

static pj_status_t transport_send_rtp( pjmedia_transport *tp,
				       const void *pkt,
				       pj_size_t size)
{
    pj_status_t status;
    transport_srtp *srtp = (transport_srtp*) tp;
    int len = size;
    err_status_t err;

    if (!srtp->session_inited)
	return PJ_SUCCESS;

    PJ_ASSERT_RETURN(size < sizeof(srtp->tx_buffer), PJ_ETOOBIG);

    pj_lock_acquire(srtp->mutex);
    pj_memcpy(srtp->tx_buffer, pkt, size);
    
    err = srtp_protect(srtp->srtp_tx_ctx, srtp->tx_buffer, &len);
    if (err == err_status_ok) {
	status = pjmedia_transport_send_rtp(srtp->real_tp, srtp->tx_buffer, len);
    } else {
	status = SRTP_ERROR(err);
    }
    
    pj_lock_release(srtp->mutex);

    return status;
}

static pj_status_t transport_send_rtcp(pjmedia_transport *tp,
				       const void *pkt,
				       pj_size_t size)
{
    pj_status_t status;
    transport_srtp *srtp = (transport_srtp*) tp;
    int len = size;
    err_status_t err;

    if (!srtp->session_inited)
	return PJ_SUCCESS;

    PJ_ASSERT_RETURN((size) < sizeof(srtp->tx_buffer), PJ_ETOOBIG);

    pj_lock_acquire(srtp->mutex);
    pj_memcpy(srtp->tx_buffer, pkt, size);

    err = srtp_protect_rtcp(srtp->srtp_tx_ctx, srtp->tx_buffer, &len);
    
    if (err == err_status_ok) {
	status = pjmedia_transport_send_rtcp(srtp->real_tp, srtp->tx_buffer, len);
    } else {
	status = SRTP_ERROR(err);
    }

    pj_lock_release(srtp->mutex);

    return status;
}

static pj_status_t transport_simulate_lost(pjmedia_transport *tp,
					   pjmedia_dir dir,
					   unsigned pct_lost)
{
    transport_srtp *srtp = (transport_srtp *) tp;

    return pjmedia_transport_simulate_lost(srtp->real_tp, dir, pct_lost);
}

static pj_status_t transport_destroy  (pjmedia_transport *tp)
{
    transport_srtp *srtp = (transport_srtp *) tp;
    pj_status_t status;

    pj_lock_destroy(srtp->mutex);

    pjmedia_transport_detach(tp, NULL);
    
    if (srtp->setting.close_member_tp) {
	pjmedia_transport_close(srtp->real_tp);
    }

    status = pjmedia_transport_srtp_stop(tp);

    pj_pool_release(srtp->pool);

    return status;
}

/*
 * This callback is called by transport when incoming rtp is received
 */
static void srtp_rtp_cb( void *user_data, const void *pkt, pj_ssize_t size)
{
    transport_srtp *srtp = (transport_srtp *) user_data;
    int len = size;
    err_status_t err;

    if (size < 0 || size > sizeof(srtp->rx_buffer) || !srtp->session_inited) {
	return;
    }

    pj_lock_acquire(srtp->mutex);
    pj_memcpy(srtp->rx_buffer, pkt, size);

    err = srtp_unprotect(srtp->srtp_rx_ctx, srtp->rx_buffer, &len);
    
    if (err == err_status_ok) {
	srtp->rtp_cb(srtp->user_data, srtp->rx_buffer, len);
    } else {
	PJ_LOG(5, (THIS_FILE, "Failed to unprotect SRTP"));
    }

    pj_lock_release(srtp->mutex);
}

/*
 * This callback is called by transport when incoming rtcp is received
 */
static void srtp_rtcp_cb( void *user_data, const void *pkt, pj_ssize_t size)
{
    transport_srtp *srtp = (transport_srtp *) user_data;
    int len = size;
    err_status_t err;

    if (size < 0 || size > sizeof(srtp->rx_buffer) || !srtp->session_inited) {
	return;
    }

    pj_lock_acquire(srtp->mutex);
    pj_memcpy(srtp->rx_buffer, pkt, size);

    err = srtp_unprotect_rtcp(srtp->srtp_rx_ctx, srtp->rx_buffer, &len);

    if (err == err_status_ok) {
	srtp->rtcp_cb(srtp->user_data, srtp->rx_buffer, len);
    } else {
	PJ_LOG(5, (THIS_FILE, "Failed to unprotect SRTCP"));
    }
    
    pj_lock_release(srtp->mutex);
}

/* Generate crypto attribute, including crypto key.
 * If crypto-suite chosen is crypto NULL, just return PJ_SUCCESS,
 * and set buffer_len = 0.
 */
static pj_status_t generate_crypto_attr_value(char *buffer, int *buffer_len, 
					      int cs_idx, int cs_tag)
{
    pj_uint8_t key[MAX_KEY_LEN];
    char b64_key[PJ_BASE256_TO_BASE64_LEN(MAX_KEY_LEN)+1];
    int b64_key_len = sizeof(b64_key);
    err_status_t err;
    pj_status_t status;
    pj_bool_t key_ok;

    PJ_ASSERT_RETURN(MAX_KEY_LEN >= crypto_suites[cs_idx].cipher_key_len,
		     PJ_ETOOSMALL);

    /* Crypto-suite NULL. */
    if (cs_idx == 0) {
	*buffer_len = 0;
	return PJ_SUCCESS;
    }

    /* Generate key. */
    do {
	unsigned i;
	key_ok = PJ_TRUE;

	err = crypto_get_random(key, crypto_suites[cs_idx].cipher_key_len);
	if (err != err_status_ok) {
	    PJ_LOG(5,(THIS_FILE, "Failed generating random key"));
	    return SRTP_ERROR(err);
	}
	for (i=0; i<crypto_suites[cs_idx].cipher_key_len && key_ok; ++i)
	    if (key[i] == 0) key_ok = PJ_FALSE;

    } while (!key_ok);

    /* Key transmitted via SDP should be base64 encoded. */
    status = pj_base64_encode(key, crypto_suites[cs_idx].cipher_key_len,
			      b64_key, &b64_key_len);
    if (status != PJ_SUCCESS) {
	PJ_LOG(5,(THIS_FILE, "Failed encoding plain key to base64"));
	return status;
    }

    b64_key[b64_key_len] = '\0';

    
    PJ_ASSERT_RETURN((unsigned)*buffer_len >= 
		     (pj_ansi_strlen(crypto_suites[cs_idx].name) +
		     b64_key_len + 16), PJ_ETOOSMALL);

    /* Print the crypto attribute value. */
    *buffer_len = pj_ansi_snprintf(buffer, *buffer_len, "%d %s inline:%s",
				   cs_tag, 
				   crypto_suites[cs_idx].name,
				   b64_key);

    return PJ_SUCCESS;
}

static pj_status_t transport_media_create(pjmedia_transport *tp,
				          pj_pool_t *pool,
				          pjmedia_sdp_session *sdp_local,
				          const pjmedia_sdp_session *sdp_remote)
{
    struct transport_srtp *srtp = (struct transport_srtp*) tp;
    enum { MAXLEN = 512 };
    char buffer[MAXLEN];
    pj_status_t status;
    unsigned i, j;
    unsigned cs_cnt = sizeof(crypto_suites)/sizeof(crypto_suites[0]);

    /* If we are the answerer side, skip generating crypto-suites offer */
    if (sdp_remote) {
	srtp->offerer_side = PJ_FALSE;
	return PJ_SUCCESS;
    }
    
    srtp->offerer_side = PJ_TRUE;

    for (i=0; i<sdp_local->media_count; ++i) {
	/* Change "RTP/AVP" transport to "RTP/SAVP" */
	if (pj_stricmp(&sdp_local->media[i]->desc.transport, &ID_RTP_AVP) == 0) {
	    sdp_local->media[i]->desc.transport = ID_RTP_SAVP;

	/* Skip media transport that is not appropriate for SRTP */
	} else if (pj_stricmp(&sdp_local->media[i]->desc.transport, 
		   &ID_RTP_SAVP) != 0)
	{
	    continue;
	}

	/* Generate "crypto" attribute(s) */
	for (j=1; j<cs_cnt; ++j) {
	    /* Offer all our crypto-suites. */
	    pj_str_t attr_value;
	    int buffer_len = MAXLEN;
	    pjmedia_sdp_attr *attr;

	    status = generate_crypto_attr_value(buffer, &buffer_len, j, j);
	    if (status != PJ_SUCCESS)
		return status;

	    /* If buffer_len==0, just skip the crypto attribute. */
	    if (buffer_len) {
		pj_strset(&attr_value, buffer, buffer_len);
		attr = pjmedia_sdp_attr_create(pool, "crypto", &attr_value);
		sdp_local->media[i]->attr[sdp_local->media[i]->attr_count++] = attr;
	    }
	}
    }

    return pjmedia_transport_media_create(srtp->real_tp, pool, sdp_local, 
					   sdp_remote);
}

/* Parse crypto attribute line */
static pj_status_t parse_attr_crypto(pj_pool_t *pool,
				     const pjmedia_sdp_attr *attr,
				     pjmedia_srtp_stream_crypto *policy,
				     int *tag)
{
    pj_str_t input;
    char *token;

    pj_bzero(policy, sizeof(*policy));
    pj_strdup_with_null(pool, &input, &attr->value);

    /* Tag */
    token = strtok(input.ptr, " ");
    if (!token) {
	PJ_LOG(5,(THIS_FILE, "Expecting tag"));
	return PJMEDIA_SDP_EINATTR;
    }
    *tag = atoi(token);

    /* Crypto-suite */
    token = strtok(NULL, " ");
    if (!token) {
	PJ_LOG(5,(THIS_FILE, "Expecting crypto suite"));
	return PJMEDIA_SDP_EINATTR;
    }
    policy->crypto_suite = pj_str(token);

    /* Key method */
    token = strtok(NULL, ":");
    if (!token) {
	PJ_LOG(5,(THIS_FILE, "Expecting key method"));
	return PJMEDIA_SDP_EINATTR;
    }
    if (pj_ansi_stricmp(token, "inline")) {
	PJ_LOG(5,(THIS_FILE, "Key method %s not supported!", token));
	return PJMEDIA_SDP_EINATTR;
    }

    /* Key */
    token = strtok(NULL, "| ");
    if (!token) {
	PJ_LOG(5,(THIS_FILE, "Expecting key"));
	return PJMEDIA_SDP_EINATTR;
    }
    policy->key = pj_str(token);

    return PJ_SUCCESS;
}

static pj_status_t transport_media_start(pjmedia_transport *tp,
				         pj_pool_t *pool,
				         pjmedia_sdp_session *sdp_local,
				         const pjmedia_sdp_session *sdp_remote,
				         unsigned media_index)
{
    struct transport_srtp *srtp = (struct transport_srtp*) tp;
    pjmedia_sdp_media *media_remote = sdp_remote->media[media_index];
    pjmedia_sdp_media *media_local  = sdp_local->media[media_index];
    pjmedia_sdp_attr *attr;
    pjmedia_srtp_stream_crypto policy_remote;
    pjmedia_srtp_stream_crypto policy_local;
    pj_status_t status;
    unsigned cs_cnt = sizeof(crypto_suites)/sizeof(crypto_suites[0]);
    int cs_tag = -1;
    unsigned i, j;

    /* If the media is inactive, do nothing. */
    if (pjmedia_sdp_media_find_attr2(media_remote, "inactive", NULL) ||
	pjmedia_sdp_media_find_attr2(media_local, "inactive", NULL))
    {
	return PJ_SUCCESS;
    }

    /* default crypto-suite = crypto 'NULL' */
    pj_bzero(&policy_remote, sizeof(policy_remote));
    policy_remote.crypto_suite = pj_str(crypto_suites[0].name);
    pj_bzero(&policy_local, sizeof(policy_local));
    policy_remote.crypto_suite = pj_str(crypto_suites[0].name);

    /* If the media transport is not RTP/SAVP, just apply crypto default */
    if (pj_stricmp(&media_local->desc.transport, &ID_RTP_SAVP) ||
	pj_stricmp(&media_remote->desc.transport, &ID_RTP_SAVP))
    {
	return PJMEDIA_SRTP_ESDPREQSECTP;
    }

    /*
     * In this stage, we need to make sure the crypto-suite negotiation
     * is completed, and also parse the key from SDP.
     */
    if (srtp->offerer_side) {
	/* If we are at the offerer side: 
	 * 1. Get what crypto-suite selected by remote, we use the same one.
	 * 2. Get what key we offered in the offering stage for 
	 *    the selected crypto-suite.
	 * 3. If no crypto-suite answered, assume the answerer want plain RTP.
	 *    (perhaps this will abuse RFC 4568)
	 */

	/* Make sure only thera is ONLY ONE crypto attribute in the answer. */
	attr = NULL;
	for (i=0; i<media_remote->attr_count; ++i) {
	    if (!pj_stricmp2(&media_local->attr[i]->name, "crypto")) {
		if (attr) {
		    PJ_LOG(5,(THIS_FILE, "More than one crypto attr in " \
					 "the SDP answer."));
		    return PJMEDIA_SRTP_ESDPAMBIGUEANS;
		}

		attr = media_local->attr[i];
	    }
	}
	if (!attr) {
	    /* the answer got no crypto-suite attribute, huh! */
	    PJ_LOG(5,(THIS_FILE, "Crypto attribute cannot be found in" \
				 "remote SDP, using NULL crypto."));
	} else {

	    /* get policy_remote */
	    status = parse_attr_crypto(pool, attr, &policy_remote, &cs_tag);
	    if (status != PJ_SUCCESS)
		return status;

	    /* lets see what crypto-suite chosen, we will use the same one */
	    for (i=1; i<cs_cnt; ++i) {
		if (!pj_stricmp2(&policy_remote.crypto_suite, 
				 crypto_suites[i].name))
    		    break;
	    }

	    /* The crypto-suite answered is not supported,
	     * this SHOULD NEVER happen, since we only offer what we support,
	     * except the answerer is trying to force us use his crypto-suite!
	     * Let's return non-PJ_SUCCESS and cancel the call.
	     */
	    if (i == cs_cnt) {
		return PJ_ENOTSUP;
	    }

	    /* check whether the answer is match to our offers,
	     * then get our own offered key along with the crypto-suite
	     * and put it in policy_local.
	     */
	    for (i=0; i<media_local->attr_count; ++i) {
		int tmp_cs_tag;
    	    
		if (pj_stricmp2(&media_local->attr[i]->name, "crypto"))
		    continue;

		status = parse_attr_crypto(pool, media_local->attr[i], 
					   &policy_local, &tmp_cs_tag);
		if (status != PJ_SUCCESS)
		    return status;

		/* Selected crypto-suite is marked by same crypto attr tag */
		if (tmp_cs_tag == cs_tag)
		    break;
	    }
	    /* This SHOULD NEVER happen. */
	    if (i == media_local->attr_count) {
		return PJMEDIA_SDPNEG_ENOMEDIA;
	    }

	    /* Check if crypto-suite name match, crypto tag was ensured same */
	    if (pj_stricmp(&policy_local.crypto_suite, 
			   &policy_remote.crypto_suite))
	    {
		return PJMEDIA_ERROR;
	    }
	}
    } else {
	/* If we are at the answerer side: 
	 * 1. Negotiate: check if any offered crypto-suite matches 
	 *    our capability.
	 * 2. If there is one, generate key and media attribute.
	 * 3. If there isn't, return error.
	 * 4. If no crypto-suite offered, apply NULL crypto-suite.
	 *    (perhaps this will abuse RFC 4568)
	 *
	 * Please note that we need to consider the existance of other media,
	 * so instead of returning non-PJ_SUCCESS on failed negotiation,
	 * which will cancel the call, perhaps it is wiser to mark the media 
	 * as inactive.
	 */

	enum { MAXLEN = 512 };
	char buffer[MAXLEN];
	int  buffer_len = MAXLEN;
	pj_str_t attr_value;
	pj_bool_t no_crypto_attr = PJ_TRUE;
	int cs_idx = -1;

	/* find supported crypto-suite, get the tag, and assign policy_local */
	for (i=0; (i<media_remote->attr_count) && (cs_idx == -1); ++i) {
	    if (pj_stricmp2(&media_remote->attr[i]->name, "crypto"))
		continue;

	    no_crypto_attr = PJ_FALSE;

	    status = parse_attr_crypto(pool, media_remote->attr[i], 
				       &policy_remote, &cs_tag);
	    if (status != PJ_SUCCESS)
		return status;
	 
	    /* lets see if the crypto-suite offered is supported */
	    for (j=1; j<cs_cnt; ++j) {
		if (!pj_stricmp2(&policy_remote.crypto_suite, 
				 crypto_suites[j].name))
		{
		    cs_idx = j;
    		    break;
		}
	    }
	}

	if (!no_crypto_attr) {
	    /* No crypto-suites offered is supported by us.
	     * What should we do?
	     * By now, let's just deactivate the media.
	     */
	    if (i == media_remote->attr_count) {
		attr = pjmedia_sdp_attr_create(pool, "inactive", NULL);
		media_local->attr[media_local->attr_count++] = attr;
		media_local->desc.port = 0;

		return PJ_SUCCESS;
	    }

	    /* there is crypto-suite supported
	     * let's generate crypto attribute and also the key,
	     * dont forget to use offerer cs_tag.
	     */
	    status = generate_crypto_attr_value(buffer, &buffer_len, cs_idx, cs_tag);
	    if (status != PJ_SUCCESS)
		return status;

	    /* If buffer_len==0, just skip the crypto attribute. */
	    if (buffer_len) {
		pj_strset(&attr_value, buffer, buffer_len);
		attr = pjmedia_sdp_attr_create(pool, "crypto", &attr_value);
		media_local->attr[media_local->attr_count++] = attr;

		/* put our key & crypto-suite name to policy_local */
		status = parse_attr_crypto(pool, attr, &policy_local, &cs_tag);
		if (status != PJ_SUCCESS)
		    return status;
	    }
	}
    }

    /* in the SDP, all keys are in base64, they have to be decoded back
     * to base256 before used.
     */
    if (policy_local.key.slen) {
	char key[MAX_KEY_LEN];
	int  key_len = MAX_KEY_LEN;
	
	status = pj_base64_decode(&policy_local.key, 
	                          (pj_uint8_t*)key, &key_len);
	if (status != PJ_SUCCESS)
	    return status;

	pj_memcpy(policy_local.key.ptr, key, key_len);
	policy_local.key.slen = key_len;
    }

    if (policy_remote.key.slen) {
	char key[MAX_KEY_LEN];
	int  key_len = MAX_KEY_LEN;
	
	status = pj_base64_decode(&policy_remote.key, 
	                          (pj_uint8_t*)key, &key_len);
	if (status != PJ_SUCCESS)
	    return status;

	pj_memcpy(policy_remote.key.ptr, key, key_len);
	policy_remote.key.slen = key_len;
    }

    /* Got policy_local & policy_remote, let's initalize the SRTP */
    status = pjmedia_transport_srtp_start(tp, &policy_local, &policy_remote);
    if (status != PJ_SUCCESS)
	return status;

    return pjmedia_transport_media_start(srtp->real_tp, pool, 
					 sdp_local, sdp_remote,
				         media_index);
}

static pj_status_t transport_media_stop(pjmedia_transport *tp)
{
    struct transport_srtp *srtp = (struct transport_srtp*) tp;
    pj_status_t status;

    status = pjmedia_transport_srtp_stop(tp);
    if (status != PJ_SUCCESS)
	PJ_LOG(4, (THIS_FILE, "Failed deinit session."));

    return pjmedia_transport_media_stop(srtp->real_tp);
}
