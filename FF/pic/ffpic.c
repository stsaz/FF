/**
Copyright (c) 2016 Simon Zolin
*/

#include <FF/pic/pic.h>
#include <FF/string.h>


const char* ffpic_fmtstr(uint fmt)
{
	switch (fmt) {
	case FFPIC_BGR:
		return "BGR";
	case FFPIC_BGRA:
		return "BGRA";
	case FFPIC_ABGR:
		return "ABGR";
	case FFPIC_RGB:
		return "RGB";
	case FFPIC_RGBA:
		return "RGBA";
	}
	return "";
}


static const char *const _ffpic_clrstr[] = {
	"aqua",
	"black",
	"blue",
	"fuchsia",
	"green",
	"grey",
	"lime",
	"maroon",
	"navy",
	"olive",
	"orange",
	"purple",
	"red",
	"silver",
	"teal",
	"white",
	"yellow",
};

const uint ffpic_clr[] = {
	/*aqua*/	0x00ffff,
	/*black*/	0x000000,
	/*blue*/	0x0000ff,
	/*fuchsia*/	0xff00ff,
	/*green*/	0x008000,
	/*grey*/	0x808080,
	/*lime*/	0x00ff00,
	/*maroon*/	0x800000,
	/*navy*/	0x000080,
	/*olive*/	0x808000,
	/*orange*/	0xffa500,
	/*purple*/	0x800080,
	/*red*/	0xff0000,
	/*silver*/	0xc0c0c0,
	/*teal*/	0x008080,
	/*white*/	0xffffff,
	/*yellow*/	0xffff00,
};

const uint ffpic_clr_a[] = {
	/*aqua*/	0x7fdbff,
	/*black*/	0x111111,
	/*blue*/	0x0074d9,
	/*fuchsia*/	0xf012be,
	/*green*/	0x2ecc40,
	/*grey*/	0xaaaaaa,
	/*lime*/	0x01ff70,
	/*maroon*/	0x85144b,
	/*navy*/	0x001f3f,
	/*olive*/	0x3d9970,
	/*orange*/	0xff851b,
	/*purple*/	0xb10dc9,
	/*red*/	0xff4136,
	/*silver*/	0xdddddd,
	/*teal*/	0x39cccc,
	/*white*/	0xffffff,
	/*yellow*/	0xffdc00,
};

uint ffpic_color3(const char *s, size_t len, const uint *clrs)
{
	ssize_t n;
	uint clr = (uint)-1;

	if (len == FFSLEN("#rrggbb") && s[0] == '#') {
		if (FFSLEN("rrggbb") != ffs_toint(s + 1, len - 1, &clr, FFS_INT32 | FFS_INTHEX))
			goto err;

	} else {
		if (-1 == (n = ffszarr_ifindsorted(_ffpic_clrstr, FFCNT(_ffpic_clrstr), s, len)))
			goto err;
		clr = clrs[n];
	}

	//LE: BGR0 -> RGB0
	//BE: 0RGB -> RGB0
	union {
		uint i;
		byte b[4];
	} u;
	u.b[0] = ((clr & 0xff0000) >> 16);
	u.b[1] = ((clr & 0x00ff00) >> 8);
	u.b[2] = (clr & 0x0000ff);
	u.b[3] = 0;
	clr = u.i;

err:
	return clr;
}


#define CASE(a, b)  (((a) << 8) | (b))

int ffpic_convert(uint in_fmt, const void *_src, uint out_fmt, void *_dst, uint pixels)
{
	uint i;
	const byte *in, *src = _src;
	byte *o, *dst = _dst;
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

	case CASE(FFPIC_ABGR, FFPIC_RGBA):
	case CASE(FFPIC_RGBA, FFPIC_ABGR):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 4;
			*(uint*)o = ffint_bswap32(*(uint*)in);
		}
		break;

	case CASE(FFPIC_RGBA, FFPIC_BGRA):
	case CASE(FFPIC_BGRA, FFPIC_RGBA):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 4;
			o[0] = in[2];
			o[1] = in[1];
			o[2] = in[0];
			o[3] = in[3];
		}
		break;

	case CASE(FFPIC_BGRA, FFPIC_ABGR):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 4;
			o[0] = in[3];
			o[1] = in[0];
			o[2] = in[1];
			o[3] = in[2];
		}
		break;

	case CASE(FFPIC_ABGR, FFPIC_BGR):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 3;
			alpha = in[0];
			// apply alpha channel (black background)
			o[0] = in[1] * alpha / 255;
			o[1] = in[2] * alpha / 255;
			o[2] = in[3] * alpha / 255;
		}
		break;

	case CASE(FFPIC_ABGR, FFPIC_RGB):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 3;
			alpha = in[0];
			// apply alpha channel (black background)
			o[0] = in[3] * alpha / 255;
			o[1] = in[2] * alpha / 255;
			o[2] = in[1] * alpha / 255;
		}
		break;

	case CASE(FFPIC_RGBA, FFPIC_RGB):
	case CASE(FFPIC_BGRA, FFPIC_BGR):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 3;
			alpha = in[3];
			// apply alpha channel (black background)
			o[0] = in[0] * alpha / 255;
			o[1] = in[1] * alpha / 255;
			o[2] = in[2] * alpha / 255;
		}
		break;

	case CASE(FFPIC_RGBA, FFPIC_BGR):
	case CASE(FFPIC_BGRA, FFPIC_RGB):
		for (i = 0;  i != pixels;  i++) {
			in = src + i * 4;
			o = dst + i * 3;
			alpha = in[3];
			// apply alpha channel (black background)
			o[0] = in[2] * alpha / 255;
			o[1] = in[1] * alpha / 255;
			o[2] = in[0] * alpha / 255;
		}
		break;

	default:
		return -1;
	}

	return 0;
}

#undef CASE
