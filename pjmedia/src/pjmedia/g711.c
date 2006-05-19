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
/* This file contains file from Sun Microsystems, Inc, with the complete 
 * notice in the second half of this file.
 */
#include <pjmedia/codec.h>
#include <pjmedia/endpoint.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pjmedia/plc.h>
#include <pjmedia/silencedet.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/assert.h>

#if defined(PJMEDIA_HAS_G711_CODEC) && PJMEDIA_HAS_G711_CODEC!=0


#define G711_BPS	    64000
#define G711_CODEC_CNT	    0	/* number of codec to preallocate in memory */
#define PTIME		    10	/* basic frame size is 10 msec	    */
#define FRAME_SIZE	    (8000 * PTIME / 1000)   /* 80 bytes	    */
#define SAMPLES_PER_FRAME   (8000 * PTIME / 1000)   /* 80 samples   */

/* These are the only public functions exported to applications */
PJ_DECL(pj_status_t) g711_init_factory (pjmedia_codec_factory *factory, 
					pj_pool_t *pool);

/* Algorithm prototypes. */
unsigned char linear2alaw(int		pcm_val);
int	      alaw2linear(unsigned char	a_val);
unsigned char linear2ulaw(int		pcm_val);
int	      ulaw2linear(unsigned char	u_val);

/* Prototypes for G711 factory */
static pj_status_t g711_test_alloc( pjmedia_codec_factory *factory, 
				    const pjmedia_codec_info *id );
static pj_status_t g711_default_attr( pjmedia_codec_factory *factory, 
				      const pjmedia_codec_info *id, 
				      pjmedia_codec_param *attr );
static pj_status_t g711_enum_codecs (pjmedia_codec_factory *factory, 
				     unsigned *count, 
				     pjmedia_codec_info codecs[]);
static pj_status_t g711_alloc_codec( pjmedia_codec_factory *factory, 
				     const pjmedia_codec_info *id, 
				     pjmedia_codec **p_codec);
static pj_status_t g711_dealloc_codec( pjmedia_codec_factory *factory, 
				       pjmedia_codec *codec );

/* Prototypes for G711 implementation. */
static pj_status_t  g711_init( pjmedia_codec *codec, 
			       pj_pool_t *pool );
static pj_status_t  g711_open( pjmedia_codec *codec, 
			       pjmedia_codec_param *attr );
static pj_status_t  g711_close( pjmedia_codec *codec );
static pj_status_t  g711_parse(pjmedia_codec *codec,
			       void *pkt,
			       pj_size_t pkt_size,
			       const pj_timestamp *timestamp,
			       unsigned *frame_cnt,
			       pjmedia_frame frames[]);
static pj_status_t  g711_encode( pjmedia_codec *codec, 
				 const struct pjmedia_frame *input,
				 unsigned output_buf_len, 
				 struct pjmedia_frame *output);
static pj_status_t  g711_decode( pjmedia_codec *codec, 
				 const struct pjmedia_frame *input,
				 unsigned output_buf_len, 
				 struct pjmedia_frame *output);
static pj_status_t  g711_recover( pjmedia_codec *codec,
				  unsigned output_buf_len,
				  struct pjmedia_frame *output);

/* Definition for G711 codec operations. */
static pjmedia_codec_op g711_op = 
{
    &g711_init,
    &g711_open,
    &g711_close,
    &g711_parse,
    &g711_encode,
    &g711_decode,
    &g711_recover
};

/* Definition for G711 codec factory operations. */
static pjmedia_codec_factory_op g711_factory_op =
{
    &g711_test_alloc,
    &g711_default_attr,
    &g711_enum_codecs,
    &g711_alloc_codec,
    &g711_dealloc_codec
};

/* G711 factory private data */
static struct g711_factory
{
    pjmedia_codec_factory	base;
    pjmedia_endpt	       *endpt;
    pj_pool_t		       *pool;
    pj_mutex_t		       *mutex;
    pjmedia_codec		codec_list;
} g711_factory;

/* G711 codec private data. */
struct g711_private
{
    unsigned		 pt;
    pj_bool_t		 plc_enabled;
    pjmedia_plc		*plc;
    pj_bool_t		 vad_enabled;
    pjmedia_silence_det *vad;
};


