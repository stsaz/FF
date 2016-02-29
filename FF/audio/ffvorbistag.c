/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/vorbistag.h>
#include <FF/number.h>


const char *const ffvorbtag_str[] = {
	"ALBUM",
	"ARTIST",
	"COMMENT",
	"DATE",
	"GENRE",
	"TITLE",
	"TOTALTRACKS", // =TRACKTOTAL
	"TRACKNUMBER",
	"TRACKTOTAL",

	"VENDOR",
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
		v->tag = FFVORBTAG_VENDOR;
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
		if (v->tag == _FFVORBTAG_TOTALTRACKS) {
			// "TOTALTRACKS" -> "TRACKTOTAL"
			ffstr_setz(&v->name, ffvorbtag_str[FFVORBTAG_TRACKTOTAL]);
		}
		return 0;
	}

	//unreachable
	return FFVORBTAG_ERR;
}


int ffvorbtag_add(ffvorbtag_cook *v, const char *name, const char *val, size_t vallen)
{
	char *d = v->out + v->outlen;
	const char *end = v->out + v->outcap;
	size_t namelen;
	uint len;

	if (v->outlen == 0) {
		//vendor
		len = vallen;
		if (4 + len + 4 > v->outcap)
			return FFVORBTAG_ERR;

		ffint_htol32(d, len);
		d += 4;
		d = ffmem_copy(d, val, vallen) + 4;
		v->outlen = d - v->out;
		return 0;
	}

	if (name == NULL) {
		len = ffint_ltoh32(v->out); // get vendor string length
		ffint_htol32(v->out + 4 + len, v->cnt);
		return 0;
	}

	namelen = ffsz_len(name);
	len = namelen + FFSLEN("=") + vallen;
	if (v->outlen + 4 + len > v->outcap)
		return FFVORBTAG_ERR;

	ffint_htol32(d, len);
	d += 4;
	d += ffs_upper(d, end, name, namelen);
	*d++ = '=';
	d = ffmem_copy(d, val, vallen);
	v->outlen = d - v->out;
	v->cnt++;
	return 0;
}
