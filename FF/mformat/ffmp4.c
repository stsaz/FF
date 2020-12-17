/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/mformat/mp4-fmt.h>
#include <FF/mformat/mp4.h>
#include <FF/number.h>
#include <FFOS/error.h>


static uint64 mp4_data(const uint64 *chunks, const struct seekpt *sk, uint isamp, uint *data_size, uint64 *cursample);

static int mp4_box_parse(ffmp4 *m, struct mp4_box *parent, struct mp4_box *box, const char *data, uint len);
static int mp4_box_process(ffmp4 *m, const ffstr *data);
static int mp4_box_close(ffmp4 *m, struct mp4_box *box);
static int mp4_meta_closed(ffmp4 *m);

static uint _ffmp4_boxsize(ffmp4_cook *m, const struct bbox *b);
static int _ffmp4_boxdata(ffmp4_cook *m, const struct bbox *b);


static const char *const mp4_errs[] = {
	"", //MP4_EOK
	"invalid data", //MP4_EDATA
	"box is larger than its parent", //MP4_ELARGE
	"too small box", //MP4_ESMALL
	"unsupported order of boxes", //MP4_EORDER
	"duplicate box", //MP4_EDUPBOX
	"mandatory box not found", //MP4_ENOREQ
	"no audio format info", //MP4_ENOFMT
	"unsupported audio codec", //MP4_EACODEC
	"trying to add more frames than expected", //MP4_ENFRAMES
	"co64 output isn't supported", //MP4_ECO64

	"invalid seek position", //MP4_ESEEK
};

const char* ffmp4_errstr(ffmp4 *m)
{
	if (m->err == MP4_ESYS)
		return fferr_strp(fferr_last());

	if (m->boxes[m->ictx].name[0] == '\0')
		return mp4_errs[m->err];

	ssize_t r = ffs_fmt2(m->errmsg, sizeof(m->errmsg), "%4s: %s%Z"
		, m->boxes[m->ictx].name, mp4_errs[m->err]);
	FF_ASSERT(r > 0); (void)r;
	return m->errmsg;
}

const char* ffmp4_werrstr(ffmp4_cook *m)
{
	if (m->err == MP4_ESYS)
		return fferr_strp(fferr_last());

	return mp4_errs[m->err];
}

#define ERR(m, e) \
	(m)->err = e, FFMP4_RERR


/** Get info for a MP4-sample.
Return file offset. */
static uint64 mp4_data(const uint64 *chunks, const struct seekpt *sk, uint isamp, uint *data_size, uint64 *cursample)
{
	uint i, off = 0;

	for (i = isamp - 1;  (int)i >= 0;  i--) {
		if (sk[i].chunk_id != sk[isamp].chunk_id)
			break;
		off += sk[i].size;
	}

	*data_size = sk[isamp].size;
	*cursample = sk[isamp].audio_pos;
	return chunks[sk[isamp].chunk_id] + off;
}


static const char* const codecs[] = {
	"unknown codec", "ALAC", "AAC", "MPEG-1",
	"H.264",
};

const char* ffmp4_codec(int codec)
{
	return codecs[codec];
}


void ffmp4_init(ffmp4 *m)
{
	m->boxes[0].ctx = mp4_ctx_global;
	m->boxes[0].size = (uint64)-1;
}

void ffmp4_close(ffmp4 *m)
{
	ffstr_free(&m->codec_conf);
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

/**
Return 0 on success;  -1 if need more data, i.e. sizeof(struct box64). */
static int mp4_box_parse(ffmp4 *m, struct mp4_box *parent, struct mp4_box *box, const char *data, uint len)
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
	box->size = box->osize - len;

	int idx = mp4_box_find(parent->ctx, pbox->type);
	if (idx != -1) {
		const struct bbox *b = &parent->ctx[idx];
		ffmemcpy(box->name, pbox->type, 4);
		box->type = b->flags;
		box->ctx = b->ctx;
	}

	FFDBG_PRINTLN(10, "%*c%4s  size:%U  offset:%xU"
		, (size_t)m->ictx, ' ', pbox->type, box->osize, m->off - len);

	if (box->osize > parent->size)
		return MP4_ELARGE;

	if (idx != -1 && ffbit_set32(&parent->usedboxes, idx) && !(box->type & F_MULTI))
		return MP4_EDUPBOX;

	return 0;
}

