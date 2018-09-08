/** FLAC.
Copyright (c) 2017 Simon Zolin
*/

/*
fLaC (HDR STREAMINFO) [HDR BLOCK]... (FRAME_HDR SUBFRAME... FRAME_FOOTER)...

ffflac_cook:
fLaC INFO VORBIS_CMT [PADDING] [PICTURE] [SEEKTABLE] (FRAME)...
*/

#pragma once

#include <FF/aformat/flac-fmt.h>
#include <FF/mtags/vorbistag.h>
#include <FF/array.h>


typedef struct ffflac {
	int st;
	uint errtype;
	ffpcm fmt;
	ffarr buf;
	uint bufoff;
	uint64 off
		, total_size;
	uint framesoff;
	ffflac_frame frame;
	byte first_framehdr[4];
	uint64 seeksample;
	uint fin :1
		, hdrlast :1
		, seek_ok :1
		;

	ffflac_info info;
	uint blksize;
	ffvorbtag vtag;

	_ffflac_seektab sktab;
	ffpcm_seekpt seekpt[2];
	uint64 skoff;

	size_t datalen;
	const char *data;
	uint bytes_skipped; // bytes skipped while trying to find sync

	ffstr output;
} ffflac;

FF_EXTN const char* ffflac_errstr(ffflac *f);

FF_EXTN void ffflac_init(ffflac *f);

/** Return 0 on success. */
FF_EXTN int ffflac_open(ffflac *f);

FF_EXTN void ffflac_close(ffflac *f);

/** Return total samples or 0 if unknown. */
#define ffflac_totalsamples(f)  ((f)->info.total_samples)

/** Get average bitrate.  May be called when FFFLAC_RHDRFIN is returned. */
static FFINL uint ffflac_bitrate(ffflac *f)
{
	if (f->total_size == 0)
		return 0;
	return ffpcm_brate(f->total_size - f->framesoff, f->info.total_samples, f->fmt.sample_rate);
}

FF_EXTN void ffflac_seek(ffflac *f, uint64 sample);

/** Get an absolute file offset to seek. */
#define ffflac_seekoff(f)  ((f)->off)

/** Set input data. */
static FFINL void ffflac_input(ffflac *f, const void *d, size_t len)
{
	f->data = d;
	f->datalen = len;
}

/** Get output data (FLAC frame). */
#define ffflac_output(f)  ((f)->output)

/** Return enum FFFLAC_R. */
FF_EXTN int ffflac_read(ffflac *f);

/** Get an absolute sample number. */
#define ffflac_cursample(f)  ((f)->frame.pos)


typedef struct ffflac_cook {
	uint state;
	uint errtype;
	ffarr outbuf;
	uint64 nsamps;
	uint64 seekoff;
	ffflac_info info;

	ffvorbtag_cook vtag;
	struct flac_picinfo picinfo;
	ffstr picdata;
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

/** Set picture.
Data must be valid until the header is written. */
static FFINL void ffflac_setpic(ffflac_cook *f, const struct flac_picinfo *info, const ffstr *pic)
{
	f->picinfo = *info;
	if (f->picinfo.mime == NULL)
		f->picinfo.mime = "";
	if (f->picinfo.desc == NULL)
		f->picinfo.desc = "";
	f->picdata = *pic;
}

/** Get an absolute file offset to seek. */
#define ffflac_wseekoff(f)  ((f)->seekoff)

/** Get approximate output file size. */
static FFINL uint64 ffflac_wsize(ffflac_cook *f)
{
	return f->hdrlen + f->total_samples * f->info.bits / 8 * f->info.channels * 73 / 100;
}
