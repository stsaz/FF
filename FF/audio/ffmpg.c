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
	SEEK_SKIPFRAMES = 4,
};


static int mpg_id31(ffmpgfile *m);
static int mpg_id32(ffmpgfile *m);
static int _ffmpg_apetag(ffmpgfile *m, ffarr *buf);
static int _ffmpg_streaminfo(ffmpgr *m, const char *data, uint len, const char *next);
static int _ffmpg_frame(ffmpgr *m, ffarr *buf);
static int _ffmpg_frame2(ffmpgr *m, ffarr *buf);


static const char *const _ffmpg_errs[] = {
	"PCM format error",
	"ID3v1 tag data error",
	"ID3v2 tag data error",
	"ID3v2 tag error",
	"APE tag error",
	"seek sample is too large",
	"can't find a valid MPEG frame",
	"unrecognized data before frame header",
};

static const char* _ffmpg_errstr(int e)
{
	switch (e) {
	case FFMPG_ESYS:
		return fferr_strp(fferr_last());
	}
	if (e < FFMPG_EFMT)
		return "";
	e -= FFMPG_EFMT;
	FF_ASSERT((uint)e < FFCNT(_ffmpg_errs));
	return _ffmpg_errs[e];
}

const char* ffmpg_rerrstr(ffmpgr *m)
{
	return _ffmpg_errstr(m->err);
}

const char* ffmpg_ferrstr(ffmpgfile *m)
{
	m->rdr.err = m->err;
	return ffmpg_rerrstr(&m->rdr);
}

const char* ffmpg_werrstr(ffmpgw *m)
{
	return _ffmpg_errstr(m->err);
}


void ffmpg_fopen(ffmpgfile *m)
{
	ffid3_parseinit(&m->id32tag);
	ffmpg_rinit(&m->rdr);
}

void ffmpg_fclose(ffmpgfile *m)
{
	ffarr_free(&m->tagval);
	if (m->is_apetag)
		ffapetag_parse_fin(&m->apetag);
	if (m->is_id32tag)
		ffid3_parsefin(&m->id32tag);
	ffarr_free(&m->buf);
	ffmpg_rclose(&m->rdr);
}

enum { I_START, I_FR,
	I_ID3V2, I_FTRTAGS_CHECK, I_FTRTAGS, I_ID31, I_APE2, I_APE2_MORE, I_TAG_SKIP };

/** Convert sample number to stream offset (in bytes) based on:
 . seek table from Xing tag
 . 'total samples' and 'total size' values (linear) */
static FFINL uint64 mpg_getseekoff(ffmpgr *m)
{
	uint64 sk = ffmax((int64)m->seek_sample - ffmpg_hdr_frame_samples(&m->firsthdr), 0);
	uint64 datasize = m->total_size - m->dataoff;
	if (m->xing.vbr && m->xing.toc[98] != 0)
		return m->dataoff + ffmpg_xing_seekoff(m->xing.toc, sk, m->total_samples, datasize);
	else
		return m->dataoff + datasize * sk / m->total_samples;
}

static FFINL int mpg_id31(ffmpgfile *m)
{
	if (m->buf.len < sizeof(ffid31))
		return FFMPG_RDONE;

	int i = ffid31_parse(&m->id31tag, ffarr_end(&m->buf) - sizeof(ffid31), sizeof(ffid31));

	switch (i) {
	case FFID3_RNO:
		break;

	case FFID3_RDONE:
		m->buf.len -= sizeof(ffid31);
		m->rdr.total_size -= sizeof(ffid31);
		m->rdr.off -= sizeof(ffid31);
		ffarr_free(&m->tagval);
		break;

	case FFID3_RDATA:
		m->tag = m->id31tag.field;
		if (m->codepage == 0) {
			ffarr_free(&m->tagval);
			ffarr_set(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len);

		} else if (0 == ffutf8_strencode(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len, m->codepage)) {
			m->err = FFMPG_EID31DATA;
			return FFMPG_RWARN;
		}
		return FFMPG_RID31;

	default:
		FF_ASSERT(0);
	}

	ffmem_tzero(&m->id31tag);
	return FFMPG_RDONE;
}

