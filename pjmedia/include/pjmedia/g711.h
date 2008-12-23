/* $Id$ */
/* 
 * Copyright (C) 2008-2009 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
#ifndef __PJMEDIA_G711_H__
#define __PJMEDIA_G711_H__

/**
 * @file g711.h
 * @brief G711 Codec
 */

#include <pjmedia-codec/types.h>

/**
 * @defgroup PJMED_G711 G711
 * @ingroup PJMEDIA_CODEC
 * @brief Standard G.711/PCMA and PCMU codec.
 * @{
 * This section describes functions to register and register G.711 codec
 * factory to the codec manager. After the codec factory has been registered,
 * application can use @ref PJMEDIA_CODEC API to manipulate the codec.
 */

PJ_BEGIN_DECL


/**
 * Initialize and register G711 codec factory to pjmedia endpoint.
 * This will register PCMU and PCMA codec, in that order.
 *
 * @param endpt		The pjmedia endpoint.
 *
 * @return		PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_g711_init(pjmedia_endpt *endpt);



/**
 * Unregister G711 codec factory from pjmedia endpoint.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_g711_deinit(void);


PJ_END_DECL

/**
 * @}
 */

#endif	/* __PJMEDIA_G711_H__ */

