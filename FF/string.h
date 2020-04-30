/** String operations.
Copyright (c) 2013 Simon Zolin
*/

/*
GENERAL CONVENTIONS:
ffsz_* - operations with a NULL-terminated string (char*)
ffs_* - operations with a string with length (char*, size_t)
ffq_* - operations with a system-character string with length (ffsyschar*, size_t)
ffstr_* - operations with ffstr (ffstr)
ffs_*i*() (e.g. ffs_icmp()) - case-insensitive operation

ALGORITHMS:
CHARACTER - operations with 1 ANSI character
  ffchar_*()
FFSTR
COMPARE - compare two strings
  ffs*_cmp*()
  ffs*_eq*()
COMPARE (case insensitive)
  ffs*_icmp*()
  ffs*_ieq*()
MATCH - match starting bytes of both strings
  ffs*_match*()
MATCH (case insensitive)
  ffs*_[i]match*()
REVERSE MATCH - match ending bytes of both strings
  ffs*_[i]rmatch*()
FIND - get position of a byte or a substring
  ffs*_find*()
FIND (case insensitive)
  ffs*_[i]find*()
REVERSE FIND
SPLIT - split string by a byte
  ffs*_*split*()
SKIP - skip bytes matching a byte, bytes or byte mask
  ffs*_[r]skip*()
REVERSE SKIP
FIND IN ARRAY - get position of a string in array
COPY DATA
CONVERT CASE - convert character case
SYSTEM STRING - operations with a system-character string
REPLACE - find and replace text
TYPE CONVERT - convert between string, number and boolean
FORMATTED I/O - string write/read by %-format string and parameters
PATTERN MATCH - match text by a pattern
COMPARE VERSIONS - compare version strings
FFBSTR
*/

#pragma once

#include <FFOS/string.h>
#include <FFOS/mem.h>
#include <FF/bitops.h>

#if defined FF_UNIX
#include <stdarg.h> // for va_arg
#endif


// CHARACTER - operations with 1 ANSI character

/** a-zA-Z0-9_ */
FF_EXTN const uint ffcharmask_name[8];

/** non-whitespace ANSI */
FF_EXTN const uint ffcharmask_nowhite[8];

/** All printable */
FF_EXTN const uint ffcharmask_printable[8];

/** All printable plus \t \r \n */
FF_EXTN const uint ffcharmask_printable_tabcrlf[8];

/** All except '\\' \a \b \f \n \r \t \v */
FF_EXTN const uint ffcharmask_nobslash_esc[8];

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

#define ffchar_isprintable(ch)  (0 == ffbit_testarr(ffcharmask_printable, (byte)(ch)))

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

/** Split integer into parts.
0..1023 -> 0;  1024..1024k-1 -> 'k';  ...
Return shift value. */
FF_EXTN uint ffint_tosfx(uint64 size, char *suffix);


// FFSTR

typedef struct ffstr {
	size_t len;
	char *ptr;
} ffstr;

#if defined FF_UNIX
typedef ffstr ffqstr;
#else
typedef struct ffqstr {
	size_t len;
	ffsyschar *ptr;
} ffqstr;
#endif

/** Set a buffer. */
#define ffstr_set(s, data, n) \
do { \
	(s)->ptr = (char*)data; \
	(s)->len = n; \
} while(0)

#define ffstr_set2(s, src)  ffstr_set(s, (src)->ptr, (src)->len)

/** Set constant NULL-terminated string. */
#define ffstr_setcz(s, csz)  ffstr_set(s, (char*)csz, FFSLEN(csz))

/** Set NULL-terminated string. */
#define ffstr_setz(s, sz)  ffstr_set(s, (char*)sz, ffsz_len(sz))

#define ffstr_setnz(s, sz, maxlen)  ffstr_set(s, (char*)sz, ffsz_nlen(sz, maxlen))

/** Set ffstr from ffiovec. */
#define ffstr_setiovec(s, iov)  ffstr_set(s, (iov)->iov_base, (iov)->iov_len)

