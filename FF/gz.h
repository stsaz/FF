/** gzip.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FFOS/mem.h>

#include <zlib-ff.h>


typedef struct ffgzheader {
	byte id1 //0x1f
		, id2 //0x8b
		, comp_meth; //8=deflate
	union {
		byte flags;
		struct {
			byte ftext : 1
				, fhcrc : 1
				, fextra : 1
				, fname : 1 //flags=0x08
				, fcomment : 1
				, reserved : 3;
		};
	};
	uint mtime;
	byte exflags
		, fstype; //0=fat, 3=unix, 11=ntfs, 255=unknown
	// fextra=1: len_lo, len_hi, data[]
	// fname=1: stringz
	// fcomment=1: stringz
	// fhcrc=1: crc16
} ffgzheader;

// static const byte ffgz_defaulthdr[10] = { 0x1f, 0x8b, 8, 0, 0,0,0,0, 0, 255 };

typedef struct ffgztrailer {
	uint crc32 //CRC of uncompressed data
		, isize; //size of uncompressed data
} ffgztrailer;

typedef z_stream fflz;

enum FFLZ_R {
	FFLZ_DATA = Z_OK,
	FFLZ_MORE = Z_BUF_ERROR,
	FFLZ_DONE = Z_STREAM_END,
};

/** Get last error string. */
static FFINL const char * fflz_errstr(const fflz *gz)
{
	return (gz->msg != NULL ? gz->msg : "");
}

/** Get window bits number for fflz_deflateinit().
@window: 512-128k */
static FFINL int fflz_wbits(int window)
{
	int i = ffbit_ffs32(window) - 1;
	//window = 1 << (windowBits+2)
	if ((window & ~(1 << i)) || window < 512 || i > MAX_WBITS + 2)
		return -1;
	return i - 2;
}

/** Get memory level number for fflz_deflateinit().
@mem: 512-256k. */
static FFINL int fflz_memlevel(int mem)
{
	int i = ffbit_ffs32(mem) - 1;
	//mem = 1 << (memLevel+9)
	if ((mem & ~(1 << i)) || mem < 512 || i > MAX_MEM_LEVEL + 9)
		return -1;
	return i - 9;
}

static FFINL void * _ff_zalloc(void *opaque, uint items, uint size)
{
	size_t n = items * size;
	return ffmem_alloc(n);
}

static FFINL void _ff_zfree(void *opaque, void *address) {
	ffmem_free(address);
}

/** Initialize gzip compression.
@level: 1-9
@wbits: add 16 to auto-write gzip header and trailer.
  if negative: don't auto-write zlib header+trailer. */
static FFINL int fflz_deflateinit(fflz *gz, int level, int wbits, int memlev)
{
	gz->zalloc = &_ff_zalloc;
	gz->zfree = &_ff_zfree;
	return deflateInit2(gz, level, Z_DEFLATED, wbits, memlev, Z_DEFAULT_STRATEGY);
}

/** Initialize deflate compression with default parameters (level=6, memory=128k+128k). */
#define fflz_deflateinit1(z)  fflz_deflateinit(z, 6, -15, 8)

/** Finish gzip compression. */
#define fflz_deflatefin  deflateEnd

/** Set input. */
static FFINL void fflz_setin(fflz *gz, const void *in, size_t len)
{
	gz->next_in = (void*)in;
	gz->avail_in = FF_TOINT(len);
}

/** Set output. */
static FFINL void fflz_setout(fflz *gz, void *out, size_t cap)
{
	gz->next_out = out;
	gz->avail_out = FF_TOINT(cap);
}

/** Compress data.
Return enum FFLZ_R. */
static FFINL int fflz_deflate(fflz *gz, int flush, size_t *rd, size_t *wr)
{
	int r;
	size_t nin = gz->avail_in;
	size_t nout = gz->avail_out;

	r = deflate(gz, flush);
	if (rd != NULL)
		*rd = nin - gz->avail_in;
	if (wr != NULL)
		*wr = nout - gz->avail_out;
	return r;
}


static FFINL int fflz_inflateinit(fflz *z, int wbits)
{
	z->zalloc = &_ff_zalloc;
	z->zfree = &_ff_zfree;
	return inflateInit2(z, wbits);
}

#define fflz_inflatefin(z)  inflateEnd(z)

/** Decompress data.
Return enum FFLZ_R. */
static FFINL int fflz_inflate(fflz *z, int flush, size_t *rd, size_t *wr)
{
	int r;
	size_t nin = z->avail_in, nout = z->avail_out;

	r = inflate(z, flush);
	if (rd != NULL)
		*rd = nin - z->avail_in;
	if (wr != NULL)
		*wr = nout - z->avail_out;
	return r;
}
