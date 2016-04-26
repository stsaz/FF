/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/mp4.h>
#include <FF/audio/id3.h>
#include <FF/number.h>
#include <FFOS/error.h>


struct seekpt {
	uint64 audio_pos;
	uint size;
	uint chunk_id; //index to ffmp4.chunktab
};


struct box {
	byte size[4];
	char type[4];
};

struct box64 {
	byte size[4]; //=1
	char type[4];
	byte largesize[8];
};

struct fullbox {
	//box

	byte version;
	byte flags[3];
};

struct ftyp {
	char major_brand[4];
	byte minor_version[4];

	char compatible_brands[4];
	//...
};

struct stsd {
	byte unused[4];
};

struct asamp {
	byte res[6];
	byte unused[2];

	byte res2[8];
	byte channels[2];
	byte bits[2];
	byte unused2[4];
	byte rate[2];
	byte rate_unused[2];
};

struct alac {
	byte conf[24];
	byte chlayout[0]; //optional, 24 bytes
};

enum DEC_TYPE {
	DEC_MPEG4_AUDIO = 0x40,
};

/* ES(DECODER_CONFIG(DECODER_SPECIFIC(ASC)))
ASC:
 object_type :5
 if object_type == 31
  object_type :6
 freq_index :4
 channels :4 */
struct esds {
	byte tag[4]; //03...
	byte size;
	byte unused[3];

	struct {
		byte tag[4]; //04...
		byte size;
		byte type; //enum DEC_TYPE
		byte unused[4];
		byte max_brate[4];
		byte avg_brate[4];

		struct {
			byte tag[4]; //05...
			byte size;
			byte data[2]; //Audio Specific Config
		} spec;

	} dec_conf;

	//...
};

struct stts_ent {
	byte sample_cnt[4];
	byte sample_delta[4];
};

struct stts {
	byte cnt[4];
	struct stts_ent ents[0];
	//...
};

struct stsz {
	byte def_size[4];
	byte cnt[4];
	byte size[0][4]; // if def_size == 0
	//...
};

struct stsc_ent {
	byte first_chunk[4];
	byte chunk_samples[4];
	byte unused[4];
};

struct stsc {
	byte cnt[4];
	struct stsc_ent ents[0];
	//...
};

struct stco {
	byte cnt[4];
	byte chunkoff[0][4];
	//...
};

struct co64 {
	byte cnt[4];
	byte chunkoff[0][8];
	//...
};

enum MP4_DATA_TYPE {
	TYPE_IMPLICIT,
	TYPE_UTF8,
	TYPE_JPEG = 13,
	TYPE_PNG,
	TYPE_INT = 21,
};

struct data {
	byte unused[3];
	byte type; //enum MP4_DATA_TYPE
	byte unused2[4];
};

struct trkn {
	byte unused[2];
	byte num[2];
	byte total[2];
	byte unused2[2];
};

struct disk {
	byte unused[2];
	byte num[2];
	byte total[2];
};


static int mp4_box_find(const struct bbox *ctx, const char type[4], struct mp4_box *box);
static int mp4_box_parse(ffmp4 *m, struct mp4_box *box, const char *data, uint len);
static int mp4_fbox_parse(ffmp4 *m, struct mp4_box *box, const char *data);
static int mp4_box_close(ffmp4 *m, struct mp4_box *box);
static int mp4_esds(ffmp4 *m, const char *data, uint len);
static int mp4_stsz(ffmp4 *m, const char *data, uint len);
static int mp4_stts(ffmp4 *m, const char *data, uint len);
static int mp4_stsc(ffmp4 *m, const char *data, uint len);
static int mp4_stco(ffmp4 *m, const char *data, uint len, uint type);
static int mp4_ilst_data(ffmp4 *m, struct mp4_box *box, const char *data, uint len);
static int mp4_seek(const struct seekpt *pts, size_t npts, uint64 sample);
static uint64 mp4_data(ffmp4 *m, uint *pisamp, uint *data_size, uint64 *cursample, uint *audio_size);


struct bbox {
	char type[4];
	uint flags; // "minsize" "reserved" "enum MP4_F" "enum BOX"
	const struct bbox *ctx;
};

#define GET_TYPE(f)  ((f) & 0xff)
#define MINSIZE(T)  (sizeof(T) << 24)
#define GET_MINSIZE(f)  ((f >> 24) & 0xff)

