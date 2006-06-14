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
#include <pjmedia/conference.h>
#include <pjmedia/errno.h>
#include <pjmedia/port.h>
#include <pjmedia/resample.h>
#include <pjmedia/silencedet.h>
#include <pjmedia/sound_port.h>
#include <pjmedia/stream.h>
#include <pj/assert.h>
#include <pj/log.h>
#include <pj/pool.h>
#include <pj/string.h>

/* CONF_DEBUG enables detailed operation of the conference bridge.
 * Beware that it prints large amounts of logs (several lines per frame).
 */
//#define CONF_DEBUG
#ifdef CONF_DEBUG
#   include <stdio.h>
#   define TRACE_(x)   PJ_LOG(5,x)
#else
#   define TRACE_(x)
#endif


/* REC_FILE macro enables recording of the samples written to the sound
 * device. The file contains RAW PCM data with no header, and has the
 * same settings (clock rate etc) as the conference bridge.
 * This should only be enabled when debugging audio quality *only*.
 */
//#define REC_FILE    "confrec.pcm"
#ifdef REC_FILE
static FILE *fhnd_rec;
#endif


#define THIS_FILE	"conference.c"
#define RX_BUF_COUNT	8

#define BYTES_PER_SAMPLE    2

#define NORMAL_LEVEL	    128


/*
 * DON'T GET CONFUSED WITH TX/RX!!
 *
 * TX and RX directions are always viewed from the conference bridge's point
 * of view, and NOT from the port's point of view. So TX means the bridge
 * is transmitting to the port, RX means the bridge is receiving from the
 * port.
 */


/**
 * This is a port connected to conference bridge.
 */
struct conf_port
{
    pj_str_t		 name;		/**< Port name.			    */
    pjmedia_port	*port;		/**< get_frame() and put_frame()    */
    pjmedia_port_op	 rx_setting;	/**< Can we receive from this port  */
    pjmedia_port_op	 tx_setting;	/**< Can we transmit to this port   */
    int			 listener_cnt;	/**< Number of listeners.	    */
    pj_bool_t		*listeners;	/**< Array of listeners.	    */
    pjmedia_silence_det	*vad;		/**< VAD for this port.		    */

    /* Shortcut for port info. */
    unsigned		 clock_rate;	/**< Port's clock rate.		    */
    unsigned		 samples_per_frame; /**< Port's samples per frame.  */

    /* Calculated signal levels: */
    pj_bool_t		 need_tx_level;	/**< Need to calculate tx level?    */
    unsigned		 tx_level;	/**< Last tx level to this port.    */
    unsigned		 rx_level;	/**< Last rx level from this port.  */

    /* The normalized signal level adjustment.
     * A value of 128 (NORMAL_LEVEL) means there's no adjustment.
     */
    unsigned		 tx_adj_level;	/**< Adjustment for TX.		    */
    unsigned		 rx_adj_level;	/**< Adjustment for RX.		    */

    /* Resample, for converting clock rate, if they're different. */
    pjmedia_resample	*rx_resample;
    pjmedia_resample	*tx_resample;

    /* RX buffer is temporary buffer to be used when there is mismatch
     * between port's sample rate or ptime with conference's sample rate
     * or ptime. The buffer is used for sampling rate conversion AND/OR to
     * buffer the samples until there are enough samples to fulfill a 
     * complete frame to be processed by the bridge.
     *
     * When both sample rate AND ptime of the port match the conference 
     * settings, this buffer will not be created.
     * 
     * This buffer contains samples at port's clock rate.
     * The size of this buffer is the sum between port's samples per frame
     * and bridge's samples per frame.
     */
    pj_int16_t		*rx_buf;	/**< The RX buffer.		    */
    unsigned		 rx_buf_cap;	/**< Max size, in samples	    */
    unsigned		 rx_buf_count;	/**< # of samples in the buf.	    */

    /* Mix buf is a temporary buffer used to calculate the average signal
     * received by this port from all other ports. Samples from all ports
     * that are transmitting to this port will be accumulated here, then
     * they will be divided by the sources count before the samples are put
     * to the TX buffer of this port.
     *
     * This buffer contains samples at bridge's clock rate.
     * The size of this buffer is equal to samples per frame of the bridge.
     *
     * Note that the samples here are unsigned 32bit.
     */
    unsigned		 sources;	/**< Number of sources.		    */
    pj_uint32_t		*mix_buf;	/**< Total sum of signal.	    */

    /* Tx buffer is a temporary buffer to be used when there's mismatch 
     * between port's clock rate or ptime with conference's sample rate
     * or ptime. This buffer is used as the source of the sampling rate
     * conversion AND/OR to buffer the samples until there are enough
     * samples to fulfill a complete frame to be transmitted to the port.
     *
     * When both sample rate and ptime of the port match the bridge's 
     * settings, this buffer will not be created.
     * 
     * This buffer contains samples at port's clock rate.
     * The size of this buffer is the sum between port's samples per frame
     * and bridge's samples per frame.
     */
    pj_int16_t		*tx_buf;	/**< Tx buffer.			    */
    unsigned		 tx_buf_cap;	/**< Max size, in samples.	    */
    unsigned		 tx_buf_count;	/**< # of samples in the buffer.    */

    /* Snd buffers is a special buffer for sound device port (port 0, master
     * port). It's not used by other ports.
     *
     * There are multiple numbers of this buffer, because we can not expect
     * the mic and speaker thread to run equally after one another. In most
     * systems, each thread will run multiple times before the other thread
     * gains execution time. For example, in my system, mic thread is called
     * three times, then speaker thread is called three times, and so on.
     */
    int			 snd_write_pos, snd_read_pos;
    pj_int16_t		*snd_buf[RX_BUF_COUNT];	/**< Buffer 		    */
};


/*
 * Conference bridge.
 */
struct pjmedia_conf
{
    unsigned		  options;	/**< Bitmask options.		    */
    unsigned		  max_ports;	/**< Maximum ports.		    */
    unsigned		  port_cnt;	/**< Current number of ports.	    */
    unsigned		  connect_cnt;	/**< Total number of connections    */
    pjmedia_snd_port	 *snd_dev_port;	/**< Sound device port.		    */
    pjmedia_port	 *master_port;	/**< Port zero's port.		    */
    pj_mutex_t		 *mutex;	/**< Conference mutex.		    */
    struct conf_port	**ports;	/**< Array of ports.		    */
    pj_uint16_t		 *uns_buf;	/**< Buf for unsigned conversion    */
    unsigned		  clock_rate;	/**< Sampling rate.		    */
    unsigned		  channel_count;/**< Number of channels (1=mono).   */
    unsigned		  samples_per_frame;	/**< Samples per frame.	    */
    unsigned		  bits_per_sample;	/**< Bits per sample.	    */
};


