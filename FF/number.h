/** Operations with numbers.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>

#ifdef FF_AMD64
#include <emmintrin.h> //SSE2
#endif


/** Protect against division by zero. */
#define FFINT_DIVSAFE(val, by) \
	((by) != 0 ? (val) / (by) : 0)

/** Increment and reset to 0 on reaching the limit. */
#define ffint_cycleinc(n, lim)  (((n) + 1) % (lim))

static FFINL size_t ffint_increset2(size_t n, size_t cap)
{
	return (n + 1) & (cap - 1);
}

#define ffint_cmp(a, b) \
	(((a) == (b)) ? 0 : ((a) < (b)) ? -1 : 1)


/** Unaligned memory access. */
#define ffint_unaligned16(p)  (*(short*)(p))
#define ffint_unaligned32(p)  (*(int*)(p))
#define ffint_unaligned64(p)  (*(int64*)(p))
#define ffint_set_unaligned16(p, n)  (*(short*)(p) = (n))
#define ffint_set_unaligned32(p, n)  (*(int*)(p) = (n))
#define ffint_set_unaligned64(p, n)  (*(int64*)(p) = (n))


/** Convert little endian integer to host integer. */

static FFINL ushort ffint_ltoh16(const void *p)
{
	return ffhtol16(ffint_unaligned16(p));
}

static FFINL uint ffint_ltoh24(const void *p)
{
	const byte *b = (byte*)p;
	return ((int)b[2] << 16) | ((int)b[1] << 8) | b[0];
}

static FFINL int ffint_ltoh24s(const void *p)
{
	const byte *b = (byte*)p;
	uint n = ((uint)b[2] << 16) | ((uint)b[1] << 8) | b[0];
	if (n & 0x00800000)
		n |= 0xff000000;
	return n;
}

static FFINL uint ffint_ltoh32(const void *p)
{
	return ffhtol32(ffint_unaligned32(p));
}

static FFINL uint64 ffint_ltoh64(const void *p)
{
	return ffhtol64(ffint_unaligned64(p));
}


/** Convert host integer to little endian integer. */

static FFINL void ffint_htol16(void *dst, ushort i)
{
	ffint_set_unaligned16(dst, ffhtol16(i));
}

static FFINL void ffint_htol24(void *p, uint n)
{
	byte *o = (byte*)p;
	o[0] = (byte)n;
	o[1] = (byte)(n >> 8);
	o[2] = (byte)(n >> 16);
}

static FFINL void ffint_htol32(void *dst, uint i)
{
	ffint_set_unaligned32(dst, ffhtol32(i));
}

static FFINL void ffint_htol64(void *dst, uint64 i)
{
	ffint_set_unaligned64(dst, ffhtol64(i));
}


/** Convert host integer to big endian integer. */

static FFINL void ffint_hton16(void *dst, ushort i)
{
	ffint_set_unaligned16(dst, ffhton16(i));
}

static FFINL void ffint_hton24(void *dst, uint i)
{
	byte *b = (byte*)dst;
	b[0] = (byte)(i >> 16);
	b[1] = (byte)(i >> 8);
	b[2] = (byte)i;
}

static FFINL void ffint_hton32(void *dst, uint i)
{
	ffint_set_unaligned32(dst, ffhton32(i));
}

static FFINL void ffint_hton64(void *dst, uint64 i)
{
	ffint_set_unaligned64(dst, ffhton64(i));
}


/** Convert big endian integer to host integer. */

static FFINL ushort ffint_ntoh16(const void *p)
{
	return ffhton16(ffint_unaligned16(p));
}

static FFINL uint ffint_ntoh24(const void *p)
{
	const byte *b = (byte*)p;
	return ((uint)b[0] << 16) | ((uint)b[1] << 8) | (uint)b[2];
}

static FFINL uint ffint_ntoh32(const void *p)
{
	return ffhton32(ffint_unaligned32(p));
}

static FFINL uint64 ffint_ntoh64(const void *p)
{
	return ffhton64(ffint_unaligned64(p));
}


/** ffint_lim*(): limit signed integer within specific bit boundary. */

static FFINL int ffint_lim8(int i)
{
	if (i < -0x80)
		i = -0x80;
	else if (i > 0x7f)
		i = 0x7f;
	return i;
}

static FFINL int ffint_lim16(int i)
{
	if (i < -0x8000)
		i = -0x8000;
	else if (i > 0x7fff)
		i = 0x7fff;
	return i;
}

static FFINL int ffint_lim24(int i)
{
	if (i < -0x800000)
		i = -0x800000;
	else if (i > 0x7fffff)
		i = 0x7fffff;
	return i;
}

static FFINL int ffint_lim32(int64 i)
{
	if (i < -0x80000000LL)
		i = -0x80000000LL;
	else if (i > 0x7fffffffLL)
		i = 0x7fffffffLL;
	return i;
}


/** Set or clear bits. */
#define ffint_bitmask(pn, mask, set) \
do { \
	if (set) \
		*(pn) |= (mask); \
	else \
		*(pn) &= ~(mask); \
} while (0)

#define ffint_mask_test(n, mask)  (((n) & (mask)) == (mask))

/** Check whether a number is within range [from, to). */
#define ffint_within(n, from, to) \
	((from) <= (n) && (n) < (to))

/** Mean value. */
#define ffint_mean(a, b) \
	(((a) + (b)) / 2)

/** Dynamic mean value with weight. */
#define ffint_mean_dyn(mean, weight, add) \
	(((mean) * (weight) + (add)) / ((weight) + 1))

/** Convert FP number to integer. */
static FFINL int ffint_ftoi(double d)
{
	int r;

#if defined FF_AMD64
	r = _mm_cvtsd_si32(_mm_load_sd(&d));

#elif defined FF_X86 && !defined FF_MSVC
	__asm__ volatile("fistpl %0"
		: "=m"(r)
		: "t"(d)
		: "st");

#else
	r = (int)((d < 0) ? d - 0.5 : d + 0.5);
#endif

	return r;
}


/** Search number within array. */
FF_EXTN ssize_t ffint_find1(const byte *arr, size_t n, int search);
FF_EXTN ssize_t ffint_find2(const ushort *arr, size_t n, uint search);
FF_EXTN ssize_t ffint_find4(const uint *arr, size_t n, uint search);

/** Binary search integer. */
FF_EXTN ssize_t ffint_binfind1(const byte *arr, size_t n, uint search);
FF_EXTN ssize_t ffint_binfind4(const uint *arr, size_t n, uint search);


FF_EXTN void ffint_sort(uint *arr, size_t n, uint flags);
