/**
Copyright (c) 2013 Simon Zolin
*/


#include <FF/data/psarg.h>


#ifdef FF_WIN
void ffpsarg_init(ffpsarg *a, const char **argv, uint argc)
{
	ffsyschar *cl = GetCommandLine();
	ffarr_null(&a->cmdln);
	if (0 == ffstr_catfmt(&a->cmdln, "%q%Z", cl))
		return;
	a->cmdln.cap = a->cmdln.len - 1;
	a->cmdln.len = 0;
}

/** Get next argument from command line.
Data in buffer 'd' is overwritten when quotes are used:
 "a b c" -> a b c
 a"b"c -> abc
Return number of bytes read;  -1: done. */
static int psarg_next(char *d, size_t len, ffstr *dst)
{
	enum { I_WS, I_TEXT, I_DQUOT, I_TEXT_COPY };
	ffstr arg = {0};
	uint i, st = I_WS;

	for (i = 0;  i != len;  i++) {

	int ch = d[i];

	switch (st) {

	case I_WS:
		if (ch == ' ')
			break;
		st = I_TEXT;
		arg.ptr = &d[i];
		// fall through

	case I_TEXT:
	case I_TEXT_COPY:
		if (ch == ' ') {
			i++;
			goto done;
		} else if (ch == '"') {
			st = I_DQUOT;
			break;
		}

		if (st == I_TEXT_COPY)
			arg.ptr[arg.len] = ch;
		arg.len++;
		break;

	case I_DQUOT:
		if (ch == '"') {
			st = I_TEXT_COPY;
			break;
		}
		arg.ptr[arg.len++] = ch;
		break;
	}
	}

	if (st == I_WS)
		return -1;

done:
	*dst = arg;
	return i;
}

const char* ffpsarg_next(ffpsarg *a)
{
	ffstr arg;
	int r = psarg_next(ffarr_end(&a->cmdln), a->cmdln.cap - a->cmdln.len, &arg);
	if (r < 0)
		return NULL;
	a->cmdln.len += r;
	arg.ptr[arg.len] = '\0';
	return arg.ptr;
}
#endif


enum ARG_IDX {
	iArgStart, iArgVal, iArgNextShortOpt, iArgDone
};

void ffpsarg_parseinit(ffpsarg_parser *p)
{
	ffmem_tzero(p);
	p->line = 1;
	p->state = iArgStart;
	p->type = 0;
}

void ffpsarg_parseclose(ffpsarg_parser *p)
{
	ffarr_free(&p->buf);
}

int ffpsarg_parse(ffpsarg_parser *p, const char *a, int *processed)
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

				eqch = ffsz_findc(a, '=');
				if (eqch != NULL) { //--arg1=val
					alen = eqch - a;
					p->ch = (uint)(eqch + FFSLEN("=") - (a - FFSLEN("--")));
					*processed = 0;
					st = iArgVal;

				} else {
					alen = ffsz_len(a);
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


int ffpsarg_scheminit(ffparser_schem *ps, ffpsarg_parser *p, const ffpars_ctx *ctx)
{
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(ctx) };
	ffpars_scheminit(ps, p, &top);

	ffpsarg_parseinit(p);
	if (FFPARS_OPEN != _ffpars_schemrun(ps, FFPARS_OPEN))
		return 1;

	return 0;
}

int ffpsarg_schemfin(ffparser_schem *ps)
{
	int r;
	ffpsarg_parser *p = ps->p;
	p->ret = FFPARS_CLOSE;
	r = ffpsarg_schemrun(ps);
	if (r != FFPARS_CLOSE)
		return r;
	return 0;
}

/*
. Process an argument without the preceding option
. Convert a string to number */
static int ffpsarg_schemval(ffparser_schem *ps)
{
	ffpsarg_parser *p = ps->p;
	const ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);

	if (p->type == FFPSARG_INPUTVAL
		|| (p->type == FFPSARG_VAL && (ps->curarg->flags & FFPARS_FALONE))) {

		ps->curarg = NULL;
		if (ctx->args[0].name[0] != '\0')
			return FFPARS_EVALUNEXP;
		ps->curarg = &ctx->args[0];

	} else if (p->type == FFPSARG_KVAL
		&& (ps->curarg->flags & FFPARS_FALONE)
		&& (ps->curarg->flags & FFPARS_FTYPEMASK) == FFPARS_TBOOL)
		return FFPARS_EVALUNEXP;

	return ffpars_arg_process(ps->curarg, &p->val, ctx->obj, ps);
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
	ffpsarg_parser *p = ps->p;
	const ffpars_arg *arg;
	ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);
	const ffstr *val = &p->val;
	uint f, i;
	int r;

	if (p->ret >= 0)
		return p->ret;

	if (ps->ctxs.len == 0)
		return FFPARS_ECONF;

	if (ps->flags & _FFPARS_SCALONE) {
		ps->flags &= ~_FFPARS_SCALONE;
		if (p->ret == FFPARS_KEY) {
			ps->flags &= ~FFPARS_SCHAVKEY;
			if (!ffpars_arg_isfunc(ps->curarg))
				return FFPARS_ECONF;
			r = ps->curarg->dst.f_str(ps, ctx->obj, NULL);
			if (r != 0)
				return r;
		}
	}

	if (ps->flags & FFPARS_SCHAVKEY) {
		ps->flags &= ~FFPARS_SCHAVKEY;
		if (p->ret != FFPARS_VAL)
			return FFPARS_EVALEMPTY; //key without a value
	}

	switch (p->ret) {

	case FFPARS_KEY:
		if (p->type == FFPSARG_SHORT) {
			if (val->len != 1)
				return FFPARS_EUKNKEY; // bare "-" option

			arg = NULL;
			uint a = (byte)val->ptr[0];
			for (i = 0;  i != ctx->nargs;  i++) {
				uint ch1 = (ctx->args[i].flags >> 24) & 0xff;
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

		if (arg->flags & FFPARS_FALONE) {
			if ((ps->curarg->flags & FFPARS_FTYPEMASK) == FFPARS_TBOOL) {
				int64 intval = 1;
				r = _ffpars_arg_process2(ps->curarg, &intval, ctx->obj, ps);
				if (r != 0)
					return r;
				return FFPARS_KEY;
			}
			else
				ps->flags |= _FFPARS_SCALONE;
		}

		if (ffsz_eq(arg->name, "*")) {
			r = ffpars_arg_process(ps->curarg, &p->val, ctx->obj, ps);
			if (r != 0)
				return r;

		} else {
			ps->flags |= FFPARS_SCHAVKEY;
		}

		r = FFPARS_KEY;
		break;

	case FFPARS_VAL:
		r = ffpsarg_schemval(ps);
		if (r != 0)
			return r;
		r = FFPARS_VAL;
		break;

	case FFPARS_OPEN:
		r = _ffpars_schemrun(ps, FFPARS_OPEN);
		break;
	case FFPARS_CLOSE:
		r = _ffpars_schemrun(ps, FFPARS_CLOSE);
		break;
	default:
		return FFPARS_EINTL;
	}

	return r;
}
