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
};


static int wvpk_parse(struct wvpk_hdr *h, const char *data, size_t len);
static int wvpk_findblock(const char *data, size_t len, struct wvpk_hdr *h);


static int wvpk_hdr(ffwvpack *w);
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
		return WavpackGetErrorMessage(w->wp);
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


static FFINL int wvpk_hdr(ffwvpack *w)
{
	int mode;

	mode = WavpackGetMode(w->wp);

	w->fmt.channels = WavpackGetNumChannels(w->wp);
	w->fmt.sample_rate = WavpackGetSampleRate(w->wp);

	switch (WavpackGetBitsPerSample(w->wp)) {
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
		WavpackGetMD5Sum(w->wp, w->info.md5);

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
	if (!w->apetag_closed)
		ffapetag_parse_fin(&w->apetag);

	ffid31_parse_fin(&w->id31tag);

	if (w->wp != NULL)
		WavpackCloseFile(w->wp);
	ffmem_safefree(w->pcm);
	ffarr_free(&w->buf);
}

enum { I_OPEN, I_BLOCKHDR, I_BLOCK, I_HDR, I_HDRFIN, I_DATA, I_SEEK,
	I_TAGSEEK, I_ID31, I_APE2_FIRST, I_APE2, I_TAGSFIN };

void ffwvpk_seek(ffwvpack *w, uint64 sample)
{
	if (w->seektab[1].sample == 0)
		return;

	w->seek_sample = sample;
	w->state = I_SEEK;
	w->buf.len = 0;

	ffmemcpy(w->seekpt, w->seektab, sizeof(w->seekpt));
}

static FFINL int wvpk_id31(ffwvpack *w)
{
	int r;
	size_t len;

	len = w->datalen;
	r = ffid31_parse(&w->id31tag, w->data, &len);
	w->data += len;
	w->datalen -= len;
	w->off += len;

	switch (r) {
	case FFID3_RNO:
		w->state = I_APE2_FIRST;
		return FFWVPK_RDONE;

	case FFID3_RDONE:
		w->total_size -= sizeof(ffid31);
		w->off = w->total_size - ffmin(sizeof(ffapehdr), w->total_size);
		w->state = I_APE2_FIRST;
		return FFWVPK_RSEEK;

	case FFID3_RDATA:
		return FFWVPK_RTAG;

	case FFID3_RMORE:
		return FFWVPK_RMORE;
	}

	FF_ASSERT(0);
	return FFWVPK_RERR;
}

static FFINL int wvpk_ape(ffwvpack *w)
{
	int r;
	size_t len;

	len = w->datalen;
	r = ffapetag_parse(&w->apetag, w->data, &len);
	w->data += len;
	w->datalen -= len;
	w->off += len;

	switch (r) {
	case FFAPETAG_RDONE:
		w->total_size -= ffapetag_size(&w->apetag.ftr);
		// break
	case FFAPETAG_RNO:
		ffapetag_parse_fin(&w->apetag);
		w->apetag_closed = 1;
		w->state = I_TAGSFIN;
		return 0;

	case FFAPETAG_RTAG:
		w->is_apetag = 1;
		return FFWVPK_RTAG;

	case FFAPETAG_RSEEK:
		w->off += w->apetag.seekoff;
		return FFWVPK_RSEEK;

	case FFAPETAG_RMORE:
		return FFWVPK_RMORE;

	case FFAPETAG_RERR:
		w->state = I_TAGSFIN;
		w->err = FFWVPK_EAPE;
		return FFWVPK_RWARN;

	default:
		FF_ASSERT(0);
	}
	//unreachable
	return FFWVPK_RERR;
}

/**
Return 0 on success.  If w->buf is used, w->data points to block body, otherwise it points to block header. */
static int _ffwvpk_gethdr(ffwvpack *w, struct wvpk_hdr *hdr)
{
	uint lostsync = 0;
	int r;

	if (w->buf.len != 0) {
		uint buflen = w->buf.len;
		r = ffmin(w->datalen, sizeof(struct wvpk_hdr) - buflen);
		if (NULL == ffarr_append(&w->buf, w->data, r)) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		}

		r = wvpk_findblock(w->buf.ptr, w->buf.len, hdr);
		if (r == -1) {
			if (w->datalen > sizeof(struct wvpk_hdr) - buflen) {
				w->buf.len = 0;
				goto dfind;
			}
			goto more;

		} else if (r != 0) {
			lostsync = 1;
			_ffarr_rmleft(&w->buf, r, sizeof(char));
		}

		FFARR_SHIFT(w->data, w->datalen, sizeof(struct wvpk_hdr) - buflen);
		w->off += sizeof(struct wvpk_hdr) - buflen;
		goto done;
	}

dfind:
	r = wvpk_findblock(w->data, w->datalen, hdr);
	if (r == -1) {
		uint n = ffmin(w->datalen, sizeof(struct wvpk_hdr) - 1);
		if (NULL == ffarr_append(&w->buf, w->data + w->datalen - n, n)) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		}

		goto more;

	} else if (r != 0) {
		lostsync = 1;
		FFARR_SHIFT(w->data, w->datalen, r);
		w->off += r;
	}

