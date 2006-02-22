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
#include <pjmedia/stream.h>
#include <pjmedia/errno.h>
#include <pjmedia/rtp.h>
#include <pjmedia/rtcp.h>
#include <pjmedia/jbuf.h>
#include <pj/os.h>
#include <pj/ctype.h>
#include <pj/log.h>
#include <pj/string.h>	    /* memcpy() */
#include <pj/pool.h>
#include <pj/assert.h>
#include <pj/compat/socket.h>
#include <pj/sock_select.h>
#include <pj/errno.h>
#include <stdlib.h>


#define THIS_FILE			"stream.c"
#define ERRLEVEL			1
#define TRACE_(expr)			stream_perror expr

#define PJMEDIA_MAX_FRAME_DURATION_MS   200
#define PJMEDIA_MAX_BUFFER_SIZE_MS	2000
#define PJMEDIA_MAX_MTU			1500
#define PJMEDIA_DTMF_DURATION		1600	/* in timestamp */


/**
 * Media channel.
 */
struct pjmedia_channel
{
    pjmedia_stream	   *stream;	    /**< Parent stream.		    */
    pjmedia_dir		    dir;	    /**< Channel direction.	    */
    unsigned		    pt;		    /**< Payload type.		    */
    pj_bool_t		    paused;	    /**< Paused?.		    */
    pj_snd_stream_info	    snd_info;	    /**< Sound stream param.	    */
    //pj_snd_stream	   *snd_stream;	    /**< Sound stream.		    */
    unsigned		    in_pkt_size;    /**< Size of input buffer.	    */
    void		   *in_pkt;	    /**< Input buffer.		    */
    unsigned		    out_pkt_size;   /**< Size of output buffer.	    */
    void		   *out_pkt;	    /**< Output buffer.		    */
    unsigned		    pcm_buf_size;   /**< Size of PCM buffer.	    */
    void		   *pcm_buf;	    /**< PCM buffer.		    */
    pjmedia_rtp_session	    rtp;	    /**< RTP session.		    */
};


struct dtmf
{
    int		    event;
    pj_uint32_t	    start_ts;
    pj_uint32_t	    end_ts;
};

/**
 * This structure describes media stream.
 * A media stream is bidirectional media transmission between two endpoints.
 * It consists of two channels, i.e. encoding and decoding channels.
 * A media stream corresponds to a single "m=" line in a SDP session
 * description.
 */
struct pjmedia_stream
{
    pjmedia_port	     port;	    /**< Port interface.	    */
    pjmedia_channel	    *enc;	    /**< Encoding channel.	    */
    pjmedia_channel	    *dec;	    /**< Decoding channel.	    */

    pjmedia_dir		     dir;	    /**< Stream direction.	    */
    pjmedia_stream_stat	     stat;	    /**< Stream statistics.	    */

    pjmedia_codec_mgr	    *codec_mgr;	    /**< Codec manager instance.    */
    pjmedia_codec	    *codec;	    /**< Codec instance being used. */
    pj_size_t		     frame_size;    /**< Size of encoded frame.	    */
    pj_mutex_t		    *jb_mutex;
    pjmedia_jbuf	    *jb;	    /**< Jitter buffer.		    */

    pjmedia_sock_info	     skinfo;	    /**< Transport info.	    */
    pj_sockaddr_in	     rem_rtp_addr;  /**< Remote RTP address.	    */
    pj_sockaddr_in	     rem_rtcp_addr; /**< Remote RTCP address.	    */

    pj_rtcp_session	     rtcp;	    /**< RTCP for incoming RTP.	    */

    pj_bool_t		     quit_flag;	    /**< To signal thread exit.	    */
    pj_thread_t		    *thread;	    /**< Jitter buffer's thread.    */

    /* RFC 2833 DTMF transmission queue: */
    int			     dtmf_count;    /**< # of digits in queue.	    */
    struct dtmf		     dtmf_queue[32];/**< Outgoing dtmf queue.	    */
};


/*
 * Print error.
 */
static void stream_perror(const char *sender, const char *title,
			  pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(3,(sender, "%s: %s [err:%d]", title, errmsg, status));
}


