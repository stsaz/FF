/** PCM.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>

#include <math.h>


enum FFPCM_FMT {
	FFPCM_16LE
	, FFPCM_32LE
	, FFPCM_FLOAT
};

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

/** Channels as string. */
FF_EXTN const char* ffpcm_channelstr(uint channels);


FF_EXTN const byte ffpcm_bits[];
/** Get size of 1 sample (in bytes). */
#define ffpcm_size(format, channels)  ((uint)ffpcm_bits[format]/8 * (channels))

#define ffpcm_size1(pcm)  ffpcm_size((pcm)->format, (pcm)->channels)

/** Convert between samples and time. */
#define ffpcm_samples(time_ms, rate)   ((uint64)(time_ms) * (rate) / 1000)
#define ffpcm_time(samples, rate)   ((uint64)(samples) * 1000 / (rate))

/** Convert between bytes and time. */
#define ffpcm_bytes(pcm, time_ms) \
	(ffpcm_samples(time_ms, (pcm)->sample_rate) * ffpcm_size1(pcm))
#define ffpcm_bytes2time(pcm, bytes) \
	ffpcm_time((bytes) / ffpcm_size1(pcm), (pcm)->sample_rate)

#define ffpcm_bitrate(bytes, time_ms)  ((bytes) * 8 * 1000 / (time_ms))

/** Combine two streams together. */
FF_EXTN void ffpcm_mix(const ffpcm *pcm, char *stm1, const char *stm2, size_t samples);

/** Convert PCM data. */
FF_EXTN int ffpcm_convert(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const void *in, size_t samples);

/** Convert volume knob position to dB value. */
#define ffpcm_vol2db(pos, db_min) \
	(((pos) != 0) ? (log10(pos) * (db_min)/2 /*log10(100)*/ - (db_min)) : -100)

#define ffpcm_vol2db_inc(pos, pos_max, db_max) \
	((pow(10, (double)(pos) / (pos_max)) - 1) / 10 * (db_max))

/* gain = 10 ^ (db / 20) */
#define ffpcm_db2gain(db)  pow(10, (double)(db) / 20)

FF_EXTN int ffpcm_gain(const ffpcmex *pcm, float gain, const void *in, void *out, uint samples);
