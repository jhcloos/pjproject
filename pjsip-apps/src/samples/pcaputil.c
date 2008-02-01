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
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>

static const char *USAGE =
"pcaputil [options] INPUT OUTPUT\n"
"\n"
"  Convert captured RTP packets in PCAP file to WAV or stream it\n"
"  to remote destination.\n"
"\n"
"INPUT is the PCAP file name/path\n"
"\n"
"Options to filter packets from PCAP file:\n"
"  --src-ip=IP            Only include packets from this source address\n"
"  --dst-ip=IP            Only include packets destined to this address\n"
"  --src-port=port        Only include packets from this source port number\n"
"  --dst-port=port        Only include packets destined to this port number\n"
"\n"
"Options for saving to WAV file:\n"
""
"  OUTPUT is WAV file:    Set output to WAV file. The program will decode the\n"
"                         RTP contents to the specified WAV file using codec\n"
"                         that is available in PJMEDIA, and optionally decrypt\n"
"                         the content using the SRTP crypto and keys below.\n"
"  --srtp-crypto=TAG, -c  Set crypto to be used to decrypt SRTP packets. Valid\n"
"                         tags are: \n"
"                           AES_CM_128_HMAC_SHA1_80 \n"
"                           AES_CM_128_HMAC_SHA1_32\n"
"  --srtp-key=KEY, -k     Set the base64 key to decrypt SRTP packets.\n"
"\n"
"  Example:\n"
"    pcaputil file.pcap output.wav\n"
"    pcaputil -c AES_CM_128_HMAC_SHA1_80 \\\n"
"              -k VLDONbsbGl2Puqy+0PV7w/uGfpSPKFevDpxGsxN3 \\\n"
"              file.pcap output.wav\n"
;

static pj_caching_pool cp;
static pj_pool_t *pool;
static pjmedia_endpt *mept;
static pj_pcap_file *pcap;
static pjmedia_port *wav;
static pjmedia_transport *srtp;

static void err_exit(const char *title, pj_status_t status)
{
    if (status != PJ_SUCCESS) {
	char errmsg[PJ_ERR_MSG_SIZE];
	pj_strerror(status, errmsg, sizeof(errmsg));
	printf("Error: %s: %s\n", title, errmsg);
    } else {
	printf("Error: %s\n", title);
    }

    if (srtp) pjmedia_transport_close(srtp);
    if (wav) pjmedia_port_destroy(wav);
    if (pcap) pj_pcap_close(pcap);
    if (mept) pjmedia_endpt_destroy(mept);
    if (pool) pj_pool_release(pool);
    pj_caching_pool_destroy(&cp);
    pj_shutdown();

    exit(1);
}

#define T(op)	    do { \
			status = op; \
			if (status != PJ_SUCCESS) \
    			    err_exit(#op, status); \
		    } while (0)


static void read_rtp(pj_uint8_t *buf, pj_size_t bufsize,
		     pjmedia_rtp_hdr **rtp,
		     pj_uint8_t **payload,
		     unsigned *payload_size)
{
    pj_size_t sz = bufsize;
    pj_status_t status;

    status = pj_pcap_read_udp(pcap, buf, &sz);
    if (status != PJ_SUCCESS)
	err_exit("Error reading PCAP file", status);

    if (sz < sizeof(pjmedia_rtp_hdr) + 10) {
	err_exit("Invalid RTP packet", PJ_SUCCESS);
    }

    /* Decrypt SRTP */
    if (srtp) {
	int len = sz;
	T(pjmedia_transport_srtp_decrypt_pkt(srtp, PJ_TRUE, buf, &len));
	sz = len;
    }

    *rtp = (pjmedia_rtp_hdr*)buf;
    *payload = (pj_uint8_t*) (buf + sizeof(pjmedia_rtp_hdr));
    *payload_size = sz - sizeof(pjmedia_rtp_hdr);
}

