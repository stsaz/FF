/** Parser generic.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/array.h>


enum FFPARS_E {
	FFPARS_MORE = 0

	, FFPARS_VAL = -1
	, FFPARS_KEY = -2
	, FFPARS_OPEN = -3
	, FFPARS_CLOSE = -4

	// all error codes are >0
	, FFPARS_ESYS = 1
	, FFPARS_EINTL
	, FFPARS_EBADCHAR
	, FFPARS_EKVSEP
	, FFPARS_ENOVAL
	, FFPARS_EBADVAL
	, FFPARS_EBIGVAL
	, FFPARS_EESC
	, FFPARS_EBADBRACE
	, FFPARS_ENOBRACE
	, FFPARS_EBADCMT

	// error codes for a parsing using scheme:
	, FFPARS_EUKNKEY
	, FFPARS_EDUPKEY
	, FFPARS_ENOREQ
	, FFPARS_EBADINT
	, FFPARS_EBADBOOL
	, FFPARS_EARRTYPE
	, FFPARS_EVALTYPE
	, FFPARS_EVALNULL
	, FFPARS_EVALEMPTY
	, FFPARS_EVALZERO
	, FFPARS_EVALNEG
	, FFPARS_EVALUNEXP
	, FFPARS_EVALUKN
	, FFPARS_EVALUNSUPP
	, FFPARS_ECONF

	, FFPARS_ELAST
};

#define ffpars_iserr(code)  ((code) > 0)

/** Get error message. */
FF_EXTN const char * ffpars_errstr(int code);


enum FFPARS_F {
	FFPARS_FTYPEMASK = 0x0f,

	/** Data pointer (not a structure's member offset, not a function pointer). */
	FFPARS_FPTR = 0x10,

	/** The argument must be specified.  Note: up to 63 arguments only. */
	FFPARS_FREQUIRED = 0x40,

	/** Allow multiple occurences.  Note: up to 63 arguments only. */
	FFPARS_FMULTI = 0x80,

//FFPARS_TSTR, FFPARS_TCHARPTR:
	/** Copy string value.
	Note: call ffmem_free() to free this memory.
	  If function pointer is used and it returns error, this memory is freed automatically. */
	FFPARS_FCOPY = 0x100,

	FFPARS_FNOTEMPTY = 0x200, // don't allow empty string
	FFPARS_FNONULL = 0x400, //don't allow escaped '\0'

	/** Prepare NULL-terminated string.  Must be used only with FFPARS_FCOPY. */
	FFPARS_FSTRZ = FFPARS_FNONULL | 0x800,

	/** Reallocate memory if target pointer is not NULL. */
	FFPARS_FRECOPY = FFPARS_FCOPY | 0x1000,

//FFPARS_TINT, FFPARS_TSIZE, FFPARS_TBOOL, FFPARS_TFLOAT:
	//FFPARS_F32BIT = 0,
	FFPARS_F64BIT = 0x100, // 64-bit number
	FFPARS_F16BIT = 0x200, // 16-bit number
	FFPARS_F8BIT = 0x300, // 8-bit number

//FFPARS_TINT, FFPARS_TSIZE, FFPARS_TFLOAT:
	FFPARS_FSIGN = 0x400, // Allow negative number
	FFPARS_FNOTZERO = 0x800, // Don't allow number zero

//FFPARS_TINT:
	/** Set/reset bit within 32/64-bit integer (see FFPARS_SETBIT()). */
	FFPARS_FBIT = 0x1000,
	FFPARS_FBITMASK = 0x2000,

//ffconf only:
	/** String value followed by object start, e.g. "key val {..."
	"val" is stored in ps->vals[0]. */
	FFPARS_FOBJ1 = 0x10000,

	/** Key may have more than 1 value: "key val1 val2..." */
	FFPARS_FLIST = 0x20000,

