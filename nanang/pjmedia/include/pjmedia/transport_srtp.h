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
#ifndef __PJMEDIA_TRANSPORT_SRTP_H__
#define __PJMEDIA_TRANSPORT_SRTP_H__

/**
 * @file srtp.h
 * @brief transport SRTP encapsulates secure media transport.
 */

#include <pjmedia/transport.h>


PJ_BEGIN_DECL

/**
 * Options that can be specified when creating SRTP transport.
 */
enum pjmedia_transport_srtp_options
{
    /**
     * This option will make the underlying transport to be closed whenever
     * the SRTP transport is closed.
     */
    PJMEDIA_SRTP_AUTO_CLOSE_UNDERLYING_TRANSPORT = 1
};

/**
 * SRTP session parameters.
 */
typedef struct pjmedia_srtp_stream_policy
{
    pj_str_t key;	     /**< Key string.		    */
    pj_str_t crypto_suite;   /**< SRTP parameter for RTP.   */
} pjmedia_srtp_stream_policy;


/**
 * Create an SRTP media transport.
 *
 * @param endpt		    The media endpoint instance.
 * @param tp		    The actual media transport 
 *			    to send and receive RTP/RTCP packets.
 * @param p_tp_srtp	    Pointer to receive the transport SRTP instance.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_transport_srtp_create(pjmedia_endpt *endpt,
					 pjmedia_transport *tp,
					 unsigned options,
					 pjmedia_transport **p_tp_srtp);

/**
 * Initialize and start SRTP session with the given parameters.
 * Please note:
 * 1. pjmedia_transport_srtp_init_session() and 
 *    pjmedia_transport_srtp_deinit_session() is automatic called by 
 *    SRTP pjmedia_transport_media_start() and pjmedia_transport_media_stop(),
 *    application needs to call these functions directly only if the application
 *    is not intended to call SRTP pjmedia_transport_media_start.
 * 2. Even if an RTP stream is only one direction, you might need to provide 
 *    both policies, because it is needed by RTCP, which is usually two 
 *    directions.
 * 3. Key for transmit and receive direction MUST be different, this is
 *    specified by libsrtp.
 *
 * @param srtp	    The SRTP transport.
 * @param prm	    Session parameters.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_transport_srtp_init_session(
		       pjmedia_transport *srtp,
		       const pjmedia_srtp_stream_policy *policy_tx,
		       const pjmedia_srtp_stream_policy *policy_rx);

/**
 * Stop SRTP seession.
 *
 * @param srtp	    The SRTP media transport.
 *
 * @return	    PJ_SUCCESS on success.
 *
 * @see pjmedia_transport_srtp_init_session() 
 */
PJ_DECL(pj_status_t) pjmedia_transport_srtp_deinit_session(
		       pjmedia_transport *srtp);


/**
 * Query real transport of SRTP.
 *
 * @param srtp		    The SRTP media transport.
 *
 * @return		    real media transport.
 */
PJ_DECL(pjmedia_transport*) pjmedia_transport_srtp_get_real_transport(
				pjmedia_transport *srtp);


PJ_END_DECL

#endif /* __PJMEDIA_TRANSPORT_SRTP_H__ */