#define ffstr_null(s)  ffstr_set(s, NULL, 0)

#define ffstr_end(s)  ((s)->ptr + (s)->len)

#define FFS_SHIFT(ptr, len, by) \
do { \
	ssize_t _by = (by); \
	(ptr) += (_by); \
	(len) -= (_by); \
} while (0)

#define ffstr_shift(s, by)  FFS_SHIFT((s)->ptr, (s)->len, by)

static inline int ffstr_popfront(ffstr *s)
{
	FF_ASSERT(s->len != 0);
	s->len--;
	return *(s->ptr++);
}


enum {
	FF_TEXT_LINE_MAX = 64 * 1024, //max line size
};

#define ffsz_len(sz)  strlen(sz)

#define ffsz_safelen(sz)  ((sz != NULL) ? strlen(sz) : 0)

/** Search the end of string limited by @maxlen. */
static size_t ffsz_nlen(const char *s, size_t maxlen);


// COMPARE - compare two strings

/** Compare two buffers. */
#define ffmemcmp(s1, s2, n)  memcmp(s1, s2, n)

#define ffs_cmp(s1, s2, n)  memcmp(s1, s2, n)

static FFINL int ffs_cmp4(const char *s1, size_t len1, const char *s2, size_t len2)
{
	int r = ffs_cmp(s1, s2, ffmin(len1, len2));
	if (r == 0)
		return len1 - len2;
	return r;
}

/** Compare buffer and NULL-terminated string.
Return 0 if equal.
Return the mismatch byte position:
 . n+1 if s1 > sz2
 . -n-1 if s1 < sz2. */
FF_EXTN ssize_t ffs_cmpz(const char *s1, size_t len, const char *sz2);
FF_EXTN ssize_t ffs_cmpn(const char *s1, const char *s2, size_t len);

#define ffsz_cmp(sz1, sz2)  strcmp(sz1, sz2)

#define ffstr_cmp2(str1, str2) \
	ffs_cmp4((str1)->ptr, (str1)->len, (str2)->ptr, (str2)->len)

/** Return TRUE if a buffer and a constant NULL-terminated string are equal. */
#define ffs_eqcz(s1, len, csz2) \
	((len) == FFSLEN(csz2) && 0 == ffs_cmp(s1, csz2, len))

#define ffsz_eq(sz1, sz2)  (!ffsz_cmp(sz1, sz2))

#define ffstr_eq(str1, s2, n) \
	((str1)->len == (n) && 0 == ffmemcmp((str1)->ptr, s2, n))

/** Return TRUE if an array is equal to a NULL-terminated string. */
#define ffstr_eqz(str1, sz2)  (0 == ffs_cmpz((str1)->ptr, (str1)->len, sz2))

/** Compare ffstr object and constant NULL-terminated string. */
#define ffstr_eqcz(str1, csz2)  ffstr_eq(str1, csz2, FFSLEN(csz2))

/** Return TRUE if both strings are equal. */
#define ffstr_eq2(str1, str2) \
	ffstr_eq(str1, (str2)->ptr, (str2)->len)


// COMPARE (case insensitive)

#ifndef FF_MSVC
#define ffsz_icmp(sz1, sz2)  strcasecmp(sz1, sz2)
#else
#define ffsz_icmp(sz1, sz2)  _stricmp(sz1, sz2)
#endif

/** Compare two ANSI strings.  Case-insensitive.
Return -1 if s1 < s2. */
FF_EXTN int ffs_icmp(const char *s1, const char *s2, size_t len);

static FFINL int ffs_icmp4(const char *s1, size_t len1, const char *s2, size_t len2)
{
	int r = ffs_icmp(s1, s2, ffmin(len1, len2));
	if (r == 0)
		return len1 - len2;
	return r;
}

