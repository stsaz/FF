/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/data/conf.h>


enum FFCONF_SCF {
	// enum FFPARS_SCHEMFLAG{}

	FFPARS_SCCTX_ANY = 2,
	FFPARS_SCCTX = 4,
	SCF_RESETCTX = 8,
};

static int hdlEsc(ffparser *p, int *st, int ch);
static int unesc(char *dst, size_t cap, const char *text, size_t len);
static int hdlQuote(ffparser *p, int *st, int *nextst, const char *data);

enum CONF_IDX {
	iKeyFirst = 16, iKey, iKeyBare, iKeySplit
	, iValSplit, iValFirst, iVal, iValBare
	, iNewCtx, iRmCtx
	, iQuot, iQuotEsc
};

static int hdlQuote(ffparser *p, int *st, int *nextst, const char *data)
{
	int r = 0;

	switch (*data) {
	case '"':
		if (*nextst == iKey) {
			*st = iKeySplit;
			break;

		} else
			p->type = (p->type == FFCONF_TVAL || p->type == FFCONF_TVALNEXT)
				? FFCONF_TVALNEXT : FFCONF_TVAL; //FFCONF_TVAL, FFCONF_TVALNEXT, ...

		*st = iValSplit;
		r = FFPARS_VAL;
		break;

	case '\\':
		*st = iQuotEsc; //wait for escape sequence
		break;

	case '\n':
		r = FFPARS_EBADCHAR; // newline within quoted value
		break;

	default:
		r = _ffpars_addchar2(p, data);
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

	if ((uint)p->esc[0] >= FFSLEN("xXX"))
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
	p->ret = FFPARS_OPEN;
	p->type = FFCONF_TOBJ;
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
		case iKeyFirst:
			ffpars_cleardata(p);
			st = iKey;
			// break

		case iKey:
			if (ch == '"') {
				st = iQuot;
				nextst = iKey;

			} else if (ch == '}') {
				st = iRmCtx;
				p->type = FFCONF_TOBJ;
				r = FFPARS_CLOSE;
				ffstr_setcz(&p->val, "}");

			} else {
				st = iKeyBare;
				again = 1;
			}
			break;

		case iKeyBare:
			if (ch == '.') {
				p->type = FFCONF_TKEYCTX;
				r = FFPARS_KEY;
				st = iKeyFirst;
				break;
			}
			// break

		case iValBare:
			if (!ffchar_iswhitespace(ch) && ch != '/' && ch != '#')
				r = _ffpars_addchar2(p, data);
			else {
				if (st == iKeyBare)
					p->type = FFCONF_TKEY;
				else
					p->type = (p->type == FFCONF_TVAL || p->type == FFCONF_TVALNEXT)
						? FFCONF_TVALNEXT : FFCONF_TVAL; //FFCONF_TVAL, FFCONF_TVALNEXT, ...

				r = (st == iKeyBare ? FFPARS_KEY : FFPARS_VAL);
				st = iValSplit;
				data--;
			}
			break;

		case iVal:
			if (ch == '"') {
				st = iQuot;
				nextst = iVal;

			} else if (ch == '{') {
				st = iNewCtx;
				p->type = FFCONF_TOBJ;
				nextst = iValSplit;
				r = FFPARS_OPEN;
				ffstr_setcz(&p->val, "{");

			} else {
				st = iValBare;
				again = 1;
			}
			break;

		case iKeySplit:
			r = FFPARS_KEY;
			if (ch == '.') {
				p->type = FFCONF_TKEYCTX;
				st = iKeyFirst;
				break;
			}
			data--;
			p->ch--;
			p->type = FFCONF_TKEY;
			st = iValSplit;
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
		case iQuot:
			r = hdlQuote(p, &st, &nextst, data);
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
			p->type = FFCONF_TOBJ;
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

	if (r == FFPARS_MORE)
		r = ffpars_savedata(p);

	p->state = st;
	p->nextst = nextst;
	*len = data - datao;
	p->ret = (char)r;
	return r;
}


/** Escape string:
. \r \n \t \\ " -> \\?
. non-printable -> \x??
*/
static ssize_t ffs_escape_conf_str(char *dst, size_t cap, const char *data, size_t len)
{
	if (dst == NULL) {
		size_t n = 0;
		for (size_t i = 0;  i != len;  i++) {
			uint ch = (byte)data[i];

			if (!ffbit_testarr(ffcharmask_nobslash_esc, ch)
				|| ch == '"')
				n += FFSLEN("\\?") - 1;

			else if (!ffbit_testarr(ffcharmask_printable, ch))
				n += FFSLEN("\\x??") - 1;
		}

		return len + n;
	}

	const char *end = dst + cap;
	const char *dsto = dst;

	for (size_t i = 0;  i != len;  i++) {
		uint ch = (byte)data[i];

		if (!ffbit_testarr(ffcharmask_nobslash_esc, ch)
			|| ch == '"') {

			if (dst + FFSLEN("\\?") > end)
				return -(ssize_t)i;

			char *d = ffmemchr(ff_escbyte, ch, FFCNT(ff_escbyte));
			FF_ASSERT(d != NULL);

			*dst++ = '\\';
			*dst++ = ff_escchar[d - ff_escbyte];

		} else if (!ffbit_testarr(ffcharmask_printable, ch)) {

			if (dst + FFSLEN("\\x??") > end)
				return -(ssize_t)i;

			*dst++ = '\\';
			*dst++ = 'x';
			dst += ffs_hexbyte(dst, ch, ffHEX);

		} else {

			if (dst == end)
				return -(ssize_t)i;
			*dst++ = ch;
		}
	}

	return dst - dsto;
}

int ffconf_write(ffconfw *c, const char *data, size_t len, uint flags)
{
	ffstr d;
	ffstr_set(&d, data, len);
	ssize_t r = 0;
	ffbool esc = 0;

	if (flags == FFCONF_TKEY) {
		if (data == NULL)
			return 0;
		if (len == 0)
			return -1;
		r += FFSLEN("\n");
	} else if (flags == FFCONF_TVAL)
		r += FFSLEN(" ");

	if (len == 0 || d.ptr + d.len != ffs_skip_mask(d.ptr, d.len, ffcharmask_name)) {
		r += ffs_escape_conf_str(NULL, 0, d.ptr, d.len);
		r += FFSLEN("\"\"");
		esc = 1;
	} else {
		r += d.len;
	}

	if (NULL == ffarr_grow(&c->buf, r, 256 | FFARR_GROWQUARTER))
		return -1;
	char *dst = ffarr_end(&c->buf);

	if (flags == FFCONF_TKEY)
		*dst++ = '\n';
	else if (flags == FFCONF_TVAL)
		*dst++ = ' ';

	if (esc) {
		*dst++ = '"';
		dst += ffs_escape_conf_str(dst, ffarr_unused(&c->buf), d.ptr, d.len);
		*dst++ = '"';

	} else {
		dst = ffmem_copy(dst, d.ptr, d.len);
	}

	c->buf.len = dst - c->buf.ptr;
	return 0;
}

void ffconf_wdestroy(ffconfw *c)
{
	ffarr_free(&c->buf);
}


int ffconf_scheminit(ffparser_schem *ps, ffparser *p, const ffpars_ctx *ctx)
{
	int r;
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(ctx) };
	ffpars_scheminit(ps, p, &top);

	if (0 != ffconf_parseinit(p))
		return FFPARS_ESYS;

	r = ffpars_schemrun(ps, FFPARS_OPEN);
	if (r != FFPARS_OPEN)
		return r;

	ps->curarg = NULL;
	return 0;
}

