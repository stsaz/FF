/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/flac.h>
#include <FF/number.h>
#include <FF/crc.h>
#include <FFOS/error.h>

enum {
	FLAC_MAXFRAME = 1 * 1024 * 1024,
	MAX_META = 1 * 1024 * 1024,
	MAX_NOSYNC = 1 * 1024 * 1024,
};

static int pcm_from32(const int **src, void **dst, uint dstbits, uint channels, uint samples);
static int pcm_to32(int **dst, const void **src, uint srcbits, uint channels, uint samples);

static int _ffflac_findsync(ffflac *f);
static int _ffflac_meta(ffflac *f);
static int _ffflac_init(ffflac *f);
static int _ffflac_seek(ffflac *f);
static int _ffflac_findhdr(ffflac *f, ffflac_frame *fr);
static int _ffflac_getframe(ffflac *f, ffstr *sframe);


/** Convert data between 32bit integer and any other integer PCM format.
e.g. 16bit: "11 22 00 00" <-> "11 22" */

static int pcm_from32(const int **src, void **dst, uint dstbits, uint channels, uint samples)
{
	uint ic, i;
	union {
	char **pb;
	short **psh;
	} to;
	to.psh = (void*)dst;

	switch (dstbits) {
	case 8:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ic][i] = (char)src[ic][i];
			}
		}
		break;

	case 16:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ic][i] = (short)src[ic][i];
			}
		}
		break;

	case 24:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ic][i * 3], src[ic][i]);
			}
		}
		break;

	default:
		return -1;
	}
	return 0;
}

static int pcm_to32(int **dst, const void **src, uint srcbits, uint channels, uint samples)
{
	uint ic, i;
	union {
	char **pb;
	short **psh;
	} from;
	from.psh = (void*)src;

	switch (srcbits) {
	case 8:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				dst[ic][i] = from.pb[ic][i];
			}
		}
		break;

	case 16:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				dst[ic][i] = from.psh[ic][i];
			}
		}
		break;

	case 24:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				dst[ic][i] = ffint_ltoh24s(&from.pb[ic][i * 3]);
			}
		}
		break;

	default:
		return -1;
	}

	return 0;
}


void ffflac_init(ffflac *f)
{
	ffmem_tzero(f);
}

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

const char* ffflac_errstr(ffflac *f)
{
	switch (f->errtype) {
	case FLAC_ESYS:
		return fferr_strp(fferr_last());

	case FLAC_ELIB:
		return flac_errstr(f->err);
	}

	if (f->errtype >= FLAC_EFMT) {
		uint e = f->errtype - FLAC_EFMT;
		FF_ASSERT(e < FFCNT(flac_errs));
		return flac_errs[e];
	}

	return "unknown error";
}

void ffflac_close(ffflac *f)
{
	ffarr_free(&f->buf);
	ffmem_safefree(f->sktab.ptr);
	if (f->dec != NULL)
		flac_decode_free(f->dec);
}

enum {
	I_INFO, I_META, I_SKIPMETA, I_TAG, I_TAG_PARSE, I_SEEKTBL, I_METALAST,
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

		if (f->total_size == 0 || f->info.total_samples == 0
			|| f->info.minblock == 0 || f->info.minblock != f->info.maxblock)
			break; //seeking not supported

		f->st = I_SEEKTBL;
		return 0;
	}

	f->off += f->blksize;
	return FFFLAC_RSEEK;
}

