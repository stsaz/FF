/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/mpeg.h>
#include <FF/audio/id3.h>
#include <FF/string.h>
#include <FF/number.h>
#include <FF/data/utf8.h>
#include <FFOS/error.h>


enum {
	//bits in each MPEG header that must not change across frames within the same stream
	MPG_HDR_CONST_MASK = 0xfffe0c00, // 1111 1111  1111 1110  0000 1100  0000 0000
	MAX_NOSYNC = 256 * 1024,
	MPG_FTRTAGS_CHKSIZE = 1000, //number of bytes at the end of file to check for ID3v1 and APE tag (initial check)
};


static int mpg_id31(ffmpg *m);
static int mpg_id32(ffmpg *m);
static int _ffmpg_streaminfo(ffmpg *m, const char *xing, uint len, uint fr_samps);
static int _ffmpg_frame(ffmpg *m, ffarr *buf);
static int _ffmpg_frame2(ffmpg *m, ffarr *buf);
#ifdef FF_LIBMAD
static int mpg_mad_decode(ffmpg *m);
#endif


static const byte mpg_kbyterate[2][3][16] = {
	{ //MPEG-1
	{ 0,32/8,64/8,96/8,128/8,160/8,192/8,224/8,256/8,288/8,320/8,352/8,384/8,416/8,448/8,0 }, //L1
	{ 0,32/8,48/8,56/8, 64/8, 80/8, 96/8,112/8,128/8,160/8,192/8,224/8,256/8,320/8,384/8,0 }, //L2
	{ 0,32/8,40/8,48/8, 56/8, 64/8, 80/8, 96/8,112/8,128/8,160/8,192/8,224/8,256/8,320/8,0 }, //L3
	},
	{ //MPEG-2
	{ 0,32/8,48/8,56/8,64/8,80/8,96/8,112/8,128/8,144/8,160/8,176/8,192/8,224/8,256/8,0 }, //L1
	{ 0, 8/8,16/8,24/8,32/8,40/8,48/8, 56/8, 64/8, 80/8, 96/8,112/8,128/8,144/8,160/8,0 }, //L2
	{ 0, 8/8,16/8,24/8,32/8,40/8,48/8, 56/8, 64/8, 80/8, 96/8,112/8,128/8,144/8,160/8,0 }, //L3
	}
};

uint ffmpg_bitrate(const ffmpg_hdr *h)
{
	return (uint)mpg_kbyterate[h->ver != FFMPG_1][3 - h->layer][h->bitrate] * 8 * 1000;
}

static const ushort mpg_sample_rate[4][3] = {
	{ 44100, 48000, 32000 }, //MPEG-1
	{ 44100/2, 48000/2, 32000/2 }, //MPEG-2
	{ 0, 0, 0 },
	{ 44100/4, 48000/4, 32000/4 }, //MPEG-2.5
};

uint ffmpg_sample_rate(const ffmpg_hdr *h)
{
	return mpg_sample_rate[3 - h->ver][h->sample_rate];
}

static const byte mpg_frsamps[2][3] = {
	{ 384/8, 1152/8, 1152/8 }, //MPEG-1
	{ 384/8, 1152/8, 576/8 }, //MPEG-2
};

uint ffmpg_frame_samples(const ffmpg_hdr *h)
{
	return mpg_frsamps[h->ver != FFMPG_1][3 - h->layer] * 8;
}

const char ffmpg_strchannel[4][8] = {
	"stereo", "joint", "dual", "mono"
};

ffbool ffmpg_valid(const ffmpg_hdr *h)
{
	return (ffint_ntoh16(h) & 0xffe0) == 0xffe0
		&& h->ver != 1
		&& h->layer != 0
		&& h->bitrate != 0 && h->bitrate != 15
		&& h->sample_rate != 3;
}

uint ffmpg_framelen(const ffmpg_hdr *h)
{
	return ffmpg_frame_samples(h)/8 * ffmpg_bitrate(h) / ffmpg_sample_rate(h)
		+ ((h->layer != FFMPG_L1) ? h->padding : h->padding * 4);
}

ffmpg_hdr* ffmpg_framefind(const char *data, size_t len, const ffmpg_hdr *h)
{
	const char *d = data, *end = d + len;

	while (d != end) {
		if ((byte)d[0] != 0xff && NULL == (d = ffs_findc(d, end - d, 0xff)))
			break;

		if ((end - d) >= (ssize_t)sizeof(ffmpg_hdr)
			&& ffmpg_valid((void*)d)
			&& (h == NULL || (ffint_ntoh32(d) & MPG_HDR_CONST_MASK) == (ffint_ntoh32(h) & MPG_HDR_CONST_MASK))) {
			return (void*)d;
		}

		d++;
	}

	return 0;
}


