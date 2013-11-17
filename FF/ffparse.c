/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/parse.h>

static const char *const _ffpars_serr[] = {
	""
	, "system"
	, "invalid character"
	, "key-value separator"
	, "no value"
	, "bad value"
	, "too large value"
	, "invalid escape sequence"
	, "inconsistent brace"
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
	, "parser misuse"
};

const char * ffpars_errstr(int code)
{
	return _ffpars_serr[code];
}


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

int _ffpars_copyText(ffparser *p, const char *text, size_t len)
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
		rc = _ffpars_copyText(p, NULL, 0);
	return rc;
}


void ffpars_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_arg *top)
{
	ps->p = p;
	ps->udata = NULL;
	ffarr_null(&ps->ctxs);
	ps->curarg = top;
	ps->valfunc = NULL;
}

// search key name in the current context
static int scHdlKey(ffparser_schem *ps, ffpars_ctx *ctx)
{
	uint i;

	for (i = 0;  i < ctx->nargs;  i++) {
		if (ffstr_eqz(&ps->p->val, ctx->args[i].name))
			break;
	}

	if (i == ctx->nargs) {
		ps->curarg = NULL;
		return FFPARS_EUKNKEY;
	}

	ps->curarg = &ctx->args[i];
	if (i < sizeof(ctx->used)*8
		&& ffbit_setarr(ctx->used, i) && !(ps->curarg->flags & FFPARS_FMULTI))
		return FFPARS_EDUPKEY;

	return 0;
}

// search and select entry by type
static int scGetArg(ffparser_schem *ps, ffpars_ctx *ctx, int type)
{
	uint i;

	ps->curarg = NULL;
	for (i = 0;  i < ctx->nargs;  i++) {
		if (type == (ctx->args[i].flags & FFPARS_FTYPEMASK)) {
			ps->curarg = &ctx->args[i];
			break;
		}
	}

	if (ps->curarg == NULL)
		return FFPARS_EARRTYPE;
	return 0;
}

// set integer byte value according to the index of char*[] array which contains all possible text values
static int scHdlEnum(ffparser_schem *ps, void *obj)
{
	char **vals = ps->curarg->dst.enum_vals;
	size_t i;

	for (i = 0;  vals[i] != NULL;  ++i) {
		if (i > 0xff)
			return FFPARS_ECONF; //enum can't contain more than 255 entries

		if (ffstr_eqz(&ps->p->val, vals[i])) {
			size_t j;
			byte *ptr;
			for (j = i + 1;  vals[j] != NULL;  ++j)
			{}

			ptr = (byte *)obj + (size_t)vals[j + 1];
			*ptr = (byte)i;
			return 0;
		}
	}

	return FFPARS_EBADVAL;
}

