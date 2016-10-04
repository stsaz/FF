/** PNG.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/number.h>

#include <png/png-ff.h>


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
		uint bpp;
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
		uint bpp;
		uint complevel; //0..9
		uint comp_bufsize;
	} info;
} ffpng_cook;

FF_EXTN const char* ffpng_werrstr(ffpng_cook *p);

FF_EXTN void ffpng_create(ffpng_cook *p);

FF_EXTN void ffpng_wclose(ffpng_cook *p);

FF_EXTN int ffpng_write(ffpng_cook *p);
