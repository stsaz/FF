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

static int hdlEsc(ffconf *p, int *st, int ch);
static int unesc(char *dst, size_t cap, const char *text, size_t len);
static int hdlQuote(ffconf *p, int *st, int *nextst, const char *data);
static int val_add(ffarr *buf, const char *s, size_t len);
static int val_store(ffarr *buf, const char *s, size_t len);

enum CONF_IDX {
	FFPARS_IWSPACE, I_ERR,
	FFPARS_ICMT_BEGIN, FFPARS_ICMT_LINE, FFPARS_ICMT_MLINE, FFPARS_ICMT_MLINESLASH,
	iKeyFirst, iKey, iKeyBare, iKeySplit
	, iValSplit, iValFirst, iVal, iValBare
	, iNewCtx, iRmCtx
	, iQuot, iQuotEsc
};

static int hdlQuote(ffconf *p, int *st, int *nextst, const char *data)
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
		p->esc[0] = 0;
		*st = iQuotEsc; //wait for escape sequence
		break;

	case '\n':
		r = FFPARS_EBADCHAR; // newline within quoted value
		break;

	default:
		r = val_add(&p->buf, data, 1);
	}

	return r;
}

static const char escchar[7] = "\"\\bfrnt";
static const char escbyte[7] = "\"\\\b\f\r\n\t";

/* 1: done
0: more
-1: error */
static int unesc(char *dst, size_t cap, const char *text, size_t len)
{
	char *d;
	if (cap == 0 || len == 0)
		return 0;

	d = ffmemchr(escchar, text[0], FFCNT(escchar));

	if (d != NULL) {
		*dst = escbyte[d - escchar];
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

static int hdlEsc(ffconf *p, int *st, int ch)
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
		r = val_store(&p->buf, buf, r);
		if (r != 0)
			return r; //allocation error
		p->esc[0] = 0;
		*st = iQuot;
	}

	return 0;
}

static int hdl_cmt(int *st, int ch)
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

void ffconf_parseinit(ffconf *p)
{
	char *ctx;
	ffmem_tzero(p);
	p->line = 1;
	p->nextst = iKey;

	ctx = ffarr_push(&p->ctxs, char);
	if (ctx == NULL) {
		p->state = I_ERR;
		return;
	}

	*ctx = FFPARS_OPEN;
	p->ret = FFPARS_OPEN;
	p->type = FFCONF_TOBJ;
}

void ffconf_parseclose(ffconf *p)
{
	ffarr_free(&p->buf);
	ffarr_free(&p->ctxs);
}

/** Get full error message. */
const char* ffconf_errmsg(ffconf *p, int r, char *buf, size_t cap)
{
	char *end = buf + cap;
	size_t n = ffs_fmt(buf, end, "%u:%u near \"%S\" : %s%Z"
		, p->line, p->ch, &p->val, ffpars_errstr(r));
	if (r == FFPARS_ESYS) {
		if (n != 0)
			n--;
		n += ffs_fmt(buf + n, end, " : %E%Z", fferr_last());
	}
	return (n != 0) ? buf : "";
}

static void conf_cleardata(ffconf *p)
{
	ffstr_null(&p->val);
	ffarr_free(&p->buf);
}

static int val_store(ffarr *buf, const char *s, size_t len)
{
	if (NULL == ffarr_grow(buf, len, 256 | FFARR_GROWQUARTER))
		return FFPARS_ESYS;
	ffarr_append(buf, s, len);
	return 0;
}

static int val_add(ffarr *buf, const char *s, size_t len)
{
	if (buf->cap != 0)
		return val_store(buf, s, len);

	if (buf->len == 0)
		buf->ptr = (char*)s;
	buf->len += len;
	return 0;
}

int ffconf_parse(ffconf *p, const char *data, size_t *len)
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
				conf_cleardata(p);
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
			r = hdl_cmt(&st, ch);
			break;

