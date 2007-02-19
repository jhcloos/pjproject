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
#include "test.h"

#define THIS_FILE   "test.c"

#define DO_TEST(test)	do { \
			    PJ_LOG(3, (THIS_FILE, "Running %s...", #test));  \
			    rc = test; \
			    PJ_LOG(3, (THIS_FILE,  \
				       "%s(%d)",  \
				       (rc ? "..ERROR" : "..success"), rc)); \
			    if (rc!=0) goto on_return; \
			} while (0)


pj_pool_factory *mem;


void app_perror(pj_status_t status, const char *msg)
{
    char errbuf[PJMEDIA_ERR_MSG_SIZE];
    
    pjmedia_strerror(status, errbuf, sizeof(errbuf));

    PJ_LOG(3,(THIS_FILE, "%s: %s", msg, errbuf));
}

int test_main(void)
{
    int rc;
    pj_caching_pool caching_pool;

    pj_init();
    pj_caching_pool_init(&caching_pool, &pj_pool_factory_default_policy, 0);

    pj_log_set_decor(PJ_LOG_HAS_NEWLINE);

    mem = &caching_pool.factory;

    DO_TEST(sdp_neg_test());
    //sdp_test (&caching_pool.factory);
    //rtp_test(&caching_pool.factory);
    //session_test (&caching_pool.factory);
    //jbuf_main(&caching_pool.factory);

    PJ_LOG(3,(THIS_FILE," "));

on_return:

    if (rc != 0) {
	PJ_LOG(3,(THIS_FILE,"Test completed with error(s)!"));
    } else {
	PJ_LOG(3,(THIS_FILE,"Looks like everything is okay!"));
    }

    pj_caching_pool_destroy(&caching_pool);
    return rc;
}
