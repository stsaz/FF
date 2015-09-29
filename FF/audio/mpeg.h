/** MPEG.
Copyright (c) 2013 Simon Zolin
*/

/*
MPEG-HEADER  [CRC16]  ([XING-TAG  LAME-TAG]  |  FRAME-DATA...) ...
*/

#pragma once

#include <FFOS/types.h>


enum FFMPG_VER {
	FFMPG_1 = 3
};

enum FFMPG_LAYER {
	FFMPG_L3 = 1
};

enum FFMPG_CHANNEL {
	FFMPG_STEREO
	, FFMPG_JOINT
	, FFMPG_DUAL
	, FFMPG_MONO
};

/** Channel as a string. */
FF_EXTN const char ffmpg_strchannel[4][8];

FF_EXTN const byte ffmpg1l3_kbyterate[16];
FF_EXTN const ushort ffmpg1_sample_rate[4];

//4 bytes
typedef struct ffmpg_hdr {
	byte sync1 :8; //0xff

	byte noprotect :1 //0: protected by CRC
		, layer :2 //enum FFMPG_LAYER
		, ver :2 //enum FFMPG_VER
		, sync2 :3; //0x07

	byte priv :1
		, padding :1 //for L3 +1 byte in frame
		, sample_rate :2
		, bitrate :4;

	byte emphasis :2
		, original :1
		, copyright :1
		, modeext :2 //mode extension (for Joint Stereo)
		, channel :2; //enum FFMPG_CHANNEL
} ffmpg_hdr;

/** Return TRUE if valid MPEG header. */
FF_EXTN ffbool ffmpg_valid(const ffmpg_hdr *h);

/** Get length of MPEG frame data. */
FF_EXTN uint ffmpg_framelen(const ffmpg_hdr *h);

/** Get bitrate (bps). */
#define ffmpg_bitrate(h) \
	((uint)ffmpg1l3_kbyterate[(h)->bitrate] * 8 * 1000)

/** Get sample rate (Hz). */
#define ffmpg_sample_rate(h) \
	((uint)ffmpg1_sample_rate[(h)->sample_rate])

/** Get channels. */
#define ffmpg_channels(h) \
	((h)->channel == FFMPG_MONO ? 1 : 2)


enum FFMPG_XING_FLAGS {
	FFMPG_XING_FRAMES = 1,
	FFMPG_XING_BYTES = 2,
	FFMPG_XING_TOC = 4,
	FFMPG_XING_VBRSCALE = 8,
};

//8-120 bytes
typedef struct ffmpg_xing {
	char id[4]; //"Xing"(VBR) or "Info"(CBR)
	uint flags; //enum FFMPG_XING_FLAGS
	uint frames;
	uint bytes;
	byte toc[100];
	uint vbr_scale; //100(worst)..0(best)
} ffmpg_xing;

/** Convert sample number to stream offset (in bytes). */
FF_EXTN uint64 ffmpg_xing_seekoff(const byte *toc, uint64 sample, uint64 total_samples, uint64 total_size);

/** Parse Xing tag.
Return 0 on success. */
FF_EXTN int ffmpg_xing_parse(ffmpg_xing *xing, const char *data, size_t *len);


//36 bytes
typedef struct ffmpg_lamehdr {
	char id[9];
	byte unsupported1[12];

	byte delay_hi;
	byte padding_hi :4
		, delay_lo :4;
	byte padding_lo;

	byte unsupported2[12];
} ffmpg_lamehdr;

typedef struct ffmpg_lame {
	char id[9]; //e.g. "LAME3.90a"
	ushort enc_delay;
	ushort enc_padding;
} ffmpg_lame;

/** Parse LAME tag.
Return 0 on success. */
FF_EXTN int ffmpg_lame_parse(ffmpg_lame *lame, const char *data, size_t *len);


#include <FF/audio/pcm.h>
#include <FF/audio/id3.h>
#include <FF/array.h>

#include <mad/mad.h>

enum FFMPG_O {
	FFMPG_O_NOXING = 1,
	FFMPG_O_ID3V1 = 2,
};

typedef struct ffmpg {
	uint state;

	struct mad_stream stream;
	struct mad_frame frame;
	struct mad_synth synth;

	ffpcm fmt;
	uint bitrate;
	uint err;
	ffstr3 buf; //holds 1 incomplete frame
	uint64 seek_sample
		, total_samples
		, total_len //msec
		, cur_sample;
	uint64 dataoff
		, total_size
		, off;
	ffmpg_xing xing;
	ffmpg_lame lame;
	uint skip_samples;

	ffid31ex id31tag;

	size_t datalen;
	const void *data;

	size_t pcmlen;
	float *pcm[2];

	uint options; //enum FFMPG_O
} ffmpg;

enum FFMPG_R {
	FFMPG_RWARN = -2
	, FFMPG_RERR
	, FFMPG_RHDR
	, FFMPG_RDATA
	, FFMPG_RMORE
	, FFMPG_RSEEK
	, FFMPG_RTAG
	, FFMPG_RDONE
};

enum FFMPG_E {
	FFMPG_EOK,
	FFMPG_ESYS,
	FFMPG_EFMT,
};

/** Get the last error as a string. */
FF_EXTN const char* ffmpg_errstr(ffmpg *m);

FF_EXTN void ffmpg_init(ffmpg *m);

FF_EXTN void ffmpg_close(ffmpg *m);

FF_EXTN void ffmpg_seek(ffmpg *m, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffmpg_seekoff(m)  ((m)->off)

/** Return enum FFMPG_R. */
FF_EXTN int ffmpg_decode(ffmpg *m);

/** Get an absolute sample number. */
#define ffmpg_cursample(m)  ((m)->cur_sample)
