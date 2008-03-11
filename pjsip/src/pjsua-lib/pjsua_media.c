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
#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>


#define THIS_FILE		"pjsua_media.c"

#define DEFAULT_RTP_PORT			4000

#ifndef PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT
#   define PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT	0
#endif


/* Next RTP port to be used */
static pj_uint16_t next_rtp_port;

/* Close existing sound device */
static void close_snd_dev(void);


/**
 * Init media subsystems.
 */
pj_status_t pjsua_media_subsys_init(const pjsua_media_config *cfg)
{
    pj_str_t codec_id = {NULL, 0};
    unsigned opt;
    pj_status_t status;

    /* To suppress warning about unused var when all codecs are disabled */
    PJ_UNUSED_ARG(codec_id);

    /* Copy configuration */
    pj_memcpy(&pjsua_var.media_cfg, cfg, sizeof(*cfg));

    /* Normalize configuration */
    if (pjsua_var.media_cfg.snd_clock_rate == 0) {
	pjsua_var.media_cfg.snd_clock_rate = pjsua_var.media_cfg.clock_rate;
    }

    if (pjsua_var.media_cfg.has_ioqueue &&
	pjsua_var.media_cfg.thread_cnt == 0)
    {
	pjsua_var.media_cfg.thread_cnt = 1;
    }

    if (pjsua_var.media_cfg.max_media_ports < pjsua_var.ua_cfg.max_calls) {
	pjsua_var.media_cfg.max_media_ports = pjsua_var.ua_cfg.max_calls + 2;
    }

    /* Create media endpoint. */
    status = pjmedia_endpt_create(&pjsua_var.cp.factory, 
				  pjsua_var.media_cfg.has_ioqueue? NULL :
				     pjsip_endpt_get_ioqueue(pjsua_var.endpt),
				  pjsua_var.media_cfg.thread_cnt,
				  &pjsua_var.med_endpt);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, 
		     "Media stack initialization has returned error", 
		     status);
	return status;
    }

    /* Register all codecs */
#if PJMEDIA_HAS_SPEEX_CODEC
    /* Register speex. */
    status = pjmedia_codec_speex_init(pjsua_var.med_endpt, 
				      0,
				      pjsua_var.media_cfg.quality, 
				      pjsua_var.media_cfg.quality);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing Speex codec",
		     status);
	return status;
    }

    /* Set speex/16000 to higher priority*/
    codec_id = pj_str("speex/16000");
    pjmedia_codec_mgr_set_codec_priority( 
	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
	&codec_id, PJMEDIA_CODEC_PRIO_NORMAL+2);

    /* Set speex/8000 to next higher priority*/
    codec_id = pj_str("speex/8000");
    pjmedia_codec_mgr_set_codec_priority( 
	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
	&codec_id, PJMEDIA_CODEC_PRIO_NORMAL+1);



#endif /* PJMEDIA_HAS_SPEEX_CODEC */

#if PJMEDIA_HAS_ILBC_CODEC
    /* Register iLBC. */
    status = pjmedia_codec_ilbc_init( pjsua_var.med_endpt, 
				      pjsua_var.media_cfg.ilbc_mode);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing iLBC codec",
		     status);
	return status;
    }
#endif /* PJMEDIA_HAS_ILBC_CODEC */

#if PJMEDIA_HAS_GSM_CODEC
    /* Register GSM */
    status = pjmedia_codec_gsm_init(pjsua_var.med_endpt);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing GSM codec",
		     status);
	return status;
    }
#endif /* PJMEDIA_HAS_GSM_CODEC */

#if PJMEDIA_HAS_G711_CODEC
    /* Register PCMA and PCMU */
    status = pjmedia_codec_g711_init(pjsua_var.med_endpt);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing G711 codec",
		     status);
	return status;
    }
#endif	/* PJMEDIA_HAS_G711_CODEC */

#if PJMEDIA_HAS_L16_CODEC
    /* Register L16 family codecs, but disable all */
    status = pjmedia_codec_l16_init(pjsua_var.med_endpt, 0);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error initializing L16 codecs",
		     status);
	return status;
    }

    /* Disable ALL L16 codecs */
    codec_id = pj_str("L16");
    pjmedia_codec_mgr_set_codec_priority( 
	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
	&codec_id, PJMEDIA_CODEC_PRIO_DISABLED);

#endif	/* PJMEDIA_HAS_L16_CODEC */


    /* Save additional conference bridge parameters for future
     * reference.
     */
    pjsua_var.mconf_cfg.samples_per_frame = pjsua_var.media_cfg.clock_rate * 
					    pjsua_var.media_cfg.audio_frame_ptime / 
					    1000;
    pjsua_var.mconf_cfg.channel_count = 1;
    pjsua_var.mconf_cfg.bits_per_sample = 16;

    /* Init options for conference bridge. */
    opt = PJMEDIA_CONF_NO_DEVICE;
    if (pjsua_var.media_cfg.quality >= 3 &&
	pjsua_var.media_cfg.quality <= 4)
    {
	opt |= PJMEDIA_CONF_SMALL_FILTER;
    }
    else if (pjsua_var.media_cfg.quality < 3) {
	opt |= PJMEDIA_CONF_USE_LINEAR;
    }
	

    /* Init conference bridge. */
    status = pjmedia_conf_create(pjsua_var.pool, 
				 pjsua_var.media_cfg.max_media_ports,
				 pjsua_var.media_cfg.clock_rate, 
				 pjsua_var.mconf_cfg.channel_count,
				 pjsua_var.mconf_cfg.samples_per_frame, 
				 pjsua_var.mconf_cfg.bits_per_sample, 
				 opt, &pjsua_var.mconf);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error creating conference bridge", 
		     status);
	return status;
    }

    /* Create null port just in case user wants to use null sound. */
    status = pjmedia_null_port_create(pjsua_var.pool, 
				      pjsua_var.media_cfg.clock_rate,
				      pjsua_var.mconf_cfg.channel_count,
				      pjsua_var.mconf_cfg.samples_per_frame,
				      pjsua_var.mconf_cfg.bits_per_sample,
				      &pjsua_var.null_port);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    /* Perform NAT detection */
    pjsua_detect_nat_type();

    return PJ_SUCCESS;
}


/* 
 * Create RTP and RTCP socket pair, and possibly resolve their public
 * address via STUN.
 */
static pj_status_t create_rtp_rtcp_sock(const pjsua_transport_config *cfg,
					pjmedia_sock_info *skinfo)
{
    enum { 
	RTP_RETRY = 100
    };
    int i;
    pj_sockaddr_in bound_addr;
    pj_sockaddr_in mapped_addr[2];
    pj_status_t status = PJ_SUCCESS;
    char addr_buf[PJ_INET6_ADDRSTRLEN+2];
    pj_sock_t sock[2];

    /* Make sure STUN server resolution has completed */
    status = pjsua_resolve_stun_server(PJ_TRUE);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error resolving STUN server", status);
	return status;
    }

    if (next_rtp_port == 0)
	next_rtp_port = (pj_uint16_t)cfg->port;

    for (i=0; i<2; ++i)
	sock[i] = PJ_INVALID_SOCKET;

    bound_addr.sin_addr.s_addr = PJ_INADDR_ANY;
    if (cfg->bound_addr.slen) {
	status = pj_sockaddr_in_set_str_addr(&bound_addr, &cfg->bound_addr);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to resolve transport bind address",
			 status);
	    return status;
	}
    }

    /* Loop retry to bind RTP and RTCP sockets. */
    for (i=0; i<RTP_RETRY; ++i, next_rtp_port += 2) {

	/* Create and bind RTP socket. */
	status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &sock[0]);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "socket() error", status);
	    return status;
	}

	status=pj_sock_bind_in(sock[0], pj_ntohl(bound_addr.sin_addr.s_addr), 
			       next_rtp_port);
	if (status != PJ_SUCCESS) {
	    pj_sock_close(sock[0]); 
	    sock[0] = PJ_INVALID_SOCKET;
	    continue;
	}

	/* Create and bind RTCP socket. */
	status = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &sock[1]);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "socket() error", status);
	    pj_sock_close(sock[0]);
	    return status;
	}

	status=pj_sock_bind_in(sock[1], pj_ntohl(bound_addr.sin_addr.s_addr), 
			       (pj_uint16_t)(next_rtp_port+1));
	if (status != PJ_SUCCESS) {
	    pj_sock_close(sock[0]); 
	    sock[0] = PJ_INVALID_SOCKET;

	    pj_sock_close(sock[1]); 
	    sock[1] = PJ_INVALID_SOCKET;
	    continue;
	}

	/*
	 * If we're configured to use STUN, then find out the mapped address,
	 * and make sure that the mapped RTCP port is adjacent with the RTP.
	 */
	if (pjsua_var.stun_srv.addr.sa_family != 0) {
	    char ip_addr[32];
	    pj_str_t stun_srv;

	    pj_ansi_strcpy(ip_addr, 
			   pj_inet_ntoa(pjsua_var.stun_srv.ipv4.sin_addr));
	    stun_srv = pj_str(ip_addr);

	    status=pjstun_get_mapped_addr(&pjsua_var.cp.factory, 2, sock,
					   &stun_srv, pj_ntohs(pjsua_var.stun_srv.ipv4.sin_port),
					   &stun_srv, pj_ntohs(pjsua_var.stun_srv.ipv4.sin_port),
					   mapped_addr);
	    if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "STUN resolve error", status);
		goto on_error;
	    }

