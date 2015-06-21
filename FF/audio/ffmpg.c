/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/mpeg.h>
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

enum { I_INPUT, I_BUFINPUT, I_FR, I_FROK, I_SYNTH, I_SEEK, I_SEEK2 };

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
				m->state = I_SYNTH;
				m->bitrate = m->frame.header.bitrate;
				m->fmt.format = FFPCM_FLOAT;
				m->fmt.channels = MAD_NCHANNELS(&m->frame.header);
				m->fmt.sample_rate = m->frame.header.samplerate;

				if (m->total_size != 0) {
					m->total_size -= m->dataoff;
					m->total_samples = ffpcm_samples(m->total_size * 1000 / (m->bitrate/8), m->fmt.sample_rate);
				}

				return FFMPG_RHDR;
			}

			goto ok;

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