PJ_DEF(pj_status_t) pjmedia_codec_g711_init(pjmedia_endpt *endpt)
{
    pjmedia_codec_mgr *codec_mgr;
    pj_status_t status;

    if (g711_factory.endpt != NULL) {
	/* Already initialized. */
	return PJ_SUCCESS;
    }

    /* Init factory */
    g711_factory.base.op = &g711_factory_op;
    g711_factory.base.factory_data = NULL;
    g711_factory.endpt = endpt;

    pj_list_init(&g711_factory.codec_list);

    /* Create pool */
    g711_factory.pool = pjmedia_endpt_create_pool(endpt, "g711", 4000, 4000);
    if (!g711_factory.pool)
	return PJ_ENOMEM;

    /* Create mutex. */
    status = pj_mutex_create_simple(g711_factory.pool, "g611", 
				    &g711_factory.mutex);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Get the codec manager. */
    codec_mgr = pjmedia_endpt_get_codec_mgr(endpt);
    if (!codec_mgr) {
	return PJ_EINVALIDOP;
    }

    /* Register codec factory to endpoint. */
    status = pjmedia_codec_mgr_register_factory(codec_mgr, 
						&g711_factory.base);
    if (status != PJ_SUCCESS)
	return status;


    return PJ_SUCCESS;

on_error:
    if (g711_factory.mutex) {
	pj_mutex_destroy(g711_factory.mutex);
	g711_factory.mutex = NULL;
    }
    if (g711_factory.pool) {
	pj_pool_release(g711_factory.pool);
	g711_factory.pool = NULL;
    }
    return status;
}

PJ_DEF(pj_status_t) pjmedia_codec_g711_deinit(void)
{
    pjmedia_codec_mgr *codec_mgr;
    pj_status_t status;

    if (g711_factory.endpt == NULL) {
	/* Not registered. */
	return PJ_SUCCESS;
    }

    /* Lock mutex. */
    pj_mutex_lock(g711_factory.mutex);

    /* Get the codec manager. */
    codec_mgr = pjmedia_endpt_get_codec_mgr(g711_factory.endpt);
    if (!codec_mgr) {
	g711_factory.endpt = NULL;
	pj_mutex_unlock(g711_factory.mutex);
	return PJ_EINVALIDOP;
    }

    /* Unregister G711 codec factory. */
    status = pjmedia_codec_mgr_unregister_factory(codec_mgr,
						  &g711_factory.base);
    g711_factory.endpt = NULL;

    /* Destroy mutex. */
    pj_mutex_destroy(g711_factory.mutex);
    g711_factory.mutex = NULL;


    /* Release pool. */
    pj_pool_release(g711_factory.pool);
    g711_factory.pool = NULL;


    return status;
}

static pj_status_t g711_test_alloc(pjmedia_codec_factory *factory, 
				   const pjmedia_codec_info *id )
{
    PJ_UNUSED_ARG(factory);

    /* It's sufficient to check payload type only. */
    return (id->pt==PJMEDIA_RTP_PT_PCMU || id->pt==PJMEDIA_RTP_PT_PCMA)? 0:-1;
}

static pj_status_t g711_default_attr (pjmedia_codec_factory *factory, 
				      const pjmedia_codec_info *id, 
				      pjmedia_codec_param *attr )
{
    PJ_UNUSED_ARG(factory);

    pj_memset(attr, 0, sizeof(pjmedia_codec_param));
    attr->info.clock_rate = 8000;
    attr->info.channel_cnt = 1;
    attr->info.avg_bps = G711_BPS;
    attr->info.pcm_bits_per_sample = 16;
    attr->info.frm_ptime = PTIME;
    attr->info.pt = (pj_uint8_t)id->pt;

    /* Set default frames per packet to 2 (or 20ms) */
    attr->setting.frm_per_pkt = 2;

    /* Enable plc by default. */
    attr->setting.plc = 1;

    /* Enable VAD by default. */
    attr->setting.vad = 1;

    /* Default all other flag bits disabled. */

    return PJ_SUCCESS;
}

static pj_status_t g711_enum_codecs(pjmedia_codec_factory *factory, 
				    unsigned *max_count, 
				    pjmedia_codec_info codecs[])
{
    unsigned count = 0;

    PJ_UNUSED_ARG(factory);

    if (count < *max_count) {
	codecs[count].type = PJMEDIA_TYPE_AUDIO;
	codecs[count].pt = PJMEDIA_RTP_PT_PCMU;
	codecs[count].encoding_name = pj_str("PCMU");
	codecs[count].clock_rate = 8000;
	codecs[count].channel_cnt = 1;
	++count;
    }
    if (count < *max_count) {
	codecs[count].type = PJMEDIA_TYPE_AUDIO;
	codecs[count].pt = PJMEDIA_RTP_PT_PCMA;
	codecs[count].encoding_name = pj_str("PCMA");
	codecs[count].clock_rate = 8000;
	codecs[count].channel_cnt = 1;
	++count;
    }

    *max_count = count;

    return PJ_SUCCESS;
}