/**
Return -1 on success;  0 if success, but the parent box must be closed too;  enum MP4_E on error. */
static int mp4_box_close(ffmp4 *m, struct mp4_box *box)
{
	struct mp4_box *parent = box - 1;

	if (GET_TYPE(box->type) == BOX_MOOV)
		m->meta_closed = 1;

	if (box->ctx != NULL) {
		for (uint i = 0;  ;  i++) {
			if ((box->ctx[i].flags & F_REQ)
				&& !ffbit_test32(&box->usedboxes, i))
				return MP4_ENOREQ;
			if (box->ctx[i].flags & F_LAST)
				break;
		}
	}

	if (m->ictx == 0) {
		ffmem_tzero(box);
		return -1;
	}

	parent->size -= box->osize;
	ffmem_tzero(box);
	m->ictx--;
	if (parent->size != 0)
		return -1;

	return 0;
}

enum {
	R_BOXREAD, R_BOX_PARSE, R_BOXSKIP, R_WHOLEDATA, R_BOXPROCESS, R_TRKTOTAL, R_METAFIN,
	R_DATA, R_DATAREAD, R_DATAOK,
	R_ERR,
};

void ffmp4_seek(ffmp4 *m, uint64 sample)
{
	int r = mp4_seek((void*)m->sktab.ptr, m->sktab.len, sample);
	if (r == -1) {
		m->err = MP4_ESEEK;
		m->state = R_ERR;
		return;
	}
	m->isamp = r;
	m->state = R_DATA;
}

/**
Return 0 on success;  enum FFMP4_R on error. */
static int mp4_box_process(ffmp4 *m, const ffstr *data)
{
	ffstr sbox = *data;
	int r;
	struct mp4_box *box = &m->boxes[m->ictx];

	switch (GET_TYPE(box->type)) {

	case BOX_FTYP:
		break;

	case BOX_STSD_ALAC:
	case BOX_STSD_MP4A:
		mp4_asamp(sbox.ptr, &m->fmt);
		break;

	case BOX_STSD_AVC1: {
		struct mp4_video v;
		r = mp4_avc1(sbox.ptr, sbox.len, &v);
		if (r < 0)
			return ERR(m, -r);
		m->video.codec = FFMP4_V_AVC1;
		m->video.width = v.width;
		m->video.height = v.height;
		break;
	}

	case BOX_ALAC:
		if (m->codec_conf.len != 0)
			break;
		if (NULL == ffstr_dup(&m->codec_conf, sbox.ptr, sbox.len))
			return ERR(m, MP4_ESYS);
		m->codec = FFMP4_ALAC;
		break;

	case BOX_ESDS: {
		struct mp4_esds esds;
		r = mp4_esds(sbox.ptr, sbox.len, &esds);
		if (r < 0)
			return ERR(m, -r);

		switch (esds.type) {
		case DEC_MPEG4_AUDIO:
			m->codec = FFMP4_AAC;
			break;
		case DEC_MPEG1_AUDIO:
			m->codec = FFMP4_MPEG1;
			break;
		}

		if (m->codec == 0)
			return ERR(m, MP4_EACODEC);

		if (m->codec_conf.len != 0)
			break;
		if (NULL == ffstr_dup(&m->codec_conf, esds.conf, esds.conflen))
			return ERR(m, MP4_ESYS);

		m->aac_brate = esds.avg_brate;
		break;
	}

	case BOX_STSZ:
		r = mp4_stsz(sbox.ptr, sbox.len, NULL);
		if (r < 0)
			return ERR(m, -r);
		if (NULL == _ffarr_alloc(&m->sktab, r, sizeof(struct seekpt)))
			return ERR(m, MP4_ESYS);
		r = mp4_stsz(sbox.ptr, sbox.len, (void*)m->sktab.ptr);
		if (r < 0)
			return ERR(m, -r);
		m->sktab.len = r;
		break;

	case BOX_STTS:
		ffstr_dup(&m->stts, sbox.ptr, sbox.len);
		break;

	case BOX_STSC:
		ffstr_dup(&m->stsc, sbox.ptr, sbox.len);
		break;

	case BOX_STCO:
	case BOX_CO64:
		r = mp4_stco(sbox.ptr, sbox.len, GET_TYPE(box->type), NULL);
		if (r < 0)
			return ERR(m, -r);
		if (NULL == _ffarr_alloc(&m->chunktab, r, sizeof(int64)))
			return ERR(m, MP4_ESYS);
		r = mp4_stco(sbox.ptr, sbox.len, GET_TYPE(box->type), (void*)m->chunktab.ptr);
		if (r < 0)
			return ERR(m, -r);
		m->chunktab.len = r;
		break;

	case BOX_ILST_DATA: {
		const struct mp4_box *parent = &m->boxes[m->ictx - 1];
		r = mp4_ilst_data(sbox.ptr, sbox.len, GET_TYPE(parent->type) - _BOX_TAG, &m->tagval, m->tagbuf, sizeof(m->tagbuf));
		if (r == 0)
			break;
		else if (r == -1) {
			m->state = R_TRKTOTAL;
			return 0;
		}

		m->tag = r;
		m->state = R_BOXSKIP;
		if (r == FFMMTAG_TRACKNO)
			m->state = R_TRKTOTAL;
		return FFMP4_RTAG;
	}

	case BOX_ITUNES_NAME:
		m->itunes_smpb = ffstr_eqcz(&sbox, "iTunSMPB");
		break;

	case BOX_ITUNES_DATA:
		if (!m->itunes_smpb)
			break;
		m->itunes_smpb = 0;
		mp4_itunes_smpb(sbox.ptr, sbox.len, &m->enc_delay, &m->end_padding);
		break;
	}

	return 0;
}

