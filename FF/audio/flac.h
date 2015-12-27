/** FLAC.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/ogg.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>

#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>
#include <FLAC/metadata.h>


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


typedef struct ffflac_enc {
	FLAC__StreamEncoder *enc;
	ffpcm fmt;
	FLAC__StreamMetadata *meta[2];
	uint metasize;
	uint bpsample;
	uint err;
	uint errtype;

	size_t datalen;
	const byte *data;
	ffstr3 data32; //int[]

	size_t pcmlen
		, off;
	const void *pcmi;
	const void **pcm;
	ffstr3 outbuf;

	uint64 total_samples;
	uint min_meta;
	uint level; //0..8
	uint fin :1;
} ffflac_enc;

FF_EXTN const char* ffflac_enc_errstr(ffflac_enc *f);

FF_EXTN void ffflac_enc_init(ffflac_enc *f);

/** Return 0 on success. */
FF_EXTN int ffflac_addtag(ffflac_enc *f, const char *name, const char *val);

#define ffflac_iaddtag(f, tag, val) \
	ffflac_addtag(f, ffflac_tagstr[tag], val)

/** Return 0 on success. */
FF_EXTN int ffflac_create(ffflac_enc *f, const ffpcm *format);

FF_EXTN void ffflac_enc_close(ffflac_enc *f);

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_encode(ffflac_enc *f);