/* Extern */
unsigned char linear2ulaw(int pcm_val);

/* Prototypes */
static pj_status_t put_frame(pjmedia_port *this_port, 
			     const pjmedia_frame *frame);
static pj_status_t get_frame(pjmedia_port *this_port, 
			     pjmedia_frame *frame);
static pj_status_t destroy_port(pjmedia_port *this_port);


/*
 * Create port.
 */
static pj_status_t create_conf_port( pj_pool_t *pool,
				     pjmedia_conf *conf,
				     pjmedia_port *port,
				     const pj_str_t *name,
				     struct conf_port **p_conf_port)
{
    struct conf_port *conf_port;
    pj_status_t status;

    /* Create port. */
    conf_port = pj_pool_zalloc(pool, sizeof(struct conf_port));
    PJ_ASSERT_RETURN(conf_port, PJ_ENOMEM);

    /* Set name */
    pj_strdup(pool, &conf_port->name, name);

    /* Default has tx and rx enabled. */
    conf_port->rx_setting = PJMEDIA_PORT_ENABLE;
    conf_port->tx_setting = PJMEDIA_PORT_ENABLE;

    /* Default level adjustment is 128 (which means no adjustment) */
    conf_port->tx_adj_level = NORMAL_LEVEL;
    conf_port->rx_adj_level = NORMAL_LEVEL;

    /* Create transmit flag array */
    conf_port->listeners = pj_pool_zalloc(pool, 
					  conf->max_ports*sizeof(pj_bool_t));
    PJ_ASSERT_RETURN(conf_port->listeners, PJ_ENOMEM);


    /* Save some port's infos, for convenience. */
    if (port) {
	conf_port->port = port;
	conf_port->clock_rate = port->info.clock_rate;
	conf_port->samples_per_frame = port->info.samples_per_frame;
    } else {
	conf_port->port = NULL;
	conf_port->clock_rate = conf->clock_rate;
	conf_port->samples_per_frame = conf->samples_per_frame;
    }

    /* Create and init vad. */
    status = pjmedia_silence_det_create( pool, 
					 conf_port->clock_rate,
					 conf_port->samples_per_frame,
					 &conf_port->vad);
    if (status != PJ_SUCCESS)
	return status;

    /* Set fixed */
    pjmedia_silence_det_set_fixed(conf_port->vad, 2);


    /* If port's clock rate is different than conference's clock rate,
     * create a resample sessions.
     */
    if (conf_port->clock_rate != conf->clock_rate) {

	pj_bool_t high_quality;
	pj_bool_t large_filter;

	high_quality = ((conf->options & PJMEDIA_CONF_USE_LINEAR)==0);
	large_filter = ((conf->options & PJMEDIA_CONF_SMALL_FILTER)==0);

	/* Create resample for rx buffer. */
	status = pjmedia_resample_create( pool, 
					  high_quality,
					  large_filter,
					  conf_port->clock_rate,/* Rate in */
					  conf->clock_rate, /* Rate out */
					  conf->samples_per_frame * 
					    conf_port->clock_rate /
					    conf->clock_rate,
					  &conf_port->rx_resample);
	if (status != PJ_SUCCESS)
	    return status;


	/* Create resample for tx buffer. */
	status = pjmedia_resample_create(pool,
					 high_quality,
					 large_filter,
					 conf->clock_rate,  /* Rate in */
					 conf_port->clock_rate, /* Rate out */
					 conf->samples_per_frame,
					 &conf_port->tx_resample);
	if (status != PJ_SUCCESS)
	    return status;
    }

    /*
     * Initialize rx and tx buffer, only when port's samples per frame or 
     * port's clock rate is different then the conference bridge settings.
     */
    if (conf_port->clock_rate != conf->clock_rate ||
	conf_port->samples_per_frame != conf->samples_per_frame)
    {
	/* Create RX buffer. */
	conf_port->rx_buf_cap = (unsigned)(conf_port->samples_per_frame +
					   conf->samples_per_frame * 
					   conf_port->clock_rate * 1.0 /
					   conf->clock_rate);
	conf_port->rx_buf_count = 0;
	conf_port->rx_buf = pj_pool_alloc(pool, conf_port->rx_buf_cap *
						sizeof(conf_port->rx_buf[0]));
	PJ_ASSERT_RETURN(conf_port->rx_buf, PJ_ENOMEM);

	/* Create TX buffer. */
	conf_port->tx_buf_cap = conf_port->rx_buf_cap;
	conf_port->tx_buf_count = 0;
	conf_port->tx_buf = pj_pool_alloc(pool, conf_port->tx_buf_cap *
						sizeof(conf_port->tx_buf[0]));
	PJ_ASSERT_RETURN(conf_port->tx_buf, PJ_ENOMEM);
    }


    /* Create mix buffer. */
    conf_port->mix_buf = pj_pool_zalloc(pool, conf->samples_per_frame *
					      sizeof(conf_port->mix_buf[0]));
    PJ_ASSERT_RETURN(conf_port->mix_buf, PJ_ENOMEM);


    /* Done */
    *p_conf_port = conf_port;
    return PJ_SUCCESS;
}

/*
 * Create port zero for the sound device.
 */
