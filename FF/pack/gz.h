/** gzip.
Copyright (c) 2014 Simon Zolin
*/

/*
(HDR DATA TRL)...
*/

#pragma once

#include <FF/array.h>
#include <FF/number.h>

#include <zlib/zlib-ff.h>


typedef struct ffgzheader {
	byte id1 //0x1f
		, id2 //0x8b
		, comp_meth; //8=deflate
	byte flags; //enum GZ_FLAGS
	byte mtime[4];
	byte exflags
		, fstype; //0=fat, 3=unix, 11=ntfs, 255=unknown
	// fextra=1: len_lo, len_hi, data[]
	// fname=1: stringz
	// fcomment=1: stringz
	// fhcrc=1: crc16
} ffgzheader;

typedef struct ffgztrailer {
	byte crc32[4]; //CRC of uncompressed data
	byte isize[4]; //size of uncompressed data
} ffgztrailer;


enum FFGZ_E {
	FFGZ_ESYS = 1,
	FFGZ_ELZ,

	FFGZ_ENOTREADY,
	FFGZ_ELZINIT,
	FFGZ_EHDR,
	FFGZ_ESMALLSIZE,
	FFGZ_ESIZE,
	FFGZ_ECRC,
};

FF_EXTN const char* _ffgz_errstr(int err, z_ctx *lz);

#define ffgz_errstr(gz)  _ffgz_errstr((gz)->err, (gz)->lz)


typedef struct ffgz {
	uint state;
	int err;
	uint nxstate;
	z_ctx *lz;

	ffstr in;
	uint hsize;
	ffarr buf;
	uint nameoff;
	union {
	uint64 inoff;
	uint64 insize;
	};

	ffstr out;
	uint crc;
	uint64 outsize;
	uint trlcrc;
} ffgz;

typedef struct ffgz_cook {
	uint state;
	int err;
	z_ctx *lz;

	ffstr in;
	uint crc;
	uint64 insize;

	ffstr out;
	ffarr2 buf;
	uint64 outsize;

	uint flush;
} ffgz_cook;

enum FFGZ_R {
	FFGZ_WARN = -2,
	FFGZ_ERR = -1,
	FFGZ_DATA,
	FFGZ_DONE,
	FFGZ_MORE,
	FFGZ_INFO,
	FFGZ_SEEK,
};

FF_EXTN void ffgz_close(ffgz *gz);

/**
@total_size: -1: gzip file size is unknown (no seeking) */
FF_EXTN void ffgz_init(ffgz *gz, int64 total_size);

/**
Return enum FFGZ_R. */
FF_EXTN int ffgz_read(ffgz *gz, char *dst, size_t cap);

static FFINL const char* ffgz_fname(ffgz *gz)
{
	return (gz->nameoff != 0) ? gz->buf.ptr + gz->nameoff : NULL;
}

static FFINL uint ffgz_mtime(ffgz *gz)
{
	const ffgzheader *h = (void*)gz->buf.ptr;
	return ffint_ltoh32(h->mtime);
}

/** Get decompressed file size (32-bit).
While (or after) decompressing, this is the actual decompressed data size (64-bit). */
#define ffgz_size(gz)  ((gz)->outsize)

/** Get decompressed file size (64-bit).
Get lower-bound value in case .gz file is larger than 4gb.
 The upper-bound value can't be determined in this case. */
#define ffgz_size64(gz, gz_fsize)  (((gz_fsize) & 0xffffffff00000000ULL) | ((gz)->outsize))

#define ffgz_crc(gz)  ((gz)->trlcrc)

#define ffgz_offset(gz)  ((gz)->inoff)


FF_EXTN void ffgz_wclose(ffgz_cook *gz);

/** Initialize deflate compression.
Return 0 on success. */
FF_EXTN int ffgz_winit(ffgz_cook *gz, uint level, uint mem);

/**
@name: optional.
Return 0 on success. */
FF_EXTN int ffgz_wfile(ffgz_cook *gz, const char *name, uint mtime);

/**
Return enum FFGZ_R. */
FF_EXTN int ffgz_write(ffgz_cook *gz, char *dst, size_t cap);

#define ffgz_wfinish(gz)  ((gz)->flush = Z_FINISH)
#define ffgz_wflush(gz)  ((gz)->flush = Z_SYNC_FLUSH)
