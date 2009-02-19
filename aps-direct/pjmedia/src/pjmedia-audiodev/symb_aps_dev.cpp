/* $Id$ */
/* 
 * Copyright (C) 2008-2009 Teluu Inc. (http://www.teluu.com)
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
#include <pjmedia-audiodev/audiodev_imp.h>
#include <pjmedia/alaw_ulaw.h>
#include <pjmedia/errno.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/math.h>
#include <pj/os.h>
#include <pj/string.h>

#include <e32msgqueue.h>
#include <sounddevice.h>
#include <APSClientSession.h>

#define THIS_FILE			"symb_aps_dev.c"
#define BITS_PER_SAMPLE			16

#if 1
#   define TRACE_(st) PJ_LOG(3, st)
#else
#   define TRACE_(st)
#endif


/* App UID to open global APS queues to communicate with the APS server. */
extern TPtrC APP_UID;

/* APS G.711 frame length */
static pj_uint8_t aps_g711_frame_len;


/* APS factory */
struct aps_factory
{
    pjmedia_aud_dev_factory	 base;
    pj_pool_t			*pool;
    pj_pool_factory		*pf;
    pjmedia_aud_dev_info	 dev_info;
};


/* Forward declaration of CPjAudioEngine */
class CPjAudioEngine;


/* APS stream. */
struct aps_stream
{
    // Base
    pjmedia_aud_stream	 base;			/**< Base class.	*/
    
    // Pool
    pj_pool_t		*pool;			/**< Memory pool.       */

    // Common settings.
    pjmedia_aud_dev_param param;		/**< Stream param.	*/
    pjmedia_aud_rec_cb   rec_cb;		/**< Record callback.  	*/
    pjmedia_aud_play_cb	 play_cb;		/**< Playback callback. */
    void                *user_data;		/**< Application data.  */

    // Audio engine
    CPjAudioEngine	*engine;		/**< Internal engine.	*/

    pj_timestamp  	 ts_play;		/**< Playback timestamp.*/
    pj_timestamp	 ts_rec;		/**< Record timestamp.	*/

    pj_int16_t		*play_buf;		/**< Playback buffer.	*/
    pj_uint16_t		 play_buf_len;		/**< Playback buffer length. */
    pj_uint16_t		 play_buf_start;	/**< Playback buffer start index. */
    pj_int16_t		*rec_buf;		/**< Record buffer.	*/
    pj_uint16_t		 rec_buf_len;		/**< Record buffer length. */
    void                *strm_data;		/**< Stream data.	*/
};


/* Prototypes */
static pj_status_t factory_init(pjmedia_aud_dev_factory *f);
static pj_status_t factory_destroy(pjmedia_aud_dev_factory *f);
static unsigned    factory_get_dev_count(pjmedia_aud_dev_factory *f);
static pj_status_t factory_get_dev_info(pjmedia_aud_dev_factory *f, 
					unsigned index,
					pjmedia_aud_dev_info *info);
static pj_status_t factory_default_param(pjmedia_aud_dev_factory *f,
					 unsigned index,
					 pjmedia_aud_dev_param *param);
static pj_status_t factory_create_stream(pjmedia_aud_dev_factory *f,
					 const pjmedia_aud_dev_param *param,
					 pjmedia_aud_rec_cb rec_cb,
					 pjmedia_aud_play_cb play_cb,
					 void *user_data,
					 pjmedia_aud_stream **p_aud_strm);

static pj_status_t stream_get_param(pjmedia_aud_stream *strm,
				    pjmedia_aud_dev_param *param);
static pj_status_t stream_get_cap(pjmedia_aud_stream *strm,
				  pjmedia_aud_dev_cap cap,
				  void *value);
static pj_status_t stream_set_cap(pjmedia_aud_stream *strm,
				  pjmedia_aud_dev_cap cap,
				  const void *value);
static pj_status_t stream_start(pjmedia_aud_stream *strm);
static pj_status_t stream_stop(pjmedia_aud_stream *strm);
static pj_status_t stream_destroy(pjmedia_aud_stream *strm);


/* Operations */
static pjmedia_aud_dev_factory_op factory_op =
{
    &factory_init,
    &factory_destroy,
    &factory_get_dev_count,
    &factory_get_dev_info,
    &factory_default_param,
    &factory_create_stream
};

static pjmedia_aud_stream_op stream_op = 
{
    &stream_get_param,
    &stream_get_cap,
    &stream_set_cap,
    &stream_start,
    &stream_stop,
    &stream_destroy
};


/****************************************************************************
 * Internal APS Engine
 */

/*
 * Utility: print sound device error
 */
static void snd_perror(const char *title, TInt rc)
{
    PJ_LOG(1,(THIS_FILE, "%s (error code=%d)", title, rc));
}

typedef void(*PjAudioCallback)(TAPSCommBuffer &buf, void *user_data);

/**
 * Abstract class for handler of callbacks from APS client.
 */
class MQueueHandlerObserver
{
public:
    MQueueHandlerObserver(PjAudioCallback RecCb_, PjAudioCallback PlayCb_,
			  void *UserData_)
    : RecCb(RecCb_), PlayCb(PlayCb_), UserData(UserData_)
    {}

    virtual void InputStreamInitialized(const TInt aStatus) = 0;
    virtual void OutputStreamInitialized(const TInt aStatus) = 0;
    virtual void NotifyError(const TInt aError) = 0;

public:
    PjAudioCallback RecCb;
    PjAudioCallback PlayCb;
    void *UserData;
};

/**
 * Handler for communication and data queue.
 */
class CQueueHandler : public CActive
{
public:
    // Types of queue handler
    enum TQueueHandlerType {
        ERecordCommQueue,
        EPlayCommQueue,
        ERecordQueue,
        EPlayQueue
    };

    // The order corresponds to the APS Server state, do not change!
    enum TState {
    	EAPSPlayerInitialize        = 1,
    	EAPSRecorderInitialize      = 2,
    	EAPSPlayData                = 3,
    	EAPSRecordData              = 4,
    	EAPSPlayerInitComplete      = 5,
    	EAPSRecorderInitComplete    = 6
    };

    static CQueueHandler* NewL(MQueueHandlerObserver* aObserver,
			       RMsgQueue<TAPSCommBuffer>* aQ,
			       RMsgQueue<TAPSCommBuffer>* aWriteQ,
			       TQueueHandlerType aType)
    {
	CQueueHandler* self = new (ELeave) CQueueHandler(aObserver, aQ, aWriteQ,
							 aType);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
    }

    // Destructor
    ~CQueueHandler() { Cancel(); }

