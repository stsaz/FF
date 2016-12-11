/** AVI.
Copyright (c) 2016 Simon Zolin
*/

/*
AVI (hdrl(strl...) movi(xxxx(DATA...)...) idx1(xxxx()...))
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/array.h>


struct ffavi_bchunk;
struct ffavi_chunk {
	uint id;
	uint size;
	uint flags;
	uint padding :1;
	const struct ffavi_bchunk *ctx;
};

enum STRF_FMT {
	FFAVI_AUDIO_MP3 = 0x0055,
	FFAVI_AUDIO_AAC = 0x00FF,
};

struct ffavi_audio_info {
	uint format; //enum STRF_FMT
	uint bits;
	uint channels;
	uint sample_rate;
	uint bitrate;
	uint64 total_samples;
	uint delay;
	uint blocksize;
	ffstr asc;

	uint len;
	uint scale;
	uint rate;
};

enum FFAVI_O {
	FFAVI_O_TAGS = 1, //process tags
};

typedef struct ffavi {
	uint state;
	uint nxstate;
	uint err;
	uint istm;
	uint idx_audio;
	uint64 nsamples;
	uint options; //enum FFAVI_O

	struct ffavi_chunk chunks[5];
	uint ictx;

	ffarr buf;
	uint gather_size;
	ffstr gbuf;
	uint64 off;

	struct ffavi_audio_info info;

	uint movi_off;
	uint movi_size;

	int tag; //enum FFMMTAG or -1
	ffstr tagval;

	uint has_fmt :1
		, fin :1
		;

	ffstr data;
	ffstr out;
} ffavi;

enum FFAVI_R {
	FFAVI_RWARN = -2,
	FFAVI_RERR = -1,
	FFAVI_RDATA,
	FFAVI_RMORE,
	FFAVI_RHDR,
	FFAVI_RSEEK,
	FFAVI_RDONE,
	FFAVI_RTAG,
};

FF_EXTN const char* ffavi_errstr(void *a);

FF_EXTN void ffavi_init(ffavi *a);
FF_EXTN void ffavi_close(ffavi *a);

/** Return enum FFAVI_R. */
FF_EXTN int ffavi_read(ffavi *a);

#define ffavi_cursample(a)  ((a)->nsamples - (a)->info.blocksize)
