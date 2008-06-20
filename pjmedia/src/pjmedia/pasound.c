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
#include <pjmedia/sound.h>
#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/string.h>
#include <portaudio.h>

#if PJMEDIA_SOUND_IMPLEMENTATION==PJMEDIA_SOUND_PORTAUDIO_SOUND

#define THIS_FILE	"pasound.c"

static int snd_init_count;

/* Latency settings */
static unsigned snd_input_latency  = PJMEDIA_SND_DEFAULT_REC_LATENCY;
static unsigned snd_output_latency = PJMEDIA_SND_DEFAULT_PLAY_LATENCY;

static struct snd_mgr
{
    pj_pool_factory *factory;
} snd_mgr;

/* 
 * Sound stream descriptor.
 * This struct may be used for both unidirectional or bidirectional sound
 * streams.
 */
struct pjmedia_snd_stream
{
    pj_pool_t		*pool;
    pj_str_t		 name;
    pjmedia_dir		 dir;
    int			 play_id;
    int			 rec_id;
    int			 bytes_per_sample;
    pj_uint32_t		 samples_per_sec;
    unsigned		 samples_per_frame;
    int			 channel_count;

    PaStream		*rec_strm;
    PaStream		*play_strm;

    void		*user_data;
    pjmedia_snd_rec_cb   rec_cb;
    pjmedia_snd_play_cb  play_cb;

    pj_uint32_t		 play_timestamp;
    pj_uint32_t		 rec_timestamp;
    pj_uint32_t		 underflow;
    pj_uint32_t		 overflow;

    pj_bool_t		 quit_flag;

    pj_bool_t		 rec_thread_exited;
    //pj_bool_t		 rec_thread_initialized;
    pj_thread_desc	 rec_thread_desc;
    pj_thread_t		*rec_thread;

    pj_bool_t		 play_thread_exited;
    //pj_bool_t		 play_thread_initialized;
    pj_thread_desc	 play_thread_desc;
    pj_thread_t		*play_thread;

    /* Sometime the record callback does not return framesize as configured
     * (e.g: in OSS), while this module must guarantee returning framesize
     * as configured in the creation settings. In this case, we need a buffer 
     * for the recorded samples.
     */
    pj_int16_t		*rec_buf;
    unsigned		 rec_buf_count;

    /* Sometime the player callback does not request framesize as configured
     * (e.g: in Linux OSS) while sound device will always get samples from 
     * the other component as many as configured samples_per_frame. 
     */
    pj_int16_t		*play_buf;
    unsigned		 play_buf_count;
};


static int PaRecorderCallback(const void *input, 
			      void *output,
			      unsigned long frameCount,
			      const PaStreamCallbackTimeInfo* timeInfo,
			      PaStreamCallbackFlags statusFlags,
			      void *userData )
{
    pjmedia_snd_stream *stream = (pjmedia_snd_stream*) userData;
    pj_status_t status = 0;
    unsigned nsamples;

    PJ_UNUSED_ARG(output);
    PJ_UNUSED_ARG(timeInfo);

    if (stream->quit_flag)
	goto on_break;

    if (input == NULL)
	return paContinue;

    // Sometime the thread, where this callback called from, is changed
    // (e.g: in MacOS this happens when plugging/unplugging headphone)
    // if (stream->rec_thread_initialized == 0) {
    if (!pj_thread_is_registered()) {
	status = pj_thread_register("pa_rec", stream->rec_thread_desc, 
				    &stream->rec_thread);
	//stream->rec_thread_initialized = 1;
	PJ_LOG(5,(THIS_FILE, "Recorder thread started"));
    }

    if (statusFlags & paInputUnderflow)
	++stream->underflow;
    if (statusFlags & paInputOverflow)
	++stream->overflow;

    stream->rec_timestamp += frameCount;

    /* Calculate number of samples we've got */
    nsamples = frameCount * stream->channel_count + stream->rec_buf_count;

    if (nsamples >= stream->samples_per_frame) 
    {
	/* If buffer is not empty, combine the buffer with the just incoming
	 * samples, then call put_frame.
	 */
	if (stream->rec_buf_count) {
	    unsigned chunk_count = 0;
	
	    chunk_count = stream->samples_per_frame - stream->rec_buf_count;
	    pjmedia_copy_samples(stream->rec_buf + stream->rec_buf_count,
				 (pj_int16_t*)input, chunk_count);
	    status = (*stream->rec_cb)(stream->user_data, 
				       stream->rec_timestamp,
				       (void*) stream->rec_buf, 
				       stream->samples_per_frame * 
				       stream->bytes_per_sample);

	    input = (pj_int16_t*) input + chunk_count;
	    nsamples -= stream->samples_per_frame;
	    stream->rec_buf_count = 0;
	}

	/* Give all frames we have */
	while (nsamples >= stream->samples_per_frame && status == 0) {
	    status = (*stream->rec_cb)(stream->user_data, 
				       stream->rec_timestamp,
				       (void*) input, 
				       stream->samples_per_frame * 
				       stream->bytes_per_sample);
	    input = (pj_int16_t*) input + stream->samples_per_frame;
	    nsamples -= stream->samples_per_frame;
	}

	/* Store the remaining samples into the buffer */
	if (nsamples && status == 0) {
	    stream->rec_buf_count = nsamples;
	    pjmedia_copy_samples(stream->rec_buf, (pj_int16_t*)input, 
			         nsamples);
	}

    } else {
	/* Not enough samples, let's just store them in the buffer */
	pjmedia_copy_samples(stream->rec_buf + stream->rec_buf_count,
			     (pj_int16_t*)input, 
			     frameCount * stream->channel_count);
	stream->rec_buf_count += frameCount * stream->channel_count;
    }

    if (status==0) 
	return paContinue;

on_break:
    stream->rec_thread_exited = 1;
    return paAbort;
}

