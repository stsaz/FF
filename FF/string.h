/** String operations.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/string.h>
#include <FFOS/mem.h>
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


#define ffsz_len(sz)  strlen(sz)
#define ffsz_findof(sz, accept)  strpbrk(sz, accept)
#define ffsz_cmp(sz1, sz2)  strcmp(sz1, sz2)

#ifndef FF_MSVC
#define ffsz_icmp(sz1, sz2)  strcasecmp(sz1, sz2)
#else
#define ffsz_icmp(sz1, sz2)  _stricmp(sz1, sz2)
#endif


/** Compare two ANSI strings.  Case-insensitive.
Return -1 if s1 < s2. */
FF_EXTN int ffs_icmp(const char *s1, const char *s2, size_t len);

#define ffs_cmp  memcmp

static FFINL int ffs_cmp4(const char *s1, size_t len1, const char *s2, size_t len2)
{
	int r = ffs_cmp(s1, s2, ffmin(len1, len2));
	if (r == 0)
		return len1 - len2;
	return r;
}

static FFINL int ffs_icmp4(const char *s1, size_t len1, const char *s2, size_t len2)
{
	int r = ffs_icmp(s1, s2, ffmin(len1, len2));
	if (r == 0)
		return len1 - len2;
	return r;
}

/** Return TRUE if a buffer and a constant NULL-terminated string are equal. */
#define ffs_eqcz(s1, len, csz2) \
	((len) == FFSLEN(csz2) && 0 == ffs_cmp(s1, csz2, len))
#define ffs_ieqcz(s1, len, csz2) \
	((len) == FFSLEN(csz2) && 0 == ffs_icmp(s1, csz2, len))

/** Compare buffer and NULL-terminated string.
Return 0 if equal.
Return the mismatch byte position:
 . n+1 if s1 > sz2
 . -n-1 if s1 < sz2. */
ssize_t ffs_cmpz(const char *s1, size_t len, const char *sz2);
ssize_t ffs_icmpz(const char *s1, size_t len, const char *sz2);


/** Return TRUE if 'n' characters are equal in both strings.
Useful to check whether "KEY=VALUE" starts with "KEY". */
#define ffs_match(s, len, starts_with, n) \
	((len) >= (n) && 0 == ffs_cmp(s, starts_with, n))

#define ffs_imatch(s, len, starts_with, n) \
	((len) >= (n) && 0 == ffs_icmp(s, starts_with, n))

static FFINL int ffs_matchz(const char *s, size_t len, const char *starts_with)
{
	uint n = ffsz_len(starts_with);
	return len >= n
		&& 0 == ffs_cmp(s, starts_with, n);
}

static FFINL int ffs_imatchz(const char *s, size_t len, const char *starts_with)
{
	uint n = ffsz_len(starts_with);
	return len >= n
		&& 0 == ffs_icmp(s, starts_with, n);
}

static FFINL ffbool ffsz_match(const char *sz, const char *starts_with, size_t n)
{
	ssize_t r = ffs_cmpz(starts_with, n, sz);
	return r == 0 || (r < 0 && (size_t)(-r) - 1 == n);
}

static FFINL ffbool ffsz_imatch(const char *sz, const char *starts_with, size_t n)
{
	ssize_t r = ffs_icmpz(starts_with, n, sz);
	return r == 0 || (r < 0 && (size_t)(-r) - 1 == n);
}


/** Search for a byte in buffer. */
FF_EXTN void * ffmemchr(const void *d, int b, size_t len);

/** Compare two buffers. */
#define ffmemcmp  memcmp

/** Search byte in a buffer.
Return END if not found. */
static FFINL char * ffs_find(const char *buf, size_t len, int ch) {
	char *pos = (char*)memchr(buf, ch, len);
	return (pos != NULL ? pos : (char*)buf + len);
}

/** Search byte in a buffer.
Return NULL if not found. */
#define ffs_findc(buf, len, ch)  memchr(buf, ch, len)

