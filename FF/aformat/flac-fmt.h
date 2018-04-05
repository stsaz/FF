/**
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FF/array.h>
#include <FF/audio/pcm.h>


enum FFFLAC_R {
	FFFLAC_RWARN = -2,
	FFFLAC_RERR = -1,
	FFFLAC_RDATA,
	FFFLAC_RSEEK,
	FFFLAC_RMORE,
	FFFLAC_RDONE,

	FFFLAC_RHDR,
	FFFLAC_RTAG,
	FFFLAC_RHDRFIN,
};

enum FLAC_E {
	FLAC_EUKN,
	FLAC_ESYS,
	FLAC_ELIB,

	FLAC_EFMT,
	FLAC_EHDR,
	FLAC_EBIGMETA,
	FLAC_ETAG,
	FLAC_ESEEKTAB,
	FLAC_ESEEK,
	FLAC_ESYNC,
	FLAC_ENOSYNC,
	FLAC_EHDRSAMPLES,
	FLAC_EBIGHDRSAMPLES,
};

enum FLAC_TYPE {
	FLAC_TINFO,
	FLAC_TPADDING,
	FLAC_TSEEKTABLE = 3,
	FLAC_TTAGS,
	FLAC_TPIC = 6,
};

struct flac_hdr {
#if defined FF_BIG_ENDIAN
	byte last :1
		, type :7;
#else
	byte type :7 //enum FLAC_TYPE
		, last :1;
#endif
	byte size[3];
};

struct flac_streaminfo {
	byte minblock[2];
	byte maxblock[2];
	byte minframe[3];
	byte maxframe[3];
	// rate :20;
	// channels :3; //=channels-1
	// bps :5; //=bps-1
	// total_samples :36;
	byte info[8];
	byte md5[16];
};

#define FLAC_SYNC  "fLaC"

enum {
	FLAC_SYNCLEN = 4,
	FLAC_MINSIZE = FLAC_SYNCLEN + sizeof(struct flac_hdr) + sizeof(struct flac_streaminfo),
	FLAC_MINFRAMEHDR = 4 + 1 + 1,
	FLAC_MAXFRAMEHDR = 4 + 7 + 2 + 2 + 1,
};


FF_EXTN uint flac_hdrsize(uint tags_size, uint padding, size_t seekpts);
FF_EXTN int flac_hdr(const char *data, size_t size, uint *blocksize, uint *islast);
FF_EXTN void flac_sethdr(void *dst, uint type, uint islast, uint size);

typedef struct ffflac_info {
	uint bits;
	uint channels;
	uint sample_rate;

	uint minblock, maxblock;
	uint minframe, maxframe;
	uint64 total_samples;
	char md5[16];
} ffflac_info;

FF_EXTN int flac_info(const char *data, size_t size, ffflac_info *info, ffpcm *fmt, uint *islast);
FF_EXTN int flac_info_write(char *out, size_t cap, const ffflac_info *info);

FF_EXTN uint flac_padding_write(char *out, uint padding, uint last);

typedef struct _ffflac_seektab {
	uint len;
	ffpcm_seekpt *ptr;
} _ffflac_seektab;

FF_EXTN int flac_seektab(const char *data, size_t len, _ffflac_seektab *sktab, uint64 total_samples);
FF_EXTN int flac_seektab_finish(_ffflac_seektab *sktab, uint64 frames_size);
FF_EXTN int flac_seektab_find(const ffpcm_seekpt *pts, size_t npts, uint64 sample);
FF_EXTN uint flac_seektab_size(size_t npts);
FF_EXTN int flac_seektab_init(_ffflac_seektab *sktab, uint64 total_samples, uint interval);
FF_EXTN uint flac_seektab_add(ffpcm_seekpt *pts, size_t npts, uint idx, uint64 nsamps, uint frlen, uint blksize);
FF_EXTN uint flac_seektab_write(void *out, size_t cap, const ffpcm_seekpt *pts, size_t npts, uint blksize);

FF_EXTN int flac_meta_pic(const char *data, size_t len, ffstr *pic);

typedef struct ffflac_frame {
	uint num; //-1 for variable-blocksize stream
	uint64 pos;
	uint samples;
	uint rate;
	uint channels;
	uint bps;
} ffflac_frame;

FF_EXTN uint flac_frame_parse(ffflac_frame *fr, const char *data, size_t len);
FF_EXTN ssize_t flac_frame_find(const char *data, size_t len, ffflac_frame *fr, byte hdr[4]);
