/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/data/parse.h>

static const char *const _ffpars_serr[] = {
	""
	, "system"
	, "internal"
	, "invalid character"
	, "key-value separator"
	, "no value"
	, "bad value"
	, "too large value"
	, "invalid escape sequence"
	, "inconsistent brace"
	, "expected brace"
	, "invalid comment"

	, "unknown key name"
	, "duplicate key"
	, "unspecified required parameter"
	, "array value type mismatch"
	, "value type mismatch"
	, "null value"
	, "empty value"
	, "zero value"
	, "negative value"
	, "unexpected value"
	, "parser misuse"
};

const char * ffpars_errstr(int code)
{
	return _ffpars_serr[code];
}

const char ff_escchar[] = "\"\\bfrnt";
const char ff_escbyte[] = "\"\\\b\f\r\n\t";

void * ffpars_defmemfunc(void *d, size_t sz)
{
	if (sz != 0) {
		sz += sz / 4; // +25%
		return ffmem_realloc(d, sz);
	}
	ffmem_free(d);
	return NULL;
}

void ffpars_init(ffparser *p)
{
	ffpars_reset(p);
	p->line = 1;
	p->memfunc = &ffpars_defmemfunc;
	p->ctxs.ptr = p->buf.ptr = NULL;
	p->ctxs.cap = p->buf.cap = 0;
}

void ffpars_reset(ffparser *p)
{
	ffpars_cleardata(p);
	p->intval = 0;
	p->state = 0;
	p->type = 0;
	p->line = 1;
	p->ch = 0;
	p->ctxs.len = 0;
}

void ffpars_free(ffparser *p)
{
	if (p->buf.cap != 0)
		p->memfunc(p->buf.ptr, 0);
	ffarr_free(&p->ctxs);
}

int _ffpars_hdlCmt(int *st, int ch)
{
	switch (*st) {
	case FFPARS_ICMT_BEGIN:
		if (ch == '/')
			*st = FFPARS_ICMT_LINE;
		else if (ch == '*')
			*st = FFPARS_ICMT_MLINE;
		else
			return FFPARS_EBADCMT; // "//" or "/*" only
		break;

	case FFPARS_ICMT_LINE:
		if (ch == '\n')
			*st = FFPARS_IWSPACE; //end of line comment
		break;

	case FFPARS_ICMT_MLINE:
		if (ch == '*')
			*st = FFPARS_ICMT_MLINESLASH;
		break;

	case FFPARS_ICMT_MLINESLASH:
		if (ch == '/')
			*st = FFPARS_IWSPACE; //end of multiline comment
		else
			*st = FFPARS_ICMT_MLINE; //skipped '*' within multiline comment
		break;
	}
	return 0;
}

int _ffpars_copyBuf(ffparser *p, const char *text, size_t len)
{
	size_t all = len;
	if (p->val.ptr != p->buf.ptr)
		all = p->val.len + len;

	if (ffarr_unused(&p->buf) < all) {
		size_t cap = p->buf.len + all;
		void *d = p->memfunc(p->buf.ptr, cap);
		if (d == NULL)
			return FFPARS_ESYS;
		p->buf.ptr = d;
		p->buf.cap = cap;
	}

	if (all != len) {
		memcpy(ffarr_end(&p->buf), p->val.ptr, p->val.len); //the first allocation, copy what we've processed so far
		p->buf.len += p->val.len;
	}

	memcpy(ffarr_end(&p->buf), text, len);
	p->buf.len += len;
	ffstr_set(&p->val, p->buf.ptr, p->buf.len);
	return 0;
}

int ffpars_savedata(ffparser *p)
{
	int rc = 0;
	if (p->val.len != 0 && p->val.ptr != p->buf.ptr)
		rc = _ffpars_copyBuf(p, NULL, 0);
	return rc;
}


static int scHdlKey(ffparser_schem *ps, ffpars_ctx *ctx);
static int scHdlEnum(ffparser_schem *ps, void *obj);
static void scHdlBits(size_t f, int64 i, union ffpars_val dst);
static int scSetInt(ffparser_schem *ps, ffpars_ctx *ctx, union ffpars_val dst, union ffpars_val edst, int64 n);
static int scHdlVal(ffparser_schem *ps, ffpars_ctx *ctx);
static int scOpenBrace(ffparser_schem *ps);
static int scCloseBrace(ffparser_schem *ps);

