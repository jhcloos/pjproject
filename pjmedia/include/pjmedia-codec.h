/* $Id$ */
/* 
 * Copyright (C)2003-2006 Benny Prijono <benny@prijono.org>
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
#ifndef __PJMEDIA_CODEC1_H__
#define __PJMEDIA_CODEC1_H__

#include <pjmedia-codec/gsm.h>
#include <pjmedia-codec/speex.h>


PJ_BEGIN_DECL


/**
 * Initialize pjmedia-codec library, and register all codec factories
 * in this library. If application wants to controll the order of
 * the codec, it MUST NOT call this function, but instead register
 * each codec individually.
 *
 * @param endpt	    The pjmedia endpoint.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_init(pjmedia_endpt *endpt);


/**
 * Deinitialize pjmedia-codec library, and unregister all codec factories
 * in this library.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_deinit(void);


PJ_END_DECL


#endif	/* __PJMEDIA_CODEC_H__ */

