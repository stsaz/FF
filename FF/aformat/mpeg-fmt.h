/** MPEG format.
Copyright (c) 2016 Simon Zolin
*/

/* .mp3 format:
[ID3v2]
MPEG_HDR [XING [LAME]] [VBRI]
(MPEG_HDR DATA)...
[APE_TAG]
[ID3v1]
*/

#pragma once

#include <FF/string.h>
#include <FF/number.h>


enum FFMPG_VER {
	FFMPG_1 = 3,
	FFMPG_2 = 2,
	FFMPG_2_5 = 0,
};

enum FFMPG_LAYER {
	FFMPG_L3 = 1,
	FFMPG_L2,
	FFMPG_L1,
};

enum FFMPG_CHANNEL {
	FFMPG_STEREO,
	FFMPG_JOINT,
	FFMPG_DUAL,
	FFMPG_MONO,
};

typedef struct ffmpg_hdr {
	ffbyte sync1 :8; //0xff

#if defined FF_BIG_ENDIAN
	ffbyte sync2 :3 //0x07
		, ver :2 //enum FFMPG_VER
		, layer :2 //enum FFMPG_LAYER
		, noprotect :1; //0: protected by CRC

	ffbyte bitrate :4
		, sample_rate :2
		, padding :1 //for L3 +1 ffbyte in frame
		, priv :1;

	ffbyte channel :2 //enum FFMPG_CHANNEL
		, modeext :2 //mode extension (for Joint Stereo)
		, copyright :1
		, original :1
		, emphasis :2;

#else
	ffbyte noprotect :1 //0: protected by CRC
		, layer :2 //enum FFMPG_LAYER
		, ver :2 //enum FFMPG_VER
		, sync2 :3; //0x07

	ffbyte priv :1
		, padding :1 //for L3 +1 ffbyte in frame
		, sample_rate :2
		, bitrate :4;

	ffbyte emphasis :2
		, original :1
		, copyright :1
		, modeext :2 //mode extension (for Joint Stereo)
		, channel :2; //enum FFMPG_CHANNEL
#endif
} ffmpg_hdr;

/** Return TRUE if valid MPEG header */
static inline ffbool ffmpg_hdr_valid(const ffmpg_hdr *h)
{
	return (ffint_be_cpu16_ptr(h) & 0xffe0) == 0xffe0
		&& h->ver != 1
		&& h->layer != 0
		&& h->bitrate != 0 && h->bitrate != 15
		&& h->sample_rate != 3;
}

