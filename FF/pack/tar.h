/**
Copyright (c) 2016 Simon Zolin
*/

/*
(HDR PADDING DATA PADDING)...  2_EMPTY_BLOCKS
*/

#include <FF/array.h>
#include <FFOS/time.h>


typedef struct fftar_file {
	const char *name;
	uint mode;
	uint uid;
	uint gid;
	uint64 size;
	fftime mtime;
	const char *uid_str;
	const char *gid_str;
} fftar_file;

enum FFTAR_E {
	FFTAR_ESYS = 1,

	FFTAR_EHDR,
	FFTAR_ETYPE,
	FFTAR_ECHKSUM,
	FFTAR_EPADDING,
	FFTAR_ENOTREADY,
	FFTAR_EBIG,
	FFTAR_EFNAME,
};

/** Parse header.
@filename: must be at least 101 bytes.
@buf: must be at least 512 bytes.
Return enum FFTAR_E on error. */
FF_EXTN int fftar_hdr_parse(fftar_file *f, char *filename, const char *buf);

/** Write header.
@buf: must be at least 512 bytes.
Return enum FFTAR_E on error. */
FF_EXTN int fftar_hdr_write(const fftar_file *f, char *buf);


typedef struct fftar {
	uint state;
	uint err;
	uint nxstate;
	fftar_file file;
	uint64 fsize;
	ffarr buf;
	ffarr name;

	ffstr in;
	ffstr out;
} fftar;

typedef struct fftar_cook {
	uint state;
	uint err;
	char *buf;
	uint64 fsize;

	ffstr in;
	ffstr out;

	uint fdone :1;
} fftar_cook;

enum FFTAR_R {
	FFTAR_ERR = -1,
	FFTAR_DATA,
	FFTAR_DONE,
	FFTAR_MORE,
	FFTAR_FILEHDR,
	FFTAR_FILEDONE,
};

FF_EXTN const char* fftar_errstr(void *t);


FF_EXTN void fftar_init(fftar *t);

FF_EXTN void fftar_close(fftar *t);

/** Get next file. */
FF_EXTN fftar_file* fftar_nextfile(fftar *t);

#define fftar_offset(t)  ((t)->inoff)

/**
Return enum FFZIP_R. */
FF_EXTN int fftar_read(fftar *t);


static FFINL void fftar_wclose(fftar_cook *t)
{
	ffmem_safefree(t->buf);
}

/** Prepare to add a new file. */
FF_EXTN int fftar_newfile(fftar_cook *t, const fftar_file *f);

/**
Return enum FFTAR_R. */
FF_EXTN int fftar_write(fftar_cook *t);

static FFINL void fftar_wfiledone(fftar_cook *t)
{
	t->fdone = 1;
}

FF_EXTN void fftar_wfinish(fftar_cook *t);