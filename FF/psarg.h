/** Command-line arguments.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/parse.h>


enum FFPSARG_TYPE {
	FFPSARG_VAL = 1
	, FFPSARG_SHORT
	, FFPSARG_LONG
};

/** Initialize command-line arguments parser.
Return 0 on success. */
FF_EXTN int ffpsarg_parseinit(ffparser *p);

/** Parse command-line arguments.
Short options: -a -b or -ab.  Note: -aVALUE is not supported.
Long options: --arg=val or --arg val.
'processed' is set to 1 if the current argument has been processed completely.
'p->line' is set to the number of arguments processed.
'p->type' is set to a value of enum FFPSARG_TYPE.
Return enum FFPARS_E. */
FF_EXTN int ffpsarg_parse(ffparser *p, const char *a, int *processed);

/** Initialize command-line arguments parser and scheme.
Return 0 on success. */
FF_EXTN int ffpsarg_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx);

/**
Return 0 on success. */
FF_EXTN int ffpsarg_schemfin(ffparser_schem *ps);

/** Value handler callback.
Search a short option within context. */
FF_EXTN int ffpsarg_schemval(ffparser_schem *ps, void *obj, void *dst);