static void pcap2wav(const char *wav_filename, const pj_str_t *srtp_crypto,
		     const pj_str_t *srtp_key)
{
    struct pkt
    {
	pj_uint8_t	 buffer[320];
	pjmedia_rtp_hdr	*rtp;
	pj_uint8_t	*payload;
	unsigned	 payload_len;
    } pkt0;
    pjmedia_codec_mgr *cmgr;
    pjmedia_codec_info *ci;
    pjmedia_codec_param param;
    pjmedia_codec *codec;
    unsigned samples_per_frame;
    pj_status_t status;

    /* Initialize all codecs */
#if PJMEDIA_HAS_SPEEX_CODEC
    T( pjmedia_codec_speex_init(mept, 0, 10, 10) );
#endif /* PJMEDIA_HAS_SPEEX_CODEC */

#if PJMEDIA_HAS_ILBC_CODEC
    T( pjmedia_codec_ilbc_init(mept, 30) );
#endif /* PJMEDIA_HAS_ILBC_CODEC */

#if PJMEDIA_HAS_GSM_CODEC
    T( pjmedia_codec_gsm_init(mept) );
#endif /* PJMEDIA_HAS_GSM_CODEC */

#if PJMEDIA_HAS_G711_CODEC
    T( pjmedia_codec_g711_init(mept) );
#endif	/* PJMEDIA_HAS_G711_CODEC */

#if PJMEDIA_HAS_L16_CODEC
    T( pjmedia_codec_l16_init(mept, 0) );
#endif	/* PJMEDIA_HAS_L16_CODEC */

    /* Create SRTP transport is needed */
    if (srtp_crypto->slen) {
	pjmedia_srtp_crypto crypto;

	pj_bzero(&crypto, sizeof(crypto));
	crypto.key = *srtp_key;
	crypto.name = *srtp_crypto;
	T( pjmedia_transport_srtp_create(mept, NULL, NULL, &srtp) );
	T( pjmedia_transport_srtp_start(srtp, &crypto, &crypto) );
    }

    /* Read first packet */
    read_rtp(pkt0.buffer, sizeof(pkt0.buffer), &pkt0.rtp, 
	     &pkt0.payload, &pkt0.payload_len);

    cmgr = pjmedia_endpt_get_codec_mgr(mept);

    /* Get codec info and param for the specified payload type */
    T( pjmedia_codec_mgr_get_codec_info(cmgr, pkt0.rtp->pt, &ci) );
    T( pjmedia_codec_mgr_get_default_param(cmgr, ci, &param) );

    /* Alloc and init codec */
    T( pjmedia_codec_mgr_alloc_codec(cmgr, ci, &codec) );
    T( codec->op->init(codec, pool) );
    T( codec->op->open(codec, &param) );

    /* Open WAV file */
    samples_per_frame = ci->clock_rate * param.info.frm_ptime / 1000;
    T( pjmedia_wav_writer_port_create(pool, wav_filename,
				      ci->clock_rate, ci->channel_cnt,
				      samples_per_frame,
				      param.info.pcm_bits_per_sample, 0, 0,
				      &wav) );

    /* Loop reading PCAP and writing WAV file */
    for (;;) {
	struct pkt pkt1;
	pj_timestamp ts;
	pjmedia_frame frames[16], pcm_frame;
	short pcm[320];
	unsigned i, frame_cnt;
	long samples_cnt, ts_gap;

	pj_assert(sizeof(pcm) >= samples_per_frame);

	/* Parse first packet */
	ts.u64 = 0;
	frame_cnt = PJ_ARRAY_SIZE(frames);
	T( codec->op->parse(codec, pkt0.payload, pkt0.payload_len, &ts, 
			    &frame_cnt, frames) );

	/* Decode and write to WAV file */
	samples_cnt = 0;
	for (i=0; i<frame_cnt; ++i) {
	    pjmedia_frame pcm_frame;

	    pcm_frame.buf = pcm;
	    pcm_frame.size = samples_per_frame * 2;

	    T( codec->op->decode(codec, &frames[i], pcm_frame.size, &pcm_frame) );
	    T( pjmedia_port_put_frame(wav, &pcm_frame) );
	    samples_cnt += samples_per_frame;
	}

	/* Read next packet */
	read_rtp(pkt1.buffer, sizeof(pkt1.buffer), &pkt1.rtp,
		 &pkt1.payload, &pkt1.payload_len);

	/* Fill in the gap (if any) between pkt0 and pkt1 */
	ts_gap = pj_ntohl(pkt1.rtp->ts) - pj_ntohl(pkt0.rtp->ts) -
		 samples_cnt;
	while (ts_gap >= (long)samples_per_frame) {

	    pcm_frame.buf = pcm;
	    pcm_frame.size = samples_per_frame * 2;

	    if (codec->op->recover) {
		T( codec->op->recover(codec, pcm_frame.size, &pcm_frame) );
	    } else {
		pj_bzero(pcm_frame.buf, pcm_frame.size);
	    }

	    T( pjmedia_port_put_frame(wav, &pcm_frame) );
	    ts_gap -= samples_per_frame;
	}
	
	/* Next */
	pkt0 = pkt1;
	pkt0.rtp = (pjmedia_rtp_hdr*)pkt0.buffer;
	pkt0.payload = pkt0.buffer + (pkt1.payload - pkt1.buffer);
    }
}


