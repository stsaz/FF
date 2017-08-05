/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/mtags/apetag.h>
#include <FF/mtags/mmtag.h>
#include <FF/number.h>


static const char *const ffapetag_str[] = {
	"album",
	"albumartist",
	"artist",
	"comment",
	"cover art (front)",
	"genre",
	"publisher",
	"title",
	"track",
	"year",
};

static const byte _ffapetag_id3[] = {
	FFMMTAG_ALBUM,
	FFMMTAG_ALBUMARTIST,
	FFMMTAG_ARTIST,
	FFMMTAG_COMMENT,
	FFMMTAG_PICTURE,
	FFMMTAG_GENRE,
	FFMMTAG_PUBLISHER,
	FFMMTAG_TITLE,
	FFMMTAG_TRACKNO,
	FFMMTAG_DATE,
};

static FFINL uint _ffapetag_field(const char *name)
{
	int r = (int)ffszarr_ifindsorted(ffapetag_str, FFCNT(ffapetag_str), name, ffsz_len(name));
	if (r == -1)
		return 0;
	return _ffapetag_id3[r];
}

int ffapetag_parse(ffapetag *a, const char *data, size_t *len)
{
	enum { I_FTR, I_FTR_DONE, I_COPYTAG, I_HDR, I_FLD };
	ffapehdr *ftr;
	ffapefld *fld;
	uint n;
	ffstr d;
	ssize_t r;

	if (a->buf.len != 0)
		ffstr_set(&d, a->buf.ptr, a->buf.len);
	else
		ffstr_set(&d, data + *len - a->size, a->size);
	ffstr_shift(&d, a->nlen);

	switch (a->state) {

	case I_FTR:
		if (*len < sizeof(ffapehdr))
			return FFAPETAG_RERR;
		ftr = (void*)(data + *len - sizeof(ffapehdr));
		if (!ffapetag_valid(ftr)) {
			*len = 0;
			return FFAPETAG_RNO; //not an APEv2 tag
		}

		a->has_hdr = ftr->has_hdr;
		a->size = ffapetag_size(ftr);
		a->state = I_FTR_DONE;
		return FFAPETAG_RFOOTER;

	case I_FTR_DONE:
		if (a->size > *len) {
			a->state = I_COPYTAG;
			return FFAPETAG_RSEEK;
		}
		//the whole tag is in one data block
		ffstr_set(&d, data + *len - a->size, a->size);
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
		if (a->has_hdr) {
			ftr = (void*)d.ptr;
			if (ffs_cmp(ftr->id, "APETAGEX", 8))
				return FFAPETAG_RERR; //invalid header
			ffstr_shift(&d, sizeof(ffapehdr));
			a->nlen += sizeof(ffapehdr);
		}

		a->state = I_FLD;
		// break

	case I_FLD:
		if (a->size - a->nlen == sizeof(ffapehdr)) {
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
		a->tag = _ffapetag_field(fld->name_val);
		ffstr_set(&a->val, fld->name_val + n + 1, fld->val_len);
		n = sizeof(int) * 2 + n + 1 + fld->val_len;
		if (n > a->size - a->nlen - sizeof(ffapehdr))
			return FFAPETAG_RERR; //too large field

		a->nlen += n;
		*len = 0;
		return FFAPETAG_RTAG;
	}

	//unreachable
	return 0;
}
