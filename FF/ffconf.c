/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/conf.h>
#include <FF/psarg.h>


static int hdlEsc(ffparser *p, int *st, int ch);
static int unesc(char *dst, size_t cap, const char *text, size_t len);
static int hdlQuote(ffparser *p, int *st, int *nextst, int ch);

enum CONF_IDX {
	iKeyFirst = 16, iKey, iKeyBare
	, iValSplit, iValFirst, iVal, iValBare
	, iNewCtx, iRmCtx
	, iQuot, iQuotFirst, iQuotEsc
};

static int hdlQuote(ffparser *p, int *st, int *nextst, int ch)
{
	int r = 0;

	switch (ch) {
	case '"':
		p->type = FFPARS_TSTR;
		*st = iValSplit;
		r = (*nextst == iKey ? FFPARS_KEY : FFPARS_VAL);
		break;

	case '\\':
		*st = iQuotEsc; //wait for escape sequence
		break;

	case '\n':
		r = FFPARS_EBADCHAR; // newline within quoted value
		break;

	default:
		r = _ffpars_addchar(p, ch);
	}

	return r;
}

/* 1: done
0: more
-1: error */
static int unesc(char *dst, size_t cap, const char *text, size_t len)
{
	char *d;
	if (cap == 0 || len == 0)
		return 0;

	d = ffmemchr(ff_escchar, text[0], FFCNT(ff_escchar));

	if (d != NULL) {
		*dst = ff_escbyte[d - ff_escchar];
		return 1;
	}

	if (text[0] == 'x') {
		if (len < FFSLEN("xXX"))
			return 0;

		if (FFSLEN("XX") == ffs_toint(text + 1, FFSLEN("XX"), dst, FFS_INT8 | FFS_INTHEX))
			return 1;
	}

	return -1;
}

static int hdlEsc(ffparser *p, int *st, int ch)
{
	char buf[8];
	int r;

	if (p->esc[0] >= FFSLEN("xXX"))
		return FFPARS_EESC; //too large escape sequence

	p->esc[(uint)++p->esc[0]] = (char)ch;
	r = unesc(buf, FFCNT(buf), p->esc + 1, p->esc[0]);
	if (r < 0)
		return FFPARS_EESC; //invalid escape sequence

	if (r != 0) {
		r = _ffpars_copyBuf(p, buf, r); //use dynamic buffer
		if (r != 0)
			return r; //allocation error
		p->esc[0] = 0;
		*st = iQuot;
	}

	return 0;
}

int ffconf_parseinit(ffparser *p)
{
	char *ctx;
	ffpars_init(p);
	p->nextst = iKey;

	ctx = ffarr_push(&p->ctxs, char);
	if (ctx == NULL)
		return 1;

	*ctx = FFPARS_OPEN;
	p->type = FFPARS_TOBJ;
	return 0;
}

int ffconf_parse(ffparser *p, const char *data, size_t *len)
{
	const char *datao = data;
	const char *end = data + *len;
	unsigned again = 0;
	int r = 0;
	int st = p->state;
	int nextst = p->nextst;

	for (;  data != end;  data++) {
		int ch = *data;
		p->ch++;

		switch (st) {
		case FFPARS_IWSPACE:
			if (ch == '\n') {
				ffpars_cleardata(p);
				nextst = iKey;

			} else if (!ffchar_iswhitespace(ch)) {
				if (ch == '/')
					st = FFPARS_ICMT_BEGIN;
				else if (ch == '#')
					st = FFPARS_ICMT_LINE;
				else {
					// met non-whitespace character
					st = nextst;
					nextst = -1;
					again = 1;
				}
			}
			break;

		case FFPARS_ICMT_LINE:
			if (ch == '\n') {
				st = FFPARS_IWSPACE;
				again = 1;
				break;
			}
			//break;
		case FFPARS_ICMT_BEGIN:
		case FFPARS_ICMT_MLINE:
		case FFPARS_ICMT_MLINESLASH:
			r = _ffpars_hdlCmt(&st, ch);
			break;

//KEY-VALUE
		case iKey:
			if (ch == '"') {
				st = iQuotFirst;
				nextst = iKey;

			} else if (ch == '}') {
				st = iRmCtx;
				p->type = FFPARS_TOBJ;
				r = FFPARS_CLOSE;
				ffstr_setcz(&p->val, "}");

			} else {
				st = iKeyBare;
				p->val.ptr = (char*)data;
				again = 1;
			}
			break;

		case iKeyBare:
		case iValBare:
			if (!ffchar_iswhitespace(ch) && ch != '/' && ch != '#')
				r = _ffpars_addchar(p, ch);
			else {
				p->type = FFPARS_TSTR;
				r = (st == iKeyBare ? FFPARS_KEY : FFPARS_VAL);
				st = iValSplit;
				data--;
			}
			break;

		case iVal:
			if (ch == '"') {
				st = iQuotFirst;
				nextst = iVal;

			} else if (ch == '{') {
				st = iNewCtx;
				p->type = FFPARS_TOBJ;
				nextst = iValSplit;
				r = FFPARS_OPEN;
				ffstr_setcz(&p->val, "{");

			} else {
				st = iValBare;
				p->val.ptr = (char*)data;
				again = 1;
			}
			break;

		case iValSplit:
			ffpars_cleardata(p);
			if (!ffchar_iswhitespace(ch) && ch != '/' && ch != '#') {
				r = FFPARS_EBADCHAR;
				break;
			}
			st = FFPARS_IWSPACE;
			nextst = iVal;
			again = 1;
			break;

//QUOTE
		case iQuotFirst:
			p->val.ptr = (char*)data;
			st = iQuot;
			//break;

		case iQuot:
			r = hdlQuote(p, &st, &nextst, ch);
			break;

		case iQuotEsc:
			r = hdlEsc(p, &st, ch);
			break;

//CTX
		case iNewCtx:
			{
				char *ctx = ffarr_push(&p->ctxs, char);
				if (ctx == NULL) {
					r = FFPARS_ESYS;
					break;
				}
				*ctx = p->type;
			}
			st = FFPARS_IWSPACE;
			again = 1;
			break;

		case iRmCtx:
			st = iValSplit;
			p->type = FFPARS_TOBJ;
			p->ctxs.len--; //ffarr_pop()
			again = 1;
			break;
		}

		if (r != 0) {
			if (r < 0)
				data++;
			break;
		}

		if (again) {
			again = 0;
			data--;
			p->ch--;

		} else if (ch == '\n') {
			p->line++;
			p->ch = 0;
		}
	}

	p->state = st;
	p->nextst = nextst;
	*len = data - datao;
	p->ret = (char)r;
	return r;
}


