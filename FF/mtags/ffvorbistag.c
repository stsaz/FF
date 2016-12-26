/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/mtags/vorbistag.h>
#include <FF/data/mmtag.h>
#include <FF/number.h>


static const byte vorb_tags[] = {
	FFMMTAG_ALBUM,
	FFMMTAG_ALBUMARTIST,
	FFMMTAG_ALBUMARTIST,
	FFMMTAG_ARTIST,
	FFMMTAG_COMMENT,
	FFMMTAG_COMPOSER,
	FFMMTAG_DATE,
	FFMMTAG_DISCNUMBER,
	FFMMTAG_GENRE,
	FFMMTAG_LYRICS,
	FFMMTAG_PUBLISHER,
	FFMMTAG_TITLE,
	FFMMTAG_TRACKTOTAL,
	FFMMTAG_TRACKNO,
	FFMMTAG_TRACKTOTAL,
};

static const char *const ffvorbtag_str[] = {
	"ALBUM",
	"ALBUM ARTIST", // =ALBUMARTIST
	"ALBUMARTIST",
	"ARTIST",
	"COMMENT",
	"COMPOSER",
	"DATE",
	"DISCNUMBER",
	"GENRE",
	"LYRICS",
	"PUBLISHER",
	"TITLE",
	"TOTALTRACKS", // =TRACKTOTAL
	"TRACKNUMBER",
	"TRACKTOTAL",
};

int ffvorbtag_find(const char *name, size_t len)
{
	return ffszarr_ifindsorted(ffvorbtag_str, FFCNT(ffvorbtag_str) - 1, name, len);
}

int ffvorbtag_parse(ffvorbtag *v)
{
	enum { I_VENDOR, I_CMTCNT, I_CMT };
	uint len;

	switch (v->state) {
	case I_VENDOR:
		if (v->datalen < 4)
			return FFVORBTAG_ERR;
		len = ffint_ltoh32(v->data);
		FFARR_SHIFT(v->data, v->datalen, 4);

		if (v->datalen < len)
			return FFVORBTAG_ERR;
		ffstr_setcz(&v->name, "VENDOR");
		ffstr_set(&v->val, v->data, len);
		v->tag = FFMMTAG_VENDOR;
		FFARR_SHIFT(v->data, v->datalen, len);
		v->state = I_CMTCNT;
		return 0;

	case I_CMTCNT:
		if (v->datalen < 4)
			return FFVORBTAG_ERR;
		len = ffint_ltoh32(v->data);
		FFARR_SHIFT(v->data, v->datalen, 4);
		v->cnt = len;
		v->state = I_CMT;
		// break

	case I_CMT:
		if (v->cnt == 0)
			return FFVORBTAG_DONE;

		if (v->datalen < 4)
			return FFVORBTAG_ERR;
		len = ffint_ltoh32(v->data);
		FFARR_SHIFT(v->data, v->datalen, 4);

		if (v->datalen < len)
			return FFVORBTAG_ERR;
		ffs_split2by(v->data, len, '=', &v->name, &v->val);
		FFARR_SHIFT(v->data, v->datalen, len);
		v->cnt--;
		v->tag = ffvorbtag_find(v->name.ptr, v->name.len);
		v->tag = (v->tag != -1) ? vorb_tags[v->tag] : 0;
		return 0;
	}

	//unreachable
	return FFVORBTAG_ERR;
}


int ffvorbtag_add(ffvorbtag_cook *v, const char *name, const char *val, size_t vallen)
{
	size_t namelen;
	uint len;

	namelen = (v->cnt != 0) ? ffsz_len(name) : 0;
	len = namelen + FFSLEN("=") + vallen;

	if (!v->nogrow) {
		if (NULL == ffarr_grow(&v->out, 4 + len, FFARR_GROWQUARTER))
			return FFVORBTAG_ERR;
	} else if (4 + len > ffarr_unused(&v->out))
		return FFVORBTAG_ERR;

	char *d = ffarr_end(&v->out);
	const char *end = ffarr_edge(&v->out);

	if (v->cnt == 0) {
		//vendor
		len = vallen;
		v->vendor_off = v->out.len;
		ffint_htol32(d, len);
		d += 4;
		d = ffmem_copy(d, val, vallen) + 4;
		goto done;
	}

	ffint_htol32(d, len);
	d += 4;
	d += ffs_upper(d, end, name, namelen);
	*d++ = '=';
	d = ffmem_copy(d, val, vallen);

done:
	v->out.len = d - v->out.ptr;
	v->cnt++;
	return 0;
}

void ffvorbtag_fin(ffvorbtag_cook *v)
{
	uint vendor_len = ffint_ltoh32(v->out.ptr + v->vendor_off);
	ffint_htol32(v->out.ptr + v->vendor_off + 4 + vendor_len, v->cnt - 1);
}
