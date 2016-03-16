/** ALAC.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/array.h>

#include <alac.h>


typedef struct ffalac {
	struct alac_ctx *al;

	int err;
	char serr[32];

	ffpcm fmt;
	uint bitrate; // 0 if unknown

	const char *data;
	size_t datalen;

	const void *pcm; // 16|20|24|32-bits interleaved
	uint pcmlen; // PCM data length in bytes
	ffarr buf;
	uint64 cursample;
	uint64 seek_sample;
} ffalac;

enum FFALAC_R {
	FFALAC_RERR = -1,
	FFALAC_RDATA,
	FFALAC_RMORE,
};

FF_EXTN const char* ffalac_errstr(ffalac *a);

/** Parse ALAC magic cookie. */
FF_EXTN int ffalac_open(ffalac *a, const char *data, size_t len);

FF_EXTN void ffalac_close(ffalac *a);

/**
Return enum FFALAC_R. */
FF_EXTN int ffalac_decode(ffalac *a);

/** Seek on decoded frame (after a target frame is found in container). */
FF_EXTN void ffalac_seek(ffalac *a, uint64 sample);

#define ffalac_cursample(a)  ((a)->cursample - (a)->pcmlen / ffpcm_size1(&(a)->fmt))