static pj_status_t create_sound_port( pj_pool_t *pool,
				      pjmedia_conf *conf )
{
    struct conf_port *conf_port;
    pj_str_t name = { "Master/sound", 12 };
    unsigned i;
    pj_status_t status;



    /* Create port */
    status = create_conf_port(pool, conf, NULL, &name, &conf_port);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Sound device has rx buffers. */
    for (i=0; i<RX_BUF_COUNT; ++i) {
	conf_port->snd_buf[i] = pj_pool_zalloc(pool, conf->samples_per_frame *
					      sizeof(conf_port->snd_buf[0][0]));
	if (conf_port->snd_buf[i] == NULL) {
	    status = PJ_ENOMEM;
	    goto on_error;
	}
    }
    conf_port->snd_write_pos = 0;
    conf_port->snd_read_pos = 0;


     /* Set to port zero */
    conf->ports[0] = conf_port;
    conf->port_cnt++;


    /* Create sound device port: */

    if ((conf->options & PJMEDIA_CONF_NO_DEVICE) == 0) {

	/*
	 * If capture is disabled then create player only port.
	 * Otherwise create bidirectional sound device port.
	 */
	if (conf->options & PJMEDIA_CONF_NO_MIC)  {
	    status = pjmedia_snd_port_create_player(pool, -1, conf->clock_rate,
						    conf->channel_count,
						    conf->samples_per_frame,
						    conf->bits_per_sample, 
						    0,	/* options */
						    &conf->snd_dev_port);

	} else {
	    status = pjmedia_snd_port_create( pool, -1, -1, conf->clock_rate, 
					      conf->channel_count, 
					      conf->samples_per_frame,
					      conf->bits_per_sample,
					      0,    /* Options */
					      &conf->snd_dev_port);
	}

	if (status != PJ_SUCCESS)
	    return status;
    }


    PJ_LOG(5,(THIS_FILE, "Sound device successfully created for port 0"));
    return PJ_SUCCESS;

on_error:
    return status;

}

/*
 * Create conference bridge.
 */
PJ_DEF(pj_status_t) pjmedia_conf_create( pj_pool_t *pool,
					 unsigned max_ports,
					 unsigned clock_rate,
					 unsigned channel_count,
					 unsigned samples_per_frame,
					 unsigned bits_per_sample,
					 unsigned options,
					 pjmedia_conf **p_conf )
{
    pjmedia_conf *conf;
    pj_status_t status;

    /* Can only accept 16bits per sample, for now.. */
    PJ_ASSERT_RETURN(bits_per_sample == 16, PJ_EINVAL);

    PJ_LOG(5,(THIS_FILE, "Creating conference bridge with %d ports",
	      max_ports));

    /* Create and init conf structure. */
    conf = pj_pool_zalloc(pool, sizeof(pjmedia_conf));
    PJ_ASSERT_RETURN(conf, PJ_ENOMEM);

    conf->ports = pj_pool_zalloc(pool, max_ports*sizeof(void*));
    PJ_ASSERT_RETURN(conf->ports, PJ_ENOMEM);

    conf->options = options;
    conf->max_ports = max_ports;
    conf->clock_rate = clock_rate;
    conf->channel_count = channel_count;
    conf->samples_per_frame = samples_per_frame;
    conf->bits_per_sample = bits_per_sample;

    
    /* Create and initialize the master port interface. */
    conf->master_port = pj_pool_zalloc(pool, sizeof(pjmedia_port));
    PJ_ASSERT_RETURN(conf->master_port, PJ_ENOMEM);
    
    conf->master_port->info.bits_per_sample = bits_per_sample;
    conf->master_port->info.bytes_per_frame = samples_per_frame *
					      bits_per_sample / 8;
    conf->master_port->info.channel_count = channel_count;
    conf->master_port->info.encoding_name = pj_str("pcm");
    conf->master_port->info.has_info = 1;
    conf->master_port->info.name = pj_str("sound-dev");
    conf->master_port->info.need_info = 0;
    conf->master_port->info.pt = 0xFF;
    conf->master_port->info.clock_rate = clock_rate;
    conf->master_port->info.samples_per_frame = samples_per_frame;
    conf->master_port->info.signature = 0;
    conf->master_port->info.type = PJMEDIA_TYPE_AUDIO;

    conf->master_port->get_frame = &get_frame;
    conf->master_port->put_frame = &put_frame;
    conf->master_port->on_destroy = &destroy_port;

    conf->master_port->user_data = conf;


    /* Create port zero for sound device. */
    status = create_sound_port(pool, conf);
    if (status != PJ_SUCCESS)
	return status;

    /* Create temporary buffer. */
    conf->uns_buf = pj_pool_zalloc(pool, samples_per_frame *
					 sizeof(conf->uns_buf[0]));

    /* Create mutex. */
    status = pj_mutex_create_recursive(pool, "conf", &conf->mutex);
    if (status != PJ_SUCCESS)
	return status;

    /* If sound device was created, connect sound device to the
     * master port.
     */
    if (conf->snd_dev_port) {
	status = pjmedia_snd_port_connect( conf->snd_dev_port, 
					   conf->master_port );
	if (status != PJ_SUCCESS) {
	    pjmedia_conf_destroy(conf);
	    return status;
	}
    }


    /* Done */

    *p_conf = conf;

    return PJ_SUCCESS;
}


/*
 * Pause sound device.
 */
static pj_status_t pause_sound( pjmedia_conf *conf )
{
    /* Do nothing. */
    PJ_UNUSED_ARG(conf);
    return PJ_SUCCESS;
}

/*
 * Resume sound device.
 */
static pj_status_t resume_sound( pjmedia_conf *conf )
{
    /* Do nothing. */
    PJ_UNUSED_ARG(conf);
    return PJ_SUCCESS;
}


/**
 * Destroy conference bridge.
 */
PJ_DEF(pj_status_t) pjmedia_conf_destroy( pjmedia_conf *conf )
{
    PJ_ASSERT_RETURN(conf != NULL, PJ_EINVAL);

    /* Destroy sound device port. */
    if (conf->snd_dev_port) {
	pjmedia_snd_port_destroy(conf->snd_dev_port);
	conf->snd_dev_port = NULL;
    }

    /* Destroy mutex */
    pj_mutex_destroy(conf->mutex);

    return PJ_SUCCESS;
}


/*
 * Destroy the master port (will destroy the conference)
 */
static pj_status_t destroy_port(pjmedia_port *this_port)
{
    pjmedia_conf *conf = this_port->user_data;
    return pjmedia_conf_destroy(conf);
}


/*
 * Get port zero interface.
 */
PJ_DEF(pjmedia_port*) pjmedia_conf_get_master_port(pjmedia_conf *conf)
{
    /* Sanity check. */
    PJ_ASSERT_RETURN(conf != NULL, NULL);

    /* Can only return port interface when PJMEDIA_CONF_NO_DEVICE was
     * present in the option.
     */
    PJ_ASSERT_RETURN((conf->options & PJMEDIA_CONF_NO_DEVICE) != 0, NULL);
    
    return conf->master_port;
}


