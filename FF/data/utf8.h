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
#include <ffbase/unicode.h>


// obsolete:
#define FFU_UTF8  FFUNICODE_UTF8
#define FFU_UTF16LE  FFUNICODE_UTF16LE
#define FFU_UTF16BE  FFUNICODE_UTF16BE
#define FFU_WIN1251  FFUNICODE_WIN1251
#define FFU_WIN1252  FFUNICODE_WIN1252
#define ffutf8_decode1  ffutf8_decode
#define ffutf8_encode1  ffutf8_encode
#define ffutf16_findc  ffutf16_findchar

enum {
	FFUTF8_MAXCHARLEN = 6
};

/** Return the number of characters in UTF-8 string. */
FF_EXTN size_t ffutf8_len(const char *p, size_t len);

/** Decode a UTF-8 number (extended to hold 36-bit value):
...
11111110 10xxxxxx*6
*/
FF_EXTN int ffutf8_decode1_64(const char *utf8, size_t len, uint64 *val);

/**
Return enum FFU_CODING;  -1 on error. */
FF_EXTN int ffu_coding(const char *data, size_t len);

enum FFU_FLAGS {
	FFU_FWHOLE = 1 << 31 //incomplete sequence will be replaced with a special character
};

/** Convert data to UTF-8.
@dst: if NULL, return the number of bytes needed in @dst.
@flags: enum FFU_CODING. */
FF_EXTN size_t ffutf8_encodedata(char *dst, size_t cap, const char *src, size_t *len, uint flags);

static FFINL size_t ffutf8_encodewhole(char *dst, size_t cap, const char *src, size_t len, uint flags)
{
	size_t ln = len;
	size_t r = ffutf8_encodedata(dst, cap, src, &ln, flags | FFU_FWHOLE);
	if (ln != len)
		return 0; //not enough output space
	return r;
}

FF_EXTN size_t ffutf8_strencode(ffstr3 *dst, const char *src, size_t len, uint flags);
