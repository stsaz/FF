/** FLAC.
Copyright (c) 2017 Simon Zolin
*/

/*
fLaC (HDR STREAMINFO) [HDR BLOCK]... (FRAME_HDR SUBFRAME... FRAME_FOOTER)...

ffflac_cook:
fLaC INFO VORBIS_CMT [PADDING] [SEEKTABLE] (FRAME)...
*/

#pragma once

#include <FF/aformat/flac-fmt.h>
#include <FF/mtags/vorbistag.h>
#include <FF/audio/pcm.h>
#include <FF/array.h>


typedef struct ffflac_cook {
	uint state;
	uint errtype;
	ffarr outbuf;
	uint64 nsamps;
	uint64 seekoff;
	ffflac_info info;

	ffvorbtag_cook vtag;
	uint min_meta; // minimum size of meta data (add padding block if needed)
	uint64 hdrlen;

	uint64 total_samples;
	uint seektable_int; // interval (in samples) for seek table.  Default: 1 sec.  0=disabled.
	uint iskpt;
	_ffflac_seektab sktab;
	uint64 frlen;
	uint seektab_off;

	uint fin :1;
	uint seekable :1;
	ffstr in, out;
} ffflac_cook;

FF_EXTN const char* ffflac_out_errstr(ffflac_cook *f);

FF_EXTN void ffflac_winit(ffflac_cook *f);

/**
@info: passed from ffflac_enc.
*/
FF_EXTN int ffflac_wnew(ffflac_cook *f, const ffflac_info *info);
FF_EXTN void ffflac_wclose(ffflac_cook *f);

/**
@in_frsamps: audio samples encoded in this frame */
FF_EXTN int ffflac_write(ffflac_cook *f, uint in_frsamps);

static FFINL void ffflac_wfin(ffflac_cook *f, const ffflac_info *info)
{
	f->fin = 1;
	f->info = *info;
}

FF_EXTN int ffflac_addtag(ffflac_cook *f, const char *name, const char *val, size_t vallen);

#define ffflac_iaddtag(f, tag, val, vallen) \
	ffflac_addtag(f, ffvorbtag_str[tag], val, vallen)

/** Get an absolute file offset to seek. */
#define ffflac_wseekoff(f)  ((f)->seekoff)

/** Get approximate output file size. */
static FFINL uint64 ffflac_wsize(ffflac_cook *f)
{
	return f->hdrlen + f->total_samples * f->info.bits / 8 * f->info.channels * 73 / 100;
}
