/** Array, buffer range.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/mem.h>
#include <FF/string.h>

/** Declare an array. */
#define FFARR(T) \
	size_t len; \
	T *ptr; \
	size_t cap;

/** Default char-array. */
typedef struct { FFARR(char) } ffarr;

/** Set a buffer. */
#define ffarr_set(ar, data, n) \
do { \
	(ar)->ptr = data; \
	(ar)->len = n; \
} while(0)

/** Shift buffer pointers. */
#define ffarr_shift(ar, by) \
do { \
	ssize_t __shiftBy = (by); \
	(ar)->ptr += __shiftBy; \
	(ar)->len -= __shiftBy; \
} while (0)

/** Set null array. */
#define ffarr_null(ar) \
do { \
	(ar)->ptr = NULL; \
	(ar)->len = (ar)->cap = 0; \
} while (0)

/** The last element in array. */
#define ffarr_back(ar)  ((ar)->ptr[(ar)->len - 1])

/** The tail of array. */
#define ffarr_end(ar)  ((ar)->ptr + (ar)->len)

/** The number of free elements. */
#define ffarr_unused(ar)  ((ar)->cap - (ar)->len)

/** Forward walk. */
#define FFARR_WALK(ar, it) \
	for (it = (ar)->ptr;  it != (ar)->ptr + (ar)->len;  it++)

/** Reverse walk. */
#define FFARR_RWALK(ar, it) \
	if ((ar)->len != 0) \
		for (it = (ar)->ptr + (ar)->len - 1;  it >= (ar)->ptr;  it--)

FF_EXTN void * _ffarr_realloc(ffarr *ar, size_t newlen, size_t elsz);

/** Reallocate array memory.
Return NULL on error. */
#define ffarr_realloc(ar, newlen) \
	_ffarr_realloc((ffarr*)(ar), newlen, sizeof(*(ar)->ptr))

static FFINL void * _ffarr_alloc(ffarr *ar, size_t len) {
	ffarr_null(ar);
	return ffarr_realloc(ar, len);
}

#define ffarr_alloc(ar, len) \
	_ffarr_alloc((ffarr*)(ar), (len) * sizeof(*(ar)->ptr))

FF_EXTN void _ffarr_free(ffarr *ar);

/** Deallocate array memory. */
#define ffarr_free(ar)  _ffarr_free((ffarr*)ar)

/** Add items into array.
Return the tail.
Return NULL on error. */
FF_EXTN void * _ffarr_append(ffarr *ar, const void *src, size_t num, size_t elsz);

#define ffarr_append(ar, src, num) \
	_ffarr_append((ffarr*)ar, src, num, sizeof(*(ar)->ptr))


typedef struct {
	size_t len;
	char *ptr;
} ffstr;

typedef struct { FFARR(char) } ffstr3;

#define FFSTR_INIT(s)  { FFSLEN(s), (char*)(s) }

#define FFSTR2(s)  (s).ptr, (s).len

static FFINL void ffstr_set(ffstr *s, const char *d, size_t len) {
	ffarr_set(s, (char*)d, len);
}

#define ffstr_null(ar) \
do { \
	(ar)->ptr = NULL; \
	(ar)->len = 0; \
} while (0)

#define ffstr_shift  ffarr_shift


static FFINL ffbool ffstr_eq(const ffstr *s1, const char *s2, size_t n) {
	return s1->len == n
		&& (n == 0 || 0 == ffs_cmp(s1->ptr, s2, n));
}

static FFINL ffbool ffstr_ieq(const ffstr *s1, const char *s2, size_t n) {
	return s1->len == n
		&& (n == 0 || 0 == ffs_icmp(s1->ptr, s2, n));
}

/** Return TRUE if both strings are equal. */
#define ffstr_ieq2(s1, s2)  ffstr_ieq(s1, (s2)->ptr, (s2)->len)

/** Return TRUE if an array is equal to a NULL-terminated string. */
static FFINL ffbool ffstr_eqz(const ffstr *s1, const char *sz2) {
	return (0 == ffs_cmp(s1->ptr, sz2, s1->len)
		&& sz2[s1->len] == '\0');
}

/** Return TRUE if n characters are equal in both strings. */
static FFINL ffbool ffstr_match(const ffstr *s1, const char *s2, size_t n) {
	return s1->len >= n
		&& ((s1->len | n) == 0 || 0 == ffs_cmp(s1->ptr, s2, n));
}

static FFINL ffbool ffstr_imatch(const ffstr *s1, const char *s2, size_t n) {
	return s1->len >= n
		&& ((s1->len | n) == 0 || 0 == ffs_icmp(s1->ptr, s2, n));
}


static FFINL char * ffstr_alloc(ffstr *s, size_t cap) {
	s->len = 0;
	s->ptr = (char*)ffmem_alloc(cap * sizeof(char));
	return s->ptr;
}

static FFINL void ffstr_free(ffstr *s) {
	FF_SAFECLOSE(s->ptr, NULL, ffmem_free);
	s->len = 0;
}


#if defined FF_UNIX
typedef ffstr ffqstr;
typedef ffstr3 ffqstr3;
#define ffqstr_set ffstr_set
#define ffqstr_alloc ffstr_alloc
#define ffqstr_free ffstr_free

#elif defined FF_WIN
typedef struct {
	size_t len;
	ffsyschar *ptr;
} ffqstr;

typedef struct { FFARR(ffsyschar) } ffqstr3;

static FFINL void ffqstr_set(ffqstr *s, const ffsyschar *d, size_t len) {
	ffarr_set(s, (ffsyschar*)d, len);
}

#define ffqstr_alloc(s, cap) \
	((ffsyschar*)ffstr_alloc((ffstr*)(s), (cap) * sizeof(ffsyschar)))

#define ffqstr_free(s)  ffstr_free((ffstr*)(s))

#endif

/** Find string in an array of strings.
Return array index.
Return -1 if not found. */
FF_EXTN ssize_t ffstr_findarr(const ffstr *ar, size_t n, const char *search, size_t search_len);

/** Get the next value from input string like "val1, val2, ...".
Spaces on the edges are trimmed.
Return the number of processed bytes. */
FF_EXTN size_t ffstr_nextval(const char *buf, size_t len, ffstr *dst, int spl);


typedef struct {
	ushort len
		, off;
} ffrange;

static FFINL void ffrang_set(ffrange *r, const char *base, const char *s, size_t len) {
	r->off = (short)(s - base);
	r->len = (ushort)len;
}

static FFINL ffstr ffrang_get(const ffrange *r, const char *base) {
	ffstr s;
	ffstr_set(&s, base + r->off, r->len);
	return s;
}

static FFINL void ffrang_clear(ffrange *r) {
	r->off = r->len = 0;
}
