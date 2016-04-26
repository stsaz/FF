/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/pcm.h>
#include <FF/string.h>
#include <FF/number.h>
#include <FFOS/mem.h>

#include <math.h>


const char* ffpcm_fmtstr(uint fmt)
{
	switch (fmt) {
	case FFPCM_16LE:
		return "int16";
	case FFPCM_24:
		return "int24";
	case FFPCM_32LE:
		return "int32";
	case FFPCM_16LE_32:
		return "int16_32";
	case FFPCM_FLOAT:
		return "float32";
	}
	FF_ASSERT(0);
	return "";
}


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

#define max16f  (32768.0)

static FFINL short _ffpcm_flt_16le(float f)
{
	double d = f * max16f;
	if (d < -max16f)
		return -0x8000;
	else if (d > max16f - 1)
		return 0x7fff;
	return ffint_ftoi(d);
}

union pcmdata {
	char *b;
	short *sh;
	int *in;
	float *f;
	char **pb;
	short **psh;
	int **pin;
	float **pf;
};

#define CASE(f1, il1, f2, il2) \
	(f1 << 16) | (il1 << 31) | (f2 & 0xffff) | (il2 << 15)

/* L,R -> L+R */
static int _ffpcm_mono_mix(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const union pcmdata from, size_t samples)
{
	size_t i;
	uint ich;
	union pcmdata to;
	to.sh = out;

	switch (CASE(inpcm->format, inpcm->ileaved, 0, 0)) {

	case CASE(FFPCM_16LE, 1, 0, 0):
		for (i = 0;  i != samples;  i++) {
			int v = 0;
			for (ich = 0;  ich != inpcm->channels;  ich++) {
				v += from.sh[ich * i];
			}

			to.sh[i] = _pcm_lim_16le(v / inpcm->channels);
		}
		break;

	default:
		return -1;
	}

	return 0;
}

/* L,R -> L */
static int _ffpcm_mono(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const union pcmdata from, size_t samples)
{
	size_t i;
	uint ch = ((outpcm->channels & ~FFPCM_CHMASK) >> 4) - 1;
	union pcmdata to;
	to.sh = out;

	switch (inpcm->format) {

	case FFPCM_16LE:
		for (i = 0;  i != samples;  i++) {
			to.sh[i] = from.sh[ch + inpcm->channels * i];
		}
		break;

	default:
		return -1;
	}
	return 0;
}

/* M -> M,M */
static int _ffpcm_stereo(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const union pcmdata from, size_t samples)
{
	size_t i;
	union pcmdata to, tmp;
	to.sh = out;

	if (samples != 0)
		tmp.sh = (inpcm->ileaved) ? from.sh : from.psh[0];

	switch (inpcm->format) {

	case FFPCM_16LE:
		for (i = 0;  i != samples;  i++) {
			to.sh[i] = tmp.sh[i] / 2;
		}
		break;

	case FFPCM_FLOAT:
		for (i = 0;  i != samples;  i++) {
			to.f[i] = tmp.f[i] * 0.5;
		}
		break;

	default:
		return -1;
	}
	return 0;
}

