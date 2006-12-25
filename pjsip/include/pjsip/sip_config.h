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
#ifndef __PJSIP_SIP_CONFIG_H__
#define __PJSIP_SIP_CONFIG_H__

/**
 * @file sip_config.h
 * @brief Compile time configuration.
 */
#include <pj/config.h>

/**
 * @defgroup PJSIP PJSIP Library Collection
 */

/**
 * @defgroup PJSIP_CORE Core SIP Library
 * @ingroup PJSIP
 * @brief The core framework from which all other SIP components depends on.
 * 
 * The PJSIP Core library only provides transport framework, event
 * dispatching/module framework, and SIP message representation and
 * parsing. It doesn't do anything usefull in itself!
 *
 * If application wants the stack to do anything usefull at all,
 * it must registers @ref PJSIP_MOD to the core library. Examples
 * of modules are @ref PJSIP_TRANSACT and @ref PJSUA_UA.
 */

/**
 * @defgroup PJSIP_BASE Base Types
 * @ingroup PJSIP_CORE
 * @brief Basic PJSIP types and configurations.
 */

/**
 * @defgroup PJSIP_CONFIG Compile Time Configuration
 * @ingroup PJSIP_BASE
 * @brief PJSIP compile time configurations.
 * @{
 */

/**
 * Specify maximum transaction count in transaction hash table.
 * Default value is 16*1024
 */
#ifndef PJSIP_MAX_TSX_COUNT
#   define PJSIP_MAX_TSX_COUNT		(16*1024)
#endif

/**
 * Specify maximum number of dialogs in the dialog hash table.
 * Default value is 16*1024.
 */
#ifndef PJSIP_MAX_DIALOG_COUNT
#   define PJSIP_MAX_DIALOG_COUNT	(16*1024)
#endif


/**
 * Specify maximum number of transports.
 * Default value is equal to maximum number of handles in ioqueue.
 * See also PJSIP_TPMGR_HTABLE_SIZE.
 */
#ifndef PJSIP_MAX_TRANSPORTS
#   define PJSIP_MAX_TRANSPORTS		(PJ_IOQUEUE_MAX_HANDLES)
#endif


/**
 * Transport manager hash table size (must be 2^n-1). 
 * See also PJSIP_MAX_TRANSPORTS
 */
#ifndef PJSIP_TPMGR_HTABLE_SIZE
#   define PJSIP_TPMGR_HTABLE_SIZE	31
#endif


/**
 * Specify maximum URL size.
 * This constant is used mainly when printing the URL for logging purpose 
 * only.
 */
#ifndef PJSIP_MAX_URL_SIZE
#   define PJSIP_MAX_URL_SIZE		256
#endif


/**
 * Specify maximum number of modules.
 * This mainly affects the size of mod_data array in various components.
 */
#ifndef PJSIP_MAX_MODULE
#   define PJSIP_MAX_MODULE		16
#endif


/**
 * Maximum packet length. We set it more than MTU since a SIP PDU
 * containing presence information can be quite large (>1500).
 */
#ifndef PJSIP_MAX_PKT_LEN
#   define PJSIP_MAX_PKT_LEN		2000
#endif


/**
 * Allow SIP modules removal or insertions during operation?
 * If yes, then locking will be employed when endpoint need to
 * access module.
 */
#ifndef PJSIP_SAFE_MODULE
#   define PJSIP_SAFE_MODULE		1
#endif


/**
 * If non-zero, SIP parser will unescape the escape characters ('%')
 * in the original message, which means that it will modify the
 * original message. Otherwise the parser will create a copy of
 * the string and store the unescaped string to the new location.
 *
 * Unescaping in-place is faster, but less elegant (and it may
 * break certain applications). So normally it's disabled, unless
 * when benchmarking (to show off big performance).
 *
 * Default: 0
 */
#ifndef PJSIP_UNESCAPE_IN_PLACE
#   define PJSIP_UNESCAPE_IN_PLACE	0
#endif


/**
 * This macro controls maximum numbers of ioqueue events to be processed
 * in a single pjsip_endpt_handle_events() poll. When PJSIP detects that
 * there are probably more events available from the network and total
 * events so far is less than this value, PJSIP will call pj_ioqueue_poll()
 * again to get more events.
 *
 * Value 1 works best for ioqueue with select() back-end, while for IOCP it is
 * probably best to set this value equal to PJSIP_MAX_TIMED_OUT_ENTRIES
 * since IOCP only processes one event at a time.
 *
 * Default: 1
 */
