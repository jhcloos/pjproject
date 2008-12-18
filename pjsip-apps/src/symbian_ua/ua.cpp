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
#include "ua.h"

#define THIS_FILE	"symbian_ua.cpp"
#define LOG_LEVEL	3

//
// Basic config.
//
#define SIP_PORT	5060


//
// Destination URI (to make call, or to subscribe presence)
//
#define SIP_DST_URI	"sip:100@pjsip.lab"

//
// Account
//
#define HAS_SIP_ACCOUNT	0	// 1 to enable registration
#define SIP_DOMAIN	"pjsip.lab"
#define SIP_USER	"400"
#define SIP_PASSWD	"400"

//
// Outbound proxy for all accounts
//
#define SIP_PROXY	NULL
//#define SIP_PROXY	"<sip:192.168.0.8;lr>"


//
// Configure nameserver if DNS SRV is to be used with both SIP
// or STUN (for STUN see other settings below)
//
#define NAMESERVER	NULL
//#define NAMESERVER	"192.168.0.2"

//
// STUN server
#if 0
	// Use this to have the STUN server resolved normally
#   define STUN_DOMAIN	NULL
#   define STUN_SERVER	"stun.pjsip.org"
#elif 0
	// Use this to have the STUN server resolved with DNS SRV
#   define STUN_DOMAIN	"pjsip.org"
#   define STUN_SERVER	NULL
#else
	// Use this to disable STUN
#   define STUN_DOMAIN	NULL
#   define STUN_SERVER	NULL
#endif

//
// Use ICE?
//
#define USE_ICE		1

//
// Use SRTP?
//
#define USE_SRTP 	PJSUA_DEFAULT_USE_SRTP

//
// Globals
//
static pjsua_acc_id g_acc_id = PJSUA_INVALID_ID;
static pjsua_call_id g_call_id = PJSUA_INVALID_ID;
static pjsua_buddy_id g_buddy_id = PJSUA_INVALID_ID;


/* Callback called by the library upon receiving incoming call */
static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
			     pjsip_rx_data *rdata)
{
    pjsua_call_info ci;

    PJ_UNUSED_ARG(acc_id);
    PJ_UNUSED_ARG(rdata);

    if (g_call_id != PJSUA_INVALID_ID) {
    	pjsua_call_answer(call_id, PJSIP_SC_BUSY_HERE, NULL, NULL);
    	return;
    }
    
    pjsua_call_get_info(call_id, &ci);

    PJ_LOG(3,(THIS_FILE, "Incoming call from %.*s!!",
			 (int)ci.remote_info.slen,
			 ci.remote_info.ptr));

    g_call_id = call_id;
    
    /* Automatically answer incoming calls with 180/Ringing */
    pjsua_call_answer(call_id, 180, NULL, NULL);
}

/* Callback called by the library when call's state has changed */
static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
    pjsua_call_info ci;

    PJ_UNUSED_ARG(e);

    pjsua_call_get_info(call_id, &ci);
    
    if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
    	if (call_id == g_call_id)
    	    g_call_id = PJSUA_INVALID_ID;
    } else if (ci.state != PJSIP_INV_STATE_INCOMING) {
    	if (g_call_id == PJSUA_INVALID_ID)
    	    g_call_id = call_id;
    }
    
    PJ_LOG(3,(THIS_FILE, "Call %d state=%.*s", call_id,
			 (int)ci.state_text.slen,
			 ci.state_text.ptr));
}