#if PJSUA_REQUIRE_CONSECUTIVE_RTCP_PORT
	    if (pj_ntohs(mapped_addr[1].sin_port) == 
		pj_ntohs(mapped_addr[0].sin_port)+1)
	    {
		/* Success! */
		break;
	    }

	    pj_sock_close(sock[0]); 
	    sock[0] = PJ_INVALID_SOCKET;

	    pj_sock_close(sock[1]); 
	    sock[1] = PJ_INVALID_SOCKET;
#else
	    if (pj_ntohs(mapped_addr[1].sin_port) != 
		pj_ntohs(mapped_addr[0].sin_port)+1)
	    {
		PJ_LOG(4,(THIS_FILE, 
			  "Note: STUN mapped RTCP port %d is not adjacent"
			  " to RTP port %d",
			  pj_ntohs(mapped_addr[1].sin_port),
			  pj_ntohs(mapped_addr[0].sin_port)));
	    }
	    /* Success! */
	    break;
#endif

	} else if (cfg->public_addr.slen) {

	    status = pj_sockaddr_in_init(&mapped_addr[0], &cfg->public_addr,
					 (pj_uint16_t)next_rtp_port);
	    if (status != PJ_SUCCESS)
		goto on_error;

	    status = pj_sockaddr_in_init(&mapped_addr[1], &cfg->public_addr,
					 (pj_uint16_t)(next_rtp_port+1));
	    if (status != PJ_SUCCESS)
		goto on_error;

	    break;

	} else {

	    if (bound_addr.sin_addr.s_addr == 0) {
		pj_sockaddr addr;

		/* Get local IP address. */
		status = pj_gethostip(pj_AF_INET(), &addr);
		if (status != PJ_SUCCESS)
		    goto on_error;

		bound_addr.sin_addr.s_addr = addr.ipv4.sin_addr.s_addr;
	    }

	    for (i=0; i<2; ++i) {
		pj_sockaddr_in_init(&mapped_addr[i], NULL, 0);
		mapped_addr[i].sin_addr.s_addr = bound_addr.sin_addr.s_addr;
	    }

	    mapped_addr[0].sin_port=pj_htons((pj_uint16_t)next_rtp_port);
	    mapped_addr[1].sin_port=pj_htons((pj_uint16_t)(next_rtp_port+1));
	    break;
	}
    }

    if (sock[0] == PJ_INVALID_SOCKET) {
	PJ_LOG(1,(THIS_FILE, 
		  "Unable to find appropriate RTP/RTCP ports combination"));
	goto on_error;
    }


    skinfo->rtp_sock = sock[0];
    pj_memcpy(&skinfo->rtp_addr_name, 
	      &mapped_addr[0], sizeof(pj_sockaddr_in));

    skinfo->rtcp_sock = sock[1];
    pj_memcpy(&skinfo->rtcp_addr_name, 
	      &mapped_addr[1], sizeof(pj_sockaddr_in));

    PJ_LOG(4,(THIS_FILE, "RTP socket reachable at %s",
	      pj_sockaddr_print(&skinfo->rtp_addr_name, addr_buf,
				sizeof(addr_buf), 3)));
    PJ_LOG(4,(THIS_FILE, "RTCP socket reachable at %s",
	      pj_sockaddr_print(&skinfo->rtcp_addr_name, addr_buf,
				sizeof(addr_buf), 3)));

    next_rtp_port += 2;
    return PJ_SUCCESS;

on_error:
    for (i=0; i<2; ++i) {
	if (sock[i] != PJ_INVALID_SOCKET)
	    pj_sock_close(sock[i]);
    }
    return status;
}


/*
 * Start pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_start(void)
{
    pj_status_t status;

    /* Create media for calls, if none is specified */
    if (pjsua_var.calls[0].med_tp == NULL) {
	pjsua_transport_config transport_cfg;

	/* Create default transport config */
	pjsua_transport_config_default(&transport_cfg);
	transport_cfg.port = DEFAULT_RTP_PORT;

	status = pjsua_media_transports_create(&transport_cfg);
	if (status != PJ_SUCCESS)
	    return status;
    }

    /* Create sound port if none is created yet */
    if (pjsua_var.snd_port==NULL && pjsua_var.null_snd==NULL && 
	!pjsua_var.no_snd) 
    {
	status = pjsua_set_snd_dev(pjsua_var.cap_dev, pjsua_var.play_dev);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Error opening sound device", status);
	    return status;
	}
    }

    return PJ_SUCCESS;
}


/*
 * Destroy pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_destroy(void)
{
    unsigned i;

    close_snd_dev();

    if (pjsua_var.mconf) {
	pjmedia_conf_destroy(pjsua_var.mconf);
	pjsua_var.mconf = NULL;
    }

    if (pjsua_var.null_port) {
	pjmedia_port_destroy(pjsua_var.null_port);
	pjsua_var.null_port = NULL;
    }

    /* Destroy file players */
    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.player); ++i) {
	if (pjsua_var.player[i].port) {
	    pjmedia_port_destroy(pjsua_var.player[i].port);
	    pjsua_var.player[i].port = NULL;
	}
    }

    /* Destroy file recorders */
    for (i=0; i<PJ_ARRAY_SIZE(pjsua_var.recorder); ++i) {
	if (pjsua_var.recorder[i].port) {
	    pjmedia_port_destroy(pjsua_var.recorder[i].port);
	    pjsua_var.recorder[i].port = NULL;
	}
    }

    /* Close media transports */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	if (pjsua_var.calls[i].med_tp) {
	    (*pjsua_var.calls[i].med_tp->op->destroy)(pjsua_var.calls[i].med_tp);
	    pjsua_var.calls[i].med_tp = NULL;
	}
    }

    /* Destroy media endpoint. */
    if (pjsua_var.med_endpt) {

	/* Shutdown all codecs: */
#	if PJMEDIA_HAS_SPEEX_CODEC
	    pjmedia_codec_speex_deinit();
#	endif /* PJMEDIA_HAS_SPEEX_CODEC */

#	if PJMEDIA_HAS_GSM_CODEC
	    pjmedia_codec_gsm_deinit();
#	endif /* PJMEDIA_HAS_GSM_CODEC */

#	if PJMEDIA_HAS_G711_CODEC
	    pjmedia_codec_g711_deinit();
#	endif	/* PJMEDIA_HAS_G711_CODEC */

#	if PJMEDIA_HAS_L16_CODEC
	    pjmedia_codec_l16_deinit();
#	endif	/* PJMEDIA_HAS_L16_CODEC */


	pjmedia_endpt_destroy(pjsua_var.med_endpt);
	pjsua_var.med_endpt = NULL;

	/* Deinitialize sound subsystem */
	// Not necessary, as pjmedia_snd_deinit() should have been called
	// in pjmedia_endpt_destroy().
	//pjmedia_snd_deinit();
    }

    /* Reset RTP port */
    next_rtp_port = 0;

    return PJ_SUCCESS;
}


