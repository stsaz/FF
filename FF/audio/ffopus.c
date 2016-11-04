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

		if (NULL == ffarr_alloc(&o->pcmbuf, OPUS_BUFLEN(o->info.rate) * o->info.channels * sizeof(float)))
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
	if (r < 0)
		return ERR(o, r);

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


int ffopus_create(ffopus_enc *o, const ffpcm *fmt, int bitrate)
{
	int r;
	opus_encode_conf conf = {0};
	conf.channels = fmt->channels;
	conf.sample_rate = fmt->sample_rate;
	conf.bitrate = bitrate;
	conf.complexity = o->complexity;
	conf.bandwidth = o->bandwidth;
	if (0 != (r = opus_encode_create(&o->enc, &conf)))
		return ERR(o, r);
	o->preskip = conf.preskip;

	if (NULL == ffarr_alloc(&o->buf, OPUS_MAX_PKT))
		return ERR(o, FFOPUS_ESYS);

	if (NULL == ffarr_alloc(&o->vtag.out, 4096))
		return ERR(o, FFOPUS_ESYS);
	o->vtag.out.len = FFSLEN("OpusTags");
	const char *vendor = opus_vendor();
	ffvorbtag_add(&o->vtag, NULL, vendor, ffsz_len(vendor));

	if (o->packet_dur == 0)
		o->packet_dur = 40;
	o->channels = fmt->channels;
	return 0;
}

void ffopus_enc_close(ffopus_enc *o)
{
	ffarr_free(&o->buf);
	ffarr_free(&o->bufpcm);
	opus_encode_free(o->enc);
}

/** Get complete packet with Vorbis comments and padding. */
static int _ffopus_tags(ffopus_enc *o, ffstr *pkt)
{
	ffarr *vt = &o->vtag.out;
	ffvorbtag_fin(&o->vtag);
	uint taglen = vt->len - FFSLEN("OpusTags");
	ffmemcpy(vt->ptr, "OpusTags", 8);
	uint npadding = (taglen < o->min_tagsize) ? o->min_tagsize - taglen : 0;
	if (NULL == ffarr_grow(vt, npadding, 0))
		return FFOPUS_ESYS;

	if (npadding != 0) {
		ffmem_zero(vt->ptr + vt->len, npadding);
		vt->len += npadding;
	}

	ffstr_set(pkt, (void*)vt->ptr, vt->len);
	return 0;
}

int ffopus_encode(ffopus_enc *o)
{
	enum { W_HDR, W_TAGS, W_DATA1, W_DATA, W_DONE };
	int r;

	switch (o->state) {
	case W_HDR: {
		struct opus_hdr *h = (void*)o->buf.ptr;
		ffmemcpy(h->id, "OpusHead", 8);
		h->ver = 1;
		h->channels = o->channels;
		ffint_htol32(h->orig_sample_rate, o->orig_sample_rate);
		ffint_htol16(h->preskip, o->preskip);
		ffmem_zero(h + 1, 3);
		ffstr_set(&o->data, o->buf.ptr, sizeof(struct opus_hdr) + 3);
		o->state = W_TAGS;
		return FFOPUS_RDATA;
	}

	case W_TAGS:
		if (0 != (r = _ffopus_tags(o, &o->data)))
			return ERR(o, r);
		o->state = W_DATA1;
		return FFOPUS_RDATA;

	case W_DATA1: {
		uint padding = o->preskip * ffpcm_size(FFPCM_FLOAT, o->channels);
		if (NULL == ffarr_grow(&o->bufpcm, padding, 0))
			return ERR(o, FFOPUS_ESYS);
		ffmem_zero(o->bufpcm.ptr + o->bufpcm.len, padding);
		o->bufpcm.len = padding;
		o->state = W_DATA;
		// break
	}

	case W_DATA:
		break;

	case W_DONE:
		o->data.len = 0;
		return FFOPUS_RDONE;
	}

	uint samp_size = ffpcm_size(FFPCM_FLOAT, o->channels);
	uint fr_samples = ffpcm_samples(o->packet_dur, 48000);
	uint fr_size = fr_samples * samp_size;
	uint samples = fr_samples;
	r = ffarr_append_until(&o->bufpcm, (void*)o->pcm, o->pcmlen, fr_size);
	switch (r) {
	case 0: {
		if (!o->fin) {
			o->pcmlen = 0;
			return FFOPUS_RMORE;
		}
		uint padding = fr_size - o->bufpcm.len;
		samples = o->bufpcm.len / samp_size;
		if (NULL == ffarr_grow(&o->bufpcm, padding, 0))
			return ERR(o, FFOPUS_ESYS);
		ffmem_zero(o->bufpcm.ptr + o->bufpcm.len, padding);
		r = o->pcmlen;
		o->state = W_DONE;
		break;
	}

	case -1:
		return ERR(o, FFOPUS_ESYS);
	}
	o->pcmlen -= r;
	o->pcm = (void*)((char*)o->pcm + r);
	o->bufpcm.len = 0;

	r = opus_encode_f(o->enc, (void*)o->bufpcm.ptr, fr_samples, o->buf.ptr);
	if (r < 0)
		return ERR(o, r);
	o->granulepos += samples;
	ffstr_set(&o->data, o->buf.ptr, r);
	return FFOPUS_RDATA;
}
