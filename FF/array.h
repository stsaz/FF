/** Array, buffer range.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/mem.h>
#include <FF/string.h>
#include <FF/chain.h>
#include <FFOS/file.h>


#define FFARR_WALKN(ar, n, it, elsz) \
	for (it = (void*)(ar) \
		;  it != (void*)((char*)(ar) + (n) * elsz) \
		;  it = (void*)((char*)it + elsz))

#define FFARR_WALKNT(ar, n, it, T) \
	FFARR_WALKN(ar, n, it, sizeof(T))

/** FOREACH() for array pointer, e.g. int *ptr */
#define FFARR_FOREACH(ptr, n, it) \
	for (it = (ptr);  it != (ptr) + (n);  it++)

/** FOREACH() for static array, e.g. int ar[4] */
#define FFARRS_FOREACH(ar, it) \
	for (it = (ar);  it != (ar) + FFCNT(ar);  it++)


typedef struct ffarr2 {
	size_t len;
	void *ptr;
} ffarr2;

#define FFARR2_WALK(a, it) \
	FFARR_WALKN((a)->ptr, (a)->len, it, sizeof(*(a)->ptr))

#define FFARR2_RWALK(ar, it) \
	for (it = ffarr2_last(ar);  it - (ar)->ptr >= 0;  it--)

/** Set data pointer and length. */
#define ffarr2_set(a, data, n) \
do { \
	(a)->ptr = data; \
	(a)->len = n; \
} while(0)

static FFINL void* ffarr2_alloc(ffarr2 *a, size_t n, size_t elsz)
{
	a->len = 0;
	return (a->ptr = ffmem_alloc(n * elsz));
}

#define ffarr2_allocT(a, n, T)  ffarr2_alloc(a, n, sizeof(T))

static FFINL void* ffarr2_calloc(ffarr2 *a, size_t n, size_t elsz)
{
	a->len = 0;
	return (a->ptr = ffmem_calloc(n, elsz));
}

#define ffarr2_callocT(a, n, T)  ffarr2_calloc(a, n, sizeof(T))

FF_EXTN void* ffarr2_realloc(ffarr2 *a, size_t n, size_t elsz);

#define ffarr2_grow(a, by, elsz)  ffarr2_realloc(a, (a)->len + by, elsz)

#define ffarr2_free(a) \
do { \
	ffmem_safefree0((a)->ptr); \
	(a)->len = 0; \
} while (0)

#define FFARR2_FREE_ALL(a, func, T) \
do { \
	T *__it; \
	FFARR_WALKT(a, __it, T) { \
		func(__it); \
	} \
	ffarr2_free(a); \
} while (0)

#define FFARR2_FREE_ALL_PTR(a, func, T) \
do { \
	T *__it; \
	FFARR_WALKT(a, __it, T) { \
		func(*__it); \
	} \
	ffarr2_free(a); \
} while (0)

#define ffarr2_last(a)  (&(a)->ptr[(a)->len - 1])

#define ffarr2_push(a)  (&(a)->ptr[(a)->len++])

/** Append data to an array. */
static FFINL size_t ffarr2_addf(ffarr2 *a, const void *src, size_t n, size_t elsz)
{
	if ((char*)a->ptr + a->len * elsz != src)
		ffmemcpy((char*)a->ptr + a->len * elsz, src, n * elsz);
	a->len += n;
	return n;
}

#define ffarr2_add(a, cap, src, n, elsz) \
	ffarr2_addf(a, src, ffmin(n, (cap) - (a)->len), elsz)

/** Remove 1 element and shift other elements to the left.
A[0]...  ( A[i] )  A[i+1]...
*/
#define ffarr2_rm_shift(a, i) \
do { \
	memmove(&(a)->ptr[i], &(a)->ptr[i + 1], ((a)->len - (i + 1)) * sizeof(*(a)->ptr)); \
	(a)->len--; \
} while (0)

#define ffarr_zero(a)  ffmem_zero((a)->ptr, (a)->len)


/** Declare an array. */
#define FFARR(T) \
	size_t len; \
	T *ptr; \
	size_t cap;

/** Default char-array.
Can be safely casted to 'ffstr'. */
typedef struct ffarr { FFARR(char) } ffarr;

/** Char-array with offset field. */
typedef struct ffarr4 {
	size_t len;
	char *ptr;
	size_t cap;
	size_t off;
} ffarr4;

/** Set a buffer. */
#define ffarr_set(ar, data, n) \
do { \
	(ar)->ptr = data; \
	(ar)->len = n; \
} while(0)