/* Create normal UDP media transports */
static pj_status_t create_udp_media_transports(pjsua_transport_config *cfg)
{
    unsigned i;
    pjmedia_sock_info skinfo;
    pj_status_t status;

    /* Create each media transport */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {

	status = create_rtp_rtcp_sock(cfg, &skinfo);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create RTP/RTCP socket",
		         status);
	    goto on_error;
	}

	status = pjmedia_transport_udp_attach(pjsua_var.med_endpt, NULL,
					      &skinfo, 0,
					      &pjsua_var.calls[i].med_tp);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create media transport",
		         status);
	    goto on_error;
	}

	pjmedia_transport_simulate_lost(pjsua_var.calls[i].med_tp,
					PJMEDIA_DIR_ENCODING,
					pjsua_var.media_cfg.tx_drop_pct);

	pjmedia_transport_simulate_lost(pjsua_var.calls[i].med_tp,
					PJMEDIA_DIR_DECODING,
					pjsua_var.media_cfg.rx_drop_pct);

    }

    return PJ_SUCCESS;

on_error:
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	if (pjsua_var.calls[i].med_tp != NULL) {
	    pjmedia_transport_close(pjsua_var.calls[i].med_tp);
	    pjsua_var.calls[i].med_tp = NULL;
	}
    }

    return status;
}


/* This callback is called when ICE negotiation completes */
static void on_ice_complete(pjmedia_transport *tp, pj_status_t result)
{
    unsigned id, c;
    pj_bool_t found = PJ_FALSE;

    /* We're only interested with failure case */
    if (result == PJ_SUCCESS)
	return;

    /* Find call which has this media transport */

    PJSUA_LOCK();

    for (id=0, c=0; id<PJSUA_MAX_CALLS && c<pjsua_var.call_cnt; ++id) {
	pjsua_call *call = &pjsua_var.calls[id];
	if (call->inv) {
	    ++c;

	    if (call->med_tp == tp) {
		call->media_st = PJSUA_CALL_MEDIA_ERROR;
		call->media_dir = PJMEDIA_DIR_NONE;
		found = PJ_TRUE;
		break;
	    }
	}
    }

    PJSUA_UNLOCK();

    if (found && pjsua_var.ua_cfg.cb.on_call_media_state) {
	pjsua_var.ua_cfg.cb.on_call_media_state(id);
    }
}


/* Create ICE media transports (when ice is enabled) */
static pj_status_t create_ice_media_transports(pjsua_transport_config *cfg)
{
    unsigned i;
    pj_sockaddr_in addr;
    pj_status_t status;

    /* Make sure STUN server resolution has completed */
    status = pjsua_resolve_stun_server(PJ_TRUE);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Error resolving STUN server", status);
	return status;
    }

    pj_sockaddr_in_init(&addr, 0, (pj_uint16_t)cfg->port);

    /* Create each media transport */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	pj_ice_strans_comp comp;
	pjmedia_ice_cb ice_cb;
	int next_port;
	char name[32];
#if PJMEDIA_ADVERTISE_RTCP
	enum { COMP_CNT=2 };
#else
	enum { COMP_CNT=1 };
#endif

	pj_bzero(&ice_cb, sizeof(pjmedia_ice_cb));
	ice_cb.on_ice_complete = &on_ice_complete;

	pj_ansi_snprintf(name, sizeof(name), "icetp%02d", i);
			 
	status = pjmedia_ice_create(pjsua_var.med_endpt, name, COMP_CNT,
				    &pjsua_var.stun_cfg, &ice_cb,
				    &pjsua_var.calls[i].med_tp);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Unable to create ICE media transport",
		         status);
	    goto on_error;
	}

	pjmedia_transport_simulate_lost(pjsua_var.calls[i].med_tp,
				        PJMEDIA_DIR_ENCODING,
				        pjsua_var.media_cfg.tx_drop_pct);

	pjmedia_transport_simulate_lost(pjsua_var.calls[i].med_tp,
				        PJMEDIA_DIR_DECODING,
				        pjsua_var.media_cfg.rx_drop_pct);

	status = pjmedia_ice_start_init(pjsua_var.calls[i].med_tp, 0, &addr,
				        &pjsua_var.stun_srv.ipv4, NULL);
	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Error starting ICE transport",
		         status);
	    goto on_error;
	}

	pjmedia_ice_get_comp(pjsua_var.calls[i].med_tp, 1, &comp);
	next_port = pj_ntohs(comp.local_addr.ipv4.sin_port);
	next_port += 2;
	addr.sin_port = pj_htons((pj_uint16_t)next_port);
    }

    /* Wait until all ICE transports are ready */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {

	/* Wait until interface status is PJ_SUCCESS */
	for (;;) {
	    status = pjmedia_ice_get_init_status(pjsua_var.calls[i].med_tp);
	    if (status == PJ_EPENDING)
		pjsua_handle_events(100);
	    else
		break;
	}

	if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, 
			 "Error adding STUN address to ICE media transport",
		         status);
	    goto on_error;
	}

    }

    return PJ_SUCCESS;

on_error:
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	if (pjsua_var.calls[i].med_tp != NULL) {
	    pjmedia_transport_close(pjsua_var.calls[i].med_tp);
	    pjsua_var.calls[i].med_tp = NULL;
	}
    }

    return status;
}


/*
 * Create UDP media transports for all the calls. This function creates
 * one UDP media transport for each call.
 */
PJ_DEF(pj_status_t) pjsua_media_transports_create(
			const pjsua_transport_config *app_cfg)
{
    pjsua_transport_config cfg;
    unsigned i;
    pj_status_t status;


    /* Make sure pjsua_init() has been called */
    PJ_ASSERT_RETURN(pjsua_var.ua_cfg.max_calls>0, PJ_EINVALIDOP);

    PJSUA_LOCK();

    /* Delete existing media transports */
    for (i=0; i<pjsua_var.ua_cfg.max_calls; ++i) {
	if (pjsua_var.calls[i].med_tp != NULL) {
	    pjmedia_transport_close(pjsua_var.calls[i].med_tp);
	    pjsua_var.calls[i].med_tp = NULL;
	}
    }

    /* Copy config */
    pjsua_transport_config_dup(pjsua_var.pool, &cfg, app_cfg);

    if (pjsua_var.media_cfg.enable_ice) {
	status = create_ice_media_transports(&cfg);
    } else {
	status = create_udp_media_transports(&cfg);
    }


    PJSUA_UNLOCK();

    return status;
}


