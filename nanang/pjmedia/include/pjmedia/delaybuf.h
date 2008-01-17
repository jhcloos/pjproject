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

#ifndef __PJMEDIA_DELAYBUF_H__
#define __PJMEDIA_DELAYBUF_H__


/**
 * @file delaybuf.h
 * @brief Delay Buffer.
 */

#include <pjmedia/types.h>

/**
 * @defgroup PJMED_DELAYBUF Delay Buffer
 * @ingroup PJMEDIA_FRAME_OP
 * @{
 * This section describes PJMEDIA's implementation of delay buffer.
 * Delay buffer works quite similarly like a fixed jitter buffer, that
 * is it will delay the frame retrieval by some interval so that caller
 * will get continuous frame from the buffer. This can be useful when
 * the put() and get() operations are not evenly interleaved, for example
 * when caller performs burst of put() operations and then followed by
 * burst of get() operations. With using this delay buffer, the buffer
 * will put the burst frames into a buffer so that get() operations
 * will always get a frame from the buffer (assuming that the number of
 * get() and put() are matched).
 *
 * The delay buffer is mostly used by the sound port, to accommodate
 * for the burst frames returned by the sound device.
 *
 * To determine the level of delay to be applied, the delay buffer
 * has a learning period on which it calculates the level of burst of
 * both the put() and get(), and use the maximum value of both as the
 * delay level.
 */

PJ_BEGIN_DECL

/** Opaque declaration for delay buffer. */
typedef struct pjmedia_delay_buf pjmedia_delay_buf;

/**
 * Create the delay buffer. Once the delay buffer is created, it will
 * enter learning state unless the delay argument is specified, which
 * in this case it will directly enter the running state.
 *
 * @param pool		    Pool where the delay buffer will be allocated
 *			    from.
 * @param name		    Optional name for the buffer for log 
 *			    identification.
 * @param samples_per_frame Number of samples per frame.
 * @param max_cnt	    Maximum number of delay to be accommodated,
 *			    in number of frames.
 * @param delay		    The delay to be applied, in number of frames.
 *			    If the value is -1, the delay buffer will
 *			    learn about the delay automatically. If
 *			    the value is greater than zero, then this
 *			    value will be used and no learning will be
 *			    performed.
 * @param p_b		    Pointer to receive the delay buffer instance.
 *
 * @return		    PJ_SUCCESS if the delay buffer has been
 *			    created successfully, otherwise the appropriate
 *			    error will be returned.
 */
PJ_DECL(pj_status_t) pjmedia_delay_buf_create(pj_pool_t *pool,
					      const char *name,
					      unsigned samples_per_frame,
					      unsigned max_cnt,
					      int delay,
					      pjmedia_delay_buf **p_b);

/**
 * Put one frame into the buffer.
 *
 * @param b		    The delay buffer.
 * @param frame		    Frame to be put into the buffer. This frame
 *			    must have samples_per_frame length.
 *
 * @return		    PJ_SUCCESS if frames can be put successfully.
 *			    PJ_EPENDING if the buffer is still at learning
 *			    state. PJ_ETOOMANY if the number of frames
 *			    will exceed maximum delay level, which in this
 *			    case the new frame will overwrite the oldest
 *			    frame in the buffer.
 */
PJ_DECL(pj_status_t) pjmedia_delay_buf_put(pjmedia_delay_buf *b,
					   pj_int16_t frame[]);

/**
 * Get one frame from the buffer.
 *
 * @param b		    The delay buffer.
 * @param frame		    Buffer to receive the frame from the delay
 *			    buffer.
 *
 * @return		    PJ_SUCCESS if frame has been copied successfully.
 *			    PJ_EPENDING if no frame is available, either
 *			    because the buffer is still at learning state or
 *			    no buffer is available during running state.
 *			    On non-successful return, the frame will be
 *			    filled with zeroes.
 */
PJ_DECL(pj_status_t) pjmedia_delay_buf_get(pjmedia_delay_buf *b,
					   pj_int16_t frame[]);

/**
 * Reinitiate learning state. This will clear the buffer's content.
 *
 * @param b		    The delay buffer.
 *
 * @return		    PJ_SUCCESS on success or the appropriate error.
 */
PJ_DECL(pj_status_t) pjmedia_delay_buf_learn(pjmedia_delay_buf *b);


PJ_END_DECL


#endif	/* __PJMEDIA_DELAYBUF_H__ */
