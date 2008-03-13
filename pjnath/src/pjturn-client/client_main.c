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
#include <pjnath.h>
#include <pjlib-util.h>
#include <pjlib.h>


#define THIS_FILE	"client_main.c"
#define LOCAL_PORT	1998
#define BANDWIDTH	64		    /* -1 to disable */
#define LIFETIME	600		    /* -1 to disable */
#define REQ_TRANSPORT	-1		    /* 0: udp, 1: tcp, -1: disable */
#define REQ_PORT_PROPS	-1		    /* -1 to disable */
#define REQ_IP		0		    /* IP address string */

//#define OPTIONS		PJ_STUN_NO_AUTHENTICATE
#define OPTIONS		0


struct peer
{
    pj_sock_t	sock;
    pj_sockaddr	addr;
};


static struct global
{
    pj_caching_pool	 cp;
    pj_pool_t		*pool;
    pj_stun_config	 stun_config;
    pj_thread_t		*thread;
    pj_bool_t		 quit;

    pj_turn_udp		*udp_rel;
    pj_sockaddr		 relay_addr;

    struct peer		 peer[2];
} g;

static struct options
{
    char    *srv_addr;
    char    *srv_port;
    char    *realm;
    char    *user_name;
    char    *password;
    char    *nonce;
    pj_bool_t use_fingerprint;
} o;


static int worker_thread(void *unused);
static pj_status_t parse_addr(const char *input, pj_sockaddr_in *addr);
static void turn_on_rx_data(pj_turn_udp *udp_rel,
			    const pj_uint8_t *pkt,
			    unsigned pkt_len,
			    const pj_sockaddr_t *peer_addr,
			    unsigned addr_len);
static void turn_on_state(pj_turn_udp *udp_rel, pj_turn_state_t old_state,
			  pj_turn_state_t new_state);


static void my_perror(const char *title, pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];
    pj_strerror(status, errmsg, sizeof(errmsg));

    PJ_LOG(3,(THIS_FILE, "%s: %s", title, errmsg));
}

#define CHECK(expr)	status=expr; \
			if (status!=PJ_SUCCESS) { \
			    my_perror(#expr, status); \
			    return status; \
			}

static int init()
{
    int i;
    pj_status_t status;

    CHECK( pj_init() );
    CHECK( pjlib_util_init() );
    CHECK( pjnath_init() );

    /* Check that server is specified */
    if (!o.srv_addr) {
	printf("Error: server must be specified\n");
	return PJ_EINVAL;
    }

    pj_caching_pool_init(&g.cp, &pj_pool_factory_default_policy, 0);

    g.pool = pj_pool_create(&g.cp.factory, "main", 1000, 1000, NULL);

    /* Init global STUN config */
    pj_stun_config_init(&g.stun_config, &g.cp.factory, 0, NULL, NULL);

    /* Create global timer heap */
    CHECK( pj_timer_heap_create(g.pool, 1000, &g.stun_config.timer_heap) );

    /* Create global ioqueue */
    CHECK( pj_ioqueue_create(g.pool, 16, &g.stun_config.ioqueue) );

    /* 
     * Create peers
     */
    for (i=0; i<PJ_ARRAY_SIZE(g.peer); ++i) {
	int len;
	pj_sockaddr addr;
	pj_uint16_t port;

	CHECK( pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &g.peer[i].sock) );
	CHECK( pj_sock_bind_in(g.peer[i].sock, 0, 0) );

	len = sizeof(addr);
	CHECK( pj_sock_getsockname(g.peer[i].sock, &addr, &len) );
	port = pj_sockaddr_get_port(&g.peer[i].addr);

	CHECK( pj_gethostip(pj_AF_INET(), &g.peer[i].addr) );
	pj_sockaddr_set_port(&g.peer[0].addr, port);

    }

    /* Start the worker thread */
    CHECK( pj_thread_create(g.pool, "stun", &worker_thread, NULL, 0, 0, &g.thread) );


    return PJ_SUCCESS;
}


