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
#ifndef __PJMEDIA_CONFIG_H__
#define __PJMEDIA_CONFIG_H__

/**
 * @file pjmedia/config.h Compile time config
 * @brief Contains some compile time constants.
 */
#include <pj/config.h>

/**
 * @defgroup PJMEDIA_BASE Base Types and Configurations
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

/** Constant for Win32 MME sound backend. */
#define PJMEDIA_SOUND_WIN32_MME_SOUND	    3

/** When this is set, pjmedia will not provide any sound device backend. 
 *  Application will have to provide its own sound device backend
 *  and link the application with it.
 */
#define PJMEDIA_SOUND_EXTERNAL		    255


/**
 * Unless specified otherwise, sound device uses PortAudio implementation
 * by default.
 */
#ifndef PJMEDIA_SOUND_IMPLEMENTATION
#  if defined(PJ_WIN32) && PJ_WIN32!=0
/*#   define PJMEDIA_SOUND_IMPLEMENTATION   PJMEDIA_SOUND_WIN32_DIRECT_SOUND*/
/*#   define PJMEDIA_SOUND_IMPLEMENTATION   PJMEDIA_SOUND_WIN32_MME_SOUND*/
#   define PJMEDIA_SOUND_IMPLEMENTATION	    PJMEDIA_SOUND_PORTAUDIO_SOUND
#  else
#   define PJMEDIA_SOUND_IMPLEMENTATION	    PJMEDIA_SOUND_PORTAUDIO_SOUND
#  endif
#endif


/**
 * Specify whether we prefer to use DirectSound on Windows.
 *
 * Default: 0
 */
#ifndef PJMEDIA_PREFER_DIRECT_SOUND
#   define PJMEDIA_PREFER_DIRECT_SOUND	    0
#endif


/**
 * Specify sound device latency default, in milisecond.
 */
#ifndef PJMEDIA_SND_DEFAULT_REC_LATENCY
#   define PJMEDIA_SND_DEFAULT_REC_LATENCY  100
#endif

#ifndef PJMEDIA_SND_DEFAULT_PLAY_LATENCY
#   define PJMEDIA_SND_DEFAULT_PLAY_LATENCY 100
#endif


/**
 * Specify whether delay buffer is used for sound device.
 * When delay buffer is enabled, the sound device callback 
 * will be called one after another evenly.
 * The delay buffer also performs the best delay calculation
 * for the sound device, and will try to limit the delay caused
 * by uneven callback calls to this delay.
 *
 * When this setting is enabled, the PJMEDIA_SOUND_BUFFER_COUNT
 * macro will specify the maximum size of the delay buffer.
 */
#ifndef PJMEDIA_SOUND_USE_DELAYBUF
#   define PJMEDIA_SOUND_USE_DELAYBUF	    0
#endif


/*
 * Types of WSOLA backend algorithm.
 */

/**
 * This denotes implementation of WSOLA using null algorithm. Expansion
 * will generate zero frames, and compression will just discard some
 * samples from the input.
 *
 * This type of implementation may be used as it requires the least
 * processing power.
 */
#define PJMEDIA_WSOLA_IMP_NULL		    0

/**
 * This denotes implementation of WSOLA using fixed or floating point WSOLA
 * algorithm. This implementation provides the best quality of the result,
 * at the expense of one frame delay and intensive processing power 
 * requirement.
 */
#define PJMEDIA_WSOLA_IMP_WSOLA		    1

/**
 * This denotes implementation of WSOLA algorithm with faster waveform 
 * similarity calculation. This implementation provides fair quality of 
 * the result with the main advantage of low processing power requirement.
 */
#define PJMEDIA_WSOLA_IMP_WSOLA_LITE	    2

/**
 * Specify type of Waveform based Similarity Overlap and Add (WSOLA) backend
 * implementation to be used. WSOLA is an algorithm to expand and/or compress 
 * audio frames without changing the pitch, and used by the delaybuf and as PLC
 * backend algorithm.
 *
 * Default is PJMEDIA_WSOLA_IMP_WSOLA
 */
#ifndef PJMEDIA_WSOLA_IMP
#   define PJMEDIA_WSOLA_IMP		    PJMEDIA_WSOLA_IMP_WSOLA
#endif


/**
 * Specify number of sound buffers. Larger number is better for sound
 * stability and to accommodate sound devices that are unable to send frames
 * in timely manner, however it would probably cause more audio delay (and 
 * definitely will take more memory). One individual buffer is normally 10ms
 * or 20 ms long, depending on ptime settings (samples_per_frame value).
 *
 * The setting here currently is used by the conference bridge, the splitter
 * combiner port, and dsound.c.
 *
 * Default: 6
 */