static int PaPlayerCallback( const void *input, 
			     void *output,
			     unsigned long frameCount,
			     const PaStreamCallbackTimeInfo* timeInfo,
			     PaStreamCallbackFlags statusFlags,
			     void *userData )
{
    pjmedia_snd_stream *stream = (pjmedia_snd_stream*) userData;
    pj_status_t status = 0;
    unsigned nsamples_req = frameCount * stream->channel_count;

    PJ_UNUSED_ARG(input);
    PJ_UNUSED_ARG(timeInfo);

    if (stream->quit_flag)
	goto on_break;

    if (output == NULL)
	return paContinue;

    // Sometime the thread, where this callback called from, is changed
    // (e.g: in MacOS this happens when plugging/unplugging headphone)
    // if (stream->play_thread_initialized == 0) {
    if (!pj_thread_is_registered()) {
	status = pj_thread_register("portaudio", stream->play_thread_desc,
				    &stream->play_thread);
	//stream->play_thread_initialized = 1;
	PJ_LOG(5,(THIS_FILE, "Player thread started"));
    }

    if (statusFlags & paOutputUnderflow)
	++stream->underflow;
    if (statusFlags & paOutputOverflow)
	++stream->overflow;

    stream->play_timestamp += frameCount;

    /* Check if any buffered samples */
    if (stream->play_buf_count) {
	/* samples buffered >= requested by sound device */
	if (stream->play_buf_count >= nsamples_req) {
	    pjmedia_copy_samples((pj_int16_t*)output, stream->play_buf, 
				 nsamples_req);
	    stream->play_buf_count -= nsamples_req;
	    pjmedia_move_samples(stream->play_buf, 
				 stream->play_buf + nsamples_req,
				 stream->play_buf_count);
	    nsamples_req = 0;
	    
	    return paContinue;
	}

	/* samples buffered < requested by sound device */
	pjmedia_copy_samples((pj_int16_t*)output, stream->play_buf, 
			     stream->play_buf_count);
	nsamples_req -= stream->play_buf_count;
	output = (pj_int16_t*)output + stream->play_buf_count;
	stream->play_buf_count = 0;
    }

    /* Fill output buffer as requested */
    while (nsamples_req && status == 0) {
	if (nsamples_req >= stream->samples_per_frame) {
	    status = (*stream->play_cb)(stream->user_data, 
					stream->play_timestamp, 
					output, 
					stream->samples_per_frame * 
					stream->bytes_per_sample);
	    nsamples_req -= stream->samples_per_frame;
	    output = (pj_int16_t*)output + stream->samples_per_frame;
	} else {
	    status = (*stream->play_cb)(stream->user_data, 
					stream->play_timestamp, 
					stream->play_buf,
					stream->samples_per_frame * 
					stream->bytes_per_sample);
	    pjmedia_copy_samples((pj_int16_t*)output, stream->play_buf, 
				 nsamples_req);
	    stream->play_buf_count = stream->samples_per_frame - nsamples_req;
	    pjmedia_move_samples(stream->play_buf, 
				 stream->play_buf+nsamples_req,
				 stream->play_buf_count);
	    nsamples_req = 0;
	}
    }
    
    if (status==0) 
	return paContinue;

on_break:
    stream->play_thread_exited = 1;
    return paAbort;
}


