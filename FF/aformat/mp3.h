/** MPEG.
Copyright (c) 2013 Simon Zolin
*/

/*
MPEG-HEADER  [CRC16]  ([XING-TAG  LAME-TAG]  |  FRAME-DATA...) ...
*/

#pragma once

#include <FF/aformat/mpeg-fmt.h>
#include <FF/audio/pcm.h>
#include <FF/mtags/id3.h>
#include <FF/mtags/apetag.h>
#include <FF/array.h>


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
	FFMPG_RXING,
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

#include "mpeg-read.h"
#include "mp3-read.h"
#include "mp3-write.h"

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
	ffstr rinput;
} ffmpgcopy;

FF_EXTN void ffmpg_copy_close(ffmpgcopy *m);

#define ffmpg_copy_errstr(m)  ffmpg_rerrstr(&(m)->rdr)
#define ffmpg_copy_fmt(m)  ffmpg_fmt(&(m)->rdr)
#define ffmpg_copy_seekoff(m)  ((m)->off)
FF_EXTN void ffmpg_copy_seek(ffmpgcopy *m, uint64 sample);
FF_EXTN void ffmpg_copy_fin(ffmpgcopy *m);

/** Copy tags and frames. */
FF_EXTN int ffmpg_copy(ffmpgcopy *m, ffstr *input, ffstr *output);