//KEY-VALUE
		case iKeyFirst:
			conf_cleardata(p);
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
				r = val_add(&p->buf, data, 1);
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
			conf_cleardata(p);
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

		case I_ERR:
			r = FFPARS_ESYS;
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

	if (r == FFPARS_MORE && p->buf.len != 0)
		r = val_store(&p->buf, NULL, 0);

	ffstr_set2(&p->val, &p->buf);
	p->state = st;
	p->nextst = nextst;
	*len = data - datao;
	p->ret = (char)r;
	FFDBG_PRINTLN(FFDBG_PARSE | 10, "line:%u  r:%d  val:%S  level:%u"
		, p->line, r, &p->val, p->ctxs.len);
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

			char *d = ffmemchr(escbyte, ch, FFCNT(escbyte));
			FF_ASSERT(d != NULL);

			*dst++ = '\\';
			*dst++ = escchar[d - escbyte];

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

enum { W_FIRST, W_NEXT, W_ERR };

/** Write into c->buf (dont't allocate).
Return # of written bytes;  0 on error;  <0 if need more space. */
static ssize_t _ffconf_write(ffconfw *c, const void *data, ssize_t len, uint flags)
{
	char *dst = ffarr_end(&c->buf), *end = ffarr_edge(&c->buf);
	ffstr d;
	size_t r = 0;
	ffbool text = 0;
	uint pretty_lev = ((c->flags | flags) & FFCONF_PRETTY) ? c->level : 0;
	uint t = flags & 0xff;
	ffconfw bkp = *c;

	if (c->state == W_ERR)
		return 0;

	if (len == FFCONF_STRZ)
		len = ffsz_len(data);
	ffstr_set(&d, data, len);

	switch (t) {

	case FFCONF_TKEY:
		FF_ASSERT(len > 0);
		if (len == 0) {
			c->state = W_ERR;
			return 0;
		}
		if (c->state != W_FIRST)
			dst = ffs_copyc(dst, end, '\n');
		if (pretty_lev != 0)
			dst += ffs_fmt(dst, end, "%*c", (size_t)pretty_lev, '\t');
		r = FFSLEN("\n") + pretty_lev;
		text = 1;
		c->state = W_NEXT;
		break;

	case FFCONF_TVAL:
		dst = ffs_copyc(dst, end, ' ');
		r = FFSLEN(" ");
		if (len == FFCONF_INT64) {
			dst += ffs_fromint(*(int64*)data, dst, end - dst, 0);
			r += FFINT_MAXCHARS;
		} else
			text = 1;
		break;

	case FFCONF_TOBJ:
		if (len == FFCONF_OPEN) {
			dst = ffs_copyc(dst, end, ' ');
			dst = ffs_copyc(dst, end, '{');
			c->level++;
		} else if (len == FFCONF_CLOSE) {
			dst = ffs_copyc(dst, end, '\n');
			if (pretty_lev > 1)
				dst += ffs_fmt(dst, end, "%*c", (size_t)pretty_lev - 1, '\t');
			dst = ffs_copyc(dst, end, '}');
			FF_ASSERT(c->level != 0);
			c->level--;
		}
		r = pretty_lev + FFSLEN(" {");
		break;

	case FFCONF_TCOMMENTSHARP:
		FF_ASSERT(len >= 0);
		if (c->state != W_FIRST)
			dst = ffs_copyc(dst, end, '\n');
		dst += ffs_fmt(dst, end, "%*c# ", (size_t)pretty_lev, '\t');
		dst = ffs_copy(dst, end, d.ptr, d.len);
		r = pretty_lev + FFSLEN("\n# ") + d.len;
		c->state = W_NEXT;
		break;

	case FFCONF_FIN:
		if (c->level != 0) {
			c->state = W_ERR;
			return 0;
		}
		dst = ffs_copyc(dst, end, '\n');
		r = FFSLEN("\n");
		c->state = W_FIRST;
		break;

	default:
		c->state = W_ERR;
		return 0;
	}

	if (text) {
		if (!(flags & FFCONF_ASIS)
			&& (d.len == 0 // empty value must be within quotes ("")
				|| d.ptr + d.len != ffs_skip_mask(d.ptr, d.len, ffcharmask_name))) {
			char *s = dst;
			int rr = 0;
			dst = ffs_copyc(dst, end, '"');
			if (dst != NULL)
				rr = ffs_escape_conf_str(dst, end - dst, d.ptr, d.len);
			if (rr >= 0)
				dst += rr;
			else
				dst = end;
			dst = ffs_copyc(dst, end, '"');
			if (dst == end)
				r += FFSLEN("\"\"") + ffs_escape_conf_str(NULL, 0, d.ptr, d.len);
			else
				r += dst - s;
		} else {
			dst = ffs_copy(dst, end, d.ptr, d.len);
			r += d.len;
		}
	}

	if (dst == ffarr_edge(&c->buf)) {
		r = -(r + 1);
		c->level = bkp.level;
		c->state = bkp.state;
	} else {
		r = dst - ffarr_end(&c->buf);
		c->buf.len = dst - c->buf.ptr;
	}
	return r;
}

