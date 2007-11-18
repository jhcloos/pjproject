/* $Id$ */
/* 
 * Copyright (C)2003-2007 Benny Prijono <benny@prijono.org>
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
#include <pjlib.h>
#include "test.h"


/**
 * \page page_pjlib_sock_test Test: Socket
 *
 * This file provides implementation of \b sock_test(). It tests the
 * various aspects of the socket API.
 *
 * \section sock_test_scope_sec Scope of the Test
 *
 * The scope of the test:
 *  - verify the validity of the address structs.
 *  - verify that address manipulation API works.
 *  - simple socket creation and destruction.
 *  - simple socket send/recv and sendto/recvfrom.
 *  - UDP connect()
 *  - send/recv big data.
 *  - all for both UDP and TCP.
 *
 * The APIs tested in this test:
 *  - pj_inet_aton()
 *  - pj_inet_ntoa()
 *  - pj_inet_pton()  (only if IPv6 is enabled)
 *  - pj_inet_ntop()  (only if IPv6 is enabled)
 *  - pj_gethostname()
 *  - pj_sock_socket()
 *  - pj_sock_close()
 *  - pj_sock_send()
 *  - pj_sock_sendto()
 *  - pj_sock_recv()
 *  - pj_sock_recvfrom()
 *  - pj_sock_bind()
 *  - pj_sock_connect()
 *  - pj_sock_listen()
 *  - pj_sock_accept()
 *  - pj_gethostbyname()
 *
 *
 * This file is <b>pjlib-test/sock.c</b>
 *
 * \include pjlib-test/sock.c
 */

#if INCLUDE_SOCK_TEST

#define UDP_PORT	51234
#define TCP_PORT        (UDP_PORT+10)
#define BIG_DATA_LEN	8192
#define ADDRESS		"127.0.0.1"

static char bigdata[BIG_DATA_LEN];
static char bigbuffer[BIG_DATA_LEN];

static int format_test(void)
{
    pj_str_t s = pj_str(ADDRESS);
    unsigned char *p;
    pj_in_addr addr;
    char zero[64];
    pj_sockaddr_in addr2;
    const pj_str_t *hostname;
    const unsigned char A[] = {127, 0, 0, 1};

    PJ_LOG(3,("test", "...format_test()"));
    
    /* pj_inet_aton() */
    if (pj_inet_aton(&s, &addr) != 1)
	return -10;
    
    /* Check the result. */
    p = (unsigned char*)&addr;
    if (p[0]!=A[0] || p[1]!=A[1] || p[2]!=A[2] || p[3]!=A[3]) {
	PJ_LOG(3,("test", "  error: mismatched address. p0=%d, p1=%d, "
			  "p2=%d, p3=%d", p[0] & 0xFF, p[1] & 0xFF, 
			   p[2] & 0xFF, p[3] & 0xFF));
	return -15;
    }

    /* pj_inet_ntoa() */
    p = (unsigned char*) pj_inet_ntoa(addr);
    if (!p)
	return -20;

    if (pj_strcmp2(&s, (char*)p) != 0)
	return -22;

#if defined(PJ_HAS_IPV6) && PJ_HAS_IPV6!=0
    /* pj_inet_pton() */
    /* pj_inet_ntop() */
    {
	const pj_str_t s_ipv4 = pj_str("127.0.0.1");
	const pj_str_t s_ipv6 = pj_str("fe80::2ff:83ff:fe7c:8b42");
	char buf_ipv4[PJ_INET_ADDRSTRLEN];
	char buf_ipv6[PJ_INET6_ADDRSTRLEN];
	pj_in_addr ipv4;
	pj_in6_addr ipv6;

	if (pj_inet_pton(pj_AF_INET(), &s_ipv4, &ipv4) != PJ_SUCCESS)
	    return -24;

	p = (unsigned char*)&ipv4;
	if (p[0]!=A[0] || p[1]!=A[1] || p[2]!=A[2] || p[3]!=A[3]) {
	    return -25;
	}

	if (pj_inet_pton(pj_AF_INET6(), &s_ipv6, &ipv6) != PJ_SUCCESS)
	    return -26;

	p = (unsigned char*)&ipv6;
	if (p[0] != 0xfe || p[1] != 0x80 || p[2] != 0 || p[3] != 0 ||
	    p[4] != 0 || p[5] != 0 || p[6] != 0 || p[7] != 0 ||
	    p[8] != 0x02 || p[9] != 0xff || p[10] != 0x83 || p[11] != 0xff ||
	    p[12]!=0xfe || p[13]!=0x7c || p[14] != 0x8b || p[15]!=0x42)
	{
	    return -27;
	}

	if (pj_inet_ntop(pj_AF_INET(), &ipv4, buf_ipv4, sizeof(buf_ipv4)) != PJ_SUCCESS)
	    return -28;
	if (pj_stricmp2(&s_ipv4, buf_ipv4) != 0)
	    return -29;

	if (pj_inet_ntop(pj_AF_INET6(), &ipv6, buf_ipv6, sizeof(buf_ipv6)) != PJ_SUCCESS)
	    return -30;
	if (pj_stricmp2(&s_ipv6, buf_ipv6) != 0)
	    return -31;
    }

#endif	/* PJ_HAS_IPV6 */

    /* Test that pj_sockaddr_in_init() initialize the whole structure, 
     * including sin_zero.
     */
    pj_sockaddr_in_init(&addr2, 0, 1000);
    pj_bzero(zero, sizeof(zero));
    if (pj_memcmp(addr2.sin_zero, zero, sizeof(addr2.sin_zero)) != 0)
	return -35;

    /* pj_gethostname() */
    hostname = pj_gethostname();
    if (!hostname || !hostname->ptr || !hostname->slen)
	return -40;

    PJ_LOG(3,("test", "....hostname is %.*s", 
	      (int)hostname->slen, hostname->ptr));

    /* pj_gethostaddr() */


    return 0;
}

