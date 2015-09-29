/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/pcm.h>

#include <math.h>


const char *const ffpcm_fmtstr[3] = { "int16", "int32", "float" };

const byte ffpcm_bits[] = { 16, 32, 32 };


static const char *const _ffpcm_channelstr[3] = {
	"mono", "stereo", "multi-channel"
};

const char* ffpcm_channelstr(uint channels)
{
	return _ffpcm_channelstr[ffmin(channels - 1, FFCNT(_ffpcm_channelstr) - 1)];
}

static FFINL int _pcm_lim_16le(int i)
{
	if (i < -0x8000)
		i = -0x8000;
	else if (i > 0x7fff)
		i = 0x7fff;
	return i;
}

void ffpcm_mix(const ffpcm *pcm, char *stm1, const char *stm2, size_t samples)
{
	size_t i;
	uint ich;
	union {
		short *sh;
		int *i;
		float *f;
	} u1, u2;

	u1.sh = (short*)stm1;
	u2.sh = (short*)stm2;

	switch (pcm->format) {
	case FFPCM_16LE:
		for (i = 0;  i < samples;  i++) {
			for (ich = 0;  ich < pcm->channels;  ich++) {
				*u1.sh = (short)_pcm_lim_16le(*u1.sh + *u2.sh++);
				u1.sh++;
			}
		}
		break;

	case FFPCM_FLOAT:
		for (i = 0;  i < samples;  i++) {
			for (ich = 0;  ich < pcm->channels;  ich++) {
				*u1.f += *u2.f++;
				u1.f++;
			}
		}
		break;
	}
}

#define _ffpcm_16le_flt(sh)  ((float)(sh) / 32768.0f)

#define _ffpcm_flt_16le(f) \
	((short)_pcm_lim_16le((int)floor(f * 32767.0f + 0.5f)))

#define CASE(f1, il1, f2, il2) \
	(f1 << 16) | (il1 << 31) | (f2 & 0xffff) | (il2 << 15)

/*
non-interleaved: data[0][..] - left,  data[1][..] - right
interleaved: data[0,2..] - left */
int ffpcm_convert(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const void *in, size_t samples)
{
	size_t i;
	uint ich, j;
	union {
		short *sh;
		float *f;
		short **psh;
		float **pf;
	} from, to;

	from.sh = (void*)in;
	to.sh = out;

	if (inpcm->channels != outpcm->channels
		|| inpcm->sample_rate != outpcm->sample_rate)
		return -1;

	switch (CASE(inpcm->format, inpcm->ileaved, outpcm->format, outpcm->ileaved)) {

	case CASE(FFPCM_16LE, 0, FFPCM_16LE, 1):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			j = ich;
			for (i = 0;  i != samples;  i++) {
				to.sh[j] = from.psh[ich][i];
				j += outpcm->channels;
			}
		}
		break;

	case CASE(FFPCM_16LE, 1, FFPCM_16LE, 0):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			from.sh = (short*)in + ich;
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i] = *from.sh;
				from.sh += inpcm->channels;
			}
		}
		break;

	case CASE(FFPCM_16LE, 0, FFPCM_FLOAT, 0):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i] = _ffpcm_16le_flt(from.psh[ich][i]);
			}
		}
		break;

	case CASE(FFPCM_16LE, 1, FFPCM_FLOAT, 1):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				*to.f++ = _ffpcm_16le_flt(*from.sh++);
			}
		}
		break;

	case CASE(FFPCM_16LE, 1, FFPCM_FLOAT, 0):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			from.sh = (short*)in + ich;
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i] = _ffpcm_16le_flt(*from.sh);
				from.sh += inpcm->channels;
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 1, FFPCM_16LE, 1):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				*to.sh++ = _ffpcm_flt_16le(*from.f++);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 0, FFPCM_16LE, 1):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			j = ich;
			for (i = 0;  i != samples;  i++) {
				to.sh[j] = _ffpcm_flt_16le(from.pf[ich][i]);
				j += outpcm->channels;
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 0, FFPCM_16LE, 0):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i] = _ffpcm_flt_16le(from.pf[ich][i]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 0, FFPCM_FLOAT, 1):
		for (ich = 0;  ich != inpcm->channels;  ich++) {
			j = ich;
			for (i = 0;  i != samples;  i++) {
				to.f[j] = from.pf[ich][i];
				j += outpcm->channels;
			}
		}
		break;

	default:
		return -1;
	}

	return 0;
}

#undef CASE

#define CASE(f1, il1) \
	(f1 << 16) | (il1 << 31)

int ffpcm_gain(const ffpcmex *pcm, float gain, const void *in, void *out, uint samples)
{
	uint i, ich;
	union {
		short *sh;
		float *f;
		short **psh;
		float **pf;
	} from, to;

	if (gain == 1)
		return 0;

	from.sh = (void*)in;
	to.sh = out;

	switch (CASE(pcm->format, pcm->ileaved)) {
	case CASE(FFPCM_16LE, 1):
		for (i = 0;  i != samples * pcm->channels;  i++) {
			to.sh[i] = _ffpcm_flt_16le(_ffpcm_16le_flt(from.sh[i]) * gain);
		}
		break;

	case CASE(FFPCM_16LE, 0):
		for (ich = 0;  ich != pcm->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i] = _ffpcm_flt_16le(_ffpcm_16le_flt(from.psh[ich][i]) * gain);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 1):
		for (i = 0;  i != samples * pcm->channels;  i++) {
			to.f[i] = from.f[i] * gain;
		}
		break;

	case CASE(FFPCM_FLOAT, 0):
		for (ich = 0;  ich != pcm->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i] = from.pf[ich][i] * gain;
			}
		}
		break;

	default:
		return -1;
	}

	return 0;
}

#undef CASE