static int _ffpars_schem_onval_def(ffparser_schem *ps, void *obj, void *dst)
{
	return FFPARS_OK;
}

void ffpars_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_arg *top)
{
	ffmem_tzero(ps);
	ps->p = p;
	ps->curarg = top;
	ps->onval = &_ffpars_schem_onval_def;
}

// search key name in the current context
static int scHdlKey(ffparser_schem *ps, ffpars_ctx *ctx)
{
	int er;
	uint i;

	ps->curarg = NULL;
	er = ps->onval(ps, ctx->obj, NULL);

	if (er > 0)
		return er;

	else if (er == FFPARS_OK) {
		int (*cmpz)(const char *s1, size_t len, const char *sz2);
		cmpz = (ps->flags & FFPARS_KEYICASE) ? &ffs_icmpz : &ffs_cmpz;

		for (i = 0;  i < ctx->nargs;  i++) {
			if (ctx->args[i].name != NULL
				&& 0 == cmpz(ps->p->val.ptr, ps->p->val.len, ctx->args[i].name)) {
				ps->curarg = &ctx->args[i];
				break;
			}
		}

	} else
		i = (uint)(ps->curarg - ctx->args);

	if (ps->curarg == NULL) {

		for (i = 0;  i < ctx->nargs;  i++) {
			const ffpars_arg *a = &ctx->args[i];
			if (a->name != NULL
				&& a->name[0] == '*'
				&& a->name[1] == '\0')
				break;
		}

		if (i == ctx->nargs) {
			return FFPARS_EUKNKEY;
		}

		ps->curarg = &ctx->args[i];
		return scHdlVal(ps, ctx);
	}

	if (i < sizeof(ctx->used)*8
		&& ffbit_setarr(ctx->used, i) && !(ps->curarg->flags & FFPARS_FMULTI))
		return FFPARS_EDUPKEY;

	if (ps->curarg->flags & FFPARS_FALONE) {
		ps->p->intval = 1;
		ps->p->ret = FFPARS_VAL;
		return scHdlVal(ps, ctx);

	} else {
		int t = ps->curarg->flags & FFPARS_FTYPEMASK;
		if (t != FFPARS_TOBJ && t != FFPARS_TARR)
			ps->flags |= FFPARS_SCHAVKEY;
	}

	return 0;
}

static int scHdlEnum(ffparser_schem *ps, void *obj)
{
	ffpars_enumlist *en = ps->curarg->dst.enum_list;
	uint i;

	for (i = 0;  i < en->nvals;  ++i) {
		if (ffstr_eqz(&ps->p->val, en->vals[i]))
			return i;
	}

	return -1;
}

static void scHdlBits(size_t f, int64 i, union ffpars_val dst)
{
	uint bit = (f & FFPARS_FBITMASK) >> 24;

	if (f & FFPARS_F64BIT) {
		if (i != 0)
			ffbit_set64((uint64*)dst.i64, bit);
		else
			ffbit_reset64((uint64*)dst.i64, bit);
	}
	//else if (f & FFPARS_F16BIT)
	//	ffbit_set32((uint*)dst.i16, bit);
	//else if (f & FFPARS_F8BIT)
	//	ffbit_set32((uint*)dst.b, bit);
	else {
		if (i != 0)
			ffbit_set32((uint*)dst.i32, bit);
		else
			ffbit_reset32((uint*)dst.i32, bit);
	}
}

static int scSetInt(ffparser_schem *ps, ffpars_ctx *ctx, union ffpars_val dst, union ffpars_val edst, int64 n)
{
	size_t f = ps->curarg->flags;

	if (!(f & FFPARS_F64BIT)) {
		int64 i = n;
		uint shift = 32;

		if (f & FFPARS_F16BIT)
			shift = 64-16;
		else if (f & FFPARS_F8BIT)
			shift = 64-8;

		if (i < 0)
			i = -i;
		if ((i >> shift) != 0)
			return FFPARS_EBIGVAL;
	}

	if (ffpars_arg_isfunc(ps->curarg))
		return edst.f_int(ps, ctx->obj, &n);

	else if (f & FFPARS_FBIT)
		scHdlBits(f, n, dst);

	else if (f & FFPARS_F64BIT)
		*dst.i64 = n;
	else if (f & FFPARS_F16BIT)
		*dst.i16 = (short)n;
	else if (f & FFPARS_F8BIT)
		*dst.b = (char)n;
	else
		*dst.i32 = (int)n;

	return 0;
}