static int simple_sock_test(void)
{
    int types[2];
    pj_sock_t sock;
    int i;
    pj_status_t rc = PJ_SUCCESS;

    types[0] = pj_SOCK_STREAM();
    types[1] = pj_SOCK_DGRAM();

    PJ_LOG(3,("test", "...simple_sock_test()"));

    for (i=0; i<(int)(sizeof(types)/sizeof(types[0])); ++i) {
	
	rc = pj_sock_socket(pj_AF_INET(), types[i], 0, &sock);
	if (rc != PJ_SUCCESS) {
	    app_perror("...error: unable to create socket", rc);
	    break;
	} else {
	    rc = pj_sock_close(sock);
	    if (rc != 0) {
		app_perror("...error: close socket", rc);
		break;
	    }
	}
    }
    return rc;
}


static int send_recv_test(int sock_type,
                          pj_sock_t ss, pj_sock_t cs,
			  pj_sockaddr_in *dstaddr, pj_sockaddr_in *srcaddr, 
			  int addrlen)
{
    enum { DATA_LEN = 16 };
    char senddata[DATA_LEN+4], recvdata[DATA_LEN+4];
    pj_ssize_t sent, received, total_received;
    pj_status_t rc;

    TRACE_(("test", "....create_random_string()"));
    pj_create_random_string(senddata, DATA_LEN);
    senddata[DATA_LEN-1] = '\0';

    /*
     * Test send/recv small data.
     */
    TRACE_(("test", "....sendto()"));
    if (dstaddr) {
        sent = DATA_LEN;
	rc = pj_sock_sendto(cs, senddata, &sent, 0, dstaddr, addrlen);
	if (rc != PJ_SUCCESS || sent != DATA_LEN) {
	    app_perror("...sendto error", rc);
	    rc = -140; goto on_error;
	}
    } else {
        sent = DATA_LEN;
	rc = pj_sock_send(cs, senddata, &sent, 0);
	if (rc != PJ_SUCCESS || sent != DATA_LEN) {
	    app_perror("...send error", rc);
	    rc = -145; goto on_error;
	}
    }

    TRACE_(("test", "....recv()"));
    if (srcaddr) {
	pj_sockaddr_in addr;
	int srclen = sizeof(addr);
	
	pj_bzero(&addr, sizeof(addr));

        received = DATA_LEN;
	rc = pj_sock_recvfrom(ss, recvdata, &received, 0, &addr, &srclen);
	if (rc != PJ_SUCCESS || received != DATA_LEN) {
	    app_perror("...recvfrom error", rc);
	    rc = -150; goto on_error;
	}
	if (srclen != addrlen)
	    return -151;
	if (pj_memcmp(&addr, srcaddr, srclen) != 0) {
	    char srcaddr_str[32], addr_str[32];
	    strcpy(srcaddr_str, pj_inet_ntoa(srcaddr->sin_addr));
	    strcpy(addr_str, pj_inet_ntoa(addr.sin_addr));
	    PJ_LOG(3,("test", "...error: src address mismatch (original=%s, "
			      "recvfrom addr=%s)", 
			      srcaddr_str, addr_str));
	    return -152;
	}
	
    } else {
        /* Repeat recv() until all data is received.
         * This applies only for non-UDP of course, since for UDP
         * we would expect all data to be received in one packet.
         */
        total_received = 0;
        do {
            received = DATA_LEN-total_received;
	    rc = pj_sock_recv(ss, recvdata+total_received, &received, 0);
	    if (rc != PJ_SUCCESS) {
	        app_perror("...recv error", rc);
	        rc = -155; goto on_error;
	    }
            if (received <= 0) {
                PJ_LOG(3,("", "...error: socket has closed! (received=%d)",
                          received));
                rc = -156; goto on_error;
            }
	    if (received != DATA_LEN-total_received) {
                if (sock_type != pj_SOCK_STREAM()) {
	            PJ_LOG(3,("", "...error: expecting %u bytes, got %u bytes",
                              DATA_LEN-total_received, received));
	            rc = -157; goto on_error;
                }
	    }
            total_received += received;
        } while (total_received < DATA_LEN);
    }

    TRACE_(("test", "....memcmp()"));
    if (pj_memcmp(senddata, recvdata, DATA_LEN) != 0) {
	PJ_LOG(3,("","...error: received data mismatch "
		     "(got:'%s' expecting:'%s'",
		     recvdata, senddata));
	rc = -160; goto on_error;
    }

    /*
     * Test send/recv big data.
     */
    TRACE_(("test", "....sendto()"));
    if (dstaddr) {
        sent = BIG_DATA_LEN;
	rc = pj_sock_sendto(cs, bigdata, &sent, 0, dstaddr, addrlen);
	if (rc != PJ_SUCCESS || sent != BIG_DATA_LEN) {
	    app_perror("...sendto error", rc);
	    rc = -161; goto on_error;
	}
    } else {
        sent = BIG_DATA_LEN;
	rc = pj_sock_send(cs, bigdata, &sent, 0);
	if (rc != PJ_SUCCESS || sent != BIG_DATA_LEN) {
	    app_perror("...send error", rc);
	    rc = -165; goto on_error;
	}
    }

    TRACE_(("test", "....recv()"));

    /* Repeat recv() until all data is received.
     * This applies only for non-UDP of course, since for UDP
     * we would expect all data to be received in one packet.
     */
    total_received = 0;
    do {
        received = BIG_DATA_LEN-total_received;
	rc = pj_sock_recv(ss, bigbuffer+total_received, &received, 0);
	if (rc != PJ_SUCCESS) {
	    app_perror("...recv error", rc);
	    rc = -170; goto on_error;
	}
        if (received <= 0) {
            PJ_LOG(3,("", "...error: socket has closed! (received=%d)",
                      received));
            rc = -173; goto on_error;
        }
	if (received != BIG_DATA_LEN-total_received) {
            if (sock_type != pj_SOCK_STREAM()) {
	        PJ_LOG(3,("", "...error: expecting %u bytes, got %u bytes",
                          BIG_DATA_LEN-total_received, received));
	        rc = -176; goto on_error;
            }
	}
        total_received += received;
    } while (total_received < BIG_DATA_LEN);

    TRACE_(("test", "....memcmp()"));
    if (pj_memcmp(bigdata, bigbuffer, BIG_DATA_LEN) != 0) {
        PJ_LOG(3,("", "...error: received data has been altered!"));
	rc = -180; goto on_error;
    }
    
    rc = 0;

on_error:
    return rc;
}