/** Get bitrate (bps) */
static inline ffuint ffmpg_hdr_bitrate(const ffmpg_hdr *h)
{
	static const ffbyte mpg_kbyterate[2][3][16] = {
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
	return (ffuint)mpg_kbyterate[h->ver != FFMPG_1][3 - h->layer][h->bitrate] * 8 * 1000;
}

/** Get sample rate (Hz) */
static inline ffuint ffmpg_hdr_sample_rate(const ffmpg_hdr *h)
{
	static const ffushort mpg_sample_rate[4][3] = {
		{ 44100, 48000, 32000 }, //MPEG-1
		{ 44100/2, 48000/2, 32000/2 }, //MPEG-2
		{ 0, 0, 0 },
		{ 44100/4, 48000/4, 32000/4 }, //MPEG-2.5
	};
	return mpg_sample_rate[3 - h->ver][h->sample_rate];
}

/** Get channels */
#define ffmpg_hdr_channels(h) \
	((h)->channel == FFMPG_MONO ? 1 : 2)

static inline ffuint ffmpg_hdr_frame_samples(const ffmpg_hdr *h)
{
	static const ffbyte mpg_frsamps[2][3] = {
		{ 384/8, 1152/8, 1152/8 }, //MPEG-1
		{ 384/8, 1152/8, 576/8 }, //MPEG-2
	};
	return mpg_frsamps[h->ver != FFMPG_1][3 - h->layer] * 8;
}

/** Get length of MPEG frame data */
static inline ffuint ffmpg_hdr_framelen(const ffmpg_hdr *h)
{
	return ffmpg_hdr_frame_samples(h)/8 * ffmpg_hdr_bitrate(h) / ffmpg_hdr_sample_rate(h)
		+ ((h->layer != FFMPG_L1) ? h->padding : h->padding * 4);
}

//bits in each MPEG header that must not change across frames within the same stream
#define MPG_HDR_CONST_MASK  0xfffe0c00 // 1111 1111  1111 1110  0000 1100  0000 0000

/** Search for a valid frame.
h: (optional) a newly found header must match with this one */
static inline ffmpg_hdr* ffmpg_framefind(const char *data, ffsize len, const ffmpg_hdr *h)
{
	const char *d = data, *end = d + len;

	while (d != end) {
		if ((ffbyte)d[0] != 0xff && NULL == (d = ffs_findc(d, end - d, 0xff)))
			break;

		if ((end - d) >= (ssize_t)sizeof(ffmpg_hdr)
			&& ffmpg_hdr_valid((void*)d)
			&& (h == NULL || (ffint_be_cpu32_ptr(d) & MPG_HDR_CONST_MASK) == (ffint_be_cpu32_ptr(h) & MPG_HDR_CONST_MASK))) {
			return (void*)d;
		}

		d++;
	}

	return 0;
}


struct ffmpg_info {
	ffuint frames;
	ffuint bytes;
	int vbr_scale; //100(worst)..0(best)
	ffbyte toc[100];
	ffuint vbr :1;
};

/** Convert sample number to stream offset (in bytes) */
static inline ffuint64 ffmpg_xing_seekoff(const ffbyte *toc, ffuint64 sample, ffuint64 total_samples, ffuint64 total_size)
{
	FF_ASSERT(sample < total_samples);

	double d = sample * 100.0 / total_samples;
	ffuint i = (int)d;
	d -= i;
	ffuint i1 = toc[i];
	ffuint i2 = (i != 99) ? toc[i + 1] : 256;

	return (i1 + (i2 - i1) * d) * total_size / 256.0;
}

enum MPG_XING_FLAGS {
	MPG_XING_FRAMES = 1,
	MPG_XING_BYTES = 2,
	MPG_XING_TOC = 4,
	MPG_XING_VBRSCALE = 8,
};

//8..120 bytes
struct mpg_xing {
	char id[4]; //"Xing"(VBR) or "Info"(CBR)
	ffbyte flags[4]; //enum MPG_XING_FLAGS
	// ffbyte frames[4];
	// ffbyte bytes[4];
	// ffbyte toc[100];
	// ffbyte vbr_scale[4]; //100(worst)..0(best)
};

static ffbool mpg_xing_valid(const char *data)
{
	return !ffs_cmp(data, "Xing", 4) || !ffs_cmp(data, "Info", 4);
}

static ffuint mpg_xing_size(ffuint flags)
{
	ffuint all = 4 + sizeof(int);
	if (flags & MPG_XING_FRAMES)
		all += sizeof(int);
	if (flags & MPG_XING_BYTES)
		all += sizeof(int);
	if (flags & MPG_XING_TOC)
		all += 100;
	if (flags & MPG_XING_VBRSCALE)
		all += sizeof(int);
	return all;
}

static int mpg_xing_off(const char *data)
{
	const ffmpg_hdr *h = (void*)data;
	static const ffbyte mpg_xingoffs[2][2] = {
		{17, 32}, //MPEG-1: MONO, 2-CH
		{9, 17}, //MPEG-2
	};
	return sizeof(ffmpg_hdr) + mpg_xingoffs[h->ver != FFMPG_1][ffmpg_hdr_channels(h) - 1];
}

/** Parse Xing tag.
Return the number of bytes read;
 <0 on error */
static inline int ffmpg_xing_parse(struct ffmpg_info *xing, const char *data, ffsize len)
{
	const char *dstart = data;
	FFS_SHIFT(data, len, mpg_xing_off(data));

	if (len < 8 || !mpg_xing_valid(data))
		return -1;

	xing->vbr = !ffs_cmp(data, "Xing", 4);
	data += 4;

	ffuint flags = ffint_be_cpu32_ptr(data);
	data += sizeof(int);
	if (len < mpg_xing_size(flags))
		return -1;

	if (flags & MPG_XING_FRAMES) {
		xing->frames = ffint_be_cpu32_ptr(data);
		data += sizeof(int);
	}

	if (flags & MPG_XING_BYTES) {
		xing->bytes = ffint_be_cpu32_ptr(data);
		data += sizeof(int);
	}

	if (flags & MPG_XING_TOC) {
		ffmem_copy(xing->toc, data, 100);
		data += 100;
	}

	if (flags & MPG_XING_VBRSCALE) {
		xing->vbr_scale = ffint_be_cpu32_ptr(data);
		data += sizeof(int);
	} else
		xing->vbr_scale = -1;

	return data - dstart;
}

/** Write Xing tag.
Note: struct ffmpg_info.toc isn't supported */
static inline int ffmpg_xing_write(const struct ffmpg_info *xing, char *data)
{
	char *d = data;
	struct mpg_xing *x = (void*)(data + mpg_xing_off(data));

	if (xing->vbr)
		ffmem_copy(x->id, "Xing", 4);
	else
		ffmem_copy(x->id, "Info", 4);

	ffuint flags = 0;
	if (xing->frames != 0)
		flags |= MPG_XING_FRAMES;
	if (xing->bytes != 0)
		flags |= MPG_XING_BYTES;
	if (xing->vbr_scale != -1)
		flags |= MPG_XING_VBRSCALE;
	ffint_hton32(x->flags, flags);
	d = (void*)(x + 1);

	if (xing->frames != 0) {
		ffint_hton32(d, xing->frames);
		d += 4;
	}

	if (xing->bytes != 0) {
		ffint_hton32(d, xing->bytes);
		d += 4;
	}

	if (xing->vbr_scale != -1) {
		ffint_hton32(d, xing->vbr_scale);
		d += 4;
	}

	return d - data;
}

struct mpg_vbri {
	ffbyte id[4]; //"VBRI"
	ffbyte ver[2];
	ffbyte delay[2];
	ffbyte quality[2];
	ffbyte bytes[4];
	ffbyte frames[4];

	//seekpt[N] = { toc_ent_frames * N => (toc[0] + ... + toc[N-1]) * toc_scale }
	ffbyte toc_ents[2];
	ffbyte toc_scale[2];
	ffbyte toc_ent_size[2];
	ffbyte toc_ent_frames[2];
	ffbyte toc[0];
};

/** Parse VBRI tag.
Return the number of bytes read;
 <0 on error */
static inline int ffmpg_vbri(struct ffmpg_info *info, const char *data, ffsize len)
{
	enum { VBRI_OFF = 32 };
	if (sizeof(ffmpg_hdr) + VBRI_OFF + sizeof(struct mpg_vbri) > len)
		return -1;

	const struct mpg_vbri *vbri = (void*)(data + sizeof(ffmpg_hdr) + VBRI_OFF);
	if (!!ffs_cmp(vbri->id, "VBRI", 4)
		|| 1 != ffint_be_cpu16_ptr(vbri->ver))
		return -1;

	info->frames = ffint_be_cpu32_ptr(vbri->frames);
	info->bytes = ffint_be_cpu32_ptr(vbri->bytes);
	info->vbr = 1;
	ffuint sz = sizeof(ffmpg_hdr) + VBRI_OFF + sizeof(struct mpg_vbri) + ffint_be_cpu16_ptr(vbri->toc_ents) * ffint_be_cpu16_ptr(vbri->toc_ent_size);
	if (sz > len)
		return -1;
	return sz;
}


struct ffmpg_lame {
	char id[9]; //e.g. "LAME3.90a"
	ffushort enc_delay;
	ffushort enc_padding;
};

struct mpg_lamehdr {
	char id[9];
	ffbyte unsupported1[12];
	ffbyte delay_padding[3]; // delay[12]  padding[12]
	ffbyte unsupported2[12];
};

/** Parse LAME tag.
Return the number of bytes read;
 <0 on error */
static inline int ffmpg_lame_parse(struct ffmpg_lame *lame, const char *data, ffsize len)
{
	const struct mpg_lamehdr *h = (void*)data;

	if (len < sizeof(struct ffmpg_lame))
		return -1;

	ffmem_copy(lame->id, h->id, sizeof(lame->id));
	lame->enc_delay = ffint_be_cpu16_ptr(h->delay_padding) >> 4; //DDDX -> DDD
	lame->enc_padding = ffint_be_cpu16_ptr(h->delay_padding + 1) & 0x0fff; //XPPP -> PPP
	return sizeof(struct mpg_lamehdr);
}
