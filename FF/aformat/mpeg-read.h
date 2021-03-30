/** ff: MPEG frame reader
2017,2021, Simon Zolin
*/

/*
ffmpg_rinit
ffmpg_rclose
ffmpg_readframe
ffmpg_rseek
ffmpg_rerrstr
ffmpg_bitrate
ffmpg_fmt
ffmpg_setsize
ffmpg_length
ffmpg_cursample
ffmpg_isvbr
*/

/** MPEG frame reader */
typedef struct ffmpgr {
	ffuint state;
	ffuint err;

	ffpcmex fmt;
	ffmpg_hdr firsthdr;
	ffvec buf; //holds 1 incomplete frame
	ffuint64 seek_sample
		, total_samples
		, total_len //msec
		, cur_sample;
	ffuint frsamps;
	ffuint64 dataoff //offset of the first MPEG header
		, total_size
		, off;
	struct ffmpg_info xing;
	struct ffmpg_lame lame;
	ffuint delay;
	ffuint frno;

	ffvec buf2;
	ffuint bytes_skipped;

	ffuint options; //enum FFMPG_O
	ffuint fr_body :1
		, lostsync :1
		, frame2 :1
		, duration_inaccurate :1
		;
} ffmpgr;

enum FFMPG_O {
	FFMPG_O_NOXING = 1, //don't parse Xing and LAME tags
	FFMPG_O_ID3V1 = 2,
	FFMPG_O_ID3V2 = 4,
	FFMPG_O_APETAG = 8,
};

#define _MPGR_MAX_NOSYNC  (256*1024)
#define _MPGR_DEC_DELAY  528
#define _MPGR_SEEK_SKIPFRAMES  4