int ffconf_schemfin(ffparser_schem *ps)
{
	if (ps->ctxs.len == 1) {
		int r;

		ps->p->ret = FFPARS_CLOSE;
		ps->p->type = FFCONF_TOBJ;
		r = ffconf_schemrun(ps);
		if (r != FFPARS_CLOSE)
			return r;
		return 0;
	}
	return FFPARS_ENOBRACE;
}

/* Convert value of type string into integer or boolean, fail if can't.
For a named object store the last parsed value. */
static int ffconf_schemval(ffparser_schem *ps)
{
	ffparser *p = ps->p;
	ffstr v = ps->p->val;
	int t;
	int r = 0;

	if (p->type == FFCONF_TVALNEXT && !(ps->curarg->flags & FFPARS_FLIST))
		return FFPARS_EVALUNEXP; //value has been specified already

	t = ps->curarg->flags & FFPARS_FTYPEMASK;

	switch(t) {
	case FFPARS_TSTR:
	case FFPARS_TCHARPTR:
	case FFPARS_TSIZE:
	case FFPARS_TENUM:
	case FFPARS_TCLOSE:
		break;

	case FFPARS_TINT:
		if (v.len < FFINT_MAXCHARS
			&& v.len == ffs_toint(v.ptr, v.len, &p->intval, FFS_INT64 | FFS_INTSIGN))
			{}
		else
			r = FFPARS_EBADINT;
		break;

	case FFPARS_TFLOAT:
		if (v.len != ffs_tofloat(v.ptr, v.len, &p->fltval, 0))
			r = FFPARS_EBADVAL;
		break;

	case FFPARS_TBOOL:
		if (ffstr_ieqcz(&v, "true")) {
			p->intval = 1;

		} else if (ffstr_ieqcz(&v, "false")) {
			p->intval = 0;

		} else
			r = FFPARS_EBADBOOL;
		break;

	case FFPARS_TOBJ:
		if (!(ps->curarg->flags & FFPARS_FOBJ1))
			return FFPARS_EVALTYPE;

		if (v.len == 0 && (ps->curarg->flags & FFPARS_FNOTEMPTY))
			return FFPARS_EVALEMPTY;

		{
			// save the value
			ffstr *s = &ps->vals[0];
			char *ptr = ffmem_realloc(s->ptr, v.len);
			if (ptr == NULL)
				return FFPARS_ESYS;
			s->ptr = ptr;

			memcpy(s->ptr, v.ptr, v.len);
			s->len = v.len;
			r = -1;
		}
		if (!ffsz_cmp(ps->curarg->name, "*"))
			ps->flags |= FFPARS_SCCTX_ANY;
		else
			ps->flags |= FFPARS_SCCTX;
		break;

	default:
		return FFPARS_EVALTYPE;
	}

	return r;
}

