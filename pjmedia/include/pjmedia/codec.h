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
#ifndef __PJMEDIA_CODEC_H__
#define __PJMEDIA_CODEC_H__


/**
 * @file codec.h
 * @brief Codec framework.
 */

#include <pjmedia/port.h>
#include <pj/list.h>

PJ_BEGIN_DECL


/**
 * @defgroup PJMED_CODEC Codec framework.
 * @ingroup PJMEDIA
 * @{
 *
 * The codec manager is used to manage all codec capabilities in the endpoint.
 * Library implementors can extend PJMEDIA codec capabilities by creating 
 * a codec factory for a new codec, and register the codec factory to
 * codec manager so that the codec can be used by the rest of application.
 *
 * When used with media endpoint (pjmedia_endpt), application can retrieve
 * the codec manager instance by calling #pjmedia_endpt_get_codec_mgr().
 */


/** 
 * Standard RTP static payload types, as defined by RFC 3551. 
 * The header file <pjmedia-codec/types.h> also declares dynamic payload
 * types that are supported by pjmedia-codec library.
 */
enum pjmedia_rtp_pt
{
    PJMEDIA_RTP_PT_PCMU = 0,	    /**< audio PCMU			    */
    PJMEDIA_RTP_PT_GSM  = 3,	    /**< audio GSM			    */
    PJMEDIA_RTP_PT_G723 = 4,	    /**< audio G723			    */
    PJMEDIA_RTP_PT_DVI4_8K = 5,	    /**< audio DVI4 8KHz		    */
    PJMEDIA_RTP_PT_DVI4_16K = 6,    /**< audio DVI4 16Khz		    */
    PJMEDIA_RTP_PT_LPC = 7,	    /**< audio LPC			    */
    PJMEDIA_RTP_PT_PCMA = 8,	    /**< audio PCMA			    */
    PJMEDIA_RTP_PT_G722 = 9,	    /**< audio G722			    */
    PJMEDIA_RTP_PT_L16_2 = 10,	    /**< audio 16bit linear 44.1KHz stereo  */
    PJMEDIA_RTP_PT_L16_1 = 11,	    /**< audio 16bit linear 44.1KHz mono    */
    PJMEDIA_RTP_PT_QCELP = 12,	    /**< audio QCELP			    */
    PJMEDIA_RTP_PT_CN = 13,	    /**< audio Comfort Noise		    */
    PJMEDIA_RTP_PT_MPA = 14,	    /**< audio MPEG1/MPEG2 elemetr. streams */
    PJMEDIA_RTP_PT_G728 = 15,	    /**< audio G728			    */
    PJMEDIA_RTP_PT_DVI4_11K = 16,   /**< audio DVI4 11.025KHz mono	    */
    PJMEDIA_RTP_PT_DVI4_22K = 17,   /**< audio DVI4 22.050KHz mono	    */
    PJMEDIA_RTP_PT_G729 = 18,	    /**< audio G729			    */

    PJMEDIA_RTP_PT_CELB = 25,	    /**< video/comb Cell-B by Sun (RFC2029) */
    PJMEDIA_RTP_PT_JPEG = 26,	    /**< video JPEG			    */
    PJMEDIA_RTP_PT_NV = 28,	    /**< video NV  by nv program by Xerox   */
    PJMEDIA_RTP_PT_H261 = 31,	    /**< video H261			    */
    PJMEDIA_RTP_PT_MPV = 32,	    /**< video MPEG1 or MPEG2 elementary    */
    PJMEDIA_RTP_PT_MP2T = 33,	    /**< video MPEG2 transport		    */
    PJMEDIA_RTP_PT_H263 = 34,	    /**< video H263			    */

    PJMEDIA_RTP_PT_DYNAMIC = 96,    /**< start of dynamic RTP payload	    */

};


/** 
 * Identification used to search for codec factory that supports specific 
 * codec specification. 
 */
struct pjmedia_codec_info
{
    pjmedia_type    type;	    /**< Media type.			*/
    unsigned	    pt;		    /**< Payload type (can be dynamic). */
    pj_str_t	    encoding_name;  /**< Encoding name.			*/
    unsigned	    clock_rate;	    /**< Sampling rate.			*/
    unsigned	    channel_cnt;    /**< Channel count.			*/
};


/**
 * @see pjmedia_codec_info
 */
