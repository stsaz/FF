/** CAF.
Copyright (c) 2020 Simon Zolin
*/

/*
HDR ADESC [COOKIE] [TAGS] PACKET_INFO ADATA(EDIT_COUNT PACKETS...)
*/

#pragma once

#include <FF/array.h>
#include <FF/audio/pcm.h>


enum FFCAF_FMT {
	FFCAF_UKN,
	FFCAF_AAC,
	FFCAF_ALAC,
};

typedef struct ffcaf_info {
	uint format; // enum FFCAF_FMT
	ffpcm pcm;
	uint packet_bytes;
	uint packet_frames;
	uint64 total_packets;
	uint64 total_frames;
	uint bitrate;
} ffcaf_info;

typedef struct ffcaf {
	uint state;
	int err;

	uint nxstate;
	size_t gathlen;
	ffstr chunk;
	ffarr buf;

	uint64 inoff; // input offset
	uint64 chunk_size; // current chunk size
	uint64 ipkt; // current packet
	uint64 iframe; // current frame
	uint64 pakt_off; // current offset in 'pakt'
	ffstr pakt; // packets sizes
	ffstr asc;
	ffstr in;
	ffstr out;

	ffstr tagname;
	ffstr tagval;

	ffcaf_info info;

	uint fin :1;
} ffcaf;

FF_EXTN const char* ffcaf_errstr(ffcaf *c);

FF_EXTN int ffcaf_open(ffcaf *c);
FF_EXTN void ffcaf_close(ffcaf *c);

enum FFCAF_R {
	FFCAF_ERR,
	FFCAF_MORE,
	FFCAF_SEEK,

	FFCAF_HDR,
	FFCAF_TAG,
	FFCAF_DATA,
	FFCAF_DONE,
};

/**
Return enum FFCAF_R */
FF_EXTN int ffcaf_read(ffcaf *c);

#define ffcaf_input(c, d, len)  ffstr_set(&(c)->in, d, len)
#define ffcaf_output(c)  (c)->out
#define ffcaf_asc(c)  (c)->asc
#define ffcaf_seekoff(c)  (c)->inoff
#define ffcaf_cursample(c)  (c)->iframe
#define ffcaf_fin(c)  (c)->fin = 1
