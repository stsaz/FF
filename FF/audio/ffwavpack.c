/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/wavpack.h>
#include <FFOS/error.h>


enum {
	MAX_BUF = 256 * 1024,
	OUT_BUF = 500, //msec
	MAX_SEEK_REQ = 20,
};

static int wvpk_hdr(ffwvpack *w);
static int wvpk_id31(ffwvpack *w);
static int wvpk_ape(ffwvpack *w);
static int wvpk_findpage(ffwvpack *w);
static int wvpk_seek(ffwvpack *w);


static int32_t wvpk_read_bytes(void *id, void *data, int32_t bcount);
static uint32_t wvpk_get_pos(void *id);
static int wvpk_set_pos_abs(void *id, uint32_t pos);
static int wvpk_push_back_byte(void *id, int c);
static uint32_t wvpk_get_length(void *id);
static int wvpk_can_seek(void *id);

static const WavpackStreamReader wvpk_reader = {
	&wvpk_read_bytes, &wvpk_get_pos, &wvpk_set_pos_abs, NULL /*set_pos_rel*/,
	&wvpk_push_back_byte, &wvpk_get_length, &wvpk_can_seek
};


enum { WVPK_READ_AGAIN = -2 };

static int32_t wvpk_read_bytes(void *id, void *data, int32_t _bcount)
{
	ffwvpack *w = id;
	uint all = 0, n, bcount = _bcount;

	if (bcount > w->buf.len - w->bufoff + w->datalen
		&& !w->fin) {
		w->async = 1;
		return WVPK_READ_AGAIN;
	}

	if (w->buf.len != w->bufoff) {
		n = ffmin(w->buf.len - w->bufoff, bcount);
		ffmemcpy(data, w->buf.ptr + w->bufoff, n);
		w->bufoff += n;
		w->off += n;
		all = n;
	}

	if (bcount != all) {
		n = ffmin(w->datalen, bcount - all);
		ffmemcpy(data + all, w->data, n);
		w->data += n;
		w->datalen -= n;
		w->off += n;
		all += n;
	}

	w->async = 0;
	return all;
}

static uint32_t wvpk_get_pos(void *id)
{
	ffwvpack *w = id;
	return w->off;
}

static int wvpk_set_pos_abs(void *id, uint32_t pos)
{
	ffwvpack *w = id;
	w->off = pos;
	return 0;
}

static int wvpk_push_back_byte(void *id, int c)
{
	ffwvpack *w = id;
	FF_ASSERT(w->datalen != 0);
	w->data -= 1;
	w->datalen += 1;
	w->off -= 1;
	return 0;
}

static uint32_t wvpk_get_length(void *id)
{
	ffwvpack *w = id;
	return (int)w->total_size;
}

static int wvpk_can_seek(void *id)
{
	return 0;
}

static const char *const wvpk_err[] = {
	"unsupported PCM format",
	"internal buffer size limit",
	"seek",
	"seek request limit",
	"bad APEv2 tag",
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

	w->outcap = ffpcm_samples(OUT_BUF, w->fmt.sample_rate);
	if (NULL == (w->pcm = ffmem_alloc(w->outcap * sizeof(int) * w->fmt.channels))) {
		w->err = FFWVPK_ESYS;
		return FFWVPK_RERR;
	}

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

enum { I_OPEN, I_HDR, I_HDRFIN, I_DATA, I_SEEK, I_FINDPAGE,
	I_TAGSEEK, I_ID31, I_APE2_FIRST, I_APE2 };

void ffwvpk_seek(ffwvpack *w, uint64 sample)
{
	w->seek_sample = sample;
	w->state = I_SEEK;
	w->seek_cnt = 0;
	w->skip_samples = 0;
	w->bufoff = 0;
	w->buf.len = 0;
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
		w->state = I_HDRFIN;
		w->off = w->lastoff;
		return FFWVPK_RSEEK;

	case FFAPETAG_RTAG:
		w->is_apetag = 1;
		return FFWVPK_RTAG;

	case FFAPETAG_RSEEK:
		w->off += w->apetag.seekoff;
		return FFWVPK_RSEEK;

	case FFAPETAG_RMORE:
		return FFWVPK_RMORE;

	case FFAPETAG_RERR:
		w->state = I_HDRFIN;
		w->err = FFWVPK_EAPE;
		return FFWVPK_RWARN;

	default:
		FF_ASSERT(0);
	}
	//unreachable
	return FFWVPK_RERR;
}

