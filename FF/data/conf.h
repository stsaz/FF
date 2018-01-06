/** Configuration parser.
Copyright (c) 2013 Simon Zolin
*/

/*
# one-line comment
// one-line comment
/ * multi-line comment * /

key value1 "value2"

ctx "ctxname" {
	key "value"
}

key1.key2 "value"
*/

#pragma once

#include <FF/data/parse.h>


enum FFCONF_T {
	FFCONF_TOBJ
	, FFCONF_TKEY
	, FFCONF_TKEYCTX // "KEY1.key2 val"
	, FFCONF_TVAL
	, FFCONF_TVALNEXT //"key val1 VAL2..."
};

/** Initialize parser.
Return 0 on success. */
FF_EXTN int ffconf_parseinit(ffparser *p);

/** Parse config.
Return enum FFPARS_E.
 @p->type: enum FFCONF_T. */
FF_EXTN int ffconf_parse(ffparser *p, const char *data, size_t *len);

static FFINL int ffconf_parsestr(ffparser *p, ffstr *data)
{
	size_t n = data->len;
	int r = ffconf_parse(p, data->ptr, &n);
	ffstr_shift(data, n);
	return r;
}


typedef struct ffconfw {
	ffarr buf;
} ffconfw;

enum FFCONF_WRITE {
	FFCONF_WRITE_FIN = ~0U,
};

/** Add one element.
flags: enum FFCONF_T or enum FFCONF_WRITE. */
FF_EXTN int ffconf_write(ffconfw *c, const char *data, size_t len, uint flags);

FF_EXTN void ffconf_wdestroy(ffconfw *c);


/** Initialize parser and scheme.
Return 0 on success. */
FF_EXTN int ffconf_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx);

/**
Return 0 on success. */
FF_EXTN int ffconf_schemfin(ffparser_schem *ps);

FF_EXTN int ffconf_schemrun(ffparser_schem *ps);


struct ffconf_loadfile {
	const char *fn;
	void *obj;
	const ffpars_arg *args;
	uint nargs;
	uint bufsize;
	char errstr[256];
};

/** Parse data from file using scheme.
Return 0 on success or enum FFPARS_E. */
FF_EXTN int ffconf_loadfile(struct ffconf_loadfile *c);