	/** "*" argument: call handler with keyname as a value. */
	//FFPARS_FNOKEY = 0,
	/** "*" argument: save keyname in ps->vals[0].  Don't call handler with keyname. */
	FFPARS_FWITHKEY = 0x40000,

//ffjson only:
	/** Allow null value, e.g. "key": null */
	FFPARS_FNULL = 0x10000,

//ffpsarg only:
	/** Key without value, e.g. "--help"
	If type is not TBOOL, ffpars_arg.dst must be a function pointer, which will receive NULL value. */
	FFPARS_FALONE = 0x10000,
};

enum FFPARS_T {
	FFPARS_TSTR = 1 // string (ffstr)
	, FFPARS_TCHARPTR // char*, must be used with FFPARS_FSTRZ
	, FFPARS_TINT // 8/16/32/64-bit integer
	, FFPARS_TFLOAT // 32/64-bit floating point number
	, FFPARS_TBOOL ///< byte integer, the possible values are 0 and 1.  Valid input: false|true
	, FFPARS_TOBJ ///< new context: sub-object
	, FFPARS_TARR ///< new context: sub-array
	, FFPARS_TENUM ///< byte index in the array of possible values of type string
	, FFPARS_TSIZE ///< integer with suffix k|m|g|t.  e.g. "1m" = 1 * 1024 * 1024

	/** Any type: the same function will be called for everything.
	Useful for mapping several array types to one function (JSON).
	To use the value you need to get the element type from parser backend (e.g. ffconf or ffjson).
	Maps to 'union ffpars_val.f_0()'. */
	, FFPARS_TANYTHING

	, FFPARS_TCLOSE ///< notification on closing the current context.  MUST be the last in list.

	, FFPARS_TINT64 = FFPARS_TINT | FFPARS_F64BIT
	, FFPARS_TINT16 = FFPARS_TINT | FFPARS_F16BIT
	, FFPARS_TINT8 = FFPARS_TINT | FFPARS_F8BIT
	, FFPARS_TFLOAT64 = FFPARS_TFLOAT | FFPARS_F64BIT
	, FFPARS_TBOOL8 = FFPARS_TBOOL | FFPARS_F8BIT
};

#define FFPARS_SETVAL(i)  ((i) << 24)
#define FFPARS_SETBIT(bit)  (FFPARS_FBIT | FFPARS_SETVAL(bit))
#define FFPARS_SETBITMASK(n)  (FFPARS_FBITMASK | FFPARS_SETVAL(n))

typedef struct ffpars_ctx ffpars_ctx;
typedef struct ffparser_schem ffparser_schem;
typedef struct ffpars_enumlist ffpars_enumlist;

union ffpars_val {
	size_t off; ///< offset of the member inside a structure (default)
	int (*f_0)(ffparser_schem *p, void *obj);
	int (*f_str)(ffparser_schem *p, void *obj, const ffstr *val);
	int (*f_charptr)(ffparser_schem *p, void *obj, const char *val);
	int (*f_int)(ffparser_schem *p, void *obj, const int64 *val);
	int (*f_flt)(ffparser_schem *p, void *obj, const double *val);
	int (*f_obj)(ffparser_schem *p, void *obj, ffpars_ctx *ctx);
	ffpars_enumlist *enum_list;

	// set with FFPARS_FPTR:
	ffstr *s;
	char **charptr;
	int64 *i64;
	int *i32;
	short *i16;
	byte *b;
	float *f32;
	double *f64;
	ffpars_ctx *ctx; ///< a new context for type object or array
};

#define FFPARS_DST(ptr) {(size_t)ptr}
#define FFPARS_DSTOFF(structType, member) {FFOFF(structType, member)}

struct ffpars_enumlist {
	const char *const *vals;
	uint nvals;
	union ffpars_val dst;
};

typedef struct ffpars_arg {
	const char *name;
	size_t flags; // EXT_DATA FLAGS TYPE
	union ffpars_val dst;
} ffpars_arg;

/** Return TRUE if a handler procedure is defined for this argument. */
static FFINL ffbool ffpars_arg_isfunc(const ffpars_arg *a)
{
	return ((a->flags & FFPARS_FTYPEMASK) != FFPARS_TENUM
		&& !(a->flags & FFPARS_FPTR))
		&& a->dst.off >= 64 * 1024;
}

