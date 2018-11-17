/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/pcm.h>
#include <FF/string.h>
#include <FF/number.h>
#include <FFOS/mem.h>

#include <math.h>


static const char pcm_fmtstr[][9] = {
	"float32",
	"float64",
	"int16",
	"int24",
	"int24-4",
	"int32",
	"int8",
};
static const ushort pcm_fmt[] = {
	FFPCM_FLOAT,
	FFPCM_FLOAT64,
	FFPCM_16,
	FFPCM_24,
	FFPCM_24_4,
	FFPCM_32,
	FFPCM_8,
};

const char* ffpcm_fmtstr(uint fmt)
{
	int r = ffint_find2(pcm_fmt, FFCNT(pcm_fmt), fmt);
	if (r < 0) {
		FF_ASSERT(0);
		return "";
	}
	return pcm_fmtstr[r];
}

int ffpcm_fmt(const char *sfmt, size_t len)
{
	int r = ffs_findarr3(pcm_fmtstr, sfmt, len);
	if (r < 0)
		return -1;
	return pcm_fmt[r];
}


static const char pcm_channelstr[][7] = {
	"1",
	"2",
	"left",
	"mono",
	"right",
	"stereo",
};
static const byte pcm_channels[] = {
	1,
	2,
	0x10 | 1,
	1,
	0x20 | 1,
	2,
};

int ffpcm_channels(const char *s, size_t len)
{
	int r = ffs_findarr3(pcm_channelstr, s, len);
	if (r < 0)
		return -1;
	return pcm_channels[r];
}


static const char *const _ffpcm_channelstr[3] = {
	"mono", "stereo", "multi-channel"
};

const char* ffpcm_channelstr(uint channels)
{
	return _ffpcm_channelstr[ffmin(channels - 1, FFCNT(_ffpcm_channelstr) - 1)];
}


#define max8f  (128.0)

static FFINL short _ffpcm_flt_8(float f)
{
	double d = f * max8f;
	if (d < -max8f)
		return -0x80;
	else if (d > max8f - 1)
		return 0x7f;
	return ffint_ftoi(d);
}

#define _ffpcm_8_flt(sh)  ((float)(sh) * (1 / max8f))

#define max16f  (32768.0)

static FFINL short _ffpcm_flt_16le(double f)
{
	double d = f * max16f;
	if (d < -max16f)
		return -0x8000;
	else if (d > max16f - 1)
		return 0x7fff;
	return ffint_ftoi(d);
}

#define max24f  (8388608.0)

static FFINL int _ffpcm_flt_24(double f)
{
	double d = f * max24f;
	if (d < -max24f)
		return -0x800000;
	else if (d > max24f - 1)
		return 0x7fffff;
	return ffint_ftoi(d);
}

#define _ffpcm_24_flt(n)  ((double)(n) * (1 / max24f))

#define max32f  (2147483648.0)

static FFINL int _ffpcm_flt_32(float f)
{
	double d = f * max32f;
	if (d < -max32f)
		return -0x80000000;
	else if (d > max32f - 1)
		return 0x7fffffff;
	return ffint_ftoi(d);
}

#define _ffpcm_32_flt(n)  ((float)(n) * (1 / max32f))

static FFINL double _ffpcm_limf(double d)
{
	if (d > 1.0)
		return 1.0;
	else if (d < -1.0)
		return -1.0;
	return d;
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
	double **pd;
};

/** Set non-interleaved array from interleaved data. */
static char** pcm_setni(void **ni, void *b, uint fmt, uint nch)
{
	for (uint i = 0;  i != nch;  i++) {
		ni[i] = (char*)b + i * ffpcm_bits(fmt) / 8;
	}
	return (char**)ni;
}


void ffpcm_mix(const ffpcmex *pcm, void *stm1, const void *stm2, size_t samples)
{
	size_t i;
	uint ich, nch = pcm->channels, step = 1;
	void *ini[8], *oni[8];
	union pcmdata u1, u2;

	u1.sh = (short*)stm1;
	u2.sh = (short*)stm2;

	if (pcm->ileaved) {
		u1.pb = pcm_setni(ini, u1.b, pcm->format, nch);
		u2.pb = pcm_setni(oni, u2.b, pcm->format, nch);
		step = nch;
	}

	switch (pcm->format) {
	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				u1.psh[ich][i * step] = (short)ffint_lim16(u1.psh[ich][i * step] + u2.psh[ich][i * step]);
			}
		}
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				u1.pf[ich][i * step] = _ffpcm_limf(u1.pf[ich][i * step] + u2.pf[ich][i * step]);
			}
		}
		break;
	}
}

