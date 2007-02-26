/* $Id$ */
/* 
 * Copyright (C)2003-2007 Benny Prijono <benny@prijono.org>
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
#ifndef __PJLIB_UTIL_ERRNO_H__
#define __PJLIB_UTIL_ERRNO_H__


#include <pj/errno.h>

/**
 * @defgroup PJLIB_UTIL_ERROR PJLIB-UTIL Error Codes
 * @ingroup PJLIB_UTIL
 * @{
 */

/**
 * Start of error code relative to PJ_ERRNO_START_USER.
 * This value is 320000.
 */
#define PJLIB_UTIL_ERRNO_START    (PJ_ERRNO_START_USER + PJ_ERRNO_SPACE_SIZE*3)


/************************************************************
 * STUN ERROR
 ***********************************************************/
/**
 * @hideinitializer
 * Unable to resolve STUN server
 */
#define PJLIB_UTIL_ESTUNRESOLVE	    (PJLIB_UTIL_ERRNO_START+1)	/* 320001 */
/**
 * @hideinitializer
 * Unknown STUN message type.
 */
#define PJLIB_UTIL_ESTUNINMSGTYPE   (PJLIB_UTIL_ERRNO_START+2)	/* 320002 */
/**
 * @hideinitializer
 * Invalid STUN message length
 */
#define PJLIB_UTIL_ESTUNINMSGLEN    (PJLIB_UTIL_ERRNO_START+3)	/* 320003 */
/**
 * @hideinitializer
 * Invalid STUN attribute length
 */
#define PJLIB_UTIL_ESTUNINATTRLEN   (PJLIB_UTIL_ERRNO_START+4)	/* 320004 */
/**
 * @hideinitializer
 * Invalid STUN attribute type
 */
#define PJLIB_UTIL_ESTUNINATTRTYPE  (PJLIB_UTIL_ERRNO_START+5)	/* 320005 */
/**
 * @hideinitializer
 * Invalid STUN server/socket index
 */
#define PJLIB_UTIL_ESTUNININDEX     (PJLIB_UTIL_ERRNO_START+6)	/* 320006 */
/**
 * @hideinitializer
 * No STUN binding response in the message
 */
#define PJLIB_UTIL_ESTUNNOBINDRES   (PJLIB_UTIL_ERRNO_START+7)	/* 320007 */
/**
 * @hideinitializer
 * Received STUN error attribute
 */
#define PJLIB_UTIL_ESTUNRECVERRATTR (PJLIB_UTIL_ERRNO_START+8)	/* 320008 */
/**
 * @hideinitializer
 * No STUN mapped address attribute
 */
#define PJLIB_UTIL_ESTUNNOMAP       (PJLIB_UTIL_ERRNO_START+9)	/* 320009 */
/**
 * @hideinitializer
 * Received no response from STUN server
 */
#define PJLIB_UTIL_ESTUNNOTRESPOND  (PJLIB_UTIL_ERRNO_START+10)	/* 320010 */
/**
 * @hideinitializer
 * Symetric NAT detected by STUN
 */
#define PJLIB_UTIL_ESTUNSYMMETRIC   (PJLIB_UTIL_ERRNO_START+11)	/* 320011 */
/**
 * @hideinitializer
 * Invalid STUN magic value
 */
#define PJLIB_UTIL_ESTUNNOTMAGIC    (PJLIB_UTIL_ERRNO_START+12)	/* 320012 */
/**
 * @hideinitializer
 * Invalid STUN fingerprint value
 */
#define PJLIB_UTIL_ESTUNFINGERPRINT (PJLIB_UTIL_ERRNO_START+13)	/* 320013 */



/************************************************************
 * XML ERROR
 ***********************************************************/
/**
 * @hideinitializer
 * General invalid XML message.
 */
#define PJLIB_UTIL_EINXML	    (PJLIB_UTIL_ERRNO_START+20)	/* 320020 */



/************************************************************
 * DNS ERROR
 ***********************************************************/
/**
 * @hideinitializer
 * DNS query packet buffer is too small.
 * This error occurs when the user supplied buffer for creating DNS
 * query (#pj_dns_make_query() function) is too small.
 */
