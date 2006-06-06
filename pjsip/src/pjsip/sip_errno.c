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
#include <pjsip/sip_errno.h>
#include <pjsip/sip_msg.h>
#include <pj/string.h>
#include <pj/errno.h>

/* PJSIP's own error codes/messages 
 * MUST KEEP THIS ARRAY SORTED!!
 */

#if defined(PJ_HAS_ERROR_STRING) && (PJ_HAS_ERROR_STRING != 0)

static const struct 
{
    int code;
    const char *msg;
} err_str[] = 
{
    /* Generic SIP errors */
    { PJSIP_EBUSY,		"Object is busy" },
    { PJSIP_ETYPEEXISTS ,	"Object with the same type exists" },
    { PJSIP_ESHUTDOWN,		"SIP stack shutting down" },
    { PJSIP_ENOTINITIALIZED,	"SIP object is not initialized." },

    /* Messaging errors */
    { PJSIP_EINVALIDMSG,	"Invalid message/syntax error" },
    { PJSIP_ENOTREQUESTMSG,	"Expecting request message"},
    { PJSIP_ENOTRESPONSEMSG,	"Expecting response message"},
    { PJSIP_EMSGTOOLONG,	"Message too long" },
    { PJSIP_EPARTIALMSG,	"Partial message" },

    { PJSIP_EINVALIDSTATUS,	"Invalid/unexpected SIP status code"},

    { PJSIP_EINVALIDURI,	"Invalid URI" },
    { PJSIP_EINVALIDSCHEME,	"Invalid URI scheme" },
    { PJSIP_EMISSINGREQURI,	"Missing Request-URI" },
    { PJSIP_EINVALIDREQURI,	"Invalid Request URI" },
    { PJSIP_EURITOOLONG,	"URI is too long" }, 

    { PJSIP_EMISSINGHDR,	"Missing required header(s)" },
    { PJSIP_EINVALIDHDR,	"Invalid header field"},
    { PJSIP_EINVALIDVIA,	"Invalid Via header" },
    { PJSIP_EMULTIPLEVIA,	"Multiple Via headers in response" },

    { PJSIP_EMISSINGBODY,	"Missing message body" },
    { PJSIP_EINVALIDMETHOD,	"Invalid/unexpected method" },

    /* Transport errors */
    { PJSIP_EUNSUPTRANSPORT,	"Unsupported transport"},
    { PJSIP_EPENDINGTX,		"Transmit buffer already pending"},
    { PJSIP_ERXOVERFLOW,	"Rx buffer overflow"},
    { PJSIP_EBUFDESTROYED,	"Buffer destroyed"},

    /* Transaction errors */
    { PJSIP_ETSXDESTROYED,	"Transaction has been destroyed"},
    { PJSIP_ENOTSX,		"No transaction is associated with the object "
			        "(expecting stateful processing)" },

    /* URI comparison status */
    { PJSIP_ECMPSCHEME,		"URI scheme mismatch" },
    { PJSIP_ECMPUSER,		"URI user part mismatch" },
    { PJSIP_ECMPPASSWD,		"URI password part mismatch" },
    { PJSIP_ECMPHOST,		"URI host part mismatch" },
    { PJSIP_ECMPPORT,		"URI port mismatch" },
    { PJSIP_ECMPTRANSPORTPRM,	"URI transport param mismatch" },
    { PJSIP_ECMPTTLPARAM,	"URI ttl param mismatch" },
    { PJSIP_ECMPUSERPARAM,	"URI user param mismatch" },
    { PJSIP_ECMPMETHODPARAM,	"URI method param mismatch" },
    { PJSIP_ECMPMADDRPARAM,	"URI maddr param mismatch" },
    { PJSIP_ECMPOTHERPARAM,	"URI other param mismatch" },
    { PJSIP_ECMPHEADERPARAM,	"URI header parameter mismatch" },

    /* Authentication. */
    { PJSIP_EFAILEDCREDENTIAL,	"Credential failed to authenticate"},
    { PJSIP_ENOCREDENTIAL,	"No suitable credential"},
    { PJSIP_EINVALIDALGORITHM,	"Invalid/unsupported digest algorithm" },
    { PJSIP_EINVALIDQOP,	"Invalid/unsupported digest qop" },
    { PJSIP_EINVALIDAUTHSCHEME,	"Unsupported authentication scheme" },
    { PJSIP_EAUTHNOPREVCHAL,	"No previous challenge" },
    { PJSIP_EAUTHNOAUTH,	"No suitable authorization header" },
    { PJSIP_EAUTHACCNOTFOUND,	"Account or credential not found" },
    { PJSIP_EAUTHACCDISABLED,	"Account or credential is disabled" },
    { PJSIP_EAUTHINVALIDREALM,	"Invalid authorization realm"},
    { PJSIP_EAUTHINVALIDDIGEST,	"Invalid authorization digest" },

    /* UA/dialog layer. */
    { PJSIP_EMISSINGTAG,	"Missing From/To tag parameter" },
    { PJSIP_ENOTREFER,		"Expecting REFER request"} ,
    { PJSIP_ENOREFERSESSION,	"Not associated with REFER subscription"},

    /* Invite session. */
    { PJSIP_ESESSIONTERMINATED,	"INVITE session already terminated" },
    { PJSIP_ESESSIONSTATE,      "Invalid INVITE session state" },
};


#endif	/* PJ_HAS_ERROR_STRING */


/*
 * pjsip_strerror()
 */
PJ_DEF(pj_str_t) pjsip_strerror( pj_status_t statcode, 
				 char *buf, pj_size_t bufsize )
{
    pj_str_t errstr;

#if defined(PJ_HAS_ERROR_STRING) && (PJ_HAS_ERROR_STRING != 0)

    if (statcode >= PJSIP_ERRNO_START && statcode < PJSIP_ERRNO_START+800) 
    {
	/* Status code. */
	const pj_str_t *status_text = 
	    pjsip_get_status_text(PJSIP_ERRNO_TO_SIP_STATUS(statcode));

	errstr.ptr = buf;
	pj_strncpy_with_null(&errstr, status_text, bufsize);
	return errstr;
    }
    else if (statcode >= PJSIP_ERRNO_START_PJSIP && 
	     statcode < PJSIP_ERRNO_START_PJSIP + 1000)
    {
	/* Find the error in the table.
	 * Use binary search!
	 */
	int first = 0;
	int n = PJ_ARRAY_SIZE(err_str);

	while (n > 0) {
	    int half = n/2;
	    int mid = first + half;

	    if (err_str[mid].code < statcode) {
		first = mid+1;
		n -= (half+1);
	    } else if (err_str[mid].code > statcode) {
		n = half;
	    } else {
		first = mid;
		break;
	    }
	}


	if (PJ_ARRAY_SIZE(err_str) && err_str[first].code == statcode) {
	    pj_str_t msg;
	    
	    msg.ptr = (char*)err_str[first].msg;
	    msg.slen = pj_ansi_strlen(err_str[first].msg);

	    errstr.ptr = buf;
	    pj_strncpy_with_null(&errstr, &msg, bufsize);
	    return errstr;

	} 
    }

#endif	/* PJ_HAS_ERROR_STRING */

    /* Error not found. */
    errstr.ptr = buf;
    errstr.slen = pj_ansi_snprintf(buf, bufsize, 
				   "Unknown pjsip error %d",
				   statcode);

    return errstr;

}