static FFINL int wvpk_findpage(ffwvpack *w)
{
	ffstr d;
	const char *s;

	for (;;) {
		if (w->buf.len != 0)
			ffstr_set(&d, w->buf.ptr, w->buf.len);
		else
			ffstr_set(&d, w->data, w->datalen);

		s = ffs_find(d.ptr, d.len, 'w');
		w->off += s - d.ptr;
		ffstr_shift(&d, s - d.ptr);

		if (d.len < sizeof(WavpackHeader)) {
			if (d.len != 0) {
				if (w->buf.len == 0 && NULL == ffarr_realloc(&w->buf, sizeof(WavpackHeader))) {
					w->err = FFWVPK_ESYS;
					return FFWVPK_RERR;
				}
				w->buf.len += ffs_append(w->buf.ptr, w->buf.len, sizeof(WavpackHeader), d.ptr, d.len);
				w->off += d.len;
			}

			return FFWVPK_RMORE;
		}

		if (w->buf.len == 0) {
			w->data = d.ptr;
			w->datalen = d.len;
		}
		if (WavpackIsHeader(d.ptr))
			break;
		if (w->buf.len != 0)
			w->buf.len = 0;
		else if (w->datalen != 0) {
			w->data += FFSLEN("w");
			w->datalen -= FFSLEN("w");
			w->off += FFSLEN("w");
		}
	}
	return FFWVPK_RDONE;
}

static FFINL int wvpk_seek(ffwvpack *w)
{
	uint64 off = w->off;
	int r = WavpackSeekPage(w->wp, (int)w->seek_sample);
	if (r == 0) {
		w->err = FFWVPK_ESEEK;
		return FFWVPK_RERR;

	} else if (r == -1) {
		if (w->seek_cnt++ == MAX_SEEK_REQ) {
			w->err = FFWVPK_ESEEKLIM;
			return FFWVPK_RERR;
		}

		if (off == w->off) {
			w->err = FFWVPK_ESEEK;
			return FFWVPK_RERR;
		}

		w->datalen = 0;
		w->state = I_FINDPAGE;
		return FFWVPK_RSEEK;
	}

	FF_ASSERT(w->seek_sample >= (uint)r);
	w->skip_samples = w->seek_sample - r;
	return FFWVPK_RDONE;
}

int ffwvpk_decode(ffwvpack *w)
{
	uint n, i, isrc;
	int r;
	size_t datalen;

	for (;;) {
	switch (w->state) {

	case I_OPEN:
		if (NULL == (w->wp = WavpackOpenFileInputEx((void*)&wvpk_reader, w, NULL, NULL, 0, 0))) {
			w->err = FFWVPK_ESYS;
			return FFWVPK_RERR;
		}
		w->state = I_HDR;
		// break

	case I_HDR:
		datalen = w->datalen;
		r = WavpackReadHeader(w->wp);
		if (r == -1) {
			w->err = FFWVPK_EDECODE;
			return FFWVPK_RERR;
		} else if (r == WVPK_READ_AGAIN)
			goto more;

		if (FFWVPK_RDONE != wvpk_hdr(w))
			return FFWVPK_RERR;

		if (w->total_size != 0) {
			w->lastoff = w->off;
			w->state = I_TAGSEEK;
		} else
			w->state = I_HDRFIN;
		return FFWVPK_RHDR;

	case I_HDRFIN:
		w->state = I_DATA;
		return FFWVPK_RHDRFIN;


	case I_FINDPAGE:
		if (FFWVPK_RDONE != (r = wvpk_findpage(w)))
			return r;
		// break

	case I_SEEK:
		if (FFWVPK_RDONE != (r = wvpk_seek(w)))
			return r;
		w->state = I_DATA;
		continue;


	case I_TAGSEEK:
		if (w->options & FFWVPK_O_ID3V1)
			w->state = I_ID31;
		else if (w->options & FFWVPK_O_APETAG)
			w->state = I_APE2_FIRST;
		else {
			w->state = I_DATA;
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
			w->state = I_HDRFIN;
			w->off = w->lastoff;
			return FFWVPK_RSEEK;
		}
		w->datalen = ffmin(w->total_size - w->off, w->datalen);
		w->state = I_APE2;
		// break

	case I_APE2:
		return wvpk_ape(w);


	case I_DATA:
		datalen = w->datalen;
		n = WavpackUnpackSamples(w->wp, w->pcm32, w->outcap);
		if (n == 0) {
more:
			if (w->off == w->total_size)
				return FFWVPK_RDONE;

			if (!w->async) {
				w->err = FFWVPK_EDECODE;
				return FFWVPK_RERR;
			}

			if (w->fin)
				return FFWVPK_RDONE;

			if (datalen != 0) {
				datalen -= w->datalen;
				w->data -= datalen;
				w->datalen += datalen;
				w->off -= datalen;
			}

			if (w->buf.len + w->datalen > MAX_BUF) {
				w->err = FFWVPK_EBIGBUF;
				return FFWVPK_RERR;
			}

			if (w->datalen != 0 && NULL == ffarr_append(&w->buf, w->data, w->datalen)) {
				w->err = FFWVPK_ESYS;
				return FFWVPK_RERR;
			}
			w->off -= w->bufoff;
			w->bufoff = 0;
			w->datalen = 0;
			return FFWVPK_RMORE;
		}

		if (w->buf.len != 0) {
			w->buf.len -= w->bufoff;
			w->bufoff = 0;
		}

		isrc = 0;
		if (w->skip_samples != 0) {
			isrc = ffmin(w->skip_samples, n);
			n -= isrc;
			w->skip_samples -= isrc;
			isrc *= w->fmt.channels;
		}

		if (n == 0)
			continue;
		goto pcm;
	}
	}

pcm:
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
	return FFWVPK_RDATA;
}
