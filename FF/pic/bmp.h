/** BMP.
Copyright (c) 2016 Simon Zolin
*/

/*
HDR (ROW#HEIGHT..ROW#1(BGR#1..BGR#WIDTH))
*/

#pragma once

#include <FF/pic/pic.h>
#include <FF/array.h>
#include <FF/number.h>


typedef struct ffbmp {
	uint state;
	uint nxstate;
	uint e;
	ffstr data;
	ffstr rgb;
	ffarr inbuf;
	uint linesize;
	uint line;
	uint dataoff;
	uint gather_size;
	uint64 seekoff;

	struct {
		uint width;
		uint height;
		uint bpp;
	} info;
} ffbmp;

enum FFBMP_R {
	FFBMP_ERR,
	FFBMP_MORE,
	FFBMP_DONE,
	FFBMP_HDR,
	FFBMP_DATA,
	FFBMP_SEEK,
};

FF_EXTN const char* ffbmp_errstr(void *b);

FF_EXTN void ffbmp_open(ffbmp *b);

FF_EXTN void ffbmp_close(ffbmp *b);

typedef struct ffbmp_pos {
	uint x, y;
	uint width, height;
} ffbmp_pos;

FF_EXTN void ffbmp_region(const ffbmp_pos *pos);

FF_EXTN int ffbmp_read(ffbmp *b);

/** Get input/output stream seek offset. */
#define ffbmp_seekoff(b)  ((b)->seekoff)


typedef struct ffbmp_cook {
	uint state;
	uint linesize;
	uint e;
	ffstr data;
	ffstr rgb;
	ffarr buf;
	uint line;
	uint64 seekoff;

	struct {
		uint width;
		uint height;
		uint bpp;
	} info;
} ffbmp_cook;

FF_EXTN void ffbmp_create(ffbmp_cook *b);

FF_EXTN void ffbmp_wclose(ffbmp_cook *b);

static FFINL uint ffbmp_wsize(ffbmp_cook *b)
{
	return b->info.height * b->info.width * (b->info.bpp / 8);
}

FF_EXTN int ffbmp_write(ffbmp_cook *b);
