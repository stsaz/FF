/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/mpeg.h>
#include <FF/audio/id3.h>
#include <FF/string.h>
#include <FF/number.h>
#include <FF/data/utf8.h>
#include <FFOS/error.h>


static int mpg_id31(ffmpg *m);
static int mpg_id32(ffmpg *m);
static int mpg_streaminfo(ffmpg *m);


const byte ffmpg1l3_kbyterate[16] = {
	0, 32/8, 40/8, 48/8
	, 56/8, 64/8, 80/8, 96/8
	, 112/8, 128/8, 160/8, 192/8
	, 224/8, 256/8, 320/8, 0
};

const ushort ffmpg1_sample_rate[4] = {
	44100, 48000, 32000, 0
};

const char ffmpg_strchannel[4][8] = {
	"stereo", "joint", "dual", "mono"
};

ffbool ffmpg_valid(const ffmpg_hdr *h)
{
	return (ffint_ntoh32(h) & 0xffe00000) == 0xffe00000
		&& h->ver == FFMPG_1 && h->layer == FFMPG_L3
		&& h->bitrate != 0 && h->bitrate != 15
		&& h->sample_rate != 3;
}

uint ffmpg_framelen(const ffmpg_hdr *h)
{
	return 144 * (ffmpg1l3_kbyterate[h->bitrate] * 8 * 1000) / ffmpg1_sample_rate[h->sample_rate] + h->padding;
}


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

const char* ffmpg_errstr(ffmpg *m)
{
	switch (m->err) {
	case FFMPG_ESTREAM:
		return mad_stream_errorstr(&m->stream);

	case FFMPG_EFMT:
		return "PCM format error";

	case FFMPG_ESYS:
		return fferr_strp(fferr_last());
	}
	return "";
}

void ffmpg_init(ffmpg *m)
{
	mad_stream_init(&m->stream);
	mad_frame_init(&m->frame);
	mad_synth_init(&m->synth);
	ffid3_parseinit(&m->id32tag);
}

void ffmpg_close(ffmpg *m)
{
	ffarr_free(&m->tagval);
	ffid3_parsefin(&m->id32tag);
	ffid31_parse_fin(&m->id31tag);
	ffarr_free(&m->buf);
	mad_synth_finish(&m->synth);
	mad_frame_finish(&m->frame);
	mad_stream_finish(&m->stream);
}

enum { I_START, I_INPUT, I_BUFINPUT, I_FR, I_FROK, I_SYNTH, I_SEEK, I_SEEK2,
	I_ID31, I_ID3V2, I_TAG_SKIP };

