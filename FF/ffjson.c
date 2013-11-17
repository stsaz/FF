/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/json.h>


static const char *const _ffjson_stypes[] = {
	"null", "string", "integer", "boolean", "object", "array"
};

const char * ffjson_stype(int type)
{
	return _ffjson_stypes[type];
}

static const char _ffjson_escchar[] = "\"\\bfrnt/";
static const char _ffjson_escbyte[] = "\"\\\b\f\r\n\t/";

int ffjson_unescape(char *dst, size_t cap, const char *text, size_t len)
{
	char *d;
	if (cap == 0 || len == 0)
		return 0;

	d = memchr(_ffjson_escchar, text[0], FFSLEN(_ffjson_escchar));
	if (d == NULL) {
		if (text[0] == 'u') {
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

	*dst = _ffjson_escbyte[d - _ffjson_escchar];
	return 1;
}

enum IDX {
	iWSpace
	, iValFirst, iVal, iValBare, iAfterVal, iCloseBrace, iFin
	, iNewCtx, iRmCtx
	, iQuot, iQuotFirst, iQuotEsc
	, iKeyFirst, iKey, iAfterKey
	, iCmtBegin, iCmtLine, iCmtMLine, iCmtMLineSlash
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

static const char ws[] = " \t\r\n";

static int hdlWspace(int ch)
{
	char *d = memchr(ws, ch, FFSLEN(ws));
	return d == NULL;
}

static int hdlCmt(int *st, int ch)
{
	switch (*st) {
	case iCmtBegin:
		if (ch == '/')
			*st = iCmtLine;
		else if (ch == '*')
			*st = iCmtMLine;
		else
			return FFPARS_EBADCMT; // "//" or "/*" only
		break;

	case iCmtLine:
		if (ch == '\n')
			*st = iWSpace; //end of line comment
		break;

	case iCmtMLine:
		if (ch == '*')
			*st = iCmtMLineSlash;
		break;

	case iCmtMLineSlash:
		if (ch == '/')
			*st = iWSpace; //end of multiline comment
		else
			*st = iCmtMLine; //skipped '*' within multiline comment
		break;
	}
	return 0;
}

static const ffstr _ffjson_words[] = {
	FFSTR_INIT("true"), FFSTR_INIT("false"), FFSTR_INIT("null")
};
static const byte wordType[] = { FFJSON_TBOOL, FFJSON_TBOOL, FFJSON_TNULL };

// true|false|null|123|-123
static int hdlValBare(ffparser *p, int ch)
{
	ffstr v;
	int i;

	if (ffchar_islow(ch) || ffchar_isnum(ch) || ch == '-') {
		int er = 0;
		if (p->val.len >= FFINT_MAXCHARS)
			return FFPARS_EBIGVAL; //value without quotes is too large

		if (p->val.ptr != p->buf.ptr)
			p->val.len++;
		else
			er = _ffpars_copyText(p, (char*)&ch, sizeof(char));
		return er;
	}
	// met stop-character

	if (p->val.len == 0)
		return FFPARS_ENOVAL;

	v = p->val;

	for (i = 0;  i < FFCNT(_ffjson_words);  i++) {
		if (ffstr_ieq2(&v, &_ffjson_words[i])) {
			p->type = wordType[i];
			p->intval = (i == 0);
			return FFPARS_VAL;
		}
	}

	if (v.len == ffs_toint(v.ptr, v.len, &p->intval, FFS_INT64 | FFS_INTSIGN))
		p->type = FFJSON_TINT;
	else
		return FFPARS_EBADVAL;
	return FFPARS_VAL;
}

static int hdlQuote(ffparser *p, int *st, int *nextst, int ch)
{
	int er = 0;

	switch (ch) {
	case '"':
		p->type = FFJSON_TSTR;
		*st = iWSpace;

		if (*nextst == iKey) {
			*nextst = iAfterKey;
			er = FFPARS_KEY;
			break;
		}

		*nextst = iAfterVal;
		if (p->ctxs.len == 0)
			*nextst = iFin; //document finished
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
			er = _ffpars_copyText(p, (char*)&ch, sizeof(char));
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
		r = _ffpars_copyText(p, buf, r); //use dynamic buffer
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
		case iWSpace:
			if (0 != hdlWspace(ch)) {
				if (ch == '/')
					st = iCmtBegin;
				else {
					// met non-whitespace character
					st = nextst;
					nextst = iFin;
					again = 1;
				}
			}
			break;

		case iCmtBegin:
		case iCmtLine:
		case iCmtMLine:
		case iCmtMLineSlash:
			er = hdlCmt(&st, ch);
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
				st = iValBare;
				p->val.ptr = (char*)data;
				again = 1;
			}
			break;

		case iValBare:
			er = hdlValBare(p, ch);
			if (er == FFPARS_VAL) {
				st = iWSpace;
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

			if (ch == ',') {
				st = iWSpace;
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
			st = iWSpace;
			again = 1;
			}
			break;

		case iRmCtx:
			st = iWSpace;
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
				st = iWSpace;
				nextst = iVal;
			}
			else
				er = FFPARS_EKVSEP;
			break;


		case iFin:
			er = FFPARS_EBADCHAR; //no more entities expected
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
		if (dst == NULL)
			return ps->curarg->dst.f_str(ps, obj, NULL);
		// else don't do anything
		return 0;
	}

	if (ps->p->type != parsTypes[t - FFPARS_TSTR]
		&& !(t == FFPARS_TSIZE && ps->p->type == FFJSON_TSTR))
		return FFPARS_EVALTYPE;

	return -1;
}
