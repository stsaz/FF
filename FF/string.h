/** String operations.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/string.h>
#include <FF/bitops.h>


/** a-zA-Z0-9_ */
FF_EXTN const uint ffcharmask_name[];

/** non-whitespace ANSI */
FF_EXTN const uint ffcharmask_nowhite[];

#define ffchar_lower(c)  ((c) | 0x20)
#define ffchar_upper(c)  ((c) & ~0x20)
#define ffchar_tonum(c)  ((c) - '0')

static FFINL ffbool ffchar_isnum(char c) {
	return (c >= '0' && c <= '9');
}

static FFINL ffbool ffchar_isup(char c) {
	return (c >= 'A' && c <= 'Z');
}

static FFINL ffbool ffchar_islow(char c) {
	return (c >= 'a' && c <= 'z');
}

static FFINL ffbool ffchar_ishex(char c) {
	byte b = ffchar_lower(c);
	return ffchar_isnum(c) || (b >= 'a' && b <= 'f');
}

#define ffchar_isletter(c)  ffchar_islow(ffchar_lower(c))

#define ffchar_isname(ch)  (0 != ffbit_testarr(ffcharmask_name, (byte)(ch)))

#define ffchar_iswhite(ch)  (0 == ffbit_testarr(ffcharmask_nowhite, (byte)(ch)))

/** Convert character to hex 4-bit number. */
FF_EXTN ffbool ffchar_tohex(char ch, byte *dst);


/** Compare two strings. */
#define ffs_cmp  strncmp

/** Compare two ANSI strings.  Case-insensitive. */
#define ffs_icmp  strncasecmp


/** Search byte in a buffer.
Return END if not found. */
static FFINL char * ffs_find(const char *buf, size_t len, int ch) {
	char *pos = (char*)memchr(buf, ch, len);
	return (pos != NULL ? pos : (char*)buf + len);
}

/** Perform reverse search of byte in a buffer. */
#if !defined FF_MSVC
static FFINL char * ffs_rfind(const char *buf, size_t len, int ch) {
	char *pos = (char*)memrchr(buf, ch, len);
	return (pos != NULL ? pos : (char*)buf + len);
}

#else
FF_EXTN char * ffs_rfind(const char *buf, size_t len, int ch);
#endif

FF_EXTN char * ffs_findof(const char *buf, size_t len, const char *anyof, size_t cnt);

FF_EXTN char * ffs_rfindof(const char *buf, size_t len, const char *anyof, size_t cnt);

/** Skip characters at the beginning of the string. */
FF_EXTN char * ffs_skip(const char *buf, size_t len, int ch);

/** Skip characters at the end of the string. */
FF_EXTN char * ffs_rskip(const char *buf, size_t len, int ch);

FF_EXTN const byte ff_intmasks[9][8];

/** Search a string in array using operations with type int64. */
FF_EXTN size_t ffs_findarr(const void *s, size_t len, const void *ar, ssize_t elsz, size_t count);

/** Copy 1 character.
Return the tail. */
static FFINL char * ffs_copyc(char *dst, const char *bufend, int ch) {
	if (dst != bufend)
		*dst++ = (char)ch;
	return dst;
}

/** Copy zero-terminated string. */
static FFINL char * ffs_copyz(char *dst, const char *bufend, const char *sz) {
	while (dst != bufend && *sz != '\0') {
		*dst++ = *sz++;
	}
	return dst;
}

/** Copy buffer. */
static FFINL char * ffs_copy(char *dst, const char *bufend, const char *s, size_t len) {
	len = ffmin(bufend - dst, len);
	memcpy(dst, s, len);
	return dst + len;
}

#if defined FF_UNIX
#define ffq_copyc ffs_copyc
#define ffq_copyz  ffs_copyz
#define ffq_copy  ffs_copy
#define ffq_copys  ffs_copy
#define ffs_copyq  ffs_copy

#define ffq_rfind  ffs_rfind
#define ffq_rfindof  ffs_rfindof

#elif defined FF_WIN
static FFINL ffsyschar * ffq_copyc(ffsyschar *dst, const ffsyschar *bufend, int ch) {
	if (dst != bufend)
		*dst++ = (ffsyschar)ch;
	return dst;
}

static FFINL ffsyschar * ffq_copyz(ffsyschar *dst, const ffsyschar *bufend, const ffsyschar *sz) {
	while (dst != bufend && *sz != 0) {
		*dst++ = *sz++;
	}
	return dst;
}

static FFINL ffsyschar * ffq_copy(ffsyschar *dst, const ffsyschar *bufend, const ffsyschar *s, size_t len) {
	return (ffsyschar*)ffs_copy((char*)dst, (char*)bufend, (char*)s, len * sizeof(ffsyschar));
}

/** Copy UTF-8 text into UCS-2 buffer. */
static FFINL ffsyschar * ffq_copys(ffsyschar *dst, const ffsyschar *bufend, const char *src, size_t len) {
	size_t dst_cap = bufend - dst;
	size_t i = ff_utow(dst, dst_cap, src, len, 0);
	if (i == 0)
		return dst + dst_cap;
	return dst + i;
}

/** Copy UCS-2 text into UTF-8 buffer. */
static FFINL char * ffs_copyq(char *dst, const char *bufend, const ffsyschar *src, size_t len) {
	size_t dst_cap = bufend - dst;
	size_t i = ff_wtou(dst, dst_cap, src, len, 0);
	if (i == 0)
		return dst + dst_cap;
	return dst + i;
}

FF_EXTN ffsyschar * ffq_rfind(const ffsyschar *buf, size_t len, int val);

FF_EXTN ffsyschar * ffq_rfindof(const ffsyschar *begin, size_t len, const ffsyschar *matchAr, size_t matchSz);

#endif

/** Find and replace a character.
On return '*n' contains the number of replacements made.
Return the number of chars written. */
FF_EXTN size_t ffs_replacechar(const char *src, size_t len, char *dst, size_t cap, int search, int replace, size_t *n);

/** Lowercase hex alphabet. */
FF_EXTN const char ffhex[];
FF_EXTN const char ffHEX[];

enum FFS_INT {
	FFS_INT8 = 1
	, FFS_INT16 = 2
	, FFS_INT32 = 4
	, FFS_INT64 = 8

	, FFS_INTSIGN = 0x10
	, FFS_INTHEX = 0x20
};

/** Convert string to integer.
'dst': int64|int|short|char
Return the number of chars processed.
Return 0 on error. */
FF_EXTN uint ffs_toint(const char *src, size_t len, void *dst, int flags);

enum { FFINT_MAXCHARS = 32 };

enum FFINT_TOSTR {
	FFINT_SIGNED = 1
	, FFINT_HEXLOW = 2
	, FFINT_HEXUP = 4
	, FFINT_ZEROWIDTH = 8
	, FFINT_SPACEWIDTH = 0

	, _FFINT_WIDTH_MASK = 0xff000000
};

#define FFINT_WIDTH(width) ((width) << 24)

/** Convert integer to string.
Return the number of chars written. */
FF_EXTN uint ffs_fromint(uint64 i, char *dst, size_t cap, int flags);