static int PaRecorderPlayerCallback( const void *input, 
				     void *output,
				     unsigned long frameCount,
				     const PaStreamCallbackTimeInfo* timeInfo,
				     PaStreamCallbackFlags statusFlags,
				     void *userData )
{
    int rc;

    rc = PaRecorderCallback(input, output, frameCount, timeInfo,
			    statusFlags, userData);
    if (rc != paContinue)
	return rc;

    rc = PaPlayerCallback(input, output, frameCount, timeInfo,
			  statusFlags, userData);
    return rc;
}

/* Logging callback from PA */
static void pa_log_cb(const char *log)
{
    PJ_LOG(5,(THIS_FILE, "PA message: %s", log));
}

/* We should include pa_debugprint.h for this, but the header
 * is not available publicly. :(
 */
typedef void (*PaUtilLogCallback ) (const char *log);
void PaUtil_SetDebugPrintFunction(PaUtilLogCallback  cb);

/*
 * Init sound library.
 */
PJ_DEF(pj_status_t) pjmedia_snd_init(pj_pool_factory *factory)
{
    if (++snd_init_count == 1) {
	int err;

	PaUtil_SetDebugPrintFunction(&pa_log_cb);

	snd_mgr.factory = factory;
	err = Pa_Initialize();

	PJ_LOG(4,(THIS_FILE, 
		  "PortAudio sound library initialized, status=%d", err));
	PJ_LOG(4,(THIS_FILE, "PortAudio host api count=%d",
			     Pa_GetHostApiCount()));
	PJ_LOG(4,(THIS_FILE, "Sound device count=%d",
			     pjmedia_snd_get_dev_count()));

	return err ? PJMEDIA_ERRNO_FROM_PORTAUDIO(err) : PJ_SUCCESS;
    } else {
	return PJ_SUCCESS;
    }
}


/*
 * Get device count.
 */
PJ_DEF(int) pjmedia_snd_get_dev_count(void)
{
    int count = Pa_GetDeviceCount();
    return count < 0 ? 0 : count;
}


/*
 * Get device info.
 */
PJ_DEF(const pjmedia_snd_dev_info*) pjmedia_snd_get_dev_info(unsigned index)
{
    static pjmedia_snd_dev_info info;
    const PaDeviceInfo *pa_info;

    pa_info = Pa_GetDeviceInfo(index);
    if (!pa_info)
	return NULL;

    pj_bzero(&info, sizeof(info));
    strncpy(info.name, pa_info->name, sizeof(info.name));
    info.name[sizeof(info.name)-1] = '\0';
    info.input_count = pa_info->maxInputChannels;
    info.output_count = pa_info->maxOutputChannels;
    info.default_samples_per_sec = (unsigned)pa_info->defaultSampleRate;

    return &info;
}


/* Get PortAudio default input device ID */
static int pa_get_default_input_dev(int channel_count)
{
    int i, count;

    /* Special for Windows - try to use the DirectSound implementation
     * first since it provides better latency.
     */
#if PJMEDIA_PREFER_DIRECT_SOUND
    if (Pa_HostApiTypeIdToHostApiIndex(paDirectSound) >= 0) {
	const PaHostApiInfo *pHI;
	int index = Pa_HostApiTypeIdToHostApiIndex(paDirectSound);
	pHI = Pa_GetHostApiInfo(index);
	if (pHI) {
	    const PaDeviceInfo *paDevInfo = NULL;
	    paDevInfo = Pa_GetDeviceInfo(pHI->defaultInputDevice);
	    if (paDevInfo && paDevInfo->maxInputChannels >= channel_count)
		return pHI->defaultInputDevice;
	}
    }
#endif

    /* Enumerate the host api's for the default devices, and return
     * the device with suitable channels.
     */
    count = Pa_GetHostApiCount();
    for (i=0; i < count; ++i) {
	const PaHostApiInfo *pHAInfo;

	pHAInfo = Pa_GetHostApiInfo(i);
	if (!pHAInfo)
	    continue;

	if (pHAInfo->defaultInputDevice >= 0) {
	    const PaDeviceInfo *paDevInfo;

	    paDevInfo = Pa_GetDeviceInfo(pHAInfo->defaultInputDevice);

	    if (paDevInfo->maxInputChannels >= channel_count)
		return pHAInfo->defaultInputDevice;
	}
    }

    /* If still no device is found, enumerate all devices */
    count = pjmedia_snd_get_dev_count();
    for (i=0; i<count; ++i) {
	const PaDeviceInfo *paDevInfo;

	paDevInfo = Pa_GetDeviceInfo(i);
	if (paDevInfo->maxInputChannels >= channel_count)
	    return i;
    }
    
    return -1;
}