/** Return the number of occurrences of byte. */
FF_EXTN size_t ffs_nfindc(const char *buf, size_t len, int ch);

/** Perform reverse search of byte in a buffer. */
#if !defined FF_MSVC && !defined FF_MINGW
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

FF_EXTN char * ffs_ifinds(const char *buf, size_t len, const char *search, size_t search_len);

FF_EXTN char * ffs_findof(const char *buf, size_t len, const char *anyof, size_t cnt);

FF_EXTN char * ffs_rfindof(const char *buf, size_t len, const char *anyof, size_t cnt);

/** Skip characters at the beginning of the string. */
FF_EXTN char * ffs_skip(const char *buf, size_t len, int ch);

FF_EXTN char * ffs_skipof(const char *buf, size_t len, const char *anyof, size_t cnt);

/** Skip characters at the end of the string. */
FF_EXTN char * ffs_rskip(const char *buf, size_t len, int ch);

FF_EXTN char * ffs_rskipof(const char *buf, size_t len, const char *anyof, size_t cnt);

/** Search a string in array using operations with type int64.
Return 'count' if not found. */
FF_EXTN size_t ffs_findarr(const void *s, size_t len, const void *ar, ssize_t elsz, size_t count);

/** Search a string in array of pointers to NULL-terminated strings.
Return -1 if not found. */
FF_EXTN ssize_t ffs_findarrz(const char *const *ar, size_t n, const char *search, size_t search_len);
FF_EXTN ssize_t ffs_ifindarrz(const char *const *ar, size_t n, const char *search, size_t search_len);
FF_EXTN ssize_t ffszarr_findsorted(const char *const *ar, size_t n, const char *search, size_t search_len);
FF_EXTN ssize_t ffszarr_ifindsorted(const char *const *ar, size_t n, const char *search, size_t search_len);

/** Search a string in char[n][m] array.
Return -1 if not found. */
FF_EXTN ssize_t ffcharr_findsorted(const void *ar, size_t n, size_t m, const char *search, size_t search_len);


#define ffmemcpy  memcpy

/** Copy data and return the tail. */
#define ffmem_copy(dst, src, len)  ((char*)ffmemcpy(dst, src, len) + (len))
#define ffmem_copycz(dst, s)  ((char*)ffmemcpy(dst, s, FFSLEN(s)) + FFSLEN(s))

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
	ffmemcpy(dst, s, len);
	return dst + len;
}

#define ffs_copycz(dst, bufend, csz)  ffs_copy(dst, bufend, csz, FFSLEN(csz))

/**
Return the bytes copied. */
static FFINL size_t ffs_append(void *dst, size_t off, size_t cap, const void *src, size_t len)
{
	size_t n = ffmin(len, cap - off);
	ffmemcpy((char*)dst + off, src, n);
	return n;
}

/** Select the larger buffer */
static FFINL void ffs_max(const void *s1, size_t len1, const void *s2, size_t len2, void **out_ptr, size_t *out_len)
{
	if (len1 >= len2) {
		*out_ptr = (void*)s1;
		*out_len = len1;
	} else {
		*out_ptr = (void*)s2;
		*out_len = len2;
	}
}


/** Search the end of string limited by @maxlen. */
static FFINL size_t ffsz_nlen(const char *s, size_t maxlen)
{
	return ffs_find(s, maxlen, '\0') - s;
}

/** Copy buffer and append zero byte.
Return the pointer to the trailing zero. */
static FFINL char * ffsz_copy(char *dst, size_t cap, const char *src, size_t len) {
	char *end = dst + cap;
	if (cap != 0) {
		dst = ffs_copy(dst, end - 1, src, len);
		*dst = '\0';
	}
	return dst;
}

static FFINL char * ffsz_fcopy(char *dst, const char *src, size_t len) {
	dst = ffmem_copy(dst, src, len);
	*dst = '\0';
	return dst;
}

#define ffsz_copycz(dst, csz)  ffmemcpy(dst, csz, sizeof(csz))