static int shutdown()
{
    unsigned i;

    if (g.thread) {
	g.quit = 1;
	pj_thread_join(g.thread);
	pj_thread_destroy(g.thread);
	g.thread = NULL;
    }
    if (g.udp_rel) {
	pj_turn_udp_destroy(g.udp_rel);
	g.udp_rel = NULL;
    }
    for (i=0; i<PJ_ARRAY_SIZE(g.peer); ++i) {
	if (g.peer[i].sock) {
	    pj_sock_close(g.peer[i].sock);
	    g.peer[i].sock = 0;
	}
    }
    if (g.stun_config.timer_heap) {
	pj_timer_heap_destroy(g.stun_config.timer_heap);
	g.stun_config.timer_heap = NULL;
    }
    if (g.stun_config.ioqueue) {
	pj_ioqueue_destroy(g.stun_config.ioqueue);
	g.stun_config.ioqueue = NULL;
    }
    if (g.pool) {
	pj_pool_release(g.pool);
	g.pool = NULL;
    }
    pj_pool_factory_dump(&g.cp.factory, PJ_TRUE);
    pj_caching_pool_destroy(&g.cp);

    return PJ_SUCCESS;
}


static int worker_thread(void *unused)
{
    PJ_UNUSED_ARG(unused);

    while (!g.quit) {
	const pj_time_val delay = {0, 10};
	int i;
	pj_fd_set_t readset;

	/* Poll ioqueue for the TURN client */
	pj_ioqueue_poll(g.stun_config.ioqueue, &delay);

	/* Poll the timer heap */
	pj_timer_heap_poll(g.stun_config.timer_heap, NULL);

	/* Poll peer sockets */
	PJ_FD_ZERO(&readset);
	for (i=0; i<PJ_ARRAY_SIZE(g.peer); ++i) {
	    PJ_FD_SET(g.peer[i].sock, &readset);
	}

	if (pj_sock_select(64, &readset, NULL, NULL, &delay) <= 0)
	    continue;

	/* Handle incoming data on peer socket */
	for (i=0; i<PJ_ARRAY_SIZE(g.peer); ++i) {
	    char buf[128];
	    pj_ssize_t len;
	    pj_sockaddr src_addr;
	    int src_addr_len;
	    pj_status_t status;

	    if (!PJ_FD_ISSET(g.peer[i].sock, &readset))
		continue;

	    len = sizeof(buf);
	    src_addr_len = sizeof(src_addr);

	    status = pj_sock_recvfrom(g.peer[i].sock, buf, &len, 0, 
				      &src_addr, &src_addr_len);
	    if (status != PJ_SUCCESS) {
		my_perror("recvfrom error", status);
	    } else {
		char addrinfo[80];
		pj_sockaddr_print(&src_addr, addrinfo, sizeof(addrinfo), 3);
		PJ_LOG(3,(THIS_FILE, "Peer%d received %d bytes from %s: %.*s",
			  i, len, addrinfo, len, buf));
	    }
	}
    }

    return 0;
}

static pj_status_t create_relay(void)
{
    pj_turn_udp_cb rel_cb;
    pj_stun_auth_cred cred;
    pj_str_t srv;
    pj_status_t status;

    if (g.udp_rel) {
	PJ_LOG(1,(THIS_FILE, "Relay already created"));
	return -1;
    }

    pj_bzero(&rel_cb, sizeof(rel_cb));
    rel_cb.on_rx_data = &turn_on_rx_data;
    rel_cb.on_state = &turn_on_state;
    CHECK( pj_turn_udp_create(&g.stun_config, pj_AF_INET(), &rel_cb, 0,
			      NULL, &g.udp_rel) );

    if (o.user_name) {
	pj_bzero(&cred, sizeof(cred));
	cred.type = PJ_STUN_AUTH_CRED_STATIC;
	cred.data.static_cred.realm = pj_str(o.realm);
	cred.data.static_cred.username = pj_str(o.user_name);
	cred.data.static_cred.data_type = 0;
	cred.data.static_cred.data = pj_str(o.password);
	cred.data.static_cred.nonce = pj_str(o.nonce);
    } else {
	PJ_LOG(2,(THIS_FILE, "Warning: no credential is set"));
    }

    srv = pj_str(o.srv_addr);
    CHECK( pj_turn_udp_init(g.udp_rel,				 /* the relay */
			    &srv,				 /* srv addr */
			    (o.srv_port?atoi(o.srv_port):PJ_STUN_PORT),/* def port */
			    NULL,				 /* resolver */
			    (o.user_name?&cred:NULL),		 /* credential */
			    NULL)				 /* alloc param */
			    );

    return PJ_SUCCESS;
}

