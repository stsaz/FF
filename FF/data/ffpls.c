/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/data/pls.h>


enum { PLS_LINE, PLS_HDR, PLS_PAIR, PLS_RET };

void ffpls_init(ffpls *p)
{
	ffmem_tzero(p);
	p->state = PLS_LINE,  p->nextst = PLS_HDR;
	p->idx = (uint)-1;
}

int ffpls_parse(ffpls *p, ffstr *data)
{
	ssize_t r;
	const char *pos;
	ffstr line = {0}, name;

	for (;;) {
	switch (p->state) {

	case PLS_LINE:
		pos = ffs_find(data->ptr, data->len, '\n');
		if (p->buf.len + pos - data->ptr > FF_TEXT_LINE_MAX)
			return -FFPARS_EBIGVAL;
		r = ffarr_append_until(&p->buf, data->ptr, data->len, p->buf.len + pos - data->ptr + 1);
		if (r == 0)
			return FFPARS_MORE;
		else if (r < 0)
			return -FFPARS_ESYS;

		ffstr_shift(data, r);
		ffstr_set2(&line, &p->buf);
		p->buf.len = 0;
		pos = ffs_rskipof(line.ptr, line.len, "\r\n", 2);
		line.len = pos - line.ptr;
		p->line++;
		p->state = p->nextst;
		continue;

	case PLS_HDR:
		if (!ffstr_ieqcz(&line, "[playlist]"))
			return FFPARS_EBADVAL;
		p->state = PLS_LINE,  p->nextst = PLS_PAIR;
		continue;

	case PLS_PAIR: {
		p->state = PLS_LINE,  p->nextst = PLS_PAIR;
		if (NULL == ffs_split2by(line.ptr, line.len, '=', &name, &p->val))
			continue;

		ffstr key;
		uint num;
		if (name.len != ffs_fmatch(name.ptr, name.len, "%S%u", &key, &num))
			continue; //possibly a key without index

		if (ffstr_ieqz(&key, "file"))
			r = FFPLS_URL;
		else if (ffstr_ieqz(&key, "title"))
			r = FFPLS_TITLE;
		else if (ffstr_ieqz(&key, "length"))
			r = FFPLS_DUR;
		else
			continue; //unsupported key

		if (p->idx != (uint)-1 && p->idx != num) {
			p->idx = (uint)-1;
			p->type = r;
			p->state = PLS_RET;
			return FFPLS_READY;
		}
		p->idx = num;
		return r;

	case PLS_RET:
		p->state = PLS_LINE,  p->nextst = PLS_PAIR;
		return p->type;
	}
	}
	}
}

int ffpls_entry_get(ffpls_entry *ent, uint type, const ffstr *val)
{
	if (ent->clear) {
		ffpls_entry_free(ent);
		ent->clear = 0;
	}

	switch (type) {

	case FFPARS_MORE:
		ffarr_copyself(&ent->url);
		ffarr_copyself(&ent->title);
		return 0;

	case FFPLS_URL:
		ffstr_set2(&ent->url, val);
		break;

	case FFPLS_TITLE:
		ffstr_set2(&ent->title, val);
		break;

	case FFPLS_DUR:
		(void)ffstr_toint(val, &ent->duration, FFS_INT32 | FFS_INTSIGN);
		break;

	case FFPLS_FIN:
		if (ent->url.len == 0)
			break;
		// break
	case FFPLS_READY:
		ent->clear = 1;
		if (ent->url.len == 0)
			return 0;
		return 1;
	}

	return 0;
}
