/**
Copyright (c) 2017 Simon Zolin
*/

#include <FF/aformat/flac.h>
#include <FF/mtags/mmtag.h>


enum {
	FLAC_MAXFRAME = 1 * 1024 * 1024,
	MAX_META = 1 * 1024 * 1024,
	MAX_NOSYNC = 1 * 1024 * 1024,
};

static int _ffflac_findsync(ffflac *f);
static int _ffflac_meta(ffflac *f);
static int _ffflac_init(ffflac *f);
static int _ffflac_seek(ffflac *f);
static int _ffflac_findhdr(ffflac *f, ffflac_frame *fr);
static int _ffflac_getframe(ffflac *f, ffstr *sframe);


#define ERR(f, e) \
	(f)->errtype = e,  FFFLAC_RERR

static const char *const flac_errs[] = {
	"unsupported PCM format", //FLAC_EFMT
	"invalid header", //FLAC_EHDR
	"too large meta", //FLAC_EBIGMETA
	"bad tags", //FLAC_ETAG
	"bad picture", //FLAC_EPIC
	"bad seek table", //FLAC_ESEEKTAB
	"seek error", //FLAC_ESEEK
	"unrecognized data before frame header", //FLAC_ESYNC
	"can't find sync", //FLAC_ENOSYNC
	"invalid total samples value in FLAC header", //FLAC_EHDRSAMPLES
	"too large total samples value in FLAC header", //FLAC_EBIGHDRSAMPLES
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


void ffflac_init(ffflac *f)
{
	ffmem_tzero(f);
}

const char* ffflac_errstr(ffflac *f)
{
	switch (f->errtype) {
	case FLAC_ESYS:
		return fferr_strp(fferr_last());
	}

	return _ffflac_errstr(f->errtype);
}

void ffflac_close(ffflac *f)
{
	ffarr_free(&f->buf);
	ffmem_safefree(f->sktab.ptr);
}

enum {
	I_INFO, I_META, I_SKIPMETA, I_TAG, I_TAG_PARSE, I_SEEKTBL, I_PIC, I_METALAST,
	I_INIT, I_DATA, I_FRHDR, I_FROK, I_SEEK, I_FIN
};

int ffflac_open(ffflac *f)
{
	return 0;
}

void ffflac_seek(ffflac *f, uint64 sample)
{
	if (f->st == I_INIT) {
		f->seeksample = sample;
		return;
	}

	int i;
	if (0 > (i = flac_seektab_find(f->sktab.ptr, f->sktab.len, sample)))
		return;
	f->seekpt[0] = f->sktab.ptr[i];
	f->seekpt[1] = f->sktab.ptr[i + 1];
	f->skoff = -1;
	f->seeksample = sample;
	f->st = I_SEEK;
}

static int _ffflac_findsync(ffflac *f)
{
	int r;
	uint lastblk = 0;

	struct ffbuf_gather d = {0};
	ffstr_set(&d.data, f->data, f->datalen);
	d.ctglen = FLAC_MINSIZE;

	while (FFBUF_DONE != (r = ffbuf_gather(&f->buf, &d))) {

		if (r == FFBUF_ERR) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;

		} else if (r == FFBUF_MORE) {
			f->bytes_skipped += f->datalen;
			if (f->bytes_skipped > MAX_NOSYNC) {
				f->errtype = FLAC_EHDR;
				return FFFLAC_RERR;
			}

			return FFFLAC_RMORE;
		}

		ssize_t n = ffstr_find((ffstr*)&f->buf, FLAC_SYNC, FLAC_SYNCLEN);
		if (n < 0)
			continue;
		r = flac_info(f->buf.ptr + n, f->buf.len - n, &f->info, &f->fmt, &lastblk);
		if (r > 0) {
			d.off = n + 1;
		} else if (r == -1 || r == 0) {
			f->errtype = FLAC_EHDR;
			return FFFLAC_RERR;
		}
	}
	f->off += f->datalen - d.data.len;
	f->data = d.data.ptr;
	f->datalen = d.data.len;
	f->bytes_skipped = 0;
	f->buf.len = 0;
	f->st = lastblk ? I_METALAST : I_META;
	return 0;
}

