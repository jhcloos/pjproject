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
#include <pjmedia-codec/ilbc.h>
#include <pjmedia-codec/types.h>
#include <pjmedia/codec.h>
#include <pjmedia/errno.h>
#include <pjmedia/endpoint.h>
#include <pjmedia/plc.h>
#include <pjmedia/port.h>
#include <pjmedia/silencedet.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/os.h>
#include "../../third_party/ilbc/iLBC_encode.h"
#include "../../third_party/ilbc/iLBC_decode.h"


/*
 * Only build this file if PJMEDIA_HAS_ILBC_CODEC != 0
 */
#if defined(PJMEDIA_HAS_ILBC_CODEC) && PJMEDIA_HAS_ILBC_CODEC != 0


#define THIS_FILE	"ilbc.c"
#define CLOCK_RATE	8000
#define DEFAULT_MODE	30


/* Prototypes for iLBC factory */
static pj_status_t ilbc_test_alloc(pjmedia_codec_factory *factory, 
				   const pjmedia_codec_info *id );
static pj_status_t ilbc_default_attr(pjmedia_codec_factory *factory, 
				     const pjmedia_codec_info *id, 
				     pjmedia_codec_param *attr );
static pj_status_t ilbc_enum_codecs(pjmedia_codec_factory *factory, 
				    unsigned *count, 
				    pjmedia_codec_info codecs[]);
static pj_status_t ilbc_alloc_codec(pjmedia_codec_factory *factory, 
				    const pjmedia_codec_info *id, 
				    pjmedia_codec **p_codec);
static pj_status_t ilbc_dealloc_codec(pjmedia_codec_factory *factory, 
				      pjmedia_codec *codec );

/* Prototypes for iLBC implementation. */
static pj_status_t  ilbc_codec_init(pjmedia_codec *codec, 
				    pj_pool_t *pool );
static pj_status_t  ilbc_codec_open(pjmedia_codec *codec, 
				    pjmedia_codec_param *attr );
static pj_status_t  ilbc_codec_close(pjmedia_codec *codec );
static pj_status_t  ilbc_codec_modify(pjmedia_codec *codec, 
				      const pjmedia_codec_param *attr );
static pj_status_t  ilbc_codec_parse(pjmedia_codec *codec,
				     void *pkt,
				     pj_size_t pkt_size,
				     const pj_timestamp *ts,
				     unsigned *frame_cnt,
				     pjmedia_frame frames[]);
static pj_status_t  ilbc_codec_encode(pjmedia_codec *codec, 
				      const struct pjmedia_frame *input,
				      unsigned output_buf_len, 
				      struct pjmedia_frame *output);
static pj_status_t  ilbc_codec_decode(pjmedia_codec *codec, 
				      const struct pjmedia_frame *input,
				      unsigned output_buf_len, 
				      struct pjmedia_frame *output);
static pj_status_t  ilbc_codec_recover(pjmedia_codec *codec,
				       unsigned output_buf_len,
				       struct pjmedia_frame *output);

/* Definition for iLBC codec operations. */
static pjmedia_codec_op ilbc_op = 
{
    &ilbc_codec_init,
    &ilbc_codec_open,
    &ilbc_codec_close,
    &ilbc_codec_modify,
    &ilbc_codec_parse,
    &ilbc_codec_encode,
    &ilbc_codec_decode,
    &ilbc_codec_recover
};

/* Definition for iLBC codec factory operations. */
static pjmedia_codec_factory_op ilbc_factory_op =
{
    &ilbc_test_alloc,
    &ilbc_default_attr,
    &ilbc_enum_codecs,
    &ilbc_alloc_codec,
    &ilbc_dealloc_codec
};

/* iLBC factory */
static struct ilbc_factory
{
    pjmedia_codec_factory    base;
    pjmedia_endpt	    *endpt;

    int			     mode;
    int			     bps;
} ilbc_factory;


/* iLBC codec private data. */
struct ilbc_codec
{
    pjmedia_codec	 base;
    pj_pool_t		*pool;
    char		 obj_name[PJ_MAX_OBJ_NAME];
    pjmedia_silence_det	*vad;
    pj_bool_t		 vad_enabled;
    pj_bool_t		 plc_enabled;
    pj_timestamp	 last_tx;

    pj_bool_t		 enc_ready;
    iLBC_Enc_Inst_t	 enc;
    unsigned		 enc_frame_size;
    unsigned		 enc_samples_per_frame;
    float		 enc_block[BLOCKL_MAX];

