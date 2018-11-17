/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/audio/flac.h>
#include <FF/number.h>
#include <FF/crc.h>
#include <FFOS/error.h>


#define ERR(f, e) \
	(f)->errtype = e,  FFFLAC_RERR

static int pcm_from32(const int **src, void **dst, uint dstbits, uint channels, uint samples);
static int pcm_to32(int **dst, const void **src, uint srcbits, uint channels, uint samples);

extern const char* _ffflac_errstr(uint e);


/** Convert data between 32bit integer and any other integer PCM format.
e.g. 16bit: "11 22 00 00" <-> "11 22" */

static int pcm_from32(const int **src, void **dst, uint dstbits, uint channels, uint samples)
{
	uint ic, i;
	union {
	char **pb;
	short **psh;
	} to;
	to.psh = (void*)dst;

	switch (dstbits) {
	case 8:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				to.pb[ic][i] = (char)src[ic][i];
			}
		}
		break;

	case 16:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				to.psh[ic][i] = (short)src[ic][i];
			}
		}
		break;

	case 24:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				ffint_htol24(&to.pb[ic][i * 3], src[ic][i]);
			}
		}
		break;

	default:
		return -1;
	}
	return 0;
}

static int pcm_to32(int **dst, const void **src, uint srcbits, uint channels, uint samples)
{
	uint ic, i;
	union {
	char **pb;
	short **psh;
	} from;
	from.psh = (void*)src;

	switch (srcbits) {
	case 8:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				dst[ic][i] = from.pb[ic][i];
			}
		}
		break;

	case 16:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				dst[ic][i] = from.psh[ic][i];
			}
		}
		break;

	case 24:
		for (ic = 0;  ic != channels;  ic++) {
			for (i = 0;  i != samples;  i++) {
				dst[ic][i] = ffint_ltoh24s(&from.pb[ic][i * 3]);
			}
		}
		break;

	default:
		return -1;
	}

	return 0;
}


const char* ffflac_dec_errstr(ffflac_dec *f)
{
	switch (f->errtype) {
	case FLAC_ESYS:
		return fferr_strp(fferr_last());

	case FLAC_ELIB:
		return flac_errstr(f->err);
	}

	return _ffflac_errstr(f->errtype);
}

int ffflac_dec_open(ffflac_dec *f, const ffflac_info *info)
{
	int r;
	flac_conf si = {0};
	si.min_blocksize = info->minblock;
	si.max_blocksize = info->maxblock;
	si.rate = info->sample_rate;
	si.channels = info->channels;
	si.bps = info->bits;
	if (0 != (r = flac_decode_init(&f->dec, &si))) {
		f->errtype = FLAC_ELIB;
		f->err = r;
		return FFFLAC_RERR;
	}
	f->info = *info;
	return 0;
}

void ffflac_dec_close(ffflac_dec *f)
{
	if (f->dec != NULL)
		flac_decode_free(f->dec);
}

int ffflac_decode(ffflac_dec *f)
{
	int r;
	const int **out;
	uint isrc, ich;

	r = flac_decode(f->dec, f->in.ptr, f->in.len, &out);
	if (r != 0) {
		f->errtype = FLAC_ELIB;
		f->err = r;
		return FFFLAC_RWARN;
	}

	f->pcm = (void**)f->out;
	isrc = 0;
	if (f->seeksample != 0) {
		FF_ASSERT(f->seeksample >= f->frsample);
		isrc = f->seeksample - f->frsample;
		f->pcmlen -= isrc;
		f->frsample = f->seeksample;
		f->seeksample = 0;
	}

	for (ich = 0;  ich != f->info.channels;  ich++) {
		f->out[ich] = out[ich];
	}

	const int *out2[FLAC__MAX_CHANNELS];
	for (ich = 0;  ich != f->info.channels;  ich++) {
		out2[ich] = out[ich] + isrc;
	}

	//in-place conversion
	pcm_from32(out2, (void*)f->out, f->info.bits, f->info.channels, f->pcmlen);
	f->pcmlen *= f->info.bits / 8 * f->info.channels;
	return FFFLAC_RDATA;
}


const char* ffflac_enc_errstr(ffflac_enc *f)
{
	ffflac_dec fl;
	fl.errtype = f->errtype;
	fl.err = f->err;
	return ffflac_dec_errstr(&fl);
}

enum ENC_STATE {
	ENC_HDR, ENC_FRAMES, ENC_DONE
};

void ffflac_enc_init(ffflac_enc *f)
{
	ffmem_tzero(f);
	f->level = 5;
}

void ffflac_enc_close(ffflac_enc *f)
{
	ffarr_free(&f->outbuf);
	FF_SAFECLOSE(f->enc, NULL, flac_encode_free);
}

