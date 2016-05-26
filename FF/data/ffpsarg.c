/**
Copyright (c) 2013 Simon Zolin
*/


#include <FF/data/psarg.h>


#ifdef FF_WIN
void ffpsarg_init(ffpsarg *a, const char **argv, uint argc)
{
	if (NULL == (a->cmdln.ptr = ffsz_alcopyqz(GetCommandLine())))
		return;
	a->cmdln.cap = ffsz_len(a->cmdln.ptr);
	a->cmdln.len = 0;
}

const char* ffpsarg_next(ffpsarg *a)
{
	ffstr arg;
	char *q, *q2, *p, *end;
	if (a->cmdln.len == a->cmdln.cap)
		return NULL;
	size_t n = ffstr_nextval(ffarr_end(&a->cmdln), ffarr_unused(&a->cmdln), &arg, ' ' | FFSTR_NV_DBLQUOT);
	a->cmdln.len += n;

	if (ffarr_end(&arg) != (q = ffs_finds(arg.ptr, arg.len, "=\"", 2))) {
		// --key="value with space" -> --key=value with space
		p = ffarr_end(&a->cmdln);
		end = ffarr_edge(&a->cmdln);

		q += FFSLEN("=");
		q2 = ffs_find(p, end - p, '"');
		memmove(q, q + 1, q2 - (q + 1));
		arg.len = q2 - 1 - arg.ptr;

		q2 += (q2 != end);
		a->cmdln.len = ffs_skip(q2, end - q2, ' ') - a->cmdln.ptr;
	}

	arg.ptr[arg.len] = '\0';
	return arg.ptr;
}
#endif


enum ARG_IDX {
	iArgStart, iArgVal, iArgNextShortOpt, iArgDone
};

int ffpsarg_parseinit(ffparser *p)
{
	char *ctx;
	ffpars_init(p);
	p->state = iArgStart;

	ctx = ffarr_push(&p->ctxs, char);
	if (ctx == NULL)
		return 1;

	*ctx = FFPARS_OPEN;
	p->type = 0;
	return 0;
}

int ffpsarg_parse(ffparser *p, const char *a, int *processed)
{
	int r = FFPARS_EBADCHAR;
	int st = p->state;

	switch (st) {
	case iArgDone:
		p->line++;
		st = iArgStart;
		//break;

	case iArgStart:
		if (a[0] == '-') {
			if (a[1] == '\0') {
				// -

			} else if (a[1] != '-') {
				p->ch = 1;
				st = iArgNextShortOpt; //-a...
				break;

			} else if (a[2] == '\0') {
				// --

			} else {
				size_t alen;
				const char *eqch;
				a += FFSLEN("--");

				eqch = strchr(a, '=');
				if (eqch != NULL) { //--arg1=val
					alen = eqch - a;
					p->ch = (uint)(eqch + FFSLEN("=") - (a - FFSLEN("--")));
					*processed = 0;
					st = iArgVal;

				} else {
					alen = strlen(a);
					*processed = 1;
					st = iArgDone;
				}

				if (alen == 0)
					return FFPARS_EUKNKEY; //"--" is not allowed

				p->type = FFPSARG_LONG;
				ffstr_set(&p->val, a, alen);
				r = FFPARS_KEY;
				break;
			}
		}
		//break;

	case iArgVal:
		a += p->ch;
		*processed = 1;
		p->ch = 0;
		if (st == iArgVal)
			p->type = FFPSARG_KVAL;
		else if (p->type == FFPSARG_LONG || p->type == FFPSARG_SHORT)
			p->type = FFPSARG_VAL;
		else
			p->type = FFPSARG_INPUTVAL;
		st = iArgDone;
		ffstr_set(&p->val, a, strlen(a));
		r = FFPARS_VAL;
		break;
	}

	if (st == iArgNextShortOpt) {
		a += p->ch;
		if (a[1] == '\0') {
			*processed = 1;
			p->ch = 0;
			st = iArgDone;

		} else {
			*processed = 0;
			p->ch++;
			//st = iArgNextShortOpt;
		}

		p->type = FFPSARG_SHORT;
		ffstr_set(&p->val, a, 1);
		r = FFPARS_KEY;
	}

	p->state = st;
	p->ret = r;
	return r;
}


int ffpsarg_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx)
{
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(ctx) };
	ffpars_scheminit(ps, p, &top);

	if (0 != ffpsarg_parseinit(p))
		return 1;
	if (FFPARS_OPEN != ffpars_schemrun(ps, FFPARS_OPEN))
		return 1;

	return 0;
}

