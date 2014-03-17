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
static const ffpars_arg * scGetArg(ffpars_ctx *ctx, int type);
static int scHdlEnum(ffparser_schem *ps, void *obj);
static void scHdlBits(size_t f, int64 i, union ffpars_val dst);
static int scSetInt(ffparser_schem *ps, ffpars_ctx *ctx, union ffpars_val dst, union ffpars_val edst, int64 n);
static int scHdlVal(ffparser_schem *ps, ffpars_ctx *ctx);
static int scOpenBrace(ffparser_schem *ps);
static int scCloseBrace(ffparser_schem *ps);

enum FFPARS_SCHEMFLAG {
	FFPARS_SCHAVKEY = 1
	, FFPARS_SCHAVVAL = 2
	, FFPARS_SCCTX = 4
};

void ffpars_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_arg *top)
{
	ffmem_tzero(ps);
	ps->p = p;
	ps->curarg = top;
}

// search key name in the current context
static int scHdlKey(ffparser_schem *ps, ffpars_ctx *ctx)
{
	int er;
	uint i = ctx->nargs;

	er = ps->onval(ps, ctx->obj, &i);
	if (er > 0)
		return er;
	else if (er == FFPARS_OK) {
		for (i = 0;  i < ctx->nargs;  i++) {
			if (ctx->args[i].name != NULL
				&& ffstr_eqz(&ps->p->val, ctx->args[i].name))
				break;
		}
	}

	if (i == ctx->nargs) {

		for (i = 0;  i < ctx->nargs;  i++) {
			const ffpars_arg *a = &ctx->args[i];
			if (a->name != NULL
				&& a->name[0] == '*'
				&& a->name[1] == '\0')
				break;
		}

		if (i == ctx->nargs) {
			ps->curarg = NULL;
			return FFPARS_EUKNKEY;
		}

		ps->curarg = &ctx->args[i];
		return scHdlVal(ps, ctx);
	}

	ps->curarg = &ctx->args[i];

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

// search and select entry by type
static const ffpars_arg * scGetArg(ffpars_ctx *ctx, int type)
{
	uint i;
	for (i = 0;  i < ctx->nargs;  i++) {
		if (type == (ctx->args[i].flags & FFPARS_FTYPEMASK))
			return &ctx->args[i];
	}

	return NULL;
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

	if (i == 0)
		return ; //we don't reset bits

	if (f & FFPARS_F64BIT)
		ffbit_set64((uint64*)dst.i64, bit);
	//else if (f & FFPARS_F16BIT)
	//	ffbit_set32((uint*)dst.i16, bit);
	//else if (f & FFPARS_F8BIT)
	//	ffbit_set32((uint*)dst.b, bit);
	else
		ffbit_set32((uint*)dst.i32, bit);
}

static int scSetInt(ffparser_schem *ps, ffpars_ctx *ctx, union ffpars_val dst, union ffpars_val edst, int64 n)
{
	size_t f = ps->curarg->flags;
	int func = (edst.off >= 64 * 1024);

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

	if (func)
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

	if (ffarr_back(&p->ctxs) == FFPARS_TARR) {
		curarg = ps->curarg = scGetArg(ctx, ps->p->type);
		if (curarg == NULL)
			return FFPARS_EARRTYPE;
	}

	f = curarg->flags;
	t = f & FFPARS_FTYPEMASK;
	edst = curarg->dst;

	if (t == FFPARS_TENUM)
		edst = curarg->dst.enum_list->dst;

	if (f & FFPARS_FPTR)
		dst.s = edst.s;
	else if (edst.off >= 64 * 1024) {
		func = 1;
		dst.s = NULL;
	}
	else
		dst.b = (byte*)ctx->obj + edst.off;

	er = ps->onval(ps, ctx->obj, dst.b);
	if (er > 0)
		return er;
	else if (er == FFPARS_DONE)
		return 0;

	switch (t) {
	case FFPARS_TSTR:
		if (p->val.len == 0 && (f & FFPARS_FNOTEMPTY))
			return FFPARS_EVALEMPTY;

		if (func) {
			ffstr tmp = p->val;

			if (f & FFPARS_FCOPY) {
				if (p->val.len == 0)
					ffstr_null(&tmp);
				else if (NULL == ffstr_copy(&tmp, p->val.ptr, p->val.len))
					return FFPARS_ESYS;
			}

			er = edst.f_str(ps, ctx->obj, &tmp);

		} else if (f & FFPARS_FCOPY) {
			if (NULL == ffstr_copy(dst.s, p->val.ptr, p->val.len))
				return FFPARS_ESYS;
		}
		else
			*dst.s = p->val;
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

	default:
		er = FFPARS_EVALTYPE;
	}

	if (er == 0 && ps->p->ret == FFPARS_VAL
		&& !(ps->curarg->flags & FFPARS_FLIST)
		&& ffarr_back(&p->ctxs) != FFPARS_TARR)
		ps->flags |= FFPARS_SCHAVVAL; // the argument with exactly 1 value

	return er;
}

/* if the ctx is object: 'curarg' is already set by FFPARS_KEY handler
if the ctx is array: search for an entry of type array or object in the array scheme
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

	if (ps->p->ctxs.len >= 2 && ffarr_back(&ps->p->ctxs) == FFPARS_TARR) {
		curarg = ps->curarg = scGetArg(curctx, ps->p->type);
		if (curarg == NULL)
			return FFPARS_EARRTYPE;
	}

	if (curarg->flags & FFPARS_FPTR) {
		*ctx = *curarg->dst.ctx;
	}
	else {
		if (ps->ctxs.len < 2)
			return FFPARS_ECONF;
		o = curctx->obj;

		er = curarg->dst.f_obj(ps, o, ctx);
		if (er != 0)
			ps->ctxs.len--;
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

	{
		const ffpars_arg *a = scGetArg(ctx, FFPARS_TCLOSE);
		if (a != NULL)
			er = a->dst.f_0(ps, ctx->obj);
	}

	ps->ctxs.len--;
	ps->curarg = ffarr_back(&ps->ctxs).args;
	return er;
}

int ffpars_schemrun(ffparser_schem *ps, int e)
{
	int rc = 0;

	if (ps->flags & FFPARS_SCHAVKEY) {
		if (e != FFPARS_VAL)
			return FFPARS_EVALEMPTY; //key without a value

		ps->flags &= ~FFPARS_SCHAVKEY;

	} else if (ps->flags & FFPARS_SCHAVVAL) {
		if (e == FFPARS_VAL)
			return FFPARS_EVALUNEXP; //value has been specified already

		ps->flags &= ~FFPARS_SCHAVVAL;

	} else if (ps->flags & FFPARS_SCCTX) {
		if (e != FFPARS_OPEN)
			return FFPARS_EVALEMPTY; //context open expected

		ps->flags &= ~FFPARS_SCCTX;
	}

	switch (e) {
	case FFPARS_KEY:
		if (ps->ctxs.len == 0)
			return FFPARS_ECONF;

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

	return _ffpars_serr[code];
}
