/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/aformat/mpc.h>
#include <FF/audio/musepack.h>
#include <FFOS/error.h>


extern const char* _ffmpc_errstr(uint e);

#define ERR(m, r) \
	(m)->err = (r),  FFMPC_RERR

enum {
	FFMPC_ESYS = 1,
};

const char* ffmpc_errstr(ffmpc *m)
{
	if (m->err < 0)
		return mpc_errstr(m->err);
	switch (m->err) {
	case FFMPC_ESYS:
		return fferr_strp(fferr_last());
	}
	return _ffmpc_errstr(m->err);
}

int ffmpc_open(ffmpc *m, ffpcmex *fmt, const char *conf, size_t len)
{
	if (0 != (m->err = mpc_decode_open(&m->mpc, conf, len)))
		return -1;
	if (NULL == (m->pcm = ffmem_alloc(MPC_ABUF_CAP)))
		return m->err = FFMPC_ESYS,  -1;
	fmt->format = FFPCM_FLOAT;
	fmt->ileaved = 1;
	m->channels = fmt->channels;
	m->need_data = 1;
	return 0;
}

void ffmpc_close(ffmpc *m)
{
	ffmem_safefree(m->pcm);
	mpc_decode_free(m->mpc);
}

int ffmpc_decode(ffmpc *m)
{
	int r;

	if (m->need_data) {
		if (m->input.len == 0)
			return FFMPC_RMORE;
		m->need_data = 0;
		mpc_decode_input(m->mpc, m->input.ptr, m->input.len);
		m->input.len = 0;
	}

	m->pcmoff = 0;

	for (;;) {

		r = mpc_decode(m->mpc, m->pcm);
		if (r == 0) {
			m->need_data = 1;
			return FFMPC_RMORE;
		} else if (r < 0) {
			m->need_data = 1;
			return ERR(m, r);
		}

		m->cursample += r;
		if (m->seek_sample != 0) {
			if (m->seek_sample >= m->cursample)
				continue;
			uint64 oldpos = m->cursample - r;
			uint skip = ffmax((int64)(m->seek_sample - oldpos), 0);
			m->pcmoff = skip * m->channels * sizeof(float);
			r -= skip;
			m->seek_sample = 0;
		}
		break;
	}

	m->pcmlen = r * m->channels * sizeof(float);
	m->frsamples = r;
	return FFMPC_RDATA;
}