    // Start listening queue event
    void Start() {
	iQ->NotifyDataAvailable(iStatus);
	SetActive();
    }

private:
    // Constructor
    CQueueHandler(MQueueHandlerObserver* aObserver,
		  RMsgQueue<TAPSCommBuffer>* aQ,
		  RMsgQueue<TAPSCommBuffer>* aWriteQ,
		  TQueueHandlerType aType)
	: CActive(CActive::EPriorityHigh),
	  iQ(aQ), iWriteQ(aWriteQ), iObserver(aObserver), iType(aType)
    {
	CActiveScheduler::Add(this);

	// use lower priority for comm queues
	if ((iType == ERecordCommQueue) || (iType == EPlayCommQueue))
	    SetPriority(CActive::EPriorityStandard);
    }

    // Second phase constructor
    void ConstructL() {}

    // Inherited from CActive
    void DoCancel() { iQ->CancelDataAvailable(); }

    void RunL() {
	if (iStatus != KErrNone) {
	    iObserver->NotifyError(iStatus.Int());
	    return;
        }

	TAPSCommBuffer buffer;
	TInt ret = iQ->Receive(buffer);

	if (ret != KErrNone) {
	    iObserver->NotifyError(ret);
	    return;
	}

	switch (iType) {
	case ERecordQueue:
	    if (buffer.iCommand == EAPSRecordData) {
		iObserver->RecCb(buffer, iObserver->UserData);
	    } else {
		iObserver->NotifyError(buffer.iStatus);
	    }
	    break;

	// Callbacks from the APS main thread
	case EPlayCommQueue:
	    switch (buffer.iCommand) {
		case EAPSPlayData:
		    if (buffer.iStatus == KErrUnderflow) {
			iObserver->PlayCb(buffer, iObserver->UserData);
			iWriteQ->Send(buffer);
		    }
		    break;
		case EAPSPlayerInitialize:
		    iObserver->NotifyError(buffer.iStatus);
		    break;
		case EAPSPlayerInitComplete:
		    iObserver->OutputStreamInitialized(buffer.iStatus);
		    break;
		case EAPSRecorderInitComplete:
		    iObserver->InputStreamInitialized(buffer.iStatus);
		    break;
		default:
		    iObserver->NotifyError(buffer.iStatus);
		    break;
	    }
	    break;

	// Callbacks from the APS recorder thread
	case ERecordCommQueue:
	    switch (buffer.iCommand) {
		// The APS recorder thread will only report errors
		// through this handler. All other callbacks will be
		// sent from the APS main thread through EPlayCommQueue
		case EAPSRecorderInitialize:
		case EAPSRecordData:
		default:
		    iObserver->NotifyError(buffer.iStatus);
		    break;
	    }
	    break;

	default:
	    break;
        }

        // issue next request
        iQ->NotifyDataAvailable(iStatus);
        SetActive();
    }

    TInt RunError(TInt) {
	return 0;
    }

    // Data
    RMsgQueue<TAPSCommBuffer>	*iQ;   // (not owned)
    RMsgQueue<TAPSCommBuffer>	*iWriteQ;   // (not owned)
    MQueueHandlerObserver	*iObserver; // (not owned)
    TQueueHandlerType            iType;
};

/*
 * Audio setting for CPjAudioEngine.
 */
class CPjAudioSetting
{
public:
    TFourCC		 fourcc;
    TAPSCodecMode	 mode;
    TBool		 plc;
    TBool		 vad;
    TBool		 cng;
    TBool		 loudspk;
};

/*
 * Implementation: Symbian Input & Output Stream.
 */
class CPjAudioEngine : public CBase, MQueueHandlerObserver
{
public:
    enum State
    {
	STATE_NULL,
	STATE_READY,
	STATE_STREAMING
    };

    ~CPjAudioEngine();

    static CPjAudioEngine *NewL(struct aps_stream *parent_strm,
			        PjAudioCallback rec_cb,
				PjAudioCallback play_cb,
				void *user_data,
				const CPjAudioSetting &setting);

    TInt StartL();
    void Stop();

    TInt ActivateSpeaker(TBool active);
    
    TInt SetVolume(TInt vol) { return iSession.SetVolume(vol); }
    TInt GetVolume() { return iSession.Volume(); }
    TInt GetMaxVolume() { return iSession.MaxVolume(); }
    
    TInt SetGain(TInt gain) { return iSession.SetGain(gain); }
    TInt GetGain() { return iSession.Gain(); }
    TInt GetMaxGain() { return iSession.MaxGain(); }

private:
    CPjAudioEngine(struct aps_stream *parent_strm,
		   PjAudioCallback rec_cb,
		   PjAudioCallback play_cb,
		   void *user_data,
		   const CPjAudioSetting &setting);
    void ConstructL();

    TInt InitPlayL();
    TInt InitRecL();
    TInt StartStreamL();

    // Inherited from MQueueHandlerObserver
    virtual void InputStreamInitialized(const TInt aStatus);
    virtual void OutputStreamInitialized(const TInt aStatus);
    virtual void NotifyError(const TInt aError);

    State			 state_;
    struct aps_stream		*parentStrm_;
    CPjAudioSetting		 setting_;

    RAPSSession                  iSession;
    TAPSInitSettings             iPlaySettings;
    TAPSInitSettings             iRecSettings;

    RMsgQueue<TAPSCommBuffer>    iReadQ;
    RMsgQueue<TAPSCommBuffer>    iReadCommQ;
    RMsgQueue<TAPSCommBuffer>    iWriteQ;
    RMsgQueue<TAPSCommBuffer>    iWriteCommQ;

    CQueueHandler		*iPlayCommHandler;
    CQueueHandler		*iRecCommHandler;
    CQueueHandler		*iRecHandler;
};


CPjAudioEngine* CPjAudioEngine::NewL(struct aps_stream *parent_strm,
				     PjAudioCallback rec_cb,
				     PjAudioCallback play_cb,
				     void *user_data,
				     const CPjAudioSetting &setting)
{
    CPjAudioEngine* self = new (ELeave) CPjAudioEngine(parent_strm,
						       rec_cb, play_cb,
						       user_data,
						       setting);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop(self);
    return self;
}

CPjAudioEngine::CPjAudioEngine(struct aps_stream *parent_strm,
			       PjAudioCallback rec_cb,
			       PjAudioCallback play_cb,
			       void *user_data,
			       const CPjAudioSetting &setting)
      : MQueueHandlerObserver(rec_cb, play_cb, user_data),
	state_(STATE_NULL),
	parentStrm_(parent_strm),
	setting_(setting),
	iPlayCommHandler(0),
	iRecCommHandler(0),
	iRecHandler(0)
{
}

