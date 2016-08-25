/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/audio/mpeg-fmt.h>
#include <FF/string.h>
#include <FF/number.h>


static const byte mpg_kbyterate[2][3][16] = {
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

uint ffmpg_hdr_bitrate(const ffmpg_hdr *h)
{
	return (uint)mpg_kbyterate[h->ver != FFMPG_1][3 - h->layer][h->bitrate] * 8 * 1000;
}

static const ushort mpg_sample_rate[4][3] = {
	{ 44100, 48000, 32000 }, //MPEG-1
	{ 44100/2, 48000/2, 32000/2 }, //MPEG-2
	{ 0, 0, 0 },
	{ 44100/4, 48000/4, 32000/4 }, //MPEG-2.5
};

uint ffmpg_hdr_sample_rate(const ffmpg_hdr *h)
{
	return mpg_sample_rate[3 - h->ver][h->sample_rate];
}

static const byte mpg_frsamps[2][3] = {
	{ 384/8, 1152/8, 1152/8 }, //MPEG-1
	{ 384/8, 1152/8, 576/8 }, //MPEG-2
};

uint ffmpg_hdr_frame_samples(const ffmpg_hdr *h)
{
	return mpg_frsamps[h->ver != FFMPG_1][3 - h->layer] * 8;
}

const char ffmpg_strchannel[4][8] = {
	"stereo", "joint", "dual", "mono"
};

ffbool ffmpg_hdr_valid(const ffmpg_hdr *h)
{
	return (ffint_ntoh16(h) & 0xffe0) == 0xffe0
		&& h->ver != 1
		&& h->layer != 0
		&& h->bitrate != 0 && h->bitrate != 15
		&& h->sample_rate != 3;
}

uint ffmpg_hdr_framelen(const ffmpg_hdr *h)
{
	return ffmpg_hdr_frame_samples(h)/8 * ffmpg_hdr_bitrate(h) / ffmpg_hdr_sample_rate(h)
		+ ((h->layer != FFMPG_L1) ? h->padding : h->padding * 4);
}

ffmpg_hdr* ffmpg_framefind(const char *data, size_t len, const ffmpg_hdr *h)
{
	const char *d = data, *end = d + len;

	while (d != end) {
		if ((byte)d[0] != 0xff && NULL == (d = ffs_findc(d, end - d, 0xff)))
			break;

		if ((end - d) >= (ssize_t)sizeof(ffmpg_hdr)
			&& ffmpg_hdr_valid((void*)d)
			&& (h == NULL || (ffint_ntoh32(d) & MPG_HDR_CONST_MASK) == (ffint_ntoh32(h) & MPG_HDR_CONST_MASK))) {
			return (void*)d;
		}

		d++;
	}

	return 0;
}


static const byte mpg_xingoffs[2][2] = {
	{17, 32}, //MPEG-1: MONO, 2-CH
	{9, 17}, //MPEG-2
};

enum MPG_XING_FLAGS {
	MPG_XING_FRAMES = 1,
	MPG_XING_BYTES = 2,
	MPG_XING_TOC = 4,
	MPG_XING_VBRSCALE = 8,
};

//8..120 bytes
struct mpg_xing {
	char id[4]; //"Xing"(VBR) or "Info"(CBR)
	byte flags[4]; //enum MPG_XING_FLAGS
	// byte frames[4];
	// byte bytes[4];
	// byte toc[100];
	// byte vbr_scale[4]; //100(worst)..0(best)
};

static FFINL ffbool mpg_xing_valid(const char *data)
{
	return !ffs_cmp(data, "Xing", 4) || !ffs_cmp(data, "Info", 4);
}

static FFINL uint mpg_xing_size(uint flags)
{
	uint all = 4 + sizeof(int);
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

int ffmpg_xing_parse(struct ffmpg_info *xing, const char *data, size_t len)
{
	const char *dstart = data;
	const ffmpg_hdr *h = (void*)data;
	uint xingoff = sizeof(ffmpg_hdr) + mpg_xingoffs[h->ver != FFMPG_1][ffmpg_hdr_channels(h) - 1];
	data += xingoff,  len -= xingoff;

	if (len < 8 || !mpg_xing_valid(data))
		return -1;

	xing->vbr = !ffs_cmp(data, "Xing", 4);
	data += 4;

	uint flags = ffint_ntoh32(data);
	data += sizeof(int);
	if (len < mpg_xing_size(flags))
		return -1;

	if (flags & MPG_XING_FRAMES) {
		xing->frames = ffint_ntoh32(data);
		data += sizeof(int);
	}

	if (flags & MPG_XING_BYTES) {
		xing->bytes = ffint_ntoh32(data);
		data += sizeof(int);
	}

	if (flags & MPG_XING_TOC) {
		ffmemcpy(xing->toc, data, 100);
		data += 100;
	}

	if (flags & MPG_XING_VBRSCALE) {
		xing->vbr_scale = ffint_ntoh32(data);
		data += sizeof(int);
	}

	return data - dstart;
}


//36 bytes
struct mpg_lamehdr {
	char id[9];
	byte unsupported1[12];

	byte delay_hi;
	byte padding_hi :4
		, delay_lo :4;
	byte padding_lo;

	byte unsupported2[12];
};

int ffmpg_lame_parse(struct ffmpg_lame *lame, const char *data, size_t len)
{
	const struct mpg_lamehdr *h = (void*)data;

	if (len < sizeof(struct ffmpg_lame))
		return -1;

	ffmemcpy(lame->id, h->id, sizeof(lame->id));
	lame->enc_delay = ((uint)h->delay_hi << 4) | (h->delay_lo);
	lame->enc_padding = (((uint)h->padding_hi) << 8) | h->padding_lo;
	return sizeof(struct mpg_lamehdr);
}


struct mpg_vbri {
	byte id[4]; //"VBRI"
	byte ver[2];
	byte delay[2];
	byte quality[2];
	byte bytes[4];
	byte frames[4];

	//seekpt[N] = { toc_ent_frames * N => (toc[0] + ... + toc[N-1]) * toc_scale }
	byte toc_ents[2];
	byte toc_scale[2];
	byte toc_ent_size[2];
	byte toc_ent_frames[2];
	byte toc[0];
};

int ffmpg_vbri(struct ffmpg_info *info, const char *data, size_t len)
{
	enum { VBRI_OFF = 32 };
	if (sizeof(ffmpg_hdr) + VBRI_OFF + sizeof(struct mpg_vbri) > len)
		return -1;

	const struct mpg_vbri *vbri = (void*)(data + sizeof(ffmpg_hdr) + VBRI_OFF);
	if (!!ffs_cmp(vbri->id, "VBRI", 4)
		|| 1 != ffint_ntoh16(vbri->ver))
		return -1;

	info->frames = ffint_ntoh32(vbri->frames);
	info->bytes = ffint_ntoh32(vbri->bytes);
	info->vbr = 1;
	uint sz = sizeof(ffmpg_hdr) + VBRI_OFF + sizeof(struct mpg_vbri) + ffint_ntoh16(vbri->toc_ents) * ffint_ntoh16(vbri->toc_ent_size);
	if (sz > len)
		return -1;
	return sz;
}
