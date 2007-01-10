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
#include <pjmedia/sound.h>
#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/os.h>


/*
 * This file provides sound implementation for Symbian Audio Streaming
 * device. Application using this sound abstraction must link with:
 *  - mediaclientaudiostream.lib, and
 *  - mediaclientaudioinputstream.lib 
 */
#include <mda/common/audio.h>
#include <MdaAudioOutputStream.h>
#include <MdaAudioInputStream.h>



//////////////////////////////////////////////////////////////////////////////
//

#define THIS_FILE	    "symbian_sound.cpp"
#define BYTES_PER_SAMPLE    2
#define POOL_NAME	    "SymbianSound"
#define POOL_SIZE	    512
#define POOL_INC	    512

static pjmedia_snd_dev_info symbian_snd_dev_info = 
{
    "Symbian Sound Device",
    1,
    1,
    8000
};

class CPjAudioInputEngine;
class CPjAudioOutputEngine;

/* 
 * PJMEDIA Sound Stream instance 
 */
struct pjmedia_snd_stream
{
    // Pool
    pj_pool_t			*pool;

    // Common settings.
    unsigned			 clock_rate;
    unsigned			 channel_count;
    unsigned			 samples_per_frame;

    // Input stream
    CPjAudioInputEngine		*inEngine;

    // Output stream
    CPjAudioOutputEngine	*outEngine;
};

static pj_pool_factory *snd_pool_factory;


/*
 * Convert clock rate to Symbian's TMdaAudioDataSettings capability.
 */
static TInt get_clock_rate_cap(unsigned clock_rate)
{
    switch (clock_rate) {
    case 8000:  return TMdaAudioDataSettings::ESampleRate8000Hz;
    case 11025: return TMdaAudioDataSettings::ESampleRate11025Hz;
    case 12000: return TMdaAudioDataSettings::ESampleRate12000Hz;
    case 16000: return TMdaAudioDataSettings::ESampleRate16000Hz;
    case 22050: return TMdaAudioDataSettings::ESampleRate22050Hz;
    case 24000: return TMdaAudioDataSettings::ESampleRate24000Hz;
    case 32000: return TMdaAudioDataSettings::ESampleRate32000Hz;
    case 44100: return TMdaAudioDataSettings::ESampleRate44100Hz;
    case 48000: return TMdaAudioDataSettings::ESampleRate48000Hz;
    case 64000: return TMdaAudioDataSettings::ESampleRate64000Hz;
    case 96000: return TMdaAudioDataSettings::ESampleRate96000Hz;
    default:
	return 0;
    }
}


/*
 * Convert number of channels into Symbian's TMdaAudioDataSettings capability.
 */
static TInt get_channel_cap(unsigned channel_count)
{
    switch (channel_count) {
    case 1: return TMdaAudioDataSettings::EChannelsMono;
    case 2: return TMdaAudioDataSettings::EChannelsStereo;
    default:
	return 0;
    }
}


//////////////////////////////////////////////////////////////////////////////
//

/*
 * Implementation: Symbian Input Stream.
 */
class CPjAudioInputEngine : public MMdaAudioInputStreamCallback
{
public:
    enum State
    {
	STATE_INACTIVE,
	STATE_ACTIVE,
    };

    ~CPjAudioInputEngine();

    static CPjAudioInputEngine *NewL(pjmedia_snd_stream *parent_strm,
				     pjmedia_snd_rec_cb rec_cb,
				     void *user_data);

    static CPjAudioInputEngine *NewLC(pjmedia_snd_stream *parent_strm,
				      pjmedia_snd_rec_cb rec_cb,
				      void *user_data);

    pj_status_t StartRecord();
    void Stop();

public:
    State		     state_;
    pjmedia_snd_stream	    *parentStrm_;
    pjmedia_snd_rec_cb	     recCb_;
    void		    *userData_;
    CMdaAudioInputStream    *iInputStream_;
    HBufC8		    *iStreamBuffer_;
    TPtr8		     iFramePtr_;
    TInt		     lastError_;
    pj_uint32_t		     timeStamp_;

    CPjAudioInputEngine(pjmedia_snd_stream *parent_strm,
			pjmedia_snd_rec_cb rec_cb,
			void *user_data);
    void ConstructL();
    
public:
    virtual void MaiscOpenComplete(TInt aError);
    virtual void MaiscBufferCopied(TInt aError, const TDesC8 &aBuffer);
    virtual void MaiscRecordComplete(TInt aError);

};