static int udp_test(void)
{
    pj_sock_t cs = PJ_INVALID_SOCKET, ss = PJ_INVALID_SOCKET;
    pj_sockaddr_in dstaddr, srcaddr;
    pj_str_t s;
    pj_status_t rc = 0, retval;

    PJ_LOG(3,("test", "...udp_test()"));

    rc = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &ss);
    if (rc != 0) {
	app_perror("...error: unable to create socket", rc);
	return -100;
    }

    rc = pj_sock_socket(pj_AF_INET(), pj_SOCK_DGRAM(), 0, &cs);
    if (rc != 0)
	return -110;

    /* Bind server socket. */
    pj_bzero(&dstaddr, sizeof(dstaddr));
    dstaddr.sin_family = pj_AF_INET();
    dstaddr.sin_port = pj_htons(UDP_PORT);
    dstaddr.sin_addr = pj_inet_addr(pj_cstr(&s, ADDRESS));
    
    if ((rc=pj_sock_bind(ss, &dstaddr, sizeof(dstaddr))) != 0) {
	app_perror("...bind error udp:"ADDRESS, rc);
	rc = -120; goto on_error;
    }

    /* Bind client socket. */
    pj_bzero(&srcaddr, sizeof(srcaddr));
    srcaddr.sin_family = pj_AF_INET();
    srcaddr.sin_port = pj_htons(UDP_PORT-1);
    srcaddr.sin_addr = pj_inet_addr(pj_cstr(&s, ADDRESS));

    if ((rc=pj_sock_bind(cs, &srcaddr, sizeof(srcaddr))) != 0) {
	app_perror("...bind error", rc);
	rc = -121; goto on_error;
    }
	    
    /* Test send/recv, with sendto */
    rc = send_recv_test(pj_SOCK_DGRAM(), ss, cs, &dstaddr, NULL, 
                        sizeof(dstaddr));
    if (rc != 0)
	goto on_error;

    /* Test send/recv, with sendto and recvfrom */
    rc = send_recv_test(pj_SOCK_DGRAM(), ss, cs, &dstaddr, 
                        &srcaddr, sizeof(dstaddr));
    if (rc != 0)
	goto on_error;

    /* Disable this test on Symbian since UDP connect()/send() failed
     * with S60 3rd edition (including MR2).
     * See http://www.pjsip.org/trac/ticket/264
     */    
