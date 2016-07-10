/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/wavpack.h>
#include <FFOS/error.h>


enum WVPK_FLAGS {
	WVPK_FBAD = 1 << 31,
};

struct wvpk_hdr {
	char id[4]; // "wvpk"
	uint size;

	ushort version; // 0x04XX
	byte unused[2];
	uint total_samples;
	uint index;
	uint samples;
	uint flags; // enum WVPK_FLAGS
	uint crc;
};

enum {
	MAX_NOSYNC = 1 * 1024 * 1024,
	WVPK_FTRTAGS_CHKSIZE = 1000,
};


static int wvpk_parse(struct wvpk_hdr *h, const char *data, size_t len);
static int wvpk_findblock(const char *data, size_t len, struct wvpk_hdr *h);


static int wvpk_hdrinfo(ffwvpack *w, const wavpack_info *inf);
static int wvpk_id31(ffwvpack *w);
static int wvpk_ape(ffwvpack *w);
static int wvpk_seek(ffwvpack *w);
static int _ffwvpk_gethdr(ffwvpack *w, struct wvpk_hdr *hdr);


const char *const ffwvpk_comp_levelstr[] = { "", "fast", "high", "very high", "extra" };


static const char *const wvpk_err[] = {
	"invalid header",
	"unsupported PCM format",
	"seek",
	"bad APEv2 tag",
	"found sync",
	"can't find sync",
};

const char* ffwvpk_errstr(ffwvpack *w)
{
	switch (w->err) {
	case FFWVPK_ESYS:
		return fferr_strp(fferr_last());

	case FFWVPK_EDECODE:
		return wavpack_errstr(w->wp);
	}

	FF_ASSERT(w->err < FFCNT(wvpk_err));
	return wvpk_err[w->err];
}


/** Parse header.
Return bytes processed;  0 if more data is needed;  -FFWVPK_E* on error. */
static int wvpk_parse(struct wvpk_hdr *hdr, const char *data, size_t len)
{
	const struct wvpk_hdr *h = (void*)data;

	if (len < sizeof(struct wvpk_hdr))
		return 0;

	if (!ffs_eqcz(h->id, 4, "wvpk"))
		return -FFWVPK_EHDR;

	hdr->size = ffint_ltoh32(&h->size) + 8;
	hdr->version = ffint_ltoh16(&h->version);
	uint flags = ffint_ltoh32(&h->flags);

	if ((hdr->size & 0xfff00001) || (flags & WVPK_FBAD))
		return -FFWVPK_EHDR;

	hdr->total_samples = ffint_ltoh32(&h->total_samples);
	hdr->index = ffint_ltoh32(&h->index);
	hdr->samples = ffint_ltoh32(&h->samples);
	return sizeof(struct wvpk_hdr);
}

/**
Return offset of the block;  -1 on error. */
static int wvpk_findblock(const char *data, size_t len, struct wvpk_hdr *h)
{
	const char *d = data, *end = data + len;
	int r;

	while (d != end) {

		if (d[0] != 'w' && NULL == (d = ffs_findc(d, end - d, 'w')))
			break;

		if (sizeof(struct wvpk_hdr) == (r = wvpk_parse(h, d, end - d)))
			return d - data;

		d++;
	}

	return -1;
}


static FFINL int wvpk_hdrinfo(ffwvpack *w, const wavpack_info *inf)
{
	int mode = inf->mode;
	w->fmt.channels = inf->channels;
	w->fmt.sample_rate = inf->rate;

	switch (inf->bps) {
	case 16:
		w->fmt.format = FFPCM_16LE;
		break;

	case 32:
		if (mode & MODE_FLOAT)
			w->fmt.format = FFPCM_FLOAT;
		else
			w->fmt.format = FFPCM_32LE;
		break;

	default:
		w->err = FFWVPK_EFMT;
		return FFWVPK_RERR;
	}
	w->frsize = ffpcm_size1(&w->fmt);

	if (mode & MODE_LOSSLESS)
		w->info.lossless = 1;

	if (mode & MODE_MD5)
		ffmemcpy(w->info.md5, inf->md5, sizeof(w->info.md5));

	if (mode & MODE_EXTRA)
		w->info.comp_level = 4;
	else if (mode & MODE_VERY_HIGH)
		w->info.comp_level = 3;
	else if (mode & MODE_HIGH)
		w->info.comp_level = 2;
	else if (mode & MODE_FAST)
		w->info.comp_level = 1;

	return FFWVPK_RDONE;
}