/*
 * play_callback()
 *
 * This callback is called by sound device's player thread when it
 * needs to feed the player with some frames.
 */
static pj_status_t get_frame( pjmedia_port *port, pjmedia_frame *frame)
{
    pjmedia_stream *stream = port->user_data;
    pjmedia_channel *channel = stream->dec;
    
    char frame_type;
    pj_status_t status;
    struct pjmedia_frame frame_in, frame_out;

    /* Do nothing if we're quitting. */
    if (stream->quit_flag) {
	frame->type = PJMEDIA_FRAME_TYPE_NONE;
	return PJ_SUCCESS;
    }

    /* Lock jitter buffer mutex */
    pj_mutex_lock( stream->jb_mutex );

    /* Get frame from jitter buffer. */
    status = pjmedia_jbuf_get_frame(stream->jb, channel->out_pkt,
				    &frame_type);

    /* Unlock jitter buffer mutex. */
    pj_mutex_unlock( stream->jb_mutex );

    if (status != PJ_SUCCESS || frame_type == PJMEDIA_JB_ZERO_FRAME ||
	frame_type == PJMEDIA_JB_MISSING_FRAME) 
    {
	frame->type = PJMEDIA_FRAME_TYPE_NONE;
	return PJ_SUCCESS;
    }


    /* Decode */
    frame_in.buf = channel->out_pkt;
    frame_in.size = stream->frame_size;
    frame_in.type = PJMEDIA_FRAME_TYPE_AUDIO;  /* ignored */
    frame_out.buf = channel->pcm_buf;
    status = stream->codec->op->decode( stream->codec, &frame_in,
					channel->pcm_buf_size, &frame_out);
    if (status != 0) {
	TRACE_((THIS_FILE, "codec decode() error", status));

	frame->type = PJMEDIA_FRAME_TYPE_NONE;
	return PJ_SUCCESS;
    }

    /* Put in sound buffer. */
    if (frame_out.size > frame->size) {
	PJ_LOG(4,(THIS_FILE, "Sound playout buffer truncated %d bytes", 
		  frame_out.size - frame->size));
	frame_out.size = frame->size;
    }

    frame->type = PJMEDIA_FRAME_TYPE_AUDIO;
    frame->size = frame_out.size;
    frame->timestamp.u64 = 0;
    pj_memcpy(frame->buf, frame_out.buf, frame_out.size);

    return PJ_SUCCESS;
}


/*
 * Transmit DTMF
 */
static void transmit_dtmf(pjmedia_stream *stream, 
			  struct pjmedia_frame *frame_out)
{
    pjmedia_rtp_dtmf_event *event;
    struct dtmf *digit = &stream->dtmf_queue[0];
    pj_uint32_t cur_ts;

    event = frame_out->buf;
    cur_ts = pj_ntohl(stream->enc->rtp.out_hdr.ts);

    event->event = (pj_uint8_t)digit->event;
    event->e_vol = 10;
    event->duration = pj_htonl(cur_ts - digit->start_ts);
}

/**
 * rec_callback()
 *
 * This callback is called when the mic device has gathered
 * enough audio samples. We will encode the audio samples and
 * send it to remote.
 */