#if !defined(PJ_SYMBIAN) || PJ_SYMBIAN==0
    /* connect() the sockets. */
    rc = pj_sock_connect(cs, &dstaddr, sizeof(dstaddr));
    if (rc != 0) {
	app_perror("...connect() error", rc);
	rc = -122; goto on_error;
    }

    /* Test send/recv with send() */
    rc = send_recv_test(pj_SOCK_DGRAM(), ss, cs, NULL, NULL, 0);
    if (rc != 0)
	goto on_error;

    /* Test send/recv with send() and recvfrom */
    rc = send_recv_test(pj_SOCK_DGRAM(), ss, cs, NULL, &srcaddr, 
                        sizeof(srcaddr));
    if (rc != 0)
	goto on_error;
#endif

on_error:
    retval = rc;
    if (cs != PJ_INVALID_SOCKET) {
        rc = pj_sock_close(cs);
        if (rc != PJ_SUCCESS) {
            app_perror("...error in closing socket", rc);
            return -1000;
        }
    }
    if (ss != PJ_INVALID_SOCKET) {
        rc = pj_sock_close(ss);
        if (rc != PJ_SUCCESS) {
            app_perror("...error in closing socket", rc);
            return -1010;
        }
    }

    return retval;
}

static int tcp_test(void)
{
    pj_sock_t cs, ss;
    pj_status_t rc = 0, retval;

    PJ_LOG(3,("test", "...tcp_test()"));

    rc = app_socketpair(pj_AF_INET(), pj_SOCK_STREAM(), 0, &ss, &cs);
    if (rc != PJ_SUCCESS) {
        app_perror("...error: app_socketpair():", rc);
        return -2000;
    }

    /* Test send/recv with send() and recv() */
    retval = send_recv_test(pj_SOCK_STREAM(), ss, cs, NULL, NULL, 0);

    rc = pj_sock_close(cs);
    if (rc != PJ_SUCCESS) {
        app_perror("...error in closing socket", rc);
        return -2000;
    }

    rc = pj_sock_close(ss);
    if (rc != PJ_SUCCESS) {
        app_perror("...error in closing socket", rc);
        return -2010;
    }

    return retval;
}