pj_status_t pjsua_media_channel_init(pjsua_call_id call_id,
				     pjsip_role_e role,
				     int security_level,
				     int *sip_err_code)
{
    pjsua_call *call = &pjsua_var.calls[call_id];

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    pjsua_acc *acc = &pjsua_var.acc[call->acc_id];
    pjmedia_srtp_setting srtp_opt;
    pjmedia_transport *srtp;
    pj_status_t status;
#endif

    PJ_UNUSED_ARG(role);

    /* Return error if media transport has not been created yet
     * (e.g. application is starting)
     */
    if (call->med_tp == NULL) {
	return PJ_EBUSY;
    }

    /* Stop media transport (for good measure!) */
    pjmedia_transport_media_stop(call->med_tp);

#if defined(PJMEDIA_HAS_SRTP) && (PJMEDIA_HAS_SRTP != 0)
    /* Check if SRTP requires secure signaling */
    if (acc->cfg.use_srtp != PJMEDIA_SRTP_DISABLED) {
	if (security_level < acc->cfg.srtp_secure_signaling) {
	    if (sip_err_code)
		*sip_err_code = PJSIP_SC_NOT_ACCEPTABLE;
	    return PJSIP_ESESSIONINSECURE;
	}
    }

    /* Always create SRTP adapter */
    pjmedia_srtp_setting_default(&srtp_opt);
    srtp_opt.close_member_tp = PJ_FALSE;
    srtp_opt.use = acc->cfg.use_srtp;
    status = pjmedia_transport_srtp_create(pjsua_var.med_endpt, 
					   call->med_tp,
					   &srtp_opt, &srtp);
    if (status != PJ_SUCCESS) {
	if (sip_err_code)
	    *sip_err_code = PJSIP_SC_INTERNAL_SERVER_ERROR;
	return status;
    }

    /* Set SRTP as current media transport */
    call->med_orig = call->med_tp;
    call->med_tp = srtp;
#else
    call->med_orig = call->med_tp;
    PJ_UNUSED_ARG(security_level);
#endif

    return PJ_SUCCESS;
}

pj_status_t pjsua_media_channel_create_sdp(pjsua_call_id call_id, 
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *rem_sdp,
					   pjmedia_sdp_session **p_sdp,
					   int *sip_status_code)
{
    enum { MAX_MEDIA = 1, MEDIA_IDX = 0 };
    pjmedia_sdp_session *sdp;
    pjmedia_sock_info skinfo;
    pjsua_call *call = &pjsua_var.calls[call_id];
    pj_status_t status;

    /* Return error if media transport has not been created yet
     * (e.g. application is starting)
     */
    if (call->med_tp == NULL) {
	return PJ_EBUSY;
    }

    /* Get media socket info */
    pjmedia_transport_get_info(call->med_tp, &skinfo);

    /* Create SDP */
    status = pjmedia_endpt_create_sdp(pjsua_var.med_endpt, pool, MAX_MEDIA,
				      &skinfo, &sdp);
    if (status != PJ_SUCCESS) {
	if (sip_status_code) *sip_status_code = 500;
	goto on_error;
    }

    /* Add NAT info in the SDP */
    if (pjsua_var.ua_cfg.nat_type_in_sdp) {
	pjmedia_sdp_attr *a;
	pj_str_t value;
	char nat_info[80];

	value.ptr = nat_info;
	if (pjsua_var.ua_cfg.nat_type_in_sdp == 1) {
	    value.slen = pj_ansi_snprintf(nat_info, sizeof(nat_info),
					  "%d", pjsua_var.nat_type);
	} else {
	    const char *type_name = pj_stun_get_nat_name(pjsua_var.nat_type);
	    value.slen = pj_ansi_snprintf(nat_info, sizeof(nat_info),
					  "%d %s",
					  pjsua_var.nat_type,
					  type_name);
	}

	a = pjmedia_sdp_attr_create(pool, "X-nat", &value);

	pjmedia_sdp_attr_add(&sdp->attr_count, sdp->attr, a);

    }

    /* Give the SDP to media transport */
    status = pjmedia_transport_media_create(call->med_tp, pool, 0,
					    sdp, rem_sdp, MEDIA_IDX);
    if (status != PJ_SUCCESS) {
	if (sip_status_code) *sip_status_code = PJSIP_SC_NOT_ACCEPTABLE;
	goto on_error;
    }

    *p_sdp = sdp;
    return PJ_SUCCESS;

on_error:
    pjsua_media_channel_deinit(call_id);
    return status;

}


static void stop_media_session(pjsua_call_id call_id)
{
    pjsua_call *call = &pjsua_var.calls[call_id];

    if (call->conf_slot != PJSUA_INVALID_ID) {
	pjmedia_conf_remove_port(pjsua_var.mconf, call->conf_slot);
	call->conf_slot = PJSUA_INVALID_ID;
    }

    if (call->session) {
	if (pjsua_var.ua_cfg.cb.on_stream_destroyed) {
	    pjsua_var.ua_cfg.cb.on_stream_destroyed(call_id, call->session, 0);
	}

	pjmedia_session_destroy(call->session);
	call->session = NULL;

	PJ_LOG(4,(THIS_FILE, "Media session for call %d is destroyed", 
			     call_id));

    }

    call->media_st = PJSUA_CALL_MEDIA_NONE;
}

pj_status_t pjsua_media_channel_deinit(pjsua_call_id call_id)
{
    pjsua_call *call = &pjsua_var.calls[call_id];

    stop_media_session(call_id);

    pjmedia_transport_media_stop(call->med_tp);

    if (call->med_tp != call->med_orig) {
	pjmedia_transport_close(call->med_tp);
	call->med_tp = call->med_orig;
    }
    return PJ_SUCCESS;
}


/*
 * DTMF callback from the stream.
 */
static void dtmf_callback(pjmedia_stream *strm, void *user_data,
			  int digit)
{
    PJ_UNUSED_ARG(strm);

    /* For discussions about call mutex protection related to this 
     * callback, please see ticket #460:
     *	http://trac.pjsip.org/repos/ticket/460#comment:4
     */
    if (pjsua_var.ua_cfg.cb.on_dtmf_digit) {
	pjsua_call_id call_id;

	call_id = (pjsua_call_id)(long)user_data;
	pjsua_var.ua_cfg.cb.on_dtmf_digit(call_id, digit);
    }
}