static int mp4_meta_closed(ffmp4 *m)
{
	int r;

	if (m->fmt.format == 0)
		return MP4_ENOFMT;

	if (m->chunktab.len == 0) {
		ffmemcpy(m->boxes[++m->ictx].name, "stco", 4);
		return MP4_EDATA;
	}

	int64 rr = mp4_stts((void*)m->sktab.ptr, m->sktab.len, m->stts.ptr, m->stts.len);
	if (rr < 0) {
		ffmemcpy(m->boxes[++m->ictx].name, "stts", 4);
		return -rr;
	}
	m->total_samples = rr;
	m->total_samples = ffmin(m->total_samples - (m->enc_delay + m->end_padding), m->total_samples);

	if (0 != (r = mp4_stsc((void*)m->sktab.ptr, m->sktab.len, m->stsc.ptr, m->stsc.len))) {
		ffmemcpy(m->boxes[++m->ictx].name, "stsc", 4);
		return -r;
	}

	uint stts_cnt = ffint_ntoh32(m->stts.ptr);
	if (stts_cnt <= 2) {
		const struct seekpt *pt = (void*)m->sktab.ptr;
		m->frame_samples = pt[1].audio_pos;
	}

	ffstr_free(&m->stts);
	ffstr_free(&m->stsc);

	if (m->codec == FFMP4_ALAC || m->codec == FFMP4_AAC)
		m->out = m->codec_conf.ptr,  m->outlen = m->codec_conf.len;
	return 0;
}

