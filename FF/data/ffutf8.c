/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/data/utf8.h>
#include <FF/string.h>
#include <FF/number.h>


static const char *const codestr[] = {
	"win866", // FFUNICODE_WIN866
	"win1251", // FFUNICODE_WIN1251
	"win1252", // FFUNICODE_WIN1252
};

int ffu_coding(const char *data, size_t len)
{
	int r = ffszarr_ifindsorted(codestr, FFCNT(codestr), data, len);
	if (r < 0)
		return -1;
	return _FFUNICODE_CP_BEGIN + r;
}


static const byte utf8_b1masks[] = { 0x1f, 0x0f, 0x07, 0x03, 0x01, 0 };

int ffutf8_decode1_64(const char *utf8, size_t len, uint64 *val)
{
	uint i, n, d = (byte)utf8[0];
	uint64 r;

	if ((d & 0x80) == 0) {
		*val = d;
		return 1;
	}

	n = ffbit_find32(~(d << 24) & 0xff000000);
	if (n < 3)
		return 0; //invalid first byte
	n--;
	r = d & utf8_b1masks[n - 2];

	if (len < n)
		return -(int)n; //need more data

	for (i = 1;  i != n;  i++) {
		d = (byte)utf8[i];
		if ((d & 0xc0) != 0x80)
			return 0; //invalid
		r = (r << 6) | (d & ~0xc0);
	}

	*val = r;
	return n;
}

size_t ffutf8_len(const char *p, size_t len)
{
	uint n;
	size_t nchars = 0;
	const char *end = p + len;
	while (p < end) {
		uint d = (byte)*p;
		if ((d & 0x80) == 0)
			p++;
		else {
			n = ffbit_find32(~(d << 24) & 0xfe000000);
			if (n >= 3 && n != 8)
				p += n - 1;
			else
				p++;//invalid char
		}
		nchars++;
	}
	return nchars;
}

size_t ffutf8_encodedata(char *dst, size_t cap, const char *src, size_t *plen, uint flags)
{
	ffssize r;
	size_t len = *plen;

	FF_ASSERT(flags & FFU_FWHOLE);
	if (!(flags & FFU_FWHOLE))
		return 0;
	flags &= ~FFU_FWHOLE;

	switch (flags) {
	case FFU_UTF8:
		if (dst == NULL)
			return len;
		len = ffmin(cap, len);
		ffmemcpy(dst, src, len);
		*plen = len;
		return len;

	case FFU_UTF16LE:
		r = ffutf8_from_utf16(dst, cap, src, *plen, FFUNICODE_UTF16LE);
		break;

	case FFU_UTF16BE:
		r = ffutf8_from_utf16(dst, cap, src, *plen, FFUNICODE_UTF16BE);
		break;

	default:
		r = ffutf8_from_cp(dst, cap, src, *plen, flags);
		break;
	}

	if (r < 0)
		return 0;
	return r;
}

size_t ffutf8_strencode(ffstr3 *dst, const char *src, size_t len, uint flags)
{
	size_t r = ffutf8_encodewhole(NULL, 0, src, len, flags);
	if (NULL == ffarr_realloc(dst, r))
		return 0;
	r = ffutf8_encodewhole(dst->ptr, dst->cap, src, len, flags);
	dst->len = r;
	return r;
}