CPjAudioInputEngine::CPjAudioInputEngine(pjmedia_snd_stream *parent_strm,
					 pjmedia_snd_rec_cb rec_cb,
					 void *user_data)
    : state_(STATE_INACTIVE), parentStrm_(parent_strm), recCb_(rec_cb), 
      iInputStream_(NULL), iStreamBuffer_(NULL), iFramePtr_(NULL, 0),
      userData_(user_data), lastError_(KErrNone), timeStamp_(0)
{
}

CPjAudioInputEngine::~CPjAudioInputEngine()
{
    Stop();
    delete iStreamBuffer_;
}

void CPjAudioInputEngine::ConstructL()
{
    iStreamBuffer_ = HBufC8::NewMaxL(parentStrm_->samples_per_frame *
				     parentStrm_->channel_count * 
				     BYTES_PER_SAMPLE);
}

CPjAudioInputEngine *CPjAudioInputEngine::NewLC(pjmedia_snd_stream *parent,
					        pjmedia_snd_rec_cb rec_cb,
					        void *user_data)
{
    CPjAudioInputEngine* self = new (ELeave) CPjAudioInputEngine(parent,
								 rec_cb, 
								 user_data);
    CleanupStack::PushL(self);
    self->ConstructL();
    return self;
}

CPjAudioInputEngine *CPjAudioInputEngine::NewL(pjmedia_snd_stream *parent,
					       pjmedia_snd_rec_cb rec_cb,
					       void *user_data)
{
    CPjAudioInputEngine *self = NewLC(parent, rec_cb, user_data);
    CleanupStack::Pop(self);
    return self;
}


pj_status_t CPjAudioInputEngine::StartRecord()
{

    // Ignore command if recording is in progress.
    if (state_ == STATE_ACTIVE)
	return PJ_SUCCESS;

    // According to Nokia's AudioStream example, some 2nd Edition, FP2 devices
    // (such as Nokia 6630) require the stream to be reconstructed each time 
    // before calling Open() - otherwise the callback never gets called.
    // For uniform behavior, lets just delete/re-create the stream for all
    // devices.

    // Destroy existing stream.
    if (iInputStream_) delete iInputStream_;
    iInputStream_ = NULL;

    // Create the stream.
    TRAPD(err, iInputStream_ = CMdaAudioInputStream::NewL(*this));
    if (err != KErrNone)
	return PJ_RETURN_OS_ERROR(err);

    // Initialize settings.
    TMdaAudioDataSettings iStreamSettings;
    iStreamSettings.iChannels = get_channel_cap(parentStrm_->channel_count);
    iStreamSettings.iSampleRate = get_clock_rate_cap(parentStrm_->clock_rate);

    pj_assert(iStreamSettings.iChannels != 0 && 
	      iStreamSettings.iSampleRate != 0);

    // Create timeout timer to wait for Open to complete
    RTimer timer;
    TRequestStatus reqStatus;
    TInt rc;
    
    rc = timer.CreateLocal();
    if (rc != KErrNone) {
    	delete iInputStream_;
	iInputStream_ = NULL;
	return PJ_RETURN_OS_ERROR(rc);
    }

    PJ_LOG(4,(THIS_FILE, "Opening sound device for capture, "
    		         "clock rate=%d, channel count=%d..",
    		         parentStrm_->clock_rate, 
    		         parentStrm_->channel_count));
    
    // Open stream.
    lastError_ = KRequestPending;
    iInputStream_->Open(&iStreamSettings);

    // Wait until callback is called.
    if (lastError_ == KRequestPending) {
	timer.After(reqStatus, 5 * 1000 * 1000);

	do {
	    User::WaitForAnyRequest();
	} while (lastError_==KRequestPending && reqStatus==KRequestPending);
	
	if (reqStatus==KRequestPending)
	    timer.Cancel();
    }

    // Close timer
    timer.Close();
    
    // Handle timeout
    if (lastError_ == KRequestPending) {
    	iInputStream_->Stop();
    	delete iInputStream_;
	iInputStream_ = NULL;
	return PJ_ETIMEDOUT;
    }
    else if (lastError_ != KErrNone) {
    	// Handle failure.
	delete iInputStream_;
	iInputStream_ = NULL;
	return PJ_RETURN_OS_ERROR(lastError_);
    }

    // Feed the first frame.
    iFramePtr_ = iStreamBuffer_->Des();
    iInputStream_->ReadL(iFramePtr_);

    // Success
    PJ_LOG(4,(THIS_FILE, "Sound capture started."));
    return PJ_SUCCESS;
}