/* MP4 reading algorithm:
. Gather box header (size + name)
. Check size, gather box64 header if needed
. Search box within the current context
. Skip box if unknown
. Check flags, gather fullbox data, or minimum size, or the whole box, if needed
. Process box
*/
int ffmp4_read(ffmp4 *m)
{
	struct mp4_box *box;
	int r;

	for (;;) {

	box = &m->boxes[m->ictx];

	switch (m->state) {

	case R_BOXSKIP:
		if (box->type & F_WHOLE) {
			//m->data points to the next box
		} else {
			if (m->datalen < box->size) {
				m->off += box->size;
				box->size = 0;
				return FFMP4_RSEEK;
			}
			FFARR_SHIFT(m->data, m->datalen, box->size);
			m->off += box->size;
		}

		m->state = R_BOXREAD;

		do {
			r = mp4_box_close(m, &m->boxes[m->ictx]);
			if (r > 0)
				return ERR(m, r);
		} while (r == 0);

		if (m->meta_closed) {
			m->meta_closed = 0;

			if (0 != (r = mp4_meta_closed(m)))
				return ERR(m, r);
			m->state = R_METAFIN;
			return FFMP4_RHDR;
		}
		continue;

	case R_METAFIN:
		ffstr_free(&m->codec_conf);
		m->state = R_DATA;
		return FFMP4_RMETAFIN;

	case R_WHOLEDATA:
		r = ffarr_append_until(&m->buf, m->data, m->datalen, m->whole_data);
		if (r == 0) {
			m->off += m->datalen;
			return FFMP4_RMORE;
		} else if (r == -1)
			return ERR(m, MP4_ESYS);
		FFARR_SHIFT(m->data, m->datalen, r);
		m->off += r;
		m->state = m->nxstate;
		continue;

	case R_BOXREAD:
		m->whole_data = sizeof(struct box);
		m->state = R_WHOLEDATA,  m->nxstate = R_BOX_PARSE;
		continue;

	case R_BOX_PARSE: {
		struct mp4_box *parent = &m->boxes[m->ictx];
		uint sz = m->buf.len;
		m->buf.len = 0;
		box = &m->boxes[m->ictx + 1];
		r = mp4_box_parse(m, parent, box, m->buf.ptr, sz);
		if (r == -1) {
			m->buf.len = sizeof(struct box);
			m->whole_data = sizeof(struct box64);
			m->state = R_WHOLEDATA,  m->nxstate = R_BOX_PARSE;
			continue;
		}
		FF_ASSERT(m->ictx != FFCNT(m->boxes));
		m->ictx++;

		if (r > 0) {
			m->err = r;
			if (r == MP4_EDUPBOX) {
				m->state = R_BOXSKIP;
				return FFMP4_RWARN;
			}
			return FFMP4_RERR;
		}

		uint prio = GET_PRIO(box->type);
		if (prio != 0) {
			if (prio > parent->prio + 1)
				return ERR(m, MP4_EORDER);
			parent->prio = prio;
		}

		uint minsize = GET_MINSIZE(box->type);
		if (box->type & F_FULLBOX)
			minsize += sizeof(struct fullbox);
		if (box->size < minsize)
			return ERR(m, MP4_ESMALL);
		if (box->type & F_WHOLE)
			minsize = box->size;
		if (minsize != 0) {
			m->whole_data = minsize;
			box->size -= minsize;
			m->state = R_WHOLEDATA,  m->nxstate = R_BOXPROCESS;
			continue;
		}

		m->state = R_BOXPROCESS;
		// break
	}

	case R_BOXPROCESS: {
		ffstr sbox;
		ffstr_set2(&sbox, &m->buf);
		m->buf.len = 0;

		if (box->type & F_FULLBOX)
			ffstr_shift(&sbox, sizeof(struct fullbox));

		if (0 != (r = mp4_box_process(m, &sbox)))
			return r;

		if (m->state == R_TRKTOTAL)
			continue;

		if (box->ctx == NULL)
			m->state = R_BOXSKIP;
		else
			m->state = R_BOXREAD;
		continue;
	}


	case R_TRKTOTAL:
		m->state = R_BOXSKIP;
		r = mp4_ilst_trkn(m->buf.ptr, &m->tagval, m->tagbuf, sizeof(m->tagbuf));
		if (r == 0)
			continue;
		m->tag = r;
		return FFMP4_RTAG;


	case R_DATAOK:
		m->state = R_DATA;
		// break

	case R_DATA: {
		if (m->isamp == m->sktab.len - 1)
			return FFMP4_RDONE;
		uint64 off = mp4_data((void*)m->chunktab.ptr, (void*)m->sktab.ptr, m->isamp, &m->frsize, &m->cursample);
		m->isamp++;
		m->whole_data = m->frsize;
		m->state = R_WHOLEDATA,  m->nxstate = R_DATAREAD;
		if (off != m->off) {
			m->off = off;
			return FFMP4_RSEEK;
		}
		continue;
	}

	case R_DATAREAD:
		m->buf.len = 0;
		m->out = m->buf.ptr,  m->outlen = m->frsize;
		m->state = R_DATAOK;

		FFDBG_PRINTLN(10, "fr#%u  size:%L  data-chunk:%u  audio-pos:%U  off:%U"
			, m->isamp - 1, m->outlen, ((struct seekpt*)m->sktab.ptr)[m->isamp - 1].chunk_id, m->cursample, m->off - m->frsize);

		return FFMP4_RDATA;

	case R_ERR:
		return FFMP4_RERR;
	}
	}

	//unreachable
}