static int ioctl_test(void)
{
    return 0;
}

static int gethostbyname_test(void)
{
    pj_str_t host;
    pj_hostent he;
    pj_status_t status;

    /* Testing pj_gethostbyname() with invalid host */
    host = pj_str("an-invalid-host-name");
    status = pj_gethostbyname(&host, &he);

    /* Must return failure! */
    if (status == PJ_SUCCESS)
	return -20100;
    else
	return 0;
}

#if 0
#include "../pj/os_symbian.h"
static int connect_test()
{
	RSocketServ rSockServ;
	RSocket rSock;
	TInetAddr inetAddr;
	TRequestStatus reqStatus;
	char buffer[16];
	TPtrC8 data((const TUint8*)buffer, (TInt)sizeof(buffer));
 	int rc;
	
	rc = rSockServ.Connect();
	if (rc != KErrNone)
		return rc;
	
	rc = rSock.Open(rSockServ, KAfInet, KSockDatagram, KProtocolInetUdp);
    	if (rc != KErrNone) 
    	{    		
    		rSockServ.Close();
    		return rc;
    	}
   	
    	inetAddr.Init(KAfInet);
    	inetAddr.Input(_L("127.0.0.1"));
    	inetAddr.SetPort(80);
    	
    	rSock.Connect(inetAddr, reqStatus);
    	User::WaitForRequest(reqStatus);

    	if (reqStatus != KErrNone) {
		rSock.Close();
    		rSockServ.Close();
		return rc;
    	}
    
    	rSock.Send(data, 0, reqStatus);
    	User::WaitForRequest(reqStatus);
    	
    	if (reqStatus!=KErrNone) {
    		rSock.Close();
    		rSockServ.Close();
    		return rc;
    	}
    	
    	rSock.Close();
    	rSockServ.Close();
	return KErrNone;
}
#endif

int sock_test()
{
    int rc;
    
    pj_create_random_string(bigdata, BIG_DATA_LEN);

// Enable this to demonstrate the error witn S60 3rd Edition MR2
#if 0
    rc = connect_test();
    if (rc != 0)
    	return rc;
#endif
    
    rc = format_test();
    if (rc != 0)
	return rc;

    rc = gethostbyname_test();
    if (rc != 0)
	return rc;

    rc = simple_sock_test();
    if (rc != 0)
	return rc;

    rc = ioctl_test();
    if (rc != 0)
	return rc;

    rc = udp_test();
    if (rc != 0)
	return rc;

    rc = tcp_test();
    if (rc != 0)
	return rc;

    return 0;
}


#else
/* To prevent warning about "translation unit is empty"
 * when this test is disabled. 
 */
int dummy_sock_test;
#endif	/* INCLUDE_SOCK_TEST */

