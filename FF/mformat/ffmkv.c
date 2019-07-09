/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/mformat/mkv.h>
#include <FF/number.h>
#include <FF/audio/pcm.h>
#include <FFOS/error.h>


static int mkv_block(ffmkv *m, ffstr *data);
static int mkv_lacing(ffstr *data, ffarr4 *lacing, uint lace);
static int mkv_lacing_ebml(ffstr *data, uint *lace, uint n);
static int mkv_lacing_xiph(ffstr *data, uint *lace, uint n);
static int mkv_lacing_fixed(ffstr *data, uint *lace, uint n);
static int ogg_pktlen(const char *data, size_t len, uint *pkt_size);


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

/** Parse variable width integer and shift input data. */
static int mkv_varint_shift(ffstr *data, uint64 *dst)
{
	int r;
	if (-1 == (r = mkv_varint(data->ptr, data->len, dst)))
		return -1;
	ffarr_shift(data, r);
	return r;
}

/** Get 1-byte integer and shift input data. */
static int mkv_getbyte_shift(ffstr *data, uint *dst)
{
	if (data->len == 0)
		return -1;
	*dst = data->ptr[0];
	ffarr_shift(data, 1);
	return 1;
}

static int mkv_int_ntoh(uint64 *dst, const char *d, size_t len)
{
	switch (len) {
	case 1:
		*dst = *(byte*)d; break;
	case 2:
		*dst = ffint_ntoh16(d); break;
	case 3:
		*dst = ffint_ntoh24(d); break;
	case 4:
		*dst = ffint_ntoh32(d); break;
	case 8:
		*dst = ffint_ntoh64(d); break;
	default:
		return -1;
	}
	return 0;
}

static int mkv_flt_ntoh(double *dst, const char *d, size_t len)
{
	union {
		uint u;
		uint64 u8;
		float f;
		double d;
	} u;

	switch (len) {
	case 4:
		u.u = ffint_ntoh32(d);
		*dst = u.f;
		break;
	case 8:
		u.u8 = ffint_ntoh64(d);
		*dst = u.d;
		break;
	default:
		return -1;
	}
	return 0;
}

#define MKV_ID  "matroska"

enum MKV_TRKTYPE {
	MKV_TRK_AUDIO = 2,
};

// MKV_CODEC_ID:
static const char* const codecstr[] = {
	"A_AAC",
	"A_ALAC",
	"A_MPEG",
	"A_VORBIS",
	"A_AC3",
	"A_PCM/INT/LIT",

	"V_MPEG4/ISO/AVC",
	"V_MPEGH/ISO/HEVC",

	"S_TEXT/UTF8",
	"S_TEXT/ASS",
};

/** Translate codec name to ID. */
static int mkv_codec(const ffstr *name)
{
	int r;
	r = ffs_findarrz(codecstr, FFCNT(codecstr), name->ptr, name->len);
	if (r >= 0)
		return r + 1;

	if (ffstr_match(name, "A_AAC", FFSLEN("A_AAC")))
		r = FFMKV_AUDIO_AAC;
	else if (ffstr_match(name, "A_MPEG", FFSLEN("A_MPEG")))
		r = FFMKV_AUDIO_MPEG;

	return r;
}

enum {
	MKV_MASK_ELID = 0x000000ff,
};

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

	T_V_WIDTH,
	T_V_HEIGHT,

	T_A_RATE,
	T_A_CHANNELS,
	T_A_BITS,

	T_TAG,
	T_TAG_NAME,
	T_TAG_VAL,
	T_TAG_BVAL,

	T_CLUST,
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
	F_INT8 = 0x4000,
};

#define DEF(n)  (0)

/** Priority, strict order of elements.
0: unspecified
1: highest priority
>1: require previous element with lower number */
#define PRIOTY(n)  ((n) << 24)
#define GET_PRIO(flags)  ((flags & 0xff000000) >> 24)

typedef struct mkv_el mkv_el;