static const byte mpg_xingoffs[2][2] = {
	{17, 32}, //MPEG-1: MONO, 2-CH
	{9, 17}, //MPEG-2
};

static const char _xing[4] = "Xing";
static const char _info[4] = "Info";

static FFINL ffbool mpg_xing_valid(const char *data)
{
	uint i = *(uint*)data, *xing = (uint*)_xing, *info = (uint*)_info;
	return (i == *xing || i == *info);
}

static FFINL uint mpg_xing_size(uint flags)
{
	uint all = 4 + sizeof(int);
	if (flags & FFMPG_XING_FRAMES)
		all += sizeof(int);
	if (flags & FFMPG_XING_BYTES)
		all += sizeof(int);
	if (flags & FFMPG_XING_TOC)
		all += 100;
	if (flags & FFMPG_XING_VBRSCALE)
		all += sizeof(int);
	return all;
}

uint64 ffmpg_xing_seekoff(const byte *toc, uint64 sample, uint64 total_samples, uint64 total_size)
{
	uint i, i1, i2;
	double d;

	FF_ASSERT(sample < total_samples);

	d = sample * 100.0 / total_samples;
	i = (int)d;
	d -= i;
	i1 = toc[i];
	i2 = (i != 99) ? toc[i + 1] : 256;

	return (i1 + (i2 - i1) * d) * total_size / 256.0;
}

int ffmpg_xing_parse(ffmpg_xing *xing, const char *data, size_t *len)
{
	const char *dstart = data;
	if (*len < 8 || !mpg_xing_valid(data))
		return -1;

	ffmemcpy(xing, data, 4);
	data += 4;

	xing->flags = ffint_ntoh32(data);
	data += sizeof(int);
	if (*len < mpg_xing_size(xing->flags))
		return -1;

	if (xing->flags & FFMPG_XING_FRAMES) {
		xing->frames = ffint_ntoh32(data);
		data += sizeof(int);
	}

	if (xing->flags & FFMPG_XING_BYTES) {
		xing->bytes = ffint_ntoh32(data);
		data += sizeof(int);
	}

	if (xing->flags & FFMPG_XING_TOC) {
		ffmemcpy(xing->toc, data, 100);
		data += 100;
	}

	if (xing->flags & FFMPG_XING_VBRSCALE) {
		xing->vbr_scale = ffint_ntoh32(data);
		data += sizeof(int);
	}

	*len = data - dstart;
	return 0;
}


int ffmpg_lame_parse(ffmpg_lame *lame, const char *data, size_t *len)
{
	const ffmpg_lamehdr *h = (void*)data;

	if (*len < sizeof(ffmpg_lame))
		return -1;

	ffmemcpy(lame->id, h->id, sizeof(lame->id));
	lame->enc_delay = ((uint)h->delay_hi << 4) | (h->delay_lo);
	lame->enc_padding = (((uint)h->padding_hi) << 8) | h->padding_lo;
	*len = sizeof(ffmpg_lame);
	return 0;
}


enum {
	DEC_DELAY = 528,
};

static const char *const _ffmpg_errs[] = {
	"PCM format error",
	"tags error",
	"seek sample is too large",
	"can't find a valid MPEG frame",
	"unrecognized data before frame header",
};

const char* ffmpg_errstr(ffmpg *m)
{
	switch (m->err) {
	case FFMPG_EMPG123:
		return mpg123_plain_strerror(m->e);

#ifdef FF_LIBMAD
	case FFMPG_ESTREAM:
		return mad_stream_errorstr(&m->stream);
#endif

	case FFMPG_ESYS:
		return fferr_strp(fferr_last());
	}
	if (m->err < FFMPG_EFMT)
		return "";
	m->err -= FFMPG_EFMT;
	FF_ASSERT(m->err < FFCNT(_ffmpg_errs));
	return _ffmpg_errs[m->err];
}

void ffmpg_init(ffmpg *m)
{
	ffid3_parseinit(&m->id32tag);
}

void ffmpg_close(ffmpg *m)
{
	ffarr_free(&m->tagval);
	if (m->is_id32tag)
		ffid3_parsefin(&m->id32tag);
	ffarr_free(&m->buf);
	ffarr_free(&m->buf2);

#ifdef FF_LIBMAD
	mad_synth_finish(&m->synth);
	mad_frame_finish(&m->frame);
	mad_stream_finish(&m->stream);

#else
	if (m->m123 != NULL)
		mpg123_delete(m->m123);
	// mpg123_exit();
#endif
}

