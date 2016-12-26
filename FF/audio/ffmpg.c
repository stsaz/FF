/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/mpeg.h>
#include <FF/audio/mp3lame.h>
#include <FF/mtags/id3.h>
#include <FF/string.h>
#include <FF/number.h>
#include <FF/data/utf8.h>
#include <FFOS/error.h>


enum {
	MAX_NOSYNC = 256 * 1024,
	MPG_FTRTAGS_CHKSIZE = 1000, //number of bytes at the end of file to check for ID3v1 and APE tag (initial check)
	DEC_DELAY = 528,
};


static int mpg_id31(ffmpg *m);
static int mpg_id32(ffmpg *m);
static int _ffmpg_apetag(ffmpg *m, ffarr *buf);
static int _ffmpg_streaminfo(ffmpg *m, const char *data, uint len);
static int _ffmpg_frame(ffmpg *m, ffarr *buf);
static int _ffmpg_frame2(ffmpg *m, ffarr *buf);


static const char *const _ffmpg_errs[] = {
	"PCM format error",
	"tags error",
	"APE tag error",
	"seek sample is too large",
	"can't find a valid MPEG frame",
	"unrecognized data before frame header",
};

const char* ffmpg_errstr(ffmpg *m)
{
	switch (m->err) {
	case FFMPG_EMPG123:
		return mpg123_errstr(m->e);

	case FFMPG_ESYS:
		return fferr_strp(fferr_last());
	}
	if (m->err < FFMPG_EFMT)
		return "";
	m->err -= FFMPG_EFMT;
	FF_ASSERT(m->err < FFCNT(_ffmpg_errs));
	return _ffmpg_errs[m->err];
}


enum { R_FRAME2, R_SEEK, R_FR, R_FROK };

void ffmpg_init(ffmpg *m)
{
	ffid3_parseinit(&m->id32tag);
	m->state2 = R_FRAME2;
}

void ffmpg_close(ffmpg *m)
{
	ffarr_free(&m->tagval);
	if (m->is_apetag)
		ffapetag_parse_fin(&m->apetag);
	if (m->is_id32tag)
		ffid3_parsefin(&m->id32tag);
	ffarr_free(&m->buf);
	ffarr_free(&m->buf2);

	if (m->m123 != NULL)
		mpg123_free(m->m123);
}

enum { I_START, I_FIRST, I_INIT, I_FR,
	I_ID3V2, I_ID31_CHECK, I_FTRTAGS, I_ID31, I_APE2, I_APE2_MORE, I_TAG_SKIP };

void ffmpg_seek(ffmpg *m, uint64 sample)
{
	if (m->total_size == 0 || m->total_samples == 0)
		return;
	if (m->skip_samples != 0)
		m->skip_samples = 0;
	m->seek_sample = sample;
	m->buf.len = 0;

	if (m->state == I_FR) {
		mpg123_decode(m->m123, (void*)-1, (size_t)-1, NULL); //reset bufferred data
		m->frame.len = 0;
	}

	m->state2 = R_SEEK;
}

/** Convert sample number to stream offset (in bytes) based on:
 . seek table from Xing tag
 . 'total samples' and 'total size' values (linear) */
static FFINL uint64 mpg_getseekoff(ffmpg *m)
{
	if (m->xing.vbr && m->xing.toc[98] != 0)
		return m->dataoff + ffmpg_xing_seekoff(m->xing.toc, m->seek_sample, m->total_samples, m->total_size);
	else
		return m->dataoff + m->total_size * m->seek_sample / m->total_samples;
}

static FFINL int mpg_id31(ffmpg *m)
{
	if (m->buf.len < sizeof(ffid31))
		return FFMPG_RDONE;

	int i = ffid31_parse(&m->id31tag, ffarr_end(&m->buf) - sizeof(ffid31), sizeof(ffid31));

	switch (i) {
	case FFID3_RNO:
		break;

	case FFID3_RDONE:
		m->buf.len -= sizeof(ffid31);
		m->total_size -= sizeof(ffid31);
		m->off -= sizeof(ffid31);
		ffarr_free(&m->tagval);
		break;

	case FFID3_RDATA:
		m->tag = m->id31tag.field;
		if (m->codepage == 0) {
			ffarr_free(&m->tagval);
			ffarr_set(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len);

		} else if (0 == ffutf8_strencode(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len, m->codepage)) {
			m->err = FFMPG_ETAG;
			return FFMPG_RWARN;
		}
		return FFMPG_RTAG;

	default:
		FF_ASSERT(0);
	}

	ffmem_tzero(&m->id31tag);
	return FFMPG_RDONE;
}

