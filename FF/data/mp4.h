/** MP4 reader/writer.
Copyright (c) 2016 Simon Zolin
*/

/*
box|box64(*)...

ftyp
moov(stts(AUDIO_POS => SAMPLE)  stsc(SAMPLE => CHUNK)  stco|co64(CHUNK => FILE_OFF)  stsz(SAMPLE => SIZE))
mdat(CHUNK(SAMPLE(DATA)...) ...)
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/array.h>


struct bbox;
struct mp4_box {
	char name[4];
	uint type; //enum BOX; flags
	uint usedboxes; //bit-table of children boxes that were processed
	uint64 osize; //the whole box size
	uint64 size; //unprocessed box size
	const struct bbox *ctx; //non-NULL if the box may have children
};

typedef struct ffmp4 {
	uint state;
	uint nxstate;
	uint whole_data;

	int err; //enum MP4_E
	char errmsg[64];

	uint ictx;
	struct mp4_box boxes[10];
	const struct bbox* ctxs[10];

	const char *data;
	size_t datalen;
	uint64 off;
	uint64 total_size;
	ffarr buf;
	uint isamp; //current MP4-sample
	uint frsize;
	ffarr sktab; //struct seekpt[], MP4-sample table
	ffarr chunktab; //uint64[], offsets of audio chunks
	ffstr stts;
	ffstr stsc;
	ffstr codec_conf;
	uint aac_brate;

	const char *out;
	size_t outlen;
	uint64 cursample;

	uint64 total_samples;
	uint enc_delay;
	uint end_padding;
	ffpcm fmt;
	byte codec; //enum FFMP4_CODEC

	uint tag; //enum FFMP4_TAG
	ffstr tagval;
	char tagbuf[32];

	uint meta_closed :1
		, ftyp :1
		, box64 :1
		, itunes_smpb :1
		;
} ffmp4;

enum FFMP4_CODEC {
	FFMP4_ALAC = 1,
	FFMP4_AAC,
};

enum FFMP4_R {
	FFMP4_RWARN = -2,
	FFMP4_RERR = -1,
	FFMP4_RDATA,
	FFMP4_RSEEK,
	FFMP4_RMORE,
	FFMP4_RDONE,

	FFMP4_RHDR, // ALAC: ffmp4.out contains magic cookie data
	FFMP4_RTAG,
	FFMP4_RMETAFIN,
};

FF_EXTN const char* ffmp4_errstr(ffmp4 *m);

FF_EXTN void ffmp4_init(ffmp4 *m);
FF_EXTN void ffmp4_close(ffmp4 *m);

/** Read meta data and codec data.
Return enum FFMP4_R. */
FF_EXTN int ffmp4_read(ffmp4 *m);

FF_EXTN void ffmp4_seek(ffmp4 *m, uint64 sample);

#define ffmp4_totalsamples(m)  ((m)->total_samples)

FF_EXTN uint ffmp4_bitrate(ffmp4 *m);

/** Get an absolute sample number. */
#define ffmp4_cursample(m)  ((m)->cursample)

/** Return codec name. */
FF_EXTN const char* ffmp4_codec(int codec);

struct ffmp4_tag;

typedef struct ffmp4_cook {
	uint state;
	int err;
	ffarr buf;
	ffarr stsz;
	ffarr stco;
	uint64 off;
	uint stco_off;
	uint stsz_off;
	const struct bbox* ctx[10];
	uint boxoff[10];
	uint ictx;
	ffpcm fmt;
	struct {
		uint nframes;
		uint frame_samples;
		uint64 total_samples;
		uint bitrate;
		uint enc_delay;
		uint end_padding;
	} info;
	uint frameno;
	uint64 samples;
	uint chunk_frames;
	uint chunk_curframe;

	char aconf[64];
	uint aconf_len;

	ffarr tags;
	struct {
		uint id;
		ushort num;
		ushort total;
	} trkn;
	struct ffmp4_tag *curtag;

	const char *data;
	size_t datalen;

	const char *out;
	size_t outlen;

	uint fin :1;
} ffmp4_cook;

const char* ffmp4_werrstr(ffmp4_cook *m);

/**
@m->info: total_samples, frame_samples, enc_delay must be set by caller. */
FF_EXTN int ffmp4_create_aac(ffmp4_cook *m, const ffpcm *fmt, const ffstr *conf);

FF_EXTN void ffmp4_wclose(ffmp4_cook *m);

/** Get approximate output file size. */
FF_EXTN uint64 ffmp4_wsize(ffmp4_cook *m);

FF_EXTN int ffmp4_addtag(ffmp4_cook *m, uint mmtag, const char *val, size_t val_len);

/**
Return enum FFMP4_R. */
FF_EXTN int ffmp4_write(ffmp4_cook *m);
