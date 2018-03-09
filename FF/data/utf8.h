/** Unicode.
Copyright (c) 2015 Simon Zolin
*/

/*
UTF-8:
U+0000..U+007F    0xxxxxxx
U+0080..U+07FF    110xxxxx 10xxxxxx
U+0800..U+FFFF    1110xxxx 10xxxxxx*2
0010000..001FFFFF 11110xxx 10xxxxxx*3
0200000..03FFFFFF 111110xx 10xxxxxx*4
4000000..7FFFFFFF 1111110x 10xxxxxx*5

UTF-16:
U+0000..U+D7FF    XX XX
U+E000..U+FFFF    XX XX
U+10000..U+10FFFF (XX XX) (XX XX)
*/

#pragma once

#include <FF/array.h>


enum {
	FFUTF8_MAXCHARLEN = 6
};

/** Return the number of characters in UTF-8 string. */
FF_EXTN size_t ffutf8_len(const char *p, size_t len);

/** Return the number of bytes needed to encode a character in UTF-8. */
FF_EXTN uint ffutf8_size(uint uch);

/** Return 1 if it's a valid UTF-8 data. */
FF_EXTN ffbool ffutf8_valid(const char *data, size_t len);

/** Decode a UTF-8 number.
Return the number of bytes parsed;  negative value if more data is needed;  0 on error. */
FF_EXTN int ffutf8_decode1(const char *utf8, size_t len, uint *val);

/** Convert one unicode character to UTF-8.
Return bytes written. */
FF_EXTN uint ffutf8_encode1(char *dst, size_t cap, uint uch);

enum FFU_CODING {
	FFU_UTF8
	, FFU_UTF16LE
	, FFU_UTF16BE

	, FFU_WIN1251 //Cyrillic
	, FFU_WIN1252 //Western
};

/**
Return enum FFU_CODING;  -1 on error. */
FF_EXTN int ffu_coding(const char *data, size_t len);

enum FFU_FLAGS {
	FFU_FWHOLE = 1 << 31 //incomplete sequence will be replaced with a special character
};

/** Detect BOM.
Return enum FFU_CODING or -1 on error. */
FF_EXTN int ffutf_bom(const void *src, size_t *len);

/** Convert data to UTF-8.
@dst: if NULL, return the number of bytes needed in @dst.
@flags: enum FFU_CODING. */
FF_EXTN size_t ffutf8_encode(char *dst, size_t cap, const char *src, size_t *len, uint flags);
FF_EXTN size_t ffutf8_from_utf16le(char *dst, size_t cap, const char *src, size_t *len, uint flags);
FF_EXTN size_t ffutf8_from_utf16be(char *dst, size_t cap, const char *src, size_t *len, uint flags);
FF_EXTN size_t ffutf8_from_ascii(char *dst, size_t cap, const char *src, size_t *len, uint flags);

static FFINL size_t ffutf8_encodewhole(char *dst, size_t cap, const char *src, size_t len, uint flags)
{
	size_t ln = len;
	size_t r = ffutf8_encode(dst, cap, src, &ln, flags | FFU_FWHOLE);
	if (ln != len)
		return 0; //not enough output space
	return r;
}

FF_EXTN size_t ffutf8_strencode(ffstr3 *dst, const char *src, size_t len, uint flags);


/** Find a character.
@len: length of buffer in bytes
Return -1 if NULL-character isn't found. */
FF_EXTN ssize_t ffutf16_findc(const char *s, size_t len, int ch);

/** Return TRUE if UTF-16 code unit is in Basic Multilingual Plane.
(0..0xd7ff) and (0xe000..0xffff). */
static FFINL ffbool ffutf16_basic(uint ch)
{
	return (ch & 0xf800) != 0xd800;
}

/** Return TRUE if UTF-16 code unit is a high surrogate value for a character in Supplementary Plane.
(0xd800..0xdbff) */
static FFINL ffbool ffutf16_highsurr(uint ch)
{
	return (ch & 0xfc00) == 0xd800;
}

/** Get character in Supplementary Plane from UTF-16 high and low surrogates. */
static FFINL uint ffutf16_suppl(uint hi, uint lo)
{
	return 0x10000 + ((hi - 0xd800) << 10) + (lo - 0xdc00);
}
