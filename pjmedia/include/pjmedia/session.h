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
#ifndef __PJMEDIA_SESSION_H__
#define __PJMEDIA_SESSION_H__


/**
 * @file session.h
 * @brief Media Session.
 */

#include <pjmedia/endpoint.h>
#include <pjmedia/stream.h>
#include <pjmedia/sdp.h>

PJ_BEGIN_DECL 

/**
 * @defgroup PJMED_SES Media session
 * @ingroup PJMEDIA
 * @{
 *
 * A media session represents multimedia communication between two
 * parties. A media session represents the multimedia session that
 * is described by SDP session descriptor. A media session consists 
 * of one or more media streams (pjmedia_stream), where each stream 
 * represents one media line (m= line) in SDP.
 *
 * This module provides functions to create and manage multimedia
 * sessions.
 *
 * Application creates the media session by calling #pjmedia_session_create(),
 * normally after it has completed negotiating both SDP offer and answer.
 * The session creation function creates the media session (including
 * media streams) based on the content of local and remote SDP.
 */


/**
 * Session info, retrieved from a session by calling
 * #pjmedia_session_get_info().
 */
struct pjmedia_session_info
{
    /** Number of streams. */
    unsigned		stream_cnt;

    /** Individual stream info. */
    pjmedia_stream_info	stream_info[PJMEDIA_MAX_SDP_MEDIA];
};


/**
 * Initialize stream info from SDP media lines.
 *
 * @param si		Stream info structure to be initialized.
 * @param pool		Pool.
 * @param endpt		Pjmedia endpoint.
 * @param local		Local SDP session descriptor.
 * @param remote	Remote SDP session descriptor.
 * @param stream_idx	Media stream index in the session descriptor.
 *
 * @return		PJ_SUCCESS if stream info is successfully initialized.
 */
PJ_DECL(pj_status_t) pjmedia_stream_info_from_sdp( 
					   pjmedia_stream_info *si,
					   pj_pool_t *pool,
					   pjmedia_endpt *endpt,
					   const pjmedia_sdp_session *local,
					   const pjmedia_sdp_session *remote,
					   unsigned stream_idx);


/**
 * Create media session based on the local and remote SDP.
 * The session will start immediately.
 *
 * @param endpt		The PJMEDIA endpoint instance.
 * @param stream_cnt	Maximum number of streams to be created. This
 *			also denotes the number of elements in the
 *			socket information.
 * @param skinfo	Array of socket informations. The argument stream_cnt
 *			specifies the number of elements in this array. One
 *			element is needed for each media stream to be
 *			created in the session.
 * @param local_sdp	The SDP describing local capability.
 * @param rem_sdp	The SDP describing remote capability.
 * @param user_data	Arbitrary user data to be kept in the session.
 * @param p_session	Pointer to receive the media session.
 *
 * @return		PJ_SUCCESS if media session can be created 
 *			successfully.
 */
PJ_DECL(pj_status_t) 
pjmedia_session_create( pjmedia_endpt *endpt, 
			unsigned stream_cnt,
			const pjmedia_sock_info skinfo[],
			const pjmedia_sdp_session *local_sdp,
			const pjmedia_sdp_session *rem_sdp,
			void *user_data,
			pjmedia_session **p_session );


/**
 * Get session info.
 *
 * @param session	The session which info is being queried.
 * @param info		Pointer to receive session info.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_session_get_info( pjmedia_session *session,
					       pjmedia_session_info *info );


/**
 * Activate all streams in media session for the specified direction.
 *
 * @param session	The media session.
 * @param dir		The direction to activate.
 *
 * @return		PJ_SUCCESS if success.
 */
PJ_DECL(pj_status_t) pjmedia_session_resume(pjmedia_session *session,
					    pjmedia_dir dir);


/**
 * Suspend receipt and transmission of all streams in media session
 * for the specified direction.
 *
 * @param session	The media session.
 * @param dir		The media direction to suspend.
 *
 * @return		PJ_SUCCESS if success.
 */
PJ_DECL(pj_status_t) pjmedia_session_pause(pjmedia_session *session,
					   pjmedia_dir dir);

/**
 * Suspend receipt and transmission of individual stream in media session
 * for the specified direction.
 *
 * @param session	The media session.
 * @param index		The stream index.
 * @param dir		The media direction to pause.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_session_pause_stream( pjmedia_session *session,
						   unsigned index,
						   pjmedia_dir dir);

/**
 * Activate individual stream in media session for the specified direction.
 *
 * @param session	The media session.
 * @param index		The stream index.
 * @param dir		The media direction to activate.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_session_resume_stream(pjmedia_session *session,
						   unsigned index,
						   pjmedia_dir dir);

/**
 * Enumerate media streams in the session.
 *
 * @param session	The media session.
 * @param count		On input, specifies the number of elements in
 *			the array. On output, the number will be filled
 *			with number of streams in the session.
 * @param strm_info	Array of stream info.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_session_enum_streams( const pjmedia_session *session,
			      unsigned *count, 
			      pjmedia_stream_info strm_info[]);


/**
 * Get the port interface for the specified stream.
 */
PJ_DECL(pj_status_t) pjmedia_session_get_port( pjmedia_session *session,
					       unsigned index,
					       pjmedia_port **p_port);


/**
 * Get session statistics. The stream statistic shows various
 * indicators such as packet count, packet lost, jitter, delay, etc.
 *
 * @param session	The media session.
 * @param index		Stream index.
 * @param sta		Stream statistic.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_session_get_stream_stat(pjmedia_session *session,
						     unsigned index,
						     pjmedia_rtcp_stat *stat);

/**
 * Dial DTMF digit to the stream, using RFC 2833 mechanism.
 *
 * @param session	The media session.
 * @param index		The stream index.
 * @param ascii_digits	String of ASCII digits (i.e. 0-9*#A-B).
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_session_dial_dtmf( pjmedia_session *session,
					        unsigned index,
						const pj_str_t *ascii_digits );


/**
 * Check if the specified stream has received DTMF digits.
 *
 * @param session	The media session.
 * @param index		The stream index.
 *
 * @return		Non-zero (PJ_TRUE) if the stream has DTMF digits.
 */
PJ_DECL(pj_status_t) pjmedia_session_check_dtmf( pjmedia_session *session,
					         unsigned index);


/**
 * Retrieve DTMF digits from the specified stream.
 *
 * @param session	The media session.
 * @param index		The stream index.
 * @param ascii_digits	Buffer to receive the digits. The length of this
 *			buffer is indicated in the "size" argument.
 * @param size		On input, contains the maximum digits to be copied
 *			to the buffer.
 *			On output, it contains the actual digits that has
 *			been copied to the buffer.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_session_get_dtmf( pjmedia_session *session,
					       unsigned index,
					       char *ascii_digits,
					       unsigned *size );

/**
 * Destroy media session.
 *
 * @param session	The media session.
 *
 * @return		PJ_SUCCESS if success.
 */
PJ_DECL(pj_status_t) pjmedia_session_destroy(pjmedia_session *session);



/**
 * @}
 */

PJ_END_DECL

#endif	/* __PJMEDIA_SESSION_H__ */