#define ffarr_set3(ar, d, _len, _cap) \
do { \
	(ar)->ptr = d; \
	(ar)->len = _len; \
	(ar)->cap = _cap; \
} while(0)

#define ffarr_setshift(a, p, n, shift) \
do { \
	ssize_t _by = (shift); \
	(a)->ptr = (p) + (_by),  (a)->len = (n) - (_by); \
} while (0)

#define FFARR_SHIFT(ptr, len, by) \
do { \
	ssize_t _by = (by); \
	(ptr) += (_by); \
	(len) -= (_by); \
} while (0)

/** Shift buffer pointers. */
#define ffarr_shift(ar, by)  FFARR_SHIFT((ar)->ptr, (ar)->len, by)

/** Set null array. */
#define ffarr_null(ar) \
do { \
	(ar)->ptr = NULL; \
	(ar)->len = (ar)->cap = 0; \
} while (0)

/** Acquire array data. */
#define ffarr_acq(dst, src) \
do { \
	*(dst) = *(src); \
	(src)->cap = 0; \
} while (0)

#define ffarr_acquire  ffarr_acq

#define _ffarr_item(ar, idx, elsz)  ((ar)->ptr + idx * elsz)
#define ffarr_itemT(ar, idx, T)  (&((T*)(ar)->ptr)[idx])

/** The last element in array. */
#define ffarr_back(ar)  ((ar)->ptr[(ar)->len - 1])
#define ffarr_lastT(ar, T)  (&((T*)(ar)->ptr)[(ar)->len - 1])

/** The tail of array. */
#define _ffarr_end(ar, elsz)  _ffarr_item(ar, ar->len, elsz)
#define ffarr_end(ar)  ((ar)->ptr + (ar)->len)
#define ffarr_endT(ar, T)  ((T*)(ar)->ptr + (ar)->len)

/** Get the edge of allocated buffer. */
#define ffarr_edge(ar)  ((ar)->ptr + (ar)->cap)

/** The number of free elements. */
#define ffarr_unused(ar)  ((ar)->cap - (ar)->len)

/** Return TRUE if array is full. */
#define ffarr_isfull(ar)  ((ar)->len == (ar)->cap)

/** Forward walk. */
#define _FFARR_WALK(ar, it, elsz) \
	FFARR_WALKN((ar)->ptr, (ar)->len, it, elsz)

#define FFARR_WALKT(ar, it, T) \
	FFARR_WALKN((ar)->ptr, (ar)->len, it, sizeof(T))

#define FFARR_WALK(ar, it) \
	for (it = (ar)->ptr;  it != (ar)->ptr + (ar)->len;  it++)

/** Reverse walk. */
#define FFARR_RWALKT(ar, it, T) \
	for (it = ffarr_lastT(ar, T);  it - (T*)(ar)->ptr >= 0;  it--)

#define FFARR_RWALK(ar, it) \
	if ((ar)->len != 0) \
		for (it = (ar)->ptr + (ar)->len - 1;  it >= (ar)->ptr;  it--)

FF_EXTN void * _ffarr_realloc(ffarr *ar, size_t newlen, size_t elsz);

/** Reallocate array memory if new size is larger.
Pointing buffer: transform into an allocated buffer, copying data.
Return NULL on error. */
#define ffarr_realloc(ar, newlen) \
	_ffarr_realloc((ffarr*)(ar), newlen, sizeof(*(ar)->ptr))

static FFINL void * _ffarr_alloc(ffarr *ar, size_t len, size_t elsz) {
	ffarr_null(ar);
	return _ffarr_realloc(ar, len, elsz);
}

/** Allocate memory for an array. */
#define ffarr_alloc(ar, len) \
	_ffarr_alloc((ffarr*)(ar), (len), sizeof(*(ar)->ptr))

#define ffarr_allocT(ar, len, T) \
	_ffarr_alloc(ar, len, sizeof(T))

static FFINL void* _ffarr_allocz(ffarr *ar, size_t len, size_t elsz)
{
	void *r;
	if (NULL != (r = _ffarr_alloc(ar, len, elsz)))
		ffmem_zero(ar->ptr, len * elsz);
	return r;
}

#define ffarr_alloczT(ar, len, T) \
	_ffarr_allocz(ar, len, sizeof(T))

#define ffarr_reallocT(ar, len, T) \
	_ffarr_realloc(ar, len, sizeof(T))

