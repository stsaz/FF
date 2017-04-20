/**
Copyright (c) 2015 Simon Zolin
*/

#include <FF/data/utf8.h>
#include <FF/string.h>
#include <FF/number.h>


static const char *const codestr[] = {
	"utf8",
	"utf16le",
	"utf16be",

	"win1251",
	"win1252",
};

int ffu_coding(const char *data, size_t len)
{
	return ffszarr_ifindsorted(codestr, FFCNT(codestr), data, len);
}


static const byte utf8_b1masks[] = { 0x1f, 0x0f, 0x07, 0x03, 0x01 };

int ffutf8_decode1(const char *utf8, size_t len, uint *val)
{
	uint i, n, r, d = (byte)utf8[0];

	if ((d & 0x80) == 0) {
		*val = d;
		return 1;
	}

	n = ffbit_find32(~(d << 24) & 0xfe000000);
	if (n < 3 || n == 8)
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

ffbool ffutf8_valid(const char *data, size_t len)
{
	int r;
	uint val;
	const char *end = data + len;
	for (;  data != end;  data += r) {
		r = ffutf8_decode1(data, end - data, &val);
		if (r <= 0)
			return 0;
	}
	return 1;
}

int ffutf_bom(const void *src, size_t *len)
{
	const byte *s = src;
	if (*len < 2)
		return -1;

	switch (s[0]) {
	case 0xff:
		if (s[1] == 0xfe) {
			*len = 2;
			return FFU_UTF16LE;
		}
		break;

	case 0xfe:
		if (s[1] == 0xff) {
			*len = 2;
			return FFU_UTF16BE;
		}
		break;

	case 0xef:
		if (*len >= 3 && s[1] == 0xbb && s[2] == 0xbf) {
			*len = 3;
			return FFU_UTF8;
		}
	}

	return -1;
}

uint ffutf8_size(uint uch)
{
	uint n;

	if (uch < 0x80)
		n = 1;
	else if (uch < 0x0800)
		n = 2;
	else if (uch < 0x10000)
		n = 3;
	else if (uch < 0x200000)
		n = 4;
	else if (uch < 0x04000000)
		n = 5;
	else if (uch < 0x80000000)
		n = 6;
	else
		n = 0;
	return n;
}

enum {
	U_REPL = 0xFFFD //utf-8: EF BF BD
};

uint ffutf8_encode1(char *dst, size_t cap, uint uch)
{
	uint n = ffutf8_size(uch);
	if (cap < n)
		return 0;

	switch (n) {
	case 1:
		*dst++ = uch;
		break;

	case 2:
		*dst++ = 0xc0 | (uch >> 6);
		*dst++ = 0x80 | ((uch >> 0) & ~0xc0);
		break;

	case 3:
		*dst++ = 0xe0 | (uch >> 12);
		*dst++ = 0x80 | ((uch >> 6) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 0) & ~0xc0);
		break;

	case 4:
		*dst++ = 0xf0 | (uch >> 18);
		*dst++ = 0x80 | ((uch >> 12) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 6) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 0) & ~0xc0);
		break;

	case 5:
		*dst++ = 0xf8 | (uch >> 24);
		*dst++ = 0x80 | ((uch >> 18) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 12) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 6) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 0) & ~0xc0);
		break;

	case 6:
		*dst++ = 0xfc | (uch >> 30);
		*dst++ = 0x80 | ((uch >> 24) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 18) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 12) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 6) & ~0xc0);
		*dst++ = 0x80 | ((uch >> 0) & ~0xc0);
		break;
	}

	return n;
}

