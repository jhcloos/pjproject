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
#ifndef __PJMEDIA_RTCP_H__
#define __PJMEDIA_RTCP_H__

/**
 * @file rtcp.h
 * @brief RTCP implementation.
 */

#include <pjmedia/types.h>
#include <pjmedia/rtp.h>


PJ_BEGIN_DECL


/**
 * @defgroup PJMED_RTCP RTCP Session and Encapsulation (RFC 3550)
 * @ingroup PJMEDIA_TRANSPORT
 * @{
 * PJMEDIA implements subsets of RTCP specification (RFC 3550) to monitor
 * the quality of the real-time media (audio/video) transmission. In
 * addition to the standard quality monitoring and reporting with RTCP
 * SR and RR types, PJMEDIA's RTCP implementation is able to report
 * extended statistics for incoming streams, such as packet duplications,
 * reorder, discarded, and loss period (to distinguish between random
 * and burst loss).
 *
 * The bidirectional media quality statistic is represented with
 * #pjmedia_rtcp_stat structure.
 *
 * When application uses the stream interface (see @ref PJMED_STRM),
 * application may retrieve the RTCP statistic by calling 
 * #pjmedia_stream_get_stat() function.
 */

#pragma pack(1)

/**
 * RTCP sender report.
 */
struct pjmedia_rtcp_sr
{
    pj_uint32_t	    ntp_sec;	    /**< NTP time, seconds part.	*/
    pj_uint32_t	    ntp_frac;	    /**< NTP time, fractions part.	*/
    pj_uint32_t	    rtp_ts;	    /**< RTP timestamp.			*/
    pj_uint32_t	    sender_pcount;  /**< Sender packet cound.		*/
    pj_uint32_t	    sender_bcount;  /**< Sender octet/bytes count.	*/
};

/**
 * @see pjmedia_rtcp_sr
 */
typedef struct pjmedia_rtcp_sr pjmedia_rtcp_sr;

/**
 * RTCP receiver report.
 */
struct pjmedia_rtcp_rr
{
    pj_uint32_t	    ssrc;	    /**< SSRC identification.		*/
#if defined(PJ_IS_BIG_ENDIAN) && PJ_IS_BIG_ENDIAN!=0
    pj_uint32_t	    fract_lost:8;   /**< Fraction lost.			*/
    pj_uint32_t	    total_lost_2:8; /**< Total lost, bit 16-23.		*/
    pj_uint32_t	    total_lost_1:8; /**< Total lost, bit 8-15.		*/
    pj_uint32_t	    total_lost_0:8; /**< Total lost, bit 0-7.		*/
#else
    pj_uint32_t	    fract_lost:8;   /**< Fraction lost.			*/
    pj_uint32_t	    total_lost_2:8; /**< Total lost, bit 0-7.		*/
    pj_uint32_t	    total_lost_1:8; /**< Total lost, bit 8-15.		*/
    pj_uint32_t	    total_lost_0:8; /**< Total lost, bit 16-23.		*/
#endif	
    pj_uint32_t	    last_seq;	    /**< Last sequence number.		*/
    pj_uint32_t	    jitter;	    /**< Jitter.			*/
    pj_uint32_t	    lsr;	    /**< Last SR.			*/
    pj_uint32_t	    dlsr;	    /**< Delay since last SR.		*/
};

/**
 * @see pjmedia_rtcp_rr
 */
typedef struct pjmedia_rtcp_rr pjmedia_rtcp_rr;


/**
 * RTCP common header.
 */
struct pjmedia_rtcp_common
{
#if defined(PJ_IS_BIG_ENDIAN) && PJ_IS_BIG_ENDIAN!=0
    unsigned	    version:2;	/**< packet type            */
    unsigned	    p:1;	/**< padding flag           */
    unsigned	    count:5;	/**< varies by payload type */
    unsigned	    pt:8;	/**< payload type           */
#else
    unsigned	    count:5;	/**< varies by payload type */
    unsigned	    p:1;	/**< padding flag           */
    unsigned	    version:2;	/**< packet type            */
    unsigned	    pt:8;	/**< payload type           */
#endif
    unsigned	    length:16;	/**< packet length          */
    pj_uint32_t	    ssrc;	/**< SSRC identification    */
};

/**
 * @see pjmedia_rtcp_common
 */
typedef struct pjmedia_rtcp_common pjmedia_rtcp_common;

/**
 * This structure declares default RTCP packet (SR) that is sent by pjmedia.
 * Incoming RTCP packet may have different format, and must be parsed
 * manually by application.
 */
typedef struct pjmedia_rtcp_sr_pkt
{
    pjmedia_rtcp_common  common;	/**< Common header.	    */
    pjmedia_rtcp_sr	 sr;		/**< Sender report.	    */
    pjmedia_rtcp_rr	 rr;		/**< variable-length list   */
} pjmedia_rtcp_sr_pkt;

