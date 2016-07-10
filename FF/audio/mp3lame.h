/** libmp3lame wrapper.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/mpeg.h>

#include <mp3lame/lame-ff.h>


enum FFMPG_ENC_OPT {
	FFMPG_WRITE_ID3V1 = 1,
	FFMPG_WRITE_ID3V2 = 2,
};

typedef struct ffmpg_enc {
	uint state;
	int err;
	lame *lam;
	uint fmt;
	uint channels;

	ffid3_cook id3;
	ffid31 id31;
	uint min_meta;

	size_t pcmlen;
	union {
	const short **pcm;
	const float **pcmf;
	const short *pcmi;
	};
	size_t pcmoff;

	ffarr buf;
	size_t datalen;
	void *data;
	uint off;

	uint fin :1
		, ileaved :1;
	uint options; //enum FFMPG_ENC_OPT
} ffmpg_enc;

#define ffmpg_enc_seekoff(m)  ((m)->off)

/**
@qual: 9..0(better) for VBR or 10..320 for CBR
Return enum FFMPG_E. */
FF_EXTN int ffmpg_create(ffmpg_enc *m, ffpcm *pcm, int qual);

FF_EXTN void ffmpg_enc_close(ffmpg_enc *m);

FF_EXTN int ffmpg_addtag(ffmpg_enc *m, uint id, const char *val, size_t vallen);

/**
Return enum FFMPG_R. */
FF_EXTN int ffmpg_encode(ffmpg_enc *m);

FF_EXTN const char* ffmpg_enc_errstr(ffmpg_enc *m);