/** Get target pointer. */
static FFINL void* ffpars_arg_ptr(const ffpars_arg *a, void *obj)
{
	return (a->flags & FFPARS_FPTR) ? a->dst.b : FF_PTR(obj, a->dst.off);
}

/** Get integer value from ffpars_arg meta and a value pointer. */
FF_EXTN int64 ffpars_getint(const ffpars_arg *a, union ffpars_val u);

/** Call handler function or set target value.
@ps: will be passed to handler function (optional)
Return 0 or enum FFPARS_E. */
FF_EXTN int ffpars_arg_process(const ffpars_arg *a, const ffstr *val, void *obj, void *ps);

/**
@val: int64* | double* */
FF_EXTN int _ffpars_arg_process2(const ffpars_arg *a, const void *val, void *obj, void *ps);


struct ffpars_ctx {
	void *obj;
	const ffpars_arg *args;
	uint nargs;
	const char * (*errfunc)(int ercod);
	uint used[2];
};

/** Set object and argument list. */
static FFINL void ffpars_setargs(ffpars_ctx *ctx, void *o, const ffpars_arg *args, uint nargs) {
	ctx->obj = o;
	ctx->args = args;
	ctx->nargs = nargs;
}

enum FFPARS_CTX_FIND {
	FFPARS_CTX_FANY = 1, // resolve "*" special argument
	FFPARS_CTX_FDUP = 2, // return (void*)-1 if argument was already used
	FFPARS_CTX_FKEYICASE = 4, // case-insensitive names
};

/** Search for an argument in context.
@flags: enum FFPARS_CTX_FIND.
Return NULL if not found. */
FF_EXTN const ffpars_arg* ffpars_ctx_findarg(ffpars_ctx *ctx, const char *name, size_t len, uint flags);

FF_EXTN void ffpars_ctx_skip(ffpars_ctx *ctx);

enum FFPARS_SCHEMFLAG {
	// internal:
	FFPARS_SCHAVKEY = 1,
	_FFPARS_SCALONE = 2,
	_FFPARS_SCOBJ = 4, // on new context, get object pointer from "ps.udata", not from the current context

	FFPARS_KEYICASE = 0x100, // case-insensitive key names
};

struct ffparser_schem {
	void *p; // parser back-end
	size_t flags;
	void *udata; ///< user-defined data
	struct { FFARR(ffpars_ctx) } ctxs;
	const ffpars_arg *curarg;
	ffstr vals[1];
	uint list_idx; //for FFPARS_FLIST
};

/** Initialize parser with a scheme. */
FF_EXTN void ffpars_scheminit(ffparser_schem *ps, void *p, const ffpars_arg *top);

static FFINL void ffpars_schemfree(ffparser_schem *ps) {
	ffarr_free(&ps->ctxs);
	ffstr_free(&ps->vals[0]);
}

FF_EXTN int _ffpars_schemrun_key(ffparser_schem *ps, ffpars_ctx *ctx, const ffstr *val);

/** Process the currently parsed entity according to the scheme.
Return enum FFPARS_E. */
FF_EXTN int _ffpars_schemrun(ffparser_schem *ps, int e);

FF_EXTN int _ffpars_skipctx(ffparser_schem *ps, int r);

/** Set the next context. */
FF_EXTN int ffpars_setctx(ffparser_schem *ps, void *o, const ffpars_arg *args, uint nargs);

/** Get error message. */
FF_EXTN const char * ffpars_schemerrstr(ffparser_schem *ps, int code, char *buf, size_t cap);

/** Get text data from internal representation using scheme.
@buf: char[64]
Return 0 or enum FFPARS_E. */
FF_EXTN int ffpars_scheme_write(char *buf, const ffpars_arg *arg, void *obj, ffstr *out);


typedef struct ffsvar {
	ffstr val;
} ffsvar;

enum FFSVAR {
	FFSVAR_TEXT,
	FFSVAR_S,
};

/** Process input string of the format "...text $var text...".
Return enum FFSVAR. */
FF_EXTN int ffsvar_parse(ffsvar *p, const char *data, size_t *plen);
