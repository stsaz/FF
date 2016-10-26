/** OGG.
Copyright (c) 2015 Simon Zolin
*/

/*
OGG_PAGE(PAGE_HDR PACKET...)...
*/

#pragma once

#include <FF/audio/ogg-fmt.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>


typedef struct ffogg {
	uint state;
	uint err;
	uint serial;
	uint64 off
		, off_data //header size
		, total_size;
	uint64 total_samples
		, cursample
		, first_sample
		, seek_sample;

	ffarr buf;
	ffarr pktdata; //holds partial packet data
	uint hdrsize;
	uint pagesize;
	uint bytes_skipped;
	uint page_num;
	uint64 page_gpos;
	uint pgno;
	uint pktno;
	uint segoff;
	uint bodyoff;
	size_t datalen;
	const char *data;

	ffstr out;

	ffpcm_seekpt seektab[2];
	ffpcm_seekpt seekpt[2];
	uint64 skoff;

	uint seekable :1 //search for eos page
		, init_done :1
		, page_continued :1
		, page_last :1
		, firstseek :1
		, pagenum_err :1
		, continued :1 //expecting continued packet within next page
		;
} ffogg;

FF_EXTN const char* ffogg_errstr(int e);

/** Initialize ffogg. */
FF_EXTN void ffogg_init(ffogg *o);

/** Get bps. */
FF_EXTN uint ffogg_bitrate(ffogg *o, uint sample_rate);

enum FFOGG_R {
	FFOGG_RWARN = -2
	, FFOGG_RERR = -1
	, FFOGG_RMORE = 0
	, FFOGG_RDATA
	, FFOGG_RSEEK
	, FFOGG_RDONE

	, FFOGG_RHDR
	, FFOGG_RHDRFIN //header is finished
	, FFOGG_RINFO //total_samples is set, ready for seeking
};

#define ffogg_granulepos(o)  ((o)->page_gpos)

#define ffogg_pageno(o)  ((o)->page_num)

FF_EXTN void ffogg_close(ffogg *o);

FF_EXTN void ffogg_seek(ffogg *o, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffogg_seekoff(o)  ((o)->off)

/** Decode OGG stream.
Return enum FFOGG_R. */
FF_EXTN int ffogg_read(ffogg *o);

/** Get an absolute sample number. */
#define ffogg_cursample(o)  ((o)->cursample)


typedef struct ffogg_cook {
	uint state;
	int err;
	ffogg_page page;
	ffarr buf;
	uint max_pagedelta; //in samples
	uint64 page_startpos;
	uint64 page_endpos;

	struct {
		uint64 npkts;
		uint64 npages;
		uint64 total_ogg;
		uint64 total_payload;
	} stat;

	uint64 pkt_endpos; //position at which the packet ends (granule pos)
	ffstr pkt; //input packet
	ffstr out; //output OGG page
	uint allow_partial :1;
	uint continued :1;
	uint flush :1;
	uint fin :1;
} ffogg_cook;

/** Create OGG stream.
@serialno: stream serial number (random).
Return 0 on success or enum OGG_E. */
FF_EXTN int ffogg_create(ffogg_cook *o, uint serialno);

FF_EXTN void ffogg_wclose(ffogg_cook *o);

enum FFOGG_F {
	FFOGG_FLUSH = 1,
	FFOGG_FIN = 2,
};

/** Add packet and return OGG page when ready.
Return enum FFOGG_R. */
FF_EXTN int ffogg_write(ffogg_cook *o);