/** Compare buffer and NULL-terminated string.
Return 0 if equal.
Return the mismatch byte position:
 . n+1 if s1 > sz2
 . -n-1 if s1 < sz2. */
FF_EXTN ssize_t ffs_icmpz(const char *s1, size_t len, const char *sz2);

#define ffs_ieqcz(s1, len, csz2) \
	((len) == FFSLEN(csz2) && 0 == ffs_icmp(s1, csz2, len))

/** Compare ANSI strings.  Case-insensitive. */
#define ffstr_icmp(str1, s2, len2) \
	ffs_icmp4((str1)->ptr, (str1)->len, s2, len2)
#define ffstr_icmp2(str1, str2) \
	ffs_icmp4((str1)->ptr, (str1)->len, (str2)->ptr, (str2)->len)

static inline ffbool ffstr_ieq(const ffstr *str, const char *s, size_t n)
{
	return str->len == n && 0 == ffs_icmp(str->ptr, s, n);
}

/** Return TRUE if both strings are equal. Case-insensitive */
#define ffstr_ieq2(str1, str2)  ffstr_ieq(str1, (str2)->ptr, (str2)->len)

#define ffstr_ieqz(str1, sz2)  (0 == ffs_icmpz((str1)->ptr, (str1)->len, sz2))

/** Compare ffstr object and constant NULL-terminated string.  Case-insensitive. */
#define ffstr_ieqcz(str1, csz2)  ffstr_ieq(str1, csz2, FFSLEN(csz2))


// MATCH - match starting bytes of both strings

/** Return TRUE if 'n' characters are equal in both strings.
Useful to check whether "KEY=VALUE" starts with "KEY". */
#define ffs_match(s, len, starts_with, n) \
	((len) >= (n) && 0 == ffs_cmp(s, starts_with, n))

static inline ffbool ffs_matchz(const char *s, size_t len, const char *starts_with)
{
	uint n = ffsz_len(starts_with);
	return len >= n
		&& 0 == ffs_cmp(s, starts_with, n);
}

static FFINL ffbool ffsz_match(const char *sz, const char *starts_with, size_t n)
{
	ssize_t r = ffs_cmpz(starts_with, n, sz);
	return r == 0 || (r < 0 && (size_t)(-r) - 1 == n);
}

static FFINL ffbool ffsz_matchz(const char *sz, const char *starts_with)
{
	return ffsz_match(sz, starts_with, ffsz_len(starts_with));
}

/** Return TRUE if n characters are equal in both strings. */
static inline ffbool ffstr_match(const ffstr *s1, const char *s2, size_t n)
{
	return s1->len >= n
		&& 0 == ffmemcmp(s1->ptr, s2, n);
}

#define ffstr_matchcz(s, csz)  ffstr_match(s, csz, FFSLEN(csz))
#define ffstr_matchz(s, sz)  ffstr_match(s, sz, ffsz_len(sz))
#define ffstr_matchstr(s, s2)  ffstr_match(s, (s2)->ptr, (s2)->len)


// MATCH (case insensitive)

#define ffs_imatch(s, len, starts_with, n) \
	((len) >= (n) && 0 == ffs_icmp(s, starts_with, n))

static inline int ffs_imatchz(const char *s, size_t len, const char *starts_with)
{
	uint n = ffsz_len(starts_with);
	return len >= n
		&& 0 == ffs_icmp(s, starts_with, n);
}

static inline ffbool ffsz_imatch(const char *sz, const char *starts_with, size_t n)
{
	ssize_t r = ffs_icmpz(starts_with, n, sz);
	return r == 0 || (r < 0 && (size_t)(-r) - 1 == n);
}

static inline ffbool ffstr_imatch(const ffstr *s1, const char *s2, size_t n)
{
	return s1->len >= n
		&& 0 == ffs_icmp(s1->ptr, s2, n);
}

#define ffstr_imatchcz(s, csz)  ffstr_imatch(s, csz, FFSLEN(csz))


// REVERSE MATCH - match ending bytes of both strings

