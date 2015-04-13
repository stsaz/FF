/** Configuration parser.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/parse.h>


enum FFCONF_T {
	FFCONF_TOBJ
	, FFCONF_TKEY
	, FFCONF_TVAL
	, FFCONF_TVALNEXT //"key val1 VAL2..."
};

/** Initialize parser.
Return 0 on success. */
FF_EXTN int ffconf_parseinit(ffparser *p);

/** Parse config.
Return enum FFPARS_E. */
FF_EXTN int ffconf_parse(ffparser *p, const char *data, size_t *len);

/** Initialize parser and scheme.
Return 0 on success. */
FF_EXTN int ffconf_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx);

/**
Return 0 on success. */
FF_EXTN int ffconf_schemfin(ffparser_schem *ps);

/** Value handler callback. */
FF_EXTN int ffconf_schemval(ffparser_schem *ps, void *obj, void *dst);