#ifndef PJSIP_MAX_NET_EVENTS
#   define PJSIP_MAX_NET_EVENTS		1
#endif


/**
 * Max entries to process in timer heap per poll. 
 * 
 * Default: 10
 */
#ifndef PJSIP_MAX_TIMED_OUT_ENTRIES
#   define PJSIP_MAX_TIMED_OUT_ENTRIES	10
#endif


/**
 * Idle timeout interval to be applied to transports with no usage
 * before the transport is destroyed. Value is in seconds.
 *
 * Default: 60
 */
#ifndef PJSIP_TRANSPORT_IDLE_TIME
#   define PJSIP_TRANSPORT_IDLE_TIME	60
#endif


/**
 * Maximum number of usages for a transport before a new transport is
 * created. This only applies for ephemeral transports such as TCP.
 *
 * Currently this is not used.
 * 
 * Default: -1
 */
#ifndef PJSIP_MAX_TRANSPORT_USAGE
#   define PJSIP_MAX_TRANSPORT_USAGE	((unsigned)-1)
#endif


/**
 * The TCP incoming connection backlog number to be set in accept().
 *
 * Default: 5
 *
 * @see PJSIP_TLS_TRANSPORT_BACKLOG
 */
#ifndef PJSIP_TCP_TRANSPORT_BACKLOG
#   define PJSIP_TCP_TRANSPORT_BACKLOG	5
#endif


/**
 * This macro specifies whether full DNS resolution should be used.
 * When enabled, #pjsip_resolve() will perform asynchronous DNS SRV and
 * A (or AAAA, when IPv6 is supported) resolution to resolve the SIP
 * domain.
 *
 * Note that even when this setting is enabled, asynchronous DNS resolution
 * will only be done when application calls #pjsip_endpt_create_resolver(),
 * configure the nameservers with #pj_dns_resolver_set_ns(), and configure
 * the SIP endpoint's DNS resolver with #pjsip_endpt_set_resolver(). If
 * these steps are not followed, the domain will be resolved with normal
 * pj_gethostbyname() function.
 *
 * Turning off this setting will save the footprint by about 16KB, since
 * it should also exclude dns.o and resolve.o from PJLIB-UTIL.
 *
 * Default: 1 (enabled)
 *
 * @see PJSIP_MAX_RESOLVED_ADDRESSES
 */
#ifndef PJSIP_HAS_RESOLVER
#   define PJSIP_HAS_RESOLVER		1
#endif


/** 
 * Maximum number of addresses returned by the resolver. The number here 
 * will slightly affect stack usage, since each entry will occupy about
 * 32 bytes of stack memory.
 *
 * Default: 8
 *
 * @see PJSIP_HAS_RESOLVER
 */
#ifndef PJSIP_MAX_RESOLVED_ADDRESSES
#   define PJSIP_MAX_RESOLVED_ADDRESSES	    8
#endif


/**
 * Enable TLS SIP transport support. For most systems this means that
 * OpenSSL must be installed.
 *
 * Default: 0 (for now)
 */
#ifndef PJSIP_HAS_TLS_TRANSPORT
#   define PJSIP_HAS_TLS_TRANSPORT	    0
#endif


/**
 * The TLS pending incoming connection backlog number to be set in accept().
 *
 * Default: 5
 *
 * @see PJSIP_TCP_TRANSPORT_BACKLOG
 */
#ifndef PJSIP_TLS_TRANSPORT_BACKLOG
#   define PJSIP_TLS_TRANSPORT_BACKLOG	    5
#endif



/* Endpoint. */
#define PJSIP_MAX_TIMER_COUNT		(2*PJSIP_MAX_TSX_COUNT + 2*PJSIP_MAX_DIALOG_COUNT)
#define PJSIP_POOL_LEN_ENDPT		(4000)
#define PJSIP_POOL_INC_ENDPT		(4000)

/* Transport related constants. */

#define PJSIP_POOL_RDATA_LEN		4000
#define PJSIP_POOL_RDATA_INC		4000
#define PJSIP_POOL_LEN_TRANSPORT	512
#define PJSIP_POOL_INC_TRANSPORT	512
#define PJSIP_POOL_LEN_TDATA		4000
#define PJSIP_POOL_INC_TDATA		4000
#define PJSIP_POOL_LEN_UA		4000
#define PJSIP_POOL_INC_UA		4000

