/** Icon reader.
Copyright (c) 2018 Simon Zolin
*/

/*
HDR ENTRY... DATA...
*/

#pragma once

#include <FF/array.h>


struct ffico_file {
	uint width, height;
	uint format; //enum FFPIC_FMT
	uint size;
	uint offset;
};

typedef struct ffico {
	uint state, nxstate;
	uint err;

	uint gathlen;
	ffarr buf;
	ffstr chunk;

	ffarr2 ents; // struct ico_ent[]
	struct ffico_file info;
	uint icons_total;
	uint off;
	uint size;
	uint filefmt;
	uint bmphdr_size;

	ffstr input;
	ffstr out;
} ffico;

enum FFICO_R {
	FFICO_ERR,
	FFICO_MORE,

	FFICO_FILEINFO,
	FFICO_HDR,

	FFICO_SEEK,
	FFICO_FILEFORMAT,
	FFICO_DATA,
	FFICO_FILEDONE,
};

/** Open .ico reader. */
FF_EXTN int ffico_open(ffico *c);

/** Close .ico reader. */
FF_EXTN void ffico_close(ffico *c);

/** Set input data. */
#define ffico_input(c, data, size)  ffstr_set(&(c)->input, data, size)

/** Process data.
Return enum FFICO_R. */
FF_EXTN int ffico_read(ffico *c);

/** Get the current file information (FFICO_FILEINFO). */
static inline const struct ffico_file* ffico_fileinfo(ffico *c)
{
	return &c->info;
}

/** Start reading file data specified by 0-based index.
User gets this file info by ffico_fileinfo(). */
FF_EXTN void ffico_readfile(ffico *c, uint idx);

/** Get file offset to seek to (FFICO_SEEK). */
#define ffico_offset(c)  ((c)->off)

/** Get output file data (FFICO_DATA). */
#define ffico_data(c)  ((c)->out)

enum FFICO_FILEFMT {
	FFICO_UKN,
	FFICO_BMP,
	FFICO_PNG,
};

/** Get data format (FFICO_FILEFORMAT).
Return enum FFICO_FILEFMT. */
#define ffico_fileformat(c)  ((c)->filefmt)

/** Get size of bmp header (FFICO_FILEFORMAT, FFICO_BMP). */
#define ffico_bmphdr_size(c)  ((c)->bmphdr_size)
