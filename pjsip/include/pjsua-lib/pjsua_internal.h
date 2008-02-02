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
#ifndef __PJSUA_INTERNAL_H__
#define __PJSUA_INTERNAL_H__

/** 
 * This is the private header used by pjsua library implementation. 
 * Applications should not include this file.
 */

PJ_BEGIN_DECL

/** 
 * Structure to be attached to invite dialog. 
 * Given a dialog "dlg", application can retrieve this structure
 * by accessing dlg->mod_data[pjsua.mod.id].
 */
typedef struct pjsua_call
{
    unsigned		 index;	    /**< Index in pjsua array.		    */
    pjsip_inv_session	*inv;	    /**< The invite session.		    */
    void		*user_data; /**< User/application data.		    */
    pjsip_status_code	 last_code; /**< Last status code seen.		    */
    pj_str_t		 last_text; /**< Last status text seen.		    */
    pj_time_val		 start_time;/**< First INVITE sent/received.	    */
    pj_time_val		 res_time;  /**< First response sent/received.	    */
    pj_time_val		 conn_time; /**< Connected/confirmed time.	    */
    pj_time_val		 dis_time;  /**< Disconnect time.		    */
    pjsua_acc_id	 acc_id;    /**< Account index being used.	    */
    int			 secure_level;/**< Signaling security level.	    */
    pjsua_call_media_status media_st;/**< Media state.			    */
    pjmedia_dir		 media_dir; /**< Media direction.		    */
    pjmedia_session	*session;   /**< The media session.		    */
    pj_uint32_t		 ssrc;	    /**< RTP SSRC			    */
    int			 conf_slot; /**< Slot # in conference bridge.	    */
    pjsip_evsub		*xfer_sub;  /**< Xfer server subscription, if this
					 call was triggered by xfer.	    */
    pjmedia_transport	*med_tp;    /**< Current media transport.	    */
    pjmedia_transport	*med_orig;  /**< Original media transport	    */
    pj_timer_entry	 refresh_tm;/**< Timer to send re-INVITE.	    */
    pj_timer_entry	 hangup_tm; /**< Timer to hangup call.		    */
    pj_stun_nat_type	 rem_nat_type; /**< NAT type of remote endpoint.    */

    char    last_text_buf_[128];    /**< Buffer for last_text.		    */

} pjsua_call;


/**
 * Server presence subscription list head.
 */
typedef struct pjsua_srv_pres
{
    PJ_DECL_LIST_MEMBER(struct pjsua_srv_pres);
    pjsip_evsub	    *sub;
    char	    *remote;
} pjsua_srv_pres;


/**
 * Account
 */
typedef struct pjsua_acc
{
    pjsua_acc_config cfg;	    /**< Account configuration.		*/
    pj_bool_t	     valid;	    /**< Is this account valid?		*/

    int		     index;	    /**< Index in accounts array.	*/
    pj_str_t	     display;	    /**< Display name, if any.		*/
    pj_str_t	     user_part;	    /**< User part of local URI.	*/

    pj_str_t	     srv_domain;    /**< Host part of reg server.	*/
    int		     srv_port;	    /**< Port number of reg server.	*/

    pjsip_regc	    *regc;	    /**< Client registration session.   */
    pj_status_t	     reg_last_err;  /**< Last registration error.	*/
    int		     reg_last_code; /**< Last status last register.	*/

    pj_timer_entry   ka_timer;	    /**< Keep-alive timer for UDP.	*/
    pjsip_transport *ka_transport;  /**< Transport for keep-alive.	*/
    pj_sockaddr	     ka_target;	    /**< Destination address for K-A	*/
    unsigned	     ka_target_len; /**< Length of ka_target.		*/

    pjsip_route_hdr  route_set;	    /**< Complete route set inc. outbnd.*/

    unsigned	     cred_cnt;	    /**< Number of credentials.		*/
    pjsip_cred_info  cred[PJSUA_ACC_MAX_PROXIES]; /**< Complete creds.	*/

    pj_bool_t	     online_status; /**< Our online status.		*/
    pjrpid_element   rpid;	    /**< RPID element information.	*/
    pjsua_srv_pres   pres_srv_list; /**< Server subscription list.	*/
    pjsip_publishc  *publish_sess;  /**< Client publication session.	*/
    pj_bool_t	     publish_state; /**< Last published online status	*/

} pjsua_acc;