typedef struct pjmedia_codec_info pjmedia_codec_info;


/** 
 * Detailed codec attributes used both to configure a codec and to query
 * the capability of codec factories.
 */
struct pjmedia_codec_param
{
    /**
     * The "info" part of codec param describes the capability of the codec,
     * and the value should NOT be changed by application.
     */
    struct {
       unsigned	   clock_rate;		/**< Sampling rate in Hz	    */
       unsigned	   channel_cnt;		/**< Channel count.		    */
       pj_uint32_t avg_bps;		/**< Average bandwidth in bits/sec  */
       pj_uint16_t frm_ptime;		/**< Base frame ptime in msec.	    */
       pj_uint8_t  pcm_bits_per_sample;	/**< Bits/sample in the PCM side    */
       pj_uint8_t  pt;			/**< Payload type.		    */
    } info;

    /**
     * The "setting" part of codec param describes various settings to be
     * applied to the codec. When the codec param is retrieved from the codec
     * or codec factory, the values of these will be filled by the capability
     * of the codec. Any features that are supported by the codec (e.g. vad
     * or plc) will be turned on, so that application can query which 
     * capabilities are supported by the codec. Application may change the
     * settings here before instantiating the codec/stream.
     */
    struct {
	pj_uint8_t  frm_per_pkt;    /**< Number of frames per packet.	*/
	unsigned    vad:1;	    /**< Voice Activity Detector.	*/
	unsigned    cng:1;	    /**< Comfort Noise Generator.	*/
	unsigned    lpf:1;	    /**< Low pass filter		*/
	unsigned    hpf:1;	    /**< High pass filter		*/
	unsigned    penh:1;	    /**< Perceptual Enhancement		*/
	unsigned    plc:1;	    /**< Packet loss concealment	*/
	unsigned    reserved:1;	    /**< Reserved, must be zero.	*/
    } setting;
};

/**
 * @see pjmedia_codec_param
 */
typedef struct pjmedia_codec_param pjmedia_codec_param;


/**
 * @see pjmedia_codec
 */
typedef struct pjmedia_codec pjmedia_codec;


/**
 * This structure describes codec operations. Each codec MUST implement
 * all of these functions.
 */
struct pjmedia_codec_op
{
    /** 
     * Initialize codec using the specified attribute.
     *
     * @param codec	The codec instance.
     * @param pool	Pool to use when the codec needs to allocate
     *			some memory.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t	(*init)(pjmedia_codec *codec, 
			pj_pool_t *pool );

    /** 
     * Open the codec and initialize with the specified parameter..
     *
     * @param codec	The codec instance.
     * @param param	Codec initialization parameter.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t	(*open)(pjmedia_codec *codec, 
			pjmedia_codec_param *param );

    /** 
     * Close and shutdown codec, releasing all resources allocated by
     * this codec, if any.
     *
     * @param codec	The codec instance.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t (*close)(pjmedia_codec *codec);


    /**
     * Instruct the codec to inspect the specified payload/packet and
     * split the packet into individual base frames. Each output frames will
     * have ptime that is equal to basic frame ptime (i.e. the value of
     * info.frm_ptime in #pjmedia_codec_param).
     *
     * @param codec	The codec instance
     * @param pkt	The input packet.
     * @param pkt_size	Size of the packet.
     * @param timestamp	The timestamp of the first sample in the packet.
     * @param frame_cnt	On input, specifies the maximum number of frames
     *			in the array. On output, the codec must fill
     *			with number of frames detected in the packet.
     * @param frames	On output, specifies the frames that have been
     *			detected in the packet.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t (*parse)( pjmedia_codec *codec,
			  void *pkt,
			  pj_size_t pkt_size,
			  const pj_timestamp *timestamp,
			  unsigned *frame_cnt,
			  pjmedia_frame frames[]);

    /** 
     * Instruct the codec to encode the specified input frame. The input
     * PCM samples MUST have ptime that is exactly equal to base frame
     * ptime (i.e. the value of info.frm_ptime in #pjmedia_codec_param).
     *
     * @param codec	The codec instance.
     * @param input	The input frame.
     * @param out_size	The length of buffer in the output frame.
     * @param output	The output frame.
     *
     * @return		PJ_SUCCESS on success;
     */
    pj_status_t (*encode)(pjmedia_codec *codec, 
			  const struct pjmedia_frame *input,
			  unsigned out_size, 
			  struct pjmedia_frame *output);