/*
 * Add stream port to the conference bridge.
 */
PJ_DEF(pj_status_t) pjmedia_conf_add_port( pjmedia_conf *conf,
					   pj_pool_t *pool,
					   pjmedia_port *strm_port,
					   const pj_str_t *port_name,
					   unsigned *p_port )
{
    struct conf_port *conf_port;
    unsigned index;
    pj_status_t status;

    PJ_ASSERT_RETURN(conf && pool && strm_port, PJ_EINVAL);

    /* If port_name is not specified, use the port's name */
    if (!port_name)
	port_name = &strm_port->info.name;

    /* For this version of PJMEDIA, port MUST have the same number of
     * PCM channels.
     */
    if (strm_port->info.channel_count != conf->channel_count) {
	pj_assert(!"Number of channels mismatch");
	return PJMEDIA_ENCCHANNEL;
    }

    pj_mutex_lock(conf->mutex);

    if (conf->port_cnt >= conf->max_ports) {
	pj_assert(!"Too many ports");
	pj_mutex_unlock(conf->mutex);
	return PJ_ETOOMANY;
    }

    /* Find empty port in the conference bridge. */
    for (index=0; index < conf->max_ports; ++index) {
	if (conf->ports[index] == NULL)
	    break;
    }

    pj_assert(index != conf->max_ports);

    /* Create conf port structure. */
    status = create_conf_port(pool, conf, strm_port, port_name, &conf_port);
    if (status != PJ_SUCCESS) {
	pj_mutex_unlock(conf->mutex);
	return status;
    }

    /* Put the port. */
    conf->ports[index] = conf_port;
    conf->port_cnt++;

    /* Done. */
    if (p_port) {
	*p_port = index;
    }

    pj_mutex_unlock(conf->mutex);

    return PJ_SUCCESS;
}


/*
 * Change TX and RX settings for the port.
 */
PJ_DECL(pj_status_t) pjmedia_conf_configure_port( pjmedia_conf *conf,
						  unsigned slot,
						  pjmedia_port_op tx,
						  pjmedia_port_op rx)
{
    struct conf_port *conf_port;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && slot<conf->max_ports, PJ_EINVAL);

    /* Port must be valid. */
    PJ_ASSERT_RETURN(conf->ports[slot] != NULL, PJ_EINVAL);

    conf_port = conf->ports[slot];

    if (tx != PJMEDIA_PORT_NO_CHANGE)
	conf_port->tx_setting = tx;

    if (rx != PJMEDIA_PORT_NO_CHANGE)
	conf_port->rx_setting = rx;

    return PJ_SUCCESS;
}


/*
 * Connect port.
 */
PJ_DEF(pj_status_t) pjmedia_conf_connect_port( pjmedia_conf *conf,
					       unsigned src_slot,
					       unsigned sink_slot,
					       int level )
{
    struct conf_port *src_port, *dst_port;
    pj_bool_t start_sound = PJ_FALSE;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && src_slot<conf->max_ports && 
		     sink_slot<conf->max_ports, PJ_EINVAL);

    /* Ports must be valid. */
    PJ_ASSERT_RETURN(conf->ports[src_slot] != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(conf->ports[sink_slot] != NULL, PJ_EINVAL);

    /* For now, level MUST be zero. */
    PJ_ASSERT_RETURN(level == 0, PJ_EINVAL);

    pj_mutex_lock(conf->mutex);

    src_port = conf->ports[src_slot];
    dst_port = conf->ports[sink_slot];

    if (src_port->listeners[sink_slot] == 0) {
	src_port->listeners[sink_slot] = 1;
	++conf->connect_cnt;
	++src_port->listener_cnt;

	if (conf->connect_cnt == 1)
	    start_sound = 1;

	PJ_LOG(4,(THIS_FILE,"Port %.*s transmitting to port %.*s",
		  (int)src_port->name.slen,
		  src_port->name.ptr,
		  (int)dst_port->name.slen,
		  dst_port->name.ptr));
    }

    pj_mutex_unlock(conf->mutex);

    /* Sound device must be started without mutex, otherwise the
     * sound thread will deadlock (?)
     */
    if (start_sound)
	resume_sound(conf);

    return PJ_SUCCESS;
}


/*
 * Disconnect port
 */
PJ_DEF(pj_status_t) pjmedia_conf_disconnect_port( pjmedia_conf *conf,
						  unsigned src_slot,
						  unsigned sink_slot )
{
    struct conf_port *src_port, *dst_port;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && src_slot<conf->max_ports && 
		     sink_slot<conf->max_ports, PJ_EINVAL);

    /* Ports must be valid. */
    PJ_ASSERT_RETURN(conf->ports[src_slot] != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(conf->ports[sink_slot] != NULL, PJ_EINVAL);

    pj_mutex_lock(conf->mutex);

    src_port = conf->ports[src_slot];
    dst_port = conf->ports[sink_slot];

    if (src_port->listeners[sink_slot] != 0) {
	src_port->listeners[sink_slot] = 0;
	--conf->connect_cnt;
	--src_port->listener_cnt;

	PJ_LOG(4,(THIS_FILE,"Port %.*s stop transmitting to port %.*s",
		  (int)src_port->name.slen,
		  src_port->name.ptr,
		  (int)dst_port->name.slen,
		  dst_port->name.ptr));

	
    }

    pj_mutex_unlock(conf->mutex);

    if (conf->connect_cnt == 0) {
	pause_sound(conf);
    }

    return PJ_SUCCESS;
}


/*
 * Remove the specified port.
 */
