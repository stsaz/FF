/** Vorbis comment.
Copyright (c) 2015 Simon Zolin
*/

/*
(LENGTH VENDOR) ENTRIES_COUNT [LENGTH KEY=VALUE]...
*/

#pragma once

#include <FF/array.h>


enum FFVORBTAG {
	FFVORBTAG_ALBUM,
	_FFVORBTAG_ALBUMARTIST,
	FFVORBTAG_ALBUMARTIST,
	FFVORBTAG_ARTIST,
	FFVORBTAG_COMMENT,
	FFVORBTAG_COMPOSER,
	FFVORBTAG_DATE,
	FFVORBTAG_GENRE,
	FFVORBTAG_LYRICS,
	FFVORBTAG_TITLE,
	_FFVORBTAG_TOTALTRACKS,
	FFVORBTAG_TRACKNO,
	FFVORBTAG_TRACKTOTAL,

	FFVORBTAG_VENDOR, // ffvorbtag_parse() uses it to return vendor string
};

/** enum FFVORBTAG as a string. */
FF_EXTN const char *const ffvorbtag_str[];

typedef struct ffvorbtag {
	uint state;
	uint cnt;
	int tag; // enum FFVORBTAG or -1
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

/**
Return enum FFVORBTAG;  -1 if unknown. */
FF_EXTN int ffvorbtag_find(const char *name, size_t len);


typedef struct ffvorbtag_cook {
	uint cnt;
	uint outlen;
	char *out;
	size_t outcap;
} ffvorbtag_cook;

/** Add an entry.  The first one must be vendor string.
Return enum FFVORBTAG_R. */
FF_EXTN int ffvorbtag_add(ffvorbtag_cook *v, const char *name, const char *val, size_t vallen);

/** @tag: enum FFVORBTAG */
#define ffvorbtag_iadd(v, tag, val, vallen) \
	ffvorbtag_add(v, ffvorbtag_str[tag], val, vallen)

/** Set the total number of entries. */
#define ffvorbtag_fin(v)  ffvorbtag_add(v, NULL, NULL, 0)
