/** OGG Vorbis.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/array.h>

#include <vorbis/codec.h>


typedef struct ffogg {
	uint state;

	ogg_sync_state osync;
	ogg_sync_state osync_seek;
	ogg_page opg;
	ogg_stream_state ostm;

	vorbis_dsp_state vds;
	vorbis_info vinfo;
	vorbis_block vblk;

	vorbis_comment vcmt;
	ffstr tagname
		, tagval;

	uint err;
	union {
	uint nsamples;
	uint nhdr;
	uint ncomm;
	};
	uint64 off
		, off_data //header size
		, total_size;
	uint64 total_samples
		, cursample
		, first_sample
		, seek_sample;

	size_t datalen;
	const char *data;

	size_t pcmlen;
	const float **pcm; //non-interleaved

	unsigned ostm_valid :1
		, vblk_valid :1
		, seekable :1 //search for eos page
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

#define ffogg_granulepos(o)  ogg_page_granulepos(&(o)->opg)

#define ffogg_pageno(o)  ogg_page_pageno(&(o)->opg)

FF_EXTN void ffogg_close(ffogg *o);


enum FFOGG_VORBTAG {
	FFOGG_ALBUM
	, FFOGG_ARTIST
	, FFOGG_COMMENT
	, FFOGG_DATE
	, FFOGG_GENRE
	, FFOGG_TITLE
	, FFOGG_TRACKNO
	, FFOGG_TRACKTOTAL
};

FF_EXTN const char *const ffogg_vorbtagstr[];

/** Return enum FFOGG_VORBTAG or -1 of unknown tag. */
FF_EXTN int ffogg_tag(const char *name, size_t len);

/** Add vorbis tag. */
#define ffogg_addtag(o, name, val) \
	vorbis_comment_add_tag(&(o)->vcmt, name, val)

#define ffogg_iaddtag(o, tag, val) \
	ffogg_addtag(o, ffogg_vorbtagstr[tag], val)


FF_EXTN void ffogg_seek(ffogg *o, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffogg_seekoff(o)  ((o)->off)

/** No more input data.
Return 0 if decoding can proceed. */
FF_EXTN int ffogg_nodata(ffogg *o);

/** Decode OGG stream.
Note: decoding errors are skipped. */
FF_EXTN int ffogg_decode(ffogg *o);

/** Get an absolute sample number. */
#define ffogg_cursample(o)  ((o)->cursample)


typedef struct ffogg_enc {
	uint state;

	ogg_sync_state osync;
	ogg_page opg;
	ogg_stream_state ostm;

	vorbis_dsp_state vds;
	vorbis_info vinfo;
	vorbis_block vblk;

	vorbis_comment vcmt;

	int err;
	unsigned ostm_valid :1
		, vblk_valid :1
		, fin :1;

	size_t datalen;
	const char *data;

	size_t pcmlen;
	const float **pcm; //non-interleaved
} ffogg_enc;

FF_EXTN void ffogg_enc_init(ffogg_enc *o);

FF_EXTN void ffogg_enc_close(ffogg_enc *o);

/** Prepare for encoding.
The function uses ffrnd_get().
@quality: -10..100.
Return 0 on success or negative number on error. */
FF_EXTN int ffogg_create(ffogg_enc *o, ffpcm *pcm, int quality);

/** Encode PCM data.
Return enum FFOGG_R. */
FF_EXTN int ffogg_encode(ffogg_enc *o);

/** Get OGG page header after ffogg_encode() returns a page. */
static FFINL const char* ffogg_pagehdr(ffogg_enc *o, size_t *plen)
{
	*plen = o->opg.header_len;
	return (void*)o->opg.header;
}
