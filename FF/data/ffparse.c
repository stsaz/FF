/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/data/parse.h>
#include <FF/number.h>


static const char *const _ffpars_serr[] = {
	"",
	"system", //FFPARS_ESYS
	"internal", //FFPARS_EINTL
	"invalid character", //FFPARS_EBADCHAR
	"key-value separator", //FFPARS_EKVSEP
	"no value", //FFPARS_ENOVAL
	"bad value", //FFPARS_EBADVAL
	"too large value", //FFPARS_EBIGVAL
	"invalid escape sequence", //FFPARS_EESC
	"inconsistent brace", //FFPARS_EBADBRACE
	"expected brace", //FFPARS_ENOBRACE
	"invalid comment", //FFPARS_EBADCMT

	"unknown key name", //FFPARS_EUKNKEY
	"duplicate key", //FFPARS_EDUPKEY
	"unspecified required parameter", //FFPARS_ENOREQ
	"invalid integer", //FFPARS_EBADINT
	"invalid boolean", //FFPARS_EBADBOOL
	"array value type mismatch", //FFPARS_EARRTYPE
	"value type mismatch", //FFPARS_EVALTYPE
	"null value", //FFPARS_EVALNULL
	"empty value", //FFPARS_EVALEMPTY
	"zero value", //FFPARS_EVALZERO
	"negative value", //FFPARS_EVALNEG
	"unexpected value", //FFPARS_EVALUNEXP
	"unknown value", //FFPARS_EVALUKN
	"unsupported value", //FFPARS_EVALUNSUPP
	"parser misuse", //FFPARS_ECONF
};

const char * ffpars_errstr(int code)
{
	return _ffpars_serr[code];
}


const ffpars_arg* ffpars_ctx_findarg(ffpars_ctx *ctx, const char *name, size_t len, uint flags)
{
	const ffpars_arg *a = NULL;
	uint i, nargs = ctx->nargs;

	FF_ASSERT(ctx->nargs != 0);

	if ((ctx->args[nargs - 1].flags & FFPARS_FTYPEMASK) == FFPARS_TCLOSE)
		nargs--;

	if (flags & FFPARS_CTX_FKEYICASE) {
		for (i = 0;  i != nargs;  i++) {
			if (0 == ffs_icmpz(name, len, ctx->args[i].name)) {
				a = &ctx->args[i];
				break;
			}
		}

	} else {
		for (i = 0;  i != nargs;  i++) {
			if (0 == ffs_cmpz(name, len, ctx->args[i].name)) {
				a = &ctx->args[i];
				break;
			}
		}
	}

	if (a != NULL && !ffsz_cmp(a->name, "*"))
		a = NULL; // "*" is a reserved name

	uint first = (ctx->args[0].name[0] == '\0'); //skip "" argument
	if (a == NULL && (flags & FFPARS_CTX_FANY) && nargs != first) {

		a = &ctx->args[first];
		if (a->name[0] == '*' && a->name[1] == '\0')
			goto done;

		a = &ctx->args[nargs - 1];
		if (a->name[0] == '*' && a->name[1] == '\0')
			goto done;

		a = NULL; // "*" must be either first or last

	} else if (a != NULL && (flags & FFPARS_CTX_FDUP) && i < sizeof(ctx->used)*8
		&& ffbit_setarr(ctx->used, i) && !(a->flags & FFPARS_FMULTI))
		return (void*)-1;

done:
	FFDBG_PRINTLN(FFDBG_PARSE | 10, "\"%*s\" (%u/%u), object:%p"
		, len, name, (a != NULL) ? (int)(a - ctx->args) : 0, ctx->nargs, ctx->obj);
	return a;
}


static int _ffpars_enum(const ffpars_arg *a, const ffstr *val, void *obj, void *ps);
static int _ffpars_bits(size_t f, int64 i, union ffpars_val dst);
static int _ffpars_int(const ffpars_arg *a, int64 val, void *obj, void *ps);
static int _ffpars_intval(const ffpars_arg *a, int64 n, void *obj, void *ps);
static int _ffpars_str(const ffpars_arg *a, const ffstr *val, void *obj, void *ps);
static int scOpenBrace(ffparser_schem *ps);
static int scCloseBrace(ffparser_schem *ps);

void ffpars_scheminit(ffparser_schem *ps, void *p, const ffpars_arg *top)
{
	ffmem_tzero(ps);
	ps->p = p;
	ps->curarg = top;
}

// search key name in the current context
int _ffpars_schemrun_key(ffparser_schem *ps, ffpars_ctx *ctx, const ffstr *val)
{
	uint f = 0;

	if (ps->flags & FFPARS_KEYICASE)
		f |= FFPARS_CTX_FKEYICASE;
	ps->curarg = NULL;
	const ffpars_arg *arg = ffpars_ctx_findarg(ctx, val->ptr, val->len, FFPARS_CTX_FDUP | f);
	if (arg == NULL)
		return FFPARS_EUKNKEY;
	else if (arg == (void*)-1)
		return FFPARS_EDUPKEY;
	ps->curarg = arg;
	return 0;
}

