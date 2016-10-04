/**
Copyright (c) 2016 Simon Zolin
*/

/*
(STM_HDR  [(BLK_HDR  DATA  [BLK_PADDING]  [CHECK])...]  IDX  STM_FTR)...
*/

#include <FF/array.h>
#include <FFOS/error.h>

#include <lzma/lzma-ff.h>


enum FFXZ_E {
	FFXZ_ESYS = 1,
	FFXZ_ELZMA,

	FFXZ_EHDR,
	FFXZ_EHDRFLAGS,
	FFXZ_EHDRCRC,

	FFXZ_EBLKHDR,
	FFXZ_EBLKHDRFLAGS,
	FFXZ_EBLKHDRCRC,
	FFXZ_EFILT,

	FFXZ_EIDX,
	FFXZ_EIDXCRC,
	FFXZ_EBIGIDX,

	FFXZ_EFTR,
	FFXZ_EFTRFLAGS,
	FFXZ_EFTRCRC,
};

FF_EXTN const char* ffxz_errstr(const void *xz);


typedef struct ffxz {
	uint state;
	int err;
	int lzma_err;
	uint nxstate;
	uint hsize;
	uint check_method;
	ffarr buf;
	uint64 inoff;
	uint64 osize;
	lzma_decoder *dec;
	uint64 idx_size;

	uint64 insize;
	uint64 outsize;

	ffstr in;
	ffstr out;
} ffxz;

enum FFXZ_R {
	FFXZ_ERR = -1,
	FFXZ_DATA,
	FFXZ_DONE,
	FFXZ_MORE,
	FFXZ_INFO,
	FFXZ_SEEK,
};

FF_EXTN void ffxz_close(ffxz *xz);

FF_EXTN void ffxz_init(ffxz *xz, int64 total_size);

#define ffxz_size(xz)  ((xz)->osize)

#define ffxz_offset(xz)  ((xz)->inoff)

FF_EXTN int ffxz_read(ffxz *xz, char *dst, size_t cap);