int ffflac_create(ffflac_enc *f, ffpcm *pcm)
{
	int r;

	switch (pcm->format) {
	case FFPCM_8:
	case FFPCM_16:
	case FFPCM_24:
		break;

	default:
		pcm->format = FFPCM_24;
		return ERR(f, FLAC_EFMT);
	}

	flac_conf conf = {0};
	conf.bps = ffpcm_bits(pcm->format);
	conf.channels = pcm->channels;
	conf.rate = pcm->sample_rate;
	conf.level = f->level;
	conf.nomd5 = !!(f->opts & FFFLAC_ENC_NOMD5);

	if (0 != (r = flac_encode_init(&f->enc, &conf))) {
		f->err = r;
		return ERR(f, FLAC_ELIB);
	}

	flac_conf info;
	flac_encode_info(f->enc, &info);
	f->info.minblock = info.min_blocksize;
	f->info.maxblock = info.max_blocksize;
	f->info.channels = pcm->channels;
	f->info.sample_rate = pcm->sample_rate;
	f->info.bits = ffpcm_bits(pcm->format);
	return 0;
}

/*
Encode audio data into FLAC frames.
  An input sample must be within 32-bit container.
  To encode a frame libFLAC needs NBLOCK+1 input samples.
  flac_encode() returns a frame with NBLOCK encoded samples,
    so 1 sample always stays cached in libFLAC until we explicitly flush output data.
*/
int ffflac_encode(ffflac_enc *f)
{
	uint samples, sampsize, blksize;
	int r;

	switch (f->state) {

	case ENC_HDR:
		if (NULL == ffarr_realloc(&f->outbuf, (f->info.minblock + 1) * sizeof(int) * f->info.channels))
			return ERR(f, FLAC_ESYS);
		for (uint i = 0;  i != f->info.channels;  i++) {
			f->pcm32[i] = (void*)(f->outbuf.ptr + (f->info.minblock + 1) * sizeof(int) * i);
		}
		f->cap_pcm32 = f->info.minblock + 1;

		f->state = ENC_FRAMES;
		// break

	case ENC_FRAMES:
		break;

	case ENC_DONE: {
		flac_conf info = {0};
		flac_encode_info(f->enc, &info);
		f->info.minblock = info.min_blocksize;
		f->info.maxblock = info.max_blocksize;
		f->info.minframe = info.min_framesize;
		f->info.maxframe = info.max_framesize;
		ffmemcpy(f->info.md5, info.md5, sizeof(f->info.md5));
		return FFFLAC_RDONE;
	}
	}

	sampsize = f->info.bits/8 * f->info.channels;
	samples = ffmin(f->pcmlen / sampsize - f->off_pcm, f->cap_pcm32 - f->off_pcm32);

	if (samples == 0 && !f->fin) {
		f->off_pcm = 0;
		return FFFLAC_RMORE;
	}

	const void* src[FLAC__MAX_CHANNELS];
	int* dst[FLAC__MAX_CHANNELS];

	for (uint i = 0;  i != f->info.channels;  i++) {
		src[i] = (char*)f->pcm[i] + f->off_pcm * f->info.bits/8;
		dst[i] = f->pcm32[i] + f->off_pcm32;
	}

	if (0 != (r = pcm_to32(dst, src, f->info.bits, f->info.channels, samples)))
		return ERR(f, FLAC_EFMT);

	f->off_pcm += samples;
	f->off_pcm32 += samples;
	if (!(f->off_pcm32 == f->cap_pcm32 || f->fin)) {
		f->off_pcm = 0;
		return FFFLAC_RMORE;
	}

	samples = f->off_pcm32;
	f->off_pcm32 = 0;
	r = flac_encode(f->enc, (const int**)f->pcm32, &samples, (char**)&f->data);
	if (r < 0)
		return f->errtype = FLAC_ELIB,  f->err = r,  FFFLAC_RERR;

	blksize = f->info.minblock;
	if (r == 0 && f->fin) {
		samples = 0;
		r = flac_encode(f->enc, (const int**)f->pcm32, &samples, (char**)&f->data);
		if (r < 0)
			return f->errtype = FLAC_ELIB,  f->err = r,  FFFLAC_RERR;
		blksize = samples;
		f->state = ENC_DONE;
	}

	FF_ASSERT(r != 0);
	FF_ASSERT(samples == f->cap_pcm32 || f->fin);

	if (f->cap_pcm32 == f->info.minblock + 1)
		f->cap_pcm32 = f->info.minblock;

	f->frsamps = blksize;
	f->datalen = r;
	return FFFLAC_RDATA;
}
