/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/data/mkv.h>
#include <FF/number.h>
#include <FF/audio/pcm.h>
#include <FFOS/error.h>


/** Parse variable width integer.
1xxxxxxx
01xxxxxx xxxxxxxx
001xxxxx xxxxxxxx*2
...
*/
static int mkv_varint(const void *data, size_t len, uint64 *dst)
{
	uint64 n = 0;
	uint size;
	const byte *d = data;

	if (len == 0)
		return 0;

	if (0 == (size = ffbit_find32((d[0] << 24) & 0xff000000)))
		return -1;

	if (dst == NULL)
		return size;

	if (size > len)
		return -1;

	n = (uint)d[0];
	if (len != (size_t)-1)
		n = n & ~(0x80 >> (size - 1));

	for (uint i = 1;  i != size;  i++) {
		n = (n << 8) | d[i];
	}
	*dst = n;
	return size;
}


#define MKV_ID  "matroska"

enum MKV_TRKTYPE {
	MKV_TRK_AUDIO = 2,
};

// MKV_CODEC_ID:
#define MKV_A_MPEG  "A_MPEG"
#define MKV_A_ALAC  "A_ALAC"
#define MKV_A_AAC  "A_AAC"
#define MKV_A_VORBIS  "A_VORBIS"
#define MKV_A_PCM  "A_PCM/INT/LIT"

enum MKV_ELID {
	T_UKN = -1,
	T_ANY,

	T_SEG,
	T_VER,
	T_DOCTYPE, //MKV_ID
	T_TRACKS,
	T_SCALE,
	T_DUR,
	T_TRKNO,
	T_TRKENT,
	T_TRKTYPE, //enum MKV_TRKTYPE
	T_CODEC_ID, //MKV_CODEC_ID
	T_CODEC_PRIV,
	T_A_RATE,
	T_A_CHANNELS,
	T_A_BITS,

	T_TAG,
	T_TAG_NAME,
	T_TAG_VAL,
	T_TAG_BVAL,

	T_TIME,
	T_BLOCK,
	T_SBLOCK,
};

enum MKV_FLAGS {
	F_LAST = 0x0100,
	F_INT = 0x0200,
	F_FLT = 0x0400,
	F_WHOLE = 0x0800,
	F_REQ = 0x1000,
	F_MULTI = 0x2000,
};

#define DEF(n)  (0)

typedef struct mkv_el mkv_el;

typedef struct mkv_bel mkv_bel;
struct mkv_bel {
	uint id;
	uint flags;
	const mkv_bel *ctx;
};

/** Search element in the context.
Return -1 if not found. */
static int mkv_el_find(const mkv_bel *ctx, uint id)
{
	for (uint i = 0;  ;  i++) {

		if (id == ctx[i].id)
			return i;

		if (ctx[i].flags & F_LAST)
			break;
	}

	return -1;
}

static int mkv_el_info(mkv_el *el, const mkv_bel *ctx, const void *data, uint level)
{
	uint64 id;
	int r = mkv_varint(data, -1, &id);

	el->id = T_UKN;
	if (id > 0x1fffffff)
		return -1;

	r = mkv_varint(data + r, -2, &el->size);

	r = mkv_el_find(ctx, (uint)id);
	if (r >= 0) {
		el->id = ctx[r].flags & 0xff;
		el->flags = ctx[r].flags & 0xffffff00;
		el->ctx = ctx[r].ctx;
	}

	FFDBG_PRINTLN(10, "%*c%xu  size:%u  %s"
		, level, ' ', (uint)id, el->size, (r < 0) ? "unsupported" : "");
	return 0;
}

