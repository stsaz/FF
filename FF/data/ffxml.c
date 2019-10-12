/**
Copyright (c) 2014 Simon Zolin
*/

#include <FF/data/xml.h>
#include <FF/string.h>


size_t ffxml_escape(char *dst, size_t cap, const char *s, size_t len)
{
	char *p = dst;
	size_t i;

	if (dst == NULL) {
		size_t n = 0;
		for (i = 0;  i < len;  ++i) {
			switch (s[i]) {
			case '<':
				n += FFSLEN("&lt;") - 1;
				break;

			case '>':
				n += FFSLEN("&gt;") - 1;
				break;

			case '&':
				n += FFSLEN("&amp;") - 1;
				break;

			case '"':
				n += FFSLEN("&quot;") - 1;
				break;
			}
		}
		return len + n;
	}

	for (i = 0;  i < len;  ++i) {
		switch (s[i]) {
		case '<':
			p = ffmem_copycz(p, "&lt;");
			break;

		case '>':
			p = ffmem_copycz(p, "&gt;");
			break;

		case '&':
			p = ffmem_copycz(p, "&amp;");
			break;

		case '"':
			p = ffmem_copycz(p, "&quot;");
			break;

		default:
			*p++ = s[i];
		}
	}
	return p - dst;
}


void ffxml_close(ffxml *x)
{
	ffarr_free(&x->buf);
}

enum R {
	TAG_BEGIN,
	TAG_NAME_BEGIN,
	TAG_NAME,
	TAG_ATTR_BEGIN,
	TAG_ATTR,
	TAG_ATTR_VAL_Q,
	TAG_ATTR_VAL,
	TAG_ATTR_VAL_SQ,
	TAG_ATTR_VAL_NOQ,
	TAG_CLOSE,
	TAG_CLOSE_NAME,
};

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

int ffxml_parse(ffxml *x, const char *data, size_t *len)
{
	int rc = FFPARS_MORE;
	ffstr d;
	ffstr_set(&d, data, *len);
	x->val.len = 0;

	while (d.len != 0) {
		int ch = d.ptr[0];

		switch (x->st) {
		case TAG_BEGIN:
			if (ch == '<') {
				x->st = TAG_NAME_BEGIN;
				if (x->buf.len != 0) {
					x->type = FFXML_TEXT;
					rc = FFPARS_VAL;
					break;
				}
				break;
			}
			if (0 != val_add(&x->buf, d.ptr, 1))
				return FFPARS_ESYS;
			break;

		case TAG_NAME_BEGIN:
			if (ch == '/') { // </...
				x->st = TAG_CLOSE_NAME;
				break;
			}
			x->st = TAG_NAME;
			// fallthrough

		case TAG_NAME:
			if (ch == ' ') { // <TAG ...
				x->st = TAG_ATTR_BEGIN;
				x->type = FFXML_TAG_OPEN;
				rc = FFPARS_OPEN;
				break;
			} else if (ch == '>') { // <TAG>...
				x->st = TAG_ATTR_BEGIN;
				x->type = FFXML_TAG_OPEN;
				rc = FFPARS_OPEN;
				goto end; // process this character again
			}
			if (0 != val_add(&x->buf, d.ptr, 1))
				return FFPARS_ESYS;
			break;

		case TAG_CLOSE_NAME:
			if (ch == '>') { // </TAG>...
				x->st = TAG_BEGIN;
				x->type = FFXML_TAG_CLOSE_NAME;
				rc = FFPARS_CLOSE;
				break;
			}
			if (0 != val_add(&x->buf, d.ptr, 1))
				return FFPARS_ESYS;
			break;

		case TAG_CLOSE:
			if (ch == '>') { // <TAG/>...
				x->st = TAG_BEGIN;
				x->type = FFXML_TAG_CLOSE;
				rc = FFPARS_CLOSE;
				break;
			}
			break;

		case TAG_ATTR_BEGIN:
			if (ffchar_iswhitespace(ch))
				break;
			else if (ch == '/') { // <TAG /...
				x->st = TAG_CLOSE;
				break;
			} else if (ch == '>') { // <TAG ATTR="VAL">...
				x->st = TAG_BEGIN;
				x->type = FFXML_TAG;
				rc = FFPARS_KEY;
				break;
			}
			x->st = TAG_ATTR;
			// fallthrough

		case TAG_ATTR:
			if (ffchar_iswhitespace(ch))
				break;
			else if (ch == '=') { // <TAG ATTR=...
				x->st = TAG_ATTR_VAL_Q;
				x->type = FFXML_TAG_ATTR;
				rc = FFPARS_KEY;
				break;
			}
			if (0 != val_add(&x->buf, d.ptr, 1))
				return FFPARS_ESYS;
			break;

		case TAG_ATTR_VAL_Q:
			if (ffchar_iswhitespace(ch))
				break;
			else if (ch == '"') { // <TAG ATTR="...
				x->st = TAG_ATTR_VAL;
				break;
			} else if (ch == '\'') { // <TAG ATTR='...
				x->st = TAG_ATTR_VAL_SQ;
				break;
			}

			if (x->options & FFXML_OSTRICT)
				return FFPARS_EBADCHAR;

			x->st = TAG_ATTR_VAL_NOQ; // <TAG ATTR=...
			continue; // again

		case TAG_ATTR_VAL:
			if (ch == '"') { // <TAG ATTR="VAL"...
				x->st = TAG_ATTR_BEGIN;
				x->type = FFXML_TAG_ATTR_VAL;
				rc = FFPARS_VAL;
				break;
			}
			if (0 != val_add(&x->buf, d.ptr, 1))
				return FFPARS_ESYS;
			break;

		case TAG_ATTR_VAL_SQ:
			if (ch == '\'') { // <TAG ATTR='VAL'...
				x->st = TAG_ATTR_BEGIN;
				x->type = FFXML_TAG_ATTR_VAL;
				rc = FFPARS_VAL;
				break;
			}
			if (0 != val_add(&x->buf, d.ptr, 1))
				return FFPARS_ESYS;
			break;

		case TAG_ATTR_VAL_NOQ:
			if (ffchar_iswhitespace(ch)) { // <TAG ATTR=VAL ...
				x->st = TAG_ATTR_BEGIN;
				x->type = FFXML_TAG_ATTR_VAL;
				rc = FFPARS_VAL;
				break;
			} else if (ch == '>') { // <TAG ATTR=VAL>...
				x->st = TAG_ATTR_BEGIN;
				x->type = FFXML_TAG_ATTR_VAL;
				rc = FFPARS_VAL;
				goto end; // process this character again
			}
			if (0 != val_add(&x->buf, d.ptr, 1))
				return FFPARS_ESYS;
			break;
		}

		ffstr_shift(&d, 1);
		if (ch == '\n') {
			x->line++;
			x->line_byte = 0;
		} else {
			x->line_byte++;
		}

		if (rc != FFPARS_MORE)
			break;
	}

end:
	if (rc != FFPARS_MORE) {
		ffstr_set2(&x->val, &x->buf);
		x->buf.len = 0;
	}
	*len = *len - d.len;
	return rc;
}
