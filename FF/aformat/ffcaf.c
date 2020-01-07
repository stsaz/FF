/**
Copyright (c) 2020 Simon Zolin
*/

#include <FF/aformat/caf.h>
#include <FF/mformat/mp4-fmt.h>


#define CHUNK_MAXSIZE  (2*1024*1024) // max. meta chunk size
#define ACHUNK_MAXSIZE  (1*1024*1024) // max. audio chunk size


struct caf_hdr {
	char caff[4]; // "caff"
	byte ver[2]; // =1
	byte flags[2]; // =0
};

static const byte hdrv1[] = "caff\x00\x01\x00\x00";

struct caf_chunk {
	char type[4];
	byte size[8]; // may be -1 for Audio Data chunk
};

struct caf_info {
	// "info"
	byte entries[4];
	// ("key" 0x00 "value" 0x00)...
};

static const char fmts[][4] = {
	"aac ",
	"alac",
};

struct caf_desc {
	// "desc"
	byte srate[8]; // float64
	char fmt[4];
	byte flags[4];
	byte pkt_size[4];
	byte pkt_frames[4];
	byte channels[4];
	byte bits[4];
};

struct caf_pakt {
	// "pakt"
	byte npackets[8];
	byte nframes[8];
	byte latency_frames[4];
	byte remainder_frames[4];
	// byte pkt_size[2]...
};

enum R {
	R_GATHER, R_CHUNK,
	R_HDR, R_DESC, R_INFO, R_TAG, R_KUKI, R_PAKT,
	R_DATA, R_DATA_NEXT, R_DATA_CHUNK,
};

enum F {
	CAF_FWHOLE = 0x0100,
	CAF_FEXACT = 0x0200,
};

struct chunk {
	char type[4];
	ushort flags; // enum F, enum R
	byte minsize;
};

#define CHUNK_TYPE(f)  ((byte)(f))

static const struct chunk chunks[] = {
	{ "desc", R_DESC | CAF_FWHOLE | CAF_FEXACT, sizeof(struct caf_desc) },
	{ "info", R_INFO | CAF_FWHOLE, sizeof(struct caf_info) },
	{ "kuki", R_KUKI | CAF_FWHOLE, 0 },
	{ "pakt", R_PAKT | CAF_FWHOLE, sizeof(struct caf_pakt) },
	{ "data", R_DATA, 0 },
};

static void gather(ffcaf *c, uint nxstate, size_t len)
{
	c->state = R_GATHER,  c->nxstate = nxstate;
	c->gathlen = len;
	c->buf.len = 0;
}

#define ERR(c, e) \
	(c)->err = e,  FFCAF_ERR

enum E {
	E_OK,
	E_SYS,
	E_HDR,
	E_CHUNKSIZE,
	E_CHUNKSMALL,
	E_CHUNKLARGE,
	E_ORDER,
	E_DATA,
};

static const char* const errstr[] = {
	"bad header", // E_HDR
	"bad chunk size", // E_CHUNKSIZE
	"chunk too small", // E_CHUNKSMALL
	"chunk too large", // E_CHUNKLARGE
	"bad chunks order", // E_ORDER
	"bad audio chunk", // E_DATA
};

const char* ffcaf_errstr(ffcaf *c)
{
	if (c->err == E_SYS)
		return fferr_strp(fferr_last());
	uint e = c->err - 2;
	if (e > FFCNT(errstr))
		return "";
	return errstr[e];
}

static int chunk_find(ffcaf *c, const struct caf_chunk *cc, const struct chunk **pchunk)
{
	uint64 sz = ffint_ntoh64(cc->size);
	FFDBG_PRINTLN(5, "type:%*s  size:%D  off:%U"
		, (size_t)4, cc->type, sz, c->inoff);

	for (uint i = 0;  i != FFCNT(chunks);  i++) {
		const struct chunk *ch = &chunks[i];

		if (!!memcmp(cc->type, ch->type, 4))
			continue;

		if (sz < ch->minsize)
			return E_CHUNKSMALL;

		if ((ch->flags & CAF_FWHOLE) && sz > CHUNK_MAXSIZE)
			return E_CHUNKLARGE;

		if ((ch->flags & CAF_FEXACT) && sz != ch->minsize)
			return E_CHUNKSIZE;

		*pchunk = ch;
		return 0;
	}
	return 0;
}

int ffcaf_open(ffcaf *c)
{
	ffarr_alloc(&c->buf, 1024);
	gather(c, R_HDR, sizeof(struct caf_hdr));
	return 0;
}

void ffcaf_close(ffcaf *c)
{
	ffarr_free(&c->buf);
	ffstr_free(&c->asc);
	ffstr_free(&c->pakt);
}

/** Read integer.

Format:
0xxxxxxx
1xxxxxxx 0xxxxxxx

Return N of bytes read;  0 if done;  -1 on error.
*/
static int caf_varint(const void *data, size_t len, uint *dst)
{
	const byte *d = data;
	if (len == 0)
		return 0;

	uint d0 = d[0];
	if (d0 & 0x80) {
		if (len < 2)
			return -1;
		if (d[1] & 0x80)
			return -1;
		*dst = (d0 & 0x7f) << 7;
		*dst |= d[1];
		return 2;
	}

	*dst = d0;
	return 1;
}