/**
 *Transport.
 */
typedef struct pjsua_transport_data
{
    int			     index;
    pjsip_transport_type_e   type;
    pjsip_host_port	     local_name;

    union {
	pjsip_transport	    *tp;
	pjsip_tpfactory	    *factory;
	void		    *ptr;
    } data;

} pjsua_transport_data;


/**
 * Buddy data.
 */
typedef struct pjsua_buddy
{
    unsigned		 index;	    /**< Buddy index.			*/
    pj_str_t		 uri;	    /**< Buddy URI.			*/
    pj_str_t		 contact;   /**< Contact learned from subscrp.	*/
    pj_str_t		 name;	    /**< Buddy name.			*/
    pj_str_t		 display;   /**< Buddy display name.		*/
    pj_str_t		 host;	    /**< Buddy host.			*/
    unsigned		 port;	    /**< Buddy port.			*/
    pj_bool_t		 monitor;   /**< Should we monitor?		*/
    pjsip_dialog	*dlg;	    /**< The underlying dialog.		*/
    pjsip_evsub		*sub;	    /**< Buddy presence subscription	*/
    pjsip_pres_status	 status;    /**< Buddy presence status.		*/

} pjsua_buddy;


/**
 * File player/recorder data.
 */
typedef struct pjsua_file_data
{
    pj_bool_t	     type;  /* 0=player, 1=playlist */
    pjmedia_port    *port;
    pj_pool_t	    *pool;
    unsigned	     slot;
} pjsua_file_data;


/**
 * Additional parameters for conference bridge.
 */
typedef struct pjsua_conf_setting
{
    unsigned	channel_count;
    unsigned	samples_per_frame;
    unsigned	bits_per_sample;
} pjsua_conf_setting;


/**
 * Global pjsua application data.
 */
struct pjsua_data
{

    /* Control: */
    pj_caching_pool	 cp;	    /**< Global pool factory.		*/
    pj_pool_t		*pool;	    /**< pjsua's private pool.		*/
    pj_mutex_t		*mutex;	    /**< Mutex protection for this data	*/

    /* Logging: */
    pjsua_logging_config log_cfg;   /**< Current logging config.	*/
    pj_oshandle_t	 log_file;  /**<Output log file handle		*/

    /* SIP: */
    pjsip_endpoint	*endpt;	    /**< Global endpoint.		*/
    pjsip_module	 mod;	    /**< pjsua's PJSIP module.		*/
    pjsua_transport_data tpdata[8]; /**< Array of transports.		*/

    /* Threading: */
    pj_bool_t		 thread_quit_flag;  /**< Thread quit flag.	*/
    pj_thread_t		*thread[4];	    /**< Array of threads.	*/

    /* STUN and resolver */
    pj_stun_config	 stun_cfg;  /**< Global STUN settings.		*/
    pj_sockaddr		 stun_srv;  /**< Resolved STUN server address	*/
    pj_status_t		 stun_status; /**< STUN server status.		*/
    pj_dns_resolver	*resolver;  /**< DNS resolver.			*/

    /* Detected NAT type */
    pj_stun_nat_type	 nat_type;	/**< NAT type.			*/
    pj_status_t		 nat_status;	/**< Detection status.		*/
    pj_bool_t		 nat_in_progress; /**< Detection in progress	*/

    /* Account: */
    unsigned		 acc_cnt;	     /**< Number of accounts.	*/
    pjsua_acc_id	 default_acc;	     /**< Default account ID	*/
    pjsua_acc		 acc[PJSUA_MAX_ACC]; /**< Account array.	*/
    pjsua_acc_id	 acc_ids[PJSUA_MAX_ACC]; /**< Acc sorted by prio*/

