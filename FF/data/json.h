/** JSON.
Copyright (c) 2013 Simon Zolin
*/

#if 0
JSON syntax:

(null | boolean | number | "string" | object{} | array[])

boolean: true | false
number: 0 | 0.0 | 0.0e-5
string: UTF-8 text with \-escaped characters.
 Characters must be escaped: [0..0x19] 0x7f "" \\
 Characters may be escaped: /
 Any UTF-16 character may be escaped with "\uXXXX".
object: { "key": value, ... }
array: [ value, ... ]

This implementation supports comments, not defined in the standard:
// one-line
/*
multi-line comment
*/
#endif

#pragma once

#include <FF/data/parse.h>


enum FFJSON_T {
	FFJSON_TNULL
	, FFJSON_TSTR
	, FFJSON_TINT
	, FFJSON_TBOOL
	, FFJSON_TOBJ
	, FFJSON_TARR
	, FFJSON_TNUM
};

/** Get type name. */
FF_EXTN const char * ffjson_stype(int type);

typedef struct ffjson {
	uint state, nextst;
	uint type;
	int ret;
	uint line;
	uint ch;
	uint flags;
	union {
		int64 intval;
		double fltval;
		struct {
			char esc[5];
			uint esc_len;
			uint esc_hiword;
		};
		uint bareval_idx;
	};

	ffstr val;
	ffstr3 buf;
	ffarr ctxs;
} ffjson;

/** Initialize parser. */
FF_EXTN void ffjson_parseinit(ffjson *p);

FF_EXTN void ffjson_parseclose(ffjson *p);

/** Reuse the parser. */
FF_EXTN void ffjson_parsereset(ffjson *p);

/** Get full error message. */
FF_EXTN const char* ffjson_errmsg(ffjson *p, int r, char *buf, size_t cap);

/** Unescape 1 character from JSON text.
@text: escape sequence without the leading backslash.
Return the number of characters read.
Return 0 if incomplete sequence.
Return -1 on error. */
FF_EXTN int ffjson_unescapechar(uint *dst, const char *text, size_t len);

/** Escape characters: " \\ \b \f \r \n \t.
Return the number of characters written.
Return 0 if there is not enough space in destination buffer. */
FF_EXTN size_t ffjson_escape(char *dst, size_t cap, const char *s, size_t len);

/** Parse JSON.
Return FFPARS_E.  p->type is set to one of FFJSON_T. */
FF_EXTN int ffjson_parse(ffjson *p, const char *data, size_t *len);

static FFINL int ffjson_parsestr(ffjson *p, ffstr *data)
{
	size_t n = data->len;
	int r = ffjson_parse(p, data->ptr, &n);
	ffstr_shift(data, n);
	return r;
}

/** Parse the whole data.
Return enum FFPARS_E. */
FF_EXTN int ffjson_validate(ffjson *json, const char *data, size_t len);


/** Initialize parser and scheme. */
static FFINL void ffjson_scheminit(ffparser_schem *ps, ffjson *p, const ffpars_arg *top) {
	ffjson_parseinit(p);
	ffpars_scheminit(ps, p, top);
}

static FFINL void ffjson_scheminit2(ffparser_schem *ps, ffjson *p, const ffpars_arg *top, void *obj) {
	ffjson_parseinit(p);
	ffpars_scheminit(ps, p, top);
	ps->udata = obj;
	ps->flags |= _FFPARS_SCOBJ;
}

FF_EXTN int ffjson_schemfin(ffparser_schem *ps);

FF_EXTN int ffjson_schemrun(ffparser_schem *ps);


typedef struct ffjson_cook {
	ffstr3 buf;
	int st;
	ffarr ctxs;
	uint gflags; //enum FFJSON_F
} ffjson_cook;

/** Prepare ffjson_cook structure. */
FF_EXTN void ffjson_cookinit(ffjson_cook *c, char *buf, size_t cap);

static FFINL void ffjson_cookreset(ffjson_cook *c) {
	c->buf.len = 0;
	c->st = 0;
	c->ctxs.len = 0;
}

static FFINL void ffjson_cookfin(ffjson_cook *c) {
	ffarr_free(&c->ctxs);
}

static FFINL void ffjson_cookfinbuf(ffjson_cook *c) {
	ffarr_free(&c->buf);
	ffarr_free(&c->ctxs);
}

enum FFJSON_F {
	FFJSON_FNOESC = 1 << 31 ///< don't escape string
	, FFJSON_PRETTY = 1 << 30 ///< pretty print using tabs
	, FFJSON_PRETTY2SPC = 1 << 29
	, FFJSON_PRETTY4SPC = 1 << 28
	, _FFJSON_PRETTYMASK = FFJSON_PRETTY | FFJSON_PRETTY2SPC | FFJSON_PRETTY4SPC

	, FFJSON_FSTRZ = FFJSON_TSTR | (1 << 27) //null terminated string
	, FFJSON_FKEYNAME = FFJSON_FSTRZ
	, FFJSON_FINTVAL = FFJSON_TINT | (1 << 27) //integer value (int or int64)
	, FFJSON_F32BIT = 1 << 26 //32-bit integer
	, FFJSON_FMORE = 1 << 25 //TSTR: don't finalize the object because more data will follow
};

enum FFJSON_E {
	FFJSON_OK
	, FFJSON_BUFFULL
	, FFJSON_ERR
};

/* For use with FFJSON_TOBJ and FFJSON_TARR. */
#define FFJSON_CTXOPEN  NULL
#define FFJSON_CTXCLOSE  ((void*)1)

/** Serialize one entity.
f: enum FFJSON_T [| enum FFJSON_F]
src: char*, ffstr*, int64*, int*, NULL
If js->buf is empty, return the number of output bytes (negative value).
Return enum FFJSON_E. */
FF_EXTN int ffjson_add(ffjson_cook *js, int f, const void *src);

FF_EXTN int ffjson_addvv(ffjson_cook *js, const int *types, size_t ntypes, va_list va);

/** Add multiple items at once.
Put NULL at the end of the list. */
static FFINL int ffjson_addv(ffjson_cook *js, const int *types, size_t ntypes, ...) {
	int r;
	va_list va;
	va_start(va, ntypes);
	r = ffjson_addvv(js, types, ntypes, va);
	va_end(va);
	return r;
}

/** Serialize one entity into a growing buffer. */
FF_EXTN int ffjson_bufadd(ffjson_cook *js, int f, const void *src);

/** Add multiple items into a growing buffer. */
FF_EXTN int ffjson_bufaddv(ffjson_cook *js, const int *types, size_t ntypes, ...);