#ifndef PJMEDIA_SOUND_BUFFER_COUNT
#   define PJMEDIA_SOUND_BUFFER_COUNT	    6
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


/*
 * Warn about obsolete macros.
 *
 * PJMEDIA_HAS_SMALL_FILTER has been deprecated in 0.7.
 */
#if defined(PJMEDIA_HAS_SMALL_FILTER)
#   ifdef _MSC_VER
#	pragma message("Warning: PJMEDIA_HAS_SMALL_FILTER macro is deprecated"\
		       " and has no effect")
#   else
#	warning "PJMEDIA_HAS_SMALL_FILTER macro is deprecated and has no effect"
#   endif
#endif


/*
 * Warn about obsolete macros.
 *
 * PJMEDIA_HAS_LARGE_FILTER has been deprecated in 0.7.
 */
#if defined(PJMEDIA_HAS_LARGE_FILTER)
#   ifdef _MSC_VER
#	pragma message("Warning: PJMEDIA_HAS_LARGE_FILTER macro is deprecated"\
		       " and has no effect")
#   else
#	warning "PJMEDIA_HAS_LARGE_FILTER macro is deprecated"
#   endif
#endif


/*
 * These macros are obsolete in 0.7.1 so it will trigger compilation error.
 * Please use PJMEDIA_RESAMPLE_IMP to select the resample implementation
 * to use.
 */
#ifdef PJMEDIA_HAS_LIBRESAMPLE
#   error "PJMEDIA_HAS_LIBRESAMPLE macro is deprecated. Use '#define PJMEDIA_RESAMPLE_IMP PJMEDIA_RESAMPLE_LIBRESAMPLE'"
#endif

#ifdef PJMEDIA_HAS_SPEEX_RESAMPLE
#   error "PJMEDIA_HAS_SPEEX_RESAMPLE macro is deprecated. Use '#define PJMEDIA_RESAMPLE_IMP PJMEDIA_RESAMPLE_SPEEX'"
#endif


/*
 * Sample rate conversion backends.
 * Select one of these backends in PJMEDIA_RESAMPLE_IMP.
 */
#define PJMEDIA_RESAMPLE_NONE		    1	/**< No resampling.	    */
#define PJMEDIA_RESAMPLE_LIBRESAMPLE	    2	/**< Sample rate conversion 
						     using libresample.  */
#define PJMEDIA_RESAMPLE_SPEEX		    3	/**< Sample rate conversion 
						     using Speex. */
#define PJMEDIA_RESAMPLE_LIBSAMPLERATE	    4	/**< Sample rate conversion 
						     using libsamplerate 
						     (a.k.a Secret Rabbit Code)
						 */

/**
 * Select which resample implementation to use. Currently pjmedia supports:
 *  - #PJMEDIA_RESAMPLE_LIBRESAMPLE, to use libresample-1.7, this is the default
 *    implementation to be used.
 *  - #PJMEDIA_RESAMPLE_LIBSAMPLERATE, to use libsamplerate implementation
 *    (a.k.a. Secret Rabbit Code).
 *  - #PJMEDIA_RESAMPLE_SPEEX, to use experimental sample rate conversion in
 *    Speex library.
 *  - #PJMEDIA_RESAMPLE_NONE, to disable sample rate conversion. Any calls to
 *    resample function will return error.
 *
 * Default is PJMEDIA_RESAMPLE_LIBRESAMPLE
 */
#ifndef PJMEDIA_RESAMPLE_IMP
#   define PJMEDIA_RESAMPLE_IMP		    PJMEDIA_RESAMPLE_LIBRESAMPLE
#endif


/**
 * Specify whether libsamplerate, when used, should be linked statically
 * into the application. This option is only useful for Visual Studio
 * projects, and when this static linking is enabled
 */


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
 * Specify whether RTCP should be advertised in SDP. This setting would
 * affect whether RTCP candidate will be added in SDP when ICE is used.
 * Application might want to disable RTCP advertisement in SDP to
 * reduce the message size.
 *
 * Default: 1 (yes)
 */
#ifndef PJMEDIA_ADVERTISE_RTCP
#   define PJMEDIA_ADVERTISE_RTCP		1
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
 * Specify whether RTCP XR support should be built into PJMEDIA. Disabling
 * this feature will reduce footprint slightly. Note that even when this 
 * setting is enabled, RTCP XR processing will only be performed in stream 
 * if it is enabled on run-time on per stream basis. See  
 * PJMEDIA_STREAM_ENABLE_XR setting for more info.
 *
 * Default: 1 (yes).
 */
#ifndef PJMEDIA_HAS_RTCP_XR
#   define PJMEDIA_HAS_RTCP_XR			0
#endif


