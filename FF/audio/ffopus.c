/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/opus.h>
#include <FF/number.h>
#include <FFOS/error.h>


struct opus_hdr {
	char id[8]; //"OpusHead"
	byte ver;
	byte channels;
	byte preskip[2];
	byte orig_sample_rate[4];
	//byte unused[3];
};


#define ERR(o, n) \
	(o)->err = n,  FFOPUS_RERR

enum {
	FFOPUS_EHDR = 1,
	FFOPUS_EVER,
	FFOPUS_ETAG,

	FFOPUS_ESYS,
};

static const char* const _ffopus_errs[] = {
	"",
	"bad header",
	"unsupported version",
	"invalid tags",
};

const char* _ffopus_errstr(int e)
{
	if (e == FFOPUS_ESYS)
		return fferr_strp(fferr_last());

	if (e >= 0)
		return _ffopus_errs[e];
	return opus_errstr(e);
}


int ffopus_open(ffopus *o)
{
	return 0;
}

void ffopus_close(ffopus *o)
{
	ffarr_free(&o->pcmbuf);
	FF_SAFECLOSE(o->dec, NULL, opus_decode_free);
}

int ffopus_decode(ffopus *o, const void *pkt, size_t len)
{
	enum { R_HDR, R_TAGS, R_TAG, R_DATA };
	int r;

	if (len == 0)
		return FFOPUS_RMORE;

	switch (o->state) {
	case R_HDR: {
		const struct opus_hdr *h = pkt;
		if (len < sizeof(struct opus_hdr)
			|| memcmp(h->id, "OpusHead", 8))
			return ERR(o, FFOPUS_EHDR);

		if (h->ver != 1)
			return ERR(o, FFOPUS_EVER);

		o->info.channels = h->channels;
		o->info.rate = 48000;
		o->info.orig_rate = ffint_ltoh32(h->orig_sample_rate);
		o->info.preskip = ffint_ltoh16(h->preskip);

		opus_conf conf = {0};
		conf.channels = h->channels;
		r = opus_decode_init(&o->dec, &conf);
		if (r != 0)
			return ERR(o, r);

		if (NULL == ffarr_alloc(&o->pcmbuf, OPUS_BUFLEN(o->info.rate)))
			return ERR(o, FFOPUS_ESYS);

		o->seek_sample = o->info.preskip;
		o->state = R_TAGS;
		return FFOPUS_RHDR;
	}

	case R_TAGS:
		if (len < 8 || memcmp(pkt, "OpusTags", 8))
			return ERR(o, FFOPUS_ETAG);
		o->vtag.data = pkt + 8,  o->vtag.datalen = len - 8;
		o->state = R_TAG;
		// break

	case R_TAG:
		r = ffvorbtag_parse(&o->vtag);
		if (r == FFVORBTAG_ERR)
			return ERR(o, FFOPUS_ETAG);
		else if (r == FFVORBTAG_DONE) {
			o->state = R_DATA;
			return FFOPUS_RHDRFIN;
		}
		return FFOPUS_RTAG;

	case R_DATA:
		break;
	}

	float *pcm = (void*)o->pcmbuf.ptr;
	r = opus_decode_f(o->dec, pkt, len, pcm);

	if (o->seek_sample != (uint64)-1) {
		if (o->seek_sample < o->pos) {
			//couldn't find the target packet within the OGG page
			o->seek_sample = o->pos;
		}

		uint skip = ffmin(o->seek_sample - o->pos, r);
		o->pos += skip;
		if (o->pos != o->seek_sample || (uint)r == skip)
			return FFOPUS_RMORE; //not yet reached the target packet

		o->seek_sample = (uint64)-1;
		pcm += r;
		r -= skip;
	}

	if (o->pos + r >= o->total_samples)
		r = o->total_samples - o->pos;

	ffstr_set(&o->pcm, pcm, r * o->info.channels * sizeof(float));
	o->pos += r;
	return FFOPUS_RDATA;
}
