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
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/log.h>
#include <pj/string.h>
#include <pjmedia/sound.h>
#include <stdio.h>
#include <string.h>

#define CLOCK_RATE		8000
#define SAMPLES_PER_FRAME	80


pj_status_t rec_cb(/* in */   void *user_data,
		   /* in */   pj_uint32_t timestamp,
		   /* in */   const void *input,
		   /* in*/    unsigned size)
{
	PJ_UNUSED_ARG(user_data);
	PJ_UNUSED_ARG(timestamp);
	PJ_UNUSED_ARG(input);
	PJ_UNUSED_ARG(size);
	return PJ_SUCCESS;
}

pj_status_t play_cb(/* in */   void *user_data,
		    /* in */   pj_uint32_t timestamp,
		    /* out */  void *output,
		    /* out */  unsigned size)
{
	PJ_UNUSED_ARG(user_data);
	PJ_UNUSED_ARG(timestamp);
	PJ_UNUSED_ARG(output);
	PJ_UNUSED_ARG(size);
	
	pj_memset(output, 0, size);
	return PJ_SUCCESS;
}


int test_main()
{
    pj_status_t status;
    pj_caching_pool cp;
    pjmedia_snd_stream *strm;

    status = pj_init();
    if (status != PJ_SUCCESS)
    	return status;
    
    pj_caching_pool_init(&cp, pj_pool_factory_get_default_policy(), 0);
    pj_log_set_decor(PJ_LOG_HAS_NEWLINE);

    status = pjmedia_snd_init(&cp.factory);
    if (status != PJ_SUCCESS)
    	goto on_return;
    
    //status = pjmedia_snd_open_rec(0, CLOCK_RATE, 1, SAMPLES_PER_FRAME,
    //				  16, &rec_cb, NULL, &strm);
    status = pjmedia_snd_open_player(0, CLOCK_RATE, 1, SAMPLES_PER_FRAME,
    				   16, &play_cb, NULL, &strm);
    if (status != PJ_SUCCESS)
    	goto on_return;				  
    
    status = pjmedia_snd_stream_start(strm);
    if (status != PJ_SUCCESS) 
    {
    	pjmedia_snd_stream_close(strm);
    	goto on_return;
    }
    
    pj_thread_sleep(1000);
    
    pjmedia_snd_stream_stop(strm);
    pjmedia_snd_stream_close(strm);
    
on_return:
    pj_caching_pool_destroy(&cp);
    return status;
}