enum { FFARR_GROWQUARTER = 0x80000000 };

/** Reserve more space for an array.
lowat: minimum number of elements to allocate
 Add FFARR_GROWQUARTER to the value to grow at least by 1/4 of array size.
*/
FF_EXTN char *_ffarr_grow(ffarr *ar, size_t by, ssize_t lowat, size_t elsz);

#define ffarr_growT(ar, by, lowat, T) \
	_ffarr_grow(ar, by, lowat, sizeof(T))

#define ffarr_grow(ar, by, lowat) \
	_ffarr_grow((ffarr*)(ar), (by), (lowat), sizeof(*(ar)->ptr))

FF_EXTN void _ffarr_free(ffarr *ar);

/** Deallocate array memory. */
#define ffarr_free(ar)  _ffarr_free((ffarr*)ar)

#define FFARR_FREE_ALL(a, func, T) \
do { \
	T *__it; \
	FFARR_WALKT(a, __it, T) { \
		func(__it); \
	} \
	ffarr_free(a); \
} while (0)

#define FFARR_FREE_ALL_PTR(a, func, T) \
do { \
	T *__it; \
	FFARR_WALKT(a, __it, T) { \
		func(*__it); \
	} \
	ffarr_free(a); \
} while (0)

/** Add 1 item into array.
Return the item pointer.
Return NULL on error. */
FF_EXTN void * _ffarr_push(ffarr *ar, size_t elsz);

#define ffarr_push(ar, T) \
	(T*)_ffarr_push((ffarr*)ar, sizeof(T))

#define ffarr_pushT(ar, T) \
	(T*)_ffarr_push(ar, sizeof(T))

/** Add 1 element.  Grow by the specified size if needed.
Return element pointer;  NULL on error. */
FF_EXTN void* ffarr_pushgrow(ffarr *ar, size_t lowat, size_t elsz);

#define ffarr_pushgrowT(ar, lowat, T) \
	(T*)ffarr_pushgrow(ar, lowat, sizeof(T))

#define ffarr_add(a, src, n, elsz) \
	ffarr2_add((ffarr2*)a, (a)->cap, src, n, elsz)

/** Add items into array.  Reallocate memory, if needed.
Return the tail.
Return NULL on error. */
FF_EXTN void * _ffarr_append(ffarr *ar, const void *src, size_t num, size_t elsz);

#define ffarr_append(ar, src, num) \
	_ffarr_append((ffarr*)ar, src, num, sizeof(*(ar)->ptr))

/** Add data into array until its size reaches the specified amount.
Don't copy any data if the required size is already available.
Return the number of bytes processed;  -1 on error. */
FF_EXTN ssize_t ffarr_gather(ffarr *ar, const char *d, size_t len, size_t until);

/**
Return the number of bytes processed;  0 if more data is needed;  -1 on error. */
static FFINL ssize_t ffarr_append_until(ffarr *ar, const char *d, size_t len, size_t until)
{
	ssize_t r = ffarr_gather(ar, d, len, until);
	FF_ASSERT(r != 0 || len == 0);
	if (r > 0 && ar->len < until)
		return 0;
	return r;
}

static FFINL void * _ffarr_copy(ffarr *ar, const void *src, size_t num, size_t elsz) {
	ar->len = 0;
	return _ffarr_append(ar, src, num, elsz);
}

#define ffarr_copy(ar, src, num) \
	_ffarr_copy((ffarr*)ar, src, num, sizeof(*(ar)->ptr))

/** Allocate and copy data from memory pointed by 'a.ptr'. */
#define ffarr_copyself(a) \
do { \
	if ((a)->cap == 0 && (a)->len != 0) \
		ffarr_realloc(a, (a)->len); \
} while (0)

#define _ffarr_swap(p1, p2, n, elsize) \
do { \
	char tmp[n * elsize]; \
	ffmemcpy(tmp, p1, n * elsize); \
	memmove(p1, p2, n * elsize); \
	ffmemcpy(p2, tmp, n * elsize); \
} while (0)

/** Remove elements.
Pointing buffer: shift left/right side.
 Does nothing if the data being removed is in the middle.
Allocated buffer: move next elements to the left. */
FF_EXTN void _ffarr_rm(ffarr *ar, size_t off, size_t n, size_t elsz);

/** Remove elements from the left. */
#define _ffarr_rmleft(ar, n, elsz)  _ffarr_rm(ar, 0, n, elsz)

