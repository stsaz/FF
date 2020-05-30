/** Array, buffer range.
Copyright (c) 2013 Simon Zolin
*/

#pragma once

#include <FFOS/mem.h>
#include <FF/string.h>
#include <FF/chain.h>
#include <FFOS/file.h>
#include <ffbase/vector.h>

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


typedef ffslice ffarr2;

#define ffarr2_allocT  ffslice_allocT
#define ffarr2_callocT  ffslice_zallocT
#define ffarr2_free  ffslice_free
#define FFARR_WALKT  FFSLICE_WALK_T

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

#define FFARR_WALK(ar, it) \
	for (it = (ar)->ptr;  it != (ar)->ptr + (ar)->len;  it++)

/** Reverse walk. */
#define FFARR_RWALKT(ar, it, T) \
	for (it = ffarr_lastT(ar, T);  it - (T*)(ar)->ptr >= 0;  it--)

#define FFARR_RWALK(ar, it) \
	if ((ar)->len != 0) \
		for (it = (ar)->ptr + (ar)->len - 1;  it >= (ar)->ptr;  it--)

#define _ffarr_realloc(ar, newlen, elsz) \
	ffvec_realloc((ffvec*)(ar), newlen, elsz)

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

/** Deallocate array memory. */
#define ffarr_free(ar)  ffvec_free((ffvec*)ar)

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

#define _ffarr_push(ar, elsz)  ffvec_push((ffvec*)(ar), elsz)

#define ffarr_push(ar, T) \
	(T*)_ffarr_push((ffarr*)ar, sizeof(T))

#define ffarr_pushT(ar, T) \
	(T*)_ffarr_push(ar, sizeof(T))

/** Add 1 element.  Grow by the specified size if needed.
Return element pointer;  NULL on error. */
FF_EXTN void* ffarr_pushgrow(ffarr *ar, size_t lowat, size_t elsz);

#define ffarr_pushgrowT(ar, lowat, T) \
	(T*)ffarr_pushgrow(ar, lowat, sizeof(T))

/** Add items into array.  Reallocate memory, if needed.
Return the tail.
Return NULL on error. */
static inline void * _ffarr_append(ffarr *ar, const void *src, size_t num, size_t elsz)
{
	if (num != ffvec_add((ffvec*)ar, src, num, elsz))
		return NULL;
	return ar->ptr + ar->len * elsz;
}

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
	ffslice_rmswap((ffslice*)ar, ((char*)el - (char*)ar->ptr) / elsz, 1, elsz);
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

#define FFSTR2(s)  (s).ptr, (s).len

static FFINL void ffstr_acq(ffstr *dst, ffstr *src) {
	*dst = *src;
	ffstr_null(src);
}

static FFINL void ffstr_acqstr3(ffstr *dst, ffstr3 *src) {
	dst->ptr = src->ptr;
	dst->len = src->len;
	ffarr_null(src);
}

static FFINL size_t ffstr_cat(ffstr *s, size_t cap, const char *d, size_t len) {
	return ffstr_add(s, cap, d, len);
}

#define ffstr_copy(dst, d, len)  ffstr_dup(dst, d, len)

#define ffstr_alcopyz(dst, sz)  ffstr_copy(dst, sz, ffsz_len(sz))
#define ffstr_alcopystr(dst, src)  ffstr_copy(dst, (src)->ptr, (src)->len)

/** Trim data by absolute bounds.
Return the number of bytes processed from the beginning. */
FF_EXTN size_t ffstr_crop_abs(ffstr *data, uint64 data_start, uint64 off, uint64 size);


static FFINL void ffstr3_cat(ffstr3 *s, const char *d, size_t len) {
	ffstr_cat((ffstr*)s, s->cap, d, len);
}

static inline size_t ffstr_catfmtv(ffstr3 *s, const char *fmt, va_list va)
{
	va_list args;
	va_copy(args, va);
	ffsize r = ffstr_growfmtv((ffstr*)s, &s->cap, fmt, args);
	va_end(args);
	return r;
}

static inline size_t ffstr_catfmt(ffstr3 *s, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	ffsize r = ffstr_growfmtv((ffstr*)s, &s->cap, fmt, args);
	va_end(args);
	return r;
}

static inline size_t ffstr_fmt(ffstr3 *s, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	s->len = 0;
	ffsize r = ffstr_growfmtv((ffstr*)s, &s->cap, fmt, args);
	va_end(args);
	return r;
}

#define ffsz_alfmtv  ffsz_allocfmtv
#define ffsz_alfmt  ffsz_allocfmt

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
Doesn't allocate memory.
@dst is set if an output data block is ready.
Return the number of processed bytes. */
FF_EXTN size_t ffbuf_add(ffstr3 *buf, const char *src, size_t len, ffstr *dst);

static inline ffstr ffbuf_addstr(ffarr *buf, ffstr *in)
{
	ffstr out;
	size_t n = ffbuf_add(buf, in->ptr, in->len, &out);
	ffstr_shift(in, n);
	return out;
}

static inline ssize_t ffarr_gather2(ffarr *ar, const char *d, size_t len, size_t until, ffstr *out)
{
	return ffstr_gather((ffstr*)ar, &ar->cap, d, len, until, out);
}


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
