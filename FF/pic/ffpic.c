/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/pic/pic.h>


const char* ffpic_fmtstr(uint fmt)
{
	switch (fmt) {
	case FFPIC_BGR:
		return "BGR";
	case FFPIC_BGRA:
		return "BGRA";
	case FFPIC_RGB:
		return "RGB";
	case FFPIC_RGBA:
		return "RGBA";
	}
	return "";
}

#define CASE(a, b)  (((a) << 8) | (b))

int ffpic_convert(uint in_fmt, const void *src, uint out_fmt, void *dst, uint pixels)
{
	uint i;
	const byte *in;
	byte *o;
	uint alpha;

	switch (CASE(in_fmt, out_fmt)) {
	case CASE(FFPIC_RGB, FFPIC_BGR):
	case CASE(FFPIC_BGR, FFPIC_RGB):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 3;
			o = dst + i * 3;
			o[0] = in[2];
			o[1] = in[1];
			o[2] = in[0];
		}
		break;

	case CASE(FFPIC_RGBA, FFPIC_BGRA):
	case CASE(FFPIC_BGRA, FFPIC_RGBA):
		for (i = 0;  i != pixels;  i++) {
			in = in + i * 4;
			o = dst + i * 4;
			o[0] = in[2];
			o[1] = in[1];
			o[2] = in[0];
			o[3] = in[3];
		}
		break;

	case CASE(FFPIC_RGBA, FFPIC_RGB):
	case CASE(FFPIC_BGRA, FFPIC_BGR):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 3;
			alpha = o[3];
			// apply alpha channel (black background)
			o[0] = in[0] * alpha / 255;
			o[1] = in[1] * alpha / 255;
			o[2] = in[2] * alpha / 255;
		}
		break;

	default:
		return -1;
	}

	return 0;
}

#undef CASE
