/** OGG Vorbis.
Copyright (c) 2015 Simon Zolin
*/

/*
OGG(VORB_INFO)  OGG(VORB_COMMENTS VORB_CODEBOOK)  OGG(PKT1 PKT2...)...
*/

#pragma once

#include <FF/audio/ogg-fmt.h>
#include <FF/audio/pcm.h>
#include <FF/audio/vorbistag.h>
#include <FF/array.h>

#include <vorbis/vorbis-ff.h>


typedef struct ffogg {
	uint state;
	uint nxstate;

	vorbis_ctx *vctx;
	struct {
		uint channels;
		uint rate;
		uint bitrate_nominal;
	} vinfo;

	ffvorbtag vtag;

	uint err;
	uint serial;
	uint nsamples;
	uint64 off
		, off_data //header size
		, total_size;
	uint64 total_samples
		, cursample
		, first_sample
		, seek_sample;

	ffarr buf;
	ffarr pktdata; //holds partial packet data
	uint hdrsize;
	uint pagesize;
	uint bytes_skipped;
	uint page_num;
	uint64 page_gpos;
	ogg_packet opkt;
	uint pktno;
	uint segoff;
	uint bodyoff;
	size_t datalen;
	const char *data;

	size_t pcmlen;
	const float **pcm; //non-interleaved
	const float* pcm_arr[8];

	ffpcm_seekpt seektab[2];
	ffpcm_seekpt seekpt[2];
	uint64 skoff;

	uint seekable :1 //search for eos page
		, init_done :1
		, page_continued :1
		, page_last :1
		, firstseek :1
		, pagenum_err :1
		, continued :1 //expecting continued packet within next page
		;
} ffogg;

FF_EXTN const char* ffogg_errstr(int e);

/** Initialize ffogg. */
FF_EXTN void ffogg_init(ffogg *o);

/** Get bps. */
FF_EXTN uint ffogg_bitrate(ffogg *o);

#define ffogg_rate(o)  ((o)->vinfo.rate)
#define ffogg_channels(o)  ((o)->vinfo.channels)

enum FFOGG_R {
	FFOGG_RWARN = -2
	, FFOGG_RERR = -1
	, FFOGG_RMORE = 0
	, FFOGG_RDATA
	, FFOGG_RSEEK
	, FFOGG_RDONE
	, FFOGG_RPAGE //returned after the whole OGG page is available

	, FFOGG_RHDR
	, FFOGG_RTAG
	, FFOGG_RHDRFIN //header is finished
	, FFOGG_RINFO //total_samples is set, ready for seeking
};

#define ffogg_granulepos(o)  ((o)->page_gpos)

#define ffogg_pageno(o)  ((o)->page_num)

FF_EXTN void ffogg_close(ffogg *o);


/** Add vorbis tag. */
#define ffogg_addtag(o, name, val, val_len) \
	ffvorbtag_add(&(o)->vtag, name, val, val_len)

#define ffogg_iaddtag(o, tag, val, val_len) \
	ffogg_addtag(o, ffvorbtag_str[tag], val, val_len)


FF_EXTN void ffogg_seek(ffogg *o, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffogg_seekoff(o)  ((o)->off)

/** Decode OGG stream.
Return enum FFOGG_R. */
FF_EXTN int ffogg_decode(ffogg *o);

/** Get an absolute sample number. */
#define ffogg_cursample(o)  ((o)->cursample)

FF_EXTN void ffogg_set_asis(ffogg *o, uint64 from_sample);

/**
Return the whole OGG pages as-is (FFOGG_RPAGE). */
FF_EXTN int ffogg_readasis(ffogg *o);

/** Get page data.
Must be called when FFOGG_RPAGE is returned. */
static FFINL void ffogg_pagedata(ffogg *o, const char **d, size_t *size)
{
	*d = o->buf.ptr;
	*size = o->pagesize;
}


typedef struct ffogg_enc {
	uint state;

	vorbis_ctx *vctx;
	struct {
		uint channels;
		uint rate;
		int quality;
	} vinfo;

	ffvorbtag_cook vtag;
	uint min_tagsize;

	int err;
	uint fin :1;

	ffogg_page page;
	ogg_packet opkt;
	uint64 granpos;
	ffarr buf;
	size_t datalen;
	const char *data;
	uint max_pagesize;

	size_t pcmlen;
	const float **pcm; //non-interleaved
} ffogg_enc;

FF_EXTN void ffogg_enc_init(ffogg_enc *o);

FF_EXTN void ffogg_enc_close(ffogg_enc *o);

/** Prepare for encoding.
@quality: -10..100.
@serialno: stream serial number (random).
Return 0 on success or negative number on error. */
FF_EXTN int ffogg_create(ffogg_enc *o, ffpcm *pcm, int quality, uint serialno);

/** Encode PCM data.
Return enum FFOGG_R. */
FF_EXTN int ffogg_encode(ffogg_enc *o);

/** Get approximate output file size.
Must be called only once after FFOGG_RDATA is returned for the first time. */
FF_EXTN uint64 ffogg_enc_size(ffogg_enc *o, uint64 total_samples);