/** Remove element from array.  Move the last element into the hole. */
static FFINL void _ffarr_rmswap(ffarr *ar, void *el, size_t elsz) {
	const void *last;
	FF_ASSERT(ar->len != 0);
	ar->len--;
	last = ar->ptr + ar->len * elsz;
	if (el != last)
		memmove(el, last, elsz);
}

#define ffarr_rmswap(ar, el) \
	_ffarr_rmswap((ffarr*)ar, (void*)el, sizeof(*(ar)->ptr))

#define ffarr_rmswapT(ar, el, T) \
	_ffarr_rmswap(ar, (void*)el, sizeof(T))

/** Shift elements to the right.
A[0]...  ( A[i]... )  A[i+n]... ->
A[0]...  ( ... )  A[i]...
*/
static inline void _ffarr_shiftr(ffarr *ar, size_t i, size_t n, size_t elsz)
{
	char *dst = ar->ptr + (i + n) * elsz;
	const char *src = ar->ptr + i * elsz;
	const char *end = ar->ptr + ar->len * elsz;
	memmove(dst, src, end - src);
}

/** Remove elements from the middle and shift other elements to the left:
A[0]...  ( A[i]... )  A[i+n]...
*/
static inline void _ffarr_rmshift_i(ffarr *ar, size_t i, size_t n, size_t elsz)
{
	char *dst = ar->ptr + i * elsz;
	const char *src = ar->ptr + (i + n) * elsz;
	const char *end = ar->ptr + ar->len * elsz;
	memmove(dst, src, end - src);
	ar->len -= n;
}

/**
"...DATA..." -> "DATA" */
FF_EXTN void _ffarr_crop(ffarr *ar, size_t off, size_t n, size_t elsz);


typedef ffarr ffstr3;

#define FFSTR_INIT(s)  { FFSLEN(s), (char*)(s) }

#define FFSTR2(s)  (s).ptr, (s).len

#define ffstr_set(s, d, len)  ffarr_set(s, (char*)(d), len)

#define ffstr_set2(s, src)  ffarr_set(s, (src)->ptr, (src)->len)

/** Set constant NULL-terminated string. */
#define ffstr_setcz(s, csz)  ffarr_set(s, (char*)csz, FFSLEN(csz))

/** Set NULL-terminated string. */
#define ffstr_setz(s, sz)  ffarr_set(s, (char*)sz, ffsz_len(sz))

#define ffstr_setnz(s, sz, maxlen)  ffarr_set(s, (char*)sz, ffsz_nlen(sz, maxlen))

/** Set ffstr from ffiovec. */
#define ffstr_setiovec(s, iov)  ffarr_set(s, (iov)->iov_base, (iov)->iov_len)

#define ffstr_null(ar) \
do { \
	(ar)->ptr = NULL; \
	(ar)->len = 0; \
} while (0)

static FFINL void ffstr_acq(ffstr *dst, ffstr *src) {
	*dst = *src;
	ffstr_null(src);
}

static FFINL void ffstr_acqstr3(ffstr *dst, ffstr3 *src) {
	dst->ptr = src->ptr;
	dst->len = src->len;
	ffarr_null(src);
}

static FFINL int ffstr_popfront(ffstr *s)
{
	FF_ASSERT(s->len != 0);
	s->len--;
	return *(s->ptr++);
}

#define ffstr_shift  ffarr_shift

/** Copy the contents of ffstr* into char* buffer. */
#define ffs_copystr(dst, bufend, pstr)  ffs_copy(dst, bufend, (pstr)->ptr, (pstr)->len)

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


#define ffstr_cmp2(s1, s2)  ffs_cmp4((s1)->ptr, (s1)->len, (s2)->ptr, (s2)->len)

/** Compare ANSI strings.  Case-insensitive. */
#define ffstr_icmp(str1, s2, len2)  ffs_icmp4((str1)->ptr, (str1)->len, s2, len2)
#define ffstr_icmp2(s1, s2)  ffs_icmp4((s1)->ptr, (s1)->len, (s2)->ptr, (s2)->len)

#define ffstr_eq(s, d, n) \
	((s)->len == (n) && 0 == ffmemcmp((s)->ptr, d, n))

/** Return TRUE if both strings are equal. */
#define ffstr_eq2(s1, s2)  ffstr_eq(s1, (s2)->ptr, (s2)->len)

static FFINL ffbool ffstr_ieq(const ffstr *s1, const char *s2, size_t n) {
	return s1->len == n
		&& 0 == ffs_icmp(s1->ptr, s2, n);
}