CPjAudioEngine::~CPjAudioEngine()
{
    Stop();

    delete iRecHandler;
    delete iPlayCommHandler;
    delete iRecCommHandler;

    // On some devices, immediate closing after stopping may cause APS server
    // panic KERN-EXEC 0, so let's wait for sometime before really closing
    // the client session.
    TTime start, now;
    enum { APS_CLOSE_WAIT_TIME = 200 }; /* in msecs */
    
    start.UniversalTime();
    do {
	pj_symbianos_poll(-1, APS_CLOSE_WAIT_TIME);
	now.UniversalTime();
    } while (now.MicroSecondsFrom(start) < APS_CLOSE_WAIT_TIME * 1000);

    iSession.Close();

    if (state_ == STATE_READY) {
	if (parentStrm_->param.dir != PJMEDIA_DIR_PLAYBACK) {
	    iReadQ.Close();
	    iReadCommQ.Close();
	}
	iWriteQ.Close();
	iWriteCommQ.Close();
    }
}

TInt CPjAudioEngine::InitPlayL()
{
    if (state_ == STATE_STREAMING || state_ == STATE_READY)
	return 0;

    TInt err = iSession.InitializePlayer(iPlaySettings);
    if (err != KErrNone) {
	snd_perror("Failed to initialize player", err);
	return err;
    }

    // Open message queues for the output stream
    TBuf<128> buf2 = iPlaySettings.iGlobal;
    buf2.Append(_L("PlayQueue"));
    TBuf<128> buf3 = iPlaySettings.iGlobal;
    buf3.Append(_L("PlayCommQueue"));

    while (iWriteQ.OpenGlobal(buf2))
	User::After(10);
    while (iWriteCommQ.OpenGlobal(buf3))
	User::After(10);

    // Construct message queue handler
    iPlayCommHandler = CQueueHandler::NewL(this, &iWriteCommQ, &iWriteQ,
					   CQueueHandler::EPlayCommQueue);

    // Start observing APS callbacks on output stream message queue
    iPlayCommHandler->Start();

    return 0;
}

TInt CPjAudioEngine::InitRecL()
{
    if (state_ == STATE_STREAMING || state_ == STATE_READY)
	return 0;

    // Initialize input stream device
    TInt err = iSession.InitializeRecorder(iRecSettings);
    if (err != KErrNone && err != KErrAlreadyExists) {
	snd_perror("Failed to initialize recorder", err);
	return err;
    }

    TBuf<128> buf1 = iRecSettings.iGlobal;
    buf1.Append(_L("RecordQueue"));
    TBuf<128> buf4 = iRecSettings.iGlobal;
    buf4.Append(_L("RecordCommQueue"));

    // Must wait for APS thread to finish creating message queues
    // before we can open and use them.
    while (iReadQ.OpenGlobal(buf1))
	User::After(10);
    while (iReadCommQ.OpenGlobal(buf4))
	User::After(10);

    // Construct message queue handlers
    iRecHandler = CQueueHandler::NewL(this, &iReadQ, NULL,
				      CQueueHandler::ERecordQueue);
    iRecCommHandler = CQueueHandler::NewL(this, &iReadCommQ, NULL,
					  CQueueHandler::ERecordCommQueue);

    // Start observing APS callbacks from on input stream message queue
    iRecHandler->Start();
    iRecCommHandler->Start();

    return 0;
}

TInt CPjAudioEngine::StartL()
{
    if (state_ == STATE_READY)
	return StartStreamL();

    // Even if only capturer are opened, playback thread of APS Server need
    // to be run(?). Since some messages will be delivered via play comm queue.
    return InitPlayL();
}

void CPjAudioEngine::Stop()
{
    if (state_ == STATE_STREAMING) {
	iSession.Stop();
	state_ = STATE_READY;
	TRACE_((THIS_FILE, "Sound device stopped"));
    }
}

void CPjAudioEngine::ConstructL()
{
    // Recorder settings
    iRecSettings.iFourCC		= setting_.fourcc;
    iRecSettings.iGlobal		= APP_UID;
    iRecSettings.iPriority		= TMdaPriority(100);
    iRecSettings.iPreference		= TMdaPriorityPreference(0x05210001);
    iRecSettings.iSettings.iChannels	= EMMFMono;
    iRecSettings.iSettings.iSampleRate	= EMMFSampleRate8000Hz;

    // Player settings
    iPlaySettings.iFourCC		= setting_.fourcc;
    iPlaySettings.iGlobal		= APP_UID;
    iPlaySettings.iPriority		= TMdaPriority(100);
    iPlaySettings.iPreference		= TMdaPriorityPreference(0x05220001);
    iPlaySettings.iSettings.iChannels	= EMMFMono;
    iPlaySettings.iSettings.iSampleRate = EMMFSampleRate8000Hz;
    iPlaySettings.iSettings.iVolume	= 0;

    User::LeaveIfError(iSession.Connect());
}

TInt CPjAudioEngine::StartStreamL()
{
    if (state_ == STATE_STREAMING)
	return 0;

    iSession.SetCng(setting_.cng);
    iSession.SetVadMode(setting_.vad);
    iSession.SetPlc(setting_.plc);
    iSession.SetEncoderMode(setting_.mode);
    iSession.SetDecoderMode(setting_.mode);
    iSession.ActivateLoudspeaker(setting_.loudspk);

    // Not only capture
    if (parentStrm_->param.dir != PJMEDIA_DIR_CAPTURE) {
	iSession.Write();
	TRACE_((THIS_FILE, "Player started"));
    }

    // Not only playback
    if (parentStrm_->param.dir != PJMEDIA_DIR_PLAYBACK) {
	iSession.Read();
	TRACE_((THIS_FILE, "Recorder started"));
    }

    state_ = STATE_STREAMING;
    return 0;
}

void CPjAudioEngine::InputStreamInitialized(const TInt aStatus)
{
    TRACE_((THIS_FILE, "Recorder initialized, err=%d", aStatus));

    state_ = STATE_READY;
    if (aStatus == KErrNone) {
	StartStreamL();
    }
}

void CPjAudioEngine::OutputStreamInitialized(const TInt aStatus)
{
    TRACE_((THIS_FILE, "Player initialized, err=%d", aStatus));

    if (aStatus == KErrNone) {
	if (parentStrm_->param.dir == PJMEDIA_DIR_PLAYBACK) {
	    state_ = STATE_READY;
	    // Only playback, start directly
	    StartStreamL();
	} else
	    InitRecL();
    }
}

void CPjAudioEngine::NotifyError(const TInt aError)
{
    snd_perror("Error from CQueueHandler", aError);
}

TInt CPjAudioEngine::ActivateSpeaker(TBool active)
{
    if (state_ == STATE_READY || state_ == STATE_STREAMING) {
        iSession.ActivateLoudspeaker(active);
        TRACE_((THIS_FILE, "Loudspeaker turned %s", (active? "on":"off")));
	return KErrNone;
    }
    return KErrNotReady;
}