/** Process header of meta block. */
static int _ffflac_meta(ffflac *f)
{
	uint islast;
	int r;

	r = ffarr_append_until(&f->buf, f->data, f->datalen, sizeof(struct flac_hdr));
	if (r == 0)
		return FFFLAC_RMORE;
	else if (r == -1) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}
	FFARR_SHIFT(f->data, f->datalen, r);
	f->off += sizeof(struct flac_hdr);
	f->buf.len = 0;

	r = flac_hdr(f->buf.ptr, sizeof(struct flac_hdr), &f->blksize, &islast);

	if (islast) {
		f->hdrlast = 1;
		f->st = I_METALAST;
	}

	if (f->off + f->blksize > MAX_META) {
		f->errtype = FLAC_EBIGMETA;
		return FFFLAC_RERR;
	}

	switch (r) {
	case FLAC_TTAGS:
		f->st = I_TAG;
		return 0;

	case FLAC_TSEEKTABLE:
		if (f->sktab.len != 0)
			break; //take only the first seek table

		if (f->total_size == 0 || f->info.total_samples == 0)
			break; //seeking not supported

		f->st = I_SEEKTBL;
		return 0;

	case FLAC_TPIC:
		f->st = I_PIC;
		return 0;
	}

	f->off += f->blksize;
	return FFFLAC_RSEEK;
}

static int _ffflac_init(ffflac *f)
{
	if (f->total_size != 0) {
		if (f->sktab.len == 0 && f->info.total_samples != 0) {
			if (NULL == (f->sktab.ptr = ffmem_callocT(2, ffpcm_seekpt))) {
				f->errtype = FLAC_ESYS;
				return FFFLAC_RERR;
			}
			f->sktab.ptr[1].sample = f->info.total_samples;
			f->sktab.len = 2;
		}

		if (f->sktab.len != 0)
			flac_seektab_finish(&f->sktab, f->total_size - f->framesoff);
	}

	return 0;
}

static int _ffflac_seek(ffflac *f)
{
	int r;
	struct ffpcm_seek sk;

	r = _ffflac_findhdr(f, &f->frame);
	if (r != 0 && !(r == FFFLAC_RWARN && f->errtype == FLAC_ESYNC))
		return r;

	sk.target = f->seeksample;
	sk.off = f->off - f->framesoff - (f->buf.len - f->bufoff);
	sk.lastoff = f->skoff;
	sk.pt = f->seekpt;
	sk.fr_index = f->frame.pos;
	sk.fr_samples = f->frame.samples;
	sk.avg_fr_samples = (f->info.minblock + f->info.maxblock) / 2;
	sk.fr_size = FLAC_MINFRAMEHDR;
	sk.flags = FFPCM_SEEK_ALLOW_BINSCH;

	r = ffpcm_seek(&sk);
	if (r == 1) {
		f->skoff = sk.off;
		f->off = f->framesoff + sk.off;
		f->datalen = 0;
		f->buf.len = 0;
		f->bufoff = 0;
		return FFFLAC_RSEEK;

	} else if (r == -1) {
		f->errtype = FLAC_ESEEK;
		return FFFLAC_RERR;
	}

	return 0;
}

/** Find frame header.
Return 0 on success. */
static int _ffflac_findhdr(ffflac *f, ffflac_frame *fr)
{
	ssize_t r;

	if (NULL == ffarr_append(&f->buf, f->data, f->datalen)) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	r = flac_frame_find(f->buf.ptr + f->bufoff, f->buf.len - f->bufoff, fr, f->first_framehdr);
	if (fr->num != (uint)-1)
		fr->pos = fr->num * f->info.minblock;

	if (r < 0) {
		if (f->fin)
			return FFFLAC_RDONE;

		f->bytes_skipped += f->datalen;
		if (f->bytes_skipped > MAX_NOSYNC) {
			f->errtype = FLAC_ENOSYNC;
			return FFFLAC_RERR;
		}

		_ffarr_rmleft(&f->buf, f->bufoff, sizeof(char));
		f->bufoff = 0;
		f->off += f->datalen;
		return FFFLAC_RMORE;
	}

	if (f->bytes_skipped != 0)
		f->bytes_skipped = 0;

	f->off += f->datalen;
	FFARR_SHIFT(f->data, f->datalen, f->datalen);

	if (r != 0) {
		f->bufoff += r;
		f->errtype = FLAC_ESYNC;
		return FFFLAC_RWARN;
	}
	return 0;
}