void CPjAudioInputEngine::Stop()
{
    // If capture is in progress, stop it.
    if (iInputStream_ && state_ == STATE_ACTIVE) {
    	lastError_ = KRequestPending;
    	iInputStream_->Stop();

	// Wait until it's actually stopped
    	while (lastError_ == KRequestPending)
	    pj_thread_sleep(100);
    }

    if (iInputStream_) {
	delete iInputStream_;
	iInputStream_ = NULL;
    }
    
    state_ = STATE_INACTIVE;
}


void CPjAudioInputEngine::MaiscOpenComplete(TInt aError)
{
    lastError_ = aError;
}

void CPjAudioInputEngine::MaiscBufferCopied(TInt aError, 
					    const TDesC8 &aBuffer)
{
    lastError_ = aError;
    if (aError != KErrNone)
	return;

    // Call the callback.
    recCb_(userData_, timeStamp_, aBuffer.Ptr(), aBuffer.Size());

    // Increment timestamp.
    timeStamp_ += (aBuffer.Size() * BYTES_PER_SAMPLE);

    // Record next frame
    iFramePtr_ = iStreamBuffer_->Des();
    iInputStream_->ReadL(iFramePtr_);
}


void CPjAudioInputEngine::MaiscRecordComplete(TInt aError)
{
    lastError_ = aError;
    state_ = STATE_INACTIVE;
}



//////////////////////////////////////////////////////////////////////////////
//

/*
 * Implementation: Symbian Output Stream.
 */

class CPjAudioOutputEngine : public MMdaAudioOutputStreamCallback
{
public:
    enum State
    {
	STATE_INACTIVE,
	STATE_ACTIVE,
    };

    ~CPjAudioOutputEngine();

    static CPjAudioOutputEngine *NewL(pjmedia_snd_stream *parent_strm,
				      pjmedia_snd_play_cb play_cb,
				      void *user_data);

    static CPjAudioOutputEngine *NewLC(pjmedia_snd_stream *parent_strm,
				       pjmedia_snd_play_cb rec_cb,
				       void *user_data);

    pj_status_t StartPlay();
    void Stop();

public:
    State		     state_;
    pjmedia_snd_stream	    *parentStrm_;
    pjmedia_snd_play_cb	     playCb_;
    void		    *userData_;
    CMdaAudioOutputStream   *iOutputStream_;
    TUint8		    *frameBuf_;
    unsigned		     frameBufSize_;
    TInt		     lastError_;
    unsigned		     timestamp_;

    CPjAudioOutputEngine(pjmedia_snd_stream *parent_strm,
			 pjmedia_snd_play_cb play_cb,
			 void *user_data);
    void ConstructL();

    virtual void MaoscOpenComplete(TInt aError);
    virtual void MaoscBufferCopied(TInt aError, const TDesC8& aBuffer);
    virtual void MaoscPlayComplete(TInt aError);
};


CPjAudioOutputEngine::CPjAudioOutputEngine(pjmedia_snd_stream *parent_strm,
					   pjmedia_snd_play_cb play_cb,
					   void *user_data) 
: state_(STATE_INACTIVE), parentStrm_(parent_strm), playCb_(play_cb), 
  userData_(user_data), iOutputStream_(NULL), frameBuf_(NULL),
  lastError_(KErrNone), timestamp_(0)
{
}


void CPjAudioOutputEngine::ConstructL()
{
    frameBufSize_ = parentStrm_->samples_per_frame *
			   parentStrm_->channel_count * 
			   BYTES_PER_SAMPLE;
    frameBuf_ = new TUint8[frameBufSize_];
}

CPjAudioOutputEngine::~CPjAudioOutputEngine()
{
    Stop();
    delete [] frameBuf_;	
}