/* Get PortAudio default output device ID */
static int pa_get_default_output_dev(int channel_count)
{
    int i, count;

    /* Special for Windows - try to use the DirectSound implementation
     * first since it provides better latency.
     */
#if PJMEDIA_PREFER_DIRECT_SOUND
    if (Pa_HostApiTypeIdToHostApiIndex(paDirectSound) >= 0) {
	const PaHostApiInfo *pHI;
	int index = Pa_HostApiTypeIdToHostApiIndex(paDirectSound);
	pHI = Pa_GetHostApiInfo(index);
	if (pHI) {
	    const PaDeviceInfo *paDevInfo = NULL;
	    paDevInfo = Pa_GetDeviceInfo(pHI->defaultOutputDevice);
	    if (paDevInfo && paDevInfo->maxOutputChannels >= channel_count)
		return pHI->defaultOutputDevice;
	}
    }
#endif

    /* Enumerate the host api's for the default devices, and return
     * the device with suitable channels.
     */
    count = Pa_GetHostApiCount();
    for (i=0; i < count; ++i) {
	const PaHostApiInfo *pHAInfo;

	pHAInfo = Pa_GetHostApiInfo(i);
	if (!pHAInfo)
	    continue;

	if (pHAInfo->defaultOutputDevice >= 0) {
	    const PaDeviceInfo *paDevInfo;

	    paDevInfo = Pa_GetDeviceInfo(pHAInfo->defaultOutputDevice);

	    if (paDevInfo->maxOutputChannels >= channel_count)
		return pHAInfo->defaultOutputDevice;
	}
    }

    /* If still no device is found, enumerate all devices */
    count = pjmedia_snd_get_dev_count();
    for (i=0; i<count; ++i) {
	const PaDeviceInfo *paDevInfo;

	paDevInfo = Pa_GetDeviceInfo(i);
	if (paDevInfo->maxOutputChannels >= channel_count)
	    return i;
    }

    return -1;
}


/*
 * Open stream.
 */