/** Get frame header and body.
Frame length becomes known only after the next frame is found. */
static int _ffflac_getframe(ffflac *f, ffstr *sframe)
{
	ffflac_frame fr;
	size_t frlen;
	ssize_t r;

	if (NULL == ffarr_append(&f->buf, f->data, f->datalen)) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	frlen = f->buf.len - f->bufoff;
	r = flac_frame_find(f->buf.ptr + f->bufoff + FLAC_MINFRAMEHDR, frlen - FLAC_MINFRAMEHDR, &fr, f->first_framehdr);
	if (r >= 0)
		frlen = r + FLAC_MINFRAMEHDR;

	if (fr.num != (uint)-1)
		fr.pos = fr.num * f->info.minblock;

	if (r < 0 && !f->fin) {

		f->bytes_skipped += f->datalen;
		if (f->bytes_skipped > FLAC_MAXFRAME) {
			f->errtype = FLAC_ENOSYNC;
			return FFFLAC_RERR;
		}

		_ffarr_rmleft(&f->buf, f->bufoff, sizeof(char));
		f->bufoff = 0;
		f->off += f->datalen;
		return FFFLAC_RMORE;
	}

	if (f->bytes_skipped != 0)
		f->bytes_skipped = 0;

	ffstr_set(sframe, f->buf.ptr + f->bufoff, frlen);
	if (*(uint*)f->first_framehdr == 0)
		*(uint*)f->first_framehdr = *(uint*)sframe->ptr;

	f->bufoff += frlen;
	f->off += f->datalen;
	FFARR_SHIFT(f->data, f->datalen, f->datalen);
	return 0;
}

