/* $Id */
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
#include <pjlib-util/stun_msg.h>
#include <pjlib-util/errno.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/rand.h>
#include <pj/string.h>

#define THIS_FILE   "stun_msg.c"

static struct
{
    int err_code;
    const char *err_msg;
} stun_err_msg_map[] = 
{
    { PJ_STUN_STATUS_TRY_ALTERNATE,	    "Try Alternate"}, 
    { PJ_STUN_STATUS_BAD_REQUEST,	    "Bad Request"},
    { PJ_STUN_STATUS_UNAUTHORIZED,	    "Unauthorized"},
    { PJ_STUN_STATUS_UNKNOWN_ATTRIBUTE,	    "Unknown Attribute"},
    { PJ_STUN_STATUS_STALE_CREDENTIALS,	    "Stale Credentials"},
    { PJ_STUN_STATUS_INTEGRITY_CHECK_FAILURE,"Integrity Check Failure"},
    { PJ_STUN_STATUS_MISSING_USERNAME,	    "Missing Username"},
    { PJ_STUN_STATUS_USE_TLS,		    "Use TLS"},
    { PJ_STUN_STATUS_MISSING_REALM,	    "Missing Realm"},
    { PJ_STUN_STATUS_MISSING_NONCE,	    "Missing Nonce"},
    { PJ_STUN_STATUS_UNKNOWN_USERNAME,	    "Unknown Username"},
    { PJ_STUN_STATUS_NO_BINDING,	    "No Binding"},
    { PJ_STUN_STATUS_STALE_NONCE,	    "Stale Nonce"},
    { PJ_STUN_STATUS_TRANSITIONING,	    "Transitioning"},
    { PJ_STUN_STATUS_WRONG_USERNAME,	    "Wrong Username"},
    { PJ_STUN_STATUS_UNSUPP_TRANSPORT_PROTO,"Unsupported Transport Protocol"},
    { PJ_STUN_STATUS_INVALID_IP_ADDR,	    "Invalid IP Address"},
    { PJ_STUN_STATUS_INVALID_PORT,	    "Invalid Port"},
    { PJ_STUN_STATUS_OPER_TCP_ONLY,	    "Operation for TCP Only"},
    { PJ_STUN_STATUS_CONNECTION_FAILURE,    "Connection Failure"},
    { PJ_STUN_STATUS_CONNECTION_TIMEOUT,    "Connection Timeout"},
    { PJ_STUN_STATUS_SERVER_ERROR,	    "Server Error"},
    { PJ_STUN_STATUS_GLOBAL_FAILURE,	    "Global Failure"}
};


/**
 * Get STUN standard reason phrase for the specified error code.
 */
PJ_DEF(pj_str_t) pj_stun_get_err_reason(int err_code)
{
    unsigned i;

    for (i=0; i<PJ_ARRAY_SIZE(stun_err_msg_map); ++i) {
	if (stun_err_msg_map[i].err_code == err_code)
	    return pj_str((char*)stun_err_msg_map[i].err_msg);
    }
    return pj_str(NULL);
}



#define INIT_ATTR(a,t,l)    (a)->hdr.type=(pj_uint16_t)(t), \
			    (a)->hdr.length=(pj_uint16_t)(l)
#define ATTR_HDR_LEN	    4


//////////////////////////////////////////////////////////////////////////////
/*
 * STUN generic IP address container
 */
#define STUN_GENERIC_IP_ADDR_LEN	8

/*
 * Create a generic STUN IP address attribute for IPv4 address.
 */