/* Supported elements:

EBMLHead (0x1A45DFA3)
 EBMLVersion (0x4286)
 EBMLDoocType (0x4282)
Segment (0x18538067)
 Info (0x1549a966)
  TimecodeScale (0x2ad7b1)
  Duration (0x4489)
 Tracks (0x1654ae6b)
  TrackEntry (0xae)
   TrackNumber (0xd7)
   TrackType (0x83)
   CodecID (0x86)
   CodecPrivate (0x63a2)
   Audio (0xe1)
    SamplingFrequency (0xb5)
    Channels (0x9f)
    BitDepth (0x6264)
 Tags (0x1254c367)
  Tag (0x7373)
   SimpleTag (0x67c8)
    TagName (0x45a3)
    TagString (0x4487)
    TagBinary (0x4485)
 Cluster (0x1f43b675)
  Timecode (0xe7)
  Block (0xa1)
  SimpleBlock (0xa3)
*/

static const mkv_bel mkv_ctx_head[];
static const mkv_bel mkv_ctx_segment[];
static const mkv_bel mkv_ctx_info[];
static const mkv_bel mkv_ctx_tracks[];
static const mkv_bel mkv_ctx_trackentry[];
static const mkv_bel mkv_ctx_trackentry_audio[];
static const mkv_bel mkv_ctx_tags[];
static const mkv_bel mkv_ctx_tag[];
static const mkv_bel mkv_ctx_tag_simple[];
static const mkv_bel mkv_ctx_cluster[];

static const mkv_bel mkv_ctx_global[] = {
	{ 0x1A45DFA3, F_REQ, mkv_ctx_head },
	{ 0x18538067, T_SEG | F_REQ | F_LAST, mkv_ctx_segment },
};