/** Return TRUE if both strings are equal. Case-insensitive */
#define ffstr_ieq2(s1, s2)  ffstr_ieq(s1, (s2)->ptr, (s2)->len)

/** Return TRUE if an array is equal to a NULL-terminated string. */
#define ffstr_eqz(str1, sz2)  (0 == ffs_cmpz((str1)->ptr, (str1)->len, sz2))
#define ffstr_ieqz(str1, sz2)  (0 == ffs_icmpz((str1)->ptr, (str1)->len, sz2))

/** Compare ffstr object and constant NULL-terminated string. */
#define ffstr_eqcz(s, constsz)  ffstr_eq(s, constsz, FFSLEN(constsz))

/** Compare ffstr object and constant NULL-terminated string.  Case-insensitive. */
#define ffstr_ieqcz(s, constsz)  ffstr_ieq(s, constsz, FFSLEN(constsz))


#define ffstr_alloc(s, cap)  ffarr2_alloc((ffarr2*)s, cap, sizeof(char))

#define ffstr_free(s)  ffarr2_free(s)

static FFINL size_t ffstr_cat(ffstr *s, size_t cap, const char *d, size_t len) {
	return ffarr2_add((ffarr2*)s, cap, d, len, sizeof(char));
}

static FFINL char* ffstr_dup(ffstr *s, const char *d, size_t len) {
	if (NULL == ffstr_alloc(s, len))
		return NULL;
	ffstr_cat(s, len, d, len);
	return s->ptr;
}
#define ffstr_copy(dst, d, len)  ffstr_dup(dst, d, len)

#define ffstr_alcopyz(dst, sz)  ffstr_copy(dst, sz, ffsz_len(sz))
#define ffstr_alcopystr(dst, src)  ffstr_copy(dst, (src)->ptr, (src)->len)


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

static inline void ffstr_skip(ffstr *s, int skip_char)
{
	char *p = ffs_skip(s->ptr, s->len, skip_char);
	s->len = s->ptr + s->len - p;
	s->ptr = p;
}

static inline void ffstr_rskip(ffstr *s, int skip_char)
{
	s->len = ffs_rskip(s->ptr, s->len, skip_char) - s->ptr;
}