static void RecCbPcm(TAPSCommBuffer &buf, void *user_data)
{
    struct aps_stream *strm = (struct aps_stream*) user_data;

    /* Buffer has to contain normal speech. */
    pj_assert(buf.iBuffer[0] == 1 && buf.iBuffer[1] == 0);

    /* Detect the recorder G.711 frame size, player frame size will follow
     * this recorder frame size.
     */
    if (aps_g711_frame_len == 0) {
	aps_g711_frame_len = buf.iBuffer.Length() < 160? 80 : 160;
	TRACE_((THIS_FILE, "Detected APS G.711 frame size = %u samples",
		aps_g711_frame_len));
    }

    /* Decode APS buffer (coded in G.711) and put the PCM result into rec_buf.
     * Whenever rec_buf is full, call parent stream callback.
     */
    unsigned dec_len = 0;

    while (dec_len < aps_g711_frame_len) {
	unsigned tmp;

	tmp = PJ_MIN(strm->param.samples_per_frame - strm->rec_buf_len,
		     aps_g711_frame_len - dec_len);
	pjmedia_ulaw_decode(&strm->rec_buf[strm->rec_buf_len],
			    buf.iBuffer.Ptr() + 2 + dec_len,
			    tmp);
	strm->rec_buf_len += tmp;
	dec_len += tmp;

	pj_assert(strm->rec_buf_len <= strm->param.samples_per_frame);

	if (strm->rec_buf_len == strm->param.samples_per_frame) {
	    pjmedia_frame f;
	    
	    f.type = PJMEDIA_FRAME_TYPE_AUDIO;
	    f.buf = strm->rec_buf;
	    f.size = strm->rec_buf_len << 1;
	    
	    strm->rec_cb(strm->user_data, &f);
	    strm->rec_buf_len = 0;
	}
    }
}

static void PlayCbPcm(TAPSCommBuffer &buf, void *user_data)
{
    struct aps_stream *strm = (struct aps_stream*) user_data;
    unsigned g711_frame_len = aps_g711_frame_len;

    /* Init buffer attributes and header. */
    buf.iCommand = CQueueHandler::EAPSPlayData;
    buf.iStatus = 0;
    buf.iBuffer.Zero();
    buf.iBuffer.Append(1);
    buf.iBuffer.Append(0);

    /* Assume frame size is 10ms if frame size hasn't been known. */
    if (g711_frame_len == 0)
	g711_frame_len = 80;

    /* Call parent stream callback to get PCM samples to play,
     * encode the PCM samples into G.711 and put it into APS buffer.
     */
    unsigned enc_len = 0;
    while (enc_len < g711_frame_len) {
	if (strm->play_buf_len == 0) {
	    pjmedia_frame f;
	    
	    f.buf = strm->play_buf;
	    f.size = strm->param.samples_per_frame << 1;
	    
	    strm->play_cb(strm->user_data, &f);
	    strm->play_buf_len = strm->param.samples_per_frame;
	    strm->play_buf_start = 0;
	}

	unsigned tmp;

	tmp = PJ_MIN(strm->play_buf_len, g711_frame_len - enc_len);
	pjmedia_ulaw_encode((pj_uint8_t*)&strm->play_buf[strm->play_buf_start],
			    &strm->play_buf[strm->play_buf_start],
			    tmp);
	buf.iBuffer.Append((TUint8*)&strm->play_buf[strm->play_buf_start], tmp);
	enc_len += tmp;
	strm->play_buf_len -= tmp;
	strm->play_buf_start += tmp;
    }
}

/****************************************************************************
 * Internal APS callbacks
 */

/* Pack/unpack G.729 frame of S60 DSP codec, taken from:  
 * http://wiki.forum.nokia.com/index.php/TSS000776_-_Payload_conversion_for_G.729_audio_format
 */
#include "../pjmedia/s60_g729_bitstream.h"
#include <pjmedia-codec/amr_helper.h>

static void RecCb(TAPSCommBuffer &buf, void *user_data)
{
    struct aps_stream *strm = (struct aps_stream*) user_data;
    pjmedia_frame_ext *frame = (pjmedia_frame_ext*) strm->rec_buf;
    
    switch(strm->param.ext_fmt.id) {
    case PJMEDIA_FORMAT_AMR:
	{
	    const pj_uint8_t *p = (const pj_uint8_t*)buf.iBuffer.Ptr() + 1;
	    unsigned len = buf.iBuffer.Length() - 1;
	    
	    pjmedia_frame_ext_append_subframe(frame, p, len << 3, 160);
	    if (frame->samples_cnt == strm->param.samples_per_frame) {
		frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		strm->rec_cb(strm->user_data, (pjmedia_frame*)frame);
		frame->samples_cnt = 0;
		frame->subframe_cnt = 0;
	    }
	}
	break;
	
    case PJMEDIA_FORMAT_G729:
	{
	    /* Check if we got a normal or SID frame. */
	    if (buf.iBuffer[0] != 0 || buf.iBuffer[1] != 0) {
		enum { NORMAL_LEN = 22, SID_LEN = 8 };
		TBitStream *bitstream = (TBitStream*)strm->strm_data;
		unsigned src_len = buf.iBuffer.Length()- 2;
		
		pj_assert(src_len == NORMAL_LEN || src_len == SID_LEN);

		const TDesC8& p = bitstream->CompressG729Frame(
					    buf.iBuffer.Right(src_len), 
					    src_len == SID_LEN);
		
		pjmedia_frame_ext_append_subframe(frame, p.Ptr(), 
						  p.Length() << 3, 80);
	    } else { /* We got null frame. */
		pjmedia_frame_ext_append_subframe(frame, NULL, 0, 80);
	    }
	    
	    if (frame->samples_cnt == strm->param.samples_per_frame) {
		frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		strm->rec_cb(strm->user_data, (pjmedia_frame*)frame);
		frame->samples_cnt = 0;
		frame->subframe_cnt = 0;
	    }
	}
	break;

    case PJMEDIA_FORMAT_ILBC:
	{
	    unsigned samples_got;
	    
	    samples_got = strm->param.ext_fmt.bitrate == 15200? 160 : 240;
	    
	    /* Check if we got a normal frame. */
	    if (buf.iBuffer[0] == 1 && buf.iBuffer[1] == 0) {
		const pj_uint8_t *p = (const pj_uint8_t*)buf.iBuffer.Ptr() + 2;
		unsigned len = buf.iBuffer.Length() - 2;
		
		pjmedia_frame_ext_append_subframe(frame, p, len << 3,
						  samples_got);
	    } else { /* We got null frame. */
		pjmedia_frame_ext_append_subframe(frame, NULL, 0, samples_got);
	    }
	    
	    if (frame->samples_cnt == strm->param.samples_per_frame) {
		frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		strm->rec_cb(strm->user_data, (pjmedia_frame*)frame);
		frame->samples_cnt = 0;
		frame->subframe_cnt = 0;
	    }
	}
	break;
	
    case PJMEDIA_FORMAT_PCMU:
    case PJMEDIA_FORMAT_PCMA:
	{
	    unsigned samples_processed = 0;
	    
	    /* Make sure it is normal frame. */
	    pj_assert(buf.iBuffer[0] == 1 && buf.iBuffer[1] == 0);

	    /* Detect the recorder G.711 frame size, player frame size will 
	     * follow this recorder frame size.
	     */
	    if (aps_g711_frame_len == 0) {
		aps_g711_frame_len = buf.iBuffer.Length() < 160? 80 : 160;
		TRACE_((THIS_FILE, "Detected APS G.711 frame size = %u samples",
			aps_g711_frame_len));
	    }
	    
	    /* Convert APS buffer format into pjmedia_frame_ext. Whenever 
	     * samples count in the frame is equal to stream's samples per 
	     * frame, call parent stream callback.
	     */
	    while (samples_processed < aps_g711_frame_len) {
		unsigned tmp;
		const pj_uint8_t *pb = (const pj_uint8_t*)buf.iBuffer.Ptr() +
				       2 + samples_processed;
    
		tmp = PJ_MIN(strm->param.samples_per_frame - frame->samples_cnt,
			     aps_g711_frame_len - samples_processed);
		
		pjmedia_frame_ext_append_subframe(frame, pb, tmp << 3, tmp);
		samples_processed += tmp;
    
		if (frame->samples_cnt == strm->param.samples_per_frame) {
		    frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		    strm->rec_cb(strm->user_data, (pjmedia_frame*)frame);
		    frame->samples_cnt = 0;
		    frame->subframe_cnt = 0;
		}
	    }
	}
	break;
	
    default:
	break;
    }
}

