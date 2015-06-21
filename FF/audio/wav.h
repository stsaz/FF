/** WAVE format.
Copyright (c) 2015 Simon Zolin
*/

/*
ffwav_riff  ([ffwav_fmt]  [ffwav_data  DATA...])
*/

#pragma once

#include <FF/string.h>


enum FFWAV_FMT {
	FFWAV_PCM = 1
	, FFWAV_IEEE_FLOAT = 3
	, FFWAV_EXT = 0xfffe
};

FF_EXTN const char* ffwav_fmtstr(uint fmt);

typedef struct ffwav_riff {
	char riff[4]; //"RIFF"
	uint size; //number of all following bytes, little-endian
	char wave[4]; //"WAVE"
} ffwav_riff;

typedef struct ffwav_fmt {
	char fmt[4]; //"fmt "
	uint size; //16, 20 or 40 (ffwav_ext)
	ushort format; //enum FFWAV_FMT
	ushort channels;
	uint sample_rate;
	uint byte_rate;
	ushort block_align;
	ushort bit_depth; //bits per sample
} ffwav_fmt;

typedef struct ffwav_ext {
	ffwav_fmt fmt;

	ushort size; //22
	ushort valid_bits_per_sample;// = fmt.bit_depth
	uint channel_mask;//0x03 for stereo
	byte subformat[16]; //[0..1]: enum FFWAV_FMT
} ffwav_ext;

typedef struct ffwav_data {
	char data[4]; //"data"
	uint size;
} ffwav_data;

typedef struct ffwavpcmhdr {
	ffwav_riff wr;
	ffwav_fmt wf;
	ffwav_data wd;
} ffwavpcmhdr;

/** WAVE PCM header.  44.1kHz, 16bit, Stereo. */
FF_EXTN const ffwavpcmhdr ffwav_pcmhdr;

static FFINL uint ffwav_samplesize(const ffwav_fmt *wf)
{
	return (uint)wf->bit_depth/8 * wf->channels;
}

/** bps */
#define ffwav_bitrate(wf) \
	((wf)->sample_rate * ffwav_samplesize(wf) * 8)

/** Number of PCM samples. */
#define ffwav_samples(wf, size) \
	((size) / ffwav_samplesize(wf))

static FFINL void ffwav_setbr(ffwav_fmt *wf)
{
	wf->block_align = ffwav_samplesize(wf);
	wf->byte_rate = wf->sample_rate * wf->block_align;
}

/** Get WAV format from extended header. */
static FFINL ushort ffwav_extfmt(const ffwav_ext *we)
{
	return ((short)we->subformat[1] << 16) | we->subformat[0];
}

/** Convert between WAV format and PCM format.
@wf: must be a valid ffwav_fmt or ffwav_ext data. */
FF_EXTN uint ffwav_pcmfmt(const ffwav_fmt *wf);
FF_EXTN void ffwav_pcmfmtset(ffwav_fmt *wf, uint pcm_fmt);


enum FFWAV_R {
	FFWAV_ERR = 1 << 31
	, FFWAV_MORE = 1 << 30

	, FFWAV_RRIFF = 1
	, FFWAV_RFMT
	, FFWAV_REXT
	, FFWAV_RDATA
};

/** Get the next chunk.
Return enum FFWAV_R. */
FF_EXTN int ffwav_parse(const char *data, size_t *len);