#define PJLIB_UTIL_EDNSQRYTOOSMALL  (PJLIB_UTIL_ERRNO_START+40)	/* 320040 */
/**
 * @hideinitializer
 * Invalid DNS packet length.
 * This error occurs when the received DNS response packet does not
 * match all the fields length.
 */
#define PJLIB_UTIL_EDNSINSIZE	    (PJLIB_UTIL_ERRNO_START+41)	/* 320041 */
/**
 * @hideinitializer
 * Invalid DNS class.
 * This error occurs when the received DNS response contains network
 * class other than IN (Internet).
 */
#define PJLIB_UTIL_EDNSINCLASS	    (PJLIB_UTIL_ERRNO_START+42)	/* 320042 */
/**
 * @hideinitializer
 * Invalid DNS name pointer.
 * This error occurs when parsing the compressed names inside DNS
 * response packet, when the name pointer points to an invalid address
 * or the parsing has triggerred too much recursion.
 */
#define PJLIB_UTIL_EDNSINNAMEPTR    (PJLIB_UTIL_ERRNO_START+43)	/* 320043 */
/**
 * @hideinitializer
 * Invalid DNS nameserver address. If hostname was specified for nameserver
 * address, this error means that the function was unable to resolve
 * the nameserver hostname.
 */
#define PJLIB_UTIL_EDNSINNSADDR	    (PJLIB_UTIL_ERRNO_START+44)	/* 320044 */
/**
 * @hideinitializer
 * No nameserver is in DNS resolver. No nameserver is configured in the 
 * resolver.
 */
#define PJLIB_UTIL_EDNSNONS	    (PJLIB_UTIL_ERRNO_START+45)	/* 320045 */
/**
 * @hideinitializer
 * No working DNS nameserver. All nameservers have been queried,
 * but none was able to serve any DNS requests. These "bad" nameservers
 * will be re-tested again for "goodness" after some period.
 */
#define PJLIB_UTIL_EDNSNOWORKINGNS  (PJLIB_UTIL_ERRNO_START+46)	/* 320046 */
/**
 * @hideinitializer
 * No answer record in the DNS response.
 */
#define PJLIB_UTIL_EDNSNOANSWERREC  (PJLIB_UTIL_ERRNO_START+47)	/* 320047 */


/* DNS ERRORS MAPPED FROM RCODE: */

/**
 * Start of error code mapped from DNS RCODE
 */
#define PJLIB_UTIL_DNS_RCODE_START  (PJLIB_UTIL_ERRNO_START+50)	/* 320050 */

/**
 * Map DNS RCODE status into pj_status_t.
 */
#define PJ_STATUS_FROM_DNS_RCODE(rcode)	(rcode==0 ? PJ_SUCCESS : \
					 PJLIB_UTIL_DNS_RCODE_START+rcode)
/**
 * @hideinitializer
 * Format error - The name server was unable to interpret the query.
 * This corresponds to DNS RCODE 1.
 */
#define PJLIB_UTIL_EDNS_FORMERR	    PJ_STATUS_FROM_DNS_RCODE(1)	/* 320051 */
/**
 * @hideinitializer
 * Server failure - The name server was unable to process this query due to a
 * problem with the name server.
 * This corresponds to DNS RCODE 2.
 */
#define PJLIB_UTIL_EDNS_SERVFAIL    PJ_STATUS_FROM_DNS_RCODE(2)	/* 320052 */
/**
 * @hideinitializer
 * Name Error - Meaningful only for responses from an authoritative name
 * server, this code signifies that the domain name referenced in the query 
 * does not exist.
 * This corresponds to DNS RCODE 3.
 */
#define PJLIB_UTIL_EDNS_NXDOMAIN    PJ_STATUS_FROM_DNS_RCODE(3)	/* 320053 */
/**
 * @hideinitializer
 * Not Implemented - The name server does not support the requested kind of 
 * query.
 * This corresponds to DNS RCODE 4.
 */
#define PJLIB_UTIL_EDNS_NOTIMPL    PJ_STATUS_FROM_DNS_RCODE(4)	/* 320054 */
/**
 * @hideinitializer
 * Refused - The name server refuses to perform the specified operation for
 * policy reasons.
 * This corresponds to DNS RCODE 5.
 */