static const mkv_bel mkv_ctx_head[] = {
	{ 0x4286, T_VER | F_INT | F_REQ, NULL },
	{ 0x4282, T_DOCTYPE | F_WHOLE | F_REQ | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_segment[] = {
	{ 0x1549a966, 0, mkv_ctx_info },
	{ 0x1654ae6b, T_TRACKS | F_REQ, mkv_ctx_tracks },
	{ 0x1254c367, 0, mkv_ctx_tags },
	{ 0x1f43b675, F_MULTI | F_LAST, mkv_ctx_cluster },
};

static const mkv_bel mkv_ctx_info[] = {
	{ 0x2ad7b1, T_SCALE | F_INT | DEF(1000000), NULL },
	{ 0x4489, T_DUR | F_FLT | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_tracks[] = {
	{ 0xae, T_TRKENT | F_LAST, mkv_ctx_trackentry },
};

static const mkv_bel mkv_ctx_trackentry[] = {
	{ 0xd7, T_TRKNO | F_INT, NULL },
	{ 0x83, T_TRKTYPE | F_INT, NULL },
	{ 0x86, T_CODEC_ID | F_WHOLE, NULL },
	{ 0x63a2, T_CODEC_PRIV | F_WHOLE, NULL },
	{ 0xe1, F_LAST, mkv_ctx_trackentry_audio },
};

static const mkv_bel mkv_ctx_trackentry_audio[] = {
	{ 0xb5, T_A_RATE | F_FLT, NULL },
	{ 0x9f, T_A_CHANNELS | F_INT, NULL },
	{ 0x6264, T_A_BITS | F_INT | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_tags[] = {
	{ 0x7373, T_TAG | F_LAST, mkv_ctx_tag },
};

static const mkv_bel mkv_ctx_tag[] = {
	{ 0x67c8, F_LAST, mkv_ctx_tag_simple },
};

static const mkv_bel mkv_ctx_tag_simple[] = {
	{ 0x45a3, T_TAG_NAME | F_WHOLE, NULL },
	{ 0x4487, T_TAG_VAL | F_WHOLE, NULL },
	{ 0x4485, T_TAG_BVAL | F_WHOLE | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_cluster[] = {
	{ 0xe7, T_TIME | F_INT, NULL },
	{ 0xa1, T_BLOCK | F_WHOLE, NULL },
	{ 0xa3, T_SBLOCK | F_WHOLE | F_LAST, NULL },
};


enum MKV_E {
	MKV_EID = 1,
	MKV_ESIZE,
	MKV_EVER,
	MKV_EDOCTYPE,
	MKV_EINTVAL,
	MKV_EFLTVAL,
	MKV_ELARGE,
	MKV_ESMALL,
	MKV_ELACING,
	MKV_EVORBISHDR,

	MKV_ESYS,
};

static const char* const mkv_errstr[] = {
	"",
	"bad ID",
	"bad size",
	"unsupported EBML version",
	"unsupported EBML doctype",
	"bad integer",
	"bad float number",
	"too large element",
	"too small element",
	"lacing isn't supported",
	"bad Vorbis codec private data",
};

const char* ffmkv_errstr(ffmkv *m)
{
	if (m->err == MKV_ESYS)
		return fferr_strp(fferr_last());
	return mkv_errstr[m->err];
}

#define ERR(m, e) \
	(m)->err = (e), FFMKV_RERR

enum {
	R_ELID1, R_ELSIZE1, R_ELSIZE, R_EL,
	R_NEXTCHUNK, R_SKIP_PARENT, R_SKIP, R_GATHER,
};

int ffmkv_open(ffmkv *m)
{
	m->info.type = -1;
	m->info.num = -1;
	m->ctxs[m->ictx] = mkv_ctx_global;
	m->state = R_ELID1;
	return 0;
}

void ffmkv_close(ffmkv *m)
{
	ffarr_free(&m->buf);
	ffstr_free(&m->info.asc);
	ffstr_free(&m->codec_data);
}

/*
. gather data for element id
. gather data for element size
. process element:
 . skip if unknown
 . or gather its data and convert (string -> int/float) if needed
*/
int ffmkv_read(ffmkv *m)
{
	int r;

	for (;;) {
	switch (m->state) {

	case R_SKIP_PARENT: {
		mkv_el *el = &m->els[m->ictx];
		m->ctxs[m->ictx--] = NULL;
		mkv_el *parent = &m->els[m->ictx];
		parent->size += el->size;
		m->state = R_SKIP;
		// break
	}

	case R_SKIP: {
		mkv_el *el = &m->els[m->ictx];
		r = ffmin(el->size, m->data.len);
		ffarr_shift(&m->data, r);
		el->size -= r;
		m->off += r;
		if (el->size != 0)
			return FFMKV_RMORE;

		m->state = R_NEXTCHUNK;
		// break
	}

	case R_NEXTCHUNK: {
		// mkv_el *el = &m->els[m->ictx];

		if (m->ictx >= 1) {
			mkv_el *parent = &m->els[m->ictx - 1];

			if (parent->size == 0) {
				m->ctxs[m->ictx--] = NULL;

				switch (parent->id) {
				case T_SEG:
					return FFMKV_RDONE;

				case T_TRACKS:
					if (m->info.scale == 0)
						m->info.scale = 1000000;
					m->info.total_samples = ffpcm_samples((double)m->info.dur * m->info.scale / 1000000, m->info.sample_rate);
					return FFMKV_RHDR;

				case T_TRKENT:
					if (m->info.type == MKV_TRK_AUDIO) {
						m->audio_trkno = m->info.num;
						ffstr_acq(&m->info.asc, &m->codec_data);
					}
					break;

				case T_TAG:
					return FFMKV_RTAG;
				}

				continue;
			}
		}

		m->state = R_ELID1;
		continue;
	}

	case R_GATHER:
		r = ffarr_append_until(&m->buf, m->data.ptr, m->data.len, m->gsize);
		if (r == 0)
			return FFMKV_RMORE;
		else if (r == -1)
			return ERR(m, MKV_ESYS);
		ffarr_shift(&m->data, r);
		m->off += m->gsize;
		ffstr_set2(&m->gbuf, &m->buf);
		m->buf.len = 0;
		m->state = m->gstate;
		continue;

	case R_ELID1:
		if (m->data.len == 0)
			return FFMKV_RMORE;
		r = mkv_varint(m->data.ptr, 1, NULL);
		if (r == -1)
			return ERR(m, MKV_EID);
		m->gsize = r + 1;
		m->state = R_GATHER,  m->gstate = R_ELSIZE1;
		continue;

	case R_ELSIZE1:
		r = mkv_varint(m->gbuf.ptr, 1, NULL);
		m->el_hdrsize = r;
		r = mkv_varint(m->gbuf.ptr + r, 1, NULL);
		if (r == -1)
			return ERR(m, MKV_ESIZE);
		m->el_hdrsize += r;
		if (r != 1) {
			m->buf.len = m->gbuf.len;
			m->gsize = m->el_hdrsize;
			m->state = R_GATHER,  m->gstate = R_ELSIZE;
			continue;
		}
		m->state = R_ELSIZE;
		// break

	case R_ELSIZE: {
		mkv_el *el = &m->els[m->ictx];
		const mkv_bel *ctx = m->ctxs[m->ictx];
		mkv_el_info(el, ctx, m->gbuf.ptr, m->ictx);

		if (m->ictx != 0) {
			mkv_el *parent = &m->els[m->ictx - 1];
			if (m->el_hdrsize + el->size > parent->size)
				return ERR(m, MKV_ELARGE);
			parent->size -= m->el_hdrsize + el->size;
		}

		if (el->id == T_UKN) {
			m->state = R_SKIP;
			continue;
		}

		if (el->flags & (F_WHOLE | F_INT | F_FLT)) {
			m->gsize = el->size;
			el->size = 0;
			m->state = R_GATHER,  m->gstate = R_EL;
			continue;
		}

		m->gbuf.len = 0;
		m->state = R_EL;
		// continue;
	}

	case R_EL: {
		int val4;
		uint64 val;
		double fval;
		mkv_el *el = &m->els[m->ictx];

		if (el->flags & F_INT) {
			if (m->gbuf.len == 1)
				val = *(byte*)m->gbuf.ptr;
			else if (m->gbuf.len == 2)
				val = ffint_ntoh16(m->gbuf.ptr);
			else if (m->gbuf.len == 3)
				val = ffint_ntoh24(m->gbuf.ptr);
			else if (m->gbuf.len == 4)
				val = ffint_ntoh32(m->gbuf.ptr);
			else if (m->gbuf.len == 8)
				val = ffint_ntoh64(m->gbuf.ptr);
			else
				return ERR(m, MKV_EINTVAL);
			val4 = val;
		}

		if (el->flags & F_FLT) {
			union {
				uint u;
				uint64 u8;
				float f;
				double d;
			} u;
			if (m->gbuf.len == 4) {
				u.u = ffint_ntoh32(m->gbuf.ptr);
				fval = u.f;
			} else if (m->gbuf.len == 8) {
				u.u8 = ffint_ntoh64(m->gbuf.ptr);
				fval = u.d;
			} else
				return ERR(m, MKV_EFLTVAL);
		}

		switch (el->id) {
		case T_VER:
			if (val4 != 1)
				return ERR(m, MKV_EVER);
			break;

		case T_DOCTYPE:
			if (!ffstr_eqcz(&m->gbuf, MKV_ID))
				return ERR(m, MKV_EDOCTYPE);
			break;


		case T_SCALE:
			m->info.scale = val4;
			break;

		case T_DUR:
			m->info.dur = fval;
			break;

		case T_TRKNO:
			m->info.num = val4;
			break;

		case T_TRKTYPE:
			m->info.type = val4;
			break;

		case T_CODEC_ID:
			if (ffstr_match(&m->gbuf, MKV_A_AAC, FFSLEN(MKV_A_AAC)))
				m->info.format = FFMKV_AUDIO_AAC;
			else if (ffstr_eqcz(&m->gbuf, MKV_A_ALAC))
				m->info.format = FFMKV_AUDIO_ALAC;
			else if (ffstr_match(&m->gbuf, MKV_A_MPEG, FFSLEN(MKV_A_MPEG)))
				m->info.format = FFMKV_AUDIO_MPEG;
			else if (ffstr_eqcz(&m->gbuf, MKV_A_VORBIS))
				m->info.format = FFMKV_AUDIO_VORBIS;
			break;

		case T_CODEC_PRIV:
			ffstr_free(&m->codec_data);
			if (NULL == ffstr_dup(&m->codec_data, m->gbuf.ptr, m->gbuf.len))
				return ERR(m, MKV_ESYS);
			break;

		case T_A_RATE:
			m->info.sample_rate = (uint)fval;
			break;

		case T_A_CHANNELS:
			m->info.channels = val4;
			break;

		case T_A_BITS:
			m->info.bits = val4;
			break;


		case T_TAG_NAME:
			m->tag = -1;
			break;

		case T_TAG_VAL:
		case T_TAG_BVAL:
			ffstr_set2(&m->tagval, &m->gbuf);
			break;


		case T_TIME:
			m->nsamples = val4;
			break;

		case T_BLOCK:
		case T_SBLOCK: {
			struct {
				uint64 trackno;
				uint time;
				uint flags;
			} sblk;

			if (-1 == (r = mkv_varint(m->gbuf.ptr, m->gbuf.len, &sblk.trackno)))
				return ERR(m, MKV_EINTVAL);
			ffarr_shift(&m->gbuf, r);

			if (sblk.trackno != (uint)m->audio_trkno)
				break;

			if (m->gbuf.len < 3)
				return ERR(m, MKV_ESMALL);
			sblk.time = ffint_ntoh16(m->gbuf.ptr);
			sblk.flags = m->gbuf.ptr[2];
			ffarr_shift(&m->gbuf, 3);

			if (sblk.flags & 0x60)
				return ERR(m, MKV_ELACING);

			m->state = R_SKIP;
			ffstr_set2(&m->out, &m->gbuf);
			return FFMKV_RDATA;
		}
		}

		if (el->ctx != NULL) {
			m->ctxs[++m->ictx] = el->ctx;
			m->state = R_NEXTCHUNK;
			continue;
		}

		m->state = R_SKIP;
		continue;
	}

	}
	}
}

/** Get packet length.
Return bytes processed;  0 on error. */
static int ogg_pktlen(const char *data, size_t len, uint *pkt_size)
{
	uint i = 0, seglen, pktlen = 0;

	do {
		if (i == len)
			return 0;
		seglen = (byte)data[i++];
		pktlen += seglen;
	} while (seglen == 255);

	*pkt_size = pktlen;
	return i;
}


/* PKTS_NUM PKT1_LEN PKT2_LEN  PKT1 PKT2 PKT3 */
int ffmkv_vorbis_hdr(ffmkv_vorbis *m)
{
	switch (m->state) {
	case 0: {
		uint n, dataoff, pkt1_len, pkt2_len;
		ffstr s = m->data;

		if (m->data.len < 3)
			return -MKV_EVORBISHDR;
		n = (byte)*m->data.ptr;
		if (n != 2)
			return -MKV_EVORBISHDR;
		ffstr_shift(&s, 1);

		if (0 == (n = ogg_pktlen(s.ptr, s.len, &pkt1_len)))
			return -MKV_EVORBISHDR;
		ffstr_shift(&s, n);

		if (0 == (n = ogg_pktlen(s.ptr, s.len, &pkt2_len)))
			return -MKV_EVORBISHDR;
		ffstr_shift(&s, n);

		dataoff = s.ptr - m->data.ptr;
		m->pkt2_off = dataoff + pkt1_len;
		m->pkt3_off = dataoff + pkt1_len + pkt2_len;

		ffstr_set(&m->out, m->data.ptr + dataoff, pkt1_len);
		break;
	}

	case 1:
		ffstr_set(&m->out, m->data.ptr + m->pkt2_off, m->data.len - m->pkt2_off);
		break;

	case 2:
		ffstr_set(&m->out, m->data.ptr + m->pkt3_off, m->data.len - m->pkt3_off);
		break;

	case 3:
		return 1;
	}

	m->state++;
	return 0;
}
