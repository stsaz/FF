/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/pcm.h>


const byte ffpcm_bits[] = { 16, 32, 32 };


static const char *const _ffpcm_channelstr[3] = {
	"mono", "stereo", "multi-channel"
};

const char* ffpcm_channelstr(uint channels)
{
	return _ffpcm_channelstr[ffmin(channels - 1, FFCNT(_ffpcm_channelstr) - 1)];
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

				int r = *u1.sh + *u2.sh++;
				if (r < -0x7fff)
					r = -0x7fff;
				else if (r > 0x8000)
					r = 0x8000;

				*u1.sh++ = (short)r;
			}
		}
		break;

	case FFPCM_32LE:
		for (i = 0;  i < samples;  i++) {
			for (ich = 0;  ich < pcm->channels;  ich++) {

				int64 r = *u1.i + *u2.i++;
				if (r < -0x7fffffff)
					r = -0x7fffffff;
				else if (r > 0x80000000)
					r = 0x80000000;

				*u1.i++ = (int)r;
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
