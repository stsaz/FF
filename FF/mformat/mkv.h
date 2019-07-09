/** "Matroska".
Copyright (c) 2016 Simon Zolin
*/

/*
(ELEMENT SIZE DATA)...
*/

#include <FF/array.h>


struct mkv_bel;

struct mkv_el {
	int id; //enum MKV_ELID
	uint flags;
	uint usemask;
	uint prio;
	uint64 size;
	const struct mkv_bel *ctx;
};


enum {
	FFMKV_O_TAGS = 1,
};

/** Codec ID */
enum FFMKV_CODEC {
	FFMKV_AUDIO_AAC = 1,
	FFMKV_AUDIO_ALAC,
	FFMKV_AUDIO_MPEG,
	FFMKV_AUDIO_VORBIS,
	FFMKV_A_AC3,
	FFMKV_A_PCM,
	_FFMKV_A_LAST = FFMKV_A_PCM,

	FFMKV_V_AVC,
	FFMKV_V_HEVC,
	_FFMKV_V_LAST = FFMKV_V_HEVC,

	FFMKV_S_UTF8,
	FFMKV_S_ASS,
};

typedef struct ffmkv {
	uint state;
	uint64 off;
	uint options;
	uint err;
	uint el_hdrsize;
	uint64 el_off;

	uint gstate;
	uint gsize;
	ffarr buf;
	ffstr gbuf;

	struct mkv_el els[6];
	uint ictx;

	struct {
		uint format; //enum FFMKV_CODEC
		uint bits;
		uint channels;
		uint sample_rate;
		uint bitrate;
		uint64 total_samples;
		uint scale; //ns
		float dur;
		uint64 dur_msec;
		ffstr asc;
		int num;
		int type;

		uint vcodec; //enum FFMKV_CODEC
		uint width, height;
	} info;
	int audio_trkno;
	uint64 nsamples;
	uint time_clust;
	ffstr codec_data;

	int tag;
	ffstr tagval;

	ffarr4 lacing; //uint[].  Sizes of frames that are placed in a single block.

	uint seg_off;
	uint clust1_off;
	ffstr data;
	ffstr out;
} ffmkv;

enum FFMKV_R {
	FFMKV_RWARN = -2,
	FFMKV_RERR = -1,
	FFMKV_RDATA,
	FFMKV_RMORE,
	FFMKV_RHDR,
	FFMKV_RSEEK,
	FFMKV_RDONE,
	FFMKV_RTAG,
};

FF_EXTN const char* ffmkv_errstr(ffmkv *m);

FF_EXTN int ffmkv_open(ffmkv *m);
FF_EXTN void ffmkv_close(ffmkv *m);

FF_EXTN int ffmkv_read(ffmkv *m);

#define ffmkv_cursample(m)  ((m)->nsamples)

FF_EXTN void ffmkv_seek(ffmkv *m, uint64 sample);

#define ffmkv_off(m)  ((m)->off)
#define ffmkv_seekoff(m)  ((m)->off)


typedef struct ffmkv_vorbis {
	uint state;
	uint pkt2_off;
	uint pkt3_off;
	ffstr data;
	ffstr out;
} ffmkv_vorbis;

/** Parse Vorbis codec private data.
Return 0 if packet is ready;  1 if done;  <0 on error. */
FF_EXTN int ffmkv_vorbis_hdr(ffmkv_vorbis *m);