/**
 * The RTCP XR feature is activated and used by stream if \a enable_rtcp_xr
 * field of \a pjmedia_stream_info structure is non-zero. This setting 
 * controls the default value of this field.
 *
 * Default: 0 (disabled)
 */
#ifndef PJMEDIA_STREAM_ENABLE_XR
#   define PJMEDIA_STREAM_ENABLE_XR		0
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
 * Specify the maximum duration of silence period in the codec, in msec. 
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
 * Default: 5000 ms
 *
 */
#ifndef PJMEDIA_CODEC_MAX_SILENCE_PERIOD
#   define PJMEDIA_CODEC_MAX_SILENCE_PERIOD	5000
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
 * Maximum silence threshold in the silence detector. The silence detector
 * will not cut the audio transmission if the audio level is above this
 * level.
 *
 * Default: 25
 */
#ifndef PJMEDIA_SILENCE_DET_MAX_THRESHOLD
#   define PJMEDIA_SILENCE_DET_MAX_THRESHOLD	25
#endif


/**
 * Speex Accoustic Echo Cancellation (AEC).
 * By default is enabled.
 */
#ifndef PJMEDIA_HAS_SPEEX_AEC
#   define PJMEDIA_HAS_SPEEX_AEC		1
#endif


/**
 * This specifies the behavior of the SDP negotiator when responding to an
 * offer, whether it should rather use the codec preference as set by
 * remote, or should it rather use the codec preference as specified by
 * local endpoint.
 *
 * For example, suppose incoming call has codec order "8 0 3", while 
 * local codec order is "3 0 8". If remote codec order is preferable,
 * the selected codec will be 8, while if local codec order is preferable,
 * the selected codec will be 3.
 *
 * If set to non-zero, the negotiator will use the codec order as specified
 * by remote in the offer.
 *
 * Note that this behavior can be changed during run-time by calling
 * pjmedia_sdp_neg_set_prefer_remote_codec_order().
 *
 * Default is 1 (to maintain backward compatibility)
 */
#ifndef PJMEDIA_SDP_NEG_PREFER_REMOTE_CODEC_ORDER
#   define PJMEDIA_SDP_NEG_PREFER_REMOTE_CODEC_ORDER	1
#endif


/**
 * Support for sending and decoding RTCP port in SDP (RFC 3605).
 * Default is equal to PJMEDIA_ADVERTISE_RTCP setting.
 */
#ifndef PJMEDIA_HAS_RTCP_IN_SDP
#   define PJMEDIA_HAS_RTCP_IN_SDP		(PJMEDIA_ADVERTISE_RTCP)
#endif


/**
 * This macro controls whether pjmedia should include SDP rtpmap 
 * attribute for static payload types. SDP rtpmap for static
 * payload types are optional, although they are normally included
 * for interoperability reason.
 *
 * Note that there is also a run-time variable to turn this setting
 * on or off, defined in endpoint.c. To access this variable, use
 * the following construct
 *
 \verbatim
    extern pj_bool_t pjmedia_add_rtpmap_for_static_pt;

    // Do not include rtpmap for static payload types (<96)
    pjmedia_add_rtpmap_for_static_pt = PJ_FALSE;
 \endverbatim
 *
 * Default: 1 (yes)
 */
#ifndef PJMEDIA_ADD_RTPMAP_FOR_STATIC_PT
#   define PJMEDIA_ADD_RTPMAP_FOR_STATIC_PT	1
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


/* 
 * Below specifies the various tone generator backend algorithm.
 */

/** 
 * The math's sine(), floating point. This has very good precision 
 * but it's the slowest and requires floating point support and
 * linking with the math library.
 */
#define PJMEDIA_TONEGEN_SINE			    1

/**
 * Floating point approximation of sine(). This has relatively good
 * precision and much faster than plain sine(), but it requires floating-
 * point support and linking with the math library.
 */
#define PJMEDIA_TONEGEN_FLOATING_POINT		    2

/**
 * Fixed point using sine signal generated by Cordic algorithm. This
 * algorithm can be tuned to provide balance between precision and
 * performance by tuning the PJMEDIA_TONEGEN_FIXED_POINT_CORDIC_LOOP 
 * setting, and may be suitable for platforms that lack floating-point
 * support.
 */
#define PJMEDIA_TONEGEN_FIXED_POINT_CORDIC	    3

/**
 * Fast fixed point using some approximation to generate sine waves.
 * The tone generated by this algorithm is not very precise, however
 * the algorithm is very fast.
 */
#define PJMEDIA_TONEGEN_FAST_FIXED_POINT	    4


