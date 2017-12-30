/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/mformat/mp4-fmt.h>
#include <FF/mformat/mp4.h>
#include <FF/mtags/id3.h>
#include <FF/number.h>


/** Search box in the context.
Return -1 if not found. */
int mp4_box_find(const struct bbox *ctx, const char type[4])
{
	for (uint i = 0;  ;  i++) {
		if (!ffs_cmp(type, ctx[i].type, 4))
			return i;
		if (ctx[i].flags & F_LAST)
			break;
	}
	return -1;
}

/**
Return total box size. */
int mp4_box_write(const char *type, char *dst, size_t len)
{
	struct box *b = (void*)dst;
	ffint_hton32(b->size, len + sizeof(struct box));
	ffmemcpy(b->type, type, 4);
	return len + sizeof(struct box);
}

/**
Return total box size. */
int mp4_fbox_write(const char *type, char *dst, size_t len)
{
	len += sizeof(struct fullbox);
	return mp4_box_write(type, dst, len);
}


struct ftyp {
	char major_brand[4];
	byte minor_version[4];

	char compatible_brands[4];
	//...
};


struct tkhd0 {
	//flags = 0x07
	byte creat_time[4];
	byte mod_time[4];
	byte id[4];
	byte res[4];
	byte duration[4];

	byte unused[4 * 15];
};
struct tkhd1 {
	//flags = 0x07
	byte creat_time[8];
	byte mod_time[8];
	byte id[4];
	byte res[4];
	byte duration[8];

	byte unused[4 * 15];
};

int mp4_tkhd_write(char *dst, uint id, uint64 total_samples)
{
	struct fullbox *fbox = (void*)dst;
	fbox->version = 1;
	fbox->flags[2] = 0x07;

	struct tkhd1 *tkhd = (void*)(fbox + 1);
	ffint_hton32(tkhd->id, 1);
	ffint_hton64(tkhd->duration, total_samples);
	return sizeof(struct tkhd1);
}


struct mvhd0 {
	byte creat_time[4];
	byte mod_time[4];
	byte timescale[4];
	byte duration[4];
	byte unused[80];
};
struct mvhd1 {
	byte creat_time[8];
	byte mod_time[8];
	byte timescale[4];
	byte duration[8];
	byte unused[80];
};

int mp4_mvhd_write(char *dst, uint rate, uint64 total_samples)
{
	struct fullbox *fbox = (void*)dst;
	fbox->version = 1;

	struct mvhd1 *mvhd = (void*)(fbox + 1);
	ffint_hton32(mvhd->timescale, rate);
	ffint_hton64(mvhd->duration, total_samples);
	return sizeof(struct mvhd1);
}


struct mdhd0 {
	byte creat_time[4];
	byte mod_time[4];
	byte timescale[4];
	byte duration[4];
	byte unused[4];
};

int mp4_mdhd_write(char *dst, uint rate, uint64 total_samples)
{
	struct fullbox *fbox = (void*)dst;
	fbox->version = 0;

	struct mdhd0 *mdhd = (void*)(fbox + 1);
	ffint_hton32(mdhd->timescale, rate);
	ffint_hton32(mdhd->duration, total_samples);
	return sizeof(struct mdhd0);
}


struct stsd {
	byte cnt[4];
};

int mp4_stsd_write(char *dst)
{
	struct stsd *stsd = (void*)dst;
	ffint_hton32(stsd->cnt, 1);
	return sizeof(struct stsd);
}


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

void mp4_asamp(const char *data, ffpcm *fmt)
{
	const struct asamp *asamp = (void*)data;
	fmt->format = ffint_ntoh16(asamp->bits);
	fmt->channels = ffint_ntoh16(asamp->channels);
	fmt->sample_rate = ffint_ntoh16(asamp->rate);
}

uint mp4_asamp_write(char *dst, const ffpcm *fmt)
{
	struct asamp *asamp = (void*)dst;
	ffint_hton16(asamp->channels, fmt->channels);
	ffint_hton16(asamp->bits, ffpcm_bits(fmt->format));
	ffint_hton16(asamp->rate, fmt->sample_rate);
	return sizeof(struct asamp);
}


struct alac {
	byte conf[24];
	byte chlayout[0]; //optional, 24 bytes
};


/* "esds" box:
(TAG SIZE ESDS) {
	(TAG SIZE DEC_CONF) {
		(TAG SIZE DEC_SPEC)
	}
	(TAG SIZE SL)
} */

