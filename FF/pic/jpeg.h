/** JPEG.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/pic/pic.h>
#include <FF/array.h>
#include <FF/number.h>
#include <FFOS/error.h>

#include <jpeg/jpeg-ff.h>


enum FFJPEG_E {
	FFJPEG_EFMT = 1,
	FFJPEG_ELINE,

	FFJPEG_ESYS,
};


typedef struct ffjpeg {
	uint state;
	uint e;
	ffstr data;
	ffstr rgb;
	ffarr buf;
	uint linesize;
	uint line;

	struct jpeg_reader *jpeg;

	struct {
		uint width;
		uint height;
		uint format; //enum FFPIC_FMT
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

#define ffjpeg_input(j, _data, len)  ffstr_set(&(j)->data, _data, len)
#define ffjpeg_output(j)  (j)->rgb

FF_EXTN int ffjpeg_read(ffjpeg *p);

#define ffjpeg_line(j)  (j)->line


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
		uint format; //enum FFPIC_FMT
		uint quality; //0..100
		uint bufcap;
	} info;
} ffjpeg_cook;

FF_EXTN const char* ffjpeg_werrstr(ffjpeg_cook *p);

/**
Return 0 on success;  enum FFJPEG_E on error. */
FF_EXTN int ffjpeg_create(ffjpeg_cook *j, ffpic_info *info);

FF_EXTN void ffjpeg_wclose(ffjpeg_cook *p);

#define ffjpeg_winput(j, _data, len)  ffstr_set(&(j)->rgb, _data, len)
#define ffjpeg_woutput(j)  (j)->data

FF_EXTN int ffjpeg_write(ffjpeg_cook *p);
