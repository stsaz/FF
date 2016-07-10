/** WavPack.
Copyright (c) 2015 Simon Zolin
*/

/*
(HDR SUB_BLOCK...)...
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/audio/apetag.h>
#include <FF/audio/id3.h>
#include <FF/array.h>

#include <wavpack/wavpack-ff.h>


enum FFWVPK_E {
	FFWVPK_EHDR,
	FFWVPK_EFMT,
	FFWVPK_ESEEK,
	FFWVPK_EAPE,
	FFWVPK_ESYNC,
	FFWVPK_ENOSYNC,

	FFWVPK_ESYS,
	FFWVPK_EDECODE,
};

typedef struct ffwvpk_info {
	uint version;
	uint block_samples;
	uint comp_level;
	uint total_samples;
	byte md5[16];
	uint lossless :1;
} ffwvpk_info;

FF_EXTN const char *const ffwvpk_comp_levelstr[];

enum FFWVPK_O {
	FFWVPK_O_ID3V1 = 1,
	FFWVPK_O_APETAG = 2,
};

typedef struct ffwvpack {
	ffwvpk_info info;
	uint state;
	wavpack_ctx *wp;
	ffpcm fmt;
	uint frsize;
	uint err;
	uint64 total_size
		, off;
	uint64 seek_sample;

	union {
	ffid31ex id31tag;
	ffapetag apetag;
	};
	int tag;
	ffstr tagval;

	const void *data;
	size_t datalen;
	ffarr buf;
	uint blksize; // size of the current block
	uint blk_samples; // samples in the current block
	uint bytes_skipped; // bytes skipped while trying to find sync
	uint64 samp_idx;

	union {
	short *pcm;
	int *pcm32;
	float *pcmf;
	};
	uint pcmlen;
	uint outcap; //samples

	ffpcm_seekpt seektab[2];
	ffpcm_seekpt seekpt[2];
	uint64 skoff;

	uint options; //enum FFWVPK_O
	uint fin :1
		, hdr_done :1
		, seek_ok :1
		, is_apetag :1;
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

#define ffwvpk_total_samples(w)  ((int64)(w)->info.total_samples)

#define ffwvpk_cursample(w)  ((int64)(w)->samp_idx)

#define ffwvpk_bitrate(w) \
	ffpcm_brate((w)->total_size, (w)->info.total_samples, (w)->fmt.sample_rate)