enum { I_START, I_FIRST, I_INIT, I_INPUT, I_BUFINPUT, I_FR, I_FROK, I_SYNTH, I_SEEK, I_SEEK2,
	I_ID3V2, I_ID31_CHECK, I_FTRTAGS, I_ID31, I_APE2, I_APE2_MORE, I_TAG_SKIP };

void ffmpg_seek(ffmpg *m, uint64 sample)
{
	if (m->total_size == 0 || m->total_samples == 0)
		return;
	if (m->skip_samples != 0)
		m->skip_samples = 0;
	m->seek_sample = sample;
	if (m->state != I_INIT)
		m->state = I_SEEK;
}

/** Convert sample number to stream offset (in bytes) based on:
 . seek table from Xing tag
 . 'total samples' and 'total size' values (linear) */
static FFINL uint64 mpg_getseekoff(ffmpg *m)
{
	if (m->xing.flags & FFMPG_XING_TOC)
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
		return FFMPG_RDONE;

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
		case FFID3_PICTURE:
			m->id32tag.flags &= ~FFID3_FWHOLE;
			break;

		default:
			m->id32tag.flags |= FFID3_FWHOLE;
		}
		continue;

	case FFID3_RDATA:
		if (!(m->id32tag.flags & FFID3_FWHOLE))
			continue;

		m->tag = m->id32tag.frame;
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
 . 'frames' value from Xing tag
 . encoder/decoder delays from LAME tag
 . TLEN frame from ID3v2
 . bitrate from the first header and total stream size */
static int _ffmpg_streaminfo(ffmpg *m, const char *p, uint datalen, uint fr_samps)
{
	uint n;
	size_t len;

	len = datalen;
	if (!(m->options & FFMPG_O_NOXING)
		&& 0 == ffmpg_xing_parse(&m->xing, p, &len)) {

		if (m->xing.flags & FFMPG_XING_FRAMES) {
			m->total_samples = m->xing.frames * fr_samps;
		}

		p += len;
		len = datalen - len;
		if (0 == ffmpg_lame_parse(&m->lame, p, &len)) {
			n = m->lame.enc_delay + DEC_DELAY + 1;
			if (m->lame.enc_padding != 0)
				n += m->lame.enc_padding - (DEC_DELAY + 1);
			m->total_samples -= ffmin(n, m->total_samples);
			m->skip_samples = m->lame.enc_delay + DEC_DELAY + 1;
		}

		if (m->total_samples != 0 && m->total_size != 0)
			m->bitrate = ffpcm_brate(m->total_size, m->total_samples, m->fmt.sample_rate);

		return 0;
	}

	if (m->total_len != 0) {
		if (m->total_size != 0)
			m->bitrate = ffpcm_brate_ms(m->total_size, m->total_len);

	} else
		m->total_len = m->total_size * 1000 / (m->bitrate/8);

	m->total_samples = ffpcm_samples(m->total_len, m->fmt.sample_rate);

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

			return FFMPG_RMORE;
		}

		if (NULL != (h = ffmpg_framefind(buf->ptr, buf->len, (m->firsthdr.sync1 != 0) ? &m->firsthdr : NULL))) {
			if ((void*)h != buf->ptr)
				m->lostsync = 1;
			d.off = (char*)h - buf->ptr + 1;
		}
	}
	m->data = d.data.ptr;
	m->datalen = d.data.len;

	m->bytes_skipped = 0;

body:
	h = (void*)buf->ptr;
	r = ffarr_append_until(buf, m->data, m->datalen, ffmpg_framelen(h));
	if (r == -1) {
		m->err = FFMPG_ESYS;
		return FFMPG_RERR;
	} else if (r == 0) {
		m->fr_body = 1;
		return FFMPG_RMORE;
	}
	FFARR_SHIFT(m->data, m->datalen, r);
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
			ffarr_free(buf);
			ffarr_acq(buf, &m->buf2);
			ffarr_null(&m->buf2);
			continue;

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
			ffarr_free(buf);
			ffarr_acq(buf, &m->buf2);
			ffarr_null(&m->buf2);
			continue;
		}

		if (NULL == ffarr_append(buf, m->buf2.ptr, m->buf2.len)) {
			m->err = FFMPG_ESYS;
			return FFMPG_RERR;
		}
		ffarr_free(&m->buf2);
		m->frame2 = 0;
		return 0;
	}
	//unreachable
}

