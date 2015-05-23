/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/wav.h>
#include <FF/audio/pcm.h>


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


int ffwav_parse(const char *data, size_t *len)
{
	int r = 0;
	union {
		const ffwav_riff *wr;
		const ffwav_fmt *wf;
		const ffwav_ext *we;
		const ffwav_data *wd;
	} u;

	u.wr = (ffwav_riff*)data;

	if (*len < 4)
		return FFWAV_MORE;

	switch (data[0]) {

	case 'R':
		if (ffs_cmp(u.wr->riff, ffwav_pcmhdr.wr.riff, 4))
			break;

		r = FFWAV_RRIFF;
		if (*len < sizeof(ffwav_riff))
			return r | FFWAV_MORE;

		if (!ffs_cmp(u.wr->wave, ffwav_pcmhdr.wr.wave, 4)) {
			*len = sizeof(ffwav_riff);
			return r;
		}
		break;

	case 'f':
		if (ffs_cmp(u.wf->fmt, ffwav_pcmhdr.wf.fmt, 4))
			break;

		r = FFWAV_RFMT;
		if (*len < sizeof(ffwav_fmt))
			return r | FFWAV_MORE;

		if (u.wf->format == 0 || u.wf->channels == 0 || u.wf->sample_rate == 0 || u.wf->bit_depth == 0)
			break;

		if (u.wf->format == FFWAV_EXT) {
			r = FFWAV_REXT;

			if (*len < sizeof(ffwav_ext))
				return r | FFWAV_MORE;

			if (u.wf->size == 40 && u.we->size == 22) {
				*len = sizeof(ffwav_ext);
				return r;
			}

		} else if (u.wf->size == 16) {
			*len = sizeof(ffwav_fmt);
			return r;
		}
		break;

	case 'd':
		if (ffs_cmp(u.wd->data, ffwav_pcmhdr.wd.data, 4))
			break;

		r = FFWAV_RDATA;
		if (*len < sizeof(ffwav_data))
			return r | FFWAV_MORE;

		*len = sizeof(ffwav_data);
		return r;
	}

	return r | FFWAV_ERR;
}
