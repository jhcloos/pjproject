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
#ifndef __PJMEDIA_MEM_PORT_H__
#define __PJMEDIA_MEM_PORT_H__

/**
 * @file mem_port.h
 * @brief Memory based media playback/capture port
 */
#include <pjmedia/port.h>

PJ_BEGIN_DECL


/**
 * @defgroup PJMEDIA_MEM_PLAYER Memory/Buffer-based Playback Port
 * @ingroup PJMEDIA_PORT
 * @brief Media playback from a fixed buffer
 * @{
 * A memory/buffer based playback port is used to play media from a fixed
 * size buffer. This is useful over @ref PJMEDIA_FILE_PLAY for 
 * situation where filesystems are not available in the target system.
 */

/**
 * Create the buffer based playback to play the media from the specified
 * buffer.
 *
 * @param pool		    Pool to allocate memory for the port structure.
 * @param buffer	    The buffer to play the media from, which should
 *			    be available throughout the life time of the port.
 *			    The player plays the media directly from this
 *			    buffer (i.e. no copying is done).
 * @param size		    The size of the buffer, in bytes.
 * @param clock_rate	    Sampling rate.
 * @param channel_count	    Number of channels.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Number of bits per sample.
 * @param options	    Option flags.
 * @param p_port	    Pointer to receive the port instance.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate
 *			    error code.
 */
PJ_DECL(pj_status_t) pjmedia_mem_player_create(pj_pool_t *pool,
					       const void *buffer,
					       pj_size_t size,
					       unsigned clock_rate,
					       unsigned channel_count,
					       unsigned samples_per_frame,
					       unsigned bits_per_sample,
					       unsigned options,
					       pjmedia_port **p_port );


/**
 * @}
 */

/**
 * @defgroup PJMEDIA_MEM_CAPTURE Memory/Buffer-based Capture Port
 * @ingroup PJMEDIA_PORT
 * @brief Capture to fixed size buffer
 * @{
 * A memory based capture is used to save media streams to a fixed size
 * buffer. This is useful over @ref PJMEDIA_FILE_REC for 
 * situation where filesystems are not available in the target system.
 */

/**
 * Create media port to capture/record media into a fixed size buffer.
 *
 * @param pool		    Pool to allocate memory for the port structure.
 * @param buffer	    The buffer to record the media to, which should
 *			    be available throughout the life time of the port.
 * @param size		    The maximum size of the buffer, in bytes.
 * @param clock_rate	    Sampling rate.
 * @param channel_count	    Number of channels.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Number of bits per sample.
 * @param options	    Option flags.
 * @param p_port	    Pointer to receive the port instance.
 *
 * @return		    PJ_SUCCESS on success, or the appropriate
 *			    error code.
 */
PJ_DECL(pj_status_t) pjmedia_mem_capture_create(pj_pool_t *pool,
						void *buffer,
						pj_size_t size,
						unsigned clock_rate,
						unsigned channel_count,
						unsigned samples_per_frame,
						unsigned bits_per_sample,
						unsigned options,
						pjmedia_port **p_port);


/**
 * @}
 */

PJ_END_DECL


#endif	/* __PJMEDIA_MEM_PORT_H__ */