/** Get the last error as a string */
static inline const char* ffmpg_rerrstr(ffmpgr *m)
{
	static const char *const _ffmpg_errs[] = {
		"PCM format error", //FFMPG_EFMT
		"ID3v1 tag data error", //FFMPG_EID31DATA
		"ID3v2 tag data error", //FFMPG_EID32DATA
		"ID3v2 tag error", //FFMPG_EID32
		"APE tag error", //FFMPG_EAPETAG
		"seek sample is too large", //FFMPG_ESEEK
		"can't find a valid MPEG frame", //FFMPG_ENOFRAME
		"unrecognized data before frame header", //FFMPG_ESYNC
	};
	int e = m->err;
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

#define ffmpg_fmt(m)  ((m)->fmt)

#define ffmpg_setsize(m, size)  (m)->total_size = (size)

#define ffmpg_length(m)  ((m)->total_samples)

/** Get frame's sample position */
static inline ffuint64 ffmpg_cursample(ffmpgr *m)
{
	return m->cur_sample - m->frsamps;
}

#define ffmpg_isvbr(m)  ((m)->xing.vbr)

static inline void ffmpg_rinit(ffmpgr *m)
{
}

static inline void ffmpg_rclose(ffmpgr *m)
{
	ffvec_free(&m->buf);
	ffvec_free(&m->buf2);
}

/** Get stream bitrate */
static inline ffuint ffmpg_bitrate(ffmpgr *m)
{
	if (m->total_size == 0 || !ffmpg_isvbr(m))
		return ffmpg_hdr_bitrate(&m->firsthdr);
	return ffpcm_brate(m->total_size - m->dataoff, m->total_samples, m->fmt.sample_rate);
}

static inline void ffmpg_rseek(ffmpgr *m, ffuint64 sample)
{
	if (m->total_size == 0 || m->total_samples == 0)
		return;
	m->seek_sample = sample;
	m->buf.len = 0;
	m->state = 3/*R_SEEK*/;
}

/**
Total samples number is estimated from:
 . 'frames' value from Xing/VBRI tag
 . encoder/decoder delays from LAME tag
 . TLEN frame from ID3v2
 . bitrate from the first header and total stream size */
static int _mpgread_streaminfo(ffmpgr *m, const char *p, ffuint datalen, const char *next)
{
	ffuint n;
	int r;
	const ffmpg_hdr *h = (void*)next;

	if (!(m->options & FFMPG_O_NOXING)
		&& 0 < (r = ffmpg_xing_parse(&m->xing, p, datalen))) {

		m->total_samples = m->xing.frames * ffmpg_hdr_frame_samples(h);

		if (0 < ffmpg_lame_parse(&m->lame, p + r, datalen - r)) {
			n = m->lame.enc_delay + _MPGR_DEC_DELAY + 1;
			if (m->lame.enc_padding != 0)
				n += m->lame.enc_padding - (_MPGR_DEC_DELAY + 1);
			m->total_samples -= ffmin(n, m->total_samples);
			m->delay = m->lame.enc_delay + _MPGR_DEC_DELAY + 1;
		}
		FFDBG_PRINTLN(5, "Xing frames:%u  total_samples:%U  LAME total_samples:%U  enc_delay:%u  enc_padding:%u"
			, m->xing.frames, m->xing.frames * ffmpg_hdr_frame_samples(h)
			, m->total_samples, m->lame.enc_delay, m->lame.enc_padding);
		return 1;
	}

	if (!(m->options & FFMPG_O_NOXING)
		&& 0 < ffmpg_vbri(&m->xing, p, datalen)) {
		m->total_samples = m->xing.frames * ffmpg_hdr_frame_samples(h);
		return 2;
	}

	h = (void*)p;

	if (m->total_len == 0 && m->total_size != 0)
		m->total_len = (m->total_size - m->dataoff) * 1000 / (ffmpg_hdr_bitrate(h) / 8);

	m->total_samples = ffpcm_samples(m->total_len, ffmpg_hdr_sample_rate(h));
	m->duration_inaccurate = 1;
	return 0;
}

/** Get the whole frame.
Return 0 on success.  input points to the next frame */
static int _mpgread_frame(ffmpgr *m, ffstr *input, ffvec *buf)
{
	ffmpg_hdr *h;
	int r;

	if (m->fr_body)
		goto body;

	struct ffbuf_gather d = {0};
	ffstr_set2(&d.data, input);
	d.ctglen = sizeof(ffmpg_hdr);

	while (FFBUF_DONE != (r = ffbuf_gather((ffarr*)buf, &d))) {

		if (r == FFBUF_ERR) {
			m->err = FFMPG_ESYS;
			return FFMPG_RERR;

		} else if (r == FFBUF_MORE) {
			m->bytes_skipped += input->len;
			if (m->bytes_skipped > _MPGR_MAX_NOSYNC) {
				m->err = FFMPG_ENOFRAME;
				return FFMPG_RERR;
			}

			m->off += input->len;
			return FFMPG_RMORE;
		}

		if (NULL != (h = ffmpg_framefind(buf->ptr, buf->len, (m->firsthdr.sync1 != 0) ? &m->firsthdr : NULL))) {
			if ((void*)h != buf->ptr)
				m->lostsync = 1;
			d.off = (char*)h - (char*)buf->ptr + 1;
		}
	}
	m->off += input->len - d.data.len;
	ffstr_set2(input, &d.data);

	m->bytes_skipped = 0;

body:
	h = (void*)buf->ptr;
	r = ffarr_append_until((ffarr*)buf, input->ptr, input->len, ffmpg_hdr_framelen(h));
	if (r == -1) {
		m->err = FFMPG_ESYS;
		return FFMPG_RERR;
	} else if (r == 0) {
		m->fr_body = 1;
		m->off += input->len;
		return FFMPG_RMORE;
	}
	ffstr_shift(input, r);
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
Note: in such a case, the next header should be searched in the first frame's data, but instead the whole frame is skipped now */
static int _mpgread_frame2(ffmpgr *m, ffstr *input, ffvec *buf)
{
	int r;
	for (;;) {
		if (!m->frame2) {
			r = _mpgread_frame(m, input, buf);
			if (r != 0 && !(r == FFMPG_RWARN && m->err == FFMPG_ESYNC))
				return r;

			m->frame2 = 1;
		}

		r = _mpgread_frame(m, input, &m->buf2);
		if (r == FFMPG_RWARN) {
			goto next;

		} else if (r == FFMPG_RMORE) {
			if (buf->cap == 0) {
				ffvec t = {};
				if (0 == ffvec_add(&t, buf->ptr, buf->len, 1)) {
					m->err = FFMPG_ESYS;
					return FFMPG_RERR;
				}
				*buf = t;
			}
			return FFMPG_RMORE;

		} else if (r != 0)
			return r;

		if ((ffint_ntoh32(buf->ptr) & MPG_HDR_CONST_MASK) != (ffint_ntoh32(m->buf2.ptr) & MPG_HDR_CONST_MASK)) {
			goto next;
		}

		if (0 == ffvec_add(buf, m->buf2.ptr, m->buf2.len, 1)) {
			m->err = FFMPG_ESYS;
			return FFMPG_RERR;
		}
		ffvec_free(&m->buf2);
		m->frame2 = 0;
		return 0;

next:
		ffvec_free(buf);
		ffarr_acq(buf, &m->buf2);
		ffvec_null(&m->buf2);
	}
	//unreachable
}

/** Convert sample number to stream offset (in bytes) based on:
 . seek table from Xing tag
 . 'total samples' and 'total size' values (linear) */
static ffuint64 _mpgr_getseekoff(ffmpgr *m)
{
	ffuint64 sk = ffmax((int64)m->seek_sample - ffmpg_hdr_frame_samples(&m->firsthdr), 0);
	ffuint64 datasize = m->total_size - m->dataoff;
	if (m->xing.vbr && m->xing.toc[98] != 0)
		return m->dataoff + ffmpg_xing_seekoff(m->xing.toc, sk, m->total_samples, datasize);
	else
		return m->dataoff + datasize * sk / m->total_samples;
}

/** Read MPEG frame.  Parse Xing tag.
Return enum FFMPG_R */
static inline int ffmpg_readframe(ffmpgr *m, ffstr *input, ffstr *frame)
{
	enum {
		R_HDR, R_HDR2, R_FRAME2, R_SEEK=3, R_FR, R_FR2
	};
	int r;

	for (;;) {
		switch (m->state) {

		case R_SEEK:
			m->buf.len = 0;
			if (m->seek_sample >= m->total_samples) {
				m->err = FFMPG_ESEEK;
				return FFMPG_RERR;
			}
			m->seek_sample = ffmax((int64)m->seek_sample - _MPGR_SEEK_SKIPFRAMES * ffmpg_hdr_frame_samples(&m->firsthdr), 0) + m->delay;
			m->off = _mpgr_getseekoff(m);
			m->cur_sample = m->seek_sample;
			m->state = R_FRAME2;
			return FFMPG_RSEEK;

		case R_HDR: {
			if (0 != (r = _mpgread_frame2(m, input, &m->buf)))
				return r;

			const ffmpg_hdr *h = (void*)m->buf.ptr;
			ffstr next;
			ffstr_set(frame, m->buf.ptr, ffmpg_hdr_framelen(h));
			ffstr_set(&next, m->buf.ptr, m->buf.len);
			ffstr_shift(&next, ffmpg_hdr_framelen(h));

			r = _mpgread_streaminfo(m, (char*)frame->ptr, frame->len, next.ptr);
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
			if (0 != (r = _mpgread_frame2(m, input, &m->buf)))
				return r;

			ffstr_set(frame, m->buf.ptr, m->buf.len);
			m->state = R_FR2;
			r = FFMPG_RFRAME;
			goto fr;

		case R_FR2:
			ffstr_set(frame, m->buf.ptr, m->buf.len);
			ffstr_shift(frame, ffmpg_hdr_framelen((void*)m->buf.ptr));
			m->frsamps = ffmpg_hdr_frame_samples((void*)frame->ptr);
			m->buf.len = 0;
			m->state = R_FR;
			r = FFMPG_RFRAME;
			goto fr;

		case R_FR:
			input->len = ffmin(input->len, m->total_size - m->off);
			if (m->off == m->total_size)
				return FFMPG_RDONE;
			if (0 != (r = _mpgread_frame(m, input, &m->buf)))
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
	m->frno++;
	FFDBG_PRINTLN(10, "frame #%u  samples:%u  size:%u  bitrate:%u  offset:%xU"
		, m->frno, (ffuint)m->frsamps, (ffuint)frame->len, ffmpg_hdr_bitrate((void*)frame->ptr), m->off - frame->len);
	return r;
}

#undef _MPGR_MAX_NOSYNC
#undef _MPGR_DEC_DELAY
#undef _MPGR_SEEK_SKIPFRAMES
