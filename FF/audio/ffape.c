/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/ape.h>
#include <FF/array.h>
#include <FFOS/error.h>


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
static uint ape_total_frames(const ffape_info *info);

static ssize_t ape_seektab(const char *data, size_t len, uint **seektab, const ffape_info *info, uint64 total_size);
static int ape_seektab_finish(uint *seektab, uint npts, uint64 total_size);

static int _ffape_id31(ffape *a);
static int _ffape_apetag(ffape *a);
static int _ffape_init(ffape *a);


const char ffape_comp_levelstr[][8] = { "", "fast", "normal", "high", "x-high", "insane" };


enum APE_E {
	APE_EOK,
	APE_EHDR,
	APE_EFMT,
	APE_ESMALL,
	APE_EAPETAG,
	APE_ESEEKTAB,

	APE_ESYS,
};

static const char *const ape_errstr[] = {
	"",
	"invalid header",
	"unsupported format",
	"too small input data",
	"bad APEv2 tag",
	"bad seek table",
};

const char* ffape_errstr(ffape *a)
{
	if (a->err == APE_ESYS)
		return fferr_strp(fferr_last());
	else if (a->err < 0)
		return ape_decode_errstr(a->err);

	return ape_errstr[a->err];
}


enum {
	I_HDR, I_SEEKTAB, I_HDR2, I_TAGSEEK, I_ID31, I_APE2_FIRST, I_APE2, I_TAGSFIN, I_HDRFIN,
	I_INIT, I_FR, I_NEXT, I_SEEK,
};

void ffape_close(ffape *a)
{
	if (a->is_apetag)
		ffapetag_parse_fin(&a->apetag);
	if (a->ap != NULL)
		ape_decode_free(a->ap);
	ffmem_safefree(a->seektab);
	ffmem_safefree(a->pcmdata);
	ffarr_free(&a->buf);
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
	info->comp_level_orig = comp_level;

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

	if (!(flags & APE_FNOWAVHDR)) {
		uint wavhdr_size = ffint_ltoh32(h->wavhdr_size);
		if (sz + wavhdr_size > len)
			return 0;
		p += wavhdr_size;
	}

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
	info->comp_level_orig = comp_level;

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

	uint flags = ffint_ltoh16(h->flags);
	if (!(flags & APE_FNOWAVHDR))
		info->wavhdr_size = ffint_ltoh32(d->wavhdr_size);

	return desc_size + hdr_size;
}

static uint ape_total_frames(const ffape_info *info)
{
	return info->total_samples / info->frame_blocks + ((info->total_samples % info->frame_blocks) != 0);
}

/** Parse seek table.
Return the number of entries;  enum APE_E on error (negative value). */
static ssize_t ape_seektab(const char *data, size_t len, uint **seektab, const ffape_info *info, uint64 total_size)
{
	uint *sp, i, total_frames, npts;

	total_frames = ape_total_frames(info);
	npts = ffmin(info->seekpoints, total_frames);
	if (npts == 0)
		return 0;

	if (len < info->seekpoints * sizeof(uint))
		return -APE_ESMALL;

	if (NULL == (sp = ffmem_talloc(uint, npts + 1)))
		return -APE_ESYS;

	for (i = 0;  i != npts;  i++) {
		uint off = ffint_ltoh32(data + i * sizeof(uint));
		if (i != 0 && off <= sp[i - 1])
			goto err; //offsets must grow

		sp[i] = off;
	}

	*seektab = sp;
	return npts + 1;

err:
	ffmem_free(sp);
	return -APE_ESEEKTAB;
}

