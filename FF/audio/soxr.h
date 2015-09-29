/** libsoxr wrapper.
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/pcm.h>
#include <FFOS/mem.h>

#include <soxr.h>


typedef struct ffsoxr {
	soxr_t soxr;
	soxr_error_t err;
	uint isampsize
		, osampsize;
	uint nchannels;

	union {
	const void *in_i;
	const void **in;
	};
	uint inlen;

	void *out;
	uint outlen;
	uint outcap;

	uint quality; // 0..4. default:3 (High quality)
	uint in_ileaved :1
		, dither :1
		, fin :1; // the last block of input data
} ffsoxr;

static FFINL void ffsoxr_init(ffsoxr *soxr)
{
	ffmem_tzero(soxr);
	soxr->quality = SOXR_HQ;
}

#define ffsoxr_errstr(soxr)  soxr_strerror((soxr)->err)

FF_EXTN int ffsoxr_create(ffsoxr *soxr, const ffpcmex *inpcm, const ffpcmex *outpcm);

FF_EXTN void ffsoxr_destroy(ffsoxr *soxr);

FF_EXTN int ffsoxr_convert(ffsoxr *soxr);