int ffmp4_create_aac(ffmp4_cook *m, struct ffmp4_info *info)
{
	if (NULL == ffarr_alloc(&m->buf, 64 * 1024))
		return ERR(m, MP4_ESYS);
	m->info.total_samples = info->total_samples;
	m->info.frame_samples = info->frame_samples;
	m->info.enc_delay = info->enc_delay;
	m->info.bitrate = info->bitrate;
	m->fmt = info->fmt;
	m->chunk_frames = (m->fmt.sample_rate / 2) / m->info.frame_samples;
	m->stream = (m->info.total_samples == 0);
	if (!m->stream) {
		m->info.total_samples += m->info.enc_delay;
		uint64 ts = m->info.total_samples;
		m->info.total_samples = ff_align_ceil(m->info.total_samples, m->info.frame_samples);
		m->info.end_padding = m->info.total_samples - ts;
		m->info.nframes = m->info.total_samples / m->info.frame_samples;
		m->ctx[0] = &mp4_ctx_global[0];
	} else {
		m->ctx[0] = &mp4_ctx_global_stream[0];
	}

	ffs_copy(m->aconf, m->aconf + sizeof(m->aconf), info->conf.ptr, info->conf.len);
	m->aconf_len = info->conf.len;
	return 0;
}

struct ffmp4_tag {
	uint id;
	ffstr val;
};

int ffmp4_addtag(ffmp4_cook *m, uint mmtag, const char *val, size_t val_len)
{
	switch (mmtag) {
	case FFMMTAG_TRACKNO:
	case FFMMTAG_TRACKTOTAL: {
		ushort n;
		if (val_len != ffs_toint(val, val_len, &n, FFS_INT16))
			return 1;
		if (mmtag == FFMMTAG_TRACKNO)
			m->trkn.num = n;
		else
			m->trkn.total = n;
		m->trkn.id = FFMMTAG_TRACKNO;
		return 0;
	}
	}

	if (mmtag == FFMMTAG_DISCNUMBER
		|| NULL == mp4_ilst_find(mmtag))
		return 1;

	if (NULL == _ffarr_grow(&m->tags, 1, 8, sizeof(struct ffmp4_tag)))
		return MP4_ESYS;

	struct ffmp4_tag *t = ffarr_pushT(&m->tags, struct ffmp4_tag);
	t->id = mmtag;
	ffstr_null(&t->val);
	if (NULL == ffstr_dup(&t->val, val, val_len))
		return MP4_ESYS;
	return 0;
}

static void tag_free(struct ffmp4_tag *t)
{
	ffstr_free(&t->val);
}

static struct ffmp4_tag* tags_find(struct ffmp4_tag *t, uint cnt, uint id)
{
	for (uint i = 0;  i != cnt;  i++) {
		if (id == t[i].id)
			return &t[i];
	}
	return NULL;
}

void ffmp4_wclose(ffmp4_cook *m)
{
	ffarr_free(&m->buf);
	ffarr_free(&m->stsz);
	ffarr_free(&m->stco);
	FFSLICE_FOREACH_T(&m->tags, tag_free, struct ffmp4_tag);
	ffslice_free(&m->tags);
}

uint64 ffmp4_wsize(ffmp4_cook *m)
{
	return 64 * 1024 + (m->info.total_samples / m->fmt.sample_rate + 1) * (m->info.bitrate / 8);
}

static const char mp4_ftyp_aac[24] = {
	"M4A " "\0\0\0\0" "M4A " "mp42" "isom" "\0\0\0\0"
};

enum { W_META, W_META_NEXT, W_DATA1, W_DATA, W_MORE, W_STSZ, W_STCO_SEEK, W_STCO, W_DONE,
	W_MDAT_HDR, W_MDAT_SEEK, W_MDAT_SIZE, W_STM_DATA,
};

