/** Picture processing.
Copyright (c) 2016 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>


enum FFPIC_FMT {
	FFPIC_RGB = 24,
	FFPIC_RGBA = 32,
	_FFPIC_BGR = 0x100,
	FFPIC_BGR = 24 | _FFPIC_BGR,
	FFPIC_BGRA = 32 | _FFPIC_BGR,
};

FF_EXTN const char* ffpic_fmtstr(uint fmt);

#define ffpic_bits(fmt)  ((fmt) & 0xff)

typedef struct ffpic_info {
	uint width;
	uint height;
	uint format; //enum FFPIC_FMT
} ffpic_info;

FF_EXTN const uint ffpic_clr[];
FF_EXTN const uint ffpic_clr_a[];

/** Convert color represented as a string to integer.
@s: "#rrggbb" or a predefined color name (e.g. "black").
Return -1 if unknown color name. */
FF_EXTN uint ffpic_color3(const char *s, size_t len, const uint *clrs);

#define ffpic_color(s, len)  ffpic_color3(s, len, ffpic_clr)

/** Convert pixels. */
FF_EXTN int ffpic_convert(uint in_fmt, const void *in, uint out_fmt, void *out, uint pixels);