/** Allocate memory and copy string. */
static FFINL char* ffsz_alcopy(const char *src, size_t len)
{
	char *s = ffmem_alloc(len + 1);
	if (s != NULL)
		ffsz_fcopy(s, src, len);
	return s;
}

#define ffsz_alcopyz(src)  ffsz_alcopy(src, ffsz_len(src))

/** Convert case (ANSI).
Return the number of bytes written. */
FF_EXTN size_t ffs_lower(char *dst, const char *end, const char *src, size_t len);
FF_EXTN size_t ffs_upper(char *dst, const char *end, const char *src, size_t len);
FF_EXTN size_t ffs_titlecase(char *dst, const char *end, const char *src, size_t len);

static FFINL char* ffsz_alcopylwr(const char *src, size_t len)
{
	char *s = ffmem_alloc(len + 1);
	if (s == NULL)
		return NULL;
	ffs_lower(s, s + len, src, len);
	s[len] = '\0';
	return s;
}


#if defined FF_UNIX
#define ffq_copyc ffs_copyc
#define ffq_copyz  ffs_copyz
#define ffq_copy  ffs_copy
#define ffq_copys  ffs_copy
#define ffs_copyq  ffs_copy
#define ffq_lens(ptr, len)  (len)
#define ffq_lenq(ptr, len)  (len)

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
	size_t i;

	if (len == 0 || dst == NULL)
		return dst;

	i = ff_utow(dst, dst_cap, src, len, 0);
	if (i == 0)
		return dst + dst_cap;
	return dst + i;
}

/** Copy UCS-2 text into UTF-8 buffer. */
static FFINL char * ffs_copyq(char *dst, const char *bufend, const ffsyschar *src, size_t len) {
	size_t dst_cap = bufend - dst;
	size_t i;

	if (len == 0 || dst == NULL)
		return dst;

	i = ff_wtou(dst, dst_cap, src, len, 0);
	if (i == 0)
		return dst + dst_cap;
	return dst + i;
}

static FFINL char* ffsz_alcopyqz(const ffsyschar *wsz)
{
	size_t len = ffq_len(wsz) + 1, cap = ff_wtou(NULL, 0, wsz, len, 0);
	char *s = ffmem_alloc(cap);
	if (s != NULL)
		ff_wtou(s, cap, wsz, len, 0);
	return s;
}

/** Return the number of characters to allocate for system string. */
#define ffq_lens(p, len)  ffutf8_len(p, len)

/** Return the number of characters to allocate for char-string. */
#define ffq_lenq(wstr, len)  ff_wtou(NULL, 0, wstr, len, 0)

FF_EXTN ffsyschar * ffq_rfind(const ffsyschar *buf, size_t len, int val);

FF_EXTN ffsyschar * ffq_rfindof(const ffsyschar *begin, size_t len, const ffsyschar *matchAr, size_t matchSz);

#endif

/** Return the pointer to the trailing zero. */
static FFINL ffsyschar * ffqz_copys(ffsyschar *dst, size_t cap, const char *src, size_t len) {
	ffsyschar *end = dst + cap;
	if (cap != 0) {
		dst = ffq_copys(dst, end - 1, src, len);
		ffq_copyc(dst, end, '\0');
	}
	return dst;
}

static FFINL ffsyschar * ffqz_copyz(ffsyschar *dst, size_t cap, const ffsyschar *src) {
	ffsyschar *end = dst + cap;
	if (cap != 0) {
		dst = ffq_copyz(dst, end - 1, src);
		ffq_copyc(dst, end, '\0');
	}
	return dst;
}

/** Fill buffer with copies of 1 byte. */
static FFINL size_t ffs_fill(char *s, const char *end, uint ch, size_t len) {
	len = ffmin(len, end - s);
	memset(s, ch, len);
	return len;
}


/** Find and replace a character.
On return '*n' contains the number of replacements made.
Return the number of chars written. */
FF_EXTN size_t ffs_replacechar(const char *src, size_t len, char *dst, size_t cap, int search, int replace, size_t *n);