/* L,R -> L+R */
static int _ffpcm_mono_mix(void *out, const ffpcmex *inpcm, const void *in, size_t samples)
{
	size_t i;
	uint ich, nch = inpcm->channels, step = 1;
	union pcmdata to, from;
	void *ni[8];
	from.b = (void*)in;
	to.sh = out;

	if (inpcm->ileaved) {
		from.pb = pcm_setni(ni, from.b, inpcm->format, nch);
		step = nch;
	}

	switch (inpcm->format) {

	case FFPCM_8: {
		int sum = 0;
		for (i = 0;  i != samples;  i++) {
			for (ich = 0;  ich != nch;  ich++) {
				sum += from.pb[ich][i * step];
			}

			int n = sum % (int)nch;
			to.b[i] = ffint_lim8(sum / (int)nch + n);
			sum = -n;
		}
		break;
	}

	case FFPCM_16: {
		int sum = 0;
		for (i = 0;  i != samples;  i++) {
			for (ich = 0;  ich != nch;  ich++) {
				sum += from.psh[ich][i * step];
			}

			int n = sum % (int)nch;
			to.sh[i] = ffint_lim16(sum / (int)nch + n);
			sum = -n;
		}
		break;
	}

	case FFPCM_24: {
		int sum = 0;
		for (i = 0;  i != samples;  i++) {
			for (ich = 0;  ich != nch;  ich++) {
				sum += (int)ffint_ltoh24s(&from.pb[ich][i * step * 3]);
			}

			int n = sum % (int)nch;
			ffint_htol24(&to.b[i * 3], ffint_lim24(sum / (int)nch + n));
			sum = -n;
		}
		break;
	}

	case FFPCM_32: {
		int64 sum = 0;
		for (i = 0;  i != samples;  i++) {
			for (ich = 0;  ich != nch;  ich++) {
				sum += from.pin[ich][i * step];
			}

			int n = sum % (int)nch;
			to.in[i] = ffint_lim32(sum / (int)nch + n);
			sum = -n;
		}
		break;
	}

	case FFPCM_FLOAT: {
		double sum;
		for (i = 0;  i != samples;  i++) {
			sum = 0;
			for (ich = 0;  ich != nch;  ich++) {
				sum += from.pf[ich][i * step];
			}

			to.f[i] = sum * (1.0 / nch);
		}
		break;
	}

	default:
		return -1;
	}

	return 0;
}

#define CASE(f1, f2) \
	(f1 << 16) | (f2 & 0xffff)

/*
If channels don't match, do channel conversion:
 . downmix to mono: use all input channels data.  Requires additional memory buffer.
 . mono: take data for 1 channel only, skip other channels
 . expand from mono: copy the input channel's data to each output channel
If format and "interleaved" flags match for both input and output, just copy the data.
Otherwise, process each channel and sample in a loop.

non-interleaved: data[0][..] - left,  data[1][..] - right
interleaved: data[0,2..] - left */
int ffpcm_convert(const ffpcmex *outpcm, void *out, const ffpcmex *inpcm, const void *in, size_t samples)
{
	size_t i;
	uint ich, nch = inpcm->channels, in_ileaved = inpcm->ileaved;
	union pcmdata from, to;
	void *tmpptr = NULL;
	int r = -1;
	void *ini[8], *oni[8];
	uint istep = 1, ostep = 1;

	from.sh = (void*)in;
	to.sh = out;

	if (inpcm->channels > 8 || (outpcm->channels & FFPCM_CHMASK) > 8)
		goto done;

	if (inpcm->sample_rate != outpcm->sample_rate)
		goto done;

	if (inpcm->channels != outpcm->channels) {

		nch = outpcm->channels & FFPCM_CHMASK;

		if (nch == 1) {
			if ((outpcm->channels & ~FFPCM_CHMASK) == 0) {
				if (NULL == (tmpptr = ffmem_alloc(samples * ffpcm_bits(inpcm->format)/8)))
					goto done;

				if (0 != _ffpcm_mono_mix(tmpptr, inpcm, in, samples))
					goto done;
				from.psh = (short**)&tmpptr;

			} else if (!inpcm->ileaved) {
				uint ch = ((outpcm->channels & ~FFPCM_CHMASK) >> 4) - 1;
				from.psh = from.psh + ch;

			} else {
				uint ch = ((outpcm->channels & ~FFPCM_CHMASK) >> 4) - 1;
				ini[0] = from.b + ch * ffpcm_bits(inpcm->format) / 8;
				from.pb = (void*)ini;
				istep = inpcm->channels;
			}

			in_ileaved = 0;

		} else if (inpcm->channels == 1) {
			if (in_ileaved) {
				for (uint i = 0;  i != nch;  i++) {
					ini[i] = from.b;
				}
				from.pb = (void*)ini;
				in_ileaved = 0;
			} else if (samples != 0) {
				for (uint i = 0;  i != nch;  i++) {
					ini[i] = from.pb[0];
				}
				from.pb = (void*)ini;
			}

		} else
			goto done; // this channel conversion is not supported
	}

	if (inpcm->format == outpcm->format && istep == 1) {

		if (in_ileaved != outpcm->ileaved && nch == 1) {
			if (samples == 0)
			{}
			else if (!in_ileaved)
				from.b = from.pb[0];
			else {
				ini[0] = from.b;
				from.pb = (void*)ini;
			}
			in_ileaved = outpcm->ileaved;
		}

		if (in_ileaved == outpcm->ileaved) {
			if (samples == 0)
				;
			else if (in_ileaved == 1)
				ffmemcpy(to.b, from.b, samples * ffpcm_size(inpcm->format, nch));
			else {
				for (ich = 0;  ich != nch;  ich++) {
					ffmemcpy(to.pb[ich], from.pb[ich], samples * ffpcm_bits(inpcm->format)/8);
				}
			}
			r = 0;
			goto done;
		}
	}

	if (in_ileaved) {
		from.pb = pcm_setni(ini, from.b, inpcm->format, nch);
		istep = nch;
	}

	if (outpcm->ileaved) {
		to.pb = pcm_setni(oni, to.b, outpcm->format, nch);
		ostep = nch;
	}

	switch (CASE(inpcm->format, outpcm->format)) {

// int8
	case CASE(FFPCM_8, FFPCM_8):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep] = from.pb[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_8, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = (int)from.pb[ich][i * istep] * 0x100;
			}
		}
		break;

