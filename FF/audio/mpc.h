/** Musepack.
Copyright (c) 2017 Simon Zolin
*/

/*
MPCK BLOCK(SH RG EI SO AP(FRAME...)... ST SE)...
*/

#include <FF/array.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/apetag.h>

#include <musepack/mpc-ff.h>


enum {
	FFMPC_BLKHDR_MAXSIZE = 2 + 8,
	FFMPC_SH_MAXSIZE = FFMPC_BLKHDR_MAXSIZE + 4 + 1 + 8 + 8 + 2,
};

enum FFMPC_O {
	FFMPC_O_APETAG = 1,
	FFMPC_O_SEEKTABLE = 2,
};

typedef struct ffmpcr {
	uint state;
	int err;
	uint options; //enum FFMPC_O

	ffpcm fmt;
	uint64 total_samples;
	uint64 delay;
	uint blk_samples;

	uint64 dataoff;
	uint64 total_size;
	uint64 off;
	uint blk_off;
	uint64 blk_size;
	uint64 blk_apos;

	mpc_seekctx *seekctx;
	uint64 ST_off;
	uint64 seek_sample;

	char sh_block[FFMPC_SH_MAXSIZE];
	uint sh_block_len;
	uint enc_profile;
	byte enc_ver[3];

	ffstr input;
	ffstr block;

	ffapetag apetag;
	ffstr tagval;
	int tag;

	uint gstate;
	uint gsize;
	ffarr gbuf;
	uint hdrok :1;
} ffmpcr;

enum FFMPC_R {
	FFMPC_RHDR = 1,
	FFMPC_RERR,
	FFMPC_RWARN,
	FFMPC_RMORE,
	FFMPC_RBLOCK,
	FFMPC_RDATA,
	FFMPC_RSEEK,
	FFMPC_RTAG,
	FFMPC_RDONE,
};

FF_EXTN const char* ffmpc_rerrstr(ffmpcr *m);
FF_EXTN void ffmpc_ropen(ffmpcr *m);
FF_EXTN void ffmpc_rclose(ffmpcr *m);
FF_EXTN int ffmpc_read(ffmpcr *m);

/** Seek to the block containing the specified audio sample.
Note: inaccurate if seek table isn't used. */
FF_EXTN void ffmpc_blockseek(ffmpcr *m, uint64 sample);

FF_EXTN void ffmpc_streamerr(ffmpcr *m);

static FFINL uint ffmpc_bitrate(ffmpcr *m)
{
	if (m->total_size == 0)
		return 0;
	return ffpcm_brate(m->total_size - m->dataoff, m->total_samples, m->fmt.sample_rate);
}

/** Set input data.
Note: decoder may overread up to MPC_FRAME_MAXSIZE bytes. */
#define ffmpc_input(m, d, len)  ffstr_set(&(m)->input, d, len)

#define ffmpc_fmt(m)  ((m)->fmt)
#define ffmpc_setsize(m, size)  ((m)->total_size = (size))
#define ffmpc_length(m)  ((m)->total_samples - (m)->delay)
#define ffmpc_off(m)  ((m)->off)
#define ffmpc_blockdata(m, blk)  (*(blk) = (m)->block)
#define ffmpc_blockpos(m)  ((m)->blk_apos - (m)->blk_samples)


typedef struct ffmpc {
	mpc_ctx *mpc;
	int err;
	ffpcmex fmt;
	uint frsamples;
	uint64 cursample;
	uint need_data :1;

	ffstr input;

	float *pcm;
	uint pcmlen;
} ffmpc;

FF_EXTN const char* ffmpc_errstr(ffmpc *m);
FF_EXTN int ffmpc_open(ffmpc *m, uint channels, const char *conf, size_t len);
FF_EXTN void ffmpc_close(ffmpc *m);

/** Decode 1 frame. */
FF_EXTN int ffmpc_decode(ffmpc *m);

#define ffmpc_cursample(m)  ((m)->cursample - (m)->frsamples)