enum ESDS_TAGS {
	ESDS_TAG = 3,
	ESDS_DEC_TAG = 4,
	ESDS_DECSPEC_TAG = 5,
	ESDS_SL_TAG = 6,
};
struct esds_tag {
	byte tag; //enum ESDS_TAGS
	byte size[4]; //"NN" | "80 80 80 NN"
};
struct esds {
	byte unused[3];
};
struct esds_dec {
	byte type; //enum ESDS_DEC_TYPE
	byte stm_type;
	byte unused[3];
	byte max_brate[4];
	byte avg_brate[4];
};
struct esds_decspec {
	byte data[2]; //Audio Specific Config
};
struct esds_sl {
	byte val;
};

/** Get next esds block.
Return block tag;  0 on error. */
static int mp4_esds_block(const char **pd, const char *end, uint *size)
{
	const struct esds_tag *tag = (void*)*pd;
	uint sz;

	if (end - *pd < 2)
		return 0;

	if (tag->size[0] != 0x80) {
		*pd += 2;
		sz = tag->size[0];

	} else {
		*pd += sizeof(struct esds_tag);
		if (*pd > end)
			return 0;
		sz = tag->size[3];
	}

	if (sz < *size)
		return 0;
	*size = sz;
	return tag->tag;
}

static FFINL uint mp4_esds_block_write(char *dst, uint tag, uint size)
{
	struct esds_tag *t = (void*)dst;
	t->tag = tag;
	ffint_hton32(t->size, 0x80808000 | size);
	return sizeof(struct esds_tag);
}

/**
Return 0 on success;  <0 on error. */
int mp4_esds(const char *data, uint len, struct mp4_esds *esds)
{
	const char *d = data;
	const char *end = data + len;
	int r = -MP4_EDATA;
	uint size;

	size = sizeof(struct esds);
	if (ESDS_TAG == mp4_esds_block(&d, end, &size)) {
		d += sizeof(struct esds);

		size = sizeof(struct esds_dec);
		if (ESDS_DEC_TAG == mp4_esds_block(&d, end, &size)) {
			const struct esds_dec *dec = (void*)d;
			d += sizeof(struct esds_dec);
			esds->type = dec->type;
			esds->stm_type = dec->stm_type;
			esds->max_brate = ffint_ntoh32(dec->max_brate);
			esds->avg_brate = ffint_ntoh32(dec->avg_brate);

			size = sizeof(struct esds_decspec);
			if (ESDS_DECSPEC_TAG == mp4_esds_block(&d, end, &size)) {
				const struct esds_decspec *spec = (void*)d;
				d += size;
				esds->conf = (char*)spec->data,  esds->conflen = size;
				r = 0;
			}
		}
	}

	return r;
}

int mp4_esds_write(char *dst, const struct mp4_esds *esds)
{
	char *d = dst;
	uint total = sizeof(struct esds)
		+ sizeof(struct esds_tag) + sizeof(struct esds_dec)
		+ sizeof(struct esds_tag) + esds->conflen
		+ sizeof(struct esds_tag) + 1;

	d += mp4_esds_block_write(d, ESDS_TAG, total);
	ffmem_zero(d, sizeof(struct esds));
	d += sizeof(struct esds);

	d += mp4_esds_block_write(d, ESDS_DEC_TAG, sizeof(struct esds_dec) + sizeof(struct esds_tag) + esds->conflen);
	struct esds_dec *dec = (void*)d;
	dec->type = esds->type;
	dec->stm_type = esds->stm_type;
	ffmem_zero(dec->unused, sizeof(dec->unused));
	ffint_hton32(dec->max_brate, esds->max_brate);
	ffint_hton32(dec->avg_brate, esds->avg_brate);
	d += sizeof(struct esds_dec);

	d += mp4_esds_block_write(d, ESDS_DECSPEC_TAG, esds->conflen);
	struct esds_decspec *spec = (void*)d;
	ffmemcpy(spec->data, esds->conf, esds->conflen);
	d += sizeof(struct esds_decspec);

	d += mp4_esds_block_write(d, ESDS_SL_TAG, 1);
	struct esds_sl *sl = (void*)d;
	sl->val = 0x02;
	d += sizeof(struct esds_sl);

	return d - dst;
}