/** Lowercase/uppercase hex alphabet. */
FF_EXTN const char ffhex[16];
FF_EXTN const char ffHEX[16];

/** Convert a byte into 2-byte hex string.
@hex: alphabet to use. */
static FFINL int ffs_hexbyte(char *dst, uint b, const char *hex) {
	dst[0] = hex[b >> 4];
	dst[1] = hex[b & 0x0f];
	return 2;
}

enum FFS_INT {
	FFS_INT8 = 1
	, FFS_INT16 = 2
	, FFS_INT32 = 4
	, FFS_INT64 = 8

	, FFS_INTSIGN = 0x10
	, FFS_INTHEX = 0x20
	, FFS_INTOCTAL = 0x40
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
	, FFINT_OCTAL = 0x20
	, FFINT_ZEROWIDTH = 8
	, FFINT_SEP1000 = 0x10 // use thousands separator, e.g. "1,000"

	, _FFINT_WIDTH_MASK = 0xff000000
	, _FFS_FLT_FRAC_WIDTH_MASK = 0x00ff0000
};

#define FFINT_WIDTH(width) ((width) << 24)
#define FFS_INT_WFRAC(width) ((width) << 16)

/** Convert integer to string.
'flags': enum FFINT_TOSTR
Return the number of chars written. */
FF_EXTN uint ffs_fromint(uint64 i, char *dst, size_t cap, int flags);

/** Convert floating point number to string.
Return the number of chars processed.
Return 0 on error. */
FF_EXTN uint ffs_tofloat(const char *s, size_t len, double *dst, int flags);

/** Convert float to string.
@flags: enum FFINT_TOSTR
Return bytes written. */
FF_EXTN uint ffs_fromfloat(double d, char *dst, size_t cap, uint flags);

/** Parse the list of numbers, e.g. "1,3,10,20".
Return 0 on success. */
FF_EXTN int ffs_numlist(const char *d, size_t *len, uint *dst);

enum FFS_HEXSTR {
	FFS_HEXLOW = 0,
	FFS_HEXUP = 1,
};

/** Convert bytes to hex string.
@flags: enum FFS_HEXSTR.
Return the number of bytes written. */
FF_EXTN size_t ffs_hexstr(char *dst, size_t cap, const char *src, size_t len, uint flags);

/** Formatted output into string.
Integer:
%[0][width] [x|X|,] d|u  int|uint
%[0][width] [x|X|,] D|U  int64|uint64
%[0][width] [x|X|,] I|L  ssize_t|size_t

Floating-point:
% [0][width][.width] F  double

Binary data:
% width|* x|X b  [size_t,] byte*
 e.g. ("%2xb", "AB")  =>  "4142"
      ("%*xb", (size_t)2, "AB")  =>  "4142"

String:
% [width|*] s    [size_t,] char*
%S     ffstr*
% [width|*] q    [size_t,] ffsyschar*
%Q     ffqstr*

% [width|*] c  [size_t,] int  (Char)

%p  void*  (Pointer)
%Z  '\0'  (NULL byte)
%%  '%'  (Percent sign)

%e  int(enum FFERR)  (System function name)
%E  int  (System error message)

Return bytes written;  0 on error (bad format string);
 <0 if not enough space: 'cap' bytes were written into 'buf' and "-RESULT" is the total number of bytes needed. */
FF_EXTN ssize_t ffs_fmtv2(char *buf, size_t cap, const char *fmt, va_list va);

static FFINL ssize_t ffs_fmt2(char *buf, size_t cap, const char *fmt, ...)
{
	ssize_t r;
	va_list args;
	va_start(args, fmt);
	r = ffs_fmtv2(buf, cap, fmt, args);
	va_end(args);
	return r;
}