size_t ffconf_write(ffconfw *c, const void *data, ssize_t len, uint flags)
{
	ssize_t r;

	r = _ffconf_write(c, data, len, flags);
	if (r >= 0)
		return r;
	r = -r;

	if (!((c->flags | flags) & FFCONF_GROW))
		return 0;

	if (NULL == ffarr_grow(&c->buf, r, 256 | FFARR_GROWQUARTER)) {
		c->state = W_ERR;
		return 0;
	}

	r = _ffconf_write(c, data, len, flags);
	FF_ASSERT(r > 0);
	if (r < 0)
		r = 0;
	return r;
}

void ffconf_wdestroy(ffconfw *c)
{
	if (c->flags & FFCONF_GROW)
		ffarr_free(&c->buf);
}


int ffconf_scheminit(ffparser_schem *ps, ffconf *p, const ffpars_ctx *ctx)
{
	int r;
	const ffpars_arg top = { NULL, FFPARS_TOBJ | FFPARS_FPTR, FFPARS_DST(ctx) };
	ffpars_scheminit(ps, p, &top);

	ffconf_parseinit(p);

	r = _ffpars_schemrun(ps, FFPARS_OPEN);
	if (r != FFPARS_OPEN)
		return r;

	ps->curarg = NULL;
	return 0;
}

int ffconf_schemfin(ffparser_schem *ps)
{
	ffconf *c = ps->p;
	if (ps->ctxs.len == 1) {
		int r;

		c->ret = FFPARS_CLOSE;
		c->type = FFCONF_TOBJ;
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
	ffconf *p = ps->p;
	ffstr v = p->val;
	int t;
	int r = 0;

	if (p->type == FFCONF_TVALNEXT) {
		if (!(ps->curarg->flags & FFPARS_FLIST))
			return FFPARS_EVALUNEXP; //value has been specified already
		ps->list_idx++;
	} else if (p->type == FFCONF_TVAL)
		ps->list_idx = 0;

	t = ps->curarg->flags & FFPARS_FTYPEMASK;

	switch(t) {
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

			ffmemcpy(s->ptr, v.ptr, v.len);
			s->len = v.len;
		}
		if (!ffsz_cmp(ps->curarg->name, "*"))
			ps->flags |= FFPARS_SCCTX_ANY;
		else
			ps->flags |= FFPARS_SCCTX;
		break;

	default:
		r = ffpars_arg_process(ps->curarg, &v, ffarr_back(&ps->ctxs).obj, ps);
	}

	if (ps->flags & SCF_RESETCTX) {
		//clear context after the whole line "ctx1.key value" is processed
		ps->ctxs.len = 1;
		ps->curarg = NULL;
		ps->flags &= ~SCF_RESETCTX;
	}

	return r;
}