struct stts_ent {
	byte sample_cnt[4];
	byte sample_delta[4];
};
struct stts {
	byte cnt[4];
	struct stts_ent ents[0];
};

/** Set audio position (in samples) for each seek point.
The last entry is always equal to the total samples.
Return total samples;  <0 on error. */
int64 mp4_stts(struct seekpt *sk, uint skcnt, const char *data, uint len)
{
	const struct stts *stts = (void*)data;
	const struct stts_ent *ents = (void*)stts->ents;
	uint64 pos = 0;
	uint i, k, cnt, isk = 0;

	cnt = ffint_ntoh32(stts->cnt);
	if (len < sizeof(struct stts) + cnt * sizeof(struct stts_ent))
		return -MP4_EDATA;

	for (i = 0;  i != cnt;  i++) {
		uint nsamps = ffint_ntoh32(ents[i].sample_cnt);
		uint delt = ffint_ntoh32(ents[i].sample_delta);

		if (isk + nsamps >= skcnt)
			return -MP4_EDATA;

		for (k = 0;  k != nsamps;  k++) {
			sk[isk++].audio_pos = pos + delt * k;
		}
		pos += nsamps * delt;
	}

	if (isk != skcnt - 1)
		return -MP4_EDATA;

	sk[skcnt - 1].audio_pos = pos;
	return pos;
}

int mp4_stts_write(char *dst, uint64 total_samples, uint framelen)
{
	struct stts *stts = (void*)dst;
	struct stts_ent *ent = stts->ents;

	if ((total_samples / framelen) != 0) {
		ffint_hton32(ent->sample_cnt, total_samples / framelen);
		ffint_hton32(ent->sample_delta, framelen);
		ent++;
	}

	if ((total_samples % framelen) != 0) {
		ffint_hton32(ent->sample_cnt, 1);
		ffint_hton32(ent->sample_delta, total_samples % framelen);
		ent++;
	}

	ffint_hton32(stts->cnt, ent - stts->ents);
	return sizeof(struct stts) + (ent - stts->ents) * sizeof(struct stts_ent);
}

/**
Return the index of lower-bound seekpoint;  -1 on error. */
int mp4_seek(const struct seekpt *pts, size_t npts, uint64 sample)
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


struct stsc_ent {
	byte first_chunk[4];
	byte chunk_samples[4];
	byte sample_description_index[4];
};
struct stsc {
	byte cnt[4];
	struct stsc_ent ents[0];
};

/** Set chunk index for each seek point. */
int mp4_stsc(struct seekpt *sk, uint skcnt, const char *data, uint len)
{
	const struct stsc *stsc = (void*)data;
	const struct stsc_ent *e = &stsc->ents[0];
	uint i, cnt, isk = 0, ich, isamp;

	cnt = ffint_ntoh32(stsc->cnt);
	if (cnt == 0 || len < sizeof(struct stsc) + cnt * sizeof(struct stsc_ent))
		return -MP4_EDATA;

	uint nsamps = ffint_ntoh32(e[0].chunk_samples);
	uint prev_first_chunk = ffint_ntoh32(e[0].first_chunk);
	if (prev_first_chunk == 0)
		return -MP4_EDATA;

	for (i = 1;  i != cnt;  i++) {
		uint first_chunk = ffint_ntoh32(e[i].first_chunk);

		if (prev_first_chunk >= first_chunk
			|| isk + (first_chunk - prev_first_chunk) * nsamps >= skcnt)
			return -MP4_EDATA;

		for (ich = prev_first_chunk;  ich != first_chunk;  ich++) {
			for (isamp = 0;  isamp != nsamps;  isamp++) {
				sk[isk++].chunk_id = ich - 1;
			}
		}

		nsamps = ffint_ntoh32(e[i].chunk_samples);
		prev_first_chunk = ffint_ntoh32(e[i].first_chunk);
	}

	for (ich = prev_first_chunk;  ;  ich++) {
		if (isk + nsamps >= skcnt)
			return -MP4_EDATA;
		for (isamp = 0;  isamp != nsamps;  isamp++) {
			sk[isk++].chunk_id = ich - 1;
		}
		if (isk == skcnt - 1)
			break;
	}

	return 0;
}