static int _ffpars_str(const ffpars_arg *a, const ffstr *val, void *obj, void *ps)
{
	uint f, t;
	ffbool func;
	ffstr tmp = {};
	int er = 0;
	union ffpars_val dst;

	f = a->flags;
	t = f & FFPARS_FTYPEMASK;
	func = ffpars_arg_isfunc(a);
	dst.b = ffpars_arg_ptr(a, obj);

	if (val->len == 0 && (f & FFPARS_FNOTEMPTY))
		return FFPARS_EVALEMPTY;

	if ((f & FFPARS_FNONULL)
		&& NULL != ffs_findc(val->ptr, val->len, '\0'))
		return FFPARS_EBADCHAR;

	if (f & FFPARS_FCOPY) {

		if (ffint_mask_test(f, FFPARS_FSTRZ)) {
			if (NULL == ffstr_alloc(&tmp, val->len + 1))
				return FFPARS_ESYS;
			tmp.len = ffsz_fcopy(tmp.ptr, val->ptr, val->len) - tmp.ptr;

		} else if (val->len == 0)
			ffstr_null(&tmp);

		else if (NULL == ffstr_alcopystr(&tmp, val))
			return FFPARS_ESYS;

	} else {

		if (ffint_mask_test(f, FFPARS_FSTRZ))
			return FFPARS_ECONF;

		tmp = *val;
	}

	if (t == FFPARS_TCHARPTR) {

		if (!ffint_mask_test(f, FFPARS_FSTRZ))
			return FFPARS_ECONF;

		if (func)
			er = a->dst.f_charptr(ps, obj, tmp.ptr);
		else {
			if (ffint_mask_test(f, FFPARS_FRECOPY))
				ffmem_safefree(*dst.charptr);

			*dst.charptr = tmp.ptr;
		}

	} else {
		if (func)
			er = a->dst.f_str(ps, obj, &tmp);
		else {
			if (ffint_mask_test(f, FFPARS_FRECOPY))
				ffmem_safefree(dst.s->ptr);

			*dst.s = tmp;
		}
	}

	if (func && er != 0 && (f & FFPARS_FCOPY))
		ffstr_free(&tmp);
	return er;
}

static int _ffpars_enum(const ffpars_arg *a, const ffstr *val, void *obj, void *ps)
{
	const ffpars_enumlist *en = a->dst.enum_list;
	uint i;
	for (i = 0;  i != en->nvals;  i++) {
		if (ffstr_eqz(val, en->vals[i]))
			break;
	}

	if (i == en->nvals)
		return FFPARS_EVALUKN;

	ffpars_arg aint;
	aint.flags = FFPARS_TINT | (a->flags & ~FFPARS_FTYPEMASK);
	aint.dst = a->dst.enum_list->dst;
	return _ffpars_intval(&aint, i, obj, ps);
}

static const byte pars_width[] = { 32, 64, 16, 8 };

#define PARS_WIDTH(f) \
	pars_width[((f) >> 8) & 0x03]

static int _ffpars_bits(size_t f, int64 i, union ffpars_val dst)
{
	uint bit = (f >> 24) & 0xff;

	switch (PARS_WIDTH(f)) {
	case 64:
		if (i != 0)
			ffbit_set64((uint64*)dst.i64, bit);
		else
			ffbit_reset64((uint64*)dst.i64, bit);
		break;

	case 32:
		if (i != 0)
			ffbit_set32((uint*)dst.i32, bit);
		else
			ffbit_reset32((uint*)dst.i32, bit);
		break;

	default:
		return FFPARS_ECONF;
	}
	return 0;
}

static int _ffpars_bitmask(size_t f, int64 i, union ffpars_val dst)
{
	uint mask = (f >> 24) & 0xff;

	switch (PARS_WIDTH(f)) {
	case 64:
		if (i != 0)
			*(uint64*)dst.i64 |= mask;
		else
			*(uint64*)dst.i64 &= ~mask;
		break;

	case 32:
		if (i != 0)
			*(uint*)dst.i32 |= mask;
		else
			*(uint*)dst.i32 &= ~mask;
		break;

	default:
		return FFPARS_ECONF;
	}
	return 0;
}

static int _ffpars_size(const ffpars_arg *a, const ffstr *val, void *obj, void *ps)
{
	uint64 intval;
	ffstr s = *val;
	if (s.len == 0)
		return FFPARS_EVALEMPTY;

	uint shift = ffchar_sizesfx(ffarr_back(&s));
	if (shift != 0)
		s.len--;
	if (!ffstr_toint(&s, &intval, FFS_INT64))
		return FFPARS_EBADINT;
	intval <<= shift;
	return _ffpars_int(a, intval, obj, ps);
}

