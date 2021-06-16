/** libmp3lame wrapper.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/mpeg.h>

#include <mp3lame/lame-ff.h>


typedef struct ffmpg_enc {
	uint state;
	int err;
	lame *lam;
	ffpcm fmt;
	uint qual;

	size_t pcmlen;
	union {
	const short **pcm;
	const float **pcmf;
	const short *pcmi;
	};
	size_t pcmoff;
	uint samp_size;

	ffarr buf;
	size_t datalen;
	const void *data;
	uint off;

	uint fin :1
		, ileaved :1
		;
	uint options; //enum FFMPG_ENC_OPT
} ffmpg_enc;

/**
@qual: 9..0(better) for VBR or 10..320 for CBR
Return enum FFMPG_E. */
FF_EXTN int ffmpg_create(ffmpg_enc *m, ffpcm *pcm, int qual);

FF_EXTN void ffmpg_enc_close(ffmpg_enc *m);

/** Get approximate output file size. */
FF_EXTN uint64 ffmpg_enc_size(ffmpg_enc *m, uint64 total_samples);

/**
Return enum FFMPG_R. */
FF_EXTN int ffmpg_encode(ffmpg_enc *m);

FF_EXTN const char* ffmpg_enc_errstr(ffmpg_enc *m);
