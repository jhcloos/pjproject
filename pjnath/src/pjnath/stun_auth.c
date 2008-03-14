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
#include <pjnath/stun_auth.h>
#include <pjnath/errno.h>
#include <pjlib-util/hmac_sha1.h>
#include <pjlib-util/sha1.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/string.h>

#define THIS_FILE   "stun_auth.c"

/* Duplicate credential */
PJ_DEF(void) pj_stun_auth_cred_dup( pj_pool_t *pool,
				      pj_stun_auth_cred *dst,
				      const pj_stun_auth_cred *src)
{
    dst->type = src->type;

    switch (src->type) {
    case PJ_STUN_AUTH_CRED_STATIC:
	pj_strdup(pool, &dst->data.static_cred.realm,
			&src->data.static_cred.realm);
	pj_strdup(pool, &dst->data.static_cred.username,
			&src->data.static_cred.username);
	dst->data.static_cred.data_type = src->data.static_cred.data_type;
	pj_strdup(pool, &dst->data.static_cred.data,
			&src->data.static_cred.data);
	pj_strdup(pool, &dst->data.static_cred.nonce,
			&src->data.static_cred.nonce);
	break;
    case PJ_STUN_AUTH_CRED_DYNAMIC:
	pj_memcpy(&dst->data.dyn_cred, &src->data.dyn_cred, 
		  sizeof(src->data.dyn_cred));
	break;
    }
}


PJ_INLINE(pj_uint16_t) GET_VAL16(const pj_uint8_t *pdu, unsigned pos)
{
    return (pj_uint16_t) ((pdu[pos] << 8) + pdu[pos+1]);
}


PJ_INLINE(void) PUT_VAL16(pj_uint8_t *buf, unsigned pos, pj_uint16_t hval)
{
    buf[pos+0] = (pj_uint8_t) ((hval & 0xFF00) >> 8);
    buf[pos+1] = (pj_uint8_t) ((hval & 0x00FF) >> 0);
}


/* Send 401 response */
static pj_status_t create_challenge(pj_pool_t *pool,
				    const pj_stun_msg *msg,
				    int err_code,
				    const char *errstr,
				    const pj_str_t *realm,
				    const pj_str_t *nonce,
				    pj_stun_msg **p_response)
{
    pj_stun_msg *response;
    pj_str_t tmp_nonce;
    pj_str_t err_msg;
    pj_status_t rc;

    rc = pj_stun_msg_create_response(pool, msg, err_code, 
			             (errstr?pj_cstr(&err_msg, errstr):NULL), 
				     &response);
    if (rc != PJ_SUCCESS)
	return rc;


    if (realm && realm->slen) {
	rc = pj_stun_msg_add_string_attr(pool, response,
					 PJ_STUN_ATTR_REALM, 
					 realm);
	if (rc != PJ_SUCCESS)
	    return rc;

	/* long term must include nonce */
	if (!nonce || nonce->slen == 0) {
	    tmp_nonce = pj_str("pjstun");
	    nonce = &tmp_nonce;
	}
    }

    if (nonce && nonce->slen) {
	rc = pj_stun_msg_add_string_attr(pool, response,
					 PJ_STUN_ATTR_NONCE, 
					 nonce);
	if (rc != PJ_SUCCESS)
	    return rc;
    }

    *p_response = response;

    return PJ_SUCCESS;
}