pj_status_t pjsua_media_channel_update(pjsua_call_id call_id,
				       pjmedia_sdp_session *local_sdp,
				       const pjmedia_sdp_session *remote_sdp)
{
    unsigned i;
    int prev_media_st = 0;
    pjsua_call *call = &pjsua_var.calls[call_id];
    pjmedia_session_info sess_info;
    pjmedia_stream_info *si = NULL;
    pjmedia_port *media_port;
    pj_status_t status;

    /* Destroy existing media session, if any. */
    prev_media_st = call->media_st;
    stop_media_session(call->index);

    /* Create media session info based on SDP parameters. 
     */    
    status = pjmedia_session_info_from_sdp( call->inv->dlg->pool, 
					    pjsua_var.med_endpt, 
					    PJMEDIA_MAX_SDP_MEDIA, &sess_info,
					    local_sdp, remote_sdp);
    if (status != PJ_SUCCESS)
	return status;

    /* Find which session is audio (we only support audio for now) */
    for (i=0; i < sess_info.stream_cnt; ++i) {
	if (sess_info.stream_info[i].type == PJMEDIA_TYPE_AUDIO &&
	    (sess_info.stream_info[i].proto == PJMEDIA_TP_PROTO_RTP_AVP ||
	     sess_info.stream_info[i].proto == PJMEDIA_TP_PROTO_RTP_SAVP))
	{
	    si = &sess_info.stream_info[i];
	    break;
	}
    }

    if (si == NULL) {
	/* Not found */
	return PJMEDIA_EINVALIMEDIATYPE;
    }

    
    /* Reset session info with only one media stream */
    sess_info.stream_cnt = 1;
    if (si != &sess_info.stream_info[0])
	pj_memcpy(&sess_info.stream_info[0], si, sizeof(pjmedia_stream_info));

    /* Check if media is put on-hold */
    if (sess_info.stream_cnt == 0 || si->dir == PJMEDIA_DIR_NONE)
    {

	/* Determine who puts the call on-hold */
	if (prev_media_st == PJSUA_CALL_MEDIA_ACTIVE) {
	    if (pjmedia_sdp_neg_was_answer_remote(call->inv->neg)) {
		/* It was local who offer hold */
		call->media_st = PJSUA_CALL_MEDIA_LOCAL_HOLD;
	    } else {
		call->media_st = PJSUA_CALL_MEDIA_REMOTE_HOLD;
	    }
	}

	call->media_dir = PJMEDIA_DIR_NONE;

	/* Shutdown transport's session */
	pjmedia_transport_media_stop(call->med_tp);

	/* No need because we need keepalive? */

    } else {
	/* Start media transport */
	status = pjmedia_transport_media_start(call->med_tp, 
					       call->inv->pool,
					       local_sdp, remote_sdp, 0);
	if (status != PJ_SUCCESS)
	    return status;

	/* Override ptime, if this option is specified. */
	if (pjsua_var.media_cfg.ptime != 0) {
	    si->param->setting.frm_per_pkt = (pj_uint8_t)
		(pjsua_var.media_cfg.ptime / si->param->info.frm_ptime);
	    if (si->param->setting.frm_per_pkt == 0)
		si->param->setting.frm_per_pkt = 1;
	}

	/* Disable VAD, if this option is specified. */
	if (pjsua_var.media_cfg.no_vad) {
	    si->param->setting.vad = 0;
	}


	/* Optionally, application may modify other stream settings here
	 * (such as jitter buffer parameters, codec ptime, etc.)
	 */
	si->jb_init = pjsua_var.media_cfg.jb_init;
	si->jb_min_pre = pjsua_var.media_cfg.jb_min_pre;
	si->jb_max_pre = pjsua_var.media_cfg.jb_max_pre;
	si->jb_max = pjsua_var.media_cfg.jb_max;

	/* Set SSRC */
	si->ssrc = call->ssrc;

	/* Create session based on session info. */
	status = pjmedia_session_create( pjsua_var.med_endpt, &sess_info,
					 &call->med_tp,
					 call, &call->session );
	if (status != PJ_SUCCESS) {
	    return status;
	}

	/* If DTMF callback is installed by application, install our
	 * callback to the session.
	 */
	if (pjsua_var.ua_cfg.cb.on_dtmf_digit) {
	    pjmedia_session_set_dtmf_callback(call->session, 0, 
					      &dtmf_callback, 
					      (void*)(long)(call->index));
	}

	/* Get the port interface of the first stream in the session.
	 * We need the port interface to add to the conference bridge.
	 */
	pjmedia_session_get_port(call->session, 0, &media_port);

	/* Notify application about stream creation.
	 * Note: application may modify media_port to point to different
	 * media port
	 */
	if (pjsua_var.ua_cfg.cb.on_stream_created) {
	    pjsua_var.ua_cfg.cb.on_stream_created(call_id, call->session,
						  0, &media_port);
	}

	/*
	 * Add the call to conference bridge.
	 */
	{
	    char tmp[PJSIP_MAX_URL_SIZE];
	    pj_str_t port_name;

	    port_name.ptr = tmp;
	    port_name.slen = pjsip_uri_print(PJSIP_URI_IN_REQ_URI,
					     call->inv->dlg->remote.info->uri,
					     tmp, sizeof(tmp));
	    if (port_name.slen < 1) {
		port_name = pj_str("call");
	    }
	    status = pjmedia_conf_add_port( pjsua_var.mconf, call->inv->pool,
					    media_port, 
					    &port_name,
					    (unsigned*)&call->conf_slot);
	    if (status != PJ_SUCCESS) {
		return status;
	    }
	}

	/* Call's media state is active */
	call->media_st = PJSUA_CALL_MEDIA_ACTIVE;
	call->media_dir = si->dir;
    }

    /* Print info. */
    {
	char info[80];
	int info_len = 0;
	unsigned i;

	for (i=0; i<sess_info.stream_cnt; ++i) {
	    int len;
	    const char *dir;
	    pjmedia_stream_info *strm_info = &sess_info.stream_info[i];

	    switch (strm_info->dir) {
	    case PJMEDIA_DIR_NONE:
		dir = "inactive";
		break;
	    case PJMEDIA_DIR_ENCODING:
		dir = "sendonly";
		break;
	    case PJMEDIA_DIR_DECODING:
		dir = "recvonly";
		break;
	    case PJMEDIA_DIR_ENCODING_DECODING:
		dir = "sendrecv";
		break;
	    default:
		dir = "unknown";
		break;
	    }
	    len = pj_ansi_sprintf( info+info_len,
				   ", stream #%d: %.*s (%s)", i,
				   (int)strm_info->fmt.encoding_name.slen,
				   strm_info->fmt.encoding_name.ptr,
				   dir);
	    if (len > 0)
		info_len += len;
	}
	PJ_LOG(4,(THIS_FILE,"Media updates%s", info));
    }

    return PJ_SUCCESS;
}


/*
 * Get maxinum number of conference ports.
 */
PJ_DEF(unsigned) pjsua_conf_get_max_ports(void)
{
    return pjsua_var.media_cfg.max_media_ports;
}


/*
 * Get current number of active ports in the bridge.
 */
PJ_DEF(unsigned) pjsua_conf_get_active_ports(void)
{
    unsigned ports[PJSUA_MAX_CONF_PORTS];
    unsigned count = PJ_ARRAY_SIZE(ports);
    pj_status_t status;

    status = pjmedia_conf_enum_ports(pjsua_var.mconf, ports, &count);
    if (status != PJ_SUCCESS)
	count = 0;

    return count;
}


/*
 * Enumerate all conference ports.
 */
PJ_DEF(pj_status_t) pjsua_enum_conf_ports(pjsua_conf_port_id id[],
					  unsigned *count)
{
    return pjmedia_conf_enum_ports(pjsua_var.mconf, (unsigned*)id, count);
}


/*
 * Get information about the specified conference port
 */
PJ_DEF(pj_status_t) pjsua_conf_get_port_info( pjsua_conf_port_id id,
					      pjsua_conf_port_info *info)
{
    pjmedia_conf_port_info cinfo;
    unsigned i;
    pj_status_t status;

    status = pjmedia_conf_get_port_info( pjsua_var.mconf, id, &cinfo);
    if (status != PJ_SUCCESS)
	return status;

    pj_bzero(info, sizeof(*info));
    info->slot_id = id;
    info->name = cinfo.name;
    info->clock_rate = cinfo.clock_rate;
    info->channel_count = cinfo.channel_count;
    info->samples_per_frame = cinfo.samples_per_frame;
    info->bits_per_sample = cinfo.bits_per_sample;

    /* Build array of listeners */
    info->listener_cnt = cinfo.listener_cnt;
    for (i=0; i<cinfo.listener_cnt; ++i) {
	info->listeners[i] = cinfo.listener_slots[i];
    }

    return PJ_SUCCESS;
}


/*
 * Add arbitrary media port to PJSUA's conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_conf_add_port( pj_pool_t *pool,
					 pjmedia_port *port,
					 pjsua_conf_port_id *p_id)
{
    pj_status_t status;

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool,
				   port, NULL, (unsigned*)p_id);
    if (status != PJ_SUCCESS) {
	if (p_id)
	    *p_id = PJSUA_INVALID_ID;
    }

    return status;
}


/*
 * Remove arbitrary slot from the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_conf_remove_port(pjsua_conf_port_id id)
{
    return pjmedia_conf_remove_port(pjsua_var.mconf, (unsigned)id);
}


/*
 * Establish unidirectional media flow from souce to sink. 
 */
PJ_DEF(pj_status_t) pjsua_conf_connect( pjsua_conf_port_id source,
					pjsua_conf_port_id sink)
{
    return pjmedia_conf_connect_port(pjsua_var.mconf, source, sink, 0);
}


/*
 * Disconnect media flow from the source to destination port.
 */
PJ_DEF(pj_status_t) pjsua_conf_disconnect( pjsua_conf_port_id source,
					   pjsua_conf_port_id sink)
{
    return pjmedia_conf_disconnect_port(pjsua_var.mconf, source, sink);
}


/*
 * Adjust the signal level to be transmitted from the bridge to the 
 * specified port by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_tx_level(pjsua_conf_port_id slot,
					       float level)
{
    return pjmedia_conf_adjust_tx_level(pjsua_var.mconf, slot,
					(int)((level-1) * 128));
}

/*
 * Adjust the signal level to be received from the specified port (to
 * the bridge) by making it louder or quieter.
 */