/**
 * This structure declares RTCP RR (Receiver Report) packet.
 */
typedef struct pjmedia_rtcp_rr_pkt
{
    pjmedia_rtcp_common  common;	/**< Common header.	    */
    pjmedia_rtcp_rr	 rr;		/**< variable-length list   */
} pjmedia_rtcp_rr_pkt;


#pragma pack()


/**
 * NTP time representation.
 */
struct pjmedia_rtcp_ntp_rec
{
    pj_uint32_t	    hi;		/**< High order 32-bit part.	*/
    pj_uint32_t	    lo;		/**< Lo order 32-bit part.	*/
};

/**
 * @see pjmedia_rtcp_ntp_rec
 */
typedef struct pjmedia_rtcp_ntp_rec pjmedia_rtcp_ntp_rec;



/**
 * Unidirectional RTP stream statistics.
 */
struct pjmedia_rtcp_stream_stat
{
    pj_time_val	    update;	/**< Time of last update.		    */
    unsigned	    update_cnt;	/**< Number of updates (to calculate avg)   */
    pj_uint32_t	    pkt;	/**< Total number of packets		    */
    pj_uint32_t	    bytes;	/**< Total number of payload/bytes	    */
    unsigned	    discard;	/**< Total number of discarded packets.	    */
    unsigned	    loss;	/**< Total number of packets lost	    */
    unsigned	    reorder;	/**< Total number of out of order packets   */
    unsigned	    dup;	/**< Total number of duplicates packets	    */

    struct {
	unsigned    count;	/**< Number of period samples(to calc avg)  */
	unsigned    min;	/**< Minimum loss period (in usec)	    */
	unsigned    avg;	/**< Average loss period (in usec)	    */
	unsigned    max;	/**< Maximum loss period (in usec)	    */
	unsigned    last;	/**< Last loss period (in usec)		    */
    } loss_period;		/**< Lost period history.		    */

    struct {
	unsigned    burst:1;	/**< Burst/sequential packet lost detected  */
    	unsigned    random:1;	/**< Random packet lost detected.	    */
    } loss_type;		/**< Types of loss detected.		    */

    struct {
	unsigned    count;	/**< Number of updates.			    */
	unsigned    min;	/**< Minimum jitter (in usec)		    */
	unsigned    avg;	/**< Average jitter (in usec)		    */
	unsigned    max;	/**< Maximum jitter (in usec)		    */
	unsigned    last;	/**< Last jitter (in usec)		    */
    } jitter;			/**< Jitter history.			    */
};


/**
 * @see pjmedia_rtcp_stream_stat
 */
typedef struct pjmedia_rtcp_stream_stat pjmedia_rtcp_stream_stat;



/**
 * Bidirectional RTP stream statistics.
 */
struct pjmedia_rtcp_stat
{
    pj_time_val		     start; /**< Time when session was created	    */

    pjmedia_rtcp_stream_stat tx;    /**< Encoder stream statistics.	    */
    pjmedia_rtcp_stream_stat rx;    /**< Decoder stream statistics.	    */
    
    struct {
	unsigned    min;	    /**< Minimum round-trip delay (in usec) */
	unsigned    avg;	    /**< Average round-trip delay (in usec) */
	unsigned    max;	    /**< Maximum round-trip delay (in usec) */
	unsigned    last;	    /**< Last round-trip delay (in usec)    */
    } rtt;			    /**< Round trip delay history.	    */

    unsigned	    rtt_update_cnt; /**< Nb of times rtt is updated.	    */
};


/**
 * @see pjmedia_rtcp_stat
 */
typedef struct pjmedia_rtcp_stat pjmedia_rtcp_stat;


/**
 * The types for keeping the average jitter value. Ideally a floating point
 * number should be used, but this is not always available/desired.
 */
#if defined(PJ_HAS_FLOATING_POINT) && PJ_HAS_FLOATING_POINT!=0
  typedef double PJMEDIA_AVG_JITTER_TYPE;
#else
  typedef pj_uint32_t PJMEDIA_AVG_JITTER_TYPE;
#endif


/**
 * RTCP session is used to monitor the RTP session of one endpoint. There
 * should only be one RTCP session for a bidirectional RTP streams.
 */
struct pjmedia_rtcp_session
{
    char		   *name;	/**< Name identification.	    */
    pjmedia_rtcp_sr_pkt	    rtcp_sr_pkt;/**< Cached RTCP packet.	    */
    pjmedia_rtcp_rr_pkt	    rtcp_rr_pkt;/**< Cached RTCP RR packet.	    */
    
    pjmedia_rtp_seq_session seq_ctrl;	/**< RTCP sequence number control.  */
    unsigned		    rtp_last_ts;/**< Last timestamp in RX RTP pkt.  */

