/* $Header: /pjproject/pjmedia/src/pjmedia/stream.h 6     8/24/05 10:30a Bennylp $ */

#ifndef __PJMEDIA_STREAM_H__
#define __PJMEDIA_STREAM_H__


/**
 * @file stream.h
 * @brief Stream of media.
 */

#include <pjmedia/sound.h>
#include <pjmedia/codec.h>
#include <pjmedia/mediamgr.h>
#include <pj/sock.h>

PJ_BEGIN_DECL


/**
 * @defgroup PJMED_SES Media session
 * @ingroup PJMEDIA
 * @{
 */

typedef struct pj_media_stream_t pj_media_stream_t;

/** Parameter for creating channel. */
typedef struct pj_media_stream_create_param
{
    /** Codec ID, must NOT be NULL. */
    pj_codec_id		 *codec_id;

    /** Media manager, must NOT be NULL. */
    pj_med_mgr_t	 *mediamgr;

    /** Direction: IN_OUT, or IN only, or OUT only. */
    pj_media_dir_t	  dir;

    /** RTP socket. */
    pj_sock_t		 rtp_sock;

    /** RTCP socket. */
    pj_sock_t		 rtcp_sock;

    /** Address of remote */
    pj_sockaddr_in	 *remote_addr;

    /** RTP SSRC */
    pj_uint32_t		  ssrc;

    /** Jitter buffer parameters. */
    int			  jb_min, jb_max, jb_maxcnt;

} pj_media_stream_create_param;

typedef struct pj_media_stream_stat
{
    pj_uint32_t pkt_tx, pkt_rx;	/* packets transmitted/received */
    pj_uint32_t oct_tx, oct_rx;	/* octets transmitted/received */
    pj_uint32_t jitter;		/* receive jitter in ms */
    pj_uint32_t pkt_lost;	/* total packet lost count */
} pj_media_stream_stat;

PJ_DECL(pj_status_t) pj_media_stream_create (pj_pool_t *pool,
					     pj_media_stream_t **enc_stream,
					     pj_media_stream_t **dec_stream,
					     pj_media_stream_create_param *param);
PJ_DECL(pj_status_t) pj_media_stream_start (pj_media_stream_t *stream);
PJ_DECL(pj_status_t) pj_media_stream_get_stat (const pj_media_stream_t *stream,
					       pj_media_stream_stat *stat);
PJ_DECL(pj_status_t) pj_media_stream_pause (pj_media_stream_t *stream);
PJ_DECL(pj_status_t) pj_media_stream_resume (pj_media_stream_t *stream);
PJ_DECL(pj_status_t) pj_media_stream_destroy (pj_media_stream_t *stream);

/**
 * @}
 */

PJ_END_DECL


#endif	/* __PJMEDIA_STREAM_H__ */