enum BOX {
	BOX_ANY,
	BOX_FTYP,
	BOX_STSZ,
	BOX_STSC,
	BOX_STTS,
	BOX_STCO,
	BOX_CO64,
	BOX_ALAC,
	BOX_ASAMP,
	BOX_META,
	BOX_ESDS,

	_TAG_FIRST,
	TAG_ART = _TAG_FIRST,
	TAG_AART,
	TAG_DAY,
	TAG_ALB,
	TAG_CMT,
	TAG_NAM,
	TAG_TOOL,
	TAG_GENRE,
	TAG_GENRE_ID31,
	TAG_TRKN,
	TAG_WRT,
	TAG_COVR,
	TAG_DISK,
	TAG_LYR,
	TAG_DESC,
	TAG_ENC,
	TAG_COPYIRGHT,

	BOX_DATA,
};

enum MP4_F {
	F_WHOLE = 0x100, //wait until the whole box is in memory
	F_FULLBOX = 0x200, //box inherits "struct fullbox"
	F_REQ = 0x400, //mandatory box
};

/** enum BOX (_TAG_FIRST..N) => enum FFMP4_TAG map */
static const byte mp4_tags[] = {
	FFMP4_ARTIST,
	FFMP4_ALBUMARTIST,
	FFMP4_YEAR,
	FFMP4_ALBUM,
	FFMP4_COMMENT,
	FFMP4_TITLE,
	FFMP4_TOOL,
	FFMP4_GENRE,
	0xff,
	0xff,
	FFMP4_COMPOSER,
	0xff,
	0xff,
	FFMP4_LYRICS,
	0xff,
	0xff,
	0xff,
};

/*
Supported boxes hierarchy:

ftyp
moov
  trak
    mdia
      minf
        stbl
          stsd
            mp4a
              esds
            alac
              alac
          stts
          stsc
          stsz
          stco | co64
  udta
    meta
      ilst
        *
          data
*/

static const struct bbox mp4_ctx_global[];
static const struct bbox mp4_ctx_moov[];

static const struct bbox mp4_ctx_trak[];
static const struct bbox mp4_ctx_mdia[];
static const struct bbox mp4_ctx_minf[];
static const struct bbox mp4_ctx_stbl[];
static const struct bbox mp4_ctx_stsd[];
static const struct bbox mp4_ctx_alac[];
static const struct bbox mp4_ctx_mp4a[];

static const struct bbox mp4_ctx_udta[];
static const struct bbox mp4_ctx_meta[];
static const struct bbox mp4_ctx_ilst[];
static const struct bbox mp4_ctx_data[];