void ffwvpk_close(ffwvpack *w)
{
	if (w->is_apetag)
		ffapetag_parse_fin(&w->apetag);

	if (w->wp != NULL)
		wavpack_decode_free(w->wp);
	ffmem_safefree(w->pcm);
	ffarr_free(&w->buf);
}

enum { I_OPEN, I_BLOCKHDR, I_BLOCK, I_HDR, I_HDRFIN, I_DATA, I_SEEK,
	I_TAGSEEK, I_FTRTAGS, I_ID31, I_APE2, I_APE2_MORE, I_TAGSFIN };

void ffwvpk_seek(ffwvpack *w, uint64 sample)
{
	if (w->seektab[1].sample == 0)
		return;

	w->seek_sample = sample;
	w->state = I_SEEK;
	w->buf.len = 0;
	w->skoff = -1;

	ffmemcpy(w->seekpt, w->seektab, sizeof(w->seekpt));
}

static FFINL int wvpk_id31(ffwvpack *w)
{
	if (w->buf.len < sizeof(ffid31))
		return 0;

	int r = ffid31_parse(&w->id31tag, ffarr_end(&w->buf) - sizeof(ffid31), sizeof(ffid31));

	switch (r) {
	case FFID3_RNO:
		break;

	case FFID3_RDONE:
		w->buf.len -= sizeof(ffid31);
		w->total_size -= sizeof(ffid31);
		w->off -= sizeof(ffid31);
		break;

	case FFID3_RDATA:
		w->tag = w->id31tag.field;
		w->tagval = w->id31tag.val;
		return FFWVPK_RTAG;

	default:
		FF_ASSERT(0);
	}

	ffmem_tzero(&w->id31tag);
	return 0;
}

static FFINL int wvpk_ape(ffwvpack *w)
{
	for (;;) {

	size_t len = w->buf.len;
	int r = ffapetag_parse(&w->apetag, w->buf.ptr, &len);

	switch (r) {
	case FFAPETAG_RDONE:
	case FFAPETAG_RNO:
		w->is_apetag = 0;
		ffapetag_parse_fin(&w->apetag);
		return 0;

	case FFAPETAG_RFOOTER:
		w->is_apetag = 1;
		w->total_size -= w->apetag.size;
		continue;

	case FFAPETAG_RTAG:
		w->is_apetag = 1;
		return FFWVPK_RTAG;

	case FFAPETAG_RSEEK:
		w->off -= w->apetag.size;
		w->state = I_APE2_MORE;
		return FFWVPK_RSEEK;

	case FFAPETAG_RMORE:
		w->state = I_APE2_MORE;
		return FFWVPK_RMORE;

	case FFAPETAG_RERR:
		w->state = I_TAGSFIN;
		w->err = FFWVPK_EAPE;
		return FFWVPK_RWARN;

	default:
		FF_ASSERT(0);
	}
	}
	//unreachable
}

/**
Return 0 on success.  w->data points to block body. */
static int _ffwvpk_gethdr(ffwvpack *w, struct wvpk_hdr *hdr)
{
	int r, n = 0;

	struct ffbuf_gather d = {0};
	ffstr_set(&d.data, w->data, w->datalen);
	d.ctglen = sizeof(struct wvpk_hdr);

	while (FFBUF_DONE != (r = ffbuf_gather(&w->buf, &d))) {

		if (r == FFBUF_ERR) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;

		} else if (r == FFBUF_MORE) {
			goto more;
		}

		if (-1 != (n = wvpk_findblock(w->buf.ptr, w->buf.len, hdr)))
			d.off = n + 1;
	}
	w->off += d.data.ptr - (char*)w->data;
	w->data = d.data.ptr;
	w->datalen = d.data.len;

	w->blksize = hdr->size;

	if (w->bytes_skipped != 0)
		w->bytes_skipped = 0;

	if (n != 0) {
		w->err = FFWVPK_ESYNC;
		return FFWVPK_RWARN;
	}
	return 0;