static pj_status_t g711_alloc_codec( pjmedia_codec_factory *factory, 
				     const pjmedia_codec_info *id,
				     pjmedia_codec **p_codec)
{
    pjmedia_codec *codec = NULL;
    pj_status_t status;

    PJ_ASSERT_RETURN(factory==&g711_factory.base, PJ_EINVAL);

    /* Lock mutex. */
    pj_mutex_lock(g711_factory.mutex);

    /* Allocate new codec if no more is available */
    if (pj_list_empty(&g711_factory.codec_list)) {
	struct g711_private *codec_priv;

	codec = pj_pool_alloc(g711_factory.pool, sizeof(pjmedia_codec));
	codec_priv = pj_pool_zalloc(g711_factory.pool, 
				    sizeof(struct g711_private));
	if (!codec || !codec_priv) {
	    pj_mutex_unlock(g711_factory.mutex);
	    return PJ_ENOMEM;
	}

	/* Set the payload type */
	codec_priv->pt = id->pt;

	/* Create PLC, always with 10ms ptime */
	status = pjmedia_plc_create(g711_factory.pool, 8000, 80,
				    0, &codec_priv->plc);
	if (status != PJ_SUCCESS) {
	    pj_mutex_unlock(g711_factory.mutex);
	    return status;
	}

	/* Create VAD */
	status = pjmedia_silence_det_create(g711_factory.pool,
					    8000, 80,
					    &codec_priv->vad);
	if (status != PJ_SUCCESS) {
	    pj_mutex_unlock(g711_factory.mutex);
	    return status;
	}

	codec->factory = factory;
	codec->op = &g711_op;
	codec->codec_data = codec_priv;
    } else {
	codec = g711_factory.codec_list.next;
	pj_list_erase(codec);
    }

    /* Zero the list, for error detection in g711_dealloc_codec */
    codec->next = codec->prev = NULL;

    *p_codec = codec;

    /* Unlock mutex. */
    pj_mutex_unlock(g711_factory.mutex);

    return PJ_SUCCESS;
}

static pj_status_t g711_dealloc_codec(pjmedia_codec_factory *factory, 
				      pjmedia_codec *codec )
{
    
    PJ_ASSERT_RETURN(factory==&g711_factory.base, PJ_EINVAL);

    /* Check that this node has not been deallocated before */
    pj_assert (codec->next==NULL && codec->prev==NULL);
    if (codec->next!=NULL || codec->prev!=NULL) {
	return PJ_EINVALIDOP;
    }

    /* Lock mutex. */
    pj_mutex_lock(g711_factory.mutex);

    /* Insert at the back of the list */
    pj_list_insert_before(&g711_factory.codec_list, codec);

    /* Unlock mutex. */
    pj_mutex_unlock(g711_factory.mutex);

    return PJ_SUCCESS;
}

static pj_status_t g711_init( pjmedia_codec *codec, pj_pool_t *pool )
{
    /* There's nothing to do here really */
    PJ_UNUSED_ARG(codec);
    PJ_UNUSED_ARG(pool);

    return PJ_SUCCESS;
}

static pj_status_t g711_open(pjmedia_codec *codec, 
			     pjmedia_codec_param *attr )
{
    struct g711_private *priv = codec->codec_data;
    priv->pt = attr->info.pt;
    priv->plc_enabled = (attr->setting.plc != 0);
    priv->vad_enabled = (attr->setting.vad != 0);
    return PJ_SUCCESS;
}

static pj_status_t g711_close( pjmedia_codec *codec )
{
    PJ_UNUSED_ARG(codec);
    /* Nothing to do */
    return PJ_SUCCESS;
}

