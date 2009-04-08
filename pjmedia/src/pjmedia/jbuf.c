/* $Id$ */
/* 
 * Copyright (C) 2008-2009 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
/*
 * Based on implementation kindly contributed by Switchlab, Ltd.
 */
#include <pjmedia/jbuf.h>
#include <pjmedia/errno.h>
#include <pj/pool.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/math.h>
#include <pj/string.h>


#define THIS_FILE   "jbuf.c"


/* Minimal difference between JB size and 2*burst-level to perform 
 * JB shrinking. 
 */
#define SAFE_SHRINKING_DIFF	1

/* Minimal gap (in ms) between JB shrinking */
#define MIN_SHRINK_GAP_MSEC	200

/* Invalid sequence number, used as the initial value. */
#define INVALID_OFFSET		-9999

/* Maximum burst length, whenever an operation is bursting longer than 
 * this value, JB will assume that the opposite operation was idle.
 */
#define MAX_BURST_MSEC		1000

/* Number of OP switches to be performed in JB_STATUS_INITIALIZING, before
 * JB can switch its states to JB_STATUS_PROCESSING.
 */
#define INIT_CYCLE		10


/* Struct of JB internal buffer, represented in a circular buffer containing
 * frame content, frame type, frame length, and frame bit info.
 */
typedef struct jb_framelist_t
{
    /* Settings */
    unsigned	     frame_size;	/**< maximum size of frame	    */
    unsigned	     max_count;		/**< maximum number of frames	    */

    /* Buffers */
    char	    *content;		/**< frame content array	    */
    int		    *frame_type;	/**< frame type array		    */
    pj_size_t	    *content_len;	/**< frame length array		    */
    pj_uint32_t	    *bit_info;		/**< frame bit info array	    */
    
    /* States */
    unsigned	     head;		/**< index of head, pointed frame
					     will be returned by next GET   */
    unsigned	     size;		/**< current size of framelist.	    */
    int		     origin;		/**< original index of flist_head   */
} jb_framelist_t;


struct pjmedia_jbuf
{
    /* Settings (consts) */
    pj_str_t	    jb_name;		/**< jitter buffer name		    */
    pj_size_t	    jb_frame_size;	/**< frame size			    */
    unsigned	    jb_frame_ptime;	/**< frame duration.		    */
    pj_size_t	    jb_max_count;	/**< capacity of jitter buffer, 
					     in frames			    */
    int		    jb_def_prefetch;	/**< Default prefetch		    */
    int		    jb_min_prefetch;	/**< Minimum allowable prefetch	    */
    int		    jb_max_prefetch;	/**< Maximum allowable prefetch	    */
    int		    jb_max_burst;	/**< maximum possible burst, whenever
					     burst exceeds this value, it 
					     won't be included in level 
					     calculation		    */
    int		    jb_min_shrink_gap;	/**< How often can we shrink	    */

    /* Buffer */
    jb_framelist_t  jb_framelist;	/**< the buffer			    */

    /* States */
    int		    jb_level;		/**< delay between source & 
					     destination (calculated according 
					     of the number of burst get/put 
					     operations)		    */
    int		    jb_max_hist_level;  /**< max level during the last level 
					     calculations		    */
    int		    jb_stable_hist;	/**< num of times the delay has	been 
					     lower then the prefetch num    */
    int		    jb_last_op;		/**< last operation executed 
					     (put/get)			    */
    int		    jb_prefetch;	/**< no. of frame to insert before 
					     removing some (at the beginning 
					     of the framelist->content 
					     operation), the value may be
					     continuously updated based on
					     current frame burst level.	    */
    int		    jb_status;		/**< status is 'init' until the	first 
					     'put' operation		    */
    int		    jb_init_cycle_cnt;	/**< status is 'init' until the	first 
					     'put' operation		    */
    int		    jb_last_del_seq;	/**< Seq # of last frame deleted    */