static inline ffbool ffstr_irmatch(const ffstr *s1, const char *s2, size_t len)
{
	return s1->len >= len
		&& 0 == ffs_icmpz(s1->ptr + s1->len - len, len, s2);
}

#define ffstr_irmatchz(s, sz)  ffstr_irmatch(s, sz, ffsz_len(sz))


/** Apply XOR on a data with a key of arbitrary length. */
FF_EXTN void ffmem_xor(byte *dst, const byte *src, size_t len, const byte *key, size_t nkey);

/** Apply XOR on a data with 4-byte key. */
FF_EXTN void ffmem_xor4(void *dst, const void *src, size_t len, uint key);


// FIND - get position of a byte or a substring

/** Search for a byte in buffer. */
FF_EXTN void * ffmemchr(const void *d, int b, size_t len);

/** Return NULL if not found. */
#define ffsz_findc(sz, ch)  strchr(sz, ch)

/** Search byte in a buffer.
Return END if not found. */
static FFINL char * ffs_find(const char *buf, size_t len, int ch) {
	char *pos = (char*)memchr(buf, ch, len);
	return (pos != NULL ? pos : (char*)buf + len);
}

/** Search byte in a buffer.
Return NULL if not found. */
#define ffs_findc(buf, len, ch)  memchr(buf, ch, len)

/** Find substring.
Return END if not found. */
FF_EXTN char * ffs_finds(const char *buf, size_t len, const char *search, size_t search_len);

FF_EXTN char * ffs_findof(const char *buf, size_t len, const char *anyof, size_t cnt);

#define ffsz_findof(sz, anyof)  strpbrk(sz, anyof)

/** Find substring.
Return -1 if not found. */
static inline ssize_t ffstr_find(const ffstr *s, const char *search, size_t search_len)
{
	const char *r = ffs_finds(s->ptr, s->len, search, search_len);
	if (r == s->ptr + s->len)
		return -1;
	return r - s->ptr;
}

#define ffstr_findstr(s, search)  ffstr_find(s, (search)->ptr, (search)->len)
#define ffstr_findz(s, search)  ffstr_find(s, search, ffsz_len(search))

/** Find end-of-line marker (LF or CRLF).
Return byte offset or 'len' if not found. */
static inline size_t ffs_findeol(const char *s, size_t len)
{
	const char *p = ffs_find(s, len, '\n');
	if (p != s && p[-1] == '\r')
		p--;
	return p - s;
}

/** Return the number of occurrences of byte. */
FF_EXTN size_t ffs_nfindc(const char *buf, size_t len, int ch);


// FIND (case insensitive)

FF_EXTN char* ffs_ifinds(const char *buf, size_t len, const char *search, size_t search_len);

static inline ssize_t ffstr_ifind(const ffstr *s, const char *search, size_t search_len)
{
	const char *r = ffs_ifinds(s->ptr, s->len, search, search_len);
	if (r == s->ptr + s->len)
		return -1;
	return r - s->ptr;
}

#define ffstr_ifindstr(s, search)  ffstr_ifind(s, (search)->ptr, (search)->len)


// REVERSE FIND

/** Perform reverse search of byte in a buffer. */
#if defined FF_WIN || defined FF_APPLE
FF_EXTN char* ffs_rfind(const char *buf, size_t len, int ch);
#else
static FFINL char* ffs_rfind(const char *buf, size_t len, int ch)
{
	char *pos = (char*)memrchr(buf, ch, len);
	return (pos != NULL ? pos : (char*)buf + len);
}
#endif

FF_EXTN char* ffs_rfindof(const char *buf, size_t len, const char *anyof, size_t cnt);


// SPLIT - split string by a byte

/** Split string by a character.
If split-character isn't found, the second string will be empty.
@first, @second: optional
@at: pointer within the range [s..s+len] or NULL.
Return @at or NULL. */
FF_EXTN const char* ffs_split2(const char *s, size_t len, const char *at, ffstr *first, ffstr *second);