static pj_status_t  g711_parse( pjmedia_codec *codec,
				void *pkt,
				pj_size_t pkt_size,
				const pj_timestamp *ts,
				unsigned *frame_cnt,
				pjmedia_frame frames[])
{
    unsigned count = 0;

    PJ_UNUSED_ARG(codec);

    PJ_ASSERT_RETURN(ts && frame_cnt && frames, PJ_EINVAL);

    while (pkt_size >= FRAME_SIZE && count < *frame_cnt) {
	frames[count].type = PJMEDIA_FRAME_TYPE_AUDIO;
	frames[count].buf = pkt;
	frames[count].size = FRAME_SIZE;
	frames[count].timestamp.u64 = ts->u64 + SAMPLES_PER_FRAME * count;

	pkt = ((char*)pkt) + FRAME_SIZE;
	pkt_size -= FRAME_SIZE;

	++count;
    }

    *frame_cnt = count;
    return PJ_SUCCESS;
}

static pj_status_t  g711_encode(pjmedia_codec *codec, 
				const struct pjmedia_frame *input,
				unsigned output_buf_len, 
				struct pjmedia_frame *output)
{
    pj_int16_t *samples = (pj_int16_t*) input->buf;
    struct g711_private *priv = codec->codec_data;

    /* Check output buffer length */
    if (output_buf_len < input->size / 2)
	return PJMEDIA_CODEC_EFRMTOOSHORT;

    /* Detect silence if VAD is enabled */
    if (priv->vad_enabled) {
	pj_bool_t is_silence;

	is_silence = pjmedia_silence_det_detect(priv->vad, input->buf, 
						input->size / 2, NULL);
	if (is_silence) {
	    output->type = PJMEDIA_FRAME_TYPE_NONE;
	    output->buf = NULL;
	    output->size = 0;
	    output->timestamp.u64 = input->timestamp.u64;
	    return PJ_SUCCESS;
	}
    }

    /* Encode */
    if (priv->pt == PJMEDIA_RTP_PT_PCMA) {
	unsigned i;
	pj_uint8_t *dst = output->buf;

	for (i=0; i!=input->size/2; ++i, ++dst) {
	    *dst = linear2alaw(samples[i]);
	}
    } else if (priv->pt == PJMEDIA_RTP_PT_PCMU) {
	unsigned i;
	pj_uint8_t *dst = output->buf;

	for (i=0; i!=input->size/2; ++i, ++dst) {
	    *dst = linear2ulaw(samples[i]);
	}

    } else {
	return PJMEDIA_EINVALIDPT;
    }

    output->type = PJMEDIA_FRAME_TYPE_AUDIO;
    output->size = input->size / 2;

    return PJ_SUCCESS;
}

static pj_status_t  g711_decode(pjmedia_codec *codec, 
				const struct pjmedia_frame *input,
				unsigned output_buf_len, 
				struct pjmedia_frame *output)
{
    struct g711_private *priv = codec->codec_data;

    /* Check output buffer length */
    PJ_ASSERT_RETURN(output_buf_len >= input->size * 2,
		     PJMEDIA_CODEC_EPCMTOOSHORT);

    /* Input buffer MUST have exactly 80 bytes long */
    PJ_ASSERT_RETURN(input->size == FRAME_SIZE, 
		     PJMEDIA_CODEC_EFRMINLEN);

    /* Decode */
    if (priv->pt == PJMEDIA_RTP_PT_PCMA) {
	unsigned i;
	pj_uint8_t *src = input->buf;
	pj_uint16_t *dst = output->buf;

	for (i=0; i!=input->size; ++i) {
	    *dst++ = (pj_uint16_t) alaw2linear(*src++);
	}
    } else if (priv->pt == PJMEDIA_RTP_PT_PCMU) {
	unsigned i;
	pj_uint8_t *src = input->buf;
	pj_uint16_t *dst = output->buf;

	for (i=0; i!=input->size; ++i) {
	    *dst++ = (pj_uint16_t) ulaw2linear(*src++);
	}

    } else {
	return PJMEDIA_EINVALIDPT;
    }

    output->type = PJMEDIA_FRAME_TYPE_AUDIO;
    output->size = input->size * 2;

    if (priv->plc_enabled)
	pjmedia_plc_save( priv->plc, output->buf);

    return PJ_SUCCESS;
}

static pj_status_t  g711_recover( pjmedia_codec *codec,
				  unsigned output_buf_len,
				  struct pjmedia_frame *output)
{
    struct g711_private *priv = codec->codec_data;

    if (!priv->plc_enabled)
	return PJ_EINVALIDOP;

    PJ_ASSERT_RETURN(output_buf_len >= SAMPLES_PER_FRAME * 2, 
		     PJMEDIA_CODEC_EPCMTOOSHORT);

