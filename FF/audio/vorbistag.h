/** Vorbis comment.
Copyright (c) 2015 Simon Zolin
*/

/*
(LENGTH VENDOR) ENTRIES_COUNT [LENGTH KEY=VALUE]...
*/

#pragma once

#include <FF/array.h>


typedef struct ffvorbtag {
	uint state;
	uint cnt;
	int tag; //enum FFMMTAG
	ffstr name
		, val;

	const char *data;
	size_t datalen;
} ffvorbtag;

enum FFVORBTAG_R {
	FFVORBTAG_OK,
	FFVORBTAG_DONE,
	FFVORBTAG_ERR = -1,
};

/** Get the next tag.
Note: partial input is not supported.
Return enum FFVORBTAG_R. */
FF_EXTN int ffvorbtag_parse(ffvorbtag *v);


typedef struct ffvorbtag_cook {
	ffarr out; //static or dynamic buffer
	uint cnt; //number of entries (including vendor)
	uint vendor_off; //offset of vendor length in 'out'
	uint nogrow :1; //don't allow ffvorbtag_add() to enlarge 'out'
} ffvorbtag_cook;

/** Add an entry.  The first one must be vendor string.
Return enum FFVORBTAG_R. */
FF_EXTN int ffvorbtag_add(ffvorbtag_cook *v, const char *name, const char *val, size_t vallen);

/** @tag: enum FFVORBTAG */
#define ffvorbtag_iadd(v, tag, val, vallen) \
	ffvorbtag_add(v, ffvorbtag_str[tag], val, vallen)

/** Set the total number of entries. */
FF_EXTN void ffvorbtag_fin(ffvorbtag_cook *v);

static FFINL void ffvorbtag_destroy(ffvorbtag_cook *v)
{
	if (!v->nogrow)
		ffarr_free(&v->out);
}