/**
Return the number of chars written.
Return 0 on error.
If 'buf' and 'end' are NULL, return the minimum buffer size required. */
static FFINL size_t ffs_fmtv(char *buf, const char *end, const char *fmt, va_list args)
{
	va_list va;
	va_copy(va, args);
	ssize_t r = ffs_fmtv2(buf, end - buf, fmt, va);
	va_end(va);
	return (r >= 0) ? r : 0;
}

static FFINL size_t ffs_fmt(char *buf, const char *end, const char *fmt, ...) {
	ssize_t r;
	va_list args;
	va_start(args, fmt);
	r = ffs_fmtv2(buf, end - buf, fmt, args);
	va_end(args);
	return (r >= 0) ? r : 0;
}

/** Match string by format:
 "% [width] x u|U" - uint|uint64
 "% width s" - char*
Return the number of bytes parsed.  Return negative value on error. */
FF_EXTN size_t ffs_fmatchv(const char *s, size_t len, const char *fmt, va_list va);

static FFINL size_t ffs_fmatch(const char *s, size_t len, const char *fmt, ...) {
	size_t r;
	va_list args;
	va_start(args, fmt);
	r = ffs_fmatchv(s, len, fmt, args);
	va_end(args);
	return r;
}


enum FFS_ESCAPE {
	FFS_ESC_NONPRINT // escape '\\' and non-printable.  '\t', '\r', '\n' are not escaped.
};

/** Replace special characters with \xXX.
@type: enum FFS_ESCAPE.
Return the number of bytes written.
Return <0 if there is no enough space (the number of input bytes processed, negative value). */
FF_EXTN ssize_t ffs_escape(char *dst, size_t cap, const char *s, size_t len, int type);


enum FFS_WILDCARD {
	FFS_WC_ICASE = 1
};

/** Match string by a wildcard pattern ('*', '?').
@flags: enum FFS_WILDCARD.
Return 0 if match. */
FF_EXTN int ffs_wildcard(const char *pattern, size_t patternlen, const char *s, size_t len, uint flags);


/** Match string by a regular expression.
REGEX:     MEANING:
.          any character
c?         optional character
\.         a special character is escaped
[az]       either 'a' or 'z'
[a-z]      any character from 'a' to 'z'
a|bc       either "a" or "bc"
Return 0 if match;  >0 if non-match;  <0 if regexp is invalid. */
FF_EXTN int ffs_regex(const char *regexp, size_t regexp_len, const char *s, size_t len, uint flags);

#define ffs_regexcz(regexpcz, s, len, flags) \
	ffs_regex(regexpcz, FFSLEN(regexpcz), s, len, flags)


#if defined FF_LINUX
#define FF_QSORT_PARAMS  const void *a, const void *b, void *udata
#define ff_qsort(ar, n, elsize, func, udata) \
	qsort_r(ar, n, elsize, func, udata)

#elif defined FF_BSD
#define FF_QSORT_PARAMS  void *udata, const void *a, const void *b
#define ff_qsort(ar, n, elsize, func, udata) \
	qsort_r(ar, n, elsize, udata, func)

#elif defined FF_MSVC
#define FF_QSORT_PARAMS  void *udata, const void *a, const void *b
#define ff_qsort(ar, n, elsize, func, udata) \
	qsort_s(ar, n, elsize, func, udata)

#else //gcc on windows:
#define FF_QSORT_PARAMS  const void *a, const void *b
#define ff_qsort(ar, n, elsize, func, udata) \
	qsort(ar, n, elsize, func)
#endif


/** Shift value in every element. */
static FFINL void ffarrp_shift(void **ar, size_t size, size_t by)
{
	size_t i;
	for (i = 0;  i != size;  i++) {
		ar[i] = (char*)ar[i] + by;
	}
}

/** Set array elements to point to consecutive regions of one buffer. */
static FFINL void ffarrp_setbuf(void **ar, size_t size, const void *buf, size_t region_len)
{
	size_t i;
	for (i = 0;  i != size;  i++) {
		ar[i] = (char*)buf + region_len * i;
	}
}
