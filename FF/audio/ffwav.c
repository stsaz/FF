/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/wav.h>
#include <FF/audio/pcm.h>
#include <FFOS/error.h>


static int _ffwav_parse(ffwav *w);

static const char *const _ffwav_sfmt[] = {
	"", "PCM", "", "IEEE (Float)"
};

const char* ffwav_fmtstr(uint fmt)
{
	if (fmt < FFCNT(_ffwav_sfmt))
		return _ffwav_sfmt[fmt];
	return "";
}

const ffwavpcmhdr ffwav_pcmhdr = {
	{{'R', 'I', 'F', 'F'}, 0, {'W', 'A', 'V', 'E'}}

	, {{'f', 'm', 't', ' '}, 16, FFWAV_PCM, 2, 44100, 44100 * 4, 16 / 8 * 2, 16}

	, {{'d', 'a', 't', 'a'}, 0}
};

uint ffwav_pcmfmt(const ffwav_fmt *wf)
{
	uint fmt = wf->format;
	if (fmt == FFWAV_EXT)
		fmt = ffwav_extfmt((ffwav_ext*)wf);

	if (fmt == FFWAV_PCM) {
		if (wf->bit_depth == 16)
			return FFPCM_16LE;

		else if (wf->bit_depth == 32)
			return FFPCM_32LE;

	} else if (fmt == FFWAV_IEEE_FLOAT)
		return FFPCM_FLOAT;

	return (uint)-1;
}

void ffwav_pcmfmtset(ffwav_fmt *wf, uint pcm_fmt)
{
	switch (pcm_fmt) {
	case FFPCM_16LE:
		wf->bit_depth = 16;
		wf->format = FFWAV_PCM;
		break;

	case FFPCM_32LE:
		wf->bit_depth = 32;
		wf->format = FFWAV_PCM;
		break;

	case FFPCM_FLOAT:
		wf->bit_depth = 32;
		wf->format = FFWAV_IEEE_FLOAT;
		break;
	}
}


enum WAV_E {
	WAV_EOK
	, WAV_ENOFMT
	, WAV_EBADFMT
	, WAV_EDUPFMT
	, WAV_EUKN

	, WAV_ESYS
};

static const char *const wav_errstr[] = {
	""
	, "no format chunk"
	, "bad format chunk"
	, "duplicate format chunk"
	, "unknown chunk"
};

const char* ffwav_errstr(ffwav *w)
{
	if (w->err == WAV_ESYS)
		return fferr_strp(fferr_last());
	return wav_errstr[w->err];
}

enum WAV_T {
	WAV_TERR = 1 << 31
	, WAV_TMORE = 1 << 30

	, WAV_TRIFF = 1
	, WAV_TFMT
	, WAV_TEXT
	, WAV_TDATA
};

struct _ffwav_ukn {
	char id[4];
	uint size;
};

/* Get the next chunk.
Return enum WAV_T. */
static int _ffwav_parse(ffwav *w)
{
	int r = 0;
	size_t len2 = w->datalen;
	union {
		const struct _ffwav_ukn *ukn;
		const ffwav_riff *wr;
		const ffwav_fmt *wf;
		const ffwav_ext *we;
		const ffwav_data *wd;
	} u;

	u.wr = (ffwav_riff*)w->data;

	if (w->datalen < 4)
		return WAV_TMORE;

	switch (((char*)w->data)[0]) {

	case 'R':
		if (ffs_cmp(u.wr->riff, ffwav_pcmhdr.wr.riff, 4))
			break;

		r = WAV_TRIFF;
		if (w->datalen < sizeof(ffwav_riff))
			return r | WAV_TMORE;

		if (!ffs_cmp(u.wr->wave, ffwav_pcmhdr.wr.wave, 4)) {
			w->datalen -= sizeof(ffwav_riff);
			goto done;
		}
		break;

	case 'f':
		if (ffs_cmp(u.wf->fmt, ffwav_pcmhdr.wf.fmt, 4))
			break;

		r = WAV_TFMT;
		if (w->datalen < sizeof(ffwav_fmt))
			return r | WAV_TMORE;

		if (u.wf->format == 0 || u.wf->channels == 0 || u.wf->sample_rate == 0 || u.wf->bit_depth == 0)
			return WAV_TFMT | WAV_EBADFMT;

		if (u.wf->format == WAV_TEXT) {
			r = WAV_TEXT;

			if (w->datalen < sizeof(ffwav_ext))
				return r | WAV_TMORE;

			if (u.wf->size == 40 && u.we->size == 22) {
				w->datalen -= sizeof(ffwav_ext);
				goto done;
			}

		} else if (u.wf->size == 16) {
			w->datalen -= sizeof(ffwav_fmt);
			goto done;
		}
		break;

	case 'd':
		if (ffs_cmp(u.wd->data, ffwav_pcmhdr.wd.data, 4))
			break;

		r = WAV_TDATA;
		if (w->datalen < sizeof(ffwav_data))
			return r | WAV_TMORE;

		w->datalen -= sizeof(ffwav_data);
		goto done;
	}

	w->err = WAV_EUKN;
	if (w->datalen < sizeof(struct _ffwav_ukn) + u.ukn->size)
		return WAV_TMORE;
	w->data += sizeof(struct _ffwav_ukn) + u.ukn->size;
	w->datalen -= sizeof(struct _ffwav_ukn) + u.ukn->size;

	return r | WAV_TERR;

done:
	w->data += len2 - w->datalen;
	return r;
}

