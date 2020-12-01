/** ff: command-line arguments parser with scheme
2020, Simon Zolin
*/

/*
ffcmdarg_scheme_init
ffcmdarg_scheme_process
ffcmdarg_parse_object
*/

#pragma once

#include <FF/data/cmdarg.h>

typedef struct ffcmdarg_arg {
	/** Short name of the argument, e.g. 's' matches "-s"
	0: argument has no short name */
	char short_name;

	/** Long name of the argument, e.g. "long-name" matches "--long-name"
	"": this entry matches any stand-alone value */
	const char *long_name;

	/** Flags
	FFCMDARG_TSTR:
	  int handler(ffjson_scheme *js, void *obj, ffstr *s)

	FFCMDARG_TINT:
	  int handler(ffjson_scheme *js, void *obj, ffint64 i)

	FFCMDARG_TSWITCH:
	  int handler(ffjson_scheme *js, void *obj)
	*/
	ffuint flags;

	/** int handler(ffcmdarg_scheme *as, void *obj)
	handler() returns 0 on success or enum FFCMDARG_E
	*/
	ffsize dst;
} ffcmdarg_arg;

typedef struct ffcmdarg_scheme {
	ffcmdarg *parser;
	ffuint flags; // enum FFCMDARG_SCHEME_F
	const ffcmdarg_arg *arg;
	const ffcmdarg_arg *args;
	void *obj;
	const char *errmsg;
} ffcmdarg_scheme;

enum FFCMDARG_SCHEME_T {
	FFCMDARG_TSWITCH,
	FFCMDARG_TSTR,
	FFCMDARG_TINT,
};

enum FFCMDARG_SCHEME_F {
	FFCMDARG_SCF_DUMMY,
};

/** Initialize parser object
args: array of arguments; terminated by NULL entry
scheme_flags: enum FFCMDARG_SCHEME_F */
static inline void ffcmdarg_scheme_init(ffcmdarg_scheme *as, const ffcmdarg_arg *args, void *obj, ffcmdarg *a, ffuint scheme_flags)
{
	ffmem_zero_obj(as);
	as->flags = scheme_flags;
	as->parser = a;
	as->args = args;
	as->obj = obj;
}

/** Find element by a long name */
static inline const ffcmdarg_arg* _ffcmdarg_arg_find(const ffcmdarg_arg *args, ffstr name)
{
	for (ffuint i = 0;  args[i].long_name != NULL;  i++) {
		if (ffstr_eqz(&name, args[i].long_name)) {
			return &args[i];
		}
	}
	return NULL;
}

/** Find element by a short name */
static inline const ffcmdarg_arg* _ffcmdarg_arg_find_short(const ffcmdarg_arg *args, char short_name)
{
	for (ffuint i = 0;  args[i].long_name != NULL;  i++) {
		if (short_name == args[i].short_name) {
			return &args[i];
		}
	}
	return NULL;
}

#define _FFCMDARG_ERR(a, msg) \
	(a)->errmsg = msg,  -FFCMDARG_ESCHEME

/** Process 1 argument
Return 'r';
  <0 on error: enum FFCMDARG_E */
static inline int ffcmdarg_scheme_process(ffcmdarg_scheme *as, int r)
{
	if (r < 0)
		return r;

	int r2;

	union {
		ffstr *s;
		ffint64 *i64;
		int (*func)(ffcmdarg_scheme *as, void *obj);
		int (*func_str)(ffcmdarg_scheme *as, void *obj, ffstr *s);
		int (*func_int)(ffcmdarg_scheme *as, void *obj, ffint64 i);
	} u;
	u.s = NULL;

	switch (r) {
	case FFCMDARG_RKEYSHORT:
		as->arg = _ffcmdarg_arg_find_short(as->args, as->parser->val.ptr[0]);
		// fallthrough

	case FFCMDARG_RKEYLONG:
		if (r == FFCMDARG_RKEYLONG)
			as->arg = _ffcmdarg_arg_find(as->args, as->parser->val);

		if (as->arg == NULL)
			return _FFCMDARG_ERR(as, "no such key in scheme");

		if (as->arg->flags == FFCMDARG_TSWITCH) {
			u.s = (ffstr*)as->arg->dst;
			r2 = u.func(as, as->obj);
			if (r2 != 0)
				return -r2; // user error
			as->arg = NULL;
		}
		break;

	case FFCMDARG_RVAL:
		if (as->arg == NULL
			&& as->args[0].long_name != NULL && as->args[0].long_name[0] == '\0')
			as->arg = &as->args[0];
		// fallthrough

	case FFCMDARG_RKEYVAL:
		if (as->arg == NULL)
			return _FFCMDARG_ERR(as, "unexpected value");

		u.s = (ffstr*)as->arg->dst;

		switch (as->arg->flags) {
		case FFCMDARG_TSTR:
			r2 = u.func_str(as, as->obj, &as->parser->val);
			if (r2 != 0)
				return -r2; // user error
			break;

		case FFCMDARG_TINT: {
			ffint64 i;
			if (!ffstr_to_int64(&as->parser->val, &i))
				return _FFCMDARG_ERR(as, "integer expected");
			r2 = u.func_int(as, as->obj, i);
			if (r2 != 0)
				return -r2; // user error
			break;
		}

		case FFCMDARG_TSWITCH:
			return _FFCMDARG_ERR(as, "value is specified but the argument is a switch");

		default:
			return _FFCMDARG_ERR(as, "invalid scheme data");
		}

		as->arg = NULL;
		break;

	case FFCMDARG_DONE:
		if (as->arg != NULL)
			return _FFCMDARG_ERR(as, "value expected");
		break;

	default:
		FF_ASSERT(0);
		return _FFCMDARG_ERR(as, "invalid psarg return value");
	}

	return r;
}

/** Convert command-line arguments to a C object
scheme_flags: enum FFCMDARG_SCHEME_F
errmsg: (optional) error message; must free with ffstr_free()
Return 0 on success
  <0 on error: enum FFCMDARG_E */
static inline int ffcmdarg_parse_object(const ffcmdarg_arg *args, void *obj, const char **argv, ffuint argc, ffuint scheme_flags, ffstr *errmsg)
{
	int r;
	ffcmdarg a;
	ffcmdarg_init(&a, argv, argc);

	ffcmdarg_scheme as;
	ffcmdarg_scheme_init(&as, args, obj, &a, scheme_flags);

	for (;;) {
		ffstr val;
		r = ffcmdarg_parse(&a, &val);
		if (r < 0)
			break;

		r = ffcmdarg_scheme_process(&as, r);
		if (r <= 0)
			break;
	}

	if (r != 0 && errmsg != NULL) {
		ffsize cap = 0;
		const char *err = ffcmdarg_errstr(r);
		if (r == -FFCMDARG_ESCHEME)
			err = as.errmsg;
		ffstr_growfmt(errmsg, &cap, "near '%S': %s"
			, &a.val, err);
	}

	return r;
}

#undef _FFCMDARG_ERR
