/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/ape.h>
#include <FF/array.h>


struct ape_desc {
	char id[4]; // "MAC "
	byte ver[2]; // = x.xx * 1000.  >=3.98
	byte skip[2];

	byte desc_size[4];
	byte hdr_size[4];
	byte seektbl_size[4];
	byte wavhdr_size[4];
	byte unused[3 * 4];
	byte md5[16];
};

enum APE_FLAGS {
	APE_F8BIT = 1,
	APE_FPEAK = 4,
	APE_F24BIT = 8,
	APE_FNSEEKEL = 0x10,
	APE_FNOWAVHDR = 0x20,
};

struct ape_hdr {
	byte comp_level[2]; // 1000..5000
	byte flags[2]; // enum APE_FLAGS

	byte frame_blocks[4];
	byte lastframe_blocks[4];
	byte total_frames[4];

	byte bps[2];
	byte channels[2];
	byte rate[4];
};

struct ape_hdr_old {
	char id[4];
	byte ver[2]; // <=3.97
	byte comp_level[2];
	byte flags[2]; // enum APE_FLAGS

	byte channels[2];
	byte rate[4];

	byte wavhdr_size[4];
	byte unused[4];

	byte total_frames[4];
	byte lastframe_blocks[4];

	//byte peak_level[4];
	//byte seekpoints[4];
};

enum {
	APE_MINHDR = sizeof(struct ape_desc) + sizeof(struct ape_hdr),
};


static int ape_parse(ffape_info *info, const char *data, size_t len);
static int _ape_parse_old(ffape_info *info, const char *data, size_t len);

static int _ffape_id31(ffape *a);
static int _ffape_apetag(ffape *a);


const char ffape_comp_levelstr[][8] = { "", "fast", "normal", "high", "x-high", "insane" };


enum {
	APE_EOK,
	APE_EHDR,
	APE_EFMT,
	APE_ESMALL,
	APE_EAPETAG,
};

static const char *const ape_errstr[] = {
	"",
	"invalid header",
	"unsupported format",
	"too small input data",
	"bad APEv2 tag",
};

const char* ffape_errstr(ffape *a)
{
	return ape_errstr[a->err];
}


enum {
	I_HDR, I_TAGSEEK, I_ID31, I_APE2_FIRST, I_APE2, I_TAGSFIN, I_HDRFIN,
	I_FR,
};

void ffape_close(ffape *a)
{
	if (a->is_apetag)
		ffapetag_parse_fin(&a->apetag);
}

static int _ape_parse_old(ffape_info *info, const char *data, size_t len)
{
	const struct ape_hdr_old *h;
	const char *p;
	h = (void*)data;

	if (sizeof(struct ape_hdr_old) > len)
		return 0;

	uint flags = ffint_ltoh16(h->flags);

	p = data + sizeof(struct ape_hdr_old);

	uint sz = sizeof(struct ape_hdr_old);
	if (flags & APE_FPEAK)
		sz += sizeof(int);
	if (flags & APE_FNSEEKEL)
		sz += sizeof(int);
	if (sz > len)
		return 0;

	uint comp_level = ffint_ltoh16(h->comp_level);
	if (!(comp_level % 1000) && comp_level <= 5000)
		info->comp_level = comp_level / 1000;

	uint frame_blocks;
	if (info->version >= 3950)
		frame_blocks = 73728 * 4;
	else if (info->version >= 3900 || (info->version >= 3800 && comp_level == 4000))
		frame_blocks = 73728;
	else
		frame_blocks = 9216;
	info->frame_blocks = frame_blocks;

	uint total_frames = ffint_ltoh32(h->total_frames);
	uint lastframe_blocks = ffint_ltoh32(h->lastframe_blocks);
	if (total_frames != 0 && lastframe_blocks < frame_blocks)
		info->total_samples = (total_frames - 1) * frame_blocks + lastframe_blocks;

	if (flags & APE_FPEAK)
		p += 4;

	if (flags & APE_FNSEEKEL) {
		uint seekpts = ffint_ltoh32(p);
		p += 4;
		info->seekpoints = seekpts;
	} else
		info->seekpoints = total_frames;

	info->fmt.format = 16;
	if (flags & APE_F8BIT)
		info->fmt.format = 8;
	else if (flags & APE_F24BIT)
		info->fmt.format = 24;

	info->fmt.channels = ffint_ltoh16(h->channels);
	info->fmt.sample_rate = ffint_ltoh32(h->rate);

	return p - data;
}

