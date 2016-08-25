/** Operations with numbers.
Copyright (c) 2015 Simon Zolin
*/

#pragma once

#include <FFOS/types.h>

#ifdef FF_64
#include <emmintrin.h> //SSE2
#endif


/** Protect against division by zero. */
#define FF_SAFEDIV(val, by) \
	((by) != 0 ? (val) / (by) : 0)


/**
ffint_ltoh*(), ffint_htol*(): Convert host integer <-> little endian.
ffint_ntoh*(), ffint_hton*(): Convert host integer <-> big endian.
*/

static FFINL ushort ffint_ltoh16(const void *p)
{
	return *(ushort*)p;
}

static FFINL uint ffint_ltoh24(const void *p)
{
	const byte *b = p;
	return (((int)b[2]) << 16) | *(ushort*)p;
}

static FFINL uint ffint_ltoh32(const void *p)
{
	return *(uint*)p;
}

static FFINL uint64 ffint_ltoh64(const void *p)
{
	return *(uint64*)p;
}

static FFINL void ffint_htol16(void *dst, ushort i)
{
	*((ushort*)dst) = i;
}

static FFINL void ffint_htol24(void *p, uint n)
{
	const byte *b = (void*)&n;
	byte *o = p;
	o[0] = b[0];
	o[1] = b[1];
	o[2] = b[2];
}

static FFINL void ffint_htol32(void *dst, uint i)
{
	*((uint*)dst) = i;
}

static FFINL void ffint_htol64(void *dst, uint64 i)
{
	*((uint64*)dst) = i;
}


static FFINL void ffint_hton16(void *dst, ushort i)
{
	*((ushort*)dst) = ffhton16(i);
}

static FFINL void ffint_hton24(void *dst, uint i)
{
	byte *b = dst;
	b[0] = (byte)(i >> 16);
	b[1] = (byte)(i >> 8);
	b[2] = (byte)i;
}

static FFINL void ffint_hton32(void *dst, uint i)
{
	*((uint*)dst) = ffhton32(i);
}

static FFINL void ffint_hton64(void *dst, uint64 i)
{
	*((uint64*)dst) = ffhton64(i);
}

static FFINL ushort ffint_ntoh16(const void *p)
{
	return ffhton16(*(ushort*)p);
}

static FFINL uint ffint_ntoh24(const void *p)
{
	const byte *b = p;
	return ((uint)b[0] << 16) | ((uint)b[1] << 8) | (uint)b[2];
}

static FFINL uint ffint_ntoh32(const void *p)
{
	return ffhton32(*(uint*)p);
}

static FFINL uint64 ffint_ntoh64(const void *p)
{
	return ffhton64(*(uint64*)p);
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

static FFINL int ffint_lim24(int64 i)
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


/** Convert FP number to integer. */
static FFINL int ffint_ftoi(double d)
{
	int r;

#ifdef FF_64
	r = _mm_cvtsd_si32(_mm_load_sd(&d));

#elif !defined FF_MSVC
	asm volatile("fistpl %0"
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