/* Verify credential in the request */
PJ_DEF(pj_status_t) pj_stun_authenticate_request(const pj_uint8_t *pkt,
					         unsigned pkt_len,
					         const pj_stun_msg *msg,
					         pj_stun_auth_cred *cred,
					         pj_pool_t *pool,
						 pj_str_t *auth_key,
					         pj_stun_msg **p_response)
{
    pj_str_t realm, nonce, password;
    const pj_stun_msgint_attr *amsgi;
    unsigned i, amsgi_pos;
    pj_bool_t has_attr_beyond_mi;
    const pj_stun_username_attr *auser;
    pj_bool_t username_ok;
    const pj_stun_realm_attr *arealm;
    const pj_stun_realm_attr *anonce;
    pj_hmac_sha1_context ctx;
    pj_uint8_t digest[PJ_SHA1_DIGEST_SIZE];
    pj_str_t key;
    pj_status_t status;

    /* msg and credential MUST be specified */
    PJ_ASSERT_RETURN(pkt && pkt_len && msg && cred, PJ_EINVAL);

    /* If p_response is specified, pool MUST be specified. */
    PJ_ASSERT_RETURN(!p_response || pool, PJ_EINVAL);

    if (p_response)
	*p_response = NULL;

    if (!PJ_STUN_IS_REQUEST(msg->hdr.type))
	p_response = NULL;

    /* Get realm and nonce from credential */
    realm.slen = nonce.slen = 0;
    if (cred->type == PJ_STUN_AUTH_CRED_STATIC) {
	realm = cred->data.static_cred.realm;
	nonce = cred->data.static_cred.nonce;
    } else if (cred->type == PJ_STUN_AUTH_CRED_DYNAMIC) {
	status = cred->data.dyn_cred.get_auth(cred->data.dyn_cred.user_data,
					      pool, &realm, &nonce);
	if (status != PJ_SUCCESS)
	    return status;
    } else {
	pj_assert(!"Unexpected");
	return PJ_EBUG;
    }

    /* Look for MESSAGE-INTEGRITY while counting the position */
    amsgi_pos = 0;
    has_attr_beyond_mi = PJ_FALSE;
    amsgi = NULL;
    for (i=0; i<msg->attr_count; ++i) {
	if (msg->attr[i]->type == PJ_STUN_ATTR_MESSAGE_INTEGRITY) {
	    amsgi = (const pj_stun_msgint_attr*) msg->attr[i];
	} else if (amsgi) {
	    has_attr_beyond_mi = PJ_TRUE;
	    break;
	} else {
	    amsgi_pos += ((msg->attr[i]->length+3) & ~0x03) + 4;
	}
    }

    if (amsgi == NULL) {
	/* According to rfc3489bis-10 Sec 10.1.2/10.2.2, we should return 400
	   for short term, and 401 for long term.
	   The rule has been changed from rfc3489bis-06
	*/
	int code;

	code = realm.slen ? PJ_STUN_SC_UNAUTHORIZED : PJ_STUN_SC_BAD_REQUEST;
	if (p_response) {
	    create_challenge(pool, msg, code, NULL,
			     &realm, &nonce, p_response);
	}
	return PJ_STATUS_FROM_STUN_CODE(code);
    }

    /* Next check that USERNAME is present */
    auser = (const pj_stun_username_attr*)
	    pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_USERNAME, 0);
    if (auser == NULL) {
	/* According to rfc3489bis-10 Sec 10.1.2/10.2.2, we should return 400
	   for both short and long term, since M-I is present.
	   The rule has been changed from rfc3489bis-06
	*/
	int code = PJ_STUN_SC_BAD_REQUEST;
	if (p_response) {
	    create_challenge(pool, msg, code, "Missing USERNAME",
			     &realm, &nonce, p_response);
	}
	return PJ_STATUS_FROM_STUN_CODE(code);
    }

    /* Get REALM, if any */
    arealm = (const pj_stun_realm_attr*)
	     pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_REALM, 0);

    /* Check if username match */
    if (cred->type == PJ_STUN_AUTH_CRED_STATIC) {
	username_ok = !pj_strcmp(&auser->value, 
				 &cred->data.static_cred.username);
	password = cred->data.static_cred.data;
    } else if (cred->type == PJ_STUN_AUTH_CRED_DYNAMIC) {
	int data_type = 0;
	pj_status_t rc;
	rc = cred->data.dyn_cred.get_password(msg, 
					      cred->data.dyn_cred.user_data,
					      (arealm?&arealm->value:NULL),
					      &auser->value, pool,
					      &data_type, &password);
	username_ok = (rc == PJ_SUCCESS);
    } else {
	username_ok = PJ_TRUE;
	password.slen = 0;
    }

    if (!username_ok) {
	/* Username mismatch */
	/* According to rfc3489bis-10 Sec 10.1.2/10.2.2, we should 
	 * return 401 
	 */
	if (p_response) {
	    create_challenge(pool, msg, PJ_STUN_SC_UNAUTHORIZED, NULL,
			     &realm, &nonce, p_response);
	}
	return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_UNAUTHORIZED);
    }


    /* Get NONCE attribute */
    anonce = (pj_stun_nonce_attr*)
	     pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_NONCE, 0);

    /* Check for long term/short term requirements. */
    if (realm.slen != 0 && arealm == NULL) {
	/* Long term credential is required and REALM is not present */
	if (p_response) {
	    create_challenge(pool, msg, PJ_STUN_SC_BAD_REQUEST, 
			     "Missing REALM",
			     &realm, &nonce, p_response);
	}
	return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_BAD_REQUEST);

    } else if (realm.slen != 0 && arealm != NULL) {
	/* We want long term, and REALM is present */

	/* NONCE must be present. */
	if (anonce == NULL && nonce.slen) {
	    if (p_response) {
		create_challenge(pool, msg, PJ_STUN_SC_BAD_REQUEST, 
				 "Missing NONCE", &realm, &nonce, p_response);
	    }
	    return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_BAD_REQUEST);
	}

	/* Verify REALM matches */
	if (pj_stricmp(&arealm->value, &realm)) {
	    /* REALM doesn't match */
	    if (p_response) {
		create_challenge(pool, msg, PJ_STUN_SC_UNAUTHORIZED, 
				 "Invalid REALM", &realm, &nonce, p_response);
	    }
	    return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_UNAUTHORIZED);
	}

	/* Valid case, will validate the message integrity later */

    } else if (realm.slen == 0 && arealm != NULL) {
	/* We want to use short term credential, but client uses long
	 * term credential. The draft doesn't mention anything about
	 * switching between long term and short term.
	 */
	
	/* For now just accept the credential, anyway it will probably
	 * cause wrong message integrity value later.
	 */
    } else if (realm.slen==0 && arealm == NULL) {
	/* Short term authentication is wanted, and one is supplied */

	/* Application MAY request NONCE to be supplied */
	if (nonce.slen != 0) {
	    if (p_response) {
		create_challenge(pool, msg, PJ_STUN_SC_UNAUTHORIZED, 
				 "NONCE required", &realm, &nonce, p_response);
	    }
	    return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_UNAUTHORIZED);
	}
    }

    /* If NONCE is present, validate it */
    if (anonce) {
	pj_bool_t ok;

	if (cred->type == PJ_STUN_AUTH_CRED_DYNAMIC &&
	    cred->data.dyn_cred.verify_nonce != NULL) 
	{
	    ok=cred->data.dyn_cred.verify_nonce(msg, 
						cred->data.dyn_cred.user_data,
						(arealm?&arealm->value:NULL),
						&auser->value,
						&anonce->value);
	} else if (cred->type == PJ_STUN_AUTH_CRED_DYNAMIC) {
	    ok = PJ_TRUE;
	} else {
	    if (nonce.slen) {
		ok = !pj_strcmp(&anonce->value, &nonce);
	    } else {
		ok = PJ_TRUE;
	    }
	}

	if (!ok) {
	    if (p_response) {
		create_challenge(pool, msg, PJ_STUN_SC_STALE_NONCE, 
				 NULL, &realm, &nonce, p_response);
	    }
	    if (auth_key) {
		pj_stun_create_key(pool, auth_key, &realm, 
				   &auser->value, &password);
	    }
	    return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_STALE_NONCE);
	}
    }

    /* Calculate key */
    pj_stun_create_key(pool, &key, &realm, &auser->value, &password);

    /* Now calculate HMAC of the message. */
    pj_hmac_sha1_init(&ctx, (pj_uint8_t*)key.ptr, key.slen);