done:
	w->blksize = hdr->size;

	if (w->bytes_skipped != 0)
		w->bytes_skipped = 0;

	if (lostsync) {
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

	sk.lastoff = w->off;

	int r = _ffwvpk_gethdr(w, &hdr);
	if (r != 0 && (r != FFWVPK_RWARN || w->err != FFWVPK_ESYNC))
		return r;

	sk.target = w->seek_sample;
	sk.off = w->off - w->buf.len;
	sk.pt = w->seekpt;
	sk.fr_index = hdr.index;
	sk.fr_samples = hdr.samples;
	sk.avg_fr_samples = hdr.samples;
	sk.fr_size = hdr.size;
	sk.flags = FFPCM_SEEK_ALLOW_BINSCH;

	r = ffpcm_seek(&sk);
	if (r == 1) {
		w->off = sk.off;
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
		if (r == 0)
			return FFWVPK_RMORE;
		else if (r == -1) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		}
		FFARR_SHIFT(w->data, w->datalen, r);
		w->off += w->blksize;

		w->state = (w->hdr_done) ? I_DATA : I_HDR;
		continue;

	case I_HDR:
		if (NULL == (w->wp = WavpackDecodeInit())) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		}

		r = WavpackReadHeader(w->wp, w->buf.ptr, w->buf.len);
		if (r == -1) {
			w->err = FFWVPK_EDECODE;
			return FFWVPK_RERR;
		}

		if (FFWVPK_RDONE != wvpk_hdr(w))
			return FFWVPK_RERR;
		w->hdr_done = 1;

		if (w->total_size != 0) {
			w->lastoff = w->off;
			w->state = I_TAGSEEK;
		} else
			w->state = I_HDRFIN;
		return FFWVPK_RHDR;

	case I_HDRFIN:
		if (w->info.total_samples != 0 && w->total_size != 0) {
			w->seektab[1].sample = w->info.total_samples;
			w->seektab[1].off = w->total_size;
		}
		w->state = I_DATA;
		return FFWVPK_RHDRFIN;


	case I_SEEK:
		if (FFWVPK_RDONE != (r = wvpk_seek(w)))
			return r;
		w->seek_ok = 1;
		w->state = I_BLOCK;
		continue;


	case I_TAGSEEK:
		if (w->options & FFWVPK_O_ID3V1)
			w->state = I_ID31;
		else if (w->options & FFWVPK_O_APETAG)
			w->state = I_APE2_FIRST;
		else {
			w->state = I_HDRFIN;
			continue;
		}
		w->off = w->total_size - ffmin(sizeof(ffid31), w->total_size);
		return FFWVPK_RSEEK;

	case I_ID31:
		if (FFWVPK_RDONE != (i = wvpk_id31(w)))
			return i;
		// break

	case I_APE2_FIRST:
		if (!(w->options & FFWVPK_O_APETAG)) {
			w->state = I_TAGSFIN;
			continue;
		}
		w->datalen = ffmin(w->total_size - w->off, w->datalen);
		w->state = I_APE2;
		// break

	case I_APE2:
		if (0 != (r = wvpk_ape(w)))
			return r;
		continue;

	case I_TAGSFIN:
		w->off = w->lastoff;
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

		n = WavpackDecode(w->wp, w->buf.ptr, w->buf.len, w->pcm32, w->outcap);
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
