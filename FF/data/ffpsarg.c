/**
Copyright (c) 2013 Simon Zolin
*/


#include <FF/data/psarg.h>


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
	ps->onval = &ffpsarg_schemval;

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
	r = ffpars_schemrun(ps, FFPARS_CLOSE);
	if (r != FFPARS_CLOSE)
		return r;
	return 0;
}

/*
. Process an argument without the preceding option
. Convert a string to integer */
int ffpsarg_schemval(ffparser_schem *ps, void *obj, void *dst)
{
	ffparser *p = ps->p;
	uint i;
	const ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);

	if (ps->p->ret == FFPARS_KEY && ps->p->type == FFPSARG_SHORT) {
		uint a;

		if (ps->p->val.len != 1)
			return FFPARS_EUKNKEY; // bare "-" option

		a = (byte)ps->p->val.ptr[0];
		for (i = 0;  i < ctx->nargs;  i++) {
			uint ch1 = (ctx->args[i].flags & FFPARS_FBITMASK) >> 24;
			if (a == ch1) {
				ps->curarg = &ctx->args[i];
				break;
			}
		}

		return FFPARS_DONE;
	}

	if (p->ret != FFPARS_VAL)
		return FFPARS_OK;

	if (p->type == FFPSARG_INPUTVAL
		|| (p->type == FFPSARG_VAL && (ps->curarg->flags & FFPARS_FALONE))) {

		ps->curarg = NULL;
		for (i = 0;  i < ctx->nargs;  i++) {
			if (ctx->args[i].name[0] == '\0') {
				ps->curarg = &ctx->args[i];
				break;
			}
		}

		if (ps->curarg == NULL)
			return FFPARS_EVALUNEXP;

	} else if (p->type == FFPSARG_KVAL && (ps->curarg->flags & FFPARS_FALONE))
		return FFPARS_EVALUNEXP;

	if ((ps->curarg->flags & FFPARS_FTYPEMASK) == FFPARS_TINT) {
		if (p->val.len != ffs_toint(p->val.ptr, p->val.len, &p->intval, FFS_INT64 | FFS_INTSIGN))
			return FFPARS_EBADVAL;
	}

	return FFPARS_OK;
}