static int scHdlVal(ffparser_schem *ps, ffpars_ctx *ctx)
{
	const ffpars_arg *curarg = ps->curarg;
	int t;
	size_t f;
	ffparser *p = ps->p;
	int er = 0;
	ffbool func = 0;
	union ffpars_val dst;
	union ffpars_val edst;
	ffstr tmp;

	f = curarg->flags;
	t = f & FFPARS_FTYPEMASK;
	edst = curarg->dst;

	if (t == FFPARS_TENUM)
		edst = curarg->dst.enum_list->dst;

	if (f & FFPARS_FPTR)
		dst.s = edst.s;
	else if (ffpars_arg_isfunc(curarg)) {
		func = 1;
		dst.s = NULL;
	}
	else
		dst.b = (byte*)ctx->obj + edst.off;

	switch (t) {
	case FFPARS_TSTR:
		if (p->val.len == 0 && (f & FFPARS_FNOTEMPTY))
			return FFPARS_EVALEMPTY;

		if ((f & FFPARS_FNONULL)
			&& NULL != ffs_findc(p->val.ptr, p->val.len, '\0'))
			return FFPARS_EBADCHAR;

		if (f & FFPARS_FCOPY) {

			if (f & FFPARS_FSTRZ) {
				if (NULL == ffstr_alloc(&tmp, p->val.len + 1))
					return FFPARS_ESYS;
				tmp.len = ffsz_fcopy(tmp.ptr, p->val.ptr, p->val.len) - tmp.ptr;

			} else if (p->val.len == 0)
				ffstr_null(&tmp);
			else if (NULL == ffstr_copy(&tmp, p->val.ptr, p->val.len))
				return FFPARS_ESYS;
		} else
			tmp = p->val;

		if (func) {
			er = edst.f_str(ps, ctx->obj, &tmp);
			if (er != 0 && (f & FFPARS_FCOPY))
				ffstr_free(&tmp);
		} else
			*dst.s = tmp;

		break;

	case FFPARS_TENUM:
		p->intval = scHdlEnum(ps, ctx->obj);
		if (p->intval == -1)
			return FFPARS_EBADVAL;
		er = scSetInt(ps, ctx, dst, edst, p->intval);
		break;

	case FFPARS_TSIZE:
		if (p->val.len == 0)
			return FFPARS_EVALEMPTY;
		{
			uint shift = ffchar_sizesfx(ffarr_back(&p->val));
			if (shift != 0)
				p->val.len--;
			if (p->val.len != ffs_toint(p->val.ptr, p->val.len, &p->intval, FFS_INT64))
				return FFPARS_EBADVAL;
			p->intval <<= shift;
		}
		//break;

	case FFPARS_TINT:
		if (p->intval == 0 && (f & FFPARS_FNOTZERO))
			return FFPARS_EVALZERO;

		if (p->intval < 0 && !(f & FFPARS_FSIGN))
			return FFPARS_EVALNEG;
		//break;

	case FFPARS_TBOOL:
		er = scSetInt(ps, ctx, dst, edst, p->intval);
		break;

	case FFPARS_TFLOAT:
		if (ffpars_arg_isfunc(ps->curarg))
			return FFPARS_ECONF; //unsupported

		if (!(f & FFPARS_FSIGN) && p->fltval < 0)
			return FFPARS_EVALNEG;

		if (!(f & (FFPARS_F64BIT | FFPARS_F16BIT | FFPARS_F8BIT)))
			*dst.f32 = (float)p->fltval;
		else if (f & FFPARS_F64BIT)
			*dst.f64 = p->fltval;
		else
			return FFPARS_ECONF; //invalid bits for a float
		break;

	default:
		er = FFPARS_EVALTYPE;
	}

	return er;
}

