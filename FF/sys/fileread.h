/** (Asynchronous) file reader with userspace cache and read-ahead options.
Copyright (c) 2019 Simon Zolin
*/

#pragma once

#include <FF/string.h>
#include <FFOS/file.h>
#include <FFOS/asyncio.h>


typedef struct fffileread fffileread;
typedef void (*fffileread_log)(void *udata, uint level, const ffstr *msg);
typedef void (*fffileread_onread)(void *udata);

typedef struct fffileread_conf {
	void *udata;
	fffileread_log log;
	fffileread_onread onread;

	fffd kq; // kqueue descriptor or -1 for synchronous reading
	uint oflags; // flags for fffile_open()

	uint bufsize; // size of 1 buffer.  Aligned to 'bufalign'.
	uint nbufs; // number of buffers
	uint bufalign; // buffer & file offset align value.  Power of 2.

	uint directio :1; // use direct I/O if available
} fffileread_conf;

/** Create reader.
Return object pointer.
 conf.directio is set according to how file was opened
*/
FF_EXTN fffileread* fffileread_create(const char *fn, fffileread_conf *conf);

/** Release object (it may be freed later after the async task is complete). */
FF_EXTN void fffileread_unref(fffileread *f);

FF_EXTN fffd fffileread_fd(fffileread *f);

enum FFFILEREAD_F {
	FFFILEREAD_FREADAHEAD = 1, // read-ahead: schedule reading of the next block
	FFFILEREAD_FBACKWARD = 2, // read-ahead: schedule reading of the previous block, not the next
};

enum FFFILEREAD_R {
	FFFILEREAD_RREAD, // returning data
	FFFILEREAD_RERR, // error occurred
	FFFILEREAD_RASYNC, // asynchronous task is pending.  onread() will be called
	FFFILEREAD_REOF, // end of file is reached
};

/** Get data block from cache or begin reading data from file.
flags: enum FFFILEREAD_F
Return enum FFFILEREAD_R. */
FF_EXTN int fffileread_getdata(fffileread *f, ffstr *dst, uint64 off, uint flags);

struct fffileread_stat {
	uint nread; // number of reads made
	uint nasync; // number of asynchronous requests
	uint ncached; // number of cache hits
};

FF_EXTN void fffileread_stat(fffileread *f, struct fffileread_stat *st);