    /* Statistics */
    pj_math_stat    jb_delay;		/**< Delay statistics of jitter buffer 
					     (in ms)			    */
    pj_math_stat    jb_burst;		/**< Burst statistics (in frames)   */
    unsigned	    jb_lost;		/**< Number of lost frames.	    */
    unsigned	    jb_discard;		/**< Number of discarded frames.    */
    unsigned	    jb_empty;		/**< Number of empty/prefetching frame
					     returned by GET. */
};


#define JB_STATUS_INITIALIZING	0
#define JB_STATUS_PROCESSING	1
#define JB_STATUS_PREFETCHING	2

/* Enabling this would log the jitter buffer state about once per 
 * second.
 */
#if 1
#  define TRACE__(args)	    PJ_LOG(5,args)
#else
#  define TRACE__(args)
#endif

static pj_status_t jb_framelist_reset(jb_framelist_t *framelist);

static pj_status_t jb_framelist_init( pj_pool_t *pool,
				      jb_framelist_t *framelist,
				      unsigned frame_size,
				      unsigned max_count) 
{
    PJ_ASSERT_RETURN(pool && framelist, PJ_EINVAL);

    pj_bzero(framelist, sizeof(jb_framelist_t));

    framelist->frame_size   = frame_size;
    framelist->max_count    = max_count;
    framelist->content	    = (char*) 
			      pj_pool_alloc(pool,
					    framelist->frame_size* 
					    framelist->max_count);
    framelist->frame_type   = (int*)
			      pj_pool_alloc(pool, 
					    sizeof(framelist->frame_type[0])*
					    framelist->max_count);
    framelist->content_len  = (pj_size_t*)
			      pj_pool_alloc(pool, 
					    sizeof(framelist->content_len[0])*
					    framelist->max_count);
    framelist->bit_info	    = (pj_uint32_t*)
			      pj_pool_alloc(pool, 
					    sizeof(framelist->bit_info[0])*
					    framelist->max_count);

    return jb_framelist_reset(framelist);

}

static pj_status_t jb_framelist_destroy(jb_framelist_t *framelist) 
{
    PJ_UNUSED_ARG(framelist);
    return PJ_SUCCESS;
}

static pj_status_t jb_framelist_reset(jb_framelist_t *framelist) 
{
    framelist->head = 0;
    framelist->origin = INVALID_OFFSET;
    framelist->size = 0;

    //pj_bzero(framelist->content, 
    //	     framelist->frame_size * 
    //	     framelist->max_count);

    pj_memset(framelist->frame_type,
	      PJMEDIA_JB_MISSING_FRAME,
	      sizeof(framelist->frame_type[0]) * 
	      framelist->max_count);

    pj_bzero(framelist->content_len, 
	     sizeof(framelist->content_len[0]) * 
	     framelist->max_count);

    //pj_bzero(framelist->bit_info,
    //	     sizeof(framelist->bit_info[0]) * 
    //	     framelist->max_count);

    return PJ_SUCCESS;
}


static unsigned jb_framelist_size(jb_framelist_t *framelist) 
{
    return framelist->size;
}

static unsigned jb_framelist_origin(jb_framelist_t *framelist) 
{
    return framelist->origin;
}


static pj_bool_t jb_framelist_get(jb_framelist_t *framelist,
				  void *frame, pj_size_t *size,
				  pjmedia_jb_frame_type *p_type,
				  pj_uint32_t *bit_info) 
{
    if (framelist->size) {
	pj_memcpy(frame, 
		  framelist->content + 
		  framelist->head * framelist->frame_size,
		  framelist->frame_size);
	*p_type = (pjmedia_jb_frame_type) 
		  framelist->frame_type[framelist->head];
	if (size)
	    *size   = framelist->content_len[framelist->head];
	if (bit_info)
	    *bit_info = framelist->bit_info[framelist->head];

	//pj_bzero(framelist->content + 
	//	 framelist->head * framelist->frame_size,
	//	 framelist->frame_size);
	framelist->frame_type[framelist->head] = PJMEDIA_JB_MISSING_FRAME;
	framelist->content_len[framelist->head] = 0;
	framelist->bit_info[framelist->head] = 0;

	framelist->origin++;
	framelist->head = (framelist->head + 1) % framelist->max_count;
	framelist->size--;
	
	return PJ_TRUE;
    } else {
	pj_bzero(frame, framelist->frame_size);

	return PJ_FALSE;
    }
}


