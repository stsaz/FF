/** AAC.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>

#include <fdk-aac-ff.h>


typedef struct ffaac {
	fdkaac_decoder *dec;

	int err;
	char serr[32];

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
};

FF_EXTN const char* ffaac_errstr(ffaac *a);

FF_EXTN int ffaac_open(ffaac *a, uint channels, const char *conf, size_t len);

FF_EXTN void ffaac_close(ffaac *a);

/**
Return enum FFAAC_R. */
FF_EXTN int ffaac_decode(ffaac *a);

/** Seek on decoded frame (after a target frame is found in container). */
FF_EXTN void ffaac_seek(ffaac *a, uint64 sample);
