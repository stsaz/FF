/** JSON.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/parse.h>


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

/** Initialize parser. */
FF_EXTN void ffjson_parseinit(ffparser *p);

/** Reuse the parser. */
FF_EXTN void ffjson_parsereset(ffparser *p);

/** Unescape text.
Return the number of characters written.
Return 0 if incomplete sequence.
Return -1 on error. */
FF_EXTN int ffjson_unescape(char *dst, size_t cap, const char *text, size_t len);

/** Escape characters: " \\ \b \f \r \n \t.
Return the number of characters written.
Return 0 if there is not enough space in destination buffer. */
FF_EXTN size_t ffjson_escape(char *dst, size_t cap, const char *s, size_t len);

/** Parse JSON.
Return FFPARS_E.  p->type is set to one of FFJSON_T. */
int ffjson_parse(ffparser *p, const char *data, size_t *len);

/** Parse the whole data.
Return enum FFPARS_E. */
FF_EXTN int ffjson_validate(ffparser *json, const char *data, size_t len);


/** Callback function for a parser with a scheme.
Check types.
Handle "null" type. */
FF_EXTN int ffjson_schemval(ffparser_schem *ps, void *obj, void *dst);

/** Initialize parser and scheme. */
static FFINL void ffjson_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_arg *top) {
	ffjson_parseinit(p);
	ffpars_scheminit(ps, p, top);
	ps->valfunc = &ffjson_schemval;
}


typedef struct {
	ffstr3 buf;
	int st;
	ffarr ctxs;
} ffjson_cook;

/** Prepare ffjson_cook structure. */
FF_EXTN void ffjson_cookinit(ffjson_cook *c, char *buf, size_t cap);

enum FFJSON_F {
	FFJSON_NOESC = 1 << 31 ///< don't escape string
	, FFJSON_PRETTY = 1 << 30 ///< pretty print using tabs
	, FFJSON_PRETTY2SPC = 1 << 29
	, FFJSON_PRETTY4SPC = 1 << 28
	, _FFJSON_PRETTYMASK = FFJSON_PRETTY | FFJSON_PRETTY2SPC | FFJSON_PRETTY4SPC
	, FFJSON_SZ = 1 << 27 ///< null terminated string
	, FFJSON_32BIT = 1 << 26 ///< 32-bit integer
};

enum FFJSON_E {
	FFJSON_OK
	, FFJSON_BUFFULL
	, FFJSON_ERR
};

/** Serialize one entity.
f: enum FFJSON_T [| enum FFJSON_F]
src: char*, ffstr*, int64*, int*, NULL
Return enum FFJSON_E. */
FF_EXTN int ffjson_cookadd(ffjson_cook *c, int f, const void *src);

/** Serialize one entity into a growing buffer. */
FF_EXTN int ffjson_cookaddbuf(ffjson_cook *c, int f, const void *src);