// int16
	case CASE(FFPCM_16, FFPCM_8):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep] = from.psh[ich][i * istep] / 0x100;
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = from.psh[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], (int)from.psh[ich][i * istep] * 0x100);
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffint_htol24(&to.pb[ich][i * ostep * 4 + 1], (int)from.psh[ich][i * istep] * 0x100);
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = (int)from.psh[ich][i * istep] * 0x10000;
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = _ffpcm_16le_flt(from.psh[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_16, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = _ffpcm_16le_flt(from.psh[ich][i * istep]);
			}
		}
		break;

// int24
	case CASE(FFPCM_24, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = ffint_ltoh24(&from.pb[ich][i * istep * 3]) / 0x100;
			}
		}
		break;


	case CASE(FFPCM_24, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffmemcpy(&to.pb[ich][i * ostep * 3], &from.pb[ich][i * istep * 3], 3);
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffmemcpy(&to.pb[ich][i * ostep * 4 + 1], &from.pb[ich][i * istep * 3], 3);
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = ffint_ltoh24(&from.pb[ich][i * istep * 3]) * 0x100;
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = _ffpcm_24_flt(ffint_ltoh24s(&from.pb[ich][i * istep * 3]));
			}
		}
		break;

	case CASE(FFPCM_24, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = _ffpcm_24_flt(ffint_ltoh24s(&from.pb[ich][i * istep * 3]));
			}
		}
		break;

// int32
	case CASE(FFPCM_32, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = from.pin[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], from.pin[ich][i * istep] / 0x100);
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffint_htol24(&to.pb[ich][i * ostep * 4 + 1], from.pin[ich][i * istep] / 0x100);
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = from.pin[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_32, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = _ffpcm_32_flt(from.pin[ich][i * istep]);
			}
		}
		break;

// float32
	case CASE(FFPCM_FLOAT, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = _ffpcm_flt_16le(from.pf[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], _ffpcm_flt_24(from.pf[ich][i * istep]));
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_24_4):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * ostep * 4 + 0] = 0;
				ffint_htol24(&to.pb[ich][i * ostep * 4 + 1], _ffpcm_flt_24(from.pf[ich][i * istep]));
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_32):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * ostep] = _ffpcm_flt_32(from.pf[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = from.pf[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_FLOAT, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = from.pf[ich][i * istep];
			}
		}
		break;

// float64
	case CASE(FFPCM_FLOAT64, FFPCM_16):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * ostep] = _ffpcm_flt_16le(from.pd[ich][i * istep]);
			}
		}
		break;

	case CASE(FFPCM_FLOAT64, FFPCM_24):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ich][i * ostep * 3], _ffpcm_flt_24(from.pd[ich][i * istep]));
			}
		}
		break;

	case CASE(FFPCM_FLOAT64, FFPCM_FLOAT):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * ostep] = from.pd[ich][i * istep];
			}
		}
		break;

	case CASE(FFPCM_FLOAT64, FFPCM_FLOAT64):
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pd[ich][i * ostep] = from.pd[ich][i * istep];
			}
		}
		break;

	default:
		goto done;
	}
	r = 0;

