/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/mp3lame.h>
#include <FFOS/error.h>


static const char *const lame_errstr[] = {
	"mp3buf was too small", //-1
	"malloc() problem", //-2
	"", //-3
	"psycho acoustic problems", //-4
};

const char* ffmpg_enc_errstr(ffmpg_enc *m)
{
	uint e;

	switch (m->err) {
	case FFMPG_ESYS:
		return fferr_strp(fferr_last());

	case FFMPG_EFMT:
		return "PCM format error";
	}

	e = -m->err - 1;
	if (e >= FFCNT(lame_errstr))
		return "";
	return lame_errstr[e];
}

static void lame_err(const char *format, va_list ap)
{
}

int ffmpg_create(ffmpg_enc *m, ffpcm *pcm, int qual)
{
	lame_global_flags *lam;

	switch (pcm->format) {
	case FFPCM_16LE:
		break;

	case FFPCM_FLOAT:
		if (!m->ileaved)
			break;
		// break

	default:
		m->err = FFMPG_EFMT;
		return FFMPG_EFMT;
	}

	if (NULL == (lam = lame_init())) {
		m->err = FFMPG_ESYS;
		return FFMPG_ESYS;
	}

	lame_set_errorf(lam, &lame_err);
	lame_set_debugf(lam, &lame_err);
	lame_set_msgf(lam, &lame_err);

	lame_set_num_channels(lam, pcm->channels);
	m->channels = pcm->channels;
	m->fmt = pcm->format;
	lame_set_in_samplerate(lam, pcm->sample_rate);
	lame_set_quality(lam, 2);

	if (qual < 10) {
		lame_set_VBR(lam, vbr_default);
		lame_set_VBR_q(lam, qual);
	} else {
		lame_set_preset(lam, qual);
		lame_set_VBR(lam, vbr_off);
	}

	if (-1 == lame_init_params(lam)) {
		lame_close(lam);
		m->err = FFMPG_ESYS;
		return FFMPG_ESYS;
	}

	ffid31_init(&m->id31);

	m->lam = lam;
	return FFMPG_EOK;
}

void ffmpg_enc_close(ffmpg_enc *m)
{
	ffarr_free(&m->id3.buf);
	ffarr_free(&m->buf);
	lame_close(m->lam);
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
	size_t cap, nsamples;
	int r = 0;

	if (m->pcmlen == 0 && !m->fin)
		return FFMPG_RMORE;

	switch (m->state) {
	case I_ID32:
		if (m->options & FFMPG_WRITE_ID3V2) {
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
		r = lame_get_lametag_frame(m->lam, (byte*)m->buf.ptr, m->buf.cap);
		m->data = m->buf.ptr;
		m->datalen = ((uint)r <= m->buf.cap) ? r : 0;
		m->state = I_DONE;
		return FFMPG_RDATA;

	case I_DONE:
		return FFMPG_RDONE;
	}

	nsamples = m->pcmlen / ffpcm_size(m->fmt, lame_get_num_channels(m->lam));
	cap = 125 * nsamples / 100 + 7200;
	if (NULL == ffarr_realloc(&m->buf, cap)) {
		m->err = FFMPG_ESYS;
		return FFMPG_RERR;
	}
	m->data = m->buf.ptr;

	if (m->ileaved) {
		// FFPCM_16LE
		r = lame_encode_buffer_interleaved(m->lam, (void*)m->pcmi, nsamples, m->data, cap);

	} else if (m->pcmlen != 0) {
		const void *ch2 = (m->channels == 1) ? NULL : m->pcm[1];

		switch (m->fmt) {
		case FFPCM_16LE:
			r = lame_encode_buffer(m->lam, m->pcm[0], ch2, nsamples, m->data, cap);
			break;

		case FFPCM_FLOAT:
			r = lame_encode_buffer_ieee_float(m->lam, m->pcmf[0], ch2, nsamples, m->data, cap);
			break;
		}
	}

	if (r < 0) {
		m->err = r;
		return FFMPG_RERR;
	}
	m->pcmlen = 0;
	m->datalen = r;

	if (m->fin) {
		r = lame_encode_flush(m->lam, (byte*)m->data + r, cap - r);
		m->datalen += r;
		m->state = I_ID31;
	}

	return FFMPG_RDATA;
}
