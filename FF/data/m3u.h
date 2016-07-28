/** M3U playlist.
Copyright (c) 2015 Simon Zolin
*/

/*
#EXTM3U
#EXTINF:DURSEC,ARTIST - TITLE
\path\filename
*/

#pragma once

#include <FF/data/parse.h>


enum FFM3U_T {
	FFM3U_URL = 1,
	FFM3U_DUR,
	FFM3U_ARTIST,
	FFM3U_TITLE,
};

FF_EXTN void ffm3u_init(ffparser *p);

/**
Return enum FFM3U_T;  <0 on error (enum FFPARS_E). */
FF_EXTN int ffm3u_parse(ffparser *p, const char *data, size_t *len);


enum FFM3U_OPT {
	FFM3U_CRLF,
	FFM3U_LF = 1,
};

typedef struct ffm3u_cook {
	uint state;
	ffarr buf;
	ffstr crlf;
	uint options;
} ffm3u_cook;

/** Add entry to playlist.
@type: enum FFM3U_T. */
FF_EXTN int ffm3u_add(ffm3u_cook *m, uint type, const char *val, size_t len);

static FFINL void ffm3u_fin(ffm3u_cook *m)
{
	ffarr_free(&m->buf);
}
