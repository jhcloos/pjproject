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
#ifndef __PJNATH_CONFIG_H__
#define __PJNATH_CONFIG_H__


/**
 * @file config.h
 * @brief Compile time settings
 */

#include <pj/types.h>

/**
 * @defgroup PJNATH_CONFIG Configuration
 * @brief Various compile time settings
 * @{
 */


/* **************************************************************************
 * GENERAL
 */

/**
 * The log level for PJNATH error display.
 *
 * default 1
 */
#ifndef PJNATH_ERROR_LEVEL
#   define PJNATH_ERROR_LEVEL			    1
#endif


/* **************************************************************************
 * STUN CLIENT CONFIGURATION
 */

/**
 * Maximum number of attributes in the STUN packet (for the new STUN
 * library).
 *
 * Default: 16
 */
#ifndef PJ_STUN_MAX_ATTR
#   define PJ_STUN_MAX_ATTR			    16
#endif

/**
 * The default initial STUN round-trip time estimation (the RTO value
 * in RFC 3489-bis), in miliseconds. 
 * This value is used to control the STUN request 
 * retransmit time. The initial value of retransmission interval 
 * would be set to this value, and will be doubled after each
 * retransmission.
 */
#ifndef PJ_STUN_RTO_VALUE
#   define PJ_STUN_RTO_VALUE			    100
#endif


/**
 * The STUN transaction timeout value, in miliseconds.
 * After the last retransmission is sent and if no response is received 
 * after this time, the STUN transaction will be considered to have failed.
 *
 * The default value is 1600 miliseconds (as per RFC 3489-bis).
 */
#ifndef PJ_STUN_TIMEOUT_VALUE
#   define PJ_STUN_TIMEOUT_VALUE		    1600
#endif


/**
 * Maximum number of STUN retransmission count.
 *
 * Default: 7 (as per RFC 3489-bis)
 */
#ifndef PJ_STUN_MAX_RETRANSMIT_COUNT
#   define PJ_STUN_MAX_RETRANSMIT_COUNT		    7
#endif


/**
 * Maximum size of STUN message.
 */
#ifndef PJ_STUN_MAX_PKT_LEN
#   define PJ_STUN_MAX_PKT_LEN			    512
#endif


/**
 * Default STUN port as defined by RFC 3489.
 */
#define PJ_STUN_PORT				    3478



/* **************************************************************************
 * ICE CONFIGURATION
 */

/**
 * Maximum number of ICE candidates.
 *
 * Default: 16
 */
#ifndef PJ_ICE_MAX_CAND
#   define PJ_ICE_MAX_CAND			    16
#endif


/**
 * Maximum number of candidates for each ICE stream transport component.
 *
 * Default: 8
 */
#ifndef PJ_ICE_ST_MAX_CAND
#   define PJ_ICE_ST_MAX_CAND			    8
#endif


/**
 * Maximum number of ICE components.
 *
 * Default: 8
 */
#ifndef PJ_ICE_MAX_COMP
#   define PJ_ICE_MAX_COMP			    8
#endif


/**
 * Maximum number of ICE checks.
 *
 * Default: 32
 */
#ifndef PJ_ICE_MAX_CHECKS
#   define PJ_ICE_MAX_CHECKS			    32
#endif


/**
 * Default timer interval (in miliseconds) for starting ICE periodic checks.
 *
 * Default: 20
 */
#ifndef PJ_ICE_TA_VAL
#   define PJ_ICE_TA_VAL			    20
#endif


/**
 * According to ICE Section 8.2. Updating States, if an In-Progress pair in 
 * the check list is for the same component as a nominated pair, the agent 
 * SHOULD cease retransmissions for its check if its pair priority is lower
 * than the lowest priority nominated pair for that component.
 *
 * If a higher priority check is In Progress, this rule would cause that
 * check to be performed even when it most likely will fail.
 *
 * The macro here controls if ICE session should cancel all In Progress 
 * checks for the same component regardless of its priority.
 *
 * Default: 1 (yes, cancel all)
 */
#ifndef PJ_ICE_CANCEL_ALL
#   define PJ_ICE_CANCEL_ALL			    1
#endif


/**
 * Minimum interval value to be used for sending STUN keep-alive on the ICE
 * stream transport, in seconds. This minimum interval, plus a random value
 * which maximum is PJ_ICE_ST_KEEP_ALIVE_MAX_RAND, specify the actual interval
 * of the STUN keep-alive.
 *
 * Default: 20 seconds
 *
 * @see PJ_ICE_ST_KEEP_ALIVE_MAX_RAND
 */
#ifndef PJ_ICE_ST_KEEP_ALIVE_MIN
#   define PJ_ICE_ST_KEEP_ALIVE_MIN		    20
#endif


/**
 * To prevent STUN keep-alives to be sent simultaneously, application should
 * add random interval to minimum interval (PJ_ICE_ST_KEEP_ALIVE_MIN). This
 * setting specifies the maximum random value to be added to the minimum
 * interval, in seconds.
 *
 * Default: 5 seconds
 *
 * @see PJ_ICE_ST_KEEP_ALIVE_MIN
 */
#ifndef PJ_ICE_ST_KEEP_ALIVE_MAX_RAND
#   define PJ_ICE_ST_KEEP_ALIVE_MAX_RAND	    5
#endif



/**
 * @}
 */

#endif	/* __PJNATH_CONFIG_H__ */

