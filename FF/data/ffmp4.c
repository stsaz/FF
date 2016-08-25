/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/data/mp4-fmt.h>
#include <FF/data/mp4.h>
#include <FF/number.h>
#include <FFOS/error.h>


static uint64 mp4_data(const uint64 *chunks, const struct seekpt *sk, uint isamp, uint *data_size, uint64 *cursample);

static int mp4_box_parse(ffmp4 *m, struct mp4_box *box, const char *data, uint len);
static int mp4_fbox_parse(ffmp4 *m, struct mp4_box *box, const char *data);
static int mp4_box_close(ffmp4 *m, struct mp4_box *box);
static int mp4_meta_closed(ffmp4 *m);


static const char *const mp4_errs[] = {
	"",
	"ftyp: missing",
	"ftyp: unsupported brand",
	"invalid data",
	"box is larger than its parent",
	"too small box",
	"duplicate box",
	"mandatory box not found",
	"no audio format info",
	"trying to add more frames than expected",
	"co64 output isn't supported",
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
	box->size = box->osize - len;

	int idx = mp4_box_find(m->ctxs[m->ictx], pbox->type);
	if (idx != -1) {
		ffmemcpy(box->name, pbox->type, 4);
		box->type = m->ctxs[m->ictx][idx].flags;
		box->ctx = m->ctxs[m->ictx][idx].ctx;
	}

	FFDBG_PRINTLN(10, "%*c%4s (%U)", (size_t)m->ictx, ' ', pbox->type, box->osize);

	struct mp4_box *parent = &m->boxes[m->ictx - 1];
	if (m->ictx != 0 && box->osize > parent->size)
		return MP4_ELARGE;

	if (m->ictx != 0 && idx != -1) {
		if (ffbit_set32(&parent->usedboxes, idx) && !(box->type & F_MULTI))
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

	if (GET_TYPE(box->type) == BOX_MOOV)
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

enum {
	R_BOXREAD, R_BOXSKIP, R_WHOLEDATA, R_FBOX, R_MINSIZE, R_BOXPROCESS, R_TRKTOTAL, R_METAFIN,
	R_DATA, R_DATAREAD, R_DATAOK,
};

void ffmp4_seek(ffmp4 *m, uint64 sample)
{
	int r = mp4_seek((void*)m->sktab.ptr, m->sktab.len, sample);
	if (r == -1)
		return;
	m->isamp = r;
	m->state = R_DATA;
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

	if (0 != (r = mp4_stsc((void*)m->sktab.ptr, m->sktab.len, m->stsc.ptr, m->stsc.len))) {
		ffmemcpy(m->boxes[++m->ictx].name, "stsc", 4);
		return -r;
	}
	ffstr_free(&m->stts);
	ffstr_free(&m->stsc);

	if (m->codec == FFMP4_ALAC || m->codec == FFMP4_AAC)
		m->out = m->codec_conf.ptr,  m->outlen = m->codec_conf.len;
	return 0;
}

int ffmp4_read(ffmp4 *m)
{
	struct mp4_box *box;
	int r;
	ffstr sbox = {0};

	for (;;) {

	box = &m->boxes[m->ictx];

	switch (m->state) {

	case R_BOXSKIP:
		if (box->type & F_WHOLE) {
			//m->data points to the next box
		} else {
			if (m->datalen < box->size) {
				box->size -= m->datalen;
				return FFMP4_RMORE;
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
		if (r == 0)
			return FFMP4_RMORE;
		else if (r == -1)
			return ERR(m, MP4_ESYS);
		FFARR_SHIFT(m->data, m->datalen, r);
		m->off += m->whole_data;
		m->state = m->nxstate;
		continue;

	case R_BOXREAD: {
		uint sz = (m->box64) ? sizeof(struct box64) : sizeof(struct box);

		const struct mp4_box *parent;
		if (m->ictx != 0) {
			parent = &m->boxes[m->ictx - 1];
			if (parent->size < sz) {
				m->ictx--;
				m->state = R_BOXSKIP;
				continue;
			}
		}

		if (m->buf.len < sz) {
			m->state = R_WHOLEDATA,  m->nxstate = R_BOXREAD;
			m->whole_data = sz;
			continue;
		}
		r = mp4_box_parse(m, box, m->buf.ptr, sz);
		if (r == -1) {
			m->box64 = 1;
			continue;
		}
		m->box64 = 0;
		m->buf.len = 0;

		if (r > 0) {
			m->err = r;
			if (r == MP4_EDUPBOX) {
				m->state = R_BOXSKIP;
				return FFMP4_RWARN;
			}
			return FFMP4_RERR;
		}

		m->state = R_FBOX;
		// break
	}

	case R_FBOX:
		if (box->type & F_FULLBOX) {
			if (m->buf.len < sizeof(struct fullbox)) {
				m->state = R_WHOLEDATA,  m->nxstate = R_FBOX;
				m->whole_data = sizeof(struct fullbox);
				continue;
			}
			m->buf.len = 0;

			if (0 != (r = mp4_fbox_parse(m, box, m->buf.ptr)))
				return ERR(m, r);
		}
		m->state = R_MINSIZE;
		// break

	case R_MINSIZE: {
		uint minsize = GET_MINSIZE(box->type);
		if (minsize != 0) {
			if (box->size < minsize)
				return ERR(m, MP4_ESMALL);

			if (!(box->type & F_WHOLE)) {
				if (m->buf.len < minsize) {
					m->state = R_WHOLEDATA,  m->nxstate = R_MINSIZE;
					m->whole_data = minsize;
					continue;
				}
				ffstr_set2(&sbox, &m->buf);
				m->buf.len = 0;
				// m->data points to box+minsize
			}
		}

		if (box->type & F_WHOLE) {
			if (m->buf.len < box->size) {
				m->state = R_WHOLEDATA,  m->nxstate = R_MINSIZE;
				m->whole_data = box->size;
				continue;
			}
			ffstr_set2(&sbox, &m->buf);
			m->buf.len = 0;
		}

		m->state = R_BOXPROCESS;
		// break
	}

	case R_BOXPROCESS:
		break;


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
		m->state = R_DATAREAD;
		if (off != m->off) {
			m->off = off;
			return FFMP4_RSEEK;
		}
		// break
	}

	case R_DATAREAD:
		r = ffarr_append_until(&m->buf, m->data, m->datalen, m->frsize);
		if (r == 0)
			return FFMP4_RMORE;
		else if (r == -1)
			return ERR(m, MP4_ESYS);
		m->buf.len = 0;
		FFARR_SHIFT(m->data, m->datalen, r);
		m->off += m->frsize;
		m->out = m->buf.ptr,  m->outlen = m->frsize;
		m->state = R_DATAOK;

		FFDBG_PRINTLN(10, "fr#%u  size:%L  data-chunk:%u  audio-pos:%U  off:%U"
			, m->isamp - 1, m->outlen, ((struct seekpt*)m->sktab.ptr)[m->isamp - 1].chunk_id, m->cursample, m->off - m->frsize);

		return FFMP4_RDATA;
	}

	// R_BOXPROCESS:

	if (!m->ftyp) {
		m->ftyp = 1;
		if (GET_TYPE(box->type) != BOX_FTYP)
			return ERR(m, MP4_ENOFTYP);
	}

	switch (GET_TYPE(box->type)) {

	case BOX_FTYP:
		break;

	case BOX_STSD_ALAC:
	case BOX_STSD_MP4A:
		mp4_asamp(sbox.ptr, &m->fmt);
		break;

	case BOX_ALAC:
		if (m->codec_conf.len != 0)
			break;
		if (NULL == ffstr_copy(&m->codec_conf, sbox.ptr, sbox.len))
			return ERR(m, MP4_ESYS);
		m->codec = FFMP4_ALAC;
		break;

	case BOX_ESDS: {
		struct mp4_esds esds;
		r = mp4_esds(sbox.ptr, sbox.len, &esds);
		if (r < 0)
			return ERR(m, -r);

		if (esds.type != DEC_MPEG4_AUDIO)
			return ERR(m, MP4_EDATA);

		if (m->codec_conf.len != 0)
			break;
		if (NULL == ffstr_copy(&m->codec_conf, esds.conf, esds.conflen))
			return ERR(m, MP4_ESYS);

		m->aac_brate = esds.avg_brate;
		m->codec = FFMP4_AAC;
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
		ffstr_copy(&m->stts, sbox.ptr, sbox.len);
		break;

	case BOX_STSC:
		ffstr_copy(&m->stsc, sbox.ptr, sbox.len);
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
			continue;
		}

		m->tag = r;
		m->state = R_BOXSKIP;
		if (r == FFMMTAG_TRACKNO)
			m->state = R_TRKTOTAL;
		return FFMP4_RTAG;
	}
	}

	if (GET_MINSIZE(box->type) != 0 && !(box->type & F_WHOLE))
		box->size -= GET_MINSIZE(box->type);

	if (box->ctx == NULL)
		m->state = R_BOXSKIP;
	else {
		m->state = R_BOXREAD;
		m->ctxs[++m->ictx] = box->ctx;
	}
	}

	//unreachable
}


int ffmp4_create_aac(ffmp4_cook *m, const ffpcm *fmt, const ffstr *conf, uint64 total_samples, uint frame_samples)
{
	if (NULL == ffarr_alloc(&m->buf, 64 * 1024))
		return ERR(m, MP4_ESYS);

	m->fmt = *fmt;
	m->info.nframes = total_samples / frame_samples + !!(total_samples % frame_samples);
	m->info.frame_samples = frame_samples;
	m->info.total_samples = total_samples;

	ffs_copy(m->aconf, m->aconf + sizeof(m->aconf), conf->ptr, conf->len);
	m->aconf_len = conf->len;

	m->ctx[0] = &mp4_ctx_global[0];
	return 0;
}

struct ffmp4_tag {
	uint id;
	ffstr val;
};

int ffmp4_addtag(ffmp4_cook *m, uint mmtag, const char *val, size_t val_len)
{
	if (mmtag == FFMMTAG_TRACKNO || mmtag == FFMMTAG_TRACKTOTAL || mmtag == FFMMTAG_DISCNUMBER
		|| NULL == mp4_ilst_find(mmtag))
		return 1;

	if (NULL == _ffarr_grow(&m->tags, 1, 8, sizeof(struct ffmp4_tag)))
		return MP4_ESYS;

	struct ffmp4_tag *t = _ffarr_push(&m->tags, sizeof(struct ffmp4_tag));
	t->id = mmtag;
	if (NULL == ffstr_copy(&t->val, val, val_len))
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
	FFARR2_FREE_ALL(&m->tags, tag_free, struct ffmp4_tag);
}

static const char mp4_ftyp_aac[24] = {
	"M4A " "\0\0\0\0" "M4A " "mp42" "isom" "\0\0\0\0"
};

enum { W_META, W_DATA1, W_DATA, W_MORE, W_STSZ, W_STCO_SEEK, W_STCO, W_DONE };

int ffmp4_write(ffmp4_cook *m)
{
	for (;;) {
	switch (m->state) {

	case W_META: {
		const struct bbox *b = m->ctx[m->ictx];
		char *box;
		void *data;
		uint n;

		if (b->type[0] == '\0') {
			if (m->ictx == 0) {
				m->state = W_DATA1;
				m->out = m->buf.ptr,  m->outlen = m->buf.len;
				m->buf.len = 0;
				m->off += m->outlen;
				return FFMP4_RDATA;
			}

			m->ictx--;

			struct box *b = (void*)(m->buf.ptr + m->boxoff[m->ictx]);
			ffint_hton32(b->size, m->buf.len - m->boxoff[m->ictx]);

			m->ctx[m->ictx]++;
			continue;
		}

		uint t = GET_TYPE(b->flags);

		// determine box size and reallocate m->buf
		n = GET_MINSIZE(b->flags);
		switch (t) {
		case BOX_STSC:
		case BOX_STTS:
			n = 64;
			break;

		case BOX_STSZ:
			n = mp4_stsz_size(m->info.nframes);
			if (NULL == ffarr_alloc(&m->stsz, n))
				return ERR(m, MP4_ESYS);
			break;

		case BOX_STCO: {
			m->chunk_frames = (m->fmt.sample_rate / 2) / m->info.frame_samples;
			uint chunks = m->info.nframes / m->chunk_frames + !!(m->info.nframes % m->chunk_frames);
			n = mp4_stco_size(BOX_STCO, chunks);
			if (NULL == ffarr_alloc(&m->stco, n))
				return ERR(m, MP4_ESYS);
			break;

		case BOX_ILST_DATA:
			n = mp4_ilst_data_write(NULL, &m->curtag->val);
			break;
		}
		}

		n += sizeof(struct box) + sizeof(struct fullbox);
		if (NULL == ffarr_grow(&m->buf, n, 0))
			return ERR(m, MP4_ESYS);

		m->boxoff[m->ictx] = m->buf.len;
		data = ffarr_end(&m->buf) + sizeof(struct box);
		struct fullbox *fbox = NULL;
		if (b->flags & F_FULLBOX) {
			fbox = data;
			ffmem_zero(fbox, sizeof(struct fullbox));
			data = fbox + 1;
		}

		n = GET_MINSIZE(b->flags);
		if (n != 0)
			ffmem_zero(data, n);

		switch (t) {
		case BOX_FTYP:
			n = sizeof(mp4_ftyp_aac);
			ffmemcpy(data, mp4_ftyp_aac, n);
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
			ffmem_zero(data, m->stsz.cap);
			ffmem_zero(m->stsz.ptr, m->stsz.cap);
			n = m->stsz.cap;
			m->stsz_off = (char*)data - m->buf.ptr;
			break;

		case BOX_STCO:
			ffmem_zero(data, m->stco.cap);
			ffmem_zero(m->stco.ptr, m->stco.cap);
			n = m->stco.cap;
			m->stco_off = (char*)data - m->buf.ptr;
			break;

		case BOX_CO64:
		case BOX_STSD_ALAC:
			goto next;

		case BOX_ILST_DATA:
			n = mp4_ilst_data_write(data, &m->curtag->val);
			break;

		default:
			if (t >= _BOX_TAG) {
				uint tag = t - _BOX_TAG;
				if (tag == 0)
					goto next;
				m->curtag = tags_find((void*)m->tags.ptr, m->tags.len, tag);
				if (m->curtag == NULL)
					goto next;
			}
		}

		box = ffarr_end(&m->buf);
		if (b->flags & F_FULLBOX)
			m->buf.len += mp4_fbox_write(b->type, box, n);
		else
			m->buf.len += mp4_box_write(b->type, box, n);

		if (b->ctx != NULL)
			m->ctx[++m->ictx] = &b->ctx[0];
		else {
next:
			m->ctx[m->ictx]++;
		}
		break;
	}

	case W_DATA1:
		ffarr_free(&m->buf);
		FFARR2_FREE_ALL(&m->tags, tag_free, struct ffmp4_tag);
		m->state = W_DATA;
		// break

	case W_DATA:
		if (m->fin) {
			if (m->frameno != m->info.nframes)
				return ERR(m, MP4_ENFRAMES);
			m->state = W_STSZ;
			m->off = m->stsz_off;
			return FFMP4_RSEEK;
		}

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
		m->out = m->data,  m->outlen = m->datalen;
		m->state = W_MORE;
		return FFMP4_RDATA;

	case W_MORE:
		m->state = W_DATA;
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
		m->state = W_DONE;
		return FFMP4_RDATA;

	case W_DONE:
		return FFMP4_RDONE;
	}
	}
}
