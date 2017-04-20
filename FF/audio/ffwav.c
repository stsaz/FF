/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/wav.h>
#include <FF/audio/pcm.h>
#include <FF/data/mmtag.h>
#include <FFOS/error.h>


struct wav_chunk {
	char id[4];
	byte size[4];
};

enum WAV_FMT {
	WAV_PCM = 1,
	WAV_IEEE_FLOAT = 3,
	WAV_EXT = 0xfffe,
};

struct wav_fmt {
	byte format[2]; //enum WAV_FMT
	byte channels[2];
	byte sample_rate[4];
	byte byte_rate[4];
	byte block_align[2];
	byte bit_depth[2]; //bits per sample
};

struct wav_fmtext {
	//struct wav_fmt

	byte size[2]; //22
	byte valid_bits_per_sample[2];// = fmt.bit_depth
	byte channel_mask[4];//0x03 for stereo
	byte subformat[16]; //[0..1]: enum WAV_FMT
};

enum WAV_E {
	WAV_EOK,
	WAV_ENOFMT,
	WAV_EBADFMT,
	WAV_EDUPFMT,
	WAV_EUKN,
	WAV_ESMALL,
	WAV_ELARGE,
	WAV_ERIFF,

	WAV_ESYS,
};


static int wav_fmt(const void *data, size_t len, ffpcm *format, uint *bitrate);
static int wav_fmt_write(void *data, const ffpcm *fmt);
static int wav_findchunk(const void *data, const struct wav_bchunk *ctx, struct ffwav_chunk *chunk, uint64 off);


uint ffwav_fmt(uint pcm_fmt)
{
	switch (pcm_fmt) {
	case FFPCM_FLOAT:
		return WAV_IEEE_FLOAT;

	default:
		return WAV_PCM;
	}
}

/** Parse "fmt " chunk. */
static int wav_fmt(const void *data, size_t len, ffpcm *format, uint *bitrate)
{
	const struct wav_fmt *f = data;
	if (len < sizeof(struct wav_fmt))
		return -WAV_ESMALL;

	uint fmt = ffint_ltoh16(f->format);

	if (fmt == WAV_EXT) {
		const struct wav_fmtext *fx = (void*)(f + 1);
		if (len < sizeof(struct wav_fmt) + sizeof(struct wav_fmtext))
			return -WAV_ESMALL;

		fmt = ffint_ltoh16(fx->subformat);
	}

	switch (fmt) {
	case WAV_PCM: {
		uint bps = ffint_ltoh16(f->bit_depth);
		switch (bps) {
		case 8:
		case 16:
		case 24:
		case 32:
			fmt = bps;
			break;

		default:
			return -WAV_EBADFMT;
		}
		break;
	}

	case WAV_IEEE_FLOAT:
		fmt = FFPCM_FLOAT;
		break;

	default:
		return -WAV_EBADFMT;
	}

	format->format = fmt;
	format->channels = ffint_ltoh16(f->channels);
	format->sample_rate = ffint_ltoh32(f->sample_rate);
	if (format->channels == 0 || format->sample_rate == 0)
		return -WAV_EBADFMT;
	*bitrate = ffint_ltoh32(f->byte_rate) * 8;
	return 0;
}

static int wav_fmt_write(void *data, const ffpcm *fmt)
{
	struct wav_fmt *f = data;
	ffint_htol16(f->format, ffwav_fmt(fmt->format));
	ffint_htol32(f->sample_rate, fmt->sample_rate);
	ffint_htol16(f->channels, fmt->channels);
	ffint_htol16(f->bit_depth, ffpcm_bits(fmt->format));
	ffint_htol16(f->block_align, ffpcm_size1(fmt));
	ffint_htol32(f->byte_rate, fmt->sample_rate * ffpcm_size1(fmt));
	return sizeof(struct wav_fmt);
}


/* Supported chunks:

RIFF WAVE
 "fmt "
 data
 LIST INFO
  *
*/

enum {
	T_RIFF = 1,
	T_FMT,
	T_LIST,
	T_DATA,

	T_TAG = 0x80,
};

enum {
	F_WHOLE = 0x100,
	F_LAST = 0x200,
	F_PADD = 0x1000,
};

#define MINSIZE(n)  ((n) << 24)
#define GET_MINSIZE(flags)  ((flags & 0xff000000) >> 24)