#define ffs_split2by(s, len, by, first, second) \
	ffs_split2(s, len, ffs_find(s, len, by), first, second)

#define ffs_rsplit2by(s, len, by, first, second) \
	ffs_split2(s, len, ffs_rfind(s, len, by), first, second)

enum FFSTR_NEXTVAL {
	FFSTR_NV_DBLQUOT = 0x100, // (obsolete) val1 "val2 with space" val3
	FFS_NV_DBLQUOT = 0x100, // val1 "val2 with space" val3
	FFS_NV_KEEPWHITE = 0x200, // don't trim whitespace
	FFS_NV_REVERSE = 0x400, // reverse search
	FFS_NV_TABS = 0x0800, // treat whitespace as spaces and tabs
	FFS_NV_WORDS = 0x1000, // ignore 'spl' char;  instead, split by whitespace
	FFS_NV_CR = 0x2000, // treat spaces, tabs, CR as whitespace
};

/** Get the next value from input string like "val1, val2, ...".
Spaces on the edges are trimmed.
@spl: split-character OR-ed with enum FFSTR_NEXTVAL.
Return the number of processed bytes. */
FF_EXTN size_t ffstr_nextval(const char *buf, size_t len, ffstr *dst, int spl);

static inline size_t ffstr_nextval3(ffstr *src, ffstr *dst, int spl)
{
	size_t n = ffstr_nextval(src->ptr, src->len, dst, spl);
	if (spl & FFS_NV_REVERSE)
		src->len -= n;
	else
		ffstr_shift(src, n);
	return n;
}


// SKIP - skip bytes matching a byte, bytes or byte mask

/** Skip characters at the beginning of the string. */
FF_EXTN char * ffs_skip(const char *buf, size_t len, int ch);

FF_EXTN char * ffs_skipof(const char *buf, size_t len, const char *anyof, size_t cnt);

/** Skip characters by mask.
@mask: uint[8] */
FF_EXTN char* ffs_skip_mask(const char *buf, size_t len, const uint *mask);

static inline void ffstr_skip(ffstr *s, int skip_char)
{
	char *p = ffs_skip(s->ptr, s->len, skip_char);
	s->len = s->ptr + s->len - p;
	s->ptr = p;
}

static inline void ffstr_skipof(ffstr *s, ffstr skip_chars)
{
	char *p = ffs_skipof(s->ptr, s->len, skip_chars.ptr, skip_chars.len);
	s->len = s->ptr + s->len - p;
	s->ptr = p;
}


// REVERSE SKIP

/** Skip characters at the end of the string. */
FF_EXTN char* ffs_rskip(const char *buf, size_t len, int ch);

FF_EXTN char* ffs_rskipof(const char *buf, size_t len, const char *anyof, size_t cnt);

static inline void ffstr_rskip(ffstr *s, int skip_char)
{
	s->len = ffs_rskip(s->ptr, s->len, skip_char) - s->ptr;
}

static inline void ffstr_rskip1(ffstr *s, int skip_char)
{
	if (s->len != 0 && *(ffstr_end(s) - 1) == skip_char)
		s->len--;
}


// FIND IN ARRAY - get position of a string in array

/** Find string in an array of strings.
Return array index.
Return -1 if not found. */
FF_EXTN ssize_t ffstr_findarr(const ffstr *ar, size_t n, const char *search, size_t search_len);

FF_EXTN ssize_t ffstr_ifindarr(const ffstr *ar, size_t n, const char *search, size_t search_len);

/** Search a string in array using operations with type int64.
Return -1 if not found. */
FF_EXTN ssize_t ffs_findarr(const void *ar, size_t n, uint elsz, const void *s, size_t len);
#define ffs_findarr3(ar, s, len)  ffs_findarr(ar, FFCNT(ar), sizeof(*ar), s, len)

