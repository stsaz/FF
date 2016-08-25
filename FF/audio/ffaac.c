/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/aac.h>
#include <FF/array.h>
#include <FFOS/error.h>


enum {
	AAC_MAXFRAMESAMPS = 2048,
};

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
	a->fmt.channels = channels;
	if (NULL == (a->pcmbuf = ffmem_alloc(AAC_MAXFRAMESAMPS * ffpcm_size1(&a->fmt)))) {
		a->err = AAC_ESYS;
		return FFAAC_RERR;
	}
	a->seek_sample = a->enc_delay;
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
	int r;
	r = fdkaac_decode(a->dec, a->data, a->datalen, a->pcmbuf);
	if (r == 0)
		return FFAAC_RMORE;
	else if (r < 0) {
		a->err = -r;
		return FFAAC_RERR;
	}

	a->datalen = 0;
	a->pcm = a->pcmbuf;
	if (a->seek_sample != (uint64)-1) {
		FF_ASSERT(a->seek_sample >= a->cursample);
		uint skip = a->seek_sample - a->cursample;
		if (skip >= (uint)r)
			return FFAAC_RMORE;

		a->seek_sample = (uint64)-1;
		a->pcm = (void*)((char*)a->pcmbuf + skip * ffpcm_size1(&a->fmt));
		r -= skip;
		a->cursample += skip;
	}

	if (a->cursample + r >= a->total_samples + a->enc_delay)
		r = ffmin(r - a->end_padding, r);

	a->pcmlen = r * ffpcm_size1(&a->fmt);
	return FFAAC_RDATA;
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
