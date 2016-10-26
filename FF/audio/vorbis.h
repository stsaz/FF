/** Vorbis.
Copyright (c) 2016 Simon Zolin
*/

/*
OGG(VORB_INFO)  OGG(VORB_COMMENTS VORB_CODEBOOK)  OGG(PKT1 PKT2...)...
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/audio/vorbistag.h>
#include <FF/array.h>

#include <vorbis/vorbis-ff.h>


enum {
	FFVORBIS_EFMT = 1,
	FFVORBIS_EPKT,
	FFVORBIS_ETAG,

	FFVORBIS_ESYS,
};

FF_EXTN const char* _ffvorbis_errstr(int e);


enum FFVORBIS_R {
	FFVORBIS_RWARN = -2,
	FFVORBIS_RERR = -1,
	FFVORBIS_RHDR, //audio info is parsed
	FFVORBIS_RTAG, //tag pair is returned
	FFVORBIS_RHDRFIN, //header is finished
	FFVORBIS_RDATA, //PCM data is returned
	FFVORBIS_RMORE,
	FFVORBIS_RDONE,
};

typedef struct ffvorbis {
	uint state;
	int err;
	vorbis_ctx *vctx;
	struct {
		uint channels;
		uint rate;
		uint bitrate_nominal;
	} info;
	uint pktno;
	uint pkt_samples;
	uint64 total_samples;
	uint64 cursample;
	uint64 seek_sample;

	ffvorbtag vtag;

	size_t pcmlen;
	const float **pcm; //non-interleaved
	const float* pcm_arr[8];
	uint fin :1;
} ffvorbis;

#define ffvorbis_errstr(v)  _ffvorbis_errstr((v)->err)

/** Initialize ffvorbis. */
FF_EXTN int ffvorbis_open(ffvorbis *v);

FF_EXTN void ffvorbis_close(ffvorbis *v);

/** Get bps. */
#define ffvorbis_bitrate(v)  ((v)->info.bitrate_nominal)

#define ffvorbis_rate(v)  ((v)->info.rate)
#define ffvorbis_channels(v)  ((v)->info.channels)

FF_EXTN void ffvorbis_seek(ffvorbis *v, uint64 sample);

/** Decode Vorbis packet.
Return enum FFVORBIS_R. */
FF_EXTN int ffvorbis_decode(ffvorbis *v, const char *pkt, size_t len);

/** Get an absolute sample number. */
#define ffvorbis_cursample(v)  ((v)->cursample - (v)->pkt_samples)


typedef struct ffvorbis_enc {
	uint state;
	vorbis_ctx *vctx;
	uint channels;
	int err;
	ffstr pkt_hdr;
	ffstr pkt_book;

	ffvorbtag_cook vtag;
	uint min_tagsize;

	ffstr data;

	size_t pcmlen;
	const float **pcm; //non-interleaved
	uint64 granulepos;

	uint fin :1;
} ffvorbis_enc;

#define ffvorbis_enc_errstr(v)  _ffvorbis_errstr((v)->err)

FF_EXTN int ffvorbis_create(ffvorbis_enc *v, const ffpcm *fmt, int quality);

FF_EXTN void ffvorbis_enc_close(ffvorbis_enc *v);

/** Add vorbis tag. */
#define ffvorbis_addtag(v, name, val, val_len) \
	ffvorbtag_add(&(v)->vtag, name, val, val_len)

/** Get bitrate from quality. */
FF_EXTN uint ffvorbis_enc_bitrate(ffvorbis_enc *v, int qual);

/**
Return enum FFVORBIS_R. */
FF_EXTN int ffvorbis_encode(ffvorbis_enc *v);

#define ffvorbis_enc_pos(v)  ((v)->granulepos)
