/** Musepack decoder.
Copyright (c) 2017 Simon Zolin
*/

#include <FF/array.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/apetag.h>

#include <musepack/mpc-ff.h>


typedef struct ffmpc {
	mpc_ctx *mpc;
	int err;
	uint channels;
	uint frsamples;
	uint64 cursample;
	uint64 seek_sample;
	uint need_data :1;

	ffstr input;

	float *pcm;
	uint pcmoff;
	uint pcmlen;
} ffmpc;

FF_EXTN const char* ffmpc_errstr(ffmpc *m);
FF_EXTN int ffmpc_open(ffmpc *m, ffpcmex *fmt, const char *conf, size_t len);
FF_EXTN void ffmpc_close(ffmpc *m);

static FFINL void ffmpc_inputblock(ffmpc *m, const char *block, size_t len, uint64 audio_pos)
{
	ffstr_set(&m->input, block, len);
	m->cursample = audio_pos;
}

/** Decode 1 frame. */
FF_EXTN int ffmpc_decode(ffmpc *m);

#define ffmpc_audiodata(m, dst) \
	ffstr_set(dst, (char*)(m)->pcm + (m)->pcmoff, (m)->pcmlen)

#define ffmpc_seek(m, sample) \
	((m)->seek_sample = (sample))

#define ffmpc_cursample(m)  ((m)->cursample - (m)->frsamples)