PJ_DEF(pj_status_t) pjmedia_conf_remove_port( pjmedia_conf *conf,
					      unsigned port )
{
    struct conf_port *conf_port;
    unsigned i;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && port < conf->max_ports, PJ_EINVAL);

    /* Port must be valid. */
    PJ_ASSERT_RETURN(conf->ports[port] != NULL, PJ_EINVAL);

    /* Suspend the sound devices.
     * Don't want to remove port while port is being accessed by sound
     * device's threads!
     */

    pj_mutex_lock(conf->mutex);

    conf_port = conf->ports[port];
    conf_port->tx_setting = PJMEDIA_PORT_DISABLE;
    conf_port->rx_setting = PJMEDIA_PORT_DISABLE;

    /* Remove this port from transmit array of other ports. */
    for (i=0; i<conf->max_ports; ++i) {
	conf_port = conf->ports[i];

	if (!conf_port)
	    continue;

	if (conf_port->listeners[port] != 0) {
	    --conf->connect_cnt;
	    --conf_port->listener_cnt;
	    conf_port->listeners[port] = 0;
	}
    }

    /* Remove all ports listening from this port. */
    conf_port = conf->ports[port];
    for (i=0; i<conf->max_ports; ++i) {
	if (conf_port->listeners[i]) {
	    --conf->connect_cnt;
	    --conf_port->listener_cnt;
	}
    }

    /* Remove the port. */
    conf->ports[port] = NULL;
    --conf->port_cnt;

    pj_mutex_unlock(conf->mutex);


    /* Stop sound if there's no connection. */
    if (conf->connect_cnt == 0) {
	pause_sound(conf);
    }

    return PJ_SUCCESS;
}


/*
 * Enum ports.
 */
PJ_DEF(pj_status_t) pjmedia_conf_enum_ports( pjmedia_conf *conf,
					     unsigned ports[],
					     unsigned *p_count )
{
    unsigned i, count=0;

    PJ_ASSERT_RETURN(conf && p_count && ports, PJ_EINVAL);

    for (i=0; i<conf->max_ports && count<*p_count; ++i) {
	if (!conf->ports[i])
	    continue;

	ports[count++] = i;
    }

    *p_count = count;
    return PJ_SUCCESS;
}

/*
 * Get port info
 */
PJ_DEF(pj_status_t) pjmedia_conf_get_port_info( pjmedia_conf *conf,
						unsigned slot,
						pjmedia_conf_port_info *info)
{
    struct conf_port *conf_port;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && slot<conf->max_ports, PJ_EINVAL);

    /* Port must be valid. */
    PJ_ASSERT_RETURN(conf->ports[slot] != NULL, PJ_EINVAL);

    conf_port = conf->ports[slot];

    info->slot = slot;
    info->name = conf_port->name;
    info->tx_setting = conf_port->tx_setting;
    info->rx_setting = conf_port->rx_setting;
    info->listener = conf_port->listeners;
    info->clock_rate = conf_port->clock_rate;
    info->channel_count = conf->channel_count;
    info->samples_per_frame = conf_port->samples_per_frame;
    info->bits_per_sample = conf->bits_per_sample;
    info->tx_adj_level = conf_port->tx_adj_level - NORMAL_LEVEL;
    info->rx_adj_level = conf_port->rx_adj_level - NORMAL_LEVEL;

    return PJ_SUCCESS;
}


PJ_DEF(pj_status_t) pjmedia_conf_get_ports_info(pjmedia_conf *conf,
						unsigned *size,
						pjmedia_conf_port_info info[])
{
    unsigned i, count=0;

    PJ_ASSERT_RETURN(conf && size && info, PJ_EINVAL);

    for (i=0; i<conf->max_ports && count<*size; ++i) {
	if (!conf->ports[i])
	    continue;

	pjmedia_conf_get_port_info(conf, i, &info[count]);
	++count;
    }

    *size = count;
    return PJ_SUCCESS;
}


/*
 * Get signal level.
 */
PJ_DEF(pj_status_t) pjmedia_conf_get_signal_level( pjmedia_conf *conf,
						   unsigned slot,
						   unsigned *tx_level,
						   unsigned *rx_level)
{
    struct conf_port *conf_port;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && slot<conf->max_ports, PJ_EINVAL);

    /* Port must be valid. */
    PJ_ASSERT_RETURN(conf->ports[slot] != NULL, PJ_EINVAL);

    conf_port = conf->ports[slot];

    if (tx_level != NULL) {
	conf_port->need_tx_level = 1;
	*tx_level = conf_port->tx_level;
    }

    if (rx_level != NULL) 
	*rx_level = conf_port->rx_level;

    return PJ_SUCCESS;
}


/*
 * Adjust RX level of individual port.
 */
PJ_DEF(pj_status_t) pjmedia_conf_adjust_rx_level( pjmedia_conf *conf,
						  unsigned slot,
						  int adj_level )
{
    struct conf_port *conf_port;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && slot<conf->max_ports, PJ_EINVAL);

    /* Port must be valid. */
    PJ_ASSERT_RETURN(conf->ports[slot] != NULL, PJ_EINVAL);

    /* Value must be from -128 to +127 */
    PJ_ASSERT_RETURN(adj_level >= -128 && adj_level <= 127, PJ_EINVAL);

    conf_port = conf->ports[slot];

    /* Set normalized adjustment level. */
    conf_port->rx_adj_level = adj_level + NORMAL_LEVEL;

    return PJ_SUCCESS;
}


/*
 * Adjust TX level of individual port.
 */
PJ_DEF(pj_status_t) pjmedia_conf_adjust_tx_level( pjmedia_conf *conf,
						  unsigned slot,
						  int adj_level )
{
    struct conf_port *conf_port;

    /* Check arguments */
    PJ_ASSERT_RETURN(conf && slot<conf->max_ports, PJ_EINVAL);

    /* Port must be valid. */
    PJ_ASSERT_RETURN(conf->ports[slot] != NULL, PJ_EINVAL);

    /* Value must be from -128 to +127 */
    PJ_ASSERT_RETURN(adj_level >= -128 && adj_level <= 127, PJ_EINVAL);

    conf_port = conf->ports[slot];

    /* Set normalized adjustment level. */
    conf_port->tx_adj_level = adj_level + NORMAL_LEVEL;

    return PJ_SUCCESS;
}


/* Convert signed 16bit pcm sample to unsigned 16bit sample */
static pj_uint16_t pcm2unsigned(pj_int32_t pcm)
{
    return (pj_uint16_t)(pcm + 32767);
}

/* Convert unsigned 16bit sample to signed 16bit pcm sample */
static pj_int16_t unsigned2pcm(pj_uint32_t uns)
{
    return (pj_int16_t)(uns - 32767);
}


/*
 * Read from port.
 */
