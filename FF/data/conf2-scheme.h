/** ff: configuration parser with scheme
2020, Simon Zolin
*/

/*
ffconf_scheme_addctx
ffconf_scheme_process
ffconf_parse_object
*/

#pragma once

#include <FF/data/conf2.h>

enum FFCONF_SCHEME_T {
	FFCONF_TSTR = 1,
	FFCONF_TINT,
	FFCONF_TBOOL, // 0 | 1 | true | false
	FFCONF_TOBJ,

	/** Key may have more than 1 value: "key val1 val2..." */
	FFCONF_FLIST = 0x10,
	FFCONF_F32BIT = 0x20,
	FFCONF_F16BIT = 0x40,
	FFCONF_FSIGN = 0x80,
	FFCONF_TINT32 = FFCONF_TINT | FFCONF_F32BIT,
	FFCONF_TINT16 = FFCONF_TINT | FFCONF_F16BIT,
};

/** Maps CONF key name to a C struct field offset or a handler function */
typedef struct ffconf_arg {
	/** Key name or "*" for an array element */
	const char *name;

	/**
	FFCONF_TSTR:
	 offset to ffstr or int handler(ffconf_scheme *cs, void *obj, ffstr *s)
	 If offset to ffstr is used, the string data is copied into the user's data field
	  and must be freed with ffstr_free().
	 Inside handler() user may use ffconf_strval_acquire() to acquire buffer from parser.

	FFCONF_TINT:
	 offset to ffint64|int|short or int handler(ffconf_scheme *cs, void *obj, ffint64 i)

	FFCONF_TBOOL:
	 offset to ffbyte or int handler(ffconf_scheme *cs, void *obj, ffint64 i)

	FFCONF_TOBJ:
	 int handler(ffconf_scheme *cs, void *obj)
	 User must call ffconf_scheme_addctx()
	*/
	ffuint flags;

	/** Offset to ffstr, ffint64, ffbyte or int handler(ffconf_scheme *cs, void *obj)
	Offset is converted to a real pointer like this:
	 ptr = current_ctx.obj + offset
	handler() returns 0 on success or enum FFCONF_E
	*/
	ffsize dst;
} ffconf_arg;

/** Find element by name */
static inline const ffconf_arg* _ffconf_arg_find(const ffconf_arg *args, const ffstr *name)
{
	for (ffuint i = 0;  args[i].name != NULL;  i++) {
		if (ffstr_eqz(name, args[i].name)) {
			return &args[i];
		}
	}
	return NULL;
}

/** Find element by name (Case-insensitive) */
static inline const ffconf_arg* _ffconf_arg_ifind(const ffconf_arg *args, const ffstr *name)
{
	for (ffuint i = 0;  args[i].name != NULL;  i++) {
		if (ffstr_ieqz(name, args[i].name)) {
			return &args[i];
		}
	}
	return NULL;
}


struct ffconf_schemectx {
	const ffconf_arg *args;
	void *obj;
};

typedef struct ffconf_scheme {
	ffconf *parser;
	ffuint flags; // enum FFCONF_SCHEME_F
	const ffconf_arg *arg;
	ffvec ctxs; // struct ffconf_schemectx[]
	const char *errmsg;
} ffconf_scheme;

static inline void ffconf_scheme_destroy(ffconf_scheme *cs)
{
	ffvec_free(&cs->ctxs);
}

enum FFCONF_SCHEME_F {
	/** Case-insensitive key names */
	FFCONF_SCF_ICASE = 1,
};

static inline void ffconf_scheme_addctx(ffconf_scheme *cs, const ffconf_arg *args, void *obj)
{
	struct ffconf_schemectx *c = ffvec_pushT(&cs->ctxs, struct ffconf_schemectx);
	c->args = args;
	c->obj = obj;
}

#define _FFCONF_ERR(c, msg) \
	(c)->errmsg = msg,  -FFCONF_ESCHEME

/** Process 1 element
Return 'r';
 <0 on error: enum FFCONF_E */