static const struct bbox mp4_ctx_global[] = {
	{"ftyp", BOX_FTYP | MINSIZE(struct ftyp), NULL},
	{"moov", BOX_META, mp4_ctx_moov},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_moov[] = {
	{"trak", BOX_ANY, mp4_ctx_trak},
	{"udta", BOX_ANY, mp4_ctx_udta},
	{"",0,NULL}
};

static const struct bbox mp4_ctx_trak[] = {
	{"mdia", BOX_ANY, mp4_ctx_mdia},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_mdia[] = {
	{"minf", BOX_ANY, mp4_ctx_minf},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_minf[] = {
	{"stbl", BOX_ANY, mp4_ctx_stbl},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_stbl[] = {
	{"co64", BOX_CO64 | F_WHOLE | F_FULLBOX | MINSIZE(struct co64), NULL},
	{"stco", BOX_STCO | F_WHOLE | F_FULLBOX | MINSIZE(struct stco), NULL},
	{"stsc", BOX_STSC | F_REQ | F_WHOLE | F_FULLBOX | MINSIZE(struct stsc), NULL},
	{"stsd", BOX_ANY | F_REQ | F_FULLBOX | MINSIZE(struct stsd), mp4_ctx_stsd},
	{"stsz", BOX_STSZ | F_REQ | F_WHOLE | F_FULLBOX | MINSIZE(struct stsz), NULL},
	{"stts", BOX_STTS | F_REQ | F_WHOLE | F_FULLBOX | MINSIZE(struct stts), NULL},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_stsd[] = {
	{"alac", BOX_ASAMP | MINSIZE(struct asamp), mp4_ctx_alac},
	{"mp4a", BOX_ASAMP | MINSIZE(struct asamp), mp4_ctx_mp4a},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_alac[] = {
	{"alac", BOX_ALAC | F_REQ | F_WHOLE | F_FULLBOX | MINSIZE(struct alac), NULL},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_mp4a[] = {
	{"esds", BOX_ESDS | F_REQ | F_WHOLE | F_FULLBOX | MINSIZE(struct esds), NULL},
	{"",0,NULL}
};

static const struct bbox mp4_ctx_udta[] = {
	{"meta", BOX_ANY | F_FULLBOX, mp4_ctx_meta},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_meta[] = {
	{"ilst", BOX_ANY, mp4_ctx_ilst},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_ilst[] = {
	{"aART",	TAG_AART,	mp4_ctx_data},
	{"covr",	TAG_COVR,	mp4_ctx_data},
	{"cprt",	TAG_COPYIRGHT,	mp4_ctx_data},
	{"desc",	TAG_DESC,	mp4_ctx_data},
	{"disk",	TAG_DISK,	mp4_ctx_data},
	{"gnre",	TAG_GENRE_ID31,	mp4_ctx_data},
	{"trkn",	TAG_TRKN,	mp4_ctx_data},
	{"\251alb",	TAG_ALB,	mp4_ctx_data},
	{"\251ART",	TAG_ART,	mp4_ctx_data},
	{"\251cmt",	TAG_CMT,	mp4_ctx_data},
	{"\251day",	TAG_DAY,	mp4_ctx_data},
	{"\251enc",	TAG_ENC,	mp4_ctx_data},
	{"\251gen",	TAG_GENRE,	mp4_ctx_data},
	{"\251lyr",	TAG_LYR,	mp4_ctx_data},
	{"\251nam",	TAG_NAM,	mp4_ctx_data},
	{"\251too",	TAG_TOOL,	mp4_ctx_data},
	{"\251wrt",	TAG_WRT,	mp4_ctx_data},
	{"",0,NULL}
};
static const struct bbox mp4_ctx_data[] = {
	{"data", BOX_DATA | F_WHOLE | MINSIZE(struct data), NULL},
	{"",0,NULL}
};

#undef MINSIZE


enum MP4_E {
	MP4_ESYS = -1,
	MP4_ENOFTYP = 1,
	MP4_EFTYP,
	MP4_ESTSZ,
	MP4_ESTTS,
	MP4_ESTSC,
	MP4_ESTCO,
	MP4_EESDS,
	MP4_ELARGE,
	MP4_ESMALL,
	MP4_EDUPBOX,
	MP4_ENOREQ,
	MP4_ENOFMT,
};

static const char *const mp4_errs[] = {
	"",
	"ftyp: missing",
	"ftyp: unsupported brand",
	"stsz: invalid data",
	"stts: invalid data",
	"stsc: invalid data",
	"stco: invalid data",
	"esds: invalid data",
	"too large box",
	"too small box",
	"duplicate box",
	"mandatory box not found",
	"no audio format info",
};

const char* ffmp4_errstr(ffmp4 *m)
{
	if (m->err == MP4_ESYS)
		return fferr_strp(fferr_last());
	return mp4_errs[m->err];
}


static const char* const codecs[] = {
	"unknown codec", "ALAC", "AAC"
};

const char* ffmp4_codec(int codec)
{
	return codecs[codec];
}


void ffmp4_init(ffmp4 *m)
{
	m->ctxs[0] = mp4_ctx_global;
}

void ffmp4_close(ffmp4 *m)
{
	ffstr_free(&m->alac);
	ffstr_free(&m->stts);
	ffstr_free(&m->stsc);
	ffarr_free(&m->sktab);
	ffarr_free(&m->chunktab);
	ffarr_free(&m->buf);
}

uint ffmp4_bitrate(ffmp4 *m)
{
	if (m->total_size == 0)
		return 0;
	return ffpcm_brate(m->total_size, m->total_samples, m->fmt.sample_rate);
}

/** Search box in the context.
Return -1 if not found. */
static int mp4_box_find(const struct bbox *ctx, const char type[4], struct mp4_box *box)
{
	uint i;
	for (i = 0;  ctx[i].type[0] != '\0';  i++) {
		if (!ffs_cmp(type, ctx[i].type, 4)) {
			box->type = ctx[i].flags;
			box->ctx = ctx[i].ctx;
			return i;
		}
	}
	return -1;
}

/**
Return 0 on success;  -1 if need more data, i.e. sizeof(struct box64). */
static int mp4_box_parse(ffmp4 *m, struct mp4_box *box, const char *data, uint len)
{
	const struct box *pbox = (void*)data;

	if (len == sizeof(struct box64)) {
		const struct box64 *box64 = (void*)data;
		box->osize = ffint_ntoh64(box64->largesize);

	} else {
		box->osize = ffint_ntoh32(pbox->size);
		if (box->osize == 1)
			return -1;
	}

	int idx = mp4_box_find(m->ctxs[m->ictx], pbox->type, box);

	FFDBG_PRINTLN(10, "%4s (%U)", pbox->type, box->osize);

	struct mp4_box *parent = &m->boxes[m->ictx - 1];
	if (m->ictx != 0 && box->osize > parent->size)
		return MP4_ELARGE;

	if (m->ictx != 0 && idx != -1) {
		if (ffbit_set32(&parent->usedboxes, idx))
			return MP4_EDUPBOX;
	}

	return 0;
}

static int mp4_fbox_parse(ffmp4 *m, struct mp4_box *box, const char *data)
{
	if (box->size < sizeof(struct fullbox))
		return MP4_ESMALL;

	box->size -= sizeof(struct fullbox);
	return 0;
}

/**
Return -1 on success;  0 if success, but the parent box must be closed too;  enum MP4_E on error. */
static int mp4_box_close(ffmp4 *m, struct mp4_box *box)
{
	struct mp4_box *parent = box - 1;

	if (GET_TYPE(box->type) == BOX_META)
		m->meta_closed = 1;

	if (box->ctx != NULL) {
		uint i;
		for (i = 0;  box->ctx[i].type[0] != '\0';  i++) {
			if ((box->ctx[i].flags & F_REQ)
				&& !ffbit_test32(box->usedboxes, i))
				return MP4_ENOREQ;
		}
	}

	if (m->ictx == 0) {
		ffmem_tzero(box);
		return -1;
	}

	parent->size -= box->osize;
	ffmem_tzero(box);
	if ((int64)parent->size > 0)
		return -1;

	m->ctxs[m->ictx] = NULL;
	m->ictx--;
	return 0;
}

static int mp4_esds(ffmp4 *m, const char *data, uint len)
{
	const struct esds *es = (void*)data;
	if (!(es->tag[0] == 0x03 && es->size >= sizeof(*es) - 5
		&& es->dec_conf.tag[0] == 0x04 && es->dec_conf.size >= sizeof(es->dec_conf) - 5
		&& es->dec_conf.type == DEC_MPEG4_AUDIO
		&& es->dec_conf.spec.tag[0] == 0x05 && es->dec_conf.spec.size >= 2)) {

		FFDBG_PRINTLN(1, "size:%u  type:0x%xu  ASC:%2xb"
			, len, (uint)es->dec_conf.type, es->dec_conf.spec.data);
		return MP4_EESDS;
	}

	ffmemcpy(m->aac_asc, es->dec_conf.spec.data, sizeof(m->aac_asc));
	m->aac_brate = ffint_ntoh32(es->dec_conf.avg_brate);
	m->codec = FFMP4_AAC;
	return 0;
}

enum {
	I_BOXREAD, I_BOXSKIP, I_WHOLEDATA, I_FBOX, I_MINSIZE, I_BOXPROCESS, I_TRKTOTAL, I_METAFIN,
	I_DATA, I_DATAREAD, I_DATAOK,
};

/** Process "ilst.*.data" box. */
static int mp4_ilst_data(ffmp4 *m, struct mp4_box *box, const char *data, uint len)
{
	const struct data *d = (void*)data;
	FFARR_SHIFT(data, len, sizeof(struct data));

	const struct mp4_box *parent = &m->boxes[m->ictx - 1];
	int type = GET_TYPE(parent->type);

	switch (type) {

	case TAG_TRKN:
		if (len < sizeof(struct trkn) || d->type != TYPE_IMPLICIT)
			return 0;

		{
		const struct trkn *trkn = (void*)data;
		int num = ffint_ntoh16(trkn->num);
		if (num == 0) {
			m->state = I_TRKTOTAL;
			return 0;
		}
		int n = ffs_fromint(num, m->tagbuf, sizeof(m->tagbuf), 0);
		ffstr_set(&m->tagval, m->tagbuf, n);
		}

		m->tag = FFMP4_TRACKNO;
		m->state = I_TRKTOTAL;
		return FFMP4_RTAG;

	case TAG_DISK:
		if (len < sizeof(struct disk) || d->type != TYPE_IMPLICIT)
			return 0;

		{
		const struct disk *disk = (void*)data;
		int num = ffint_ntoh16(disk->num);
		int n = ffs_fromint(num, m->tagbuf, sizeof(m->tagbuf), 0);
		ffstr_set(&m->tagval, m->tagbuf, n);
		}

		m->tag = FFMP4_DISK;
		m->state = I_BOXSKIP;
		return FFMP4_RTAG;

	case TAG_GENRE_ID31:
		if (len < sizeof(short) || !(d->type == TYPE_IMPLICIT || d->type == TYPE_INT))
			return 0;

		{
		const char *g = ffid31_genre(ffint_ntoh16(data));
		ffstr_setz(&m->tagval, g);
		}
		m->tag = FFMP4_GENRE;
		m->state = I_BOXSKIP;
		return FFMP4_RTAG;
	}

	if (d->type != TYPE_UTF8)
		return 0;

	m->tag = mp4_tags[type - _TAG_FIRST];
	if (m->tag == 0xff)
		return 0;

	ffstr_set(&m->tagval, data, len);
	m->state = I_BOXSKIP;
	return FFMP4_RTAG;
}

/** Initialize seek table.  Set size (in bytes) for each seek point. */
static int mp4_stsz(ffmp4 *m, const char *data, uint len)
{
	const struct stsz *stsz = (void*)data;
	uint i, def_size, cnt;

	cnt = ffint_ntoh32(stsz->cnt);
	if (len < sizeof(struct stsz) + cnt * sizeof(int))
		return MP4_ESTSZ;

	if (NULL == _ffarr_alloc(&m->sktab, cnt + 1, sizeof(struct seekpt)))
		return MP4_ESYS;
	m->sktab.len = cnt + 1;
	struct seekpt *sk = (void*)m->sktab.ptr;

	def_size = ffint_ntoh32(stsz->def_size);
	if (def_size != 0) {
		for (i = 0;  i != cnt;  i++) {
			sk[i].size = def_size;
		}

	} else {

		const int *psize = (void*)stsz->size;
		for (i = 0;  i != cnt;  i++) {
			sk[i].size = ffint_ntoh32(&psize[i]);
		}
	}

	return 0;
}

/** Set audio position (in samples) for each seek point.
The last entry is always equal to the total samples. */
static int mp4_stts(ffmp4 *m, const char *data, uint len)
{
	const struct stts *stts = (void*)data;
	const struct stts_ent *ents;
	struct seekpt *sk = (void*)m->sktab.ptr;
	uint64 pos = 0;
	uint i, k, cnt, isk = 0;

	cnt = ffint_ntoh32(stts->cnt);
	if (len < sizeof(struct stts) + cnt * sizeof(struct stts_ent))
		return MP4_ESTTS;

	ents = (void*)stts->ents;
	for (i = 0;  i != cnt;  i++) {
		uint nsamps = ffint_ntoh32(ents[i].sample_cnt);
		uint delt = ffint_ntoh32(ents[i].sample_delta);

		if (isk + nsamps >= m->sktab.len)
			return MP4_ESTTS;

		for (k = 0;  k != nsamps;  k++) {
			sk[isk++].audio_pos = pos + delt * k;
		}
		pos += nsamps * delt;
	}

	if (isk != m->sktab.len - 1)
		return MP4_ESTTS;

	m->total_samples = pos;
	sk[m->sktab.len - 1].audio_pos = pos;
	return 0;
}

/** Set chunk index for each seek point. */
static int mp4_stsc(ffmp4 *m, const char *data, uint len)
{
	const struct stsc *stsc = (void*)data;
	const struct stsc_ent *e;
	struct seekpt *sk = (void*)m->sktab.ptr;
	uint i, cnt, isk = 0, ich, isamp;

	cnt = ffint_ntoh32(stsc->cnt);
	if (cnt == 0 || len < sizeof(struct stsc) + cnt * sizeof(struct stsc_ent))
		return MP4_ESTSC;

	e = &stsc->ents[0];
	uint nsamps = ffint_ntoh32(e[0].chunk_samples);
	uint prev_first_chunk = ffint_ntoh32(e[0].first_chunk);
	if (prev_first_chunk == 0)
		return MP4_ESTSC;

	for (i = 1;  i != cnt;  i++) {
		uint first_chunk = ffint_ntoh32(e[i].first_chunk);

		if (prev_first_chunk >= first_chunk
			|| isk + (first_chunk - prev_first_chunk) * nsamps >= m->sktab.len)
			return MP4_ESTSC;

		for (ich = prev_first_chunk;  ich != first_chunk;  ich++) {
			for (isamp = 0;  isamp != nsamps;  isamp++) {
				sk[isk++].chunk_id = ich - 1;
			}
		}

		nsamps = ffint_ntoh32(e[i].chunk_samples);
		prev_first_chunk = ffint_ntoh32(e[i].first_chunk);
	}

	for (ich = prev_first_chunk;  ;  ich++) {
		for (isamp = 0;  isamp != nsamps;  isamp++) {
			sk[isk++].chunk_id = ich - 1;
		}
		if (isk == m->sktab.len - 1)
			break;
	}

	return 0;
}

/** Set absolute file offset for each chunk. */
static int mp4_stco(ffmp4 *m, const char *data, uint len, uint type)
{
	const struct stco *stco = (void*)data;
	uint64 off, lastoff = 0, *chunk_off;
	uint i, cnt, sz;
	const int *chunkoff = (void*)stco->chunkoff;
	const int64 *chunkoff64 = (void*)stco->chunkoff;

	sz = (type == BOX_STCO) ? sizeof(int) : sizeof(int64);

	cnt = ffint_ntoh32(stco->cnt);
	if (len < sizeof(struct stco) + cnt * sz)
		return MP4_ESTCO;

	if (NULL == _ffarr_alloc(&m->chunktab, cnt, sizeof(int64)))
		return MP4_ESYS;
	m->chunktab.len = cnt;
	chunk_off = (void*)m->chunktab.ptr;

	for (i = 0;  i != cnt;  i++) {

		if (sz == sizeof(int))
			off = ffint_ntoh32(&chunkoff[i]);
		else
			off = ffint_ntoh64(&chunkoff64[i]);

		if (off < lastoff)
			return MP4_ESTCO; //offsets must grow

		chunk_off[i] = lastoff = off;
	}

	return 0;
}

/** Get info for a MP4-sample.
Return file offset. */
static uint64 mp4_data(ffmp4 *m, uint *pisamp, uint *data_size, uint64 *cursample, uint *audio_size)
{
	const uint64 *chunks = (void*)m->chunktab.ptr;
	const struct seekpt *sk = (void*)m->sktab.ptr;
	uint i, off = 0, isamp = *pisamp;

	for (i = isamp - 1;  (int)i >= 0;  i--) {
		if (sk[i].chunk_id != sk[isamp].chunk_id)
			break;
		off += sk[i].size;
	}

	*data_size = sk[isamp].size;
	*audio_size = sk[isamp + 1].audio_pos - sk[isamp].audio_pos;
	*cursample = sk[isamp].audio_pos;
	*pisamp = isamp + 1;
	return chunks[sk[isamp].chunk_id] + off;
}

void ffmp4_seek(ffmp4 *m, uint64 sample)
{
	int r = mp4_seek((void*)m->sktab.ptr, m->sktab.len, sample);
	if (r == -1)
		return;
	m->isamp = r;
	m->state = I_DATA;
}

/**
Return the index of lower-bound seekpoint;  -1 on error. */
static int mp4_seek(const struct seekpt *pts, size_t npts, uint64 sample)
{
	size_t n = npts;
	uint i = -1, start = 0;

	while (start != n) {
		i = start + (n - start) / 2;
		if (sample == pts[i].audio_pos)
			return i;
		else if (sample < pts[i].audio_pos)
			n = i--;
		else
			start = i + 1;
	}

	if (i == (uint)-1 || i == npts - 1)
		return -1;

	FF_ASSERT(sample > pts[i].audio_pos && sample < pts[i + 1].audio_pos);
	return i;
}

int ffmp4_read(ffmp4 *m)
{
	struct mp4_box *box;
	int r;
	ffstr sbox = {0};

	for (;;) {

	box = &m->boxes[m->ictx];

	switch (m->state) {

	case I_BOXSKIP:
		if (box->type & F_WHOLE) {
			//m->data points to the next box
		} else {
			if (m->datalen < box->size) {
				box->size -= m->datalen;
				return FFMP4_RMORE;
			}
			FFARR_SHIFT(m->data, m->datalen, box->size);
		}

		m->state = I_BOXREAD;

		do {
			r = mp4_box_close(m, &m->boxes[m->ictx]);
			if (r > 0) {
				m->err = r;
				return FFMP4_RERR;
			}
		} while (r == 0);

		if (m->meta_closed) {
			m->meta_closed = 0;

			if (m->fmt.format == 0) {
				m->err = MP4_ENOFMT;
				return FFMP4_RERR;
			}

			if (m->chunktab.len == 0) {
				m->err = MP4_ESTCO;
				return FFMP4_RERR;
			}

			if (0 != (r = mp4_stts(m, m->stts.ptr, m->stts.len))
				|| 0 != (r = mp4_stsc(m, m->stsc.ptr, m->stsc.len))) {
				m->err = r;
				return FFMP4_RERR;
			}
			ffstr_free(&m->stts);
			ffstr_free(&m->stsc);

			if (m->codec == FFMP4_ALAC) {
				m->out = m->alac.ptr;
				m->outlen = m->alac.len;
			}

			m->state = I_METAFIN;
			return FFMP4_RHDR;
		}
		continue;

	case I_METAFIN:
		ffstr_free(&m->alac);
		m->state = I_DATA;
		return FFMP4_RMETAFIN;

	case I_WHOLEDATA:
		r = ffarr_append_until(&m->buf, m->data, m->datalen, m->whole_data);
		if (r == 0)
			return FFMP4_RMORE;
		else if (r == -1) {
			m->err = MP4_ESYS;
			return FFMP4_RERR;
		}
		FFARR_SHIFT(m->data, m->datalen, r);
		m->state = m->nxstate;
		continue;

	case I_BOXREAD:
		{
		uint sz = (m->box64) ? sizeof(struct box64) : sizeof(struct box);
		if (m->buf.len < sz) {
			m->nxstate = I_BOXREAD;
			m->state = I_WHOLEDATA;
			m->whole_data = sz;
			continue;
		}
		r = mp4_box_parse(m, box, m->buf.ptr, sz);
		if (r > 0) {
			m->err = r;
			return FFMP4_RERR;
		} else if (r == -1) {
			m->box64 = 1;
			continue;
		}
		m->box64 = 0;
		m->buf.len = 0;
		box->size = box->osize - sz;
		m->state = I_FBOX;
		}
		// break

	case I_FBOX:
		if (box->type & F_FULLBOX) {
			if (m->buf.len < sizeof(struct fullbox)) {
				m->nxstate = I_FBOX;
				m->state = I_WHOLEDATA;
				m->whole_data = sizeof(struct fullbox);
				continue;
			}
			m->buf.len = 0;

			if (0 != (r = mp4_fbox_parse(m, box, m->buf.ptr))) {
				m->err = r;
				return FFMP4_RERR;
			}
		}
		m->state = I_MINSIZE;
		// break

	case I_MINSIZE:
		{
		uint minsize = GET_MINSIZE(box->type);
		if (minsize != 0) {
			if (box->size < minsize) {
				m->err = MP4_ESMALL;
				return FFMP4_RERR;
			}

			if (!(box->type & F_WHOLE)) {
				if (m->buf.len < minsize) {
					m->nxstate = I_MINSIZE;
					m->state = I_WHOLEDATA;
					m->whole_data = minsize;
					continue;
				}
				ffstr_set2(&sbox, &m->buf);
				m->buf.len = 0;
				// m->data points to box+minsize
			}
		}
		}

		if (box->type & F_WHOLE) {
			if (m->buf.len < box->size) {
				m->nxstate = I_MINSIZE;
				m->state = I_WHOLEDATA;
				m->whole_data = box->size;
				continue;
			}
			ffstr_set2(&sbox, &m->buf);
			m->buf.len = 0;
		}

		m->state = I_BOXPROCESS;
		// break

	case I_BOXPROCESS:
		break;


	case I_TRKTOTAL:
		// process total tracks number from "ilst.trkn.data"
		m->state = I_BOXSKIP;
		{
		const struct trkn *trkn = (void*)(m->buf.ptr + sizeof(struct data));
		uint total = ffint_ntoh16(trkn->total);
		if (total == 0)
			continue;
		m->tag = FFMP4_TRACKTOTAL;
		uint n = ffs_fromint(total, m->tagbuf, sizeof(m->tagbuf), 0);
		ffstr_set(&m->tagval, m->tagbuf, n);
		}
		return FFMP4_RTAG;


	case I_DATAOK:
		m->state = I_DATA;
		// break

	case I_DATA:
		{
		if (m->isamp == m->sktab.len - 1)
			return FFMP4_RDONE;
		uint64 off = mp4_data(m, &m->isamp, &m->chunk_size, &m->cursample, &m->chunk_audio);
		m->state = I_DATAREAD;
		if (off == m->off)
			continue;
		m->off = off;
		return FFMP4_RSEEK;
		}

	case I_DATAREAD:
		r = ffarr_append_until(&m->buf, m->data, m->datalen, m->chunk_size);
		if (r == 0)
			return FFMP4_RMORE;
		else if (r == -1) {
			m->err = MP4_ESYS;
			return FFMP4_RERR;
		}
		FFARR_SHIFT(m->data, m->datalen, r);
		m->off += m->chunk_size;
		m->out = m->buf.ptr;
		m->outlen = m->chunk_size;
		m->buf.len = 0;
		m->state = I_DATAOK;

		FFDBG_PRINTLN(10, "mp4-sample:%u  size:%L  data-chunk:%u  audio-pos:%U"
			, m->isamp - 1, m->outlen, ((struct seekpt*)m->sktab.ptr)[m->isamp - 1].chunk_id, m->cursample);

		return FFMP4_RDATA;
	}

	// I_BOXPROCESS:

	if (!m->ftyp) {
		m->ftyp = 1;
		if (GET_TYPE(box->type) != BOX_FTYP) {
			m->err = MP4_ENOFTYP;
			return FFMP4_RERR;
		}
	}

	switch (GET_TYPE(box->type)) {

	case BOX_FTYP:
		{
		const struct ftyp *ftyp = (void*)sbox.ptr;
		if (!!ffs_cmp(ftyp->major_brand, "M4A ", 4)) {
			m->err = MP4_EFTYP;
			return FFMP4_RERR;
		}
		}
		break;

	case BOX_ASAMP:
		{
		const struct asamp *asamp = (void*)sbox.ptr;
		m->fmt.format = ffint_ntoh16(asamp->bits);
		m->fmt.channels = ffint_ntoh16(asamp->channels);
		m->fmt.sample_rate = ffint_ntoh16(asamp->rate);
		}
		break;

	case BOX_ALAC:
		ffstr_copy(&m->alac, sbox.ptr, sbox.len);
		m->codec = FFMP4_ALAC;
		break;

	case BOX_ESDS:
		if (0 != (r = mp4_esds(m, sbox.ptr, sbox.len))) {
			m->err = r;
			return FFMP4_RERR;
		}
		break;

	case BOX_STSZ:
		if (0 != (r = mp4_stsz(m, sbox.ptr, sbox.len))) {
			m->err = r;
			return FFMP4_RERR;
		}
		break;

	case BOX_STTS:
		ffstr_copy(&m->stts, sbox.ptr, sbox.len);
		break;

	case BOX_STSC:
		ffstr_copy(&m->stsc, sbox.ptr, sbox.len);
		break;

	case BOX_STCO:
	case BOX_CO64:
		if (0 != (r = mp4_stco(m, sbox.ptr, sbox.len, GET_TYPE(box->type)))) {
			m->err = r;
			return FFMP4_RERR;
		}
		break;

	case BOX_DATA:
		if (0 != (r = mp4_ilst_data(m, box, sbox.ptr, sbox.len)))
			return r;
		if (m->state == I_TRKTOTAL)
			continue;
		break;
	}

	if (GET_MINSIZE(box->type) != 0 && !(box->type & F_WHOLE))
		box->size -= GET_MINSIZE(box->type);

	if (box->ctx == NULL)
		m->state = I_BOXSKIP;
	else {
		m->state = I_BOXREAD;
		m->ctxs[++m->ictx] = box->ctx;
	}
	}

	//unreachable
}