/* Callback called by the library when call's media state has changed */
static void on_call_media_state(pjsua_call_id call_id)
{
    pjsua_call_info ci;

    pjsua_call_get_info(call_id, &ci);

    if (ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {
	// When media is active, connect call to sound device.
	pjsua_conf_connect(ci.conf_slot, 0);
	pjsua_conf_connect(0, ci.conf_slot);
    }
}


/* Handler on buddy state changed. */
static void on_buddy_state(pjsua_buddy_id buddy_id)
{
    pjsua_buddy_info info;
    pjsua_buddy_get_info(buddy_id, &info);

    PJ_LOG(3,(THIS_FILE, "%.*s status is %.*s",
	      (int)info.uri.slen,
	      info.uri.ptr,
	      (int)info.status_text.slen,
	      info.status_text.ptr));
}


/* Incoming IM message (i.e. MESSAGE request)!  */
static void on_pager(pjsua_call_id call_id, const pj_str_t *from, 
		     const pj_str_t *to, const pj_str_t *contact,
		     const pj_str_t *mime_type, const pj_str_t *text)
{
    /* Note: call index may be -1 */
    PJ_UNUSED_ARG(call_id);
    PJ_UNUSED_ARG(to);
    PJ_UNUSED_ARG(contact);
    PJ_UNUSED_ARG(mime_type);

    PJ_LOG(3,(THIS_FILE,"MESSAGE from %.*s: %.*s",
	      (int)from->slen, from->ptr,
	      (int)text->slen, text->ptr));
}


/* Received typing indication  */
static void on_typing(pjsua_call_id call_id, const pj_str_t *from,
		      const pj_str_t *to, const pj_str_t *contact,
		      pj_bool_t is_typing)
{
    PJ_UNUSED_ARG(call_id);
    PJ_UNUSED_ARG(to);
    PJ_UNUSED_ARG(contact);

    PJ_LOG(3,(THIS_FILE, "IM indication: %.*s %s",
	      (int)from->slen, from->ptr,
	      (is_typing?"is typing..":"has stopped typing")));
}


/* Call transfer request status. */
static void on_call_transfer_status(pjsua_call_id call_id,
				    int status_code,
				    const pj_str_t *status_text,
				    pj_bool_t final,
				    pj_bool_t *p_cont)
{
    PJ_LOG(3,(THIS_FILE, "Call %d: transfer status=%d (%.*s) %s",
	      call_id, status_code,
	      (int)status_text->slen, status_text->ptr,
	      (final ? "[final]" : "")));

    if (status_code/100 == 2) {
	PJ_LOG(3,(THIS_FILE, 
	          "Call %d: call transfered successfully, disconnecting call",
		  call_id));
	pjsua_call_hangup(call_id, PJSIP_SC_GONE, NULL, NULL);
	*p_cont = PJ_FALSE;
    }
}


/* NAT detection result */
static void on_nat_detect(const pj_stun_nat_detect_result *res) 
{
    if (res->status != PJ_SUCCESS) {
	pjsua_perror(THIS_FILE, "NAT detection failed", res->status);
    } else {
	PJ_LOG(3, (THIS_FILE, "NAT detected as %s", res->nat_type_name));
    }    
}

/* Notification that call is being replaced. */
static void on_call_replaced(pjsua_call_id old_call_id,
			     pjsua_call_id new_call_id)
{
    pjsua_call_info old_ci, new_ci;

    pjsua_call_get_info(old_call_id, &old_ci);
    pjsua_call_get_info(new_call_id, &new_ci);

    PJ_LOG(3,(THIS_FILE, "Call %d with %.*s is being replaced by "
			 "call %d with %.*s",
			 old_call_id, 
			 (int)old_ci.remote_info.slen, old_ci.remote_info.ptr,
			 new_call_id,
			 (int)new_ci.remote_info.slen, new_ci.remote_info.ptr));
}


//#include<e32debug.h>

/* Logging callback */
static void log_writer(int level, const char *buf, int len)
{
    static wchar_t buf16[PJ_LOG_MAX_SIZE];

    PJ_UNUSED_ARG(level);
    
    pj_ansi_to_unicode(buf, len, buf16, PJ_ARRAY_SIZE(buf16));

    TPtrC16 aBuf((const TUint16*)buf16, (TInt)len);
    //RDebug::Print(aBuf);
    console->Write(aBuf);
    
}

/*
 * app_startup()
 *
 * url may contain URL to call.
 */
static pj_status_t app_startup()
{
    pj_status_t status;

    /* Redirect log before pjsua_init() */
    pj_log_set_log_func(&log_writer);
    
    /* Set log level */
    pj_log_set_level(LOG_LEVEL);

    /* Create pjsua first! */
    status = pjsua_create();
    if (status != PJ_SUCCESS) {
    	pjsua_perror(THIS_FILE, "pjsua_create() error", status);
    	return status;
    }

    /* Init pjsua */
    pjsua_config cfg;
    pjsua_logging_config log_cfg;
    pjsua_media_config med_cfg;

    pjsua_config_default(&cfg);
    cfg.max_calls = 2;
    cfg.thread_cnt = 0; // Disable threading on Symbian
    cfg.use_srtp = USE_SRTP;
    cfg.srtp_secure_signaling = 0;
    
    cfg.cb.on_incoming_call = &on_incoming_call;
    cfg.cb.on_call_media_state = &on_call_media_state;
    cfg.cb.on_call_state = &on_call_state;
    cfg.cb.on_buddy_state = &on_buddy_state;
    cfg.cb.on_pager = &on_pager;
    cfg.cb.on_typing = &on_typing;
    cfg.cb.on_call_transfer_status = &on_call_transfer_status;
    cfg.cb.on_call_replaced = &on_call_replaced;
    cfg.cb.on_nat_detect = &on_nat_detect;
    
    if (SIP_PROXY) {
	    cfg.outbound_proxy_cnt = 1;
	    cfg.outbound_proxy[0] = pj_str(SIP_PROXY);
    }
    
    if (NAMESERVER) {
	    cfg.nameserver_count = 1;
	    cfg.nameserver[0] = pj_str(NAMESERVER);
    }
    
    if (NAMESERVER && STUN_DOMAIN) {
	    cfg.stun_domain = pj_str(STUN_DOMAIN);
    } else if (STUN_SERVER) {
	    cfg.stun_host = pj_str(STUN_SERVER);
    }
    
    
    pjsua_logging_config_default(&log_cfg);
    log_cfg.level = LOG_LEVEL;
    log_cfg.console_level = LOG_LEVEL;
    log_cfg.cb = &log_writer;
    //log_cfg.log_filename = pj_str("C:\\data\\symbian_ua.log");

    pjsua_media_config_default(&med_cfg);
    med_cfg.thread_cnt = 0; // Disable threading on Symbian
    med_cfg.has_ioqueue = PJ_FALSE;
    med_cfg.clock_rate = 8000;
#if defined(PJMEDIA_SYM_SND_USE_APS) && (PJMEDIA_SYM_SND_USE_APS==1)
    med_cfg.audio_frame_ptime = 20;
#else
    med_cfg.audio_frame_ptime = 40;
#endif
    med_cfg.ec_tail_len = 0;
    med_cfg.enable_ice = USE_ICE;
    med_cfg.snd_auto_close_time = 5; // wait for 5 seconds idle before sound dev get auto-closed
    
    status = pjsua_init(&cfg, &log_cfg, &med_cfg);
    if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "pjsua_init() error", status);
	    pjsua_destroy();
	    return status;
    }
    
    /* Adjust Speex priority and enable only the narrowband */
    {
        pj_str_t codec_id = pj_str("speex/8000");
        pjmedia_codec_mgr_set_codec_priority( 
        	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
        	&codec_id, PJMEDIA_CODEC_PRIO_NORMAL+1);

        codec_id = pj_str("speex/16000");
        pjmedia_codec_mgr_set_codec_priority( 
        	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
        	&codec_id, PJMEDIA_CODEC_PRIO_DISABLED);

        codec_id = pj_str("speex/32000");
        pjmedia_codec_mgr_set_codec_priority( 
        	pjmedia_endpt_get_codec_mgr(pjsua_var.med_endpt),
        	&codec_id, PJMEDIA_CODEC_PRIO_DISABLED);
    }
    
    /* Add UDP transport. */
    pjsua_transport_config tcfg;
    pjsua_transport_id tid;

    pjsua_transport_config_default(&tcfg);
    tcfg.port = SIP_PORT;
    status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &tcfg, &tid);
    if (status != PJ_SUCCESS) {
	    pjsua_perror(THIS_FILE, "Error creating transport", status);
	    pjsua_destroy();
	    return status;
    }

    /* Add account for the transport */
    pjsua_acc_add_local(tid, PJ_TRUE, &g_acc_id);


    /* Initialization is done, now start pjsua */
    status = pjsua_start();
    if (status != PJ_SUCCESS) {
    	pjsua_perror(THIS_FILE, "Error starting pjsua", status);
    	pjsua_destroy();
    	return status;
    }

    /* Register to SIP server by creating SIP account. */
    if (HAS_SIP_ACCOUNT) {
	pjsua_acc_config cfg;

	pjsua_acc_config_default(&cfg);
	cfg.id = pj_str("sip:" SIP_USER "@" SIP_DOMAIN);
	cfg.reg_uri = pj_str("sip:" SIP_DOMAIN);
	cfg.cred_count = 1;
	cfg.cred_info[0].realm = pj_str("*");
	cfg.cred_info[0].scheme = pj_str("digest");
	cfg.cred_info[0].username = pj_str(SIP_USER);
	cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	cfg.cred_info[0].data = pj_str(SIP_PASSWD);

	status = pjsua_acc_add(&cfg, PJ_TRUE, &g_acc_id);
	if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "Error adding account", status);
		pjsua_destroy();
		return status;
	}
    }

    if (SIP_DST_URI) {
    	pjsua_buddy_config bcfg;
    
    	pjsua_buddy_config_default(&bcfg);
    	bcfg.uri = pj_str(SIP_DST_URI);
    	bcfg.subscribe = PJ_FALSE;
    	
    	pjsua_buddy_add(&bcfg, &g_buddy_id);
    }
    return PJ_SUCCESS;
}


