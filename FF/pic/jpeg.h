/** JPEG.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/number.h>
#include <FFOS/error.h>

#include <jpeg/jpeg-ff.h>


typedef struct ffjpeg {
	uint state;
	uint e;
	ffstr data;
	ffstr rgb;
	ffarr buf;
	uint linesize;

	struct jpeg_reader *jpeg;

	struct {
		uint width;
		uint height;
		uint bpp;
	} info;
} ffjpeg;

enum FFJPEG_R {
	FFJPEG_ERR,
	FFJPEG_MORE,
	FFJPEG_DONE,
	FFJPEG_HDR,
	FFJPEG_DATA,
};

FF_EXTN const char* ffjpeg_errstr(ffjpeg *p);

FF_EXTN void ffjpeg_open(ffjpeg *p);

FF_EXTN void ffjpeg_close(ffjpeg *p);

FF_EXTN int ffjpeg_read(ffjpeg *p);


typedef struct ffjpeg_cook {
	uint state;
	uint e;
	ffstr data;
	ffstr rgb;
	ffarr buf;
	uint linesize;

	struct jpeg_writer *jpeg;

	struct {
		uint width;
		uint height;
		uint bpp;
		uint quality; //0..100
		uint bufcap;
	} info;
} ffjpeg_cook;

FF_EXTN const char* ffjpeg_werrstr(ffjpeg_cook *p);

FF_EXTN void ffjpeg_create(ffjpeg_cook *p);

FF_EXTN void ffjpeg_wclose(ffjpeg_cook *p);

FF_EXTN int ffjpeg_write(ffjpeg_cook *p);