/**
 * Specify the tone generator algorithm to be used. Please see 
 * http://trac.pjsip.org/repos/wiki/Tone_Generator for the performance
 * analysis results of the various tone generator algorithms.
 *
 * Default value:
 *  - PJMEDIA_TONEGEN_FLOATING_POINT when PJ_HAS_FLOATING_POINT is set
 *  - PJMEDIA_TONEGEN_FIXED_POINT_CORDIC when PJ_HAS_FLOATING_POINT is not set
 */
#ifndef PJMEDIA_TONEGEN_ALG
#   if defined(PJ_HAS_FLOATING_POINT) && PJ_HAS_FLOATING_POINT
#	define PJMEDIA_TONEGEN_ALG	PJMEDIA_TONEGEN_FLOATING_POINT
#   else
#	define PJMEDIA_TONEGEN_ALG	PJMEDIA_TONEGEN_FIXED_POINT_CORDIC
#   endif
#endif


/**
 * Specify the number of calculation loops to generate the tone, when
 * PJMEDIA_TONEGEN_FIXED_POINT_CORDIC algorithm is used. With more calculation
 * loops, the tone signal gets more precise, but this will add more 
 * processing.
 *
 * Valid values are 1 to 28.
 *
 * Default value: 10
 */
#ifndef PJMEDIA_TONEGEN_FIXED_POINT_CORDIC_LOOP
#   define PJMEDIA_TONEGEN_FIXED_POINT_CORDIC_LOOP  10
#endif


/**
 * Enable high quality of tone generation, the better quality will cost
 * more CPU load. This is only applied to floating point enabled machines.
 *
 * By default it is enabled when PJ_HAS_FLOATING_POINT is set.
 *
 * This macro has been deprecated in version 1.0-rc3.
 */
#ifdef PJMEDIA_USE_HIGH_QUALITY_TONEGEN
#   error   "The PJMEDIA_USE_HIGH_QUALITY_TONEGEN macro is obsolete"
#endif


/**
 * Fade-in duration for the tone, in milliseconds. Set to zero to disable
 * this feature.
 *
 * Default: 1 (msec)
 */
#ifndef PJMEDIA_TONEGEN_FADE_IN_TIME
#   define PJMEDIA_TONEGEN_FADE_IN_TIME		    1
#endif


/**
 * Fade-out duration for the tone, in milliseconds. Set to zero to disable
 * this feature.
 *
 * Default: 2 (msec)
 */
#ifndef PJMEDIA_TONEGEN_FADE_OUT_TIME
#   define PJMEDIA_TONEGEN_FADE_OUT_TIME	    2
#endif


/**
 * The default tone generator amplitude (1-32767).
 *
 * Default value: 12288
 */
#ifndef PJMEDIA_TONEGEN_VOLUME
#   define PJMEDIA_TONEGEN_VOLUME		    12288
#endif


/**
 * Enable support for SRTP media transport. This will require linking
 * with libsrtp from the third_party directory.
 *
 * By default it is enabled.
 */
#ifndef PJMEDIA_HAS_SRTP
#   define PJMEDIA_HAS_SRTP			    1
#endif


/**
 * Enable support to handle codecs with inconsistent clock rate
 * between clock rate in SDP/RTP & the clock rate that is actually used.
 * This happens for example with G.722 and MPEG audio codecs.
 * See:
 *  - G.722      : RFC 3551 4.5.2
 *  - MPEG audio : RFC 3551 4.5.13 & RFC 3119
 *
 * Also when this feature is enabled, some handling will be performed
 * to deal with clock rate incompatibilities of some phones.
 *
 * By default it is enabled.
 */
#ifndef PJMEDIA_HANDLE_G722_MPEG_BUG
#   define PJMEDIA_HANDLE_G722_MPEG_BUG		    1
#endif


/**
 * Transport info (pjmedia_transport_info) contains a socket info and list
 * of transport specific info, since transports can be chained together 
 * (for example, SRTP transport uses UDP transport as the underlying 
 * transport). This constant specifies maximum number of transport specific
 * infos that can be held in a transport info.
 */
#ifndef PJMEDIA_TRANSPORT_SPECIFIC_INFO_MAXCNT
#   define PJMEDIA_TRANSPORT_SPECIFIC_INFO_MAXCNT   4
#endif


/**
 * Maximum size in bytes of storage buffer of a transport specific info.
 */
#ifndef PJMEDIA_TRANSPORT_SPECIFIC_INFO_MAXSIZE
#   define PJMEDIA_TRANSPORT_SPECIFIC_INFO_MAXSIZE  (16*sizeof(long))
#endif



/**
 * @}
 */


#endif	/* __PJMEDIA_CONFIG_H__ */