struct wav_bchunk {
	char id[4];
	uint flags;
};

static const struct wav_bchunk wav_ctx_global[] = {
	{ "RIFF", T_RIFF | MINSIZE(4) | F_LAST },
};
static const struct wav_bchunk wav_ctx_riff[] = {
	{ "fmt ", T_FMT | F_WHOLE },
	{ "LIST", T_LIST | MINSIZE(4) },
	{ "data", T_DATA | F_LAST },
};
static const struct wav_bchunk wav_ctx_list[] = {
	{ "IART", T_TAG | FFMMTAG_ARTIST | F_WHOLE },
	{ "ICOP", T_TAG | FFMMTAG_COPYRIGHT | F_WHOLE },
	{ "ICRD", T_TAG | FFMMTAG_DATE | F_WHOLE },
	{ "IGNR", T_TAG | FFMMTAG_GENRE | F_WHOLE },
	{ "INAM", T_TAG | FFMMTAG_TITLE | F_WHOLE },
	{ "IPRD", T_TAG | FFMMTAG_ALBUM | F_WHOLE },
	{ "IPRT", T_TAG | FFMMTAG_TRACKNO | F_WHOLE },
	{ "ISFT", T_TAG | FFMMTAG_VENDOR | F_WHOLE | F_LAST },
};

/** Find chunk within the specified context. */
static int wav_findchunk(const void *data, const struct wav_bchunk *ctx, struct ffwav_chunk *chunk, uint64 off)
{
	const struct wav_chunk *ch = (void*)data;
	chunk->id = 0;
	for (uint i = 0;  ;  i++) {
		if (!ffs_cmp(ch->id, ctx[i].id, 4)) {
			chunk->id = ctx[i].flags & 0xff;
			chunk->flags = ctx[i].flags & 0xffffff00;
			break;
		}
		if (ctx[i].flags & F_LAST)
			break;
	}

	chunk->size = ffint_ltoh32(ch->size);
	chunk->flags |= (chunk->size % 2) ? F_PADD : 0;

	FFDBG_PRINTLN(10, "chunk \"%4s\"  size:%u  off:%xU"
		, ch->id, chunk->size, off);
	return 0;
}


static const char *const wav_errstr[] = {
	"",
	"no format chunk",
	"bad format chunk",
	"duplicate format chunk",
	"unknown chunk",
	"too small chunk",
	"too large chunk",
	"invalid RIFF chunk",
};

const char* ffwav_errstr(void *_w)
{
	ffwav *w = _w;
	if (w->err == WAV_ESYS)
		return fferr_strp(fferr_last());
	return wav_errstr[w->err];
}

#define ERR(w, e) \
	(w)->err = (e), FFWAV_RERR


void ffwav_close(ffwav *w)
{
	ffarr_free(&w->buf);
}

enum { R_FIRSTCHUNK, R_NEXTCHUNK, R_GATHER, R_CHUNKHDR, R_CHUNK, R_SKIP, R_PADDING,
	R_DATA, R_DATAOK, R_BUFDATA, R_SEEK };

void ffwav_seek(ffwav *w, uint64 sample)
{
	w->cursample = sample;
	w->state = R_SEEK;
}

void ffwav_init(ffwav *w)
{
	w->state = R_FIRSTCHUNK;
	w->chunks[0].ctx = wav_ctx_global;
	w->chunks[0].size = (uint)-1;
}