static int _ffflac_init(ffflac *f)
{
	int r;
	flac_conf si = {0};
	si.min_blocksize = f->info.minblock;
	si.max_blocksize = f->info.maxblock;
	si.rate = f->fmt.sample_rate;
	si.channels = f->fmt.channels;
	si.bps = ffpcm_bits(f->fmt.format);
	if (0 != (r = flac_decode_init(&f->dec, &si))) {
		f->errtype = FLAC_ELIB;
		f->err = r;
		return FFFLAC_RERR;
	}

	if (f->total_size != 0) {
		if (f->sktab.len == 0 && f->info.total_samples != 0) {
			if (NULL == (f->sktab.ptr = ffmem_tcalloc(ffpcm_seekpt, 2))) {
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
	sk.fr_index = f->frame.num * f->frame.samples;
	sk.fr_samples = f->frame.samples;
	sk.avg_fr_samples = f->info.minblock;
	sk.fr_size = sizeof(struct flac_frame); //note: frame body size is not known, this may slightly reduce efficiency of ffpcm_seek()
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
	size_t hdroff;
	uint hdrlen;

	fr->channels = f->fmt.channels;
	fr->rate = f->fmt.sample_rate;
	fr->bps = ffpcm_bits(f->fmt.format);

	if (NULL == ffarr_append(&f->buf, f->data, f->datalen)) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	hdroff = f->buf.len - f->bufoff;
	hdrlen = flac_frame_find(f->buf.ptr + f->bufoff, &hdroff, fr);

	if (hdrlen == 0) {
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

	if (hdroff != 0) {
		f->bufoff += hdroff;
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
	uint hdrlen;

	fr.channels = f->fmt.channels;
	fr.rate = f->fmt.sample_rate;
	fr.bps = ffpcm_bits(f->fmt.format);

	if (NULL == ffarr_append(&f->buf, f->data, f->datalen)) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	frlen = f->buf.len - f->bufoff - sizeof(struct flac_frame);
	hdrlen = flac_frame_find(f->buf.ptr + f->bufoff + sizeof(struct flac_frame), &frlen, &fr);
	frlen += sizeof(struct flac_frame);

	if (hdrlen == 0 && !f->fin) {

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
	f->bufoff += frlen;
	f->off += f->datalen;
	FFARR_SHIFT(f->data, f->datalen, f->datalen);
	return 0;
}

int ffflac_decode(ffflac *f)
{
	int r;
	uint isrc, ich;
	const int **out;
	ffstr sframe;

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
		// break

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
		f->buf.len = 0;
		if (0 > flac_seektab(f->buf.ptr, f->blksize, &f->sktab, f->info.total_samples)) {
			f->st = I_SKIPMETA;
			f->errtype = FLAC_ESEEKTAB;
			return FFFLAC_RWARN;
		}
		f->off += f->blksize;
		f->st = (f->hdrlast) ? I_METALAST : I_META;
		break;

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
		f->st = I_FRHDR;
		// break

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
		// break

	case I_DATA:
		if (0 != (r = _ffflac_getframe(f, &sframe)))
			return r;

		FFDBG_PRINT(10, "%s(): frame #%u: size:%L, samples:%u\n"
			, FF_FUNC, f->frame.num, sframe.len, f->frame.samples);

		r = flac_decode(f->dec, sframe.ptr, sframe.len, &out);
		if (r != 0) {
			f->errtype = FLAC_ELIB;
			f->err = r;
			f->st = I_FROK;
			return FFFLAC_RWARN;
		}

		f->pcm = (void**)f->out;
		f->pcmlen = f->frame.samples;
		f->frsample = f->frame.num * f->info.minblock;
		isrc = 0;
		if (f->seek_ok) {
			f->seek_ok = 0;
			FF_ASSERT(f->seeksample >= f->frsample);
			isrc = f->seeksample - f->frsample;
			f->pcmlen -= isrc;
			f->frsample = f->seeksample;
			f->seeksample = 0;
		}

		for (ich = 0;  ich != f->fmt.channels;  ich++) {
			f->out[ich] = out[ich];
		}

		const int *out2[FLAC__MAX_CHANNELS];
		for (ich = 0;  ich != f->fmt.channels;  ich++) {
			out2[ich] = out[ich] + isrc;
		}

		//in-place conversion
		pcm_from32(out2, (void*)f->out, ffpcm_bits(f->fmt.format), f->fmt.channels, f->pcmlen);
		f->pcmlen *= ffpcm_size1(&f->fmt);
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
		if (f->info.total_samples != 0 && f->info.total_samples != f->frsample + f->frame.samples) {
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


const char* ffflac_enc_errstr(ffflac_enc *f)
{
	ffflac fl;
	fl.errtype = f->errtype;
	fl.err = f->err;
	return ffflac_errstr(&fl);
}

int ffflac_addtag(ffflac_enc *f, const char *name, const char *val, size_t vallen)
{
	if (0 != ffvorbtag_add(&f->vtag, name, val, vallen)) {
		f->errtype = FLAC_EHDR;
		return FFFLAC_RERR;
	}
	return 0;
}

enum ENC_STATE {
	ENC_HDR, ENC_FRAMES,
	ENC_SEEK0, ENC_INFO_WRITE, ENC_SEEKTAB_SEEK, ENC_SEEKTAB_WRITE,
};

void ffflac_enc_init(ffflac_enc *f)
{
	ffmem_tzero(f);
	f->min_meta = 1000;
	f->seektable_int = (uint)-1;
	f->level = 5;
	f->seekable = 1;
}

void ffflac_enc_close(ffflac_enc *f)
{
	ffvorbtag_destroy(&f->vtag);
	ffarr_free(&f->outbuf);
	ffmem_safefree(f->sktab.ptr);
	FF_SAFECLOSE(f->enc, NULL, flac_encode_free);
}

int ffflac_create(ffflac_enc *f, ffpcm *pcm)
{
	int r;

	switch (pcm->format) {
	case FFPCM_8:
	case FFPCM_16:
	case FFPCM_24:
		break;

	default:
		pcm->format = FFPCM_24;
		f->errtype = FLAC_EFMT;
		return -1;
	}

	flac_conf conf = {0};
	conf.bps = ffpcm_bits(pcm->format);
	conf.channels = pcm->channels;
	conf.rate = pcm->sample_rate;
	conf.level = f->level;
	conf.nomd5 = !!(f->opts & FFFLAC_ENC_NOMD5);

	if (0 != (r = flac_encode_init(&f->enc, &conf))) {
		f->errtype = FLAC_ELIB;
		f->err = r;
		return FFFLAC_RERR;
	}

	if (f->seektable_int != 0 && f->total_samples != 0) {
		uint interval = (f->seektable_int == (uint)-1) ? pcm->sample_rate : f->seektable_int;
		if (0 > flac_seektab_init(&f->sktab, f->total_samples, interval)) {
			f->errtype = FLAC_ESYS;
			return FFFLAC_RERR;
		}
	}

	if (NULL == ffarr_alloc(&f->outbuf, 4096)) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	flac_conf info;
	flac_encode_info(f->enc, &info);
	f->info.minblock = info.min_blocksize;
	f->info.maxblock = info.max_blocksize;
	f->info.channels = pcm->channels;
	f->info.sample_rate = pcm->sample_rate;
	f->info.bits = ffpcm_bits(pcm->format);
	r = flac_info_write(f->outbuf.ptr, f->outbuf.cap, &f->info);
	if (r < 0) {
		f->errtype = -r;
		return FFFLAC_RERR;
	}
	f->outbuf.len += r;

	ffarr_acq(&f->vtag.out, &f->outbuf);
	f->vtag.out.len += sizeof(struct flac_hdr);
	const char *vendor = flac_vendor();
	if (0 != ffvorbtag_add(&f->vtag, NULL, vendor, ffsz_len(vendor))) {
		f->errtype = FLAC_EHDR;
		return FFFLAC_RERR;
	}

	return 0;
}

static int _ffflac_enc_hdr(ffflac_enc *f)
{
	ffvorbtag_fin(&f->vtag);
	uint tagoff = f->outbuf.len;
	uint taglen = f->vtag.out.len - sizeof(struct flac_hdr) - tagoff;
	ffarr_acq(&f->outbuf, &f->vtag.out);
	uint padding = f->min_meta - taglen;

	flac_sethdr(f->outbuf.ptr + tagoff, FLAC_TTAGS, (padding == 0 && f->sktab.len == 0), taglen);

	if (NULL == ffarr_realloc(&f->outbuf, flac_hdrsize(taglen, padding, f->sktab.len))) {
		f->errtype = FLAC_ESYS;
		return FFFLAC_RERR;
	}

	if (padding != 0)
		f->outbuf.len += flac_padding_write(ffarr_end(&f->outbuf), padding, f->sktab.len == 0);

	if (f->sktab.len != 0) {
		f->seektab_off = f->outbuf.len;
		uint len = flac_seektab_size(f->sktab.len);
		ffmem_zero(ffarr_end(&f->outbuf), len);
		f->outbuf.len += len;
	}

	f->metalen = f->outbuf.len;
	return 0;
}

/*
Reserve the space in output file for FLAC stream info and seek table.
Write vorbis comments and padding.
Encode audio data into FLAC frames.
  An input sample must be within 32-bit container.
  To encode a frame libFLAC needs NBLOCK+1 input samples.
  flac_encode() returns a frame with NBLOCK encoded samples,
    so 1 sample always stays cached in libFLAC until we explicitly flush output data.
After all frames have been written,
  seek back to the beginning and write the complete FLAC stream info and seek table.
*/
int ffflac_encode(ffflac_enc *f)
{
	uint samples, sampsize, blksize;
	int r;

	switch (f->state) {

	case ENC_HDR:
		if (0 != (r = _ffflac_enc_hdr(f)))
			return r;

		if (NULL == ffarr_realloc(&f->outbuf, (f->info.minblock + 1) * sizeof(int) * f->info.channels))
			return f->errtype = FLAC_ESYS,  FFFLAC_RERR;
		for (uint i = 0;  i != f->info.channels;  i++) {
			f->pcm32[i] = (void*)(f->outbuf.ptr + (f->info.minblock + 1) * sizeof(int) * i);
		}
		f->cap_pcm32 = f->info.minblock + 1;

		f->state = ENC_FRAMES;
		f->data = (void*)f->outbuf.ptr;
		f->datalen = f->outbuf.len;
		f->outbuf.len = 0;
		return FFFLAC_RDATA;

	case ENC_FRAMES:
		break;

	case ENC_SEEK0:
		if (!f->seekable) {
			f->datalen = 0;
			return FFFLAC_RDONE;
		}
		f->state = ENC_INFO_WRITE;
		f->seekoff = 0;
		return FFFLAC_RSEEK;

	case ENC_INFO_WRITE: {
		flac_conf info = {0};
		flac_encode_info(f->enc, &info);
		f->info.minblock = info.min_blocksize;
		f->info.maxblock = info.max_blocksize;
		f->info.minframe = info.min_framesize;
		f->info.maxframe = info.max_framesize;
		f->info.total_samples = f->nsamps;
		ffmemcpy(f->info.md5, info.md5, sizeof(f->info.md5));
		r = flac_info_write(f->outbuf.ptr, f->outbuf.cap, &f->info);
		if (r < 0) {
			f->errtype = -r;
			return FFFLAC_RERR;
		}
		f->data = (void*)f->outbuf.ptr;
		f->datalen = r;
		if (f->sktab.len == 0)
			return FFFLAC_RDONE;
		f->state = ENC_SEEKTAB_SEEK;
		return FFFLAC_RDATA;
	}

	case ENC_SEEKTAB_SEEK:
		f->state = ENC_SEEKTAB_WRITE;
		f->seekoff = f->seektab_off;
		return FFFLAC_RSEEK;

	case ENC_SEEKTAB_WRITE:
		r = flac_seektab_write(f->outbuf.ptr, f->outbuf.cap, f->sktab.ptr, f->sktab.len, f->info.minblock);
		f->data = (void*)f->outbuf.ptr;
		f->datalen = r;
		return FFFLAC_RDONE;
	}

	sampsize = f->info.bits/8 * f->info.channels;
	samples = ffmin(f->pcmlen / sampsize - f->off_pcm, f->cap_pcm32 - f->off_pcm32);

	if (samples == 0 && !f->fin) {
		f->off_pcm = 0;
		return FFFLAC_RMORE;
	}

	const void* src[FLAC__MAX_CHANNELS];
	int* dst[FLAC__MAX_CHANNELS];

	for (uint i = 0;  i != f->info.channels;  i++) {
		src[i] = f->pcm[i] + f->off_pcm * f->info.bits/8;
		dst[i] = f->pcm32[i] + f->off_pcm32;
	}

	if (0 != (r = pcm_to32(dst, src, f->info.bits, f->info.channels, samples)))
		return f->errtype = FLAC_EFMT,  FFFLAC_RERR;

	f->off_pcm += samples;
	f->off_pcm32 += samples;
	if (!(f->off_pcm32 == f->cap_pcm32 || f->fin)) {
		f->off_pcm = 0;
		return FFFLAC_RMORE;
	}

	samples = f->off_pcm32;
	f->off_pcm32 = 0;
	r = flac_encode(f->enc, (const int**)f->pcm32, &samples, (char**)&f->data);
	if (r < 0)
		return f->errtype = FLAC_ELIB,  f->err = r,  FFFLAC_RERR;

	blksize = f->info.minblock;
	if (r == 0 && f->fin) {
		samples = 0;
		r = flac_encode(f->enc, (const int**)f->pcm32, &samples, (char**)&f->data);
		if (r < 0)
			return f->errtype = FLAC_ELIB,  f->err = r,  FFFLAC_RERR;
		blksize = samples;
		f->state = ENC_SEEK0;
	}

	FF_ASSERT(r != 0);
	FF_ASSERT(samples == f->cap_pcm32 || f->fin);

	if (f->cap_pcm32 == f->info.minblock + 1)
		f->cap_pcm32 = f->info.minblock;

	f->datalen = r;
	f->iskpt = flac_seektab_add(f->sktab.ptr, f->sktab.len, f->iskpt, f->nsamps, f->frlen, blksize);
	f->frlen += f->datalen;
	f->nsamps += blksize;
	return FFFLAC_RDATA;
}
