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
	ffflac_frame frame;
	ffstr in;

	size_t pcmlen;
	void **pcm;
	const void *out[FLAC__MAX_CHANNELS];
} ffflac_dec;


typedef struct ffflac {
	int st;
	int err;
	uint errtype;
	ffpcm fmt;
	ffstr3 buf;
	uint bufoff;
	uint64 off
		, total_size;
	uint framesoff;
	ffflac_frame frame;
	byte first_framehdr[4];
	uint64 seeksample;
	unsigned fin :1
		, hdrlast :1
		, seek_ok :1
		;

	ffflac_info info;
	uint blksize;
	ffvorbtag vtag;

	_ffflac_seektab sktab;
	ffpcm_seekpt seekpt[2];
	uint64 skoff;

	size_t datalen;
	const char *data;
	uint bytes_skipped; // bytes skipped while trying to find sync

	ffstr output;

	// deprecated:
	ffflac_dec decoder;
	size_t pcmlen;
	void **pcm;
} ffflac;

FF_EXTN const char* ffflac_errstr(ffflac *f);

FF_EXTN void ffflac_init(ffflac *f);

/** Return 0 on success. */
FF_EXTN int ffflac_open(ffflac *f);

FF_EXTN void ffflac_close(ffflac *f);

/** Return total samples or 0 if unknown. */
#define ffflac_totalsamples(f)  ((f)->info.total_samples)

/** Get average bitrate.  May be called when FFFLAC_RHDRFIN is returned. */
static FFINL uint ffflac_bitrate(ffflac *f)
{
	if (f->total_size == 0)
		return 0;
	return ffpcm_brate(f->total_size - f->framesoff, f->info.total_samples, f->fmt.sample_rate);
}

FF_EXTN void ffflac_seek(ffflac *f, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffflac_seekoff(f)  ((f)->off)

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_read(ffflac *f);
FF_EXTN int ffflac_read_decode(ffflac *f); // deprecated

/** Get an absolute sample number. */
#define ffflac_cursample(f)  ((f)->decoder.frsample)


FF_EXTN const char* ffflac_dec_errstr(ffflac_dec *f);

/** Return 0 on success. */
FF_EXTN int ffflac_dec_open(ffflac_dec *f, const ffflac_info *info);

FF_EXTN void ffflac_dec_close(ffflac_dec *f);

#define ffflac_dec_seek(f, sample) \
	(f)->seeksample = sample

#define ffflac_dec_input(f, fr, input) \
	(f)->frame = *(fr),  (f)->in = *(input)

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_decode(ffflac_dec *f);

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
