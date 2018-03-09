/** Command-line arguments.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/data/parse.h>


#ifdef FF_WIN

typedef struct ffpsarg {
	ffarr cmdln;
} ffpsarg;

FF_EXTN void ffpsarg_init(ffpsarg *a, const char **argv, uint argc);

/** Get next argument from command line.
Return NULL if done. */
FF_EXTN const char* ffpsarg_next(ffpsarg *a);

static FFINL void ffpsarg_destroy(ffpsarg *a)
{
	a->cmdln.len = 0;
	ffarr_free(&a->cmdln);
}

#else //unix:

typedef struct ffpsarg {
	const char **arr;
	uint cnt;
} ffpsarg;

static FFINL void ffpsarg_init(ffpsarg *a, const char **argv, uint argc)
{
	a->arr = argv;
	a->cnt = argc;
}

static FFINL const char* ffpsarg_next(ffpsarg *a)
{
	if (a->cnt == 0)
		return NULL;
	a->cnt--;
	return *a->arr++;
}

#define ffpsarg_destroy(a)

#endif


enum FFPSARG_TYPE {
	FFPSARG_VAL = 1 //-s VAL | --key VAL
	, FFPSARG_KVAL //--key=VAL
	, FFPSARG_INPUTVAL //value without a key
	, FFPSARG_SHORT //-s
	, FFPSARG_LONG //--long
};

typedef struct ffpsarg_parser {
	uint state, nextst;
	uint type;
	int ret;
	uint line;
	uint ch;

	ffstr val;
	ffstr3 buf;
} ffpsarg_parser;

/** Initialize command-line arguments parser. */
FF_EXTN void ffpsarg_parseinit(ffpsarg_parser *p);

FF_EXTN void ffpsarg_parseclose(ffpsarg_parser *p);

/** Parse command-line arguments.
Short options: -a -b or -ab.  Note: -aVALUE is not supported.
Long options: --arg=val or --arg val.
'processed' is set to 1 if the current argument has been processed completely.
'p->line' is set to the number of arguments processed.
'p->type' is set to a value of enum FFPSARG_TYPE.
Return enum FFPARS_E. */
FF_EXTN int ffpsarg_parse(ffpsarg_parser *p, const char *a, int *processed);

/** Initialize command-line arguments parser and scheme.
Return 0 on success. */
FF_EXTN int ffpsarg_scheminit(ffparser_schem *ps, ffpsarg_parser *p, const ffpars_ctx *ctx);

/**
Return 0 on success. */
FF_EXTN int ffpsarg_schemfin(ffparser_schem *ps);

FF_EXTN int ffpsarg_schemrun(ffparser_schem *ps);
