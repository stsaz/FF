/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/data/json.h>
#include <FF/data/utf8.h>


static const char *const _ffjson_stypes[] = {
	"null", "string", "integer", "boolean", "object", "array", "number"
};

const char * ffjson_stype(int type)
{
	return _ffjson_stypes[type];
}

static const char escchar[8] = "\"\\bfrnt/";
static const char escbyte[8] = "\"\\\b\f\r\n\t/";

int ffjson_unescapechar(uint *dst, const char *text, size_t len)
{
	char *d;
	if (len == 0)
		return 0;

	d = ffmemchr(escchar, text[0], FFCNT(escchar));

	if (d != NULL) {
		*dst = escbyte[d - escchar];
		return 1;
	}

	if (text[0] == 'u') {
		ushort sh;

		if (len < FFSLEN("uXXXX")) // \\uXXXX (UCS-2 BE)
			return 0;

		if (FFSLEN("XXXX") != ffs_toint(text + 1, FFSLEN("XXXX"), &sh, FFS_INT16 | FFS_INTHEX))
			return -1;

		*dst = sh;
		return FFSLEN("uXXXX");
	}

	return -1;
}

size_t ffjson_escape(char *dst, size_t cap, const char *s, size_t len)
{
	size_t i;
	const char *dstend = dst + cap;
	const char *dsto = dst;
	uint nEsc = FFSLEN("\\n");

	if (dst == NULL) {
		size_t n = 0;
		for (i = 0; i < len; ++i) {
			if (NULL != ffmemchr(escbyte, (byte)s[i], FFCNT(escbyte) - 1))
				n += nEsc;
			else if ((byte)s[i] < 0x20 || s[i] == 0x7f)
				n += FFSLEN("\\uXXXX");
			else
				n++;
		}
		return n;
	}

	for (i = 0; i < len; ++i) {
		char *d;

		if (dst == dstend)
			return 0;

		d = ffmemchr(escbyte, (byte)s[i], FFCNT(escbyte) - 1);
		if (d != NULL) {
			if (dst + nEsc > dstend)
				return 0;
			*dst++ = '\\';
			*dst++ = escchar[d - escbyte];

		} else if ((byte)s[i] < 0x20 || s[i] == 0x7f) {
			if (dst + FFSLEN("\\uXXXX") > dstend)
				return 0;
			dst = ffmem_copycz(dst, "\\u00");
			dst += ffs_hexbyte(dst, s[i], ffHEX);

		} else
			*dst++ = s[i];
	}

	return dst - dsto;
}


enum IDX {
	FFPARS_IWSPACE,
	FFPARS_ICMT_BEGIN, FFPARS_ICMT_LINE, FFPARS_ICMT_MLINE, FFPARS_ICMT_MLINESLASH,
	iValFirst, iVal, iValBare, iValBareNum, iAfterVal, iCloseBrace
	, iNewCtx, iRmCtx
	, iQuot, iQuotFirst, iQuotEsc
	, iKeyFirst, iKey, iAfterKey
};

enum FLAGS {
	F_ESC_UTF16_2 = 1, //UTF-16 escaped char must follow
};

void ffjson_parseinit(ffjson *p)
{
	ffmem_tzero(p);
	p->line = 1;
	p->nextst = iVal;
}

void ffjson_parseclose(ffjson *p)
{
	ffarr_free(&p->buf);
	ffarr_free(&p->ctxs);
}

static void json_cleardata(ffjson *p)
{
	ffarr_free(&p->buf);
	p->intval = 0;
}