PJ_DEF(pj_status_t) pjmedia_snd_open_rec( int index,
					  unsigned clock_rate,
					  unsigned channel_count,
					  unsigned samples_per_frame,
					  unsigned bits_per_sample,
					  pjmedia_snd_rec_cb rec_cb,
					  void *user_data,
					  pjmedia_snd_stream **p_snd_strm)
{
    pj_pool_t *pool;
    pjmedia_snd_stream *stream;
    PaStreamParameters inputParam;
    int sampleFormat;
    const PaDeviceInfo *paDevInfo = NULL;
    const PaHostApiInfo *paHostApiInfo = NULL;
    unsigned paFrames, paRate, paLatency;
    const PaStreamInfo *paSI;
    PaError err;

    if (index < 0) {
	index = pa_get_default_input_dev(channel_count);
	if (index < 0) {
	    /* No such device. */
	    return PJMEDIA_ENOSNDREC;
	}
    }

    paDevInfo = Pa_GetDeviceInfo(index);
    if (!paDevInfo) {
	/* Assumed it is "No such device" error. */
	return PJMEDIA_ESNDINDEVID;
    }

    if (bits_per_sample == 8)
	sampleFormat = paUInt8;
    else if (bits_per_sample == 16)
	sampleFormat = paInt16;
    else if (bits_per_sample == 32)
	sampleFormat = paInt32;
    else
	return PJMEDIA_ESNDINSAMPLEFMT;
    
    pool = pj_pool_create( snd_mgr.factory, "sndstream", 1024, 1024, NULL);
    if (!pool)
	return PJ_ENOMEM;

    stream = PJ_POOL_ZALLOC_T(pool, pjmedia_snd_stream);
    stream->pool = pool;
    pj_strdup2_with_null(pool, &stream->name, paDevInfo->name);
    stream->dir = PJMEDIA_DIR_CAPTURE;
    stream->rec_id = index;
    stream->play_id = -1;
    stream->user_data = user_data;
    stream->samples_per_sec = clock_rate;
    stream->samples_per_frame = samples_per_frame;
    stream->bytes_per_sample = bits_per_sample / 8;
    stream->channel_count = channel_count;
    stream->rec_cb = rec_cb;

    stream->rec_buf = (pj_int16_t*)pj_pool_alloc(pool, 
		      stream->samples_per_frame * stream->bytes_per_sample);
    stream->rec_buf_count = 0;

    pj_bzero(&inputParam, sizeof(inputParam));
    inputParam.device = index;
    inputParam.channelCount = channel_count;
    inputParam.hostApiSpecificStreamInfo = NULL;
    inputParam.sampleFormat = sampleFormat;
    inputParam.suggestedLatency = snd_input_latency / 1000.0;

    paHostApiInfo = Pa_GetHostApiInfo(paDevInfo->hostApi);

    /* Frames in PortAudio is number of samples in a single channel */
    paFrames = samples_per_frame / channel_count;

    err = Pa_OpenStream( &stream->rec_strm, &inputParam, NULL,
			 clock_rate, paFrames, 
			 paClipOff, &PaRecorderCallback, stream );
    if (err != paNoError) {
	pj_pool_release(pool);
	return PJMEDIA_ERRNO_FROM_PORTAUDIO(err);
    }

    paSI = Pa_GetStreamInfo(stream->rec_strm);
    paRate = (unsigned)paSI->sampleRate;
    paLatency = (unsigned)(paSI->inputLatency * 1000);

    PJ_LOG(5,(THIS_FILE, "Opened device %s (%s) for recording, sample "
			 "rate=%d, ch=%d, "
			 "bits=%d, %d samples per frame, latency=%d ms",
			 paDevInfo->name, paHostApiInfo->name,
			 paRate, channel_count,
			 bits_per_sample, samples_per_frame,
			 paLatency));

    *p_snd_strm = stream;
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_snd_open_player( int index,
					unsigned clock_rate,
					unsigned channel_count,
					unsigned samples_per_frame,
					unsigned bits_per_sample,
					pjmedia_snd_play_cb play_cb,
					void *user_data,
					pjmedia_snd_stream **p_snd_strm)
{
    pj_pool_t *pool;
    pjmedia_snd_stream *stream;
    PaStreamParameters outputParam;
    int sampleFormat;
    const PaDeviceInfo *paDevInfo = NULL;
    const PaHostApiInfo *paHostApiInfo = NULL;
    const PaStreamInfo *paSI;
    unsigned paFrames, paRate, paLatency;
    PaError err;

    if (index < 0) {
	index = pa_get_default_output_dev(channel_count);
	if (index < 0) {
	    /* No such device. */
	    return PJMEDIA_ENOSNDPLAY;
	}
    } 

    paDevInfo = Pa_GetDeviceInfo(index);
    if (!paDevInfo) {
	/* Assumed it is "No such device" error. */
	return PJMEDIA_ESNDINDEVID;
    }

    if (bits_per_sample == 8)
	sampleFormat = paUInt8;
    else if (bits_per_sample == 16)
	sampleFormat = paInt16;
    else if (bits_per_sample == 32)
	sampleFormat = paInt32;
    else
	return PJMEDIA_ESNDINSAMPLEFMT;
    
    pool = pj_pool_create( snd_mgr.factory, "sndstream", 1024, 1024, NULL);
    if (!pool)
	return PJ_ENOMEM;

    stream = PJ_POOL_ZALLOC_T(pool, pjmedia_snd_stream);
    stream->pool = pool;
    pj_strdup2_with_null(pool, &stream->name, paDevInfo->name);
    stream->dir = PJMEDIA_DIR_PLAYBACK;
    stream->play_id = index;
    stream->rec_id = -1;
    stream->user_data = user_data;
    stream->samples_per_sec = clock_rate;
    stream->samples_per_frame = samples_per_frame;
    stream->bytes_per_sample = bits_per_sample / 8;
    stream->channel_count = channel_count;
    stream->play_cb = play_cb;

    stream->play_buf = (pj_int16_t*)pj_pool_alloc(pool, 
		       stream->samples_per_frame * stream->bytes_per_sample);
    stream->play_buf_count = 0;

    pj_bzero(&outputParam, sizeof(outputParam));
    outputParam.device = index;
    outputParam.channelCount = channel_count;
    outputParam.hostApiSpecificStreamInfo = NULL;
    outputParam.sampleFormat = sampleFormat;
    outputParam.suggestedLatency = snd_output_latency / 1000.0;

    paHostApiInfo = Pa_GetHostApiInfo(paDevInfo->hostApi);

    /* Frames in PortAudio is number of samples in a single channel */
    paFrames = samples_per_frame / channel_count;

    err = Pa_OpenStream( &stream->play_strm, NULL, &outputParam,
			 clock_rate,  paFrames, 
			 paClipOff, &PaPlayerCallback, stream );
    if (err != paNoError) {
	pj_pool_release(pool);
	return PJMEDIA_ERRNO_FROM_PORTAUDIO(err);
    }

    paSI = Pa_GetStreamInfo(stream->play_strm);
    paRate = (unsigned)(paSI->sampleRate);
    paLatency = (unsigned)(paSI->outputLatency * 1000);

    PJ_LOG(5,(THIS_FILE, "Opened device %d: %s(%s) for playing, sample rate=%d"
			 ", ch=%d, "
			 "bits=%d, %d samples per frame, latency=%d ms",
			 index, paDevInfo->name, paHostApiInfo->name, 
			 paRate, channel_count,
		 	 bits_per_sample, samples_per_frame, paLatency));

    *p_snd_strm = stream;

    return PJ_SUCCESS;
}


/*
 * Open both player and recorder.
 */
PJ_DEF(pj_status_t) pjmedia_snd_open( int rec_id,
				      int play_id,
				      unsigned clock_rate,
				      unsigned channel_count,
				      unsigned samples_per_frame,
				      unsigned bits_per_sample,
				      pjmedia_snd_rec_cb rec_cb,
				      pjmedia_snd_play_cb play_cb,
				      void *user_data,
				      pjmedia_snd_stream **p_snd_strm)
{
    pj_pool_t *pool;
    pjmedia_snd_stream *stream;
    PaStream *paStream = NULL;
    PaStreamParameters inputParam;
    PaStreamParameters outputParam;
    int sampleFormat;
    const PaDeviceInfo *paRecDevInfo = NULL;
    const PaDeviceInfo *paPlayDevInfo = NULL;
    const PaHostApiInfo *paRecHostApiInfo = NULL;
    const PaHostApiInfo *paPlayHostApiInfo = NULL;
    const PaStreamInfo *paSI;
    unsigned paFrames, paRate, paInputLatency, paOutputLatency;
    PaError err;

    if (rec_id < 0) {
	rec_id = pa_get_default_input_dev(channel_count);
	if (rec_id < 0) {
	    /* No such device. */
	    return PJMEDIA_ENOSNDREC;
	}
    }

    paRecDevInfo = Pa_GetDeviceInfo(rec_id);
    if (!paRecDevInfo) {
	/* Assumed it is "No such device" error. */
	return PJMEDIA_ESNDINDEVID;
    }

    if (play_id < 0) {
	play_id = pa_get_default_output_dev(channel_count);
	if (play_id < 0) {
	    /* No such device. */
	    return PJMEDIA_ENOSNDPLAY;
	}
    } 

    paPlayDevInfo = Pa_GetDeviceInfo(play_id);
    if (!paPlayDevInfo) {
	/* Assumed it is "No such device" error. */
	return PJMEDIA_ESNDINDEVID;
    }


    if (bits_per_sample == 8)
	sampleFormat = paUInt8;
    else if (bits_per_sample == 16)
	sampleFormat = paInt16;
    else if (bits_per_sample == 32)
	sampleFormat = paInt32;
    else
	return PJMEDIA_ESNDINSAMPLEFMT;
    
    pool = pj_pool_create( snd_mgr.factory, "sndstream", 1024, 1024, NULL);
    if (!pool)
	return PJ_ENOMEM;

    stream = PJ_POOL_ZALLOC_T(pool, pjmedia_snd_stream);
    stream->pool = pool;
    pj_strdup2_with_null(pool, &stream->name, paRecDevInfo->name);
    stream->dir = PJMEDIA_DIR_CAPTURE_PLAYBACK;
    stream->play_id = play_id;
    stream->rec_id = rec_id;
    stream->user_data = user_data;
    stream->samples_per_sec = clock_rate;
    stream->samples_per_frame = samples_per_frame;
    stream->bytes_per_sample = bits_per_sample / 8;
    stream->channel_count = channel_count;
    stream->rec_cb = rec_cb;
    stream->play_cb = play_cb;

    stream->rec_buf = (pj_int16_t*)pj_pool_alloc(pool, 
		      stream->samples_per_frame * stream->bytes_per_sample);
    stream->rec_buf_count = 0;

    stream->play_buf = (pj_int16_t*)pj_pool_alloc(pool, 
		       stream->samples_per_frame * stream->bytes_per_sample);
    stream->play_buf_count = 0;

    pj_bzero(&inputParam, sizeof(inputParam));
    inputParam.device = rec_id;
    inputParam.channelCount = channel_count;
    inputParam.hostApiSpecificStreamInfo = NULL;
    inputParam.sampleFormat = sampleFormat;
    inputParam.suggestedLatency = snd_input_latency / 1000.0;

    paRecHostApiInfo = Pa_GetHostApiInfo(paRecDevInfo->hostApi);

    pj_bzero(&outputParam, sizeof(outputParam));
    outputParam.device = play_id;
    outputParam.channelCount = channel_count;
    outputParam.hostApiSpecificStreamInfo = NULL;
    outputParam.sampleFormat = sampleFormat;
    outputParam.suggestedLatency = snd_output_latency / 1000.0;

    paPlayHostApiInfo = Pa_GetHostApiInfo(paPlayDevInfo->hostApi);

    /* Frames in PortAudio is number of samples in a single channel */
    paFrames = samples_per_frame / channel_count;

    /* If both input and output are on the same device, open a single stream
     * for both input and output.
     */
    if (rec_id == play_id) {
	err = Pa_OpenStream( &paStream, &inputParam, &outputParam,
			     clock_rate, paFrames, 
			     paClipOff, &PaRecorderPlayerCallback, stream );
	if (err == paNoError) {
	    /* Set play stream and record stream to the same stream */
	    stream->play_strm = stream->rec_strm = paStream;
	}
    } else {
	err = -1;
    }

    /* .. otherwise if input and output are on the same device, OR if we're
     * unable to open a bidirectional stream, then open two separate
     * input and output stream.
     */
    if (paStream == NULL) {
	/* Open input stream */
	err = Pa_OpenStream( &stream->rec_strm, &inputParam, NULL,
			     clock_rate, paFrames, 
			     paClipOff, &PaRecorderCallback, stream );
	if (err == paNoError) {
	    /* Open output stream */
	    err = Pa_OpenStream( &stream->play_strm, NULL, &outputParam,
				 clock_rate, paFrames, 
				 paClipOff, &PaPlayerCallback, stream );
	    if (err != paNoError)
		Pa_CloseStream(stream->rec_strm);
	}
    }

    if (err != paNoError) {
	pj_pool_release(pool);
	return PJMEDIA_ERRNO_FROM_PORTAUDIO(err);
    }

    paSI = Pa_GetStreamInfo(stream->rec_strm);
    paRate = (unsigned)(paSI->sampleRate);
    paInputLatency = (unsigned)(paSI->inputLatency * 1000);
    paSI = Pa_GetStreamInfo(stream->play_strm);
    paOutputLatency = (unsigned)(paSI->outputLatency * 1000);

    PJ_LOG(5,(THIS_FILE, "Opened device %s(%s)/%s(%s) for recording and "
			 "playback, sample rate=%d, ch=%d, "
			 "bits=%d, %d samples per frame, input latency=%d ms, "
			 "output latency=%d ms",
			 paRecDevInfo->name, paRecHostApiInfo->name,
			 paPlayDevInfo->name, paPlayHostApiInfo->name,
			 paRate, channel_count,
			 bits_per_sample, samples_per_frame,
			 paInputLatency, paOutputLatency));

    *p_snd_strm = stream;


    return PJ_SUCCESS;
}


/*
 * Get stream info.
 */
PJ_DEF(pj_status_t) pjmedia_snd_stream_get_info(pjmedia_snd_stream *strm,
						pjmedia_snd_stream_info *pi)
{
    const PaStreamInfo *paPlaySI = NULL, *paRecSI = NULL;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);
    PJ_ASSERT_RETURN(strm->play_strm || strm->rec_strm, PJ_EINVALIDOP);

    if (strm->play_strm) {
	paPlaySI = Pa_GetStreamInfo(strm->play_strm);
    }
    if (strm->rec_strm) {
	paRecSI = Pa_GetStreamInfo(strm->rec_strm);
    }

    pj_bzero(pi, sizeof(*pi));
    pi->dir = strm->dir;
    pi->play_id = strm->play_id;
    pi->rec_id = strm->rec_id;
    pi->clock_rate = (unsigned)(paPlaySI ? paPlaySI->sampleRate : 
				paRecSI->sampleRate);
    pi->channel_count = strm->channel_count;
    pi->samples_per_frame = strm->samples_per_frame;
    pi->bits_per_sample = strm->bytes_per_sample * 8;
    pi->rec_latency = (unsigned)(paRecSI ? paRecSI->inputLatency * 
					   paRecSI->sampleRate : 0);
    pi->play_latency = (unsigned)(paPlaySI ? paPlaySI->outputLatency * 
					     paPlaySI->sampleRate : 0);

    return PJ_SUCCESS;
}


