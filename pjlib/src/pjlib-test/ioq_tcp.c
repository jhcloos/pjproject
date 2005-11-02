/* $Id$
 */
#include "test.h"

/**
 * \page page_pjlib_ioqueue_tcp_test Test: I/O Queue (TCP)
 *
 * This file provides implementation to test the
 * functionality of the I/O queue when TCP socket is used.
 *
 *
 * This file is <b>pjlib-test/ioq_tcp.c</b>
 *
 * \include pjlib-test/ioq_tcp.c
 */


#if INCLUDE_TCP_IOQUEUE_TEST

#include <pjlib.h>

#if PJ_HAS_TCP

#define THIS_FILE	    "test_tcp"
#define PORT		    50000
#define NON_EXISTANT_PORT   50123
#define LOOP		    100
#define BUF_MIN_SIZE	    32
#define BUF_MAX_SIZE	    2048
#define SOCK_INACTIVE_MIN   (4-2)
#define SOCK_INACTIVE_MAX   (PJ_IOQUEUE_MAX_HANDLES - 2)
#define POOL_SIZE	    (2*BUF_MAX_SIZE + SOCK_INACTIVE_MAX*128 + 2048)

static pj_ssize_t	callback_read_size,
                        callback_write_size,
                        callback_accept_status,
                        callback_connect_status;
static pj_ioqueue_key_t*callback_read_key,
                       *callback_write_key,
                       *callback_accept_key,
                       *callback_connect_key;

static void on_ioqueue_read(pj_ioqueue_key_t *key, pj_ssize_t bytes_read)
{
    callback_read_key = key;
    callback_read_size = bytes_read;
}

static void on_ioqueue_write(pj_ioqueue_key_t *key, pj_ssize_t bytes_written)
{
    callback_write_key = key;
    callback_write_size = bytes_written;
}

static void on_ioqueue_accept(pj_ioqueue_key_t *key, pj_sock_t sock, 
                              int status)
{
    PJ_UNUSED_ARG(sock);

    callback_accept_key = key;
    callback_accept_status = status;
}

static void on_ioqueue_connect(pj_ioqueue_key_t *key, int status)
{
    callback_connect_key = key;
    callback_connect_status = status;
}

static pj_ioqueue_callback test_cb = 
{
    &on_ioqueue_read,
    &on_ioqueue_write,
    &on_ioqueue_accept,
    &on_ioqueue_connect,
};

static int send_recv_test(pj_ioqueue_t *ioque,
			  pj_ioqueue_key_t *skey,
			  pj_ioqueue_key_t *ckey,
			  void *send_buf,
			  void *recv_buf,
			  pj_ssize_t bufsize,
			  pj_timestamp *t_elapsed)
{
    int rc;
    pj_ssize_t bytes;
    pj_timestamp t1, t2;
    int pending_op = 0;

    // Start reading on the server side.
    rc = pj_ioqueue_read(ioque, skey, recv_buf, bufsize);
    if (rc != 0 && rc != PJ_EPENDING) {
	return -100;
    }
    
    ++pending_op;

    // Randomize send buffer.
    pj_create_random_string((char*)send_buf, bufsize);

    // Starts send on the client side.
    bytes = pj_ioqueue_write(ioque, ckey, send_buf, bufsize);
    if (bytes != bufsize && bytes != PJ_EPENDING) {
	return -120;
    }
    if (bytes == PJ_EPENDING) {
	++pending_op;
    }

    // Begin time.
    pj_get_timestamp(&t1);

    // Reset indicators
    callback_read_size = callback_write_size = 0;
    callback_read_key = callback_write_key = NULL;

    // Poll the queue until we've got completion event in the server side.
    rc = 0;
    while (pending_op > 0) {
	rc = pj_ioqueue_poll(ioque, NULL);
	if (rc > 0) {
            if (callback_read_size) {
                if (callback_read_size != bufsize) {
                    return -160;
                }
                if (callback_read_key != skey)
                    return -161;
            }
            if (callback_write_size) {
                if (callback_write_key != ckey)
                    return -162;
            }
	    pending_op -= rc;
	}
	if (rc < 0) {
	    return -170;
	}
    }

    // End time.
    pj_get_timestamp(&t2);
    t_elapsed->u32.lo += (t2.u32.lo - t1.u32.lo);

    if (rc < 0) {
	return -150;
    }

    // Compare recv buffer with send buffer.
    if (pj_memcmp(send_buf, recv_buf, bufsize) != 0) {
	return -180;
    }

    // Success
    return 0;
}


/*
 * Compliance test for success scenario.
 */
