/* $Id$
 */
#ifndef __PJSIP_AUTH_SIP_AUTH_PARSER_H__
#define __PJSIP_AUTH_SIP_AUTH_PARSER_H__

/**
 * @file pjsip_auth_parser.h
 * @brief SIP Authorization Parser Module.
 */

#include <pj/types.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJSIP_AUTH_PARSER_MODULE Authorization Parser Module
 * @ingroup PJSIP_AUTH
 * @{
 */

/**
 * Initialize and register authorization parser module.
 * This will register parser handler for various Authorization related headers
 * such as Authorization, WWW-Authenticate, Proxy-Authorizization, and 
 * Proxy-Authenticate headers.
 *
 * @return      PJ_SUCCESS or the appropriate status code.
 */
PJ_DECL(pj_status_t) pjsip_auth_init_parser(void);

/**
 * DeInitialize authorization parser module.
 */
PJ_DECL(void) pjsip_auth_deinit_parser();


extern const pj_str_t	pjsip_USERNAME_STR,
			pjsip_REALM_STR,
			pjsip_NONCE_STR,
			pjsip_URI_STR,
			pjsip_RESPONSE_STR,
			pjsip_ALGORITHM_STR,
			pjsip_DOMAIN_STR,
			pjsip_STALE_STR,
			pjsip_QOP_STR,
			pjsip_CNONCE_STR,
			pjsip_OPAQUE_STR,
			pjsip_NC_STR,
			pjsip_TRUE_STR,
			pjsip_FALSE_STR,
			pjsip_DIGEST_STR,
			pjsip_PGP_STR,
			pjsip_MD5_STR,
			pjsip_AUTH_STR;
/*
extern const pj_str_t	pjsip_QUOTED_TRUE_STR,
			pjsip_QUOTED_FALSE_STR,
			pjsip_QUOTED_DIGEST_STR,
			pjsip_QUOTED_PGP_STR,
			pjsip_QUOTED_MD5_STR,
			pjsip_QUOTED_AUTH_STR;
*/

/**
 * @}
 */

PJ_END_DECL

#endif	/* __PJSIP_AUTH_SIP_AUTH_PARSER_H__ */