/** Return size needed for the box. */
static uint _ffmp4_boxsize(ffmp4_cook *m, const struct bbox *b)
{
	uint t = GET_TYPE(b->flags), n = GET_MINSIZE(b->flags);

	switch (t) {
	case BOX_STSC:
	case BOX_STTS:
		n = 64;
		break;

	case BOX_STSZ:
		if (m->stream) {
			n = m->stsz.len;
			break;
		}

		n = mp4_stsz_size(m->info.nframes);
		if (NULL == ffarr_alloc(&m->stsz, n))
			return ERR(m, MP4_ESYS);
		break;

	case BOX_STCO: {
		if (m->stream) {
			n = m->stco.len;
			break;
		}

		uint chunks = m->info.nframes / m->chunk_frames + !!(m->info.nframes % m->chunk_frames);
		n = mp4_stco_size(BOX_STCO, chunks);
		if (NULL == ffarr_alloc(&m->stco, n))
			return ERR(m, MP4_ESYS);
		break;
	}

	case BOX_ILST_DATA:
		if (m->curtag->id == FFMMTAG_TRACKNO) {
			n = mp4_ilst_trkn_data_write(NULL, 0, 0);
			break;
		}
		n = mp4_ilst_data_write(NULL, &m->curtag->val);
		break;

	case BOX_ITUNES_MEAN:
		n = FFSLEN("com.apple.iTunes");
		break;

	case BOX_ITUNES_NAME:
		n = FFSLEN("iTunSMPB");
		break;

	case BOX_ITUNES_DATA:
		n = mp4_itunes_smpb_write(NULL, 0, 0, 0);
		break;
	}

	return n;
}

/** Write box data.
Return -1 if box should be skipped. */
static int _ffmp4_boxdata(ffmp4_cook *m, const struct bbox *b)
{
	uint t = GET_TYPE(b->flags), n, boxoff = m->buf.len, box_data_off;
	struct fullbox *fbox = NULL;
	void *data = ffarr_end(&m->buf) + sizeof(struct box);

	if (b->flags & F_FULLBOX) {
		fbox = data;
		ffmem_zero(fbox, sizeof(struct fullbox));
		data = fbox + 1;
	}
	box_data_off = (char*)data - m->buf.ptr;

	n = GET_MINSIZE(b->flags);
	if (n != 0)
		ffmem_zero(data, n);

	switch (t) {
	case BOX_FTYP:
		n = sizeof(mp4_ftyp_aac);
		ffmemcpy(data, mp4_ftyp_aac, n);
		break;

	case BOX_MDAT:
		m->mdat_off = boxoff;
		if (m->stream)
			m->state = W_MDAT_HDR;
		break;

	case BOX_TKHD:
		n = mp4_tkhd_write((void*)fbox, 1, m->info.total_samples);
		break;

	case BOX_MVHD:
		n = mp4_mvhd_write((void*)fbox, m->fmt.sample_rate, m->info.total_samples);
		break;

	case BOX_MDHD:
		n = mp4_mdhd_write((void*)fbox, m->fmt.sample_rate, m->info.total_samples);
		break;

	case BOX_DREF: {
		struct dref *dref = data;
		ffint_hton32(dref->cnt, 1);
		break;
	}

	case BOX_DREF_URL:
		ffint_hton24(fbox->flags, 1); //"mdat" is in the same file as "moov"
		break;

	case BOX_STSD:
		n = mp4_stsd_write(data);
		break;

	case BOX_HDLR: {
		struct hdlr *hdlr = data;
		ffmemcpy(hdlr->type, "soun", 4);
		n = sizeof(struct hdlr);
		break;
	}

	case BOX_STSD_MP4A:
		n = mp4_asamp_write(data, &m->fmt);
		break;

	case BOX_ESDS: {
		struct mp4_esds esds = {
			.type = DEC_MPEG4_AUDIO,
			.stm_type = 0x15,
			.avg_brate = m->info.bitrate,
			.conf = m->aconf,  .conflen = m->aconf_len,
		};
		n = mp4_esds_write(data, &esds);
		break;
	}

	case BOX_STSC:
		n = mp4_stsc_write(data, m->info.total_samples, m->info.frame_samples, m->fmt.sample_rate / 2);
		break;

	case BOX_STTS:
		n = mp4_stts_write(data, m->info.total_samples, m->info.frame_samples);
		break;

	case BOX_STSZ:
		if (m->stream) {
			ffmemcpy(data, m->stsz.ptr, m->stsz.len);
			n = m->stsz.len;
			break;
		}

		ffmem_zero(data, m->stsz.cap);
		ffmem_zero(m->stsz.ptr, m->stsz.cap);
		n = m->stsz.cap;
		m->stsz_off = box_data_off;
		break;

	case BOX_STCO:
		if (m->stream) {
			ffmemcpy(data, m->stco.ptr, m->stco.len);
			n = m->stco.len;
			break;
		}

		ffmem_zero(data, m->stco.cap);
		ffmem_zero(m->stco.ptr, m->stco.cap);
		n = m->stco.cap;
		m->stco_off = box_data_off;
		break;

	case BOX_CO64:
	case BOX_STSD_ALAC:
	case BOX_STSD_AVC1:
		return -1;

	case BOX_ILST_DATA:
		if (m->curtag->id == FFMMTAG_TRACKNO) {
			n = mp4_ilst_trkn_data_write(data, m->trkn.num, m->trkn.total);
			break;
		}
		n = mp4_ilst_data_write(data, &m->curtag->val);
		break;

	case BOX_ITUNES_MEAN:
		n = ffmem_copycz(data, "com.apple.iTunes") - (char*)data;
		break;

	case BOX_ITUNES_NAME:
		n = ffmem_copycz(data, "iTunSMPB") - (char*)data;
		break;

	case BOX_ITUNES_DATA:
		n = mp4_itunes_smpb_write(data, m->info.total_samples, m->info.enc_delay, m->info.end_padding);
		break;

	default:
		if (t >= _BOX_TAG) {
			uint tag = t - _BOX_TAG;
			if (tag == 0)
				return -1;
			m->curtag = tags_find((void*)m->tags.ptr, m->tags.len, tag);

			if (tag == FFMMTAG_TRACKNO && (m->trkn.num != 0 || m->trkn.total != 0))
				m->curtag = (void*)&m->trkn;

			if (m->curtag == NULL)
				return -1;
		}
	}

	return n;
}