/*
non-interleaved: data[0][..] - left,  data[1][..] - right
interleaved: data[0,2..] - left */
int ffpcm_convert(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const void *in, size_t samples)
{
	size_t i;
	uint ich, j, nch = inpcm->channels, in_ileaved = inpcm->ileaved;
	union pcmdata from, to;
	void *tmpptr = NULL;
	int r = -1;

	from.sh = (void*)in;
	to.sh = out;

	if (inpcm->sample_rate != outpcm->sample_rate)
		goto done;

	if (inpcm->channels != outpcm->channels) {

		nch = outpcm->channels & FFPCM_CHMASK;

		if (nch == 1) {
			if ((outpcm->channels & ~FFPCM_CHMASK) == 0) {
				if (NULL == (tmpptr = ffmem_alloc(samples * ffpcm_bits(inpcm->format)/8)))
					goto done;

				if (0 != _ffpcm_mono_mix(outpcm, tmpptr, inpcm, from, samples))
					goto done;
				from.psh = (short**)&tmpptr;

			} else if (!inpcm->ileaved) {
				uint ch = ((outpcm->channels & ~FFPCM_CHMASK) >> 4) - 1;
				from.psh = from.psh + ch;

			} else {
				if (NULL == (tmpptr = ffmem_alloc(samples * ffpcm_bits(inpcm->format)/8))) // note: slows down performance by ~5%
					goto done;

				if (0 != _ffpcm_mono(outpcm, tmpptr, inpcm, from, samples))
					goto done;
				from.psh = (short**)&tmpptr;
			}

			in_ileaved = 0;

		} else if (nch == 2 && inpcm->channels == 1) {

			if (NULL == (tmpptr = ffmem_alloc(sizeof(void*) * 2 + samples * ffpcm_bits(inpcm->format)/8)))
				goto done;
			void *p = ((byte*)tmpptr) + sizeof(void*) * 2;

			if (0 != _ffpcm_stereo(outpcm, p, inpcm, from, samples))
				goto done;
			from.psh = (short**)tmpptr;
			from.psh[0] = from.psh[1] = p;
			in_ileaved = 0;

		} else
			goto done; // this channel conversion is not supported
	}

	switch (CASE(inpcm->format, in_ileaved, outpcm->format, outpcm->ileaved)) {

	case CASE(FFPCM_16LE, 0, FFPCM_16LE, 1):
		for (ich = 0;  ich != nch;  ich++) {
			j = ich;
			for (i = 0;  i != samples;  i++) {
				to.sh[j] = from.psh[ich][i];
				j += nch;
			}
		}
		break;

	case CASE(FFPCM_16LE, 1, FFPCM_16LE, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i] = from.sh[ich + nch * i];
			}
		}
		break;

	case CASE(FFPCM_16LE, 0, FFPCM_16LE_32, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i] = from.psh[ich][i];
			}
		}
		break;

	case CASE(FFPCM_16LE, 1, FFPCM_16LE_32, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i] = from.sh[ich + nch * i];
			}
		}
		break;

	case CASE(FFPCM_16LE, 0, FFPCM_FLOAT, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i] = _ffpcm_16le_flt(from.psh[ich][i]);
			}
		}
		break;

	case CASE(FFPCM_16LE, 1, FFPCM_FLOAT, 1):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				*to.f++ = _ffpcm_16le_flt(*from.sh++);
			}
		}
		break;

	case CASE(FFPCM_16LE, 1, FFPCM_FLOAT, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i] = _ffpcm_16le_flt(from.sh[ich + nch * i]);
			}
		}
		break;


	case CASE(FFPCM_24, 0, FFPCM_16LE, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i] = ffint_24(&from.pb[ich][i * 3]) / 256;
			}
		}
		break;

	case CASE(FFPCM_24, 0, FFPCM_16LE, 1):
		for (ich = 0;  ich != nch;  ich++) {
			uint j = ich;
			for (i = 0;  i != samples;  i++) {
				to.sh[j] = ffint_24(&from.pb[ich][i * 3]) / 256;
				j += nch;
			}
		}
		break;

	case CASE(FFPCM_24, 0, FFPCM_24, 1):
		for (ich = 0;  ich != nch;  ich++) {
			uint j = ich;
			for (i = 0;  i != samples;  i++) {
				ffmemcpy(&to.b[j * 3], &from.pb[ich][i * 3], 3);
				j += nch;
			}
		}
		break;

	case CASE(FFPCM_24, 0, FFPCM_32LE, 1):
		for (ich = 0;  ich != nch;  ich++) {
			uint j = ich;
			for (i = 0;  i != samples;  i++) {
				to.in[j] = ffint_24(&from.pb[ich][i * 3]) * 256;
				j += nch;
			}
		}
		break;

	case CASE(FFPCM_24, 0, FFPCM_32LE, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i] = ffint_24(&from.pb[ich][i * 3]) * 256;
			}
		}
		break;


	case CASE(FFPCM_FLOAT, 1, FFPCM_16LE, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i] = _ffpcm_flt_16le(from.f[ich + nch * i]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 1, FFPCM_16LE, 1):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				*to.sh++ = _ffpcm_flt_16le(*from.f++);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 0, FFPCM_16LE, 1):
		for (ich = 0;  ich != nch;  ich++) {
			j = ich;
			for (i = 0;  i != samples;  i++) {
				to.sh[j] = _ffpcm_flt_16le(from.pf[ich][i]);
				j += nch;
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 0, FFPCM_16LE, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i] = _ffpcm_flt_16le(from.pf[ich][i]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 0, FFPCM_16LE_32, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i] = _ffpcm_flt_16le(from.pf[ich][i]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 1, FFPCM_16LE_32, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i] = _ffpcm_flt_16le(from.f[ich + nch * i]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 1, FFPCM_FLOAT, 0):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i] = from.f[ich + nch * i];
			}
		}
		break;

	case CASE(FFPCM_FLOAT, 0, FFPCM_FLOAT, 1):
		for (ich = 0;  ich != nch;  ich++) {
			j = ich;
			for (i = 0;  i != samples;  i++) {
				to.f[j] = from.pf[ich][i];
				j += nch;
			}
		}
		break;

	default:
		if (inpcm->format == outpcm->format && in_ileaved == outpcm->ileaved) {
			if (in_ileaved == 1)
				ffmemcpy(to.sh, from.sh, samples * ffpcm_size(inpcm->format, nch));
			else {
				for (ich = 0;  ich != nch;  ich++) {
					ffmemcpy(to.psh[ich], from.psh[ich], samples * ffpcm_bits(inpcm->format)/8);
				}
			}
			break;
		}

		goto done;
	}
	r = 0;

