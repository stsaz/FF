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
	uint qual;

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

	struct ffmpg_info xing;

	ffarr buf;
	size_t datalen;
	const void *data;
	uint off;

	uint fin :1
		, ileaved :1
		, have_xing :1
		;
	uint options; //enum FFMPG_ENC_OPT
} ffmpg_enc;

#define ffmpg_enc_seekoff(m)  ((m)->off)

/**
@qual: 9..0(better) for VBR or 10..320 for CBR
Return enum FFMPG_E. */
FF_EXTN int ffmpg_create(ffmpg_enc *m, ffpcm *pcm, int qual);

FF_EXTN void ffmpg_enc_close(ffmpg_enc *m);

FF_EXTN int ffmpg_addtag(ffmpg_enc *m, uint id, const char *val, size_t vallen);

/** Get approximate output file size. */
FF_EXTN uint64 ffmpg_enc_size(ffmpg_enc *m, uint64 total_samples);

/**
Return enum FFMPG_R. */
FF_EXTN int ffmpg_encode(ffmpg_enc *m);

FF_EXTN const char* ffmpg_enc_errstr(ffmpg_enc *m);


FF_EXTN void ffmpg_create_copy(ffmpg_enc *m);

/** Pass MPEG frame as-is.  Skip input Xing frame.  Write Xing frame on finish. */
FF_EXTN int ffmpg_writeframe(ffmpg_enc *m, const char *fr, uint len, ffstr *data);