static pj_status_t put_frame( pjmedia_port *port, 
			      const pjmedia_frame *frame )
{
    pjmedia_stream *stream = port->user_data;
    pjmedia_channel *channel = stream->enc;
    pj_status_t status = 0;
    struct pjmedia_frame frame_out;
    int ts_len;
    void *rtphdr;
    int rtphdrlen;
    pj_ssize_t sent;


    /* Check if stream is quitting. */
    if (stream->quit_flag)
	return -1;

    /* Number of samples in the frame */
    ts_len = frame->size / (channel->snd_info.bits_per_sample / 8);

    /* Init frame_out buffer. */
    frame_out.buf = ((char*)channel->out_pkt) + sizeof(pjmedia_rtp_hdr);

    /* If we have DTMF digits in the queue, transmit the digits. 
     * Otherwise encode the PCM buffer.
     */
    if (stream->dtmf_count) {
	transmit_dtmf(stream, &frame_out);
    } else {
	unsigned max_size;

	max_size = channel->out_pkt_size - sizeof(pjmedia_rtp_hdr);
	status = stream->codec->op->encode( stream->codec, frame, 
					    max_size, 
					    &frame_out);
	if (status != 0) {
	    TRACE_((THIS_FILE, "Codec encode() error", status));
	    return status;
	}
    }

    /* Encapsulate. */
    status = pjmedia_rtp_encode_rtp( &channel->rtp, 
				channel->pt, 0, 
				frame_out.size, ts_len, 
				(const void**)&rtphdr, &rtphdrlen);
    if (status != 0) {
	TRACE_((THIS_FILE, "RTP encode_rtp() error", status));
	return status;
    }

    if (rtphdrlen != sizeof(pjmedia_rtp_hdr)) {
	/* We don't support RTP with extended header yet. */
	PJ_TODO(SUPPORT_SENDING_RTP_WITH_EXTENDED_HEADER);
	//TRACE_((THIS_FILE, "Unsupported extended RTP header for transmission"));
	return 0;
    }

    pj_memcpy(channel->out_pkt, rtphdr, sizeof(pjmedia_rtp_hdr));

    /* Send. */
    sent = frame_out.size+sizeof(pjmedia_rtp_hdr);
    status = pj_sock_sendto(stream->skinfo.rtp_sock, channel->out_pkt, &sent, 0, 
			    &stream->rem_rtp_addr, sizeof(stream->rem_rtp_addr));
    if (status != PJ_SUCCESS)
	return status;

    /* Update stat */
    stream->stat.enc.pkt++;
    stream->stat.enc.bytes += frame_out.size+sizeof(pjmedia_rtp_hdr);

    return PJ_SUCCESS;
}


static void dump_bin(const char *buf, unsigned len)
{
    unsigned i;

    PJ_LOG(3,(THIS_FILE, "begin dump"));
    for (i=0; i<len; ++i) {
	int j;
	char bits[9];
	unsigned val = buf[i] & 0xFF;

	bits[8] = '\0';
	for (j=0; j<8; ++j) {
	    if (val & (1 << (7-j)))
		bits[j] = '1';
	    else
		bits[j] = '0';
	}

	PJ_LOG(3,(THIS_FILE, "%2d %s [%d]", i, bits, val));
    }
    PJ_LOG(3,(THIS_FILE, "end dump"));
}

/*
 * This thread will poll the socket for incoming packets, and put
 * the packets to jitter buffer.
 */
static int PJ_THREAD_FUNC jitter_buffer_thread (void*arg)
{
    pjmedia_stream *stream = arg;
    pjmedia_channel *channel = stream->dec;


    while (!stream->quit_flag) {
	pj_ssize_t len;
	const pjmedia_rtp_hdr *hdr;
	const void *payload;
	unsigned payloadlen;
	int status;

	/* Wait for packet. */
	pj_fd_set_t fds;
	pj_time_val timeout;

	PJ_FD_ZERO (&fds);
	PJ_FD_SET (stream->skinfo.rtp_sock, &fds);
	timeout.sec = 0;
	timeout.msec = 1;

	/* Wait with timeout. */
	status = pj_sock_select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
	if (status < 0) {
	    TRACE_((THIS_FILE, "Jitter buffer select() error", 
		    pj_get_netos_error()));
	    pj_thread_sleep(500);
	    continue;
	} else if (status == 0)
	    continue;

	/* Get packet from socket. */
	len = channel->in_pkt_size;
	status = pj_sock_recv(stream->skinfo.rtp_sock, 
			      channel->in_pkt, &len, 0);
	if (len < 1 || status != PJ_SUCCESS) {
	    if (pj_get_netos_error() == PJ_STATUS_FROM_OS(OSERR_ECONNRESET)) {
		/* On Win2K SP2 (or above) and WinXP, recv() will get 
		 * WSAECONNRESET when the sending side receives ICMP port 
		 * unreachable.
		 */
		continue;
	    }
	    pj_thread_sleep(1);
	    continue;
	}

	if (channel->paused)
	    continue;

	/* Update RTP and RTCP session. */
	status = pjmedia_rtp_decode_rtp(&channel->rtp, channel->in_pkt, len, 
				   &hdr, &payload, &payloadlen);
	if (status != PJ_SUCCESS) {
	    TRACE_((THIS_FILE, "RTP decode error", status));
	    continue;
	}

#if 1
	if (hdr->pt == 101) {
	    dump_bin((char*)payload, payloadlen);
	    continue;
	}
#endif

	status = pjmedia_rtp_session_update(&channel->rtp, hdr);
	if (status != 0 && 
	    status != PJMEDIA_RTP_ESESSPROBATION && 
	    status != PJMEDIA_RTP_ESESSRESTART) 
	{
	    TRACE_((THIS_FILE, "RTP session_update error", status));
	    continue;
	}
	pj_rtcp_rx_rtp(&stream->rtcp, pj_ntohs(hdr->seq), pj_ntohl(hdr->ts));

	/* Update stat */
	stream->stat.dec.pkt++;
	stream->stat.dec.bytes += len;

	/* Put to jitter buffer. */
	pj_mutex_lock( stream->jb_mutex );
	status = pjmedia_jbuf_put_frame(stream->jb, payload, payloadlen, pj_ntohs(hdr->seq));
	pj_mutex_unlock( stream->jb_mutex );

	if (status != 0) {
	    TRACE_((THIS_FILE, "Jitter buffer put() error", status));
	    continue;
	}
    }

    return 0;
}