    pj_bool_t		 dec_ready;
    iLBC_Dec_Inst_t	 dec;
    unsigned		 dec_frame_size;
    unsigned		 dec_samples_per_frame;
    float		 dec_block[BLOCKL_MAX];
};



/*
 * Initialize and register iLBC codec factory to pjmedia endpoint.
 */
PJ_DEF(pj_status_t) pjmedia_codec_ilbc_init( pjmedia_endpt *endpt,
					     int mode )
{
    pjmedia_codec_mgr *codec_mgr;
    pj_status_t status;

    PJ_ASSERT_RETURN(endpt != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(mode==0 || mode==20 || mode==30, PJ_EINVAL);

    /* Create iLBC codec factory. */
    ilbc_factory.base.op = &ilbc_factory_op;
    ilbc_factory.base.factory_data = NULL;
    ilbc_factory.endpt = endpt;

    if (mode == 0)
	mode = DEFAULT_MODE;

    ilbc_factory.mode = mode;

    if (mode == 20) {
	ilbc_factory.bps = 15200;	
    } else {
	ilbc_factory.bps = 13333;
    }

    /* Get the codec manager. */
    codec_mgr = pjmedia_endpt_get_codec_mgr(endpt);
    if (!codec_mgr)
	return PJ_EINVALIDOP;

    /* Register codec factory to endpoint. */
    status = pjmedia_codec_mgr_register_factory(codec_mgr, 
						&ilbc_factory.base);
    if (status != PJ_SUCCESS)
	return status;


    /* Done. */
    return PJ_SUCCESS;
}



/*
 * Unregister iLBC codec factory from pjmedia endpoint and deinitialize
 * the iLBC codec library.
 */
PJ_DEF(pj_status_t) pjmedia_codec_ilbc_deinit(void)
{
    pjmedia_codec_mgr *codec_mgr;
    pj_status_t status;


    /* Get the codec manager. */
    codec_mgr = pjmedia_endpt_get_codec_mgr(ilbc_factory.endpt);
    if (!codec_mgr)
	return PJ_EINVALIDOP;

    /* Unregister iLBC codec factory. */
    status = pjmedia_codec_mgr_unregister_factory(codec_mgr,
						  &ilbc_factory.base);
    
    return status;
}

/* 
 * Check if factory can allocate the specified codec. 
 */
static pj_status_t ilbc_test_alloc( pjmedia_codec_factory *factory, 
				   const pjmedia_codec_info *info )
{
    const pj_str_t ilbc_tag = { "iLBC", 4};

    PJ_UNUSED_ARG(factory);
    PJ_ASSERT_RETURN(factory==&ilbc_factory.base, PJ_EINVAL);


    /* Type MUST be audio. */
    if (info->type != PJMEDIA_TYPE_AUDIO)
	return PJMEDIA_CODEC_EUNSUP;

    /* Check encoding name. */
    if (pj_stricmp(&info->encoding_name, &ilbc_tag) != 0)
	return PJMEDIA_CODEC_EUNSUP;

    /* Check clock-rate */
    if (info->clock_rate != CLOCK_RATE)
	return PJMEDIA_CODEC_EUNSUP;
    
    /* Channel count must be one */
    if (info->channel_cnt != 1)
	return PJMEDIA_CODEC_EUNSUP;

    /* Yes, this should be iLBC! */
    return PJ_SUCCESS;
}


/*
 * Generate default attribute.
 */
static pj_status_t ilbc_default_attr (pjmedia_codec_factory *factory, 
				      const pjmedia_codec_info *id, 
				      pjmedia_codec_param *attr )
{
    PJ_UNUSED_ARG(factory);
    PJ_ASSERT_RETURN(factory==&ilbc_factory.base, PJ_EINVAL);

    PJ_UNUSED_ARG(id);
    PJ_ASSERT_RETURN(pj_stricmp2(&id->encoding_name, "iLBC")==0, PJ_EINVAL);

    pj_bzero(attr, sizeof(pjmedia_codec_param));

    attr->info.clock_rate = CLOCK_RATE;
    attr->info.channel_cnt = 1;
    attr->info.avg_bps = ilbc_factory.bps;
    attr->info.max_bps = 15200;
    attr->info.pcm_bits_per_sample = 16;
    attr->info.frm_ptime = (short)ilbc_factory.mode;
    attr->info.pt = PJMEDIA_RTP_PT_ILBC;

    attr->setting.frm_per_pkt = 1;
    attr->setting.vad = 1;
    attr->setting.plc = 1;
    attr->setting.penh = 1;
    attr->setting.dec_fmtp_mode = (pj_uint8_t)ilbc_factory.mode;

    return PJ_SUCCESS;
}

/*
 * Enum codecs supported by this factory (i.e. only iLBC!).
 */
static pj_status_t ilbc_enum_codecs(pjmedia_codec_factory *factory, 
				    unsigned *count, 
				    pjmedia_codec_info codecs[])
{
    PJ_UNUSED_ARG(factory);
    PJ_ASSERT_RETURN(factory==&ilbc_factory.base, PJ_EINVAL);

    PJ_ASSERT_RETURN(codecs && *count > 0, PJ_EINVAL);

    pj_bzero(&codecs[0], sizeof(pjmedia_codec_info));

    codecs[0].encoding_name = pj_str("iLBC");
    codecs[0].pt = PJMEDIA_RTP_PT_ILBC;
    codecs[0].type = PJMEDIA_TYPE_AUDIO;
    codecs[0].clock_rate = 8000;
    codecs[0].channel_cnt = 1;

    *count = 1;

    return PJ_SUCCESS;
}

/*
 * Allocate a new iLBC codec instance.
 */
static pj_status_t ilbc_alloc_codec(pjmedia_codec_factory *factory, 
				    const pjmedia_codec_info *id,
				    pjmedia_codec **p_codec)
{
    pj_pool_t *pool;
    struct ilbc_codec *codec;

    PJ_ASSERT_RETURN(factory && id && p_codec, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &ilbc_factory.base, PJ_EINVAL);

    pool = pjmedia_endpt_create_pool(ilbc_factory.endpt, "iLBC%p",
				     2000, 2000);
    PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);

    codec = PJ_POOL_ZALLOC_T(pool, struct ilbc_codec);
    codec->base.op = &ilbc_op;
    codec->base.factory = factory;
    codec->pool = pool;

    pj_ansi_snprintf(codec->obj_name,  sizeof(codec->obj_name),
		     "ilbc%p", codec);

    *p_codec = &codec->base;
    return PJ_SUCCESS;
}


