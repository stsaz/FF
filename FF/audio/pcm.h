/** PCM.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>
#include <FF/number.h>

#include <math.h>


enum FFPCM_FMT {
	FFPCM_8 = 8,
	FFPCM_16LE = 16,
	FFPCM_16 = 16,
	FFPCM_24 = 24,
	FFPCM_32 = 32,
	FFPCM_24_4 = 0x0100 | 32,

	_FFPCM_ISFLOAT = 0x0200,
	FFPCM_FLOAT = 0x0200 | 32,
	FFPCM_FLOAT64 = 0x0200 | 64,
};

/** Get format name. */
FF_EXTN const char* ffpcm_fmtstr(uint fmt);

/** Get format by name. */
FF_EXTN int ffpcm_fmt(const char *sfmt, size_t len);

typedef struct ffpcm {
	uint format; //enum FFPCM_FMT
	uint channels;
	uint sample_rate;
} ffpcm;

typedef struct ffpcmex {
	uint format
		, channels
		, sample_rate;
	unsigned ileaved :1;
} ffpcmex;

#define ffpcm_fmtcopy(dst, src) \
do { \
	(dst)->format = (src)->format; \
	(dst)->channels = (src)->channels; \
	(dst)->sample_rate = (src)->sample_rate; \
} while (0)

enum {
	FFPCM_CHMASK = 0x0f,
};

/** Channels as string. */
FF_EXTN const char* ffpcm_channelstr(uint channels);

/** Get channels by name. */
FF_EXTN int ffpcm_channels(const char *s, size_t len);

/** Get bits per sample for one channel. */
#define ffpcm_bits(fmt)  ((fmt) & 0xff)

/** Get size of 1 sample (in bytes). */
#define ffpcm_size(format, channels)  (ffpcm_bits(format) / 8 * (channels))

#define ffpcm_size1(pcm)  ffpcm_size((pcm)->format, (pcm)->channels)

/** Convert between samples and time. */
#define ffpcm_samples(time_ms, rate)   ((uint64)(time_ms) * (rate) / 1000)
#define ffpcm_time(samples, rate)   ((uint64)(samples) * 1000 / (rate))

/** Convert between bytes and time. */
#define ffpcm_bytes(pcm, time_ms) \
	(ffpcm_samples(time_ms, (pcm)->sample_rate) * ffpcm_size1(pcm))
#define ffpcm_bytes2time(pcm, bytes) \
	ffpcm_time((bytes) / ffpcm_size1(pcm), (pcm)->sample_rate)

/** Return bits/sec. */
#define ffpcm_brate(bytes, samples, rate) \
	FFINT_DIVSAFE((uint64)(bytes) * 8 * (rate), samples)

#define ffpcm_brate_ms(bytes, time_ms) \
	FFINT_DIVSAFE((uint64)(bytes) * 8 * 1000, time_ms)

/** Combine two streams together. */
FF_EXTN void ffpcm_mix(const ffpcmex *pcm, void *stm1, const void *stm2, size_t samples);


/** Convert 16LE sample to FLOAT. */
#define _ffpcm_16le_flt(sh)  ((double)(sh) * (1 / 32768.0))

/** Convert PCM data.
Note: sample rate conversion isn't supported. */
FF_EXTN int ffpcm_convert(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const void *in, size_t samples);


/** Convert volume knob position to dB value. */
#define ffpcm_vol2db(pos, db_min) \
	(((pos) != 0) ? (log10(pos) * (db_min)/2 /*log10(100)*/ - (db_min)) : -100)

#define ffpcm_vol2db_inc(pos, pos_max, db_max) \
	(pow(10, (double)(pos) / (pos_max)) / 10 * (db_max))

/* gain = 10 ^ (db / 20) */
#define ffpcm_db2gain(db)  pow(10, (double)(db) / 20)
#define ffpcm_gain2db(gain)  (log10(gain) * 20)

FF_EXTN int ffpcm_gain(const ffpcmex *pcm, float gain, const void *in, void *out, uint samples);


/** Find the highest peak value. */
FF_EXTN int ffpcm_peak(const ffpcmex *fmt, const void *data, size_t samples, float *maxpeak);


typedef struct ffpcm_seekpt {
	uint64 sample;
	uint64 off;
} ffpcm_seekpt;

enum FFPCM_SEEKF {
	FFPCM_SEEK_BINSCH = 1, // force use of binary search
	FFPCM_SEEK_ALLOW_BINSCH = 2, // allow using binary search when necessary
};

struct ffpcm_seek {
	uint64 target;
	struct ffpcm_seekpt *pt; // search bounds (in/out)
	uint64 off; // absolute audio offset (in bytes) (in/out)
	uint64 lastoff;
	uint64 fr_index; // absolute sample index
	uint fr_samples; // samples in frame.  Set to 0 if user doesn't have a frame.
	uint avg_fr_samples;
	uint fr_size; // frame size (in bytes)
	uint flags; // enum FFPCM_SEEKF
};

/** Find a frame containing the specified sample.
A hybrid algorithm is used:
. Sample-based: estimate offset using the target sample position within the given range of samples.
. Binary search.
Return 0 on success;  1 if file seek is needed (s->off is set to file offset);  -1 on error. */
FF_EXTN int ffpcm_seek(struct ffpcm_seek *s);