/*
 * Create sound stream parameter from codec attributes.
 */
static void init_snd_param( pj_snd_stream_info *snd_param,
			    const pjmedia_codec_param *codec_param)
{
    pj_memset(snd_param, 0, sizeof(*snd_param));

    snd_param->bits_per_sample	 = codec_param->pcm_bits_per_sample;
    snd_param->bytes_per_frame   = 2;
    snd_param->frames_per_packet = codec_param->sample_rate * 
				   codec_param->ptime / 
				   1000;
    snd_param->samples_per_frame = 1;
    snd_param->samples_per_sec   = codec_param->sample_rate;
}


/*
 * Create media channel.
 */
static pj_status_t create_channel( pj_pool_t *pool,
				   pjmedia_stream *stream,
				   pjmedia_dir dir,
				   const pjmedia_stream_info *param,
				   const pjmedia_codec_param *codec_param,
				   pjmedia_channel **p_channel)
{
    pjmedia_channel *channel;
    pj_status_t status;
    
    /* Allocate memory for channel descriptor */

    channel = pj_pool_zalloc(pool, sizeof(pjmedia_channel));
    PJ_ASSERT_RETURN(channel != NULL, PJ_ENOMEM);

    /* Init channel info. */

    channel->stream = stream;
    channel->dir = dir;
    channel->paused = 1;
    channel->pt = param->fmt.pt;

    /* Allocate buffer for incoming packet. */

    channel->in_pkt_size = PJMEDIA_MAX_MTU;
    channel->in_pkt = pj_pool_alloc( pool, channel->in_pkt_size );
    PJ_ASSERT_RETURN(channel->in_pkt != NULL, PJ_ENOMEM);

    
    /* Allocate buffer for outgoing packet. */

    channel->out_pkt_size = sizeof(pjmedia_rtp_hdr) + 
			    codec_param->avg_bps/8 * 
			    PJMEDIA_MAX_FRAME_DURATION_MS / 
			    1000;

    if (channel->out_pkt_size > PJMEDIA_MAX_MTU)
	channel->out_pkt_size = PJMEDIA_MAX_MTU;

    channel->out_pkt = pj_pool_alloc(pool, channel->out_pkt_size);
    PJ_ASSERT_RETURN(channel->out_pkt != NULL, PJ_ENOMEM);


    /* Allocate buffer for decoding to PCM: */

    channel->pcm_buf_size = codec_param->sample_rate * 
			    codec_param->pcm_bits_per_sample / 8 *
			    PJMEDIA_MAX_FRAME_DURATION_MS / 1000;
    channel->pcm_buf = pj_pool_alloc (pool, channel->pcm_buf_size);
    PJ_ASSERT_RETURN(channel->pcm_buf != NULL, PJ_ENOMEM);


    /* Create RTP and RTCP sessions: */

    status = pjmedia_rtp_session_init(&channel->rtp, param->fmt.pt, 
				 param->ssrc);
    if (status != PJ_SUCCESS)
	return status;

    /* Create and initialize sound device */

    init_snd_param(&channel->snd_info, codec_param);

    /*
    if (dir == PJMEDIA_DIR_ENCODING)
	channel->snd_stream = pj_snd_open_recorder(-1, &channel->snd_info, 
						   &rec_callback, channel);
    else
	channel->snd_stream = pj_snd_open_player(-1, &channel->snd_info, 
						 &play_callback, channel);

    if (!channel->snd_stream)
	return -1;
    */

    /* Done. */
    *p_channel = channel;
    return PJ_SUCCESS;
}