int ffpsarg_schemfin(ffparser_schem *ps)
{
	int r;
	ps->p->ret = FFPARS_CLOSE;
	r = ffpsarg_schemrun(ps);
	if (r != FFPARS_CLOSE)
		return r;
	return 0;
}

/*
. Process an argument without the preceding option
. Convert a string to integer */
static int ffpsarg_schemval(ffparser_schem *ps)
{
	ffparser *p = ps->p;
	const ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);

	if (p->type == FFPSARG_INPUTVAL
		|| (p->type == FFPSARG_VAL && (ps->curarg->flags & FFPARS_FALONE))) {

		ps->curarg = NULL;
		if (ctx->args[0].name[0] != '\0')
			return FFPARS_EVALUNEXP;
		ps->curarg = &ctx->args[0];

	} else if (p->type == FFPSARG_KVAL && (ps->curarg->flags & FFPARS_FALONE))
		return FFPARS_EVALUNEXP;

	if ((ps->curarg->flags & FFPARS_FTYPEMASK) == FFPARS_TINT) {
		if (p->val.len != ffs_toint(p->val.ptr, p->val.len, &p->intval, FFS_INT64 | FFS_INTSIGN))
			return FFPARS_EBADVAL;

	} else if ((ps->curarg->flags & FFPARS_FTYPEMASK) == FFPARS_TFLOAT) {
		if (p->val.len != ffs_tofloat(p->val.ptr, p->val.len, &p->fltval, 0))
			return FFPARS_EBADVAL;
	}

	return 0;
}

static const ffpars_arg* _arg_any(const ffpars_ctx *ctx)
{
	const ffpars_arg *a;
	uint nargs = ctx->nargs;
	if ((ctx->args[nargs - 1].flags & FFPARS_FTYPEMASK) == FFPARS_TCLOSE)
		nargs--;

	uint first = (ctx->args[0].name[0] == '\0'); //skip "" argument
	if (nargs != first) {

		a = &ctx->args[first];
		if (a->name[0] == '*' && a->name[1] == '\0')
			return a;

		a = &ctx->args[nargs - 1];
		if (a->name[0] == '*' && a->name[1] == '\0')
			return a;
	}
	return NULL;
}

int ffpsarg_schemrun(ffparser_schem *ps)
{
	const ffpars_arg *arg;
	ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);
	const ffstr *val = &ps->p->val;
	uint f, i;
	int r;

	if (ps->p->ret >= 0)
		return ps->p->ret;

	if (ps->flags & FFPARS_SCHAVKEY) {
		ps->flags &= ~FFPARS_SCHAVKEY;
		if (ps->p->ret != FFPARS_VAL)
			return FFPARS_EVALEMPTY; //key without a value
	}

	switch (ps->p->ret) {

	case FFPARS_KEY:
		if (ps->p->type == FFPSARG_SHORT) {
			if (val->len != 1)
				return FFPARS_EUKNKEY; // bare "-" option

			arg = NULL;
			uint a = (byte)val->ptr[0];
			for (i = 0;  i != ctx->nargs;  i++) {
				uint ch1 = (ctx->args[i].flags & FFPARS_FBITMASK) >> 24;
				if (a == ch1) {
					arg = &ctx->args[i];
					break;
				}
			}

			if (arg == NULL)
				arg = _arg_any(ctx);

			else if (i < sizeof(ctx->used)*8
				&& ffbit_setarr(ctx->used, i) && !(arg->flags & FFPARS_FMULTI))
				arg = (void*)-1;

		} else {
			f = 0;
			if (ps->flags & FFPARS_KEYICASE)
				f |= FFPARS_CTX_FKEYICASE;
			arg = ffpars_ctx_findarg(ctx, val->ptr, val->len, FFPARS_CTX_FANY | FFPARS_CTX_FDUP | f);
		}

		if (arg == NULL)
			return FFPARS_EUKNKEY;
		else if (arg == (void*)-1)
			return FFPARS_EDUPKEY;
		ps->curarg = arg;

		if ((arg->flags & FFPARS_FALONE) || !ffsz_cmp(arg->name, "*")) {
			ps->p->intval = 1;
			ps->p->ret = FFPARS_VAL;
			if (FFPARS_VAL != (r = ffpars_schemrun(ps, ps->p->ret)))
				return r;
			return FFPARS_KEY;

		} else {
			ps->flags |= FFPARS_SCHAVKEY;
		}

		return FFPARS_KEY;

	case FFPARS_VAL:
		r = ffpsarg_schemval(ps);
		if (ffpars_iserr(r))
			return r;
		break;
	}

	r = ffpars_schemrun(ps, ps->p->ret);
	return r;
}