static unsigned jb_framelist_remove_head(jb_framelist_t *framelist,
					 unsigned count) 
{
    if (count > framelist->size) 
	count = framelist->size;

    if (count) {
	/* may be done in two steps if overlapping */
	unsigned step1,step2;
	unsigned tmp = framelist->head+count;

	if (tmp > framelist->max_count) {
	    step1 = framelist->max_count - framelist->head;
	    step2 = count-step1;
	} else {
	    step1 = count;
	    step2 = 0;
	}

	//pj_bzero(framelist->content + 
	//	    framelist->head * framelist->frame_size,
	//          step1*framelist->frame_size);
	pj_memset(framelist->frame_type+framelist->head,
		  PJMEDIA_JB_MISSING_FRAME,
		  step1*sizeof(framelist->frame_type[0]));
	pj_bzero(framelist->content_len+framelist->head,
		  step1*sizeof(framelist->content_len[0]));

	if (step2) {
	    //pj_bzero( framelist->content,
	    //	      step2*framelist->frame_size);
	    pj_memset(framelist->frame_type,
		      PJMEDIA_JB_MISSING_FRAME,
		      step2*sizeof(framelist->frame_type[0]));
	    pj_bzero (framelist->content_len,
		      step2*sizeof(framelist->content_len[0]));
	}

	/* update states */
	framelist->origin += count;
	framelist->head = (framelist->head + count) % framelist->max_count;
	framelist->size -= count;
    }
    
    return count;
}


static pj_status_t jb_framelist_put_at(jb_framelist_t *framelist,
				       int index,
				       const void *frame,
				       unsigned frame_size,
				       pj_uint32_t bit_info)
{
    int distance;
    unsigned where;
    enum { MAX_MISORDER = 100 };
    enum { MAX_DROPOUT = 3000 };

    assert(frame_size <= framelist->frame_size);

    /* the first ever frame since inited/resetted. */
    if (framelist->origin == INVALID_OFFSET)
	framelist->origin = index;

    /* too late or duplicated or sequence restart */
    if (index < framelist->origin) {
	if (framelist->origin - index < MAX_MISORDER) {
	    /* too late or duplicated */
	    return PJ_ETOOSMALL;
	} else {
	    /* sequence restart */
	    framelist->origin = index - framelist->size;
	}
    }

    /* get distance of this frame to the first frame in the buffer */
    distance = index - framelist->origin;

    /* far jump, the distance is greater than buffer capacity */
    if (distance >= (int)framelist->max_count) {
	if (distance > MAX_DROPOUT) {
	    /* jump too far, reset the buffer */
	    jb_framelist_reset(framelist);
	    framelist->origin = index;
	    distance = 0;
	} else {
	    if (framelist->size == 0) {
		/* if jbuf is empty, process the frame */
		framelist->origin = index;
		distance = 0;
	    } else {
		/* otherwise, reject the frame */
		return PJ_ETOOMANY;
	    }
	}
    }

    /* get the slot position */
    where = (framelist->head + distance) % framelist->max_count;

    /* if the slot is occupied, it must be duplicated frame, ignore it. */
    if (framelist->frame_type[where] != PJMEDIA_JB_MISSING_FRAME)
	return PJ_EEXISTS;

    /* put the frame into the slot */
    pj_memcpy(framelist->content + where * framelist->frame_size,
	      frame, frame_size);
    framelist->frame_type[where] = PJMEDIA_JB_NORMAL_FRAME;
    framelist->content_len[where] = frame_size;
    framelist->bit_info[where] = bit_info;
    if (framelist->origin + (int)framelist->size <= index)
	framelist->size = distance + 1;

    return PJ_SUCCESS;
}



