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

/** Convert color represented as a string to integer.
@s: "#rrggbb" or a predefined color name (e.g. "black").
Return -1 if unknown color name. */
FF_EXTN uint ffpic_color(const char *s, size_t len);

/** Convert pixels. */
FF_EXTN int ffpic_convert(uint in_fmt, const void *in, uint out_fmt, void *out, uint pixels);