/*
Workflow:
 . parse ID3v2
 . parse ID3v1
 . parse Xing, LAME tags
 . decode frames...
*/
int ffmpg_decode(ffmpg *m)
{
	int i, r;

	for (;;) {
	switch (m->state) {
		case I_SEEK:
			m->buf.len = 0;
			if (m->seek_sample >= m->total_samples) {
				m->err = FFMPG_ESEEK;
				return FFMPG_RERR;
			}
			m->off = mpg_getseekoff(m);
			m->cur_sample = m->seek_sample;
			m->state = I_SEEK2;
			return FFMPG_RSEEK;

		case I_SEEK2:
#ifdef FF_LIBMAD
			mad_stream_finish(&m->stream);
			mad_stream_init(&m->stream);
			mad_stream_buffer(&m->stream, m->data, m->datalen);
			m->stream.sync = 0;

#else
			if (0 != (r = _ffmpg_frame2(m, &m->buf)))
				return r;
			mpg123_decode(m->m123, (void*)-1, (size_t)-1, NULL, NULL); //reset bufferred data
#endif
			m->state = I_FR;
			break;


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
		{
		if (0 != (r = _ffmpg_frame2(m, &m->buf)))
			return r;
		ffmpg_hdr *h = (void*)m->buf.ptr;
		m->fmt.format = (m->options & FFMPG_O_INT16) ? FFPCM_16LE : FFPCM_FLOAT;
		m->fmt.sample_rate = ffmpg_sample_rate(h);
		m->fmt.channels = ffmpg_channels(h);
		m->bitrate = ffmpg_bitrate(h);

		uint xingoff = sizeof(ffmpg_hdr) + mpg_xingoffs[h->ver != FFMPG_1][ffmpg_channels(h) - 1];
		_ffmpg_streaminfo(m, (char*)h + xingoff, ffmpg_framelen(h) - xingoff, ffmpg_frame_samples(h));

		m->firsthdr = *h;
		m->state = I_INIT;
		}

#ifndef FF_LIBMAD
		m->fmt.ileaved = 1;
#endif

		//note: libmad: this frame will be skipped
		return FFMPG_RHDR;


#ifdef FF_LIBMAD
	default:
		r = mpg_mad_decode(m);
		if (r == 0x100)
			continue;
		return r;

#else
	case I_INIT:
		i = MPG123_QUIET | ((m->options & FFMPG_O_INT16) ? 0 : MPG123_FORCE_FLOAT);
		if (MPG123_OK != (r = mpg123_open(&m->m123, i))) {
			m->err = FFMPG_EMPG123;
			m->e = r;
			return FFMPG_RERR;
		}
		m->state = (m->seek_sample != 0) ? I_SEEK : I_FR;
		continue;

	case I_FROK:
		m->cur_sample += m->pcmlen / ffpcm_size1(&m->fmt);
		m->state = I_FR;
		// break

	case I_FR:
		if (m->buf.len != 0) {
			r = mpg123_decode(m->m123, (byte*)m->buf.ptr, m->buf.len, NULL, NULL);
			m->buf.len = 0;
		}

		r = mpg123_decode(m->m123, m->data, m->datalen, (byte**)&m->pcmi, &m->pcmlen);
		FFARR_SHIFT(m->data, m->datalen, m->datalen);
		if (r == MPG123_NEED_MORE)
			return FFMPG_RMORE;
		else if (r != MPG123_OK) {
			m->err = FFMPG_EMPG123;
			m->e = r;
			return FFMPG_RERR;
		}
		m->state = I_FROK;
		return FFMPG_RDATA;
#endif
	}
	}
}

