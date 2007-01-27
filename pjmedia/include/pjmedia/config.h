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
#ifndef __PJMEDIA_CONFIG_H__
#define __PJMEDIA_CONFIG_H__

/**
 * @file pjmedia/config.h Compile time config
 * @brief Contains some compile time constants.
 */
#include <pj/config.h>

/**
 * @defgroup PJMEDIA_BASE Base Types and Configurations
 * @ingroup PJMEDIA
 */

/**
 * @defgroup PJMEDIA_CONFIG Compile time configuration
 * @ingroup PJMEDIA_BASE
 * @brief Some compile time configuration settings.
 * @{
 */

/*
 * Include config_auto.h if autoconf is used (PJ_AUTOCONF is set)
 */
#if defined(PJ_AUTOCONF)
#   include <pjmedia/config_auto.h>
#endif

/*
 * Types of sound stream backends.
 */

/** Constant for NULL sound backend. */
#define PJMEDIA_SOUND_NULL_SOUND	    0

/** Constant for PortAudio sound backend. */
#define PJMEDIA_SOUND_PORTAUDIO_SOUND	    1

/** Constant for Win32 DirectSound sound backend. */
#define PJMEDIA_SOUND_WIN32_DIRECT_SOUND    2


/**
 * Unless specified otherwise, sound device uses PortAudio implementation
 * by default.
 */
#ifndef PJMEDIA_SOUND_IMPLEMENTATION
#  if defined(PJ_WIN32) && PJ_WIN32!=0
/*#   define PJMEDIA_SOUND_IMPLEMENTATION   PJMEDIA_SOUND_WIN32_DIRECT_SOUND*/
#   define PJMEDIA_SOUND_IMPLEMENTATION	    PJMEDIA_SOUND_PORTAUDIO_SOUND
#  else
#   define PJMEDIA_SOUND_IMPLEMENTATION	    PJMEDIA_SOUND_PORTAUDIO_SOUND
#  endif
#endif


/**
 * Specify number of sound buffers. Larger number is better for sound
 * stability and to accomodate sound devices that are unable to send frames
 * in timely manner, however it would take more memory. One individual
 * buffer is about 10 or 20 ms long.
 *
 * Default: 32
 */
#ifndef PJMEDIA_SOUND_BUFFER_COUNT
#   define PJMEDIA_SOUND_BUFFER_COUNT	    32
#endif


/**
 * Specify which A-law/U-law conversion algorithm to use.
 * By default the conversion algorithm uses A-law/U-law table which gives
 * the best performance, at the expense of 33 KBytes of static data.
 * If this option is disabled, a smaller but slower algorithm will be used.
 */
#ifndef PJMEDIA_HAS_ALAW_ULAW_TABLE
#   define PJMEDIA_HAS_ALAW_ULAW_TABLE	    1
#endif


/**
 * Unless specified otherwise, G711 codec is included by default.
 */
#ifndef PJMEDIA_HAS_G711_CODEC
#   define PJMEDIA_HAS_G711_CODEC	    1
#endif


/**
 * Include small filter table in resample.
 * This adds about 9KB in rdata.
 */
#ifndef PJMEDIA_HAS_SMALL_FILTER
#   define PJMEDIA_HAS_SMALL_FILTER	    1
#endif


/**
 * Include large filter table in resample.
 * This adds about 32KB in rdata.
 */
#ifndef PJMEDIA_HAS_LARGE_FILTER
#   define PJMEDIA_HAS_LARGE_FILTER	    1
#endif


/**
 * Default file player/writer buffer size.
 */
#ifndef PJMEDIA_FILE_PORT_BUFSIZE
#   define PJMEDIA_FILE_PORT_BUFSIZE    4000
#endif


/**
 * Maximum frame duration (in msec) to be supported.
 * This (among other thing) will affect the size of buffers to be allocated
 * for outgoing packets.
 */
#ifndef PJMEDIA_MAX_FRAME_DURATION_MS   
#   define PJMEDIA_MAX_FRAME_DURATION_MS   	200
#endif


/**
 * Max packet size to support.
 */
#ifndef PJMEDIA_MAX_MTU			
#  define PJMEDIA_MAX_MTU			1500
#endif


/**
 * DTMF/telephone-event duration, in timestamp.
 */
#ifndef PJMEDIA_DTMF_DURATION		
#  define PJMEDIA_DTMF_DURATION			1600	/* in timestamp */
#endif


/**
 * Number of packets received from different source IP address from the
 * remote address required to make the stream switch transmission
 * to the source address.
 */
#ifndef PJMEDIA_RTP_NAT_PROBATION_CNT	
#  define PJMEDIA_RTP_NAT_PROBATION_CNT		10
#endif


/**
 * Interval to send RTCP packets, in msec
 */
#ifndef PJMEDIA_RTCP_INTERVAL
#	define PJMEDIA_RTCP_INTERVAL		5000	/* msec*/
#endif