CPjAudioOutputEngine *
CPjAudioOutputEngine::NewLC(pjmedia_snd_stream *parent_strm,
			    pjmedia_snd_play_cb rec_cb,
			    void *user_data)
{
    CPjAudioOutputEngine* self = new (ELeave) CPjAudioOutputEngine(parent_strm,
								   rec_cb, 
								   user_data);
    CleanupStack::PushL(self);
    self->ConstructL();
    return self;
}

CPjAudioOutputEngine *
CPjAudioOutputEngine::NewL(pjmedia_snd_stream *parent_strm,
			   pjmedia_snd_play_cb play_cb,
			   void *user_data)
{
    CPjAudioOutputEngine *self = NewLC(parent_strm, play_cb, user_data);
    CleanupStack::Pop(self);
    return self;
}

pj_status_t CPjAudioOutputEngine::StartPlay()
{
    // Ignore command if playing is in progress.
    if (state_ == STATE_ACTIVE)
	return PJ_SUCCESS;
    
    // Destroy existing stream.
    if (iOutputStream_) delete iOutputStream_;
    iOutputStream_ = NULL;
    
    // Create the stream
    TRAPD(err, iOutputStream_ = CMdaAudioOutputStream::NewL(*this));
    if (err != KErrNone)
	return PJ_RETURN_OS_ERROR(err);
    
    // Initialize settings.
    TMdaAudioDataSettings iStreamSettings;
    iStreamSettings.iChannels = get_channel_cap(parentStrm_->channel_count);
    iStreamSettings.iSampleRate = get_clock_rate_cap(parentStrm_->clock_rate);

    pj_assert(iStreamSettings.iChannels != 0 && 
	      iStreamSettings.iSampleRate != 0);
    
    PJ_LOG(4,(THIS_FILE, "Opening sound device for playback, "
    		         "clock rate=%d, channel count=%d..",
    		         parentStrm_->clock_rate, 
    		         parentStrm_->channel_count));
    
    // Open stream.
    lastError_ = KRequestPending;
    iOutputStream_->Open(&iStreamSettings);

    // Wait until callback is called.
    while (lastError_ == KRequestPending)
	pj_thread_sleep(100);

    // Handle failure.
    if (lastError_ != KErrNone) {
	delete iOutputStream_;
	iOutputStream_ = NULL;
	return PJ_RETURN_OS_ERROR(lastError_);
    }

    // Success
    PJ_LOG(4,(THIS_FILE, "Sound playback started"));
    return PJ_SUCCESS;

}

void CPjAudioOutputEngine::Stop()
{
    // Stop stream if it's playing
    if (iOutputStream_ && state_ != STATE_INACTIVE) {
    	lastError_ = KRequestPending;
    	iOutputStream_->Stop();

	// Wait until it's actually stopped
    	while (lastError_ == KRequestPending)
	    pj_thread_sleep(100);
    }
    
    if (iOutputStream_) {	
	delete iOutputStream_;
	iOutputStream_ = NULL;
    }
    
    state_ = STATE_INACTIVE;
}

void CPjAudioOutputEngine::MaoscOpenComplete(TInt aError)
{
    lastError_ = aError;
    
    if (aError==KErrNone) {
	// output stream opened succesfully, set status to Active
	state_ = STATE_ACTIVE;

	// set stream properties, 16bit 8KHz mono
	TMdaAudioDataSettings iSettings;
	iSettings.iChannels = get_channel_cap(parentStrm_->channel_count);
	iSettings.iSampleRate = get_clock_rate_cap(parentStrm_->clock_rate);

	iOutputStream_->SetAudioPropertiesL(iSettings.iSampleRate, 
					    iSettings.iChannels);

	// set volume to 1/4th of stream max volume
	iOutputStream_->SetVolume(iOutputStream_->MaxVolume()/4);
	
	// set stream priority to normal and time sensitive
	iOutputStream_->SetPriority(EPriorityNormal, 
				    EMdaPriorityPreferenceTime);				

	// Call callback to retrieve frame from upstream.
	pj_status_t status;
	status = playCb_(this->userData_, timestamp_, frameBuf_, 
			 frameBufSize_);
	if (status != PJ_SUCCESS) {
	    this->Stop();
	    return;
	}

	// Increment timestamp.
	timestamp_ += (frameBufSize_ / BYTES_PER_SAMPLE);

	// issue WriteL() to write the first audio data block, 
	// subsequent calls to WriteL() will be issued in 
	// MMdaAudioOutputStreamCallback::MaoscBufferCopied() 
	// until whole data buffer is written.
	TPtrC8 frame(frameBuf_, frameBufSize_);
	iOutputStream_->WriteL(frame);
    } 
}