void ffmpg_seek(ffmpg *m, uint64 sample)
{
	if (m->total_size == 0 || m->total_samples == 0)
		return;
	if (m->skip_samples != 0)
		m->skip_samples = 0;
	m->seek_sample = sample;
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
	int i;
	size_t len;

	len = m->datalen;
	i = ffid31_parse(&m->id31tag, m->data, &len);
	m->data += len;
	m->datalen -= len;

	switch (i) {
	case FFID3_RNO:
		break;

	case FFID3_RDONE:
		m->total_size -= sizeof(ffid31);
		ffarr_free(&m->tagval);
		break;

	case FFID3_RMORE:
		return FFMPG_RMORE;

	case FFID3_RDATA:
		if (m->codepage == 0) {
			ffarr_free(&m->tagval);
			ffarr_set(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len);

		} else if (0 == ffutf8_strencode(&m->tagval, m->id31tag.val.ptr, m->id31tag.val.len, m->codepage)) {
			m->err = FFMPG_ETAG;
			return FFMPG_RWARN;
		}
		return FFMPG_RTAG;
	}

	m->state = I_INPUT;
	if (m->options & FFMPG_O_ID3V2)
		m->state = I_ID3V2;
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
		ffid3_parsefin(&m->id32tag);
		ffarr_free(&m->tagval);
		return FFMPG_RDONE;

	case FFID3_RMORE:
		return FFMPG_RMORE;

	case FFID3_RHDR:
		m->dataoff = sizeof(ffid3_hdr) + ffid3_size(&m->id32tag.h);
		m->total_size -= m->dataoff;
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

		if (0 > ffid3_getdata(m->id32tag.frame, m->id32tag.data.ptr, m->id32tag.data.len, m->id32tag.txtenc, m->codepage, &m->tagval)) {
			m->err = FFMPG_ETAG;
			return FFMPG_RWARN;
		}

		if (m->id32tag.frame == FFID3_LENGTH && m->id32tag.data.len != 0) {
			uint64 dur;
			if (m->id32tag.data.len == ffs_toint(m->id32tag.data.ptr, m->id32tag.data.len, &dur, FFS_INT64))
				m->total_len = dur;
		}

		m->is_id32tag = 1;
		return FFMPG_RTAG;

	case FFID3_RERR:
		m->state = I_TAG_SKIP;
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
static FFINL int mpg_streaminfo(ffmpg *m)
{
	const char *p;
	uint n;
	size_t len;

	m->bitrate = m->frame.header.bitrate;
	m->fmt.format = FFPCM_FLOAT;
	m->fmt.channels = MAD_NCHANNELS(&m->frame.header);
	m->fmt.sample_rate = m->frame.header.samplerate;

	if (m->total_size != 0)
		m->total_size -= m->dataoff;

	len = m->stream.anc_bitlen / 8;
	if (!(m->options & FFMPG_O_NOXING)
		&& 0 == ffmpg_xing_parse(&m->xing, (char*)m->stream.anc_ptr.byte, &len)) {

		if (m->xing.flags & FFMPG_XING_FRAMES) {
			m->total_samples = m->xing.frames * 32 * MAD_NSBSAMPLES(&m->frame.header);
		}

		p = (char*)m->stream.anc_ptr.byte + len;
		len = m->stream.anc_bitlen / 8 - len;
		if (0 == ffmpg_lame_parse(&m->lame, p, &len)) {
			n = m->lame.enc_delay + DEC_DELAY + 1;
			if (m->lame.enc_padding != 0)
				n += m->lame.enc_padding - (DEC_DELAY + 1);
			m->total_samples -= ffmin(n, m->total_samples);
			m->skip_samples = m->lame.enc_delay + DEC_DELAY + 1;
		}

		if (m->total_samples != 0 && m->total_size != 0)
			m->bitrate = ffpcm_bitrate(m->total_size, ffpcm_time(m->total_samples, m->fmt.sample_rate));

		return FFMPG_RHDR;
	}

	if (m->total_len != 0) {
		if (m->total_size != 0)
			m->bitrate = ffpcm_bitrate(m->total_size, m->total_len);

	} else
		m->total_len = m->total_size * 1000 / (m->bitrate/8);

	m->total_samples = ffpcm_samples(m->total_len, m->fmt.sample_rate);

	m->state = I_SYNTH;
	return FFMPG_RHDR;
}

/* stream -> frame -> synth

Workflow:
 . parse ID3v1
 . parse ID3v2
 . parse Xing, LAME tags
 . decode frames...

Frame decode:
To decode a frame libmad needs data also for the following frames:
 (hdr1 data1)  (hdr2 data2)  ...
When there's not enough contiguous data, MAD_ERROR_BUFLEN is returned.
ffmpg.buf is used to provide libmad with contiguous data enough to decode one frame.
*/
int ffmpg_decode(ffmpg *m)
{
	uint i, ich, isrc, skip;
	size_t len;

	for (;;) {
		switch (m->state) {
		case I_SEEK:
			m->buf.len = 0;
			m->off = mpg_getseekoff(m);
			m->cur_sample = m->seek_sample;
			m->state = I_SEEK2;
			return FFMPG_RSEEK;

		case I_SEEK2:
			mad_stream_finish(&m->stream);
			mad_stream_init(&m->stream);
			mad_stream_buffer(&m->stream, m->data, m->datalen);
			m->stream.sync = 0;
			m->state = I_FR;
			break;


		case I_ID31:
			if (FFMPG_RDONE != (i = mpg_id31(m)))
				return i;
			m->off = 0;
			return FFMPG_RSEEK;

		case I_ID3V2:
			if (FFMPG_RDONE != (i = mpg_id32(m)))
				return i;

			m->state = I_INPUT;
			continue;

		case I_TAG_SKIP:
			m->off = m->dataoff;
			m->state = I_INPUT;
			return FFMPG_RSEEK;


		case I_START:
			if ((m->options & FFMPG_O_ID3V1) && m->total_size > sizeof(ffid31)) {
				m->state = I_ID31;
				m->off = m->total_size - sizeof(ffid31);
				return FFMPG_RSEEK;
			}

			if (m->options & FFMPG_O_ID3V2) {
				m->state = I_ID3V2;
				continue;
			}

			// m->state = I_INPUT;
			// break;

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
					&& m->stream.bufend - m->stream.this_frame >= FFSLEN("TAG")
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

			if (m->fmt.channels == 0) {
				return mpg_streaminfo(m);
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