enum pjmedia_jb_op
{
    JB_OP_INIT  = -1,
    JB_OP_PUT   = 1,
    JB_OP_GET   = 2
};


PJ_DEF(pj_status_t) pjmedia_jbuf_create(pj_pool_t *pool, 
					const pj_str_t *name,
					unsigned frame_size, 
					unsigned ptime,
					unsigned max_count,
					pjmedia_jbuf **p_jb)
{
    pjmedia_jbuf *jb;
    pj_status_t status;

    jb = PJ_POOL_ZALLOC_T(pool, pjmedia_jbuf);

    status = jb_framelist_init(pool, &jb->jb_framelist, frame_size, max_count);
    if (status != PJ_SUCCESS)
	return status;

    pj_strdup_with_null(pool, &jb->jb_name, name);
    jb->jb_frame_size	 = frame_size;
    jb->jb_frame_ptime   = ptime;
    jb->jb_prefetch	 = PJ_MIN(PJMEDIA_JB_DEFAULT_INIT_DELAY,max_count*4/5);
    jb->jb_min_prefetch  = 0;
    jb->jb_max_prefetch  = max_count*4/5;
    jb->jb_max_count	 = max_count;
    jb->jb_min_shrink_gap= MIN_SHRINK_GAP_MSEC / ptime;
    jb->jb_max_burst	 = MAX_BURST_MSEC / ptime;
    pj_math_stat_init(&jb->jb_delay);
    pj_math_stat_init(&jb->jb_burst);

    pjmedia_jbuf_reset(jb);

    *p_jb = jb;
    return PJ_SUCCESS;
}


/*
 * Set the jitter buffer to fixed delay mode. The default behavior
 * is to adapt the delay with actual packet delay.
 *
 */
PJ_DEF(pj_status_t) pjmedia_jbuf_set_fixed( pjmedia_jbuf *jb,
					    unsigned prefetch)
{
    PJ_ASSERT_RETURN(jb, PJ_EINVAL);
    PJ_ASSERT_RETURN(prefetch <= jb->jb_max_count, PJ_EINVAL);

    jb->jb_min_prefetch = jb->jb_max_prefetch = 
	jb->jb_prefetch = jb->jb_def_prefetch = prefetch;

    return PJ_SUCCESS;
}


/*
 * Set the jitter buffer to adaptive mode.
 */
