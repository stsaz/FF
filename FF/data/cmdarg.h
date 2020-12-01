/** ff: command-line arguments parser
2020, Simon Zolin
*/

/*
ffcmdarg_errstr
ffcmdarg_init
ffcmdarg_fin
ffcmdarg_parse
*/

/* Format:
val0 -s val1 --long2 val3 --long=val2
*/

#pragma once

#include <FF/string.h>

typedef struct ffcmdarg {
	ffuint state;
	ffuint iarg;
	ffstr val;
	ffstr longval;

	const char **argv;
	ffuint argc;
} ffcmdarg;

enum FFCMDARG_R {
	FFCMDARG_RVAL = 1, // any stand-alone value
	FFCMDARG_RKEYSHORT, // -s
	FFCMDARG_RKEYLONG, // --long
	FFCMDARG_RKEYVAL, // --long=VAL
};

enum FFCMDARG_E {
	FFCMDARG_DONE,
	FFCMDARG_ERROR,
	FFCMDARG_ESHORT,
	FFCMDARG_ENOVAL,
	FFCMDARG_ESCHEME,
	FFCMDARG_FIN,
};

/** Get error string from code (<0) */
static inline const char* ffcmdarg_errstr(int err)
{
	if (err >= 0)
		return "";

	static const char* const cmdarg_err[] = {
		"FFCMDARG_DONE",
		"FFCMDARG_ERROR",
		"FFCMDARG_ESHORT",
		"FFCMDARG_ENOVAL",
		"FFCMDARG_ESCHEME",
		"FFCMDARG_FIN",
	};
	return cmdarg_err[-err];
}

/** Initialize reader, skipping the first argument (program name) */
static inline void ffcmdarg_init(ffcmdarg *p, const char **argv, ffuint argc)
{
	ffmem_zero_obj(p);
	p->argv = argv + 1;
	p->argc = argc - 1;
}

static inline int ffcmdarg_fin(ffcmdarg *p)
{
	if (p->state != 0)
		return -FFCMDARG_ENOVAL;
	return 0;
}

/** Get next argument
Return enum FFCMDARG_R
  <0: enum FFCMDARG_E */
static inline int ffcmdarg_parse(ffcmdarg *p, ffstr *dst)
{
	enum { I_KV = 0, I_VAL, };
	ffstr s;
	switch (p->state) {
	case I_KV:
		if (p->iarg >= p->argc)
			return FFCMDARG_DONE;

		ffstr_setz(&s, p->argv[p->iarg]);

		if (s.ptr[0] == '-') {

			if (s.ptr[1] == '-') {
				ffssize pos = ffstr_splitby(&s, '=', &s, &p->longval);
				if (pos >= 0)
					p->state = I_VAL;
				else
					p->iarg++;
				ffstr_set(&p->val, &s.ptr[2], s.len - 2);
				*dst = p->val;
				return FFCMDARG_RKEYLONG;
			}

			if (s.len != 2)
				return -FFCMDARG_ESHORT;

			p->iarg++;
			ffstr_set(&p->val, &s.ptr[1], 1);
			*dst = p->val;
			return FFCMDARG_RKEYSHORT;
		}

		p->iarg++;
		p->val = s;
		*dst = p->val;
		return FFCMDARG_RVAL;

	case I_VAL:
		p->state = I_KV;
		p->iarg++;
		p->val = p->longval;
		ffstr_null(&p->longval);
		*dst = p->val;
		return FFCMDARG_RKEYVAL;
	}

	return -FFCMDARG_ERROR;
}
