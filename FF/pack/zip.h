/**
Copyright (c) 2016 Simon Zolin
*/

/*
(FILE_HDR DATA [DATA_DESC])... EXTRA_DATA CDIR_HDR... [CDIR64_EOF CDIR64_EOF_LOCATOR] CDIR_EOF
*/

#include <FF/array.h>
#include <FF/chain.h>
#include <FFOS/time.h>
#include <FFOS/file.h>

#include <zlib/zlib-ff.h>


enum FFZIP_E {
	FFZIP_ESYS = 1,
	FFZIP_ELZ,

	FFZIP_ENOTREADY,
	FFZIP_ELZINIT,
	FFZIP_EMAXITEMS,
	FFZIP_EFNAME,
	FFZIP_EDISK,
	FFZIP_ECDIR,
	FFZIP_EHDR,
	FFZIP_EFLAGS,
	FFZIP_ECOMP,
	FFZIP_EHDR_CDIR,
	FFZIP_ECRC,
	FFZIP_ECDIR_SIZE,
	FFZIP_EFSIZE,
	FFZIP_EMSG,
};

FF_EXTN const char* _ffzip_errstr(int err, z_ctx *lz);

typedef struct ffzip_fattr {
	ushort win; //enum FFWIN_FILEATTR
	ushort unixmode; //enum FFUNIX_FILEATTR
} ffzip_fattr;

static FFINL void ffzip_setsysattr(ffzip_fattr *a, uint sysattr)
{
	a->win = a->unixmode = 0;
#ifdef FF_WIN
	a->win = sysattr;
#else
	a->unixmode = sysattr;
#endif
}

static FFINL ffbool ffzip_isdir(const ffzip_fattr *a)
{
	return (a->win & FFWIN_FILE_DIR) || (a->unixmode & FFUNIX_FILE_DIR);
}


typedef struct ffzip_file {
	char *fn;
	uint64 osize;
	uint64 zsize;
	uint crc;
	fftime mtime;
	ffzip_fattr attrs;
	uint64 offset;
	byte comp;

	ffchain_item sib;
} ffzip_file;

typedef void (*ffzip_log)(void *udata, uint level, ffstr msg);

typedef struct ffzip {
	uint state;
	int err;
	const char *errmsg;
	uint nxstate;
	uint crc;
	uint hsize;
	ffarr buf;
	ffchain cdir; //ffzip_file[]
	ffchain_item *curfile;
	z_ctx *lz;
	uint64 outsize;
	uint64 inoff;
	uint64 cdir_off;
	uint64 cdir_end;

	ffzip_log log;
	void *udata;

	ffstr in;
	ffstr out;

	uint lzinit :1
		;
} ffzip;

static inline const char* ffzip_errstr(ffzip *z)
{
	if (z->err == FFZIP_EMSG)
		return z->errmsg;
	return _ffzip_errstr(z->err, z->lz);
}

typedef struct ffzip_cook {
	uint state;
	int err;
	ffarr buf;
	ffarr cdir;
	z_ctx *lz;
	uint crc;
	uint cdir_hdrlen;
	uint64 file_insize;
	uint64 file_outsize;
	uint64 total_in;
	uint64 total_out;
	uint items;

	ffstr in;
	ffstr out;

	uint filedone :1
		;
} ffzip_cook;

static inline const char* ffzip_werrstr(ffzip_cook *z)
{
	return _ffzip_errstr(z->err, z->lz);
}

enum FFZIP_R {
	FFZIP_ERR = -1,
	FFZIP_DATA,
	FFZIP_DONE,
	FFZIP_MORE,
	FFZIP_SEEK,
	FFZIP_FILEINFO,
	FFZIP_FILEHDR,
	FFZIP_FILEDONE,
};

FF_EXTN void ffzip_close(ffzip *z);

FF_EXTN void ffzip_init(ffzip *z, uint64 total_size);

FF_EXTN void ffzip_readfile(ffzip *z, uint64 off);

/** Get next file from CDIR.
Return NULL if no more files. */
FF_EXTN ffzip_file* ffzip_nextfile(ffzip *z);

#define ffzip_offset(z)  ((z)->inoff)

/**
Return enum FFZIP_R. */
FF_EXTN int ffzip_read(ffzip *z, char *dst, size_t cap);


FF_EXTN void ffzip_wclose(ffzip_cook *z);

/** Initialize deflate compression.
Return 0 on success. */
FF_EXTN int ffzip_winit(ffzip_cook *z, uint level, uint mem);

/** Prepare info for a new file. */
FF_EXTN int ffzip_wfile(ffzip_cook *z, const char *name, const fftime *mtime, const ffzip_fattr *attrs);

FF_EXTN int ffzip_write(ffzip_cook *z, char *dst, size_t cap);

#define ffzip_wfiledone(z)  ((z)->filedone = 1)

FF_EXTN void ffzip_wfinish(ffzip_cook *z);