PJ_DEF(pj_status_t) pjsua_conf_adjust_rx_level(pjsua_conf_port_id slot,
					       float level)
{
    return pjmedia_conf_adjust_rx_level(pjsua_var.mconf, slot,
					(int)((level-1) * 128));
}


/*
 * Get last signal level transmitted to or received from the specified port.
 */
PJ_DEF(pj_status_t) pjsua_conf_get_signal_level(pjsua_conf_port_id slot,
						unsigned *tx_level,
						unsigned *rx_level)
{
    return pjmedia_conf_get_signal_level(pjsua_var.mconf, slot, 
					 tx_level, rx_level);
}

/*****************************************************************************
 * File player.
 */

static char* get_basename(const char *path, unsigned len)
{
    char *p = ((char*)path) + len;

    if (len==0)
	return p;

    for (--p; p!=path && *p!='/' && *p!='\\'; ) --p;

    return (p==path) ? p : p+1;
}


/*
 * Create a file player, and automatically connect this player to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_player_create( const pj_str_t *filename,
					 unsigned options,
					 pjsua_player_id *p_id)
{
    unsigned slot, file_id;
    char path[PJ_MAXPATH];
    pj_pool_t *pool;
    pjmedia_port *port;
    pj_status_t status;

    if (pjsua_var.player_cnt >= PJ_ARRAY_SIZE(pjsua_var.player))
	return PJ_ETOOMANY;

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.player); ++file_id) {
	if (pjsua_var.player[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.player)) {
	/* This is unexpected */
	PJSUA_UNLOCK();
	pj_assert(0);
	return PJ_EBUG;
    }

    pj_memcpy(path, filename->ptr, filename->slen);
    path[filename->slen] = '\0';

    pool = pjsua_pool_create(get_basename(path, filename->slen), 1000, 1000);
    if (!pool) {
	PJSUA_UNLOCK();
	return PJ_ENOMEM;
    }

    status = pjmedia_wav_player_port_create(pool, path,
					    pjsua_var.mconf_cfg.samples_per_frame *
					      1000 / pjsua_var.media_cfg.clock_rate, 
					    options, 0, &port);
    if (status != PJ_SUCCESS) {
	PJSUA_UNLOCK();
	pjsua_perror(THIS_FILE, "Unable to open file for playback", status);
	pj_pool_release(pool);
	return status;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pjsua_var.pool, 
				   port, filename, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	PJSUA_UNLOCK();
	pjsua_perror(THIS_FILE, "Unable to add file to conference bridge", 
		     status);
	pj_pool_release(pool);
	return status;
    }

    pjsua_var.player[file_id].type = 0;
    pjsua_var.player[file_id].pool = pool;
    pjsua_var.player[file_id].port = port;
    pjsua_var.player[file_id].slot = slot;

    if (p_id) *p_id = file_id;

    ++pjsua_var.player_cnt;

    PJSUA_UNLOCK();
    return PJ_SUCCESS;
}


/*
 * Create a file playlist media port, and automatically add the port
 * to the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_playlist_create( const pj_str_t file_names[],
					   unsigned file_count,
					   const pj_str_t *label,
					   unsigned options,
					   pjsua_player_id *p_id)
{
    unsigned slot, file_id, ptime;
    pj_pool_t *pool;
    pjmedia_port *port;
    pj_status_t status;

    if (pjsua_var.player_cnt >= PJ_ARRAY_SIZE(pjsua_var.player))
	return PJ_ETOOMANY;

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.player); ++file_id) {
	if (pjsua_var.player[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.player)) {
	/* This is unexpected */
	PJSUA_UNLOCK();
	pj_assert(0);
	return PJ_EBUG;
    }


    ptime = pjsua_var.mconf_cfg.samples_per_frame * 1000 / 
	    pjsua_var.media_cfg.clock_rate;

    pool = pjsua_pool_create("playlist", 1000, 1000);
    if (!pool) {
	PJSUA_UNLOCK();
	return PJ_ENOMEM;
    }

    status = pjmedia_wav_playlist_create(pool, label, 
					 file_names, file_count,
					 ptime, options, 0, &port);
    if (status != PJ_SUCCESS) {
	PJSUA_UNLOCK();
	pjsua_perror(THIS_FILE, "Unable to create playlist", status);
	pj_pool_release(pool);
	return status;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool, 
				   port, &port->info.name, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	PJSUA_UNLOCK();
	pjsua_perror(THIS_FILE, "Unable to add port", status);
	pj_pool_release(pool);
	return status;
    }

    pjsua_var.player[file_id].type = 1;
    pjsua_var.player[file_id].pool = pool;
    pjsua_var.player[file_id].port = port;
    pjsua_var.player[file_id].slot = slot;

    if (p_id) *p_id = file_id;

    ++pjsua_var.player_cnt;

    PJSUA_UNLOCK();
    return PJ_SUCCESS;

}


/*
 * Get conference port ID associated with player.
 */
PJ_DEF(pjsua_conf_port_id) pjsua_player_get_conf_port(pjsua_player_id id)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);

    return pjsua_var.player[id].slot;
}

/*
 * Get the media port for the player.
 */
PJ_DEF(pj_status_t) pjsua_player_get_port( pjsua_player_id id,
					   pjmedia_port **p_port)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(p_port != NULL, PJ_EINVAL);
    
    *p_port = pjsua_var.player[id].port;

    return PJ_SUCCESS;
}

/*
 * Set playback position.
 */
PJ_DEF(pj_status_t) pjsua_player_set_pos( pjsua_player_id id,
					  pj_uint32_t samples)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].type == 0, PJ_EINVAL);

    return pjmedia_wav_player_port_set_pos(pjsua_var.player[id].port, samples);
}


/*
 * Close the file, remove the player from the bridge, and free
 * resources associated with the file player.
 */
