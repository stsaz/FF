/** Unicode.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FF/array.h>


#ifdef FF_UNIX
#define ffutf8_init() \
	setlocale(LC_CTYPE, "en_US.UTF-8")
#else
#define ffutf8_init()
#endif


enum {
	FFUTF8_MAXCHARLEN = 4
};

/** Return the number of characters in UTF-8 string. */
FF_EXTN size_t ffutf8_len(const char *p, size_t len);

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

enum FFU_FLAGS {
	FFU_FWHOLE = 1 << 31 //incomplete sequence will be replaced with a special character
};

/** Detect BOM.
Return enum FFU_CODING or -1 on error. */
FF_EXTN int ffutf_bom(const void *src, size_t *len);

/** Convert data to UTF-8.
Note: UTF-16: U+10000 and higher are not supported.
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
