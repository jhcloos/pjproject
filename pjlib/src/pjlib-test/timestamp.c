/* $Id$ */
/* 
 * Copyright (C)2003-2006 Benny Prijono <benny@prijono.org>
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
#include <pj/os.h>
#include <pj/log.h>
#include <pj/rand.h>


/**
 * \page page_pjlib_timestamp_test Test: Timestamp
 *
 * This file provides implementation of timestamp_test()
 *
 * \section timestamp_test_sec Scope of the Test
 *
 * This tests whether timestamp API works.
 *
 * API tested:
 *  - pj_get_timestamp_freq()
 *  - pj_get_timestamp()
 *  - pj_elapsed_usec()
 *  - PJ_LOG()
 *
 *
 * This file is <b>pjlib-test/timestamp.c</b>
 *
 * \include pjlib-test/timestamp.c
 */

#if INCLUDE_TIMESTAMP_TEST

#define THIS_FILE   "timestamp"

int timestamp_test(void)
{
    enum { CONSECUTIVE_LOOP = 100 };
    volatile unsigned i;
    pj_timestamp freq, t1, t2;
    pj_time_val tv1, tv2;
    unsigned elapsed;
    pj_status_t rc;

    PJ_LOG(3,(THIS_FILE, "...Testing timestamp (high res time)"));
    
    /* Get and display timestamp frequency. */
    if ((rc=pj_get_timestamp_freq(&freq)) != PJ_SUCCESS) {
	app_perror("...ERROR: get timestamp freq", rc);
	return -1000;
    }

    PJ_LOG(3,(THIS_FILE, "....frequency: hiword=%lu loword=%lu", 
			freq.u32.hi, freq.u32.lo));

    PJ_LOG(3,(THIS_FILE, "...checking if time can run backwards (pls wait).."));

    /*
     * Check if consecutive readings should yield timestamp value
     * that is bigger than previous value.
     * First we get the first timestamp.
     */
    rc = pj_get_timestamp(&t1);
    if (rc != PJ_SUCCESS) {
	app_perror("...ERROR: pj_get_timestamp", rc);
	return -1001;
    }
    rc = pj_gettimeofday(&tv1);
    if (rc != PJ_SUCCESS) {
	app_perror("...ERROR: pj_gettimeofday", rc);
	return -1002;
    }
    for (i=0; i<CONSECUTIVE_LOOP; ++i) {
        
        pj_thread_sleep(pj_rand() % 100);

	rc = pj_get_timestamp(&t2);
	if (rc != PJ_SUCCESS) {
	    app_perror("...ERROR: pj_get_timestamp", rc);
	    return -1003;
	}
	rc = pj_gettimeofday(&tv2);
	if (rc != PJ_SUCCESS) {
	    app_perror("...ERROR: pj_gettimeofday", rc);
	    return -1004;
	}

	/* compare t2 with t1, expecting t2 >= t1. */
	if (t2.u32.hi < t1.u32.hi ||
	    (t2.u32.hi == t1.u32.hi && t2.u32.lo < t1.u32.lo))
	{
	    PJ_LOG(3,(THIS_FILE, "...ERROR: timestamp run backwards!"));
	    return -1005;
	}

	/* compare tv2 with tv1, expecting tv2 >= tv1. */
	if (PJ_TIME_VAL_LT(tv2, tv1)) {
	    PJ_LOG(3,(THIS_FILE, "...ERROR: time run backwards!"));
	    return -1006;
	}
    }

    /* 
     * Simple test to time some loop. 
     */
    PJ_LOG(3,(THIS_FILE, "....testing simple 1000000 loop"));


    /* Mark start time. */
    if ((rc=pj_get_timestamp(&t1)) != PJ_SUCCESS) {
	app_perror("....error: cat't get timestamp", rc);
	return -1010;
    }

    /* Loop.. */
    for (i=0; i<1000000; ++i)
	;

    /* Mark end time. */
    pj_get_timestamp(&t2);

    /* Get elapsed time in usec. */
    elapsed = pj_elapsed_usec(&t1, &t2);
    PJ_LOG(3,(THIS_FILE, "....elapsed: %u usec", (unsigned)elapsed));

    /* See if elapsed time is reasonable. */
    if (elapsed < 1 || elapsed > 100000) {
	PJ_LOG(3,(THIS_FILE, "....error: elapsed time outside window (%u, "
			     "t1.u32.hi=%u, t1.u32.lo=%u, "
			     "t2.u32.hi=%u, t2.u32.lo=%u)",
			     elapsed, 
			     t1.u32.hi, t1.u32.lo, t2.u32.hi, t2.u32.lo));
	return -1030;
    }
    return 0;
}


#else
/* To prevent warning about "translation unit is empty"
 * when this test is disabled. 
 */
int dummy_timestamp_test;
#endif	/* INCLUDE_TIMESTAMP_TEST */

