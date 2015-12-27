/** APE (MAC).
Copyright (c) 2015 Simon Zolin
*/

/*
(DESC HDR) SEEK_TBL  [WAV_HDR]
ver <= 3.97: HDR  [WAV_HDR]  SEEK_TBL
*/

#pragma once

#include <FF/audio/apetag.h>
#include <FF/audio/id3.h>
#include <FF/audio/pcm.h>


enum FFAPE_R {
	FFAPE_RDATA,
	FFAPE_RWARN,
	FFAPE_RERR,
	FFAPE_RMORE,
	FFAPE_RSEEK,
	FFAPE_RHDR,
	FFAPE_RTAG,
	FFAPE_RHDRFIN,
	FFAPE_RDONE,
};

enum FFAPE_O {
	FFAPE_O_ID3V1 = 1,
	FFAPE_O_APETAG = 2,
};

typedef struct ffape_info {
	ffpcm fmt;
	ushort version;
	byte comp_level;
	uint seekpoints;
	uint frame_blocks;
	uint64 total_samples;
	byte md5[16];
} ffape_info;

typedef struct ffape {
	ffape_info info;
	uint64 total_size;
	uint64 off;
	uint64 froff;
	uint options; // enum FFAPE_O
	const char *data;
	size_t datalen;
	// short *pcm;
	size_t pcmlen;
	uint fin :1
		, is_apetag :1;

	union {
	ffid31ex id31tag;
	ffapetag apetag;
	};

	uint state;
	uint err;
} ffape;

FF_EXTN const char ffape_comp_levelstr[][8];

FF_EXTN const char* ffape_errstr(ffape *a);

FF_EXTN void ffape_close(ffape *a);

/**
Return enum FFAPE_R. */
FF_EXTN int ffape_decode(ffape *a);

/** Get average bitrate.  May be called when FFAPE_RHDRFIN is returned. */
#define ffape_bitrate(a) \
	ffpcm_brate((a)->total_size - (a)->froff, (a)->info.total_samples, (a)->info.fmt.sample_rate)

#define ffape_seek(a, sample)

#define ffape_totalsamples(a)  ((a)->info.total_samples)

#define ffape_cursample(a)  (0)