/*
 * Create media stream.
 */
PJ_DEF(pj_status_t) pjmedia_stream_create( pjmedia_endpt *endpt,
					   pj_pool_t *pool,
					   const pjmedia_stream_info *info,
					   pjmedia_stream **p_stream)

{
    pjmedia_stream *stream;
    pjmedia_codec_param codec_param;
    pj_status_t status;

    PJ_ASSERT_RETURN(pool && info && p_stream, PJ_EINVAL);


    /* Allocate the media stream: */

    stream = pj_pool_zalloc(pool, sizeof(pjmedia_stream));
    PJ_ASSERT_RETURN(stream != NULL, PJ_ENOMEM);

    /* Init port. */
    stream->port.info.name = pj_str("stream");
    stream->port.info.signature = ('S'<<3 | 'T'<<2 | 'R'<<1 | 'M');
    stream->port.info.type = PJMEDIA_TYPE_AUDIO;
    stream->port.info.has_info = 1;
    stream->port.info.need_info = 0;
    stream->port.info.pt = info->fmt.pt;
    pj_strdup(pool, &stream->port.info.encoding_name, &info->fmt.encoding_name);
    stream->port.info.sample_rate = info->fmt.sample_rate;
    stream->port.user_data = stream;
    stream->port.put_frame = &put_frame;
    stream->port.get_frame = &get_frame;


    /* Init stream: */
   
    stream->dir = info->dir;
    stream->codec_mgr = pjmedia_endpt_get_codec_mgr(endpt);
    stream->skinfo = info->sock_info;
    stream->rem_rtp_addr = info->rem_addr;

    PJ_TODO(INITIALIZE_RTCP_REMOTE_ADDRESS);

    /* Create mutex to protect jitter buffer: */

    status = pj_mutex_create_simple(pool, NULL, &stream->jb_mutex);
    if (status != PJ_SUCCESS)
	goto err_cleanup;


    /* Create and initialize codec: */

    status = pjmedia_codec_mgr_alloc_codec( stream->codec_mgr,
					    &info->fmt, &stream->codec);
    if (status != PJ_SUCCESS)
	goto err_cleanup;


    /* Get default codec param: */

    status = stream->codec->op->default_attr(stream->codec, &codec_param);
    if (status != PJ_SUCCESS)
	goto err_cleanup;

    /* Set additional info. */
    stream->port.info.bits_per_sample = 0;
    stream->port.info.samples_per_frame = info->fmt.sample_rate*codec_param.ptime/1000;
    stream->port.info.bytes_per_frame = codec_param.avg_bps/8 * codec_param.ptime/1000;


    /* Open the codec: */

    status = stream->codec->op->open(stream->codec, &codec_param);
    if (status != PJ_SUCCESS)
	goto err_cleanup;


    /* Get the frame size: */

    stream->frame_size = (codec_param.avg_bps / 8) * codec_param.ptime / 1000;


    /* Init RTCP session: */

    pj_rtcp_init(&stream->rtcp, info->ssrc);


    /* Create jitter buffer: */

    status = pjmedia_jbuf_create(pool, stream->frame_size, 15, 100,
				 &stream->jb);
    if (status != PJ_SUCCESS)
	goto err_cleanup;


    /*  Create jitter buffer thread: */

    status = pj_thread_create(pool, "decode", 
			      &jitter_buffer_thread, stream,
			      0, PJ_THREAD_SUSPENDED, &stream->thread);
    if (status != PJ_SUCCESS)
	goto err_cleanup;


    /* Create decoder channel: */

    status = create_channel( pool, stream, PJMEDIA_DIR_DECODING, info,
			     &codec_param, &stream->dec);
    if (status != PJ_SUCCESS)
	goto err_cleanup;


    /* Create encoder channel: */

    status = create_channel( pool, stream, PJMEDIA_DIR_ENCODING, info,
			     &codec_param, &stream->enc);
    if (status != PJ_SUCCESS)
	goto err_cleanup;

    /* Resume jitter buffer thread. */
    status = pj_thread_resume( stream->thread );
    if (status != PJ_SUCCESS)
	goto err_cleanup;

    /* Success! */
    *p_stream = stream;
    return PJ_SUCCESS;


err_cleanup:
    pjmedia_stream_destroy(stream);
    return status;
}


