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

/** Parse JSON.
Return FFPARS_E.  p->type is set to one of FFJSON_T. */
int ffjson_parse(ffparser *p, const char *data, size_t *len);

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