int ffconf_schemrun(ffparser_schem *ps)
{
	ffconf *c = ps->p;
	const ffpars_arg *arg;
	ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);
	uint f;
	int r;

	if (c->ret >= 0)
		return c->ret;

	if (ps->ctxs.len == 0)
		return FFPARS_ECONF;

	if (0 != (r = _ffpars_skipctx(ps, c->ret)))
		return r;

	if (ps->flags & FFPARS_SCHAVKEY) {
		ps->flags &= ~FFPARS_SCHAVKEY;
		if (c->ret != FFPARS_VAL)
			return FFPARS_EVALEMPTY; //key without a value

	} else if (ps->flags & FFPARS_SCCTX) {
		ps->flags &= ~FFPARS_SCCTX;
		if (c->ret != FFPARS_OPEN)
			return FFPARS_EVALEMPTY; //expecting context open
	}

	switch (c->ret) {
	case FFPARS_KEY:
		f = 0;
		if (ps->flags & FFPARS_KEYICASE)
			f |= FFPARS_CTX_FKEYICASE;
		if (c->type == FFCONF_TKEYCTX)
			ps->flags |= SCF_RESETCTX;
		arg = ffpars_ctx_findarg(ctx, c->val.ptr, c->val.len, FFPARS_CTX_FANY | FFPARS_CTX_FDUP | f);
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

				c->ret = FFPARS_OPEN;
				r = _ffpars_schemrun(ps, FFPARS_OPEN);
				return r;
			}

			if (ps->curarg->flags & FFPARS_FWITHKEY) {
				ffstr v = c->val;
				ffstr *s = &ps->vals[0];
				char *ptr = ffmem_realloc(s->ptr, v.len);
				if (ptr == NULL)
					return FFPARS_ESYS;
				s->ptr = ptr;

				ffmemcpy(s->ptr, v.ptr, v.len);
				s->len = v.len;
				return 0;
			}

			/* The first word in a row is KEY, but we handle it like a value, i.e. call "ffpars_val.f_str(..., key_name)".
			ctx {
				KEY val val val
			} */
			r = ffpars_arg_process(ps->curarg, &c->val, ctx->obj, ps);
			if (r != 0)
				return r;

		} else {
			if (t != FFPARS_TOBJ && t != FFPARS_TARR)
				ps->flags |= FFPARS_SCHAVKEY;
		}

		r = FFPARS_KEY;
		break;

	case FFPARS_VAL:
		r = ffconf_schemval(ps);
		if (r != 0)
			return r;
		r = FFPARS_VAL;
		break;

	case FFPARS_OPEN:
		r = _ffpars_schemrun(ps, FFPARS_OPEN);
		ps->vals[0].len = 0;
		break;
	case FFPARS_CLOSE:
		r = _ffpars_schemrun(ps, FFPARS_CLOSE);
		break;

	default:
		return FFPARS_EINTL;
	}

	return r;
}


int ffconf_loadfile(struct ffconf_loadfile *c)
{
	int r;
	size_t n;
	char *buf = NULL;
	ffstr s;
	fffd f = FF_BADFD;
	ffconf p;
	ffparser_schem ps;
	ffpars_ctx ctx = {0};

	if (c->bufsize == 0)
		c->bufsize = 4096;

	ffpars_setargs(&ctx, c->obj, c->args, c->nargs);
	r = ffconf_scheminit(&ps, &p, &ctx);
	if (r != 0)
		goto done;

	if (FF_BADFD == (f = fffile_open(c->fn, O_RDONLY))) {
		r = FFPARS_ESYS;
		goto done;
	}

	if (NULL == (buf = ffmem_alloc(c->bufsize))) {
		r = FFPARS_ESYS;
		goto done;
	}

	for (;;) {
		n = fffile_read(f, buf, c->bufsize);
		if (n == (size_t)-1) {
			r = FFPARS_ESYS;
			goto done;
		} else if (n == 0)
			break;
		ffstr_set(&s, buf, n);

		while (s.len != 0) {
			r = ffconf_parsestr(&p, &s);
			r = ffconf_schemrun(&ps);

			if (ffpars_iserr(r))
				goto done;
		}
	}

	r = ffconf_schemfin(&ps);

done:
	if (ffpars_iserr(r)) {
		ffconf_errmsg(&p, r, c->errstr, sizeof(c->errstr));
	}

	ffconf_parseclose(&p);
	ffpars_schemfree(&ps);
	ffmem_safefree(buf);
	FF_SAFECLOSE(f, FF_BADFD, fffile_close);
	return r;
}