static FFINL int _ffmpg_apetag(ffmpg *m, ffarr *buf)
{
	for (;;) {

	size_t len = buf->len;
	int r = ffapetag_parse(&m->apetag, buf->ptr, &len);

	switch (r) {
	case FFAPETAG_RDONE:
	case FFAPETAG_RNO:
		m->is_apetag = 0;
		ffapetag_parse_fin(&m->apetag);
		return 0;

	case FFAPETAG_RFOOTER:
		m->is_apetag = 1;
		m->total_size -= m->apetag.size;
		continue;

	case FFAPETAG_RTAG:
		m->tag = m->apetag.tag;
		ffstr_set2(&m->tagval, &m->apetag.val);
		return FFMPG_RTAG;

	case FFAPETAG_RSEEK:
		m->off -= m->apetag.size;
		m->state = I_APE2_MORE;
		return FFMPG_RSEEK;

	case FFAPETAG_RMORE:
		m->state = I_APE2_MORE;
		return FFMPG_RMORE;

	case FFAPETAG_RERR:
		m->state = I_TAG_SKIP;
		m->err = FFMPG_EAPETAG;
		return FFMPG_RWARN;

	default:
		FF_ASSERT(0);
	}
	}
	//unreachable
}

static FFINL int mpg_id32(ffmpg *m)
{
	int i;
	size_t len;

	for (;;) {

	len = m->datalen;
	i = ffid3_parse(&m->id32tag, m->data, &len);
	m->data += len;
	m->datalen -= len;

	switch (i) {
	case FFID3_RNO:
	case FFID3_RDONE:
		m->is_id32tag = 0;
		ffid3_parsefin(&m->id32tag);
		ffmem_tzero(&m->id32tag);
		ffarr_free(&m->tagval);
		return FFMPG_RDONE;

	case FFID3_RMORE:
		return FFMPG_RMORE;

	case FFID3_RHDR:
		m->dataoff = sizeof(ffid3_hdr) + ffid3_size(&m->id32tag.h);
		m->total_size -= m->dataoff;
		m->is_id32tag = 1;
		continue;

	case FFID3_RFRAME:
		switch (m->id32tag.frame) {
		case FFMMTAG_PICTURE:
			m->id32tag.flags &= ~FFID3_FWHOLE;
			break;

		default:
			m->id32tag.flags |= FFID3_FWHOLE;
		}
		continue;

	case FFID3_RDATA:
		if (!(m->id32tag.flags & FFID3_FWHOLE))
			continue;

		m->tag = (m->id32tag.frame < _FFMMTAG_N) ? m->id32tag.frame : 0;
		if (0 > ffid3_getdata(m->id32tag.frame, m->id32tag.data.ptr, m->id32tag.data.len, m->id32tag.txtenc, m->codepage, &m->tagval)) {
			m->err = FFMPG_ETAG;
			return FFMPG_RWARN;
		}

		if (m->id32tag.frame == FFID3_LENGTH && m->id32tag.data.len != 0) {
			uint64 dur;
			if (m->id32tag.data.len == ffs_toint(m->id32tag.data.ptr, m->id32tag.data.len, &dur, FFS_INT64))
				m->total_len = dur;
		}
		return FFMPG_RTAG;

	case FFID3_RERR:
		m->is_id32tag = 0;
		ffid3_parsefin(&m->id32tag);
		ffmem_tzero(&m->id32tag);
		ffarr_free(&m->tagval);
		m->state = I_ID31_CHECK;
		m->err = FFMPG_ETAG;
		return FFMPG_RWARN;

	default:
		FF_ASSERT(0);
	}
	}
	//unreachable
}

/**
Total samples number is estimated from:
 . 'frames' value from Xing/VBRI tag
 . encoder/decoder delays from LAME tag
 . TLEN frame from ID3v2
 . bitrate from the first header and total stream size */