typedef struct mkv_bel mkv_bel;
struct mkv_bel {
	uint id;
	uint flags; //PRIO FLAGS ID
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

static int mkv_el_info(mkv_el *el, const mkv_bel *ctx, const void *data, uint level, uint64 off)
{
	uint64 id;
	int r = mkv_varint(data, -1, &id);

	el->id = T_UKN;
	if (r <= 0 || id > 0x1fffffff)
		return -1;

	r = mkv_varint((char*)data + r, -2, &el->size);
	if (r <= 0)
		return -1;

	r = mkv_el_find(ctx, (uint)id);
	if (r >= 0) {
		el->id = ctx[r].flags & MKV_MASK_ELID;
		el->flags = ctx[r].flags & ~MKV_MASK_ELID;
		el->ctx = ctx[r].ctx;
	}

	FFDBG_PRINTLN(10, "%*c%xu  size:%u  offset:%xU  %s"
		, (size_t)level, ' ', (uint)id, el->size, off, (r < 0) ? "unsupported" : "");
	return r;
}

/* Supported elements:

EBMLHead (0x1a45dfa3)
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
   Video (0xe0)
    PixelWidth (0xb0)
    PixelHeight (0xba)
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
static const mkv_bel mkv_ctx_trackentry_video[];
static const mkv_bel mkv_ctx_trackentry_audio[];
static const mkv_bel mkv_ctx_tags[];
static const mkv_bel mkv_ctx_tag[];
static const mkv_bel mkv_ctx_tag_simple[];
static const mkv_bel mkv_ctx_cluster[];

static const mkv_bel mkv_ctx_global[] = {
	{ 0x1A45DFA3, F_REQ | PRIOTY(1), mkv_ctx_head },
	{ 0x18538067, T_SEG | F_REQ | PRIOTY(2) | F_LAST, mkv_ctx_segment },
};

static const mkv_bel mkv_ctx_head[] = {
	{ 0x4286, T_VER | F_INT | F_REQ, NULL },
	{ 0x4282, T_DOCTYPE | F_WHOLE | F_REQ | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_segment[] = {
	{ 0x1549a966, 0 | PRIOTY(1), mkv_ctx_info },
	{ 0x1654ae6b, T_TRACKS | F_REQ | PRIOTY(2), mkv_ctx_tracks },
	{ 0x1254c367, 0, mkv_ctx_tags },
	{ 0x1f43b675, T_CLUST | F_MULTI | PRIOTY(3) | F_LAST, mkv_ctx_cluster },
};

static const mkv_bel mkv_ctx_info[] = {
	{ 0x2ad7b1, T_SCALE | F_INT | DEF(1000000), NULL },
	{ 0x4489, T_DUR | F_FLT | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_tracks[] = {
	{ 0xae, T_TRKENT | F_MULTI | F_LAST, mkv_ctx_trackentry },
};
static const mkv_bel mkv_ctx_trackentry[] = {
	{ 0xd7, T_TRKNO | F_INT, NULL },
	{ 0x83, T_TRKTYPE | F_INT, NULL },
	{ 0x86, T_CODEC_ID | F_WHOLE, NULL },
	{ 0x63a2, T_CODEC_PRIV | F_WHOLE, NULL },
	{ 0xe0, T_TAG, mkv_ctx_trackentry_video },
	{ 0xe1, F_LAST, mkv_ctx_trackentry_audio },
};
static const mkv_bel mkv_ctx_trackentry_video[] = {
	{ 0xb0, T_V_WIDTH | F_INT, NULL },
	{ 0xba, T_V_HEIGHT | F_INT | F_LAST, NULL },
};
static const mkv_bel mkv_ctx_trackentry_audio[] = {
	{ 0xb5, T_A_RATE | F_FLT, NULL },
	{ 0x9f, T_A_CHANNELS | F_INT, NULL },
	{ 0x6264, T_A_BITS | F_INT | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_tags[] = {
	{ 0x7373, T_TAG | F_MULTI | F_LAST, mkv_ctx_tag },
};
static const mkv_bel mkv_ctx_tag[] = {
	{ 0x67c8, F_MULTI | F_LAST, mkv_ctx_tag_simple },
};
static const mkv_bel mkv_ctx_tag_simple[] = {
	{ 0x45a3, T_TAG_NAME | F_WHOLE, NULL },
	{ 0x4487, T_TAG_VAL | F_WHOLE, NULL },
	{ 0x4485, T_TAG_BVAL | F_WHOLE | F_LAST, NULL },
};

static const mkv_bel mkv_ctx_cluster[] = {
	{ 0xe7, T_TIME | F_INT | F_REQ | PRIOTY(1), NULL },
	{ 0xa1, T_BLOCK | F_MULTI | F_WHOLE | PRIOTY(2), NULL },
	{ 0xa3, T_SBLOCK | F_MULTI | F_WHOLE | PRIOTY(2) | F_LAST, NULL },
};


enum MKV_E {
	MKV_EID = 1,
	MKV_ESIZE,
	MKV_EVER,
	MKV_EDOCTYPE,
	MKV_EINTVAL,
	MKV_EFLTVAL,
	MKV_EDUPEL,
	MKV_ELARGE,
	MKV_ESMALL,
	MKV_EORDER,
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
	"duplicate element",
	"too large element",
	"too small element",
	"unsupported order of elements",
	"lacing: bad frame size",
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

/** Read lacing data.
byte num_frames
byte Xiph[] | byte EBML[]
*/
static int mkv_lacing(ffstr *data, ffarr4 *lacing, uint lace)
{
	int r;
	uint nframes;
	if (0 > mkv_getbyte_shift(data, &nframes))
		return MKV_EINTVAL;

	lacing->len = 0;
	if (NULL == ffarr_reallocT((ffarr*)lacing, nframes, uint))
		return MKV_ESYS;

	switch (lace) {
	case 0x02:
		if (0 != (r = mkv_lacing_xiph(data, (void*)lacing->ptr, nframes)))
			return r;
		break;

	case 0x04:
		if (0 != (r = mkv_lacing_fixed(data, (void*)lacing->ptr, nframes)))
			return r;
		break;

	case 0x06:
		if (0 != (r = mkv_lacing_ebml(data, (void*)lacing->ptr, nframes)))
			return r;
		break;
	}

	lacing->len = nframes;
	lacing->off = 0;
	return 0;
}

/** Read EBML lacing.
varint size_first
varint size_delta[]
*/
static int mkv_lacing_ebml(ffstr *data, uint *lace, uint n)
{
	int r;
	int64 val, prev;

	if (0 > (r = mkv_varint_shift(data, (uint64*)&prev))
		|| prev > (uint)-1)
		return MKV_ELACING;
	*lace++ = prev;

	FFDBG_PRINTLN(10, "EBML lacing: [%u] 0x%xU %*xb"
		, n, prev, (size_t)n - 1, data->ptr);

	for (uint i = 1;  i != n;  i++) {
		if (0 > (r = mkv_varint_shift(data, (uint64*)&val)))
			return MKV_ELACING;

		switch (r) {
		case 1:
			//.sxx xxxx  0..0x3e: negative;  0x40..0x7f: positive
			val = val - 0x3f;
			break;
		case 2:
			//..sx xxxx xxxx xxxx
			val = val - 0x1fff;
			break;
		default:
			return MKV_ELACING;
		}

		if (prev + val < 0)
			return MKV_ELACING;
		*lace++ = prev + val;
		prev = prev + val;
	}

	return 0;
}

/** Read Xiph lacing. */
static int mkv_lacing_xiph(ffstr *data, uint *lace, uint n)
{
	for (uint i = 0;  i != n;  i++) {
		int r = ogg_pktlen(data->ptr, data->len, lace);
		if (r == 0)
			return MKV_ELACING;
		lace++;
		ffstr_shift(data, r);
	}
	return 0;
}

/** Read fixed-size lacing. */
static int mkv_lacing_fixed(ffstr *data, uint *lace, uint n)
{
	if (data->len % n)
		return MKV_ELACING;
	for (uint i = 0;  i != n;  i++) {
		*lace++ = data->len / n;
	}
	return 0;
}

enum {
	R_ELID1, R_ELSIZE1, R_ELSIZE, R_EL,
	R_LACING,
	R_NEXTCHUNK, R_SKIP, R_GATHER,
};

int ffmkv_open(ffmkv *m)
{
	m->info.type = -1;
	m->info.num = -1;
	m->els[m->ictx].size = (uint64)-1;
	m->els[m->ictx].ctx = mkv_ctx_global;
	m->state = R_ELID1;
	return 0;
}

void ffmkv_close(ffmkv *m)
{
	ffarr_free(&m->buf);
	ffstr_free(&m->info.asc);
	ffstr_free(&m->codec_data);
	ffarr_free(&m->lacing);
}

/** Convert audio time units to samples. */
static uint64 mkv_units_samples(ffmkv *m, uint64 units)
{
	return ffpcm_samples(units * m->info.scale / 1000000, m->info.sample_rate);
}

void ffmkv_seek(ffmkv *m, uint64 sample)
{
}

/** Read block header. */
static int mkv_block(ffmkv *m, ffstr *data)
{
	int r;
	struct {
		uint64 trackno;
		uint time;
		uint flags;
	} sblk;

	if (-1 == (r = mkv_varint_shift(data, &sblk.trackno)))
		return ERR(m, MKV_EINTVAL);

	if (data->len < 3)
		return ERR(m, MKV_ESMALL);
	sblk.time = ffint_ntoh16(data->ptr);
	sblk.flags = (byte)data->ptr[2];
	ffarr_shift(data, 3);

	FFDBG_PRINTLN(10, "block: track:%U  time:%u (cluster:%u)  flags:%xu"
		, sblk.trackno, sblk.time, m->time_clust, sblk.flags);

	if (sblk.trackno != (uint)m->audio_trkno)
		return FFMKV_RDONE;

	m->nsamples = mkv_units_samples(m, m->time_clust + sblk.time);

	if (sblk.flags & 0x06) {
		if (0 != (r = mkv_lacing(data, &m->lacing, sblk.flags & 0x06)))
			return ERR(m, r);
		m->state = R_LACING;
		return 1;
	}

	return 0;
}

/* MKV read algorithm:
. gather data for element id
. gather data for element size
. process element:
 . search element within the current context
 . skip if unknown
 . or gather its data and convert (string -> int/float) if needed
*/
int ffmkv_read(ffmkv *m)
{
	int r;

	for (;;) {
	switch (m->state) {

	case R_SKIP: {
		mkv_el *el = &m->els[m->ictx];
		if (el->size > m->data.len) {
			m->off += el->size;
			el->size = 0;
			m->state = R_NEXTCHUNK;
			return FFMKV_RSEEK;
		}
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
		mkv_el *el = &m->els[m->ictx];

		if (el->size == 0) {
			uint id = el->id;
			ffmem_tzero(el);
			m->ictx--;

			switch (id) {
			case T_SEG:
				return FFMKV_RDONE;

			case T_TRACKS:
				if (m->info.scale == 0)
					m->info.scale = 1000000;
				m->info.total_samples = ffpcm_samples((double)m->info.dur * m->info.scale / 1000000, m->info.sample_rate);
				break;

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

		m->state = R_ELID1;
		continue;
	}

	case R_GATHER:
		r = ffarr_append_until(&m->buf, m->data.ptr, m->data.len, m->gsize);
		if (r == 0) {
			m->off += m->data.len;
			return FFMKV_RMORE;
		} else if (r == -1)
			return ERR(m, MKV_ESYS);
		ffarr_shift(&m->data, r);
		m->off += r;
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
		mkv_el *parent = &m->els[m->ictx];
		FF_ASSERT(m->ictx != FFCNT(m->els));
		m->ictx++;
		mkv_el *el = &m->els[m->ictx];
		m->el_off = m->off - m->gbuf.len;
		int r = mkv_el_info(el, parent->ctx, m->gbuf.ptr, m->ictx - 1, m->el_off);

		if (m->el_hdrsize + el->size > parent->size)
			return ERR(m, MKV_ELARGE);
		parent->size -= m->el_hdrsize + el->size;

		if (el->id == T_UKN) {
			m->state = R_SKIP;
			continue;
		}

		if ((uint)r < 32 && ffbit_set32(&parent->usemask, r)
			&& !(el->flags & F_MULTI))
			return ERR(m, MKV_EDUPEL);

		uint prio = GET_PRIO(el->flags);
		if (prio != 0) {
			if (prio > parent->prio + 1)
				return ERR(m, MKV_EORDER);
			parent->prio = prio;
		}

		if (el->flags & (F_WHOLE | F_INT | F_INT8 | F_FLT)) {
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
		int val4 = 0;
		uint64 val = 0;
		double fval = 0;
		mkv_el *el = &m->els[m->ictx];

		if (el->flags & (F_INT | F_INT8)) {
			if (0 != mkv_int_ntoh(&val, m->gbuf.ptr, m->gbuf.len))
				return ERR(m, MKV_EINTVAL);

			if ((el->flags & F_INT) && val > 0xffffffff)
				return ERR(m, MKV_EINTVAL);

			val4 = val;
		}

		if (el->flags & F_FLT) {
			if (0 != mkv_flt_ntoh(&fval, m->gbuf.ptr, m->gbuf.len))
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


		case T_SEG:
			m->seg_off = m->off;
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
			FFDBG_PRINTLN(10, "codec: %S", &m->gbuf);
			if (0 != (r = mkv_codec(&m->gbuf))) {

				if (m->info.format == 0 && r >= FFMKV_AUDIO_AAC && r <= _FFMKV_A_LAST)
					m->info.format = r;

				if (m->info.vcodec == 0 && r > _FFMKV_A_LAST && r <= _FFMKV_V_LAST)
					m->info.vcodec = r;
			}
			break;

		case T_CODEC_PRIV:
			ffstr_free(&m->codec_data);
			if (NULL == ffstr_dup(&m->codec_data, m->gbuf.ptr, m->gbuf.len))
				return ERR(m, MKV_ESYS);
			break;

		case T_V_WIDTH:
			m->info.width = val4;
			break;

		case T_V_HEIGHT:
			m->info.height = val4;
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


		case T_CLUST:
			if (m->clust1_off == 0) {
				m->clust1_off = m->el_off;
				return FFMKV_RHDR;
			}
			break;

		case T_TIME:
			m->time_clust = val4;
			break;

		case T_BLOCK:
		case T_SBLOCK:
			if (0 != (r = mkv_block(m, &m->gbuf))) {
				if (m->state == R_LACING)
					continue;
				if (r == FFMKV_RDONE)
					break;
				return r;
			}

			m->state = R_SKIP;
			ffstr_set2(&m->out, &m->gbuf);
			return FFMKV_RDATA;
		}

		if (el->ctx != NULL) {
			m->state = R_NEXTCHUNK;
			continue;
		}

		m->state = R_SKIP;
		continue;
	}

	case R_LACING: {
		if (m->lacing.off == m->lacing.len) {
			m->state = R_SKIP;
			ffstr_set2(&m->out, &m->gbuf);
			return FFMKV_RDATA;
		}
		uint n = *ffarr_itemT(&m->lacing, m->lacing.off++, uint);
		ffstr_set(&m->out, m->gbuf.ptr, n);
		ffarr_shift(&m->gbuf, n);
		return FFMKV_RDATA;
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