static void PlayCb(TAPSCommBuffer &buf, void *user_data)
{
    struct aps_stream *strm = (struct aps_stream*) user_data;
    pjmedia_frame_ext *frame = (pjmedia_frame_ext*) strm->play_buf;

    /* Init buffer attributes and header. */
    buf.iCommand = CQueueHandler::EAPSPlayData;
    buf.iStatus = 0;
    buf.iBuffer.Zero();

    switch(strm->param.ext_fmt.id) {
    case PJMEDIA_FORMAT_AMR:
	{
	    if (frame->samples_cnt == 0) {
		frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		strm->play_cb(strm->user_data, (pjmedia_frame*)frame);
		pj_assert(frame->base.type==PJMEDIA_FRAME_TYPE_EXTENDED ||
			  frame->base.type==PJMEDIA_FRAME_TYPE_NONE);
	    }

	    if (frame->base.type == PJMEDIA_FRAME_TYPE_EXTENDED) { 
		pjmedia_frame_ext_subframe *sf;
		unsigned samples_cnt;
		
		sf = pjmedia_frame_ext_get_subframe(frame, 0);
		samples_cnt = frame->samples_cnt / frame->subframe_cnt;
		
		if (sf->data && sf->bitlen) {
		    /* AMR header for APS is one byte, the format (may be!):
		     * 0xxxxy00, where xxxx:frame type, y:not sure. 
		     */
		    unsigned len = (sf->bitlen+7)>>3;
		    enum {SID_FT = 8 };
		    pj_uint8_t amr_header = 4, ft = SID_FT;

		    if (len >= pjmedia_codec_amrnb_framelen[0])
			ft = pjmedia_codec_amr_get_mode2(PJ_TRUE, len);
		    
		    amr_header |= ft << 3;
		    buf.iBuffer.Append(amr_header);
		    
		    buf.iBuffer.Append((TUint8*)sf->data, len);
		} else {
		    buf.iBuffer.Append(0);
		}

		pjmedia_frame_ext_pop_subframes(frame, 1);
	    
	    } else { /* PJMEDIA_FRAME_TYPE_NONE */
		buf.iBuffer.Append(0);
		
		frame->samples_cnt = 0;
		frame->subframe_cnt = 0;
	    }
	}
	break;
	
    case PJMEDIA_FORMAT_G729:
	{
	    if (frame->samples_cnt == 0) {
		frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		strm->play_cb(strm->user_data, (pjmedia_frame*)frame);
		pj_assert(frame->base.type==PJMEDIA_FRAME_TYPE_EXTENDED ||
			  frame->base.type==PJMEDIA_FRAME_TYPE_NONE);
	    }

	    if (frame->base.type == PJMEDIA_FRAME_TYPE_EXTENDED) { 
		pjmedia_frame_ext_subframe *sf;
		unsigned samples_cnt;
		
		sf = pjmedia_frame_ext_get_subframe(frame, 0);
		samples_cnt = frame->samples_cnt / frame->subframe_cnt;
		
		if (sf->data && sf->bitlen) {
		    enum { NORMAL_LEN = 10, SID_LEN = 2 };
		    pj_bool_t sid_frame = ((sf->bitlen >> 3) == SID_LEN);
		    TBitStream *bitstream = (TBitStream*)strm->strm_data;
		    const TPtrC8 src(sf->data, sf->bitlen>>3);
		    const TDesC8 &dst = bitstream->ExpandG729Frame(src,
								   sid_frame); 
		    if (sid_frame) {
			buf.iBuffer.Append(0);
			buf.iBuffer.Append(1);
		    } else {
			buf.iBuffer.Append(1);
			buf.iBuffer.Append(0);
		    }
		    buf.iBuffer.Append(dst);
		} else {
		    buf.iBuffer.Append(0);
		    buf.iBuffer.Append(0);
		}

		pjmedia_frame_ext_pop_subframes(frame, 1);
	    
	    } else { /* PJMEDIA_FRAME_TYPE_NONE */
		buf.iBuffer.Append(0);
		buf.iBuffer.Append(0);
		
		frame->samples_cnt = 0;
		frame->subframe_cnt = 0;
	    }
	}
	break;
	
    case PJMEDIA_FORMAT_ILBC:
	{
	    if (frame->samples_cnt == 0) {
		frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		strm->play_cb(strm->user_data, (pjmedia_frame*)frame);
		pj_assert(frame->base.type==PJMEDIA_FRAME_TYPE_EXTENDED ||
			  frame->base.type==PJMEDIA_FRAME_TYPE_NONE);
	    }

	    if (frame->base.type == PJMEDIA_FRAME_TYPE_EXTENDED) { 
		pjmedia_frame_ext_subframe *sf;
		unsigned samples_cnt;
		
		sf = pjmedia_frame_ext_get_subframe(frame, 0);
		samples_cnt = frame->samples_cnt / frame->subframe_cnt;
		
		pj_assert((strm->param.ext_fmt.bitrate == 15200 && 
			   samples_cnt == 160) ||
			  (strm->param.ext_fmt.bitrate != 15200 &&
			   samples_cnt == 240));
		
		if (sf->data && sf->bitlen) {
		    buf.iBuffer.Append(1);
		    buf.iBuffer.Append(0);
		    buf.iBuffer.Append((TUint8*)sf->data, sf->bitlen>>3);
		} else {
		    buf.iBuffer.Append(0);
		    buf.iBuffer.Append(0);
		}

		pjmedia_frame_ext_pop_subframes(frame, 1);
	    
	    } else { /* PJMEDIA_FRAME_TYPE_NONE */
		buf.iBuffer.Append(0);
		buf.iBuffer.Append(0);
		
		frame->samples_cnt = 0;
		frame->subframe_cnt = 0;
	    }
	}
	break;
	
    case PJMEDIA_FORMAT_PCMU:
    case PJMEDIA_FORMAT_PCMA:
	{
	    unsigned samples_ready = 0;
	    unsigned samples_req = aps_g711_frame_len;
	    
	    /* Assume frame size is 10ms if frame size hasn't been known. */
	    if (samples_req == 0)
		samples_req = 80;
	    
	    buf.iBuffer.Append(1);
	    buf.iBuffer.Append(0);
	    
	    /* Call parent stream callback to get samples to play. */
	    while (samples_ready < samples_req) {
		if (frame->samples_cnt == 0) {
		    frame->base.type = PJMEDIA_FRAME_TYPE_EXTENDED;
		    strm->play_cb(strm->user_data, (pjmedia_frame*)frame);
		    pj_assert(frame->base.type==PJMEDIA_FRAME_TYPE_EXTENDED ||
			      frame->base.type==PJMEDIA_FRAME_TYPE_NONE);
		}
    
		if (frame->base.type == PJMEDIA_FRAME_TYPE_EXTENDED) { 
		    pjmedia_frame_ext_subframe *sf;
		    unsigned samples_cnt;
		    
		    sf = pjmedia_frame_ext_get_subframe(frame, 0);
		    samples_cnt = frame->samples_cnt / frame->subframe_cnt;
		    if (sf->data && sf->bitlen) {
			buf.iBuffer.Append((TUint8*)sf->data, sf->bitlen>>3);
		    } else {
			pj_uint8_t silc;
			silc = (strm->param.ext_fmt.id==PJMEDIA_FORMAT_PCMU)?
				pjmedia_linear2ulaw(0) : pjmedia_linear2alaw(0);
			buf.iBuffer.AppendFill(silc, samples_cnt);
		    }
		    samples_ready += samples_cnt;
		    
		    pjmedia_frame_ext_pop_subframes(frame, 1);
		
		} else { /* PJMEDIA_FRAME_TYPE_NONE */
		    pj_uint8_t silc;
		    
		    silc = (strm->param.ext_fmt.id==PJMEDIA_FORMAT_PCMU)?
			    pjmedia_linear2ulaw(0) : pjmedia_linear2alaw(0);
		    buf.iBuffer.AppendFill(silc, samples_req - samples_ready);

		    samples_ready = samples_req;
		    frame->samples_cnt = 0;
		    frame->subframe_cnt = 0;
		}
	    }
	}
	break;
	
    default:
	break;
    }
}


