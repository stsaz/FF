/** AAC.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/array.h>

#include <fdk-aac/fdk-aac-ff.h>


typedef struct ffaac {
	fdkaac_decoder *dec;
	int err;

	const char *data;
	size_t datalen;

	ffpcm fmt;
	short *pcm;
	uint pcmlen; // PCM data length in bytes
	void *pcmbuf;
	uint frsamples;
	uint64 skip_samples;
} ffaac;

enum FFAAC_R {
	FFAAC_RERR = -1,
	FFAAC_RDATA,
	FFAAC_RMORE,
	FFAAC_RDONE,
};

FF_EXTN const char* ffaac_errstr(ffaac *a);

FF_EXTN int ffaac_open(ffaac *a, uint channels, const char *conf, size_t len);

FF_EXTN void ffaac_close(ffaac *a);

/**
Return enum FFAAC_R. */
FF_EXTN int ffaac_decode(ffaac *a);

/** Seek on decoded frame (after a target frame is found in container). */
FF_EXTN void ffaac_seek(ffaac *a, uint64 sample);


typedef struct ffaac_enc {
	fdkaac_encoder *enc;
	fdkaac_conf info;
	int err;

	const char *data;
	size_t datalen;
	void *buf;

	const short *pcm;
	uint pcmlen; // PCM data length in bytes

	uint fin :1;
} ffaac_enc;

FF_EXTN const char* ffaac_enc_errstr(ffaac_enc *a);

FF_EXTN int ffaac_create(ffaac_enc *a, const ffpcm *fmt, uint quality);

FF_EXTN void ffaac_enc_close(ffaac_enc *a);

/**
Return enum FFAAC_R. */
FF_EXTN int ffaac_encode(ffaac_enc *a);

/** Get ASC. */
static FFINL ffstr ffaac_enc_conf(ffaac_enc *a)
{
	ffstr s;
	ffstr_set(&s, a->info.conf, a->info.conf_len);
	return s;
}

/** Get audio samples per AAC frame. */
#define ffaac_enc_frame_samples(a)  ((a)->info.frame_samples)

/** Get max. size per AAC frame. */
#define ffaac_enc_frame_maxsize(a)  ((a)->info.max_frame_size)