/*
 * Destroy stream.
 */
PJ_DEF(pj_status_t) pjmedia_stream_destroy( pjmedia_stream *stream )
{

    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);

    /* Signal threads to quit. */

    stream->quit_flag = 1;


    /* Close encoding sound stream. */
    
    /*
    if (stream->enc && stream->enc->snd_stream) {

	pj_snd_stream_stop(stream->enc->snd_stream);
	pj_snd_stream_close(stream->enc->snd_stream);
	stream->enc->snd_stream = NULL;

    }
    */

    /* Close decoding sound stream. */

    /*
    if (stream->dec && stream->dec->snd_stream) {

	pj_snd_stream_stop(stream->dec->snd_stream);
	pj_snd_stream_close(stream->dec->snd_stream);
	stream->dec->snd_stream = NULL;

    }
    */

    /* Wait for jitter buffer thread to quit: */

    if (stream->thread) {
	pj_thread_join(stream->thread);
	pj_thread_destroy(stream->thread);
	stream->thread = NULL;
    }

    /* Free codec. */

    if (stream->codec) {
	stream->codec->op->close(stream->codec);
	pjmedia_codec_mgr_dealloc_codec(stream->codec_mgr, stream->codec);
	stream->codec = NULL;
    }

    /* Free mutex */
    
    if (stream->jb_mutex) {
	pj_mutex_destroy(stream->jb_mutex);
	stream->jb_mutex = NULL;
    }

    return PJ_SUCCESS;
}



/*
 * Get the port interface.
 */
PJ_DEF(pj_status_t) pjmedia_stream_get_port( pjmedia_stream *stream,
					     pjmedia_port **p_port )
{
    *p_port = &stream->port;
    return PJ_SUCCESS;
}


/*
 * Start stream.
 */
PJ_DEF(pj_status_t) pjmedia_stream_start(pjmedia_stream *stream)
{

    PJ_ASSERT_RETURN(stream && stream->enc && stream->dec, PJ_EINVALIDOP);

    if (stream->enc && (stream->dir & PJMEDIA_DIR_ENCODING)) {
	stream->enc->paused = 0;
	//pj_snd_stream_start(stream->enc->snd_stream);
	PJ_LOG(4,(THIS_FILE, "Encoder stream started"));
    } else {
	PJ_LOG(4,(THIS_FILE, "Encoder stream paused"));
    }

    if (stream->dec && (stream->dir & PJMEDIA_DIR_DECODING)) {
	stream->dec->paused = 0;
	//pj_snd_stream_start(stream->dec->snd_stream);
	PJ_LOG(4,(THIS_FILE, "Decoder stream started"));
    } else {
	PJ_LOG(4,(THIS_FILE, "Decoder stream paused"));
    }

    return PJ_SUCCESS;
}


/*
 * Get stream statistics.
 */