int ffwav_decode(ffwav *w)
{
	int r;
	struct ffwav_chunk *chunk, *parent;

	for (;;) {
	switch (w->state) {

	case R_SKIP:
		chunk = &w->chunks[w->ictx];
		r = ffmin(chunk->size, w->datalen);
		FFARR_SHIFT(w->data, w->datalen, r);
		chunk->size -= r;
		w->off += r;
		if (chunk->size != 0)
			return FFWAV_RMORE;

		w->state = R_NEXTCHUNK;
		continue;

	case R_PADDING:
		if (w->datalen == 0)
			return FFWAV_RMORE;

		if (((char*)w->data)[0] == '\0') {
			// skip padding byte
			FFARR_SHIFT(w->data, w->datalen, 1);
			w->off += 1;
			parent = &w->chunks[w->ictx - 1];
			if (parent->size != 0)
				parent->size -= 1;
		}

		w->state = R_NEXTCHUNK;
		// break

	case R_NEXTCHUNK:
		chunk = &w->chunks[w->ictx];

		if (chunk->size == 0) {
			if (chunk->flags & F_PADD) {
				chunk->flags &= ~F_PADD;
				w->state = R_PADDING;
				continue;
			}

			uint id = chunk->id;
			ffmem_tzero(chunk);
			w->ictx--;

			switch (id) {
			case T_RIFF:
				return FFWAV_RDONE;
			}

			continue;
		}

		FF_ASSERT(chunk->ctx != NULL);
		// break

	case R_FIRSTCHUNK:
		w->gather_size = sizeof(struct wav_chunk);
		w->state = R_GATHER,  w->nxstate = R_CHUNKHDR;
		// break

	case R_GATHER:
		r = ffarr_append_until(&w->buf, w->data, w->datalen, w->gather_size);
		if (r == 0)
			return FFWAV_RMORE;
		else if (r == -1)
			return ERR(w, WAV_ESYS);
		FFARR_SHIFT(w->data, w->datalen, r);
		w->off += w->gather_size;
		ffstr_set2(&w->gather_buf, &w->buf);
		w->buf.len = 0;
		w->state = w->nxstate;
		continue;

	case R_CHUNKHDR: {
		parent = &w->chunks[w->ictx];
		chunk = &w->chunks[++w->ictx];
		wav_findchunk(w->gather_buf.ptr, parent->ctx, chunk, w->off - w->gather_buf.len);

		if (!(chunk->id == T_DATA && chunk->size == (uint)-1)) {
			if (chunk->size > parent->size)
				return ERR(w, WAV_ELARGE);
			parent->size -= sizeof(struct wav_chunk) + chunk->size;
		}

		if (chunk->id == 0) {
			//unknown chunk
			w->state = R_SKIP;
			continue;
		}

		if (chunk->id == T_DATA && chunk->size == (uint)-1)
			w->inf_data = 1;

		uint minsize = GET_MINSIZE(chunk->flags);
		if (minsize != 0 && chunk->size < minsize)
			return ERR(w, WAV_ESMALL);

		if ((chunk->flags & F_WHOLE) || minsize != 0) {
			w->gather_size = (minsize != 0) ? minsize : chunk->size;
			chunk->size -= w->gather_size;
			w->state = R_GATHER,  w->nxstate = R_CHUNK;
			continue;
		}

		w->state = R_CHUNK;
		continue;
	}

	case R_CHUNK:
		chunk = &w->chunks[w->ictx];

		if (chunk->id & T_TAG) {
			w->tag = chunk->id & ~T_TAG;
			ffstr_setz(&w->tagval, w->gather_buf.ptr);
			w->state = R_NEXTCHUNK;
			return FFWAV_RTAG;
		}

		switch (chunk->id) {
		case T_RIFF:
			if (!!ffs_cmp(w->gather_buf.ptr, "WAVE", 4))
				return ERR(w, WAV_ERIFF);

			w->chunks[w->ictx].ctx = wav_ctx_riff;
			break;

		case T_FMT:
			if (w->has_fmt)
				return ERR(w, WAV_EDUPFMT);
			w->has_fmt = 1;

			if (0 > (r = wav_fmt(w->gather_buf.ptr, w->gather_buf.len, &w->fmt, &w->bitrate)))
				return ERR(w, -r);

			w->sampsize = ffpcm_size1(&w->fmt);
			if (NULL == ffarr_realloc(&w->buf, w->sampsize))
				return ERR(w, WAV_ESYS);
			break;

		case T_LIST:
			if (!!ffs_cmp(w->gather_buf.ptr, "INFO", 4)) {
				w->state = R_SKIP;
				continue;
			}

			w->chunks[w->ictx].ctx = wav_ctx_list;
			break;

		case T_DATA:
			if (!w->has_fmt)
				return ERR(w, WAV_ENOFMT);

			w->dataoff = w->off;
			w->datasize = ff_align_floor(chunk->size, w->sampsize);
			if (!w->inf_data)
				w->total_samples = chunk->size / w->sampsize;
			w->state = R_DATA;
			return FFWAV_RHDR;
		}

		w->state = R_NEXTCHUNK;
		continue;

	case R_SEEK:
		w->off = w->dataoff + w->cursample * w->sampsize;
		w->state = R_DATA;
		return FFWAV_RSEEK;

	case R_DATAOK:
		w->cursample += w->pcmlen / w->sampsize;
		w->state = R_DATA;
		// break

	case R_DATA: {
		uint chunk_size = w->dataoff + w->datasize - w->off;
		if (chunk_size == 0) {
			w->state = R_NEXTCHUNK;
			continue;
		}
		uint n = (uint)ffmin(chunk_size, w->datalen);
		n = ff_align_floor(n, w->sampsize);

		if (n == 0) {
			w->gather_size = w->sampsize;
			w->state = R_GATHER,  w->nxstate = R_BUFDATA;
			continue; //not even 1 complete PCM sample
		}
		w->pcm = (void*)w->data,  w->pcmlen = n;
		FFARR_SHIFT(w->data, w->datalen, n);
		w->off += n;
		w->state = R_DATAOK;
		return FFWAV_RDATA;
	}

	case R_BUFDATA:
		w->pcm = w->gather_buf.ptr,  w->pcmlen = w->gather_buf.len;
		w->state = R_DATAOK;
		return FFWAV_RDATA;

	}
	}
	//unreachable
}