PJ_DEF(pj_status_t) pjsua_player_destroy(pjsua_player_id id)
{
    PJ_ASSERT_RETURN(id>=0&&id<(int)PJ_ARRAY_SIZE(pjsua_var.player), PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.player[id].port != NULL, PJ_EINVAL);

    PJSUA_LOCK();

    if (pjsua_var.player[id].port) {
	pjmedia_conf_remove_port(pjsua_var.mconf, 
				 pjsua_var.player[id].slot);
	pjmedia_port_destroy(pjsua_var.player[id].port);
	pjsua_var.player[id].port = NULL;
	pjsua_var.player[id].slot = 0xFFFF;
	pj_pool_release(pjsua_var.player[id].pool);
	pjsua_var.player[id].pool = NULL;
	pjsua_var.player_cnt--;
    }

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*****************************************************************************
 * File recorder.
 */

/*
 * Create a file recorder, and automatically connect this recorder to
 * the conference bridge.
 */
PJ_DEF(pj_status_t) pjsua_recorder_create( const pj_str_t *filename,
					   unsigned enc_type,
					   void *enc_param,
					   pj_ssize_t max_size,
					   unsigned options,
					   pjsua_recorder_id *p_id)
{
    enum Format
    {
	FMT_UNKNOWN,
	FMT_WAV,
	FMT_MP3,
    };
    unsigned slot, file_id;
    char path[PJ_MAXPATH];
    pj_str_t ext;
    int file_format;
    pj_pool_t *pool;
    pjmedia_port *port;
    pj_status_t status;

    /* Filename must present */
    PJ_ASSERT_RETURN(filename != NULL, PJ_EINVAL);

    /* Don't support max_size at present */
    PJ_ASSERT_RETURN(max_size == 0 || max_size == -1, PJ_EINVAL);

    /* Don't support encoding type at present */
    PJ_ASSERT_RETURN(enc_type == 0, PJ_EINVAL);

    if (pjsua_var.rec_cnt >= PJ_ARRAY_SIZE(pjsua_var.recorder))
	return PJ_ETOOMANY;

    /* Determine the file format */
    ext.ptr = filename->ptr + filename->slen - 4;
    ext.slen = 4;

    if (pj_stricmp2(&ext, ".wav") == 0)
	file_format = FMT_WAV;
    else if (pj_stricmp2(&ext, ".mp3") == 0)
	file_format = FMT_MP3;
    else {
	PJ_LOG(1,(THIS_FILE, "pjsua_recorder_create() error: unable to "
			     "determine file format for %.*s",
			     (int)filename->slen, filename->ptr));
	return PJ_ENOTSUP;
    }

    PJSUA_LOCK();

    for (file_id=0; file_id<PJ_ARRAY_SIZE(pjsua_var.recorder); ++file_id) {
	if (pjsua_var.recorder[file_id].port == NULL)
	    break;
    }

    if (file_id == PJ_ARRAY_SIZE(pjsua_var.recorder)) {
	/* This is unexpected */
	PJSUA_UNLOCK();
	pj_assert(0);
	return PJ_EBUG;
    }

    pj_memcpy(path, filename->ptr, filename->slen);
    path[filename->slen] = '\0';

    pool = pjsua_pool_create(get_basename(path, filename->slen), 1000, 1000);
    if (!pool) {
	PJSUA_UNLOCK();
	return PJ_ENOMEM;
    }

    if (file_format == FMT_WAV) {
	status = pjmedia_wav_writer_port_create(pool, path, 
						pjsua_var.media_cfg.clock_rate, 
						pjsua_var.mconf_cfg.channel_count,
						pjsua_var.mconf_cfg.samples_per_frame,
						pjsua_var.mconf_cfg.bits_per_sample, 
						options, 0, &port);
    } else {
	PJ_UNUSED_ARG(enc_param);
	port = NULL;
	status = PJ_ENOTSUP;
    }

    if (status != PJ_SUCCESS) {
	PJSUA_UNLOCK();
	pjsua_perror(THIS_FILE, "Unable to open file for recording", status);
	pj_pool_release(pool);
	return status;
    }

    status = pjmedia_conf_add_port(pjsua_var.mconf, pool, 
				   port, filename, &slot);
    if (status != PJ_SUCCESS) {
	pjmedia_port_destroy(port);
	PJSUA_UNLOCK();
	pj_pool_release(pool);
	return status;
    }

    pjsua_var.recorder[file_id].port = port;
    pjsua_var.recorder[file_id].slot = slot;
    pjsua_var.recorder[file_id].pool = pool;

    if (p_id) *p_id = file_id;

    ++pjsua_var.rec_cnt;

    PJSUA_UNLOCK();
    return PJ_SUCCESS;
}


/*
 * Get conference port associated with recorder.
 */
PJ_DEF(pjsua_conf_port_id) pjsua_recorder_get_conf_port(pjsua_recorder_id id)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);

    return pjsua_var.recorder[id].slot;
}

/*
 * Get the media port for the recorder.
 */
PJ_DEF(pj_status_t) pjsua_recorder_get_port( pjsua_recorder_id id,
					     pjmedia_port **p_port)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(p_port != NULL, PJ_EINVAL);

    *p_port = pjsua_var.recorder[id].port;
    return PJ_SUCCESS;
}

/*
 * Destroy recorder (this will complete recording).
 */
PJ_DEF(pj_status_t) pjsua_recorder_destroy(pjsua_recorder_id id)
{
    PJ_ASSERT_RETURN(id>=0 && id<(int)PJ_ARRAY_SIZE(pjsua_var.recorder), 
		     PJ_EINVAL);
    PJ_ASSERT_RETURN(pjsua_var.recorder[id].port != NULL, PJ_EINVAL);

    PJSUA_LOCK();

    if (pjsua_var.recorder[id].port) {
	pjmedia_conf_remove_port(pjsua_var.mconf, 
				 pjsua_var.recorder[id].slot);
	pjmedia_port_destroy(pjsua_var.recorder[id].port);
	pjsua_var.recorder[id].port = NULL;
	pjsua_var.recorder[id].slot = 0xFFFF;
	pj_pool_release(pjsua_var.recorder[id].pool);
	pjsua_var.recorder[id].pool = NULL;
	pjsua_var.rec_cnt--;
    }

    PJSUA_UNLOCK();

    return PJ_SUCCESS;
}


/*****************************************************************************
 * Sound devices.
 */

/*
 * Enum sound devices.
 */
PJ_DEF(pj_status_t) pjsua_enum_snd_devs( pjmedia_snd_dev_info info[],
					 unsigned *count)
{
    unsigned i, dev_count;

    dev_count = pjmedia_snd_get_dev_count();
    
    if (dev_count > *count) dev_count = *count;

    for (i=0; i<dev_count; ++i) {
	const pjmedia_snd_dev_info *ci;

	ci = pjmedia_snd_get_dev_info(i);
	pj_memcpy(&info[i], ci, sizeof(*ci));
    }

    *count = dev_count;

    return PJ_SUCCESS;
}


/* Close existing sound device */
static void close_snd_dev(void)
{
    /* Close sound device */
    if (pjsua_var.snd_port) {
	const pjmedia_snd_dev_info *cap_info, *play_info;

	cap_info = pjmedia_snd_get_dev_info(pjsua_var.cap_dev);
	play_info = pjmedia_snd_get_dev_info(pjsua_var.play_dev);

	PJ_LOG(4,(THIS_FILE, "Closing %s sound playback device and "
			     "%s sound capture device",
			     play_info->name, cap_info->name));

	pjmedia_snd_port_disconnect(pjsua_var.snd_port);
	pjmedia_snd_port_destroy(pjsua_var.snd_port);
	pjsua_var.snd_port = NULL;
    }

    /* Close null sound device */
    if (pjsua_var.null_snd) {
	PJ_LOG(4,(THIS_FILE, "Closing null sound device.."));
	pjmedia_master_port_destroy(pjsua_var.null_snd, PJ_FALSE);
	pjsua_var.null_snd = NULL;
    }
}

/*
 * Select or change sound device. Application may call this function at
 * any time to replace current sound device.
 */