static int _ffmpg_streaminfo(ffmpg *m, const char *p, uint datalen)
{
	const ffmpg_hdr *h = (void*)p;
	uint n;
	int r;

	if (!(m->options & FFMPG_O_NOXING)
		&& 0 < (r = ffmpg_xing_parse(&m->xing, p, datalen))) {

		m->total_samples = m->xing.frames * ffmpg_hdr_frame_samples(h);

		p += r,  datalen -= r;
		if (0 < ffmpg_lame_parse(&m->lame, p, datalen)) {
			n = m->lame.enc_delay + DEC_DELAY + 1;
			if (m->lame.enc_padding != 0)
				n += m->lame.enc_padding - (DEC_DELAY + 1);
			m->total_samples -= ffmin(n, m->total_samples);
			m->skip_samples = m->lame.enc_delay + DEC_DELAY + 1;
		}
		return 0;
	}

	if (!(m->options & FFMPG_O_NOXING)
		&& 0 < ffmpg_vbri(&m->xing, p, datalen)) {
		m->total_samples = m->xing.frames * ffmpg_hdr_frame_samples(h);
		return 0;
	}

	if (m->total_len == 0)
		m->total_len = m->total_size * 1000 / (ffmpg_hdr_bitrate(h) / 8);

	m->total_samples = ffpcm_samples(m->total_len, ffmpg_hdr_sample_rate(h));
	return 0;
}

/** Get the whole frame.
Return 0 on success.  m->data points to the next frame. */
static int _ffmpg_frame(ffmpg *m, ffarr *buf)
{
	ffmpg_hdr *h;
	int r;

	if (m->fr_body)
		goto body;

	struct ffbuf_gather d = {0};
	ffstr_set(&d.data, m->data, m->datalen);
	d.ctglen = sizeof(ffmpg_hdr);

	while (FFBUF_DONE != (r = ffbuf_gather(buf, &d))) {

		if (r == FFBUF_ERR) {
			m->err = FFMPG_ESYS;
			return FFMPG_RERR;

		} else if (r == FFBUF_MORE) {
			m->bytes_skipped += m->datalen;
			if (m->bytes_skipped > MAX_NOSYNC) {
				m->err = FFMPG_ENOFRAME;
				return FFMPG_RERR;
			}

			m->off += m->datalen;
			return FFMPG_RMORE;
		}

		if (NULL != (h = ffmpg_framefind(buf->ptr, buf->len, (m->firsthdr.sync1 != 0) ? &m->firsthdr : NULL))) {
			if ((void*)h != buf->ptr)
				m->lostsync = 1;
			d.off = (char*)h - buf->ptr + 1;
		}
	}
	m->off += m->datalen - d.data.len;
	m->data = d.data.ptr;
	m->datalen = d.data.len;

	m->bytes_skipped = 0;

body:
	h = (void*)buf->ptr;
	r = ffarr_append_until(buf, m->data, m->datalen, ffmpg_hdr_framelen(h));
	if (r == -1) {
		m->err = FFMPG_ESYS;
		return FFMPG_RERR;
	} else if (r == 0) {
		m->fr_body = 1;
		m->off += m->datalen;
		return FFMPG_RMORE;
	}
	FFARR_SHIFT(m->data, m->datalen, r);
	m->off += r;
	m->fr_body = 0;

	if (m->lostsync) {
		m->lostsync = 0;
		m->err = FFMPG_ESYNC;
		return FFMPG_RWARN;
	}
	return 0;
};