////////////////////////////////////////////////////////////////////////////
/*
 * The interractive console UI
 */
#include <e32base.h>

class ConsoleUI : public CActive 
{
public:
    ConsoleUI(CConsoleBase *con);

    // Run console UI
    void Run();

    // Stop
    void Stop();
    
protected:
    // Cancel asynchronous read.
    void DoCancel();

    // Implementation: called when read has completed.
    void RunL();
    
private:
    CConsoleBase *con_;
};


ConsoleUI::ConsoleUI(CConsoleBase *con) 
: CActive(EPriorityStandard), con_(con)
{
    CActiveScheduler::Add(this);
}

// Run console UI
void ConsoleUI::Run() 
{
    con_->Read(iStatus);
    SetActive();
}

// Stop console UI
void ConsoleUI::Stop() 
{
    DoCancel();
}

// Cancel asynchronous read.
void ConsoleUI::DoCancel() 
{
    con_->ReadCancel();
}

static void PrintMenu() 
{
    PJ_LOG(3, (THIS_FILE, "\n\n"
	    "Menu:\n"
	    "  d    Dump states\n"
	    "  D    Dump states detail\n"
	    "  P    Dump pool factory\n"
   	    "  l    Start loopback audio device\n"
   	    "  L    Stop loopback audio device\n"
	    "  m    Call " SIP_DST_URI "\n"
	    "  a    Answer call\n"
	    "  g    Hangup all calls\n"
	    "  s    Subscribe " SIP_DST_URI "\n"
	    "  S    Unsubscribe presence\n"
	    "  o    Set account online\n"
	    "  O    Set account offline\n"
	    "  w    Quit\n"));
}

