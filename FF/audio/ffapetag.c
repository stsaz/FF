/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/apetag.h>
#include <FF/number.h>


const char *const ffapetag_str[_FFAPETAG_COUNT] = {
	"album",
	"artist",
	"comment",
	"cover art (front)",
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
	ssize_t r;

	if (a->buf.len != 0)
		ffstr_set(&d, a->buf.ptr + a->nlen, a->buf.len - a->nlen);
	else
		ffstr_set(&d, data + a->nlen, *len - a->nlen);

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
			a->seekoff = -(int)a->size;
			return FFAPETAG_RSEEK;
		}
		//the whole tag is in one data block
		ffstr_set(&d, data + d.len - a->size, a->size);
		goto ihdr;

	case I_COPYTAG:
		r = ffarr_append_until(&a->buf, data, *len, a->size);
		if (r == -1)
			return FFAPETAG_RERR;
		else if (r == 0)
			return FFAPETAG_RMORE;
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
			*len = 0;
			return FFAPETAG_RDONE;
		}

		if (sizeof(ffapefld) >= d.len)
			return FFAPETAG_RERR; //too large field

		fld = (void*)d.ptr;
		a->flags = ffint_ltoh32(&fld->flags);
		a->name.ptr = fld->name_val;
		n = ffsz_nlen(fld->name_val, d.len);
		a->name.len = n;
		ffstr_set(&a->val, fld->name_val + n + 1, fld->val_len);
		n = sizeof(int) * 2 + n + 1 + fld->val_len;
		if (n > a->size - sizeof(ffapehdr))
			return FFAPETAG_RERR; //too large field

		a->size -= n;
		a->nlen += n;
		*len = 0;
		return FFAPETAG_RTAG;
	}

	//unreachable
	return 0;
}
