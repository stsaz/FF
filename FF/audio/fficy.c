/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/icy.h>


const ffstr fficy_shdr[] = {
	FFSTR_INIT("icy-name")
	, FFSTR_INIT("icy-genre")
	, FFSTR_INIT("icy-url")
	, FFSTR_INIT("icy-metaint")
	//icy-br

	, FFSTR_INIT("Icy-MetaData")
};


int fficy_parse(fficy *ic, const char *data, size_t *len, ffstr *dst)
{
	const char *d = data, *end = data + *len;

	if (ic->datasize == 0) {

		if (ic->metasize == 0) {
			if (d == end) {
				dst->len = 0;
				return FFICY_RMETACHUNK;
			}

			ic->metasize = fficy_metasize(*d++);
			ic->meta_len = 0;
		}

		if (ic->metasize == 0)
			ic->datasize = ic->meta_interval;
		else {
			ffstr_set(dst, d, ffmin(ic->metasize, end - d));
			*len = (d + dst->len) - data;
			ic->metasize -= (uint)dst->len;

			if (ic->metasize != 0 || ic->meta_len != 0) {
				// append the current meta chunk to "ic->meta"
				ffmemcpy(ic->meta + ic->meta_len, dst->ptr, dst->len);
				ic->meta_len += dst->len;
				if (ic->metasize == 0) {
					ffstr_set(dst, ic->meta, ic->meta_len);
				}
			}

			if (ic->metasize == 0) {
				ic->datasize = ic->meta_interval;
				return FFICY_RMETA;
			}

			return FFICY_RMETACHUNK;
		}
	}

	ffstr_set(dst, d, ffmin(ic->datasize, end - d));
	*len = (d + dst->len) - data;

	if (ic->meta_interval != FFICY_NOMETA)
		ic->datasize -= (uint)dst->len;

	return FFICY_RDATA;
}


int fficy_metaparse(fficymeta *p, const char *data, size_t *len)
{
	const char *d = data, *end = data + *len, *s;
	enum { I_KEY, I_QOPEN, I_VAL, I_SMCOL };

	while (d != end) {
		switch (p->state) {
		case I_KEY:
			s = ffs_findc(data, *len, '=');
			if (s == NULL)
				return FFPARS_EKVSEP;

			ffstr_set(&p->val, data, s - data);
			p->state = I_QOPEN;
			*len = s - data + FFSLEN("=");
			return FFPARS_KEY;

		case I_QOPEN:
			if (*d != '\'')
				return FFPARS_ENOVAL; //expected quote after '='

			d += FFSLEN("'");
			p->val.ptr = (char*)d;
			p->state = I_VAL;
			//break;

		case I_VAL:
			s = ffs_findc(d, end - d, '\'');
			if (s == NULL)
				return FFPARS_EBADBRACE; //no closing quote

			p->val.len = s - p->val.ptr;
			p->state = I_SMCOL;
			break;

		case I_SMCOL:
			if (*d != ';') {
				p->state = I_VAL; //a quote inside a value
				continue;
			}

			p->state = I_KEY;
			*len = d + FFSLEN(";") - data;
			return FFPARS_VAL;
		}

		d++;
	}

	return FFPARS_MORE;
}

int fficy_metaparse_str(fficymeta *p, ffstr *meta, ffstr *dst)
{
	size_t n = meta->len;
	int r = fficy_metaparse(p, meta->ptr, &n);
	ffstr_shift(meta, n);
	*dst = p->val;
	return r;
}

int fficy_streamtitle(const char *data, size_t len, ffstr *artist, ffstr *title)
{
	const char *spl;
	if (data + len != (spl = ffs_finds(data, len, FFSTR(" - ")))) {
		ffstr_set(artist, data, spl - data);
		spl += FFSLEN(" - ");
		ffstr_set(title, spl, data + len - spl);
		return artist->len;
	}
	return -1;
}


int fficy_initmeta(ffarr *meta, char *buf, size_t cap)
{
	if (cap != 0) {
		ffarr_set3(meta, buf, 0, cap);
		meta->len = 1; //leave space for the size
	} else {
		if (NULL == ffarr_alloc(meta, FFICY_MAXMETA))
			return -1;
	}
	return 0;
}

size_t fficy_addmeta(ffarr *meta, const char *key, size_t keylen, const char *val, size_t vallen)
{
	size_t r = ffs_fmt(ffarr_end(meta), ffarr_edge(meta), "%*s='%*s';"
		, keylen, key, vallen, val);
	meta->len += r;
	return r;
}

uint fficy_finmeta(ffarr *meta)
{
	uint n = meta->len - 1;
	if (n & 0x0f) {
		uint pad = 16 - (n & 0x0f);
		ffmem_zero(ffarr_end(meta), pad);
		meta->len += pad;
		n += pad;
	}

	meta->ptr[0] = (byte)(n / 16);
	return meta->len;
}