static int scHdlVal(ffparser_schem *ps)
{
	const ffpars_arg *curarg = ps->curarg;
	int t;
	ffparser *p = ps->p;
	int er = 0;
	ffbool func = 0;
	union ffpars_val dst;
	ffpars_ctx *ctx = NULL;

	if (ps->ctxs.len == 0 && !(curarg->flags & FFPARS_FPTR))
		return FFPARS_ECONF;

	if (ps->ctxs.len != 0) {
		ctx = &ffarr_back(&ps->ctxs);
	}

	if (ffarr_back(&p->ctxs) == FFPARS_TARR) {
		er = scGetArg(ps, ctx, ps->p->type);
		if (er != 0)
			return er;
		curarg = ps->curarg;
	}

	if (curarg->flags & FFPARS_FPTR)
		dst.s = curarg->dst.s;
	else if (curarg->dst.off >= 64 * 1024) {
		func = 1;
		dst.s = NULL;
	}
	else
		dst.b = (byte*)ctx->obj + curarg->dst.off;

	er = ps->valfunc(ps, ctx->obj, dst.b);
	if (er >= 0)
		return er;
	er = 0;

	t = curarg->flags & FFPARS_FTYPEMASK;
	switch (t) {
	case FFPARS_TSTR:
		if (p->val.len == 0 && (curarg->flags & FFPARS_FNOTEMPTY))
			return FFPARS_EVALEMPTY;

		if (func)
			er = curarg->dst.f_str(ps, ctx->obj, &p->val);
		else if (curarg->flags & FFPARS_FCOPY) {
			if (NULL == ffstr_copy(dst.s, p->val.ptr, p->val.len))
				return FFPARS_ESYS;
		}
		else
			*dst.s = p->val;
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
		if (p->intval == 0 && (curarg->flags & FFPARS_FNOTZERO))
			return FFPARS_EVALZERO;

		if (p->intval < 0 && !(curarg->flags & FFPARS_FSIGN))
			return FFPARS_EVALNEG;

		if (!func) {
			if (curarg->flags & FFPARS_F64BIT)
				*dst.i64 = p->intval;
			else {
				if ((p->intval >> 32) != 0)
					return FFPARS_EBIGVAL;
				*dst.i32 = (int)p->intval;
			}
			break;
		}
		//break;

	case FFPARS_TBOOL:
		if (func)
			er = curarg->dst.f_int(ps, ctx->obj, &p->intval);
		else if (t == FFPARS_TBOOL)
			*dst.b = p->intval != 0;
		break;

	case FFPARS_TENUM:
		er = scHdlEnum(ps, ctx->obj);
		break;
	}

	return er;
}

/* if the ctx is object: 'curarg' is already set by FFPARS_KEY handler
if the ctx is array: search for an entry of type array or object in the array scheme
if FPTR flag: copy the ffpars_args structure from the pointer specified in scheme
if no FPTR flag: call object handler func */
static int scOpenBrace(ffparser_schem *ps)
{
	ffpars_ctx *ctx;
	ffpars_ctx *curctx;
	const ffpars_arg *curarg = ps->curarg;
	int t;
	int er = 0;
	void *o;

	ctx = ffarr_push(&ps->ctxs, ffpars_ctx);
	if (ctx == NULL)
		return FFPARS_ESYS;
	memset(ctx, 0, sizeof(ffpars_ctx));

	curctx = &ps->ctxs.ptr[ps->ctxs.len - 2];

	if (ps->p->ctxs.len >= 2 && ffarr_back(&ps->p->ctxs) == FFPARS_TARR) {
		er = scGetArg(ps, curctx, ps->p->type);
		if (er != 0)
			return er;
		curarg = ps->curarg;
	}

	t = curarg->flags & FFPARS_FTYPEMASK;

	if (t != ps->p->type)
		return FFPARS_EVALTYPE;

	if (curarg->flags & FFPARS_FPTR) {
		*ctx = *curarg->dst.ctx;
	}
	else {
		if (ps->ctxs.len < 2)
			return FFPARS_ECONF;
		o = curctx->obj;

		er = curarg->dst.f_obj(ps, o, ctx);
	}

	return er;
}

/* Check whether all required parameters have been specified.
Call "on-close" callback function, if it exists. */
static int scCloseBrace(ffparser_schem *ps)
{
	uint i;
	int er = 0;
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
			if (i + bit >= maxargs)
				break;
			if (ctx->args[i + bit].flags & FFPARS_FREQUIRED)
				return FFPARS_ENOREQ;
			ffbit_reset32(&n, bit);
		}
	}

	er = scGetArg(ps, ctx, FFPARS_TCLOSE);
	if (er == 0)
		er = ps->curarg->dst.f_0(ps, ctx->obj);
	else
		er = 0; //ignore error

	ps->ctxs.len--;
	return 0;
}

int ffpars_schemrun(ffparser_schem *ps, int e)
{
	int rc = 0;

	switch (e) {
	case FFPARS_KEY:
		if (ps->ctxs.len == 0)
			return FFPARS_ECONF;
		rc = scHdlKey(ps, &ffarr_back(&ps->ctxs));
		break;

	case FFPARS_VAL:
		rc = scHdlVal(ps);
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