    pjmedia_plc_generate(priv->plc, output->buf);
    output->size = SAMPLES_PER_FRAME * 2;

    return PJ_SUCCESS;
}

#endif	/* PJMEDIA_HAS_G711_CODEC */


/*
 * This source code is a product of Sun Microsystems, Inc. and is provided
 * for unrestricted use.  Users may copy or modify this source code without
 * charge.
 *
 * SUN SOURCE CODE IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING
 * THE WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun source code is provided with no support and without any obligation on
 * the part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY THIS SOFTWARE
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */



#ifdef _MSC_VER
#  pragma warning ( disable: 4244 ) /* Conversion from int to char etc */
#endif

/*
 * g711.c
 *
 * u-law, A-law and linear PCM conversions.
 */
#define	SIGN_BIT	(0x80)		/* Sign bit for a A-law byte. */
#define	QUANT_MASK	(0xf)		/* Quantization field mask. */
#define	NSEGS		(8)		/* Number of A-law segments. */
#define	SEG_SHIFT	(4)		/* Left shift for segment number. */
#define	SEG_MASK	(0x70)		/* Segment field mask. */

static short seg_end[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF,
			    0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};

/* copy from CCITT G.711 specifications */
static unsigned char _u2a[128] = {		/* u- to A-law conversions */
	1,	1,	2,	2,	3,	3,	4,	4,
	5,	5,	6,	6,	7,	7,	8,	8,
	9,	10,	11,	12,	13,	14,	15,	16,
	17,	18,	19,	20,	21,	22,	23,	24,
	25,	27,	29,	31,	33,	34,	35,	36,
	37,	38,	39,	40,	41,	42,	43,	44,
	46,	48,	49,	50,	51,	52,	53,	54,
	55,	56,	57,	58,	59,	60,	61,	62,
	64,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	81,	82,	83,	84,	85,	86,	87,	88,
	89,	90,	91,	92,	93,	94,	95,	96,
	97,	98,	99,	100,	101,	102,	103,	104,
	105,	106,	107,	108,	109,	110,	111,	112,
	113,	114,	115,	116,	117,	118,	119,	120,
	121,	122,	123,	124,	125,	126,	127,	128};

static unsigned char _a2u[128] = {		/* A- to u-law conversions */
	1,	3,	5,	7,	9,	11,	13,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31,
	32,	32,	33,	33,	34,	34,	35,	35,
	36,	37,	38,	39,	40,	41,	42,	43,
	44,	45,	46,	47,	48,	48,	49,	49,
	50,	51,	52,	53,	54,	55,	56,	57,
	58,	59,	60,	61,	62,	63,	64,	64,
	65,	66,	67,	68,	69,	70,	71,	72,
	73,	74,	75,	76,	77,	78,	79,	79,
	80,	81,	82,	83,	84,	85,	86,	87,
	88,	89,	90,	91,	92,	93,	94,	95,
	96,	97,	98,	99,	100,	101,	102,	103,
	104,	105,	106,	107,	108,	109,	110,	111,
	112,	113,	114,	115,	116,	117,	118,	119,
	120,	121,	122,	123,	124,	125,	126,	127};

static int
search(
	int		val,
	short		*table,
	int		size)
{
	int		i;

	for (i = 0; i < size; i++) {
		if (val <= *table++)
			return (i);
	}
	return (size);
}

/*
 * linear2alaw() - Convert a 16-bit linear PCM value to 8-bit A-law
 *
 * linear2alaw() accepts an 16-bit integer and encodes it as A-law data.
 *
 *		Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	0000000wxyza			000wxyz
 *	0000001wxyza			001wxyz
 *	000001wxyzab			010wxyz
 *	00001wxyzabc			011wxyz
 *	0001wxyzabcd			100wxyz
 *	001wxyzabcde			101wxyz
 *	01wxyzabcdef			110wxyz
 *	1wxyzabcdefg			111wxyz
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
unsigned char
linear2alaw(
	int		pcm_val)	/* 2's complement (16-bit range) */
{
	int		mask;
	int		seg;
	unsigned char	aval;

	if (pcm_val >= 0) {
		mask = 0xD5;		/* sign (7th) bit = 1 */
	} else {
		mask = 0x55;		/* sign bit = 0 */
		pcm_val = -pcm_val - 8;
	}

	/* Convert the scaled magnitude to segment number. */
	seg = search(pcm_val, seg_end, 8);

	/* Combine the sign, segment, and quantization bits. */

	if (seg >= 8)		/* out of range, return maximum value. */
		return (0x7F ^ mask);
	else {
		aval = seg << SEG_SHIFT;
		if (seg < 2)
			aval |= (pcm_val >> 4) & QUANT_MASK;
		else
			aval |= (pcm_val >> (seg + 3)) & QUANT_MASK;
		return (aval ^ mask);
	}
}

