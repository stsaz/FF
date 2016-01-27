/** OGG Vorbis.
Copyright (c) 2015 Simon Zolin
*/

/*
OGG(VORB_INFO)  OGG(VORB_COMMENTS VORB_CODEBOOK)  OGG(PKT1 PKT2...)...
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/audio/vorbistag.h>
#include <FF/array.h>

#include <vorbis/codec.h>


typedef struct ffogg_page {
	uint size;
	uint nsegments;
	byte segs[255];

	uint serial;
	uint number;
} ffogg_page;

typedef struct ffogg {
	uint state;

	ogg_stream_state ostm;

	vorbis_dsp_state vds;
	vorbis_info vinfo;
	vorbis_block vblk;

	ffvorbtag vtag;

	uint err;
	union {
	uint nsamples;
	uint nhdr;
	};
	uint64 off
		, off_data //header size
		, total_size;
	uint64 total_samples
		, cursample
		, first_sample
		, seek_sample;

	ffarr buf;
	uint hdrsize;
	uint pagesize;
	uint bytes_skipped;
	uint page_num;
	uint64 page_gpos;
	size_t datalen;
	const char *data;

	size_t pcmlen;
	const float **pcm; //non-interleaved

	ffpcm_seekpt seektab[2];
	ffpcm_seekpt seekpt[2];

	unsigned ostm_valid :1
		, vblk_valid :1
		, seekable :1 //search for eos page
		, init_done :1
		, page_last :1
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
Note: decoding errors are skipped. */
FF_EXTN int ffogg_decode(ffogg *o);

/** Get an absolute sample number. */
#define ffogg_cursample(o)  ((o)->cursample)


typedef struct ffogg_enc {
	uint state;

	vorbis_dsp_state vds;
	vorbis_info vinfo;
	vorbis_block vblk;

	ffvorbtag_cook vtag;
	uint min_tagsize;

	int err;
	unsigned ostm_valid :1
		, vblk_valid :1
		, fin :1;

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
static FFINL uint64 ffogg_enc_size(ffogg_enc *o, uint64 total_samples)
{
	uint metalen = o->datalen;
	return metalen + (224000 * total_samples) / (8 * o->vinfo.rate);
}