// Implementation: called when read has completed.
void ConsoleUI::RunL() 
{
    TKeyCode kc = con_->KeyCode();
    pj_bool_t reschedule = PJ_TRUE;
    
    switch (kc) {
    case 'w':
	    CActiveScheduler::Stop();
	    reschedule = PJ_FALSE;
	    break;
    case 'D':
    case 'd':
	    pjsua_dump(kc == 'D');
	    break;
    case 'p':
    case 'P':
	    pj_pool_factory_dump(pjsua_get_pool_factory(), PJ_TRUE);
	    break;
    case 'l':
		pjsua_conf_connect(0, 0);
	    break;
    case 'L':
		pjsua_conf_disconnect(0, 0);
	    break;
    case 'm':
	    if (g_call_id != PJSUA_INVALID_ID) {
		    PJ_LOG(3,(THIS_FILE, "Another call is active"));	
		    break;
	    }
    
	    if (pjsua_verify_sip_url(SIP_DST_URI) == PJ_SUCCESS) {
		    pj_str_t dst = pj_str(SIP_DST_URI);
		    pjsua_call_make_call(g_acc_id, &dst, 0, NULL,
					 NULL, &g_call_id);
	    } else {
		    PJ_LOG(3,(THIS_FILE, "Invalid SIP URI"));
	    }
	    break;
    case 'a':
	    if (g_call_id != PJSUA_INVALID_ID)
		    pjsua_call_answer(g_call_id, 200, NULL, NULL);
	    break;
    case 'g':
	    pjsua_call_hangup_all();
	    break;
    case 's':
    case 'S':
	    if (g_buddy_id != PJSUA_INVALID_ID)
		    pjsua_buddy_subscribe_pres(g_buddy_id, kc=='s');
	    break;
    case 'o':
    case 'O':
	    pjsua_acc_set_online_status(g_acc_id, kc=='o');
	    break;
    default:
	    PJ_LOG(3,(THIS_FILE, "Keycode '%c' (%d) is pressed",
		      kc, kc));
	    break;
    }

    PrintMenu();
    
    if (reschedule)
	Run();
}