done:
	ffmem_safefree(tmpptr);
	return r;
}

#undef CASE


int ffpcm_gain(const ffpcmex *pcm, float gain, const void *in, void *out, uint samples)
{
	uint i, ich, step = 1, nch = pcm->channels;
	void *ini[8], *oni[8];
	union pcmdata from, to;

	if (gain == 1)
		return 0;

	if (pcm->channels > 8)
		return -1;

	from.sh = (void*)in;
	to.sh = out;

	if (pcm->ileaved) {
		from.pb = pcm_setni(ini, from.b, pcm->format, nch);
		to.pb = pcm_setni(oni, to.b, pcm->format, nch);
		step = nch;
	}

	switch (pcm->format) {
	case FFPCM_8:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ich][i * step] = _ffpcm_flt_8(_ffpcm_8_flt(from.pb[ich][i * step]) * gain);
			}
		}
		break;

	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ich][i * step] = _ffpcm_flt_16le(_ffpcm_16le_flt(from.psh[ich][i * step]) * gain);
			}
		}
		break;

	case FFPCM_24:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh24s(&from.pb[ich][i * step * 3]);
				ffint_htol24(&to.pb[ich][i * step * 3], _ffpcm_flt_24(_ffpcm_24_flt(n) * gain));
			}
		}
		break;

	case FFPCM_32:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pin[ich][i * step] = _ffpcm_flt_32(_ffpcm_32_flt(from.pin[ich][i * step]) * gain);
			}
		}
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				to.pf[ich][i * step] = from.pf[ich][i * step] * gain;
			}
		}
		break;

	default:
		return -1;
	}

	return 0;
}


int ffpcm_peak(const ffpcmex *fmt, const void *data, size_t samples, double *maxpeak)
{
	double max_f = 0.0;
	uint max_sh = 0;
	uint ich, nch = fmt->channels, step = 1;
	size_t i;
	void *ni[8];
	union pcmdata d;
	d.sh = (void*)data;

	if (fmt->channels > 8)
		return 1;

	if (fmt->ileaved) {
		d.pb = pcm_setni(ni, d.b, fmt->format, nch);
		step = nch;
	}

	switch (fmt->format) {

	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				uint sh = ffabs(d.psh[ich][i * step]);
				if (max_sh < sh)
					max_sh = sh;
			}
		}
		max_f = _ffpcm_16le_flt(max_sh);
		break;

	case FFPCM_24:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh24s(&d.pb[ich][i * step * 3]);
				uint u = ffabs(n);
				if (max_sh < u)
					max_sh = u;
			}
		}
		max_f = _ffpcm_24_flt(max_sh);
		break;

	case FFPCM_32:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh32(&d.pin[ich][i * step]);
				uint u = ffabs(n);
				if (max_sh < u)
					max_sh = u;
			}
		}
		max_f = _ffpcm_32_flt(max_sh);
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				double f = ffabs(d.pf[ich][i * step]);
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

ssize_t ffpcm_process(const ffpcmex *fmt, const void *data, size_t samples, ffpcm_process_func func, void *udata)
{
	double f;
	union pcmdata d;
	void *ni[8];
	uint i, ich, nch = fmt->channels, step = 1, u;

	d.sh = (void*)data;

	if (fmt->channels > 8)
		return -1;

	if (fmt->ileaved) {
		d.pb = pcm_setni(ni, d.b, fmt->format, nch);
		step = nch;
	}

	switch (fmt->format) {

	case FFPCM_16:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				u = ffabs(d.psh[ich][i * step]);
				f = _ffpcm_16le_flt(u);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	case FFPCM_24:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh24s(&d.pb[ich][i * step * 3]);
				u = ffabs(n);
				f = _ffpcm_24_flt(u);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	case FFPCM_32:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				int n = ffint_ltoh32(&d.pin[ich][i * step]);
				u = ffabs(n);
				f = _ffpcm_32_flt(u);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	case FFPCM_FLOAT:
		for (ich = 0;  ich != nch;  ich++) {
			for (i = 0;  i != samples;  i++) {
				f = ffabs(d.pf[ich][i * step]);
				if (0 != func(udata, f))
					goto done;
			}
		}
		break;

	default:
		return -2;
	}

	return -1;

done:
	return i;
}


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