static int compliance_test_0(void)
{
    pj_sock_t ssock=-1, csock0=-1, csock1=-1;
    pj_sockaddr_in addr, client_addr, rmt_addr;
    int client_addr_len;
    pj_pool_t *pool = NULL;
    char *send_buf, *recv_buf;
    pj_ioqueue_t *ioque = NULL;
    pj_ioqueue_key_t *skey, *ckey0, *ckey1;
    int bufsize = BUF_MIN_SIZE;
    pj_ssize_t status = -1;
    int pending_op = 0;
    pj_timestamp t_elapsed;
    pj_str_t s;
    pj_status_t rc;

    // Create pool.
    pool = pj_pool_create(mem, NULL, POOL_SIZE, 4000, NULL);

    // Allocate buffers for send and receive.
    send_buf = (char*)pj_pool_alloc(pool, bufsize);
    recv_buf = (char*)pj_pool_alloc(pool, bufsize);

    // Create server socket and client socket for connecting
    rc = pj_sock_socket(PJ_AF_INET, PJ_SOCK_STREAM, 0, &ssock);
    if (rc != PJ_SUCCESS) {
        app_perror("...error creating socket", rc);
        status=-1; goto on_error;
    }

    rc = pj_sock_socket(PJ_AF_INET, PJ_SOCK_STREAM, 0, &csock1);
    if (rc != PJ_SUCCESS) {
        app_perror("...error creating socket", rc);
	status=-1; goto on_error;
    }

    // Bind server socket.
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = PJ_AF_INET;
    addr.sin_port = pj_htons(PORT);
    if (pj_sock_bind(ssock, &addr, sizeof(addr))) {
        app_perror("...bind error", rc);
	status=-10; goto on_error;
    }

    // Create I/O Queue.
    rc = pj_ioqueue_create(pool, PJ_IOQUEUE_MAX_HANDLES, 0, &ioque);
    if (rc != PJ_SUCCESS) {
        app_perror("...ERROR in pj_ioqueue_create()", rc);
	status=-20; goto on_error;
    }

    // Register server socket and client socket.
    rc = pj_ioqueue_register_sock(pool, ioque, ssock, NULL, &test_cb, &skey);
    if (rc == PJ_SUCCESS)
        rc = pj_ioqueue_register_sock(pool, ioque, csock1, NULL, &test_cb, 
                                      &ckey1);
    else
        ckey1 = NULL;
    if (rc != PJ_SUCCESS) {
        app_perror("...ERROR in pj_ioqueue_register_sock()", rc);
	status=-23; goto on_error;
    }

    // Server socket listen().
    if (pj_sock_listen(ssock, 5)) {
        app_perror("...ERROR in pj_sock_listen()", rc);
	status=-25; goto on_error;
    }

    // Server socket accept()
    client_addr_len = sizeof(pj_sockaddr_in);
    status = pj_ioqueue_accept(ioque, skey, &csock0, &client_addr, &rmt_addr, &client_addr_len);
    if (status != PJ_EPENDING) {
        app_perror("...ERROR in pj_ioqueue_accept()", rc);
	status=-30; goto on_error;
    }
    if (status==PJ_EPENDING) {
	++pending_op;
    }

    // Initialize remote address.
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = PJ_AF_INET;
    addr.sin_port = pj_htons(PORT);
    addr.sin_addr = pj_inet_addr(pj_cstr(&s, "127.0.0.1"));

    // Client socket connect()
    status = pj_ioqueue_connect(ioque, ckey1, &addr, sizeof(addr));
    if (status!=PJ_SUCCESS && status != PJ_EPENDING) {
        app_perror("...ERROR in pj_ioqueue_connect()", rc);
	status=-40; goto on_error;
    }
    if (status==PJ_EPENDING) {
	++pending_op;
    }

    // Poll until connected
    callback_read_size = callback_write_size = 0;
    callback_accept_status = callback_connect_status = -2;

    callback_read_key = callback_write_key = 
        callback_accept_key = callback_connect_key = NULL;

    while (pending_op) {
	pj_time_val timeout = {1, 0};

	status=pj_ioqueue_poll(ioque, &timeout);
	if (status > 0) {
            if (callback_accept_status != -2) {
                if (callback_accept_status != 0) {
                    status=-41; goto on_error;
                }
                if (callback_accept_key != skey) {
                    status=-41; goto on_error;
                }
            }

            if (callback_connect_status != -2) {
                if (callback_connect_status != 0) {
                    status=-50; goto on_error;
                }
                if (callback_connect_key != ckey1) {
                    status=-51; goto on_error;
                }
            }

	    pending_op -= status;

	    if (pending_op == 0) {
		status = 0;
	    }
	}
    }

    // Check accepted socket.
    if (csock0 == PJ_INVALID_SOCKET) {
	status = -69;
        app_perror("...accept() error", pj_get_os_error());
	goto on_error;
    }

    // Register newly accepted socket.
    rc = pj_ioqueue_register_sock(pool, ioque, csock0, NULL, 
                                  &test_cb, &ckey0);
    if (rc != PJ_SUCCESS) {
        app_perror("...ERROR in pj_ioqueue_register_sock", rc);
	status = -70;
	goto on_error;
    }

    // Test send and receive.
    t_elapsed.u32.lo = 0;
    status = send_recv_test(ioque, ckey0, ckey1, send_buf, recv_buf, bufsize, &t_elapsed);
    if (status != 0) {
	goto on_error;
    }

    // Success
    status = 0;

on_error:
    if (ssock != PJ_INVALID_SOCKET)
	pj_sock_close(ssock);
    if (csock1 != PJ_INVALID_SOCKET)
	pj_sock_close(csock1);
    if (csock0 != PJ_INVALID_SOCKET)
	pj_sock_close(csock0);
    if (ioque != NULL)
	pj_ioqueue_destroy(ioque);
    pj_pool_release(pool);
    return status;

}