/****************************************************************************
 * Factory operations
 */
/*
 * Init APS audio driver.
 */
pjmedia_aud_dev_factory* pjmedia_aps_factory(pj_pool_factory *pf)
{
    struct aps_factory *f;
    pj_pool_t *pool;

    pool = pj_pool_create(pf, "APS", 1000, 1000, NULL);
    f = PJ_POOL_ZALLOC_T(pool, struct aps_factory);
    f->pf = pf;
    f->pool = pool;
    f->base.op = &factory_op;

    return &f->base;
}

/* API: init factory */
static pj_status_t factory_init(pjmedia_aud_dev_factory *f)
{
    struct aps_factory *af = (struct aps_factory*)f;

    pj_ansi_strcpy(af->dev_info.name, "S60 APS");
    af->dev_info.default_samples_per_sec = 8000;
    af->dev_info.caps = PJMEDIA_AUD_DEV_CAP_EXT_FORMAT |
			PJMEDIA_AUD_DEV_CAP_INPUT_VOLUME_SETTING |
			PJMEDIA_AUD_DEV_CAP_OUTPUT_VOLUME_SETTING |
			PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE |
			PJMEDIA_AUD_DEV_CAP_VAD |
			PJMEDIA_AUD_DEV_CAP_CNG;
    af->dev_info.routes = PJMEDIA_AUD_DEV_ROUTE_EARPIECE | 
			  PJMEDIA_AUD_DEV_ROUTE_LOUDSPEAKER;
    af->dev_info.input_count = 1;
    af->dev_info.output_count = 1;

    af->dev_info.ext_fmt_cnt = 6;
    af->dev_info.ext_fmt[0].id = PJMEDIA_FORMAT_AMR;
    af->dev_info.ext_fmt[1].id = PJMEDIA_FORMAT_G729;
    af->dev_info.ext_fmt[2].id = PJMEDIA_FORMAT_ILBC;
    af->dev_info.ext_fmt[3].id = PJMEDIA_FORMAT_PCMU;
    af->dev_info.ext_fmt[4].id = PJMEDIA_FORMAT_PCMA;
    af->dev_info.ext_fmt[5].id = PJMEDIA_FORMAT_L16;
    
    PJ_LOG(4, (THIS_FILE, "APS initialized"));

    return PJ_SUCCESS;
}

/* API: destroy factory */
static pj_status_t factory_destroy(pjmedia_aud_dev_factory *f)
{
    struct aps_factory *af = (struct aps_factory*)f;
    pj_pool_t *pool = af->pool;

    af->pool = NULL;
    pj_pool_release(pool);

    PJ_LOG(4, (THIS_FILE, "APS destroyed"));
    
    return PJ_SUCCESS;
}

/* API: get number of devices */
static unsigned factory_get_dev_count(pjmedia_aud_dev_factory *f)
{
    PJ_UNUSED_ARG(f);
    return 1;
}

/* API: get device info */
static pj_status_t factory_get_dev_info(pjmedia_aud_dev_factory *f, 
					unsigned index,
					pjmedia_aud_dev_info *info)
{
    struct aps_factory *af = (struct aps_factory*)f;

    PJ_ASSERT_RETURN(index == 0, PJMEDIA_EAUD_INVDEV);

    pj_memcpy(info, &af->dev_info, sizeof(*info));

    return PJ_SUCCESS;
}

