/** String operations.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/string.h>
#include <FF/bitops.h>

#if defined FF_UNIX
#include <stdarg.h> // for va_arg
#endif


/** a-zA-Z0-9_ */
FF_EXTN const uint ffcharmask_name[8];

/** non-whitespace ANSI */
FF_EXTN const uint ffcharmask_nowhite[8];

#define ffchar_lower(ch)  ((ch) | 0x20)
#define ffchar_upper(ch)  ((ch) & ~0x20)
#define ffchar_tonum(ch)  ((ch) - '0')

static FFINL ffbool ffchar_isdigit(int ch) {
	return (ch >= '0' && ch <= '9');
}

static FFINL ffbool ffchar_isup(int ch) {
	return (ch >= 'A' && ch <= 'Z');
}

static FFINL ffbool ffchar_islow(int ch) {
	return (ch >= 'a' && ch <= 'z');
}

static FFINL ffbool ffchar_ishex(int ch) {
	uint b = ffchar_lower(ch);
	return ffchar_isdigit(ch) || (b >= 'a' && b <= 'f');
}

#define ffchar_isletter(ch)  ffchar_islow(ffchar_lower(ch))

#define ffchar_isname(ch)  (0 != ffbit_testarr(ffcharmask_name, (byte)(ch)))

#define ffchar_isansiwhite(ch)  (0 == ffbit_testarr(ffcharmask_nowhite, (byte)(ch)))

static FFINL ffbool ffchar_iswhitespace(int ch) {
	return (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
}

/** Convert character to hex 4-bit number.
Return -1 if invalid hex char. */
static FFINL int ffchar_tohex(int ch) {
	uint b;
	if (ffchar_isdigit(ch))
		return ch - '0';
	b = ffchar_lower(ch) - 'a' + 10;
	if ((uint)b <= 0x0f)
		return b;
	return -1;
}

/** Get bits to shift by size suffix K, M, G, T. */
FF_EXTN uint ffchar_sizesfx(int suffix);


/** Compare two ANSI strings.  Case-insensitive.
Return -1 if s1 < s2. */
FF_EXTN int ffs_icmp(const char *s1, const char *s2, size_t len);

/** Search for a byte in buffer. */
FF_EXTN void * ffmemchr(const void *d, int b, size_t len);

#define ffmemzero(d, len)  memset(d, 0, len)

/** Compare two buffers. */
#define ffmemcmp  memcmp

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

/** Find substring.
Return END if not found. */
FF_EXTN char * ffs_finds(const char *buf, size_t len, const char *search, size_t search_len);

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

/** Lowercase copy. */
FF_EXTN char * ffs_lower(char *dst, const char *bufend, const char *src, size_t len);

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

/** Lowercase/uppercase hex alphabet. */
FF_EXTN const char ffhex[16];
FF_EXTN const char ffHEX[16];

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
'flags': enum FFS_INT
Return the number of chars processed.
Return 0 on error. */
FF_EXTN uint ffs_toint(const char *src, size_t len, void *dst, int flags);

enum { FFINT_MAXCHARS = FFSLEN("18446744073709551615") };

enum FFINT_TOSTR {
	FFINT_SIGNED = 1
	, FFINT_HEXLOW = 2
	, FFINT_HEXUP = 4
	, FFINT_ZEROWIDTH = 8

	, _FFINT_WIDTH_MASK = 0xff000000
};

#define FFINT_WIDTH(width) ((width) << 24)

/** Convert integer to string.
'flags': enum FFINT_TOSTR
Return the number of chars written. */
FF_EXTN uint ffs_fromint(uint64 i, char *dst, size_t cap, int flags);

/** Convert floating point number to string.
Return the number of chars processed.
Return 0 on error. */
FF_EXTN uint ffs_tofloat(const char *s, size_t len, double *dst, int flags);

/** String format.
%[0][width][x|X]d|u  int|uint
%[0][width][x|X]D|U  int64|uint64
%[0][width][x|X]I|L  ssize_t|size_t

%[*]s  [size_t,] char*
%S     ffstr*
%[*]q  [size_t,] ffsyschar*
%Q     ffqstr*

%e     int
%E     int

%[*]c  [size_t,] int
%p     void*
%%
Return the number of chars written.
Return 0 on error. */
FF_EXTN size_t ffs_fmtv(char *buf, const char *end, const char *fmt, va_list va);

static FFINL size_t ffs_fmt(char *buf, const char *end, const char *fmt, ...) {
	size_t r;
	va_list args;
	va_start(args, fmt);
	r = ffs_fmtv(buf, end, fmt, args);
	va_end(args);
	return r;
}