/*
 * Start stream.
 */
PJ_DEF(pj_status_t) pjmedia_snd_stream_start(pjmedia_snd_stream *stream)
{
    int err = 0;

    PJ_LOG(5,(THIS_FILE, "Starting %s stream..", stream->name.ptr));

    if (stream->play_strm)
	err = Pa_StartStream(stream->play_strm);

    if (err==0 && stream->rec_strm && stream->rec_strm != stream->play_strm) {
	err = Pa_StartStream(stream->rec_strm);
	if (err != 0)
	    Pa_StopStream(stream->play_strm);
    }

    PJ_LOG(5,(THIS_FILE, "Done, status=%d", err));

    return err ? PJMEDIA_ERRNO_FROM_PORTAUDIO(err) : PJ_SUCCESS;
}

/*
 * Stop stream.
 */
PJ_DEF(pj_status_t) pjmedia_snd_stream_stop(pjmedia_snd_stream *stream)
{
    int i, err = 0;

    stream->quit_flag = 1;
    for (i=0; !stream->rec_thread_exited && i<100; ++i)
	pj_thread_sleep(10);
    for (i=0; !stream->play_thread_exited && i<100; ++i)
	pj_thread_sleep(10);

    pj_thread_sleep(1);

    PJ_LOG(5,(THIS_FILE, "Stopping stream.."));

    if (stream->play_strm)
	err = Pa_StopStream(stream->play_strm);

    if (stream->rec_strm && stream->rec_strm != stream->play_strm)
	err = Pa_StopStream(stream->rec_strm);

    PJ_LOG(5,(THIS_FILE, "Done, status=%d", err));

    return err ? PJMEDIA_ERRNO_FROM_PORTAUDIO(err) : PJ_SUCCESS;
}