/*
 * Free codec.
 */
static pj_status_t ilbc_dealloc_codec( pjmedia_codec_factory *factory, 
				      pjmedia_codec *codec )
{
    struct ilbc_codec *ilbc_codec;

    PJ_ASSERT_RETURN(factory && codec, PJ_EINVAL);
    PJ_UNUSED_ARG(factory);
    PJ_ASSERT_RETURN(factory == &ilbc_factory.base, PJ_EINVAL);

    ilbc_codec = (struct ilbc_codec*) codec;
    pj_pool_release(ilbc_codec->pool);

    return PJ_SUCCESS;
}

/*
 * Init codec.
 */
static pj_status_t ilbc_codec_init(pjmedia_codec *codec, 
				   pj_pool_t *pool )
{
    PJ_UNUSED_ARG(codec);
    PJ_UNUSED_ARG(pool);
    return PJ_SUCCESS;
}

/*
 * Open codec.
 */
static pj_status_t ilbc_codec_open(pjmedia_codec *codec, 
				   pjmedia_codec_param *attr )
{
    struct ilbc_codec *ilbc_codec = (struct ilbc_codec*)codec;
    pj_status_t status;

    pj_assert(ilbc_codec != NULL);
    pj_assert(ilbc_codec->enc_ready == PJ_FALSE && 
	      ilbc_codec->dec_ready == PJ_FALSE);

    /* Decoder mode must be set */
    PJ_ASSERT_RETURN(attr->setting.dec_fmtp_mode==20 ||
		     attr->setting.dec_fmtp_mode==30, PJMEDIA_CODEC_EINMODE);

    /* The enc mode must be set in the attribute 
     * (from the mode parameter in fmtp attribute in the SDP
     * received from remote) 
     */
    if (attr->setting.enc_fmtp_mode == 0)
	attr->setting.enc_fmtp_mode = attr->setting.dec_fmtp_mode;

    PJ_ASSERT_RETURN(attr->setting.enc_fmtp_mode==20 ||
		     attr->setting.enc_fmtp_mode==30, PJMEDIA_CODEC_EINMODE);

    /* Update enc_ptime in the param */
    if (attr->setting.enc_fmtp_mode != attr->setting.dec_fmtp_mode) {
	attr->info.enc_ptime = attr->setting.enc_fmtp_mode;
    } else {
	attr->info.enc_ptime = 0;
    }

    /* Create enc */
    ilbc_codec->enc_frame_size = initEncode(&ilbc_codec->enc, 
					    attr->setting.enc_fmtp_mode);
    ilbc_codec->enc_samples_per_frame = CLOCK_RATE*attr->setting.enc_fmtp_mode/
					1000;
    ilbc_codec->enc_ready = PJ_TRUE;

    /* Create decoder */
    ilbc_codec->dec_samples_per_frame = initDecode(&ilbc_codec->dec,
						   attr->setting.dec_fmtp_mode,
						   attr->setting.penh);
    if (attr->setting.dec_fmtp_mode == 20)
	ilbc_codec->dec_frame_size = 38;
    else if (attr->setting.dec_fmtp_mode == 30)
	ilbc_codec->dec_frame_size = 50;
    else {
	pj_assert(!"Invalid iLBC mode");
	ilbc_codec->dec_frame_size = ilbc_codec->enc_frame_size;
    }
    ilbc_codec->dec_ready = PJ_TRUE;

    /* Save plc flags */
    ilbc_codec->plc_enabled = (attr->setting.plc != 0);

    /* Create silence detector. */
    ilbc_codec->vad_enabled = (attr->setting.vad != 0);
    status = pjmedia_silence_det_create(ilbc_codec->pool, CLOCK_RATE,
					ilbc_codec->enc_samples_per_frame,
					&ilbc_codec->vad);
    if (status != PJ_SUCCESS)
	return status;

    /* Init last_tx (not necessary because of zalloc, but better
     * be safe in case someone remove zalloc later.
     */
    pj_set_timestamp32(&ilbc_codec->last_tx, 0, 0);

    PJ_LOG(5,(ilbc_codec->obj_name, 
	      "iLBC codec opened, encoder mode=%d, decoder mode=%d",
	      attr->setting.enc_fmtp_mode, attr->setting.dec_fmtp_mode));

    return PJ_SUCCESS;
}