int mp4_stsc_write(char *dst, uint64 total_samples, uint frame_samples, uint chunk_samples)
{
	struct stsc *stsc = (void*)dst;
	struct stsc_ent *ent = stsc->ents;

	uint total_frames = total_samples / frame_samples + !!(total_samples % frame_samples);
	uint chunk_frames = chunk_samples / frame_samples;

	if (0 != total_frames / chunk_frames) {
		ffint_hton32(ent->first_chunk, 1);
		ffint_hton32(ent->chunk_samples, chunk_frames);
		ffint_hton32(ent->sample_description_index, 1);
		ent++;
	}

	if (0 != (total_frames % chunk_frames)) {
		ffint_hton32(ent->first_chunk, total_frames / chunk_frames + 1);
		ffint_hton32(ent->chunk_samples, total_frames % chunk_frames);
		ffint_hton32(ent->sample_description_index, 1);
		ent++;
	}

	uint cnt = ent - stsc->ents;
	ffint_hton32(stsc->cnt, cnt);
	return sizeof(struct stsc) + cnt * sizeof(struct stsc_ent);
}


struct stsz {
	byte def_size[4];
	byte cnt[4];
	byte size[0][4]; // if def_size == 0
};

/** Initialize seek table.  Set size (in bytes) for each seek point.
Return the number of seek points;  <0 on error. */
int mp4_stsz(const char *data, uint len, struct seekpt *sk)
{
	const struct stsz *stsz = (void*)data;
	uint i, def_size, cnt = ffint_ntoh32(stsz->cnt);

	if (len < sizeof(struct stsz) + cnt * sizeof(int))
		return -MP4_EDATA;

	if (sk == NULL)
		return cnt + 1;

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

	return cnt + 1;
}

int mp4_stsz_size(uint frames)
{
	return sizeof(struct stsz) + frames * sizeof(int);
}

int mp4_stsz_add(char *dst, uint frsize)
{
	struct stsz *stsz = (void*)dst;
	// ffmem_zero(stsz->def_size, sizeof(stsz->def_size));
	uint n = ffint_ntoh32(stsz->cnt);
	ffint_hton32(stsz->cnt, n + 1);
	ffint_hton32(stsz->size[n], frsize);
	return sizeof(struct stsz) + (n + 1) * sizeof(int);
}


struct stco {
	byte cnt[4];
	byte chunkoff[0][4];
};
struct co64 {
	byte cnt[4];
	byte chunkoff[0][8];
};

/** Set absolute file offset for each chunk.
Return number of chunks;  <0 on error. */
int mp4_stco(const char *data, uint len, uint type, uint64 *chunktab)
{
	const struct stco *stco = (void*)data;
	uint64 off, lastoff = 0;
	uint i, cnt, sz;
	const uint *chunkoff = (void*)stco->chunkoff;
	const uint64 *chunkoff64 = (void*)stco->chunkoff;

	sz = (type == BOX_STCO) ? sizeof(int) : sizeof(int64);
	cnt = ffint_ntoh32(stco->cnt);
	if (len < sizeof(struct stco) + cnt * sz)
		return -MP4_EDATA;

	if (chunktab == NULL)
		return cnt;

	for (i = 0;  i != cnt;  i++) {

		if (type == BOX_STCO)
			off = ffint_ntoh32(&chunkoff[i]);
		else
			off = ffint_ntoh64(&chunkoff64[i]);

		if (off < lastoff)
			return -MP4_EDATA; //offsets must grow

		chunktab[i] = lastoff = off;
	}

	return cnt;
}

int mp4_stco_size(uint type, uint chunks)
{
	if (type == BOX_STCO)
		return sizeof(struct stco) + chunks * sizeof(int);
	return sizeof(struct co64) + chunks * sizeof(int64);
}

int mp4_stco_add(const char *data, uint type, uint64 offset)
{
	struct stco *stco = (void*)data;
	uint n = ffint_ntoh32(stco->cnt);
	ffint_hton32(stco->cnt, n + 1);

	if (type == BOX_STCO) {
		ffint_hton32(stco->chunkoff[n], offset);
		return sizeof(struct stco) + (n + 1) * sizeof(int);
	}

	struct co64 *co64 = (void*)data;
	ffint_hton64(co64->chunkoff[n], offset);
	return sizeof(struct stco) + (n + 1) * sizeof(int64);
}


enum ILST_DATA_TYPE {
	ILST_IMPLICIT,
	ILST_UTF8,
	ILST_JPEG = 13,
	ILST_PNG,
	ILST_INT = 21,
};