/** Search a string in array of pointers to NULL-terminated strings.
Return -1 if not found. */
FF_EXTN ssize_t ffs_findarrz(const char *const *ar, size_t n, const char *search, size_t search_len);
FF_EXTN ssize_t ffs_ifindarrz(const char *const *ar, size_t n, const char *search, size_t search_len);
#define ffszarr_find(ar, n, search, search_len) \
	ffs_findarrz(ar, n, search, search_len)
#define ffszarr_ifind(ar, n, search, search_len) \
	ffs_ifindarrz(ar, n, search, search_len)
FF_EXTN ssize_t ffszarr_findsorted(const char *const *ar, size_t n, const char *search, size_t search_len);
FF_EXTN ssize_t ffszarr_ifindsorted(const char *const *ar, size_t n, const char *search, size_t search_len);

/** Search a string in char[n][m] array.
Return -1 if not found. */
FF_EXTN ssize_t ffcharr_findsorted(const void *ar, size_t n, size_t m, const char *search, size_t search_len);

/** Find entry that matches expression "KEY=VAL".
Return pointer to VAL;  NULL if not found. */
FF_EXTN char* ffszarr_findkeyz(const char *const *arz, const char *key, size_t key_len);


/** Count entries in array with the last entry =NULL. */
FF_EXTN size_t ffszarr_countz(const char *const *arz);


// COPY DATA

#define _ffmemcpy  memcpy
#ifdef FFMEM_DBG
#include <FFOS/atomic.h>
FF_EXTN ffatomic ffmemcpy_total;
FF_EXTN void* ffmemcpy(void *dst, const void *src, size_t len);
#else
#define ffmemcpy  _ffmemcpy
#endif

#define ffmem_copyT(dst, src, T)  ffmemcpy(dst, src, sizeof(T))
#define ffmem_ncopy(dst, src, n, elsz)  ffmemcpy(dst, src, (n) * (elsz))
#define ffmem_ncopyT(dst, src, n, T)  ffmemcpy(dst, src, (n) * sizeof(T))

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

/** Copy the contents of ffstr* into char* buffer. */
#define ffs_copystr(dst, bufend, pstr)  ffs_copy(dst, bufend, (pstr)->ptr, (pstr)->len)

#define ffs_copycz(dst, bufend, csz)  ffs_copy(dst, bufend, csz, FFSLEN(csz))

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

#define ffsz_fcopyz(dst, src)  strcpy(dst, src)

#define ffsz_copycz(dst, csz)  ffmemcpy(dst, csz, sizeof(csz))

/** Allocate memory and copy string. */
static FFINL char* ffsz_alcopy(const char *src, size_t len)
{
	char *s = (char*)ffmem_alloc(len + 1);
	if (s != NULL)
		ffsz_fcopy(s, src, len);
	return s;
}

#define ffsz_alcopyz(src)  ffsz_alcopy(src, ffsz_len(src))
#define ffsz_alcopystr(src)  ffsz_alcopy((src)->ptr, (src)->len)

/**
Return the bytes copied. */
static inline size_t ffs_append(void *dst, size_t off, size_t cap, const void *src, size_t len)
{
	size_t n = ffmin(len, cap - off);
	ffmemcpy((char*)dst + off, src, n);
	return n;
}


/** Select the larger buffer */
static inline void ffs_max(const void *s1, size_t len1, const void *s2, size_t len2, void **out_ptr, size_t *out_len)
{
	if (len1 >= len2) {
		*out_ptr = (void*)s1;
		*out_len = len1;
	} else {
		*out_ptr = (void*)s2;
		*out_len = len2;
	}
}


// CONVERT CASE - convert character case

/** Convert case (ANSI).
Return the number of bytes written. */
FF_EXTN size_t ffs_lower(char *dst, const char *end, const char *src, size_t len);
FF_EXTN size_t ffs_upper(char *dst, const char *end, const char *src, size_t len);
FF_EXTN size_t ffs_titlecase(char *dst, const char *end, const char *src, size_t len);