static FFINL int _ffmpg_apetag(ffmpgfile *m, ffarr *buf)
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
		m->rdr.total_size -= m->apetag.size;
		continue;

	case FFAPETAG_RTAG:
		m->tag = m->apetag.tag;
		ffstr_set2(&m->tagval, &m->apetag.val);
		return FFMPG_RAPETAG;

	case FFAPETAG_RSEEK:
		m->rdr.off -= m->apetag.size;
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

static FFINL int mpg_id32(ffmpgfile *m)
{
	int i;
	size_t len;

	for (;;) {

	len = m->input.len;
	i = ffid3_parse(&m->id32tag, m->input.ptr, &len);
	ffarr_shift(&m->input, len);

	switch (i) {
	case FFID3_RNO:
	case FFID3_RDONE:
		ffarr_free(&m->tagval);
		return FFMPG_RDONE;

	case FFID3_RMORE:
		return FFMPG_RMORE;

	case FFID3_RHDR:
		m->rdr.dataoff = sizeof(ffid3_hdr) + ffid3_size(&m->id32tag.h);
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
			m->err = FFMPG_EID32DATA;
			return FFMPG_RWARN;
		}

		if (m->id32tag.frame == FFID3_LENGTH && m->id32tag.data.len != 0) {
			uint64 dur;
			if (m->id32tag.data.len == ffs_toint(m->id32tag.data.ptr, m->id32tag.data.len, &dur, FFS_INT64))
				m->rdr.total_len = dur;
		}
		return FFMPG_RID32;

	case FFID3_RERR:
		ffarr_free(&m->tagval);
		m->state = I_FTRTAGS_CHECK;
		m->err = FFMPG_EID32;
		return FFMPG_RWARN;

	default:
		FF_ASSERT(0);
	}
	}
	//unreachable
}


enum { R_HDR, R_HDR2, R_FRAME2, R_SEEK, R_FR, R_FR2 };

void ffmpg_rinit(ffmpgr *m)
{
	m->state = R_HDR;
}

void ffmpg_rclose(ffmpgr *m)
{
	ffarr_free(&m->buf);
	ffarr_free(&m->buf2);
}

uint ffmpg_bitrate(ffmpgr *m)
{
	if (m->total_size == 0 || !ffmpg_isvbr(m))
		return ffmpg_hdr_bitrate(&m->firsthdr);
	return ffpcm_brate(m->total_size - m->dataoff, m->total_samples, m->fmt.sample_rate);
}

void ffmpg_rseek(ffmpgr *m, uint64 sample)
{
	if (m->total_size == 0 || m->total_samples == 0)
		return;
	m->seek_sample = sample;
	m->buf.len = 0;
	m->input.len = 0;
	m->state = R_SEEK;
}

/**
Total samples number is estimated from:
 . 'frames' value from Xing/VBRI tag
 . encoder/decoder delays from LAME tag
 . TLEN frame from ID3v2
 . bitrate from the first header and total stream size */
static int _ffmpg_streaminfo(ffmpgr *m, const char *p, uint datalen, const char *next)
{
	uint n;
	int r;
	const ffmpg_hdr *h = (void*)next;

	if (!(m->options & FFMPG_O_NOXING)
		&& 0 < (r = ffmpg_xing_parse(&m->xing, p, datalen))) {

		m->total_samples = m->xing.frames * ffmpg_hdr_frame_samples(h);

		if (0 < ffmpg_lame_parse(&m->lame, p + r, datalen - r)) {
			n = m->lame.enc_delay + DEC_DELAY + 1;
			if (m->lame.enc_padding != 0)
				n += m->lame.enc_padding - (DEC_DELAY + 1);
			m->total_samples -= ffmin(n, m->total_samples);
			m->delay = m->lame.enc_delay + DEC_DELAY + 1;
		}
		return 1;
	}

	if (!(m->options & FFMPG_O_NOXING)
		&& 0 < ffmpg_vbri(&m->xing, p, datalen)) {
		m->total_samples = m->xing.frames * ffmpg_hdr_frame_samples(h);
		return 2;
	}

	h = (void*)p;

	if (m->total_len == 0)
		m->total_len = (m->total_size - m->dataoff) * 1000 / (ffmpg_hdr_bitrate(h) / 8);

	m->total_samples = ffpcm_samples(m->total_len, ffmpg_hdr_sample_rate(h));
	return 0;
}