/** Get 2 consecutive frames.
If there's a gap between the frames, continue search.
Note: in such a case, the next header should be searched in the first frame's data, but instead the whole frame is skipped now. */
static int _ffmpg_frame2(ffmpg *m, ffarr *buf)
{
	int r;
	for (;;) {
		if (!m->frame2) {
			r = _ffmpg_frame(m, buf);
			if (r != 0 && !(r == FFMPG_RWARN && m->err == FFMPG_ESYNC))
				return r;

			m->frame2 = 1;
		}

		r = _ffmpg_frame(m, &m->buf2);
		if (r == FFMPG_RWARN) {
			goto next;

		} else if (r == FFMPG_RMORE) {
			if (buf->cap == 0
				&& NULL == ffarr_copy(buf, buf->ptr, buf->len)) {
				m->err = FFMPG_ESYS;
				return FFMPG_RERR;
			}
			return FFMPG_RMORE;

		} else if (r != 0)
			return r;

		if ((ffint_ntoh32(buf->ptr) & MPG_HDR_CONST_MASK) != (ffint_ntoh32(m->buf2.ptr) & MPG_HDR_CONST_MASK)) {
			goto next;
		}

		if (NULL == ffarr_append(buf, m->buf2.ptr, m->buf2.len)) {
			m->err = FFMPG_ESYS;
			return FFMPG_RERR;
		}
		ffarr_free(&m->buf2);
		m->frame2 = 0;
		return 0;

next:
		ffarr_free(buf);
		ffarr_acq(buf, &m->buf2);
		ffarr_null(&m->buf2);
	}
	//unreachable
}

int ffmpg_readframe(ffmpg *m)
{
	int r;

	switch (m->state2) {

	case R_SEEK:
		m->buf.len = 0;
		if (m->seek_sample >= m->total_samples) {
			m->err = FFMPG_ESEEK;
			return FFMPG_RERR;
		}
		m->off = mpg_getseekoff(m);
		m->cur_sample = m->seek_sample;
		m->state2 = R_FRAME2;
		return FFMPG_RSEEK;

	case R_FRAME2:
		if (0 != (r = _ffmpg_frame2(m, &m->buf)))
			return r;
		ffstr_set(&m->frame, m->buf.ptr, ffmpg_hdr_framelen((void*)m->buf.ptr));

		if (m->firsthdr.sync1 == 0) {
			const ffmpg_hdr *h = (void*)m->frame.ptr;
			m->firsthdr = *h;

			m->fmt.sample_rate = ffmpg_hdr_sample_rate(h);
			m->fmt.channels = ffmpg_hdr_channels(h);
			_ffmpg_streaminfo(m, (char*)h, ffmpg_hdr_framelen(h));

			if (m->dataoff == 0) {
				m->dataoff = m->off - m->buf.len;
				if (m->total_size != 0)
					m->total_size -= m->dataoff;
			}
		}

		m->state2 = R_FROK;
		return FFMPG_RFRAME;

	case R_FROK:
		m->cur_sample += ffmpg_hdr_frame_samples((void*)m->frame.ptr);
		m->state2 = R_FR;
		if (m->buf.len != 0) {
			ffstr_set(&m->frame, m->buf.ptr + m->frame.len, m->buf.len - m->frame.len);
			m->buf.len = 0;
			return FFMPG_RFRAME;
		}
		// break

	case R_FR:
		m->datalen = ffmin(m->datalen, m->dataoff + m->total_size - m->off);
		if (m->off == m->dataoff + m->total_size)
			return FFMPG_RDONE;
		if (0 != (r = _ffmpg_frame(m, &m->buf)))
			return r;
		ffstr_set(&m->frame, m->buf.ptr, m->buf.len);
		m->buf.len = 0;
		m->state2 = R_FROK;
		return FFMPG_RFRAME;
	}

	return FFMPG_RERR;
}