static void destroy_relay(void)
{
    if (g.udp_rel) {
	pj_turn_udp_destroy(g.udp_rel);
	g.udp_rel = NULL;
    }
}


static void turn_on_rx_data(pj_turn_udp *udp_rel,
			    const pj_uint8_t *pkt,
			    unsigned pkt_len,
			    const pj_sockaddr_t *peer_addr,
			    unsigned addr_len)
{
    char addrinfo[80];

    pj_sockaddr_print(peer_addr, addrinfo, sizeof(addrinfo), 3);

    PJ_LOG(3,(THIS_FILE, "Client received %d bytes data from %s: %.*s",
	      pkt_len, addrinfo, pkt_len, pkt));
}


static void turn_on_state(pj_turn_udp *udp_rel, pj_turn_state_t old_state,
			  pj_turn_state_t new_state)
{
    if (new_state == PJ_TURN_STATE_READY) {
	pj_turn_session_info info;
	pj_turn_udp_get_info(udp_rel, &info);
	pj_memcpy(&g.relay_addr, &info.relay_addr, sizeof(pj_sockaddr));
    }
}

static pj_status_t parse_addr(const char *input,
			      pj_sockaddr_in *addr)
{
    const char *pos;
    pj_str_t ip;
    pj_uint16_t port;
    pj_sockaddr tmp_addr;

    pos = pj_ansi_strchr(input, ':');
    if (pos==NULL) {
	puts("Invalid format");
	return -1;
    }

    ip.ptr = (char*)input;
    ip.slen = pos - input;
    port = (pj_uint16_t)atoi(pos+1);

    if (port==0) {
	puts("Invalid port");
	return -1;
    }

    if (pj_sockaddr_in_init(&tmp_addr.ipv4, &ip, port)!=PJ_SUCCESS) {
	puts("Invalid address");
	return -1;
    }

    pj_memcpy(addr, &tmp_addr, sizeof(pj_sockaddr_in));

    return PJ_SUCCESS;
}

static void menu(void)
{
    pj_turn_session_info info;
    char client_state[20], relay_addr[80], peer0_addr[80], peer1_addr[80];

    if (g.udp_rel) {
	pj_turn_udp_get_info(g.udp_rel, &info);
	strcpy(client_state, pj_turn_state_name(info.state));
	pj_sockaddr_print(&info.relay_addr, relay_addr, sizeof(relay_addr), 3);
    } else {
	strcpy(client_state, "NULL");
	strcpy(relay_addr, "0.0.0.0:0");
    }

    pj_sockaddr_print(&g.peer[0].addr, peer0_addr, sizeof(peer0_addr), 3);
    pj_sockaddr_print(&g.peer[1].addr, peer1_addr, sizeof(peer1_addr), 3);


    puts("\n");
    puts("+====================================================================+");
    puts("|             CLIENT                |             PEER-0             |");
    puts("|                                   |                                |");
    printf("| State     : %12s          | Address: %21s |\n",
	   client_state, peer0_addr);
    printf("| Relay addr: %21s |                                |\n",
	   relay_addr);
    puts("|                                   | 0  Send data to relay address  |");
    puts("| A      Allocate relay             +--------------------------------+	");
    puts("| S[01]  Send data to peer 0/1      |             PEER-1             |");
    puts("| B[01]  BindChannel to peer 0/1    |                                |");
    printf("| X      Delete allocation          | Address: %21s |\n",
	  peer1_addr);
    puts("+-----------------------------------+                                |");
    puts("| q  Quit                           | 1  Send data to relay adderss  |");
    puts("+-----------------------------------+--------------------------------+");
    printf(">>> ");
    fflush(stdout);
}