static pj_status_t read_port( pjmedia_conf *conf,
			      struct conf_port *cport, pj_int16_t *frame,
			      pj_size_t count, pjmedia_frame_type *type )
{

    pj_assert(count == conf->samples_per_frame);

    TRACE_((THIS_FILE, "read_port %.*s: count=%d", 
		       (int)cport->name.slen, cport->name.ptr,
		       count));

    /* If port's samples per frame and sampling rate matches conference
     * bridge's settings, get the frame directly from the port.
     */
    if (cport->rx_buf_cap == 0) {
	pjmedia_frame f;
	pj_status_t status;

	f.buf = frame;
	f.size = count * BYTES_PER_SAMPLE;

	TRACE_((THIS_FILE, "  get_frame %.*s: count=%d", 
		   (int)cport->name.slen, cport->name.ptr,
		   count));

	status = (cport->port->get_frame)(cport->port, &f);

	*type = f.type;

	return status;

    } else {

	/*
	 * If we don't have enough samples in rx_buf, read from the port 
	 * first. Remember that rx_buf may be in different clock rate!
	 */
	while (cport->rx_buf_count < count * 1.0 *
		cport->clock_rate / conf->clock_rate) {

	    pjmedia_frame f;
	    pj_status_t status;

	    f.buf = cport->rx_buf + cport->rx_buf_count;
	    f.size = cport->samples_per_frame * BYTES_PER_SAMPLE;

	    TRACE_((THIS_FILE, "  get_frame, count=%d", 
		       cport->samples_per_frame));

	    status = pjmedia_port_get_frame(cport->port, &f);

	    if (status != PJ_SUCCESS) {
		/* Fatal error! */
		return status;
	    }

	    if (f.type != PJMEDIA_FRAME_TYPE_AUDIO) {
		TRACE_((THIS_FILE, "  get_frame returned non-audio"));
		pjmedia_zero_samples( cport->rx_buf + cport->rx_buf_count,
				      cport->samples_per_frame);
	    }

	    cport->rx_buf_count += cport->samples_per_frame;

	    TRACE_((THIS_FILE, "  rx buffer size is now %d",
		    cport->rx_buf_count));

	    pj_assert(cport->rx_buf_count <= cport->rx_buf_cap);
	}

	/*
	 * If port's clock_rate is different, resample.
	 * Otherwise just copy.
	 */
	if (cport->clock_rate != conf->clock_rate) {
	    
	    unsigned src_count;

	    TRACE_((THIS_FILE, "  resample, input count=%d", 
		    pjmedia_resample_get_input_size(cport->rx_resample)));

	    pjmedia_resample_run( cport->rx_resample,cport->rx_buf, frame);

	    src_count = (unsigned)(count * 1.0 * cport->clock_rate / 
				   conf->clock_rate);
	    cport->rx_buf_count -= src_count;
	    if (cport->rx_buf_count) {
		pjmedia_copy_samples(cport->rx_buf, cport->rx_buf+src_count,
				     cport->rx_buf_count);
	    }

	    TRACE_((THIS_FILE, "  rx buffer size is now %d",
		    cport->rx_buf_count));

	} else {

	    pjmedia_copy_samples(frame, cport->rx_buf, count);
	    cport->rx_buf_count -= count;
	    if (cport->rx_buf_count) {
		pjmedia_copy_samples(cport->rx_buf, cport->rx_buf+count,
				     cport->rx_buf_count);
	    }
	}
    }

    return PJ_SUCCESS;
}


/*
 * Write the mixed signal to the port.
 */