#define PJLIB_UTIL_EDNS_REFUSED	    PJ_STATUS_FROM_DNS_RCODE(5)	/* 320055 */
/**
 * @hideinitializer
 * The name exists.
 * This corresponds to DNS RCODE 6.
 */
#define PJLIB_UTIL_EDNS_YXDOMAIN    PJ_STATUS_FROM_DNS_RCODE(6)	/* 320056 */
/**
 * @hideinitializer
 * The RRset (name, type) exists.
 * This corresponds to DNS RCODE 7.
 */
#define PJLIB_UTIL_EDNS_YXRRSET	    PJ_STATUS_FROM_DNS_RCODE(7)	/* 320057 */
/**
 * @hideinitializer
 * The RRset (name, type) does not exist.
 * This corresponds to DNS RCODE 8.
 */
#define PJLIB_UTIL_EDNS_NXRRSET	    PJ_STATUS_FROM_DNS_RCODE(8)	/* 320058 */
/**
 * @hideinitializer
 * The requestor is not authorized to perform this operation.
 * This corresponds to DNS RCODE 9.
 */
#define PJLIB_UTIL_EDNS_NOTAUTH	    PJ_STATUS_FROM_DNS_RCODE(9)	/* 320059 */
/**
 * @hideinitializer
 * The zone specified is not a zone.
 * This corresponds to DNS RCODE 10.
 */
#define PJLIB_UTIL_EDNS_NOTZONE	    PJ_STATUS_FROM_DNS_RCODE(10)/* 320060 */


/************************************************************
 * NEW STUN ERROR
 ***********************************************************/
/* Messaging errors */
/**
 * @hideinitializer
 * Invalid STUN attribute
 */
#define PJLIB_UTIL_ESTUNINATTR	    (PJLIB_UTIL_ERRNO_START+110)/* 320110 */
/**
 * @hideinitializer
 * Too many STUN attributes.
 */
#define PJLIB_UTIL_ESTUNTOOMANYATTR (PJLIB_UTIL_ERRNO_START+111)/* 320111 */
/**
 * @hideinitializer
 * Unknown STUN attribute.
 */
#define PJLIB_UTIL_ESTUNUNKNOWNATTR (PJLIB_UTIL_ERRNO_START+112)/* 320112 */
/**
 * @hideinitializer
 * Invalid STUN socket address length.
 */
#define PJLIB_UTIL_ESTUNINADDRLEN   (PJLIB_UTIL_ERRNO_START+113)/* 320113 */
/**
 * @hideinitializer
 * STUN IPv6 attribute not supported
 */
#define PJLIB_UTIL_ESTUNIPV6NOTSUPP (PJLIB_UTIL_ERRNO_START+113)/* 320113 */
/**
 * @hideinitializer
 * Expecting STUN response message.
 */
#define PJLIB_UTIL_ESTUNNOTRESPONSE (PJLIB_UTIL_ERRNO_START+114)/* 320114 */
/**
 * @hideinitializer
 * STUN transaction ID mismatch.
 */
#define PJLIB_UTIL_ESTUNINVALIDID   (PJLIB_UTIL_ERRNO_START+115)/* 320115 */
/**
 * @hideinitializer
 * Unable to find handler for the request.
 */
#define PJLIB_UTIL_ESTUNNOHANDLER   (PJLIB_UTIL_ERRNO_START+116)/* 320116 */
/**
 * @hideinitializer
 * Invalid STUN MESSAGE-INTEGRITY attribute position in message.
 * STUN MESSAGE-INTEGRITY must be put last in the message, or before
 * FINGERPRINT attribute.
 */
#define PJLIB_UTIL_ESTUNMSGINT	    (PJLIB_UTIL_ERRNO_START+117)/* 320117 */
/**
 * @hideinitializer
 * Missing STUN USERNAME attribute.
 * When credential is included in the STUN message (MESSAGE-INTEGRITY is
 * present), the USERNAME attribute must be present in the message.
 */
#define PJLIB_UTIL_ESTUNNOUSERNAME  (PJLIB_UTIL_ERRNO_START+118)/* 320118 */


#define PJ_STATUS_FROM_STUN_CODE(code)	(PJLIB_UTIL_ERRNO_START+code)




/**
 * @}
 */

#endif	/* __PJLIB_UTIL_ERRNO_H__ */