int ffconf_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx)
{
	int r;
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(ctx) };
	ffpars_scheminit(ps, p, &top);
	ps->onval = &ffconf_schemval;

	if (0 != ffconf_parseinit(p))
		return FFPARS_ESYS;

	r = ffpars_schemrun(ps, FFPARS_OPEN);
	if (r != FFPARS_OPEN)
		return r;

	return 0;
}

int ffconf_schemfin(ffparser_schem *ps)
{
	if (ps->ctxs.len == 1) {
		int r = ffpars_schemrun(ps, FFPARS_CLOSE);
		if (r != FFPARS_CLOSE)
			return r;
	}
	return 0;
}

/* Convert value of type string into integer or boolean, fail if can't.
For a named object store the last parsed value. */
int ffconf_schemval(ffparser_schem *ps, void *obj, void *dst)
{
	ffparser *p = ps->p;
	ffstr v = ps->p->val;
	int t;
	int r = FFPARS_OK;

	if (p->ret == FFPARS_KEY)
		return FFPARS_OK;

	t = ps->curarg->flags & FFPARS_FTYPEMASK;

	switch(t) {
	case FFPARS_TSTR:
	case FFPARS_TSIZE:
	case FFPARS_TENUM:
	case FFPARS_TCLOSE:
		break;

	case FFPARS_TINT:
		if (v.len < FFINT_MAXCHARS
			&& v.len == ffs_toint(v.ptr, v.len, &p->intval, FFS_INT64 | FFS_INTSIGN))
			p->type = FFPARS_TINT;
		else
			r = FFPARS_EVALTYPE;
		break;

	case FFPARS_TBOOL:
		if (ffstr_ieqcz(&v, "true")) {
			p->type = FFPARS_TBOOL;
			p->intval = 1;

		} else if (ffstr_ieqcz(&v, "false")) {
			p->type = FFPARS_TBOOL;
			p->intval = 0;

		} else
			r = FFPARS_EVALTYPE;
		break;

	case FFPARS_TOBJ:
		if (!(ps->curarg->flags & FFPARS_FOBJ1))
			return FFPARS_EVALTYPE;

		{
			// save the value
			ffstr *s = &ps->vals[0];
			s->ptr = ffmem_realloc(s->ptr, v.len);
			if (s->ptr == NULL)
				return FFPARS_ESYS;

			memcpy(s->ptr, v.ptr, v.len);
			s->len = v.len;
			r = FFPARS_DONE;
		}
		ps->flags |= 4; //FFPARS_SCCTX
		break;

	default:
		return FFPARS_EVALTYPE;
	}

	return r;
}


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
	p->type = FFPARS_TOBJ;
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
		st = iArgDone;
		p->type = FFPSARG_VAL;
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
	int r = ffpars_schemrun(ps, FFPARS_CLOSE);
	if (r != FFPARS_CLOSE)
		return r;
	return 0;
}

int ffpsarg_schemval(ffparser_schem *ps, void *obj, void *dst)
{
	if (ps->p->ret == FFPARS_KEY && ps->p->type == FFPSARG_SHORT) {
		uint i;
		const ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);
		uint a;

		if (ps->p->val.len != 1)
			return FFPARS_EUKNKEY; // bare "-" option

		a = (byte)ps->p->val.ptr[0];
		for (i = 0;  i < ctx->nargs;  i++) {
			uint ch1 = (ctx->args[i].flags & FFPARS_FBITMASK) >> 24;
			if (a == ch1) {
				*(uint*)dst = i;
				break;
			}
		}

		return FFPARS_DONE;
	}

	return FFPARS_OK;
}