    /* Calls: */
    pjsua_config	 ua_cfg;		/**< UA config.		*/
    unsigned		 call_cnt;		/**< Call counter.	*/
    pjsua_call		 calls[PJSUA_MAX_CALLS];/**< Calls array.	*/
    pjsua_call_id	 next_call_id;		/**< Next call id to use*/

    /* Buddy; */
    unsigned		 buddy_cnt;		    /**< Buddy count.	*/
    pjsua_buddy		 buddy[PJSUA_MAX_BUDDIES];  /**< Buddy array.	*/

    /* Presence: */
    pj_timer_entry	 pres_timer;/**< Presence refresh timer.	*/

    /* Media: */
    pjsua_media_config   media_cfg; /**< Media config.			*/
    pjmedia_endpt	*med_endpt; /**< Media endpoint.		*/
    pjsua_conf_setting	 mconf_cfg; /**< Additionan conf. bridge. param */
    pjmedia_conf	*mconf;	    /**< Conference bridge.		*/
    int			 cap_dev;   /**< Capture device ID.		*/
    int			 play_dev;  /**< Playback device ID.		*/
    pj_bool_t		 no_snd;    /**< No sound (app will manage it)	*/
    pjmedia_snd_port	*snd_port;  /**< Sound port.			*/
    pjmedia_master_port	*null_snd;  /**< Master port for null sound.	*/
    pjmedia_port	*null_port; /**< Null port.			*/


    /* File players: */
    unsigned		 player_cnt;/**< Number of file players.	*/
    pjsua_file_data	 player[PJSUA_MAX_PLAYERS];/**< Array of players.*/

    /* File recorders: */
    unsigned		 rec_cnt;   /**< Number of file recorders.	*/
    pjsua_file_data	 recorder[PJSUA_MAX_RECORDERS];/**< Array of recs.*/
};


extern struct pjsua_data pjsua_var;

/**
 * Get the instance of pjsua
 */
PJ_DECL(struct pjsua_data*) pjsua_get_var(void);



/**
 * IM callback data.
 */
typedef struct pjsua_im_data
{
    pjsua_acc_id     acc_id;
    pjsua_call_id    call_id;
    pj_str_t	     to;
    pj_str_t	     body;
    void	    *user_data;
} pjsua_im_data;


/**
 * Duplicate IM data.
 */
PJ_INLINE(pjsua_im_data*) pjsua_im_data_dup(pj_pool_t *pool, 
					    const pjsua_im_data *src)
{
    pjsua_im_data *dst;

    dst = (pjsua_im_data*) pj_pool_alloc(pool, sizeof(*dst));
    dst->acc_id = src->acc_id;
    dst->call_id = src->call_id;
    pj_strdup_with_null(pool, &dst->to, &src->to);
    dst->user_data = src->user_data;
    pj_strdup_with_null(pool, &dst->body, &src->body);

    return dst;
}


#if 1
#define PJSUA_LOCK()	    pj_mutex_lock(pjsua_var.mutex)
#define PJSUA_TRY_LOCK()    pj_mutex_trylock(pjsua_var.mutex)
#define PJSUA_UNLOCK()	    pj_mutex_unlock(pjsua_var.mutex)
#else
#define PJSUA_LOCK()
#define PJSUA_TRY_LOCK()    PJ_SUCCESS
#define PJSUA_UNLOCK()
#endif


/**
 * Resolve STUN server.
 */
pj_status_t pjsua_resolve_stun_server(pj_bool_t wait);

/**
 * Handle incoming invite request.
 */
pj_bool_t pjsua_call_on_incoming(pjsip_rx_data *rdata);

/*
 * Media channel.
 */
pj_status_t pjsua_media_channel_init(pjsua_call_id call_id,
				     pjsip_role_e role,
				     int security_level,
				     int *sip_err_code);