/*
 * alaw2linear() - Convert an A-law value to 16-bit linear PCM
 *
 */
int
alaw2linear(
	unsigned char	a_val)
{
	int		t;
	int		seg;

	a_val ^= 0x55;

	t = (a_val & QUANT_MASK) << 4;
	seg = ((unsigned)a_val & SEG_MASK) >> SEG_SHIFT;
	switch (seg) {
	case 0:
		t += 8;
		break;
	case 1:
		t += 0x108;
		break;
	default:
		t += 0x108;
		t <<= seg - 1;
	}
	return ((a_val & SIGN_BIT) ? t : -t);
}

#define	BIAS		(0x84)		/* Bias for linear code. */

/*
 * linear2ulaw() - Convert a linear PCM value to u-law
 *
 * In order to simplify the encoding process, the original linear magnitude
 * is biased by adding 33 which shifts the encoding range from (0 - 8158) to
 * (33 - 8191). The result can be seen in the following encoding table:
 *
 *	Biased Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	00000001wxyza			000wxyz
 *	0000001wxyzab			001wxyz
 *	000001wxyzabc			010wxyz
 *	00001wxyzabcd			011wxyz
 *	0001wxyzabcde			100wxyz
 *	001wxyzabcdef			101wxyz
 *	01wxyzabcdefg			110wxyz
 *	1wxyzabcdefgh			111wxyz
 *
 * Each biased linear code has a leading 1 which identifies the segment
 * number. The value of the segment number is equal to 7 minus the number
 * of leading 0's. The quantization interval is directly available as the
 * four bits wxyz.  * The trailing bits (a - h) are ignored.
 *
 * Ordinarily the complement of the resulting code word is used for
 * transmission, and so the code word is complemented before it is returned.
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */
unsigned char
linear2ulaw(
	int		pcm_val)	/* 2's complement (16-bit range) */
{
	int		mask;
	int		seg;
	unsigned char	uval;

	/* Get the sign and the magnitude of the value. */
	if (pcm_val < 0) {
		pcm_val = BIAS - pcm_val;
		mask = 0x7F;
	} else {
		pcm_val += BIAS;
		mask = 0xFF;
	}

	/* Convert the scaled magnitude to segment number. */
	seg = search(pcm_val, seg_end, 8);

	/*
	 * Combine the sign, segment, quantization bits;
	 * and complement the code word.
	 */
	if (seg >= 8)		/* out of range, return maximum value. */
		return (0x7F ^ mask);
	else {
		uval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0xF);
		return (uval ^ mask);
	}

}

/*
 * ulaw2linear() - Convert a u-law value to 16-bit linear PCM
 *
 * First, a biased linear code is derived from the code word. An unbiased
 * output can then be obtained by subtracting 33 from the biased code.
 *
 * Note that this function expects to be passed the complement of the
 * original code word. This is in keeping with ISDN conventions.
 */
int
ulaw2linear(
	unsigned char	u_val)
{
	int		t;

	/* Complement to obtain normal u-law value. */
	u_val = ~u_val;

	/*
	 * Extract and bias the quantization bits. Then
	 * shift up by the segment number and subtract out the bias.
	 */
	t = ((u_val & QUANT_MASK) << 3) + BIAS;
	t <<= ((unsigned)u_val & SEG_MASK) >> SEG_SHIFT;

	return ((u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS));
}

/* A-law to u-law conversion */
unsigned char
alaw2ulaw(
	unsigned char	aval)
{
	aval &= 0xff;
	return ((aval & 0x80) ? (0xFF ^ _a2u[aval ^ 0xD5]) :
	    (0x7F ^ _a2u[aval ^ 0x55]));
}

/* u-law to A-law conversion */
unsigned char
ulaw2alaw(
	unsigned char	uval)
{
	uval &= 0xff;
	return ((uval & 0x80) ? (0xD5 ^ (_u2a[0xFF ^ uval] - 1)) :
	    (0x55 ^ (_u2a[0x7F ^ uval] - 1)));
}




