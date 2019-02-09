/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/data/m3u.h>


enum { M3U_LINE, M3U_HDR, M3U_EXTINF, M3U_TITLE, M3U_ARTIST, M3U_URL };

void ffm3u_init(ffm3u *p)
{
	ffmem_tzero(p);
	p->nextst = M3U_HDR;
}

int ffm3u_parse(ffm3u *p, ffstr *data)
{
	ssize_t r;
	const char *pos;
	ffstr s = p->tmp;

	for (;;) {
	switch (p->state) {

	case M3U_LINE:
		pos = ffs_find(data->ptr, data->len, '\n');
		if (p->buf.len + pos - data->ptr > FF_TEXT_LINE_MAX)
			return -FFPARS_EBIGVAL;
		r = ffarr_append_until(&p->buf, data->ptr, data->len, p->buf.len + pos - data->ptr + 1);
		if (r == 0)
			return FFPARS_MORE;
		else if (r < 0)
			return -FFPARS_ESYS;

		ffstr_shift(data, r);
		ffstr_set2(&s, &p->buf);
		p->buf.len = 0;
		pos = ffs_rskipof(s.ptr, s.len, "\r\n", 2);
		s.len = pos - s.ptr;
		p->line++;
		p->state = p->nextst;
		continue;

	case M3U_HDR:
		if (ffs_matchz(s.ptr, s.len, "\xef\xbb\xbf"))
			ffstr_shift(&s, 3);
		if (!ffstr_ieqcz(&s, "#EXTM3U")) {
			p->state = M3U_EXTINF;
			continue;
		}
		p->state = M3U_LINE,  p->nextst = M3U_EXTINF;
		continue;

	case M3U_EXTINF: {
		if (!ffs_match(s.ptr, s.len, "#", 1)) {
			p->state = M3U_URL;
			continue;
		}
		if (!ffs_imatchz(s.ptr, s.len, "#EXTINF:")) {
			p->val = s;
			p->state = M3U_LINE;
			return FFM3U_EXT;
		}
		ffstr_shift(&s, FFSLEN("#EXTINF:"));

		uint n = ffs_toint(s.ptr, s.len, &p->intval, FFS_INT64 | FFS_INTSIGN);
		ffstr_set(&p->val, s.ptr, n);
		ffstr_shift(&s, n);
		if (s.len == 0 || s.ptr[0] != ',') {
			p->state = M3U_LINE,  p->nextst = M3U_URL;
			continue;
		}
		ffstr_shift(&s, FFSLEN(","));
		p->state = M3U_ARTIST;
		if (n == 0)
			continue;
		r = FFM3U_DUR;
		goto done;
	}

	case M3U_ARTIST:
		pos = ffs_finds(s.ptr, s.len, " - ", 3);
		if (pos == s.ptr + s.len) {
			p->state = M3U_TITLE;
			continue;
		}
		ffstr_set(&p->val, s.ptr, pos - s.ptr);
		ffstr_shift(&s, pos - s.ptr + FFSLEN(" - "));
		r = FFM3U_ARTIST;
		goto done;

	case M3U_TITLE:
		ffstr_set2(&p->val, &s);
		p->state = M3U_LINE,  p->nextst = M3U_URL;
		r = FFM3U_TITLE;
		goto done;

	case M3U_URL:
		ffstr_set2(&p->val, &s);
		p->state = M3U_LINE,  p->nextst = M3U_EXTINF;
		r = FFM3U_URL;
		if (s.len != 0)
			goto done;
		continue;
	}
	}

done:
	p->tmp = s;
	return r;
}


int ffm3u_add(ffm3u_cook *m, uint type, const char *val, size_t len)
{
	enum { I_EXTM3U, I_EXTINF, I_DUR, I_ARTIST, I_TITLE, I_NAME };

	switch (m->state) {
	case I_EXTM3U:
		if (m->options & FFM3U_LF)
			ffstr_set(&m->crlf, "\n", 1);
		else
			ffstr_set(&m->crlf, "\r\n", 2);

		if (0 == ffstr_catfmt(&m->buf, "#EXTM3U%S", &m->crlf))
			goto err;
		m->state = I_EXTINF;
		// break

	case I_EXTINF:
		if (NULL == ffarr_append(&m->buf, "#EXTINF:", FFSLEN("#EXTINF:")))
			goto err;
		m->state = I_DUR;
		// break

	case I_DUR:
		if (0 == ffstr_catfmt(&m->buf, "%*s,", len, val))
			goto err;
		m->state = I_ARTIST;
		break;

	case I_ARTIST:
		if (0 == ffstr_catfmt(&m->buf, "%*s - ", len, val))
			goto err;
		m->state = I_TITLE;
		break;

	case I_TITLE:
		if (0 == ffstr_catfmt(&m->buf, "%*s%S", len, val, &m->crlf))
			goto err;
		m->state = I_NAME;
		break;

	case I_NAME:
		if (0 == ffstr_catfmt(&m->buf, "%*s%S", len, val, &m->crlf))
			goto err;
		m->state = I_EXTINF;
		break;
	}

	return 0;

err:
	return -1;
}


int ffm3u_entry_get(ffpls_entry *ent, uint type, const ffstr *val)
{
	if (ent->clear) {
		ffpls_entry_free(ent);
		ent->clear = 0;
	}

	switch (type) {

	case FFPARS_MORE:
		ffarr_copyself(&ent->url);
		ffarr_copyself(&ent->artist);
		ffarr_copyself(&ent->title);
		return 0;

	case FFM3U_ARTIST:
		ffstr_set2(&ent->artist, val);
		break;

	case FFM3U_TITLE:
		ffstr_set2(&ent->title, val);
		break;

	case FFM3U_DUR:
		(void)ffstr_toint(val, &ent->duration, FFS_INT32 | FFS_INTSIGN);
		break;

	case FFM3U_URL:
		ffstr_set2(&ent->url, val);
		ent->clear = 1;
		return 1;
	}

	return 0;
}