/**
 * Tell RTCP to ignore the first N packets when calculating the
 * jitter statistics. From experimentation, the first few packets
 * (25 or so) have relatively big jitter, possibly because during
 * this time, the program is also busy setting up the signaling,
 * so they make the average jitter big.
 *
 * Default: 25.
 */
#ifndef PJMEDIA_RTCP_IGNORE_FIRST_PACKETS
#   define  PJMEDIA_RTCP_IGNORE_FIRST_PACKETS	25
#endif


/**
 * Specify how long (in miliseconds) the stream should suspend the
 * silence detector/voice activity detector (VAD) during the initial
 * period of the session. This feature is useful to open bindings in
 * all NAT routers between local and remote endpoint since most NATs
 * do not allow incoming packet to get in before local endpoint sends
 * outgoing packets.
 *
 * Specify zero to disable this feature.
 *
 * Default: 600 msec (which gives good probability that some RTP 
 *                    packets will reach the destination, but without
 *                    filling up the jitter buffer on the remote end).
 */
#ifndef PJMEDIA_STREAM_VAD_SUSPEND_MSEC
#   define PJMEDIA_STREAM_VAD_SUSPEND_MSEC	600
#endif


/**
 * Specify the maximum duration of silence period in the codec. 
 * This is useful for example to keep NAT binding open in the firewall
 * and to prevent server from disconnecting the call because no 
 * RTP packet is received.
 *
 * This only applies to codecs that use PJMEDIA's VAD (pretty much
 * everything including iLBC, except Speex, which has its own DTX 
 * mechanism).
 *
 * Use (-1) to disable this feature.
 *
 * Default: 8000 (one second on 8KHz).
 *
 */
#ifndef PJMEDIA_CODEC_MAX_SILENCE_PERIOD
#   define PJMEDIA_CODEC_MAX_SILENCE_PERIOD	8000
#endif


/**
 * Suggested or default threshold to be set for fixed silence detection
 * or as starting threshold for adaptive silence detection. The threshold
 * has the range from zero to 255.
 */
#ifndef PJMEDIA_SILENCE_DET_THRESHOLD
#   define PJMEDIA_SILENCE_DET_THRESHOLD	4
#endif


/**
 * Enable Steve Underwood's PLC.
 */
#ifndef PJMEDIA_HAS_STEVEU_PLC
#   define PJMEDIA_HAS_STEVEU_PLC		PJ_HAS_FLOATING_POINT
#endif


/**
 * Speex Accoustic Echo Cancellation (AEC).
 * By default is enabled.
 */
#ifndef PJMEDIA_HAS_SPEEX_AEC
#   define PJMEDIA_HAS_SPEEX_AEC		1
#endif


/**
 * Initial signal threshold to be applied to echo suppressor. When
 * playback signal level is greater than this threshold, the microphone
 * signal will be reduced or cut.
 */
#ifndef PJMEDIA_ECHO_SUPPRESS_THRESHOLD
#   define PJMEDIA_ECHO_SUPPRESS_THRESHOLD	PJMEDIA_SILENCE_DET_THRESHOLD
#endif


/**
 * The signal reduction factor to be applied into the microphone signal
 * when the mic signal needs to be reduced. Valid values are [1-16], where
 * 1 will leave signal as it is (thus probably transmitting the echo) and
 * 16 will effectively zero the signal.
 */
#ifndef PJMEDIA_ECHO_SUPPRESS_FACTOR
#   define PJMEDIA_ECHO_SUPPRESS_FACTOR		4
#endif


/**
 * Support for sending and decoding RTCP port in SDP (RFC 3605).
 * Default is yes.
 */
#ifndef PJMEDIA_HAS_RTCP_IN_SDP
#   define PJMEDIA_HAS_RTCP_IN_SDP		1
#endif


/**
 * This macro declares the payload type for telephone-event
 * that is advertised by PJMEDIA for outgoing SDP. If this macro
 * is set to zero, telephone events would not be advertised nor
 * supported.
 *
 * If this value is changed to other number, please update the
 * PJMEDIA_RTP_PT_TELEPHONE_EVENTS_STR too.
 */
#ifndef PJMEDIA_RTP_PT_TELEPHONE_EVENTS
#   define PJMEDIA_RTP_PT_TELEPHONE_EVENTS	    101
#endif


/**
 * Macro to get the string representation of the telephone-event
 * payload type.
 */
#ifndef PJMEDIA_RTP_PT_TELEPHONE_EVENTS_STR
#   define PJMEDIA_RTP_PT_TELEPHONE_EVENTS_STR	    "101"
#endif


/**
 * Maximum tones/digits that can be enqueued in the tone generator.
 */
#ifndef PJMEDIA_TONEGEN_MAX_DIGITS
#   define PJMEDIA_TONEGEN_MAX_DIGITS		    32
#endif



/**
 * @}
 */


#endif	/* __PJMEDIA_CONFIG_H__ */


