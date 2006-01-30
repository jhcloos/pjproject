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
#ifndef __PJSIP_SIP_MODULE_H__
#define __PJSIP_SIP_MODULE_H__

/**
 * @file sip_module.h
 * @brief Module helpers
 */
#include <pjsip/sip_types.h>
#include <pj/list.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJSIP_MOD SIP Modules
 * @ingroup PJSIP
 * @{
 */

/**
 * Module registration structure, which is passed by the module to the
 * endpoint during the module registration process. This structure enables
 * the endpoint to query the module capability and to further communicate
 * with the module.
 */
struct pjsip_module
{
    /** To allow chaining of modules in the endpoint. */
    PJ_DECL_LIST_MEMBER(struct pjsip_module);

    /**
     * Module name.
     */
    pj_str_t name;

    /**
     * Module ID.
     */
    int id;

    /**
     * Integer number to identify module initialization and start order with
     * regard to other modules. Higher number will make the module gets
     * initialized later.
     */
    int priority;

    /**
     * Opaque data which can be used by a module to identify a resource within
     * the module itself.
     */
    void *user_data;

    /**
     * Pointer to function to be called to initialize the module.
     *
     * @param endpt	The endpoint instance.
     * @return		Module should return PJ_SUCCESS to indicate success.
     */
    pj_status_t (*load)(pjsip_endpoint *endpt);

    /**
     * Pointer to function to be called to start the module.
     *
     * @return		Module should return zero to indicate success.
     */
    pj_status_t (*start)(void);

    /**
     * Pointer to function to be called to deinitialize the module before
     * it is unloaded.
     *
     * @return		Module should return PJ_SUCCESS to indicate success.
     */
    pj_status_t (*stop)(void);

    /**
     * Pointer to function to be called to deinitialize the module before
     * it is unloaded.
     *
     * @param mod	The module.
     *
     * @return		Module should return PJ_SUCCESS to indicate success.
     */
    pj_status_t (*unload)(void);

    /**
     * Called to process incoming request.
     *
     * @param rdata	The incoming message.
     *
     * @return		Module should return PJ_TRUE if it handles the request,
     *			or otherwise it should return PJ_FALSE to allow other
     *			modules to handle the request.
     */
    pj_bool_t (*on_rx_request)(pjsip_rx_data *rdata);

    /**
     * Called to processed incoming response.
     *
     * @param rdata	The incoming message.
     *
     * @return		Module should return PJ_TRUE if it handles the 
     *			response, or otherwise it should return PJ_FALSE to 
     *			allow other modules to handle the response.
     */
    pj_bool_t (*on_rx_response)(pjsip_rx_data *rdata);

    /**
     * Called to process outgoing request.
     *
     * @param tdata	The outgoing request message.
     *
     * @return		Module should return PJ_SUCCESS in all cases. 
     *			If non-zero (or PJ_FALSE) is returned, the message 
     *			will not be sent.
     */
    pj_status_t (*on_tx_request)(pjsip_tx_data *tdata);

    /**
     * Called to process outgoing response message.
     *
     * @param tdata	The outgoing response message.
     *
     * @return		Module should return PJ_SUCCESS in all cases. 
     *			If non-zero (or PJ_FALSE) is returned, the message 
     *			will not be sent.
     */
    pj_status_t (*on_tx_response)(pjsip_tx_data *tdata);

    /**
     * Called when this module is acting as transaction user for the specified
     * transaction, when the transaction's state has changed.
     *
     * @param tsx	The transaction.
     * @param event	The event which has caused the transaction state
     *			to change.
     */
    void (*on_tsx_state)(pjsip_transaction *tsx, pjsip_event *event);

};


/**
 * Module priority guidelines.
 */
enum pjsip_module_priority
{
    PJSIP_MOD_PRIORITY_TRANSPORT_LAYER	= 8,
    PJSIP_MOD_PRIORITY_TSX_LAYER	= 16,
    PJSIP_MOD_PRIORITY_UA_PROXY_LAYER	= 32,
    PJSIP_MOD_PRIORITY_APPLICATION	= 64,
};


/**
 * @}
 */

PJ_END_DECL

#endif	/* __PJSIP_SIP_MODULE_H__ */