static int _ffpars_int(const ffpars_arg *a, int64 val, void *obj, void *ps)
{
	size_t f = a->flags;

	if ((f & FFPARS_FNOTZERO) && val == 0)
		return FFPARS_EVALZERO;

	if (!(f & FFPARS_FSIGN) && val < 0)
		return FFPARS_EVALNEG;

	return _ffpars_intval(a, val, obj, ps);
}

static int _ffpars_intval(const ffpars_arg *a, int64 n, void *obj, void *ps)
{
	size_t f = a->flags;
	uint width = PARS_WIDTH(f);
	union ffpars_val dst;

	if (width != 64 && 0 != (ffabs(n) >> width))
		return FFPARS_EBIGVAL;

	if (ffpars_arg_isfunc(a))
		return a->dst.f_int(ps, obj, &n);

	dst.b = ffpars_arg_ptr(a, obj);

	if (f & FFPARS_FBIT)
		return _ffpars_bits(f, n, dst);

	else if (f & FFPARS_FBITMASK)
		return _ffpars_bitmask(f, n, dst);

	switch (width) {
	case 64:
		*dst.i64 = n; break;
	case 32:
		*dst.i32 = (int)n; break;
	case 16:
		*dst.i16 = (short)n; break;
	case 8:
		*dst.b = (char)n; break;
	}

	return 0;
}

int64 ffpars_getint(const ffpars_arg *a, union ffpars_val u)
{
	size_t f = a->flags;
	uint width = PARS_WIDTH(f);
	int64 n = 0;

	if (f & FFPARS_FSIGN) {
		switch (width) {
		case 64:
			n = *u.i64; break;
		case 32:
			n = *u.i32; break;
		case 16:
			n = *u.i16; break;
		case 8:
			n = (char)*u.b; break;
		default:
			FF_ASSERT(0);
		}

	} else {
		switch (width) {
		case 64:
			n = *u.i64; break;
		case 32:
			n = (uint)*u.i32; break;
		case 16:
			n = (ushort)*u.i16; break;
		case 8:
			n = (byte)*u.b; break;
		default:
			FF_ASSERT(0);
		}

		if (f & FFPARS_FBIT) {
			uint bit = (f >> 24) & 0xff;
			n = ffbit_test64((uint64*)&n, bit);
		}
	}

	return n;
}

static int _ffpars_flt(const ffpars_arg *a, double val, void *obj, void *ps)
{
	union ffpars_val dst;
	size_t f = a->flags;

	if ((f & FFPARS_FNOTZERO) && val == 0)
		return FFPARS_EVALZERO;

	if (!(f & FFPARS_FSIGN) && val < 0)
		return FFPARS_EVALNEG;

	if (ffpars_arg_isfunc(a))
		return a->dst.f_flt(ps, obj, &val);

	dst.b = ffpars_arg_ptr(a, obj);

	switch (PARS_WIDTH(f)) {
	case 64:
		*dst.f64 = val; break;
	case 32:
		*dst.f32 = (float)val; break;
	default:
		return FFPARS_ECONF; //invalid bits for a float
	}

	return 0;
}

int ffpars_arg_process(const ffpars_arg *a, const ffstr *val, void *obj, void *ps)
{
	int er = 0;
	int64 intval;
	double fltval;
	ffbool boolval;

	switch (a->flags & FFPARS_FTYPEMASK) {

	case FFPARS_TSTR:
	case FFPARS_TCHARPTR:
		er = _ffpars_str(a, val, obj, ps);
		break;

	case FFPARS_TENUM:
		er = _ffpars_enum(a, val, obj, ps);
		break;

	case FFPARS_TSIZE:
		er = _ffpars_size(a, val, obj, ps);
		break;

	case FFPARS_TINT:
		if (val->len > FFINT_MAXCHARS
			|| !ffstr_toint(val, &intval, FFS_INT64 | FFS_INTSIGN))
			return FFPARS_EBADINT;
		er = _ffpars_int(a, intval, obj, ps);
		break;

	case FFPARS_TBOOL:
		if (ffstr_toint(val, &boolval, FFS_INT32)) {
			if (!(boolval == 0 || boolval == 1))
				return FFPARS_EBADBOOL;

		} else if (!ffstr_tobool(val, &boolval, 0))
			return FFPARS_EBADBOOL;
		er = _ffpars_intval(a, boolval, obj, ps);
		break;

	case FFPARS_TFLOAT:
		if (val->len != ffs_tofloat(val->ptr, val->len, &fltval, 0))
			return FFPARS_EBADVAL;
		er = _ffpars_flt(a, fltval, obj, ps);
		break;

	case FFPARS_TANYTHING:
		er = a->dst.f_0(ps, obj);
		break;

	default:
		er = FFPARS_EVALTYPE;
	}

	return er;
}