void ffwav_close(ffwav *w)
{
	ffarr_free(&w->sample);
}

enum { I_HDR, I_DATA, I_DATAOK, I_BUFDATA, I_SEEK };

static int _ffwav_gethdr(ffwav *w)
{
	const ffwav_fmt *wf;
	const ffwav_data *wd;
	const char *data_first = w->data;
	const void *pchunk;
	int r;

	for (;;) {
		pchunk = w->data;
		r = _ffwav_parse(w);

		if (r & WAV_TERR) {
			if (w->err == WAV_EUKN)
				return FFWAV_RWARN;
			return FFWAV_RERR;
		}

		if (r & WAV_TMORE) {
			return FFWAV_RERR;
		}

		switch (r) {
		case WAV_TFMT:
		case WAV_TEXT:
			if (w->has_fmt) {
				w->err = WAV_EDUPFMT;
				return FFWAV_RERR;
			}

			wf = pchunk;
			w->bitrate = wf->byte_rate * 8;
			w->fmt.format = ffwav_pcmfmt(wf);
			w->fmt.channels = wf->channels;
			w->fmt.sample_rate = wf->sample_rate;
			if (NULL == ffarr_alloc(&w->sample, ffpcm_size1(&w->fmt))) {
				w->err = WAV_ESYS;
				return FFWAV_RERR;
			}
			w->has_fmt = 1;
			break;

		case WAV_TDATA:
			if (!w->has_fmt) {
				w->err = WAV_ENOFMT;
				return FFWAV_RERR;
			}

			wd = pchunk;
			w->dataoff = (char*)w->data - data_first;
			w->datasize = w->dsize = wd->size;
			w->total_samples = w->datasize / ffpcm_size1(&w->fmt);
			w->state = I_DATA;
			return FFWAV_RHDR;
		}
	}

	//unreachable
}

void ffwav_seek(ffwav *w, uint64 sample)
{
	w->seek_sample = sample;
	w->state = I_SEEK;
}

int ffwav_decode(ffwav *w)
{
	size_t ntail;
	uint n;

	for (;;) {

	switch (w->state) {
	case I_HDR:
		return _ffwav_gethdr(w);

	case I_SEEK:
		w->cursample = w->seek_sample;
		w->off = w->dataoff + w->datasize * w->seek_sample / w->total_samples;
		w->dsize = w->datasize - w->off;
		w->state = I_DATA;
		return FFWAV_RSEEK;

	case I_DATAOK:
		w->cursample += w->pcmlen / ffpcm_size1(&w->fmt);
		w->state = (w->sample.len == 0) ? I_DATA : I_BUFDATA;
		break;

	case I_DATA:
		if (w->dsize == 0)
			return FFWAV_RDONE;
		if (w->datalen == 0)
			return FFWAV_RMORE;
		n = (uint)ffmin(w->dsize, w->datalen);
		ntail = n % w->sample.cap;

		ffmemcpy(w->sample.ptr, w->data + n - ntail, ntail);
		w->sample.len = ntail;

		w->datalen = 0;
		w->dsize -= n;
		if (n == ntail) {
			w->state = I_BUFDATA;
			return FFWAV_RMORE; //not even 1 complete PCM sample
		}
		w->pcm = (void*)w->data;
		w->pcmlen = n - ntail;
		w->state = I_DATAOK;
		return FFWAV_RDATA;

	case I_BUFDATA:
		if (w->dsize == 0)
			return FFWAV_RDONE;
		n = (uint)ffmin(w->dsize, w->datalen);
		n = (uint)ffmin(n, ffarr_unused(&w->sample));
		ffmemcpy(ffarr_end(&w->sample), w->data, n);
		w->sample.len += n;

		w->data += n;
		w->datalen -= n;
		w->dsize -= n;
		if (!ffarr_isfull(&w->sample))
			return FFWAV_RMORE;

		w->pcm = w->sample.ptr;
		w->pcmlen = w->sample.len;
		w->sample.len = 0;
		w->state = I_DATAOK;
		return FFWAV_RDATA;
	}
	}
	//unreachable
}
