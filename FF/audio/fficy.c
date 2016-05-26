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


size_t fficy_addmeta(char *dst, size_t cap, const char *key, size_t keylen, const char *val, size_t vallen)
{
	return ffs_fmt(dst, dst + cap, "%*s='%*s';"
		, keylen, key, vallen, val);
}

uint fficy_finmeta(char *meta, size_t metacap, size_t metalen)
{
	if (metacap == 0)
		return 0;
	metacap = ffmin((metacap - 1) & ~0x0fU, FFICY_MAXMETA - 1);
	metalen = ffmin(metalen, metacap);

	if (metalen & 0x0f) {
		uint pad = 16 - (metalen & 0x0f);
		ffmem_zero(meta + 1 + metalen, pad);
		metalen += pad;
	}

	meta[0] = (byte)(metalen / 16);
	return 1 + (uint)metalen;
}
