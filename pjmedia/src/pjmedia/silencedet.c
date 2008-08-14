/* $Id$ */
/* 
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
#include <pjmedia/silencedet.h>
#include <pjmedia/alaw_ulaw.h>
#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/pool.h>


#define THIS_FILE   "silencedet.c"

typedef enum pjmedia_silence_det_mode {
    VAD_MODE_NONE,
    VAD_MODE_FIXED,
    VAD_MODE_ADAPTIVE
} pjmedia_silence_det_mode;



/**
 * This structure holds the silence detector state.
 */
struct pjmedia_silence_det
{
    char      objname[PJ_MAX_OBJ_NAME]; /**< VAD name.			    */

    int	      mode;		/**< VAD mode.				    */
    unsigned  ptime;		/**< Frame time, in msec.		    */

    unsigned  min_signal_cnt;	/**< # of signal frames.before talk burst   */
    unsigned  min_silence_cnt;	/**< # of silence frames before silence.    */
    unsigned  recalc_cnt;	/**< # of frames before adaptive recalc.    */

    pj_bool_t in_talk;		/**< In talk burst?			    */
    unsigned  cur_cnt;		/**< # of frames in current mode.	    */
    unsigned  signal_cnt;	/**< # of signal frames received.	    */
    unsigned  silence_cnt;	/**< # of silence frames received	    */
    unsigned  cur_threshold;	/**< Current silence threshold.		    */
    unsigned  weakest_signal;	/**< Weakest signal detected.		    */
    unsigned  loudest_silence;	/**< Loudest silence detected.		    */
};