/** Validate file offset and complete the last seek point. */
static int ape_seektab_finish(uint *seektab, uint npts, uint64 total_size)
{
	if (seektab[npts - 2] >= total_size)
		return APE_ESEEKTAB; //too large offset

	seektab[npts - 1] = total_size;
	return 0;
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

static uint frame_offset(ffape *a, uint frame)
{
	uint align4 = (a->seektab[frame] - a->seektab[0]) % 4;
	return a->seektab[frame] - align4;
}

/** Return frame size aligned on both sides to 4-byte boundary. */
static uint frame_size(ffape *a, uint frame)
{
	uint off = frame_offset(a, frame);
	uint off_next = frame_offset(a, frame + 1);
	uint align4_next = (a->seektab[frame + 1] - a->seektab[0]) % 4;
	if (align4_next != 0)
		off_next += 4;
	return off_next - off;
}

static uint frame_samples(ffape *a, uint frame)
{
	return (frame != ape_total_frames(&a->info) - 1)
		? a->info.frame_blocks
		: a->info.total_samples % a->info.frame_blocks;
}

void ffape_seek(ffape *a, uint64 sample)
{
	uint frame = sample / a->info.frame_blocks;
	if (frame >= a->nseekpts)
		return;
	a->seeksample = sample;
	if (a->state != I_INIT)
		a->state = I_SEEK;
}

static int _ffape_init(ffape *a)
{
	int r;
	struct ape_info info;
	info.version = a->info.version;
	info.compressionlevel = a->info.comp_level_orig;
	info.bitspersample = a->info.fmt.format;
	info.samplerate = a->info.fmt.sample_rate;
	info.channels = a->info.fmt.channels;
	if (0 != (r = ape_decode_init(&a->ap, &info))) {
		a->err = r;
		return FFAPE_RERR;
	}

	if (NULL == (a->pcmdata = ffmem_alloc(a->info.frame_blocks * ffpcm_size1(&a->info.fmt)))) {
		a->err = APE_ESYS;
		return FFAPE_RERR;
	}

	return 0;
}

int ffape_decode(ffape *a)
{
	int r;

	for (;;) {
	switch (a->state) {

	case I_HDR:
		if (0 != (r = _ffape_hdr(a)))
			return r;
		a->state = I_SEEKTAB;
		// break

	case I_SEEKTAB:
		r = ffarr_append_until(&a->buf, a->data, a->datalen, a->info.seekpoints * sizeof(uint));
		if (r == 0)
			return FFAPE_RMORE;
		else if (r == -1) {
			a->err = APE_ESYS;
			return FFAPE_RERR;
		}
		FFARR_SHIFT(a->data, a->datalen, r);
		a->off += a->info.seekpoints * sizeof(uint);

		r = ape_seektab(a->buf.ptr, a->buf.len, &a->seektab, &a->info, a->total_size);
		a->buf.len = 0;
		if (r < 0) {
			a->err = -r;
			return FFAPE_RERR;
		}
		a->nseekpts = r;
		if (a->nseekpts != ape_total_frames(&a->info) + 1) {
			a->err = APE_ESEEKTAB;
			return FFAPE_RERR;
		}

		if (a->info.wavhdr_size != 0) {
			a->off += a->info.wavhdr_size;
			a->state = I_HDR2;
			return FFAPE_RSEEK;
		}
		// break

	case I_HDR2:
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
		if (0 != (r = ape_seektab_finish(a->seektab, a->nseekpts, a->total_size))) {
			a->err = -r;
			return FFAPE_RERR;
		}
		a->state = I_INIT;
		return FFAPE_RHDRFIN;

	case I_INIT:
		if (0 != (r = _ffape_init(a)))
			return r;
		if (a->seeksample != 0) {
			a->state = I_SEEK;
			continue;
		}
		a->state = I_FR;
		continue;

	case I_NEXT:
		a->cursample += a->pcmlen / ffpcm_size1(&a->info.fmt);
		if (a->cursample == a->info.total_samples)
			return FFAPE_RDONE;
		a->state = I_FR;
		// break

	case I_FR:
		{
		uint frame = a->cursample / a->info.frame_blocks;
		FF_ASSERT(frame < a->nseekpts);
		uint frsize = frame_size(a, frame);
		uint align4 = (a->seektab[frame] - a->seektab[0]) % 4;

		r = ffarr_append_until(&a->buf, a->data, a->datalen, frsize);
		if (r == 0)
			return FFAPE_RMORE;
		else if (r == -1) {
			a->err = APE_ESYS;
			return FFAPE_RERR;
		}
		FFARR_SHIFT(a->data, a->datalen, r);
		a->off += frsize;

		FFDBG_PRINTLN(10, "frame #%u  size:%u", frame, frsize);

		a->pcm = a->pcmdata;
		a->pcmlen = frame_samples(a, frame);
		r = ape_decode(a->ap, a->buf.ptr, a->buf.len, (void*)a->pcm, &a->pcmlen, align4);

		uint off = frame_offset(a, frame);
		uint off_next = frame_offset(a, frame + 1);
		_ffarr_rmleft(&a->buf, off_next - off, sizeof(char));
		if (a->buf.cap == 0) {
			// a->buf points to memory region of a->data
			FFARR_SHIFT(a->data, a->datalen, -(ssize_t)a->buf.len);
			a->off -= a->buf.len;
			a->buf.len = 0;
		}

		if (r != 0) {
			a->pcmlen *= ffpcm_size1(&a->info.fmt);
			a->state = I_NEXT;
			a->err = r;
			return FFAPE_RWARN;
		}
		}

		if (a->seekdone) {
			a->seekdone = 0;
			uint n = a->seeksample - a->cursample;
			FF_ASSERT(n < a->pcmlen);
			a->cursample += n;
			a->pcm = (char*)a->pcm + n * ffpcm_size1(&a->info.fmt);
			a->pcmlen -= n;
		}

		a->pcmlen *= ffpcm_size1(&a->info.fmt);
		a->state = I_NEXT;
		return FFAPE_RDATA;

	case I_SEEK:
		{
		uint frame = a->seeksample / a->info.frame_blocks;
		a->off = frame_offset(a, frame);
		a->cursample = frame * a->info.frame_blocks;
		a->seekdone = 1;
		a->buf.len = 0;
		a->state = I_FR;
		return FFAPE_RSEEK;
		}
	}
	}

	//unreachable
	return 0;
}
