/** File writer with prebuffer.
Copyright (c) 2020 Simon Zolin
*/

#pragma once

#include <FF/string.h>
#include <FFOS/file.h>


typedef struct fffilewrite fffilewrite;
typedef void (*fffilewrite_log)(void *udata, uint level, ffstr msg);
typedef void (*fffileread_onwrite)(void *udata);

typedef struct {
	void *udata;
	fffilewrite_log log;
	// fffileread_onwrite onwrite;

	uint oflags; // additional flags for fffile_open()
	fffd kq;
	uint bufsize; // buffer size.  default:64k
	uint align; // buffer align.  default:4k
	uint64 prealloc; // preallocate-by value (or total file size if known in advance).  default:128k
	uint prealloc_grow :1; // increase 'prealloc' value x2 on each preallocation.  default:1
	uint create :1; // create file. default:1
	uint overwrite :1; // overwrite existing file
	uint mkpath :1; // create full path.  default:1
	uint del_on_err :1; // delete the file if writing is incomplete
	// uint directio :1; // use O_DIRECT (if available)
} fffilewrite_conf;

FF_EXTN void fffilewrite_setconf(fffilewrite_conf *conf);

FF_EXTN fffilewrite* fffilewrite_create(const char *fn, fffilewrite_conf *conf);

FF_EXTN void fffilewrite_free(fffilewrite *f);

enum FFFILEWRITE_R {
	FFFILEWRITE_RWRITTEN = 1, // some data was written
	FFFILEWRITE_RERR, // error occurred
	FFFILEWRITE_RASYNC, // asynchronous task is pending.  onwrite() will be called
};

enum FFFILEWRITE_F {
	FFFILEWRITE_FFLUSH = 1, // write bufferred data
};

/**
off: file offset;
 -1: write next chunk to the previous offset
flags: enum FFFILEWRITE_F
Return enum FFFILEWRITE_R */
FF_EXTN int fffilewrite_write(fffilewrite *f, ffstr data, int64 off, uint flags);

/** Get file descriptor. */
FF_EXTN fffd fffilewrite_fd(fffilewrite *f);

typedef struct {
	uint nmwrite; // N of memory writes
	uint nfwrite; // N of file writes
	uint nprealloc; // N of preallocations made
} fffilewrite_stat;

FF_EXTN void fffilewrite_getstat(fffilewrite *f, fffilewrite_stat *stat);
