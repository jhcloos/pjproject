/* $Id$
 */
#include "test.h"
#include <pjlib.h>

static pj_atomic_t *total_bytes;

static int worker_thread(void *arg)
{
    pj_sock_t    sock = (pj_sock_t)arg;
    char         buf[1516];
    pj_status_t  last_recv_err = PJ_SUCCESS, last_write_err = PJ_SUCCESS;

    for (;;) {
        pj_ssize_t len;
        pj_status_t rc;
        pj_sockaddr_in addr;
        int addrlen;

        len = sizeof(buf);
        addrlen = sizeof(addr);
        rc = pj_sock_recvfrom(sock, buf, &len, 0, &addr, &addrlen);
        if (rc != 0) {
            if (rc != last_recv_err) {
                app_perror("...recv error", rc);
                last_recv_err = rc;
            }
            continue;
        }

        pj_atomic_add(total_bytes, len);

        rc = pj_sock_sendto(sock, buf, &len, 0, &addr, addrlen);
        if (rc != PJ_SUCCESS) {
            if (rc != last_write_err) {
                app_perror("...send error", rc);
                last_write_err = rc;
            }
            continue;
        }
    }
}


int echo_srv_sync(void)
{
    pj_pool_t *pool;
    pj_sock_t sock;
    pj_thread_t *thread[ECHO_SERVER_MAX_THREADS];
    pj_status_t rc;
    pj_highprec_t last_received, avg_bw, highest_bw;
    pj_time_val last_print;
    unsigned count;
    int i;

    pool = pj_pool_create(mem, NULL, 4000, 4000, NULL);
    if (!pool)
        return -5;

    rc = pj_atomic_create(pool, 0, &total_bytes);
    if (rc != PJ_SUCCESS) {
        app_perror("...unable to create atomic_var", rc);
        return -6;
    }

    rc = app_socket(PJ_AF_INET, PJ_SOCK_DGRAM, 0, ECHO_SERVER_START_PORT, &sock);
    if (rc != PJ_SUCCESS) {
        app_perror("...socket error", rc);
        return -10;
    }

    for (i=0; i<ECHO_SERVER_MAX_THREADS; ++i) {
        rc = pj_thread_create(pool, NULL, &worker_thread, (void*)sock,
                              PJ_THREAD_DEFAULT_STACK_SIZE, 0,
                              &thread[i]);
        if (rc != PJ_SUCCESS) {
            app_perror("...unable to create thread", rc);
            return -20;
        }
    }

    PJ_LOG(3,("", "...UDP echo server running with %d threads at port %d",
                  ECHO_SERVER_MAX_THREADS, ECHO_SERVER_START_PORT));
    PJ_LOG(3,("", "...Press Ctrl-C to abort"));

    last_received = 0;
    pj_gettimeofday(&last_print);
    avg_bw = highest_bw = 0;
    count = 0;

    for (;;) {
        pj_highprec_t received, cur_received, bw;
        unsigned msec;
        pj_time_val now, duration;

        pj_thread_sleep(1000);

        received = cur_received = pj_atomic_get(total_bytes);
        cur_received = cur_received - last_received;

        pj_gettimeofday(&now);
        duration = now;
        PJ_TIME_VAL_SUB(duration, last_print);
        msec = PJ_TIME_VAL_MSEC(duration);
        
        bw = cur_received;
        pj_highprec_mul(bw, 1000);
        pj_highprec_div(bw, msec);

        last_print = now;
        last_received = received;

        avg_bw = avg_bw + bw;
        count++;

        PJ_LOG(3,("", "Synchronous UDP (%d threads): %u KB/s  (avg=%u KB/s) %s", 
                  ECHO_SERVER_MAX_THREADS, 
                  (unsigned)(bw / 1000),
                  (unsigned)(avg_bw / count / 1000),
                  (count==20 ? "<ses avg>" : "")));

        if (count==20) {
            if (avg_bw/count > highest_bw)
                highest_bw = avg_bw/count;

            count = 0;
            avg_bw = 0;

            PJ_LOG(3,("", "Highest average bandwidth=%u KB/s",
                          (unsigned)(highest_bw/1000)));
        }
    }
}


