/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/mp3lame.h>
#include <FFOS/error.h>


const char* ffmpg_enc_errstr(ffmpg_enc *m)
{
	switch (m->err) {
	case FFMPG_ESYS:
		return fferr_strp(fferr_last());

	case FFMPG_EFMT:
		return "PCM format error";
	}

	return lame_errstr(m->err);
}

int ffmpg_create(ffmpg_enc *m, ffpcm *pcm, int qual)
{
	int r;

	switch (pcm->format) {
	case FFPCM_16:
		break;

	case FFPCM_FLOAT:
		if (!m->ileaved)
			break;
		// break

	default:
		pcm->format = FFPCM_16;
		m->err = FFMPG_EFMT;
		return FFMPG_EFMT;
	}

	lame_params conf = {0};
	conf.format = ffpcm_bits(pcm->format);
	conf.interleaved = m->ileaved;
	conf.channels = pcm->channels;
	conf.rate = pcm->sample_rate;
	conf.rate = pcm->sample_rate;
	conf.quality = qual;
	if (0 != (r = lame_create(&m->lam, &conf))) {
		m->err = r;
		return FFMPG_EFMT;
	}

	if (NULL == ffarr_realloc(&m->buf, 125 * (8 * 1152) / 100 + 7200)) {
		m->err = FFMPG_ESYS;
		return FFMPG_ESYS;
	}

	m->channels = pcm->channels;
	m->fmt = pcm->format;

	ffid31_init(&m->id31);
	m->min_meta = 1000;
	return FFMPG_EOK;
}

void ffmpg_enc_close(ffmpg_enc *m)
{
	ffarr_free(&m->id3.buf);
	ffarr_free(&m->buf);
	FF_SAFECLOSE(m->lam, NULL, lame_free);
}

int ffmpg_addtag(ffmpg_enc *m, uint id, const char *val, size_t vallen)
{
	if ((m->options & FFMPG_WRITE_ID3V2)
		&& 0 == ffid3_add(&m->id3, id, val, vallen)) {
		m->err = FFMPG_ESYS;
		return FFMPG_RERR;
	}
	if (m->options & FFMPG_WRITE_ID3V1)
		ffid31_add(&m->id31, id, val, vallen);
	return 0;
}

int ffmpg_encode(ffmpg_enc *m)
{
	enum { I_ID32, I_ID32_AFTER, I_ID31,
		I_DATA, I_LAMETAG_SEEK, I_LAMETAG, I_DONE };
	size_t nsamples;
	int r = 0;

	for (;;) {
	switch (m->state) {
	case I_ID32:
		if (m->options & FFMPG_WRITE_ID3V2) {
			ffid3_flush(&m->id3);
			if (m->min_meta > m->id3.buf.len
				&& 0 != ffid3_padding(&m->id3, m->min_meta - m->id3.buf.len)) {
				m->err = FFMPG_ESYS;
				return FFMPG_RERR;
			}
			ffid3_fin(&m->id3);
			m->data = m->id3.buf.ptr;
			m->datalen = m->id3.buf.len;
			m->off = m->datalen;
			m->state = I_ID32_AFTER;
			return FFMPG_RDATA;
		}
		// break

	case I_ID32_AFTER:
		ffarr_free(&m->id3.buf);
		m->state = I_DATA;
		// break

	case I_DATA:
		break;

	case I_ID31:
		m->state = I_LAMETAG_SEEK;
		if (m->options & FFMPG_WRITE_ID3V1) {
			m->data = &m->id31;
			m->datalen = sizeof(m->id31);
			return FFMPG_RDATA;
		}
		// break

	case I_LAMETAG_SEEK:
		m->state = I_LAMETAG;
		return FFMPG_RSEEK;

	case I_LAMETAG:
		r = lame_lametag(m->lam, m->buf.ptr, m->buf.cap);
		m->data = m->buf.ptr;
		m->datalen = ((uint)r <= m->buf.cap) ? r : 0;
		m->state = I_DONE;
		return FFMPG_RDATA;

	case I_DONE:
		return FFMPG_RDONE;
	}

	nsamples = m->pcmlen / ffpcm_size(m->fmt, m->channels);
	nsamples = ffmin(nsamples, 8 * 1152);

	r = 0;
	if (nsamples != 0) {
		const void *pcm[2];
		if (m->ileaved)
			pcm[0] = (char*)m->pcmi + m->pcmoff * m->channels;
		else {
			for (uint i = 0;  i != m->channels;  i++) {
				pcm[i] = (char*)m->pcm[i] + m->pcmoff;
			}
		}
		r = lame_encode(m->lam, pcm, nsamples, m->buf.ptr, m->buf.cap);
		if (r < 0) {
			m->err = r;
			return FFMPG_RERR;
		}
		m->pcmoff += nsamples * ffpcm_bits(m->fmt)/8;
		m->pcmlen -= nsamples * ffpcm_size(m->fmt, m->channels);
	}

	if (r == 0) {
		if (m->pcmlen != 0)
			continue;

		if (!m->fin) {
			m->pcmoff = 0;
			return FFMPG_RMORE;
		}

		r = lame_encode(m->lam, NULL, 0, (char*)m->buf.ptr, m->buf.cap);
		if (r < 0) {
			m->err = r;
			return FFMPG_RERR;
		}
		m->state = I_ID31;
	}

	m->data = m->buf.ptr;
	m->datalen = r;
	return FFMPG_RDATA;
	}
}