/*
 * Close codec.
 */
static pj_status_t ilbc_codec_close( pjmedia_codec *codec )
{
    struct ilbc_codec *ilbc_codec = (struct ilbc_codec*)codec;

    PJ_UNUSED_ARG(codec);

    PJ_LOG(5,(ilbc_codec->obj_name, "iLBC codec closed"));

    return PJ_SUCCESS;
}

/*
 * Modify codec settings.
 */
static pj_status_t  ilbc_codec_modify(pjmedia_codec *codec, 
				      const pjmedia_codec_param *attr )
{
    struct ilbc_codec *ilbc_codec = (struct ilbc_codec*)codec;

    ilbc_codec->plc_enabled = (attr->setting.plc != 0);
    ilbc_codec->vad_enabled = (attr->setting.vad != 0);

    return PJ_SUCCESS;
}

/*
 * Get frames in the packet.
 */
static pj_status_t  ilbc_codec_parse( pjmedia_codec *codec,
				     void *pkt,
				     pj_size_t pkt_size,
				     const pj_timestamp *ts,
				     unsigned *frame_cnt,
				     pjmedia_frame frames[])
{
    struct ilbc_codec *ilbc_codec = (struct ilbc_codec*)codec;
    unsigned count;

    PJ_ASSERT_RETURN(frame_cnt, PJ_EINVAL);

    count = 0;
    while (pkt_size >= ilbc_codec->dec_frame_size && count < *frame_cnt) {
	frames[count].type = PJMEDIA_FRAME_TYPE_AUDIO;
	frames[count].buf = pkt;
	frames[count].size = ilbc_codec->dec_frame_size;
	frames[count].timestamp.u64 = ts->u64 + count * 
					  ilbc_codec->dec_samples_per_frame;

	pkt = ((char*)pkt) + ilbc_codec->dec_frame_size;
	pkt_size -= ilbc_codec->dec_frame_size;

	++count;
    }

    *frame_cnt = count;
    return PJ_SUCCESS;
}

/*
 * Encode frame.
 */
static pj_status_t ilbc_codec_encode(pjmedia_codec *codec, 
				     const struct pjmedia_frame *input,
				     unsigned output_buf_len, 
				     struct pjmedia_frame *output)
{
    struct ilbc_codec *ilbc_codec = (struct ilbc_codec*)codec;
    pj_int16_t *pcm_in;
    unsigned nsamples;

    pj_assert(ilbc_codec && input && output);

    pcm_in = (pj_int16_t*)input->buf;
    nsamples = input->size >> 1;

    PJ_ASSERT_RETURN(nsamples % ilbc_codec->enc_samples_per_frame == 0, 
		     PJMEDIA_CODEC_EPCMFRMINLEN);
    PJ_ASSERT_RETURN(output_buf_len >= ilbc_codec->enc_frame_size * nsamples /
		     ilbc_codec->enc_samples_per_frame,
		     PJMEDIA_CODEC_EFRMTOOSHORT);