/* MP4 writing algorithm:
If total data length is known in advance:
  . Write MP4 header: "ftyp", "moov" with empty "stco" & "stsz", "mdat" box header.
  . Pass audio frames data and fill "stco" & "stsz" data buffers.
  . After all frames are written, seek back to header and write "stsz" data.
  . Seek to "stco" and write its data.
  . Seek to "mdat" and write its size.
else:
  . Write "ftyp", "mdat" box header with 0 size.
  . Pass audio frames data and fill "stco" & "stsz" data buffers.
  . After all frames are written, write "moov".
  . Seek back to "mdat" and write its size.
*/
int ffmp4_write(ffmp4_cook *m)
{
	for (;;) {
	switch (m->state) {

	case W_META_NEXT:
		if (m->ctx[m->ictx]->flags & F_LAST) {
			if (m->ictx == 0) {
				m->mp4_size += m->buf.len;
				m->out = m->buf.ptr,  m->outlen = m->buf.len;
				m->buf.len = 0;
				m->off += m->outlen;
				m->state = (m->stream) ? W_MDAT_SEEK : W_DATA1;
				return FFMP4_RDATA;
			}

			m->ctx[m->ictx--] = NULL;

			struct box *b = (void*)(m->buf.ptr + m->boxoff[m->ictx]);
			ffint_hton32(b->size, m->buf.len - m->boxoff[m->ictx]);
			continue;
		}
		m->ctx[m->ictx]++;
		m->state = W_META;
		// break

	case W_META: {
		const struct bbox *b = m->ctx[m->ictx];
		char *box;
		uint n;

		// determine box size and reallocate m->buf
		n = _ffmp4_boxsize(m, b);
		n += sizeof(struct box) + sizeof(struct fullbox);
		if (NULL == ffarr_grow(&m->buf, n, 0))
			return ERR(m, MP4_ESYS);

		m->boxoff[m->ictx] = m->buf.len;
		n = _ffmp4_boxdata(m, b);
		if ((int)n == -1) {
			m->state = W_META_NEXT;
			continue;
		}

		box = ffarr_end(&m->buf);
		if (b->flags & F_FULLBOX)
			m->buf.len += mp4_fbox_write(b->type, box, n);
		else
			m->buf.len += mp4_box_write(b->type, box, n);

		if (b->ctx != NULL) {
			FF_ASSERT(m->ictx != FFCNT(m->ctx));
			m->ctx[++m->ictx] = &b->ctx[0];
		} else if (m->state == W_META) { // state may be changed in _ffmp4_boxdata()
			m->state = W_META_NEXT;
		}
		break;
	}

	case W_MDAT_HDR:
		if (NULL == ffarr_alloc(&m->stsz, mp4_stsz_size(0))
			|| NULL == ffarr_alloc(&m->stco, mp4_stco_size(BOX_STCO, 0)))
			return ERR(m, MP4_ESYS);
		ffmem_zero(m->stsz.ptr, m->stsz.cap);
		m->stsz.len = m->stsz.cap;
		ffmem_zero(m->stco.ptr, m->stco.cap);
		m->stco.len = m->stco.cap;

		m->state = W_STM_DATA;
		m->mp4_size += m->buf.len;
		m->out = m->buf.ptr,  m->outlen = m->buf.len;
		m->buf.len = 0;
		m->off += m->outlen;
		return FFMP4_RDATA;

	case W_MDAT_SEEK:
		m->state = W_MDAT_SIZE;
		m->off = m->mdat_off;
		return FFMP4_RSEEK;

	case W_MDAT_SIZE:
		m->buf.len = 0;
		if (NULL == ffarr_growT(&m->buf, 1, 0, struct box))
			return ERR(m, MP4_ESYS);
		mp4_box_write("mdat", m->buf.ptr, m->mdat_size);
		m->out = m->buf.ptr,  m->outlen = sizeof(struct box);
		m->state = W_DONE;
		return FFMP4_RDATA;

	case W_STM_DATA:
		if (m->fin) {
			m->info.total_samples = m->frameno * m->info.frame_samples;
			m->buf.len = 0;
			m->state = W_META_NEXT;
			continue;
		} else if (m->datalen == 0)
			return FFMP4_RMORE;

		if (NULL == ffarr_grow(&m->stsz, sizeof(int), (128 * sizeof(int)) | FFARR_GROWQUARTER)
			|| NULL == ffarr_grow(&m->stco, sizeof(int), (4 * sizeof(int)) | FFARR_GROWQUARTER))
			return ERR(m, MP4_ESYS);
		goto fr;

	case W_DATA1:
		ffarr_free(&m->buf);
		FFSLICE_FOREACH_T(&m->tags, tag_free, struct ffmp4_tag);
		ffslice_free(&m->tags);
		m->state = W_DATA;
		// break

	case W_DATA:
		if (m->fin) {
			if (m->frameno != m->info.nframes)
				return ERR(m, MP4_ENFRAMES);
			m->state = W_STSZ;
			m->off = m->stsz_off;
			return FFMP4_RSEEK;
		} else if (m->datalen == 0)
			return FFMP4_RMORE;

		if (m->frameno == m->info.nframes)
			return ERR(m, MP4_ENFRAMES);

fr:
		m->stsz.len = mp4_stsz_add(m->stsz.ptr, m->datalen);
		m->frameno++;

		if (m->chunk_curframe == 0) {
			if (m->off > (uint)-1)
				return ERR(m, MP4_ECO64);
			m->stco.len = mp4_stco_add(m->stco.ptr, BOX_STCO, m->off);
		}
		m->chunk_curframe = (m->chunk_curframe + 1) % m->chunk_frames;

		FFDBG_PRINTLN(10, "fr#%u  pos:%U  size:%L  off:%U"
			, m->frameno - 1, m->samples, m->datalen, m->off);

		m->samples += m->info.frame_samples;
		m->off += m->datalen;
		m->mdat_size += m->datalen;
		m->out = m->data,  m->outlen = m->datalen;
		m->state = W_MORE;
		return FFMP4_RDATA;

	case W_MORE:
		m->state = (m->stream) ? W_STM_DATA : W_DATA;
		return FFMP4_RMORE;

	case W_STSZ:
		m->out = m->stsz.ptr,  m->outlen = m->stsz.len;
		m->state = W_STCO_SEEK;
		return FFMP4_RDATA;

	case W_STCO_SEEK:
		m->state = W_STCO;
		m->off = m->stco_off;
		return FFMP4_RSEEK;

	case W_STCO:
		m->out = m->stco.ptr,  m->outlen = m->stco.len;
		m->state = W_MDAT_SEEK;
		return FFMP4_RDATA;

	case W_DONE:
		return FFMP4_RDONE;
	}
	}
}
