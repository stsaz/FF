/** FLAC.
Copyright (c) 2015 Simon Zolin
*/

/*
fLaC (HDR STREAMINFO) [HDR BLOCK]... (FRAME_HDR SUBFRAME... FRAME_FOOTER)...

ffflac_encode():
fLaC INFO VORBIS_CMT [PADDING] [SEEKTABLE] (FRAME)...
*/

#pragma once

#include <FF/audio/vorbistag.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>

#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>
#include <FLAC/metadata.h>


typedef struct ffflac_info {
	uint bits;
	uint channels;
	uint sample_rate;

	uint minblock, maxblock;
	uint minframe, maxframe;
	uint64 total_samples;
	char md5[16];
} ffflac_info;

typedef struct _ffflac_seektab {
	uint len;
	ffpcm_seekpt *ptr;
} _ffflac_seektab;

typedef struct ffflac {
	FLAC__StreamDecoder *dec;
	int st;
	int r;
	int err;
	ffpcm fmt;
	uint bpsample;
	size_t nbuf;
	union {
	short *out16[FLAC__MAX_CHANNELS];
	const int *out32[FLAC__MAX_CHANNELS];
	};
	ffstr3 buf;
	uint64 off
		, total_size;
	uint64 frsample;
	unsigned fin :1
		, errtype :8
		;

	size_t datalen;
	const byte *data;

	size_t pcmlen;
	void **pcm;

	ffstr tagname
		, tagval;
	uint idx;
} ffflac;

enum FFFLAC_R {
	FFFLAC_RERR = -1
	, FFFLAC_RDATA
	, FFFLAC_RSEEK
	, FFFLAC_RMORE
	, FFFLAC_RDONE

	, FFFLAC_RHDR
	, FFFLAC_RTAG
	, FFFLAC_RHDRFIN
};

FF_EXTN const char* ffflac_errstr(ffflac *f);

FF_EXTN void ffflac_init(ffflac *f);

/** Return 0 on success. */
FF_EXTN int ffflac_open(ffflac *f);

FF_EXTN void ffflac_close(ffflac *f);

/** Return total samples or 0 if unknown. */
#define ffflac_totalsamples(f) \
	FLAC__stream_decoder_get_total_samples((f)->dec)

/** Get average bitrate.  May be called when FFFLAC_RHDRFIN is returned. */
#define ffflac_bitrate(f) \
	ffpcm_brate((f)->total_size - (f)->framesoff, (f)->info.total_samples, (f)->fmt.sample_rate)

FF_EXTN void ffflac_seek(ffflac *f, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffflac_seekoff(f)  ((f)->off)

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_decode(ffflac *f);

/** Get an absolute sample number. */
#define ffflac_cursample(f)  ((f)->frsample)


enum FFFLAC_ENC_OPT {
	FFFLAC_ENC_NOMD5 = 1, // don't generate MD5 checksum of uncompressed data
};

typedef struct ffflac_enc {
	uint state;
	FLAC__StreamEncoder *enc;
	ffflac_info info;
	uint err;
	uint errtype;
	ffvorbtag_cook vtag;
	uint64 seekoff;
	uint64 frlen;
	uint64 metalen;
	uint64 nsamps;

	size_t datalen;
	const byte *data;

	size_t pcmlen
		, off;
	const int **pcm;
	ffstr3 outbuf;

	uint64 total_samples;
	uint min_meta; // minimum size of meta data (add padding block if needed)
	uint level; //0..8.  Default: 5.
	uint fin :1
		, have_padding :1;

	uint seektable_int; // interval (in samples) for seek table.  Default: 1 sec.  0=disabled.
	uint iskpt;
	_ffflac_seektab sktab;

	uint opts; //enum FFFLAC_ENC_OPT
} ffflac_enc;

FF_EXTN const char* ffflac_enc_errstr(ffflac_enc *f);

FF_EXTN void ffflac_enc_init(ffflac_enc *f);

/** Return 0 on success. */
FF_EXTN int ffflac_create(ffflac_enc *f, const ffpcm *format);

FF_EXTN void ffflac_enc_close(ffflac_enc *f);

/**
Note: support only up to 4k data. */
FF_EXTN int ffflac_addtag(ffflac_enc *f, const char *name, const char *val, size_t vallen);

#define ffflac_iaddtag(f, tag, val, vallen) \
	ffflac_addtag(f, ffvorbtag_str[tag], val, vallen)

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_encode(ffflac_enc *f);

/** Get approximate output file size. */
static FFINL uint64 ffflac_enc_size(ffflac_enc *f)
{
	return f->metalen + f->total_samples * ffpcm_size(f->info.bits, f->info.channels) * 73 / 100;
}