    /** 
     * Instruct the codec to decode the specified input frame. The input
     * frame MUST have ptime that is exactly equal to base frame
     * ptime (i.e. the value of info.frm_ptime in #pjmedia_codec_param).
     * Application can achieve this by parsing the packet into base
     * frames before decoding each frame.
     *
     * @param codec	The codec instance.
     * @param input	The input frame.
     * @param out_size	The length of buffer in the output frame.
     * @param output	The output frame.
     *
     * @return		PJ_SUCCESS on success;
     */
    pj_status_t (*decode)(pjmedia_codec *codec, 
			  const struct pjmedia_frame *input,
			  unsigned out_size, 
			  struct pjmedia_frame *output);

    /**
     * Instruct the codec to recover a missing frame. Not all codec has
     * this capability, so this function may be NULL.
     *
     * @param codec	The codec instance.
     * @param out_size	The length of buffer in the output frame.
     * @param output	The output frame.
     *
     * @return		PJ_SUCCESS on success;
     */
    pj_status_t (*recover)(pjmedia_codec *codec,
			   unsigned out_size,
			   struct pjmedia_frame *output);
};


/**
 * Codec operation.
 */
typedef struct pjmedia_codec_op pjmedia_codec_op;


/**
 * @see pjmedia_codec_factory
 */
typedef struct pjmedia_codec_factory pjmedia_codec_factory;


/**
 * This structure describes a codec instance. 
 */
struct pjmedia_codec
{
    /** Entries to put this codec instance in codec factory's list. */
    PJ_DECL_LIST_MEMBER(struct pjmedia_codec);

    /** Codec's private data. */
    void		    *codec_data;

    /** Codec factory where this codec was allocated. */
    pjmedia_codec_factory   *factory;

    /** Operations to codec. */
    pjmedia_codec_op	    *op;
};



/**
 * This structure describes operations that must be supported by codec 
 * factories.
 */
struct pjmedia_codec_factory_op
{
    /** 
     * Check whether the factory can create codec with the specified 
     * codec info.
     *
     * @param factory	The codec factory.
     * @param info	The codec info.
     *
     * @return		PJ_SUCCESS if this factory is able to create an
     *			instance of codec with the specified info.
     */
    pj_status_t	(*test_alloc)(pjmedia_codec_factory *factory, 
			      const pjmedia_codec_info *info );

    /** 
     * Create default attributes for the specified codec ID. This function
     * can be called by application to get the capability of the codec.
     *
     * @param factory	The codec factory.
     * @param info	The codec info.
     * @param attr	The attribute to be initialized.
     *
     * @return		PJ_SUCCESS if success.
     */
    pj_status_t (*default_attr)(pjmedia_codec_factory *factory, 
    				const pjmedia_codec_info *info,
    				pjmedia_codec_param *attr );

    /** 
     * Enumerate supported codecs that can be created using this factory.
     * 
     *  @param factory	The codec factory.
     *  @param count	On input, specifies the number of elements in
     *			the array. On output, the value will be set to
     *			the number of elements that have been initialized
     *			by this function.
     *  @param info	The codec info array, which contents will be 
     *			initialized upon return.
     *
     *  @return		PJ_SUCCESS on success.
     */
    pj_status_t (*enum_info)(pjmedia_codec_factory *factory, 
			     unsigned *count, 
			     pjmedia_codec_info codecs[]);

    /** 
     * Create one instance of the codec with the specified codec info.
     *
     * @param factory	The codec factory.
     * @param info	The codec info.
     * @param p_codec	Pointer to receive the codec instance.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t (*alloc_codec)(pjmedia_codec_factory *factory, 
			       const pjmedia_codec_info *info,
			       pjmedia_codec **p_codec);

    /** 
     * This function is called by codec manager to return a particular 
     * instance of codec back to the codec factory.
     *
     * @param factory	The codec factory.
     * @param codec	The codec instance to be returned.
     *
     * @return		PJ_SUCCESS on success.
     */
    pj_status_t (*dealloc_codec)(pjmedia_codec_factory *factory, 
				 pjmedia_codec *codec );

};


/**
 * @see pjmedia_codec_factory_op
 */
typedef struct pjmedia_codec_factory_op pjmedia_codec_factory_op;


/**
 * Codec factory describes a module that is able to create codec with specific
 * capabilities. These capabilities can be queried by codec manager to create
 * instances of codec.
 */