/* if the ctx is object: 'curarg' is already set by FFPARS_KEY handler
if FPTR flag: copy the ffpars_ctx structure from the pointer specified in scheme
if no FPTR flag: call object handler func */
static int scOpenBrace(ffparser_schem *ps)
{
	ffpars_ctx *ctx;
	ffpars_ctx *curctx;
	const ffpars_arg *curarg = ps->curarg;
	int er = 0;
	void *o;

	ctx = ffarr_push(&ps->ctxs, ffpars_ctx);
	if (ctx == NULL)
		return FFPARS_ESYS;
	memset(ctx, 0, sizeof(ffpars_ctx));

	curctx = &ps->ctxs.ptr[ps->ctxs.len - 2];

	if (curarg->flags & FFPARS_FPTR) {
		*ctx = *curarg->dst.ctx;
	}
	else {
		if (ps->ctxs.len < 2)
			return FFPARS_ECONF;
		o = curctx->obj;

		er = curarg->dst.f_obj(ps, o, ctx);
		if (er == 0) {
			ps->curarg = NULL;
			ps->vals[0].len = 0;
		} else
			ps->ctxs.len--;
	}

	return er;
}

/* Check whether all required parameters have been specified.
Call "on-close" callback function, if it exists. */
static int scCloseBrace(ffparser_schem *ps)
{
	uint i;
	ffpars_ctx *ctx;
	uint maxargs;

	if (ps->ctxs.len == 0)
		return FFPARS_ECONF;
	ctx = &ffarr_back(&ps->ctxs);

	maxargs = (uint)ffmin(ctx->nargs, sizeof(ctx->used)*8);
	for (i = 0;  i < maxargs;  i += 32) {
		uint n = ~ctx->used[i / 32];
		while (n != 0) {
			uint bit = ffbit_ffs(n) - 1;
			const ffpars_arg *arg = &ctx->args[i + bit];
			if (i + bit >= maxargs)
				break;

			if (arg->flags & FFPARS_FREQUIRED) {
				ps->curarg = arg;
				return FFPARS_ENOREQ;
			}

			ffbit_reset32(&n, bit);
		}
	}

	if (ctx->nargs != 0) {
		const ffpars_arg *a = &ctx->args[ctx->nargs - 1];
		if ((a->flags & FFPARS_FTYPEMASK) == FFPARS_TCLOSE) {
			int er = a->dst.f_0(ps, ctx->obj);
			if (er != 0)
				return er;
		}
	}

	ps->ctxs.len--;
	ps->curarg = ffarr_back(&ps->ctxs).args;
	return 0;
}

int ffpars_schemrun(ffparser_schem *ps, int e)
{
	int rc = 0;

	if (e >= 0)
		return e;

	if (ps->flags & FFPARS_SCHAVKEY) {
		if (e != FFPARS_VAL)
			return FFPARS_EVALEMPTY; //key without a value

		ps->flags &= ~FFPARS_SCHAVKEY;

	} else if (ps->flags & FFPARS_SCCTX) {
		if (e != FFPARS_OPEN)
			return FFPARS_EVALEMPTY; //context open expected

		ps->flags &= ~FFPARS_SCCTX;
	}

	if (e != FFPARS_KEY && ps->ctxs.len != 0) {
		rc = ps->onval(ps, ffarr_back(&ps->ctxs).obj, NULL);
		if (ffpars_iserr(rc))
			return rc;
		else if (rc == FFPARS_DONE)
			return 0;
	}

	switch (e) {
	case FFPARS_KEY:
		if (ps->ctxs.len == 0)
			return FFPARS_ECONF;

#ifdef FFDBG_PARSE
		ffdbg_print(0, "%s(): processing key '%S'\n", FF_FUNC, &ps->p->val);
#endif

		rc = scHdlKey(ps, &ffarr_back(&ps->ctxs));
		break;

	case FFPARS_VAL:
		if (ps->ctxs.len == 0)
			return FFPARS_ECONF;

		rc = scHdlVal(ps, &ffarr_back(&ps->ctxs));
		break;

	case FFPARS_OPEN:
		rc = scOpenBrace(ps);
		break;

	case FFPARS_CLOSE:
		rc = scCloseBrace(ps);
		break;
	}

	if (rc != 0)
		e = rc;
	return e;
}

const char * ffpars_schemerrstr(ffparser_schem *ps, int code, char *buf, size_t cap)
{
	(void)buf;
	(void)cap;

	if (code >= 0x8000) {
		FF_ASSERT(ps->ctxs.len != 0);
		return ffarr_back(&ps->ctxs).errfunc(code);
	}

	FF_ASSERT(code < FFCNT(_ffpars_serr));
	return _ffpars_serr[code];
}
