/** File mapping.
Copyright (c) 2014 Simon Zolin
*/

#pragma once

#include <FF/string.h>


/** File mapping. */
typedef struct fffilemap {
	fffd fd;
	uint64 foff;
	uint64 fsize;

	size_t blocksize;
	char *map;
	size_t mapsz;
	uint64 mapoff;
	fffd hmap;
} fffilemap;

static FFINL void fffile_mapinit(fffilemap *fm) {
	ffmem_tzero(fm);
	fm->fd = FF_BADFD;
}

/** Close file mapping. */
FF_EXTN void fffile_mapclose(fffilemap *fm);

/** Set properties.
@blocksize: size of a single mapped block.  Must be a multiple of the system page size. */
static FFINL void fffile_mapset(fffilemap *fm, size_t blocksize, fffd fd, uint64 foff, uint64 fsize) {
	fm->blocksize = blocksize;
	fm->fd = fd;
	fm->foff = foff;
	fm->fsize = fsize;
}

/** Get the currently mapped block.
Return 0 on success. */
FF_EXTN int fffile_mapbuf(fffilemap *fm, ffstr *dst);

/** Shift offset in a file mapping.
Note: @by < 0 is not supported.
Return 0 if there is no more data. */
FF_EXTN int fffile_mapshift(fffilemap *fm, int64 by);
