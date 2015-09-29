/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/mpeg.h>
#include <FF/audio/id3.h>
#include <FF/string.h>
#include <FFOS/error.h>


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
	MPG_EFMT = 0
	, MPG_ESTM = 1
	, MPG_ESYS = 2
};

const char* ffmpg_errstr(ffmpg *m)
{
	switch (m->err) {
	case MPG_ESTM:
		return mad_stream_errorstr(&m->stream);

	case MPG_EFMT:
		return "PCM format error";

	case MPG_ESYS:
		return fferr_strp(fferr_last());
	}
	return "";
}

void ffmpg_init(ffmpg *m)
{
	mad_stream_init(&m->stream);
	mad_frame_init(&m->frame);
	mad_synth_init(&m->synth);
}

void ffmpg_close(ffmpg *m)
{
	ffarr_free(&m->buf);
	mad_synth_finish(&m->synth);
	mad_frame_finish(&m->frame);
	mad_stream_finish(&m->stream);
}

enum { I_INPUT, I_BUFINPUT, I_FR, I_FROK, I_SYNTH, I_SEEK, I_SEEK2, I_HDR, I_ID31 };

void ffmpg_seek(ffmpg *m, uint64 sample)
{
	if (m->total_size == 0 || m->total_samples == 0)
		return;
	m->seek_sample = sample;
	m->state = I_SEEK;
}

/* stream -> frame -> synth */
int ffmpg_decode(ffmpg *m)
{
	uint i, ich;

	for (;;) {
		switch (m->state) {
		case I_SEEK:
			m->buf.len = 0;
			m->off = m->dataoff + m->total_size * m->seek_sample / m->total_samples;
			m->cur_sample = m->seek_sample;
			m->state = I_SEEK2;
			return FFMPG_RSEEK;

		case I_SEEK2:
			mad_stream_buffer(&m->stream, m->data, m->datalen);
			m->stream.sync = 0;
			m->state = I_FR;
			break;


		case I_ID31:
			if (m->datalen == sizeof(ffid31)
				&& (m->id31state != 0 || ffid31_valid((void*)m->data))
				&& -1 != (m->tagframe = ffid31_parse((void*)m->data, &m->id31state, &m->tagval)))
				return FFMPG_RTAG;

			m->state = I_HDR;
			m->off = m->dataoff;
			if (m->id31state != 0)
				m->total_size -= sizeof(ffid31);
			return FFMPG_RSEEK;


		case I_INPUT:
			mad_stream_buffer(&m->stream, m->data, m->datalen);
			m->state = I_FR;
			break;

		case I_BUFINPUT:
			if (NULL == ffarr_append(&m->buf, m->data, m->datalen)) {
				m->err = MPG_ESYS;
				return FFMPG_RERR;
			}
			mad_stream_buffer(&m->stream, (void*)m->buf.ptr, m->buf.len);
			m->state = I_FR;
			break;

		case I_FROK:
			m->cur_sample += m->synth.pcm.length;
			m->state = I_FR;
			// break;

		case I_FR:
			if (0 != mad_frame_decode(&m->frame, &m->stream)) {
				m->err = MPG_ESTM;

				if (MAD_RECOVERABLE(m->stream.error))
					return FFMPG_RWARN;

				else if (m->stream.error == MAD_ERROR_BUFLEN) {
					m->state = I_INPUT;

					if (m->stream.this_frame != m->stream.bufend) {
						if (NULL == ffarr_copy(&m->buf, m->stream.this_frame, m->stream.bufend - m->stream.this_frame)) {
							m->err = MPG_ESYS;
							return FFMPG_RERR;
						}
						m->state = I_BUFINPUT;
					}
					return FFMPG_RMORE;
				}

				return FFMPG_RERR;
			}

			if (m->fmt.channels == 0) {
				m->bitrate = m->frame.header.bitrate;
				m->fmt.format = FFPCM_FLOAT;
				m->fmt.channels = MAD_NCHANNELS(&m->frame.header);
				m->fmt.sample_rate = m->frame.header.samplerate;

				if (m->seekable && m->total_size > sizeof(ffid31)) {
					m->state = I_ID31;
					m->off = m->total_size - sizeof(ffid31);
					return FFMPG_RSEEK;
				}

				goto hdr;
			}

			goto ok;

		case I_HDR:
hdr:
			if (m->total_len != 0) {
				m->total_samples = ffpcm_samples(m->total_len, m->fmt.sample_rate);
				if (m->total_size != 0) {
					m->total_size -= m->dataoff;
					m->bitrate = ffpcm_bitrate(m->total_size, m->total_len);
				}

			} else if (m->total_size != 0) {
				m->total_size -= m->dataoff;
				m->total_samples = ffpcm_samples(m->total_size * 1000 / (m->bitrate/8), m->fmt.sample_rate);
			}

			m->state = I_SYNTH;
			return FFMPG_RHDR;

		case I_SYNTH:
			m->state = I_FR;
			goto ok;
		}
	}

ok:
	mad_synth_frame(&m->synth, &m->frame);
	if (m->synth.pcm.channels != m->fmt.channels
		|| m->synth.pcm.samplerate != m->fmt.sample_rate) {
		m->err = MPG_EFMT;
		return FFMPG_RERR;
	}

	//in-place convert int[] -> float[]
	for (ich = 0;  ich != m->synth.pcm.channels;  ich++) {
		m->pcm[ich] = (float*)&m->synth.pcm.samples[ich];
		for (i = 0;  i != m->synth.pcm.length;  i++) {
			m->pcm[ich][i] = (float)mad_f_todouble(m->synth.pcm.samples[ich][i]);
		}
	}
	m->pcmlen = m->synth.pcm.length * m->synth.pcm.channels * sizeof(float);
	m->state = I_FROK;
	return FFMPG_RDATA;
}
