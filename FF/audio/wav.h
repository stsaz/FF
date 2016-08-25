/** WAVE.
Copyright (c) 2015 Simon Zolin
*/

/*
RIFF(fmt data(DATA...))
*/

#pragma once

#include <FF/audio/pcm.h>
#include <FF/array.h>


/** Get WAV format from enum FFPCM_FMT.*/
FF_EXTN uint ffwav_fmt(uint pcm_fmt);

#ifdef FF_WIN
/** ffpcm -> WAVEFORMATEX */
static FFINL void ffwav_makewfx(WAVEFORMATEX *wf, ffpcm *f)
{
	wf->wFormatTag = ffwav_fmt(f->format);
	wf->wBitsPerSample = ffpcm_bits(f->format);
	wf->nSamplesPerSec = f->sample_rate;
	wf->nChannels = f->channels;
	wf->nBlockAlign = ffpcm_size1(f);
	wf->nAvgBytesPerSec = f->sample_rate * ffpcm_size1(f);
	wf->cbSize = 0;
}
#endif

struct ffwav_chunk {
	uint id;
	uint size;
	uint flags;
};

struct wav_bchunk;

typedef struct ffwav {
	uint state;
	uint nxstate;
	uint err;

	struct ffwav_chunk chunks[3];
	const struct wav_bchunk* ctx[3];
	uint ictx;

	ffarr buf;
	uint gather_size;
	ffstr gather_buf;

	ffpcm fmt;
	uint bitrate;
	uint64 total_samples;

	uint64 datasize
		, dataoff
		, off;
	uint64 cursample;
	uint has_fmt :1
		, fin :1
		;

	int tag; //enum FFMMTAG or -1
	ffstr tagval;

	size_t datalen;
	const void *data;

	size_t pcmlen;
	void *pcm;
} ffwav;

enum FFWAV_R {
	FFWAV_RWARN = -2,
	FFWAV_RERR = -1
	, FFWAV_RMORE
	, FFWAV_RHDR
	, FFWAV_RSEEK
	, FFWAV_RDATA
	, FFWAV_RDONE
	, FFWAV_RTAG
};

FF_EXTN const char* ffwav_errstr(void *w);

FF_EXTN void ffwav_init(ffwav *w);

#define ffwav_rate(w)  ((w)->fmt.sample_rate)

FF_EXTN void ffwav_close(ffwav *w);

FF_EXTN void ffwav_seek(ffwav *w, uint64 sample);

#define ffwav_seekoff(w)  ((w)->off)

/** Return enum FFWAV_R. */
FF_EXTN int ffwav_decode(ffwav *w);

#define ffwav_cursample(w)  ((w)->cursample)


typedef struct ffwav_cook {
	uint state;
	int err;

	ffarr buf;

	ffpcm fmt;
	uint64 total_samples;

	size_t pcmlen;
	const void *pcm;
	uint doff;
	uint dsize;

	size_t datalen;
	const void *data;
	uint64 off;

	uint fin :1;
} ffwav_cook;

FF_EXTN int ffwav_create(ffwav_cook *w, ffpcm *fmt, uint64 total_samples);
FF_EXTN void ffwav_wclose(ffwav_cook *w);
FF_EXTN int ffwav_write(ffwav_cook *w);

/** Get output file size.
Call it only after FFWAV_RHDR is returned. */
FF_EXTN int ffwav_wsize(ffwav_cook *w);

#define ffwav_wseekoff(w)  ((w)->off)