#if PJ_STUN_OLD_STYLE_MI_FINGERPRINT
    /* Pre rfc3489bis-06 style of calculation */
    pj_hmac_sha1_update(&ctx, pkt, 20);
#else
    /* First calculate HMAC for the header.
     * The calculation is different depending on whether FINGERPRINT attribute
     * is present in the message.
     */
    if (has_attr_beyond_mi) {
	pj_uint8_t hdr_copy[20];
	pj_memcpy(hdr_copy, pkt, 20);
	PUT_VAL16(hdr_copy, 2, (pj_uint16_t)(amsgi_pos + 24));
	pj_hmac_sha1_update(&ctx, hdr_copy, 20);
    } else {
	pj_hmac_sha1_update(&ctx, pkt, 20);
    }
#endif	/* PJ_STUN_OLD_STYLE_MI_FINGERPRINT */

    /* Now update with the message body */
    pj_hmac_sha1_update(&ctx, pkt+20, amsgi_pos);
#if PJ_STUN_OLD_STYLE_MI_FINGERPRINT
    // This is no longer necessary as per rfc3489bis-08
    if ((amsgi_pos+20) & 0x3F) {
    	pj_uint8_t zeroes[64];
    	pj_bzero(zeroes, sizeof(zeroes));
    	pj_hmac_sha1_update(&ctx, zeroes, 64-((amsgi_pos+20) & 0x3F));
    }
