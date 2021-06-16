/** MPEG.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FF/string.h>
#include <FF/audio/pcm.h>
#include <avpack/mpeg1-fmt.h>

#include <mpg123/mpg123-ff.h>


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
	uint fin :1;
} ffmpg;

#define ffmpg_input(m, data, len)  ffstr_set(&(m)->input, data, len)

FF_EXTN const char* ffmpg_errstr(ffmpg *m);

enum FFMPG_DEC_O {
	FFMPG_O_INT16 = 1, //libmpg123: produce 16-bit integer output
};

#define ffmpg_init()  mpg123_init()

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