int ffconf_schemrun(ffparser_schem *ps)
{
	const ffpars_arg *arg;
	ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);
	const ffstr *val = &ps->p->val;
	uint f;
	int r;

	if (ps->p->ret >= 0)
		return ps->p->ret;

	if (0 != (r = ffpars_skipctx(ps)))
		return r;

	if (ps->flags & FFPARS_SCHAVKEY) {
		ps->flags &= ~FFPARS_SCHAVKEY;
		if (ps->p->ret != FFPARS_VAL)
			return FFPARS_EVALEMPTY; //key without a value

	} else if (ps->flags & FFPARS_SCCTX) {
		ps->flags &= ~FFPARS_SCCTX;
		if (ps->p->ret != FFPARS_OPEN)
			return FFPARS_EVALEMPTY; //expecting context open
	}

	switch (ps->p->ret) {
	case FFPARS_KEY:
		f = 0;
		if (ps->flags & FFPARS_KEYICASE)
			f |= FFPARS_CTX_FKEYICASE;
		if (ps->p->type == FFCONF_TKEYCTX)
			ps->flags |= SCF_RESETCTX;
		arg = ffpars_ctx_findarg(ctx, val->ptr, val->len, FFPARS_CTX_FANY | FFPARS_CTX_FDUP | f);
		if (arg == NULL)
			return FFPARS_EUKNKEY;
		else if (arg == (void*)-1)
			return FFPARS_EDUPKEY;
		ps->curarg = arg;
		int t = arg->flags & FFPARS_FTYPEMASK;

		if (!ffsz_cmp(arg->name, "*")) {

			if (t == FFPARS_TOBJ) {
				r = ffconf_schemval(ps);
				if (ffpars_iserr(r))
					return r;

				ps->p->ret = FFPARS_OPEN;
				break;
			}

			/* The first word in a row is KEY, but we handle it like a value, i.e. call "ffpars_val.f_str(..., key_name)".
			ctx {
				KEY val val val
			} */
			r = ffpars_schemrun(ps, FFPARS_VAL);
			return r;

		} else {
			if (t != FFPARS_TOBJ && t != FFPARS_TARR)
				ps->flags |= FFPARS_SCHAVKEY;
		}

		return FFPARS_KEY;

	case FFPARS_VAL:
		r = ffconf_schemval(ps);
		if (ffpars_iserr(r))
			return r;
		if (r != 0)
			return FFPARS_VAL;
		break;
	}

	r = ffpars_schemrun(ps, ps->p->ret);

	if ((ps->flags & SCF_RESETCTX) && r == FFPARS_VAL) {
		//clear context after the whole line "ctx1.key value" is processed
		ps->ctxs.len = 1;
		ps->curarg = NULL;
		ps->flags &= ~SCF_RESETCTX;
	}

	return r;
}
