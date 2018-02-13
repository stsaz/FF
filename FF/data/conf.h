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
	uint state;
	uint level;
	uint flags; // global flags. enum FFCONF_WRITE
} ffconfw;

enum FFCONF_WRITE {
	//FFCONF_TOBJ
	//FFCONF_TKEY
	//FFCONF_TVAL
	FFCONF_TCOMMENTSHARP = 0x10, // "# text"
	FFCONF_FIN, // final new-line

	/** Indent lines within object context using tabs.
	key {
		text
	}
	*/
	FFCONF_PRETTY = 0x0100,
	FFCONF_GROW = 0x0200, // auto-grow ffconfw.buf
};

enum FFCONF_LEN {
	// FFCONF_TOBJ:
	FFCONF_OPEN = 0, // "key {"
	FFCONF_CLOSE = 1, // "}"

	// FFCONF_TVAL:
	FFCONF_STRZ = -1, // const char *data
	FFCONF_INT64 = -2, // int64 *data
};

static FFINL void ffconf_winit(ffconfw *c, char *buf, size_t cap)
{
	ffmem_tzero(c);
	if (cap != 0)
		ffarr_set3(&c->buf, buf, 0, cap);
	else
		c->flags = FFCONF_GROW;
}

FF_EXTN void ffconf_wdestroy(ffconfw *c);

/** Add one element.
@data: char* | int64*
@len: enum FFCONF_LEN
@flags: enum FFCONF_WRITE
Return # of written bytes;  0 on error. */
FF_EXTN size_t ffconf_write(ffconfw *c, const void *data, ssize_t len, uint flags);
static FFINL size_t ffconf_writestr(ffconfw *c, const ffstr *data, uint flags)
{
	return ffconf_write(c, data->ptr, data->len, flags);
}
static FFINL size_t ffconf_writez(ffconfw *c, const char *data, uint flags)
{
	return ffconf_write(c, data, FFCONF_STRZ, flags);
}
static FFINL size_t ffconf_writeint(ffconfw *c, int64 data, uint intflags, uint flags)
{
	char buf[64];
	uint n = ffs_fromint(data, buf, sizeof(buf), intflags);
	return ffconf_write(c, buf, n, flags);
}

/** Get output data. */
#define ffconf_output(c, out)  ffstr_set2(out, &(c)->buf)

/** Clear output. */
#define ffconf_clear(c)  ((c)->buf.len = 0)


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