#endif
    pj_hmac_sha1_final(&ctx, digest);


    /* Compare HMACs */
    if (pj_memcmp(amsgi->hmac, digest, 20)) {
	/* HMAC value mismatch */
	/* According to rfc3489bis-10 Sec 10.1.2 we should return 401 */
	if (p_response) {
	    create_challenge(pool, msg, PJ_STUN_SC_UNAUTHORIZED,
			     NULL, &realm, &nonce, p_response);
	}
	return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_UNAUTHORIZED);
    }

    /* Everything looks okay! */
    return PJ_SUCCESS;
}


/* Determine if STUN message can be authenticated */
PJ_DEF(pj_bool_t) pj_stun_auth_valid_for_msg(const pj_stun_msg *msg)
{
    unsigned msg_type = msg->hdr.type;
    const pj_stun_errcode_attr *err_attr;

    /* STUN requests and success response can be authenticated */
    if (!PJ_STUN_IS_ERROR_RESPONSE(msg_type) && 
	!PJ_STUN_IS_INDICATION(msg_type))
    {
	return PJ_TRUE;
    }

    /* STUN Indication cannot be authenticated */
    if (PJ_STUN_IS_INDICATION(msg_type))
	return PJ_FALSE;

    /* Authentication for STUN error responses depend on the error
     * code.
     */
    err_attr = (const pj_stun_errcode_attr*)
	       pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_ERROR_CODE, 0);
    if (err_attr == NULL) {
	PJ_LOG(4,(THIS_FILE, "STUN error code attribute not present in "
			     "error response"));
	return PJ_TRUE;
    }

    switch (err_attr->err_code) {
    case PJ_STUN_SC_BAD_REQUEST:	    /* 400 (Bad Request)	    */
    case PJ_STUN_SC_UNAUTHORIZED:	    /* 401 (Unauthorized)	    */
    //case PJ_STUN_SC_STALE_CREDENTIALS:    /* 430 (Stale Credential)	    */
    //case PJ_STUN_SC_MISSING_USERNAME:	    /* 432 (Missing Username)	    */
    //case PJ_STUN_SC_MISSING_REALM:	    /* 434 (Missing Realm)	    */
    //case PJ_STUN_SC_UNKNOWN_USERNAME:	    /* 436 (Unknown Username)	    */
    //case PJ_STUN_SC_INTEGRITY_CHECK_FAILURE:/* 431 (Integrity Check Fail) */
	return PJ_FALSE;
    default:
	return PJ_TRUE;
    }
}