/* API: create default device parameter */
static pj_status_t factory_default_param(pjmedia_aud_dev_factory *f,
					 unsigned index,
					 pjmedia_aud_dev_param *param)
{
    struct aps_factory *af = (struct aps_factory*)f;

    PJ_ASSERT_RETURN(index == 0, PJMEDIA_EAUD_INVDEV);

    pj_bzero(param, sizeof(*param));
    param->dir = PJMEDIA_DIR_CAPTURE_PLAYBACK;
    param->rec_id = index;
    param->play_id = index;
    param->clock_rate = af->dev_info.default_samples_per_sec;
    param->channel_count = 1;
    param->samples_per_frame = af->dev_info.default_samples_per_sec * 20 / 1000;
    param->bits_per_sample = BITS_PER_SAMPLE;
    param->flags = af->dev_info.caps;
    param->ext_fmt.id = PJMEDIA_FORMAT_L16;
    param->out_route = PJMEDIA_AUD_DEV_ROUTE_EARPIECE;

    return PJ_SUCCESS;
}


/* API: create stream */
static pj_status_t factory_create_stream(pjmedia_aud_dev_factory *f,
					 const pjmedia_aud_dev_param *param,
					 pjmedia_aud_rec_cb rec_cb,
					 pjmedia_aud_play_cb play_cb,
					 void *user_data,
					 pjmedia_aud_stream **p_aud_strm)
{
    struct aps_factory *af = (struct aps_factory*)f;
    pj_pool_t *pool;
    struct aps_stream *strm;

    CPjAudioSetting aps_setting;
    PjAudioCallback aps_rec_cb;
    PjAudioCallback aps_play_cb;

    /* Can only support 16bits per sample */
    PJ_ASSERT_RETURN(param->bits_per_sample == BITS_PER_SAMPLE, PJ_EINVAL);

    /* Create and Initialize stream descriptor */
    pool = pj_pool_create(af->pf, "aps-dev", 1000, 1000, NULL);
    PJ_ASSERT_RETURN(pool, PJ_ENOMEM);

    strm = PJ_POOL_ZALLOC_T(pool, struct aps_stream);
    strm->pool = pool;
    strm->param = *param;

    if (strm->param.flags & PJMEDIA_AUD_DEV_CAP_EXT_FORMAT == 0)
	strm->param.ext_fmt.id = PJMEDIA_FORMAT_L16;
	
    /* Set audio engine fourcc. */
    switch(strm->param.ext_fmt.id) {
    case PJMEDIA_FORMAT_L16:
    case PJMEDIA_FORMAT_PCMU:
    case PJMEDIA_FORMAT_PCMA:
	aps_setting.fourcc = TFourCC(KMCPFourCCIdG711);
	break;
    case PJMEDIA_FORMAT_AMR:
	aps_setting.fourcc = TFourCC(KMCPFourCCIdAMRNB);
	break;
    case PJMEDIA_FORMAT_G729:
	aps_setting.fourcc = TFourCC(KMCPFourCCIdG729);
	break;
    case PJMEDIA_FORMAT_ILBC:
	aps_setting.fourcc = TFourCC(KMCPFourCCIdILBC);
	break;
    default:
	aps_setting.fourcc = 0;
	break;
    }

    /* Set audio engine mode. */
    if (strm->param.ext_fmt.id == PJMEDIA_FORMAT_AMR)
    {
	aps_setting.mode = (TAPSCodecMode)strm->param.ext_fmt.bitrate;
    } 
    else if (strm->param.ext_fmt.id == PJMEDIA_FORMAT_PCMU ||
	     strm->param.ext_fmt.id == PJMEDIA_FORMAT_L16 ||
	    (strm->param.ext_fmt.id == PJMEDIA_FORMAT_ILBC  &&
	     strm->param.ext_fmt.bitrate != 15200))
    {
	aps_setting.mode = EULawOr30ms;
    } 
    else if (strm->param.ext_fmt.id == PJMEDIA_FORMAT_PCMA ||
	    (strm->param.ext_fmt.id == PJMEDIA_FORMAT_ILBC &&
	     strm->param.ext_fmt.bitrate == 15200))
    {
	aps_setting.mode = EALawOr20ms;
    }

    /* Disable VAD on L16 and G711. */
    if (strm->param.ext_fmt.id == PJMEDIA_FORMAT_PCMU ||
	strm->param.ext_fmt.id == PJMEDIA_FORMAT_PCMA ||
	strm->param.ext_fmt.id == PJMEDIA_FORMAT_L16)
    {
	aps_setting.vad = EFalse;
    } else {
	aps_setting.vad = strm->param.ext_fmt.vad;
    }
    
    /* Set other audio engine attributes. */
    aps_setting.plc = strm->param.plc_enabled;
    aps_setting.cng = aps_setting.vad;
    aps_setting.loudspk = 
		strm->param.out_route==PJMEDIA_AUD_DEV_ROUTE_LOUDSPEAKER;

    /* Set audio engine callbacks. */
    if (strm->param.ext_fmt.id == PJMEDIA_FORMAT_L16) {
	aps_play_cb = &PlayCbPcm;
	aps_rec_cb  = &RecCbPcm;
    } else {
	aps_play_cb = &PlayCb;
	aps_rec_cb  = &RecCb;
    }

    /* Create the audio engine. */
    TRAPD(err, strm->engine = CPjAudioEngine::NewL(strm,
						   aps_rec_cb, aps_play_cb,
						   strm, aps_setting));
    if (err != KErrNone) {
    	pj_pool_release(pool);
	return PJ_RETURN_OS_ERROR(err);
    }

    strm->rec_cb = rec_cb;
    strm->play_cb = play_cb;
    strm->user_data = user_data;

    /* play_buf size is samples per frame. */
    strm->play_buf = (pj_int16_t*)pj_pool_zalloc(pool, 
					strm->param.samples_per_frame << 1);
    strm->play_buf_len = 0;
    strm->play_buf_start = 0;

    /* rec_buf size is samples per frame. */
    strm->rec_buf  = (pj_int16_t*)pj_pool_zalloc(pool, 
					strm->param.samples_per_frame << 1);
    strm->rec_buf_len = 0;

    if (strm->param.ext_fmt.id == PJMEDIA_FORMAT_G729) {
	TBitStream *g729_bitstream = new TBitStream;
	
	PJ_ASSERT_RETURN(g729_bitstream, PJ_ENOMEM);
	strm->strm_data = (void*)g729_bitstream;
    }
	
    /* Done */
    strm->base.op = &stream_op;
    *p_aud_strm = &strm->base;

    return PJ_SUCCESS;
}