struct ilst_data {
	byte unused[3];
	byte type; //enum ILST_DATA_TYPE
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

static const struct bbox mp4_ctx_ilst[];

/** Process "ilst.*.data" box.
Return enum FFMMTAG;  0 on error;  -1: trackno tag. */
int mp4_ilst_data(const char *data, uint len, uint parent_type, ffstr *tagval, char *tagbuf, size_t tagbuf_cap)
{
	const struct ilst_data *d = (void*)data;
	FFARR_SHIFT(data, len, sizeof(struct ilst_data));

	switch (parent_type) {

	case FFMMTAG_TRACKNO: {
		if (len < sizeof(struct trkn) || d->type != ILST_IMPLICIT)
			return 0;

		const struct trkn *trkn = (void*)data;
		int num = ffint_ntoh16(trkn->num);
		if (num == 0)
			return -1;
		int n = ffs_fromint(num, tagbuf, tagbuf_cap, 0);
		ffstr_set(tagval, tagbuf, n);
		return FFMMTAG_TRACKNO;
	}

	case FFMMTAG_DISCNUMBER: {
		if (len < sizeof(struct disk) || d->type != ILST_IMPLICIT)
			return 0;

		const struct disk *disk = (void*)data;
		int num = ffint_ntoh16(disk->num);
		int n = ffs_fromint(num, tagbuf, tagbuf_cap, 0);
		ffstr_set(tagval, tagbuf, n);
		return FFMMTAG_DISCNUMBER;
	}

	case BOX_TAG_GENRE_ID31: {
		if (len < sizeof(short) || !(d->type == ILST_IMPLICIT || d->type == ILST_INT))
			return 0;

		const char *g = ffid31_genre(ffint_ntoh16(data));
		ffstr_setz(tagval, g);
		return FFMMTAG_GENRE;
	}
	}

	if (d->type != ILST_UTF8)
		return 0;

	ffstr_set(tagval, data, len);
	return parent_type;
}

/** Process total tracks number from "ilst.trkn.data" */
int mp4_ilst_trkn(const char *data, ffstr *tagval, char *tagbuf, size_t tagbuf_cap)
{
	const struct trkn *trkn = (void*)(data + sizeof(struct ilst_data));
	uint total = ffint_ntoh16(trkn->total);
	if (total == 0)
		return 0;
	uint n = ffs_fromint(total, tagbuf, tagbuf_cap, 0);
	ffstr_set(tagval, tagbuf, n);
	return FFMMTAG_TRACKTOTAL;
}

/** Find box by tag ID. */
const struct bbox* mp4_ilst_find(uint mmtag)
{
	const struct bbox *p = mp4_ctx_ilst;
	for (uint i = 0;  ;  i++) {
		if (_BOX_TAG + mmtag == GET_TYPE(p[i].flags))
			return &p[i];
		if (p[i].flags & F_LAST)
			break;
	}
	return NULL;
}

int mp4_ilst_data_write(char *data, const ffstr *val)
{
	if (data == NULL)
		return sizeof(struct ilst_data) + val->len;

	struct ilst_data *d = (void*)data;
	ffmem_tzero(d);
	d->type = ILST_UTF8;
	ffmemcpy(d + 1, val->ptr, val->len);
	return sizeof(struct ilst_data) + val->len;
}

int mp4_ilst_trkn_data_write(char *data, uint num, uint total)
{
	if (data == NULL)
		return sizeof(struct ilst_data) + sizeof(struct trkn);

	struct ilst_data *d = (void*)data;
	ffmem_tzero(d);
	d->type = ILST_IMPLICIT;

	struct trkn *t = (void*)(d + 1);
	ffmem_tzero(t);
	ffint_hton16(t->num, num);
	ffint_hton16(t->total, total);
	return sizeof(struct ilst_data) + sizeof(struct trkn);
}

int mp4_itunes_smpb(const char *data, size_t len, uint *_enc_delay, uint *_padding)
{
	ffstr s;
	if (0 == mp4_ilst_data(data, len, (uint)-1, &s, NULL, 0))
		return 0;

	uint tmp, enc_delay, padding;
	uint64 samples;
	int r = ffs_fmatch(s.ptr, s.len, " %8xu %8xu %8xu %16xU"
		, &tmp, &enc_delay, &padding, &samples);
	if (r <= 0)
		return 0;

	*_enc_delay = enc_delay;
	*_padding = padding;
	return r;
}

int mp4_itunes_smpb_write(char *data, uint64 total_samples, uint enc_delay, uint padding)
{
	char buf[255];
	ffstr s;

	if (data == NULL) {
		s.len = (1 + 8) * 11 + (1 + 16);
		return mp4_ilst_data_write(NULL, &s);
	}

	s.ptr = buf;
	s.len = ffs_fmt(buf, buf + sizeof(buf), " 00000000 %08Xu %08Xu %016XU 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000"
		, enc_delay, padding, total_samples - enc_delay - padding);
	return mp4_ilst_data_write(data, &s);
}


/*
Supported boxes hierarchy:

ftyp
moov
 mvhd
 trak
  tkhd
  mdia
   mdhd
   hdlr
   minf
    smhd
    dinf
     dref
      url
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
    ----
     mean
     name
     data
mdat
*/

static const struct bbox mp4_ctx_moov[];

static const struct bbox mp4_ctx_trak[];
static const struct bbox mp4_ctx_mdia[];
static const struct bbox mp4_ctx_minf[];
static const struct bbox mp4_ctx_dinf[];
static const struct bbox mp4_ctx_dref[];
static const struct bbox mp4_ctx_stbl[];
static const struct bbox mp4_ctx_stsd[];
static const struct bbox mp4_ctx_alac[];
static const struct bbox mp4_ctx_mp4a[];

static const struct bbox mp4_ctx_udta[];
static const struct bbox mp4_ctx_meta[];
static const struct bbox mp4_ctx_data[];
static const struct bbox mp4_ctx_itunes[];

const struct bbox mp4_ctx_global[] = {
	{"ftyp", BOX_FTYP | PRIOTY(1) | MINSIZE(sizeof(struct ftyp)), NULL},
	{"moov", BOX_MOOV | PRIOTY(2), mp4_ctx_moov},
	{"mdat", BOX_MDAT | PRIOTY(2) | F_LAST, NULL},
};
const struct bbox mp4_ctx_global_stream[] = {
	{"ftyp", BOX_FTYP | MINSIZE(sizeof(struct ftyp)), NULL},
	{"mdat", BOX_MDAT, NULL},
	{"moov", BOX_MOOV | F_LAST, mp4_ctx_moov},
};
static const struct bbox mp4_ctx_moov[] = {
	{"mvhd", BOX_MVHD | F_FULLBOX | F_REQ | MINSIZE(sizeof(struct mvhd0)), NULL},
	{"trak", BOX_ANY | F_MULTI, mp4_ctx_trak},
	{"udta", BOX_ANY | F_LAST, mp4_ctx_udta},
};

static const struct bbox mp4_ctx_trak[] = {
	{"tkhd", BOX_TKHD | F_FULLBOX | F_REQ | MINSIZE(sizeof(struct tkhd0)), NULL},
	{"mdia", BOX_ANY | F_LAST, mp4_ctx_mdia},
};
static const struct bbox mp4_ctx_mdia[] = {
	{"hdlr", BOX_HDLR | F_FULLBOX | F_REQ | MINSIZE(sizeof(struct hdlr)), NULL},
	{"mdhd", BOX_MDHD | F_FULLBOX | F_REQ | MINSIZE(sizeof(struct mdhd0)), NULL},
	{"minf", BOX_ANY | F_LAST, mp4_ctx_minf},
};
static const struct bbox mp4_ctx_minf[] = {
	{"smhd", BOX_ANY | F_FULLBOX | MINSIZE(sizeof(struct smhd)), NULL},
	{"dinf", BOX_ANY | F_REQ, mp4_ctx_dinf},
	{"stbl", BOX_ANY | F_LAST, mp4_ctx_stbl},
};
static const struct bbox mp4_ctx_dinf[] = {
	{"dref", BOX_DREF | F_FULLBOX | F_REQ | MINSIZE(sizeof(struct dref)) | F_LAST, mp4_ctx_dref},
};
static const struct bbox mp4_ctx_dref[] = {
	{"url ", BOX_DREF_URL | F_FULLBOX | F_LAST, NULL},
};
static const struct bbox mp4_ctx_stbl[] = {
	{"stsd", BOX_STSD | F_FULLBOX | F_REQ | MINSIZE(sizeof(struct stsd)), mp4_ctx_stsd},
	{"co64", BOX_CO64 | F_FULLBOX | F_WHOLE | MINSIZE(sizeof(struct co64)), NULL},
	{"stco", BOX_STCO | F_FULLBOX | F_WHOLE | MINSIZE(sizeof(struct stco)), NULL},
	{"stsc", BOX_STSC | F_FULLBOX | F_REQ | F_WHOLE | MINSIZE(sizeof(struct stsc)), NULL},
	{"stsz", BOX_STSZ | F_FULLBOX | F_REQ | F_WHOLE | MINSIZE(sizeof(struct stsz)), NULL},
	{"stts", BOX_STTS | F_FULLBOX | F_REQ | F_WHOLE | MINSIZE(sizeof(struct stts)) | F_LAST, NULL},
};
static const struct bbox mp4_ctx_stsd[] = {
	{"alac", BOX_STSD_ALAC | MINSIZE(sizeof(struct asamp)), mp4_ctx_alac},
	{"mp4a", BOX_STSD_MP4A | MINSIZE(sizeof(struct asamp)) | F_LAST, mp4_ctx_mp4a},
};
static const struct bbox mp4_ctx_alac[] = {
	{"alac", BOX_ALAC | F_FULLBOX | F_REQ | F_WHOLE | MINSIZE(sizeof(struct alac)) | F_LAST, NULL},
};
static const struct bbox mp4_ctx_mp4a[] = {
	{"esds", BOX_ESDS | F_FULLBOX | F_REQ | F_WHOLE | F_LAST, NULL},
};

static const struct bbox mp4_ctx_udta[] = {
	{"meta", BOX_ANY | F_FULLBOX | F_LAST, mp4_ctx_meta},
};
static const struct bbox mp4_ctx_meta[] = {
	{"ilst", BOX_ANY | F_LAST, mp4_ctx_ilst},
};
static const struct bbox mp4_ctx_ilst[] = {
	{"aART",	_BOX_TAG + FFMMTAG_ALBUMARTIST,	mp4_ctx_data},
	{"covr",	_BOX_TAG,	mp4_ctx_data},
	{"cprt",	_BOX_TAG + FFMMTAG_COPYRIGHT,	mp4_ctx_data},
	{"desc",	_BOX_TAG,	mp4_ctx_data},
	{"disk",	_BOX_TAG + FFMMTAG_DISCNUMBER,	mp4_ctx_data},
	{"gnre",	_BOX_TAG + BOX_TAG_GENRE_ID31,	mp4_ctx_data},
	{"trkn",	_BOX_TAG + FFMMTAG_TRACKNO,	mp4_ctx_data},
	{"\251alb",	_BOX_TAG + FFMMTAG_ALBUM,	mp4_ctx_data},
	{"\251ART",	_BOX_TAG + FFMMTAG_ARTIST,	mp4_ctx_data},
	{"\251cmt",	(_BOX_TAG + FFMMTAG_COMMENT) | F_MULTI,	mp4_ctx_data},
	{"\251day",	_BOX_TAG + FFMMTAG_DATE,	mp4_ctx_data},
	{"\251enc",	_BOX_TAG,	mp4_ctx_data},
	{"\251gen",	_BOX_TAG + FFMMTAG_GENRE,	mp4_ctx_data},
	{"\251lyr",	_BOX_TAG + FFMMTAG_LYRICS,	mp4_ctx_data},
	{"\251nam",	_BOX_TAG + FFMMTAG_TITLE,	mp4_ctx_data},
	{"\251too",	_BOX_TAG + FFMMTAG_VENDOR,	mp4_ctx_data},
	{"\251wrt",	_BOX_TAG + FFMMTAG_COMPOSER,	mp4_ctx_data},
	{"----",	BOX_ITUNES | F_MULTI | F_LAST,	mp4_ctx_itunes},
};

static const struct bbox mp4_ctx_data[] = {
	{"data", BOX_ILST_DATA | F_WHOLE | MINSIZE(sizeof(struct ilst_data)) | F_LAST, NULL},
};

static const struct bbox mp4_ctx_itunes[] = {
	{"mean", BOX_ITUNES_MEAN | F_FULLBOX | F_WHOLE, NULL},
	{"name", BOX_ITUNES_NAME | F_FULLBOX | F_WHOLE, NULL},
	{"data", BOX_ITUNES_DATA | F_WHOLE | MINSIZE(sizeof(struct ilst_data)) | F_LAST, NULL},
};