static inline int ffconf_scheme_process(ffconf_scheme *cs, int r)
{
	if (r <= 0)
		return r;

	const ffuint MAX_OFF = 64*1024;
	int r2;
	ffuint t = 0;
	struct ffconf_schemectx *ctx = ffslice_lastT(&cs->ctxs, struct ffconf_schemectx);
	union {
		ffstr *s;
		short *i16;
		int *i32;
		ffint64 *i64;
		ffbyte *b;
		int (*func)(ffconf_scheme *cs, void *obj);
		int (*func_str)(ffconf_scheme *cs, void *obj, ffstr *s);
		int (*func_int)(ffconf_scheme *cs, void *obj, ffint64 i);
	} u;
	u.b = NULL;

	if (cs->arg != NULL) {
		t = cs->arg->flags & 0x0f;

		u.b = (ffbyte*)cs->arg->dst;
		if (cs->arg->dst < MAX_OFF)
			u.b = (ffbyte*)FF_PTR(ctx->obj, cs->arg->dst);
	}

	switch (r) {
	case FFCONF_ROBJ_OPEN: {
		ffsize nctx = cs->ctxs.len;
		if (0 != (r2 = u.func(cs, ctx->obj)))
			return -r2; // user error
		if (nctx + 1 != cs->ctxs.len)
			return _FFCONF_ERR(cs, "object handler must add a new context");
		cs->arg = NULL;
		break;
	}

	case FFCONF_ROBJ_CLOSE:
		cs->ctxs.len--;
		cs->arg = NULL;
		break;

	case FFCONF_RKEY:
		if (cs->flags & FFCONF_SCF_ICASE)
			cs->arg = _ffconf_arg_ifind(ctx->args, &cs->parser->val);
		else
			cs->arg = _ffconf_arg_find(ctx->args, &cs->parser->val);
		if (cs->arg == NULL)
			return _FFCONF_ERR(cs, "no such key in the current context");
		break;

	case FFCONF_RVAL_NEXT:
		if (!(cs->arg->flags & FFCONF_FLIST))
			return _FFCONF_ERR(cs, "the key doesn't expect multiple values");
		// fallthrough

	case FFCONF_RVAL:
		switch (t) {
		case FFCONF_TSTR:
			if (cs->arg->dst < MAX_OFF) {
				ffstr_free(u.s);
				if (0 != ffconf_strval_acquire(cs->parser, u.s))
					return -FFCONF_ESYS;
			} else if (0 != (r2 = u.func_str(cs, ctx->obj, &cs->parser->val))) {
				return -r2; // user error
			}
			break;

		case FFCONF_TINT: {
			ffint64 i = 0;
			ffuint f = FFS_INT64;
			if (cs->arg->flags & FFCONF_F32BIT)
				f = FFS_INT32;
			else if (cs->arg->flags & FFCONF_F16BIT)
				f = FFS_INT16;

			if (cs->arg->flags & FFCONF_FSIGN)
				f |= FFS_INTSIGN;

			if (!ffstr_toint(&cs->parser->val, &i, f))
				return _FFCONF_ERR(cs, "integer expected");

			if (cs->arg->dst < MAX_OFF) {
				if (cs->arg->flags & FFCONF_F32BIT)
					*u.i32 = i;
				else if (cs->arg->flags & FFCONF_F16BIT)
					*u.i16 = i;
				else
					*u.i64 = i;
			} else if (0 != (r2 = u.func_int(cs, ctx->obj, i))) {
				return -r2; // user error
			}
			break;
		}

		case FFCONF_TBOOL: {
			int i;
			if (ffstr_ieqz(&cs->parser->val, "true")) {
				i = 1;
			} else if (ffstr_ieqz(&cs->parser->val, "false")) {
				i = 0;
			} else if (ffstr_to_uint32(&cs->parser->val, &i)
				&& (i == 0 || i == 1)) {
				//
			} else {
				return _FFCONF_ERR(cs, "boolean expected");
			}

			if (cs->arg->dst < MAX_OFF)
				*u.b = !!i;
			else if (0 != (r2 = u.func_int(cs, ctx->obj, i)))
				return -r2; // user error
			break;
		}
		}
		break;
	}

	return r;
}

/** Convert data into a C object
scheme_flags: enum FFCONF_SCHEME_F
errmsg: (optional) error message; must free with ffstr_free()
Return 0 on success
 <0 on error: enum FFCONF_E */
static inline int ffconf_parse_object(const ffconf_arg *args, void *obj, ffstr *data, ffuint scheme_flags, ffstr *errmsg)
{
	int r, r2;
	ffconf c = {};
	ffconf_init(&c);

	ffconf_scheme cs = {};
	cs.flags = scheme_flags;
	cs.parser = &c;
	ffconf_scheme_addctx(&cs, args, obj);

	for (;;) {
		r = ffconf_parse(&c, data);
		if (r < 0)
			goto end;
		else if (r == 0)
			break;

		r = ffconf_scheme_process(&cs, r);
		if (r < 0)
			goto end;
	}

end:
	ffconf_scheme_destroy(&cs);
	r2 = ffconf_fin(&c);
	if (r == 0)
		r = r2;

	if (r != 0 && errmsg != NULL) {
		ffsize cap = 0;
		const char *err = ffconf_errstr(r);
		if (r == -FFCONF_ESCHEME)
			err = cs.errmsg;
		ffstr_growfmt(errmsg, &cap, "%u:%u: %s"
			, (int)c.line, (int)c.linechar
			, err);
	}

	return r;
}

#undef _FFCONF_ERR
