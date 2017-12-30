/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/mpeg.h>
#include <FF/aformat/mp3.h>


const char* ffmpg_errstr(ffmpg *m)
{
	return mpg123_errstr(m->err);
}

int ffmpg_open(ffmpg *m, uint delay, uint options)
{
	int r;
	uint opt = (options & FFMPG_O_INT16) ? 0 : MPG123_FORCE_FLOAT;
	if (0 != (r = mpg123_open(&m->m123, opt))) {
		m->err = r;
		return FFMPG_RERR;
	}
	m->fmt.format = (options & FFMPG_O_INT16) ? FFPCM_16 : FFPCM_FLOAT;
	m->fmt.ileaved = 1;
	m->delay_start = delay;
	m->seek = m->delay_start;
	return 0;
}

void ffmpg_close(ffmpg *m)
{
	if (m->m123 != NULL)
		mpg123_free(m->m123);
}

void ffmpg_seek(ffmpg *m, uint64 sample)
{
	mpg123_decode(m->m123, (void*)-1, (size_t)-1, NULL); //reset bufferred data
	m->seek = sample + m->delay_start;
	m->delay_dec = 0;
}

int ffmpg_decode(ffmpg *m)
{
	int r;

	if (!m->fin && m->input.len == 0)
		return FFMPG_RMORE;

	r = mpg123_decode(m->m123, m->input.ptr, m->input.len, (byte**)&m->pcmi);
	m->input.len = 0;
	if (r == 0) {
		m->delay_dec += ffmpg_hdr_frame_samples((void*)m->input.ptr);
		return FFMPG_RMORE;

	} else if (r < 0) {
		m->err = r;
		m->delay_dec = 0;
		return FFMPG_RWARN;
	}

	m->pos = ffmax((int64)m->pos - m->delay_dec, 0);

	if (m->seek != (uint64)-1) {
		uint skip = ffmax((int64)(m->seek - m->pos), 0);
		if (skip >= (uint)r / ffpcm_size1(&m->fmt))
			return FFMPG_RMORE;

		m->seek = (uint64)-1;
		m->pcmi = (void*)((char*)m->pcmi + skip * ffpcm_size1(&m->fmt));
		r -= skip * ffpcm_size1(&m->fmt);
		m->pos += skip;
	}

	m->pcmlen = r;
	m->pos += m->pcmlen / ffpcm_size1(&m->fmt) - m->delay_start;
	return FFMPG_RDATA;
}
