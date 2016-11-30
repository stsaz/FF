/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/data/avi.h>
#include <FF/data/mmtag.h>
#include <FFOS/error.h>


struct avi_seekpt {
	uint off;
};

struct avi_chunk {
	char id[4];
	byte size[4];
};

// STRH_TYPE
#define STRH_AUDIO  "auds"
#define STRH_VIDEO  "vids"
#define STRH_TEXT  "txts"

struct strh {
	byte type[4]; //STRH_TYPE
	byte unused[16];
	byte scale[4];
	byte rate[4];
	byte delay[4];
	byte length[4];
};

// MOVI_TYPE
#define MOVI_AUDIO  "wb"
#define MOVI_VIDEO  "dc"


struct strf_audio {
	byte format[2]; //enum STRF_FMT
	byte channels[2];
	byte sample_rate[4];
	byte byte_rate[4];
	byte block_align[2];
	byte bit_depth[2];
	byte exsize[2];
};

struct strf_mp3 {
	byte id[2];
	byte flags[4];
	byte blocksize[2];
	byte blockframes[2];
	byte delay[2];
};

/** Process strf chunk. */
static int avi_strf(struct ffavi_audio_info *ai, const char *data, size_t len)
{
	const struct strf_audio *f = (void*)data;
	ai->format = ffint_ltoh16(&f->format);
	ai->bits = ffint_ltoh16(&f->bit_depth);
	ai->channels = ffint_ltoh16(&f->channels);
	ai->sample_rate = ffint_ltoh32(&f->sample_rate);
	ai->total_samples = ffpcm_samples(ai->len * 1000 * ai->scale / ai->rate, ai->sample_rate);
	ai->bitrate = ffint_ltoh32(&f->byte_rate) * 8;

	uint exsize = ffint_ltoh16(f->exsize);
	if (exsize > len)
		return -1;

	switch (ai->format) {

	case FFAVI_AUDIO_MP3: {
		if (exsize < sizeof(struct strf_mp3))
			break;

		const struct strf_mp3 *mp3 = (void*)(f + 1);
		ai->delay = ffint_ltoh16(mp3->delay);
		ai->blocksize = ffint_ltoh16(mp3->blocksize);
		break;
	}

	case FFAVI_AUDIO_AAC:
		ffstr_set(&ai->asc, (void*)(f + 1), exsize);
		break;
	}

	return 0;
}


enum {
	T_UKN,
	T_ANY,
	T_STRH,
	T_STRF,
	T_INFO,
	T_MOVI,
	T_MOVI_CHUNK,

	_T_TAG,
};

enum AVI_F {
	F_WHOLE = 0x100,
	F_LAST = 0x200,
	F_LIST = 0x400,
};

#define MINSIZE(n)  ((n) << 24)
#define GET_MINSIZE(flags)  ((flags & 0xff000000) >> 24)

typedef struct ffavi_bchunk ffavi_bchunk;
struct ffavi_bchunk {
	char id[4];
	uint flags; //enum AVI_F
	const ffavi_bchunk *ctx;
};

/** Search chunk in the context.
Return -1 if not found. */
static int avi_chunkfind(const ffavi_bchunk *ctx, const char *name)
{
	for (uint i = 0;  ;  i++) {

		if (!ffs_cmp(name, ctx[i].id, 4))
			return i;

		if (ctx[i].flags & F_LAST) {
			if (ctx[i].id[0] == '*')
				return i;
			break;
		}
	}

	return -1;
}

static void avi_chunkinfo(const void *data, const ffavi_bchunk *ctx, struct ffavi_chunk *chunk)
{
	const struct avi_chunk *ch = data;
	int i;
	if (-1 != (i = avi_chunkfind(ctx, ch->id))) {
		chunk->id = ctx[i].flags & 0xff;
		chunk->flags = ctx[i].flags & 0xffffff00;
	} else
		chunk->id = T_UKN;

	chunk->osize = ffint_ltoh32(ch->size);
	chunk->size = chunk->osize;

	FFDBG_PRINTLN(10, "chunk \"%4s\"  size:%u"
		, ch->id, chunk->size);
}


/* Supported chunks:

RIFF "AVI "
 LIST hdrl
  LIST strl
   strh
   strf
 LIST INFO
 LIST movi
  NNwb
*/

static const ffavi_bchunk avi_ctx_riff[];
static const ffavi_bchunk avi_ctx_avi[];
static const ffavi_bchunk avi_ctx_avi_list[];
static const ffavi_bchunk avi_ctx_hdrl[];
static const ffavi_bchunk avi_ctx_hdrl_list[];
static const ffavi_bchunk avi_ctx_strl[];
static const ffavi_bchunk avi_ctx_info[];
static const ffavi_bchunk avi_ctx_movi[];