    unsigned		    clock_rate;	/**< Clock rate of the stream	    */
    unsigned		    pkt_size;	/**< Avg pkt size, in samples.	    */
    pj_uint32_t		    received;   /**< # pkt received		    */
    pj_uint32_t		    exp_prior;	/**< # pkt expected at last interval*/
    pj_uint32_t		    rx_prior;	/**< # pkt received at last interval*/
    pj_int32_t		    transit;    /**< Rel transit time for prev pkt  */
    pj_uint32_t		    jitter;	/**< Scaled jitter		    */
    pj_time_val		    tv_base;	/**< Base time, in seconds.	    */
    pj_timestamp	    ts_base;	/**< Base system timestamp.	    */
    pj_timestamp	    ts_freq;	/**< System timestamp frequency.    */

    pj_uint32_t		    rx_lsr;	/**< NTP ts in last SR received	    */
    pj_timestamp	    rx_lsr_time;/**< Time when last SR is received  */
    pj_uint32_t		    peer_ssrc;	/**< Peer SSRC			    */
    
    pjmedia_rtcp_stat	    stat;	/**< Bidirectional stream stat.	    */

    /* Keep jitter calculation in floating point to prevent the values
     * from being rounded-down to nearest integer.
     */
    PJMEDIA_AVG_JITTER_TYPE avg_jitter;	/**< Average RX jitter.		    */
};

/**
 * @see pjmedia_rtcp_session
 */
typedef struct pjmedia_rtcp_session pjmedia_rtcp_session;


/**
 * Initialize RTCP session.
 *
 * @param session	    The session
 * @param name		    Optional name to identify the session (for
 *			    logging purpose).
 * @param clock_rate	    Codec clock rate in samples per second.
 * @param samples_per_frame Average number of samples per frame.
 * @param ssrc		    The SSRC used in to identify the session.
 */
PJ_DECL(void) pjmedia_rtcp_init( pjmedia_rtcp_session *session, 
				 char *name,
				 unsigned clock_rate,
				 unsigned samples_per_frame,
				 pj_uint32_t ssrc );


/**
 * Utility function to retrieve current NTP timestamp.
 *
 * @param sess		    RTCP session.
 * @param ntp		    NTP record.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_rtcp_get_ntp_time(const pjmedia_rtcp_session *sess,
					       pjmedia_rtcp_ntp_rec *ntp);


/**
 * Deinitialize RTCP session.
 *
 * @param session   The session.
 */
PJ_DECL(void) pjmedia_rtcp_fini( pjmedia_rtcp_session *session);


/**
 * Call this function everytime an RTP packet is received to let the RTCP
 * session do its internal calculations.
 *
 * @param session   The session.
 * @param seq	    The RTP packet sequence number, in host byte order.
 * @param ts	    The RTP packet timestamp, in host byte order.
 * @param payload   Size of the payload.
 */
PJ_DECL(void) pjmedia_rtcp_rx_rtp( pjmedia_rtcp_session *session, 
				   unsigned seq, 
				   unsigned ts,
				   unsigned payload);


/**
 * Call this function everytime an RTP packet is sent to let the RTCP session
 * do its internal calculations.
 *
 * @param session   The session.
 * @param ptsize    The payload size of the RTP packet (ie packet minus
 *		    RTP header) in bytes.
 */
PJ_DECL(void) pjmedia_rtcp_tx_rtp( pjmedia_rtcp_session *session, 
				   unsigned ptsize );


/**
 * Call this function when an RTCP packet is received from remote peer.
 * This RTCP packet received from remote is used to calculate the end-to-
 * end delay of the network.
 *
 * @param session   RTCP session.
 * @param rtcp_pkt  The received RTCP packet.
 * @param size	    Size of the incoming packet.
 */
PJ_DECL(void) pjmedia_rtcp_rx_rtcp( pjmedia_rtcp_session *session,
				    const void *rtcp_pkt,
				    pj_size_t size);


/**
 * Build a RTCP packet to be transmitted to remote RTP peer. This will
 * create RTCP Sender Report (SR) or Receiver Report (RR) depending on
 * whether the endpoint has been transmitting RTP since the last interval.
 * Note that this function will reset the interval counters (such as
 * the ones to calculate fraction lost) in the session.
 *
 * @param session   The RTCP session.
 * @param rtcp_pkt  Upon return, it will contain pointer to the 
 *		    RTCP packet, which can be RTCP SR or RR.
 * @param len	    Upon return, it will indicate the size of 
 *		    the RTCP packet.
 */
PJ_DECL(void) pjmedia_rtcp_build_rtcp( pjmedia_rtcp_session *session, 
				       void **rtcp_pkt, int *len);


/**
 * @}
 */

PJ_END_DECL


#endif	/* __PJMEDIA_RTCP_H__ */
