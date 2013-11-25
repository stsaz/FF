/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/json.h>


static const char *const _ffjson_stypes[] = {
	"null", "string", "integer", "boolean", "object", "array", "number"
};

const char * ffjson_stype(int type)
{
	return _ffjson_stypes[type];
}

int ffjson_unescape(char *dst, size_t cap, const char *text, size_t len)
{
	char *d;
	if (cap == 0 || len == 0)
		return 0;

	d = memchr(ff_escchar, text[0], FFCNT(ff_escchar));

	if (d != NULL) {
		*dst = ff_escbyte[d - ff_escchar];
		return 1;
	}

	if (text[0] == '/') {
		*dst = '/';
		return 1;
	}
	else if (text[0] == 'u') {
		wchar_t wch;
		ushort sh;
		size_t r;

		if (len < FFSLEN("uXXXX")) // \\uXXXX (UCS-2 BE)
			return 0;

		if (FFSLEN("XXXX") != ffs_toint(text + 1, FFSLEN("XXXX"), &sh, FFS_INT16 | FFS_INTHEX))
			return -1;

		wch = sh;
		r = ff_wtou(dst, cap, &wch, 1, 0);
		if (r != 0)
			return (int)r;
	}

	return -1;
}

enum IDX {
	iValFirst = 16, iVal, iValBare, iValBareNum, iAfterVal, iCloseBrace
	, iNewCtx, iRmCtx
	, iQuot, iQuotFirst, iQuotEsc
	, iKeyFirst, iKey, iAfterKey
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
	int er = 0;
	int i = (int)p->esc[0] - 1;

	int len = p->val.len;
	if (ch != _ffjson_words[i].ptr[len])
		return FFPARS_EBADVAL;

	if (len + 1 == _ffjson_words[i].len) {
		p->type = wordType[i];
		p->intval = (i == 0);
		er = FFPARS_VAL;
	}

	if (p->val.ptr != p->buf.ptr)
		p->val.len++;
	else {
		int e = _ffpars_copyBuf(p, (char*)&ch, sizeof(char));
		if (e != 0)
			er = e;
	}

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
		if (p->val.ptr != p->buf.ptr)
			p->val.len++;
		else
			er = _ffpars_copyBuf(p, (char*)&ch, sizeof(char));
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
		if (p->val.ptr != p->buf.ptr)
			p->val.len++;
		else
			er = _ffpars_copyBuf(p, (char*)&ch, sizeof(char));
	}

	return er;
}

static int hdlEsc(ffparser *p, int *st, int ch)
{
	char buf[8];
	int r;

	if (p->esc[0] >= FFSLEN("uXXXX"))
		return FFPARS_EESC; //too large escape sequence

	p->esc[(uint)++p->esc[0]] = (char)ch;
	r = ffjson_unescape(buf, FFCNT(buf), p->esc + 1, p->esc[0]);
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

static const byte ctxType[] = { FFJSON_TOBJ, FFJSON_TARR };

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
			p->intval = 0;

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


// keep in sync with FFPARS_T
static const byte parsTypes[] = {
	FFJSON_TSTR
	, FFJSON_TINT
	, FFJSON_TBOOL
	, FFJSON_TOBJ
	, FFJSON_TARR
	, FFJSON_TSTR
	, FFJSON_TINT
};

int ffjson_schemval(ffparser_schem *ps, void *obj, void *dst)
{
	int t = ps->curarg->flags & FFPARS_FTYPEMASK;

	if (ps->p->type == FFJSON_TNULL) {
		if (!(ps->curarg->flags & FFPARS_FNULL))
			return FFPARS_EVALNULL;
		if (dst == NULL) {
			int er = ps->curarg->dst.f_str(ps, obj, NULL);
			if (er != 0)
				return er;
		}
		// else don't do anything
		return -1;
	}

	if (ps->p->type != parsTypes[t - FFPARS_TSTR]
		&& !(t == FFPARS_TSIZE && ps->p->type == FFJSON_TSTR))
		return FFPARS_EVALTYPE;

	return 0;
}