void CPjAudioOutputEngine::MaoscBufferCopied(TInt aError, 
					     const TDesC8& aBuffer)
{
    PJ_UNUSED_ARG(aBuffer);

    if (aError==KErrNone) {
    	// Buffer successfully written, feed another one.

	// Call callback to retrieve frame from upstream.
	pj_status_t status;
	status = playCb_(this->userData_, timestamp_, frameBuf_, 
			 frameBufSize_);
	if (status != PJ_SUCCESS) {
	    this->Stop();
	    return;
	}

	// Increment timestamp.
	timestamp_ += (frameBufSize_ / BYTES_PER_SAMPLE);

	// Write to playback stream.
	TPtrC8 frame(frameBuf_, frameBufSize_);
	iOutputStream_->WriteL(frame);

    } else if (aError==KErrAbort) {
	// playing was aborted, due to call to CMdaAudioOutputStream::Stop()
	state_ = STATE_INACTIVE;
    } else  {
	// error writing data to output
	lastError_ = aError;
	state_ = STATE_INACTIVE;
    }
}

void CPjAudioOutputEngine::MaoscPlayComplete(TInt aError)
{
    lastError_ = aError;
    state_ = STATE_INACTIVE;
}


//////////////////////////////////////////////////////////////////////////////
//


/*
 * Initialize sound subsystem.
 */
PJ_DEF(pj_status_t) pjmedia_snd_init(pj_pool_factory *factory)
{
    snd_pool_factory = factory;
    return PJ_SUCCESS;
}

/*
 * Get device count.
 */
PJ_DEF(int) pjmedia_snd_get_dev_count(void)
{
    /* Always return 1 */
    return 1;
}

/*
 * Get device info.
 */
PJ_DEF(const pjmedia_snd_dev_info*) pjmedia_snd_get_dev_info(unsigned index)
{
    /* Always return the default sound device */
    PJ_ASSERT_RETURN(index==0, NULL);
    return &symbian_snd_dev_info;
}