more:
	w->bytes_skipped += w->datalen;
	if (w->bytes_skipped > MAX_NOSYNC) {
		w->err = FFWVPK_ENOSYNC;
		return FFWVPK_RERR;
	}

	w->off += w->datalen;
	return FFWVPK_RMORE;
}

static FFINL int wvpk_seek(ffwvpack *w)
{
	struct wvpk_hdr hdr;
	struct ffpcm_seek sk;

	int r = _ffwvpk_gethdr(w, &hdr);
	if (r != 0 && (r != FFWVPK_RWARN || w->err != FFWVPK_ESYNC))
		return r;

	sk.target = w->seek_sample;
	sk.off = w->off - w->buf.len;
	sk.lastoff = w->skoff;
	sk.pt = w->seekpt;
	sk.fr_index = hdr.index;
	sk.fr_samples = hdr.samples;
	sk.avg_fr_samples = hdr.samples;
	sk.fr_size = hdr.size;
	sk.flags = FFPCM_SEEK_ALLOW_BINSCH;

	r = ffpcm_seek(&sk);
	if (r == 1) {
		w->off = w->skoff = sk.off;
		w->buf.len = 0;
		return FFWVPK_RSEEK;

	} else if (r == -1) {
		w->err = FFWVPK_ESEEK;
		return FFWVPK_RERR;
	}

	w->blk_samples = hdr.samples;
	w->samp_idx = hdr.index;
	return FFWVPK_RDONE;
}