size_t ffutf8_from_utf16le(char *dst, size_t cap, const char *src, size_t *plen, uint flags)
{
	size_t r, i, idst = 0, len = *plen;
	const ushort *us = (void*)src;

	if (dst == NULL) {
		for (i = 0;  i < len / 2;  i++) {
			idst += ffutf8_size(ffint_ltoh16(us + i));
		}
		i *= 2;

		if ((len % 2) != 0 && (flags & FFU_FWHOLE)) {
			idst += 3; //length of U_REPL in UTF-8
			i = len;
		}

		goto done;
	}

	for (i = 0;  i < len / 2 && idst < cap;  i++) {
		r = ffutf8_encode1(dst + idst, cap - idst, us[i]);
		if (r == 0) {
			i *= 2;
			goto done;
		}
		idst += r;
	}
	i *= 2;

	if ((len % 2) != 0 && (flags & FFU_FWHOLE)) {
		r = ffutf8_encode1(dst + idst, cap - idst, U_REPL);
		if (r != 0) {
			idst += 3;
			i = len;
		}
	}

done:
	*plen = i;
	return idst;
}

size_t ffutf8_from_utf16be(char *dst, size_t cap, const char *src, size_t *plen, uint flags)
{
	size_t r, i, idst = 0, len = *plen;
	const ushort *us = (void*)src;

	if (dst == NULL) {
		for (i = 0;  i < len / 2;  i++) {
			idst += ffutf8_size(ffint_ntoh16(us + i));
		}
		i *= 2;

		if ((len % 2) != 0 && (flags & FFU_FWHOLE)) {
			idst += 3; //length of U_REPL in UTF-8
			i = len;
		}

		goto done;
	}

	for (i = 0;  i < len / 2 && idst < cap;  i++) {
		r = ffutf8_encode1(dst + idst, cap - idst, ffhton16(us[i]));
		if (r == 0) {
			i *= 2;
			goto done;
		}
		idst += r;
	}
	i *= 2;

	if ((len % 2) != 0 && (flags & FFU_FWHOLE)) {
		r = ffutf8_encode1(dst + idst, cap - idst, U_REPL);
		if (r != 0) {
			idst += 3;
			i = len;
		}
	}

done:
	*plen = i;
	return idst;
}


/* Unicode code-points for characters 0x80-0xff. */
static const ushort cods[][128] = {

	//win1251
	{
	  0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F
	, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, U_REPL, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F
	, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407
	, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457
	, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F
	, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F
	, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F
	, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
	}

	, //win1252
	{
	  0x20AC, U_REPL, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, U_REPL, 0x017D, U_REPL
	, U_REPL, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, U_REPL, 0x017E, 0x0178
	, 0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF
	, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF
	, 0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF
	, 0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF
	, 0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF
	, 0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF
	}
};

size_t ffutf8_from_ascii(char *dst, size_t cap, const char *src, size_t *plen, uint flags)
{
	const ushort *cod = cods[(flags & 0xffff) - FFU_WIN1251];
	size_t r, i, idst = 0, len = *plen;

	if (dst == NULL) {
		for (i = 0;  i < len;  i++) {
			if (src[i] & 0x80)
				idst += ffutf8_size(cod[(byte)src[i] & 0x7f]);
			else
				idst++;
		}

		*plen = i;
		return idst;
	}

	for (i = 0;  i < len && idst < cap;  i++) {
		if (src[i] & 0x80) {
			r = ffutf8_encode1(dst + idst, cap - idst, cod[(byte)src[i] & 0x7f]);
			if (r == 0)
				break;
			idst += r;
		} else
			dst[idst++] = src[i];
	}

	*plen = i;
	return idst;
}


size_t ffutf8_encode(char *dst, size_t cap, const char *src, size_t *plen, uint flags)
{
	size_t len = *plen;

	switch (flags & 0xffff) {
	case FFU_UTF8:
		if (dst != NULL)
			return len;
		len = ffmin(cap, len);
		ffmemcpy(dst, src, len);
		*plen = len;
		return len;

	case FFU_UTF16LE:
		return ffutf8_from_utf16le(dst, cap, src, plen, flags);

	case FFU_UTF16BE:
		return ffutf8_from_utf16be(dst, cap, src, plen, flags);

	default:
		return ffutf8_from_ascii(dst, cap, src, plen, flags);
	}
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


ssize_t ffutf16_findc(const char *s, size_t len, int ch)
{
	const short *p = (void*)s;
	len /= 2;
	for (uint i = 0;  i != len;  i++) {
		if (p[i] == ch)
			return i * 2;
	}
	return -1;
}