static pj_status_t write_port(pjmedia_conf *conf, struct conf_port *cport,
			      pj_uint32_t timestamp)
{
    pj_int16_t *buf;
    unsigned j;
    pj_status_t status;

    /* If port is muted or nobody is transmitting to this port, 
     * transmit NULL frame. 
     */
    /* note:
     *  the "cport->sources==0" checking will cause discontinuous
     *  transmission for RTP stream.
     */
    if (cport->tx_setting == PJMEDIA_PORT_MUTE || cport->sources==0) {

	pjmedia_frame frame;

	frame.type = PJMEDIA_FRAME_TYPE_NONE;
	frame.buf = NULL;
	frame.size = 0;

	if (cport->port && cport->port->put_frame) {
	    pjmedia_port_put_frame(cport->port, &frame);
	}

	cport->tx_level = 0;
	return PJ_SUCCESS;

    } else if (cport->tx_setting != PJMEDIA_PORT_ENABLE) {
	cport->tx_level = 0;
	return PJ_SUCCESS;
    }

    /* If there are sources in the mix buffer, convert the mixed samples
     * to the mixed samples itself. This is possible because mixed sample
     * is 32bit.
     *
     * In addition to this process, if we need to change the level of
     * TX signal, we adjust is here too.
     */
    buf = (pj_int16_t*)cport->mix_buf;

    if (cport->tx_adj_level != NORMAL_LEVEL && cport->sources) {

	unsigned adj_level = cport->tx_adj_level;

	/* We need to adjust signal level. */
	for (j=0; j<conf->samples_per_frame; ++j) {
	    pj_int32_t itemp;

	    /* Calculate average level, and convert the sample to
	     * 16bit signed integer.
	     */
	    itemp = unsigned2pcm(cport->mix_buf[j] / cport->sources);

	    /* Adjust the level */
	    itemp = itemp * adj_level / NORMAL_LEVEL;

	    /* Clip the signal if it's too loud */
	    if (itemp > 32767) itemp = 32767;
	    else if (itemp < -32768) itemp = -32768;

	    /* Put back in the buffer. */
	    buf[j] = (pj_int16_t) itemp;
	}

    } else if (cport->sources) {
	/* No need to adjust signal level. */
	for (j=0; j<conf->samples_per_frame; ++j) {
	    buf[j] = unsigned2pcm(cport->mix_buf[j] / cport->sources);
	}
    } else {
	// Not necessarry. Buffer has been zeroed before.
	// pjmedia_zero_samples(buf, conf->samples_per_frame);
	pj_assert(buf[0] == 0);
    }

    /* Calculate TX level if we need to do so. 
     * This actually is not the most correct place to calculate TX signal 
     * level of the port; it should calculate the level of the actual
     * frame just before put_frame() is called.
     * But doing so would make the code more complicated than it is
     * necessary, since the purpose of level calculation mostly is just
     * for VU meter display. By doing it here, it should give the acceptable
     * indication of the signal level of the port.
     */
    if (cport->need_tx_level && cport->sources) {
	pj_uint32_t level;

	/* Get the signal level. */
	level = pjmedia_calc_avg_signal(buf, conf->samples_per_frame);

	/* Convert level to 8bit complement ulaw */
	cport->tx_level = linear2ulaw(level) ^ 0xff;

    } else {
	cport->tx_level = 0;
    }

    /* If port has the same clock_rate and samples_per_frame settings as
     * the conference bridge, transmit the frame as is.
     */
    if (cport->clock_rate == conf->clock_rate &&
	cport->samples_per_frame == conf->samples_per_frame)
    {
	if (cport->port != NULL) {
	    pjmedia_frame frame;

	    frame.type = PJMEDIA_FRAME_TYPE_AUDIO;
	    frame.buf = (pj_int16_t*)cport->mix_buf;
	    frame.size = conf->samples_per_frame * BYTES_PER_SAMPLE;
	    frame.timestamp.u64 = timestamp;

	    TRACE_((THIS_FILE, "put_frame %.*s, count=%d", 
			       (int)cport->name.slen, cport->name.ptr,
			       frame.size / BYTES_PER_SAMPLE));

	    return pjmedia_port_put_frame(cport->port, &frame);
	} else
	    return PJ_SUCCESS;
    }

    /* If it has different clock_rate, must resample. */
    if (cport->clock_rate != conf->clock_rate) {

	unsigned dst_count;

	pjmedia_resample_run( cport->tx_resample, buf, 
			      cport->tx_buf + cport->tx_buf_count );

	dst_count = (unsigned)(conf->samples_per_frame * 1.0 *
			       cport->clock_rate / conf->clock_rate);
	cport->tx_buf_count += dst_count;

    } else {
	/* Same clock rate.
	 * Just copy the samples to tx_buffer.
	 */
	pjmedia_copy_samples( cport->tx_buf + cport->tx_buf_count,
			      buf, conf->samples_per_frame );
	cport->tx_buf_count += conf->samples_per_frame;
    }

    /* Transmit while we have enough frame in the tx_buf. */
    status = PJ_SUCCESS;
    while (cport->tx_buf_count >= cport->samples_per_frame &&
	   status == PJ_SUCCESS) 
    {
	
	TRACE_((THIS_FILE, "write_port %.*s: count=%d", 
			   (int)cport->name.slen, cport->name.ptr,
			   cport->samples_per_frame));

	if (cport->port) {
	    pjmedia_frame frame;

	    frame.type = PJMEDIA_FRAME_TYPE_AUDIO;
	    frame.buf = cport->tx_buf;
	    frame.size = cport->samples_per_frame * BYTES_PER_SAMPLE;
	    frame.timestamp.u64 = timestamp;

	    TRACE_((THIS_FILE, "put_frame %.*s, count=%d", 
			       (int)cport->name.slen, cport->name.ptr,
			       frame.size / BYTES_PER_SAMPLE));

	    status = pjmedia_port_put_frame(cport->port, &frame);

	} else
	    status = PJ_SUCCESS;

	cport->tx_buf_count -= cport->samples_per_frame;
	if (cport->tx_buf_count) {
	    pjmedia_copy_samples(cport->tx_buf, 
				 cport->tx_buf + cport->samples_per_frame,
				 cport->tx_buf_count);
	}

	TRACE_((THIS_FILE, " tx_buf count now is %d", 
			   cport->tx_buf_count));
    }

    return status;
}


/*
 * Player callback.
 */
