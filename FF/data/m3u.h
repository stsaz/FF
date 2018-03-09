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

typedef struct ffm3u {
	uint state, nextst;
	uint line;
	int64 intval;
	ffstr val;
	ffarr buf;
	ffstr tmp;
} ffm3u;

FF_EXTN void ffm3u_init(ffm3u *m);

static FFINL void ffm3u_close(ffm3u *m)
{
	ffarr_free(&m->buf);
}

/**
Return enum FFM3U_T;  <0 on error (enum FFPARS_E). */
FF_EXTN int ffm3u_parse(ffm3u *m, ffstr *data);


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


typedef struct ffpls_entry {
	ffarr url;
	ffarr artist;
	ffarr title;
	int duration;
	uint clear :1;
} ffpls_entry;

static FFINL void ffpls_entry_free(ffpls_entry *ent)
{
	ffarr_free(&ent->url);
	ffarr_free(&ent->artist);
	ffarr_free(&ent->title);
	ent->duration = -1;
}

/** Get next playlist entry.
@type: value returned by ffm3u_parse()
Return 1 when an entry is ready. */
FF_EXTN int ffm3u_entry_get(ffpls_entry *ent, uint type, const ffstr *val);
