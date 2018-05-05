/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/aformat/flac.h>


#define ERR(f, e) \
	(f)->errtype = e,  FFFLAC_RERR

static const char *const flac_errs[] = {
	"unsupported PCM format",
	"invalid header",
	"too large meta",
	"bad tags",
	"bad seek table",
	"seek error",
	"unrecognized data before frame header",
	"can't find sync",
	"invalid total samples value in FLAC header",
	"too large total samples value in FLAC header",
};

const char* _ffflac_errstr(uint e)
{
	switch (e) {
	case FLAC_ESYS:
		return fferr_strp(fferr_last());
	}

	if (e >= FLAC_EFMT) {
		e -= FLAC_EFMT;
		FF_ASSERT(e < FFCNT(flac_errs));
		return flac_errs[e];
	}

	return "unknown error";
}


const char* ffflac_out_errstr(ffflac_cook *f)
{
	return _ffflac_errstr(f->errtype);
}

void ffflac_winit(ffflac_cook *f)
{
	f->min_meta = 1000;
	f->seektable_int = (uint)-1;
	f->seekable = 1;
}

int ffflac_wnew(ffflac_cook *f, const ffflac_info *info)
{
	int r;

	if (NULL == ffarr_alloc(&f->outbuf, 4096))
		return ERR(f, FLAC_ESYS);
	r = flac_info_write(f->outbuf.ptr, f->outbuf.cap, info);
	if (r < 0)
		return ERR(f, -r);
	f->outbuf.len += r;

	ffarr_acq(&f->vtag.out, &f->outbuf);
	f->vtag.out.len += sizeof(struct flac_hdr);

	f->info = *info;
	return 0;
}

void ffflac_wclose(ffflac_cook *f)
{
	ffvorbtag_destroy(&f->vtag);
	ffmem_safefree(f->sktab.ptr);
	ffarr_free(&f->outbuf);
}

int ffflac_addtag(ffflac_cook *f, const char *name, const char *val, size_t vallen)
{
	if (0 != ffvorbtag_add(&f->vtag, name, val, vallen))
		return ERR(f, FLAC_EHDR);
	return 0;
}

/** Get buffer with INFO, TAGS and PADDING blocks. */
static int _ffflac_whdr(ffflac_cook *f)
{
	if (f->seektable_int != 0 && f->total_samples != 0) {
		uint interval = (f->seektable_int == (uint)-1) ? f->info.sample_rate : f->seektable_int;
		if (0 > flac_seektab_init(&f->sktab, f->total_samples, interval))
			return ERR(f, FLAC_ESYS);
	}

	ffvorbtag_fin(&f->vtag);
	uint tagoff = f->outbuf.len;
	uint taglen = f->vtag.out.len - sizeof(struct flac_hdr) - tagoff;
	ffarr_acq(&f->outbuf, &f->vtag.out);
	uint padding = ffmax((int)(f->min_meta - taglen), 0);

	uint nblocks = 1 + (padding != 0) + (f->sktab.len != 0);

	flac_sethdr(f->outbuf.ptr + tagoff, FLAC_TTAGS, (nblocks == 1), taglen);

	if (NULL == ffarr_realloc(&f->outbuf, flac_hdrsize(taglen, padding)))
		return ERR(f, FLAC_ESYS);

	if (padding != 0)
		f->outbuf.len += flac_padding_write(ffarr_end(&f->outbuf), padding, (nblocks == 2));

	f->hdrlen = f->outbuf.len;
	return 0;
}

enum {
	W_HDR, W_SEEKTAB_SPACE,
	W_FRAMES,
	W_SEEK0, W_INFO_WRITE, W_SEEKTAB_SEEK, W_SEEKTAB_WRITE,
};

/* FLAC write:
Reserve the space in output file for FLAC stream info.
Write vorbis comments and padding.
Write empty seek table.
After all frames have been written,
  seek back to the beginning and write the complete FLAC stream info and seek table.
*/
int ffflac_write(ffflac_cook *f, uint in_frsamps)
{
	int r;

	for (;;) {
	switch (f->state) {

	case W_HDR:
		if (0 != (r = _ffflac_whdr(f)))
			return r;

		ffstr_set2(&f->out, &f->outbuf);
		f->outbuf.len = 0;
		f->state = W_SEEKTAB_SPACE;
		return FFFLAC_RDATA;

	case W_SEEKTAB_SPACE: {
		f->state = W_FRAMES;
		if (f->sktab.len == 0)
			continue;
		uint len = flac_seektab_size(f->sktab.len);
		if (NULL == ffarr_realloc(&f->outbuf, len))
			return ERR(f, FLAC_ESYS);
		f->seektab_off = f->hdrlen;
		ffmem_zero(f->outbuf.ptr, len);
		ffstr_set(&f->out, f->outbuf.ptr, len);
		return FFFLAC_RDATA;
	}

	case W_FRAMES:
		if (f->fin) {
			f->state = W_SEEK0;
			continue;
		}
		if (f->in.len == 0)
			return FFFLAC_RMORE;
		f->iskpt = flac_seektab_add(f->sktab.ptr, f->sktab.len, f->iskpt, f->nsamps, f->frlen, in_frsamps);
		f->out = f->in;
		f->in.len = 0;
		f->frlen += f->out.len;
		f->nsamps += in_frsamps;
		return FFFLAC_RDATA;

	case W_SEEK0:
		if (!f->seekable) {
			f->out.len = 0;
			return FFFLAC_RDONE;
		}
		f->state = W_INFO_WRITE;
		f->seekoff = 0;
		return FFFLAC_RSEEK;

	case W_INFO_WRITE:
		f->info.total_samples = f->nsamps;
		r = flac_info_write(f->outbuf.ptr, f->outbuf.cap, &f->info);
		if (r < 0)
			return ERR(f, -r);
		ffstr_set(&f->out, f->outbuf.ptr, r);
		if (f->sktab.len == 0)
			return FFFLAC_RDONE;
		f->state = W_SEEKTAB_SEEK;
		return FFFLAC_RDATA;

	case W_SEEKTAB_SEEK:
		f->state = W_SEEKTAB_WRITE;
		f->seekoff = f->seektab_off;
		return FFFLAC_RSEEK;

	case W_SEEKTAB_WRITE:
		r = flac_seektab_write(f->outbuf.ptr, f->outbuf.cap, f->sktab.ptr, f->sktab.len, f->info.minblock);
		ffstr_set(&f->out, f->outbuf.ptr, r);
		return FFFLAC_RDONE;
	}
	}

	return FFFLAC_RERR;
}