/** Parse APE header.
Return bytes processed;  0 if more data is needed;  -APE_E* on error. */
static int ape_parse(ffape_info *info, const char *data, size_t len)
{
	const struct ape_desc *d;
	const struct ape_hdr *h;

	if (len < sizeof(8))
		return 0;

	d = (void*)data;
	if (!ffs_eqcz(d->id, 4, "MAC "))
		return -APE_EHDR;
	info->version = ffint_ltoh16(d->ver);

	if (info->version < 3980)
		return _ape_parse_old(info, data, len);

	uint desc_size = ffmax(sizeof(struct ape_desc), ffint_ltoh32(d->desc_size));
	uint hdr_size = ffmax(sizeof(struct ape_hdr), ffint_ltoh32(d->hdr_size));
	if ((uint64)desc_size + hdr_size > len)
		return 0;

	h = (void*)(data + desc_size);

	uint comp_level = ffint_ltoh16(h->comp_level);
	if (!(comp_level % 1000) && comp_level <= 5000)
		info->comp_level = comp_level / 1000;

	info->frame_blocks = ffint_ltoh32(h->frame_blocks);

	uint total_frames = ffint_ltoh32(h->total_frames);
	uint lastframe_blocks = ffint_ltoh32(h->lastframe_blocks);
	if (total_frames != 0 && lastframe_blocks < info->frame_blocks)
		info->total_samples = (total_frames - 1) * info->frame_blocks + lastframe_blocks;

	uint bps = ffint_ltoh16(h->bps);
	if (bps > 32)
		return -APE_EFMT;
	info->fmt.format = bps;

	info->fmt.channels = ffint_ltoh16(h->channels);
	info->fmt.sample_rate = ffint_ltoh32(h->rate);

	uint seektbl_size = ffint_ltoh32(d->seektbl_size);
	info->seekpoints = seektbl_size / 4;

	ffmemcpy(info->md5, d->md5, sizeof(d->md5));

	return desc_size + hdr_size;
}

static int _ffape_hdr(ffape *a)
{
	int r = ape_parse(&a->info, a->data, a->datalen);
	if (r == 0) {
		a->err = APE_ESMALL;
		return FFAPE_RERR;
	} else if (r < 0) {
		a->err = -r;
		return FFAPE_RERR;
	}
	FFARR_SHIFT(a->data, a->datalen, r);
	a->off += r;
	return 0;
}

static int _ffape_id31(ffape *a)
{
	int r;
	size_t len;

	len = a->datalen;
	r = ffid31_parse(&a->id31tag, a->data, &len);
	FFARR_SHIFT(a->data, a->datalen, len);
	a->off += len;

	switch (r) {
	case FFID3_RNO:
		a->state = I_APE2_FIRST;
		return 0;

	case FFID3_RDONE:
		a->total_size -= sizeof(ffid31);
		a->off = a->total_size - ffmin(sizeof(ffapehdr), a->total_size);
		a->state = I_APE2_FIRST;
		return FFAPE_RSEEK;

	case FFID3_RDATA:
		return FFAPE_RTAG;

	case FFID3_RMORE:
		return FFAPE_RMORE;
	}

	FF_ASSERT(0);
	return FFAPE_RERR;
}

static int _ffape_apetag(ffape *a)
{
	int r;
	size_t len;

	len = a->datalen;
	r = ffapetag_parse(&a->apetag, a->data, &len);
	FFARR_SHIFT(a->data, a->datalen, len);
	a->off += len;

	switch (r) {
	case FFAPETAG_RDONE:
		a->total_size -= ffapetag_size(&a->apetag.ftr);
		// break
	case FFAPETAG_RNO:
		a->is_apetag = 0;
		ffapetag_parse_fin(&a->apetag);
		a->state = I_TAGSFIN;
		return 0;

	case FFAPETAG_RTAG:
		return FFAPE_RTAG;

	case FFAPETAG_RSEEK:
		a->off += a->apetag.seekoff;
		return FFAPE_RSEEK;

	case FFAPETAG_RMORE:
		return FFAPE_RMORE;

	case FFAPETAG_RERR:
		a->state = I_TAGSFIN;
		a->err = APE_EAPETAG;
		return FFAPE_RWARN;

	default:
		FF_ASSERT(0);
	}
	//unreachable
	return FFAPE_RERR;
}

int ffape_decode(ffape *a)
{
	int r;

	for (;;) {
	switch (a->state) {

	case I_HDR:
		if (0 != (r = _ffape_hdr(a)))
			return r;

		if (a->total_size != 0) {
			a->froff = a->off;
			a->state = I_TAGSEEK;
		} else
			a->state = I_HDRFIN;
		return FFAPE_RHDR;

	case I_TAGSEEK:
		if (a->options & FFAPE_O_ID3V1)
			a->state = I_ID31;
		else if (a->options & FFAPE_O_APETAG)
			a->state = I_APE2_FIRST;
		else {
			a->state = I_HDRFIN;
			continue;
		}
		a->off = a->total_size - ffmin(sizeof(ffid31), a->total_size);
		return FFAPE_RSEEK;

	case I_ID31:
		if (0 != (r = _ffape_id31(a)))
			return r;
		// break

	case I_APE2_FIRST:
		if (!(a->options & FFAPE_O_APETAG)) {
			a->state = I_TAGSFIN;
			continue;
		}
		a->is_apetag = 1;
		a->datalen = ffmin(a->total_size - a->off, a->datalen);
		a->state = I_APE2;
		// break

	case I_APE2:
		if (0 != (r = _ffape_apetag(a)))
			return r;
		// break

	case I_TAGSFIN:
		a->state = I_HDRFIN;
		a->off = a->froff;
		return FFAPE_RSEEK;

	case I_HDRFIN:
		a->state = I_FR;
		return FFAPE_RHDRFIN;

	case I_FR:
		return FFAPE_RERR;
	}
	}

	//unreachable
	return 0;
}
