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

	uint n = ffs_fromint(a->err, a->serr, sizeof(a->serr) - 1, FFINT_HEXLOW);
	a->serr[n] = '\0';
	return a->serr;
}


int ffaac_open(ffaac *a, uint channels, const char *conf, size_t len)
{
	int r;
	if (0 != (r = fdkaac_decode_open(&a->dec, conf, len))) {
		a->err = r;
		return FFAAC_RERR;
	}

	a->fmt.format = FFPCM_16LE;
	a->fmt.channels = channels;
	if (NULL == (a->pcmbuf = ffmem_alloc(AAC_MAXFRAMESAMPS * ffpcm_size1(&a->fmt)))) {
		a->err = AAC_ESYS;
		return FFAAC_RERR;
	}
	return 0;
}

void ffaac_close(ffaac *a)
{
	FF_SAFECLOSE(a->dec, NULL, fdkaac_decode_free);
	ffmem_safefree(a->pcmbuf);
}

void ffaac_seek(ffaac *a, uint64 sample)
{
	a->skip_samples = sample;
}

int ffaac_decode(ffaac *a)
{
	int r;
	r = fdkaac_decode(a->dec, a->data, a->datalen, a->pcmbuf, &a->frsamples);
	if (r == 0)
		return FFAAC_RMORE;
	else if (r < 0) {
		a->err = -r;
		return FFAAC_RERR;
	}

	FFARR_SHIFT(a->data, a->datalen, r);
	a->pcm = a->pcmbuf;

	if (a->skip_samples != 0) {
		if (a->frsamples > a->skip_samples) {
			a->frsamples -= a->skip_samples;
			a->pcm = (void*)((char*)a->pcmbuf + a->skip_samples * ffpcm_size1(&a->fmt));
		}
		a->skip_samples = 0;
	}

	a->pcmlen = a->frsamples * ffpcm_size1(&a->fmt);
	return FFAAC_RDATA;
}
