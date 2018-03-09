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
	iValFirst = 16, iVal, iValBare, iValBareNum, iAfterVal, iCloseBrace
	, iNewCtx, iRmCtx
	, iQuot, iQuotFirst, iQuotEsc
	, iKeyFirst, iKey, iAfterKey
};

enum FLAGS {
	F_ESC_UTF16_2 = 1, //UTF-16 escaped char must follow
};

void ffjson_parseinit(ffparser *p)
{
	ffpars_init(p);
	p->nextst = iVal;
}

void ffjson_parsereset(ffparser *p)
{
	ffpars_reset(p);
	p->nextst = iVal;
}

static const ffstr _ffjson_words[] = {
	FFSTR_INIT("true"), FFSTR_INIT("false"), FFSTR_INIT("null")
};
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
static int hdlValBare(ffparser *p, int ch)
{
	int er = 0, e;
	int i = (int)p->esc[0] - 1;

	size_t len = p->val.len;
	if (ch != _ffjson_words[i].ptr[len])
		return FFPARS_EBADVAL;

	if (len + 1 == _ffjson_words[i].len) {
		p->type = wordType[i];
		p->intval = (i == 0);
		er = FFPARS_VAL;
	}

	e = _ffpars_addchar(p, ch);
	if (e != 0)
		er = e;

	return er;
}

/* integer or float */
static int hdlValBareNum(ffparser *p, int ch)
{
	int er = 0;

	if (!(ffchar_isdigit(ch) || ch == '-' || ch == '+' || ch == '.' || ffchar_lower(ch) == 'e')) {
		ffstr v = p->val;
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
		er = _ffpars_addchar(p, ch);
	}

	return er;
}

static int hdlQuote(ffparser *p, int *st, int *nextst, int ch)
{
	int er = 0;

	switch (ch) {
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
		*st = iQuotEsc; //wait for escape sequence
		break;

	case '\n':
		er = FFPARS_EBADCHAR; // newline within quoted value
		break;

	default:
		er = _ffpars_addchar(p, ch);
	}

	if ((p->flags & F_ESC_UTF16_2) && *st != iQuotEsc)
		return FFPARS_EESC; //UTF-16 escaped char must follow

	return er;
}

static int hdlEsc(ffparser *p, int *st, int ch)
{
	char buf[8];
	int r;
	uint uch;

	if ((uint)p->esc[0] >= FFSLEN("uXXXX"))
		return FFPARS_EESC; //too large escape sequence

	p->esc[(uint)++p->esc[0]] = (char)ch;
	r = ffjson_unescapechar(&uch, p->esc + 1, p->esc[0]);
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
		p->esc[0] = 0;
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
	r = _ffpars_copyBuf(p, buf, r); //use dynamic buffer
	if (r != 0)
		return r; //allocation error
	p->esc[0] = 0;
	*st = iQuot;

	return 0;
}

static const byte ctxType[] = { FFJSON_TOBJ, FFJSON_TARR };
static const char ctxCharOpen[] = { '{', '[' };
static const char ctxCharClose[] = { '}', ']' };

int ffjson_parse(ffparser *p, const char *data, size_t *len)
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
			er = _ffpars_hdlCmt(&st, ch);
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
				p->val.ptr = (char*)data;
				{
					int t = valBareType(ch);
					st = (t != 0 ? iValBare : iValBareNum);
					p->esc[0] = t;
				}
				again = 1;
			}
			break;

		case iValBare:
			er = hdlValBare(p, ch);
			if (er == FFPARS_VAL) {
				st = FFPARS_IWSPACE;
				nextst = iAfterVal;
			}
			break;

		case iValBareNum:
			er = hdlValBareNum(p, ch);
			if (er == FFPARS_VAL) {
				st = FFPARS_IWSPACE;
				nextst = iAfterVal;
				data--; // process this char again
			}
			break;

//QUOTE
		case iQuotFirst:
			p->val.ptr = (char*)data;
			st = iQuot;
			//break;

		case iQuot:
			er = hdlQuote(p, &st, &nextst, ch);
			break;

		case iQuotEsc:
			er = hdlEsc(p, &st, ch);
			break;

//AFTER VALUE
		case iAfterVal:
			ffpars_cleardata(p);

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
			ffpars_cleardata(p);
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

	if (er == FFPARS_MORE)
		er = ffpars_savedata(p);

	p->state = st;
	p->nextst = nextst;
	*len = data - datao;
	p->ret = (char)er;
	return er;
}

int ffjson_validate(ffparser *json, const char *data, size_t len)
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
	FFJSON_TSTR
	, FFJSON_TSTR
	, FFJSON_TINT
	, FFJSON_TINT
	, FFJSON_TBOOL
	, FFJSON_TOBJ
	, FFJSON_TARR
	, FFJSON_TSTR
	, FFJSON_TINT
};

/*
. Check types.
. Handle "null" type.
. If the context is array, find a handler for a data type */
static int ffjson_schemval(ffparser_schem *ps, void *obj, void *dst)
{
	int t;
	uint pt = ps->p->type;

	if (ffarr_back(&ps->p->ctxs) == FFJSON_TARR) {
		ps->curarg = _ffjson_schem_argfind(&ffarr_back(&ps->ctxs), ps->p->type);
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
	if (pt != parsTypes[t - FFPARS_TSTR]
		&& !(t == FFPARS_TSIZE && pt == FFJSON_TSTR))
		return FFPARS_EVALTYPE;

	return 0;
}

int ffjson_schemfin(ffparser_schem *ps)
{
	if (ps->ctxs.len != 0)
		return FFPARS_ENOBRACE;
	if (ps->p->line == 1 && ps->p->ch == 0)
		return FFPARS_ENOVAL;
	return 0;
}

int ffjson_schemrun(ffparser_schem *ps)
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

	switch (ps->p->ret) {
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
			ps->p->ret = FFPARS_VAL;
			break;
		}

		return FFPARS_KEY;

	case FFPARS_OPEN:
		if (ps->p->ctxs.len >= 2 && ffarr_back(&ps->p->ctxs) == FFJSON_TARR) {

			ps->curarg = _ffjson_schem_argfind(&ffarr_back(&ps->ctxs), ps->p->type);
			if (ps->curarg == NULL)
				return FFPARS_EARRTYPE;
		}
		break;

	case FFPARS_VAL:
		r = ffjson_schemval(ps, ctx->obj, NULL);
		if (ffpars_iserr(r))
			return r;
		if (r != 0)
			return FFPARS_VAL;
		break;
	}

	r = ffpars_schemrun(ps, ps->p->ret);
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