/** Get the whole frame.
Return 0 on success.  m->input points to the next frame. */
static int _ffmpg_frame(ffmpgr *m, ffarr *buf)
{
	ffmpg_hdr *h;
	int r;

	if (m->fr_body)
		goto body;

	struct ffbuf_gather d = {0};
	ffstr_set2(&d.data, &m->input);
	d.ctglen = sizeof(ffmpg_hdr);

	while (FFBUF_DONE != (r = ffbuf_gather(buf, &d))) {

		if (r == FFBUF_ERR) {
			m->err = FFMPG_ESYS;
			return FFMPG_RERR;

		} else if (r == FFBUF_MORE) {
			m->bytes_skipped += m->input.len;
			if (m->bytes_skipped > MAX_NOSYNC) {
				m->err = FFMPG_ENOFRAME;
				return FFMPG_RERR;
			}

			m->off += m->input.len;
			return FFMPG_RMORE;
		}

		if (NULL != (h = ffmpg_framefind(buf->ptr, buf->len, (m->firsthdr.sync1 != 0) ? &m->firsthdr : NULL))) {
			if ((void*)h != buf->ptr)
				m->lostsync = 1;
			d.off = (char*)h - buf->ptr + 1;
		}
	}
	m->off += m->input.len - d.data.len;
	ffstr_set2(&m->input, &d.data);

	m->bytes_skipped = 0;

body:
	h = (void*)buf->ptr;
	r = ffarr_append_until(buf, m->input.ptr, m->input.len, ffmpg_hdr_framelen(h));
	if (r == -1) {
		m->err = FFMPG_ESYS;
		return FFMPG_RERR;
	} else if (r == 0) {
		m->fr_body = 1;
		m->off += m->input.len;
		return FFMPG_RMORE;
	}
	ffarr_shift(&m->input, r);
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
static int _ffmpg_frame2(ffmpgr *m, ffarr *buf)
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

int ffmpg_readframe(ffmpgr *m, ffstr *frame)
{
	int r;

	for (;;) {
	switch (m->state) {

	case R_SEEK:
		m->buf.len = 0;
		if (m->seek_sample >= m->total_samples) {
			m->err = FFMPG_ESEEK;
			return FFMPG_RERR;
		}
		m->seek_sample = ffmax((int64)m->seek_sample - SEEK_SKIPFRAMES * ffmpg_hdr_frame_samples(&m->firsthdr), 0) + m->delay;
		m->off = mpg_getseekoff(m);
		m->cur_sample = m->seek_sample;
		m->state = R_FRAME2;
		return FFMPG_RSEEK;

	case R_HDR: {
		if (0 != (r = _ffmpg_frame2(m, &m->buf)))
			return r;

		const ffmpg_hdr *h = (void*)m->buf.ptr;
		ffstr next;
		ffstr_set(frame, m->buf.ptr, ffmpg_hdr_framelen(h));
		ffarr_setshift(&next, m->buf.ptr, m->buf.len, ffmpg_hdr_framelen(h));

		r = _ffmpg_streaminfo(m, (char*)frame->ptr, frame->len, next.ptr);
		if (r != 0) {
			m->dataoff = m->off - next.len;
			m->firsthdr = *(ffmpg_hdr*)next.ptr;
			r = FFMPG_RXING;
			m->state = R_HDR2;
		} else {
			m->dataoff = m->off - m->buf.len;
			m->firsthdr = *(ffmpg_hdr*)frame->ptr;
			r = FFMPG_RHDR;
			m->state = R_FR2;
		}

		m->fmt.sample_rate = ffmpg_hdr_sample_rate(&m->firsthdr);
		m->fmt.channels = ffmpg_hdr_channels(&m->firsthdr);
		return r;
	}

	case R_HDR2:
		m->state = R_FR2;
		return FFMPG_RHDR;

	case R_FRAME2:
		if (0 != (r = _ffmpg_frame2(m, &m->buf)))
			return r;

		m->state = R_FR2;
		r = FFMPG_RFRAME;
		goto fr;

	case R_FR2:
		ffarr_setshift(frame, m->buf.ptr, m->buf.len, ffmpg_hdr_framelen((void*)m->buf.ptr));
		m->frsamps = ffmpg_hdr_frame_samples((void*)frame->ptr);
		m->buf.len = 0;
		m->state = R_FR;
		r = FFMPG_RFRAME;
		goto fr;

	case R_FR:
		m->input.len = ffmin(m->input.len, m->total_size - m->off);
		if (m->off == m->total_size)
			return FFMPG_RDONE;
		if (0 != (r = _ffmpg_frame(m, &m->buf)))
			return r;
		ffstr_set(frame, m->buf.ptr, m->buf.len);
		m->buf.len = 0;
		m->frsamps = ffmpg_hdr_frame_samples((void*)frame->ptr);
		r = FFMPG_RFRAME;
		goto fr;
	}
	}

	return FFMPG_RERR;

fr:
	m->cur_sample += m->frsamps;
	FFDBG_PRINTLN(10, "frame #%u  size:%L  bitrate:%u  offset:%xU"
		, m->frno++, frame->len, ffmpg_hdr_bitrate((void*)frame->ptr), m->off - frame->len);
	return r;
}


/* Read MPEG file:
 . parse ID3v2
 . parse ID3v1
 . parse APE tag
 . parse Xing, LAME tags
 . read frames...
*/
int ffmpg_read(ffmpgfile *m)
{
	int i, r;

	for (;;) {
	switch (m->state) {

	case I_START:
		m->rdr.options = m->options & FFMPG_O_NOXING;

		if (m->options & FFMPG_O_ID3V2) {
			m->is_id32tag = 1;
			m->state = I_ID3V2;
			continue;
		}
		m->state = I_FTRTAGS_CHECK;
		continue;

	case I_ID3V2:
		if (FFMPG_RDONE != (i = mpg_id32(m)))
			return i;

		m->state = I_FTRTAGS_CHECK;
		continue;

	case I_FTRTAGS_CHECK:
		if (m->is_id32tag) {
			m->is_id32tag = 0;
			ffid3_parsefin(&m->id32tag);
			ffmem_tzero(&m->id32tag);
		}

		if (m->options & (FFMPG_O_ID3V1 | FFMPG_O_APETAG)) {
			m->state = I_FTRTAGS;
			m->rdr.off = m->rdr.total_size - ffmin(MPG_FTRTAGS_CHKSIZE, m->rdr.total_size);
			return FFMPG_RSEEK;
		}
		m->state = I_FR;
		continue;

	case I_FTRTAGS:
		r = ffarr_append_until(&m->buf, m->input.ptr, m->input.len, ffmin(MPG_FTRTAGS_CHKSIZE, m->rdr.total_size));
		if (r < 0) {
			m->err = FFMPG_ESYS;
			return FFMPG_RERR;
		} else if (r == 0) {
			m->rdr.off += m->input.len;
			return FFMPG_RMORE;
		}
		ffarr_shift(&m->input, r);
		m->rdr.off += r;
		m->state = I_ID31;
		// break

	case I_ID31:
		if ((m->options & FFMPG_O_ID3V1) && FFMPG_RDONE != (i = mpg_id31(m)))
			return i;
		m->state = I_APE2;
		continue;

	case I_APE2_MORE:
		ffarr_free(&m->buf);
		ffstr_set(&m->buf, m->input.ptr, m->input.len);
		m->state = I_APE2;
		// break

	case I_APE2:
		if ((m->options & FFMPG_O_APETAG) && 0 != (r = _ffmpg_apetag(m, &m->buf)))
			return r;
		m->state = I_TAG_SKIP;
		continue;

	case I_TAG_SKIP:
		m->buf.len = 0;
		m->rdr.off = m->rdr.dataoff;
		m->state = I_FR;
		return FFMPG_RSEEK;


	case I_FR:
		if (m->input.len != 0) {
			ffmpg_input(&m->rdr, m->input.ptr, m->input.len);
			m->input.len = 0;
		}
		r = ffmpg_readframe(&m->rdr, &m->frame);
		m->err = m->rdr.err;
		return r;
	}
	}
}


const char* ffmpg_errstr(ffmpg *m)
{
	return mpg123_errstr(m->err);
}

int ffmpg_open(ffmpg *m, uint delay, uint options)
{
	int r;
	uint opt = (options & FFMPG_O_INT16) ? 0 : MPG123_FORCE_FLOAT;
	if (0 != (r = mpg123_open(&m->m123, opt))) {
		m->err = r;
		return FFMPG_RERR;
	}
	m->fmt.format = (options & FFMPG_O_INT16) ? FFPCM_16 : FFPCM_FLOAT;
	m->fmt.ileaved = 1;
	m->delay_start = delay;
	m->seek = m->delay_start;
	return 0;
}

void ffmpg_close(ffmpg *m)
{
	if (m->m123 != NULL)
		mpg123_free(m->m123);
}

void ffmpg_seek(ffmpg *m, uint64 sample)
{
	mpg123_decode(m->m123, (void*)-1, (size_t)-1, NULL); //reset bufferred data
	m->seek = sample + m->delay_start;
	m->delay_dec = 0;
}

int ffmpg_decode(ffmpg *m)
{
	int r;

	if (!m->fin && m->input.len == 0)
		return FFMPG_RMORE;

	r = mpg123_decode(m->m123, m->input.ptr, m->input.len, (byte**)&m->pcmi);
	m->input.len = 0;
	if (r == 0) {
		m->delay_dec += ffmpg_hdr_frame_samples((void*)m->input.ptr);
		return FFMPG_RMORE;

	} else if (r < 0) {
		m->err = r;
		m->delay_dec = 0;
		return FFMPG_RWARN;
	}

	m->pos = ffmax((int64)m->pos - m->delay_dec, 0);

	if (m->seek != (uint64)-1) {
		FF_ASSERT(m->seek >= m->pos);
		uint skip = m->seek - m->pos;
		if (skip >= (uint)r / ffpcm_size1(&m->fmt))
			return FFMPG_RMORE;

		m->seek = (uint64)-1;
		m->pcmi = (void*)((char*)m->pcmi + skip * ffpcm_size1(&m->fmt));
		r -= skip * ffpcm_size1(&m->fmt);
		m->pos += skip;
	}

	m->pcmlen = r;
	m->pos += m->pcmlen / ffpcm_size1(&m->fmt) - m->delay_start;
	return FFMPG_RDATA;
}


enum { W_ID32, W_ID32_DONE, W_ID31,
	W_FRAME1, W_FRAME, W_XING_SEEK, W_XING, W_LAMETAG, W_DONE };

void ffmpg_winit(ffmpgw *m)
{
	m->state = W_ID32;
	m->xing.vbr_scale = -1;

	ffid31_init(&m->id31);
	m->min_meta = 1000;
}

void ffmpg_wclose(ffmpgw *m)
{
	ffarr_free(&m->buf);
	ffarr_free(&m->id3.buf);
}

int ffmpg_addtag(ffmpgw *m, uint id, const char *val, size_t vallen)
{
	if ((m->options & FFMPG_WRITE_ID3V2)
		&& 0 == ffid3_add(&m->id3, id, val, vallen))
		return m->err = FFMPG_ESYS,  FFMPG_RERR;

	if (m->options & FFMPG_WRITE_ID3V1)
		ffid31_add(&m->id31, id, val, vallen);

	return 0;
}

int ffmpg_writeframe(ffmpgw *m, const char *fr, uint frlen, ffstr *data)
{
	switch (m->state) {
	case W_FRAME1:
	case W_FRAME:
		if (m->fin)
			m->state = W_ID31;
		else if (frlen == 0)
			return FFMPG_RMORE;
		break;
	}

	switch (m->state) {

	case W_ID32:
		if (m->options & FFMPG_WRITE_ID3V2) {
			ffid3_flush(&m->id3);
			if (m->min_meta > m->id3.buf.len
				&& 0 != ffid3_padding(&m->id3, m->min_meta - m->id3.buf.len))
				return m->err = FFMPG_ESYS,  FFMPG_RERR;
			ffid3_fin(&m->id3);
			ffstr_set2(data, &m->id3.buf);
			m->off = m->id3.buf.len;
			m->state = W_ID32_DONE;
			return FFMPG_RID32;
		}
		// break

	case W_ID32_DONE:
		ffarr_free(&m->id3.buf);
		m->state = W_FRAME1;
		// break

	case W_FRAME1:
		m->state = W_FRAME;
		if (m->options & FFMPG_WRITE_XING) {
			ffmpg_hdr xing = *(ffmpg_hdr*)fr;
			uint hlen = ffmpg_hdr_framelen(&xing);
			if (NULL == ffarr_alloc(&m->buf, hlen))
				return m->err = FFMPG_ESYS,  FFMPG_RERR;
			ffmem_zero(m->buf.ptr, hlen);
			ffmemcpy(m->buf.ptr, &xing, sizeof(ffmpg_hdr));
			m->buf.len = hlen;
			ffstr_set2(data, &m->buf);
			return FFMPG_RDATA;
		}
		break;

	case W_FRAME:
		break;

	case W_ID31:
		m->state = W_XING_SEEK;
		if (m->options & FFMPG_WRITE_ID3V1) {
			ffstr_set(data, &m->id31, sizeof(m->id31));
			return FFMPG_RID31;
		}
		// break

	case W_XING_SEEK:
		if (m->lametag) {
			m->state = W_LAMETAG;
			return FFMPG_RSEEK;
		}
		if (!(m->options & FFMPG_WRITE_XING))
			return FFMPG_RDONE;
		m->state = W_XING;
		return FFMPG_RSEEK;

	case W_XING:
		ffmpg_xing_write(&m->xing, m->buf.ptr);
		ffstr_set2(data, &m->buf);
		m->state = W_DONE;
		return FFMPG_RDATA;

	case W_LAMETAG:
		ffstr_set(data, fr, frlen);
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


enum {
	CPY_START, CPY_GATHER,
	CPY_ID32_HDR, CPY_ID32_DATA,
	CPY_DATA, CPY_FR1, CPY_FR, CPY_FR_OUT,
	CPY_FTRTAGS_SEEK, CPY_FTRTAGS, CPY_FTRTAGS_OUT,
};

void ffmpg_copy_close(ffmpgcopy *m)
{
	ffarr_free(&m->buf);
	ffmpg_rclose(&m->rdr);
	ffmpg_wclose(&m->writer);
}

/* MPEG copy:
. Read and return ID3v2 (FFMPG_RID32)
. Read the first frame (FFMPG_RHDR)
  User may call ffmpg_copy_seek() now.
. If input is seekable:
  . Seek input to the end (FFMPG_RSEEK), read ID3v1
  . Seek input to the needed audio position (FFMPG_RSEEK)
. Return empty Xing frame (FFMPG_RFRAME)
. Read and return MPEG frames (FFMPG_RFRAME) until:
  . User calls ffmpg_copy_fin()
  . Or the end of audio data is reached
. Return ID3v1 (FFMPG_RID31)
. Seek output to Xing tag offset (FFMPG_ROUTSEEK)
. Write the complete Xing tag (FFMPG_RFRAME)
*/
int ffmpg_copy(ffmpgcopy *m, ffstr *output)
{
	int r;
	ffstr fr;

	for (;;) {
	switch (m->state) {

	case CPY_START:
		ffmpg_rinit(&m->rdr);

		ffmpg_winit(&m->writer);
		m->writer.options = m->options & FFMPG_WRITE_XING;

		if (m->options & FFMPG_WRITE_ID3V2) {
			m->gsize = sizeof(ffid3_hdr);
			m->state = CPY_GATHER,  m->gstate = CPY_ID32_HDR;
			continue;
		}
		m->state = CPY_DATA,  m->gstate = CPY_FR1;
		continue;

	case CPY_GATHER:
		r = ffarr_append_until(&m->buf, m->input.ptr, m->input.len, m->gsize);
		switch (r) {
		case 0:
			return FFMPG_RMORE;
		case -1:
			return m->rdr.err = FFMPG_ESYS,  FFMPG_RERR;
		}
		ffarr_shift(&m->input, r);
		m->state = m->gstate;
		continue;


	case CPY_ID32_HDR:
		if (ffid3_valid((void*)m->buf.ptr)) {
			m->gsize = ffid3_size((void*)m->buf.ptr);
			m->rdr.dataoff = sizeof(ffid3_hdr) + m->gsize;
			m->rdr.off += m->rdr.dataoff;
			m->wdataoff = m->rdr.dataoff;
			m->state = CPY_ID32_DATA;
			ffstr_set2(output, &m->buf);
			m->buf.len = 0;
			return FFMPG_RID32;
		}

		m->rdr.dataoff = 0;
		m->wdataoff = 0;
		ffmpg_input(&m->rdr, m->buf.ptr, m->buf.len);
		m->buf.len = 0;
		m->state = CPY_FR1;
		continue;

	case CPY_ID32_DATA:
		if (m->gsize == 0) {
			m->state = CPY_DATA,  m->gstate = CPY_FR1;
			continue;
		}

		if (m->input.len == 0)
			return FFMPG_RMORE;

		r = ffmin(m->input.len, m->gsize);
		m->gsize -= r;
		ffstr_set(output, m->input.ptr, r);
		ffarr_shift(&m->input, r);
		return FFMPG_RID32;


	case CPY_FTRTAGS_SEEK:
		if (m->rdr.total_size == 0) {
			m->state = CPY_DATA,  m->gstate = CPY_FR;
			continue;
		}

		m->gsize = ffmin64(MPG_FTRTAGS_CHKSIZE, m->rdr.total_size);
		m->state = CPY_GATHER,  m->gstate = CPY_FTRTAGS;
		m->off = ffmin64(m->rdr.total_size - MPG_FTRTAGS_CHKSIZE, m->rdr.total_size);
		return FFMPG_RSEEK;

	case CPY_FTRTAGS: {
		const void *h = m->buf.ptr + m->buf.len - sizeof(ffid31);
		if (m->buf.len >= sizeof(ffid31) && ffid31_valid(h)) {
			if (m->options & FFMPG_WRITE_ID3V1)
				ffmemcpy(&m->id31, h, sizeof(ffid31));
			m->rdr.total_size -= sizeof(ffid31);
		}

		m->state = CPY_DATA,  m->gstate = CPY_FR;
		m->buf.len = 0;
		m->off = mpg_getseekoff(&m->rdr);
		return FFMPG_RSEEK;
	}

	case CPY_FTRTAGS_OUT:
		m->writer.fin = 1;
		m->state = CPY_FR_OUT;
		if (m->id31.tag[0] != '\0') {
			ffstr_set(output, &m->id31, sizeof(ffid31));
			return FFMPG_RID31;
		}
		continue;


	case CPY_DATA:
		if (m->input.len == 0)
			return FFMPG_RMORE;
		ffmpg_input(&m->rdr, m->input.ptr, m->input.len);
		m->input.len = 0;
		m->state = m->gstate;
		continue;

	case CPY_FR:
	case CPY_FR1:
		r = ffmpg_readframe(&m->rdr, &fr);
		switch (r) {

		case FFMPG_RMORE:
			m->gstate = m->state;
			m->state = CPY_DATA;
			continue;

		case FFMPG_RSEEK:
			m->off = m->rdr.off;
			m->gstate = m->state;
			m->state = CPY_DATA;
			return FFMPG_RSEEK;

		case FFMPG_RDONE:
			m->state = CPY_FTRTAGS_OUT;
			continue;

		case FFMPG_RXING:
			m->writer.xing.vbr = m->rdr.xing.vbr;
			return FFMPG_RXING;

		case FFMPG_RHDR:
			m->state = CPY_FTRTAGS_SEEK;
			return FFMPG_RHDR;

		case FFMPG_RFRAME:
			m->state = CPY_FR_OUT;
			continue;
		}
		return r;

	case CPY_FR_OUT:
		r = ffmpg_writeframe(&m->writer, fr.ptr, fr.len, output);
		switch (r) {

		case FFMPG_RDONE:
			return FFMPG_RDONE;

		case FFMPG_RDATA:
			if (!m->writer.fin)
				m->state = CPY_FR;
			return FFMPG_RFRAME;

		case FFMPG_RERR:
			m->rdr.err = m->writer.err;
			return FFMPG_RERR;

		case FFMPG_RSEEK:
			m->off = m->wdataoff + ffmpg_wseekoff(&m->writer);
			return FFMPG_ROUTSEEK;
		}
		FF_ASSERT(0);
		return FFMPG_RERR;

	}
	}
}

void ffmpg_copy_seek(ffmpgcopy *m, uint64 sample)
{
	ffmpg_rseek(&m->rdr, sample);
}

void ffmpg_copy_fin(ffmpgcopy *m)
{
	m->state = CPY_FTRTAGS_OUT;
}