int ffflac_read(ffflac *f)
{
	int r;

	for (;;) {
	switch (f->st) {

	case I_INFO:
		if (0 != (r = _ffflac_findsync(f)))
			return r;

		return FFFLAC_RHDR;

	case I_SKIPMETA:
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		f->off += f->blksize;
		return FFFLAC_RSEEK;

	case I_META:
		r = _ffflac_meta(f);
		if (r != 0)
			return r;
		break;

	case I_TAG:
		r = ffarr_append_until(&f->buf, f->data, f->datalen, f->blksize);
		if (r == 0)
			return FFFLAC_RMORE;
		else if (r == -1) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}

		FFARR_SHIFT(f->data, f->datalen, r);
		f->buf.len = 0;
		f->vtag.data = f->buf.ptr;
		f->vtag.datalen = f->blksize;
		f->st = I_TAG_PARSE;
		//fallthrough

	case I_TAG_PARSE:
		r = ffvorbtag_parse(&f->vtag);

		if (r == FFVORBTAG_OK)
			return FFFLAC_RTAG;

		else if (r == FFVORBTAG_ERR) {
			f->st = I_SKIPMETA;
			f->errtype = FLAC_ETAG;
			return FFFLAC_RWARN;
		}

		//FFVORBTAG_DONE
		f->off += f->blksize;
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		break;

	case I_SEEKTBL:
		r = ffarr_append_until(&f->buf, f->data, f->datalen, f->blksize);
		if (r == 0)
			return FFFLAC_RMORE;
		else if (r == -1) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}

		FFARR_SHIFT(f->data, f->datalen, r);
		f->off += f->blksize;
		f->buf.len = 0;
		if (0 > flac_seektab(f->buf.ptr, f->blksize, &f->sktab, f->info.total_samples)) {
			f->st = I_SKIPMETA;
			f->errtype = FLAC_ESEEKTAB;
			return FFFLAC_RWARN;
		}
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		break;

	case I_PIC:
		r = ffarr_append_until(&f->buf, f->data, f->datalen, f->blksize);
		if (r == 0)
			return FFFLAC_RMORE;
		else if (r == -1) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}
		FFARR_SHIFT(f->data, f->datalen, r);
		f->off += f->blksize;
		f->buf.len = 0;

		if (0 != (r = flac_meta_pic(f->buf.ptr, f->blksize, &f->vtag.val))) {
			f->st = I_SKIPMETA;
			f->errtype = r;
			return FFFLAC_RWARN;
		}
		f->vtag.tag = FFMMTAG_PICTURE;
		f->vtag.name.len = 0;
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		return FFFLAC_RTAG;

	case I_METALAST:
		f->st = I_INIT;
		f->framesoff = (uint)f->off;
		f->buf.len = 0;
		return FFFLAC_RHDRFIN;

	case I_INIT:
		if (0 != (r = _ffflac_init(f)))
			return r;
		f->st = I_FRHDR;
		if (f->seeksample != 0)
			ffflac_seek(f, f->seeksample);
		break;

	case I_FROK:
		f->seek_ok = 0;
		f->st = I_FRHDR;
		//fallthrough

	case I_FRHDR:
		r = _ffflac_findhdr(f, &f->frame);
		if (r != 0) {
			if (r == FFFLAC_RWARN)
				f->st = I_DATA;
			else if (r == FFFLAC_RDONE) {
				f->st = I_FIN;
				continue;
			}
			return r;
		}

		f->st = I_DATA;
		//fallthrough

	case I_DATA:
		if (0 != (r = _ffflac_getframe(f, &f->output)))
			return r;

		FFDBG_PRINT(10, "%s(): frame #%d: pos:%U  size:%L, samples:%u\n"
			, FF_FUNC, f->frame.num, f->frame.pos, f->output.len, f->frame.samples);

		f->st = I_FROK;
		return FFFLAC_RDATA;

	case I_SEEK:
		if (0 != (r = _ffflac_seek(f)))
			return r;
		f->seek_ok = 1;
		f->st = I_DATA;
		break;


	case I_FIN:
		if (f->datalen != 0) {
			f->st = I_FIN + 1;
			f->errtype = FLAC_ESYNC;
			return FFFLAC_RWARN;
		}

	case I_FIN + 1:
		if (f->info.total_samples != 0 && f->info.total_samples != f->frame.pos + f->frame.samples) {
			f->st = I_FIN + 2;
			f->errtype = FLAC_EHDRSAMPLES;
			return FFFLAC_RWARN;
		}

	case I_FIN + 2:
		return FFFLAC_RDONE;
	}
	}
	//unreachable
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

	uint nblocks = 1 + (padding != 0) + (f->picdata.len != 0) + (f->sktab.len != 0);

	flac_sethdr(f->outbuf.ptr + tagoff, FLAC_TTAGS, (nblocks == 1), taglen);

	if (NULL == ffarr_realloc(&f->outbuf, flac_hdrsize(taglen, padding)))
		return ERR(f, FLAC_ESYS);

	if (padding != 0)
		f->outbuf.len += flac_padding_write(ffarr_end(&f->outbuf), padding, (nblocks == 2));

	f->hdrlen = f->outbuf.len;
	return 0;
}

enum {
	W_HDR, W_PIC, W_SEEKTAB_SPACE,
	W_FRAMES,
	W_SEEK0, W_INFO_WRITE, W_SEEKTAB_SEEK, W_SEEKTAB_WRITE,
};

/* FLAC write:
Reserve the space in output file for FLAC stream info.
Write vorbis comments and padding.
Write picture.
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
		f->state = W_PIC;
		return FFFLAC_RDATA;

	case W_PIC: {
		f->state = W_SEEKTAB_SPACE;
		if (f->picdata.len == 0)
			continue;
		ssize_t n = flac_pic_write(NULL, 0, &f->picinfo, &f->picdata, 0);
		if (NULL == ffarr_realloc(&f->outbuf, n))
			return ERR(f, FLAC_ESYS);
		n = flac_pic_write(f->outbuf.ptr, f->outbuf.cap, &f->picinfo, &f->picdata, (f->sktab.len == 0));
		if (n < 0)
			return ERR(f, FLAC_EHDR);
		ffstr_set(&f->out, f->outbuf.ptr, n);
		f->hdrlen += n;
		return FFFLAC_RDATA;
	}

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