enum FFSTR_NEXTVAL {
	FFSTR_NV_DBLQUOT = 0x100, // val1 "val2 with space" val3
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

static FFINL size_t ffstr_nextval3(ffstr *src, ffstr *dst, int spl)
{
	size_t n = ffstr_nextval(src->ptr, src->len, dst, spl);
	if (spl & FFS_NV_REVERSE)
		src->len -= n;
	else
		ffstr_shift(src, n);
	return n;
}

#define ffstr_toint(s, dst, flags) \
	((s)->len != 0 && (s)->len == ffs_toint((s)->ptr, (s)->len, dst, flags))
#define ffstr_tobool(s, dst, flags) \
	((s)->len != 0 && (s)->len == ffs_tobool((s)->ptr, (s)->len, dst, flags))

/** Trim data by absolute bounds.
Return the number of bytes processed from the beginning. */
FF_EXTN size_t ffstr_crop_abs(ffstr *data, uint64 data_start, uint64 off, uint64 size);


static FFINL void ffstr3_cat(ffstr3 *s, const char *d, size_t len) {
	ffstr_cat((ffstr*)s, s->cap, d, len);
}

FF_EXTN size_t ffstr_catfmtv(ffstr3 *s, const char *fmt, va_list args);

static FFINL size_t ffstr_catfmt(ffstr3 *s, const char *fmt, ...) {
	size_t r;
	va_list args;
	va_start(args, fmt);
	r = ffstr_catfmtv(s, fmt, args);
	va_end(args);
	return r;
}

static inline size_t ffstr_fmt(ffstr3 *s, const char *fmt, ...)
{
	size_t r;
	va_list args;
	va_start(args, fmt);
	s->len = 0;
	r = ffstr_catfmtv(s, fmt, args);
	va_end(args);
	return r;
}

/** Formatted output to a newly allocated NULL-terminated string. */
static FFINL char* _ffsz_alfmt(const char *fmt, ...)
{
	size_t r;
	ffarr a = {};
	va_list args;
	va_start(args, fmt);
	r = ffstr_catfmtv(&a, fmt, args);
	va_end(args);
	if (r == 0)
		ffarr_free(&a);
	return a.ptr;
}
static inline char* ffsz_alfmtv(const char *fmt, va_list args)
{
	ffarr a = {};
	if ((0 == ffstr_catfmtv(&a, fmt, args) && a.len != 0)
		|| 0 == ffarr_append(&a, "", 1))
		ffarr_free(&a);
	return a.ptr;
}
#define ffsz_alfmt(fmt, ...) _ffsz_alfmt(fmt "%Z", __VA_ARGS__)

/** Formatted output into a file.
'buf': optional buffer. */
FF_EXTN size_t fffile_fmt(fffd fd, ffstr3 *buf, const char *fmt, ...);

/** Read the whole file into memory buffer.
@limit: maximum allowed file size
*/
FF_EXTN int fffile_readall(ffarr *a, const char *fn, uint64 limit);

/** Create (overwrite) file from buffer.
@flags: FFO_* (default: FFO_CREATE | FFO_TRUNC) */
FF_EXTN int fffile_writeall(const char *fn, const char *d, size_t len, uint flags);

/** Compare files by content.
@limit: maximum amount of data to process (0:unlimited). */
FF_EXTN int fffile_cmp(const char *fn1, const char *fn2, uint64 limit);

/** Buffered data output.
@dst is set if an output data block is ready.
Return the number of processed bytes. */
FF_EXTN size_t ffbuf_add(ffstr3 *buf, const char *src, size_t len, ffstr *dst);

FF_EXTN ssize_t ffarr_gather2(ffarr *ar, const char *d, size_t len, size_t until, ffstr *out);


struct ffbuf_gather {
	uint state;
	uint buflen;
	ffstr data; //input data
	size_t ctglen; //length of contiguous data
	uint off; //offset (+1) of the needed data within buffer
};

enum FFBUF_R {
	FFBUF_ERR = -1,
	FFBUF_MORE,
	FFBUF_READY,
	FFBUF_DONE,
};

/** Gather contiguous data from separate memory buffers.
Buffer #1: "...D"
Buffer #2: "ATA..."
Result: "DATA"
Return enum FFBUF_R. */
FF_EXTN int ffbuf_gather(ffarr *buf, struct ffbuf_gather *d);

/**
Return the number of input bytes processed;  <0 on error. */
FF_EXTN int ffbuf_contig(ffarr *buf, const ffstr *in, size_t ctglen, ffstr *s);
FF_EXTN int ffbuf_contig_store(ffarr *buf, const ffstr *in, size_t ctglen);


/** Memory block that can be linked with another block.
BLK0 <-> BLK1 <-> ... */
typedef struct ffmblk {
	ffarr buf;
	ffchain_item sib;
} ffmblk;

/** Allocate and add new block into the chain. */
FF_EXTN ffmblk* ffmblk_chain_push(ffchain *blocks);

/** Get the last block in chain. */
static FFINL ffmblk* ffmblk_chain_last(ffchain *blocks)
{
	if (ffchain_empty(blocks))
		return NULL;
	ffchain_item *blk = ffchain_last(blocks);
	return FF_GETPTR(ffmblk, sib, blk);
}

static FFINL void ffmblk_free(ffmblk *m)
{
	ffarr_free(&m->buf);
	ffmem_free(m);
}


typedef struct ffbstr {
	ushort len;
	char data[0];
} ffbstr;

/** Add one more ffbstr into array.  Reallocate memory, if needed.
If @data is set, copy it into a new ffbstr. */
FF_EXTN ffbstr * ffbstr_push(ffstr *buf, const char *data, size_t len);

/** Copy data into ffbstr. */
static FFINL void ffbstr_copy(ffbstr *bs, const char *data, size_t len)
{
	bs->len = (ushort)len;
	ffmemcpy(bs->data, data, len);
}

/** Get the next string from array.
@off: set value to 0 before the first call.
Return 0 if there is no more data. */
static FFINL ffbstr* ffbstr_next(const char *buf, size_t len, size_t *off, ffstr *dst)
{
	ffbstr *bs = (ffbstr*)(buf + *off);
	if (*off == len)
		return NULL;

	if (dst != NULL)
		ffstr_set(dst, bs->data, bs->len);
	*off += sizeof(ffbstr) + bs->len;
	return bs;
}


typedef struct ffrange {
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

static FFINL ffstr ffrang_get_off(const ffrange *r, const char *base, uint off) {
	FF_ASSERT(off <= r->len);
	ffstr s;
	ffstr_set(&s, base + r->off + off, r->len - off);
	return s;
}

static FFINL void ffrang_clear(ffrange *r) {
	r->off = r->len = 0;
}