int ffwvpk_decode(ffwvpack *w)
{
	int n;
	uint i, isrc;
	int r;
	struct wvpk_hdr hdr;

	for (;;) {
	switch (w->state) {

	case I_OPEN:
		w->state = I_BLOCKHDR;
		// break

	case I_BLOCKHDR:
		if (w->off == w->total_size || (w->datalen == 0 && w->fin))
			return FFWVPK_RDONE;

		r = _ffwvpk_gethdr(w, &hdr);
		if (r != 0 && (r != FFWVPK_RWARN || w->err != FFWVPK_ESYNC))
			return r;

		if (!w->hdr_done) {
			w->info.total_samples = hdr.total_samples;
			w->info.version = hdr.version;
			w->info.block_samples = hdr.samples;
		}

		w->blk_samples = hdr.samples;
		w->samp_idx = hdr.index;

		w->state = I_BLOCK;
		if (r != 0)
			return r; // FFWVPK_ESYNC
		// break

	case I_BLOCK:
		r = ffarr_append_until(&w->buf, w->data, w->datalen, w->blksize);
		if (r == 0) {
			w->off += w->datalen;
			return FFWVPK_RMORE;
		} else if (r == -1) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		}
		FFARR_SHIFT(w->data, w->datalen, r);
		w->off += r;

		w->state = (w->hdr_done) ? I_DATA : I_HDR;
		continue;

	case I_HDR:
		if (NULL == (w->wp = wavpack_decode_init())) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		}

		wavpack_info info = {0};
		r = wavpack_read_header(w->wp, w->buf.ptr, w->buf.len, &info);
		if (r == -1) {
			w->err = FFWVPK_EDECODE;
			return FFWVPK_RERR;
		}
		w->buf.len = 0;

		if (FFWVPK_RDONE != wvpk_hdrinfo(w, &info))
			return FFWVPK_RERR;
		w->hdr_done = 1;

		if (w->total_size != 0) {
			w->state = I_TAGSEEK;
		} else
			w->state = I_HDRFIN;
		return FFWVPK_RHDR;

	case I_HDRFIN:
		if (w->info.total_samples != 0 && w->total_size != 0) {
			w->seektab[1].sample = w->info.total_samples;
			w->seektab[1].off = w->total_size;
		}
		w->state = I_BLOCKHDR;
		return FFWVPK_RHDRFIN;


	case I_SEEK:
		if (FFWVPK_RDONE != (r = wvpk_seek(w)))
			return r;
		w->seek_ok = 1;
		w->state = I_BLOCK;
		continue;


	case I_TAGSEEK:
		if (w->options & (FFWVPK_O_ID3V1 | FFWVPK_O_APETAG)) {
			w->state = I_FTRTAGS;
			w->off = w->total_size - ffmin(WVPK_FTRTAGS_CHKSIZE, w->total_size);
			return FFWVPK_RSEEK;
		}
		w->state = I_HDRFIN;
		continue;

	case I_FTRTAGS:
		r = ffarr_append_until(&w->buf, w->data, w->datalen, ffmin(WVPK_FTRTAGS_CHKSIZE, w->total_size));
		if (r < 0) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		} else if (r == 0) {
			w->off += w->datalen;
			return FFWVPK_RMORE;
		}
		FFARR_SHIFT(w->data, w->datalen, r);
		w->off += r;
		w->state = I_ID31;
		// break

	case I_ID31:
		if ((w->options & FFWVPK_O_ID3V1) && 0 != (i = wvpk_id31(w)))
			return i;
		w->state = I_APE2;
		continue;

	case I_APE2_MORE:
		ffarr_free(&w->buf);
		ffstr_set(&w->buf, w->data, w->datalen);
		w->state = I_APE2;
		// break

	case I_APE2:
		if ((w->options & FFWVPK_O_APETAG) && 0 != (r = wvpk_ape(w)))
			return r;
		w->state = I_TAGSFIN;
		// break

	case I_TAGSFIN:
		w->buf.len = 0;
		w->off = 0;
		w->state = I_HDRFIN;
		return FFWVPK_RSEEK;


	case I_DATA:
		FFDBG_PRINT(10, "%s(): index:%U  block-size:%u  samples:%u\n"
			, FF_FUNC, w->samp_idx, (int)w->buf.len, w->blk_samples);

		if (w->outcap < w->blk_samples) {
			if (NULL == (w->pcm = ffmem_saferealloc(w->pcm, w->blk_samples * sizeof(int) * w->fmt.channels))) {
				w->err = FFWVPK_ESYS;
				return FFWVPK_RERR;
			}
			w->outcap = w->blk_samples;
		}

		n = wavpack_decode(w->wp, w->buf.ptr, w->buf.len, w->pcm32, w->outcap);
		w->buf.len = 0;
		if (n == -1) {
			w->err = FFWVPK_EDECODE;
			return FFWVPK_RERR;
		} else if (n == 0) {
			w->state = I_BLOCKHDR;
			continue;
		}

		goto pcm;
	}
	}

pcm:
	isrc = 0;
	if (w->seek_ok) {
		w->seek_ok = 0;
		isrc = w->seek_sample - w->samp_idx;
		w->samp_idx += isrc;
		n -= isrc;
		isrc *= w->fmt.channels;
	}

	switch (w->fmt.format) {
	case FFPCM_16LE:
		//in-place conversion: int[] -> short[]
		for (i = 0;  i != n * w->fmt.channels;  i++, isrc++) {
			w->pcm[i] = (short)w->pcm32[isrc];
		}
		break;

	case FFPCM_32LE:
	case FFPCM_FLOAT:
		if (isrc != 0) {
			for (i = 0;  i != n * w->fmt.channels;  i++, isrc++) {
				w->pcm32[i] = w->pcm32[isrc];
			}
		}
		break;
	}

	w->pcmlen = n * w->frsize;

	w->state = I_BLOCKHDR;
	return FFWVPK_RDATA;
}