pj_status_t pjsua_media_channel_create_sdp(pjsua_call_id call_id, 
					   pj_pool_t *pool,
					   const pjmedia_sdp_session *rem_sdp,
					   pjmedia_sdp_session **p_sdp,
					   int *sip_err_code);
pj_status_t pjsua_media_channel_update(pjsua_call_id call_id,
				       pjmedia_sdp_session *local_sdp,
				       const pjmedia_sdp_session *remote_sdp);
pj_status_t pjsua_media_channel_deinit(pjsua_call_id call_id);


/**
 * Init presence.
 */
pj_status_t pjsua_pres_init();

/*
 * Start presence subsystem.
 */
pj_status_t pjsua_pres_start(void);

/**
 * Refresh presence subscriptions
 */
void pjsua_pres_refresh(void);

/*
 * Update server subscription (e.g. when our online status has changed)
 */
void pjsua_pres_update_acc(int acc_id, pj_bool_t force);

/*
 * Shutdown presence.
 */
void pjsua_pres_shutdown(void);

/**
 * Init presence for aoocunt.
 */
pj_status_t pjsua_pres_init_acc(int acc_id);

/**
 * Send PUBLISH
 */
pj_status_t pjsua_pres_init_publish_acc(int acc_id);

/**
 * Terminate server subscription for the account 
 */
void pjsua_pres_delete_acc(int acc_id);

/**
 * Init IM module handler to handle incoming MESSAGE outside dialog.
 */
pj_status_t pjsua_im_init(void);

/**
 * Init call subsystem.
 */
pj_status_t pjsua_call_subsys_init(const pjsua_config *cfg);

/**
 * Start call subsystem.
 */
pj_status_t pjsua_call_subsys_start(void);

/**
 * Init media subsystems.
 */
pj_status_t pjsua_media_subsys_init(const pjsua_media_config *cfg);

/**
 * Start pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_start(void);

/**
 * Destroy pjsua media subsystem.
 */
pj_status_t pjsua_media_subsys_destroy(void);

/**
 * Private: check if we can accept the message.
 *	    If not, then p_accept header will be filled with a valid
 *	    Accept header.
 */
pj_bool_t pjsua_im_accept_pager(pjsip_rx_data *rdata,
				pjsip_accept_hdr **p_accept_hdr);

/**
 * Private: process pager message.
 *	    This may trigger pjsua_ui_on_pager() or pjsua_ui_on_typing().
 */
void pjsua_im_process_pager(int call_id, const pj_str_t *from,
			    const pj_str_t *to, pjsip_rx_data *rdata);


/**
 * Create Accept header for MESSAGE.
 */
pjsip_accept_hdr* pjsua_im_create_accept(pj_pool_t *pool);

/*
 * Add additional headers etc in msg_data specified by application
 * when sending requests.
 */
void pjsua_process_msg_data(pjsip_tx_data *tdata,
			    const pjsua_msg_data *msg_data);


/*
 * Add route_set to outgoing requests
 */
void pjsua_set_msg_route_set( pjsip_tx_data *tdata,
			      const pjsip_route_hdr *route_set );


/*
 * Simple version of MIME type parsing (it doesn't support parameters)
 */
void pjsua_parse_media_type( pj_pool_t *pool,
			     const pj_str_t *mime,
			     pjsip_media_type *media_type);


/*
 * Internal function to init transport selector from transport id.
 */
void pjsua_init_tpselector(pjsua_transport_id tp_id,
			   pjsip_tpselector *sel);


pj_status_t acquire_call(const char *title,
                         pjsua_call_id call_id,
                         pjsua_call **p_call,
                         pjsip_dialog **p_dlg);
const char *good_number(char *buf, pj_int32_t val);
void print_call(const char *title,
                int call_id,
                char *buf, pj_size_t size);


PJ_END_DECL

#endif	/* __PJSUA_INTERNAL_H__ */

