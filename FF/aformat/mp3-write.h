/** ff: .mp3 writer
2017,2021, Simon Zolin
*/

/*
ffmpg_winit
ffmpg_wclose
ffmpg_addtag
ffmpg_writeframe
ffmpg_werrstr
ffmpg_wseekoff
ffmpg_wframes
*/

enum FFMPG_ENC_OPT {
	FFMPG_WRITE_ID3V1 = 1,
	FFMPG_WRITE_ID3V2 = 2,
	FFMPG_WRITE_XING = 4,
};

/** .mp3 writer. */
typedef struct ffmpgw {
	ffuint state;
	ffuint options; //enum FFMPG_ENC_OPT
	ffuint off;
	struct ffmpg_info xing;
	ffvec buf;
	ffuint fin :1;
	ffuint lametag :1; //set before passing LAME tag data

	ffid3_cook id3;
	ffid31 id31;
	ffuint min_meta;
} ffmpgw;

static inline void ffmpg_winit(ffmpgw *m)
{
	m->xing.vbr_scale = -1;

	ffid31_init(&m->id31);
	m->min_meta = 1000;
}

static inline void ffmpg_wclose(ffmpgw *m)
{
	ffvec_free(&m->buf);
	ffarr_free(&m->id3.buf);
}

static inline const char* ffmpg_werrstr(ffmpgw *m)
{
	(void)m;
	return "not enough memory";
}

static inline int ffmpg_addtag(ffmpgw *m, ffuint id, const char *val, ffsize vallen)
{
	if ((m->options & FFMPG_WRITE_ID3V2)
		&& 0 == ffid3_add(&m->id3, id, val, vallen))
		return FFMPG_RERR;

	if (m->options & FFMPG_WRITE_ID3V1)
		ffid31_add(&m->id31, id, val, vallen);

	return 0;
}

/*
. Return ID3v2 (FFMPG_RID32)
. Return next frame (FFMPG_RDATA)
. Return ID3v1 (FFMPG_RID31)
. Seek output to Xing tag offset (FFMPG_RSEEK)
. Return the complete Xing tag (FFMPG_RDATA)
*/
static inline int ffmpg_writeframe(ffmpgw *m, const char *fr, ffuint frlen, ffstr *data)
{
	enum {
		W_ID32, W_ID32_DONE, W_ID31,
		W_FRAME1, W_FRAME, W_XING_SEEK, W_XING, W_LAMETAG, W_DONE
	};

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
				return FFMPG_RERR;
			ffid3_fin(&m->id3);
			ffstr_set2(data, &m->id3.buf);
			m->off = m->id3.buf.len;
			m->state = W_ID32_DONE;
			return FFMPG_RID32;
		}
		// fallthrough

	case W_ID32_DONE:
		ffarr_free(&m->id3.buf);
		m->state = W_FRAME1;
		// fallthrough

	case W_FRAME1:
		m->state = W_FRAME;
		if (m->options & FFMPG_WRITE_XING) {
			ffmpg_hdr xing = *(ffmpg_hdr*)fr;
			ffuint hlen = ffmpg_hdr_framelen(&xing);
			if (NULL == ffvec_alloc(&m->buf, hlen, 1))
				return FFMPG_RERR;
			ffmem_zero(m->buf.ptr, hlen);
			ffmem_copy(m->buf.ptr, &xing, sizeof(ffmpg_hdr));
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
		// fallthrough

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

#define ffmpg_wseekoff(m)  ((m)->off)
#define ffmpg_wframes(m)  ((m)->xing.frames)