/*
 * Compliance test for failed scenario.
 * In this case, the client connects to a non-existant service.
 */
static int compliance_test_1(void)
{
    pj_sock_t csock1=-1;
    pj_sockaddr_in addr;
    pj_pool_t *pool = NULL;
    pj_ioqueue_t *ioque = NULL;
    pj_ioqueue_key_t *ckey1;
    pj_ssize_t status = -1;
    int pending_op = 0;
    pj_str_t s;
    pj_status_t rc;

    // Create pool.
    pool = pj_pool_create(mem, NULL, POOL_SIZE, 4000, NULL);

    // Create I/O Queue.
    rc = pj_ioqueue_create(pool, PJ_IOQUEUE_MAX_HANDLES, 0, &ioque);
    if (!ioque) {
	status=-20; goto on_error;
    }

    // Create client socket
    rc = pj_sock_socket(PJ_AF_INET, PJ_SOCK_STREAM, 0, &csock1);
    if (rc != PJ_SUCCESS) {
        app_perror("...ERROR in pj_sock_socket()", rc);
	status=-1; goto on_error;
    }

    // Register client socket.
    rc = pj_ioqueue_register_sock(pool, ioque, csock1, NULL, 
                                  &test_cb, &ckey1);
    if (rc != PJ_SUCCESS) {
        app_perror("...ERROR in pj_ioqueue_register_sock()", rc);
	status=-23; goto on_error;
    }

    // Initialize remote address.
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = PJ_AF_INET;
    addr.sin_port = pj_htons(NON_EXISTANT_PORT);
    addr.sin_addr = pj_inet_addr(pj_cstr(&s, "127.0.0.1"));

    // Client socket connect()
    status = pj_ioqueue_connect(ioque, ckey1, &addr, sizeof(addr));
    if (status==PJ_SUCCESS) {
	// unexpectedly success!
	status = -30;
	goto on_error;
    }
    if (status != PJ_EPENDING) {
	// success
    } else {
	++pending_op;
    }

    callback_connect_status = -2;
    callback_connect_key = NULL;

    // Poll until we've got result
    while (pending_op) {
	pj_time_val timeout = {1, 0};

	status=pj_ioqueue_poll(ioque, &timeout);
	if (status > 0) {
            if (callback_connect_key==ckey1) {
		if (callback_connect_status == 0) {
		    // unexpectedly connected!
		    status = -50;
		    goto on_error;
		}
	    }

	    pending_op -= status;
	    if (pending_op == 0) {
		status = 0;
	    }
	}
    }

    // Success
    status = 0;

on_error:
    if (csock1 != PJ_INVALID_SOCKET)
	pj_sock_close(csock1);
    if (ioque != NULL)
	pj_ioqueue_destroy(ioque);
    pj_pool_release(pool);
    return status;
}

int tcp_ioqueue_test()
{
    int status;

    PJ_LOG(3, (THIS_FILE, "..compliance test 0 (success scenario)"));
    if ((status=compliance_test_0()) != 0) {
	PJ_LOG(1, (THIS_FILE, "....FAILED (status=%d)\n", status));
	return status;
    }
    PJ_LOG(3, (THIS_FILE, "..compliance test 1 (failed scenario)"));
    if ((status=compliance_test_1()) != 0) {
	PJ_LOG(1, (THIS_FILE, "....FAILED (status=%d)\n", status));
	return status;
    }

    return 0;
}

#endif	/* PJ_HAS_TCP */


#else
/* To prevent warning about "translation unit is empty"
 * when this test is disabled. 
 */
int dummy_uiq_tcp;
#endif	/* INCLUDE_TCP_IOQUEUE_TEST */