PJ_DEF(pj_status_t) pjmedia_stream_get_stat( const pjmedia_stream *stream,
					     pjmedia_stream_stat *stat)
{
    PJ_ASSERT_RETURN(stream && stat, PJ_EINVAL);

    pj_memcpy(stat, &stream->stat, sizeof(pjmedia_stream_stat));

    return PJ_SUCCESS;
}


/*
 * Pause stream.
 */
PJ_DEF(pj_status_t) pjmedia_stream_pause( pjmedia_stream *stream,
					  pjmedia_dir dir)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if ((dir & PJMEDIA_DIR_ENCODING) && stream->enc) {
	stream->enc->paused = 1;
	PJ_LOG(4,(THIS_FILE, "Encoder stream paused"));
    }

    if ((dir & PJMEDIA_DIR_DECODING) && stream->dec) {
	stream->dec->paused = 1;
	PJ_LOG(4,(THIS_FILE, "Decoder stream paused"));
    }

    return PJ_SUCCESS;
}


/*
 * Resume stream
 */
PJ_DEF(pj_status_t) pjmedia_stream_resume( pjmedia_stream *stream,
					   pjmedia_dir dir)
{
    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if ((dir & PJMEDIA_DIR_ENCODING) && stream->enc) {
	stream->enc->paused = 1;
	PJ_LOG(4,(THIS_FILE, "Encoder stream resumed"));
    }

    if ((dir & PJMEDIA_DIR_DECODING) && stream->dec) {
	stream->dec->paused = 1;
	PJ_LOG(4,(THIS_FILE, "Decoder stream resumed"));
    }

    return PJ_SUCCESS;
}

/*
 * Dial DTMF
 */
PJ_DEF(pj_status_t) pjmedia_stream_dial_dtmf( pjmedia_stream *stream,
					      const pj_str_t *digit_char)
{
    pj_status_t status = PJ_SUCCESS;

    /* By convention we use jitter buffer mutex to access DTMF
     * queue.
     */
    PJ_ASSERT_RETURN(stream && digit_char, PJ_EINVAL);

    pj_mutex_lock(stream->jb_mutex);
    
    if (stream->dtmf_count+digit_char->slen >=
	PJ_ARRAY_SIZE(stream->dtmf_queue))
    {
	status = PJ_ETOOMANY;
    } else {
	int i;

	/* convert ASCII digits into payload type first, to make sure
	 * that all digits are valid. 
	 */
	for (i=0; i<digit_char->slen; ++i) {
	    unsigned pt;

	    if (digit_char->ptr[i] >= '0' &&
		digit_char->ptr[i] <= '9')
	    {
		pt = digit_char->ptr[i] - '0';
	    } 
	    else if (pj_tolower(digit_char->ptr[i]) >= 'a' &&
		     pj_tolower(digit_char->ptr[i]) <= 'd')
	    {
		pt = pj_tolower(digit_char->ptr[i]) - 'a' + 12;
	    }
	    else if (digit_char->ptr[i] == '*')
	    {
		pt = 10;
	    }
	    else if (digit_char->ptr[i] == '#')
	    {
		pt = 11;
	    }
	    else
	    {
		status = PJMEDIA_RTP_EINDTMF;
		break;
	    }

	    stream->dtmf_queue[stream->dtmf_count+1].event = pt;
	    stream->dtmf_queue[stream->dtmf_count+1].start_ts = start_ts;
	    stream->dtmf_queue[stream->dtmf_count+1].end_ts = 
		start_ts + PJMEDIA_DTMF_DURATION;

	    start_ts += PJMEDIA_DTMF_DURATION + 320;
	}

	if (status != PJ_SUCCESS)
	    goto on_return;

	if (stream->dtmf_count ==0) {
	    pj_uint32_t start_ts;

	    start_ts = pj_ntohl(stream->enc->rtp.out_hdr.ts);
	    stream->dtmf_queue[0].start_ts = start_ts;
	    stream->dtmf_queue[0].end_ts = start_ts + PJMEDIA_DTMF_DURATION;
	}

	/* Increment digit count only if all digits are valid. */
	stream->dtmf_count += digit_char->slen;

    }

on_return:
    pj_mutex_unlock(stream->jb_mutex);

    return status;
}

