/** Configuration parser.
Copyright (c) 2013 Simon Zolin
*/

// Data format:
#if 0
# one-line comment
// one-line comment
/*
multi-line comment
*/

# A key or value MAY be enclosed in quotes
#  but MUST be enclosed in quotes if it contains a non-name characters (name regexp: "a-zA-Z_0-9")
#  or is empty ("")
# A key may have multiple values divided by whitespace.
# Whitespace around a key or value is trimmed,
#  but whitespace within quotes is preserved.
key value_1 "value-2"

# Contexts can be nested if enclosed in {}
# '{' MUST be on the same line
# '}' MUST be on a new line
key {
	key "value"
}

key "value" {
	key "value"
}

# Access a 2nd-level object via '.'
key1.key2 "value"
#endif

#pragma once

#include <FF/data/parse.h>


enum FFCONF_T {
	FFCONF_TOBJ
	, FFCONF_TKEY
	, FFCONF_TKEYCTX // "KEY1.key2 val"
	, FFCONF_TVAL
	, FFCONF_TVALNEXT //"key val1 VAL2..."
};

typedef struct ffconf {
	uint state, nextst;
	uint type; //enum FFCONF_T
	int ret; //enum FFPARS_E
	uint line;
	uint ch;
	char esc[8];

	ffstr val;
	ffstr3 buf;
	ffarr ctxs;
} ffconf;

/** Initialize parser. */
FF_EXTN void ffconf_parseinit(ffconf *p);

FF_EXTN void ffconf_parseclose(ffconf *p);

/** Get full error message. */
FF_EXTN const char* ffconf_errmsg(ffconf *p, int r, char *buf, size_t cap);

/** Parse config.
Return enum FFPARS_E. */
FF_EXTN int ffconf_parse(ffconf *p, const char *data, size_t *len);

static FFINL int ffconf_parsestr(ffconf *p, ffstr *data)
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
	FFCONF_ASIS = 0x0400, // don't escape, don't use quotes
	FFCONF_CRLF = 0x0800, // use CRLF
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
static inline size_t ffconf_writeln(ffconfw *c, const ffstr *data, uint flags)
{
	return ffconf_writestr(c, data, FFCONF_TKEY | FFCONF_ASIS | flags);
}

/**
intflags: enum FFS_FROMINT */
static FFINL size_t ffconf_writeint(ffconfw *c, int64 data, uint intflags, uint flags)
{
	char buf[64];
	uint n = ffs_fromint(data, buf, sizeof(buf), intflags);
	return ffconf_write(c, buf, n, flags);
}
static inline size_t ffconf_writebool(ffconfw *c, int val, uint flags)
{
	ffstr s;
	if (val)
		ffstr_setz(&s, "true");
	else
		ffstr_setz(&s, "false");
	return ffconf_write(c, s.ptr, s.len, flags);
}

#define ffconf_writefin(c) \
	ffconf_write(c, NULL, 0, FFCONF_FIN)

/** Get output data. */
#define ffconf_output(c, out)  ffstr_set2(out, &(c)->buf)

/** Clear output. */
#define ffconf_clear(c)  ((c)->buf.len = 0)


/** Initialize parser and scheme.
Return 0 on success. */
FF_EXTN int ffconf_scheminit(ffparser_schem *ps, ffconf *p, const ffpars_ctx *ctx);

/**
Return 0 on success. */
FF_EXTN int ffconf_schemfin(ffparser_schem *ps);

FF_EXTN int ffconf_schemrun(ffparser_schem *ps);


/** Copy data within context.  Useful for deferred processing. */
typedef struct ffconf_ctxcopy {
	ffconfw wr;
	uint level;
} ffconf_ctxcopy;

static inline void ffconf_ctxcopy_init(ffconf_ctxcopy *cc, ffparser_schem *ps)
{
	ffmem_tzero(cc);
	ffconf_winit(&cc->wr, NULL, 0);
	ps->ctxs.len--; // when FFPARS_OPEN handler is called, a place for new context is already created
}

static inline void ffconf_ctxcopy_destroy(ffconf_ctxcopy *cc)
{
	ffconf_wdestroy(&cc->wr);
}

/** Acquire data and reset writer's buffer.
Free with ffstr_free(). */
static inline ffstr ffconf_ctxcopy_acquire(ffconf_ctxcopy *cc)
{
	ffstr d;
	ffconf_output(&cc->wr, &d);
	ffarr_null(&cc->wr.buf);
	return d;
}

/** Copy data.
Return 0: data chunk was copied;  >0: finished;  <0: error. */
FF_EXTN int ffconf_ctx_copy(ffconf_ctxcopy *cc, const ffconf *conf);


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