/* Authenticate MESSAGE-INTEGRITY in the response */
PJ_DEF(pj_status_t) pj_stun_authenticate_response(const pj_uint8_t *pkt,
					          unsigned pkt_len,
					          const pj_stun_msg *msg,
					          const pj_str_t *key)
{
    const pj_stun_msgint_attr *amsgi;
    unsigned i, amsgi_pos;
    pj_bool_t has_attr_beyond_mi;
    pj_hmac_sha1_context ctx;
    pj_uint8_t digest[PJ_SHA1_DIGEST_SIZE];

    PJ_ASSERT_RETURN(pkt && pkt_len && msg && key, PJ_EINVAL);

    /* First check that MESSAGE-INTEGRITY is present */
    amsgi = (const pj_stun_msgint_attr*)
	    pj_stun_msg_find_attr(msg, PJ_STUN_ATTR_MESSAGE_INTEGRITY, 0);
    if (amsgi == NULL) {
	return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_UNAUTHORIZED);
    }


    /* Check that message length is valid */
    if (msg->hdr.length < 24) {
	return PJNATH_EINSTUNMSGLEN;
    }

    /* Look for MESSAGE-INTEGRITY while counting the position */
    amsgi_pos = 0;
    has_attr_beyond_mi = PJ_FALSE;
    amsgi = NULL;
    for (i=0; i<msg->attr_count; ++i) {
	if (msg->attr[i]->type == PJ_STUN_ATTR_MESSAGE_INTEGRITY) {
	    amsgi = (const pj_stun_msgint_attr*) msg->attr[i];
	} else if (amsgi) {
	    has_attr_beyond_mi = PJ_TRUE;
	    break;
	} else {
	    amsgi_pos += ((msg->attr[i]->length+3) & ~0x03) + 4;
	}
    }

    if (amsgi == NULL) {
	return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_BAD_REQUEST);
    }

    /* Now calculate HMAC of the message. */
    pj_hmac_sha1_init(&ctx, (pj_uint8_t*)key->ptr, key->slen);

#if PJ_STUN_OLD_STYLE_MI_FINGERPRINT
    /* Pre rfc3489bis-06 style of calculation */
    pj_hmac_sha1_update(&ctx, pkt, 20);
#else
    /* First calculate HMAC for the header.
     * The calculation is different depending on whether FINGERPRINT attribute
     * is present in the message.
     */
    if (has_attr_beyond_mi) {
	pj_uint8_t hdr_copy[20];
	pj_memcpy(hdr_copy, pkt, 20);
	PUT_VAL16(hdr_copy, 2, (pj_uint16_t)(amsgi_pos+24));
	pj_hmac_sha1_update(&ctx, hdr_copy, 20);
    } else {
	pj_hmac_sha1_update(&ctx, pkt, 20);
    }
#endif	/* PJ_STUN_OLD_STYLE_MI_FINGERPRINT */

    /* Now update with the message body */
    pj_hmac_sha1_update(&ctx, pkt+20, amsgi_pos);
#if PJ_STUN_OLD_STYLE_MI_FINGERPRINT
    // This is no longer necessary as per rfc3489bis-08
    if ((amsgi_pos+20) & 0x3F) {
    	pj_uint8_t zeroes[64];
    	pj_bzero(zeroes, sizeof(zeroes));
    	pj_hmac_sha1_update(&ctx, zeroes, 64-((amsgi_pos+20) & 0x3F));
    }
#endif
    pj_hmac_sha1_final(&ctx, digest);

    /* Compare HMACs */
    if (pj_memcmp(amsgi->hmac, digest, 20)) {
	/* HMAC value mismatch */
	return PJ_STATUS_FROM_STUN_CODE(PJ_STUN_SC_UNAUTHORIZED);
    }

    /* Everything looks okay! */
    return PJ_SUCCESS;
}