static const ffavi_bchunk avi_ctx_global[] = {
	{ "RIFF", T_ANY | MINSIZE(4) | F_LIST | F_LAST, avi_ctx_riff },
};
static const ffavi_bchunk avi_ctx_riff[] = {
	{ "AVI ", T_ANY | F_LAST, avi_ctx_avi },
};
static const ffavi_bchunk avi_ctx_avi[] = {
	{ "LIST", T_ANY | MINSIZE(4) | F_LIST | F_LAST, avi_ctx_avi_list },
};
static const ffavi_bchunk avi_ctx_avi_list[] = {
	{ "hdrl", T_ANY, avi_ctx_hdrl },
	{ "INFO", T_INFO, avi_ctx_info },
	{ "movi", T_MOVI, avi_ctx_movi },
};

static const ffavi_bchunk avi_ctx_hdrl[] = {
	{ "LIST", T_ANY | MINSIZE(4) | F_LIST | F_LAST, avi_ctx_hdrl_list },
};
static const ffavi_bchunk avi_ctx_hdrl_list[] = {
	{ "strl", T_ANY | F_LAST, avi_ctx_strl },
};
static const ffavi_bchunk avi_ctx_strl[] = {
	{ "strh", T_STRH | MINSIZE(sizeof(struct strh)), NULL },
	{ "strf", T_STRF | F_WHOLE | MINSIZE(sizeof(struct strf_audio)) | F_LAST, NULL },
};

static const ffavi_bchunk avi_ctx_info[] = {
	{ "IART", (_T_TAG + FFMMTAG_ARTIST) | F_WHOLE, NULL },
	{ "ICOP", (_T_TAG + FFMMTAG_COPYRIGHT) | F_WHOLE, NULL },
	{ "ICRD", (_T_TAG + FFMMTAG_DATE) | F_WHOLE, NULL },
	{ "IGNR", (_T_TAG + FFMMTAG_GENRE) | F_WHOLE, NULL },
	{ "INAM", (_T_TAG + FFMMTAG_TITLE) | F_WHOLE, NULL },
	{ "IPRD", (_T_TAG + FFMMTAG_ALBUM) | F_WHOLE, NULL },
	{ "IPRT", (_T_TAG + FFMMTAG_TRACKNO) | F_WHOLE, NULL },
	{ "ISFT", (_T_TAG + FFMMTAG_VENDOR) | F_WHOLE | F_LAST, NULL },
};

static const ffavi_bchunk avi_ctx_movi[] = {
	{ "*", T_MOVI_CHUNK | F_LAST, NULL },
};


enum AVI_E {
	AVI_EOK,
	AVI_ESMALL,
	AVI_ELARGE,

	AVI_ESYS,
};

static const char *const avi_errstr[] = {
	"",
	"too small chunk",
	"too large chunk",
};

const char* ffavi_errstr(void *_a)
{
	ffavi *a = _a;
	if (a->err == AVI_ESYS)
		return fferr_strp(fferr_last());
	return avi_errstr[a->err];
}

#define ERR(a, e) \
	(a)->err = (e), FFAVI_RERR


void ffavi_close(ffavi *a)
{
	ffarr_free(&a->buf);
	ffstr_free(&a->info.asc);
}

enum { R_GATHER_CHUNKHDR, R_NEXTCHUNK, R_GATHER, R_CHUNK_HDR, R_CHUNK, R_SKIP_PARENT, R_SKIP,
	R_DATA };

void ffavi_init(ffavi *a)
{
	a->state = R_GATHER_CHUNKHDR;
	a->ctx[0] = avi_ctx_global;
}