void ffjson_parsereset(ffjson *p)
{
	json_cleardata(p);
	p->state = 0;
	p->nextst = iVal;
	p->type = 0;
	p->line = 1;
	p->ch = 0;
	p->ctxs.len = 0;
	p->flags = 0;
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

const char* ffjson_errmsg(ffjson *p, int r, char *buf, size_t cap)
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

static const char _ffjson_words[][6] = { "true", "false", "null" };
static const byte wordType[] = { FFJSON_TBOOL, FFJSON_TBOOL, FFJSON_TNULL };

static int valBareType(int ch)
{
	switch (ch) {
	case 't':
		return 1;
	case 'f':
		return 2;
	case 'n':
		return 3;
	}
	return 0;
}

/* true|false|null */
static int hdlValBare(ffjson *p, const char *data)
{
	int er;
	uint i = p->bareval_idx;

	er = val_add(&p->buf, data, 1);
	if (er != 0)
		return er;

	size_t len = p->buf.len - 1;
	if (*data != _ffjson_words[i][len])
		return FFPARS_EBADVAL;

	if (_ffjson_words[i][len + 1] == '\0') {
		p->type = wordType[i];
		p->intval = (i == 0);
		er = FFPARS_VAL;
	}

	return er;
}

/* integer or float */
static int hdlValBareNum(ffjson *p, const char *data)
{
	int er = 0, ch = *data;

	if (!(ffchar_isdigit(ch) || ch == '-' || ch == '+' || ch == '.' || ffchar_lower(ch) == 'e')) {
		ffstr v;
		ffstr_set2(&v, &p->buf);
		er = FFPARS_VAL;

		if (v.len == 0)
			er = FFPARS_ENOVAL;

		else if (v.len == ffs_toint(v.ptr, v.len, &p->intval, FFS_INT64 | FFS_INTSIGN))
			p->type = FFJSON_TINT;

		else if (v.len == ffs_tofloat(v.ptr, v.len, &p->fltval, 0))
			p->type = FFJSON_TNUM;

		else
			er = FFPARS_EBADVAL;
	}
	else {
		er = val_add(&p->buf, data, 1);
	}

	return er;
}

static int hdlQuote(ffjson *p, int *st, int *nextst, const char *data)
{
	int er = 0;

	switch (*data) {
	case '"':
		p->type = FFJSON_TSTR;
		*st = FFPARS_IWSPACE;

		if (*nextst == iKey) {
			*nextst = iAfterKey;
			er = FFPARS_KEY;
			break;
		}

		*nextst = iAfterVal;
		er = FFPARS_VAL;
		break;

	case '\\':
		p->esc_len = 0;
		*st = iQuotEsc; //wait for escape sequence
		break;

	case '\n':
		er = FFPARS_EBADCHAR; // newline within quoted value
		break;

	default:
		er = val_add(&p->buf, data, 1);
	}

	if ((p->flags & F_ESC_UTF16_2) && *st != iQuotEsc)
		return FFPARS_EESC; //UTF-16 escaped char must follow

	return er;
}

static int hdlEsc(ffjson *p, int *st, int ch)
{
	char buf[8];
	int r;
	uint uch;

	if (p->esc_len >= FFSLEN("uXXXX"))
		return FFPARS_EESC; //too large escape sequence

	p->esc[p->esc_len++] = (char)ch;
	r = ffjson_unescapechar(&uch, p->esc, p->esc_len);
	if (r < 0)
		return FFPARS_EESC; //invalid escape sequence
	else if (r == 0)
		return 0;

	if (ffutf16_basic(uch)) {

	} else if (ffutf16_highsurr(uch)) {
		if (p->flags & F_ESC_UTF16_2)
			return FFPARS_EESC; //2nd high surrogate
		p->flags |= F_ESC_UTF16_2;
		p->esc_hiword = uch;
		*st = iQuot;
		return 0;

	} else {
		if (!(p->flags & F_ESC_UTF16_2))
			return FFPARS_EESC; //low surrogate without previous high surrogate
		p->flags &= ~F_ESC_UTF16_2;
		uch = ffutf16_suppl(p->esc_hiword, uch);
		p->esc_hiword = 0;
	}

	r = ffutf8_encode1(buf, sizeof(buf), uch);
	r = val_store(&p->buf, buf, r);
	if (r != 0)
		return r; //allocation error
	*st = iQuot;

	return 0;
}

static int json_cmt(int *st, int ch)
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

static const byte ctxType[] = { FFJSON_TOBJ, FFJSON_TARR };
static const char ctxCharOpen[] = { '{', '[' };
static const char ctxCharClose[] = { '}', ']' };

int ffjson_parse(ffjson *p, const char *data, size_t *len)
{
	const char *datao = data;
	const char *end = data + *len;
	unsigned again = 0;
	int er = 0;
	int st = p->state;
	int nextst = p->nextst;

	for (;  data != end;  data++) {
		int ch = *data;
		p->ch++;

		switch (st) {
		case FFPARS_IWSPACE:
			if (!ffchar_iswhitespace(ch)) {
				if (ch == '/')
					st = FFPARS_ICMT_BEGIN;
				else {
					// met non-whitespace character
					st = nextst;
					nextst = -1;
					again = 1;
				}
			}
			break;

		case FFPARS_ICMT_BEGIN:
		case FFPARS_ICMT_LINE:
		case FFPARS_ICMT_MLINE:
		case FFPARS_ICMT_MLINESLASH:
			er = json_cmt(&st, ch);
			break;

//VALUE
		case iValFirst:
			if (ch == ']') {
				// empty array: "[]"
				st = iCloseBrace;
				again = 1;
				break;
			}
			st = iVal;
			//break;

		case iVal:
			switch (ch) {
			case '"':
				st = iQuotFirst;
				nextst = iVal;
				break;

			case '[':
			case '{':
				er = FFPARS_OPEN;
				st = iNewCtx;
				p->type = ctxType[ch == '['];
				nextst = (ch == '[' ? iValFirst : iKeyFirst);
				break;

			default:
				// value without quotes
				{
					int t = valBareType(ch);
					st = (t != 0 ? iValBare : iValBareNum);
					p->bareval_idx = t - 1;
				}
				again = 1;
			}
			break;

		case iValBare:
			er = hdlValBare(p, data);
			if (er == FFPARS_VAL) {
				st = FFPARS_IWSPACE;
				nextst = iAfterVal;
			}
			break;

		case iValBareNum:
			er = hdlValBareNum(p, data);
			if (er == FFPARS_VAL) {
				st = FFPARS_IWSPACE;
				nextst = iAfterVal;
				data--; // process this char again
			}
			break;

//QUOTE
		case iQuotFirst:
			st = iQuot;
			//break;

		case iQuot:
			er = hdlQuote(p, &st, &nextst, data);
			break;

		case iQuotEsc:
			er = hdlEsc(p, &st, ch);
			break;

//AFTER VALUE
		case iAfterVal:
			json_cleardata(p);

			if (p->ctxs.len == 0) {
				er = FFPARS_EBADCHAR; //document finished. no more entities expected
				break;
			}

			if (ch == ',') {
				st = FFPARS_IWSPACE;
				FF_ASSERT(p->ctxs.len != 0);
				nextst = (ffarr_back(&p->ctxs) == FFJSON_TARR ? iVal : iKey);
				break;
			}
			else if (ch != ']' && ch != '}') {
				er = FFPARS_EBADCHAR;
				break;
			}
			st = iCloseBrace;
			//break;

		case iCloseBrace:
			FF_ASSERT(p->ctxs.len != 0);
			if (ffarr_back(&p->ctxs) != ctxType[ch == ']']) {
				er = FFPARS_EBADBRACE; //closing brace should match the context type
				break;
			}

			st = iRmCtx;
			p->type = ctxType[ch == ']'];
			er = FFPARS_CLOSE;
			break;

//CTX
		case iNewCtx: {
			char *ctx = ffarr_push(&p->ctxs, char);
			if (ctx == NULL) {
				er = FFPARS_ESYS;
				break;
			}
			*ctx = p->type;
			st = FFPARS_IWSPACE;
			again = 1;
			}
			break;

		case iRmCtx:
			st = FFPARS_IWSPACE;
			nextst = iAfterVal;
			p->ctxs.len--; //ffarr_pop()
			again = 1;
			break;

//KEY
		case iKeyFirst:
			if (ch == '}') {
				// empty object: "{}"
				st = iCloseBrace;
				again = 1;
				break;
			}
			st = iKey;
			//break;

		case iKey:
			if (ch == '"') {
				st = iQuotFirst;
				nextst = iKey;
			}
			else
				er = FFPARS_EBADCHAR;
			break;

		case iAfterKey:
			json_cleardata(p);
			if (ch == ':') {
				st = FFPARS_IWSPACE;
				nextst = iVal;
			}
			else
				er = FFPARS_EKVSEP;
			break;
		}

		if (er != 0) {
			if (er < 0)
				data++;
			break;
		}

		if (again) {
			again = 0;
			data--;
			p->ch--;
		}
		else if (ch == '\n') {
			p->line++;
			p->ch = 0;
		}
	}

	if (er == FFPARS_MORE && p->buf.len != 0)
		er = val_store(&p->buf, NULL, 0);

	ffstr_set2(&p->val, &p->buf);
	p->state = st;
	p->nextst = nextst;
	*len = data - datao;
	p->ret = (char)er;
	return er;
}

int ffjson_validate(ffjson *json, const char *data, size_t len)
{
	int er = 0;
	const char *end = data + len;

	while (data != end) {
		len = end - data;
		er = ffjson_parse(json, data, &len);
		data += len;
		if (er > 0)
			break;
	}

	if (er == FFPARS_MORE) {
		len = 1;
		er = ffjson_parse(json, "", &len);
	}

	return er;
}


static const byte parsTypes[];

/* Search and select an argument by type */
static const ffpars_arg * _ffjson_schem_argfind(ffpars_ctx *ctx, int type)
{
	uint i;
	for (i = 0;  i < ctx->nargs;  i++) {
		if (type == parsTypes[(ctx->args[i].flags & FFPARS_FTYPEMASK) - FFPARS_TSTR])
			return &ctx->args[i];
	}

	return NULL;
}

// keep in sync with FFPARS_T
static const byte parsTypes[] = {
	FFJSON_TSTR, // <-> FFPARS_TSTR
	FFJSON_TSTR, // <-> FFPARS_TCHARPTR
	FFJSON_TINT, // <-> FFPARS_TINT
	FFJSON_TINT, // <-> FFPARS_TFLOAT
	FFJSON_TBOOL, // <-> FFPARS_TBOOL
	FFJSON_TOBJ, // <-> FFPARS_TOBJ
	FFJSON_TARR, // <-> FFPARS_TARR
	FFJSON_TSTR, // <-> FFPARS_TENUM
	FFJSON_TINT, // <-> FFPARS_TSIZE
	0, // <-> FFPARS_TANYTHING
};

/*
. Check types.
. Handle "null" type.
. If the context is array, find a handler for a data type */
static int ffjson_schemval(ffparser_schem *ps, void *obj, void *dst)
{
	ffjson *c = ps->p;
	int t;
	uint pt = c->type;

	if (ffarr_back(&c->ctxs) == FFJSON_TARR) {
		ps->curarg = _ffjson_schem_argfind(&ffarr_back(&ps->ctxs), c->type);
		if (ps->curarg == NULL)
			return FFPARS_EARRTYPE; //handler for this data type isn't defined in context of the array
	}

	if (pt == FFJSON_TNULL) {
		if (!(ps->curarg->flags & FFPARS_FNULL))
			return FFPARS_EVALNULL;

		if (ffpars_arg_isfunc(ps->curarg)) {
			int er = ps->curarg->dst.f_str(ps, obj, NULL);
			if (er != 0)
				return er;
		}

		return -1;
	}

	t = ps->curarg->flags & FFPARS_FTYPEMASK;
	FF_ASSERT(FFCNT(parsTypes) == FFPARS_TCLOSE - 1);
	if (pt != parsTypes[t - FFPARS_TSTR]
		&& t != FFPARS_TANYTHING
		&& !(t == FFPARS_TSIZE && pt == FFJSON_TSTR))
		return FFPARS_EVALTYPE;

	return 0;
}

int ffjson_schemfin(ffparser_schem *ps)
{
	ffjson *c = ps->p;
	if (ps->ctxs.len != 0)
		return FFPARS_ENOBRACE;
	if (c->line == 1 && c->ch == 0)
		return FFPARS_ENOVAL;
	return 0;
}

int ffjson_schemrun(ffparser_schem *ps)
{
	ffjson *c = ps->p;
	const ffpars_arg *arg;
	ffpars_ctx *ctx = &ffarr_back(&ps->ctxs);
	const ffstr *val = &c->val;
	uint f;
	int r;

	if (c->ret >= 0)
		return c->ret;

	if (c->ret != FFPARS_OPEN && ps->ctxs.len == 0)
		return FFPARS_ECONF;

	if (0 != (r = _ffpars_skipctx(ps, c->ret)))
		return r;

	switch (c->ret) {
	case FFPARS_KEY:
		f = 0;
		if (ps->flags & FFPARS_KEYICASE)
			f |= FFPARS_CTX_FKEYICASE;
		arg = ffpars_ctx_findarg(ctx, val->ptr, val->len, FFPARS_CTX_FANY | FFPARS_CTX_FDUP | f);
		if (arg == NULL)
			return FFPARS_EUKNKEY;
		else if (arg == (void*)-1)
			return FFPARS_EDUPKEY;
		ps->curarg = arg;

		if (!ffsz_cmp(arg->name, "*")) {
			c->ret = FFPARS_VAL;
			goto val;
		}

		return FFPARS_KEY;

	case FFPARS_OPEN:
		if (c->ctxs.len >= 2 && ffarr_back(&c->ctxs) == FFJSON_TARR) {

			ps->curarg = _ffjson_schem_argfind(&ffarr_back(&ps->ctxs), c->type);
			if (ps->curarg == NULL)
				return FFPARS_EARRTYPE;
		}

		if (ps->flags & _FFPARS_SCOBJ) {
			ps->flags &= ~_FFPARS_SCOBJ;
			if (ps->curarg->flags & FFPARS_FPTR) {
				return FFPARS_EVALUNSUPP;
			}
			r = ffpars_setctx(ps, NULL, NULL, 0);
			if (r != 0)
				break;
			r = ps->curarg->dst.f_obj(ps, ps->udata, &ffarr_back(&ps->ctxs));
			break;
		}

		r = _ffpars_schemrun(ps, FFPARS_OPEN);
		break;

	case FFPARS_CLOSE:
		r = _ffpars_schemrun(ps, FFPARS_CLOSE);
		break;

	case FFPARS_VAL:
		r = ffjson_schemval(ps, ctx->obj, NULL);
		if (ffpars_iserr(r))
			return r;
		if (r != 0)
			return FFPARS_VAL;

val:
		switch (ps->curarg->flags & FFPARS_FTYPEMASK) {
		case FFPARS_TINT:
		case FFPARS_TBOOL:
		case FFPARS_TFLOAT:
			r = _ffpars_arg_process2(ps->curarg, &c->intval, ctx->obj, ps);
			break;
		default:
			r = ffpars_arg_process(ps->curarg, &c->val, ctx->obj, ps);
		}
		break;

	default:
		return FFPARS_EINTL;
	}

	return r;
}


void ffjson_cookinit(ffjson_cook *c, char *buf, size_t cap)
{
	ffarr_set3(&c->buf, buf, 0, cap);
	ffarr_null(&c->ctxs);
	c->st = 0;
	c->gflags = 0;
}

enum State {
	stComma = 1
	, stKey = 2
	, stVal = 4
	,
	ST_CONTINUE = 8,
	ST_LF = 0x10,
};

int ffjson_add(ffjson_cook *c, int f, const void *src)
{
	char *d = ffarr_end(&c->buf);
	const char *end = c->buf.ptr + c->buf.cap;
	int type = f & 0x0f;
	ffbool closingCtx = 0;
	size_t len = 0;
	size_t tmp;
	unsigned nobuf = (c->buf.ptr == NULL);
	union {
		const void *p;
		const char *sz;
		const ffstr *s;
		const int64 *i64;
		const int *i32;
	} un;

	f |= c->gflags;

	un.p = src;
	closingCtx = src != NULL && (type == FFJSON_TOBJ || type == FFJSON_TARR);

	// insert comma if needed
	if ((c->st & stComma) && !closingCtx) {
		d = ffs_copyc(d, end, ',');
		len++;
	}

	// insert whitespace
	if ((f & _FFJSON_PRETTYMASK) && !(c->st & stVal)) {
		size_t n = c->ctxs.len;
		int ch = '\t';

		if (closingCtx)
			n--;

		if (!(f & FFJSON_PRETTY)) {
			ch = ' ';
			n *= (f & FFJSON_PRETTY4SPC) ? 4 : 2;
		}

		if (c->st & ST_LF) {
			d = ffs_copyc(d, end, '\n');
			len++;
		}
		d += ffs_fill(d, end, ch, n);
		len += n;
	}

	switch (type) {
	case FFJSON_TSTR:
		if (!(c->st & ST_CONTINUE)) {
			d = ffs_copyc(d, end, '"');
			len++;
		}

		if (f & FFJSON_FNOESC) {
			if ((f & FFJSON_FSTRZ) == FFJSON_FSTRZ) {
				d = ffs_copyz(d, end, un.sz);
				if (nobuf)
					len += strlen(un.sz);

			} else {
				d = ffs_copy(d, end, un.s->ptr, un.s->len);
				len += un.s->len;
			}
		}
		else {
			ffstr s;
			if ((f & FFJSON_FSTRZ) == FFJSON_FSTRZ)
				ffstr_set(&s, un.sz, strlen(un.sz));
			else
				s = *un.s;
			tmp = ffjson_escape(d, end - d, s.ptr, s.len);
			if (!nobuf)
				d += tmp;
			len += tmp;
		}

		if (f & FFJSON_FMORE) {
			c->st |= ST_CONTINUE;
			goto done;
		}
		c->st &= ~ST_CONTINUE;

		d = ffs_copyc(d, end, '"');
		len++;
		break;

	case FFJSON_TINT:
		{
			int64 i = (f & FFJSON_F32BIT) ? *un.i32 : *un.i64;
			d += ffs_fromint(i, d, end - d, FFINT_SIGNED);
			len += FFINT_MAXCHARS;
		}
		break;

	case FFJSON_TNUM:
		d = ffs_copy(d, end, un.s->ptr, un.s->len);
		len += un.s->len;
		break;

	case FFJSON_TBOOL:
		d = ffs_copyz(d, end, (un.p != NULL ? "true" : "false"));
		len += (un.p != NULL ? FFSLEN("true") : FFSLEN("false"));
		break;

	case FFJSON_TNULL:
		d = ffs_copycz(d, end, "null");
		len += FFSLEN("null");
		break;

	case FFJSON_TOBJ:
	case FFJSON_TARR:
		if (!closingCtx) {
			// open new context
			char *ctx = ffarr_push(&c->ctxs, char);
			if (ctx == NULL)
				return FFJSON_ERR;
			*ctx = type;
			c->st = 0;
			if (type == FFJSON_TOBJ)
				c->st |= stKey;

			d = ffs_copyc(d, end, ctxCharOpen[type == FFJSON_TARR]);
			len++;
		}
		else {
			// close context
			if (c->ctxs.len == 0)
				return FFJSON_ERR; //there is no context to close

			if (ffarr_back(&c->ctxs) != type)
				return FFJSON_ERR; //context types mismatch

			c->ctxs.len--;
			c->st = stComma;
			if (c->ctxs.len != 0 && ffarr_back(&c->ctxs) == FFJSON_TOBJ)
				c->st |= stKey;

			d = ffs_copyc(d, end, ctxCharClose[type == FFJSON_TARR]);
			len++;
		}
		break;

	default:
		return FFJSON_ERR; //invalid type specified
	}

	if (!(type == FFJSON_TOBJ || type == FFJSON_TARR)) {
		if (c->st & stKey) {
			// insert separator between key and value
			d = ffs_copyc(d, end, ':');
			len++;

			if (f & _FFJSON_PRETTYMASK) {
				d = ffs_copyc(d, end, ' ');
				len++;
			}

			c->st = stVal;
		}
		else if (c->ctxs.len != 0) {
			// ',' is needed if the next item of the same level follows
			c->st = stComma;
			if (ffarr_back(&c->ctxs) == FFJSON_TOBJ)
				c->st |= stKey;
		}
	}

done:
	if (nobuf)
		return -(int)len;

	if (d == end)
		return FFJSON_BUFFULL;

	c->st |= ST_LF;
	c->buf.len = d - c->buf.ptr;
	return FFJSON_OK;
}

int ffjson_addvv(ffjson_cook *js, const int *types, size_t ntypes, va_list va)
{
	size_t i;
	int r, ret = FFJSON_OK;
	int64 v;
	void *ptr;

	for (i = 0;  i < ntypes;  i++) {

		if ((types[i] & FFJSON_FINTVAL) == FFJSON_FINTVAL) {
			v = (types[i] & FFJSON_F32BIT) ? va_arg(va, int) : va_arg(va, int64);
			ptr = &v;
		} else
			ptr = va_arg(va, void*);

		r = ffjson_add(js, types[i], ptr);
		if (r == FFJSON_OK) {
		} else if (r < 0)
			ret += r;
		else
			return r;
	}

	FF_ASSERT(NULL == va_arg(va, void*));

	return ret;
}

int ffjson_bufadd(ffjson_cook *js, int f, const void *src)
{
	int r;
	size_t len;
	ffjson_cook tmp;

	tmp = *js;
	ffarr_null(&js->buf);
	r = ffjson_add(js, f, src);
	js->buf = tmp.buf;
	js->st = tmp.st;
	js->ctxs.len = tmp.ctxs.len;
	if (r >= 0)
		return r;

	len = -r;

	if (NULL == ffarr_grow(&js->buf, len + 1, FFARR_GROWQUARTER))
		return FFJSON_ERR;

	return ffjson_add(js, f, src);
}

int ffjson_bufaddv(ffjson_cook *js, const int *types, size_t ntypes, ...)
{
	va_list va;
	size_t len = 0;
	int r = FFJSON_OK;
	ffjson_cook tmp;

	// get overall length of data being inserted
	tmp = *js;
	ffarr_null(&js->buf);
	va_start(va, ntypes);
	r = ffjson_addvv(js, types, ntypes, va);
	va_end(va);
	js->buf = tmp.buf;
	js->st = tmp.st;
	js->ctxs.len = tmp.ctxs.len;
	if (r >= 0)
		return r;
	len = -r;

	if (NULL == ffarr_grow(&js->buf, len + 1, 0))
		return FFJSON_ERR;

	va_start(va, ntypes);
	r = ffjson_addvv(js, types, ntypes, va);
	va_end(va);

	return r;
}