int main(int argc, char *argv[])
{
    pj_str_t input, output, wav, srtp_crypto, srtp_key;
    pj_pcap_filter filter;
    pj_status_t status;

    enum { OPT_SRC_IP = 1, OPT_DST_IP, OPT_SRC_PORT, OPT_DST_PORT };
    struct pj_getopt_option long_options[] = {
	{ "srtp-crypto",    1, 0, 'c' },
	{ "srtp-key",	    1, 0, 'k' },
	{ "src-ip",	    1, 0, OPT_SRC_IP },
	{ "dst-ip",	    1, 0, OPT_DST_IP },
	{ "src-port",	    1, 0, OPT_SRC_PORT },
	{ "dst-port",	    1, 0, OPT_DST_PORT },
	{ NULL, 0, 0, 0}
    };
    int c;
    int option_index;
    char key_bin[32];

    srtp_crypto.slen = srtp_key.slen = 0;

    pj_pcap_filter_default(&filter);
    filter.link = PJ_PCAP_LINK_TYPE_ETH;
    filter.proto = PJ_PCAP_PROTO_TYPE_UDP;

    /* Parse arguments */
    pj_optind = 0;
    while((c=pj_getopt_long(argc,argv, "c:k:", long_options, &option_index))!=-1) {
	switch (c) {
	case 'c':
	    srtp_crypto = pj_str(pj_optarg);
	    break;
	case 'k':
	    {
		int key_len = sizeof(key_bin);
		srtp_key = pj_str(pj_optarg);
		if (pj_base64_decode(&srtp_key, (pj_uint8_t*)key_bin, &key_len)) {
		    puts("Error: invalid key");
		    return 1;
		}
		srtp_key.ptr = key_bin;
		srtp_key.slen = key_len;
	    }
	    break;
	case OPT_SRC_IP:
	    {
		pj_str_t t = pj_str(pj_optarg);
		pj_in_addr a = pj_inet_addr(&t);
		filter.ip_src = a.s_addr;
	    }
	    break;
	case OPT_DST_IP:
	    {
		pj_str_t t = pj_str(pj_optarg);
		pj_in_addr a = pj_inet_addr(&t);
		filter.ip_dst = a.s_addr;
	    }
	    break;
	case OPT_SRC_PORT:
	    filter.src_port = pj_htons((pj_uint16_t)atoi(pj_optarg));
	    break;
	case OPT_DST_PORT:
	    filter.dst_port = pj_htons((pj_uint16_t)atoi(pj_optarg));
	    break;
	default:
	    puts("Error: invalid option");
	    return 1;
	}
    }

    if (pj_optind != argc - 2) {
	puts(USAGE);
	return 1;
    }

    if (!(srtp_crypto.slen) != !(srtp_key.slen)) {
	puts("Error: both SRTP crypto and key must be specified");
	puts(USAGE);
	return 1;
    }

    input = pj_str(argv[pj_optind]);
    output = pj_str(argv[pj_optind+1]);
    wav = pj_str(".wav");
    
    T( pj_init() );

    pj_caching_pool_init(&cp, NULL, 0);
    pool = pj_pool_create(&cp.factory, "pcaputil", 1000, 1000, NULL);

    T( pjlib_util_init() );
    T( pjmedia_endpt_create(&cp.factory, NULL, 0, &mept) );

    T( pj_pcap_open(pool, input.ptr, &pcap) );
    T( pj_pcap_set_filter(pcap, &filter) );

    if (pj_stristr(&output, &wav)) {
	pcap2wav(output.ptr, &srtp_crypto, &srtp_key);
    } else {
	err_exit("invalid output file", PJ_EINVAL);
    }

    pjmedia_endpt_destroy(mept);
    pj_pool_release(pool);
    pj_caching_pool_destroy(&cp);
    pj_shutdown();
    return 0;
}

