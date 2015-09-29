/** libmp3lame wrapper.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/mpeg.h>

#include <lame.h>


typedef struct ffmpg_enc {
	uint state;
	int err;
	lame_global_flags *lam;
	uint fmt;
	uint channels;

	size_t pcmlen;
	union {
	const short **pcm;
	const float **pcmf;
	const short *pcmi;
	};

	size_t datalen;
	void *data;
	size_t cap;

	uint fin :1
		, ileaved :1;
} ffmpg_enc;

/**
@qual: 9..0(better) for VBR or 10..320 for CBR
Return enum FFMPG_E. */
FF_EXTN int ffmpg_create(ffmpg_enc *m, ffpcm *pcm, int qual);

FF_EXTN void ffmpg_enc_close(ffmpg_enc *m);

/**
Return enum FFMPG_R. */
FF_EXTN int ffmpg_encode(ffmpg_enc *m);

FF_EXTN const char* ffmpg_enc_errstr(ffmpg_enc *m);