struct pjmedia_codec_factory
{
    /** Entries to put this structure in the codec manager list. */
    PJ_DECL_LIST_MEMBER(struct pjmedia_codec_factory);

    /** The factory's private data. */
    void		     *factory_data;

    /** Operations to the factory. */
    pjmedia_codec_factory_op *op;

};


/**
 * Declare maximum codecs
 */
#define PJMEDIA_CODEC_MGR_MAX_CODECS	    32


/**
 * Specify these values to set the codec priority, by calling
 * #pjmedia_codec_mgr_set_codec_priority().
 */
enum pjmedia_codec_priority
{
    /**
     * This priority makes the codec the highest in the order.
     * The last codec specified with this priority will get the
     * highest place in the order, and will change the priority
     * of previously highest priority codec to NEXT_HIGHER.
     */
    PJMEDIA_CODEC_PRIO_HIGHEST = 255,

    /**
     * This priority will put the codec as the next codec after
     * codecs with this same priority.
     */
    PJMEDIA_CODEC_PRIO_NEXT_HIGHER = 254,

    /**
     * This is the initial codec priority when it is registered to
     * codec manager by codec factory.
     */
    PJMEDIA_CODEC_PRIO_NORMAL = 128,

    /**
     * This priority makes the codec the lowest in the order.
     * The last codec specified with this priority will be put
     * in the last place in the order.
     */
    PJMEDIA_CODEC_PRIO_LOWEST = 1,

    /**
     * This priority will prevent the codec from being listed in the
     * SDP created by media endpoint, thus should prevent the codec
     * from being used in the sessions. However, the codec will still
     * be listed by #pjmedia_codec_mgr_enum_codecs() and other codec
     * query functions.
     */
    PJMEDIA_CODEC_PRIO_DISABLED = 0,
};


/**
 * @see pjmedia_codec_priority
 */
typedef enum pjmedia_codec_priority pjmedia_codec_priority;


/** Fully qualified codec name  (e.g. "pcmu/8000/1") */
typedef char pjmedia_codec_id[32];


/** 
 * Codec manager maintains array of these structs for each supported
 * codec.
 */
struct pjmedia_codec_desc
{
    pjmedia_codec_info	    info;	/**< Codec info.	    */
    pjmedia_codec_id	    id;		/**< Fully qualified name   */
    pjmedia_codec_priority  prio;	/**< Priority.		    */
    pjmedia_codec_factory  *factory;	/**< The factory.	    */
};


/**
 * The declaration for codec manager. Application doesn't normally need
 * to see this declaration, but nevertheless this declaration is needed
 * by media endpoint to instantiate the codec manager.
 */
struct pjmedia_codec_mgr
{
    /** List of codec factories registered to codec manager. */
    pjmedia_codec_factory	factory_list;

    /** Number of supported codesc. */
    unsigned			codec_cnt;

    /** Array of codec descriptor. */
    struct pjmedia_codec_desc	codec_desc[PJMEDIA_CODEC_MGR_MAX_CODECS];
};


/**
 * @see pjmedia_codec_mgr
 */
typedef struct pjmedia_codec_mgr pjmedia_codec_mgr;



/**
 * Initialize codec manager. Normally this function is called by pjmedia
 * endpoint's initialization code.
 *
 * @param mgr	    Codec manager instance.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_mgr_init(pjmedia_codec_mgr *mgr);


/** 
 * Register codec factory to codec manager. This will also register
 * all supported codecs in the factory to the codec manager.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param factory   The codec factory to be registered.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_codec_mgr_register_factory( pjmedia_codec_mgr *mgr,
				    pjmedia_codec_factory *factory);

/**
 * Unregister codec factory from the codec manager. This will also
 * remove all the codecs registered by the codec factory from the
 * codec manager's list of supported codecs.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param factory   The codec factory to be unregistered.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_codec_mgr_unregister_factory( pjmedia_codec_mgr *mgr, 
				      pjmedia_codec_factory *factory);

/**
 * Enumerate all supported codecs that have been registered to the
 * codec manager by codec factories.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param count	    On input, specifies the number of elements in
 *		    the array. On output, the value will be set to
 *		    the number of elements that have been initialized
 *		    by this function.
 * @param info	    The codec info array, which contents will be 
 *		    initialized upon return.
 * @param prio	    Optional pointer to receive array of codec priorities.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_mgr_enum_codecs( pjmedia_codec_mgr *mgr, 
						    unsigned *count, 
						    pjmedia_codec_info info[],
						    unsigned *prio);

/**
 * Get codec info for the specified static payload type. Note that
 * this can only find codec with static payload types. This function can
 * be used to find codec info for a payload type inside SDP which doesn't
 * have the corresponding rtpmap attribute.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param pt	    Static payload type/number.
 * @param inf	    Pointer to receive codec info.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_codec_mgr_get_codec_info( pjmedia_codec_mgr *mgr,
				  unsigned pt,
				  const pjmedia_codec_info **inf);

/**
 * Convert codec info struct into a unique codec identifier.
 * A codec identifier looks something like "L16/44100/2".
 *
 * @param info	    The codec info
 * @param id	    Buffer to put the codec info string.
 * @param max_len   The length of the buffer.
 *
 * @return	    The null terminated codec info string, or NULL if
 *		    the buffer is not long enough.
 */
