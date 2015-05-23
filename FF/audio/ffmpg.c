/**
Copyright (c) 2013 Simon Zolin
*/

#include <FF/audio/mpeg.h>
#include <FF/string.h>


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
