/** PCM.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


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

/** Channels as string. */
FF_EXTN const char* ffpcm_channelstr(uint channels);


FF_EXTN const byte ffpcm_bits[];
/** Get size of 1 sample (in bytes). */
#define ffpcm_size(format, channels)  ((uint)ffpcm_bits[format]/8 * (channels))

/** Convert between samples and time. */
#define ffpcm_samples(time_ms, rate)   ((uint64)(time_ms) * (rate) / 1000)
#define ffpcm_time(samples, rate)   ((uint64)(samples) * 1000 / (rate))


/** Combine two streams together. */
FF_EXTN void ffpcm_mix(const ffpcm *pcm, char *stm1, const char *stm2, size_t samples);