static pj_status_t get_frame(pjmedia_port *this_port, 
			     pjmedia_frame *frame)
{
    pjmedia_conf *conf = this_port->user_data;
    unsigned ci, cj, i, j;
    
    TRACE_((THIS_FILE, "- clock -"));

    /* Check that correct size is specified. */
    pj_assert(frame->size == conf->samples_per_frame *
			     conf->bits_per_sample / 8);

    /* Must lock mutex (must we??) */
    pj_mutex_lock(conf->mutex);

    /* Zero all port's temporary buffers. */
    for (i=0, ci=0; i<conf->max_ports && ci < conf->port_cnt; ++i) {
	struct conf_port *conf_port = conf->ports[i];
	pj_uint32_t *mix_buf;

	/* Skip empty slot. */
	if (!conf_port)
	    continue;

	++ci;

	conf_port->sources = 0;
	mix_buf = conf_port->mix_buf;

	pj_memset(mix_buf, 0, conf->samples_per_frame*sizeof(mix_buf[0]));
    }

    /* Get frames from all ports, and "mix" the signal 
     * to mix_buf of all listeners of the port.
     */
    for (i=0, ci=0; i<conf->max_ports && ci<conf->port_cnt; ++i) {
	struct conf_port *conf_port = conf->ports[i];
	pj_int32_t level;

	/* Skip empty port. */
	if (!conf_port)
	    continue;

	/* Var "ci" is to count how many ports have been visited so far. */
	++ci;

	/* Skip if we're not allowed to receive from this port. */
	if (conf_port->rx_setting == PJMEDIA_PORT_DISABLE) {
	    conf_port->rx_level = 0;
	    continue;
	}

	/* Also skip if this port doesn't have listeners. */
	if (conf_port->listener_cnt == 0) {
	    conf_port->rx_level = 0;
	    continue;
	}

	/* Get frame from this port. 
	 * For port zero (sound port), get the frame  from the rx_buffer
	 * instead.
	 */
	if (i==0) {
	    pj_int16_t *snd_buf;

	    if (conf_port->snd_read_pos == conf_port->snd_write_pos) {
		conf_port->snd_read_pos = 
		    (conf_port->snd_write_pos+RX_BUF_COUNT-RX_BUF_COUNT/2) % 
			RX_BUF_COUNT;
	    }

	    /* Skip if this port is muted/disabled. */
	    if (conf_port->rx_setting != PJMEDIA_PORT_ENABLE) {
		conf_port->rx_level = 0;
		continue;
	    }

	    snd_buf = conf_port->snd_buf[conf_port->snd_read_pos];
	    pjmedia_copy_samples(frame->buf, snd_buf, conf->samples_per_frame);
	    conf_port->snd_read_pos = (conf_port->snd_read_pos+1) % RX_BUF_COUNT;

	} else {

	    pj_status_t status;
	    pjmedia_frame_type frame_type;

	    status = read_port(conf, conf_port, frame->buf, 
			       conf->samples_per_frame, &frame_type);
	    
	    if (status != PJ_SUCCESS) {
		/* bennylp: why do we need this????
		 * Also see comments on similar issue with write_port().
		PJ_LOG(4,(THIS_FILE, "Port %.*s get_frame() returned %d. "
				     "Port is now disabled",
				     (int)conf_port->name.slen,
				     conf_port->name.ptr,
				     status));
		conf_port->rx_setting = PJMEDIA_PORT_DISABLE;
		 */
		continue;
	    }
	}

	/* If we need to adjust the RX level from this port, adjust the level
	 * and calculate the average level at the same time.
	 * Otherwise just calculate the averate level.
	 */
	if (conf_port->rx_adj_level != NORMAL_LEVEL) {
	    pj_int16_t *input = frame->buf;
	    pj_int32_t adj = conf_port->rx_adj_level;

	    level = 0;
	    for (j=0; j<conf->samples_per_frame; ++j) {
		pj_int32_t itemp;

		/* For the level adjustment, we need to store the sample to
		 * a temporary 32bit integer value to avoid overflowing the
		 * 16bit sample storage.
		 */
		itemp = input[j];
		itemp = itemp * adj / NORMAL_LEVEL;

		/* Clip the signal if it's too loud */
		if (itemp > 32767) itemp = 32767;
		else if (itemp < -32768) itemp = -32768;

		input[j] = (pj_int16_t) itemp;
		level += itemp;
	    }

	    level /= conf->samples_per_frame;

	} else {
	    level = pjmedia_calc_avg_signal(frame->buf, 
					    conf->samples_per_frame);
	}

	/* Convert level to 8bit complement ulaw */
	level = linear2ulaw(level) ^ 0xff;

	/* Put this level to port's last RX level. */
	conf_port->rx_level = level;

	/* Convert the buffer to unsigned 16bit value */
	for (j=0; j<conf->samples_per_frame; ++j)
	    conf->uns_buf[j] = pcm2unsigned(((pj_int16_t*)frame->buf)[j]);

	/* Add the signal to all listeners. */
	for (j=0, cj=0; 
	     j<conf->max_ports && cj<(unsigned)conf_port->listener_cnt;
	     ++j) 
	{
	    struct conf_port *listener = conf->ports[j];
	    pj_uint32_t *mix_buf;
	    unsigned k;

	    if (listener == 0)
		continue;

	    /* Skip if this is not the listener. */
	    if (!conf_port->listeners[j])
		continue;

	    /* Var "cj" is the number of listeners we have visited so far */
	    ++cj;

	    /* Skip if this listener doesn't want to receive audio */
	    if (listener->tx_setting != PJMEDIA_PORT_ENABLE)
		continue;

	    /* Mix the buffer */
	    mix_buf = listener->mix_buf;
	    for (k=0; k<conf->samples_per_frame; ++k)
		mix_buf[k] += (conf->uns_buf[k] * level);

	    listener->sources += level;
	}
    }

    /* Time for all ports to transmit whetever they have in their
     * buffer. 
     */
    for (i=0, ci=0; i<conf->max_ports && ci<conf->port_cnt; ++i) {
	struct conf_port *conf_port = conf->ports[i];
	pj_status_t status;

	if (!conf_port)
	    continue;

	/* Var "ci" is to count how many ports have been visited. */
	++ci;

	status = write_port( conf, conf_port, frame->timestamp.u32.lo);
	if (status != PJ_SUCCESS) {
	    /* bennylp: why do we need this????
	       One thing for sure, put_frame()/write_port() may return
	       non-successfull status on Win32 if there's temporary glitch
	       on network interface, so disabling the port here does not
	       sound like a good idea.

	    PJ_LOG(4,(THIS_FILE, "Port %.*s put_frame() returned %d. "
				 "Port is now disabled",
				 (int)conf_port->name.slen,
				 conf_port->name.ptr,
				 status));
	    conf_port->tx_setting = PJMEDIA_PORT_DISABLE;
	    */
	    continue;
	}
    }

    /* Return sound playback frame. */
    if (conf->ports[0]->sources) {
	TRACE_((THIS_FILE, "write to audio, count=%d", 
			   conf->samples_per_frame));

	pjmedia_copy_samples( frame->buf, (pj_int16_t*)conf->ports[0]->mix_buf, 
			      conf->samples_per_frame);
    } else {
	pjmedia_zero_samples( frame->buf, conf->samples_per_frame ); 
    }

    /* MUST set frame type */
    frame->type = PJMEDIA_FRAME_TYPE_AUDIO;

    pj_mutex_unlock(conf->mutex);

#ifdef REC_FILE
    if (fhnd_rec == NULL)
	fhnd_rec = fopen(REC_FILE, "wb");
    if (fhnd_rec)
	fwrite(frame->buf, frame->size, 1, fhnd_rec);
#endif

    return PJ_SUCCESS;
}


/*
 * Recorder callback.
 */
static pj_status_t put_frame(pjmedia_port *this_port, 
			     const pjmedia_frame *frame)
{
    pjmedia_conf *conf = this_port->user_data;
    struct conf_port *snd_port = conf->ports[0];
    const pj_int16_t *input = frame->buf;
    pj_int16_t *target_snd_buf;

    /* Check for correct size. */
    PJ_ASSERT_RETURN( frame->size == conf->samples_per_frame *
				     conf->bits_per_sample / 8,
		      PJMEDIA_ENCSAMPLESPFRAME);

    /* Skip if this port is muted/disabled. */
    if (snd_port->rx_setting != PJMEDIA_PORT_ENABLE) {
	return PJ_SUCCESS;
    }

    /* Skip if no port is listening to the microphone */
    if (snd_port->listener_cnt == 0) {
	return PJ_SUCCESS;
    }


    /* Determine which rx_buffer to fill in */
    target_snd_buf = snd_port->snd_buf[snd_port->snd_write_pos];
    
    /* Copy samples from audio device to target rx_buffer */
    pjmedia_copy_samples(target_snd_buf, input, conf->samples_per_frame);

    /* Switch buffer */
    snd_port->snd_write_pos = (snd_port->snd_write_pos+1)%RX_BUF_COUNT;


    return PJ_SUCCESS;
}

