/** WavPack.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/audio/apetag.h>
#include <FF/audio/id3.h>
#include <FF/array.h>

#include <wavpack.h>


enum FFWVPK_E {
	FFWVPK_EFMT,
	FFWVPK_EBIGBUF,
	FFWVPK_ESEEK,
	FFWVPK_ESEEKLIM,
	FFWVPK_EAPE,

	FFWVPK_ESYS,
	FFWVPK_EDECODE,
};

enum FFWVPK_O {
	FFWVPK_O_ID3V1 = 1,
	FFWVPK_O_APETAG = 2,
};

typedef struct ffwvpack {
	uint state;
	WavpackContext *wp;
	ffpcm fmt;
	uint frsize;
	uint err;
	uint64 total_size
		, off;
	uint64 seek_sample;
	uint64 skip_samples;
	uint seek_cnt;

	uint lastoff;
	ffid31ex id31tag;
	ffapetag apetag;

	const void *data;
	size_t datalen;
	ffarr buf;
	size_t bufoff;

	union {
	short *pcm;
	int *pcm32;
	float *pcmf;
	};
	uint pcmlen;
	uint outcap; //samples

	uint options; //enum FFWVPK_O
	uint fin :1
		, is_apetag :1
		, async :1
		, apetag_closed :1;
} ffwvpack;

enum FFWVPK_R {
	FFWVPK_RWARN = -2,
	FFWVPK_RERR = -1,
	FFWVPK_RDATA,
	FFWVPK_RHDR,
	FFWVPK_RHDRFIN,
	FFWVPK_RDONE,
	FFWVPK_RMORE,
	FFWVPK_RSEEK,
	FFWVPK_RTAG,
};

FF_EXTN const char* ffwvpk_errstr(ffwvpack *w);

FF_EXTN void ffwvpk_close(ffwvpack *w);

FF_EXTN void ffwvpk_seek(ffwvpack *w, uint64 sample);

#define ffwvpk_seekoff(w)  ((int64)(w)->off)

/** Return enum FFWVPK_R. */
FF_EXTN int ffwvpk_decode(ffwvpack *w);

#define ffwvpk_total_samples(w)  ((int64)WavpackGetNumSamples((w)->wp))

#define ffwvpk_cursample(w)  ((int64)WavpackGetSampleIndex((w)->wp) - (w)->pcmlen / ffpcm_size1(&(w)->fmt))

#define ffwvpk_bitrate(w)  ((int)WavpackGetAverageBitrate((w)->wp, 0))