done:
	ffmem_safefree(tmpptr);
	return r;
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


#define CASE(f, il) \
	(f & 0xffff) | (il << 15)

int ffpcm_peak(const ffpcmex *fmt, const void *data, size_t samples, float *maxpeak)
{
	float max_f = 0.0;
	uint max_sh = 0;
	uint ich;
	size_t i;
	union pcmdata d;
	d.sh = (void*)data;

	switch (CASE(fmt->format, fmt->ileaved)) {

	case CASE(FFPCM_16LE, 1):
		for (i = 0;  i != fmt->channels * samples;  i++) {
			uint sh = ffabs(d.sh[i]);
			if (max_sh < sh)
				max_sh = sh;
		}
		max_f = _ffpcm_16le_flt(max_sh);
		break;

	case CASE(FFPCM_16LE, 0):
		for (ich = 0;  ich != fmt->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				uint sh = ffabs(d.psh[ich][i]);
				if (max_sh < sh)
					max_sh = sh;
			}
		}
		max_f = _ffpcm_16le_flt(max_sh);
		break;


	case CASE(FFPCM_FLOAT, 1):
		for (i = 0;  i != fmt->channels * samples;  i++) {
			float f = ffabs(d.f[i]);
			if (max_f < f)
				max_f = f;
		}
		break;

	case CASE(FFPCM_FLOAT, 0):
		for (ich = 0;  ich != fmt->channels;  ich++) {
			for (i = 0;  i != samples;  i++) {
				float f = ffabs(d.pf[ich][i]);
				if (max_f < f)
					max_f = f;
			}
		}
		break;

	default:
		return 1;
	}

	*maxpeak = max_f;
	return 0;
}

#undef CASE


int ffpcm_seek(struct ffpcm_seek *s)
{
	uint64 size, samples, newoff;
	uint opts = s->flags;
	struct ffpcm_seekpt *pt = s->pt;

	FF_ASSERT(s->target >= pt[0].sample && s->target < pt[1].sample);
	FFDBG_PRINT(5, "%s(): %xU, %xU: [%xU..%xU] (%xU), %xU: [%xU..%xU] (%xU)\n"
		, FF_FUNC, s->target, s->fr_index, pt[0].sample, pt[1].sample, pt[1].sample - pt[0].sample
		, s->off, pt[0].off, pt[1].off, pt[1].off - pt[0].off);

	if (s->fr_samples == 0) {

	} else if (s->fr_index >= pt[0].sample && s->fr_index < pt[1].sample
		&& s->off >= pt[0].off && s->off < pt[1].off) {

		if (s->target < s->fr_index) {
			pt[1].sample = s->fr_index;
			pt[1].off = s->off;

		} else if (s->target < s->fr_index + s->fr_samples) {
			return 0;

		} else {
			// s->target > s->fr_index
			pt[0].sample = s->fr_index + s->fr_samples;
			pt[0].off = s->off + s->fr_size;
		}
	} else if (s->lastoff >= pt[0].off && s->lastoff < pt[1].off) {
		// no frame is found within range lastoff..pt[1].off
		pt[1].off = s->lastoff;
		opts |= FFPCM_SEEK_BINSCH;
	}

	size = pt[1].off - pt[0].off;
	samples = pt[1].sample - pt[0].sample;

	if ((opts & (FFPCM_SEEK_ALLOW_BINSCH | FFPCM_SEEK_BINSCH)) == (FFPCM_SEEK_ALLOW_BINSCH | FFPCM_SEEK_BINSCH))
		newoff = size / 2; //binary search
	else
		newoff = (s->target - pt[0].sample) * size / samples; //sample-based search

	uint avg_frsize = s->fr_size; //average size per frame
	if (s->avg_fr_samples != 0)
		avg_frsize = size / ffmax(samples / s->avg_fr_samples, 1);
	if (newoff > avg_frsize)
		newoff -= avg_frsize;
	else
		newoff = 0;

	newoff += pt[0].off;
	if (newoff == s->lastoff)
		return -1;
	s->off = newoff;
	return 1;
}