#define PJSIP_MAX_FORWARDS_VALUE	70

#define PJSIP_RFC3261_BRANCH_ID		"z9hG4bK"
#define PJSIP_RFC3261_BRANCH_LEN	7

/* Transaction related constants. */
#define PJSIP_POOL_TSX_LAYER_LEN	4000
#define PJSIP_POOL_TSX_LAYER_INC	4000
#define PJSIP_POOL_TSX_LEN		1536 /* 768 */
#define PJSIP_POOL_TSX_INC		256
#define PJSIP_MAX_TSX_KEY_LEN		(PJSIP_MAX_URL_SIZE*2)

/* User agent. */
#define PJSIP_POOL_LEN_USER_AGENT	1024
#define PJSIP_POOL_INC_USER_AGENT	1024

/* Message/URL related constants. */
#define PJSIP_MAX_CALL_ID_LEN		PJ_GUID_STRING_LENGTH
#define PJSIP_MAX_TAG_LEN		PJ_GUID_STRING_LENGTH
#define PJSIP_MAX_BRANCH_LEN		(PJSIP_RFC3261_BRANCH_LEN + PJ_GUID_STRING_LENGTH + 2)
#define PJSIP_MAX_HNAME_LEN		64

/* Dialog related constants. */
#define PJSIP_POOL_LEN_DIALOG		1200
#define PJSIP_POOL_INC_DIALOG		512

/* Maximum header types. */
#define PJSIP_MAX_HEADER_TYPES		64

/* Maximum URI types. */
#define PJSIP_MAX_URI_TYPES		4

/*****************************************************************************
 *  Default timeout settings, in miliseconds. 
 */

/** Transaction T1 timeout value. */
#if !defined(PJSIP_T1_TIMEOUT)
#  define PJSIP_T1_TIMEOUT	500
#endif

/** Transaction T2 timeout value. */
#if !defined(PJSIP_T2_TIMEOUT)
#  define PJSIP_T2_TIMEOUT	4000
#endif

/** Transaction completed timer for non-INVITE */
#if !defined(PJSIP_T4_TIMEOUT)
#  define PJSIP_T4_TIMEOUT	5000
#endif

/** Transaction completed timer for INVITE */
#if !defined(PJSIP_TD_TIMEOUT)
#  define PJSIP_TD_TIMEOUT	32000
#endif


/*****************************************************************************
 *  Authorization
 */

/**
 * If this flag is set, the stack will keep the Authorization/Proxy-Authorization
 * headers that are sent in a cache. Future requests with the same realm and
 * the same method will use the headers in the cache (as long as no qop is
 * required by server).
 *
 * Turning on this flag will make authorization process goes faster, but
 * will grow the memory usage undefinitely until the dialog/registration
 * session is terminated.
 *
 * Default: 1
 */
#if !defined(PJSIP_AUTH_HEADER_CACHING)
#   define PJSIP_AUTH_HEADER_CACHING	    0
#endif

/**
 * If this flag is set, the stack will proactively send Authorization/Proxy-
 * Authorization header for next requests. If next request has the same method
 * with any of previous requests, then the last header which is saved in
 * the cache will be used (if PJSIP_AUTH_CACHING is set). Otherwise a fresh
 * header will be recalculated. If a particular server has requested qop, then
 * a fresh header will always be calculated.
 *
 * If this flag is NOT set, then the stack will only send Authorization/Proxy-
 * Authorization headers when it receives 401/407 response from server.
 *
 * Turning ON this flag will grow memory usage of a dialog/registration pool
 * indefinitely until it is terminated, because the stack needs to keep the
 * last WWW-Authenticate/Proxy-Authenticate challenge.
 *
 * Default: 1
 */
#if !defined(PJSIP_AUTH_AUTO_SEND_NEXT)
#   define PJSIP_AUTH_AUTO_SEND_NEXT	    0
#endif

/**
 * Support qop="auth" directive.
 * This option also requires client to cache the last challenge offered by
 * server.
 *
 * Default: 1
 */
#if !defined(PJSIP_AUTH_QOP_SUPPORT)
#   define PJSIP_AUTH_QOP_SUPPORT	    1
#endif


/**
 * @}
 */

#include <pj/config.h>


#endif	/* __PJSIP_SIP_CONFIG_H__ */

