/** PNG.
Copyright (c) 2016 Simon Zolin
*/

/*
SIGN IHDR IDAT... IEND
*/

#pragma once

#include <FF/pic/pic.h>
#include <FF/array.h>
#include <FF/number.h>

#include <png/png-ff.h>

#define FFPNG_MIME "image/png"


enum FFPNG_E {
	FFPNG_EFMT = 1,
	FFPNG_ELINE,

	FFPNG_ESYS,
};


/** .png reader */
struct ffpngr {
	uint state, nxstate;
	uint gathlen;
	ffarr buf;
	ffstr chunk;
	struct {
		uint width, height;
		uint bpp;
	} info;
	ffstr input;
};

FF_EXTN int ffpngr_open(struct ffpngr *p);
FF_EXTN void ffpngr_close(struct ffpngr *p);

/**
Return enum FFPNG_R. */
FF_EXTN int ffpngr_read(struct ffpngr *p);


typedef struct ffpng {
	uint state;
	uint e;
	ffstr data;
	ffstr rgb;
	ffarr buf;
	uint linesize;

	struct png_reader *png;

	struct {
		uint width;
		uint height;
		uint format;
		uint total_size;
	} info;
} ffpng;

enum FFPNG_R {
	FFPNG_ERR,
	FFPNG_MORE,
	FFPNG_DONE,
	FFPNG_HDR,
	FFPNG_DATA,
};

FF_EXTN const char* ffpng_errstr(ffpng *p);

FF_EXTN void ffpng_open(ffpng *p);

FF_EXTN void ffpng_close(ffpng *p);

#define ffpng_input(p, _data, len)  ffstr_set(&(p)->data, _data, len)
#define ffpng_output(p)  (p)->rgb

/**
Return enum FFPNG_R. */
FF_EXTN int ffpng_read(ffpng *p);


typedef struct ffpng_cook {
	uint state;
	uint e;
	ffstr data;
	ffstr rgb;
	uint linesize;

	struct png_writer *png;

	struct {
		uint width;
		uint height;
		uint format;
		uint complevel; //0..9
		uint comp_bufsize;
	} info;
} ffpng_cook;

FF_EXTN const char* ffpng_werrstr(ffpng_cook *p);

/**
Return 0 on success;  enum FFPNG_E on error. */
FF_EXTN int ffpng_create(ffpng_cook *p, ffpic_info *info);

FF_EXTN void ffpng_wclose(ffpng_cook *p);

#define ffpng_winput(p, _data, len)  ffstr_set(&(p)->rgb, _data, len)
#define ffpng_woutput(p)  (p)->data

FF_EXTN int ffpng_write(ffpng_cook *p);