#ifdef FF_LIBMAD
/* stream -> frame -> synth

Frame decode:
To decode a frame libmad needs data also for the following frames:
 (hdr1 data1)  (hdr2 data2)  ...
When there's not enough contiguous data, MAD_ERROR_BUFLEN is returned.
ffmpg.buf is used to provide libmad with contiguous data enough to decode one frame.
*/
static int mpg_mad_decode(ffmpg *m)
{
	uint i, ich, isrc, skip;
	size_t len;

	for (;;) {
		switch (m->state) {

		case I_INIT:
			mad_stream_init(&m->stream);
			mad_frame_init(&m->frame);
			mad_synth_init(&m->synth);
			if (m->seek_sample != 0) {
				m->state = I_SEEK;
				return 0x100;
			}
			// m->state = I_INPUT;
			// break

		case I_INPUT:
			mad_stream_buffer(&m->stream, m->data, m->datalen);
			m->state = I_FR;
			break;

		case I_BUFINPUT:
			FF_ASSERT(m->datalen >= m->dlen);
			i = ffmin(m->datalen - m->dlen, 1024);
			if (NULL == ffarr_append(&m->buf, m->data + m->dlen, i)) {
				m->err = FFMPG_ESYS;
				return FFMPG_RERR;
			}
			m->dlen += i;
			mad_stream_buffer(&m->stream, (void*)m->buf.ptr, m->buf.len);
			if (m->datalen == 0)
				m->stream.options |= MAD_OPTION_LASTFRAME;
			m->state = I_FR;
			break;

		case I_FROK:
			m->cur_sample += m->synth.pcm.length;
			m->state = I_FR;
			// break;

		case I_FR:
			i = mad_frame_decode(&m->frame, &m->stream);

			if (m->stream.buffer == (void*)m->buf.ptr
				&& (i == 0 || MAD_RECOVERABLE(m->stream.error))) {

				len = m->buf.len - (m->stream.next_frame - m->stream.buffer); //number of unprocessed bytes in m->buf
				if (len <= m->dlen) {
					len = m->dlen - len;
					m->data += len;
					m->datalen -= len;
					mad_stream_buffer(&m->stream, m->data, m->datalen);
					m->buf.len = 0;
					m->dlen = 0;
				}
			}

			if (i != 0) {
				m->err = FFMPG_ESTREAM;

				if (m->stream.error == MAD_ERROR_LOSTSYNC
					&& (m->options & FFMPG_O_ID3V1)
					&& (size_t)(m->stream.bufend - m->stream.this_frame) >= FFSLEN("TAG")
					&& ffid31_valid((ffid31*)m->stream.this_frame))
					return FFMPG_RDONE;

				else if (MAD_RECOVERABLE(m->stream.error))
					return FFMPG_RWARN;

				else if (m->stream.error == MAD_ERROR_BUFLEN) {

					if (m->stream.buffer == (void*)m->buf.ptr) {
						m->state = I_BUFINPUT;
						len = m->stream.bufend - m->stream.next_frame;
						ffmemcpy(m->buf.ptr, m->stream.next_frame, len);
						m->buf.len = m->stream.bufend - m->stream.next_frame;
						if (m->datalen - m->dlen == 0) {
							m->dlen = 0;
							return FFMPG_RMORE;
						}
						continue;
					}

					m->state = I_INPUT;

					if (m->stream.next_frame != m->stream.bufend) {
						if (NULL == ffarr_copy(&m->buf, m->stream.next_frame, m->stream.bufend - m->stream.next_frame)) {
							m->err = FFMPG_ESYS;
							return FFMPG_RERR;
						}
						m->state = I_BUFINPUT;
					}
					return FFMPG_RMORE;
				}

				return FFMPG_RERR;
			}
			// m->state = I_SYNTH;
			// break

		case I_SYNTH:
			mad_synth_frame(&m->synth, &m->frame);
			if (m->synth.pcm.channels != m->fmt.channels
				|| m->synth.pcm.samplerate != m->fmt.sample_rate) {
				m->err = FFMPG_EFMT;
				return FFMPG_RERR;
			}

			skip = 0;
			if (m->skip_samples != 0) {
				skip = ffmin(m->synth.pcm.length, m->skip_samples);
				m->synth.pcm.length -= skip;
				m->skip_samples -= skip;
			}

			if (m->cur_sample + m->synth.pcm.length > m->total_samples
				&& m->lame.enc_padding != 0) {
				m->synth.pcm.length = m->total_samples - m->cur_sample;
			}

			if (m->synth.pcm.length == 0) {
				m->state = I_FR;
				continue;
			}

			goto ok;
		}
	}

ok:
	//in-place convert int[] -> float[]
	for (ich = 0;  ich != m->synth.pcm.channels;  ich++) {
		m->pcm[ich] = (float*)&m->synth.pcm.samples[ich];
		for (i = 0, isrc = skip;  i != m->synth.pcm.length;  i++, isrc++) {
			m->pcm[ich][i] = (float)mad_f_todouble(m->synth.pcm.samples[ich][isrc]);
		}
	}
	m->pcmlen = m->synth.pcm.length * m->synth.pcm.channels * sizeof(float);

	m->state = I_FROK;
	return FFMPG_RDATA;
}
#endif