    /* Detect silence */
    if (ilbc_codec->vad_enabled) {
	pj_bool_t is_silence;
	pj_int32_t silence_period;

	silence_period = pj_timestamp_diff32(&ilbc_codec->last_tx,
					      &input->timestamp);

	is_silence = pjmedia_silence_det_detect(ilbc_codec->vad, 
					        (const pj_int16_t*)input->buf,
						(input->size >> 1),
						NULL);
	if (is_silence &&
	    PJMEDIA_CODEC_MAX_SILENCE_PERIOD != -1 &&
	    silence_period < PJMEDIA_CODEC_MAX_SILENCE_PERIOD)
	{
	    output->type = PJMEDIA_FRAME_TYPE_NONE;
	    output->buf = NULL;
	    output->size = 0;
	    output->timestamp = input->timestamp;
	    return PJ_SUCCESS;
	} else {
	    ilbc_codec->last_tx = input->timestamp;
	}
    }

    /* Encode */
    output->size = 0;
    while (nsamples >= ilbc_codec->enc_samples_per_frame) {
	unsigned i;
	
	/* Convert to float */
	for (i=0; i<ilbc_codec->enc_samples_per_frame; ++i) {
	    ilbc_codec->enc_block[i] = (float) (*pcm_in++);
	}

	iLBC_encode((unsigned char *)output->buf + output->size, 
		    ilbc_codec->enc_block, 
		    &ilbc_codec->enc);

	output->size += ilbc_codec->enc.no_of_bytes;
	nsamples -= ilbc_codec->enc_samples_per_frame;
    }

    output->type = PJMEDIA_FRAME_TYPE_AUDIO;
    output->timestamp = input->timestamp;

    return PJ_SUCCESS;
}

/*
 * Decode frame.
 */
static pj_status_t ilbc_codec_decode(pjmedia_codec *codec, 
				     const struct pjmedia_frame *input,
				     unsigned output_buf_len, 
				     struct pjmedia_frame *output)
{
    struct ilbc_codec *ilbc_codec = (struct ilbc_codec*)codec;
    unsigned i;

    pj_assert(ilbc_codec != NULL);
    PJ_ASSERT_RETURN(input && output, PJ_EINVAL);

    if (output_buf_len < (ilbc_codec->dec_samples_per_frame << 1))
	return PJMEDIA_CODEC_EPCMTOOSHORT;

    if (input->size != ilbc_codec->dec_frame_size)
	return PJMEDIA_CODEC_EFRMINLEN;

    /* Decode to temporary buffer */
    iLBC_decode(ilbc_codec->dec_block, (unsigned char*) input->buf,
		&ilbc_codec->dec, 1);

    /* Convert decodec samples from float to short */
    for (i=0; i<ilbc_codec->dec_samples_per_frame; ++i) {
	((short*)output->buf)[i] = (short)ilbc_codec->dec_block[i];
    }
    output->size = (ilbc_codec->dec_samples_per_frame << 1);
    output->type = PJMEDIA_FRAME_TYPE_AUDIO;
    output->timestamp = input->timestamp;

    return PJ_SUCCESS;
}


/*
 * Recover lost frame.
 */
static pj_status_t  ilbc_codec_recover(pjmedia_codec *codec,
				      unsigned output_buf_len,
				      struct pjmedia_frame *output)
{
    struct ilbc_codec *ilbc_codec = (struct ilbc_codec*)codec;
    unsigned i;

    pj_assert(ilbc_codec != NULL);
    PJ_ASSERT_RETURN(output, PJ_EINVAL);

    if (output_buf_len < (ilbc_codec->dec_samples_per_frame << 1))
	return PJMEDIA_CODEC_EPCMTOOSHORT;

    /* Decode to temporary buffer */
    iLBC_decode(ilbc_codec->dec_block, NULL, &ilbc_codec->dec, 0);

    /* Convert decodec samples from float to short */
    for (i=0; i<ilbc_codec->dec_samples_per_frame; ++i) {
	((short*)output->buf)[i] = (short)ilbc_codec->dec_block[i];
    }
    output->size = (ilbc_codec->dec_samples_per_frame << 1);
    output->type = PJMEDIA_FRAME_TYPE_AUDIO;

    return PJ_SUCCESS;
}


#endif	/* PJMEDIA_HAS_ILBC_CODEC */

