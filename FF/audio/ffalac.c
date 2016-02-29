/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/alac.h>
#include <FFOS/error.h>


struct alac_conf {
	byte frame_length[4];
	byte compatible_version;
	byte bit_depth;
	byte unused[3];
	byte channels;
	byte maxrun[2];
	byte max_frame_bytes[4];
	byte avg_bitrate[4];
	byte sample_rate[4];
};


enum {
	ESYS = 1,
	EINIT,
};

const char* ffalac_errstr(ffalac *a)
{
	if (a->err == ESYS)
		return fferr_strp(fferr_last());

	else if (a->err == EINIT)
		return "bad magic cookie";

	uint n = ffs_fromint(a->err, a->serr, sizeof(a->serr), FFINT_SIGNED);
	a->serr[n] = '\0';
	return a->serr;
}

int ffalac_open(ffalac *a, const char *data, size_t len)
{
	if (NULL == (a->al = alac_init(data, len))) {
		a->err = EINIT;
		return FFALAC_RERR;
	}

	const struct alac_conf *conf = (void*)data;
	a->fmt.format = conf->bit_depth;
	a->fmt.channels = conf->channels;
	a->fmt.sample_rate = ffint_ntoh32(conf->sample_rate);
	a->bitrate = ffint_ntoh32(conf->avg_bitrate);

	if (NULL == ffarr_alloc(&a->buf, ffint_ntoh32(conf->frame_length) * ffpcm_size1(&a->fmt))) {
		a->err = ESYS;
		return FFALAC_RERR;
	}
	return 0;
}

void ffalac_close(ffalac *a)
{
	if (a->al != NULL)
		alac_free(a->al);
	ffarr_free(&a->buf);
}

int ffalac_decode(ffalac *a)
{
	int r;
	uint samps;

	r = alac_decode(a->al, a->data, a->datalen, a->buf.ptr, &samps);
	if (r < 0) {
		a->err = r;
		return FFALAC_RERR;
	}

	FFARR_SHIFT(a->data, a->datalen, r);
	if (samps == 0)
		return FFALAC_RMORE;

	a->pcm = a->buf.ptr;
	a->pcmlen = samps * ffpcm_size1(&a->fmt);
	return 0;
}
