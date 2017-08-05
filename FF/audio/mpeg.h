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
	FFMPG_O_APETAG = 8,
};

/** MPEG frame reader. */
typedef struct ffmpgr {
	uint state;
	uint err;

	ffpcmex fmt;
	ffmpg_hdr firsthdr;
	ffstr3 buf; //holds 1 incomplete frame
	uint64 seek_sample
		, total_samples
		, total_len //msec
		, cur_sample;
	uint frsamps;
	uint64 dataoff //offset of the first MPEG header
		, total_size
		, off;
	struct ffmpg_info xing;
	struct ffmpg_lame lame;
	uint delay;
	uint frno;

	ffstr input;
	ffarr buf2;
	uint bytes_skipped;

	uint options; //enum FFMPG_O
	uint fr_body :1
		, lostsync :1
		, frame2 :1
		;
} ffmpgr;

FF_EXTN void ffmpg_rinit(ffmpgr *m);
FF_EXTN void ffmpg_rclose(ffmpgr *m);
FF_EXTN void ffmpg_rseek(ffmpgr *m, uint64 sample);

#define ffmpg_input(m, data, len)  ffstr_set(&(m)->input, data, len)

/** Get the last error as a string. */
FF_EXTN const char* ffmpg_rerrstr(ffmpgr *m);

/** Get stream bitrate. */
FF_EXTN uint ffmpg_bitrate(ffmpgr *m);

#define ffmpg_fmt(m)  ((m)->fmt)

#define ffmpg_setsize(m, size)  (m)->total_size = (size)

#define ffmpg_length(m)  ((m)->total_samples)

/** Get frame's sample position. */
static FFINL uint64 ffmpg_cursample(ffmpgr *m)
{
	return m->cur_sample - m->frsamps;
}

#define ffmpg_isvbr(m)  ((m)->xing.vbr)

/** Read MPEG frame.  Parse Xing tag.
Return enum FFMPG_R. */
FF_EXTN int ffmpg_readframe(ffmpgr *m, ffstr *frame);


/** MPEG file reader. */
typedef struct ffmpgfile {
	uint state;
	uint err;
	ffmpgr rdr;

	union {
	ffid31ex id31tag;
	ffid3 id32tag;
	ffapetag apetag;
	};
	int tag;
	ffarr tagval;
	uint codepage; //codepage for non-Unicode meta tags
	ffarr buf;

	ffstr input;
	ffstr frame;

	uint options; //enum FFMPG_O
	uint is_id32tag :1
		, is_apetag :1
		;
} ffmpgfile;

enum FFMPG_R {
	FFMPG_RWARN = -2
	, FFMPG_RERR
	, FFMPG_RHDR
	, FFMPG_RDATA
	, FFMPG_RFRAME
	, FFMPG_RMORE
	, FFMPG_RSEEK
	, FFMPG_RDONE
	,
	FFMPG_RID32,
	FFMPG_RID31,
	FFMPG_RAPETAG,
	FFMPG_ROUTSEEK,
	FFMPG_RFRAME1,
};

enum FFMPG_E {
	FFMPG_EOK,
	FFMPG_ESYS,
	FFMPG_EFMT,
	FFMPG_EID31DATA,
	FFMPG_EID32DATA,
	FFMPG_EID32,
	FFMPG_EAPETAG,
	FFMPG_ESEEK,
	FFMPG_ENOFRAME,
	FFMPG_ESYNC,
};

FF_EXTN const char* ffmpg_ferrstr(ffmpgfile *m);

FF_EXTN void ffmpg_init(ffmpgfile *m);

FF_EXTN void ffmpg_fclose(ffmpgfile *m);

/** Get an absolute file offset to seek. */
#define ffmpg_seekoff(m)  ((m)->rdr.off)

#define ffmpg_hdrok(m)  ((m)->rdr.fmt.channels != 0)

FF_EXTN int ffmpg_read(ffmpgfile *m);


/** MPEG decoder. */
typedef struct ffmpg {
	int err;
	mpg123 *m123;
	ffpcmex fmt;
	uint delay_start
		, delay_dec;
	uint64 pos;
	uint64 seek;

	ffstr input;
	size_t pcmlen;
	void *pcmi; //libmpg123: float | short
} ffmpg;

FF_EXTN const char* ffmpg_errstr(ffmpg *m);

enum FFMPG_DEC_O {
	FFMPG_O_INT16 = 1, //libmpg123: produce 16-bit integer output
};

/** Open decoder.
@options: enum FFMPG_DEC_O. */
FF_EXTN int ffmpg_open(ffmpg *m, uint delay, uint options);

FF_EXTN void ffmpg_close(ffmpg *m);

/** Decode 1 frame.
Return enum FFMPG_R. */
FF_EXTN int ffmpg_decode(ffmpg *m);

FF_EXTN void ffmpg_seek(ffmpg *m, uint64 sample);

/** Get absolute audio position. */
#define ffmpg_pos(m)  ((m)->pos - (m)->pcmlen / ffpcm_size1(&(m)->fmt))


enum FFMPG_ENC_OPT {
	FFMPG_WRITE_ID3V1 = 1,
	FFMPG_WRITE_ID3V2 = 2,
	FFMPG_WRITE_XING = 4,
};

/** .mp3 writer. */
typedef struct ffmpgw {
	uint state;
	int err;
	uint options; //enum FFMPG_ENC_OPT
	uint off;
	struct ffmpg_info xing;
	ffarr buf;
	uint fin :1;
	uint lametag :1; //set before passing LAME tag data

	ffid3_cook id3;
	ffid31 id31;
	uint min_meta;
} ffmpgw;

FF_EXTN const char* ffmpg_werrstr(ffmpgw *m);

FF_EXTN void ffmpg_winit(ffmpgw *m);
FF_EXTN void ffmpg_wclose(ffmpgw *m);

FF_EXTN int ffmpg_addtag(ffmpgw *m, uint id, const char *val, size_t vallen);

#define ffmpg_wseekoff(m)  ((m)->off)
#define ffmpg_wframes(m)  ((m)->xing.frames)

FF_EXTN int ffmpg_writeframe(ffmpgw *m, const char *fr, uint len, ffstr *data);


typedef struct ffmpgcopy {
	uint state;
	uint options; //enum FFMPG_ENC_OPT
	ffmpgr rdr;
	ffmpgw writer;
	uint gstate;
	uint gsize;
	uint wdataoff;
	uint64 off;
	ffid31 id31;
	ffarr buf;
	ffstr input;
} ffmpgcopy;

FF_EXTN void ffmpg_copy_close(ffmpgcopy *m);

#define ffmpg_copy_errstr(m)  ffmpg_rerrstr(&(m)->rdr)
#define ffmpg_copy_fmt(m)  ffmpg_fmt(&(m)->rdr)
#define ffmpg_copy_seekoff(m)  ((m)->off)
FF_EXTN void ffmpg_copy_seek(ffmpgcopy *m, uint64 sample);
FF_EXTN void ffmpg_copy_fin(ffmpgcopy *m);

/** Copy tags and frames. */
FF_EXTN int ffmpg_copy(ffmpgcopy *m, ffstr *output);