/* API: Get stream info. */
static pj_status_t stream_get_param(pjmedia_aud_stream *s,
				    pjmedia_aud_dev_param *pi)
{
    struct aps_stream *strm = (struct aps_stream*)s;

    PJ_ASSERT_RETURN(strm && pi, PJ_EINVAL);

    pj_memcpy(pi, &strm->param, sizeof(*pi));
    
    return PJ_SUCCESS;
}

/* API: get capability */
static pj_status_t stream_get_cap(pjmedia_aud_stream *s,
				  pjmedia_aud_dev_cap cap,
				  void *pval)
{
    struct aps_stream *strm = (struct aps_stream*)s;
    pj_status_t status = PJ_ENOTSUP;

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    switch (cap) {
    case PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE: 
	if (strm->param.dir & PJMEDIA_DIR_PLAYBACK) {
	    *(pjmedia_aud_dev_route*)pval = strm->param.out_route;
	    status = PJ_SUCCESS;
	}
	break;
    case PJMEDIA_AUD_DEV_CAP_INPUT_VOLUME_SETTING:
	if (strm->param.dir & PJMEDIA_DIR_CAPTURE) {
	    PJ_ASSERT_RETURN(strm->engine, PJ_EINVAL);
	    
	    TInt max_gain = strm->engine->GetMaxGain();
	    TInt gain = strm->engine->GetGain();
	    
	    if (max_gain > 0 && gain >= 0) {
		*(unsigned*)pval = gain * 100 / max_gain; 
		status = PJ_SUCCESS;
	    } else {
		status = PJMEDIA_EAUD_NOTREADY;
	    }
	}
	break;
    case PJMEDIA_AUD_DEV_CAP_OUTPUT_VOLUME_SETTING:
	if (strm->param.dir & PJMEDIA_DIR_PLAYBACK) {
	    PJ_ASSERT_RETURN(strm->engine, PJ_EINVAL);
	    
	    TInt max_vol = strm->engine->GetMaxVolume();
	    TInt vol = strm->engine->GetVolume();
	    
	    if (max_vol > 0 && vol >= 0) {
		*(unsigned*)pval = vol * 100 / max_vol; 
		status = PJ_SUCCESS;
	    } else {
		status = PJMEDIA_EAUD_NOTREADY;
	    }
	}
	break;
    default:
	break;
    }
    
    return status;
}

/* API: set capability */
static pj_status_t stream_set_cap(pjmedia_aud_stream *s,
				  pjmedia_aud_dev_cap cap,
				  const void *pval)
{
    struct aps_stream *strm = (struct aps_stream*)s;
    pj_status_t status = PJ_ENOTSUP;

    PJ_ASSERT_RETURN(s && pval, PJ_EINVAL);

    switch (cap) {
    case PJMEDIA_AUD_DEV_CAP_OUTPUT_ROUTE: 
	if (strm->param.dir & PJMEDIA_DIR_PLAYBACK) {
	    pjmedia_aud_dev_route r = *(const pjmedia_aud_dev_route*)pval;
	    TInt err;

	    PJ_ASSERT_RETURN(strm->engine, PJ_EINVAL);
	    
	    switch (r) {
	    case PJMEDIA_AUD_DEV_ROUTE_DEFAULT:
	    case PJMEDIA_AUD_DEV_ROUTE_EARPIECE:
		err = strm->engine->ActivateSpeaker(EFalse);
		status = (err==KErrNone)? PJ_SUCCESS:PJ_RETURN_OS_ERROR(err);
		break;
	    case PJMEDIA_AUD_DEV_ROUTE_LOUDSPEAKER:
		err = strm->engine->ActivateSpeaker(ETrue);
		status = (err==KErrNone)? PJ_SUCCESS:PJ_RETURN_OS_ERROR(err);
		break;
	    default:
		status = PJ_EINVAL;
		break;
	    }
	    if (status == PJ_SUCCESS)
		strm->param.out_route = r; 
	}
	break;
    case PJMEDIA_AUD_DEV_CAP_INPUT_VOLUME_SETTING:
	if (strm->param.dir & PJMEDIA_DIR_CAPTURE) {
	    PJ_ASSERT_RETURN(strm->engine, PJ_EINVAL);
	    
	    TInt max_gain = strm->engine->GetMaxGain();
	    if (max_gain > 0) {
		TInt gain, err;
		
		gain = *(unsigned*)pval * max_gain / 100;
		err = strm->engine->SetGain(gain);
		status = (err==KErrNone)? PJ_SUCCESS:PJ_RETURN_OS_ERROR(err);
	    } else {
		status = PJMEDIA_EAUD_NOTREADY;
	    }
	}
	break;
    case PJMEDIA_AUD_DEV_CAP_OUTPUT_VOLUME_SETTING:
	if (strm->param.dir & PJMEDIA_DIR_CAPTURE) {
	    PJ_ASSERT_RETURN(strm->engine, PJ_EINVAL);
	    
	    TInt max_vol = strm->engine->GetMaxVolume();
	    if (max_vol > 0) {
		TInt vol, err;
		
		vol = *(unsigned*)pval * max_vol / 100;
		err = strm->engine->SetVolume(vol);
		status = (err==KErrNone)? PJ_SUCCESS:PJ_RETURN_OS_ERROR(err);
	    } else {
		status = PJMEDIA_EAUD_NOTREADY;
	    }
	}
	break;
    default:
	break;
    }
    
    return status;
}

/* API: Start stream. */
static pj_status_t stream_start(pjmedia_aud_stream *strm)
{
    struct aps_stream *stream = (struct aps_stream*)strm;

    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if (stream->engine) {
	TInt err = stream->engine->StartL();
    	if (err != KErrNone)
    	    return PJ_RETURN_OS_ERROR(err);
    }

    return PJ_SUCCESS;
}

/* API: Stop stream. */
static pj_status_t stream_stop(pjmedia_aud_stream *strm)
{
    struct aps_stream *stream = (struct aps_stream*)strm;

    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    if (stream->engine) {
    	stream->engine->Stop();
    }

    return PJ_SUCCESS;
}


/* API: Destroy stream. */
static pj_status_t stream_destroy(pjmedia_aud_stream *strm)
{
    struct aps_stream *stream = (struct aps_stream*)strm;

    PJ_ASSERT_RETURN(stream, PJ_EINVAL);

    stream_stop(strm);

    delete stream->engine;
    stream->engine = NULL;

    if (stream->param.ext_fmt.id == PJMEDIA_FORMAT_G729) {
	TBitStream *g729_bitstream = (TBitStream*)stream->strm_data;
	stream->strm_data = NULL;
	delete g729_bitstream;
    }

    pj_pool_t *pool;
    pool = stream->pool;
    if (pool) {
    	stream->pool = NULL;
    	pj_pool_release(pool);
    }

    return PJ_SUCCESS;
}