int ffcaf_read(ffcaf *c)
{
	int r;

	for (;;) {
	switch (c->state) {

	case R_GATHER:
		r = ffarr_append_until(&c->buf, c->in.ptr, c->in.len, c->gathlen);
		if (r == 0) {
			c->in.len = 0;
			return FFCAF_MORE;
		} else if (r == -1)
			return ERR(c, E_SYS);

		ffstr_set2(&c->chunk, &c->buf);
		ffarr_shift(&c->in, r);
		c->inoff += r;
		c->state = c->nxstate;
		break;

	case R_HDR:
		if (!!ffmemcmp(c->chunk.ptr, hdrv1, sizeof(struct caf_hdr)))
			return ERR(c, E_HDR);
		gather(c, R_CHUNK, sizeof(struct caf_chunk));
		break;

	case R_CHUNK: {
		const struct caf_chunk *cc = (void*)c->chunk.ptr;
		c->chunk_size = ffint_ntoh64(cc->size);
		const struct chunk *ch = NULL;
		r = chunk_find(c, cc, &ch);
		if (r != 0)
			return ERR(c, r);

		if (ch != NULL) {
			if (ch->flags & CAF_FWHOLE)
				gather(c, CHUNK_TYPE(ch->flags), c->chunk_size);
			else
				c->state = CHUNK_TYPE(ch->flags);

		} else {
			gather(c, R_CHUNK, sizeof(struct caf_chunk));
			c->inoff += c->chunk_size;
			return FFCAF_SEEK;
		}
		break;
	}

	case R_DESC: {
		const struct caf_desc *d = (void*)c->chunk.ptr;
		int64 i = ffint_ntoh64(d->srate);
		c->info.pcm.sample_rate = *(double*)&i;
		c->info.pcm.channels = ffint_ntoh32(d->channels);
		c->info.pcm.format = ffint_ntoh32(d->bits);
		c->info.packet_bytes = ffint_ntoh32(d->pkt_size);
		c->info.packet_frames = ffint_ntoh32(d->pkt_frames);
		r = ffcharr_findsorted(fmts, FFCNT(fmts), sizeof(fmts[0]), d->fmt, 4);
		if (r != -1)
			c->info.format = r + 1;
		gather(c, R_CHUNK, sizeof(struct caf_chunk));
		break;
	}

	case R_INFO:
		// const struct caf_info *i = (void*)c->chunk.ptr;
		ffstr_shift(&c->chunk, sizeof(struct caf_info));
		c->state = R_TAG;
		break;

	case R_TAG: {
		if (c->chunk.len == 0) {
			gather(c, R_CHUNK, sizeof(struct caf_chunk));
			break;
		}

		ffs_split2by(c->chunk.ptr, c->chunk.len, '\0', &c->tagname, &c->chunk);
		ffs_split2by(c->chunk.ptr, c->chunk.len, '\0', &c->tagval, &c->chunk);
		return FFCAF_TAG;
	}

	case R_KUKI: {
		struct mp4_esds esds;
		mp4_esds(c->chunk.ptr, c->chunk.len, &esds);
		c->info.bitrate = esds.avg_brate;
		ffstr_free(&c->asc);
		ffstr_copy(&c->asc, esds.conf, esds.conflen);
		gather(c, R_CHUNK, sizeof(struct caf_chunk));
		break;
	}

	case R_PAKT: {
		const struct caf_pakt *p = (void*)c->chunk.ptr;
		c->info.total_packets = ffint_ntoh64(p->npackets);
		c->info.total_frames = ffint_ntoh64(p->nframes);

		ffstr_shift(&c->chunk, sizeof(struct caf_pakt));
		ffstr_free(&c->pakt);
		ffstr_alcopystr(&c->pakt, &c->chunk);

		if (c->info.pcm.sample_rate == 0)
			return ERR(c, E_ORDER);

		gather(c, R_CHUNK, sizeof(struct caf_chunk));
		return FFCAF_HDR;
	}

	case R_DATA:
		ffstr_free(&c->asc);
		gather(c, R_DATA_NEXT, 4); // skip "edit count" field
		break;

	case R_DATA_NEXT: {
		if (c->chunk_size == 0)
			return FFCAF_DONE;
		if (c->fin && c->in.len == 0 && (int64)c->chunk_size == -1)
			return FFCAF_DONE;

		uint sz = c->info.packet_bytes;
		if (sz == 0) {
			r = caf_varint(c->pakt.ptr + c->pakt_off, c->pakt.len - c->pakt_off, &sz);
			if (r == 0)
				return FFCAF_DONE;
			if (r < 0)
				return ERR(c, E_DATA);
			c->pakt_off += r;
		}

		FFDBG_PRINTLN(10, "pkt#%U  size:%u"
			, c->ipkt, sz);

		if (sz > c->chunk_size)
			return ERR(c, E_DATA);
		if (sz > ACHUNK_MAXSIZE)
			return ERR(c, E_CHUNKLARGE);

		c->ipkt++;
		if ((int64)c->chunk_size != -1)
			c->chunk_size -= sz;
		gather(c, R_DATA_CHUNK, sz);
		break;
	}

	case R_DATA_CHUNK:
		c->out = c->chunk;
		c->iframe += c->info.packet_frames;
		c->state = R_DATA_NEXT;
		return FFCAF_DATA;

	}
	}
}