PJ_DEF(pj_status_t) pjmedia_jbuf_set_adaptive( pjmedia_jbuf *jb,
					       unsigned prefetch,
					       unsigned min_prefetch,
					       unsigned max_prefetch)
{
    PJ_ASSERT_RETURN(jb, PJ_EINVAL);
    PJ_ASSERT_RETURN(min_prefetch < max_prefetch &&
		     prefetch <= max_prefetch &&
		     max_prefetch <= jb->jb_max_count,
		     PJ_EINVAL);

    jb->jb_prefetch = jb->jb_def_prefetch = prefetch;
    jb->jb_min_prefetch = min_prefetch;
    jb->jb_max_prefetch = max_prefetch;

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_jbuf_reset(pjmedia_jbuf *jb)
{
    jb->jb_level	 = 0;
    jb->jb_last_op	 = JB_OP_INIT;
    jb->jb_stable_hist	 = 0;
    jb->jb_status	 = JB_STATUS_INITIALIZING;
    jb->jb_init_cycle_cnt= 0;
    jb->jb_max_hist_level= 0;

    jb_framelist_reset(&jb->jb_framelist);

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_jbuf_destroy(pjmedia_jbuf *jb)
{
    TRACE__((jb->jb_name.ptr, "\n"
	    "  JB summary:\n"
	    "    size=%d prefetch=%d\n"
	    "    delay (min/max/avg/dev)=%d/%d/%d/%d ms\n"
	    "    burst (min/max/avg/dev)=%d/%d/%d/%d frames\n"
	    "    lost=%d discard=%d empty=%d\n",
	    jb->jb_framelist.size, jb->jb_prefetch,
	    jb->jb_delay.min, jb->jb_delay.max, jb->jb_delay.mean, 
	    pj_math_stat_get_stddev(&jb->jb_delay),
	    jb->jb_burst.min, jb->jb_burst.max, jb->jb_burst.mean, 
	    pj_math_stat_get_stddev(&jb->jb_burst),
	    jb->jb_lost, jb->jb_discard, jb->jb_empty));

    return jb_framelist_destroy(&jb->jb_framelist);
}


static void jbuf_calculate_jitter(pjmedia_jbuf *jb)
{
    int diff, cur_size;

    cur_size = jb_framelist_size(&jb->jb_framelist);
    pj_math_stat_update(&jb->jb_burst, jb->jb_level);
    jb->jb_max_hist_level = PJ_MAX(jb->jb_max_hist_level, jb->jb_level);

    /* Burst level is decreasing */
    if (jb->jb_level < jb->jb_prefetch) {

	enum { STABLE_HISTORY_LIMIT = 100 };
        
	jb->jb_stable_hist++;
        
	/* Only update the prefetch if 'stable' condition is reached 
	 * (not just short time impulse)
	 */
	if (jb->jb_stable_hist > STABLE_HISTORY_LIMIT) {
    	
	    diff = (jb->jb_prefetch - jb->jb_max_hist_level) / 3;

	    if (diff < 1)
		diff = 1;

	    jb->jb_prefetch -= diff;
	    if (jb->jb_prefetch < jb->jb_min_prefetch) 
		jb->jb_prefetch = jb->jb_min_prefetch;

	    /* Reset history */
	    jb->jb_max_hist_level = 0;
	    jb->jb_stable_hist = 0;

	    TRACE__((jb->jb_name.ptr,"jb updated(1), prefetch=%d, size=%d",
		     jb->jb_prefetch, cur_size));
	}
    }

    /* Burst level is increasing */
    else if (jb->jb_level > jb->jb_prefetch) {

	/* Instaneous set prefetch to recent maximum level (max_hist_level) */
	jb->jb_prefetch = PJ_MIN(jb->jb_max_hist_level,
				 (int)(jb->jb_max_count*4/5));
	if (jb->jb_prefetch > jb->jb_max_prefetch)
	    jb->jb_prefetch = jb->jb_max_prefetch;

	jb->jb_stable_hist = 0;
	/* Do not reset max_hist_level. */
	//jb->jb_max_hist_level = 0;

	TRACE__((jb->jb_name.ptr,"jb updated(2), prefetch=%d, size=%d", 
		 jb->jb_prefetch, cur_size));
    }

    /* Level is unchanged */
    else {
	jb->jb_stable_hist = 0;
    }
}

PJ_INLINE(void) jbuf_update(pjmedia_jbuf *jb, int oper)
{
    int diff, burst_level;

    if(jb->jb_last_op != oper) {
	jb->jb_last_op = oper;

	if (jb->jb_status == JB_STATUS_INITIALIZING) {
	    /* Switch status 'initializing' -> 'processing' after some OP 
	     * switch cycles and current OP is GET (burst level is calculated 
	     * based on PUT burst), so burst calculation is guaranted to be
	     * performed right after the status switching.
	     */
	    if (++jb->jb_init_cycle_cnt >= INIT_CYCLE && oper == JB_OP_GET) {
		jb->jb_status = JB_STATUS_PROCESSING;
	    } else {
		jb->jb_level = 0;
		return;
	    }
	}

	/* Perform jitter calculation based on PUT burst-level only, since 
	 * GET burst-level may not be accurate, e.g: when VAD is active.
	 * Note that when burst-level is too big, i.e: exceeds jb_max_burst,
	 * the GET op may be idle, in this case, we better skip the jitter 
	 * calculation.
	 */
	if (oper == JB_OP_GET && jb->jb_level < jb->jb_max_burst)
	    jbuf_calculate_jitter(jb);

	jb->jb_level = 0;
    }

    /* These code is used for shortening the delay in the jitter buffer.
     * It needs shrink only when there is possibility of drift. Drift
     * detection is performed by inspecting the jitter buffer size, if
     * its size is twice of current burst level, there can be drift.
     *
     * Moreover, normally drift level is quite low, so JB shouldn't need
     * to shrink aggresively, it will shrink maximum one frame per 
     * MIN_SHRINK_GAP_MSEC ms. Theoritically, JB may handle drift level 
     * as much as = FRAME_PTIME/MIN_SHRINK_GAP_MSEC * 100%
     *
     * Whenever there is drift, where PUT > GET, this method will keep 
     * the latency (JB size) as much as twice of burst level.
     */

    if (jb->jb_status != JB_STATUS_PROCESSING)
	return;

    burst_level = PJ_MAX(jb->jb_prefetch, jb->jb_level);
    diff = jb_framelist_size(&jb->jb_framelist) - burst_level*2;

    if (diff >= SAFE_SHRINKING_DIFF) {
	/* Check and adjust jb_last_del_seq, in case there was seq restart */
	if (jb->jb_framelist.origin < jb->jb_last_del_seq)
	    jb->jb_last_del_seq = jb->jb_framelist.origin;

	if (jb->jb_framelist.origin - jb->jb_last_del_seq >=
	    jb->jb_min_shrink_gap)
	{
	    /* Shrink slowly, one frame per cycle */
	    diff = 1;

	    /* Drop frame(s)! */
	    diff = jb_framelist_remove_head(&jb->jb_framelist, diff);
	    jb->jb_last_del_seq = jb->jb_framelist.origin;
	    jb->jb_discard += diff;

	    TRACE__((jb->jb_name.ptr, 
		     "JB shrinking %d frame(s), cur size=%d", diff,
		     jb_framelist_size(&jb->jb_framelist)));
	}
    }
}

PJ_DEF(void) pjmedia_jbuf_put_frame( pjmedia_jbuf *jb, 
				     const void *frame, 
				     pj_size_t frame_size, 
				     int frame_seq)
{
    pjmedia_jbuf_put_frame2(jb, frame, frame_size, 0, frame_seq, NULL);
}

PJ_DEF(void) pjmedia_jbuf_put_frame2(pjmedia_jbuf *jb, 
				     const void *frame, 
				     pj_size_t frame_size, 
				     pj_uint32_t bit_info,
				     int frame_seq,
				     pj_bool_t *discarded)
{
    pj_size_t min_frame_size;
    int prev_size, cur_size;
    pj_status_t status;

    /* Get JB size before PUT */
    prev_size = jb_framelist_size(&jb->jb_framelist);
    
    /* Attempt to store the frame */
    min_frame_size = PJ_MIN(frame_size, jb->jb_frame_size);
    status = jb_framelist_put_at(&jb->jb_framelist, frame_seq, frame,
				 min_frame_size, bit_info);
    
    /* Jitter buffer is full, cannot store the frame */
    while (status == PJ_ETOOMANY) {
	unsigned removed;

	removed = jb_framelist_remove_head(&jb->jb_framelist,
					   PJ_MAX(jb->jb_max_count/4, 1));
	status = jb_framelist_put_at(&jb->jb_framelist, frame_seq, frame,
				     min_frame_size, bit_info);

	jb->jb_discard += removed;
    }

    /* Get JB size after PUT */
    cur_size = jb_framelist_size(&jb->jb_framelist);

    /* Return the flag if this frame is discarded */
    if (discarded)
	*discarded = (status != PJ_SUCCESS);

    if (status == PJ_SUCCESS) {
	if (jb->jb_status == JB_STATUS_PREFETCHING) {
	    TRACE__((jb->jb_name.ptr, "PUT prefetch_cnt=%d/%d", 
		     cur_size, jb->jb_prefetch));
	    if (cur_size >= jb->jb_prefetch)
		jb->jb_status = JB_STATUS_PROCESSING;
	}
	jb->jb_level += (cur_size > prev_size ? cur_size-prev_size : 1);
	jbuf_update(jb, JB_OP_PUT);
    } else
	jb->jb_discard++;
}

/*
 * Get frame from jitter buffer.
 */
PJ_DEF(void) pjmedia_jbuf_get_frame( pjmedia_jbuf *jb, 
				     void *frame, 
				     char *p_frame_type)
{
    pjmedia_jbuf_get_frame2(jb, frame, NULL, p_frame_type, NULL);
}

/*
 * Get frame from jitter buffer.
 */
PJ_DEF(void) pjmedia_jbuf_get_frame2(pjmedia_jbuf *jb, 
				     void *frame, 
				     pj_size_t *size,
				     char *p_frame_type,
				     pj_uint32_t *bit_info)
{
    int cur_size;

    cur_size = jb_framelist_size(&jb->jb_framelist);

    if (cur_size == 0) {
	/* jitter buffer empty */

	if (jb->jb_def_prefetch)
	    jb->jb_status = JB_STATUS_PREFETCHING;

	//pj_bzero(frame, jb->jb_frame_size);
	*p_frame_type = PJMEDIA_JB_ZERO_EMPTY_FRAME;
	if (size)
	    *size = 0;

	jb->jb_empty++;

    } else if (jb->jb_status == JB_STATUS_PREFETCHING) {

	/* Can't return frame because jitter buffer is filling up
	 * minimum prefetch.
	 */

	//pj_bzero(frame, jb->jb_frame_size);
	*p_frame_type = PJMEDIA_JB_ZERO_PREFETCH_FRAME;
	if (size)
	    *size = 0;

	TRACE__((jb->jb_name.ptr, "GET prefetch_cnt=%d/%d",
		 cur_size, jb->jb_prefetch));

	jb->jb_empty++;

    } else {

	pjmedia_jb_frame_type ftype;
	pj_bool_t res;

	/* Retrieve a frame from frame list */
	res = jb_framelist_get(&jb->jb_framelist, frame, size, &ftype, 
			       bit_info);
	pj_assert(res);

	/* We've successfully retrieved a frame from the frame list, but
	 * the frame could be a blank frame!
	 */
	if (ftype == PJMEDIA_JB_NORMAL_FRAME) {
	    *p_frame_type = PJMEDIA_JB_NORMAL_FRAME;
	} else {
	    *p_frame_type = PJMEDIA_JB_MISSING_FRAME;
	    jb->jb_lost++;
	}

	/* Calculate delay on the first GET */
	if (jb->jb_last_op == JB_OP_PUT)
	    pj_math_stat_update(&jb->jb_delay, cur_size * jb->jb_frame_ptime);
    }

    jb->jb_level++;
    jbuf_update(jb, JB_OP_GET);
}

/*
 * Get jitter buffer state.
 */
PJ_DEF(pj_status_t) pjmedia_jbuf_get_state( pjmedia_jbuf *jb,
					    pjmedia_jb_state *state )
{
    PJ_ASSERT_RETURN(jb && state, PJ_EINVAL);

    state->frame_size = jb->jb_frame_size;
    state->min_prefetch = jb->jb_min_prefetch;
    state->max_prefetch = jb->jb_max_prefetch;
    
    state->prefetch = jb->jb_prefetch;
    state->size = jb_framelist_size(&jb->jb_framelist);
    
    state->avg_delay = jb->jb_delay.mean;
    state->min_delay = jb->jb_delay.min;
    state->max_delay = jb->jb_delay.max;
    state->dev_delay = pj_math_stat_get_stddev(&jb->jb_delay);
    
    state->avg_burst = jb->jb_burst.mean;
    state->empty = jb->jb_empty;
    state->discard = jb->jb_discard;
    state->lost = jb->jb_lost;

    return PJ_SUCCESS;
}
