/** .pls playlist.
Copyright (c) 2016 Simon Zolin
*/

/*
[playlist]
FileN=URL
TitleN=TITLE
LengthN=LEN
*/

#pragma once

#include <FF/data/parse.h>
#include <FF/data/m3u.h>


enum FFPLS_T {
	FFPLS_URL = 1,
	FFPLS_TITLE,
	FFPLS_DUR,
	FFPLS_READY,

	FFPLS_FIN, //call ffpls_entry_get() with this value to get the last playlist entry
};

typedef struct ffpls {
	uint state, nextst;
	uint type;
	uint line;
	uint idx; //index of the current entry
	ffstr val;
	ffarr buf;
} ffpls;

FF_EXTN void ffpls_init(ffpls *p);

static FFINL void ffpls_close(ffpls *p)
{
	ffarr_free(&p->buf);
}

/** Parse data from .pls file.
Return enum FFPLS_T;  <0 on error (enum FFPARS_E). */
FF_EXTN int ffpls_parse(ffpls *p, ffstr *data);


/** Get next playlist entry.
@type: value returned by ffpls_parse()
Return 1 when an entry is ready. */
FF_EXTN int ffpls_entry_get(ffpls_entry *ent, uint type, const ffstr *val);