int _ffpars_arg_process2(const ffpars_arg *a, const void *val, void *obj, void *ps)
{
	int r = 0;
	switch (a->flags & FFPARS_FTYPEMASK) {
	case FFPARS_TINT:
		r = _ffpars_int(a, *(int64*)val, obj, ps);
		break;
	case FFPARS_TBOOL:
		r = _ffpars_intval(a, *(int64*)val, obj, ps);
		break;
	case FFPARS_TFLOAT:
		r = _ffpars_flt(a, *(double*)val, obj, ps);
		break;
	default:
		FF_ASSERT(0);
	}
	return r;
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

	if (curarg->flags & FFPARS_FPTR) {
		ctx = curarg->dst.ctx;
		if (0 != ffpars_setctx(ps, ctx->obj, ctx->args, ctx->nargs))
			return FFPARS_ESYS;
	}
	else {
		curctx = ffarr_lastT(&ps->ctxs, ffpars_ctx);
		o = curctx->obj;
		if (0 != ffpars_setctx(ps, NULL, NULL, 0))
			return FFPARS_ESYS;
		if (ps->ctxs.len < 2)
			return FFPARS_ECONF;

		ctx = ffarr_lastT(&ps->ctxs, ffpars_ctx);
		er = curarg->dst.f_obj(ps, o, ctx);
		if (er != 0)
			ps->ctxs.len--;
	}

	if (er == 0)
		ps->curarg = NULL;
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
			uint bit = ffbit_ffs32(n) - 1;
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
	ps->curarg = NULL;
	return 0;
}

int ffpars_setctx(ffparser_schem *ps, void *o, const ffpars_arg *args, uint nargs)
{
	ffpars_ctx *newctx;
	if (NULL == (newctx = ffarr_pushgrowT((ffarr*)&ps->ctxs, 4, ffpars_ctx)))
		return FFPARS_ESYS;
	memset(newctx, 0, sizeof(ffpars_ctx));
	ffpars_setargs(newctx, o, args, nargs);
	return 0;
}

void ffpars_ctx_skip(ffpars_ctx *ctx)
{
	ffpars_setargs(ctx, (void*)-1, (void*)-1, 0);
}

int _ffpars_skipctx(ffparser_schem *ps, int ret)
{
	if (ps->ctxs.len == 0)
		return 0;

	ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);
	if (ctx->obj == (void*)-1 && ctx->args == (void*)-1) {

		switch (ret) {
		case FFPARS_OPEN:
			ctx = ffarr_push(&ps->ctxs, ffpars_ctx);
			if (ctx == NULL)
				return FFPARS_ESYS;
			ffpars_ctx_skip(ctx);
			break;

		case FFPARS_CLOSE:
			ps->ctxs.len--;
			break;
		}
		return ret;
	}
	return 0;
}

int _ffpars_schemrun(ffparser_schem *ps, int e)
{
	int rc = 0;

	if (e >= 0)
		return e;

	switch (e) {
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

	FF_ASSERT((uint)code < FFCNT(_ffpars_serr));
	return _ffpars_serr[code];
}

int ffpars_scheme_write(char *buf, const ffpars_arg *arg, void *obj, ffstr *out)
{
	union ffpars_val u;
	u.b = ffpars_arg_ptr(arg, obj);
	uint t = (arg->flags & FFPARS_FTYPEMASK);
	switch (t) {
	case FFPARS_TSTR:
		ffstr_set2(out, u.s);
		break;
	case FFPARS_TCHARPTR:
		ffstr_setz(out, *u.charptr);
		break;
	case FFPARS_TINT: {
		int64 val = ffpars_getint(arg, u);
		uint f = (arg->flags & FFPARS_FSIGN) ? FFINT_SIGNED : 0;
		uint n = ffs_fromint(val, buf, 64, f);
		ffstr_set(out, buf, n);
		break;
	}
	default:
		return FFPARS_EVALTYPE;
	}

	return 0;
}


int ffsvar_parse(ffsvar *p, const char *data, size_t *plen)
{
	size_t i;

	if (*data != '$') {
		const char *s = ffs_find(data, *plen, '$');
		p->val.ptr = (char*)data;
		p->val.len = s - data;
		*plen = p->val.len;
		return FFSVAR_TEXT;
	}

	for (i = 1 /*skip $*/;  i != *plen;  i++) {
		if (!ffchar_isname(data[i]))
			break;
	}
	p->val.ptr = (char*)&data[1];
	p->val.len = i - 1;
	*plen = i;
	return FFSVAR_S;
}