PJ_DEF(pj_status_t) 
pj_stun_generic_ip_addr_attr_create(pj_pool_t *pool,
				    int attr_type,
				    pj_uint32_t ip_addr,
				    int port,
				    pj_stun_generic_ip_addr_attr **p_attr)
{
    pj_stun_generic_ip_addr_attr *attr;

    PJ_ASSERT_RETURN(pool && p_attr, PJ_EINVAL);

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_generic_ip_addr_attr);
    INIT_ATTR(attr, attr_type, STUN_GENERIC_IP_ADDR_LEN);
    attr->family = 1;
    attr->port = (pj_uint16_t) port;
    attr->addr.ipv4 = ip_addr;

    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t decode_generic_ip_addr_attr(pj_pool_t *pool, 
					       const pj_uint8_t *buf, 
					       void **p_attr)
{
    enum
    {
	ATTR_LEN = STUN_GENERIC_IP_ADDR_LEN + ATTR_HDR_LEN
    };
    pj_stun_generic_ip_addr_attr *attr;

    /* Check that the struct address is valid */
    pj_assert(sizeof(pj_stun_generic_ip_addr_attr) == ATTR_LEN);

    /* Create the attribute */
    attr = PJ_POOL_ALLOC_TYPE(pool, pj_stun_generic_ip_addr_attr);
    pj_memcpy(attr, buf, ATTR_LEN);

    /* Check address family is valid (only supports ipv4 for now) */
    if (attr->family != 1)
	return PJLIB_UTIL_ESTUNINATTR;

    /* Convert to host byte order */
    attr->hdr.type = pj_ntohs(attr->hdr.type);
    attr->hdr.length = pj_ntohs(attr->hdr.length);
    attr->port = pj_ntohs(attr->port);
    attr->addr.ipv4 = pj_ntohl(attr->addr.ipv4);

    /* Check that the attribute length is valid */
    if (attr->hdr.length != STUN_GENERIC_IP_ADDR_LEN)
	return PJLIB_UTIL_ESTUNINATTRLEN;

    /* Done */
    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t encode_generic_ip_addr_attr(const void *a, pj_uint8_t *buf, 
					       unsigned len, unsigned *printed)
{
    enum
    {
	ATTR_LEN = STUN_GENERIC_IP_ADDR_LEN + ATTR_HDR_LEN
    };
    pj_stun_generic_ip_addr_attr *attr;

    if (len < ATTR_LEN) 
	return PJ_ETOOSMALL;

    /* Copy and convert attribute to network byte order */
    pj_memcpy(buf, a, ATTR_LEN);
    attr = (pj_stun_generic_ip_addr_attr*) buf;
    attr->hdr.type = pj_htons(attr->hdr.type);
    attr->hdr.length = pj_htons(attr->hdr.length);
    attr->port = pj_htons(attr->port);
    attr->addr.ipv4 = pj_htonl(attr->addr.ipv4);

    /* Done */
    *printed = ATTR_LEN;

    return PJ_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////
/*
 * STUN generic string attribute
 */

/*
 * Create a STUN generic string attribute.
 */
PJ_DEF(pj_status_t) 
pj_stun_generic_string_attr_create(pj_pool_t *pool,
				   int attr_type,
				   const pj_str_t *value,
				   pj_stun_generic_string_attr **p_attr)
{
    pj_stun_generic_string_attr *attr;

    PJ_ASSERT_RETURN(pool && value && p_attr, PJ_EINVAL);

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_generic_string_attr);
    INIT_ATTR(attr, attr_type, value->slen);
    pj_strdup(pool, &attr->value, value);

    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t decode_generic_string_attr(pj_pool_t *pool, 
					      const pj_uint8_t *buf, 
					      void **p_attr)
{
    pj_stun_generic_string_attr *attr;
    pj_str_t value;

    /* Create the attribute */
    attr = PJ_POOL_ALLOC_TYPE(pool, pj_stun_generic_string_attr);

    /* Copy the header */
    pj_memcpy(attr, buf, ATTR_HDR_LEN);

    /* Convert to host byte order */
    attr->hdr.type = pj_ntohs(attr->hdr.type);
    attr->hdr.length = pj_ntohs(attr->hdr.length);

    /* Get pointer to the string in the message */
    value.ptr = ((char*)buf + ATTR_HDR_LEN);
    value.slen = attr->hdr.length;

    /* Copy the string to the attribute */
    pj_strdup(pool, &attr->value, &value);

    /* Done */
    *p_attr = attr;

    return PJ_SUCCESS;

}


static pj_status_t encode_generic_string_attr(const void *a, pj_uint8_t *buf, 
					      unsigned len, unsigned *printed)
{
    const pj_stun_generic_string_attr *ca = 
	(const pj_stun_generic_string_attr*)a;
    pj_stun_attr_hdr *attr;

    /* Calculated total attr_len (add padding if necessary) */
    *printed = (ca->value.slen + ATTR_HDR_LEN + 3) & (~3);
    if (len < *printed)
	return PJ_ETOOSMALL;

    /* Copy header */
    pj_memcpy(buf, a, ATTR_HDR_LEN);

    /* Set the correct length */
    attr = (pj_stun_attr_hdr*)buf;
    attr->length = (pj_uint16_t) ca->value.slen;

    /* Convert to network byte order */
    attr->type = pj_htons(attr->type);
    attr->length = pj_htons(attr->length);

    /* Copy the string */
    pj_memcpy(buf+ATTR_HDR_LEN, ca->value.ptr, ca->value.slen);

    /* Done */
    return PJ_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////
/*
 * STUN generic 32bit integer attribute.
 */
#define STUN_UINT_LEN	4

/*
 * Create a STUN generic 32bit value attribute.
 */
PJ_DEF(pj_status_t) 
pj_stun_generic_uint_attr_create(pj_pool_t *pool,
				 int attr_type,
				 pj_uint32_t value,
				 pj_stun_generic_uint_attr **p_attr)
{
    pj_stun_generic_uint_attr *attr;

    PJ_ASSERT_RETURN(pool && p_attr, PJ_EINVAL);

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_generic_uint_attr);
    INIT_ATTR(attr, attr_type, STUN_UINT_LEN);
    attr->value = value;

    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t decode_generic_uint_attr(pj_pool_t *pool, 
					    const pj_uint8_t *buf, 
					    void **p_attr)
{
    enum
    {
	ATTR_LEN = STUN_UINT_LEN + ATTR_HDR_LEN
    };
    pj_stun_generic_uint_attr *attr;

    /* Check that the struct address is valid */
    pj_assert(sizeof(pj_stun_generic_uint_attr) == ATTR_LEN);

    /* Create the attribute */
    attr = PJ_POOL_ALLOC_TYPE(pool, pj_stun_generic_uint_attr);
    pj_memcpy(attr, buf, ATTR_LEN);

    /* Convert to host byte order */
    attr->hdr.type = pj_ntohs(attr->hdr.type);
    attr->hdr.length = pj_ntohs(attr->hdr.length);
    attr->value = pj_ntohl(attr->value);

    /* Check that the attribute length is valid */
    if (attr->hdr.length != STUN_UINT_LEN)
	return PJLIB_UTIL_ESTUNINATTRLEN;

    /* Done */
    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t encode_generic_uint_attr(const void *a, pj_uint8_t *buf, 
					    unsigned len, unsigned *printed)
{
    enum
    {
	ATTR_LEN = STUN_UINT_LEN + ATTR_HDR_LEN
    };
    pj_stun_generic_uint_attr *attr;

    if (len < ATTR_LEN) 
	return PJ_ETOOSMALL;

    /* Copy and convert attribute to network byte order */
    pj_memcpy(buf, a, ATTR_LEN);
    attr = (pj_stun_generic_uint_attr*) buf;
    attr->hdr.type = pj_htons(attr->hdr.type);
    attr->hdr.length = pj_htons(attr->hdr.length);
    attr->value = pj_htonl(attr->value);

    /* Done */
    *printed = ATTR_LEN;

    return PJ_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////
/*
 * STUN MESSAGE-INTEGRITY attribute.
 */

#define STUN_MSG_INTEGRITY_LEN	20

/*
 * Create a STUN MESSAGE-INTEGRITY attribute.
 */
PJ_DEF(pj_status_t) 
pj_stun_msg_integrity_attr_create(pj_pool_t *pool,
				  pj_stun_msg_integrity_attr **p_attr)
{
    pj_stun_msg_integrity_attr *attr;

    PJ_ASSERT_RETURN(pool && p_attr, PJ_EINVAL);

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_msg_integrity_attr);
    INIT_ATTR(attr, PJ_STUN_ATTR_MESSAGE_INTEGRITY, STUN_MSG_INTEGRITY_LEN);

    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t decode_msg_integrity_attr(pj_pool_t *pool, 
					     const pj_uint8_t *buf,
					     void **p_attr)
{
    enum
    {
	ATTR_LEN = STUN_MSG_INTEGRITY_LEN + ATTR_HDR_LEN
    };
    pj_stun_msg_integrity_attr *attr;

    /* Check that struct size is valid */
    pj_assert(sizeof(pj_stun_msg_integrity_attr)==STUN_MSG_INTEGRITY_LEN);

    /* Create attribute */
    attr = PJ_POOL_ALLOC_TYPE(pool, pj_stun_msg_integrity_attr);
    pj_memcpy(attr, buf, sizeof(pj_stun_msg_integrity_attr));
    attr->hdr.type = pj_ntohs(attr->hdr.type);
    attr->hdr.length = pj_ntohs(attr->hdr.length);

    /* Check that the attribute length is valid */
    if (attr->hdr.length != STUN_MSG_INTEGRITY_LEN)
	return PJLIB_UTIL_ESTUNINATTRLEN;

    /* Done */
    *p_attr = attr;
    return PJ_SUCCESS;
}


static pj_status_t encode_msg_integrity_attr(const void *a, pj_uint8_t *buf, 
					     unsigned len, unsigned *printed)
{
    enum
    {
	ATTR_LEN = STUN_MSG_INTEGRITY_LEN + ATTR_HDR_LEN
    };
    pj_stun_msg_integrity_attr *attr;

    if (len < ATTR_LEN) 
	return PJ_ETOOSMALL;

    /* Copy and convert attribute to network byte order */
    pj_memcpy(buf, a, ATTR_LEN);
    attr = (pj_stun_msg_integrity_attr*) buf;
    attr->hdr.type = pj_htons(attr->hdr.type);
    attr->hdr.length = pj_htons(attr->hdr.length);

    /* Done */
    *printed = ATTR_LEN;

    return PJ_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////
/*
 * STUN ERROR-CODE
 */

/*
 * Create a STUN ERROR-CODE attribute.
 */
PJ_DEF(pj_status_t) 
pj_stun_error_code_attr_create(pj_pool_t *pool,
			       int err_code,
			       const pj_str_t *err_reason,
			       pj_stun_error_code_attr **p_attr)
{
    pj_stun_error_code_attr *attr;
    char err_buf[80];
    pj_str_t str;

    PJ_ASSERT_RETURN(pool && err_code && p_attr, PJ_EINVAL);

    if (err_reason == NULL) {
	str = pj_stun_get_err_reason(err_code);
	if (str.slen == 0) {
	    str.slen = pj_ansi_snprintf(err_buf, sizeof(err_buf),
				        "Unknown error %d", err_code);
	    str.ptr = err_buf;
	}
	err_reason = &str;
    }

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_error_code_attr);
    INIT_ATTR(attr, PJ_STUN_ATTR_ERROR_CODE, 4+err_reason->slen);
    attr->err_class = (pj_uint8_t)(err_code / 100);
    attr->number = (pj_uint8_t) (err_code % 100);
    pj_strdup(pool, &attr->reason, err_reason);

    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t decode_error_code_attr(pj_pool_t *pool, 
					  const pj_uint8_t *buf,
					  void **p_attr)
{
    pj_stun_error_code_attr *attr;
    pj_str_t value;

    /* Create the attribute */
    attr = PJ_POOL_ALLOC_TYPE(pool, pj_stun_error_code_attr);

    /* Copy the header */
    pj_memcpy(attr, buf, ATTR_HDR_LEN + 4);

    /* Convert to host byte order */
    attr->hdr.type = pj_ntohs(attr->hdr.type);
    attr->hdr.length = pj_ntohs(attr->hdr.length);

    /* Get pointer to the string in the message */
    value.ptr = ((char*)buf + ATTR_HDR_LEN + 4);
    value.slen = attr->hdr.length - 4;

    /* Copy the string to the attribute */
    pj_strdup(pool, &attr->reason, &value);

    /* Done */
    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t encode_error_code_attr(const void *a, pj_uint8_t *buf, 
					  unsigned len, unsigned *printed)
{
    const pj_stun_error_code_attr *ca = 
	(const pj_stun_error_code_attr*)a;
    pj_stun_error_code_attr *attr;

    if (len < ATTR_HDR_LEN + 4 + (unsigned)ca->reason.slen) 
	return PJ_ETOOSMALL;

    /* Copy and convert attribute to network byte order */
    pj_memcpy(buf, ca, ATTR_HDR_LEN + 4);

    /* Update length */
    attr = (pj_stun_error_code_attr*) buf;
    attr->hdr.length = (pj_uint16_t)(4 + ca->reason.slen);

    /* Convert fiends to network byte order */
    attr->hdr.type = pj_htons(attr->hdr.type);
    attr->hdr.length = pj_htons(attr->hdr.length);

    /* Copy error string */
    pj_memcpy(buf + ATTR_HDR_LEN + 4, ca->reason.ptr, ca->reason.slen);

    /* Done */
    *printed = (ATTR_HDR_LEN + 4 + ca->reason.slen + 3) & (~3);

    return PJ_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////
/*
 * STUN UNKNOWN-ATTRIBUTES attribute
 */

/*
 * Create an empty instance of STUN UNKNOWN-ATTRIBUTES attribute.
 *
 * @param pool		The pool to allocate memory from.
 * @param p_attr	Pointer to receive the attribute.
 *
 * @return		PJ_SUCCESS on success or the appropriate error code.
 */
PJ_DEF(pj_status_t) 
pj_stun_unknown_attr_create(pj_pool_t *pool,
			    unsigned attr_cnt,
			    pj_uint16_t attr_array[],
			    pj_stun_unknown_attr **p_attr)
{
    pj_stun_unknown_attr *attr;
    unsigned i;

    PJ_ASSERT_RETURN(pool && attr_cnt < PJ_STUN_MAX_ATTR && p_attr, PJ_EINVAL);

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_unknown_attr);
    INIT_ATTR(attr, PJ_STUN_ATTR_UNKNOWN_ATTRIBUTES, attr_cnt * 2);

    attr->attr_count = attr_cnt;
    for (i=0; i<attr_cnt; ++i) {
	attr->attrs[i] = attr_array[i];
    }

    /* If the number of unknown attributes is an odd number, one of the
     * attributes MUST be repeated in the list.
     */
    if ((attr_cnt & 0x01)) {
	attr->attrs[attr_cnt] = attr_array[attr_cnt-1];
    }

    *p_attr = NULL;

    return PJ_SUCCESS;
}


static pj_status_t decode_unknown_attr(pj_pool_t *pool, 
				       const pj_uint8_t *buf, 
				       void **p_attr)
{
    pj_stun_unknown_attr *attr;
    const pj_uint16_t *punk_attr;
    unsigned i;

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_unknown_attr);
    pj_memcpy(attr, buf, ATTR_HDR_LEN);

    attr->hdr.type = pj_ntohs(attr->hdr.type);
    attr->hdr.length = pj_ntohs(attr->hdr.length);

    attr->attr_count = (attr->hdr.length >> 1);
    if (attr->attr_count > PJ_STUN_MAX_ATTR)
	return PJ_ETOOMANY;

    punk_attr = (const pj_uint16_t*)(buf + ATTR_HDR_LEN);
    for (i=0; i<attr->attr_count; ++i) {
	attr->attrs[i] = pj_ntohs(punk_attr[i]);
    }

    /* Done */
    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t encode_unknown_attr(const void *a, pj_uint8_t *buf, 
				       unsigned len, unsigned *printed)
{
    const pj_stun_unknown_attr *ca = (const pj_stun_unknown_attr*) a;
    pj_stun_unknown_attr *attr;
    pj_uint16_t *dst_unk_attr;
    unsigned i;

    /* Check that buffer is enough */
    if (len < ATTR_HDR_LEN + (ca->attr_count << 1))
	return PJ_ETOOSMALL;

    /* Copy to message */
    pj_memcpy(buf, ca, ATTR_HDR_LEN);

    /* Set the correct length */
    attr = (pj_stun_unknown_attr *) buf;
    attr->hdr.length = (pj_uint16_t)(ca->attr_count << 1);

    /* Convert to network byte order */
    attr->hdr.type = pj_htons(attr->hdr.type);
    attr->hdr.length = pj_htons(attr->hdr.length);

    /* Copy individual attribute */
    dst_unk_attr = (pj_uint16_t*)(buf + ATTR_HDR_LEN);
    for (i=0; i < ca->attr_count; ++i, ++dst_unk_attr) {
	*dst_unk_attr = pj_htons(attr->attrs[i]);
    }

    /* Done */
    *printed = (ATTR_HDR_LEN + (ca->attr_count << 1) + 3) & (~3);

    return PJ_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////
/*
 * STUN generic binary attribute
 */

/*
 * Create a blank binary attribute.
 */
PJ_DEF(pj_status_t)
pj_stun_binary_attr_create(pj_pool_t *pool,
			   int attr_type,
			   pj_stun_binary_attr **p_attr)
{
    pj_stun_binary_attr *attr;

    PJ_ASSERT_RETURN(pool && attr_type && p_attr, PJ_EINVAL);

    attr = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_binary_attr);
    INIT_ATTR(attr, attr_type, sizeof(pj_stun_binary_attr));

    *p_attr = attr;

    return PJ_SUCCESS;
}


static pj_status_t decode_binary_attr(pj_pool_t *pool, 
				      const pj_uint8_t *buf,
				      void **p_attr)
{
    pj_stun_binary_attr *attr;

    /* Create the attribute */
    attr = PJ_POOL_ALLOC_TYPE(pool, pj_stun_binary_attr);

    /* Copy the header */
    pj_memcpy(attr, buf, ATTR_HDR_LEN);

    /* Convert to host byte order */
    attr->hdr.type = pj_ntohs(attr->hdr.type);
    attr->hdr.length = pj_ntohs(attr->hdr.length);

    /* Copy the data to the attribute */
    attr->length = attr->hdr.length;
    attr->data = pj_pool_alloc(pool, attr->length);
    pj_memcpy(attr->data, buf+ATTR_HDR_LEN, attr->length);

    /* Done */
    *p_attr = attr;

    return PJ_SUCCESS;

}


static pj_status_t encode_binary_attr(const void *a, pj_uint8_t *buf, 
				      unsigned len, unsigned *printed)
{
    const pj_stun_binary_attr *ca = (const pj_stun_binary_attr*)a;
    pj_stun_attr_hdr *attr;

    /* Calculated total attr_len (add padding if necessary) */
    *printed = (ca->length + ATTR_HDR_LEN + 3) & (~3);
    if (len < *printed)
	return PJ_ETOOSMALL;

    /* Copy header */
    pj_memcpy(buf, a, ATTR_HDR_LEN);

    /* Set the correct length */
    attr = (pj_stun_attr_hdr*)buf;
    attr->length = (pj_uint16_t) ca->length;

    /* Convert to network byte order */
    attr->type = pj_htons(attr->type);
    attr->length = pj_htons(attr->length);

    /* Copy the data */
    pj_memcpy(buf+ATTR_HDR_LEN, ca->data, ca->length);

    /* Done */
    return PJ_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////


struct attr_desc
{
    unsigned  type;
    const char *attr_name;

    pj_status_t	(*decode_attr)(pj_pool_t *pool, const pj_uint8_t *buf, 
			       void **p_attr);
    pj_status_t (*encode_attr)(const void *a, pj_uint8_t *buf, 
			       unsigned len, unsigned *printed);

} attr_desc[] = 
{
    {
	PJ_STUN_ATTR_MAPPED_ADDR,
	"MAPPED-ADDRESS",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_RESPONSE_ADDR,
	"RESPONSE-ADDRESS",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_CHANGE_REQUEST,
	"CHANGE-REQUEST",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_SOURCE_ADDR,
	"SOURCE-ADDRESS",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_CHANGED_ADDR,
	"CHANGED-ADDRESS",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_USERNAME,
	"USERNAME",
	&decode_generic_string_attr,
	&encode_generic_string_attr
    },
    {
	PJ_STUN_ATTR_PASSWORD,
	"PASSWORD",
	&decode_generic_string_attr,
	&encode_generic_string_attr
    },
    {
	PJ_STUN_ATTR_MESSAGE_INTEGRITY,
	"MESSAGE-INTEGRITY",
	&decode_msg_integrity_attr,
	&encode_msg_integrity_attr
    },
    {
	PJ_STUN_ATTR_ERROR_CODE,
	"ERROR-CODE",
	&decode_error_code_attr,
	&encode_error_code_attr
    },
    {
	PJ_STUN_ATTR_UNKNOWN_ATTRIBUTES,
	"UNKNOWN-ATTRIBUTES",
	&decode_unknown_attr,
	&encode_unknown_attr
    },
    {
	PJ_STUN_ATTR_REFLECTED_FROM,
	"REFLECTED-FROM",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_REALM,
	"REALM",
	&decode_generic_string_attr,
	&encode_generic_string_attr
    },
    {
	PJ_STUN_ATTR_NONCE,
	"NONCE",
	&decode_generic_string_attr,
	&encode_generic_string_attr
    },
    {
	PJ_STUN_ATTR_XOR_MAPPED_ADDRESS,
	"XOR-MAPPED-ADDRESS",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_FINGERPRINT,
	"FINGERPRINT",
	&decode_generic_uint_attr,
	&encode_generic_uint_attr
    },
    {
	PJ_STUN_ATTR_SERVER,
	"SERVER",
	&decode_generic_string_attr,
	&encode_generic_string_attr
    },
    {
	PJ_STUN_ATTR_ALTERNATE_SERVER,
	"ALTERNATE-SERVER",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_REFRESH_INTERVAL,
	"REFRESH-INTERVAL",
	&decode_generic_uint_attr,
	&encode_generic_uint_attr
    },
    {
	PJ_STUN_ATTR_LIFETIME,
	"LIFETIME",
	&decode_generic_uint_attr,
	&encode_generic_uint_attr
    },
    {
	PJ_STUN_ATTR_BANDWIDTH,
	"BANDWIDTH",
	&decode_generic_uint_attr,
	&encode_generic_uint_attr
    },
    {
	PJ_STUN_ATTR_REMOTE_ADDRESS,
	"REMOTE-ADDRESS",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_DATA,
	"DATA",
	&decode_binary_attr,
	&encode_binary_attr
    },
    {
	PJ_STUN_ATTR_RELAY_ADDRESS,
	"RELAY-ADDRESS",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_REQUESTED_PORT_PROPS,
	"REQUESTED-PORT-PROPS",
	&decode_generic_uint_attr,
	&encode_generic_uint_attr
    },
    {
	PJ_STUN_ATTR_REQUESTED_TRANSPORT,
	"REQUESTED-TRANSPORT",
	&decode_generic_uint_attr,
	&encode_generic_uint_attr
    },
    {
	PJ_STUN_ATTR_REQUESTED_IP,
	"STUN REQUESTED-IP",
	&decode_generic_ip_addr_attr,
	&encode_generic_ip_addr_attr
    },
    {
	PJ_STUN_ATTR_TIMER_VAL,
	"TIMER-VAL",
	&decode_generic_uint_attr,
	&encode_generic_uint_attr
    }
};

static struct attr_desc *find_attr_desc(unsigned attr_type)
{
    unsigned i;

    for (i=0; i<PJ_ARRAY_SIZE(attr_desc); ++i) {
	if (attr_desc[i].type == attr_type)
	    return &attr_desc[i];
    }

    return NULL;
}



/*
 * Create a blank STUN message.
 */
PJ_DEF(pj_status_t) pj_stun_msg_create( pj_pool_t *pool,
					unsigned msg_type,
					const pj_uint8_t tsx_id[12],
					pj_stun_msg **p_msg)
{
    pj_stun_msg *msg;

    PJ_ASSERT_RETURN(pool && msg_type && p_msg, PJ_EINVAL);

    msg = PJ_POOL_ZALLOC_TYPE(pool, pj_stun_msg);
    msg->hdr.type = (pj_uint16_t) msg_type;
    msg->hdr.magic = (pj_uint32_t) PJ_STUN_MAGIC;

    if (tsx_id) {
	pj_memcpy(&msg->hdr.tsx_id, tsx_id, sizeof(msg->hdr.tsx_id));
    } else {
	struct transaction_id
	{
	    pj_uint32_t	    proc_id;
	    pj_uint32_t	    random;
	    pj_uint32_t	    counter;
	} id;
	static pj_uint32_t pj_stun_tsx_id_counter;

	id.proc_id = pj_getpid();
	id.random = pj_rand();
	id.counter = pj_stun_tsx_id_counter++;

	pj_memcpy(&msg->hdr.tsx_id, &id, sizeof(msg->hdr.tsx_id));
    }

    *p_msg = msg;

    return PJ_SUCCESS;
}


/*
 * Check that the PDU is potentially a valid STUN message.
 */
PJ_DEF(pj_status_t) pj_stun_msg_check(const void *pdu, unsigned pdu_len,
				      unsigned options)
{
    pj_stun_msg_hdr *hdr;

    PJ_ASSERT_RETURN(pdu && pdu_len > sizeof(pj_stun_msg_hdr), 
		     PJLIB_UTIL_ESTUNINMSGLEN);

    PJ_UNUSED_ARG(options);

    hdr = (pj_stun_msg_hdr*) pdu;

    /* First byte of STUN message is always 0x00 or 0x01. */
    if ((*(const char*)pdu) != 0x00 && (*(const char*)pdu) != 0x01)
	return PJ_FALSE;

    /* If magic is set, then there is great possibility that this is
     * a STUN message.
     */
    if (pj_ntohl(hdr->magic) == PJ_STUN_MAGIC)
	return PJ_TRUE;

    /* Check the PDU length */
    if (pj_ntohs(hdr->length) > pdu_len)
	return PJ_FALSE;

    /* Could be a STUN message */
    return PJ_TRUE;
}


/*
 * Parse incoming packet into STUN message.
 */
PJ_DEF(pj_status_t) pj_stun_msg_decode(pj_pool_t *pool,
				       const pj_uint8_t *pdu,
				       unsigned pdu_len,
				       unsigned options,
				       pj_stun_msg **p_msg,
				       unsigned *p_parsed_len,
				       unsigned *p_err_code,
				       unsigned *p_uattr_cnt,
				       pj_uint16_t uattr[])
{
    
    pj_stun_msg *msg;
    unsigned uattr_cnt;
    const pj_uint8_t *start_pdu = pdu;
    pj_status_t status;

    PJ_UNUSED_ARG(options);

    PJ_ASSERT_RETURN(pool && pdu && pdu_len && p_msg, PJ_EINVAL);
    PJ_ASSERT_RETURN(sizeof(pj_stun_msg_hdr) == 20, PJ_EBUG);

    /* Application should have checked that this is a valid STUN msg */
    PJ_ASSERT_RETURN((status=pj_stun_msg_check(pdu, pdu_len, options))
			== PJ_SUCCESS, status);

    /* Create the message, copy the header, and convert to host byte order */
    msg = PJ_POOL_ALLOC_TYPE(pool, pj_stun_msg);
    pj_memcpy(&msg->hdr, pdu, sizeof(pj_stun_msg_hdr));
    msg->hdr.type = pj_ntohs(msg->hdr.type);
    msg->hdr.length = pj_ntohs(msg->hdr.length);
    msg->hdr.magic = pj_ntohl(msg->hdr.magic);

    pdu += sizeof(pj_stun_msg_hdr);
    pdu_len -= sizeof(pj_stun_msg_hdr);

    /* Parse attributes */
    uattr_cnt = 0;
    while (pdu_len > 0) {
	unsigned attr_type, attr_val_len;
	struct attr_desc *adesc;

	/* Get attribute type and length. If length is not aligned
	 * to 4 bytes boundary, add padding.
	 */
	attr_type = pj_ntohs(*(pj_uint16_t*)pdu);
	attr_val_len = pj_ntohs(*(pj_uint16_t*)(pdu+2));
	attr_val_len = (attr_val_len + 3) & (~3);

	/* Get the attribute descriptor */
	adesc = find_attr_desc(attr_type);

	/* Check length */
	if (pdu_len < attr_val_len)
	    return PJLIB_UTIL_ESTUNINATTRLEN;

	if (adesc == NULL) {
	    /* Unrecognized attribute */

	    PJ_LOG(4,(THIS_FILE, "Unrecognized attribute type %d", 
		      attr_type));

	    /* Put to unrecognized attribute array */
	    if (p_uattr_cnt && uattr && uattr_cnt < *p_uattr_cnt) {
		uattr[uattr_cnt++] = (pj_uint16_t)attr_type;
	    }

	    /* Is this a fatal condition? */
	    if (attr_type <= 0x7FFF) {
		/* This is a mandatory attribute, we must return error
		 * if we don't understand the attribute.
		 */
		if (p_err_code)
		    *p_err_code = PJ_STUN_STATUS_UNKNOWN_ATTRIBUTE;

		return PJLIB_UTIL_ESTUNUNKNOWNATTR;
	    }

	} else {
	    void *attr;

	    /* Parse the attribute */
	    status = (adesc->decode_attr)(pool, pdu, &attr);

	    if (status != PJ_SUCCESS) {
		PJ_LOG(4,(THIS_FILE, 
			  "Error parsing STUN %s attribute: status=%d",
			  adesc->attr_name, status));
		return status;
	    }
	    
	    /* Make sure we have rooms for the new attribute */
	    if (msg->attr_count >= PJ_STUN_MAX_ATTR)
		return PJLIB_UTIL_ESTUNTOOMANYATTR;

	    /* Add the attribute */
	    msg->attr[msg->attr_count++] = (pj_stun_attr_hdr*)attr;
	}

	pdu += (attr_val_len + 4);
	pdu_len -= (attr_val_len + 4);
    }

    *p_msg = msg;

    if (p_uattr_cnt)
	*p_uattr_cnt = uattr_cnt;

    if (p_parsed_len)
	*p_parsed_len = (pdu - start_pdu);

    return PJ_SUCCESS;
}


/*
 * Print the message structure to a buffer.
 */
PJ_DEF(pj_status_t) pj_stun_msg_encode(const pj_stun_msg *msg,
				       pj_uint8_t *buf, unsigned buf_size,
				       unsigned options,
				       unsigned *p_msg_len)
{
    pj_stun_msg_hdr *hdr;
    pj_uint8_t *start = buf;
    unsigned i;

    PJ_ASSERT_RETURN(msg && buf && buf_size, PJ_EINVAL);

    PJ_UNUSED_ARG(options);

    /* Copy the message header part and convert the header fields to
     * network byte order
     */
    if (buf_size < sizeof(pj_stun_msg_hdr))
	return PJ_ETOOSMALL;
    pj_memcpy(buf, &msg->hdr, sizeof(pj_stun_msg_hdr));
    hdr = (pj_stun_msg_hdr*) buf;
    hdr->magic = pj_htonl(hdr->magic);
    hdr->type = pj_htons(hdr->type);
    /* We'll fill in the length later */

    buf += sizeof(pj_stun_msg_hdr);
    buf_size -= sizeof(pj_stun_msg_hdr);

    /* Print each attribute */
    for (i=0; i<msg->attr_count; ++i) {
	struct attr_desc *adesc;
	const pj_stun_attr_hdr *attr_hdr;
	unsigned printed;
	pj_status_t status;

	attr_hdr = msg->attr[i];

	adesc = find_attr_desc(attr_hdr->type);
	PJ_ASSERT_RETURN(adesc != NULL, PJ_EBUG);

	status = adesc->encode_attr(attr_hdr, buf, buf_size, &printed);
	if (status != PJ_SUCCESS)
	    return status;

	buf += printed;
	buf_size -= printed;
    }

    /* Update the message length in the header */
    hdr->length = pj_htons((pj_uint16_t)(buf - start));

    /* Done */
    if (p_msg_len)
	*p_msg_len = (buf - start);

    return PJ_SUCCESS;
}


/*
 * Find STUN attribute in the STUN message, starting from the specified
 * index.
 */
PJ_DEF(pj_stun_attr_hdr*) pj_stun_msg_find_attr( const pj_stun_msg *msg,
						 int attr_type,
						 unsigned index)
{
    PJ_ASSERT_RETURN(msg, NULL);

    for (; index < msg->attr_count; ++index) {
	if (msg->attr[index]->type == attr_type)
	    return (pj_stun_attr_hdr*) &msg->attr[index];
    }

    return NULL;
}