static FFINL char* ffsz_alcopylwr(const char *src, size_t len)
{
	char *s = (char*)ffmem_alloc(len + 1);
	if (s == NULL)
		return NULL;
	ffs_lower(s, s + len, src, len);
	s[len] = '\0';
	return s;
}


// SYSTEM STRING - operations with a system-character string

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
	char *s = (char*)ffmem_alloc(cap);
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


// REPLACE - find and replace text

/** Find and replace a character.
On return '*n' contains the number of replacements made.
Return the number of chars written. */
FF_EXTN size_t ffs_replacechar(const char *src, size_t len, char *dst, size_t cap, int search, int replace, size_t *n);

enum FFSTR_REPL_F {
	FFSTR_REPL_ICASE = 1, // case-insensitive matching
	FFSTR_REPL_NOTAIL = 2, // don't copy tail data after replacement
};

/** Replace text in 'src' and write output to 'dst'.
'dst': points to an allocated buffer.  Min size: (src.len - search.len + replace.len).
'flags': enum FFSTR_REPL_F
Return the position after replacement;  0:no match ('dst' will be empty). */
FF_EXTN ssize_t ffstr_replace(ffstr *dst, const ffstr *src, const ffstr *search, const ffstr *replace, uint flags);

enum FFS_ESCAPE {
	FFS_ESC_BKSLX = 0, // replace with \xXX

	FFS_ESC_NONPRINT = 0x10, // escape '\\' and non-printable.  '\t', '\r', '\n' are not escaped.
};

/** Replace special characters.
@type: enum FFS_ESCAPE.
Return the number of bytes written.
Return <0 if there is no enough space (the number of input bytes processed, negative value). */
FF_EXTN ssize_t _ffs_escape(char *dst, size_t cap, const char *s, size_t len, int type, const uint *mask);
FF_EXTN ssize_t ffs_escape(char *dst, size_t cap, const char *s, size_t len, int type);


// TYPE CONVERT - convert between string, number and boolean

/** Lowercase/uppercase hex alphabet. */
FF_EXTN const char ffhex[16];
FF_EXTN const char ffHEX[16];

