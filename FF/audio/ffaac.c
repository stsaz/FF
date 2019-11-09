/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/aac.h>
#include <FFOS/error.h>


enum {
	AAC_ESYS = -1,
};

const char* ffaac_errstr(ffaac *a)
{
	if (a->err == AAC_ESYS)
		return fferr_strp(fferr_last());

	return fdkaac_decode_errstr(-a->err);
}

int ffaac_open(ffaac *a, uint channels, const char *conf, size_t len)
{
	int r;
	if (0 != (r = fdkaac_decode_open(&a->dec, conf, len))) {
		a->err = -r;
		return FFAAC_RERR;
	}

	a->fmt.format = FFPCM_16;
	a->fmt.sample_rate = a->contr_samprate;
	a->fmt.channels = channels;
	if (NULL == (a->pcmbuf = ffmem_alloc(AAC_MAXFRAMESAMPLES * ffpcm_size(FFPCM_16, AAC_MAXCHANNELS))))
		return a->err = AAC_ESYS,  FFAAC_RERR;
	a->seek_sample = a->enc_delay;
	a->rate_mul = 1;
	return 0;
}

void ffaac_close(ffaac *a)
{
	FF_SAFECLOSE(a->dec, NULL, fdkaac_decode_free);
	ffmem_safefree(a->pcmbuf);
}

void ffaac_seek(ffaac *a, uint64 sample)
{
	a->seek_sample = sample + a->enc_delay;
}

int ffaac_decode(ffaac *a)
{
	int r, rc;
	r = fdkaac_decode(a->dec, a->data, a->datalen, a->pcmbuf);
	if (r == 0)
		return FFAAC_RMORE;
	else if (r < 0) {
		a->err = -r;
		return FFAAC_RERR;
	}

	rc = FFAAC_RDATA;
	fdkaac_frameinfo(a->dec, &a->info);
	if (a->fmt.sample_rate != a->info.rate
		|| a->fmt.channels != a->info.channels) {
		rc = FFAAC_RDATA_NEWFMT;
	}

	a->datalen = 0;
	a->pcm = a->pcmbuf;
	if (a->seek_sample != (uint64)-1) {
		uint skip = ffmax((int64)(a->seek_sample - a->cursample), 0);
		if (skip >= (uint)r)
			return FFAAC_RMORE;

		a->seek_sample = (uint64)-1;
		a->pcm = (void*)((char*)a->pcmbuf + skip * ffpcm_size(a->fmt.format, a->info.channels));
		r -= skip;
		a->cursample += skip;
	}

	if (a->cursample + r >= a->total_samples + a->enc_delay)
		r = ffmin(r - a->end_padding, r);

	if (rc == FFAAC_RDATA_NEWFMT) {
		a->fmt.channels = a->info.channels;
		a->fmt.sample_rate = a->info.rate;
		a->rate_mul = a->info.rate / a->contr_samprate;
	}

	a->pcmlen = r * ffpcm_size(a->fmt.format, a->info.channels);
	return rc;
}


const char* ffaac_enc_errstr(ffaac_enc *a)
{
	if (a->err == AAC_ESYS)
		return fferr_strp(fferr_last());

	return fdkaac_encode_errstr(-a->err);
}

int ffaac_create(ffaac_enc *a, const ffpcm *pcm, uint quality)
{
	int r;
	if (a->info.aot == 0)
		a->info.aot = AAC_LC;
	a->info.channels = pcm->channels;
	a->info.rate = pcm->sample_rate;
	a->info.quality = quality;

	if (0 != (r = fdkaac_encode_create(&a->enc, &a->info))) {
		a->err = -r;
		return FFAAC_RERR;
	}

	if (NULL == (a->buf = ffmem_alloc(a->info.max_frame_size))) {
		a->err = AAC_ESYS;
		return FFAAC_RERR;
	}
	return 0;
}

void ffaac_enc_close(ffaac_enc *a)
{
	FF_SAFECLOSE(a->enc, NULL, fdkaac_encode_free);
	ffmem_safefree(a->buf);
}

static const byte vbr_brate[] = {
	32, 40, 56, 64, 96
};

uint ffaac_bitrate(ffaac_enc *a, uint qual)
{
	if (qual == 0)
		return 0;

	if (qual <= 5)
		return a->info.channels * vbr_brate[qual - 1] * 1000;

	return qual;
}

int ffaac_encode(ffaac_enc *a)
{
	int r;
	size_t n = a->pcmlen / (a->info.channels * sizeof(short));
	if (n == 0 && !a->fin)
		return FFAAC_RMORE;

	for (;;) {
		r = fdkaac_encode(a->enc, a->pcm, &n, a->buf);
		if (r < 0) {
			a->err = -r;
			return FFAAC_RERR;
		}

		a->pcm += n * a->info.channels,  a->pcmlen -= n * a->info.channels * sizeof(short);

		if (r == 0) {
			if (a->fin) {
				if (n != 0) {
					n = 0;
					continue;
				}
				return FFAAC_RDONE;
			}
			return FFAAC_RMORE;
		}
		break;
	}

	a->data = a->buf,  a->datalen = r;
	return FFAAC_RDATA;
}