enum { W_HDR, W_DATA, W_HDRFIN, W_DONE };

int ffwav_create(ffwav_cook *w, ffpcm *fmt, uint64 total_samples)
{
	w->state = W_HDR;
	w->fmt = *fmt;
	w->total_samples = total_samples;
	w->dsize = (w->total_samples != 0) ? w->total_samples * ffpcm_size1(&w->fmt) : (uint)-1;
	w->doff = sizeof(struct wav_chunk) + FFSLEN("WAVE")
		+ sizeof(struct wav_chunk) + sizeof(struct wav_fmt)
		+ sizeof(struct wav_chunk);
	w->seekable = 1;
	return 0;
}

void ffwav_wclose(ffwav_cook *w)
{
	ffarr_free(&w->buf);
}

int ffwav_write(ffwav_cook *w)
{
	switch (w->state) {
	case W_HDR:
	case W_HDRFIN: {
		struct wav_chunk *ch;
		void *p;

		if (NULL == ffarr_realloc(&w->buf, w->doff))
			return ERR(w, WAV_ESYS);

		ffmemcpy(w->buf.ptr, "RIFF\0\0\0\0WAVE", 12);
		p = w->buf.ptr + 12;

		ch = p;
		p += sizeof(struct wav_chunk);
		ffmemcpy(ch->id, "fmt ", 4);
		uint fmtsize = sizeof(struct wav_fmt);
		ffint_htol32(ch->size, fmtsize);
		p += wav_fmt_write(p, &w->fmt);

		ch = p;
		p += sizeof(struct wav_chunk);
		ffmemcpy(ch->id, "data", 4);
		ffint_htol32(ch->size, w->dsize);

		ch = (void*)w->buf.ptr;
		ffint_htol32(ch->size, FFSLEN("WAVE")
			+ sizeof(struct wav_chunk) + fmtsize
			+ sizeof(struct wav_chunk) + w->dsize);

		w->data = w->buf.ptr,  w->datalen = (char*)p - w->buf.ptr;

		if (w->state == W_HDRFIN) {
			w->state = W_DONE;
			return FFWAV_RDATA;
		}

		w->dsize = 0;
		w->state = W_DATA;
		return FFWAV_RHDR;
	}

	case W_DONE:
		return FFWAV_RDONE;

	case W_DATA:
		if (w->pcmlen == 0) {
			if (!w->fin)
				return FFWAV_RMORE;

			if (w->dsize == w->total_samples * ffpcm_size1(&w->fmt) || !w->seekable)
				return FFWAV_RDONE; //header already has the correct data size

			w->state = W_HDRFIN;
			w->off = 0;
			return FFWAV_RSEEK;
		}

		if ((uint64)w->dsize + w->pcmlen > (uint)-1)
			return ERR(w, WAV_ELARGE);

		w->dsize += w->pcmlen;
		w->data = w->pcm,  w->datalen = w->pcmlen;
		w->pcmlen = 0;
		return FFWAV_RDATA;
	}

	//unreachable
	return FFWAV_RERR;
}

int ffwav_wsize(ffwav_cook *w)
{
	return w->doff + w->total_samples * ffpcm_size1(&w->fmt);
}