/** Convert a byte into 2-byte hex string.
@hex: alphabet to use. */
static FFINL int ffs_hexbyte(char *dst, int b, const char *hex) {
	dst[0] = hex[(byte)b >> 4];
	dst[1] = hex[(byte)b & 0x0f];
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

#define ffs_toint32(src, len, dst, flags) \
	ffs_toint(src, len, dst, FFS_INT32 | (flags))

#define ffs_toint64(src, len, dst, flags) \
	ffs_toint(src, len, dst, FFS_INT64 | (flags))

/** Return TRUE on success. */
#define ffstr_toint(s, dst, flags) \
	((s)->len != 0 && (s)->len == ffs_toint((s)->ptr, (s)->len, dst, flags))

enum { FFINT_MAXCHARS = FFSLEN("18446744073709551615") };

enum FFINT_TOSTR {
	FFINT_SIGNED = 1
	, FFINT_HEXLOW = 2
	, FFINT_HEXUP = 4
	, FFINT_OCTAL = 0x20
	, FFINT_ZEROWIDTH = 8
	, FFINT_SEP1000 = 0x10 // use thousands separator, e.g. "1,000"
	, FFINT_NEG = 0x40 // value is always negative

	, _FFINT_WIDTH_MASK = 0xff000000
	, _FFS_FLT_FRAC_WIDTH_MASK = 0x00ff0000
};

#define FFINT_WIDTH(width) ((width) << 24)
#define FFS_INT_WFRAC(width) ((width) << 16)

/** Convert integer to string.
'flags': enum FFINT_TOSTR
Return the number of chars written. */
FF_EXTN uint ffs_fromint(uint64 i, char *dst, size_t cap, int flags);

enum FFS_FROMSIZE {
	FFS_FROMSIZE_FRAC = 1, //use 1-digit fraction: 1524 -> "1.5k"
	FFS_FROMSIZE_Z = 2, //terminate string with NULL-char
};

/** Convert integer size to human readable string.
1023 -> "1023";  1024 -> "1k";  2047 -> "1k"
@flags: enum FFS_FROMSIZE
Return number of bytes written. */
FF_EXTN int ffs_fromsize(char *buf, size_t cap, uint64 size, uint flags);

/** Convert string to a floating point number.
Return the number of chars processed.
Return 0 on error. */
FF_EXTN uint ffs_tofloat(const char *s, size_t len, double *dst, int flags);

/** Convert float to string.
@flags: enum FFINT_TOSTR
Return bytes written. */
FF_EXTN uint ffs_fromfloat(double d, char *dst, size_t cap, uint flags);

/** Convert "true" or "false" to boolean integer.
Return the number of chars processed;  0 on error. */
FF_EXTN uint ffs_tobool(const char *s, size_t len, ffbool *dst, uint flags);

/** Return TRUE on success. */
#define ffstr_tobool(s, dst, flags) \
	((s)->len != 0 && (s)->len == ffs_tobool((s)->ptr, (s)->len, dst, flags))

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

/** Convert hex string to bytes data.
dst: if NULL, return the capacity needed
Return the number of bytes written;  <0 on error. */
FF_EXTN ssize_t ffs_hex_to_bytes(void *dst, size_t cap, const char *s, size_t len);


// FORMATTED I/O - string write/read by %-format string and parameters

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
Return 0 on error. */
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

/** Add %-formatted data into array's tail (the buffer must be allocated). */
static inline int ffstr_addfmt(ffstr *s, size_t cap, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	ssize_t n = ffs_fmtv2(s->ptr + s->len, cap - s->len, fmt, args);
	va_end(args);
	if (n > 0)
		s->len += n;
	return n;
}

/** Match string by format:
 "% [width] x u|U" - uint|uint64
 "% width s" - char* (copy)
 "%S" - ffstr*
  "%S" - match letters only
  "% width S" - match "width" bytes
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


// PATTERN MATCH - match text by a pattern

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


// COMPARE VERSIONS - compare version strings

enum FFSTR_VERCMP {
	FFSTR_VERCMP_1GREATER2 = 1,
	FFSTR_VERCMP_EQ = 0,
	FFSTR_VERCMP_1LESS2 = -1,
	FFSTR_VERCMP_ERRV1 = -2, //error in the first string
	FFSTR_VERCMP_ERRV2 = -3, //error in the second string
};

/** Compare dotted-decimal version string: e.g. "2.0" < "10.0".
Return enum FFSTR_VERCMP. */
FF_EXTN int ffstr_vercmp(const ffstr *v1, const ffstr *v2);


// FFBSTR

typedef struct ffbstr {
	ushort len;
	char data[0];
} ffbstr;

/** Add one more ffbstr into array.  Reallocate memory, if needed.
If @data is set, copy it into a new ffbstr. */
FF_EXTN ffbstr* ffbstr_push(ffstr *buf, const char *data, size_t len);

/** Copy data into ffbstr. */
static inline void ffbstr_copy(ffbstr *bs, const char *data, size_t len)
{
	bs->len = (ushort)len;
	ffmemcpy(bs->data, data, len);
}

/** Get the next string from array.
@off: set value to 0 before the first call.
Return 0 if there is no more data. */
static inline ffbstr* ffbstr_next(const char *buf, size_t len, size_t *off, ffstr *dst)
{
	ffbstr *bs = (ffbstr*)(buf + *off);
	if (*off == len)
		return NULL;

	if (dst != NULL)
		ffstr_set(dst, bs->data, bs->len);
	*off += sizeof(ffbstr) + bs->len;
	return bs;
}


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


static inline size_t ffsz_nlen(const char *s, size_t maxlen)
{
	return ffs_find(s, maxlen, '\0') - s;
}