#if 0
// IP networking related testing
static pj_status_t test_addr(void)
{
	int af;
	unsigned i, count;
	pj_addrinfo ai[8];
	pj_sockaddr ifs[8];
	const pj_str_t *hostname;
	pj_hostent he;
	pj_status_t status;
	
	pj_log_set_log_func(&log_writer);
	
	status = pj_init();
	if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "pj_init() error", status);
		return status;
	}
	
	af = pj_AF_INET();
	
#if 0
	pj_in_addr in_addr;
	pj_str_t aa = pj_str("1.1.1.1");
	in_addr = pj_inet_addr(&aa);
	char *the_addr = pj_inet_ntoa(in_addr);
	PJ_LOG(3,(THIS_FILE, "IP addr=%s", the_addr));

	aa = pj_str("192.168.0.15");
	in_addr = pj_inet_addr(&aa);
	the_addr = pj_inet_ntoa(in_addr);
	PJ_LOG(3,(THIS_FILE, "IP addr=%s", the_addr));

	aa = pj_str("2.2.2.2");
	in_addr = pj_inet_addr(&aa);
	the_addr = pj_inet_ntoa(in_addr);
	PJ_LOG(3,(THIS_FILE, "IP addr=%s", the_addr));
	
	return -1;
#endif
	
	// Hostname
	hostname = pj_gethostname();
	if (hostname == NULL) {
		status = PJ_ERESOLVE;
		pjsua_perror(THIS_FILE, "pj_gethostname() error", status);
		goto on_return;
	}
	
	PJ_LOG(3,(THIS_FILE, "Hostname: %.*s", hostname->slen, hostname->ptr));
	
	// Gethostbyname
	status = pj_gethostbyname(hostname, &he);
	if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "pj_gethostbyname() error", status);
	} else {
		PJ_LOG(3,(THIS_FILE, "gethostbyname: %s", 
				  pj_inet_ntoa(*(pj_in_addr*)he.h_addr)));
	}
	
	// Getaddrinfo
	count = PJ_ARRAY_SIZE(ai);
	status = pj_getaddrinfo(af, hostname, &count, ai);
	if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "pj_getaddrinfo() error", status);
	} else {
		for (i=0; i<count; ++i) {
			char ipaddr[PJ_INET6_ADDRSTRLEN+2];
			PJ_LOG(3,(THIS_FILE, "Addrinfo: %s", 
					  pj_sockaddr_print(&ai[i].ai_addr, ipaddr, sizeof(ipaddr), 2)));
		}
	}
	
	// Enum interface
	count = PJ_ARRAY_SIZE(ifs);
	status = pj_enum_ip_interface(af, &count, ifs);
	if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "pj_enum_ip_interface() error", status);
	} else {
		for (i=0; i<count; ++i) {
			char ipaddr[PJ_INET6_ADDRSTRLEN+2];
			PJ_LOG(3,(THIS_FILE, "Interface: %s", 
					  pj_sockaddr_print(&ifs[i], ipaddr, sizeof(ipaddr), 2)));
		}
	}

	// Get default iinterface
	status = pj_getdefaultipinterface(af, &ifs[0]);
	if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "pj_getdefaultipinterface() error", status);
	} else {
		char ipaddr[PJ_INET6_ADDRSTRLEN+2];
		PJ_LOG(3,(THIS_FILE, "Default IP: %s", 
				  pj_sockaddr_print(&ifs[0], ipaddr, sizeof(ipaddr), 2)));
	}
	
	// Get default IP address
	status = pj_gethostip(af, &ifs[0]);
	if (status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "pj_gethostip() error", status);
	} else {
		char ipaddr[PJ_INET6_ADDRSTRLEN+2];
		PJ_LOG(3,(THIS_FILE, "Host IP: %s", 
				  pj_sockaddr_print(&ifs[0], ipaddr, sizeof(ipaddr), 2)));
	}
	
	status = -1;
	
