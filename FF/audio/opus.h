/** Opus.
Copyright (c) 2016 Simon Zolin
*/

/*
OGG(OPUS_HDR)  OGG(OPUS_TAGS)  OGG(OPUS_PKT...)...
*/

#pragma once

#include <FF/mtags/vorbistag.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>

#include <opus/opus-ff.h>


enum FFOPUS_R {
	FFOPUS_RWARN = -2,
	FFOPUS_RERR = -1,
	FFOPUS_RHDR, //audio info is parsed
	FFOPUS_RTAG, //tag pair is returned
	FFOPUS_RHDRFIN, //header is finished
	FFOPUS_RDATA, //PCM data is returned
	FFOPUS_RMORE,
	FFOPUS_RDONE,
};

FF_EXTN const char* _ffopus_errstr(int e);


#define FFOPUS_HEAD_STR  "OpusHead"

typedef struct ffopus {
	uint state;
	int err;
	opus_ctx *dec;
	struct {
		uint channels;
		uint rate;
		uint orig_rate;
		uint preskip;
	} info;
	uint64 pos;
	ffuint64 dec_pos;
	uint last_decoded;
	ffarr pcmbuf;
	uint64 seek_sample;
	uint64 total_samples;
	int flush;

	ffvorbtag vtag;
} ffopus;

#define ffopus_errstr(o)  _ffopus_errstr((o)->err)

FF_EXTN int ffopus_open(ffopus *o);

FF_EXTN void ffopus_close(ffopus *o);

static FFINL void ffopus_seek(ffopus *o, uint64 sample)
{
	o->seek_sample = sample + o->info.preskip;
	o->dec_pos = (ffuint64)-1;
	o->pos = 0;
	o->last_decoded = 0;
}

/** Decode Opus packet.
Return enum FFOPUS_R. */
FF_EXTN int ffopus_decode(ffopus *o, ffstr *input, ffstr *output);

/** Get starting position (sample number) of the last decoded data */
static inline ffuint64 ffopus_startpos(ffopus *o)
{
	return o->dec_pos - o->last_decoded;
}

static inline void ffopus_setpos(ffopus *o, ffuint64 val, int reset)
{
	if (reset) {
		opus_decode_reset(o->dec);
	}
	if (o->dec_pos == (uint64)-1)
		o->dec_pos = val;
	o->pos = val;
	o->last_decoded = 0;
}

#define ffopus_flush(o)  ((o)->flush = 1)


typedef struct ffopus_enc {
	uint state;
	opus_ctx *enc;
	uint orig_sample_rate;
	uint bitrate;
	uint sample_rate;
	uint channels;
	uint complexity;
	uint bandwidth;
	int err;
	ffarr buf;
	ffarr bufpcm;
	uint preskip;
	uint packet_dur; //msec

	ffvorbtag_cook vtag;
	uint min_tagsize;

	ffstr data;

	size_t pcmlen;
	const float *pcm;
	uint64 granulepos;

	uint fin :1;
} ffopus_enc;

#define ffopus_enc_errstr(o)  _ffopus_errstr((o)->err)

FF_EXTN int ffopus_create(ffopus_enc *o, const ffpcm *fmt, int bitrate);

FF_EXTN void ffopus_enc_close(ffopus_enc *o);

/** Add vorbis tag. */
#define ffopus_addtag(o, name, val, val_len) \
	ffvorbtag_add(&(o)->vtag, name, val, val_len)

/** Get approximate output file size. */
FF_EXTN uint64 ffopus_enc_size(ffopus_enc *o, uint64 total_samples);

/**
Return enum FFVORBIS_R. */
FF_EXTN int ffopus_encode(ffopus_enc *o);

#define ffopus_enc_pos(o)  ((o)->granulepos)