/*
Workflow:
 . parse ID3v2
 . parse ID3v1
 . parse APE tag
 . parse Xing, LAME tags
 . decode frames...
*/
int ffmpg_decode(ffmpg *m)
{
	int i, r;

	for (;;) {
	switch (m->state) {
		case I_ID31_CHECK:
			if (m->options & (FFMPG_O_ID3V1 | FFMPG_O_APETAG)) {
				m->state = I_FTRTAGS;
				m->off = m->dataoff + m->total_size - ffmin(MPG_FTRTAGS_CHKSIZE, m->total_size);
				return FFMPG_RSEEK;
			}
			m->state = I_FIRST;
			continue;

		case I_FTRTAGS:
			r = ffarr_append_until(&m->buf, m->data, m->datalen, ffmin(MPG_FTRTAGS_CHKSIZE, m->total_size));
			if (r < 0) {
				m->err = FFMPG_ESYS;
				return FFMPG_RERR;
			} else if (r == 0) {
				m->off += m->datalen;
				return FFMPG_RMORE;
			}
			FFARR_SHIFT(m->data, m->datalen, r);
			m->off += r;
			m->state = I_ID31;
			// break

		case I_ID31:
			if ((m->options & FFMPG_O_ID3V1) && FFMPG_RDONE != (i = mpg_id31(m)))
				return i;
			m->state = I_APE2;
			continue;

		case I_APE2_MORE:
			ffarr_free(&m->buf);
			ffstr_set(&m->buf, m->data, m->datalen);
			m->state = I_APE2;
			// break

		case I_APE2:
			if ((m->options & FFMPG_O_APETAG) && 0 != (r = _ffmpg_apetag(m, &m->buf)))
				return r;
			m->state = I_TAG_SKIP;
			continue;

		case I_ID3V2:
			if (FFMPG_RDONE != (i = mpg_id32(m)))
				return i;

			m->state = I_ID31_CHECK;
			continue;

		case I_TAG_SKIP:
			m->buf.len = 0;
			m->off = m->dataoff;
			m->state = I_FIRST;
			return FFMPG_RSEEK;


		case I_START:
			if (m->options & FFMPG_O_ID3V2) {
				m->state = I_ID3V2;
				continue;
			}
			m->state = I_ID31_CHECK;
			continue;

	case I_FIRST:
		if (FFMPG_RFRAME != (r = ffmpg_readframe(m)))
			return r;

		m->fmt.format = (m->options & FFMPG_O_INT16) ? FFPCM_16 : FFPCM_FLOAT;
		m->fmt.ileaved = 1;
		m->state = I_INIT;
		return FFMPG_RHDR;

	case I_INIT:
		i = (m->options & FFMPG_O_INT16) ? 0 : MPG123_FORCE_FLOAT;
		if (0 != (r = mpg123_init(&m->m123, i))) {
			m->err = FFMPG_EMPG123;
			m->e = r;
			return FFMPG_RERR;
		}
		m->state = I_FR;
		// break

	case I_FR:
		if (m->frame.len == 0 && FFMPG_RFRAME != (r = ffmpg_readframe(m)))
			return r;
		r = mpg123_decode(m->m123, m->frame.ptr, m->frame.len, (byte**)&m->pcmi);
		m->frame.len = 0;
		if (r == 0) {
			continue;

		} else if (r < 0) {
			m->err = FFMPG_EMPG123;
			m->e = r;
			m->state2 = R_FRAME2;
			return FFMPG_RWARN;
		}
		m->pcmlen = r;
		return FFMPG_RDATA;
	}
	}
}


enum { W_CHK_XING, W_FRAME, W_XING_SEEK, W_XING, W_DONE };

void ffmpg_create_copy(ffmpg_enc *m)
{
	m->state = W_CHK_XING;
}

int ffmpg_writeframe(ffmpg_enc *m, const char *fr, uint frlen, ffstr *data)
{
	switch (m->state) {
	case W_CHK_XING:
	case W_FRAME:
		if (m->fin)
			m->state = (m->have_xing) ? W_XING_SEEK : W_DONE;
		break;
	}

	switch (m->state) {
	case W_CHK_XING:
		m->state = W_FRAME;
		if (0 < ffmpg_xing_parse(&m->xing, fr, frlen)) {
			if (NULL == ffarr_realloc(&m->buf, frlen))
				return m->err = FFMPG_ESYS,  FFMPG_RERR;
			m->have_xing = 1;
			m->xing.frames = 0;
			m->xing.bytes = 0;
			ffmem_zero(m->buf.ptr, frlen);
			ffmemcpy(m->buf.ptr, fr, sizeof(ffmpg_hdr));
			m->buf.len = frlen;
			ffstr_set2(data, &m->buf);
			return FFMPG_RDATA;
		}
		break;

	case W_FRAME:
		break;

	case W_XING_SEEK:
		m->off = 0;
		m->state = W_XING;
		return FFMPG_RSEEK;

	case W_XING:
		ffmpg_xing_write(&m->xing, m->buf.ptr);
		ffstr_set2(data, &m->buf);
		m->state = W_DONE;
		return FFMPG_RDATA;

	case W_DONE:
		return FFMPG_RDONE;
	}

	ffstr_set(data, fr, frlen);
	m->xing.frames++;
	m->xing.bytes += frlen;
	return FFMPG_RDATA;
}