/*
 * Open sound recorder stream.
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
    pjmedia_snd_stream *strm;

    PJ_ASSERT_RETURN(index == 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(clock_rate && channel_count && samples_per_frame &&
    		     bits_per_sample && rec_cb && p_snd_strm, PJ_EINVAL);

    pool = pj_pool_create(snd_pool_factory, POOL_NAME, POOL_SIZE, POOL_INC, 
    			  NULL);
    if (!pool)
	return PJ_ENOMEM;

    strm = (pjmedia_snd_stream*) pj_pool_zalloc(pool, 
    						sizeof(pjmedia_snd_stream));
    strm->pool = pool;
    strm->clock_rate = clock_rate;
    strm->channel_count = channel_count;
    strm->samples_per_frame = samples_per_frame;

    TMdaAudioDataSettings settings;
    TInt clockRateCap, channelCountCap;

    clockRateCap = get_clock_rate_cap(clock_rate);
    channelCountCap = get_channel_cap(channel_count);

    PJ_ASSERT_RETURN(bits_per_sample == 16, PJ_EINVAL);
    PJ_ASSERT_RETURN(clockRateCap != 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(channelCountCap != 0, PJ_EINVAL);

    // Create the input stream.
    TRAPD(err, strm->inEngine = CPjAudioInputEngine::NewL(strm, rec_cb, 
    							  user_data));
    if (err != KErrNone) {
    	pj_pool_release(pool);
	return PJ_RETURN_OS_ERROR(err);
    }


    // Done.
    *p_snd_strm = strm;
    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t) pjmedia_snd_open_player( int index,
					unsigned clock_rate,
					unsigned channel_count,
					unsigned samples_per_frame,
					unsigned bits_per_sample,
					pjmedia_snd_play_cb play_cb,
					void *user_data,
					pjmedia_snd_stream **p_snd_strm )
{
    pj_pool_t *pool;
    pjmedia_snd_stream *strm;

    PJ_ASSERT_RETURN(index == 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(clock_rate && channel_count && samples_per_frame &&
    		     bits_per_sample && play_cb && p_snd_strm, PJ_EINVAL);

    pool = pj_pool_create(snd_pool_factory, POOL_NAME, POOL_SIZE, POOL_INC, 
    			  NULL);
    if (!pool)
	return PJ_ENOMEM;

    strm = (pjmedia_snd_stream*) pj_pool_zalloc(pool, 
    						sizeof(pjmedia_snd_stream));
    strm->pool = pool;
    strm->clock_rate = clock_rate;
    strm->channel_count = channel_count;
    strm->samples_per_frame = samples_per_frame;

    TMdaAudioDataSettings settings;
    TInt clockRateCap, channelCountCap;

    clockRateCap = get_clock_rate_cap(clock_rate);
    channelCountCap = get_channel_cap(channel_count);

    PJ_ASSERT_RETURN(bits_per_sample == 16, PJ_EINVAL);
    PJ_ASSERT_RETURN(clockRateCap != 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(channelCountCap != 0, PJ_EINVAL);

    // Create the output stream.
    TRAPD(err, strm->outEngine = CPjAudioOutputEngine::NewL(strm, play_cb, 
    							    user_data));
    if (err != KErrNone) {
    	pj_pool_release(pool);	
	return PJ_RETURN_OS_ERROR(err);
    }

    // Done.
    *p_snd_strm = strm;
    return PJ_SUCCESS;
}

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
    pjmedia_snd_stream *strm;

    PJ_ASSERT_RETURN(rec_id == 0 && play_id == 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(clock_rate && channel_count && samples_per_frame &&
    		     bits_per_sample && rec_cb && play_cb && p_snd_strm, 
    		     PJ_EINVAL);

    pool = pj_pool_create(snd_pool_factory, POOL_NAME, POOL_SIZE, POOL_INC, 
    			  NULL);
    if (!pool)
	return PJ_ENOMEM;

    strm = (pjmedia_snd_stream*) pj_pool_zalloc(pool, 
    						sizeof(pjmedia_snd_stream));
    strm->pool = pool;
    strm->clock_rate = clock_rate;
    strm->channel_count = channel_count;
    strm->samples_per_frame = samples_per_frame;

    TMdaAudioDataSettings settings;
    TInt clockRateCap, channelCountCap;

    clockRateCap = get_clock_rate_cap(clock_rate);
    channelCountCap = get_channel_cap(channel_count);

    PJ_ASSERT_RETURN(bits_per_sample == 16, PJ_EINVAL);
    PJ_ASSERT_RETURN(clockRateCap != 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(channelCountCap != 0, PJ_EINVAL);

    // Create the output stream.
    TRAPD(err, strm->outEngine = CPjAudioOutputEngine::NewL(strm, play_cb, 
    							    user_data));
    if (err != KErrNone) {
    	pj_pool_release(pool);	
	return PJ_RETURN_OS_ERROR(err);
    }

    // Done.
    *p_snd_strm = strm;
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_snd_stream_start(pjmedia_snd_stream *stream)
{
    pj_status_t status;
    
    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);
    
    if (stream->inEngine) {
    	status = stream->inEngine->StartRecord();
    	if (status != PJ_SUCCESS)
    	    return status;
    }
    	
    if (stream->outEngine) {
    	status = stream->outEngine->StartPlay();
    	if (status != PJ_SUCCESS)
    	    return status;
    }
    
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_snd_stream_stop(pjmedia_snd_stream *stream)
{
    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);
    
    if (stream->inEngine) {
    	stream->inEngine->Stop();
    }
    	
    if (stream->outEngine) {
    	stream->outEngine->Stop();
    }
    
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_snd_stream_close(pjmedia_snd_stream *stream)
{
    pj_pool_t *pool;
    
    PJ_ASSERT_RETURN(stream != NULL, PJ_EINVAL);
    
    if (stream->inEngine) {
    	delete stream->inEngine;
    	stream->inEngine = NULL;
    }

    if (stream->outEngine) {
    	delete stream->outEngine;
    	stream->outEngine = NULL;
    }
    
    pool = stream->pool;
    if (pool) {	
    	stream->pool = NULL;
    	pj_pool_release(pool);
    }
    
    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_snd_deinit(void)
{
    /* Nothing to do */
    return PJ_SUCCESS;
}

