/** FLAC.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/aformat/flac-fmt.h>
#include <FF/mtags/vorbistag.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>

#include <flac/FLAC-ff.h>


typedef struct ffflac_dec {
	flac_decoder *dec;
	int err;
	uint errtype;
	ffflac_info info;
	uint64 frsample;
	uint64 seeksample;
	ffstr in;

	size_t pcmlen;
	void **pcm;
	const void *out[FLAC__MAX_CHANNELS];
} ffflac_dec;

FF_EXTN const char* ffflac_dec_errstr(ffflac_dec *f);

/** Return 0 on success. */
FF_EXTN int ffflac_dec_open(ffflac_dec *f, const ffflac_info *info);

FF_EXTN void ffflac_dec_close(ffflac_dec *f);

#define ffflac_dec_seek(f, sample) \
	(f)->seeksample = sample

/** Set input data. */
static FFINL void ffflac_dec_input(ffflac_dec *f, const ffstr *frame, uint frame_samples, uint64 frame_pos)
{
	f->in = *frame;
	f->frsample = frame_pos;
	f->pcmlen = frame_samples;
}

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_decode(ffflac_dec *f);

/** Get an absolute sample number. */
#define ffflac_dec_cursample(f)  ((f)->frsample)

/** Get output data (non-interleaved PCM). */
static FFINL size_t ffflac_dec_output(ffflac_dec *f, void ***pcm)
{
	*pcm = f->pcm;
	return f->pcmlen;
}


enum FFFLAC_ENC_OPT {
	FFFLAC_ENC_NOMD5 = 1, // don't generate MD5 checksum of uncompressed data
};

typedef struct ffflac_enc {
	uint state;
	flac_encoder *enc;
	ffflac_info info;
	uint err;
	uint errtype;

	size_t datalen;
	const byte *data;

	size_t pcmlen;
	const void **pcm;
	uint frsamps;
	ffstr3 outbuf;
	int* pcm32[FLAC__MAX_CHANNELS];
	size_t off_pcm
		, off_pcm32
		, cap_pcm32;

	uint level; //0..8.  Default: 5.
	uint fin :1;

	uint opts; //enum FFFLAC_ENC_OPT
} ffflac_enc;

FF_EXTN const char* ffflac_enc_errstr(ffflac_enc *f);

FF_EXTN void ffflac_enc_init(ffflac_enc *f);

/** Return 0 on success. */
FF_EXTN int ffflac_create(ffflac_enc *f, ffpcm *format);

FF_EXTN void ffflac_enc_close(ffflac_enc *f);

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_encode(ffflac_enc *f);

#define ffflac_enc_fin(f)  ((f)->fin = 1)