int ffavi_read(ffavi *a)
{
	int r;

	for (;;) {
	switch (a->state) {

	case R_SKIP_PARENT: {
		struct ffavi_chunk *chunk = &a->chunks[a->ictx];
		a->ctx[a->ictx--] = NULL;
		struct ffavi_chunk *parent = &a->chunks[a->ictx];
		parent->size += chunk->size;
		a->state = R_SKIP;
		// break
	}

	case R_SKIP: {
		struct ffavi_chunk *chunk = &a->chunks[a->ictx];
		r = ffmin(chunk->size, a->data.len);
		ffarr_shift(&a->data, r);
		chunk->size -= r;
		a->off += r;
		if (chunk->size != 0)
			return FFAVI_RMORE;

		a->state = R_NEXTCHUNK;
		// break
	}

	case R_NEXTCHUNK: {
		struct ffavi_chunk *chunk = &a->chunks[a->ictx];

		if (!!(chunk->osize % 2)) {

			if (a->data.len == 0)
				return (a->fin) ? FFAVI_RDONE : FFAVI_RMORE;

			if (a->data.ptr[0] == '\0') {
				// skip padding byte
				ffarr_shift(&a->data, 1);
				a->off += 1;
				if (a->ictx >= 1) {
					struct ffavi_chunk *parent = &a->chunks[a->ictx - 1];
					if (parent->size != 0)
						parent->size -= 1;
				}
				continue;
			}
		}

		if (a->ictx >= 1) {
			struct ffavi_chunk *parent = &a->chunks[a->ictx - 1];
			if (parent->size == 0) {
				a->ctx[a->ictx--] = NULL;
			}
		}

		if (a->ictx == 0 && (a->ctx[a->ictx]->flags & F_LAST)) {
			return FFAVI_RDONE;
		}

		// break
	}

	case R_GATHER_CHUNKHDR:
		a->gather_size = sizeof(struct avi_chunk);
		a->state = R_GATHER,  a->nxstate = R_CHUNK_HDR;
		// break

	case R_GATHER:
		r = ffarr_append_until(&a->buf, a->data.ptr, a->data.len, a->gather_size);
		if (r == 0)
			return FFAVI_RMORE;
		else if (r == -1)
			return ERR(a, AVI_ESYS);
		ffarr_shift(&a->data, r);
		a->off += a->gather_size;
		ffstr_set2(&a->gbuf, &a->buf);
		a->buf.len = 0;
		a->state = a->nxstate;
		continue;

	case R_CHUNK_HDR: {
		struct ffavi_chunk *chunk = &a->chunks[a->ictx];
		avi_chunkinfo(a->gbuf.ptr, a->ctx[a->ictx], chunk);

		if (a->ictx != 0) {
			struct ffavi_chunk *parent = &a->chunks[a->ictx - 1];
			if (sizeof(struct avi_chunk) + chunk->size > parent->size)
				return ERR(a, AVI_ELARGE);
			parent->size -= sizeof(struct avi_chunk) + chunk->size;
		}

		if (chunk->id == T_UKN) {
			//unknown chunk
			a->state = R_SKIP;
			continue;
		}

		uint minsize = GET_MINSIZE(chunk->flags);
		if (minsize != 0 && chunk->size < minsize)
			return ERR(a, AVI_ESMALL);

		if ((chunk->flags & F_WHOLE) || minsize != 0) {
			a->gather_size = (chunk->flags & F_WHOLE) ? chunk->size : minsize;
			chunk->size -= a->gather_size;
			a->state = R_GATHER,  a->nxstate = R_CHUNK;
			continue;
		}

		a->state = R_CHUNK;
		continue;
	}

	case R_CHUNK: {
		struct ffavi_chunk *chunk = &a->chunks[a->ictx];

		switch (chunk->id) {
		case T_STRH: {
			const struct strh *strh = (void*)a->gbuf.ptr;
			uint istm = a->istm++;
			if (ffmemcmp(strh->type, STRH_AUDIO, 4)) {
				a->state = R_SKIP_PARENT; //skip strl
				continue;
			}
			a->idx_audio = istm;
			struct ffavi_audio_info *ai = &a->info;
			ai->len = ffint_ltoh32(strh->length);
			ai->scale = ffint_ltoh32(strh->scale);
			ai->rate = ffint_ltoh32(strh->rate);
			// uint delay = ffint_ltoh32(strh->delay);
			break;
		}

		case T_STRF:
			ffstr_free(&a->info.asc);
			if (0 != avi_strf(&a->info, a->gbuf.ptr, a->gbuf.len))
				break;

			if (a->info.asc.len != 0)
				if (NULL == ffstr_dup(&a->info.asc, a->info.asc.ptr, a->info.asc.len))
					return ERR(a, AVI_ESYS);
			break;

		case T_INFO:
			if (!(a->options & FFAVI_O_TAGS)) {
				a->state = R_SKIP;
				continue;
			}
			break;

		case T_MOVI_CHUNK: {
			const struct avi_chunk *ch = (void*)a->gbuf.ptr;
			uint idx;
			if (2 != ffs_toint(ch->id, 2, &idx, FFS_INT32)
				|| idx != a->idx_audio
				|| ffmemcmp(ch->id + 2, MOVI_AUDIO, 2)) {
				a->state = R_SKIP;
				continue;
			}

			if (chunk->size == 0) {
				a->state = R_NEXTCHUNK;
				continue;
			}

			a->gather_size = chunk->size;
			chunk->size = 0;
			a->state = R_GATHER,  a->nxstate = R_DATA;
			continue;
		}

		default:
			if (chunk->id >= _T_TAG) {
				a->tag = chunk->id - _T_TAG;
				ffstr_setz(&a->tagval, a->gbuf.ptr);
				a->state = R_SKIP;
				return FFAVI_RTAG;
			}
		}

		const ffavi_bchunk *ctx = a->ctx[a->ictx];
		if (chunk->flags & F_LIST) {
			int i = avi_chunkfind(ctx->ctx, a->gbuf.ptr);
			if (i != -1) {
				ctx = ctx->ctx;
				chunk->id = ctx[i].flags & 0xff;
				chunk->flags = ctx[i].flags & 0xffffff00;
				a->ctx[++a->ictx] = ctx[i].ctx;
				a->state = R_GATHER_CHUNKHDR;

				switch (chunk->id) {
				case T_MOVI:
					a->movi_off = a->off;
					a->movi_size = chunk->size;
					return FFAVI_RHDR;
				}

				continue;
			}
		} else if (ctx->ctx != NULL) {
			a->ctx[++a->ictx] = ctx->ctx;
		}

		a->state = R_SKIP;
		continue;
	}

	case R_DATA:
		ffstr_set(&a->out, a->gbuf.ptr, a->gbuf.len);
		a->state = R_NEXTCHUNK;
		a->nsamples += a->info.blocksize;
		return FFAVI_RDATA;

	}
	}
}