PJ_DECL(char*) pjmedia_codec_info_to_id(const pjmedia_codec_info *info,
				        char *id, unsigned max_len );


/**
 * Find codecs by the unique codec identifier. This function will find
 * all codecs that match the codec identifier prefix. For example, if
 * "L16" is specified, then it will find "L16/8000/1", "L16/16000/1",
 * and so on, up to the maximum count specified in the argument.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param codec_id  The full codec ID or codec ID prefix. If an empty
 *		    string is given, it will match all codecs.
 * @param count	    Maximum number of codecs to find. On return, it
 *		    contains the actual number of codecs found.
 * @param p_info    Array of pointer to codec info to be filled. This
 *		    argument may be NULL, which in this case, only
 *		    codec count will be returned.
 * @param prio	    Optional array of codec priorities.
 *
 * @return	    PJ_SUCCESS if at least one codec info is found.
 */
PJ_DECL(pj_status_t) 
pjmedia_codec_mgr_find_codecs_by_id( pjmedia_codec_mgr *mgr,
				     const pj_str_t *codec_id,
				     unsigned *count,
				     const pjmedia_codec_info *p_info[],
				     unsigned prio[]);


/**
 * Set codec priority. The codec priority determines the order of
 * the codec in the SDP created by the endpoint. If more than one codecs
 * are found with the same codec_id prefix, then the function sets the
 * priorities of all those codecs.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param codec_id  The full codec ID or codec ID prefix. If an empty
 *		    string is given, it will match all codecs.
 * @param prio	    Priority to be set. The priority can have any value
 *		    between 1 to 255. When the priority is set to zero,
 *		    the codec will be disabled.
 *
 * @return	    PJ_SUCCESS if at least one codec info is found.
 */
PJ_DECL(pj_status_t)
pjmedia_codec_mgr_set_codec_priority(pjmedia_codec_mgr *mgr, 
				     const pj_str_t *codec_id,
				     pj_uint8_t prio);


/**
 * Get default codec param for the specified codec info.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param info	    The codec info, which default parameter's is being
 *		    queried.
 * @param param	    On return, will be filled with the default codec
 *		    parameter.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_codec_mgr_get_default_param( pjmedia_codec_mgr *mgr,
				     const pjmedia_codec_info *info,
				     pjmedia_codec_param *param );

/**
 * Request the codec manager to allocate one instance of codec with the
 * specified codec info. The codec will enumerate all codec factories
 * until it finds factory that is able to create the specified codec.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param info	    The information about the codec to be created.
 * @param p_codec   Pointer to receive the codec instance.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) 
pjmedia_codec_mgr_alloc_codec( pjmedia_codec_mgr *mgr, 
			       const pjmedia_codec_info *info,
			       pjmedia_codec **p_codec);

/**
 * Deallocate the specified codec instance. The codec manager will return
 * the instance of the codec back to its factory.
 *
 * @param mgr	    The codec manager instance. Application can get the
 *		    instance by calling #pjmedia_endpt_get_codec_mgr().
 * @param codec	    The codec instance.
 *
 * @return	    PJ_SUCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_mgr_dealloc_codec(pjmedia_codec_mgr *mgr, 
						     pjmedia_codec *codec);





/**
 * @}
 */

PJ_END_DECL


#endif	/* __PJMEDIA_CODEC_H__ */
