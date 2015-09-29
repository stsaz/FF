/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/apetag.h>


const char *const ffapetag_str[_FFAPETAG_COUNT] = {
	"album",
	"artist",
	"comment",
	"genre",
	"title",
	"track",
	"year",
};

int ffapetag_parse(ffapetag *a, const char *data, size_t *len)
{
	enum { I_FTR, I_COPYTAG, I_HDR, I_FLD };
	ffapefld *fld;
	uint n;
	ffstr d;

	if (a->buf.len != 0)
		ffstr_set(&d, a->buf.ptr + a->nlen, a->buf.len - a->nlen);
	else
		ffstr_set(&d, data, *len);

	switch (a->state) {

	case I_FTR:
		n = ffmin(*len, sizeof(ffapehdr) - a->nlen);
		ffmemcpy((char*)&a->ftr + a->nlen, data + d.len - n, n);
		a->nlen += n;
		if (a->nlen != sizeof(ffapehdr))
			return FFAPETAG_RMORE;
		a->nlen = 0;

		if (!ffapetag_valid(&a->ftr)) {
			*len = 0;
			return FFAPETAG_RNO; //not an APEv2 tag
		}

		a->size = ffapetag_size(&a->ftr);
		if (a->size > d.len) {
			a->state = I_COPYTAG;
			*len = n;
			a->seekoff = -(int)a->size;
			return FFAPETAG_RSEEK;
		}
		//the whole tag is in one data block
		ffstr_set(&d, data + d.len - a->size, a->size);
		goto ihdr;

	case I_COPYTAG:
		n = ffmin(a->size - a->buf.len, *len);
		if (NULL == ffarr_append(&a->buf, data, n))
			return FFAPETAG_RERR;

		if (a->buf.len != a->size) {
			return FFAPETAG_RMORE;
		}
		ffstr_set(&d, a->buf.ptr, a->buf.len);

		// a->state = I_HDR;
		// break

	case I_HDR:
ihdr:
		if (a->ftr.has_hdr) {
			ffstr_shift(&d, sizeof(ffapehdr));
			a->size -= sizeof(ffapehdr);
			a->nlen += sizeof(ffapehdr);
		}

		a->state = I_FLD;
		// break

	case I_FLD:
		if (a->size == sizeof(ffapehdr)) {
			return FFAPETAG_RDONE;
		}

		if (sizeof(ffapefld) >= d.len)
			return FFAPETAG_RERR; //too large field

		fld = (void*)d.ptr;
		a->name.ptr = fld->name_val;
		n = ffsz_nlen(fld->name_val, d.len);
		a->name.len = n;
		ffstr_set(&a->val, fld->name_val + n + 1, fld->val_len);
		n = sizeof(int) * 2 + n + 1 + fld->val_len;
		if (n > a->size - sizeof(ffapehdr))
			return FFAPETAG_RERR; //too large field

		a->size -= n;
		a->nlen += n;
		*len = (d.ptr - data) + n;

		if (a->buf.len != 0)
			a->buf.len = 0;

		return FFAPETAG_RTAG;
	}

	//unreachable
	return 0;
}