/*
 * Destroy stream.
 */
PJ_DEF(pj_status_t) pjmedia_snd_stream_close(pjmedia_snd_stream *stream)
{
    int i, err = 0;

    stream->quit_flag = 1;
    for (i=0; !stream->rec_thread_exited && i<100; ++i) {
	pj_thread_sleep(1);
    }
    for (i=0; !stream->play_thread_exited && i<100; ++i) {
	pj_thread_sleep(1);
    }

    PJ_LOG(5,(THIS_FILE, "Closing %.*s: %lu underflow, %lu overflow",
			 (int)stream->name.slen,
			 stream->name.ptr,
			 stream->underflow, stream->overflow));

    if (stream->play_strm)
	err = Pa_CloseStream(stream->play_strm);

    if (stream->rec_strm && stream->rec_strm != stream->play_strm)
	err = Pa_CloseStream(stream->rec_strm);

    pj_pool_release(stream->pool);

    return err ? PJMEDIA_ERRNO_FROM_PORTAUDIO(err) : PJ_SUCCESS;
}

/*
 * Deinitialize sound library.
 */
PJ_DEF(pj_status_t) pjmedia_snd_deinit(void)
{
    if (--snd_init_count == 0) {
	int err;

	PJ_LOG(4,(THIS_FILE, "PortAudio sound library shutting down.."));

	err = Pa_Terminate();

	return err ? PJMEDIA_ERRNO_FROM_PORTAUDIO(err) : PJ_SUCCESS;
    } else {
	return PJ_SUCCESS;
    }
}

/*
 * Set sound latency.
 */
PJ_DEF(pj_status_t) pjmedia_snd_set_latency(unsigned input_latency, 
					    unsigned output_latency)
{
    snd_input_latency  = (input_latency == 0)? 
			 PJMEDIA_SND_DEFAULT_REC_LATENCY : input_latency;
    snd_output_latency = (output_latency == 0)? 
			 PJMEDIA_SND_DEFAULT_PLAY_LATENCY : output_latency;

    return PJ_SUCCESS;
}

#endif	/* PJMEDIA_SOUND_IMPLEMENTATION */