PJ_DEF(pj_status_t) pjsua_set_snd_dev( int capture_dev,
				       int playback_dev)
{
    pjmedia_port *conf_port;
    const pjmedia_snd_dev_info *play_info;
    unsigned clock_rates[] = {0, 22050, 44100, 48000, 32000, 16000, 
			      8000};
    unsigned selected_clock_rate = 0;
    unsigned i;
    pjmedia_snd_stream *strm;
    pjmedia_snd_stream_info si;
    pj_str_t tmp;
    pj_status_t status = -1;

    /* Close existing sound port */
    close_snd_dev();


    /* Set default clock rate */
    clock_rates[0] = pjsua_var.media_cfg.snd_clock_rate;
    if (clock_rates[0] == 0)
	clock_rates[0] = pjsua_var.media_cfg.clock_rate;

    /* Get the port0 of the conference bridge. */
    conf_port = pjmedia_conf_get_master_port(pjsua_var.mconf);
    pj_assert(conf_port != NULL);

    /* Attempts to open the sound device with different clock rates */
    for (i=0; i<PJ_ARRAY_SIZE(clock_rates); ++i) {
	char errmsg[PJ_ERR_MSG_SIZE];
	unsigned fps;

	PJ_LOG(4,(THIS_FILE, 
		  "pjsua_set_snd_dev(): attempting to open devices "
		  "@%d Hz", clock_rates[i]));

	/* Create the sound device. Sound port will start immediately. */
	fps = 1000 / pjsua_var.media_cfg.audio_frame_ptime;
	status = pjmedia_snd_port_create(pjsua_var.pool, capture_dev,
					 playback_dev, 
					 clock_rates[i], 1,
					 clock_rates[i]/fps,
					 16, 0, &pjsua_var.snd_port);

	if (status == PJ_SUCCESS) {
	    selected_clock_rate = clock_rates[i];

	    /* If there's mismatch between sound port and conference's port,
	     * create a resample port to bridge them.
	     */
	    if (selected_clock_rate != pjsua_var.media_cfg.clock_rate) {
		pjmedia_port *resample_port;
		unsigned resample_opt = 0;

		if (pjsua_var.media_cfg.quality >= 3 &&
		    pjsua_var.media_cfg.quality <= 4)
		{
		    resample_opt |= PJMEDIA_CONF_SMALL_FILTER;
		}
		else if (pjsua_var.media_cfg.quality < 3) {
		    resample_opt |= PJMEDIA_CONF_USE_LINEAR;
		}
		
		status = pjmedia_resample_port_create(pjsua_var.pool, 
						      conf_port,
						      selected_clock_rate,
						      resample_opt, 
						      &resample_port);
		if (status != PJ_SUCCESS) {
		    pj_strerror(status, errmsg, sizeof(errmsg));
		    PJ_LOG(4, (THIS_FILE, 
			       "Error creating resample port, trying next "
			       "clock rate", 
			       errmsg));
		    pjmedia_snd_port_destroy(pjsua_var.snd_port);
		    pjsua_var.snd_port = NULL;
		    continue;
		} else {
		    conf_port = resample_port;
		    break;
		}

	    } else {
		break;
	    }
	}

	pj_strerror(status, errmsg, sizeof(errmsg));
	PJ_LOG(4, (THIS_FILE, "..failed: %s", errmsg));
    }

    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to open sound device", status);
	return status;
    }

    /* Set AEC */
    pjmedia_snd_port_set_ec( pjsua_var.snd_port, pjsua_var.pool, 
			     pjsua_var.media_cfg.ec_tail_len, 
			     pjsua_var.media_cfg.ec_options);

    /* Connect sound port to the bridge */ 	 
    status = pjmedia_snd_port_connect(pjsua_var.snd_port, 	 
				      conf_port ); 	 
    if (status != PJ_SUCCESS) { 	 
	pjsua_perror(THIS_FILE, "Unable to connect conference port to "
			        "sound device", status); 	 
	pjmedia_snd_port_destroy(pjsua_var.snd_port); 	 
	pjsua_var.snd_port = NULL; 	 
	return status; 	 
    }

    /* Save the device IDs */
    pjsua_var.cap_dev = capture_dev;
    pjsua_var.play_dev = playback_dev;

    /* Update sound device name. */
    strm = pjmedia_snd_port_get_snd_stream(pjsua_var.snd_port);
    pjmedia_snd_stream_get_info(strm, &si);
    play_info = pjmedia_snd_get_dev_info(si.rec_id);

    if (si.clock_rate != pjsua_var.media_cfg.clock_rate) {
	char tmp_buf[128];
	int tmp_buf_len = sizeof(tmp_buf);

	tmp_buf_len = pj_ansi_snprintf(tmp_buf, sizeof(tmp_buf)-1, "%s (%dKHz)",
				       play_info->name, si.clock_rate/1000);
	pj_strset(&tmp, tmp_buf, tmp_buf_len);
        pjmedia_conf_set_port0_name(pjsua_var.mconf, &tmp); 
    } else {
        pjmedia_conf_set_port0_name(pjsua_var.mconf, 
				    pj_cstr(&tmp, play_info->name));
    }

    return PJ_SUCCESS;
}


/*
 * Get currently active sound devices. If sound devices has not been created
 * (for example when pjsua_start() is not called), it is possible that
 * the function returns PJ_SUCCESS with -1 as device IDs.
 */
PJ_DEF(pj_status_t) pjsua_get_snd_dev(int *capture_dev,
				      int *playback_dev)
{
    if (capture_dev) {
	*capture_dev = pjsua_var.cap_dev;
    }
    if (playback_dev) {
	*playback_dev = pjsua_var.play_dev;
    }

    return PJ_SUCCESS;
}


/*
 * Use null sound device.
 */
PJ_DEF(pj_status_t) pjsua_set_null_snd_dev(void)
{
    pjmedia_port *conf_port;
    pj_status_t status;

    /* Close existing sound device */
    close_snd_dev();

    PJ_LOG(4,(THIS_FILE, "Opening null sound device.."));

    /* Get the port0 of the conference bridge. */
    conf_port = pjmedia_conf_get_master_port(pjsua_var.mconf);
    pj_assert(conf_port != NULL);

    /* Create master port, connecting port0 of the conference bridge to
     * a null port.
     */
    status = pjmedia_master_port_create(pjsua_var.pool, pjsua_var.null_port,
					conf_port, 0, &pjsua_var.null_snd);
    if (status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "Unable to create null sound device",
		     status);
	return status;
    }

    /* Start the master port */
    status = pjmedia_master_port_start(pjsua_var.null_snd);
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    return PJ_SUCCESS;
}



/*
 * Use no device!
 */
PJ_DEF(pjmedia_port*) pjsua_set_no_snd_dev(void)
{
    /* Close existing sound device */
    close_snd_dev();

    pjsua_var.no_snd = PJ_TRUE;
    return pjmedia_conf_get_master_port(pjsua_var.mconf);
}


/*
 * Configure the AEC settings of the sound port.
 */
PJ_DEF(pj_status_t) pjsua_set_ec(unsigned tail_ms, unsigned options)
{
    pjsua_var.media_cfg.ec_tail_len = tail_ms;

    if (pjsua_var.snd_port)
	return pjmedia_snd_port_set_ec( pjsua_var.snd_port, pjsua_var.pool,
					tail_ms, options);
    
    return PJ_SUCCESS;
}


/*
 * Get current AEC tail length.
 */
PJ_DEF(pj_status_t) pjsua_get_ec_tail(unsigned *p_tail_ms)
{
    *p_tail_ms = pjsua_var.media_cfg.ec_tail_len;
    return PJ_SUCCESS;
}


/*****************************************************************************
 * Codecs.
 */

/*
 * Enum all supported codecs in the system.
 */
PJ_DEF(pj_status_t) pjsua_enum_codecs( pjsua_codec_info id[],
				       unsigned *p_count )
{
    pjmedia_codec_mgr *codec_mgr;
    pjmedia_codec_info info[32];
    unsigned i, count, prio[32];
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);
    count = PJ_ARRAY_SIZE(info);
    status = pjmedia_codec_mgr_enum_codecs( codec_mgr, &count, info, prio);
    if (status != PJ_SUCCESS) {
	*p_count = 0;
	return status;
    }

    if (count > *p_count) count = *p_count;

    for (i=0; i<count; ++i) {
	pjmedia_codec_info_to_id(&info[i], id[i].buf_, sizeof(id[i].buf_));
	id[i].codec_id = pj_str(id[i].buf_);
	id[i].priority = (pj_uint8_t) prio[i];
    }

    *p_count = count;

    return PJ_SUCCESS;
}


/*
 * Change codec priority.
 */
PJ_DEF(pj_status_t) pjsua_codec_set_priority( const pj_str_t *codec_id,
					      pj_uint8_t priority )
{
    pjmedia_codec_mgr *codec_mgr;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    return pjmedia_codec_mgr_set_codec_priority(codec_mgr, codec_id, 
					        priority);
}


/*
 * Get codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_codec_get_param( const pj_str_t *codec_id,
					   pjmedia_codec_param *param )
{
    const pjmedia_codec_info *info;
    pjmedia_codec_mgr *codec_mgr;
    unsigned count = 1;
    pj_status_t status;

    codec_mgr = pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt);

    status = pjmedia_codec_mgr_find_codecs_by_id(codec_mgr, codec_id,
						 &count, &info, NULL);
    if (status != PJ_SUCCESS)
	return status;

    if (count != 1)
	return PJ_ENOTFOUND;

    status = pjmedia_codec_mgr_get_default_param( codec_mgr, info, param);
    return status;
}


/*
 * Set codec parameters.
 */
PJ_DEF(pj_status_t) pjsua_codec_set_param( const pj_str_t *id,
					   const pjmedia_codec_param *param)
{
    PJ_UNUSED_ARG(id);
    PJ_UNUSED_ARG(param);
    PJ_TODO(set_codec_param);
    return PJ_SUCCESS;
}