on_return:
	pj_shutdown();
	return status;
}
#endif


#include <es_sock.h>

#if 0
// Force network connection to use the first IAP, 
// this is useful for debugging on emulator without GUI. 
// Include commdb.lib & apengine.lib in symbian_ua.mmp file
// if this is enabled.

#include <apdatahandler.h>

inline void ForceUseFirstIAP() 
{
    TUint32 rank = 1;
    TUint32 bearers;
    TUint32 prompt;
    TUint32 iap;

    CCommsDatabase* commDb = CCommsDatabase::NewL(EDatabaseTypeIAP);
    CleanupStack::PushL(commDb);

    CApDataHandler* apDataHandler = CApDataHandler::NewLC(*commDb);
    
    TCommDbConnectionDirection direction = ECommDbConnectionDirectionOutgoing;
    apDataHandler->GetPreferredIfDbIapTypeL(rank, direction, bearers, prompt, iap);
    prompt = ECommDbDialogPrefDoNotPrompt;
    apDataHandler->SetPreferredIfDbIapTypeL(rank, direction, bearers, (TCommDbDialogPref)prompt, iap, ETrue);
    CleanupStack::PopAndDestroy(2); // apDataHandler, commDb
}

static void SelectIAP() 
{
    ForceUseFirstIAP();
}

#else

static void SelectIAP() 
{
}

#endif


////////////////////////////////////////////////////////////////////////////
int ua_main() 
{
    RSocketServ aSocketServer;
    RConnection aConn;
    TInt err;
    pj_symbianos_params sym_params;
    pj_status_t status;

    SelectIAP();
    
    // Initialize RSocketServ
    if ((err=aSocketServer.Connect()) != KErrNone)
    	return PJ_STATUS_FROM_OS(err);
    
    // Open up a connection
    if ((err=aConn.Open(aSocketServer)) != KErrNone) {
	aSocketServer.Close();
	return PJ_STATUS_FROM_OS(err);
    }
    
    if ((err=aConn.Start()) != KErrNone) {
    	aSocketServer.Close();
    	return PJ_STATUS_FROM_OS(err);
    }
    
    // Set Symbian OS parameters in pjlib.
    // This must be done before pj_init() is called.
    pj_bzero(&sym_params, sizeof(sym_params));
    sym_params.rsocketserv = &aSocketServer;
    sym_params.rconnection = &aConn;
    pj_symbianos_set_params(&sym_params);
    
    // Initialize pjsua
    status  = app_startup();
    //status = test_addr();
    if (status != PJ_SUCCESS) {
    	aConn.Close();
    	aSocketServer.Close();
	return status;
    }

    // Run the UI
    ConsoleUI *con = new ConsoleUI(console);
    
    con->Run();
    PrintMenu();

    CActiveScheduler::Start();
    
    delete con;

    // Dump memory statistics
    PJ_LOG(3,(THIS_FILE, "Max heap usage: %u.%03uMB",
	      pjsua_var.cp.peak_used_size / 1000000,
	      (pjsua_var.cp.peak_used_size % 1000000)/1000));
    
    // check max stack usage
#if defined(PJ_OS_HAS_CHECK_STACK) && PJ_OS_HAS_CHECK_STACK!=0
	pj_thread_t* this_thread = pj_thread_this();
	if (!this_thread)
	    return status;
	
	const char* max_stack_file;
	int max_stack_line;
	status = pj_thread_get_stack_info(this_thread, &max_stack_file, &max_stack_line);
	
	PJ_LOG(3,(THIS_FILE, "Max stack usage: %u at %s:%d", 
		  pj_thread_get_stack_max_usage(this_thread), 
		  max_stack_file, max_stack_line));
#endif
	
    // Shutdown pjsua
    pjsua_destroy();
    
    // Close connection and socket server
    aConn.Close();
    aSocketServer.Close();
	
    return status;
}

