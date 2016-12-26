/** MPEG.
Copyright (c) 2013 Simon Zolin
*/

/*
MPEG-HEADER  [CRC16]  ([XING-TAG  LAME-TAG]  |  FRAME-DATA...) ...
*/

#pragma once

#include <FF/audio/mpeg-fmt.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/id3.h>
#include <FF/mtags/apetag.h>
#include <FF/array.h>

#include <mpg123/mpg123-ff.h>


enum FFMPG_O {
	FFMPG_O_NOXING = 1, //don't parse Xing and LAME tags
	FFMPG_O_ID3V1 = 2,
	FFMPG_O_ID3V2 = 4,
	FFMPG_O_INT16 = 8, //libmpg123: produce 16-bit integer output
	FFMPG_O_APETAG = 0x10,
};

typedef struct ffmpg {
	uint state;
	uint state2;

	mpg123 *m123;

	ffpcmex fmt;
	ffmpg_hdr firsthdr;
	uint err;
	uint e;
	ffstr3 buf; //holds 1 incomplete frame
	uint dlen; //number of bytes of input data copied to 'buf'
	uint64 seek_sample
		, total_samples
		, total_len //msec
		, cur_sample;
	uint64 dataoff //offset of the first MPEG header
		, total_size
		, off;
	struct ffmpg_info xing;
	struct ffmpg_lame lame;
	uint skip_samples;

	union {
	ffid31ex id31tag;
	ffid3 id32tag;
	ffapetag apetag;
	};
	int tag;
	ffarr tagval;
	uint codepage; //codepage for non-Unicode meta tags

	size_t datalen;
	const void *data;
	ffarr buf2;
	uint bytes_skipped;
	ffstr frame;

	size_t pcmlen;
	union {
	float *pcm[2];
	void *pcmi; //libmpg123: float | short
	};

	uint options; //enum FFMPG_O
	uint is_id32tag :1
		, is_apetag :1
		, fr_body :1
		, lostsync :1
		, frame2 :1
		;
} ffmpg;

enum FFMPG_R {
	FFMPG_RWARN = -2
	, FFMPG_RERR
	, FFMPG_RHDR
	, FFMPG_RDATA
	, FFMPG_RFRAME
	, FFMPG_RMORE
	, FFMPG_RSEEK
	, FFMPG_RTAG
	, FFMPG_RDONE
};

enum FFMPG_E {
	FFMPG_EOK,
	FFMPG_ESYS,
	FFMPG_EMPG123,
	FFMPG_EFMT,
	FFMPG_ETAG,
	FFMPG_EAPETAG,
	FFMPG_ESEEK,
	FFMPG_ENOFRAME,
	FFMPG_ESYNC,
};

/** Get the last error as a string. */
FF_EXTN const char* ffmpg_errstr(ffmpg *m);

FF_EXTN void ffmpg_init(ffmpg *m);

FF_EXTN void ffmpg_close(ffmpg *m);

FF_EXTN void ffmpg_seek(ffmpg *m, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffmpg_seekoff(m)  ((m)->off)

#define ffmpg_hdrok(m)  ((m)->fmt.format != 0)

/** Return enum FFMPG_R. */
FF_EXTN int ffmpg_decode(ffmpg *m);

/** Get an absolute sample number. */
#define ffmpg_cursample(m) \
	(((m)->cur_sample >= (m)->skip_samples) ? (m)->cur_sample - (m)->skip_samples : 0)

/** Get stream bitrate. */
#define ffmpg_bitrate(m) \
	ffpcm_brate((m)->total_size, (m)->total_samples, (m)->fmt.sample_rate)

#define ffmpg_isvbr(m)  ((m)->xing.vbr)


/** Read MPEG frame.  Parse Xing tag.
Note: it doesn't account for encoder/decoder delays.
Return enum FFMPG_R. */
FF_EXTN int ffmpg_readframe(ffmpg *m);

/** Get frame data. */
static FFINL ffstr ffmpg_framedata(ffmpg *m)
{
	return m->frame;
}
