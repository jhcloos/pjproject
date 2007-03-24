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
#ifndef __PJNATH_TYPES_H__
#define __PJNATH_TYPES_H__

/**
 * @file types.h
 * @brief PJNATH types.
 */

#include <pj/types.h>
#include <pjnath/config.h>

/**
 * @defgroup PJNATH NAT Helper Library
 * @{
 */

PJ_BEGIN_DECL

/**
 * Initialize pjnath library.
 *
 * @return  Initialization status.
 */
PJ_DECL(pj_status_t) pjnath_init(void);


PJ_END_DECL

/**
 * @}
 */

/* Doxygen documentation below: */

/**
 * @mainpage NAT Helper Library
 *
 * \n
 * \n
 * \n
 * This is the documentation of PJNATH, an auxiliary library providing
 * NAT helper functionalities such as STUN and ICE.
 * 
 * Please go to the <A HREF="modules.htm"><B>Modules</B></A> page for list
 * of modules.
 *
 */

#endif	/* __PJNATH_TYPES_H__ */