static void console_main(void)
{
    while (!g.quit) {
	char input[32];
	struct peer *peer;
	pj_ssize_t len;
	pj_status_t status;

	menu();

	fgets(input, sizeof(input), stdin);
	
	switch (input[0]) {
	case 'A':
	    create_relay();
	    break;
	case 'S':
	    if (g.udp_rel == NULL) {
		puts("Error: no relay");
		continue;
	    }
	    if (input[1] != '0' && input[1] != '1') {
		puts("Usage: S0 or S1");
		continue;
	    }
	    peer = &g.peer[input[1]-'0'];
	    strcpy(input, "Hello from client");
	    status = pj_turn_udp_sendto(g.udp_rel, input, strlen(input)+1, 
					&peer->addr, 
					pj_sockaddr_get_len(&peer->addr));
	    if (status != PJ_SUCCESS)
		my_perror("turn_udp_sendto() failed", status);
	    break;
	case 'B':
	    if (g.udp_rel == NULL) {
		puts("Error: no relay");
		continue;
	    }
	    if (input[1] != '0' && input[1] != '1') {
		puts("Usage: B0 or B1");
		continue;
	    }
	    peer = &g.peer[input[1]-'0'];
	    status = pj_turn_udp_bind_channel(g.udp_rel, &peer->addr,
					      pj_sockaddr_get_len(&peer->addr));
	    if (status != PJ_SUCCESS)
		my_perror("turn_udp_bind_channel() failed", status);
	    break;
	case 'X':
	    if (g.udp_rel == NULL) {
		puts("Error: no relay");
		continue;
	    }
	    destroy_relay();
	    break;
	case '0':
	case '1':
	    peer = &g.peer[input[1]-'0'];
	    sprintf(input, "Hello from peer%d", input[0]-'0');
	    len = strlen(input)+1;
	    pj_sock_sendto(peer->sock, input, &len, 0, &g.relay_addr, 
			   pj_sockaddr_get_len(&g.relay_addr));
	    break;
	case 'q':
	    g.quit = PJ_TRUE;
	    break;
	}
    }
}


static void usage(void)
{
    puts("Usage: pjturn_client TARGET [OPTIONS]");
    puts("");
    puts("where TARGET is \"host[:port]\"");
    puts("");
    puts("and OPTIONS:");
    puts(" --realm, -r       Set realm of the credential");
    puts(" --username, -u    Set username of the credential");
    puts(" --password, -p    Set password of the credential");
    puts(" --nonce, -N       Set NONCE");   
    puts(" --fingerprint, -F Use fingerprint for outgoing requests");
    puts(" --help, -h");
}

int main(int argc, char *argv[])
{
    struct pj_getopt_option long_options[] = {
	{ "realm",	1, 0, 'r'},
	{ "username",	1, 0, 'u'},
	{ "password",	1, 0, 'p'},
	{ "nonce",	1, 0, 'N'},
	{ "fingerprint",0, 0, 'F'},
	{ "data",	1, 0, 'D'},
	{ "help",	0, 0, 'h'}
    };
    int c, opt_id;
    char *pos;
    pj_status_t status;

    while((c=pj_getopt_long(argc,argv, "r:u:p:N:hF", long_options, &opt_id))!=-1) {
	switch (c) {
	case 'r':
	    o.realm = pj_optarg;
	    break;
	case 'u':
	    o.user_name = pj_optarg;
	    break;
	case 'p':
	    o.password = pj_optarg;
	    break;
	case 'N':
	    o.nonce = pj_optarg;
	    break;
	case 'h':
	    usage();
	    return 0;
	case 'F':
	    o.use_fingerprint = PJ_TRUE;
	    break;

	default:
	    printf("Argument \"%s\" is not valid. Use -h to see help",
		   argv[pj_optind]);
	    return 1;
	}
    }

    if (pj_optind == argc) {
	puts("Error: TARGET is needed");
	return 1;
    }

    if ((pos=pj_ansi_strchr(argv[pj_optind], ':')) != NULL) {
	o.srv_addr = argv[pj_optind];
	*pos = '\0';
	o.srv_port = pos+1;
    } else {
	o.srv_addr = argv[pj_optind];
    }

    if ((status=init()) != 0)
	goto on_return;
    
    if ((status=create_relay()) != 0)
	goto on_return;
    
    console_main();

on_return:
    shutdown();
    return status ? 1 : 0;
}

