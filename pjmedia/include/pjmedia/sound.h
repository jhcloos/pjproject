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
#ifndef __PJMEDIA_SOUND_H__
#define __PJMEDIA_SOUND_H__


/**
 * @file sound.h
 * @brief Sound player and recorder device framework.
 */
#include <pjmedia/types.h>
#include <pj/pool.h>

PJ_BEGIN_DECL

/**
 * @defgroup PJMED_SND Sound device abstraction.
 * @ingroup PJMEDIA
 * @{
 */

/** Opaque data type for audio stream. */
typedef struct pjmedia_snd_stream pjmedia_snd_stream;

/**
 * Device information structure returned by #pjmedia_snd_get_dev_info.
 */
typedef struct pjmedia_snd_dev_info
{
    char	name[64];	        /**< Device name.		    */
    unsigned	input_count;	        /**< Max number of input channels.  */
    unsigned	output_count;	        /**< Max number of output channels. */
    unsigned	default_samples_per_sec;/**< Default sampling rate.	    */
} pjmedia_snd_dev_info;

/** 
 * This callback is called by player stream when it needs additional data
 * to be played by the device. Application must fill in the whole of output 
 * buffer with sound samples.
 *
 * @param user_data	User data associated with the stream.
 * @param timestamp	Timestamp, in samples.
 * @param output	Buffer to be filled out by application.
 * @param size		The size requested in bytes, which will be equal to
 *			the size of one whole packet.
 *
 * @return		Non-zero to stop the stream.
 */
typedef pj_status_t (*pjmedia_snd_play_cb)(/* in */   void *user_data,
				      /* in */   pj_uint32_t timestamp,
				      /* out */  void *output,
				      /* out */  unsigned size);

/**
 * This callback is called by recorder stream when it has captured the whole
 * packet worth of audio samples.
 *
 * @param user_data	User data associated with the stream.
 * @param timestamp	Timestamp, in samples.
 * @param output	Buffer containing the captured audio samples.
 * @param size		The size of the data in the buffer, in bytes.
 *
 * @return		Non-zero to stop the stream.
 */
typedef pj_status_t (*pjmedia_snd_rec_cb)(/* in */   void *user_data,
				     /* in */   pj_uint32_t timestamp,
				     /* in */   const void *input,
				     /* in*/    unsigned size);

/**
 * Init the sound library.
 *
 * @param factory	The sound factory.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_init(pj_pool_factory *factory);


/**
 * Get the number of devices detected by the library.
 *
 * @return		Number of devices.
 */
PJ_DECL(int) pjmedia_snd_get_dev_count(void);


/**
 * Get device info.
 *
 * @param index		The index of the device, which should be in the range
 *			from zero to #pjmedia_snd_get_dev_count - 1.
 */
PJ_DECL(const pjmedia_snd_dev_info*) pjmedia_snd_get_dev_info(unsigned index);


/**
 * Create sound stream for both capturing audio and audio playback,  from the 
 * same device. This is the recommended way to create simultaneous recorder 
 * and player streams, because it should work on backends that does not allow
 * a device to be opened more than once.
 *
 * @param rec_id	    Device index for recorder/capture stream, or
 *			    -1 to use the first capable device.
 * @param play_id	    Device index for playback stream, or -1 to use 
 *			    the first capable device.
 * @param clock_rate	    Sound device's clock rate to set.
 * @param channel_count	    Set number of channels, 1 for mono, or 2 for
 *			    stereo. The channel count determines the format
 *			    of the frame.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Set the number of bits per sample. The normal 
 *			    value for this parameter is 16 bits per sample.
 * @param rec_cb	    Callback to handle captured audio samples.
 * @param play_cb	    Callback to be called when the sound player needs
 *			    more audio samples to play.
 * @param user_data	    User data to be associated with the stream.
 * @param p_snd_strm	    Pointer to receive the stream instance.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_open(int rec_id,
				      int play_id,
				      unsigned clock_rate,
				      unsigned channel_count,
				      unsigned samples_per_frame,
				      unsigned bits_per_sample,
				      pjmedia_snd_rec_cb rec_cb,
				      pjmedia_snd_play_cb play_cb,
				      void *user_data,
				      pjmedia_snd_stream **p_snd_strm);


/**
 * Create a unidirectional audio stream for capturing audio samples from
 * the sound device.
 *
 * @param index		    Device index, or -1 to let the library choose the 
 *			    first available device.
 * @param clock_rate	    Sound device's clock rate to set.
 * @param channel_count	    Set number of channels, 1 for mono, or 2 for
 *			    stereo. The channel count determines the format
 *			    of the frame.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Set the number of bits per sample. The normal 
 *			    value for this parameter is 16 bits per sample.
 * @param rec_cb	    Callback to handle captured audio samples.
 * @param user_data	    User data to be associated with the stream.
 * @param p_snd_strm	    Pointer to receive the stream instance.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_open_rec( int index,
					   unsigned clock_rate,
					   unsigned channel_count,
					   unsigned samples_per_frame,
					   unsigned bits_per_sample,
					   pjmedia_snd_rec_cb rec_cb,
					   void *user_data,
					   pjmedia_snd_stream **p_snd_strm);

/**
 * Create a unidirectional audio stream for playing audio samples to the
 * sound device.
 *
 * @param index		    Device index, or -1 to let the library choose the 
 *			    first available device.
 * @param clock_rate	    Sound device's clock rate to set.
 * @param channel_count	    Set number of channels, 1 for mono, or 2 for
 *			    stereo. The channel count determines the format
 *			    of the frame.
 * @param samples_per_frame Number of samples per frame.
 * @param bits_per_sample   Set the number of bits per sample. The normal 
 *			    value for this parameter is 16 bits per sample.
 * @param play_cb	    Callback to be called when the sound player needs
 *			    more audio samples to play.
 * @param user_data	    User data to be associated with the stream.
 * @param p_snd_strm	    Pointer to receive the stream instance.
 *
 * @return		    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_open_player( int index,
					 unsigned clock_rate,
					 unsigned channel_count,
					 unsigned samples_per_frame,
					 unsigned bits_per_sample,
					 pjmedia_snd_play_cb play_cb,
					 void *user_data,
					 pjmedia_snd_stream **p_snd_strm );


/**
 * Start the stream.
 *
 * @param stream	The recorder or player stream.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_stream_start(pjmedia_snd_stream *stream);

/**
 * Stop the stream.
 *
 * @param stream	The recorder or player stream.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_stream_stop(pjmedia_snd_stream *stream);

/**
 * Destroy the stream.
 *
 * @param stream	The recorder of player stream.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_stream_close(pjmedia_snd_stream *stream);

/**
 * Deinitialize sound library.
 *
 * @return		Zero on success.
 */
PJ_DECL(pj_status_t) pjmedia_snd_deinit(void);



/**
 * @}
 */

PJ_END_DECL


#endif	/* __PJMEDIA_SOUND_H__ */