PJ_DEF(pj_status_t) pjmedia_silence_det_create( pj_pool_t *pool,
						unsigned clock_rate,
						unsigned samples_per_frame,
						pjmedia_silence_det **p_sd)
{
    pjmedia_silence_det *sd;

    PJ_ASSERT_RETURN(pool && p_sd, PJ_EINVAL);

    sd = PJ_POOL_ZALLOC_T(pool, pjmedia_silence_det);

    pj_ansi_strncpy(sd->objname, THIS_FILE, PJ_MAX_OBJ_NAME);
    sd->objname[PJ_MAX_OBJ_NAME-1] = '\0';

    sd->ptime = samples_per_frame * 1000 / clock_rate;
    sd->signal_cnt = 0;
    sd->silence_cnt = 0;
    sd->weakest_signal = 0xFFFFFFFFUL;
    sd->loudest_silence = 0;
     
    /* Default settings */
    pjmedia_silence_det_set_params(sd, -1, -1, -1);

    /* Restart in fixed, silent mode */
    sd->in_talk = PJ_FALSE;
    pjmedia_silence_det_set_adaptive( sd, -1 );

    *p_sd = sd;
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_silence_det_set_name( pjmedia_silence_det *sd,
						  const char *name)
{
    PJ_ASSERT_RETURN(sd && name, PJ_EINVAL);

    pj_ansi_snprintf(sd->objname, PJ_MAX_OBJ_NAME, name, sd);
    sd->objname[PJ_MAX_OBJ_NAME-1] = '\0';
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_silence_det_set_adaptive(pjmedia_silence_det *sd,
						     int threshold)
{
    PJ_ASSERT_RETURN(sd, PJ_EINVAL);

    if (threshold < 0)
	threshold = PJMEDIA_SILENCE_DET_THRESHOLD;

    sd->mode = VAD_MODE_ADAPTIVE;
    sd->cur_threshold = threshold;

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_silence_det_set_fixed( pjmedia_silence_det *sd,
						   int threshold )
{
    PJ_ASSERT_RETURN(sd, PJ_EINVAL);

    if (threshold < 0)
	threshold = PJMEDIA_SILENCE_DET_THRESHOLD;

    sd->mode = VAD_MODE_FIXED;
    sd->cur_threshold = threshold;

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_silence_det_set_params( pjmedia_silence_det *sd,
						    int min_silence,
						    int min_signal,
						    int recalc_time)
{
    PJ_ASSERT_RETURN(sd, PJ_EINVAL);

    if (min_silence == -1)
	min_silence = 500;
    if (min_signal < 0)
	min_signal = sd->ptime;
    if (recalc_time < 0)
	recalc_time = 2000;

    sd->min_signal_cnt = min_signal / sd->ptime;
    sd->min_silence_cnt = min_silence / sd->ptime;
    sd->recalc_cnt = recalc_time / sd->ptime;

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_silence_det_disable( pjmedia_silence_det *sd )
{
    PJ_ASSERT_RETURN(sd, PJ_EINVAL);

    sd->mode = VAD_MODE_NONE;

    return PJ_SUCCESS;
}


PJ_DEF(pj_int32_t) pjmedia_calc_avg_signal( const pj_int16_t samples[],
					    pj_size_t count)
{
    pj_uint32_t sum = 0;
    
    const pj_int16_t * pcm = samples;
    const pj_int16_t * end = samples + count;

    if (count==0)
	return 0;

    while (pcm != end) {
	if (*pcm < 0)
	    sum -= *pcm++;
	else
	    sum += *pcm++;
    }
    
    return (pj_int32_t)(sum / count);
}

PJ_DEF(pj_bool_t) pjmedia_silence_det_apply( pjmedia_silence_det *sd,
					     pj_uint32_t level)
{
    pj_bool_t have_signal;

    /* Always return false if VAD is disabled */
    if (sd->mode == VAD_MODE_NONE)
	return PJ_FALSE;

    /* Convert PCM level to ulaw */
    level = pjmedia_linear2ulaw(level) ^ 0xff;
    
    /* Do we have signal? */
    have_signal = level > sd->cur_threshold;
    
    /* We we're in transition between silence and signel, increment the 
     * current frame counter. We will only switch mode when we have enough
     * frames.
     */
    if (sd->in_talk != have_signal) {
	unsigned limit;

	sd->cur_cnt++;

	limit = (sd->in_talk ? sd->min_silence_cnt : 
				sd->min_signal_cnt);

	if (sd->cur_cnt > limit) {

	    /* Swap mode */
	    sd->in_talk = !sd->in_talk;
	    
	    /* Restart adaptive cur_threshold measurements */
	    sd->weakest_signal = 0xFFFFFFFFUL;
	    sd->loudest_silence = 0;
	    sd->signal_cnt = 0;
	    sd->silence_cnt = 0;
	    sd->cur_cnt = 0;
	}

    } else {
	/* Reset frame count */
	sd->cur_cnt = 0;
    }
    

    /* Count the number of silent and signal frames and calculate min/max */
    if (have_signal) {
	if (level < sd->weakest_signal)
	    sd->weakest_signal = level;
	sd->signal_cnt++;
    }
    else {
	if (level > sd->loudest_silence)
	    sd->loudest_silence = level;
	sd->silence_cnt++;
    }

    /* See if we have had enough frames to look at proportions of 
     * silence/signal frames.
     */
    if ((sd->signal_cnt + sd->silence_cnt) > sd->recalc_cnt) {
	
	if (sd->mode == VAD_MODE_ADAPTIVE) {
	    pj_bool_t updated = PJ_TRUE;
	    unsigned pct_signal, new_threshold = sd->cur_threshold;

	    /* Get percentage of signal */
	    pct_signal = sd->signal_cnt * 100 / 
		        (sd->signal_cnt + sd->silence_cnt);

	    /* Adjust according to signal/silence proportions. */
	    if (pct_signal > 95) {
		new_threshold += (sd->weakest_signal+1 - sd->cur_threshold)/2;
	    } else if (pct_signal < 5) {
		new_threshold = (sd->cur_threshold+sd->loudest_silence)/2+1;
	    } else if (pct_signal > 80) {
		new_threshold++;
	    } else if (pct_signal < 10) {
		new_threshold--;
	    } else {
		updated = PJ_FALSE;
	    }

	    if (new_threshold > PJMEDIA_SILENCE_DET_MAX_THRESHOLD)
		new_threshold = PJMEDIA_SILENCE_DET_MAX_THRESHOLD;

	    if (updated && sd->cur_threshold != new_threshold) {
		PJ_LOG(5,(sd->objname, 
			  "Vad cur_threshold updated %d-->%d. "
			  "Signal lo=%d",
			  sd->cur_threshold, new_threshold,
			  sd->weakest_signal));
		sd->cur_threshold = new_threshold;
	    }
	}

	/* Reset. */
	sd->weakest_signal = 0xFFFFFFFFUL;
	sd->loudest_silence = 0;
	sd->signal_cnt = 0;
	sd->silence_cnt = 0;
    }
    
    return !sd->in_talk;

}


PJ_DEF(pj_bool_t) pjmedia_silence_det_detect( pjmedia_silence_det *sd,
					      const pj_int16_t samples[],
					      pj_size_t count,
					      pj_int32_t *p_level)
{
    pj_uint32_t level;
    
    /* Calculate average signal level. */
    level = pjmedia_calc_avg_signal(samples, count);
    
    /* Report to caller, if required. */
    if (p_level)
	*p_level = level;

    return pjmedia_silence_det_apply(sd, level);
}

